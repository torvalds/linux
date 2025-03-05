// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: YT SHEN <yt.shen@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_gem.h"

#define DRIVER_NAME "mediatek"
#define DRIVER_DESC "Mediatek SoC DRM"
#define DRIVER_DATE "20150513"
#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0

static const struct drm_mode_config_helper_funcs mtk_drm_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static struct drm_framebuffer *
mtk_drm_mode_fb_create(struct drm_device *dev,
		       struct drm_file *file,
		       const struct drm_mode_fb_cmd2 *cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev, cmd);

	if (info->num_planes != 1)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, file, cmd);
}

static const struct drm_mode_config_funcs mtk_drm_mode_config_funcs = {
	.fb_create = mtk_drm_mode_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const enum mtk_ddp_comp_id mt2701_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_BLS,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt2701_mtk_ddp_ext[] = {
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt7623_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_BLS,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt7623_mtk_ddp_ext[] = {
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt2712_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_OD0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_DPI0,
	DDP_COMPONENT_PWM0,
};

static const enum mtk_ddp_comp_id mt2712_mtk_ddp_ext[] = {
	DDP_COMPONENT_OVL1,
	DDP_COMPONENT_COLOR1,
	DDP_COMPONENT_AAL1,
	DDP_COMPONENT_OD1,
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DPI1,
	DDP_COMPONENT_PWM1,
};

static const enum mtk_ddp_comp_id mt2712_mtk_ddp_third[] = {
	DDP_COMPONENT_RDMA2,
	DDP_COMPONENT_DSI3,
	DDP_COMPONENT_PWM2,
};

static enum mtk_ddp_comp_id mt8167_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_DITHER0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt8173_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_OD0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_UFOE,
	DDP_COMPONENT_DSI0,
	DDP_COMPONENT_PWM0,
};

static const enum mtk_ddp_comp_id mt8173_mtk_ddp_ext[] = {
	DDP_COMPONENT_OVL1,
	DDP_COMPONENT_COLOR1,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt8183_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_OVL_2L0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_DITHER0,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt8183_mtk_ddp_ext[] = {
	DDP_COMPONENT_OVL_2L1,
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt8186_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_POSTMASK0,
	DDP_COMPONENT_DITHER0,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt8186_mtk_ddp_ext[] = {
	DDP_COMPONENT_OVL_2L0,
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt8192_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_OVL_2L0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_POSTMASK0,
	DDP_COMPONENT_DITHER0,
	DDP_COMPONENT_DSI0,
};

static const enum mtk_ddp_comp_id mt8192_mtk_ddp_ext[] = {
	DDP_COMPONENT_OVL_2L2,
	DDP_COMPONENT_RDMA4,
	DDP_COMPONENT_DPI0,
};

static const enum mtk_ddp_comp_id mt8195_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_DITHER0,
	DDP_COMPONENT_DSC0,
	DDP_COMPONENT_MERGE0,
	DDP_COMPONENT_DP_INTF0,
};

static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {
	.main_path = mt2701_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt2701_mtk_ddp_main),
	.ext_path = mt2701_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt2701_mtk_ddp_ext),
	.shadow_register = true,
};

static const struct mtk_mmsys_match_data mt2701_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt2701_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt7623_mmsys_driver_data = {
	.main_path = mt7623_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt7623_mtk_ddp_main),
	.ext_path = mt7623_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt7623_mtk_ddp_ext),
	.shadow_register = true,
};

static const struct mtk_mmsys_match_data mt7623_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt7623_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt2712_mmsys_driver_data = {
	.main_path = mt2712_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt2712_mtk_ddp_main),
	.ext_path = mt2712_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt2712_mtk_ddp_ext),
	.third_path = mt2712_mtk_ddp_third,
	.third_len = ARRAY_SIZE(mt2712_mtk_ddp_third),
};

static const struct mtk_mmsys_match_data mt2712_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt2712_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8167_mmsys_driver_data = {
	.main_path = mt8167_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8167_mtk_ddp_main),
};

static const struct mtk_mmsys_match_data mt8167_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8167_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8173_mmsys_driver_data = {
	.main_path = mt8173_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8173_mtk_ddp_main),
	.ext_path = mt8173_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt8173_mtk_ddp_ext),
};

