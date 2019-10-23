//! Note: Please link with Ws2_32.lib on Windows

//! Includes

#include "orx.h"


//! Prototypes

orxSTATUS orxFASTCALL orxRemote_Init();


#ifndef orxREMOTE_HEADER_ONLY

//! External includes

#ifdef __orxWINDOWS__

#include <winsock2.h>

#define close                     closesocket

#else // __orxWINDOWS__

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#endif // __orxWINDOWS__

//! Defines

#define orxREMOTE_DEFAULT_HOST    "localhost"
#define orxREMOVE_DEFAULT_PORT    8080
#ifdef __orxX86_64__
#define orxREMOTE_CAST_HELPER     (orxU64)
#else // __orxX86_64__
#define orxREMOTE_CAST_HELPER
#endif // __orxX86_64__


//! Variables / Structures

#ifdef __orxWINDOWS__

#define Socket SOCKET

#else // __orxWINDOWS__

#define Socket int

#endif // __orxWINDOWS__

typedef struct __WebArchive_t
{
  orxS64  s64Size, s64Cursor;
  orxU8  *zLocation;
} WebArchive;

typedef enum __orxREMOTE_QUERY_TYPE_t
{
  orxREMOTE_QUERY_TYPE_CONTENT,
  orxREMOTE_QUERY_TYPE_TIME,

  orxREMOTE_QUERY_TYPE_NONE = orxENUM_NONE

} orxREMOTE_QUERY_TYPE;

static orxHASHTABLE        *spstTable;
static orxTHREAD_SEMAPHORE *spstSemaphore;


//! Code

static orxU32 orxRemote_GetHash(const orxSTRING _zHost, orxU32 _u32Port)
{
  orxU32 u32Result;
  orxCHAR p[32] = { 0 };

  orxString_NPrint(p, 31, "%d", _u32Port);

  // Updates result
  u32Result = orxString_ToCRC(_zHost);
  u32Result = orxString_NContinueCRC(p, u32Result, sizeof(orxU32));

  // Done!
  return u32Result;
}

static Socket orxRemote_Connect(const orxSTRING _zHost, orxU32 _u32Port)
{
  orxU32  u32Key;
  Socket  stResult = 0;

  // Gets key
  u32Key = orxRemote_GetHash(_zHost, _u32Port);

  // Not found?
  if((stResult = (Socket) orxREMOTE_CAST_HELPER orxHashTable_Get(spstTable, u32Key)) == 0)
  {
    struct hostent *pstHost;

    // Gets host
    pstHost = gethostbyname(_zHost);

    // Valid?
    if(pstHost != NULL)
    {
      struct sockaddr_in stServer;

      // Clears server
      orxMemory_Zero(&stServer, sizeof(struct sockaddr_in));

      // Inits it
      orxMemory_Move(&stServer.sin_addr, pstHost->h_addr, pstHost->h_length);
      stServer.sin_family = pstHost->h_addrtype;
      stServer.sin_port   = htons((unsigned short)_u32Port);

      // Creates socket
      stResult = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

      // Valid?
      if(stResult >= 0)
      {
        // Updates it
        setsockopt(stResult, SOL_SOCKET, SO_KEEPALIVE, 0, 0);

        // Connects
        if(connect(stResult, (struct sockaddr *)&stServer, sizeof(struct sockaddr_in)) == 0)
        {
          // Valid?
          if(stResult != 0)
          {
            // Stores socket
            orxHashTable_Set(spstTable, u32Key, (void *) orxREMOTE_CAST_HELPER stResult);
          }
        }
        else
        {
          // Clears result
          stResult = 0;
        }
      }
    }
  }

  // Done!
  return stResult;
}

