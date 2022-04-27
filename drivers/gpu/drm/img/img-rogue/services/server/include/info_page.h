/*************************************************************************/ /*!
@File
@Title          Kernel/User mode general purpose shared memory.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    General purpose memory shared between kernel driver and user
                mode.
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

#ifndef INFO_PAGE_KM_H
#define INFO_PAGE_KM_H

#include "pvrsrv_error.h"

#include "pmr.h"
#include "pvrsrv.h"
#include "info_page_defs.h"

/**
 * @Function InfoPageCreate
 * @Description Allocates resources for global information page.
 * @Input psData pointer to PVRSRV data
 * @Return PVRSRV_OK on success and other PVRSRV_ERROR code on error.
 */
PVRSRV_ERROR InfoPageCreate(PVRSRV_DATA *psData);

/**
 * @Function InfoPageDestroy
 * @Description Frees all of the resource of global information page.
 * @Input psData pointer to PVRSRV data
 * @Return PVRSRV_OK on success and other PVRSRV_ERROR code on error.
 */
void InfoPageDestroy(PVRSRV_DATA *psData);

/**
 * @Function PVRSRVAcquireInfoPageKM()
 * @Description This interface is used for obtaining the global information page
 *              which acts as a general purpose shared memory between KM and UM.
 *              The use of this information page outside of services is _not_
 *              recommended.
 * @Output ppsPMR handle to exported PMR
 * @Return
 */
PVRSRV_ERROR PVRSRVAcquireInfoPageKM(PMR **ppsPMR);

/**
 * @Function PVRSRVReleaseInfoPageKM()
 * @Description This function matches PVRSRVAcquireInfoPageKM().
 * @Input psPMR handle to exported PMR
 * @Return PVRSRV_OK on success and other PVRSRV_ERROR code on error.
 */
PVRSRV_ERROR PVRSRVReleaseInfoPageKM(PMR *psPMR);

/**
 * @Function GetInfoPageDebugFlagsKM()
 * @Description Return info page debug flags
 * @Return info page debug flags
 */
static INLINE IMG_UINT32 GetInfoPageDebugFlagsKM(void)
{
	return (PVRSRVGetPVRSRVData())->pui32InfoPage[DEBUG_FEATURE_FLAGS];
}

#endif /* INFO_PAGE_KM_H */
