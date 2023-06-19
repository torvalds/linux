/*******************************************************************************
@File
@Title          Common bridge header for rgxcmp
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxcmp
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
*******************************************************************************/

#ifndef COMMON_RGXCMP_BRIDGE_H
#define COMMON_RGXCMP_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_RGXCMP_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXCMP_RGXCREATECOMPUTECONTEXT			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXCMP_RGXDESTROYCOMPUTECONTEXT			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXCMP_RGXFLUSHCOMPUTEDATA			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXCMP_RGXSETCOMPUTECONTEXTPRIORITY			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXCMP_RGXNOTIFYCOMPUTEWRITEOFFSETUPDATE			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXCMP_RGXKICKCDM2			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXCMP_RGXSETCOMPUTECONTEXTPROPERTY			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXCMP_RGXGETLASTDEVICEERROR			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+7
#define PVRSRV_BRIDGE_RGXCMP_RGXKICKTIMESTAMPQUERY			PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+8
#define PVRSRV_BRIDGE_RGXCMP_CMD_LAST			(PVRSRV_BRIDGE_RGXCMP_CMD_FIRST+8)

/*******************************************
            RGXCreateComputeContext
 *******************************************/

/* Bridge in structure for RGXCreateComputeContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATECOMPUTECONTEXT_TAG
{
	IMG_UINT64 ui64RobustnessAddress;
	IMG_HANDLE hPrivData;
	IMG_BYTE *pui8FrameworkCmd;
	IMG_BYTE *pui8StaticComputeContextState;
	IMG_INT32 i32Priority;
	IMG_UINT32 ui32ContextFlags;
	IMG_UINT32 ui32FrameworkCmdSize;
	IMG_UINT32 ui32MaxDeadlineMS;
	IMG_UINT32 ui32PackedCCBSizeU88;
	IMG_UINT32 ui32StaticComputeContextStateSize;
} __packed PVRSRV_BRIDGE_IN_RGXCREATECOMPUTECONTEXT;

/* Bridge out structure for RGXCreateComputeContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATECOMPUTECONTEXT_TAG
{
	IMG_HANDLE hComputeContext;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATECOMPUTECONTEXT;

/*******************************************
            RGXDestroyComputeContext
 *******************************************/

/* Bridge in structure for RGXDestroyComputeContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYCOMPUTECONTEXT_TAG
{
	IMG_HANDLE hComputeContext;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYCOMPUTECONTEXT;

/* Bridge out structure for RGXDestroyComputeContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYCOMPUTECONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYCOMPUTECONTEXT;

/*******************************************
            RGXFlushComputeData
 *******************************************/

/* Bridge in structure for RGXFlushComputeData */
typedef struct PVRSRV_BRIDGE_IN_RGXFLUSHCOMPUTEDATA_TAG
{
	IMG_HANDLE hComputeContext;
} __packed PVRSRV_BRIDGE_IN_RGXFLUSHCOMPUTEDATA;

/* Bridge out structure for RGXFlushComputeData */
typedef struct PVRSRV_BRIDGE_OUT_RGXFLUSHCOMPUTEDATA_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFLUSHCOMPUTEDATA;

/*******************************************
            RGXSetComputeContextPriority
 *******************************************/

/* Bridge in structure for RGXSetComputeContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPRIORITY_TAG
{
	IMG_HANDLE hComputeContext;
	IMG_INT32 i32Priority;
} __packed PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPRIORITY;

/* Bridge out structure for RGXSetComputeContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPRIORITY;

/*******************************************
            RGXNotifyComputeWriteOffsetUpdate
 *******************************************/

/* Bridge in structure for RGXNotifyComputeWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_IN_RGXNOTIFYCOMPUTEWRITEOFFSETUPDATE_TAG
{
	IMG_HANDLE hComputeContext;
} __packed PVRSRV_BRIDGE_IN_RGXNOTIFYCOMPUTEWRITEOFFSETUPDATE;

/* Bridge out structure for RGXNotifyComputeWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_OUT_RGXNOTIFYCOMPUTEWRITEOFFSETUPDATE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXNOTIFYCOMPUTEWRITEOFFSETUPDATE;

/*******************************************
            RGXKickCDM2
 *******************************************/

/* Bridge in structure for RGXKickCDM2 */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKCDM2_TAG
{
	IMG_UINT64 ui64DeadlineInus;
	IMG_HANDLE hComputeContext;
	IMG_UINT32 *pui32ClientUpdateOffset;
	IMG_UINT32 *pui32ClientUpdateValue;
	IMG_UINT32 *pui32SyncPMRFlags;
	IMG_BYTE *pui8DMCmd;
	IMG_CHAR *puiUpdateFenceName;
	IMG_HANDLE *phClientUpdateUFOSyncPrimBlock;
	IMG_HANDLE *phSyncPMRs;
	PVRSRV_FENCE hCheckFenceFd;
	PVRSRV_TIMELINE hUpdateTimeline;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_UINT32 ui32CmdSize;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32NumOfWorkgroups;
	IMG_UINT32 ui32NumOfWorkitems;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32SyncPMRCount;
} __packed PVRSRV_BRIDGE_IN_RGXKICKCDM2;

/* Bridge out structure for RGXKickCDM2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKCDM2_TAG
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE hUpdateFence;
} __packed PVRSRV_BRIDGE_OUT_RGXKICKCDM2;

/*******************************************
            RGXSetComputeContextProperty
 *******************************************/

/* Bridge in structure for RGXSetComputeContextProperty */
typedef struct PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Input;
	IMG_HANDLE hComputeContext;
	IMG_UINT32 ui32Property;
} __packed PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPROPERTY;

/* Bridge out structure for RGXSetComputeContextProperty */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Output;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPROPERTY;

/*******************************************
            RGXGetLastDeviceError
 *******************************************/

/* Bridge in structure for RGXGetLastDeviceError */
typedef struct PVRSRV_BRIDGE_IN_RGXGETLASTDEVICEERROR_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_RGXGETLASTDEVICEERROR;

/* Bridge out structure for RGXGetLastDeviceError */
typedef struct PVRSRV_BRIDGE_OUT_RGXGETLASTDEVICEERROR_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32Error;
} __packed PVRSRV_BRIDGE_OUT_RGXGETLASTDEVICEERROR;

/*******************************************
            RGXKickTimestampQuery
 *******************************************/

/* Bridge in structure for RGXKickTimestampQuery */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKTIMESTAMPQUERY_TAG
{
	IMG_HANDLE hComputeContext;
	IMG_BYTE *pui8DMCmd;
	PVRSRV_FENCE hCheckFenceFd;
	IMG_UINT32 ui32CmdSize;
	IMG_UINT32 ui32ExtJobRef;
} __packed PVRSRV_BRIDGE_IN_RGXKICKTIMESTAMPQUERY;

/* Bridge out structure for RGXKickTimestampQuery */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKTIMESTAMPQUERY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXKICKTIMESTAMPQUERY;

#endif /* COMMON_RGXCMP_BRIDGE_H */