static orxSTATUS orxFASTCALL orxRemote_ParseURL(const orxSTRING _zURL, orxCHAR *_acHost, orxU32 _u32HostLength, orxU32 *_pu32Port, const orxSTRING *_pzResource)
{
  const orxCHAR  *pcSeparator;
  orxSTATUS       eResult = orxSTATUS_SUCCESS;

  // Has port?
  if((pcSeparator = orxString_SearchChar(_zURL + 7, ':')) != orxNULL)
  {
    // Gets host
    orxString_NPrint(_acHost, _u32HostLength - 1, "%.*s", pcSeparator - _zURL - 7, _zURL + 7);

    // Gets port
    orxString_ToU32(pcSeparator + 1, _pu32Port, orxNULL);

    if(_pzResource != orxNULL)
    {
      pcSeparator = orxString_SearchChar(pcSeparator + 1, '/');
      *_pzResource = (pcSeparator != orxNULL) ? pcSeparator + 1 : orxSTRING_EMPTY;
    }
  }
  else
  {
    // Has separator?
    if((pcSeparator = orxString_SearchChar(_zURL + 7, '/')) != orxNULL)
    {
      // Gets host
      orxString_NPrint(_acHost, _u32HostLength - 1, "%.*s", pcSeparator - _zURL - 7, _zURL + 7);
    }
    else
    {
      // Gets host
      orxString_NPrint(_acHost, _u32HostLength - 1, "%s", _zURL + 7);
    }

    if(_pzResource != orxNULL)
    {
      *_pzResource = (pcSeparator != orxNULL) ? pcSeparator + 1 : orxSTRING_EMPTY;
    }

    // Gets default port
    *_pu32Port = 80;
  }

  // Done!
  return eResult;
}

static orxS64 orxFASTCALL orxRemote_ExecuteQuery(const orxSTRING _zHost, orxU32 _u32Port, const orxSTRING _zResource, orxREMOTE_QUERY_TYPE _eType, orxU8 **_ppu8Buffer)
{
  orxCHAR acBuffer[4096] = {0};
  orxS32  s32Length;
  Socket  stSocket;
  orxBOOL bClose = orxFALSE;
  orxS64  s64Result = 0;

  // Waits for semaphore
  orxThread_WaitSemaphore(spstSemaphore);

  // Connects to host
  stSocket = orxRemote_Connect(_zHost, _u32Port);

  // Success?
  if(stSocket > 0)
  {
    // Prints query header
    orxString_NPrint(acBuffer, sizeof(acBuffer) - 1, "%s /%s HTTP/1.1\r\nHost: %s:%u\r\nConnection: keep-alive\r\nUser-Agent: orxRemoteClient/1.0 (+http://orx-project.org)\r\n\r\n", (_eType == orxREMOTE_QUERY_TYPE_TIME || _ppu8Buffer == orxNULL) ? "HEAD" : "GET", _zResource, _zHost, _u32Port);

    // Gets its length
    s32Length = (orxS32)orxString_GetLength(acBuffer);

    // Sends it
    if(send(stSocket, acBuffer, s32Length, 0) == s32Length)
    {
      // Gets answer's header
      if((s32Length = recv(stSocket, acBuffer, sizeof(acBuffer) - 1, 0)) > 0)
      {
        orxU32 u32ReturnCode;

        // Success?
        if((orxString_Scan(acBuffer, "HTTP/1.%*d %03u", (unsigned int *)&u32ReturnCode) == 1) && (u32ReturnCode == 200))
        {
          const orxCHAR  *pc, *pcContentStart = orxNULL;
          orxS32          s32ContentStartLength = 0;

          // Looks for end of header
          if((pc = orxString_SearchString(acBuffer, "\r\n\r\n")) != orxNULL)
          {
            orxS32 s32HeaderLength;

            // Gets real header length
            s32HeaderLength = pc + 4 - acBuffer;

            // Has content started?
            if(s32Length != s32HeaderLength)
            {
              // Stores content start
              pcContentStart = pc + 4;

              // Stores content start length
              s32ContentStartLength = s32Length - s32HeaderLength;

              // Adjusts header length
              s32Length = s32HeaderLength;
            }
          }

          // Depending on query type
          switch(_eType)
          {
            case orxREMOTE_QUERY_TYPE_CONTENT:
            {
              // Looks for content length
              if(((pc = orxString_SearchString(acBuffer, "Content-Length:")) != orxNULL)
              || ((pc = orxString_SearchString(acBuffer, "content-length:")) != orxNULL))
              {
                // Updates result
                orxString_ToS64(pc + 15, &s64Result, orxNULL);

                // Valid?
                if(s64Result > 0)
                {
                  // Asked for actual content?
                  if(_ppu8Buffer != orxNULL)
                  {
                    orxS32 s32Offset = 0;

                    // Allocates buffer
                    *_ppu8Buffer = (orxU8 *)orxMemory_Allocate((orxU32)s64Result, orxMEMORY_TYPE_MAIN);

                    // Checks
                    orxASSERT(*_ppu8Buffer != orxNULL);

                    // Did content start?
                    if(s32ContentStartLength != 0)
                    {
                      // Copies it
                      orxMemory_Copy(*_ppu8Buffer, pcContentStart, s32ContentStartLength);
                      s32Offset = (orxU32)s32ContentStartLength;
                    }

                    while((int)s64Result - s32Offset > 0)
                    {
                      // Retrieves some content
                      s32Length = recv(stSocket, (char *)*_ppu8Buffer + s32Offset, (int)s64Result - s32Offset, 0);

                      // Success?
                      if(s32Length > 0)
                      {
                        // Updates offset
                        s32Offset += s32Length;
                      }
                      else
                      {
                        // Updates result
                        s64Result = 0;

                        // Asks for connection close
                        bClose = orxTRUE;
                        break;
                      }
                    }
                  }
                }
              }

              break;
            }

            case orxREMOTE_QUERY_TYPE_TIME:
            {
              // Looks for last modified
              if(((pc = orxString_SearchString(acBuffer, "Last-Modified:")) != orxNULL)
              || ((pc = orxString_SearchString(acBuffer, "last-modified:")) != orxNULL))
              {
                // Gets its CRC
                s64Result = (orxS64)orxString_NToCRC(pc, orxString_SearchChar(pc, orxCHAR_LF) - pc);
              }

              break;
            }
          }
        }
      }
      else
      {
        // Asks for connection close
        bClose = orxTRUE;
      }
    }
    else
    {
      // Asks for connection close
      bClose = orxTRUE;
    }

    // Should close connection?
    if(bClose != orxFALSE)
    {
      // Closes socket
      close(stSocket);

      // Removes it from table
      orxHashTable_Remove(spstTable, orxRemote_GetHash(_zHost, _u32Port));
    }
  }

  // Signals semaphore
  orxThread_SignalSemaphore(spstSemaphore);

  // Done!
  return s64Result;
}

