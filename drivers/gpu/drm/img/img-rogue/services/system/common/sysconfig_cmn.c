/*************************************************************************/ /*!
@File
@Title          Sysconfig layer common to all platforms
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements system layer functions common to all platforms
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

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#include "pvr_debug.h"

void SysRGXErrorNotify(IMG_HANDLE hSysData,
                       PVRSRV_ROBUSTNESS_NOTIFY_DATA *psErrorData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

#if defined(PVRSRV_NEED_PVR_DPF)
	{
		IMG_UINT32 ui32DgbLvl;

		switch (psErrorData->eResetReason)
		{
			case RGX_CONTEXT_RESET_REASON_NONE:
			case RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP:
			case RGX_CONTEXT_RESET_REASON_INNOCENT_LOCKUP:
			case RGX_CONTEXT_RESET_REASON_GUILTY_OVERRUNING:
			case RGX_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING:
			case RGX_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH:
			case RGX_CONTEXT_RESET_REASON_GPU_ECC_OK:
			case RGX_CONTEXT_RESET_REASON_FW_ECC_OK:
			{
				ui32DgbLvl = PVR_DBG_MESSAGE;
				break;
			}
			case RGX_CONTEXT_RESET_REASON_GPU_ECC_HWR:
			case RGX_CONTEXT_RESET_REASON_FW_EXEC_ERR:
			{
				ui32DgbLvl = PVR_DBG_WARNING;
				break;
			}
			case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_FW_ECC_ERR:
			case RGX_CONTEXT_RESET_REASON_FW_WATCHDOG:
			case RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT:
			case RGX_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR:
			{
				ui32DgbLvl = PVR_DBG_ERROR;
				break;
			}
			default:
			{
				PVR_ASSERT(false && "Unhandled reset reason");
				ui32DgbLvl = PVR_DBG_ERROR;
				break;
			}
		}

		if (psErrorData->pid > 0)
		{
			PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, " PID %d experienced error %d",
					 psErrorData->pid, psErrorData->eResetReason);
		}
		else
		{
			PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, " Device experienced error %d",
					 psErrorData->eResetReason);
		}

		switch (psErrorData->eResetReason)
		{
			case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
			{
				PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, "   ExtJobRef 0x%x, DM %d",
						 psErrorData->uErrData.sChecksumErrData.ui32ExtJobRef,
						 psErrorData->uErrData.sChecksumErrData.eDM);
			break;
			}
			default:
			{
				break;
			}
		}
	}
#else
	PVR_UNREFERENCED_PARAMETER(psErrorData);
#endif /* PVRSRV_NEED_PVR_DPF */
}

/******************************************************************************
 End of file (sysconfig_cmn.c)
******************************************************************************/
