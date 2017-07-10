/*************************************************************************/ /*!
@File           adf_ext.h
@Title          IMG extension ioctls and ioctl packages for ADF
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
/* vi: set ts=8: */

#ifndef __ADF_EXT_H__
#define __ADF_EXT_H__

#include <drm/drm.h>

#define ADF_BUFFER_TRANSFORM_NONE_EXT		(0 << 0)
#define ADF_BUFFER_TRANSFORM_FLIP_H_EXT		(1 << 0)
#define ADF_BUFFER_TRANSFORM_FLIP_V_EXT		(1 << 1)
#define ADF_BUFFER_TRANSFORM_ROT_90_EXT		(1 << 2)
#define ADF_BUFFER_TRANSFORM_ROT_180_EXT	((1 << 0) + (1 << 1))
#define ADF_BUFFER_TRANSFORM_ROT_270_EXT	((1 << 0) + (1 << 1) + (1 << 2))

#define ADF_BUFFER_BLENDING_NONE_EXT		0
#define ADF_BUFFER_BLENDING_PREMULT_EXT		1
#define ADF_BUFFER_BLENDING_COVERAGE_EXT	2

struct adf_buffer_config_ext {
	/* Crop applied to surface (BEFORE transformation) */
	struct drm_clip_rect	crop;

	/* Region of screen to display surface in (AFTER scaling) */
	struct drm_clip_rect	display;

	/* Surface rotation / flip / mirror */
	__u32			transform;

	/* Alpha blending mode e.g. none / premult / coverage */
	__u32			blend_type;

	/* Plane alpha */
	__u8			plane_alpha;
	__u8			reserved[3];
} __packed;

struct adf_post_ext {
	__u32	post_id;
	struct adf_buffer_config_ext bufs_ext[];
} __packed;

struct adf_validate_config_ext {
	__u32 n_interfaces;
	__u32 __user *interfaces;

	__u32 n_bufs;

	struct adf_buffer_config __user *bufs;
	struct adf_post_ext __user *post_ext;
} __packed;

#define ADF_IOCTL_NR_VALIDATE_IMG (ADF_IOCTL_NR_CUSTOM + 0)

#define ADF_VALIDATE_CONFIG_EXT \
	_IOW(ADF_IOCTL_TYPE, ADF_IOCTL_NR_VALIDATE_IMG, \
		struct adf_validate_config_ext)

#endif /* __ADF_EXT_H__ */
