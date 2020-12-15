// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/component.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>

#include "sun4i_backend.h"
#include "sun4i_drv.h"
#include "sun4i_frontend.h"
#include "sun4i_layer.h"
#include "sunxi_engine.h"

struct sun4i_backend_quirks {
	/* backend <-> TCON muxing selection done in backend */
	bool needs_output_muxing;

	/* alpha at the lowest z position is not always supported */
	bool supports_lowest_plane_alpha;
};

static const u32 sunxi_rgb2yuv_coef[12] = {
	0x00000107, 0x00000204, 0x00000064, 0x00000108,
	0x00003f69, 0x00003ed6, 0x000001c1, 0x00000808,
	0x000001c1, 0x00003e88, 0x00003fb8, 0x00000808
};

static void sun4i_backend_apply_color_correction(struct sunxi_engine *engine)
{
	int i;

	DRM_DEBUG_DRIVER("Applying RGB to YUV color correction\n");

	/* Set color correction */
	regmap_write(engine->regs, SUN4I_BACKEND_OCCTL_REG,
		     SUN4I_BACKEND_OCCTL_ENABLE);

	for (i = 0; i < 12; i++)
		regmap_write(engine->regs, SUN4I_BACKEND_OCRCOEF_REG(i),
			     sunxi_rgb2yuv_coef[i]);
}

static void sun4i_backend_disable_color_correction(struct sunxi_engine *engine)
{
	DRM_DEBUG_DRIVER("Disabling color correction\n");

	/* Disable color correction */
	regmap_update_bits(engine->regs, SUN4I_BACKEND_OCCTL_REG,
			   SUN4I_BACKEND_OCCTL_ENABLE, 0);
}

static void sun4i_backend_commit(struct sunxi_engine *engine)
{
	DRM_DEBUG_DRIVER("Committing changes\n");

	regmap_write(engine->regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS |
		     SUN4I_BACKEND_REGBUFFCTL_LOADCTL);
}

void sun4i_backend_layer_enable(struct sun4i_backend *backend,
				int layer, bool enable)
{
	u32 val;

	DRM_DEBUG_DRIVER("%sabling layer %d\n", enable ? "En" : "Dis",
			 layer);

	if (enable)
		val = SUN4I_BACKEND_MODCTL_LAY_EN(layer);
	else
		val = 0;

	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_LAY_EN(layer), val);
}

static int sun4i_backend_drm_format_to_layer(u32 format, u32 *mode)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB8888;
		break;

	case DRM_FORMAT_ARGB4444:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB4444;
		break;

	case DRM_FORMAT_ARGB1555:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB1555;
		break;

	case DRM_FORMAT_RGBA5551:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGBA5551;
		break;

	case DRM_FORMAT_RGBA4444:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGBA4444;
		break;

	case DRM_FORMAT_XRGB8888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_XRGB8888;
		break;

	case DRM_FORMAT_RGB888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGB888;
		break;

	case DRM_FORMAT_RGB565:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGB565;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const uint32_t sun4i_backend_formats[] = {
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
};

bool sun4i_backend_format_is_supported(uint32_t fmt, uint64_t modifier)
{
	unsigned int i;

	if (modifier != DRM_FORMAT_MOD_LINEAR)
		return false;

	for (i = 0; i < ARRAY_SIZE(sun4i_backend_formats); i++)
		if (sun4i_backend_formats[i] == fmt)
			return true;

	return false;
}

