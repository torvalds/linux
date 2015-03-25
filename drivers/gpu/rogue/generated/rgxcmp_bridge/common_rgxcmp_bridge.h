/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxcmp
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for rgxcmp
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

#ifndef COMMON_RGXCMP_BRIDGE_H
#define COMMON_RGXCMP_BRIDGE_H

#include "rgx_bridge.h"
#include "sync_external.h"
#include "rgx_fwif_shared.h"


#include "pvr_bridge_io.h"

#define PVRSRV_BRIDGE_RGXCMP_CMD_FIRST			(PVRSRV_BRIDGE_RGXCMP_START)
#define PVRSRV_BRIDGE_RGXCMP_RGXCREATECOMPUTECONTEXT			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+0)
#define PVRSRV_BRIDGE_RGXCMP_RGXDESTROYCOMPUTECONTEXT			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+1)
#define PVRSRV_BRIDGE_RGXCMP_RGXKICKCDM			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+2)
#define PVRSRV_BRIDGE_RGXCMP_RGXFLUSHCOMPUTEDATA			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+3)
#define PVRSRV_BRIDGE_RGXCMP_RGXSETCOMPUTECONTEXTPRIORITY			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+4)
#define PVRSRV_BRIDGE_RGXCMP_RGXKICKSYNCCDM			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+5)
#define PVRSRV_BRIDGE_RGXCMP_CMD_LAST			(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+5)


/*******************************************
            RGXCreateComputeContext          
 *******************************************/

/* Bridge in structure for RGXCreateComputeContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATECOMPUTECONTEXT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32Priority;
	IMG_DEV_VIRTADDR sMCUFenceAddr;
	IMG_UINT32 ui32FrameworkCmdize;
	IMG_BYTE * psFrameworkCmd;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCREATECOMPUTECONTEXT;


/* Bridge out structure for RGXCreateComputeContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATECOMPUTECONTEXT_TAG
{
	IMG_HANDLE hComputeContext;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCREATECOMPUTECONTEXT;

/*******************************************
            RGXDestroyComputeContext          
 *******************************************/

/* Bridge in structure for RGXDestroyComputeContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYCOMPUTECONTEXT_TAG
{
	IMG_HANDLE hComputeContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDESTROYCOMPUTECONTEXT;


/* Bridge out structure for RGXDestroyComputeContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYCOMPUTECONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDESTROYCOMPUTECONTEXT;

/*******************************************
            RGXKickCDM          
 *******************************************/

/* Bridge in structure for RGXKickCDM */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKCDM_TAG
{
	IMG_HANDLE hComputeContext;
	IMG_UINT32 ui32ClientFenceCount;
	PRGXFWIF_UFO_ADDR * psClientFenceUFOAddress;
	IMG_UINT32 * pui32ClientFenceValue;
	IMG_UINT32 ui32ClientUpdateCount;
	PRGXFWIF_UFO_ADDR * psClientUpdateUFOAddress;
	IMG_UINT32 * pui32ClientUpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerSyncFlags;
	IMG_HANDLE * phServerSyncs;
	IMG_UINT32 ui32CmdSize;
	IMG_BYTE * psDMCmd;
	IMG_BOOL bbPDumpContinuous;
	IMG_UINT32 ui32ExternalJobReference;
	IMG_UINT32 ui32InternalJobReference;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXKICKCDM;


/* Bridge out structure for RGXKickCDM */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKCDM_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXKICKCDM;

/*******************************************
            RGXFlushComputeData          
 *******************************************/

/* Bridge in structure for RGXFlushComputeData */
typedef struct PVRSRV_BRIDGE_IN_RGXFLUSHCOMPUTEDATA_TAG
{
	IMG_HANDLE hComputeContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXFLUSHCOMPUTEDATA;


/* Bridge out structure for RGXFlushComputeData */
typedef struct PVRSRV_BRIDGE_OUT_RGXFLUSHCOMPUTEDATA_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXFLUSHCOMPUTEDATA;

/*******************************************
            RGXSetComputeContextPriority          
 *******************************************/

/* Bridge in structure for RGXSetComputeContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPRIORITY_TAG
{
	IMG_HANDLE hComputeContext;
	IMG_UINT32 ui32Priority;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPRIORITY;


/* Bridge out structure for RGXSetComputeContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPRIORITY;

/*******************************************
            RGXKickSyncCDM          
 *******************************************/

/* Bridge in structure for RGXKickSyncCDM */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKSYNCCDM_TAG
{
	IMG_HANDLE hComputeContext;
	IMG_UINT32 ui32ClientFenceCount;
	PRGXFWIF_UFO_ADDR * psClientFenceUFOAddress;
	IMG_UINT32 * pui32ClientFenceValue;
	IMG_UINT32 ui32ClientUpdateCount;
	PRGXFWIF_UFO_ADDR * psClientUpdateUFOAddress;
	IMG_UINT32 * pui32ClientUpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerSyncFlags;
	IMG_HANDLE * phServerSyncs;
	IMG_UINT32 ui32NumFenceFDs;
	IMG_INT32 * pi32FenceFDs;
	IMG_BOOL bbPDumpContinuous;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXKICKSYNCCDM;


/* Bridge out structure for RGXKickSyncCDM */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKSYNCCDM_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXKICKSYNCCDM;

#endif /* COMMON_RGXCMP_BRIDGE_H */