static orxS64 orxFASTCALL orxRemote_ExecuteRange(const orxSTRING _zHost, orxU32 _u32Port, const orxSTRING _zResource, orxS64 start, orxS64 count, orxU8* _pu8Buffer)
{
	orxCHAR acBuffer[4096] = { 0 };
	orxS32  s32Length;
	Socket  stSocket;
	orxBOOL bClose = orxFALSE;
	orxS64  s64Result = 0;

	// Waits for semaphore
	orxThread_WaitSemaphore(spstSemaphore);

	// Connects to host
	stSocket = orxRemote_Connect(_zHost, _u32Port);

	// Success?
	if (stSocket > 0)
	{
		// Prints query header
		orxString_NPrint(acBuffer, sizeof(acBuffer) - 1, "GET /%s HTTP/1.1\r\nHost: %s:%u\r\nConnection: keep-alive\r\nUser-Agent: orxRemoteClient/1.0 (+http://orx-project.org)\r\nrange: bytes=%d-%d\r\n\r\n", _zResource, _zHost, _u32Port, start, count);

		// Gets its length
		s32Length = (orxS32)orxString_GetLength(acBuffer);

		// Sends it
		if (send(stSocket, acBuffer, s32Length, 0) == s32Length)
		{
			// Gets answer's header
			if ((s32Length = recv(stSocket, acBuffer, sizeof(acBuffer) - 1, 0)) > 0)
			{
				orxU32 u32ReturnCode;

				// Success?
				if ((orxString_Scan(acBuffer, "HTTP/1.%*d %03u", (unsigned int*)&u32ReturnCode) == 1) && (u32ReturnCode == 206))
				{
					const orxCHAR* pc, * pcContentStart = orxNULL;
					orxS32          s32ContentStartLength = 0;

					// Looks for end of header
					if ((pc = orxString_SearchString(acBuffer, "\r\n\r\n")) != orxNULL)
					{
						orxS32 s32HeaderLength;

						// Gets real header length
						s32HeaderLength = pc + 4 - acBuffer;

						// Has content started?
						if (s32Length != s32HeaderLength)
						{
							// Stores content start
							pcContentStart = pc + 4;

							// Stores content start length
							s32ContentStartLength = s32Length - s32HeaderLength;

							// Adjusts header length
							s32Length = s32HeaderLength;
						}
					}

					// Looks for content length
					if (((pc = orxString_SearchString(acBuffer, "Content-Length:")) != orxNULL)
						|| ((pc = orxString_SearchString(acBuffer, "content-length:")) != orxNULL))
					{
						// Updates result
						orxString_ToS64(pc + 15, &s64Result, orxNULL);

						// Valid?
						if (s64Result > 0)
						{
							// Asked for actual content?
							if (_pu8Buffer != orxNULL)
							{
								orxS32 s32Offset = 0;

								// Did content start?
								if (s32ContentStartLength != 0)
								{
									// Copies it
									orxMemory_Copy(_pu8Buffer, pcContentStart, s32ContentStartLength);
									s32Offset = (orxU32)s32ContentStartLength;
								}

								while ((int)s64Result - s32Offset > 0)
								{
									// Retrieves some content
									s32Length = recv(stSocket, (char*)_pu8Buffer + s32Offset, (int)s64Result - s32Offset, 0);

									// Success?
									if (s32Length > 0)
									{
										// Updates offset
										s32Offset += s32Length;
									}
									else
									{
										// Updates result
										s64Result = 0;

										// Asks for connection close
										bClose = orxTRUE;
										break;
									}
								}
							}
						}
					}
				}
			}
			else
			{
				// Asks for connection close
				bClose = orxTRUE;
			}
		}
		else
		{
			// Asks for connection close
			bClose = orxTRUE;
		}

		// Should close connection?
		if (bClose != orxFALSE)
		{
			// Closes socket
			close(stSocket);

			// Removes it from table
			orxHashTable_Remove(spstTable, orxRemote_GetHash(_zHost, _u32Port));
		}
	}

	// Signals semaphore
	orxThread_SignalSemaphore(spstSemaphore);

	// Done!
	return s64Result;
}