int sun4i_backend_update_layer_coord(struct sun4i_backend *backend,
				     int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;

	DRM_DEBUG_DRIVER("Updating layer %d\n", layer);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		DRM_DEBUG_DRIVER("Primary layer, updating global size W: %u H: %u\n",
				 state->crtc_w, state->crtc_h);
		regmap_write(backend->engine.regs, SUN4I_BACKEND_DISSIZE_REG,
			     SUN4I_BACKEND_DISSIZE(state->crtc_w,
						   state->crtc_h));
	}

	/* Set height and width */
	DRM_DEBUG_DRIVER("Layer size W: %u H: %u\n",
			 state->crtc_w, state->crtc_h);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_LAYSIZE_REG(layer),
		     SUN4I_BACKEND_LAYSIZE(state->crtc_w,
					   state->crtc_h));

	/* Set base coordinates */
	DRM_DEBUG_DRIVER("Layer coordinates X: %d Y: %d\n",
			 state->crtc_x, state->crtc_y);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_LAYCOOR_REG(layer),
		     SUN4I_BACKEND_LAYCOOR(state->crtc_x,
					   state->crtc_y));

	return 0;
}

static int sun4i_backend_update_yuv_format(struct sun4i_backend *backend,
					   int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	const struct drm_format_info *format = fb->format;
	const uint32_t fmt = format->format;
	u32 val = SUN4I_BACKEND_IYUVCTL_EN;
	int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_bt601_yuv2rgb_coef); i++)
		regmap_write(backend->engine.regs,
			     SUN4I_BACKEND_YGCOEF_REG(i),
			     sunxi_bt601_yuv2rgb_coef[i]);

	/*
	 * We should do that only for a single plane, but the
	 * framebuffer's atomic_check has our back on this.
	 */
	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_YUVEN,
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_YUVEN);

	/* TODO: Add support for the multi-planar YUV formats */
	if (drm_format_info_is_yuv_packed(format) &&
	    drm_format_info_is_yuv_sampling_422(format))
		val |= SUN4I_BACKEND_IYUVCTL_FBFMT_PACKED_YUV422;
	else
		DRM_DEBUG_DRIVER("Unsupported YUV format (0x%x)\n", fmt);

	/*
	 * Allwinner seems to list the pixel sequence from right to left, while
	 * DRM lists it from left to right.
	 */
	switch (fmt) {
	case DRM_FORMAT_YUYV:
		val |= SUN4I_BACKEND_IYUVCTL_FBPS_VYUY;
		break;
	case DRM_FORMAT_YVYU:
		val |= SUN4I_BACKEND_IYUVCTL_FBPS_UYVY;
		break;
	case DRM_FORMAT_UYVY:
		val |= SUN4I_BACKEND_IYUVCTL_FBPS_YVYU;
		break;
	case DRM_FORMAT_VYUY:
		val |= SUN4I_BACKEND_IYUVCTL_FBPS_YUYV;
		break;
	default:
		DRM_DEBUG_DRIVER("Unsupported YUV pixel sequence (0x%x)\n",
				 fmt);
	}

	regmap_write(backend->engine.regs, SUN4I_BACKEND_IYUVCTL_REG, val);

	return 0;
}

int sun4i_backend_update_layer_formats(struct sun4i_backend *backend,
				       int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	bool interlaced = false;
	u32 val;
	int ret;

	/* Clear the YUV mode */
	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_YUVEN, 0);

	if (plane->state->crtc)
		interlaced = plane->state->crtc->state->adjusted_mode.flags
			& DRM_MODE_FLAG_INTERLACE;

	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_ITLMOD_EN,
			   interlaced ? SUN4I_BACKEND_MODCTL_ITLMOD_EN : 0);

	DRM_DEBUG_DRIVER("Switching display backend interlaced mode %s\n",
			 interlaced ? "on" : "off");

	val = SUN4I_BACKEND_ATTCTL_REG0_LAY_GLBALPHA(state->alpha >> 8);
	if (state->alpha != DRM_BLEND_ALPHA_OPAQUE)
		val |= SUN4I_BACKEND_ATTCTL_REG0_LAY_GLBALPHA_EN;
	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_GLBALPHA_MASK |
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_GLBALPHA_EN,
			   val);

	if (fb->format->is_yuv)
		return sun4i_backend_update_yuv_format(backend, layer, plane);

	ret = sun4i_backend_drm_format_to_layer(fb->format->format, &val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return ret;
	}

	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG1(layer),
			   SUN4I_BACKEND_ATTCTL_REG1_LAY_FBFMT, val);

	return 0;
}

