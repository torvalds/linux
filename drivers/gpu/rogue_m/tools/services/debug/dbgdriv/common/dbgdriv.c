/*************************************************************************/ /*!
@File
@Title          Debug Driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    32 Bit kernel mode debug driver
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if defined(_WIN32)
#pragma  warning(disable:4201)
#pragma  warning(disable:4214)
#pragma  warning(disable:4115)
#pragma  warning(disable:4514)


#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#endif /* _WIN32 */

#ifdef LINUX
#include <linux/string.h>
#endif

#if defined (__QNXNTO__)
#include <string.h>
#endif

#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include "dbgdrvif_srv5.h"
#include "dbgdriv.h"
#include "hostfunc.h"

#ifdef _WIN32
#pragma  warning(default:4214)
#pragma  warning(default:4115)
#endif /* _WIN32 */


/******************************************************************************
 Types
******************************************************************************/

#define DBG_STREAM_NAME_MAX		30

/*
	Per-buffer control structure.
*/
typedef struct _DBG_STREAM_
{
	struct _DBG_STREAM_* psNext;
	struct _DBG_STREAM_* psInitStream;
	struct _DBG_STREAM_* psDeinitStream;
	IMG_UINT32 ui32Flags;			/*!< flags (see DEBUG_FLAGS) */
	IMG_PVOID  pvBase;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32RPtr;
	IMG_UINT32 ui32WPtr;

	IMG_UINT32 ui32Marker;			/*!< Size marker for file splitting */

	IMG_UINT32 ui32InitPhaseWOff;	/*!< snapshot offset for init phase end for follow-on pdump */

	IMG_CHAR   szName[DBG_STREAM_NAME_MAX];			/* Give this a size, some compilers don't like [] */
} DBG_STREAM;

/* Check 4xDBG_STREAM will fit in one page */
BLD_ASSERT(sizeof(DBG_STREAM)<<2<HOST_PAGESIZE,dbgdriv_c)

/******************************************************************************
 Global variables
******************************************************************************/

static PDBG_STREAM          g_psStreamList = 0;

/* Mutex used to prevent UM threads (via the dbgdrv ioctl interface) and KM
 * threads (from pvrsrvkm via the ExtDBG API) entering the debug driver core
 * and changing the state of share data at the same time.
 */
IMG_VOID *                  g_pvAPIMutex=IMG_NULL;

static IMG_UINT32			g_PDumpCurrentFrameNo = 0;

DBGKM_SERVICE_TABLE g_sDBGKMServices =
{
	sizeof (DBGKM_SERVICE_TABLE),
	ExtDBGDrivCreateStream,
	ExtDBGDrivDestroyStream,
	ExtDBGDrivWrite2,
	ExtDBGDrivSetMarker,
	ExtDBGDrivWaitForEvent,
	ExtDBGDrivGetCtrlState,
	ExtDBGDrivSetFrame
};


/***************************************************************************
 Forward declarations
***************************************************************************/

IMG_BOOL   IMG_CALLCONV DBGDrivCreateStream(IMG_CHAR *pszName, IMG_UINT32 ui32Flags, IMG_UINT32 ui32Pages, IMG_HANDLE* phInit, IMG_HANDLE* phMain, IMG_HANDLE* phDeinit);
IMG_VOID   IMG_CALLCONV DBGDrivDestroyStream(IMG_HANDLE hInit,IMG_HANDLE hMain, IMG_HANDLE hDeinit);
IMG_VOID * IMG_CALLCONV DBGDrivFindStream(IMG_CHAR * pszName, IMG_BOOL bResetStream);
IMG_UINT32 IMG_CALLCONV DBGDrivRead(PDBG_STREAM psStream, IMG_UINT32 ui32BufID, IMG_UINT32 ui32OutBufferSize,IMG_UINT8 *pui8OutBuf);
IMG_VOID   IMG_CALLCONV DBGDrivSetCaptureMode(PDBG_STREAM psStream,IMG_UINT32 ui32Mode,IMG_UINT32 ui32Start,IMG_UINT32 ui32Stop,IMG_UINT32 ui32SampleRate);
IMG_UINT32 IMG_CALLCONV DBGDrivWrite2(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize);
IMG_VOID   IMG_CALLCONV DBGDrivSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker);
IMG_UINT32 IMG_CALLCONV DBGDrivGetMarker(PDBG_STREAM psStream);
IMG_VOID   IMG_CALLCONV DBGDrivWaitForEvent(DBG_EVENT eEvent);
IMG_UINT32 IMG_CALLCONV DBGDrivGetCtrlState(PDBG_STREAM psStream, IMG_UINT32 ui32StateID);
IMG_UINT32 IMG_CALLCONV DBGDrivGetFrame(void);
IMG_VOID   IMG_CALLCONV DBGDrivSetFrame(IMG_UINT32 ui32Frame);
IMG_VOID   DestroyAllStreams(IMG_VOID);

/* Static function declarations */
static IMG_UINT32 SpaceInStream(PDBG_STREAM psStream);
static IMG_BOOL ExpandStreamBuffer(PDBG_STREAM psStream, IMG_UINT32 ui32NewSize);
static IMG_VOID InvalidateAllStreams(IMG_VOID);


/*****************************************************************************
 Code
*****************************************************************************/

/*!
 @name	ExtDBGDrivCreateStream
 */
IMG_BOOL IMG_CALLCONV ExtDBGDrivCreateStream(IMG_CHAR *pszName, IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size, IMG_HANDLE* phInit, IMG_HANDLE* phMain, IMG_HANDLE* phDeinit)
{
	IMG_BOOL pvRet;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	pvRet=DBGDrivCreateStream(pszName, ui32Flags, ui32Size, phInit, phMain, phDeinit);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return pvRet;
}

/*!
 @name	ExtDBGDrivDestroyStream
 */
IMG_VOID IMG_CALLCONV ExtDBGDrivDestroyStream(IMG_HANDLE hInit,IMG_HANDLE hMain, IMG_HANDLE hDeinit)
{
	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	DBGDrivDestroyStream(hInit, hMain, hDeinit);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return;
}

/*!
 @name	ExtDBGDrivFindStream
 */
IMG_VOID * IMG_CALLCONV ExtDBGDrivFindStream(IMG_CHAR * pszName, IMG_BOOL bResetStream)
{
	IMG_VOID *	pvRet;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	pvRet=DBGDrivFindStream(pszName, bResetStream);
	if (pvRet == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "ExtDBGDrivFindStream: Stream not found"));
	}


	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return pvRet;
}