static const struct mtk_mmsys_match_data mt8173_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8173_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8183_mmsys_driver_data = {
	.main_path = mt8183_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8183_mtk_ddp_main),
	.ext_path = mt8183_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt8183_mtk_ddp_ext),
};

static const struct mtk_mmsys_match_data mt8183_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8183_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8186_mmsys_driver_data = {
	.main_path = mt8186_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8186_mtk_ddp_main),
	.ext_path = mt8186_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt8186_mtk_ddp_ext),
};

static const struct mtk_mmsys_match_data mt8186_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8186_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8192_mmsys_driver_data = {
	.main_path = mt8192_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8192_mtk_ddp_main),
	.ext_path = mt8192_mtk_ddp_ext,
	.ext_len = ARRAY_SIZE(mt8192_mtk_ddp_ext),
};

static const struct mtk_mmsys_match_data mt8192_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8192_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8195_vdosys0_driver_data = {
	.io_start = 0x1c01a000,
	.main_path = mt8195_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt8195_mtk_ddp_main),
};

static const struct mtk_mmsys_driver_data mt8195_vdosys1_driver_data = {
	.io_start = 0x1c100000,
};

static const struct mtk_mmsys_match_data mt8195_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8195_vdosys0_driver_data,
		&mt8195_vdosys1_driver_data,
	},
};

static int mtk_drm_kms_init(struct drm_device *drm)
{
	struct mtk_drm_private *private = drm->dev_private;
	struct platform_device *pdev;
	struct device_node *np;
	struct device *dma_dev;
	int ret;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	if (!iommu_present(&platform_bus_type))
		return -EPROBE_DEFER;

	pdev = of_find_device_by_node(private->mutex_node);
	if (!pdev) {
		dev_err(drm->dev, "Waiting for disp-mutex device %pOF\n",
			private->mutex_node);
		of_node_put(private->mutex_node);
		return -EPROBE_DEFER;
	}
	private->mutex_dev = &pdev->dev;

	ret = drmm_mode_config_init(drm);
	if (ret)
		goto put_mutex_dev;

	drm->mode_config.min_width = 64;
	drm->mode_config.min_height = 64;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &mtk_drm_mode_config_funcs;
	drm->mode_config.helper_private = &mtk_drm_mode_config_helpers;

	ret = component_bind_all(drm->dev, drm);
	if (ret)
		goto put_mutex_dev;

	/*
	 * We currently support two fixed data streams, each optional,
	 * and each statically assigned to a crtc:
	 * OVL0 -> COLOR0 -> AAL -> OD -> RDMA0 -> UFOE -> DSI0 ...
	 */
	ret = mtk_drm_crtc_create(drm, private->data->main_path,
				  private->data->main_len);
	if (ret < 0)
		goto err_component_unbind;
	/* ... and OVL1 -> COLOR1 -> GAMMA -> RDMA1 -> DPI0. */
	ret = mtk_drm_crtc_create(drm, private->data->ext_path,
				  private->data->ext_len);
	if (ret < 0)
		goto err_component_unbind;

	ret = mtk_drm_crtc_create(drm, private->data->third_path,
				  private->data->third_len);
	if (ret < 0)
		goto err_component_unbind;

	/* Use OVL device for all DMA memory allocations */
	np = private->comp_node[private->data->main_path[0]] ?:
	     private->comp_node[private->data->ext_path[0]];
	pdev = of_find_device_by_node(np);
	if (!pdev) {
		ret = -ENODEV;
		dev_err(drm->dev, "Need at least one OVL device\n");
		goto err_component_unbind;
	}

	dma_dev = &pdev->dev;
	private->dma_dev = dma_dev;

	/*
	 * Configure the DMA segment size to make sure we get contiguous IOVA
	 * when importing PRIME buffers.
	 */
	ret = dma_set_max_seg_size(dma_dev, UINT_MAX);
	if (ret) {
		dev_err(dma_dev, "Failed to set DMA segment size\n");
		goto err_component_unbind;
	}

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret < 0)
		goto err_component_unbind;

	drm_kms_helper_poll_init(drm);
	drm_mode_config_reset(drm);

	return 0;