int sun4i_backend_update_layer_frontend(struct sun4i_backend *backend,
					int layer, uint32_t fmt)
{
	u32 val;
	int ret;

	ret = sun4i_backend_drm_format_to_layer(fmt, &val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return ret;
	}

	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_VDOEN,
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_VDOEN);

	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG1(layer),
			   SUN4I_BACKEND_ATTCTL_REG1_LAY_FBFMT, val);

	return 0;
}

static int sun4i_backend_update_yuv_buffer(struct sun4i_backend *backend,
					   struct drm_framebuffer *fb,
					   dma_addr_t paddr)
{
	/* TODO: Add support for the multi-planar YUV formats */
	DRM_DEBUG_DRIVER("Setting packed YUV buffer address to %pad\n", &paddr);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_IYUVADD_REG(0), paddr);

	DRM_DEBUG_DRIVER("Layer line width: %d bits\n", fb->pitches[0] * 8);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_IYUVLINEWIDTH_REG(0),
		     fb->pitches[0] * 8);

	return 0;
}

int sun4i_backend_update_layer_buffer(struct sun4i_backend *backend,
				      int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	u32 lo_paddr, hi_paddr;
	dma_addr_t paddr;

	/* Set the line width */
	DRM_DEBUG_DRIVER("Layer line width: %d bits\n", fb->pitches[0] * 8);
	regmap_write(backend->engine.regs,
		     SUN4I_BACKEND_LAYLINEWIDTH_REG(layer),
		     fb->pitches[0] * 8);

	/* Get the start of the displayed memory */
	paddr = drm_fb_cma_get_gem_addr(fb, state, 0);
	DRM_DEBUG_DRIVER("Setting buffer address to %pad\n", &paddr);

	if (fb->format->is_yuv)
		return sun4i_backend_update_yuv_buffer(backend, fb, paddr);

	/* Write the 32 lower bits of the address (in bits) */
	lo_paddr = paddr << 3;
	DRM_DEBUG_DRIVER("Setting address lower bits to 0x%x\n", lo_paddr);
	regmap_write(backend->engine.regs,
		     SUN4I_BACKEND_LAYFB_L32ADD_REG(layer),
		     lo_paddr);

	/* And the upper bits */
	hi_paddr = paddr >> 29;
	DRM_DEBUG_DRIVER("Setting address high bits to 0x%x\n", hi_paddr);
	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_LAYFB_H4ADD_REG,
			   SUN4I_BACKEND_LAYFB_H4ADD_MSK(layer),
			   SUN4I_BACKEND_LAYFB_H4ADD(layer, hi_paddr));

	return 0;
}

int sun4i_backend_update_layer_zpos(struct sun4i_backend *backend, int layer,
				    struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct sun4i_layer_state *p_state = state_to_sun4i_layer_state(state);
	unsigned int priority = state->normalized_zpos;
	unsigned int pipe = p_state->pipe;

	DRM_DEBUG_DRIVER("Setting layer %d's priority to %d and pipe %d\n",
			 layer, priority, pipe);
	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL_MASK |
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_PRISEL_MASK,
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL(p_state->pipe) |
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_PRISEL(priority));

	return 0;
}

void sun4i_backend_cleanup_layer(struct sun4i_backend *backend,
				 int layer)
{
	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG0(layer),
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_VDOEN |
			   SUN4I_BACKEND_ATTCTL_REG0_LAY_YUVEN, 0);
}

static bool sun4i_backend_plane_uses_scaler(struct drm_plane_state *state)
{
	u16 src_h = state->src_h >> 16;
	u16 src_w = state->src_w >> 16;

	DRM_DEBUG_DRIVER("Input size %dx%d, output size %dx%d\n",
			 src_w, src_h, state->crtc_w, state->crtc_h);

	if ((state->crtc_h != src_h) || (state->crtc_w != src_w))
		return true;

	return false;
}

