/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#ifndef _DBGDRIV_
#define _DBGDRIV_

/*****************************************************************************
 The odd constant or two
*****************************************************************************/

#define DBGDRIV_VERSION 	0x100
#define MAX_PROCESSES 		2
#define BLOCK_USED			0x01
#define BLOCK_LOCKED		0x02
#define DBGDRIV_MONOBASE	0x000B0000


/*****************************************************************************
 * OS-specific declarations and init/cleanup functions
*****************************************************************************/
extern void *	g_pvAPIMutex;

extern IMG_INT dbgdrv_init(void);
extern void dbgdrv_cleanup(void);

/*****************************************************************************
 Internal debug driver core functions
*****************************************************************************/
/* Called by WDDM debug driver win7/hostfunc.c */
IMG_BOOL IMG_CALLCONV DBGDrivCreateStream(IMG_CHAR *pszName, IMG_UINT32 ui32Flags, IMG_UINT32 ui32Pages,
											IMG_HANDLE* phInit, IMG_HANDLE* phMain, IMG_HANDLE* phDeinit);

/* Called by Linux debug driver main.c to allow the API mutex lock to be used
 * to protect the common IOCTL read buffer while avoiding deadlock in the Ext
 * layer
 */
IMG_UINT32 IMG_CALLCONV DBGDrivRead(PDBG_STREAM psStream, IMG_UINT32 ui32BufID,
									IMG_UINT32 ui32OutBufferSize,IMG_UINT8 *pui8OutBuf);
IMG_UINT32 IMG_CALLCONV DBGDrivGetMarker(PDBG_STREAM psStream);

/* Used in ioctl.c in DBGDIOCDrivGetServiceTable() which is called in WDDM PDump files */
void * IMG_CALLCONV DBGDrivGetServiceTable(void);

/* Used in WDDM version of debug driver win7/main.c */
void DestroyAllStreams(void);

/*****************************************************************************
 Function prototypes
*****************************************************************************/
IMG_UINT32 AtoI(IMG_CHAR *szIn);

void HostMemSet(void *pvDest,IMG_UINT8 ui8Value,IMG_UINT32 ui32Size);
void HostMemCopy(void *pvDest,void *pvSrc,IMG_UINT32 ui32Size);

/*****************************************************************************
 Secure handle Function prototypes
*****************************************************************************/
IMG_SID PStream2SID(PDBG_STREAM psStream);
PDBG_STREAM SID2PStream(IMG_SID hStream); 
IMG_BOOL AddSIDEntry(PDBG_STREAM psStream);
IMG_BOOL RemoveSIDEntry(PDBG_STREAM psStream);

/*****************************************************************************
 Declarations for IOCTL Service table and KM table entry points
*****************************************************************************/
IMG_BOOL   IMG_CALLCONV ExtDBGDrivCreateStream(IMG_CHAR *pszName, IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size, IMG_HANDLE* phInit, IMG_HANDLE* phMain, IMG_HANDLE* phDeinit);
void   IMG_CALLCONV ExtDBGDrivDestroyStream(IMG_HANDLE hInit, IMG_HANDLE hMain, IMG_HANDLE hDeinit);
void * IMG_CALLCONV ExtDBGDrivFindStream(IMG_CHAR * pszName, IMG_BOOL bResetStream);
IMG_UINT32 IMG_CALLCONV ExtDBGDrivRead(PDBG_STREAM psStream, IMG_UINT32 ui32BufID, IMG_UINT32 ui32OutBuffSize,IMG_UINT8 *pui8OutBuf);
IMG_UINT32 IMG_CALLCONV ExtDBGDrivWrite2(PDBG_STREAM psStream,IMG_UINT8 *pui8InBuf,IMG_UINT32 ui32InBuffSize);
void   IMG_CALLCONV ExtDBGDrivSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker);
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetMarker(PDBG_STREAM psStream);
void   IMG_CALLCONV ExtDBGDrivWaitForEvent(DBG_EVENT eEvent);
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetCtrlState(PDBG_STREAM psStream, IMG_UINT32 ui32StateID);
IMG_UINT32 IMG_CALLCONV ExtDBGDrivGetFrame(void);
void   IMG_CALLCONV ExtDBGDrivSetFrame(IMG_UINT32 ui32Frame);

#endif

/*****************************************************************************
 End of file (DBGDRIV.H)
*****************************************************************************/
