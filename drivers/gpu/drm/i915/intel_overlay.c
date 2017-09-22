/*
 * Copyright © 2009
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
 *    Daniel Vetter <daniel@ffwll.ch>
 *
 * Derived from Xorg ddx, xf86-video-intel, src/i830_video.c
 */
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"

/* Limits for overlay size. According to intel doc, the real limits are:
 * Y width: 4095, UV width (planar): 2047, Y height: 2047,
 * UV width (planar): * 1023. But the xorg thinks 2048 for height and width. Use
 * the mininum of both.  */
#define IMAGE_MAX_WIDTH		2048
#define IMAGE_MAX_HEIGHT	2046 /* 2 * 1023 */
/* on 830 and 845 these large limits result in the card hanging */
#define IMAGE_MAX_WIDTH_LEGACY	1024
#define IMAGE_MAX_HEIGHT_LEGACY	1088

/* overlay register definitions */
/* OCMD register */
#define OCMD_TILED_SURFACE	(0x1<<19)
#define OCMD_MIRROR_MASK	(0x3<<17)
#define OCMD_MIRROR_MODE	(0x3<<17)
#define OCMD_MIRROR_HORIZONTAL	(0x1<<17)
#define OCMD_MIRROR_VERTICAL	(0x2<<17)
#define OCMD_MIRROR_BOTH	(0x3<<17)
#define OCMD_BYTEORDER_MASK	(0x3<<14) /* zero for YUYV or FOURCC YUY2 */
#define OCMD_UV_SWAP		(0x1<<14) /* YVYU */
#define OCMD_Y_SWAP		(0x2<<14) /* UYVY or FOURCC UYVY */
#define OCMD_Y_AND_UV_SWAP	(0x3<<14) /* VYUY */
#define OCMD_SOURCE_FORMAT_MASK (0xf<<10)
#define OCMD_RGB_888		(0x1<<10) /* not in i965 Intel docs */
#define OCMD_RGB_555		(0x2<<10) /* not in i965 Intel docs */
#define OCMD_RGB_565		(0x3<<10) /* not in i965 Intel docs */
#define OCMD_YUV_422_PACKED	(0x8<<10)
#define OCMD_YUV_411_PACKED	(0x9<<10) /* not in i965 Intel docs */
#define OCMD_YUV_420_PLANAR	(0xc<<10)
#define OCMD_YUV_422_PLANAR	(0xd<<10)
#define OCMD_YUV_410_PLANAR	(0xe<<10) /* also 411 */
#define OCMD_TVSYNCFLIP_PARITY	(0x1<<9)
#define OCMD_TVSYNCFLIP_ENABLE	(0x1<<7)
#define OCMD_BUF_TYPE_MASK	(0x1<<5)
#define OCMD_BUF_TYPE_FRAME	(0x0<<5)
#define OCMD_BUF_TYPE_FIELD	(0x1<<5)
#define OCMD_TEST_MODE		(0x1<<4)
#define OCMD_BUFFER_SELECT	(0x3<<2)
#define OCMD_BUFFER0		(0x0<<2)
#define OCMD_BUFFER1		(0x1<<2)
#define OCMD_FIELD_SELECT	(0x1<<2)
#define OCMD_FIELD0		(0x0<<1)
#define OCMD_FIELD1		(0x1<<1)
#define OCMD_ENABLE		(0x1<<0)

/* OCONFIG register */
#define OCONF_PIPE_MASK		(0x1<<18)
#define OCONF_PIPE_A		(0x0<<18)
#define OCONF_PIPE_B		(0x1<<18)
#define OCONF_GAMMA2_ENABLE	(0x1<<16)
#define OCONF_CSC_MODE_BT601	(0x0<<5)
#define OCONF_CSC_MODE_BT709	(0x1<<5)
#define OCONF_CSC_BYPASS	(0x1<<4)
#define OCONF_CC_OUT_8BIT	(0x1<<3)
#define OCONF_TEST_MODE		(0x1<<2)
#define OCONF_THREE_LINE_BUFFER	(0x1<<0)
#define OCONF_TWO_LINE_BUFFER	(0x0<<0)

/* DCLRKM (dst-key) register */
#define DST_KEY_ENABLE		(0x1<<31)
#define CLK_RGB24_MASK		0x0
#define CLK_RGB16_MASK		0x070307
#define CLK_RGB15_MASK		0x070707
#define CLK_RGB8I_MASK		0xffffff

#define RGB16_TO_COLORKEY(c) \
	(((c & 0xF800) << 8) | ((c & 0x07E0) << 5) | ((c & 0x001F) << 3))
#define RGB15_TO_COLORKEY(c) \
	(((c & 0x7c00) << 9) | ((c & 0x03E0) << 6) | ((c & 0x001F) << 3))

/* overlay flip addr flag */
#define OFC_UPDATE		0x1

/* polyphase filter coefficients */
#define N_HORIZ_Y_TAPS          5
#define N_VERT_Y_TAPS           3
#define N_HORIZ_UV_TAPS         3
#define N_VERT_UV_TAPS          3
#define N_PHASES                17
#define MAX_TAPS                5

/* memory bufferd overlay registers */
struct overlay_registers {
	u32 OBUF_0Y;
	u32 OBUF_1Y;
	u32 OBUF_0U;
	u32 OBUF_0V;
	u32 OBUF_1U;
	u32 OBUF_1V;
	u32 OSTRIDE;
	u32 YRGB_VPH;
	u32 UV_VPH;
	u32 HORZ_PH;
	u32 INIT_PHS;
	u32 DWINPOS;
	u32 DWINSZ;
	u32 SWIDTH;
	u32 SWIDTHSW;
	u32 SHEIGHT;
	u32 YRGBSCALE;
	u32 UVSCALE;
	u32 OCLRC0;
	u32 OCLRC1;
	u32 DCLRKV;
	u32 DCLRKM;
	u32 SCLRKVH;
	u32 SCLRKVL;
	u32 SCLRKEN;
	u32 OCONFIG;
	u32 OCMD;
	u32 RESERVED1; /* 0x6C */
	u32 OSTART_0Y;
	u32 OSTART_1Y;
	u32 OSTART_0U;
	u32 OSTART_0V;
	u32 OSTART_1U;
	u32 OSTART_1V;
	u32 OTILEOFF_0Y;
	u32 OTILEOFF_1Y;
	u32 OTILEOFF_0U;
	u32 OTILEOFF_0V;
	u32 OTILEOFF_1U;
	u32 OTILEOFF_1V;
	u32 FASTHSCALE; /* 0xA0 */
	u32 UVSCALEV; /* 0xA4 */
	u32 RESERVEDC[(0x200 - 0xA8) / 4]; /* 0xA8 - 0x1FC */
	u16 Y_VCOEFS[N_VERT_Y_TAPS * N_PHASES]; /* 0x200 */
	u16 RESERVEDD[0x100 / 2 - N_VERT_Y_TAPS * N_PHASES];
	u16 Y_HCOEFS[N_HORIZ_Y_TAPS * N_PHASES]; /* 0x300 */
	u16 RESERVEDE[0x200 / 2 - N_HORIZ_Y_TAPS * N_PHASES];
	u16 UV_VCOEFS[N_VERT_UV_TAPS * N_PHASES]; /* 0x500 */
	u16 RESERVEDF[0x100 / 2 - N_VERT_UV_TAPS * N_PHASES];
	u16 UV_HCOEFS[N_HORIZ_UV_TAPS * N_PHASES]; /* 0x600 */
	u16 RESERVEDG[0x100 / 2 - N_HORIZ_UV_TAPS * N_PHASES];
};