err_component_unbind:
	component_unbind_all(drm->dev, drm);
put_mutex_dev:
	put_device(private->mutex_dev);
	return ret;
}

static void mtk_drm_kms_deinit(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);

	component_unbind_all(drm->dev, drm);
}

DEFINE_DRM_GEM_FOPS(mtk_drm_fops);

/*
 * We need to override this because the device used to import the memory is
 * not dev->dev, as drm_gem_prime_import() expects.
 */
static struct drm_gem_object *mtk_drm_gem_prime_import(struct drm_device *dev,
						       struct dma_buf *dma_buf)
{
	struct mtk_drm_private *private = dev->dev_private;

	return drm_gem_prime_import_dev(dev, dma_buf, private->dma_dev);
}

static const struct drm_driver mtk_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.dumb_create = mtk_drm_gem_dumb_create,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = mtk_drm_gem_prime_import,
	.gem_prime_import_sg_table = mtk_gem_prime_import_sg_table,
	.gem_prime_mmap = drm_gem_prime_mmap,
	.fops = &mtk_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

static int mtk_drm_bind(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&mtk_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	drm->dev_private = private;
	private->drm = drm;

	ret = mtk_drm_kms_init(drm);
	if (ret < 0)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_deinit;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_deinit:
	mtk_drm_kms_deinit(drm);
err_free:
	private->drm = NULL;
	drm_dev_put(drm);
	return ret;
}

static void mtk_drm_unbind(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);

	drm_dev_unregister(private->drm);
	mtk_drm_kms_deinit(private->drm);
	drm_dev_put(private->drm);
	private->num_pipes = 0;
	private->drm = NULL;
}

static const struct component_master_ops mtk_drm_ops = {
	.bind		= mtk_drm_bind,
	.unbind		= mtk_drm_unbind,
};

static const struct of_device_id mtk_ddp_comp_dt_ids[] = {
	{ .compatible = "mediatek,mt8167-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8173-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8183-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8192-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8167-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt8183-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt8192-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt2701-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8167-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8173-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8167-disp-dither",
	  .data = (void *)MTK_DISP_DITHER },
	{ .compatible = "mediatek,mt8183-disp-dither",
	  .data = (void *)MTK_DISP_DITHER },
	{ .compatible = "mediatek,mt8195-disp-dsc",
	  .data = (void *)MTK_DISP_DSC },
	{ .compatible = "mediatek,mt8167-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8173-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8183-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8195-disp-merge",
	  .data = (void *)MTK_DISP_MERGE },
	{ .compatible = "mediatek,mt2701-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt2712-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8167-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8173-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8183-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8186-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8192-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8195-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8173-disp-od",
	  .data = (void *)MTK_DISP_OD },
	{ .compatible = "mediatek,mt2701-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8167-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8173-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8183-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8192-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8195-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8183-disp-ovl-2l",
	  .data = (void *)MTK_DISP_OVL_2L },
	{ .compatible = "mediatek,mt8192-disp-ovl-2l",
	  .data = (void *)MTK_DISP_OVL_2L },
	{ .compatible = "mediatek,mt8192-disp-postmask",
	  .data = (void *)MTK_DISP_POSTMASK },
	{ .compatible = "mediatek,mt2701-disp-pwm",
	  .data = (void *)MTK_DISP_BLS },
	{ .compatible = "mediatek,mt8167-disp-pwm",
	  .data = (void *)MTK_DISP_PWM },
	{ .compatible = "mediatek,mt8173-disp-pwm",
	  .data = (void *)MTK_DISP_PWM },
	{ .compatible = "mediatek,mt2701-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8167-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8173-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8183-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8195-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8173-disp-ufoe",
	  .data = (void *)MTK_DISP_UFOE },
	{ .compatible = "mediatek,mt8173-disp-wdma",
	  .data = (void *)MTK_DISP_WDMA },
	{ .compatible = "mediatek,mt2701-dpi",
	  .data = (void *)MTK_DPI },
	{ .compatible = "mediatek,mt8167-dsi",
	  .data = (void *)MTK_DSI },
	{ .compatible = "mediatek,mt8173-dpi",
	  .data = (void *)MTK_DPI },
	{ .compatible = "mediatek,mt8183-dpi",
	  .data = (void *)MTK_DPI },
	{ .compatible = "mediatek,mt8192-dpi",
	  .data = (void *)MTK_DPI },
	{ .compatible = "mediatek,mt8195-dp-intf",
	  .data = (void *)MTK_DP_INTF },
	{ .compatible = "mediatek,mt2701-dsi",
	  .data = (void *)MTK_DSI },
	{ .compatible = "mediatek,mt8173-dsi",
	  .data = (void *)MTK_DSI },
	{ .compatible = "mediatek,mt8183-dsi",
	  .data = (void *)MTK_DSI },
	{ .compatible = "mediatek,mt8186-dsi",
	  .data = (void *)MTK_DSI },
	{ }
};