/*!
 @name	ExtDBGDrivRead
 */
IMG_UINT32 IMG_CALLCONV ExtDBGDrivRead(PDBG_STREAM psStream, IMG_UINT32 ui32BufID, IMG_UINT32 ui32OutBuffSize,IMG_UINT8 * pui8OutBuf)
{
	IMG_UINT32 ui32Ret;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	ui32Ret=DBGDrivRead(psStream, ui32BufID, ui32OutBuffSize, pui8OutBuf);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return ui32Ret;
}

/*!
 @name	ExtDBGDrivWrite2
 */
IMG_UINT32 IMG_CALLCONV ExtDBGDrivWrite2(PDBG_STREAM psStream,IMG_UINT8 * pui8InBuf,IMG_UINT32 ui32InBuffSize)
{
	IMG_UINT32	ui32Ret;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	ui32Ret=DBGDrivWrite2(psStream, pui8InBuf, ui32InBuffSize);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return ui32Ret;
}

/*!
 @name	ExtDBGDrivSetMarker
 */
IMG_VOID IMG_CALLCONV ExtDBGDrivSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker)
{
	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	DBGDrivSetMarker(psStream, ui32Marker);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return;
}

/*!
 @name	ExtDBGDrivGetMarker
 */
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetMarker(PDBG_STREAM psStream)
{
	IMG_UINT32	ui32Marker;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	ui32Marker = DBGDrivGetMarker(psStream);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return ui32Marker;
}

/*!
 @name	ExtDBGDrivWaitForEvent
 */
IMG_VOID IMG_CALLCONV ExtDBGDrivWaitForEvent(DBG_EVENT eEvent)
{
#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
	DBGDrivWaitForEvent(eEvent);
#else	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */
	PVR_UNREFERENCED_PARAMETER(eEvent);				/* PRQA S 3358 */
#endif	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */
}


/*!
 @name	ExtDBGDrivGetCtrlState
 */
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetCtrlState(PDBG_STREAM psStream, IMG_UINT32 ui32StateID)
{
	IMG_UINT32 ui32State = 0;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	ui32State = DBGDrivGetCtrlState(psStream, ui32StateID);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return ui32State;
}

/*!
 @name	ExtDBGDrivGetFrame
 */
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetFrame(void)
{
	IMG_UINT32 ui32Frame = 0;

	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	ui32Frame = DBGDrivGetFrame();

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return ui32Frame;
}

/*!
 @name	ExtDBGDrivGetCtrlState
 */
IMG_VOID IMG_CALLCONV ExtDBGDrivSetFrame(IMG_UINT32 ui32Frame)
{
	/* Aquire API Mutex */
	HostAquireMutex(g_pvAPIMutex);

	DBGDrivSetFrame(ui32Frame);

	/* Release API Mutex */
	HostReleaseMutex(g_pvAPIMutex);

	return;
}



/*!****************************************************************************
 @name		AtoI
 @brief		Returns the integer value of a decimal string
 @param		szIn - String with hexadecimal value
 @return	IMG_UINT32 integer value, 0 if string is null or not valid
				Based on Max`s one, now copes with (only) hex ui32ords, upper or lower case a-f.
*****************************************************************************/
IMG_UINT32 AtoI(IMG_CHAR *szIn)
{
	IMG_INT		iLen = 0;
	IMG_UINT32	ui32Value = 0;
	IMG_UINT32	ui32Digit=1;
	IMG_UINT32	ui32Base=10;
	IMG_INT		iPos;
	IMG_CHAR	bc;

	//get len of string
	while (szIn[iLen] > 0)
	{
		iLen ++;
	}

	//nothing to do
	if (iLen == 0)
	{
		return (0);
	}

	/* See if we have an 'x' or 'X' before the number to make it a hex number */
	iPos=0;
	while (szIn[iPos] == '0')
	{
		iPos++;
	}
	if (szIn[iPos] == '\0')
	{
		return 0;
	}
	if (szIn[iPos] == 'x' || szIn[iPos] == 'X')
	{
		ui32Base=16;
		szIn[iPos]='0';
	}

	//go through string from right (least significant) to left
	for (iPos = iLen - 1; iPos >= 0; iPos --)
	{
		bc = szIn[iPos];

		if ( (bc >= 'a') && (bc <= 'f') && ui32Base == 16)			//handle lower case a-f
		{
			bc -= 'a' - 0xa;
		}
		else
		if ( (bc >= 'A') && (bc <= 'F') && ui32Base == 16)			//handle upper case A-F
		{
			bc -= 'A' - 0xa;
		}
		else
		if ((bc >= '0') && (bc <= '9'))				//if char out of range, return 0
		{
			bc -= '0';
		}
		else
			return (0);

		ui32Value += (IMG_UINT32)bc  * ui32Digit;

		ui32Digit = ui32Digit * ui32Base;
	}
	return (ui32Value);
}


/*!****************************************************************************
 @name		StreamValid
 @brief		Validates supplied debug buffer.
 @param		psStream - debug stream
 @return	true if valid
*****************************************************************************/
static IMG_BOOL StreamValid(PDBG_STREAM psStream)
{
	PDBG_STREAM	psThis;

	psThis = g_psStreamList;

	while (psThis)
	{
		if (psStream && ((psThis == psStream) ||
						(psThis->psInitStream == psStream) ||
						(psThis->psDeinitStream == psStream)) )
		{
			return(IMG_TRUE);
		}
		else
		{
			psThis = psThis->psNext;
		}
	}

	return(IMG_FALSE);
}


/*!****************************************************************************
 @name		StreamValidForRead
 @brief		Validates supplied debug buffer for read op.
 @param		psStream - debug stream
 @return	true if readable
*****************************************************************************/
static IMG_BOOL StreamValidForRead(PDBG_STREAM psStream)
{
	if( StreamValid(psStream) &&
		((psStream->ui32Flags & DEBUG_FLAGS_WRITEONLY) == 0) )
	{
		return(IMG_TRUE);
	}

	return(IMG_FALSE);
}