// Locate function, returns NULL if it can't handle the storage or if the resource can't be found in this storage
static const orxSTRING orxFASTCALL orxRemote_WebLocate(const orxSTRING _zStorage, const orxSTRING _zResource, orxBOOL _bRequireExistence)
{
  const orxSTRING zResult = orxNULL;

  // Isn't a relative file storage?
  if(*_zStorage != '.')
  {
    orxU32          u32Port, u32Offset = 0;
    const orxSTRING zResource;
    orxBOOL         bPrintSeparator;
    orxU32          u32Length;
    orxCHAR         acHost[256] = {0};
    static orxCHAR  sacBuffer[1024] = {0};

    // No explicit host in storage?
    if(orxString_SearchString(_zStorage, "http://") != _zStorage)
    {
      // Prints default host
      u32Offset = orxString_NPrint(sacBuffer, sizeof(sacBuffer) - 1 - u32Offset, "http://%s:%u/", orxREMOTE_DEFAULT_HOST, orxREMOVE_DEFAULT_PORT);
    }

    // Gets storage length
    u32Length = orxString_GetLength(_zStorage);

    // Should print separator?
    bPrintSeparator = ((_zStorage[u32Length - 1] != '/') && (_zStorage[u32Length - 1] != '?') && (_zStorage[u32Length - 1] != '=')) ? orxTRUE : orxFALSE;

    // Prints storage + resource
    u32Offset += orxString_NPrint(sacBuffer + u32Offset, sizeof(sacBuffer) - 1 - u32Offset, "%s%s%s", _zStorage, (bPrintSeparator != orxFALSE) ? "/" : orxSTRING_EMPTY, _zResource);

    // Parses storage to extract host & port
    orxRemote_ParseURL(sacBuffer, acHost, sizeof(acHost), &u32Port, &zResource);

    // Exists or doesn't require existence?
    if((_bRequireExistence == orxFALSE)
    || (orxRemote_ExecuteQuery(acHost, u32Port, zResource, orxREMOTE_QUERY_TYPE_TIME, orxNULL) != 0))
    {
      // Updates result
      zResult = sacBuffer;
    }
  }

  // Done!
  return zResult;
}