static const struct of_device_id mtk_drm_of_ids[] = {
	{ .compatible = "mediatek,mt2701-mmsys",
	  .data = &mt2701_mmsys_match_data},
	{ .compatible = "mediatek,mt7623-mmsys",
	  .data = &mt7623_mmsys_match_data},
	{ .compatible = "mediatek,mt2712-mmsys",
	  .data = &mt2712_mmsys_match_data},
	{ .compatible = "mediatek,mt8167-mmsys",
	  .data = &mt8167_mmsys_match_data},
	{ .compatible = "mediatek,mt8173-mmsys",
	  .data = &mt8173_mmsys_match_data},
	{ .compatible = "mediatek,mt8183-mmsys",
	  .data = &mt8183_mmsys_match_data},
	{ .compatible = "mediatek,mt8186-mmsys",
	  .data = &mt8186_mmsys_match_data},
	{ .compatible = "mediatek,mt8192-mmsys",
	  .data = &mt8192_mmsys_match_data},
	{ .compatible = "mediatek,mt8195-mmsys",
	  .data = &mt8195_mmsys_match_data},
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_drm_of_ids);

static int mtk_drm_find_match_data(struct device *dev,
				   const struct mtk_mmsys_match_data *match_data)
{
	int i;
	struct platform_device *pdev = of_find_device_by_node(dev->parent->of_node);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get parent resource\n");
		return -EINVAL;
	}

	for (i = 0; i < match_data->num_drv_data; i++)
		if (match_data->drv_data[i]->io_start == res->start)
			return i;

	return -EINVAL;
}

