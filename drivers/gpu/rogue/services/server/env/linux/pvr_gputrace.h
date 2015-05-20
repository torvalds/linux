/*************************************************************************/ /*!
@File           pvr_gputrace.h
@Title          PVR GPU Trace module common environment interface
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

#ifndef PVR_GPUTRACE_H_
#define PVR_GPUTRACE_H_

#include "img_types.h"


/******************************************************************************
 Module out-bound API
******************************************************************************/

/*
  The device layer of the KM driver defines these two APIs to allow a
  platform module to set and retrieve the feature's on/off state.
*/
extern void PVRGpuTraceEnabledSet(IMG_BOOL bNewValue);
extern IMG_BOOL PVRGpuTraceEnabled(void);


/******************************************************************************
 Module In-bound API
******************************************************************************/

typedef enum {
	PVR_GPUTRACE_SWITCH_TYPE_UNDEF = 0,

	PVR_GPUTRACE_SWITCH_TYPE_BEGIN = 1,
	PVR_GPUTRACE_SWITCH_TYPE_END = 2

} PVR_GPUTRACE_SWITCH_TYPE;


void PVRGpuTraceClientWork(
		const IMG_UINT32 ui32Pid,
		const IMG_UINT32 ui32ExtJobRef,
		const IMG_UINT32 ui32IntJobRef,
		const IMG_CHAR* pszKickType);


void PVRGpuTraceWorkSwitch(
		IMG_UINT64 ui64OSTimestamp,
		const IMG_UINT32 ui32Pid,
		const IMG_UINT32 ui32ExtJobRef,
		const IMG_UINT32 ui32IntJobRef,
		const IMG_CHAR* pszWorkType,
		PVR_GPUTRACE_SWITCH_TYPE eSwType);


PVRSRV_ERROR PVRGpuTraceInit(void);


void PVRGpuTraceDeInit(void);


#endif /* PVR_GPUTRACE_H_ */