struct intel_overlay {
	struct drm_i915_private *i915;
	struct intel_crtc *crtc;
	struct i915_vma *vma;
	struct i915_vma *old_vma;
	bool active;
	bool pfit_active;
	u32 pfit_vscale_ratio; /* shifted-point number, (1<<12) == 1.0 */
	u32 color_key:24;
	u32 color_key_enabled:1;
	u32 brightness, contrast, saturation;
	u32 old_xscale, old_yscale;
	/* register access */
	u32 flip_addr;
	struct drm_i915_gem_object *reg_bo;
	/* flip handling */
	struct i915_gem_active last_flip;
};

static void i830_overlay_clock_gating(struct drm_i915_private *dev_priv,
				      bool enable)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	u8 val;

	/* WA_OVERLAY_CLKGATE:alm */
	if (enable)
		I915_WRITE(DSPCLK_GATE_D, 0);
	else
		I915_WRITE(DSPCLK_GATE_D, OVRUNIT_CLOCK_GATE_DISABLE);

	/* WA_DISABLE_L2CACHE_CLOCK_GATING:alm */
	pci_bus_read_config_byte(pdev->bus,
				 PCI_DEVFN(0, 0), I830_CLOCK_GATE, &val);
	if (enable)
		val &= ~I830_L2_CACHE_CLOCK_GATE_DISABLE;
	else
		val |= I830_L2_CACHE_CLOCK_GATE_DISABLE;
	pci_bus_write_config_byte(pdev->bus,
				  PCI_DEVFN(0, 0), I830_CLOCK_GATE, val);
}

static struct overlay_registers __iomem *
intel_overlay_map_regs(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct overlay_registers __iomem *regs;

	if (OVERLAY_NEEDS_PHYSICAL(dev_priv))
		regs = (struct overlay_registers __iomem *)overlay->reg_bo->phys_handle->vaddr;
	else
		regs = io_mapping_map_wc(&dev_priv->ggtt.mappable,
					 overlay->flip_addr,
					 PAGE_SIZE);

	return regs;
}

static void intel_overlay_unmap_regs(struct intel_overlay *overlay,
				     struct overlay_registers __iomem *regs)
{
	if (!OVERLAY_NEEDS_PHYSICAL(overlay->i915))
		io_mapping_unmap(regs);
}

static void intel_overlay_submit_request(struct intel_overlay *overlay,
					 struct drm_i915_gem_request *req,
					 i915_gem_retire_fn retire)
{
	GEM_BUG_ON(i915_gem_active_peek(&overlay->last_flip,
					&overlay->i915->drm.struct_mutex));
	i915_gem_active_set_retire_fn(&overlay->last_flip, retire,
				      &overlay->i915->drm.struct_mutex);
	i915_gem_active_set(&overlay->last_flip, req);
	i915_add_request(req);
}

static int intel_overlay_do_wait_request(struct intel_overlay *overlay,
					 struct drm_i915_gem_request *req,
					 i915_gem_retire_fn retire)
{
	intel_overlay_submit_request(overlay, req, retire);
	return i915_gem_active_retire(&overlay->last_flip,
				      &overlay->i915->drm.struct_mutex);
}

static struct drm_i915_gem_request *alloc_request(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct intel_engine_cs *engine = dev_priv->engine[RCS];

	return i915_gem_request_alloc(engine, dev_priv->kernel_context);
}

/* overlay needs to be disable in OCMD reg */
static int intel_overlay_on(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct drm_i915_gem_request *req;
	u32 *cs;

	WARN_ON(overlay->active);

	req = alloc_request(overlay);
	if (IS_ERR(req))
		return PTR_ERR(req);

	cs = intel_ring_begin(req, 4);
	if (IS_ERR(cs)) {
		i915_add_request(req);
		return PTR_ERR(cs);
	}

	overlay->active = true;

	if (IS_I830(dev_priv))
		i830_overlay_clock_gating(dev_priv, false);

	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_ON;
	*cs++ = overlay->flip_addr | OFC_UPDATE;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;
	*cs++ = MI_NOOP;
	intel_ring_advance(req, cs);

	return intel_overlay_do_wait_request(overlay, req, NULL);
}

static void intel_overlay_flip_prepare(struct intel_overlay *overlay,
				       struct i915_vma *vma)
{
	enum pipe pipe = overlay->crtc->pipe;

	WARN_ON(overlay->old_vma);

	i915_gem_track_fb(overlay->vma ? overlay->vma->obj : NULL,
			  vma ? vma->obj : NULL,
			  INTEL_FRONTBUFFER_OVERLAY(pipe));

	intel_frontbuffer_flip_prepare(overlay->i915,
				       INTEL_FRONTBUFFER_OVERLAY(pipe));

	overlay->old_vma = overlay->vma;
	if (vma)
		overlay->vma = i915_vma_get(vma);
	else
		overlay->vma = NULL;
}

/* overlay needs to be enabled in OCMD reg */
static int intel_overlay_continue(struct intel_overlay *overlay,
				  struct i915_vma *vma,
				  bool load_polyphase_filter)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct drm_i915_gem_request *req;
	u32 flip_addr = overlay->flip_addr;
	u32 tmp, *cs;

	WARN_ON(!overlay->active);

	if (load_polyphase_filter)
		flip_addr |= OFC_UPDATE;

	/* check for underruns */
	tmp = I915_READ(DOVSTA);
	if (tmp & (1 << 17))
		DRM_DEBUG("overlay underrun, DOVSTA: %x\n", tmp);

	req = alloc_request(overlay);
	if (IS_ERR(req))
		return PTR_ERR(req);

	cs = intel_ring_begin(req, 2);
	if (IS_ERR(cs)) {
		i915_add_request(req);
		return PTR_ERR(cs);
	}

	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_CONTINUE;
	*cs++ = flip_addr;
	intel_ring_advance(req, cs);

	intel_overlay_flip_prepare(overlay, vma);

	intel_overlay_submit_request(overlay, req, NULL);

	return 0;
}