/*!****************************************************************************
 @name		StreamValidForWrite
 @brief		Validates supplied debug buffer for write op.
 @param		psStream - debug stream
 @return	true if writable
*****************************************************************************/
static IMG_BOOL StreamValidForWrite(PDBG_STREAM psStream)
{
	if( StreamValid(psStream) &&
		((psStream->ui32Flags & DEBUG_FLAGS_READONLY) == 0) )
	{
		return(IMG_TRUE);
	}

	return(IMG_FALSE);
}

/*!****************************************************************************
 @name		Write
 @brief		Copies data from a buffer into selected stream. Stream size is fixed.
 @param		psStream - stream for output
 @param		pui8Data - input buffer
 @param		ui32InBuffSize - size of input
 @return	none
*****************************************************************************/
static IMG_VOID Write(PDBG_STREAM psStream,IMG_PUINT8 pui8Data,IMG_UINT32 ui32InBuffSize)
{
	/*
		Split copy into two bits as necessary (if we're allowed to wrap).
	*/
	if ((psStream->ui32Flags & DEBUG_FLAGS_CIRCULAR) == 0)
	{
		PVR_ASSERT( (psStream->ui32WPtr + ui32InBuffSize) < psStream->ui32Size );
	}

	if ((psStream->ui32WPtr + ui32InBuffSize) > psStream->ui32Size)
	{
		/* Yes we need two bits, calculate their sizes */
		IMG_UINT32 ui32B1 = psStream->ui32Size - psStream->ui32WPtr;
		IMG_UINT32 ui32B2 = ui32InBuffSize - ui32B1;

		/* Copy first block to current location */
		HostMemCopy((IMG_PVOID)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32WPtr),
				(IMG_PVOID) pui8Data,
				ui32B1);

		/* Copy second block to start of buffer */
		HostMemCopy(psStream->pvBase,
				(IMG_PVOID)(pui8Data + ui32B1),
				ui32B2);

		/* Set pointer to be the new end point */
		psStream->ui32WPtr = ui32B2;
	}
	else
	{	/* Can fit block in single chunk */
		HostMemCopy((IMG_PVOID)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32WPtr),
				(IMG_PVOID) pui8Data,
				ui32InBuffSize);

		psStream->ui32WPtr += ui32InBuffSize;

		if (psStream->ui32WPtr == psStream->ui32Size)
		{
			psStream->ui32WPtr = 0;
		}
	}
}


/*!****************************************************************************
 @name		WriteExpandingBuffer
 @brief		Copies data from a buffer into selected stream. Stream size may be expandable.
 @param		psStream - stream for output
 @param		pui8InBuf - input buffer
 @param		ui32InBuffSize - size of input
 @return	bytes copied
*****************************************************************************/
static IMG_UINT32 WriteExpandingBuffer(PDBG_STREAM psStream,IMG_UINT8 * pui8InBuf,IMG_UINT32 ui32InBuffSize)
{
	IMG_UINT ui32Space;

	/*
		How much space have we got in the buffer ?
	*/
	ui32Space = SpaceInStream(psStream);

	/*
		Check if we can expand the buffer 
	*/
	if (psStream->ui32Flags & DEBUG_FLAGS_NO_BUF_EXPANDSION)
	{
		/*
			Don't do anything if we've got less that 32 ui8tes of space and
			we're not allowing expansion of buffer space...
		*/
		if (ui32Space < 32)
		{
			PVR_DPF((PVR_DBG_ERROR, "WriteExpandingBuffer: buffer %p is full and isn't expandable", psStream));
			return(0);
		}
	}
	else
	{
		if ((ui32Space < 32) || (ui32Space <= (ui32InBuffSize + 4)))
		{
			IMG_UINT32	ui32NewBufSize;

			/*
				Find new buffer size, double the current size or increase by 1MB
			*/
			ui32NewBufSize = MIN(psStream->ui32Size<<1,psStream->ui32Size+(1<<20));
			ui32NewBufSize = MIN(ui32NewBufSize, (PDUMP_STREAMBUF_MAX_SIZE_MB<<20));

			PVR_DPF((PVR_DBGDRIV_MESSAGE, "Expanding buffer size = %x, new size = %x",
					psStream->ui32Size, ui32NewBufSize));

			if (ui32InBuffSize > psStream->ui32Size)
			{
				ui32NewBufSize += ui32InBuffSize;
				PVR_DPF((PVR_DBG_ERROR, "WriteExpandingBuffer: buffer %p is expanding by size of input buffer %u", psStream, ui32NewBufSize));
			}

			/* 
				Attempt to expand the buffer 
			*/
			if ((ui32NewBufSize < psStream->ui32Size) ||
					!ExpandStreamBuffer(psStream,ui32NewBufSize))
			{
				if (ui32Space < 32)
				{
					if((psStream->ui32Flags & DEBUG_FLAGS_CIRCULAR) != 0)
					{
						return(0);
					}
					else
					{
						/* out of memory */
						PVR_DPF((PVR_DBG_ERROR, "WriteExpandingBuffer: Unable to expand %p. Out of memory.", psStream));
						InvalidateAllStreams();
						return (0xFFFFFFFFUL);
					}
				}
			}

			/* 
				Recalc the space in the buffer 
			*/
			ui32Space = SpaceInStream(psStream);
			PVR_DPF((PVR_DBGDRIV_MESSAGE, "Expanded buffer, free space = %x",
					ui32Space));
		}
	}

	/*
		Only copy what we can..
	*/
	if (ui32Space <= (ui32InBuffSize + 4))
	{
		ui32InBuffSize = ui32Space - 4;
	}

	/*
		Write the stuff...
	*/
	Write(psStream,pui8InBuf,ui32InBuffSize);

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
	if (ui32InBuffSize)
	{
		HostSignalEvent(DBG_EVENT_STREAM_DATA);
	}
#endif
	return(ui32InBuffSize);
}

/*****************************************************************************
******************************************************************************
******************************************************************************
 THE ACTUAL FUNCTIONS
******************************************************************************
******************************************************************************
*****************************************************************************/

