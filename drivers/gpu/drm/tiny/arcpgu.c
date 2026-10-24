// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/clk.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_plane.h>
#include <drm/drm_probe_helper.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#define ARCPGU_REG_CTRL		0x00
#define ARCPGU_REG_STAT		0x04
#define ARCPGU_REG_FMT		0x10
#define ARCPGU_REG_HSYNC	0x14
#define ARCPGU_REG_VSYNC	0x18
#define ARCPGU_REG_ACTIVE	0x1c
#define ARCPGU_REG_BUF0_ADDR	0x40
#define ARCPGU_REG_STRIDE	0x50
#define ARCPGU_REG_START_SET	0x84

#define ARCPGU_REG_ID		0x3FC

#define ARCPGU_CTRL_ENABLE_MASK	0x02
#define ARCPGU_CTRL_VS_POL_MASK	0x1
#define ARCPGU_CTRL_VS_POL_OFST	0x3
#define ARCPGU_CTRL_HS_POL_MASK	0x1
#define ARCPGU_CTRL_HS_POL_OFST	0x4
#define ARCPGU_MODE_XRGB8888	BIT(2)
#define ARCPGU_STAT_BUSY_MASK	0x02

struct arcpgu_drm_private {
	struct drm_device	drm;
	void __iomem		*regs;
	struct clk		*clk;
	struct drm_plane	plane;
	struct drm_crtc		crtc;
	struct drm_encoder	encoder;
	struct drm_connector	sim_conn;
};

static inline struct arcpgu_drm_private *dev_to_arcpgu(struct drm_device *drm)
{
	return container_of(drm, struct arcpgu_drm_private, drm);
}

static inline void arc_pgu_write(struct arcpgu_drm_private *arcpgu,
				 unsigned int reg, u32 value)
{
	iowrite32(value, arcpgu->regs + reg);
}

static inline u32 arc_pgu_read(struct arcpgu_drm_private *arcpgu,
			       unsigned int reg)
{
	return ioread32(arcpgu->regs + reg);
}

#define XRES_DEF	640
#define YRES_DEF	480

#define XRES_MAX	8192
#define YRES_MAX	8192

static int arcpgu_drm_connector_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, XRES_MAX, YRES_MAX);
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);
	return count;
}

static const struct drm_connector_helper_funcs
arcpgu_drm_connector_helper_funcs = {
	.get_modes = arcpgu_drm_connector_get_modes,
};

static const struct drm_connector_funcs arcpgu_drm_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int arcpgu_drm_sim_init(struct drm_device *drm, struct drm_connector *connector)
{
	drm_connector_helper_add(connector, &arcpgu_drm_connector_helper_funcs);
	return drm_connector_init(drm, connector, &arcpgu_drm_connector_funcs,
				  DRM_MODE_CONNECTOR_VIRTUAL);
}

#define ENCODE_PGU_XY(x, y)	((((x) - 1) << 16) | ((y) - 1))

static const u32 arc_pgu_supported_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static void arc_pgu_set_pxl_fmt(struct arcpgu_drm_private *arcpgu,
				const struct drm_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->fb;
	uint32_t pixel_format = fb->format->format;
	u32 format = DRM_FORMAT_INVALID;
	int i;
	u32 reg_ctrl;

	for (i = 0; i < ARRAY_SIZE(arc_pgu_supported_formats); i++) {
		if (arc_pgu_supported_formats[i] == pixel_format)
			format = arc_pgu_supported_formats[i];
	}

	if (WARN_ON(format == DRM_FORMAT_INVALID))
		return;

	reg_ctrl = arc_pgu_read(arcpgu, ARCPGU_REG_CTRL);
	if (format == DRM_FORMAT_RGB565)
		reg_ctrl &= ~ARCPGU_MODE_XRGB8888;
	else
		reg_ctrl |= ARCPGU_MODE_XRGB8888;
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL, reg_ctrl);
}

static enum drm_mode_status arcpgu_crtc_helper_mode_valid(struct drm_crtc *crtc,
							  const struct drm_display_mode *mode)
{
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(crtc->dev);
	long rate, clk_rate = mode->clock * 1000;
	long diff = clk_rate / 200; /* +-0.5% allowed by HDMI spec */

	rate = clk_round_rate(arcpgu->clk, clk_rate);
	if ((max(rate, clk_rate) - min(rate, clk_rate) < diff) && (rate > 0))
		return MODE_OK;

	return MODE_NOCLOCK;
}

static void arc_pgu_mode_set(struct arcpgu_drm_private *arcpgu,
			     struct drm_crtc_state *crtc_state,
			     struct drm_plane_state *plane_state)
{
	struct drm_display_mode *m = &crtc_state->adjusted_mode;
	u32 val;

	arc_pgu_write(arcpgu, ARCPGU_REG_FMT,
		      ENCODE_PGU_XY(m->crtc_htotal, m->crtc_vtotal));

	arc_pgu_write(arcpgu, ARCPGU_REG_HSYNC,
		      ENCODE_PGU_XY(m->crtc_hsync_start - m->crtc_hdisplay,
				    m->crtc_hsync_end - m->crtc_hdisplay));