static void intel_overlay_release_old_vma(struct intel_overlay *overlay)
{
	struct i915_vma *vma;

	vma = fetch_and_zero(&overlay->old_vma);
	if (WARN_ON(!vma))
		return;

	intel_frontbuffer_flip_complete(overlay->i915,
					INTEL_FRONTBUFFER_OVERLAY(overlay->crtc->pipe));

	i915_gem_object_unpin_from_display_plane(vma);
	i915_vma_put(vma);
}

static void intel_overlay_release_old_vid_tail(struct i915_gem_active *active,
					       struct drm_i915_gem_request *req)
{
	struct intel_overlay *overlay =
		container_of(active, typeof(*overlay), last_flip);

	intel_overlay_release_old_vma(overlay);
}

static void intel_overlay_off_tail(struct i915_gem_active *active,
				   struct drm_i915_gem_request *req)
{
	struct intel_overlay *overlay =
		container_of(active, typeof(*overlay), last_flip);
	struct drm_i915_private *dev_priv = overlay->i915;

	intel_overlay_release_old_vma(overlay);

	overlay->crtc->overlay = NULL;
	overlay->crtc = NULL;
	overlay->active = false;

	if (IS_I830(dev_priv))
		i830_overlay_clock_gating(dev_priv, true);
}

/* overlay needs to be disabled in OCMD reg */
static int intel_overlay_off(struct intel_overlay *overlay)
{
	struct drm_i915_gem_request *req;
	u32 *cs, flip_addr = overlay->flip_addr;

	WARN_ON(!overlay->active);

	/* According to intel docs the overlay hw may hang (when switching
	 * off) without loading the filter coeffs. It is however unclear whether
	 * this applies to the disabling of the overlay or to the switching off
	 * of the hw. Do it in both cases */
	flip_addr |= OFC_UPDATE;

	req = alloc_request(overlay);
	if (IS_ERR(req))
		return PTR_ERR(req);

	cs = intel_ring_begin(req, 6);
	if (IS_ERR(cs)) {
		i915_add_request(req);
		return PTR_ERR(cs);
	}

	/* wait for overlay to go idle */
	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_CONTINUE;
	*cs++ = flip_addr;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;

	/* turn overlay off */
	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_OFF;
	*cs++ = flip_addr;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;

	intel_ring_advance(req, cs);

	intel_overlay_flip_prepare(overlay, NULL);

	return intel_overlay_do_wait_request(overlay, req,
					     intel_overlay_off_tail);
}

/* recover from an interruption due to a signal
 * We have to be careful not to repeat work forever an make forward progess. */
static int intel_overlay_recover_from_interrupt(struct intel_overlay *overlay)
{
	return i915_gem_active_retire(&overlay->last_flip,
				      &overlay->i915->drm.struct_mutex);
}

/* Wait for pending overlay flip and release old frame.
 * Needs to be called before the overlay register are changed
 * via intel_overlay_(un)map_regs
 */
static int intel_overlay_release_old_vid(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	u32 *cs;
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	/* Only wait if there is actually an old frame to release to
	 * guarantee forward progress.
	 */
	if (!overlay->old_vma)
		return 0;

	if (I915_READ(ISR) & I915_OVERLAY_PLANE_FLIP_PENDING_INTERRUPT) {
		/* synchronous slowpath */
		struct drm_i915_gem_request *req;

		req = alloc_request(overlay);
		if (IS_ERR(req))
			return PTR_ERR(req);

		cs = intel_ring_begin(req, 2);
		if (IS_ERR(cs)) {
			i915_add_request(req);
			return PTR_ERR(cs);
		}

		*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;
		*cs++ = MI_NOOP;
		intel_ring_advance(req, cs);

		ret = intel_overlay_do_wait_request(overlay, req,
						    intel_overlay_release_old_vid_tail);
		if (ret)
			return ret;
	} else
		intel_overlay_release_old_vid_tail(&overlay->last_flip, NULL);

	return 0;
}

void intel_overlay_reset(struct drm_i915_private *dev_priv)
{
	struct intel_overlay *overlay = dev_priv->overlay;

	if (!overlay)
		return;

	intel_overlay_release_old_vid(overlay);

	overlay->old_xscale = 0;
	overlay->old_yscale = 0;
	overlay->crtc = NULL;
	overlay->active = false;
}

struct put_image_params {
	int format;
	short dst_x;
	short dst_y;
	short dst_w;
	short dst_h;
	short src_w;
	short src_scan_h;
	short src_scan_w;
	short src_h;
	short stride_Y;
	short stride_UV;
	int offset_Y;
	int offset_U;
	int offset_V;
};

static int packed_depth_bytes(u32 format)
{
	switch (format & I915_OVERLAY_DEPTH_MASK) {
	case I915_OVERLAY_YUV422:
		return 4;
	case I915_OVERLAY_YUV411:
		/* return 6; not implemented */
	default:
		return -EINVAL;
	}
}

static int packed_width_bytes(u32 format, short width)
{
	switch (format & I915_OVERLAY_DEPTH_MASK) {
	case I915_OVERLAY_YUV422:
		return width << 1;
	default:
		return -EINVAL;
	}
}

static int uv_hsubsampling(u32 format)
{
	switch (format & I915_OVERLAY_DEPTH_MASK) {
	case I915_OVERLAY_YUV422:
	case I915_OVERLAY_YUV420:
		return 2;
	case I915_OVERLAY_YUV411:
	case I915_OVERLAY_YUV410:
		return 4;
	default:
		return -EINVAL;
	}
}

static int uv_vsubsampling(u32 format)
{
	switch (format & I915_OVERLAY_DEPTH_MASK) {
	case I915_OVERLAY_YUV420:
	case I915_OVERLAY_YUV410:
		return 2;
	case I915_OVERLAY_YUV422:
	case I915_OVERLAY_YUV411:
		return 1;
	default:
		return -EINVAL;
	}
}

static u32 calc_swidthsw(struct drm_i915_private *dev_priv, u32 offset, u32 width)
{
	u32 sw;

	if (IS_GEN2(dev_priv))
		sw = ALIGN((offset & 31) + width, 32);
	else
		sw = ALIGN((offset & 63) + width, 64);

	if (sw == 0)
		return 0;

	return (sw - 32) >> 3;
}

