/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ovly.h"

static const u32
ovly917e_format[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR16161616F,
	0
};

int
ovly917e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly907e, ovly917e_format, drm, head, oclass,
			     0x00000004 << (head * 4), pwndw);
}