// Get time function: returns last modified time
static orxS64 orxFASTCALL orxRemote_GetTime(const orxSTRING _zLocation)
{
  orxU32          u32Port;
  const orxSTRING zResource;
  orxCHAR         acHost[256] = {0};
  orxS64          s64Result = 0;

  // Parses storage to extract host & port
  orxRemote_ParseURL(_zLocation, acHost, sizeof(acHost), &u32Port, &zResource);

  // Updates result
  s64Result = orxRemote_ExecuteQuery(acHost, u32Port, zResource, orxREMOTE_QUERY_TYPE_TIME, orxNULL);

  // Done!
  return s64Result;
}

// Open function: returns an opaque handle for subsequent function calls (GetSize, Seek, Tell, Read and Close) upon success, orxHANDLE_UNDEFINED otherwise
static orxHANDLE orxFASTCALL orxRemote_WebOpen(const orxSTRING _zLocation, orxBOOL _bEraseMode)
{
  orxHANDLE hResult = orxHANDLE_UNDEFINED;

  // Not in erase mode?
  if(_bEraseMode == orxFALSE)
  {
    orxS64          s64Size;
    orxU32          u32Port;
    const orxSTRING zResource;
    orxU8          *pu8Buffer;
    orxCHAR         acHost[256] = {0};

    // Parses storage to extract host & port
    orxRemote_ParseURL(_zLocation, acHost, sizeof(acHost), &u32Port, &zResource);

    // Retrieves resource size
    s64Size = orxRemote_ExecuteQuery(acHost, u32Port, zResource, orxREMOTE_QUERY_TYPE_CONTENT, orxNULL);

    // Valid?
    if(s64Size > 0)
    {
      WebArchive *pstWebArchive;

      // Allocates memory for our archive wrapper
      pstWebArchive = (WebArchive *)orxMemory_Allocate(sizeof(WebArchive), orxMEMORY_TYPE_MAIN);

      // Success?
      if(pstWebArchive != orxNULL)
      {
        // Stores its size
        pstWebArchive->s64Size = s64Size;

        // Stores location for use in WebRead
        orxU32 nb = orxString_GetLength(_zLocation) + 1;
        pstWebArchive->zLocation = (orxSTRING)orxMemory_Allocate(nb, orxMEMORY_TYPE_MAIN);
        orxMemory_Copy(pstWebArchive->zLocation, _zLocation, nb);

        // Inits read cursor
        pstWebArchive->s64Cursor = 0;

        // Updates result
        hResult = (orxHANDLE)pstWebArchive;
      }
    }
  }

  // Done!
  return hResult;
}

// Close function: releases all that has been allocated in Open
static void orxFASTCALL orxRemote_WebClose(orxHANDLE _hResource)
{
  WebArchive *pstWebArchive;

  // Gets archive wrapper
  pstWebArchive = (WebArchive *)_hResource;

  // Frees its internal buffer
  if (pstWebArchive->zLocation != orxNULL)
    orxMemory_Free(pstWebArchive->zLocation);

  // Frees it
  orxMemory_Free(pstWebArchive);
}

// GetSize function: simply returns the size of the extracted resource, in bytes
static orxS64 orxFASTCALL orxRemote_WebGetSize(orxHANDLE _hResource)
{
  orxS64 s64Result;

  // Updates result
  s64Result = ((WebArchive *)_hResource)->s64Size;

  // Done!
  return s64Result;
}