	arc_pgu_write(arcpgu, ARCPGU_REG_VSYNC,
		      ENCODE_PGU_XY(m->crtc_vsync_start - m->crtc_vdisplay,
				    m->crtc_vsync_end - m->crtc_vdisplay));

	arc_pgu_write(arcpgu, ARCPGU_REG_ACTIVE,
		      ENCODE_PGU_XY(m->crtc_hblank_end - m->crtc_hblank_start,
				    m->crtc_vblank_end - m->crtc_vblank_start));

	val = arc_pgu_read(arcpgu, ARCPGU_REG_CTRL);

	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		val |= ARCPGU_CTRL_VS_POL_MASK << ARCPGU_CTRL_VS_POL_OFST;
	else
		val &= ~(ARCPGU_CTRL_VS_POL_MASK << ARCPGU_CTRL_VS_POL_OFST);

	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		val |= ARCPGU_CTRL_HS_POL_MASK << ARCPGU_CTRL_HS_POL_OFST;
	else
		val &= ~(ARCPGU_CTRL_HS_POL_MASK << ARCPGU_CTRL_HS_POL_OFST);

	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL, val);
	arc_pgu_write(arcpgu, ARCPGU_REG_STRIDE, 0);
	arc_pgu_write(arcpgu, ARCPGU_REG_START_SET, 1);

	arc_pgu_set_pxl_fmt(arcpgu, plane_state);

	clk_set_rate(arcpgu->clk, m->crtc_clock * 1000);
}

static void arcpgu_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					     struct drm_atomic_commit *commit)
{
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(crtc->dev);
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(commit, &arcpgu->plane);
	struct drm_crtc_state *cstate = drm_atomic_get_new_crtc_state(commit, crtc);

	arc_pgu_mode_set(arcpgu, cstate, pstate);

	clk_prepare_enable(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
		      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) |
		      ARCPGU_CTRL_ENABLE_MASK);
}

static void arcpgu_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					      struct drm_atomic_commit *commit)
{
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(crtc->dev);

	clk_disable_unprepare(arcpgu->clk);
	arc_pgu_write(arcpgu, ARCPGU_REG_CTRL,
			      arc_pgu_read(arcpgu, ARCPGU_REG_CTRL) &
			      ~ARCPGU_CTRL_ENABLE_MASK);
}

static void arcpgu_plane_helper_atomic_update(struct drm_plane *plane,
					      struct drm_atomic_commit *commit)
{
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(commit, plane);
	struct arcpgu_drm_private *arcpgu;
	struct drm_gem_dma_object *gem;

	if (!pstate->fb)
		return;

	arcpgu = dev_to_arcpgu(plane->dev);
	gem = drm_fb_dma_get_gem_obj(pstate->fb, 0);
	arc_pgu_write(arcpgu, ARCPGU_REG_BUF0_ADDR, gem->dma_addr);
}