static IMG_VOID DBGDrivSetStreamName(PDBG_STREAM psStream,
									 IMG_CHAR* pszBase,
									 IMG_CHAR* pszExt)
{
	IMG_CHAR* pCh = psStream->szName;
	IMG_CHAR* pChEnd = psStream->szName+DBG_STREAM_NAME_MAX-8;
	IMG_CHAR* pSrcCh;
	IMG_CHAR* pSrcChEnd;

	for (pSrcCh = pszBase, pSrcChEnd = pszBase+strlen(pszBase);
			(pSrcCh < pSrcChEnd) && (pCh < pChEnd) ;
			pSrcCh++, pCh++)
	{
		*pCh = *pSrcCh;
	}

	for (pSrcCh = pszExt, pSrcChEnd = pszExt+strlen(pszExt);
			(pSrcCh < pSrcChEnd) && (pCh < pChEnd) ;
			pSrcCh++, pCh++)
	{
		*pCh = *pSrcCh;
	}

	*pCh = '\0';
}

/*!****************************************************************************
 @name		DBGDrivCreateStream
 @brief		Creates a pdump/debug stream
 @param		pszName - stream name
 @param		ui32Flags - output flags, text stream bit is set for pdumping
 @param		ui32Size - size of stream buffer in pages
 @return	none
*****************************************************************************/
IMG_BOOL IMG_CALLCONV DBGDrivCreateStream(IMG_CHAR *pszName,
                                          IMG_UINT32 ui32Flags,
                                          IMG_UINT32 ui32Size,
                                          IMG_HANDLE* phInit,
                                          IMG_HANDLE* phMain,
                                          IMG_HANDLE* phDeinit)
{
	IMG_BOOL            bUseNonPagedMem4Buffers = ((ui32Flags & DEBUG_FLAGS_USE_NONPAGED_MEM) != 0);
	PDBG_STREAM         psStream = IMG_NULL;
	PDBG_STREAM	        psInitStream = IMG_NULL;
	PDBG_STREAM         psStreamDeinit = IMG_NULL;
	IMG_VOID*           pvBase = IMG_NULL;

	/*
		If we already have a buffer using this name just return
		its handle.
	*/
	psStream = (PDBG_STREAM) DBGDrivFindStream(pszName, IMG_FALSE);
	if (psStream)
	{
		*phInit = psStream->psInitStream;
		*phMain = psStream;
		*phDeinit = psStream->psDeinitStream;
		return IMG_TRUE;
	}

	/*
		Allocate memory for control structures
	*/
	psStream = HostNonPageablePageAlloc(1);
	if	(!psStream)
	{
		PVR_DPF((PVR_DBG_ERROR,"DBGDriv: Couldn't alloc control structs\n\r"));
		goto errCleanup;
	}
	psInitStream = psStream+1;
	psStreamDeinit = psStream+2;


	/* Allocate memory for Main buffer */
	psStream->pvBase = IMG_NULL;
	if (bUseNonPagedMem4Buffers)
	{
		pvBase = HostNonPageablePageAlloc(ui32Size);
	}
	else
	{
		pvBase = HostPageablePageAlloc(ui32Size);
	}

	if (!pvBase)
	{
		PVR_DPF((PVR_DBG_ERROR,"DBGDriv: Couldn't alloc Stream buffer\n\r"));
		goto errCleanup;
	}

	/*
		Setup debug buffer state.
	*/
	psStream->psNext = 0;
	psStream->pvBase = pvBase;
	psStream->ui32Flags = ui32Flags | DEBUG_FLAGS_CIRCULAR;
	psStream->ui32Size = ui32Size * HOST_PAGESIZE;
	psStream->ui32RPtr = 0;
	psStream->ui32WPtr = 0;
	psStream->ui32Marker = 0;
	psStream->ui32InitPhaseWOff = 0;
	DBGDrivSetStreamName(psStream, pszName, "");
	PVR_DPF((PVR_DBG_MESSAGE,"DBGDriv: Created stream with deinit name (%s)\n\r", psStream->szName));

	/* Allocate memory for Init buffer */
	psInitStream->pvBase = IMG_NULL;
	if (bUseNonPagedMem4Buffers)
	{
		pvBase = HostNonPageablePageAlloc(ui32Size);
	}
	else
	{
		pvBase = HostPageablePageAlloc(ui32Size);
	}

	if (!pvBase)
	{
		PVR_DPF((PVR_DBG_ERROR,"DBGDriv: Couldn't alloc InitStream buffer\n\r"));
		goto errCleanup;
	}

	/* Initialise the stream for the Init phase */
	psInitStream->psNext = psInitStream->psInitStream = psInitStream->psDeinitStream = IMG_NULL;
	psInitStream->ui32Flags = ui32Flags;
	psInitStream->pvBase = pvBase;
	psInitStream->ui32Size = ui32Size * HOST_PAGESIZE;
	psInitStream->ui32RPtr = 0;
	psInitStream->ui32WPtr = 0;
	psInitStream->ui32Marker = 0;
	psInitStream->ui32InitPhaseWOff = 0;
	DBGDrivSetStreamName(psInitStream, pszName, "_Init");
	PVR_DPF((PVR_DBG_MESSAGE,"DBGDriv: Created stream with init name (%s)\n\r", psInitStream->szName));
	psStream->psInitStream = psInitStream;

	/* Allocate memory for Deinit buffer */
	psStreamDeinit->pvBase = IMG_NULL;
	if (bUseNonPagedMem4Buffers)
	{
		pvBase = HostNonPageablePageAlloc(1);
	}
	else
	{
		pvBase = HostPageablePageAlloc(1);
	}

	if (!pvBase)
	{
		PVR_DPF((PVR_DBG_ERROR,"DBGDriv: Couldn't alloc DeinitStream buffer\n\r"));
		goto errCleanup;
	}

	/* Initialise the stream for the Deinit phase */
	psStreamDeinit->psNext = psStreamDeinit->psInitStream = psStreamDeinit->psDeinitStream = IMG_NULL;
	psStreamDeinit->pvBase = pvBase;
	psStreamDeinit->ui32Flags = ui32Flags;
	psStreamDeinit->ui32Size = HOST_PAGESIZE;
	psStreamDeinit->ui32RPtr = 0;
	psStreamDeinit->ui32WPtr = 0;
	psStreamDeinit->ui32Marker = 0;
	psStreamDeinit->ui32InitPhaseWOff = 0;
	DBGDrivSetStreamName(psStreamDeinit, pszName, "_Deinit");
	PVR_DPF((PVR_DBG_MESSAGE,"DBGDriv: Created stream with deinit name (%s)\n\r", psStreamDeinit->szName));

	psStream->psDeinitStream = psStreamDeinit;

	/*
		Insert into list.
	*/
	psStream->psNext = g_psStreamList;
	g_psStreamList = psStream;

	AddSIDEntry(psStream);
	
	*phInit = psStream->psInitStream;
	*phMain = psStream;
	*phDeinit = psStream->psDeinitStream;

	return IMG_TRUE;

errCleanup:
	if (bUseNonPagedMem4Buffers)
	{
		if (psStream) HostNonPageablePageFree(psStream->pvBase);
		if (psInitStream) HostNonPageablePageFree(psInitStream->pvBase);
		if (psStreamDeinit) HostNonPageablePageFree(psStreamDeinit->pvBase);
	}
	else
	{
		if (psStream) HostPageablePageFree(psStream->pvBase);
		if (psInitStream) HostPageablePageFree(psInitStream->pvBase);
		if (psStreamDeinit) HostPageablePageFree(psStreamDeinit->pvBase);
	}
	HostNonPageablePageFree(psStream);
	psStream = psInitStream = psStreamDeinit = IMG_NULL;
	return IMG_FALSE;
}

