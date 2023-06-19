/*******************************************************************************
@File
@Title          Common bridge header for syncfallback
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for syncfallback
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

#ifndef COMMON_SYNCFALLBACK_BRIDGE_H
#define COMMON_SYNCFALLBACK_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST			0
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBTIMELINECREATEPVR			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+0
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBTIMELINERELEASE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+1
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEDUP			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+2
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEMERGE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+3
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCERELEASE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+4
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEWAIT			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+5
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEDUMP			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+6
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBTIMELINECREATESW			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+7
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCECREATESW			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+8
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBTIMELINEADVANCESW			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+9
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEEXPORTINSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+10
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEEXPORTDESTROYINSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+11
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEIMPORTINSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+12
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEEXPORTSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+13
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEEXPORTDESTROYSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+14
#define PVRSRV_BRIDGE_SYNCFALLBACK_SYNCFBFENCEIMPORTSECURE			PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+15
#define PVRSRV_BRIDGE_SYNCFALLBACK_CMD_LAST			(PVRSRV_BRIDGE_SYNCFALLBACK_CMD_FIRST+15)

/*******************************************
            SyncFbTimelineCreatePVR
 *******************************************/

/* Bridge in structure for SyncFbTimelineCreatePVR */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBTIMELINECREATEPVR_TAG
{
	const IMG_CHAR *puiTimelineName;
	IMG_UINT32 ui32TimelineNameSize;
} __packed PVRSRV_BRIDGE_IN_SYNCFBTIMELINECREATEPVR;