static const u16 y_static_hcoeffs[N_PHASES][N_HORIZ_Y_TAPS] = {
	[ 0] = { 0x3000, 0xb4a0, 0x1930, 0x1920, 0xb4a0, },
	[ 1] = { 0x3000, 0xb500, 0x19d0, 0x1880, 0xb440, },
	[ 2] = { 0x3000, 0xb540, 0x1a88, 0x2f80, 0xb3e0, },
	[ 3] = { 0x3000, 0xb580, 0x1b30, 0x2e20, 0xb380, },
	[ 4] = { 0x3000, 0xb5c0, 0x1bd8, 0x2cc0, 0xb320, },
	[ 5] = { 0x3020, 0xb5e0, 0x1c60, 0x2b80, 0xb2c0, },
	[ 6] = { 0x3020, 0xb5e0, 0x1cf8, 0x2a20, 0xb260, },
	[ 7] = { 0x3020, 0xb5e0, 0x1d80, 0x28e0, 0xb200, },
	[ 8] = { 0x3020, 0xb5c0, 0x1e08, 0x3f40, 0xb1c0, },
	[ 9] = { 0x3020, 0xb580, 0x1e78, 0x3ce0, 0xb160, },
	[10] = { 0x3040, 0xb520, 0x1ed8, 0x3aa0, 0xb120, },
	[11] = { 0x3040, 0xb4a0, 0x1f30, 0x3880, 0xb0e0, },
	[12] = { 0x3040, 0xb400, 0x1f78, 0x3680, 0xb0a0, },
	[13] = { 0x3020, 0xb340, 0x1fb8, 0x34a0, 0xb060, },
	[14] = { 0x3020, 0xb240, 0x1fe0, 0x32e0, 0xb040, },
	[15] = { 0x3020, 0xb140, 0x1ff8, 0x3160, 0xb020, },
	[16] = { 0xb000, 0x3000, 0x0800, 0x3000, 0xb000, },
};

static const u16 uv_static_hcoeffs[N_PHASES][N_HORIZ_UV_TAPS] = {
	[ 0] = { 0x3000, 0x1800, 0x1800, },
	[ 1] = { 0xb000, 0x18d0, 0x2e60, },
	[ 2] = { 0xb000, 0x1990, 0x2ce0, },
	[ 3] = { 0xb020, 0x1a68, 0x2b40, },
	[ 4] = { 0xb040, 0x1b20, 0x29e0, },
	[ 5] = { 0xb060, 0x1bd8, 0x2880, },
	[ 6] = { 0xb080, 0x1c88, 0x3e60, },
	[ 7] = { 0xb0a0, 0x1d28, 0x3c00, },
	[ 8] = { 0xb0c0, 0x1db8, 0x39e0, },
	[ 9] = { 0xb0e0, 0x1e40, 0x37e0, },
	[10] = { 0xb100, 0x1eb8, 0x3620, },
	[11] = { 0xb100, 0x1f18, 0x34a0, },
	[12] = { 0xb100, 0x1f68, 0x3360, },
	[13] = { 0xb0e0, 0x1fa8, 0x3240, },
	[14] = { 0xb0c0, 0x1fe0, 0x3140, },
	[15] = { 0xb060, 0x1ff0, 0x30a0, },
	[16] = { 0x3000, 0x0800, 0x3000, },
};

static void update_polyphase_filter(struct overlay_registers __iomem *regs)
{
	memcpy_toio(regs->Y_HCOEFS, y_static_hcoeffs, sizeof(y_static_hcoeffs));
	memcpy_toio(regs->UV_HCOEFS, uv_static_hcoeffs,
		    sizeof(uv_static_hcoeffs));
}

static bool update_scaling_factors(struct intel_overlay *overlay,
				   struct overlay_registers __iomem *regs,
				   struct put_image_params *params)
{
	/* fixed point with a 12 bit shift */
	u32 xscale, yscale, xscale_UV, yscale_UV;
#define FP_SHIFT 12
#define FRACT_MASK 0xfff
	bool scale_changed = false;
	int uv_hscale = uv_hsubsampling(params->format);
	int uv_vscale = uv_vsubsampling(params->format);

	if (params->dst_w > 1)
		xscale = ((params->src_scan_w - 1) << FP_SHIFT)
			/(params->dst_w);
	else
		xscale = 1 << FP_SHIFT;

	if (params->dst_h > 1)
		yscale = ((params->src_scan_h - 1) << FP_SHIFT)
			/(params->dst_h);
	else
		yscale = 1 << FP_SHIFT;

	/*if (params->format & I915_OVERLAY_YUV_PLANAR) {*/
	xscale_UV = xscale/uv_hscale;
	yscale_UV = yscale/uv_vscale;
	/* make the Y scale to UV scale ratio an exact multiply */
	xscale = xscale_UV * uv_hscale;
	yscale = yscale_UV * uv_vscale;
	/*} else {
	  xscale_UV = 0;
	  yscale_UV = 0;
	  }*/

	if (xscale != overlay->old_xscale || yscale != overlay->old_yscale)
		scale_changed = true;
	overlay->old_xscale = xscale;
	overlay->old_yscale = yscale;

	iowrite32(((yscale & FRACT_MASK) << 20) |
		  ((xscale >> FP_SHIFT)  << 16) |
		  ((xscale & FRACT_MASK) << 3),
		 &regs->YRGBSCALE);

	iowrite32(((yscale_UV & FRACT_MASK) << 20) |
		  ((xscale_UV >> FP_SHIFT)  << 16) |
		  ((xscale_UV & FRACT_MASK) << 3),
		 &regs->UVSCALE);

	iowrite32((((yscale    >> FP_SHIFT) << 16) |
		   ((yscale_UV >> FP_SHIFT) << 0)),
		 &regs->UVSCALEV);

	if (scale_changed)
		update_polyphase_filter(regs);

	return scale_changed;
}

static void update_colorkey(struct intel_overlay *overlay,
			    struct overlay_registers __iomem *regs)
{
	const struct intel_plane_state *state =
		to_intel_plane_state(overlay->crtc->base.primary->state);
	u32 key = overlay->color_key;
	u32 format = 0;
	u32 flags = 0;

	if (overlay->color_key_enabled)
		flags |= DST_KEY_ENABLE;

	if (state->base.visible)
		format = state->base.fb->format->format;

	switch (format) {
	case DRM_FORMAT_C8:
		key = 0;
		flags |= CLK_RGB8I_MASK;
		break;
	case DRM_FORMAT_XRGB1555:
		key = RGB15_TO_COLORKEY(key);
		flags |= CLK_RGB15_MASK;
		break;
	case DRM_FORMAT_RGB565:
		key = RGB16_TO_COLORKEY(key);
		flags |= CLK_RGB16_MASK;
		break;
	default:
		flags |= CLK_RGB24_MASK;
		break;
	}

	iowrite32(key, &regs->DCLRKV);
	iowrite32(flags, &regs->DCLRKM);
}

