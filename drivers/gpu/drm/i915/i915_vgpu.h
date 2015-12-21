/*
 * Copyright(c) 2011-2015 Intel Corporation. All rights reserved.
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
 */

#ifndef _I915_VGPU_H_
#define _I915_VGPU_H_

/* The MMIO offset of the shared info between guest and host emulator */
#define VGT_PVINFO_PAGE	0x78000
#define VGT_PVINFO_SIZE	0x1000

/*
 * The following structure pages are defined in GEN MMIO space
 * for virtualization. (One page for now)
 */
#define VGT_MAGIC         0x4776544776544776ULL	/* 'vGTvGTvG' */
#define VGT_VERSION_MAJOR 1
#define VGT_VERSION_MINOR 0

#define INTEL_VGT_IF_VERSION_ENCODE(major, minor) ((major) << 16 | (minor))
#define INTEL_VGT_IF_VERSION \
	INTEL_VGT_IF_VERSION_ENCODE(VGT_VERSION_MAJOR, VGT_VERSION_MINOR)

/*
 * notifications from guest to vgpu device model
 */
enum vgt_g2v_type {
	VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE = 2,
	VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY,
	VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE,
	VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY,
	VGT_G2V_EXECLIST_CONTEXT_CREATE,
	VGT_G2V_EXECLIST_CONTEXT_DESTROY,
	VGT_G2V_MAX,
};

struct vgt_if {
	uint64_t magic;		/* VGT_MAGIC */
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t vgt_id;	/* ID of vGT instance */
	uint32_t rsv1[12];	/* pad to offset 0x40 */
	/*
	 *  Data structure to describe the balooning info of resources.
	 *  Each VM can only have one portion of continuous area for now.
	 *  (May support scattered resource in future)
	 *  (starting from offset 0x40)
	 */
	struct {
		/* Aperture register balooning */
		struct {
			uint32_t base;
			uint32_t size;
		} mappable_gmadr;	/* aperture */
		/* GMADR register balooning */
		struct {
			uint32_t base;
			uint32_t size;
		} nonmappable_gmadr;	/* non aperture */
		/* allowed fence registers */
		uint32_t fence_num;
		uint32_t rsv2[3];
	} avail_rs;		/* available/assigned resource */
	uint32_t rsv3[0x200 - 24];	/* pad to half page */
	/*
	 * The bottom half page is for response from Gfx driver to hypervisor.
	 */
	uint32_t rsv4;
	uint32_t display_ready;	/* ready for display owner switch */

	uint32_t rsv5[4];

	uint32_t g2v_notify;
	uint32_t rsv6[7];

	struct {
		uint32_t lo;
		uint32_t hi;
	} pdp[4];

	uint32_t execlist_context_descriptor_lo;
	uint32_t execlist_context_descriptor_hi;

	uint32_t  rsv7[0x200 - 24];    /* pad to one page */
} __packed;

#define vgtif_reg(x) \
	_MMIO((VGT_PVINFO_PAGE + (long)&((struct vgt_if *)NULL)->x))

/* vGPU display status to be used by the host side */
#define VGT_DRV_DISPLAY_NOT_READY 0
#define VGT_DRV_DISPLAY_READY     1  /* ready for display switch */

extern void i915_check_vgpu(struct drm_device *dev);
extern int intel_vgt_balloon(struct drm_device *dev);
extern void intel_vgt_deballoon(void);

#endif /* _I915_VGPU_H_ */
