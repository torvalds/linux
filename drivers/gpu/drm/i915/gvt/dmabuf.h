/*
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Xiaoguang Chen
 *    Tina Zhang <tina.zhang@intel.com>
 */

#ifndef _GVT_DMABUF_H_
#define _GVT_DMABUF_H_
#include <linux/vfio.h>

struct intel_vgpu_fb_info {
	__u64 start;
	__u64 start_gpa;
	__u64 drm_format_mod;
	__u32 drm_format;	/* drm format of plane */
	__u32 width;	/* width of plane */
	__u32 height;	/* height of plane */
	__u32 stride;	/* stride of plane */
	__u32 size;	/* size of plane in bytes, align on page */
	__u32 x_pos;	/* horizontal position of cursor plane */
	__u32 y_pos;	/* vertical position of cursor plane */
	__u32 x_hot;    /* horizontal position of cursor hotspot */
	__u32 y_hot;    /* vertical position of cursor hotspot */
	struct intel_vgpu_dmabuf_obj *obj;
};

/**
 * struct intel_vgpu_dmabuf_obj- Intel vGPU device buffer object
 */
struct intel_vgpu_dmabuf_obj {
	struct intel_vgpu *vgpu;
	struct intel_vgpu_fb_info *info;
	__u32 dmabuf_id;
	struct kref kref;
	bool initref;
	struct list_head list;
};

int intel_vgpu_query_plane(struct intel_vgpu *vgpu, void *args);
int intel_vgpu_get_dmabuf(struct intel_vgpu *vgpu, unsigned int dmabuf_id);
void intel_vgpu_dmabuf_cleanup(struct intel_vgpu *vgpu);

#endif
