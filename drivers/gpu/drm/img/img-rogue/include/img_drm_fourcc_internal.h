/*************************************************************************/ /*!
@File
@Title          Wrapper around drm_fourcc.h
@Description    FourCCs and the DRM framebuffer modifiers should be added here
                unless they are used by kernel code or a known user outside of
                the DDK. If FourCCs or DRM framebuffer modifiers are required
                outside of the DDK, they shall be moved to the corresponding
                public header.
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

#ifndef IMG_DRM_FOURCC_INTERNAL_H
#define IMG_DRM_FOURCC_INTERNAL_H

#include <powervr/img_drm_fourcc.h>

/*
 * Modifier names are structured using the following convention,
 * with underscores (_) between items:
 * - prefix: DRM_FORMAT_MOD
 * - identifier for our driver: PVR
 * - category: FBCDC
 *   - compression tile dimension: 8x8, 16x4, 32x2
 *   - FBDC version: V0, V1, V2, V3, V7, V8, V10, V12
 */
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V0      fourcc_mod_code(PVR, 1)
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V0_FIX  fourcc_mod_code(PVR, 2) /* Fix for HW_BRN_37464 */
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V1      fourcc_mod_code(PVR, 3)
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V2      fourcc_mod_code(PVR, 4)
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V3      fourcc_mod_code(PVR, 5)
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_V7 - moved to the public header */
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V8      fourcc_mod_code(PVR, 18)
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_V10 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_V12 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_LOSSY25_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_LOSSY50_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_8x8_LOSSY75_V13 - moved to the public header */
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V0     fourcc_mod_code(PVR, 7)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V0_FIX fourcc_mod_code(PVR, 8) /* Fix for HW_BRN_37464 */
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V1     fourcc_mod_code(PVR, 9)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V2     fourcc_mod_code(PVR, 10)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V3     fourcc_mod_code(PVR, 11)
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_V7 - moved to the public header */
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V8     fourcc_mod_code(PVR, 19)
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_V10 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_V12 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_LOSSY25_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_LOSSY50_V13 - moved to the public header */
/* DRM_FORMAT_MOD_PVR_FBCDC_16x4_LOSSY75_V13 - moved to the public header */
#define DRM_FORMAT_MOD_PVR_FBCDC_32x2_V1     fourcc_mod_code(PVR, 13)
#define DRM_FORMAT_MOD_PVR_FBCDC_32x2_V3     fourcc_mod_code(PVR, 14)
#define DRM_FORMAT_MOD_PVR_FBCDC_32x2_V8     fourcc_mod_code(PVR, 20)
/* DRM_FORMAT_MOD_PVR_FBCDC_32x2_V10 - moved to the public header */
#define DRM_FORMAT_MOD_PVR_FBCDC_32x2_V12    fourcc_mod_code(PVR, 17)

#endif /* IMG_DRM_FOURCC_INTERNAL_H */