/*!****************************************************************************
 @name		DBGDrivDestroyStream
 @brief		Delete a stream and free its memory
 @param		psStream - stream to be removed
 @return	none
*****************************************************************************/
IMG_VOID IMG_CALLCONV DBGDrivDestroyStream(IMG_HANDLE hInit,IMG_HANDLE hMain, IMG_HANDLE hDeinit)
{
	PDBG_STREAM psStreamInit = (PDBG_STREAM) hInit;
	PDBG_STREAM psStream = (PDBG_STREAM) hMain;
	PDBG_STREAM	psStreamDeinit = (PDBG_STREAM) hDeinit;
	PDBG_STREAM	psStreamThis;
	PDBG_STREAM	psStreamPrev;

	PVR_DPF((PVR_DBG_MESSAGE, "DBGDriv: Destroying stream %s\r\n", psStream->szName ));

	/*
		Validate buffer.
	*/
	if (!StreamValid(psStream))
	{
		return;
	}

	RemoveSIDEntry(psStream);
	
	/*
		Remove from linked list.
	*/
	psStreamThis = g_psStreamList;
	psStreamPrev = 0;

	while (psStreamThis)
	{
		if (psStreamThis == psStream)
		{
			if (psStreamPrev)
			{
				psStreamPrev->psNext = psStreamThis->psNext;
			}
			else
			{
				g_psStreamList = psStreamThis->psNext;
			}

			psStreamThis = 0;
		}
		else
		{
			psStreamPrev = psStreamThis;
			psStreamThis = psStreamThis->psNext;
		}
	}

	/*
		And free its memory.
	*/
	if ((psStream->ui32Flags & DEBUG_FLAGS_USE_NONPAGED_MEM) != 0)
	{
		HostNonPageablePageFree(psStream->pvBase);
		HostNonPageablePageFree(psStreamInit->pvBase);
		HostNonPageablePageFree(psStreamDeinit->pvBase);
	}
	else
	{
		HostPageablePageFree(psStream->pvBase);
		HostPageablePageFree(psStreamInit->pvBase);
		HostPageablePageFree(psStreamDeinit->pvBase);
	}

	/* Free the shared page used for the three stream tuple */
	HostNonPageablePageFree(psStream);
	psStream = psStreamInit = psStreamDeinit = IMG_NULL;

	if (g_psStreamList == 0)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"DBGDriv: Stream list now empty" ));
	}

	return;
}

/*!****************************************************************************
 @name		DBGDrivFindStream
 @brief		Finds/resets a named stream
 @param		pszName - stream name
 @param		bResetStream - whether to reset the stream, e.g. to end pdump init phase
 @return	none
*****************************************************************************/
IMG_VOID * IMG_CALLCONV DBGDrivFindStream(IMG_CHAR * pszName, IMG_BOOL bResetStream)
{
	PDBG_STREAM	psStream;
	PDBG_STREAM	psThis;
	IMG_UINT32	ui32Off;
	IMG_BOOL	bAreSame;

	psStream = 0;

	PVR_DPF((PVR_DBGDRIV_MESSAGE, "PDump client connecting to %s %s",
			pszName,
			(bResetStream == IMG_TRUE) ? "with reset" : "no reset"));

	/*
		Scan buffer names for supplied one.
	*/
	for (psThis = g_psStreamList; psThis != IMG_NULL; psThis = psThis->psNext)
	{
		bAreSame = IMG_TRUE;
		ui32Off = 0;

		if (strlen(psThis->szName) == strlen(pszName))
		{
			while ((ui32Off < DBG_STREAM_NAME_MAX) && (psThis->szName[ui32Off] != 0) && (pszName[ui32Off] != 0) && bAreSame)
			{
				if (psThis->szName[ui32Off] != pszName[ui32Off])
				{
					bAreSame = IMG_FALSE;
				}

				ui32Off++;
			}
		}
		else
		{
			bAreSame = IMG_FALSE;
		}

		if (bAreSame)
		{
			psStream = psThis;
			break;
		}
	}

	if(psStream)
	{
		psStream->psInitStream->ui32RPtr = 0;
		psStream->psDeinitStream->ui32RPtr = 0;
		psStream->ui32RPtr = 0;
		if (bResetStream)
		{
			/* This will erase any data written to the main stream 
			 * before the client starts. */
			psStream->ui32WPtr = 0;
		}
		psStream->ui32Marker = psStream->psInitStream->ui32Marker = 0;


		/* mark init stream to prevent further reading by pdump client */
		/* Check for possible race condition */
		psStream->psInitStream->ui32InitPhaseWOff = psStream->psInitStream->ui32WPtr;

		PVR_DPF((PVR_DBGDRIV_MESSAGE, "Set %s client marker bo %x",
				psStream->szName,
				psStream->psInitStream->ui32InitPhaseWOff));
	}

	return((IMG_VOID *) psStream);
}