/* Bridge out structure for SyncFbTimelineCreatePVR */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBTIMELINECREATEPVR_TAG
{
	IMG_HANDLE hTimeline;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBTIMELINECREATEPVR;

/*******************************************
            SyncFbTimelineRelease
 *******************************************/

/* Bridge in structure for SyncFbTimelineRelease */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBTIMELINERELEASE_TAG
{
	IMG_HANDLE hTimeline;
} __packed PVRSRV_BRIDGE_IN_SYNCFBTIMELINERELEASE;

/* Bridge out structure for SyncFbTimelineRelease */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBTIMELINERELEASE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBTIMELINERELEASE;

/*******************************************
            SyncFbFenceDup
 *******************************************/

/* Bridge in structure for SyncFbFenceDup */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEDUP_TAG
{
	IMG_HANDLE hInFence;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEDUP;

/* Bridge out structure for SyncFbFenceDup */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEDUP_TAG
{
	IMG_HANDLE hOutFence;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEDUP;

/*******************************************
            SyncFbFenceMerge
 *******************************************/

/* Bridge in structure for SyncFbFenceMerge */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEMERGE_TAG
{
	IMG_HANDLE hInFence1;
	IMG_HANDLE hInFence2;
	const IMG_CHAR *puiFenceName;
	IMG_UINT32 ui32FenceNameSize;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEMERGE;

/* Bridge out structure for SyncFbFenceMerge */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEMERGE_TAG
{
	IMG_HANDLE hOutFence;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEMERGE;

/*******************************************
            SyncFbFenceRelease
 *******************************************/

/* Bridge in structure for SyncFbFenceRelease */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCERELEASE_TAG
{
	IMG_HANDLE hFence;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCERELEASE;

/* Bridge out structure for SyncFbFenceRelease */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCERELEASE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCERELEASE;

/*******************************************
            SyncFbFenceWait
 *******************************************/

/* Bridge in structure for SyncFbFenceWait */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEWAIT_TAG
{
	IMG_HANDLE hFence;
	IMG_UINT32 ui32Timeout;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEWAIT;

/* Bridge out structure for SyncFbFenceWait */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEWAIT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEWAIT;

/*******************************************
            SyncFbFenceDump
 *******************************************/

/* Bridge in structure for SyncFbFenceDump */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEDUMP_TAG
{
	IMG_HANDLE hFence;
	const IMG_CHAR *puiDescStr;
	const IMG_CHAR *puiFileStr;
	const IMG_CHAR *puiModuleStr;
	IMG_UINT32 ui32DescStrLength;
	IMG_UINT32 ui32FileStrLength;
	IMG_UINT32 ui32Line;
	IMG_UINT32 ui32ModuleStrLength;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEDUMP;

/* Bridge out structure for SyncFbFenceDump */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEDUMP_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEDUMP;

/*******************************************
            SyncFbTimelineCreateSW
 *******************************************/

/* Bridge in structure for SyncFbTimelineCreateSW */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBTIMELINECREATESW_TAG
{
	const IMG_CHAR *puiTimelineName;
	IMG_UINT32 ui32TimelineNameSize;
} __packed PVRSRV_BRIDGE_IN_SYNCFBTIMELINECREATESW;

/* Bridge out structure for SyncFbTimelineCreateSW */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBTIMELINECREATESW_TAG
{
	IMG_HANDLE hTimeline;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBTIMELINECREATESW;

/*******************************************
            SyncFbFenceCreateSW
 *******************************************/

/* Bridge in structure for SyncFbFenceCreateSW */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCECREATESW_TAG
{
	IMG_HANDLE hTimeline;
	const IMG_CHAR *puiFenceName;
	IMG_UINT32 ui32FenceNameSize;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCECREATESW;

/* Bridge out structure for SyncFbFenceCreateSW */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCECREATESW_TAG
{
	IMG_UINT64 ui64SyncPtIdx;
	IMG_HANDLE hOutFence;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCECREATESW;

/*******************************************
            SyncFbTimelineAdvanceSW
 *******************************************/

/* Bridge in structure for SyncFbTimelineAdvanceSW */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBTIMELINEADVANCESW_TAG
{
	IMG_HANDLE hTimeline;
} __packed PVRSRV_BRIDGE_IN_SYNCFBTIMELINEADVANCESW;

/* Bridge out structure for SyncFbTimelineAdvanceSW */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBTIMELINEADVANCESW_TAG
{
	IMG_UINT64 ui64SyncPtIdx;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBTIMELINEADVANCESW;

/*******************************************
            SyncFbFenceExportInsecure
 *******************************************/

/* Bridge in structure for SyncFbFenceExportInsecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTINSECURE_TAG
{
	IMG_HANDLE hFence;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTINSECURE;

/* Bridge out structure for SyncFbFenceExportInsecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTINSECURE_TAG
{
	IMG_HANDLE hExport;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTINSECURE;

/*******************************************
            SyncFbFenceExportDestroyInsecure
 *******************************************/

/* Bridge in structure for SyncFbFenceExportDestroyInsecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTDESTROYINSECURE_TAG
{
	IMG_HANDLE hExport;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTDESTROYINSECURE;

/* Bridge out structure for SyncFbFenceExportDestroyInsecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTDESTROYINSECURE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTDESTROYINSECURE;

/*******************************************
            SyncFbFenceImportInsecure
 *******************************************/

/* Bridge in structure for SyncFbFenceImportInsecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEIMPORTINSECURE_TAG
{
	IMG_HANDLE hImport;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEIMPORTINSECURE;

/* Bridge out structure for SyncFbFenceImportInsecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEIMPORTINSECURE_TAG
{
	IMG_HANDLE hSyncHandle;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEIMPORTINSECURE;

/*******************************************
            SyncFbFenceExportSecure
 *******************************************/

/* Bridge in structure for SyncFbFenceExportSecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTSECURE_TAG
{
	IMG_HANDLE hFence;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTSECURE;

/* Bridge out structure for SyncFbFenceExportSecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTSECURE_TAG
{
	IMG_SECURE_TYPE Export;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTSECURE;

/*******************************************
            SyncFbFenceExportDestroySecure
 *******************************************/

/* Bridge in structure for SyncFbFenceExportDestroySecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTDESTROYSECURE_TAG
{
	IMG_HANDLE hExport;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEEXPORTDESTROYSECURE;

/* Bridge out structure for SyncFbFenceExportDestroySecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTDESTROYSECURE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEEXPORTDESTROYSECURE;

/*******************************************
            SyncFbFenceImportSecure
 *******************************************/

/* Bridge in structure for SyncFbFenceImportSecure */
typedef struct PVRSRV_BRIDGE_IN_SYNCFBFENCEIMPORTSECURE_TAG
{
	IMG_SECURE_TYPE Import;
} __packed PVRSRV_BRIDGE_IN_SYNCFBFENCEIMPORTSECURE;

/* Bridge out structure for SyncFbFenceImportSecure */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFBFENCEIMPORTSECURE_TAG
{
	IMG_HANDLE hSyncHandle;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFBFENCEIMPORTSECURE;

#endif /* COMMON_SYNCFALLBACK_BRIDGE_H */