static bool sun4i_backend_plane_uses_frontend(struct drm_plane_state *state)
{
	struct sun4i_layer *layer = plane_to_sun4i_layer(state->plane);
	struct sun4i_backend *backend = layer->backend;
	uint32_t format = state->fb->format->format;
	uint64_t modifier = state->fb->modifier;

	if (IS_ERR(backend->frontend))
		return false;

	if (!sun4i_frontend_format_is_supported(format, modifier))
		return false;

	if (!sun4i_backend_format_is_supported(format, modifier))
		return true;

	/*
	 * TODO: The backend alone allows 2x and 4x integer scaling, including
	 * support for an alpha component (which the frontend doesn't support).
	 * Use the backend directly instead of the frontend in this case, with
	 * another test to return false.
	 */

	if (sun4i_backend_plane_uses_scaler(state))
		return true;

	/*
	 * Here the format is supported by both the frontend and the backend
	 * and no frontend scaling is required, so use the backend directly.
	 */
	return false;
}

static bool sun4i_backend_plane_is_supported(struct drm_plane_state *state,
					     bool *uses_frontend)
{
	if (sun4i_backend_plane_uses_frontend(state)) {
		*uses_frontend = true;
		return true;
	}

	*uses_frontend = false;

	/* Scaling is not supported without the frontend. */
	if (sun4i_backend_plane_uses_scaler(state))
		return false;

	return true;
}

static void sun4i_backend_atomic_begin(struct sunxi_engine *engine,
				       struct drm_crtc_state *old_state)
{
	u32 val;

	WARN_ON(regmap_read_poll_timeout(engine->regs,
					 SUN4I_BACKEND_REGBUFFCTL_REG,
					 val, !(val & SUN4I_BACKEND_REGBUFFCTL_LOADCTL),
					 100, 50000));
}

static int sun4i_backend_atomic_check(struct sunxi_engine *engine,
				      struct drm_crtc_state *crtc_state)
{
	struct drm_plane_state *plane_states[SUN4I_BACKEND_NUM_LAYERS] = { 0 };
	struct sun4i_backend *backend = engine_to_sun4i_backend(engine);
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_device *drm = state->dev;
	struct drm_plane *plane;
	unsigned int num_planes = 0;
	unsigned int num_alpha_planes = 0;
	unsigned int num_frontend_planes = 0;
	unsigned int num_alpha_planes_max = 1;
	unsigned int num_yuv_planes = 0;
	unsigned int current_pipe = 0;
	unsigned int i;

	DRM_DEBUG_DRIVER("Starting checking our planes\n");

	if (!crtc_state->planes_changed)
		return 0;

	drm_for_each_plane_mask(plane, drm, crtc_state->plane_mask) {
		struct drm_plane_state *plane_state =
			drm_atomic_get_plane_state(state, plane);
		struct sun4i_layer_state *layer_state =
			state_to_sun4i_layer_state(plane_state);
		struct drm_framebuffer *fb = plane_state->fb;
		struct drm_format_name_buf format_name;

		if (!sun4i_backend_plane_is_supported(plane_state,
						      &layer_state->uses_frontend))
			return -EINVAL;

		if (layer_state->uses_frontend) {
			DRM_DEBUG_DRIVER("Using the frontend for plane %d\n",
					 plane->index);
			num_frontend_planes++;
		} else {
			if (fb->format->is_yuv) {
				DRM_DEBUG_DRIVER("Plane FB format is YUV\n");
				num_yuv_planes++;
			}
		}

		DRM_DEBUG_DRIVER("Plane FB format is %s\n",
				 drm_get_format_name(fb->format->format,
						     &format_name));
		if (fb->format->has_alpha || (plane_state->alpha != DRM_BLEND_ALPHA_OPAQUE))
			num_alpha_planes++;

		DRM_DEBUG_DRIVER("Plane zpos is %d\n",
				 plane_state->normalized_zpos);

		/* Sort our planes by Zpos */
		plane_states[plane_state->normalized_zpos] = plane_state;

		num_planes++;
	}

	/* All our planes were disabled, bail out */
	if (!num_planes)
		return 0;