static IMG_VOID IMG_CALLCONV DBGDrivInvalidateStream(PDBG_STREAM psStream)
{
	IMG_CHAR pszErrorMsg[] = "**OUTOFMEM\n";
	IMG_UINT32 ui32Space;
	IMG_UINT32 ui32Off = 0;
	IMG_UINT32 ui32WPtr = psStream->ui32WPtr;
	IMG_PUINT8 pui8Buffer = (IMG_UINT8 *) psStream->pvBase;
	
	PVR_DPF((PVR_DBG_ERROR, "DBGDrivInvalidateStream: An error occurred for stream %s", psStream->szName ));

	/*
		Validate buffer.
	*/
	/*
	if (!StreamValid(psStream))
	{
		return;
	}
*/
	/* Write what we can of the error message */
	ui32Space = SpaceInStream(psStream);

	/* Make sure there's space for termination character */
	if(ui32Space > 0)
	{
		ui32Space--;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "DBGDrivInvalidateStream: Buffer full."));
	}

	while((pszErrorMsg[ui32Off] != 0) && (ui32Off < ui32Space))
	{
		pui8Buffer[ui32WPtr] = (IMG_UINT8)pszErrorMsg[ui32Off];
		ui32Off++;
		ui32WPtr++;
	}
	pui8Buffer[ui32WPtr++] = '\0';
	psStream->ui32WPtr = ui32WPtr;

	/* Buffer will accept no more params from Services/client driver */
	psStream->ui32Flags |= DEBUG_FLAGS_READONLY;
}

/*!****************************************************************************
 @name		InvalidateAllStreams
 @brief		invalidate all streams in list
 @return	none
*****************************************************************************/
static IMG_VOID InvalidateAllStreams(IMG_VOID)
{
	PDBG_STREAM psStream = g_psStreamList;
	while (psStream != IMG_NULL)
	{
		DBGDrivInvalidateStream(psStream);
		DBGDrivInvalidateStream(psStream->psInitStream);
		DBGDrivInvalidateStream(psStream->psDeinitStream);
		psStream = psStream->psNext;
	}
	return;
}

/*!****************************************************************************
 @name		DBGDrivWrite2
 @brief		Copies data from a buffer into selected (expandable) stream.
 @param		psStream - stream for output
 @param		pui8InBuf - input buffer
 @param		ui32InBuffSize - size of input
 @return	bytes copied, 0 if recoverable error, -1 if unrecoverable error
*****************************************************************************/
IMG_UINT32 IMG_CALLCONV DBGDrivWrite2(PDBG_STREAM psStream,IMG_UINT8 * pui8InBuf,IMG_UINT32 ui32InBuffSize)
{

	/*
		Validate buffer.
	*/
	if (!StreamValidForWrite(psStream))
	{
		PVR_DPF((PVR_DBG_ERROR, "DBGDrivWrite2: stream not valid"));
		return(0xFFFFFFFFUL);
	}

	PVR_DPF((PVR_DBGDRIV_MESSAGE, "Recv(exp) %d b for %s: Roff = %x, WOff = %x",
			ui32InBuffSize,
			psStream->szName,
			psStream->ui32RPtr,
			psStream->ui32WPtr));

	return( WriteExpandingBuffer(psStream, pui8InBuf, ui32InBuffSize) );
}

/*!****************************************************************************
 @name		DBGDrivRead
 @brief		Read from debug driver buffers
 @param		psMainStream - stream
 @param		ui32BufID - on of the DEBUG_READ_BUFID flags to indicate which buffer
 @param		ui32OutBuffSize - available space in client buffer
 @param		pui8OutBuf - output buffer
 @return	bytes read, 0 if failure occurred
*****************************************************************************/
IMG_UINT32 IMG_CALLCONV DBGDrivRead(PDBG_STREAM psMainStream, IMG_UINT32 ui32BufID, IMG_UINT32 ui32OutBuffSize,IMG_UINT8 * pui8OutBuf)
{
	IMG_UINT32 ui32Data;
	DBG_STREAM *psStream;

	/*
		Validate buffer.
	*/
	if (!StreamValidForRead(psMainStream))
	{
		PVR_DPF((PVR_DBG_ERROR, "DBGDrivRead: buffer %p is invalid", psMainStream));
		return(0);
	}

	if(ui32BufID == DEBUG_READ_BUFID_INIT)
	{
		psStream = psMainStream->psInitStream;
	}
	else if (ui32BufID == DEBUG_READ_BUFID_DEINIT)
	{
		psStream = psMainStream->psDeinitStream;
	}
	else
	{
		psStream = psMainStream;
	}

	/* Don't read beyond the init phase marker point */
	if (psStream->ui32RPtr == psStream->ui32WPtr ||
		((psStream->ui32InitPhaseWOff > 0) &&
		 (psStream->ui32RPtr >= psStream->ui32InitPhaseWOff)) )
	{
		return(0);
	}

	/*
		Get amount of data in buffer.
	*/
	if (psStream->ui32RPtr <= psStream->ui32WPtr)
	{
		ui32Data = psStream->ui32WPtr - psStream->ui32RPtr;
	}
	else
	{
		ui32Data = psStream->ui32WPtr + (psStream->ui32Size - psStream->ui32RPtr);
	}

	/*
		Don't read beyond the init phase marker point
	*/
	if ((psStream->ui32InitPhaseWOff > 0) &&
		(psStream->ui32InitPhaseWOff < psStream->ui32WPtr))
	{
		ui32Data = psStream->ui32InitPhaseWOff - psStream->ui32RPtr;
	}

	/*
		Only transfer what target buffer can handle.
	*/
	if (ui32Data > ui32OutBuffSize)
	{
		ui32Data = ui32OutBuffSize;
	}

	PVR_DPF((PVR_DBGDRIV_MESSAGE, "Send %x b from %s: Roff = %x, WOff = %x",
			ui32Data,
			psStream->szName,
			psStream->ui32RPtr,
			psStream->ui32WPtr));

	/*
		Split copy into two bits or one depending on W/R position.
	*/
	if ((psStream->ui32RPtr + ui32Data) > psStream->ui32Size)
	{	/* Calc block 1 and block 2 sizes */
		IMG_UINT32 ui32B1 = psStream->ui32Size - psStream->ui32RPtr;
		IMG_UINT32 ui32B2 = ui32Data - ui32B1;

		/* Copy up to end of circular buffer */
		HostMemCopy((IMG_VOID *) pui8OutBuf,
				(IMG_VOID *)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32RPtr),
				ui32B1);

		/* Copy from start of circular buffer */
		HostMemCopy((IMG_VOID *)(pui8OutBuf + ui32B1),
				psStream->pvBase,
				ui32B2);

		/* Update read pointer now that we've copied the data out */
		psStream->ui32RPtr = ui32B2;
	}
	else
	{	/* Copy data from wherever */
		HostMemCopy((IMG_VOID *) pui8OutBuf,
				(IMG_VOID *)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32RPtr),
				ui32Data);

		/* Update read pointer now that we've copied the data out */
		psStream->ui32RPtr += ui32Data;

		/* Check for wrapping */
		if ((psStream->ui32RPtr != psStream->ui32WPtr) &&
			(psStream->ui32RPtr >= psStream->ui32Size))
		{
			psStream->ui32RPtr = 0;
		}
	}

	return(ui32Data);
}

