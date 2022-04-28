/*************************************************************************/ /*!
@File
@Title          Wrapper around drm_fourcc.h
@Description    FourCCs and DRM framebuffer modifiers that are not in the
                Kernel's and libdrm's drm_fourcc.h can be added here.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        MIT

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ /**************************************************************************/

#ifndef IMG_DRM_FOURCC_H
#define IMG_DRM_FOURCC_H

#if defined(__KERNEL__)
#include <drm/drm_fourcc.h>
#else
/*
 * Include types.h to workaround versions of libdrm older than 2.4.68
 * not including the correct headers.
 */
#include <linux/types.h>

#include <drm_fourcc.h>
#endif

/*
 * Don't get too inspired by this example :)
 * ADF doesn't support DRM modifiers, so the memory layout had to be
 * included in the fourcc name, but the proper way to specify information
 * additional to pixel formats is to use DRM modifiers.
 *
 * See upstream drm_fourcc.h for the proper naming convention.
 */
#ifndef DRM_FORMAT_BGRA8888_DIRECT_16x4
#define DRM_FORMAT_BGRA8888_DIRECT_16x4 fourcc_code('I', 'M', 'G', '0')
#endif

#if !defined(__KERNEL__)
/*
 * A definition for the same format was added in Linux kernel 5.2 in commit
 * 88ab9c76d191ad8645b483f31e2b394b0f3e280e. As such, this definition has been
 * deprecated and the DRM_FORMAT_ABGR16161616F kernel define should be used
 * instead of this one.
 */
#define DRM_FORMAT_ABGR16_IMG_DEPRECATED fourcc_code('I', 'M', 'G', '1')
#endif

/*
 * Upstream does not have a packed 10 Bits Per Channel YVU format yet,
 * so let`s make one up.
 * Note: at the moment this format is not intended to be used with
 *       a framebuffer, so the kernels core DRM doesn`t need to know
 *       about this format. This means that the kernel doesn`t need
 *       to be patched.
 */
#if !defined(__KERNEL__)
#define DRM_FORMAT_YVU444_PACK10_IMG fourcc_code('I', 'M', 'G', '2')
#define DRM_FORMAT_YUV422_2PLANE_PACK10_IMG fourcc_code('I', 'M', 'G', '3')
#define DRM_FORMAT_YUV420_2PLANE_PACK10_IMG fourcc_code('I', 'M', 'G', '4')
#endif

/*
 * Value chosen in the middle of 255 pool to minimise the chance of hitting
 * the same value potentially defined by other vendors in the drm_fourcc.h
 */
#define DRM_FORMAT_MOD_VENDOR_PVR 0x92

#ifndef DRM_FORMAT_MOD_VENDOR_NONE
#define DRM_FORMAT_MOD_VENDOR_NONE 0
#endif

#ifndef DRM_FORMAT_RESERVED
#define DRM_FORMAT_RESERVED ((1ULL << 56) - 1)
#endif

#define img_fourcc_mod_combine(uiModHi, uiModLo) \
	((__u64) ((__u32) (uiModHi)) << 32 | (__u64) ((__u32) (uiModLo)))

#define img_fourcc_mod_hi(ui64Mod) \
	((__u32) ((__u64) (ui64Mod) >> 32))

#define img_fourcc_mod_lo(ui64Mod) \
	((__u32) ((__u64) (ui64Mod)) & 0xffffffff)

#ifndef fourcc_mod_code
#define fourcc_mod_code(vendor, val) \
	((((__u64)DRM_FORMAT_MOD_VENDOR_## vendor) << 56) | (val & 0x00ffffffffffffffULL))
#endif

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID fourcc_mod_code(NONE, DRM_FORMAT_RESERVED)
#endif

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR fourcc_mod_code(NONE, 0)
#endif

#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V7      fourcc_mod_code(PVR, 6)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V7     fourcc_mod_code(PVR, 12)

#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V10     fourcc_mod_code(PVR, 21)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V10    fourcc_mod_code(PVR, 22)
#define DRM_FORMAT_MOD_PVR_FBCDC_32x2_V10    fourcc_mod_code(PVR, 23)

#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V12     fourcc_mod_code(PVR, 15)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V12    fourcc_mod_code(PVR, 16)

#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V13             fourcc_mod_code(PVR, 24)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY25_8x8_V13     fourcc_mod_code(PVR, 25)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY50_8x8_V13     fourcc_mod_code(PVR, 26)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY75_8x8_V13     fourcc_mod_code(PVR, 27)

#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V13            fourcc_mod_code(PVR, 28)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY25_16x4_V13    fourcc_mod_code(PVR, 29)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY50_16x4_V13    fourcc_mod_code(PVR, 30)
#define DRM_FORMAT_MOD_PVR_FBCDC_LOSSY75_16x4_V13    fourcc_mod_code(PVR, 31)

#endif /* IMG_DRM_FOURCC_H */
