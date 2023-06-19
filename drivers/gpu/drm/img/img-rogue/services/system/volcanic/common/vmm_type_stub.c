/*************************************************************************/ /*!
@File			vmm_type_stub.c
@Title          Stub VM manager type
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Sample stub (no-operation) VM manager implementation
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
#include "pvrsrv.h"
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "rgxheapconfig.h"

#include "vmm_impl.h"
#include "vmm_pvz_server.h"

static PVRSRV_ERROR
StubVMMMapDevPhysHeap(IMG_UINT64 ui64Size,
					  IMG_UINT64 ui64Addr)
{
	PVR_UNREFERENCED_PARAMETER(ui64Size);
	PVR_UNREFERENCED_PARAMETER(ui64Addr);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static PVRSRV_ERROR
StubVMMUnmapDevPhysHeap(void)
{
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
}

static VMM_PVZ_CONNECTION gsStubVmmPvz =
{
	.sClientFuncTab = {
		/* pfnMapDevPhysHeap */
		&StubVMMMapDevPhysHeap,

		/* pfnUnmapDevPhysHeap */
		&StubVMMUnmapDevPhysHeap
	},

	.sServerFuncTab = {
		/* pfnMapDevPhysHeap */
		&PvzServerMapDevPhysHeap,

		/* pfnUnmapDevPhysHeap */
		&PvzServerUnmapDevPhysHeap
	},

	.sVmmFuncTab = {
		/* pfnOnVmOnline */
		&PvzServerOnVmOnline,

		/* pfnOnVmOffline */
		&PvzServerOnVmOffline,

		/* pfnVMMConfigure */
		&PvzServerVMMConfigure
	}
};

PVRSRV_ERROR VMMCreatePvzConnection(VMM_PVZ_CONNECTION **psPvzConnection)
{
	PVR_LOG_RETURN_IF_FALSE((NULL != psPvzConnection), "VMMCreatePvzConnection", PVRSRV_ERROR_INVALID_PARAMS);
	*psPvzConnection = &gsStubVmmPvz;
	PVR_DPF((PVR_DBG_ERROR, "Using a stub VM manager type, no runtime VZ support"));
	return PVRSRV_OK;
}

void VMMDestroyPvzConnection(VMM_PVZ_CONNECTION *psPvzConnection)
{
	PVR_LOG_IF_FALSE((NULL != psPvzConnection), "VMMDestroyPvzConnection");
}

/******************************************************************************
 End of file (vmm_type_stub.c)
******************************************************************************/