static const struct drm_mode_config_funcs arcpgu_drm_modecfg_funcs = {
	.fb_create  = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_DMA_FOPS(arcpgu_drm_ops);

static int arcpgu_plane_helper_atomic_check(struct drm_plane *plane,
					    struct drm_atomic_commit *commit)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(commit, plane);
	struct drm_crtc_state *crtc_state = NULL;

	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_crtc_state(commit, plane_state->crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static const struct drm_plane_helper_funcs arcpgu_plane_helper_funcs = {
	.prepare_fb	= drm_gem_plane_helper_prepare_fb,
	.atomic_check	= arcpgu_plane_helper_atomic_check,
	.atomic_update	= arcpgu_plane_helper_atomic_update,
};

static const struct drm_plane_funcs arcpgu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.atomic_create_state	= drm_atomic_helper_plane_create_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int arcpgu_crtc_helper_atomic_check(struct drm_crtc *crtc,
					   struct drm_atomic_commit *commit)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(commit, crtc);
	int ret;

	if (crtc_state->enable) {
		ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
		if (ret)
			return ret;
	}

	return drm_atomic_add_affected_planes(commit, crtc);
}

static const struct drm_crtc_helper_funcs arcpgu_crtc_helper_funcs = {
	.mode_valid	= arcpgu_crtc_helper_mode_valid,
	.atomic_check	= arcpgu_crtc_helper_atomic_check,
	.atomic_enable	= arcpgu_crtc_helper_atomic_enable,
	.atomic_disable	= arcpgu_crtc_helper_atomic_disable,
};

static const struct drm_crtc_funcs arcpgu_crtc_funcs = {
	.atomic_create_state	= drm_atomic_helper_crtc_create_state,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs arcpgu_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int arcpgu_load(struct arcpgu_drm_private *arcpgu)
{
	struct platform_device *pdev = to_platform_device(arcpgu->drm.dev);
	struct device_node *encoder_node __free(device_node) = NULL;
	struct device_node *endpoint_node = NULL;
	struct drm_connector *connector = NULL;
	struct drm_device *drm = &arcpgu->drm;
	struct drm_plane *plane;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	int ret;

	arcpgu->clk = devm_clk_get(drm->dev, "pxlclk");
	if (IS_ERR(arcpgu->clk))
		return PTR_ERR(arcpgu->clk);

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 1920;
	drm->mode_config.max_height = 1080;
	drm->mode_config.funcs = &arcpgu_drm_modecfg_funcs;

	arcpgu->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(arcpgu->regs))
		return PTR_ERR(arcpgu->regs);

	dev_info(drm->dev, "arc_pgu ID: 0x%x\n",
		 arc_pgu_read(arcpgu, ARCPGU_REG_ID));

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(drm->dev);
	if (ret && ret != -ENODEV)
		return ret;

	if (dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32)))
		return -ENODEV;

	plane = &arcpgu->plane;
	ret = drm_universal_plane_init(drm, plane, 0,
				       &arcpgu_plane_funcs,
				       arc_pgu_supported_formats,
				       ARRAY_SIZE(arc_pgu_supported_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &arcpgu_plane_helper_funcs);

	crtc = &arcpgu->crtc;
	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL,
					&arcpgu_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &arcpgu_crtc_helper_funcs);

	encoder = &arcpgu->encoder;
	ret = drm_encoder_init(drm, encoder, &arcpgu_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/*
	 * There is only one output port inside each device. It is linked with
	 * encoder endpoint.
	 */
	endpoint_node = of_graph_get_endpoint_by_regs(pdev->dev.of_node, 0, -1);
	if (endpoint_node) {
		encoder_node = of_graph_get_remote_port_parent(endpoint_node);
		of_node_put(endpoint_node);
		if (!encoder_node)
			return -ENODEV;

		/* Locate drm bridge from the hdmi encoder DT node */
		struct drm_bridge *bridge __free(drm_bridge_put) =
			of_drm_find_and_get_bridge(encoder_node);
		if (!bridge)
			return -EPROBE_DEFER;

		ret = drm_bridge_attach(encoder, bridge, NULL, 0);
		if (ret)
			return ret;
	} else {
		connector = &arcpgu->sim_conn;
		dev_info(drm->dev, "no encoder found. Assumed virtual LCD on simulation platform\n");
		ret = arcpgu_drm_sim_init(drm, connector);
		if (ret < 0)
			return ret;

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret)
			return ret;
	}

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, drm);
	return 0;
}

static int arcpgu_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int arcpgu_show_pxlclock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *drm = node->minor->dev;
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(drm);
	unsigned long clkrate = clk_get_rate(arcpgu->clk);
	unsigned long mode_clock;
	int ret;

	ret = drm_modeset_lock_single_interruptible(&arcpgu->crtc.mutex);
	if (ret)
		return ret;
	mode_clock = arcpgu->crtc.state->mode.crtc_clock * 1000;
	drm_modeset_unlock(&arcpgu->crtc.mutex);

	seq_printf(m, "hw  : %lu\n", clkrate);
	seq_printf(m, "mode: %lu\n", mode_clock);
	return 0;
}

static struct drm_info_list arcpgu_debugfs_list[] = {
	{ "clocks", arcpgu_show_pxlclock, 0 },
};

static void arcpgu_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(arcpgu_debugfs_list,
				 ARRAY_SIZE(arcpgu_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif

static const struct drm_driver arcpgu_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.name = "arcpgu",
	.desc = "ARC PGU Controller",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &arcpgu_drm_ops,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = arcpgu_debugfs_init,
#endif
};

static int arcpgu_probe(struct platform_device *pdev)
{
	struct arcpgu_drm_private *arcpgu;
	int ret;

	arcpgu = devm_drm_dev_alloc(&pdev->dev, &arcpgu_drm_driver,
				    struct arcpgu_drm_private, drm);
	if (IS_ERR(arcpgu))
		return PTR_ERR(arcpgu);

	ret = arcpgu_load(arcpgu);
	if (ret)
		return ret;

	ret = drm_dev_register(&arcpgu->drm, 0);
	if (ret)
		goto err_unload;

	drm_client_setup_with_fourcc(&arcpgu->drm, DRM_FORMAT_RGB565);

	return 0;

err_unload:
	arcpgu_unload(&arcpgu->drm);

	return ret;
}

static void arcpgu_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	arcpgu_unload(drm);
}

static const struct of_device_id arcpgu_of_table[] = {
	{.compatible = "snps,arcpgu"},
	{}
};

MODULE_DEVICE_TABLE(of, arcpgu_of_table);

static struct platform_driver arcpgu_platform_driver = {
	.probe = arcpgu_probe,
	.remove = arcpgu_remove,
	.driver = {
		   .name = "arcpgu",
		   .of_match_table = arcpgu_of_table,
		   },
};

drm_module_platform_driver(arcpgu_platform_driver);

MODULE_AUTHOR("Carlos Palminha <palminha@synopsys.com>");
MODULE_DESCRIPTION("ARC PGU DRM driver");
MODULE_LICENSE("GPL");