	/*
	 * The hardware is a bit unusual here.
	 *
	 * Even though it supports 4 layers, it does the composition
	 * in two separate steps.
	 *
	 * The first one is assigning a layer to one of its two
	 * pipes. If more that 1 layer is assigned to the same pipe,
	 * and if pixels overlaps, the pipe will take the pixel from
	 * the layer with the highest priority.
	 *
	 * The second step is the actual alpha blending, that takes
	 * the two pipes as input, and uses the potential alpha
	 * component to do the transparency between the two.
	 *
	 * This two-step scenario makes us unable to guarantee a
	 * robust alpha blending between the 4 layers in all
	 * situations, since this means that we need to have one layer
	 * with alpha at the lowest position of our two pipes.
	 *
	 * However, we cannot even do that on every platform, since
	 * the hardware has a bug where the lowest plane of the lowest
	 * pipe (pipe 0, priority 0), if it has any alpha, will
	 * discard the pixel data entirely and just display the pixels
	 * in the background color (black by default).
	 *
	 * This means that on the affected platforms, we effectively
	 * have only three valid configurations with alpha, all of
	 * them with the alpha being on pipe1 with the lowest
	 * position, which can be 1, 2 or 3 depending on the number of
	 * planes and their zpos.
	 */

	/* For platforms that are not affected by the issue described above. */
	if (backend->quirks->supports_lowest_plane_alpha)
		num_alpha_planes_max++;

	if (num_alpha_planes > num_alpha_planes_max) {
		DRM_DEBUG_DRIVER("Too many planes with alpha, rejecting...\n");
		return -EINVAL;
	}

	/* We can't have an alpha plane at the lowest position */
	if (!backend->quirks->supports_lowest_plane_alpha &&
	    (plane_states[0]->alpha != DRM_BLEND_ALPHA_OPAQUE))
		return -EINVAL;

	for (i = 1; i < num_planes; i++) {
		struct drm_plane_state *p_state = plane_states[i];
		struct drm_framebuffer *fb = p_state->fb;
		struct sun4i_layer_state *s_state = state_to_sun4i_layer_state(p_state);

		/*
		 * The only alpha position is the lowest plane of the
		 * second pipe.
		 */
		if (fb->format->has_alpha || (p_state->alpha != DRM_BLEND_ALPHA_OPAQUE))
			current_pipe++;

		s_state->pipe = current_pipe;
	}

	/* We can only have a single YUV plane at a time */
	if (num_yuv_planes > SUN4I_BACKEND_NUM_YUV_PLANES) {
		DRM_DEBUG_DRIVER("Too many planes with YUV, rejecting...\n");
		return -EINVAL;
	}

	if (num_frontend_planes > SUN4I_BACKEND_NUM_FRONTEND_LAYERS) {
		DRM_DEBUG_DRIVER("Too many planes going through the frontend, rejecting\n");
		return -EINVAL;
	}

	DRM_DEBUG_DRIVER("State valid with %u planes, %u alpha, %u video, %u YUV\n",
			 num_planes, num_alpha_planes, num_frontend_planes,
			 num_yuv_planes);

	return 0;
}

static void sun4i_backend_vblank_quirk(struct sunxi_engine *engine)
{
	struct sun4i_backend *backend = engine_to_sun4i_backend(engine);
	struct sun4i_frontend *frontend = backend->frontend;

	if (!frontend)
		return;

	/*
	 * In a teardown scenario with the frontend involved, we have
	 * to keep the frontend enabled until the next vblank, and
	 * only then disable it.
	 *
	 * This is due to the fact that the backend will not take into
	 * account the new configuration (with the plane that used to
	 * be fed by the frontend now disabled) until we write to the
	 * commit bit and the hardware fetches the new configuration
	 * during the next vblank.
	 *
	 * So we keep the frontend around in order to prevent any
	 * visual artifacts.
	 */
	spin_lock(&backend->frontend_lock);
	if (backend->frontend_teardown) {
		sun4i_frontend_exit(frontend);
		backend->frontend_teardown = false;
	}
	spin_unlock(&backend->frontend_lock);
};

