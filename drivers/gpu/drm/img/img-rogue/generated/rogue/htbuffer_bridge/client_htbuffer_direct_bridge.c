/*******************************************************************************
@File
@Title          Direct client bridge for htbuffer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the client side of the bridge for htbuffer
                which is used in calls from Server context.
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

#include "client_htbuffer_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "devicemem_typedefs.h"
#include "htbuffer_types.h"

#include "htbserver.h"

IMG_INTERNAL PVRSRV_ERROR BridgeHTBControl(IMG_HANDLE hBridge,
					   IMG_UINT32 ui32NumGroups,
					   IMG_UINT32 * pui32GroupEnable,
					   IMG_UINT32 ui32LogLevel,
					   IMG_UINT32 ui32EnablePID,
					   IMG_UINT32 ui32LogMode, IMG_UINT32 ui32OpMode)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	eError =
	    HTBControlKM(ui32NumGroups,
			 pui32GroupEnable, ui32LogLevel, ui32EnablePID, ui32LogMode, ui32OpMode);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeHTBLog(IMG_HANDLE hBridge,
				       IMG_UINT32 ui32PID,
				       IMG_UINT32 ui32TID,
				       IMG_UINT64 ui64TimeStamp,
				       IMG_UINT32 ui32SF,
				       IMG_UINT32 ui32NumArgs, IMG_UINT32 * pui32Args)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	eError = HTBLogKM(ui32PID, ui32TID, ui64TimeStamp, ui32SF, ui32NumArgs, pui32Args);

	return eError;
}