/*!****************************************************************************
 @name		DBGDrivSetMarker
 @brief		Sets the marker in the stream to split output files
 @param		psStream, ui32Marker
 @return	nothing
*****************************************************************************/
IMG_VOID IMG_CALLCONV DBGDrivSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker)
{
	/*
		Validate buffer
	*/
	if (!StreamValid(psStream))
	{
		return;
	}

	/* Called by PDump client to reset the marker to zero after a file split */
	if ((ui32Marker == 0) && (psStream->ui32Marker == 0))
	{
		PVR_DPF((PVR_DBG_ERROR, "DBGDrivSetMarker: Client resetting marker that is already zero!"));
	}
	/* Called by pvrsrvkm to set the marker to signal a file split is required */
	if ((ui32Marker != 0) && (psStream->ui32Marker != 0))
	{
		/* In this case a previous split request is still outstanding. The
		 * client has not yet actioned and acknowledged the previous
		 * marker. This may be an error if the client does not catch-up and
		 * the stream's written data is allowed to pass the max file
		 * size again. If this happens the PDump is invalid as the offsets
		 * from the script file will be incorrect.
		 */
		PVR_DPF((PVR_DBG_ERROR, "DBGDrivSetMarker: Server setting marker that is already set!"));
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "DBGDrivSetMarker: Setting stream split marker to %d (was %d)", ui32Marker, psStream->ui32Marker));
	}

	psStream->ui32Marker = ui32Marker;
}

/*!****************************************************************************
 @name		DBGDrivGetMarker
 @brief 	Gets the marker in the stream to split output files
 @param	 	psStream - stream
 @return	marker offset
*****************************************************************************/
IMG_UINT32 IMG_CALLCONV DBGDrivGetMarker(PDBG_STREAM psStream)
{
	/*
		Validate buffer
	*/
	if (!StreamValid(psStream))
	{
		return 0;
	}

	return psStream->ui32Marker;
}

/*!****************************************************************************
 @name		DBGDrivGetServiceTable
 @brief		get jump table for Services driver
 @return	pointer to jump table
*****************************************************************************/
IMG_PVOID IMG_CALLCONV DBGDrivGetServiceTable(IMG_VOID)
{
	return((IMG_PVOID)&g_sDBGKMServices);
}


#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
/*!****************************************************************************
 @name		DBGDrivWaitForEvent
 @brief		waits for an event
 @param		eEvent - debug driver event
 @return	IMG_VOID
*****************************************************************************/
IMG_VOID IMG_CALLCONV DBGDrivWaitForEvent(DBG_EVENT eEvent)
{
	HostWaitForEvent(eEvent);
}
#endif

/*	Use PVR_DPF() to avoid state messages in release build */
#if defined(PVR_DISABLE_LOGGING) || !defined(DEBUG)
#define PVR_LOG(...)
#else

extern IMG_VOID PVRSRVDebugPrintf(IMG_UINT32	ui32DebugLevel,
						const IMG_CHAR*	pszFileName,
						IMG_UINT32	ui32Line,
						const IMG_CHAR*	pszFormat,
						...	);
/* Reproduce the PVR_LOG macro here but direct it to DPF */
#define PVR_LOG(...)	PVRSRVDebugPrintf( DBGPRIV_CALLTRACE, __FILE__, __LINE__ , __VA_ARGS__);

#endif


/*!****************************************************************************
 @name		DBGDrivGetCtrlState
 @brief		Gets a state value from the debug driver or stream
 @param		psStream - stream
 @param		ui32StateID - state ID
 @return	Nothing
*****************************************************************************/
IMG_UINT32 IMG_CALLCONV DBGDrivGetCtrlState(PDBG_STREAM psStream, IMG_UINT32 ui32StateID)
{
	/* Validate buffer */
	if (!StreamValid(psStream))
	{
		return (0xFFFFFFFF);
	}

	/* Retrieve the state asked for */
	switch (ui32StateID)
	{
	case DBG_GET_STATE_FLAG_IS_READONLY:
		return ((psStream->ui32Flags & DEBUG_FLAGS_READONLY) != 0);
		break;

	case 0xFE: /* Dump the current stream state */
		PVR_LOG("------ PDUMP DBGDriv: psStream( %p ) ( -- %s -- ) ui32Flags( %x )",
				psStream, psStream->szName, psStream->ui32Flags);
		PVR_LOG("------ PDUMP DBGDriv: psStream->pvBase( %p ) psStream->ui32Size( %u )",
				psStream->pvBase, psStream->ui32Size);
		PVR_LOG("------ PDUMP DBGDriv: psStream->ui32RPtr( %u ) psStream->ui32WPtr( %u )",
				psStream->ui32RPtr, psStream->ui32WPtr);
		PVR_LOG("------ PDUMP DBGDriv: psStream->ui32Marker( %u ) psStream->ui32InitPhaseWOff( %u )",
				psStream->ui32Marker, psStream->ui32InitPhaseWOff);
		if (psStream->psInitStream)
		{
			PVR_LOG("-------- PDUMP DBGDriv: psInitStream( %p ) ( -- %s -- ) ui32Flags( %x )",
					psStream->psInitStream, psStream->psInitStream->szName, psStream->ui32Flags);
			PVR_LOG("-------- PDUMP DBGDriv: psInitStream->pvBase( %p ) psInitStream->ui32Size( %u )",
					psStream->psInitStream->pvBase, psStream->psInitStream->ui32Size);
			PVR_LOG("-------- PDUMP DBGDriv: psInitStream->ui32RPtr( %u ) psInitStream->ui32WPtr( %u )",
					psStream->psInitStream->ui32RPtr, psStream->psInitStream->ui32WPtr);
			PVR_LOG("-------- PDUMP DBGDriv: psInitStream->ui32Marker( %u ) psInitStream->ui32InitPhaseWOff( %u ) ",
					psStream->psInitStream->ui32Marker, psStream->psInitStream->ui32InitPhaseWOff);
		}

		break;

	case 0xFF: /* Dump driver state not in a stream */
		{
			PVR_LOG("------ PDUMP DBGDriv: g_psStreamList( head %p ) g_pvAPIMutex( %p ) g_PDumpCurrentFrameNo( %u )", g_psStreamList, g_pvAPIMutex, g_PDumpCurrentFrameNo);
		}
		break;

	default:
		PVR_ASSERT(0);
	}

	return (0xFFFFFFFF);
}