static u32 overlay_cmd_reg(struct put_image_params *params)
{
	u32 cmd = OCMD_ENABLE | OCMD_BUF_TYPE_FRAME | OCMD_BUFFER0;

	if (params->format & I915_OVERLAY_YUV_PLANAR) {
		switch (params->format & I915_OVERLAY_DEPTH_MASK) {
		case I915_OVERLAY_YUV422:
			cmd |= OCMD_YUV_422_PLANAR;
			break;
		case I915_OVERLAY_YUV420:
			cmd |= OCMD_YUV_420_PLANAR;
			break;
		case I915_OVERLAY_YUV411:
		case I915_OVERLAY_YUV410:
			cmd |= OCMD_YUV_410_PLANAR;
			break;
		}
	} else { /* YUV packed */
		switch (params->format & I915_OVERLAY_DEPTH_MASK) {
		case I915_OVERLAY_YUV422:
			cmd |= OCMD_YUV_422_PACKED;
			break;
		case I915_OVERLAY_YUV411:
			cmd |= OCMD_YUV_411_PACKED;
			break;
		}

		switch (params->format & I915_OVERLAY_SWAP_MASK) {
		case I915_OVERLAY_NO_SWAP:
			break;
		case I915_OVERLAY_UV_SWAP:
			cmd |= OCMD_UV_SWAP;
			break;
		case I915_OVERLAY_Y_SWAP:
			cmd |= OCMD_Y_SWAP;
			break;
		case I915_OVERLAY_Y_AND_UV_SWAP:
			cmd |= OCMD_Y_AND_UV_SWAP;
			break;
		}
	}

	return cmd;
}

static int intel_overlay_do_put_image(struct intel_overlay *overlay,
				      struct drm_i915_gem_object *new_bo,
				      struct put_image_params *params)
{
	int ret, tmp_width;
	struct overlay_registers __iomem *regs;
	bool scale_changed = false;
	struct drm_i915_private *dev_priv = overlay->i915;
	u32 swidth, swidthsw, sheight, ostride;
	enum pipe pipe = overlay->crtc->pipe;
	struct i915_vma *vma;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);
	WARN_ON(!drm_modeset_is_locked(&dev_priv->drm.mode_config.connection_mutex));

	ret = intel_overlay_release_old_vid(overlay);
	if (ret != 0)
		return ret;

	atomic_inc(&dev_priv->gpu_error.pending_fb_pin);

	vma = i915_gem_object_pin_to_display_plane(new_bo, 0, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_pin_section;
	}

	ret = i915_vma_put_fence(vma);
	if (ret)
		goto out_unpin;

	if (!overlay->active) {
		u32 oconfig;
		regs = intel_overlay_map_regs(overlay);
		if (!regs) {
			ret = -ENOMEM;
			goto out_unpin;
		}
		oconfig = OCONF_CC_OUT_8BIT;
		if (IS_GEN4(dev_priv))
			oconfig |= OCONF_CSC_MODE_BT709;
		oconfig |= pipe == 0 ?
			OCONF_PIPE_A : OCONF_PIPE_B;
		iowrite32(oconfig, &regs->OCONFIG);
		intel_overlay_unmap_regs(overlay, regs);

		ret = intel_overlay_on(overlay);
		if (ret != 0)
			goto out_unpin;
	}

	regs = intel_overlay_map_regs(overlay);
	if (!regs) {
		ret = -ENOMEM;
		goto out_unpin;
	}

	iowrite32((params->dst_y << 16) | params->dst_x, &regs->DWINPOS);
	iowrite32((params->dst_h << 16) | params->dst_w, &regs->DWINSZ);

	if (params->format & I915_OVERLAY_YUV_PACKED)
		tmp_width = packed_width_bytes(params->format, params->src_w);
	else
		tmp_width = params->src_w;

	swidth = params->src_w;
	swidthsw = calc_swidthsw(dev_priv, params->offset_Y, tmp_width);
	sheight = params->src_h;
	iowrite32(i915_ggtt_offset(vma) + params->offset_Y, &regs->OBUF_0Y);
	ostride = params->stride_Y;

	if (params->format & I915_OVERLAY_YUV_PLANAR) {
		int uv_hscale = uv_hsubsampling(params->format);
		int uv_vscale = uv_vsubsampling(params->format);
		u32 tmp_U, tmp_V;
		swidth |= (params->src_w/uv_hscale) << 16;
		tmp_U = calc_swidthsw(dev_priv, params->offset_U,
				      params->src_w/uv_hscale);
		tmp_V = calc_swidthsw(dev_priv, params->offset_V,
				      params->src_w/uv_hscale);
		swidthsw |= max_t(u32, tmp_U, tmp_V) << 16;
		sheight |= (params->src_h/uv_vscale) << 16;
		iowrite32(i915_ggtt_offset(vma) + params->offset_U,
			  &regs->OBUF_0U);
		iowrite32(i915_ggtt_offset(vma) + params->offset_V,
			  &regs->OBUF_0V);
		ostride |= params->stride_UV << 16;
	}

	iowrite32(swidth, &regs->SWIDTH);
	iowrite32(swidthsw, &regs->SWIDTHSW);
	iowrite32(sheight, &regs->SHEIGHT);
	iowrite32(ostride, &regs->OSTRIDE);

	scale_changed = update_scaling_factors(overlay, regs, params);

	update_colorkey(overlay, regs);

	iowrite32(overlay_cmd_reg(params), &regs->OCMD);

	intel_overlay_unmap_regs(overlay, regs);

	ret = intel_overlay_continue(overlay, vma, scale_changed);
	if (ret)
		goto out_unpin;

	return 0;

out_unpin:
	i915_gem_object_unpin_from_display_plane(vma);
out_pin_section:
	atomic_dec(&dev_priv->gpu_error.pending_fb_pin);

	return ret;
}

int intel_overlay_switch_off(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct overlay_registers __iomem *regs;
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);
	WARN_ON(!drm_modeset_is_locked(&dev_priv->drm.mode_config.connection_mutex));

	ret = intel_overlay_recover_from_interrupt(overlay);
	if (ret != 0)
		return ret;

	if (!overlay->active)
		return 0;

	ret = intel_overlay_release_old_vid(overlay);
	if (ret != 0)
		return ret;

	regs = intel_overlay_map_regs(overlay);
	iowrite32(0, &regs->OCMD);
	intel_overlay_unmap_regs(overlay, regs);

	return intel_overlay_off(overlay);
}

static int check_overlay_possible_on_crtc(struct intel_overlay *overlay,
					  struct intel_crtc *crtc)
{
	if (!crtc->active)
		return -EINVAL;

	/* can't use the overlay with double wide pipe */
	if (crtc->config->double_wide)
		return -EINVAL;

	return 0;
}

static void update_pfit_vscale_ratio(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	u32 pfit_control = I915_READ(PFIT_CONTROL);
	u32 ratio;

	/* XXX: This is not the same logic as in the xorg driver, but more in
	 * line with the intel documentation for the i965
	 */
	if (INTEL_GEN(dev_priv) >= 4) {
		/* on i965 use the PGM reg to read out the autoscaler values */
		ratio = I915_READ(PFIT_PGM_RATIOS) >> PFIT_VERT_SCALE_SHIFT_965;
	} else {
		if (pfit_control & VERT_AUTO_SCALE)
			ratio = I915_READ(PFIT_AUTO_RATIOS);
		else
			ratio = I915_READ(PFIT_PGM_RATIOS);
		ratio >>= PFIT_VERT_SCALE_SHIFT;
	}

	overlay->pfit_vscale_ratio = ratio;
}