static int mtk_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *phandle = dev->parent->of_node;
	const struct of_device_id *of_id;
	const struct mtk_mmsys_match_data *match_data;
	struct mtk_drm_private *private;
	struct device_node *node;
	struct component_match *match = NULL;
	int ret;
	int i;

	private = devm_kzalloc(dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	private->mmsys_dev = dev->parent;
	if (!private->mmsys_dev) {
		dev_err(dev, "Failed to get MMSYS device\n");
		return -ENODEV;
	}

	of_id = of_match_node(mtk_drm_of_ids, phandle);
	if (!of_id)
		return -ENODEV;

	match_data = of_id->data;
	if (match_data->num_drv_data > 1) {
		/* This SoC has multiple mmsys channels */
		ret = mtk_drm_find_match_data(dev, match_data);
		if (ret < 0) {
			dev_err(dev, "Couldn't get match driver data\n");
			return ret;
		}
		private->data = match_data->drv_data[ret];
	} else {
		dev_dbg(dev, "Using single mmsys channel\n");
		private->data = match_data->drv_data[0];
	}

	/* Iterate over sibling DISP function blocks */
	for_each_child_of_node(phandle->parent, node) {
		const struct of_device_id *of_id;
		enum mtk_ddp_comp_type comp_type;
		int comp_id;

		of_id = of_match_node(mtk_ddp_comp_dt_ids, node);
		if (!of_id)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled component %pOF\n",
				node);
			continue;
		}

		comp_type = (enum mtk_ddp_comp_type)of_id->data;

		if (comp_type == MTK_DISP_MUTEX) {
			private->mutex_node = of_node_get(node);
			continue;
		}

		comp_id = mtk_ddp_comp_get_id(node, comp_type);
		if (comp_id < 0) {
			dev_warn(dev, "Skipping unknown component %pOF\n",
				 node);
			continue;
		}

		private->comp_node[comp_id] = of_node_get(node);

		/*
		 * Currently only the AAL, CCORR, COLOR, GAMMA, MERGE, OVL, RDMA, DSI, and DPI
		 * blocks have separate component platform drivers and initialize their own
		 * DDP component structure. The others are initialized here.
		 */
		if (comp_type == MTK_DISP_AAL ||
		    comp_type == MTK_DISP_CCORR ||
		    comp_type == MTK_DISP_COLOR ||
		    comp_type == MTK_DISP_GAMMA ||
		    comp_type == MTK_DISP_MERGE ||
		    comp_type == MTK_DISP_OVL ||
		    comp_type == MTK_DISP_OVL_2L ||
		    comp_type == MTK_DISP_RDMA ||
		    comp_type == MTK_DP_INTF ||
		    comp_type == MTK_DPI ||
		    comp_type == MTK_DSI) {
			dev_info(dev, "Adding component match for %pOF\n",
				 node);
			drm_of_component_match_add(dev, &match, component_compare_of,
						   node);
		}

		ret = mtk_ddp_comp_init(node, &private->ddp_comp[comp_id], comp_id);
		if (ret) {
			of_node_put(node);
			goto err_node;
		}
	}

	if (!private->mutex_node) {
		dev_err(dev, "Failed to find disp-mutex node\n");
		ret = -ENODEV;
		goto err_node;
	}

	pm_runtime_enable(dev);

	platform_set_drvdata(pdev, private);

	ret = component_master_add_with_match(dev, &mtk_drm_ops, match);
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_disable(dev);
err_node:
	of_node_put(private->mutex_node);
	for (i = 0; i < DDP_COMPONENT_ID_MAX; i++)
		of_node_put(private->comp_node[i]);
	return ret;
}

static int mtk_drm_remove(struct platform_device *pdev)
{
	struct mtk_drm_private *private = platform_get_drvdata(pdev);
	int i;

	component_master_del(&pdev->dev, &mtk_drm_ops);
	pm_runtime_disable(&pdev->dev);
	of_node_put(private->mutex_node);
	for (i = 0; i < DDP_COMPONENT_ID_MAX; i++)
		of_node_put(private->comp_node[i]);

	return 0;
}

static int mtk_drm_sys_prepare(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;

	return drm_mode_config_helper_suspend(drm);
}

static void mtk_drm_sys_complete(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;
	int ret;

	ret = drm_mode_config_helper_resume(drm);
	if (ret)
		dev_err(dev, "Failed to resume\n");
}

static const struct dev_pm_ops mtk_drm_pm_ops = {
	.prepare = mtk_drm_sys_prepare,
	.complete = mtk_drm_sys_complete,
};

static struct platform_driver mtk_drm_platform_driver = {
	.probe	= mtk_drm_probe,
	.remove	= mtk_drm_remove,
	.driver	= {
		.name	= "mediatek-drm",
		.pm     = &mtk_drm_pm_ops,
	},
};

static struct platform_driver * const mtk_drm_drivers[] = {
	&mtk_disp_aal_driver,
	&mtk_disp_ccorr_driver,
	&mtk_disp_color_driver,
	&mtk_disp_gamma_driver,
	&mtk_disp_merge_driver,
	&mtk_disp_ovl_driver,
	&mtk_disp_rdma_driver,
	&mtk_dpi_driver,
	&mtk_drm_platform_driver,
	&mtk_dsi_driver,
	&mtk_mdp_rdma_driver,
};

static int __init mtk_drm_init(void)
{
	return platform_register_drivers(mtk_drm_drivers,
					 ARRAY_SIZE(mtk_drm_drivers));
}

static void __exit mtk_drm_exit(void)
{
	platform_unregister_drivers(mtk_drm_drivers,
				    ARRAY_SIZE(mtk_drm_drivers));
}

module_init(mtk_drm_init);
module_exit(mtk_drm_exit);

MODULE_AUTHOR("YT SHEN <yt.shen@mediatek.com>");
MODULE_DESCRIPTION("Mediatek SoC DRM driver");
MODULE_LICENSE("GPL v2");