static int sun4i_backend_init_sat(struct device *dev) {
	struct sun4i_backend *backend = dev_get_drvdata(dev);
	int ret;

	backend->sat_reset = devm_reset_control_get(dev, "sat");
	if (IS_ERR(backend->sat_reset)) {
		dev_err(dev, "Couldn't get the SAT reset line\n");
		return PTR_ERR(backend->sat_reset);
	}

	ret = reset_control_deassert(backend->sat_reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert the SAT reset line\n");
		return ret;
	}

	backend->sat_clk = devm_clk_get(dev, "sat");
	if (IS_ERR(backend->sat_clk)) {
		dev_err(dev, "Couldn't get our SAT clock\n");
		ret = PTR_ERR(backend->sat_clk);
		goto err_assert_reset;
	}

	ret = clk_prepare_enable(backend->sat_clk);
	if (ret) {
		dev_err(dev, "Couldn't enable the SAT clock\n");
		return ret;
	}

	return 0;

err_assert_reset:
	reset_control_assert(backend->sat_reset);
	return ret;
}

static int sun4i_backend_free_sat(struct device *dev) {
	struct sun4i_backend *backend = dev_get_drvdata(dev);

	clk_disable_unprepare(backend->sat_clk);
	reset_control_assert(backend->sat_reset);

	return 0;
}

/*
 * The display backend can take video output from the display frontend, or
 * the display enhancement unit on the A80, as input for one it its layers.
 * This relationship within the display pipeline is encoded in the device
 * tree with of_graph, and we use it here to figure out which backend, if
 * there are 2 or more, we are currently probing. The number would be in
 * the "reg" property of the upstream output port endpoint.
 */
static int sun4i_backend_of_get_id(struct device_node *node)
{
	struct device_node *ep, *remote;
	struct of_endpoint of_ep;

	/* Input port is 0, and we want the first endpoint. */
	ep = of_graph_get_endpoint_by_regs(node, 0, -1);
	if (!ep)
		return -EINVAL;

	remote = of_graph_get_remote_endpoint(ep);
	of_node_put(ep);
	if (!remote)
		return -EINVAL;

	of_graph_parse_endpoint(remote, &of_ep);
	of_node_put(remote);
	return of_ep.id;
}

/* TODO: This needs to take multiple pipelines into account */
static struct sun4i_frontend *sun4i_backend_find_frontend(struct sun4i_drv *drv,
							  struct device_node *node)
{
	struct device_node *port, *ep, *remote;
	struct sun4i_frontend *frontend;

	port = of_graph_get_port_by_id(node, 0);
	if (!port)
		return ERR_PTR(-EINVAL);

	for_each_available_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote)
			continue;
		of_node_put(remote);

		/* does this node match any registered engines? */
		list_for_each_entry(frontend, &drv->frontend_list, list) {
			if (remote == frontend->node) {
				of_node_put(port);
				of_node_put(ep);
				return frontend;
			}
		}
	}
	of_node_put(port);
	return ERR_PTR(-EINVAL);
}

static const struct sunxi_engine_ops sun4i_backend_engine_ops = {
	.atomic_begin			= sun4i_backend_atomic_begin,
	.atomic_check			= sun4i_backend_atomic_check,
	.commit				= sun4i_backend_commit,
	.layers_init			= sun4i_layers_init,
	.apply_color_correction		= sun4i_backend_apply_color_correction,
	.disable_color_correction	= sun4i_backend_disable_color_correction,
	.vblank_quirk			= sun4i_backend_vblank_quirk,
};

static struct regmap_config sun4i_backend_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x5800,
};

