/*******************************************************************************
@File
@Title          Common bridge header for rgxfwdbg
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxfwdbg
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

#ifndef COMMON_RGXFWDBG_BRIDGE_H
#define COMMON_RGXFWDBG_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "devicemem_typedefs.h"
#include "rgx_bridge.h"
#include "pvrsrv_memallocflags.h"

#define PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGSETFWLOG			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGDUMPFREELISTPAGELIST			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGSETHCSDEADLINE			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGSETOSIDPRIORITY			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGSETOSNEWONLINESTATE			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGPHRCONFIGURE			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXFWDBG_RGXFWDEBUGWDGCONFIGURE			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXFWDBG_RGXCURRENTTIME			PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+7
#define PVRSRV_BRIDGE_RGXFWDBG_CMD_LAST			(PVRSRV_BRIDGE_RGXFWDBG_CMD_FIRST+7)

/*******************************************
            RGXFWDebugSetFWLog
 *******************************************/

/* Bridge in structure for RGXFWDebugSetFWLog */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGSETFWLOG_TAG
{
	IMG_UINT32 ui32RGXFWLogType;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGSETFWLOG;

/* Bridge out structure for RGXFWDebugSetFWLog */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETFWLOG_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETFWLOG;

/*******************************************
            RGXFWDebugDumpFreelistPageList
 *******************************************/

/* Bridge in structure for RGXFWDebugDumpFreelistPageList */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGDUMPFREELISTPAGELIST_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGDUMPFREELISTPAGELIST;

/* Bridge out structure for RGXFWDebugDumpFreelistPageList */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGDUMPFREELISTPAGELIST_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGDUMPFREELISTPAGELIST;

/*******************************************
            RGXFWDebugSetHCSDeadline
 *******************************************/

/* Bridge in structure for RGXFWDebugSetHCSDeadline */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGSETHCSDEADLINE_TAG
{
	IMG_UINT32 ui32RGXHCSDeadline;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGSETHCSDEADLINE;

/* Bridge out structure for RGXFWDebugSetHCSDeadline */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETHCSDEADLINE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETHCSDEADLINE;

/*******************************************
            RGXFWDebugSetOSidPriority
 *******************************************/

/* Bridge in structure for RGXFWDebugSetOSidPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGSETOSIDPRIORITY_TAG
{
	IMG_UINT32 ui32OSid;
	IMG_UINT32 ui32Priority;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGSETOSIDPRIORITY;

/* Bridge out structure for RGXFWDebugSetOSidPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETOSIDPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETOSIDPRIORITY;

/*******************************************
            RGXFWDebugSetOSNewOnlineState
 *******************************************/

/* Bridge in structure for RGXFWDebugSetOSNewOnlineState */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGSETOSNEWONLINESTATE_TAG
{
	IMG_UINT32 ui32OSNewState;
	IMG_UINT32 ui32OSid;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGSETOSNEWONLINESTATE;

/* Bridge out structure for RGXFWDebugSetOSNewOnlineState */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETOSNEWONLINESTATE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGSETOSNEWONLINESTATE;

/*******************************************
            RGXFWDebugPHRConfigure
 *******************************************/

/* Bridge in structure for RGXFWDebugPHRConfigure */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGPHRCONFIGURE_TAG
{
	IMG_UINT32 ui32ui32PHRMode;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGPHRCONFIGURE;

/* Bridge out structure for RGXFWDebugPHRConfigure */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGPHRCONFIGURE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGPHRCONFIGURE;

/*******************************************
            RGXFWDebugWdgConfigure
 *******************************************/

/* Bridge in structure for RGXFWDebugWdgConfigure */
typedef struct PVRSRV_BRIDGE_IN_RGXFWDEBUGWDGCONFIGURE_TAG
{
	IMG_UINT32 ui32ui32WdgPeriodUs;
} __packed PVRSRV_BRIDGE_IN_RGXFWDEBUGWDGCONFIGURE;

/* Bridge out structure for RGXFWDebugWdgConfigure */
typedef struct PVRSRV_BRIDGE_OUT_RGXFWDEBUGWDGCONFIGURE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXFWDEBUGWDGCONFIGURE;

/*******************************************
            RGXCurrentTime
 *******************************************/

/* Bridge in structure for RGXCurrentTime */
typedef struct PVRSRV_BRIDGE_IN_RGXCURRENTTIME_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_RGXCURRENTTIME;

/* Bridge out structure for RGXCurrentTime */
typedef struct PVRSRV_BRIDGE_OUT_RGXCURRENTTIME_TAG
{
	IMG_UINT64 ui64Time;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCURRENTTIME;

#endif /* COMMON_RGXFWDBG_BRIDGE_H */