static int check_overlay_dst(struct intel_overlay *overlay,
			     struct drm_intel_overlay_put_image *rec)
{
	const struct intel_crtc_state *pipe_config =
		overlay->crtc->config;

	if (rec->dst_x < pipe_config->pipe_src_w &&
	    rec->dst_x + rec->dst_width <= pipe_config->pipe_src_w &&
	    rec->dst_y < pipe_config->pipe_src_h &&
	    rec->dst_y + rec->dst_height <= pipe_config->pipe_src_h)
		return 0;
	else
		return -EINVAL;
}

static int check_overlay_scaling(struct put_image_params *rec)
{
	u32 tmp;

	/* downscaling limit is 8.0 */
	tmp = ((rec->src_scan_h << 16) / rec->dst_h) >> 16;
	if (tmp > 7)
		return -EINVAL;
	tmp = ((rec->src_scan_w << 16) / rec->dst_w) >> 16;
	if (tmp > 7)
		return -EINVAL;

	return 0;
}

static int check_overlay_src(struct drm_i915_private *dev_priv,
			     struct drm_intel_overlay_put_image *rec,
			     struct drm_i915_gem_object *new_bo)
{
	int uv_hscale = uv_hsubsampling(rec->flags);
	int uv_vscale = uv_vsubsampling(rec->flags);
	u32 stride_mask;
	int depth;
	u32 tmp;

	/* check src dimensions */
	if (IS_I845G(dev_priv) || IS_I830(dev_priv)) {
		if (rec->src_height > IMAGE_MAX_HEIGHT_LEGACY ||
		    rec->src_width  > IMAGE_MAX_WIDTH_LEGACY)
			return -EINVAL;
	} else {
		if (rec->src_height > IMAGE_MAX_HEIGHT ||
		    rec->src_width  > IMAGE_MAX_WIDTH)
			return -EINVAL;
	}

	/* better safe than sorry, use 4 as the maximal subsampling ratio */
	if (rec->src_height < N_VERT_Y_TAPS*4 ||
	    rec->src_width  < N_HORIZ_Y_TAPS*4)
		return -EINVAL;

	/* check alignment constraints */
	switch (rec->flags & I915_OVERLAY_TYPE_MASK) {
	case I915_OVERLAY_RGB:
		/* not implemented */
		return -EINVAL;

	case I915_OVERLAY_YUV_PACKED:
		if (uv_vscale != 1)
			return -EINVAL;

		depth = packed_depth_bytes(rec->flags);
		if (depth < 0)
			return depth;

		/* ignore UV planes */
		rec->stride_UV = 0;
		rec->offset_U = 0;
		rec->offset_V = 0;
		/* check pixel alignment */
		if (rec->offset_Y % depth)
			return -EINVAL;
		break;

	case I915_OVERLAY_YUV_PLANAR:
		if (uv_vscale < 0 || uv_hscale < 0)
			return -EINVAL;
		/* no offset restrictions for planar formats */
		break;

	default:
		return -EINVAL;
	}

	if (rec->src_width % uv_hscale)
		return -EINVAL;

	/* stride checking */
	if (IS_I830(dev_priv) || IS_I845G(dev_priv))
		stride_mask = 255;
	else
		stride_mask = 63;

	if (rec->stride_Y & stride_mask || rec->stride_UV & stride_mask)
		return -EINVAL;
	if (IS_GEN4(dev_priv) && rec->stride_Y < 512)
		return -EINVAL;

	tmp = (rec->flags & I915_OVERLAY_TYPE_MASK) == I915_OVERLAY_YUV_PLANAR ?
		4096 : 8192;
	if (rec->stride_Y > tmp || rec->stride_UV > 2*1024)
		return -EINVAL;

	/* check buffer dimensions */
	switch (rec->flags & I915_OVERLAY_TYPE_MASK) {
	case I915_OVERLAY_RGB:
	case I915_OVERLAY_YUV_PACKED:
		/* always 4 Y values per depth pixels */
		if (packed_width_bytes(rec->flags, rec->src_width) > rec->stride_Y)
			return -EINVAL;

		tmp = rec->stride_Y*rec->src_height;
		if (rec->offset_Y + tmp > new_bo->base.size)
			return -EINVAL;
		break;

	case I915_OVERLAY_YUV_PLANAR:
		if (rec->src_width > rec->stride_Y)
			return -EINVAL;
		if (rec->src_width/uv_hscale > rec->stride_UV)
			return -EINVAL;

		tmp = rec->stride_Y * rec->src_height;
		if (rec->offset_Y + tmp > new_bo->base.size)
			return -EINVAL;

		tmp = rec->stride_UV * (rec->src_height / uv_vscale);
		if (rec->offset_U + tmp > new_bo->base.size ||
		    rec->offset_V + tmp > new_bo->base.size)
			return -EINVAL;
		break;
	}

	return 0;
}