static int sun4i_backend_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_backend *backend;
	const struct sun4i_backend_quirks *quirks;
	struct resource *res;
	void __iomem *regs;
	int i, ret;

	backend = devm_kzalloc(dev, sizeof(*backend), GFP_KERNEL);
	if (!backend)
		return -ENOMEM;
	dev_set_drvdata(dev, backend);
	spin_lock_init(&backend->frontend_lock);

	if (of_find_property(dev->of_node, "interconnects", NULL)) {
		/*
		 * This assume we have the same DMA constraints for all our the
		 * devices in our pipeline (all the backends, but also the
		 * frontends). This sounds bad, but it has always been the case
		 * for us, and DRM doesn't do per-device allocation either, so
		 * we would need to fix DRM first...
		 */
		ret = of_dma_configure(drm->dev, dev->of_node, true);
		if (ret)
			return ret;
	} else {
		/*
		 * If we don't have the interconnect property, most likely
		 * because of an old DT, we need to set the DMA offset by hand
		 * on our device since the RAM mapping is at 0 for the DMA bus,
		 * unlike the CPU.
		 */
		drm->dev->dma_pfn_offset = PHYS_PFN_OFFSET;
	}

	backend->engine.node = dev->of_node;
	backend->engine.ops = &sun4i_backend_engine_ops;
	backend->engine.id = sun4i_backend_of_get_id(dev->of_node);
	if (backend->engine.id < 0)
		return backend->engine.id;

	backend->frontend = sun4i_backend_find_frontend(drv, dev->of_node);
	if (IS_ERR(backend->frontend))
		dev_warn(dev, "Couldn't find matching frontend, frontend features disabled\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	backend->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(backend->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(backend->reset);
	}

	ret = reset_control_deassert(backend->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	backend->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(backend->bus_clk)) {
		dev_err(dev, "Couldn't get the backend bus clock\n");
		ret = PTR_ERR(backend->bus_clk);
		goto err_assert_reset;
	}
	clk_prepare_enable(backend->bus_clk);

	backend->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(backend->mod_clk)) {
		dev_err(dev, "Couldn't get the backend module clock\n");
		ret = PTR_ERR(backend->mod_clk);
		goto err_disable_bus_clk;
	}

	ret = clk_set_rate_exclusive(backend->mod_clk, 300000000);
	if (ret) {
		dev_err(dev, "Couldn't set the module clock frequency\n");
		goto err_disable_bus_clk;
	}

	clk_prepare_enable(backend->mod_clk);

	backend->ram_clk = devm_clk_get(dev, "ram");
	if (IS_ERR(backend->ram_clk)) {
		dev_err(dev, "Couldn't get the backend RAM clock\n");
		ret = PTR_ERR(backend->ram_clk);
		goto err_disable_mod_clk;
	}
	clk_prepare_enable(backend->ram_clk);

	if (of_device_is_compatible(dev->of_node,
				    "allwinner,sun8i-a33-display-backend")) {
		ret = sun4i_backend_init_sat(dev);
		if (ret) {
			dev_err(dev, "Couldn't init SAT resources\n");
			goto err_disable_ram_clk;
		}
	}

	backend->engine.regs = devm_regmap_init_mmio(dev, regs,
						     &sun4i_backend_regmap_config);
	if (IS_ERR(backend->engine.regs)) {
		dev_err(dev, "Couldn't create the backend regmap\n");
		return PTR_ERR(backend->engine.regs);
	}

	list_add_tail(&backend->engine.list, &drv->engine_list);

	/*
	 * Many of the backend's layer configuration registers have
	 * undefined default values. This poses a risk as we use
	 * regmap_update_bits in some places, and don't overwrite
	 * the whole register.
	 *
	 * Clear the registers here to have something predictable.
	 */
	for (i = 0x800; i < 0x1000; i += 4)
		regmap_write(backend->engine.regs, i, 0);

	/* Disable registers autoloading */
	regmap_write(backend->engine.regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS);

	/* Enable the backend */
	regmap_write(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
		     SUN4I_BACKEND_MODCTL_DEBE_EN |
		     SUN4I_BACKEND_MODCTL_START_CTL);

	/* Set output selection if needed */
	quirks = of_device_get_match_data(dev);
	if (quirks->needs_output_muxing) {
		/*
		 * We assume there is no dynamic muxing of backends
		 * and TCONs, so we select the backend with same ID.
		 *
		 * While dynamic selection might be interesting, since
		 * the CRTC is tied to the TCON, while the layers are
		 * tied to the backends, this means, we will need to
		 * switch between groups of layers. There might not be
		 * a way to represent this constraint in DRM.
		 */
		regmap_update_bits(backend->engine.regs,
				   SUN4I_BACKEND_MODCTL_REG,
				   SUN4I_BACKEND_MODCTL_OUT_SEL,
				   (backend->engine.id
				    ? SUN4I_BACKEND_MODCTL_OUT_LCD1
				    : SUN4I_BACKEND_MODCTL_OUT_LCD0));
	}

	backend->quirks = quirks;

	return 0;

