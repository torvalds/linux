/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __DRM_NULLDISP_NETLINK_H__
#define __DRM_NULLDISP_NETLINK_H__

#include <linux/types.h>
#include <linux/version.h>

typedef void (*nlpvrdpy_disconnect_cb)(void *data);
typedef int (*nlpvrdpy_flipped_cb)(void *data);
typedef int (*nlpvrdpy_copied_cb)(void *data);

struct nlpvrdpy *nlpvrdpy_create(struct drm_device *dev,
					nlpvrdpy_disconnect_cb disconnect_cb,
					void *disconnect_cb_data,
					nlpvrdpy_flipped_cb flipped_cb,
					void *flipped_cb_data,
					nlpvrdpy_copied_cb copied_cb,
					void *copied_cb_data);

void nlpvrdpy_destroy(struct nlpvrdpy *nlpvrdpy);

int nlpvrdpy_send_flip(struct nlpvrdpy *nlpvrdpy,
		       struct drm_framebuffer *fb,
		       struct drm_gem_object **objs);

int nlpvrdpy_send_copy(struct nlpvrdpy *nlpvrdpy,
		       struct drm_framebuffer *fb,
		       struct drm_gem_object **objs);

void nlpvrdpy_send_disconnect(struct nlpvrdpy *nlpvrdpy);

int nlpvrdpy_register(void);

int nlpvrdpy_unregister(void);

#endif /* __DRM_NULLDISP_NETLINK_H__ */
