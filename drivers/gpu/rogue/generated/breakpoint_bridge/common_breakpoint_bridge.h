/*************************************************************************/ /*!
@File
@Title          Common bridge header for breakpoint
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for breakpoint
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

#ifndef COMMON_BREAKPOINT_BRIDGE_H
#define COMMON_BREAKPOINT_BRIDGE_H

#include "rgx_bridge.h"


#include "pvr_bridge_io.h"

#define PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST			(PVRSRV_BRIDGE_BREAKPOINT_START)
#define PVRSRV_BRIDGE_BREAKPOINT_RGXSETBREAKPOINT			PVRSRV_IOWR(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+0)
#define PVRSRV_BRIDGE_BREAKPOINT_RGXCLEARBREAKPOINT			PVRSRV_IOWR(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+1)
#define PVRSRV_BRIDGE_BREAKPOINT_RGXENABLEBREAKPOINT			PVRSRV_IOWR(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+2)
#define PVRSRV_BRIDGE_BREAKPOINT_RGXDISABLEBREAKPOINT			PVRSRV_IOWR(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+3)
#define PVRSRV_BRIDGE_BREAKPOINT_RGXOVERALLOCATEBPREGISTERS			PVRSRV_IOWR(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+4)
#define PVRSRV_BRIDGE_BREAKPOINT_CMD_LAST			(PVRSRV_BRIDGE_BREAKPOINT_CMD_FIRST+4)


/*******************************************
            RGXSetBreakpoint          
 *******************************************/

/* Bridge in structure for RGXSetBreakpoint */
typedef struct PVRSRV_BRIDGE_IN_RGXSETBREAKPOINT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_HANDLE hPrivData;
	IMG_UINT32 eFWDataMaster;
	IMG_UINT32 ui32BreakpointAddr;
	IMG_UINT32 ui32HandlerAddr;
	IMG_UINT32 ui32DM;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXSETBREAKPOINT;


/* Bridge out structure for RGXSetBreakpoint */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETBREAKPOINT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXSETBREAKPOINT;

/*******************************************
            RGXClearBreakpoint          
 *******************************************/

/* Bridge in structure for RGXClearBreakpoint */
typedef struct PVRSRV_BRIDGE_IN_RGXCLEARBREAKPOINT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCLEARBREAKPOINT;


/* Bridge out structure for RGXClearBreakpoint */
typedef struct PVRSRV_BRIDGE_OUT_RGXCLEARBREAKPOINT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCLEARBREAKPOINT;

/*******************************************
            RGXEnableBreakpoint          
 *******************************************/

/* Bridge in structure for RGXEnableBreakpoint */
typedef struct PVRSRV_BRIDGE_IN_RGXENABLEBREAKPOINT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXENABLEBREAKPOINT;


/* Bridge out structure for RGXEnableBreakpoint */
typedef struct PVRSRV_BRIDGE_OUT_RGXENABLEBREAKPOINT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXENABLEBREAKPOINT;

/*******************************************
            RGXDisableBreakpoint          
 *******************************************/

/* Bridge in structure for RGXDisableBreakpoint */
typedef struct PVRSRV_BRIDGE_IN_RGXDISABLEBREAKPOINT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDISABLEBREAKPOINT;


/* Bridge out structure for RGXDisableBreakpoint */
typedef struct PVRSRV_BRIDGE_OUT_RGXDISABLEBREAKPOINT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDISABLEBREAKPOINT;

/*******************************************
            RGXOverallocateBPRegisters          
 *******************************************/

/* Bridge in structure for RGXOverallocateBPRegisters */
typedef struct PVRSRV_BRIDGE_IN_RGXOVERALLOCATEBPREGISTERS_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32TempRegs;
	IMG_UINT32 ui32SharedRegs;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXOVERALLOCATEBPREGISTERS;


/* Bridge out structure for RGXOverallocateBPRegisters */
typedef struct PVRSRV_BRIDGE_OUT_RGXOVERALLOCATEBPREGISTERS_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXOVERALLOCATEBPREGISTERS;

#endif /* COMMON_BREAKPOINT_BRIDGE_H */