err_disable_ram_clk:
	clk_disable_unprepare(backend->ram_clk);
err_disable_mod_clk:
	clk_rate_exclusive_put(backend->mod_clk);
	clk_disable_unprepare(backend->mod_clk);
err_disable_bus_clk:
	clk_disable_unprepare(backend->bus_clk);
err_assert_reset:
	reset_control_assert(backend->reset);
	return ret;
}

static void sun4i_backend_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct sun4i_backend *backend = dev_get_drvdata(dev);

	list_del(&backend->engine.list);

	if (of_device_is_compatible(dev->of_node,
				    "allwinner,sun8i-a33-display-backend"))
		sun4i_backend_free_sat(dev);

	clk_disable_unprepare(backend->ram_clk);
	clk_rate_exclusive_put(backend->mod_clk);
	clk_disable_unprepare(backend->mod_clk);
	clk_disable_unprepare(backend->bus_clk);
	reset_control_assert(backend->reset);
}

static const struct component_ops sun4i_backend_ops = {
	.bind	= sun4i_backend_bind,
	.unbind	= sun4i_backend_unbind,
};

static int sun4i_backend_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_backend_ops);
}

static int sun4i_backend_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_backend_ops);

	return 0;
}

static const struct sun4i_backend_quirks sun4i_backend_quirks = {
	.needs_output_muxing = true,
};

static const struct sun4i_backend_quirks sun5i_backend_quirks = {
};

static const struct sun4i_backend_quirks sun6i_backend_quirks = {
};

static const struct sun4i_backend_quirks sun7i_backend_quirks = {
	.needs_output_muxing = true,
};

static const struct sun4i_backend_quirks sun8i_a33_backend_quirks = {
	.supports_lowest_plane_alpha = true,
};

static const struct sun4i_backend_quirks sun9i_backend_quirks = {
};

static const struct of_device_id sun4i_backend_of_table[] = {
	{
		.compatible = "allwinner,sun4i-a10-display-backend",
		.data = &sun4i_backend_quirks,
	},
	{
		.compatible = "allwinner,sun5i-a13-display-backend",
		.data = &sun5i_backend_quirks,
	},
	{
		.compatible = "allwinner,sun6i-a31-display-backend",
		.data = &sun6i_backend_quirks,
	},
	{
		.compatible = "allwinner,sun7i-a20-display-backend",
		.data = &sun7i_backend_quirks,
	},
	{
		.compatible = "allwinner,sun8i-a23-display-backend",
		.data = &sun8i_a33_backend_quirks,
	},
	{
		.compatible = "allwinner,sun8i-a33-display-backend",
		.data = &sun8i_a33_backend_quirks,
	},
	{
		.compatible = "allwinner,sun9i-a80-display-backend",
		.data = &sun9i_backend_quirks,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_backend_of_table);

static struct platform_driver sun4i_backend_platform_driver = {
	.probe		= sun4i_backend_probe,
	.remove		= sun4i_backend_remove,
	.driver		= {
		.name		= "sun4i-backend",
		.of_match_table	= sun4i_backend_of_table,
	},
};
module_platform_driver(sun4i_backend_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Backend Driver");
MODULE_LICENSE("GPL");