IMG_UINT32 IMG_CALLCONV DBGDrivGetFrame(void)
{
	return g_PDumpCurrentFrameNo;
}

IMG_VOID IMG_CALLCONV DBGDrivSetFrame(IMG_UINT32 ui32Frame)
{
	g_PDumpCurrentFrameNo = ui32Frame;
}


/*!****************************************************************************
 @name		ExpandStreamBuffer
 @brief		allocates a new buffer when the current one is full
 @param		psStream - stream
 @param		ui32NewSize - new size
 @return	IMG_TRUE - if allocation succeeded, IMG_FALSE - if not
*****************************************************************************/
static IMG_BOOL ExpandStreamBuffer(PDBG_STREAM psStream, IMG_UINT32 ui32NewSize)
{
	IMG_VOID *	pvNewBuf;
	IMG_UINT32	ui32NewSizeInPages;
	IMG_UINT32	ui32NewWOffset;
	IMG_UINT32	ui32NewROffset;
	IMG_UINT32	ui32SpaceInOldBuf;

	/* 
		First check new size is bigger than existing size 
	*/
	if (psStream->ui32Size >= ui32NewSize)
	{
		return IMG_FALSE;
	}

	/*
		Calc space in old buffer 
	*/
	ui32SpaceInOldBuf = SpaceInStream(psStream);

	/*
		Allocate new buffer 
	*/
	ui32NewSizeInPages = ((ui32NewSize + 0xfffUL) & ~0xfffUL) / 4096UL;

	if ((psStream->ui32Flags & DEBUG_FLAGS_USE_NONPAGED_MEM) != 0)
	{
		pvNewBuf = HostNonPageablePageAlloc(ui32NewSizeInPages);
	}
	else
	{
		pvNewBuf = HostPageablePageAlloc(ui32NewSizeInPages);
	}

	if (pvNewBuf == IMG_NULL)
	{
		return IMG_FALSE;
	}

	if ((psStream->ui32Flags & DEBUG_FLAGS_CIRCULAR) != 0)
	{
		/*
			Copy over old buffer to new one, we place data at start of buffer
			even if Read offset is not at start of buffer
		*/
		if (psStream->ui32RPtr <= psStream->ui32WPtr)
		{
			/*
				No wrapping of data so copy data to start of new buffer 
			*/
		HostMemCopy(pvNewBuf,
					(IMG_VOID *)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32RPtr),
					psStream->ui32WPtr - psStream->ui32RPtr);
		}
		else
		{
			IMG_UINT32	ui32FirstCopySize;
	
			/*
				The data has wrapped around the buffer, copy beginning of buffer first 
			*/
			ui32FirstCopySize = psStream->ui32Size - psStream->ui32RPtr;
	
			HostMemCopy(pvNewBuf,
					(IMG_VOID *)((IMG_UINTPTR_T)psStream->pvBase + psStream->ui32RPtr),
					ui32FirstCopySize);
	
			/*
				Now second half 
			*/
			HostMemCopy((IMG_VOID *)((IMG_UINTPTR_T)pvNewBuf + ui32FirstCopySize),
					(IMG_VOID *)(IMG_PBYTE)psStream->pvBase,
					psStream->ui32WPtr);
		}
		ui32NewROffset = 0;
	}
	else
	{
		/* Copy everything in the old buffer to the new one */
		HostMemCopy(pvNewBuf, psStream->pvBase,	psStream->ui32WPtr);
		ui32NewROffset = psStream->ui32RPtr;
	}

	/*
		New Write offset is at end of data 
	*/                                                        
	ui32NewWOffset = psStream->ui32Size - ui32SpaceInOldBuf;

	/*
		Free old buffer 
	*/
	if ((psStream->ui32Flags & DEBUG_FLAGS_USE_NONPAGED_MEM) != 0)
	{
		HostNonPageablePageFree(psStream->pvBase);
	}
	else
	{
		HostPageablePageFree(psStream->pvBase);
	}

	/*
		Now set new params up 
	*/
	psStream->pvBase = pvNewBuf;
	psStream->ui32RPtr = ui32NewROffset;
	psStream->ui32WPtr = ui32NewWOffset;
	psStream->ui32Size = ui32NewSizeInPages * 4096;

	return IMG_TRUE;
}

/*!****************************************************************************
 @name		SpaceInStream
 @brief		remaining space in stream
 @param		psStream - stream
 @return	bytes remaining
*****************************************************************************/
static IMG_UINT32 SpaceInStream(PDBG_STREAM psStream)
{
	IMG_UINT32	ui32Space;

	if ((psStream->ui32Flags & DEBUG_FLAGS_CIRCULAR) != 0)
	{
		/* Allow overwriting the buffer which was already read */
		if (psStream->ui32RPtr > psStream->ui32WPtr)
		{
			ui32Space = psStream->ui32RPtr - psStream->ui32WPtr;
		}
		else
		{
			ui32Space = psStream->ui32RPtr + (psStream->ui32Size - psStream->ui32WPtr);
		}
	}
	else
	{
		/* Don't overwrite anything */
		ui32Space = psStream->ui32Size - psStream->ui32WPtr;
	}

	return ui32Space;
}


/*!****************************************************************************
 @name		DestroyAllStreams
 @brief		delete all streams in list
 @return	none
*****************************************************************************/
IMG_VOID DestroyAllStreams(IMG_VOID)
{
	PDBG_STREAM psStream = g_psStreamList;
	PDBG_STREAM psStreamToFree;

	while (psStream != IMG_NULL)
	{
		psStreamToFree = psStream;
		psStream = psStream->psNext;
		DBGDrivDestroyStream(psStreamToFree->psInitStream, psStreamToFree, psStreamToFree->psDeinitStream);
	}
	g_psStreamList = IMG_NULL;
	return;
}

/******************************************************************************
 End of file (DBGDRIV.C)
******************************************************************************/