int intel_overlay_put_image_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct drm_intel_overlay_put_image *put_image_rec = data;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_overlay *overlay;
	struct drm_crtc *drmmode_crtc;
	struct intel_crtc *crtc;
	struct drm_i915_gem_object *new_bo;
	struct put_image_params *params;
	int ret;

	overlay = dev_priv->overlay;
	if (!overlay) {
		DRM_DEBUG("userspace bug: no overlay\n");
		return -ENODEV;
	}

	if (!(put_image_rec->flags & I915_OVERLAY_ENABLE)) {
		drm_modeset_lock_all(dev);
		mutex_lock(&dev->struct_mutex);

		ret = intel_overlay_switch_off(overlay);

		mutex_unlock(&dev->struct_mutex);
		drm_modeset_unlock_all(dev);

		return ret;
	}

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	drmmode_crtc = drm_crtc_find(dev, put_image_rec->crtc_id);
	if (!drmmode_crtc) {
		ret = -ENOENT;
		goto out_free;
	}
	crtc = to_intel_crtc(drmmode_crtc);

	new_bo = i915_gem_object_lookup(file_priv, put_image_rec->bo_handle);
	if (!new_bo) {
		ret = -ENOENT;
		goto out_free;
	}

	drm_modeset_lock_all(dev);
	mutex_lock(&dev->struct_mutex);

	if (i915_gem_object_is_tiled(new_bo)) {
		DRM_DEBUG_KMS("buffer used for overlay image can not be tiled\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = intel_overlay_recover_from_interrupt(overlay);
	if (ret != 0)
		goto out_unlock;

	if (overlay->crtc != crtc) {
		ret = intel_overlay_switch_off(overlay);
		if (ret != 0)
			goto out_unlock;

		ret = check_overlay_possible_on_crtc(overlay, crtc);
		if (ret != 0)
			goto out_unlock;

		overlay->crtc = crtc;
		crtc->overlay = overlay;

		/* line too wide, i.e. one-line-mode */
		if (crtc->config->pipe_src_w > 1024 &&
		    crtc->config->gmch_pfit.control & PFIT_ENABLE) {
			overlay->pfit_active = true;
			update_pfit_vscale_ratio(overlay);
		} else
			overlay->pfit_active = false;
	}

	ret = check_overlay_dst(overlay, put_image_rec);
	if (ret != 0)
		goto out_unlock;

	if (overlay->pfit_active) {
		params->dst_y = ((((u32)put_image_rec->dst_y) << 12) /
				 overlay->pfit_vscale_ratio);
		/* shifting right rounds downwards, so add 1 */
		params->dst_h = ((((u32)put_image_rec->dst_height) << 12) /
				 overlay->pfit_vscale_ratio) + 1;
	} else {
		params->dst_y = put_image_rec->dst_y;
		params->dst_h = put_image_rec->dst_height;
	}
	params->dst_x = put_image_rec->dst_x;
	params->dst_w = put_image_rec->dst_width;

	params->src_w = put_image_rec->src_width;
	params->src_h = put_image_rec->src_height;
	params->src_scan_w = put_image_rec->src_scan_width;
	params->src_scan_h = put_image_rec->src_scan_height;
	if (params->src_scan_h > params->src_h ||
	    params->src_scan_w > params->src_w) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = check_overlay_src(dev_priv, put_image_rec, new_bo);
	if (ret != 0)
		goto out_unlock;
	params->format = put_image_rec->flags & ~I915_OVERLAY_FLAGS_MASK;
	params->stride_Y = put_image_rec->stride_Y;
	params->stride_UV = put_image_rec->stride_UV;
	params->offset_Y = put_image_rec->offset_Y;
	params->offset_U = put_image_rec->offset_U;
	params->offset_V = put_image_rec->offset_V;

	/* Check scaling after src size to prevent a divide-by-zero. */
	ret = check_overlay_scaling(params);
	if (ret != 0)
		goto out_unlock;

	ret = intel_overlay_do_put_image(overlay, new_bo, params);
	if (ret != 0)
		goto out_unlock;

	mutex_unlock(&dev->struct_mutex);
	drm_modeset_unlock_all(dev);
	i915_gem_object_put(new_bo);

	kfree(params);

	return 0;

out_unlock:
	mutex_unlock(&dev->struct_mutex);
	drm_modeset_unlock_all(dev);
	i915_gem_object_put(new_bo);
out_free:
	kfree(params);

	return ret;
}

static void update_reg_attrs(struct intel_overlay *overlay,
			     struct overlay_registers __iomem *regs)
{
	iowrite32((overlay->contrast << 18) | (overlay->brightness & 0xff),
		  &regs->OCLRC0);
	iowrite32(overlay->saturation, &regs->OCLRC1);
}

static bool check_gamma_bounds(u32 gamma1, u32 gamma2)
{
	int i;

	if (gamma1 & 0xff000000 || gamma2 & 0xff000000)
		return false;

	for (i = 0; i < 3; i++) {
		if (((gamma1 >> i*8) & 0xff) >= ((gamma2 >> i*8) & 0xff))
			return false;
	}

	return true;
}

static bool check_gamma5_errata(u32 gamma5)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (((gamma5 >> i*8) & 0xff) == 0x80)
			return false;
	}

	return true;
}

static int check_gamma(struct drm_intel_overlay_attrs *attrs)
{
	if (!check_gamma_bounds(0, attrs->gamma0) ||
	    !check_gamma_bounds(attrs->gamma0, attrs->gamma1) ||
	    !check_gamma_bounds(attrs->gamma1, attrs->gamma2) ||
	    !check_gamma_bounds(attrs->gamma2, attrs->gamma3) ||
	    !check_gamma_bounds(attrs->gamma3, attrs->gamma4) ||
	    !check_gamma_bounds(attrs->gamma4, attrs->gamma5) ||
	    !check_gamma_bounds(attrs->gamma5, 0x00ffffff))
		return -EINVAL;

	if (!check_gamma5_errata(attrs->gamma5))
		return -EINVAL;

	return 0;
}

int intel_overlay_attrs_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_intel_overlay_attrs *attrs = data;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_overlay *overlay;
	struct overlay_registers __iomem *regs;
	int ret;

	overlay = dev_priv->overlay;
	if (!overlay) {
		DRM_DEBUG("userspace bug: no overlay\n");
		return -ENODEV;
	}

	drm_modeset_lock_all(dev);
	mutex_lock(&dev->struct_mutex);

	ret = -EINVAL;
	if (!(attrs->flags & I915_OVERLAY_UPDATE_ATTRS)) {
		attrs->color_key  = overlay->color_key;
		attrs->brightness = overlay->brightness;
		attrs->contrast   = overlay->contrast;
		attrs->saturation = overlay->saturation;

		if (!IS_GEN2(dev_priv)) {
			attrs->gamma0 = I915_READ(OGAMC0);
			attrs->gamma1 = I915_READ(OGAMC1);
			attrs->gamma2 = I915_READ(OGAMC2);
			attrs->gamma3 = I915_READ(OGAMC3);
			attrs->gamma4 = I915_READ(OGAMC4);
			attrs->gamma5 = I915_READ(OGAMC5);
		}
	} else {
		if (attrs->brightness < -128 || attrs->brightness > 127)
			goto out_unlock;
		if (attrs->contrast > 255)
			goto out_unlock;
		if (attrs->saturation > 1023)
			goto out_unlock;

		overlay->color_key  = attrs->color_key;
		overlay->brightness = attrs->brightness;
		overlay->contrast   = attrs->contrast;
		overlay->saturation = attrs->saturation;

		regs = intel_overlay_map_regs(overlay);
		if (!regs) {
			ret = -ENOMEM;
			goto out_unlock;
		}

		update_reg_attrs(overlay, regs);

		intel_overlay_unmap_regs(overlay, regs);

		if (attrs->flags & I915_OVERLAY_UPDATE_GAMMA) {
			if (IS_GEN2(dev_priv))
				goto out_unlock;

			if (overlay->active) {
				ret = -EBUSY;
				goto out_unlock;
			}

			ret = check_gamma(attrs);
			if (ret)
				goto out_unlock;

			I915_WRITE(OGAMC0, attrs->gamma0);
			I915_WRITE(OGAMC1, attrs->gamma1);
			I915_WRITE(OGAMC2, attrs->gamma2);
			I915_WRITE(OGAMC3, attrs->gamma3);
			I915_WRITE(OGAMC4, attrs->gamma4);
			I915_WRITE(OGAMC5, attrs->gamma5);
		}
	}
	overlay->color_key_enabled = (attrs->flags & I915_OVERLAY_DISABLE_DEST_COLORKEY) == 0;

	ret = 0;
out_unlock:
	mutex_unlock(&dev->struct_mutex);
	drm_modeset_unlock_all(dev);

	return ret;
}