// Seek function: position the read cursor inside the data and returns the offset from start upon success or -1 upon failure
static orxS64 orxFASTCALL orxRemote_WebSeek(orxHANDLE _hResource, orxS64 _s64Offset, orxSEEK_OFFSET_WHENCE _eWhence)
{
  WebArchive *pstWebArchive;
  orxS64      s64Cursor;

  // Gets archive wrapper
  pstWebArchive = (WebArchive *)_hResource;

  // Depending on seek mode
  switch(_eWhence)
  {
    case orxSEEK_OFFSET_WHENCE_START:
    {
      // Computes cursor
      s64Cursor = _s64Offset;
      break;
    }

    case orxSEEK_OFFSET_WHENCE_CURRENT:
    {
      // Computes cursor
      s64Cursor = pstWebArchive->s64Cursor + _s64Offset;
      break;
    }

    case orxSEEK_OFFSET_WHENCE_END:
    {
      // Computes cursor
      s64Cursor = pstWebArchive->s64Size - _s64Offset;
      break;
    }

    default:
    {
      // Failure
      s64Cursor = -1;
      break;
    }
  }

  // Is cursor valid?
  if((s64Cursor >= 0) && (s64Cursor <= pstWebArchive->s64Size))
  {
    // Updates archive's cursor
    pstWebArchive->s64Cursor = s64Cursor;
  }

  // Done!
  return s64Cursor;
}

// Tell function: returns current read cursor
static orxS64 orxFASTCALL orxRemote_WebTell(orxHANDLE _hResource)
{
  orxS64 s64Result;

  // Updates result
  s64Result = ((WebArchive *)_hResource)->s64Cursor;

  // Done!
  return s64Result;
}

// Read function: copies the requested amount of data, in bytes, to the given buffer and returns the amount of bytes copied
static orxS64 orxFASTCALL orxRemote_WebRead(orxHANDLE _hResource, orxS64 _s64Size, void *_pu8Buffer)
{
  WebArchive *pstWebArchive;
  orxS64      s64CopySize, s64Result = 0;
  orxU32          u32Port;
  const orxSTRING zResource;
  orxCHAR         acHost[256] = { 0 };

  // Gets archive wrapper
  pstWebArchive = (WebArchive *)_hResource;

  // Gets actual copy size to prevent any out-of-bound access
  s64CopySize = orxMIN(_s64Size, pstWebArchive->s64Size - pstWebArchive->s64Cursor);

  if (s64CopySize > 0)
  {
    // Parses storage to extract host & port
    orxRemote_ParseURL(pstWebArchive->zLocation, acHost, sizeof(acHost), &u32Port, &zResource);

    // Read the range directly into caller's buffer
    orxRemote_ExecuteRange(acHost, u32Port, zResource, pstWebArchive->s64Cursor, s64CopySize, _pu8Buffer);
  }

  // Updates cursor
  pstWebArchive->s64Cursor += s64CopySize;

  // Done!
  return s64CopySize;
}

orxSTATUS orxFASTCALL orxRemote_Init()
{
  orxRESOURCE_TYPE_INFO stInfo;
  orxSTATUS             eResult = orxSTATUS_FAILURE;

#ifdef __orxWINDOWS__
  WSADATA stData;

  // Starts network support
  if(WSAStartup(MAKEWORD(2, 2), &stData) == 0)
  {
#endif // __orxWINDOWS__

  // Creates semaphore
  spstSemaphore = orxThread_CreateSemaphore(1);

  // Creates table
  spstTable = orxHashTable_Create(256, orxHASHTABLE_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

  orxASSERT(spstTable != orxNULL);

  // Inits our web resource wrapper
  orxMemory_Zero(&stInfo, sizeof(orxRESOURCE_TYPE_INFO));
  stInfo.zTag       = "web";
  stInfo.pfnLocate  = &orxRemote_WebLocate;
  stInfo.pfnGetTime = &orxRemote_GetTime;
  stInfo.pfnOpen    = &orxRemote_WebOpen;
  stInfo.pfnClose   = &orxRemote_WebClose;
  stInfo.pfnGetSize = &orxRemote_WebGetSize;
  stInfo.pfnSeek    = &orxRemote_WebSeek;
  stInfo.pfnTell    = &orxRemote_WebTell;
  stInfo.pfnRead    = &orxRemote_WebRead;
  stInfo.pfnWrite   = orxNULL; // No write support

  // Registers it
  eResult = orxResource_RegisterType(&stInfo);

#ifdef __orxWINDOWS__
  }
#endif // __orxWINDOWS__

  // Done!
  return eResult;
}

#ifdef __orxWINDOWS__
#undef close
#endif // __orxWINDOWS__

#undef Socket

#endif // orxREMOTE_HEADER_ONLY
