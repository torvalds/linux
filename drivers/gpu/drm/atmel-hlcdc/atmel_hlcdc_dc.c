/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "atmel_hlcdc_dc.h"

#define ATMEL_HLCDC_LAYER_IRQS_OFFSET		8

static const struct atmel_hlcdc_layer_desc atmel_hlcdc_at91sam9n12_layers[] = {
	{
		.name = "base",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x40,
		.id = 0,
		.type = ATMEL_HLCDC_BASE_LAYER,
		.nconfigs = 5,
		.layout = {
			.xstride = { 2 },
			.default_color = 3,
			.general_config = 4,
		},
	},
};

static const struct atmel_hlcdc_dc_desc atmel_hlcdc_dc_at91sam9n12 = {
	.min_width = 0,
	.min_height = 0,
	.max_width = 1280,
	.max_height = 860,
	.max_spw = 0x3f,
	.max_vpw = 0x3f,
	.max_hpw = 0xff,
	.conflicting_output_formats = true,
	.nlayers = ARRAY_SIZE(atmel_hlcdc_at91sam9n12_layers),
	.layers = atmel_hlcdc_at91sam9n12_layers,
};

static const struct atmel_hlcdc_layer_desc atmel_hlcdc_at91sam9x5_layers[] = {
	{
		.name = "base",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x40,
		.id = 0,
		.type = ATMEL_HLCDC_BASE_LAYER,
		.nconfigs = 5,
		.layout = {
			.xstride = { 2 },
			.default_color = 3,
			.general_config = 4,
			.disc_pos = 5,
			.disc_size = 6,
		},
	},
	{
		.name = "overlay1",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x100,
		.id = 1,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 10,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
	{
		.name = "high-end-overlay",
		.formats = &atmel_hlcdc_plane_rgb_and_yuv_formats,
		.regs_offset = 0x280,
		.id = 2,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 17,
		.layout = {
			.pos = 2,
			.size = 3,
			.memsize = 4,
			.xstride = { 5, 7 },
			.pstride = { 6, 8 },
			.default_color = 9,
			.chroma_key = 10,
			.chroma_key_mask = 11,
			.general_config = 12,
			.csc = 14,
		},
	},
	{
		.name = "cursor",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x340,
		.id = 3,
		.type = ATMEL_HLCDC_CURSOR_LAYER,
		.nconfigs = 10,
		.max_width = 128,
		.max_height = 128,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
};

static const struct atmel_hlcdc_dc_desc atmel_hlcdc_dc_at91sam9x5 = {
	.min_width = 0,
	.min_height = 0,
	.max_width = 800,
	.max_height = 600,
	.max_spw = 0x3f,
	.max_vpw = 0x3f,
	.max_hpw = 0xff,
	.conflicting_output_formats = true,
	.nlayers = ARRAY_SIZE(atmel_hlcdc_at91sam9x5_layers),
	.layers = atmel_hlcdc_at91sam9x5_layers,
};

static const struct atmel_hlcdc_layer_desc atmel_hlcdc_sama5d3_layers[] = {
	{
		.name = "base",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x40,
		.id = 0,
		.type = ATMEL_HLCDC_BASE_LAYER,
		.nconfigs = 7,
		.layout = {
			.xstride = { 2 },
			.default_color = 3,
			.general_config = 4,
			.disc_pos = 5,
			.disc_size = 6,
		},
	},
	{
		.name = "overlay1",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x140,
		.id = 1,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 10,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
	{
		.name = "overlay2",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x240,
		.id = 2,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 10,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
	{
		.name = "high-end-overlay",
		.formats = &atmel_hlcdc_plane_rgb_and_yuv_formats,
		.regs_offset = 0x340,
		.id = 3,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 42,
		.layout = {
			.pos = 2,
			.size = 3,
			.memsize = 4,
			.xstride = { 5, 7 },
			.pstride = { 6, 8 },
			.default_color = 9,
			.chroma_key = 10,
			.chroma_key_mask = 11,
			.general_config = 12,
			.csc = 14,
		},
	},
	{
		.name = "cursor",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x440,
		.id = 4,
		.type = ATMEL_HLCDC_CURSOR_LAYER,
		.nconfigs = 10,
		.max_width = 128,
		.max_height = 128,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
};

static const struct atmel_hlcdc_dc_desc atmel_hlcdc_dc_sama5d3 = {
	.min_width = 0,
	.min_height = 0,
	.max_width = 2048,
	.max_height = 2048,
	.max_spw = 0x3f,
	.max_vpw = 0x3f,
	.max_hpw = 0x1ff,
	.conflicting_output_formats = true,
	.nlayers = ARRAY_SIZE(atmel_hlcdc_sama5d3_layers),
	.layers = atmel_hlcdc_sama5d3_layers,
};

static const struct atmel_hlcdc_layer_desc atmel_hlcdc_sama5d4_layers[] = {
	{
		.name = "base",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x40,
		.id = 0,
		.type = ATMEL_HLCDC_BASE_LAYER,
		.nconfigs = 7,
		.layout = {
			.xstride = { 2 },
			.default_color = 3,
			.general_config = 4,
			.disc_pos = 5,
			.disc_size = 6,
		},
	},
	{
		.name = "overlay1",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x140,
		.id = 1,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 10,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
	{
		.name = "overlay2",
		.formats = &atmel_hlcdc_plane_rgb_formats,
		.regs_offset = 0x240,
		.id = 2,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 10,
		.layout = {
			.pos = 2,
			.size = 3,
			.xstride = { 4 },
			.pstride = { 5 },
			.default_color = 6,
			.chroma_key = 7,
			.chroma_key_mask = 8,
			.general_config = 9,
		},
	},
	{
		.name = "high-end-overlay",
		.formats = &atmel_hlcdc_plane_rgb_and_yuv_formats,
		.regs_offset = 0x340,
		.id = 3,
		.type = ATMEL_HLCDC_OVERLAY_LAYER,
		.nconfigs = 42,
		.layout = {
			.pos = 2,
			.size = 3,
			.memsize = 4,
			.xstride = { 5, 7 },
			.pstride = { 6, 8 },
			.default_color = 9,
			.chroma_key = 10,
			.chroma_key_mask = 11,
			.general_config = 12,
			.csc = 14,
		},
	},
};

static const struct atmel_hlcdc_dc_desc atmel_hlcdc_dc_sama5d4 = {
	.min_width = 0,
	.min_height = 0,
	.max_width = 2048,
	.max_height = 2048,
	.max_spw = 0xff,
	.max_vpw = 0xff,
	.max_hpw = 0x3ff,
	.nlayers = ARRAY_SIZE(atmel_hlcdc_sama5d4_layers),
	.layers = atmel_hlcdc_sama5d4_layers,
};
static const struct of_device_id atmel_hlcdc_of_match[] = {
	{
		.compatible = "atmel,at91sam9n12-hlcdc",
		.data = &atmel_hlcdc_dc_at91sam9n12,
	},
	{
		.compatible = "atmel,at91sam9x5-hlcdc",
		.data = &atmel_hlcdc_dc_at91sam9x5,
	},
	{
		.compatible = "atmel,sama5d2-hlcdc",
		.data = &atmel_hlcdc_dc_sama5d4,
	},
	{
		.compatible = "atmel,sama5d3-hlcdc",
		.data = &atmel_hlcdc_dc_sama5d3,
	},
	{
		.compatible = "atmel,sama5d4-hlcdc",
		.data = &atmel_hlcdc_dc_sama5d4,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, atmel_hlcdc_of_match);

int atmel_hlcdc_dc_mode_valid(struct atmel_hlcdc_dc *dc,
			      struct drm_display_mode *mode)
{
	int vfront_porch = mode->vsync_start - mode->vdisplay;
	int vback_porch = mode->vtotal - mode->vsync_end;
	int vsync_len = mode->vsync_end - mode->vsync_start;
	int hfront_porch = mode->hsync_start - mode->hdisplay;
	int hback_porch = mode->htotal - mode->hsync_end;
	int hsync_len = mode->hsync_end - mode->hsync_start;

	if (hsync_len > dc->desc->max_spw + 1 || hsync_len < 1)
		return MODE_HSYNC;

	if (vsync_len > dc->desc->max_spw + 1 || vsync_len < 1)
		return MODE_VSYNC;

	if (hfront_porch > dc->desc->max_hpw + 1 || hfront_porch < 1 ||
	    hback_porch > dc->desc->max_hpw + 1 || hback_porch < 1 ||
	    mode->hdisplay < 1)
		return MODE_H_ILLEGAL;

	if (vfront_porch > dc->desc->max_vpw + 1 || vfront_porch < 1 ||
	    vback_porch > dc->desc->max_vpw || vback_porch < 0 ||
	    mode->vdisplay < 1)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

static irqreturn_t atmel_hlcdc_dc_irq_handler(int irq, void *data)
{
	struct drm_device *dev = data;
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	unsigned long status;
	unsigned int imr, isr;
	int i;

	regmap_read(dc->hlcdc->regmap, ATMEL_HLCDC_IMR, &imr);
	regmap_read(dc->hlcdc->regmap, ATMEL_HLCDC_ISR, &isr);
	status = imr & isr;
	if (!status)
		return IRQ_NONE;

	if (status & ATMEL_HLCDC_SOF)
		atmel_hlcdc_crtc_irq(dc->crtc);

	for (i = 0; i < ATMEL_HLCDC_MAX_LAYERS; i++) {
		struct atmel_hlcdc_layer *layer = dc->layers[i];

		if (!(ATMEL_HLCDC_LAYER_STATUS(i) & status) || !layer)
			continue;

		atmel_hlcdc_layer_irq(layer);
	}

	return IRQ_HANDLED;
}

static struct drm_framebuffer *atmel_hlcdc_fb_create(struct drm_device *dev,
		struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static void atmel_hlcdc_fb_output_poll_changed(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;

	if (dc->fbdev)
		drm_fbdev_cma_hotplug_event(dc->fbdev);
}

struct atmel_hlcdc_dc_commit {
	struct work_struct work;
	struct drm_device *dev;
	struct drm_atomic_state *state;
};

static void
atmel_hlcdc_dc_atomic_complete(struct atmel_hlcdc_dc_commit *commit)
{
	struct drm_device *dev = commit->dev;
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct drm_atomic_state *old_state = commit->state;

	/* Apply the atomic update. */
	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);

	drm_atomic_state_put(old_state);

	/* Complete the commit, wake up any waiter. */
	spin_lock(&dc->commit.wait.lock);
	dc->commit.pending = false;
	wake_up_all_locked(&dc->commit.wait);
	spin_unlock(&dc->commit.wait.lock);

	kfree(commit);
}

static void atmel_hlcdc_dc_atomic_work(struct work_struct *work)
{
	struct atmel_hlcdc_dc_commit *commit =
		container_of(work, struct atmel_hlcdc_dc_commit, work);

	atmel_hlcdc_dc_atomic_complete(commit);
}

static int atmel_hlcdc_dc_atomic_commit(struct drm_device *dev,
					struct drm_atomic_state *state,
					bool async)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_dc_commit *commit;
	int ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/* Allocate the commit object. */
	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (!commit) {
		ret = -ENOMEM;
		goto error;
	}

	INIT_WORK(&commit->work, atmel_hlcdc_dc_atomic_work);
	commit->dev = dev;
	commit->state = state;

	spin_lock(&dc->commit.wait.lock);
	ret = wait_event_interruptible_locked(dc->commit.wait,
					      !dc->commit.pending);
	if (ret == 0)
		dc->commit.pending = true;
	spin_unlock(&dc->commit.wait.lock);

	if (ret) {
		kfree(commit);
		goto error;
	}

	/* Swap the state, this is the point of no return. */
	drm_atomic_helper_swap_state(state, true);

	drm_atomic_state_get(state);
	if (async)
		queue_work(dc->wq, &commit->work);
	else
		atmel_hlcdc_dc_atomic_complete(commit);

	return 0;

error:
	drm_atomic_helper_cleanup_planes(dev, state);
	return ret;
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = atmel_hlcdc_fb_create,
	.output_poll_changed = atmel_hlcdc_fb_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = atmel_hlcdc_dc_atomic_commit,
};

static int atmel_hlcdc_dc_modeset_init(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_planes *planes;
	int ret;
	int i;

	drm_mode_config_init(dev);

	ret = atmel_hlcdc_create_outputs(dev);
	if (ret) {
		dev_err(dev->dev, "failed to create HLCDC outputs: %d\n", ret);
		return ret;
	}

	planes = atmel_hlcdc_create_planes(dev);
	if (IS_ERR(planes)) {
		dev_err(dev->dev, "failed to create planes\n");
		return PTR_ERR(planes);
	}

	dc->planes = planes;

	dc->layers[planes->primary->layer.desc->id] =
						&planes->primary->layer;

	if (planes->cursor)
		dc->layers[planes->cursor->layer.desc->id] =
							&planes->cursor->layer;

	for (i = 0; i < planes->noverlays; i++)
		dc->layers[planes->overlays[i]->layer.desc->id] =
						&planes->overlays[i]->layer;

	ret = atmel_hlcdc_crtc_create(dev);
	if (ret) {
		dev_err(dev->dev, "failed to create crtc\n");
		return ret;
	}

	dev->mode_config.min_width = dc->desc->min_width;
	dev->mode_config.min_height = dc->desc->min_height;
	dev->mode_config.max_width = dc->desc->max_width;
	dev->mode_config.max_height = dc->desc->max_height;
	dev->mode_config.funcs = &mode_config_funcs;

	return 0;
}

static int atmel_hlcdc_dc_load(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	const struct of_device_id *match;
	struct atmel_hlcdc_dc *dc;
	int ret;

	match = of_match_node(atmel_hlcdc_of_match, dev->dev->parent->of_node);
	if (!match) {
		dev_err(&pdev->dev, "invalid compatible string\n");
		return -ENODEV;
	}

	if (!match->data) {
		dev_err(&pdev->dev, "invalid hlcdc description\n");
		return -EINVAL;
	}

	dc = devm_kzalloc(dev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	dc->wq = alloc_ordered_workqueue("atmel-hlcdc-dc", 0);
	if (!dc->wq)
		return -ENOMEM;

	init_waitqueue_head(&dc->commit.wait);
	dc->desc = match->data;
	dc->hlcdc = dev_get_drvdata(dev->dev->parent);
	dev->dev_private = dc;

	ret = clk_prepare_enable(dc->hlcdc->periph_clk);
	if (ret) {
		dev_err(dev->dev, "failed to enable periph_clk\n");
		goto err_destroy_wq;
	}

	pm_runtime_enable(dev->dev);

	ret = drm_vblank_init(dev, 1);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		goto err_periph_clk_disable;
	}

	ret = atmel_hlcdc_dc_modeset_init(dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize mode setting\n");
		goto err_periph_clk_disable;
	}

	drm_mode_config_reset(dev);

	pm_runtime_get_sync(dev->dev);
	ret = drm_irq_install(dev, dc->hlcdc->irq);
	pm_runtime_put_sync(dev->dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to install IRQ handler\n");
		goto err_periph_clk_disable;
	}

	platform_set_drvdata(pdev, dev);

	dc->fbdev = drm_fbdev_cma_init(dev, 24,
			dev->mode_config.num_connector);
	if (IS_ERR(dc->fbdev))
		dc->fbdev = NULL;

	drm_kms_helper_poll_init(dev);

	return 0;

err_periph_clk_disable:
	pm_runtime_disable(dev->dev);
	clk_disable_unprepare(dc->hlcdc->periph_clk);

err_destroy_wq:
	destroy_workqueue(dc->wq);

	return ret;
}

static void atmel_hlcdc_dc_unload(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;

	if (dc->fbdev)
		drm_fbdev_cma_fini(dc->fbdev);
	flush_workqueue(dc->wq);
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);

	pm_runtime_get_sync(dev->dev);
	drm_irq_uninstall(dev);
	pm_runtime_put_sync(dev->dev);

	dev->dev_private = NULL;

	pm_runtime_disable(dev->dev);
	clk_disable_unprepare(dc->hlcdc->periph_clk);
	destroy_workqueue(dc->wq);
}

static void atmel_hlcdc_dc_lastclose(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;

	drm_fbdev_cma_restore_mode(dc->fbdev);
}

static int atmel_hlcdc_dc_irq_postinstall(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	unsigned int cfg = 0;
	int i;

	/* Enable interrupts on activated layers */
	for (i = 0; i < ATMEL_HLCDC_MAX_LAYERS; i++) {
		if (dc->layers[i])
			cfg |= ATMEL_HLCDC_LAYER_STATUS(i);
	}

	regmap_write(dc->hlcdc->regmap, ATMEL_HLCDC_IER, cfg);

	return 0;
}

static void atmel_hlcdc_dc_irq_uninstall(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	unsigned int isr;

	regmap_write(dc->hlcdc->regmap, ATMEL_HLCDC_IDR, 0xffffffff);
	regmap_read(dc->hlcdc->regmap, ATMEL_HLCDC_ISR, &isr);
}

static int atmel_hlcdc_dc_enable_vblank(struct drm_device *dev,
					unsigned int pipe)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;

	/* Enable SOF (Start Of Frame) interrupt for vblank counting */
	regmap_write(dc->hlcdc->regmap, ATMEL_HLCDC_IER, ATMEL_HLCDC_SOF);

	return 0;
}

static void atmel_hlcdc_dc_disable_vblank(struct drm_device *dev,
					  unsigned int pipe)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;

	regmap_write(dc->hlcdc->regmap, ATMEL_HLCDC_IDR, ATMEL_HLCDC_SOF);
}

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
	.compat_ioctl       = drm_compat_ioctl,
	.poll               = drm_poll,
	.read               = drm_read,
	.llseek             = no_llseek,
	.mmap               = drm_gem_cma_mmap,
};

static struct drm_driver atmel_hlcdc_dc_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_GEM |
			   DRIVER_MODESET | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.lastclose = atmel_hlcdc_dc_lastclose,
	.irq_handler = atmel_hlcdc_dc_irq_handler,
	.irq_preinstall = atmel_hlcdc_dc_irq_uninstall,
	.irq_postinstall = atmel_hlcdc_dc_irq_postinstall,
	.irq_uninstall = atmel_hlcdc_dc_irq_uninstall,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.enable_vblank = atmel_hlcdc_dc_enable_vblank,
	.disable_vblank = atmel_hlcdc_dc_disable_vblank,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.fops = &fops,
	.name = "atmel-hlcdc",
	.desc = "Atmel HLCD Controller DRM",
	.date = "20141504",
	.major = 1,
	.minor = 0,
};

static int atmel_hlcdc_dc_drm_probe(struct platform_device *pdev)
{
	struct drm_device *ddev;
	int ret;

	ddev = drm_dev_alloc(&atmel_hlcdc_dc_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ret = atmel_hlcdc_dc_load(ddev);
	if (ret)
		goto err_unref;

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_unload;

	return 0;

err_unload:
	atmel_hlcdc_dc_unload(ddev);

err_unref:
	drm_dev_unref(ddev);

	return ret;
}

static int atmel_hlcdc_dc_drm_remove(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);

	drm_dev_unregister(ddev);
	atmel_hlcdc_dc_unload(ddev);
	drm_dev_unref(ddev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int atmel_hlcdc_dc_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct drm_crtc *crtc;

	if (pm_runtime_suspended(dev))
		return 0;

	drm_modeset_lock_all(drm_dev);
	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head)
		atmel_hlcdc_crtc_suspend(crtc);
	drm_modeset_unlock_all(drm_dev);
	return 0;
}

static int atmel_hlcdc_dc_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct drm_crtc *crtc;

	if (pm_runtime_suspended(dev))
		return 0;

	drm_modeset_lock_all(drm_dev);
	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head)
		atmel_hlcdc_crtc_resume(crtc);
	drm_modeset_unlock_all(drm_dev);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(atmel_hlcdc_dc_drm_pm_ops,
		atmel_hlcdc_dc_drm_suspend, atmel_hlcdc_dc_drm_resume);

static const struct of_device_id atmel_hlcdc_dc_of_match[] = {
	{ .compatible = "atmel,hlcdc-display-controller" },
	{ },
};

static struct platform_driver atmel_hlcdc_dc_platform_driver = {
	.probe	= atmel_hlcdc_dc_drm_probe,
	.remove	= atmel_hlcdc_dc_drm_remove,
	.driver	= {
		.name	= "atmel-hlcdc-display-controller",
		.pm	= &atmel_hlcdc_dc_drm_pm_ops,
		.of_match_table = atmel_hlcdc_dc_of_match,
	},
};
module_platform_driver(atmel_hlcdc_dc_platform_driver);

MODULE_AUTHOR("Jean-Jacques Hiblot <jjhiblot@traphandler.com>");
MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("Atmel HLCDC Display Controller DRM Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel-hlcdc-dc");