void intel_setup_overlay(struct drm_i915_private *dev_priv)
{
	struct intel_overlay *overlay;
	struct drm_i915_gem_object *reg_bo;
	struct overlay_registers __iomem *regs;
	struct i915_vma *vma = NULL;
	int ret;

	if (!HAS_OVERLAY(dev_priv))
		return;

	overlay = kzalloc(sizeof(*overlay), GFP_KERNEL);
	if (!overlay)
		return;

	mutex_lock(&dev_priv->drm.struct_mutex);
	if (WARN_ON(dev_priv->overlay))
		goto out_free;

	overlay->i915 = dev_priv;

	reg_bo = NULL;
	if (!OVERLAY_NEEDS_PHYSICAL(dev_priv))
		reg_bo = i915_gem_object_create_stolen(dev_priv, PAGE_SIZE);
	if (reg_bo == NULL)
		reg_bo = i915_gem_object_create(dev_priv, PAGE_SIZE);
	if (IS_ERR(reg_bo))
		goto out_free;
	overlay->reg_bo = reg_bo;

	if (OVERLAY_NEEDS_PHYSICAL(dev_priv)) {
		ret = i915_gem_object_attach_phys(reg_bo, PAGE_SIZE);
		if (ret) {
			DRM_ERROR("failed to attach phys overlay regs\n");
			goto out_free_bo;
		}
		overlay->flip_addr = reg_bo->phys_handle->busaddr;
	} else {
		vma = i915_gem_object_ggtt_pin(reg_bo, NULL,
					       0, PAGE_SIZE, PIN_MAPPABLE);
		if (IS_ERR(vma)) {
			DRM_ERROR("failed to pin overlay register bo\n");
			ret = PTR_ERR(vma);
			goto out_free_bo;
		}
		overlay->flip_addr = i915_ggtt_offset(vma);

		ret = i915_gem_object_set_to_gtt_domain(reg_bo, true);
		if (ret) {
			DRM_ERROR("failed to move overlay register bo into the GTT\n");
			goto out_unpin_bo;
		}
	}

	/* init all values */
	overlay->color_key = 0x0101fe;
	overlay->color_key_enabled = true;
	overlay->brightness = -19;
	overlay->contrast = 75;
	overlay->saturation = 146;

	init_request_active(&overlay->last_flip, NULL);

	regs = intel_overlay_map_regs(overlay);
	if (!regs)
		goto out_unpin_bo;

	memset_io(regs, 0, sizeof(struct overlay_registers));
	update_polyphase_filter(regs);
	update_reg_attrs(overlay, regs);

	intel_overlay_unmap_regs(overlay, regs);

	dev_priv->overlay = overlay;
	mutex_unlock(&dev_priv->drm.struct_mutex);
	DRM_INFO("initialized overlay support\n");
	return;

out_unpin_bo:
	if (vma)
		i915_vma_unpin(vma);
out_free_bo:
	i915_gem_object_put(reg_bo);
out_free:
	mutex_unlock(&dev_priv->drm.struct_mutex);
	kfree(overlay);
	return;
}

void intel_cleanup_overlay(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->overlay)
		return;

	/* The bo's should be free'd by the generic code already.
	 * Furthermore modesetting teardown happens beforehand so the
	 * hardware should be off already */
	WARN_ON(dev_priv->overlay->active);

	i915_gem_object_put(dev_priv->overlay->reg_bo);
	kfree(dev_priv->overlay);
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

struct intel_overlay_error_state {
	struct overlay_registers regs;
	unsigned long base;
	u32 dovsta;
	u32 isr;
};

static struct overlay_registers __iomem *
intel_overlay_map_regs_atomic(struct intel_overlay *overlay)
{
	struct drm_i915_private *dev_priv = overlay->i915;
	struct overlay_registers __iomem *regs;

	if (OVERLAY_NEEDS_PHYSICAL(dev_priv))
		/* Cast to make sparse happy, but it's wc memory anyway, so
		 * equivalent to the wc io mapping on X86. */
		regs = (struct overlay_registers __iomem *)
			overlay->reg_bo->phys_handle->vaddr;
	else
		regs = io_mapping_map_atomic_wc(&dev_priv->ggtt.mappable,
						overlay->flip_addr);

	return regs;
}

static void intel_overlay_unmap_regs_atomic(struct intel_overlay *overlay,
					struct overlay_registers __iomem *regs)
{
	if (!OVERLAY_NEEDS_PHYSICAL(overlay->i915))
		io_mapping_unmap_atomic(regs);
}

struct intel_overlay_error_state *
intel_overlay_capture_error_state(struct drm_i915_private *dev_priv)
{
	struct intel_overlay *overlay = dev_priv->overlay;
	struct intel_overlay_error_state *error;
	struct overlay_registers __iomem *regs;

	if (!overlay || !overlay->active)
		return NULL;

	error = kmalloc(sizeof(*error), GFP_ATOMIC);
	if (error == NULL)
		return NULL;

	error->dovsta = I915_READ(DOVSTA);
	error->isr = I915_READ(ISR);
	error->base = overlay->flip_addr;

	regs = intel_overlay_map_regs_atomic(overlay);
	if (!regs)
		goto err;

	memcpy_fromio(&error->regs, regs, sizeof(struct overlay_registers));
	intel_overlay_unmap_regs_atomic(overlay, regs);

	return error;

err:
	kfree(error);
	return NULL;
}

void
intel_overlay_print_error_state(struct drm_i915_error_state_buf *m,
				struct intel_overlay_error_state *error)
{
	i915_error_printf(m, "Overlay, status: 0x%08x, interrupt: 0x%08x\n",
			  error->dovsta, error->isr);
	i915_error_printf(m, "  Register file at 0x%08lx:\n",
			  error->base);

#define P(x) i915_error_printf(m, "    " #x ":	0x%08x\n", error->regs.x)
	P(OBUF_0Y);
	P(OBUF_1Y);
	P(OBUF_0U);
	P(OBUF_0V);
	P(OBUF_1U);
	P(OBUF_1V);
	P(OSTRIDE);
	P(YRGB_VPH);
	P(UV_VPH);
	P(HORZ_PH);
	P(INIT_PHS);
	P(DWINPOS);
	P(DWINSZ);
	P(SWIDTH);
	P(SWIDTHSW);
	P(SHEIGHT);
	P(YRGBSCALE);
	P(UVSCALE);
	P(OCLRC0);
	P(OCLRC1);
	P(DCLRKV);
	P(DCLRKM);
	P(SCLRKVH);
	P(SCLRKVL);
	P(SCLRKEN);
	P(OCONFIG);
	P(OCMD);
	P(OSTART_0Y);
	P(OSTART_1Y);
	P(OSTART_0U);
	P(OSTART_0V);
	P(OSTART_1U);
	P(OSTART_1V);
	P(OTILEOFF_0Y);
	P(OTILEOFF_1Y);
	P(OTILEOFF_0U);
	P(OTILEOFF_0V);
	P(OTILEOFF_1U);
	P(OTILEOFF_1V);
	P(FASTHSCALE);
	P(UVSCALEV);
#undef P
}

#endif
