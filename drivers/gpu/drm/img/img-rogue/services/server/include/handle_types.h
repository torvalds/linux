/**************************************************************************/ /*!
@File
@Title          Handle Manager handle types
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provide handle management
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
*/ /***************************************************************************/
/* NOTE: Do not add include guards to this file */

HANDLETYPE(NONE)
HANDLETYPE(SHARED_EVENT_OBJECT)
HANDLETYPE(EVENT_OBJECT_CONNECT)
HANDLETYPE(PMR_LOCAL_EXPORT_HANDLE)
HANDLETYPE(PHYSMEM_PMR)
HANDLETYPE(PHYSMEM_PMR_EXPORT)
HANDLETYPE(PHYSMEM_PMR_SECURE_EXPORT)
HANDLETYPE(DEVMEMINT_CTX)
HANDLETYPE(DEVMEMINT_CTX_EXPORT)
HANDLETYPE(DEVMEMINT_HEAP)
HANDLETYPE(DEVMEMINT_RESERVATION)
HANDLETYPE(DEVMEMXINT_RESERVATION)
HANDLETYPE(DEVMEMINT_MAPPING)
HANDLETYPE(RGX_FW_MEMDESC)
HANDLETYPE(RGX_FREELIST)
HANDLETYPE(RGX_MEMORY_BLOCK)
HANDLETYPE(RGX_SERVER_RENDER_CONTEXT)
HANDLETYPE(RGX_SERVER_TQ_CONTEXT)
HANDLETYPE(RGX_SERVER_TQ_TDM_CONTEXT)
HANDLETYPE(RGX_SERVER_COMPUTE_CONTEXT)
HANDLETYPE(RGX_SERVER_RAY_CONTEXT)
HANDLETYPE(RGX_SERVER_KICKSYNC_CONTEXT)
#if defined(PVR_TESTING_UTILS) && defined(SUPPORT_VALIDATION)
HANDLETYPE(RGX_SERVER_GPUMAP_CONTEXT)
#endif
HANDLETYPE(SYNC_PRIMITIVE_BLOCK)
HANDLETYPE(SYNC_RECORD_HANDLE)
HANDLETYPE(PVRSRV_TIMELINE_SERVER)
HANDLETYPE(PVRSRV_FENCE_SERVER)
HANDLETYPE(PVRSRV_FENCE_EXPORT)
HANDLETYPE(RGX_KM_HW_RT_DATASET)
HANDLETYPE(RGX_FWIF_ZSBUFFER)
HANDLETYPE(RGX_POPULATION)
HANDLETYPE(DC_DEVICE)
HANDLETYPE(DC_DISPLAY_CONTEXT)
HANDLETYPE(DC_BUFFER)
HANDLETYPE(DC_PIN_HANDLE)
HANDLETYPE(DEVMEM_MEM_IMPORT)
HANDLETYPE(PHYSMEM_PMR_PAGELIST)
HANDLETYPE(PVR_TL_SD)
HANDLETYPE(RI_HANDLE)
HANDLETYPE(DEV_PRIV_DATA)
HANDLETYPE(MM_PLAT_CLEANUP)
HANDLETYPE(WORKEST_RETURN_DATA)
HANDLETYPE(DI_CONTEXT)
