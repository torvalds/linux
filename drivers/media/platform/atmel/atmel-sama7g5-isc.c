// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip eXtended Image Sensor Controller (XISC) driver
 *
 * Copyright (C) 2019-2021 Microchip Technology, Inc. and its subsidiaries
 *
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 * Sensor-->PFE-->DPC-->WB-->CFA-->CC-->GAM-->VHXS-->CSC-->CBHS-->SUB-->RLP-->DMA-->HIS
 *
 * ISC video pipeline integrates the following submodules:
 * PFE: Parallel Front End to sample the camera sensor input stream
 * DPC: Defective Pixel Correction with black offset correction, green disparity
 *      correction and defective pixel correction (3 modules total)
 *  WB: Programmable white balance in the Bayer domain
 * CFA: Color filter array interpolation module
 *  CC: Programmable color correction
 * GAM: Gamma correction
 *VHXS: Vertical and Horizontal Scaler
 * CSC: Programmable color space conversion
 *CBHS: Contrast Brightness Hue and Saturation control
 * SUB: This module performs YCbCr444 to YCbCr420 chrominance subsampling
 * RLP: This module performs rounding, range limiting
 *      and packing of the incoming data
 * DMA: This module performs DMA master accesses to write frames to external RAM
 * HIS: Histogram module performs statistic counters on the frames
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "atmel-isc-regs.h"
#include "atmel-isc.h"

#define ISC_SAMA7G5_MAX_SUPPORT_WIDTH   3264
#define ISC_SAMA7G5_MAX_SUPPORT_HEIGHT  2464

#define ISC_SAMA7G5_PIPELINE \
	(WB_ENABLE | CFA_ENABLE | CC_ENABLE | GAM_ENABLES | CSC_ENABLE | \
	CBC_ENABLE | SUB422_ENABLE | SUB420_ENABLE)

/* This is a list of the formats that the ISC can *output* */
static const struct isc_format sama7g5_controller_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_ARGB444,
	},
	{
		.fourcc		= V4L2_PIX_FMT_ARGB555,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
	},
	{
		.fourcc		= V4L2_PIX_FMT_ABGR32,
	},
	{
		.fourcc		= V4L2_PIX_FMT_XBGR32,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420,
	},
	{
		.fourcc		= V4L2_PIX_FMT_UYVY,
	},
	{
		.fourcc		= V4L2_PIX_FMT_VYUY,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV422P,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y16,
	},
};

/* This is a list of formats that the ISC can receive as *input* */
static struct isc_format sama7g5_formats_list[] = {
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
		.cfa_baycfg	= ISC_BAY_CFG_BGBG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
		.cfa_baycfg	= ISC_BAY_CFG_GBGB,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
		.cfa_baycfg	= ISC_BAY_CFG_GRGR,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
		.cfa_baycfg	= ISC_BAY_CFG_RGRG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TEN,
		.cfa_baycfg	= ISC_BAY_CFG_RGRG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TEN,
		.cfa_baycfg	= ISC_BAY_CFG_GBGB,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TEN,
		.cfa_baycfg	= ISC_BAY_CFG_GRGR,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TEN,
		.cfa_baycfg	= ISC_BAY_CFG_RGRG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TWELVE,
		.cfa_baycfg	= ISC_BAY_CFG_BGBG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TWELVE,
		.cfa_baycfg	= ISC_BAY_CFG_GBGB,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TWELVE,
		.cfa_baycfg	= ISC_BAY_CFG_GRGR,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TWELVE,
		.cfa_baycfg	= ISC_BAY_CFG_RGRG,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.mbus_code	= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.pfe_cfg0_bps	= ISC_PFE_CFG0_BPS_EIGHT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10,
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.pfe_cfg0_bps	= ISC_PFG_CFG0_BPS_TEN,
	},

};

static void isc_sama7g5_config_csc(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;

	/* Convert RGB to YUV */
	regmap_write(regmap, ISC_CSC_YR_YG + isc->offsets.csc,
		     0x42 | (0x81 << 16));
	regmap_write(regmap, ISC_CSC_YB_OY + isc->offsets.csc,
		     0x19 | (0x10 << 16));
	regmap_write(regmap, ISC_CSC_CBR_CBG + isc->offsets.csc,
		     0xFDA | (0xFB6 << 16));
	regmap_write(regmap, ISC_CSC_CBB_OCB + isc->offsets.csc,
		     0x70 | (0x80 << 16));
	regmap_write(regmap, ISC_CSC_CRR_CRG + isc->offsets.csc,
		     0x70 | (0xFA2 << 16));
	regmap_write(regmap, ISC_CSC_CRB_OCR + isc->offsets.csc,
		     0xFEE | (0x80 << 16));
}

static void isc_sama7g5_config_cbc(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;

	/* Configure what is set via v4l2 ctrls */
	regmap_write(regmap, ISC_CBC_BRIGHT + isc->offsets.cbc, isc->ctrls.brightness);
	regmap_write(regmap, ISC_CBC_CONTRAST + isc->offsets.cbc, isc->ctrls.contrast);
	/* Configure Hue and Saturation as neutral midpoint */
	regmap_write(regmap, ISC_CBCHS_HUE, 0);
	regmap_write(regmap, ISC_CBCHS_SAT, (1 << 4));
}

static void isc_sama7g5_config_cc(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;

	/* Configure each register at the neutral fixed point 1.0 or 0.0 */
	regmap_write(regmap, ISC_CC_RR_RG, (1 << 8));
	regmap_write(regmap, ISC_CC_RB_OR, 0);
	regmap_write(regmap, ISC_CC_GR_GG, (1 << 8) << 16);
	regmap_write(regmap, ISC_CC_GB_OG, 0);
	regmap_write(regmap, ISC_CC_BR_BG, 0);
	regmap_write(regmap, ISC_CC_BB_OB, (1 << 8));
}

static void isc_sama7g5_config_ctrls(struct isc_device *isc,
				     const struct v4l2_ctrl_ops *ops)
{
	struct isc_ctrls *ctrls = &isc->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;

	ctrls->contrast = 16;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -2048, 2047, 1, 16);
}

static void isc_sama7g5_config_dpc(struct isc_device *isc)
{
	u32 bay_cfg = isc->config.sd_format->cfa_baycfg;
	struct regmap *regmap = isc->regmap;

	regmap_update_bits(regmap, ISC_DPC_CFG, ISC_DPC_CFG_BLOFF_MASK,
			   (64 << ISC_DPC_CFG_BLOFF_SHIFT));
	regmap_update_bits(regmap, ISC_DPC_CFG, ISC_DPC_CFG_BAYCFG_MASK,
			   (bay_cfg << ISC_DPC_CFG_BAYCFG_SHIFT));
}

static void isc_sama7g5_config_gam(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;

	regmap_update_bits(regmap, ISC_GAM_CTRL, ISC_GAM_CTRL_BIPART,
			   ISC_GAM_CTRL_BIPART);
}

static void isc_sama7g5_config_rlp(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 rlp_mode = isc->config.rlp_cfg_mode;

	regmap_update_bits(regmap, ISC_RLP_CFG + isc->offsets.rlp,
			   ISC_RLP_CFG_MODE_MASK | ISC_RLP_CFG_LSH |
			   ISC_RLP_CFG_YMODE_MASK, rlp_mode);
}

static void isc_sama7g5_adapt_pipeline(struct isc_device *isc)
{
	isc->try_config.bits_pipeline &= ISC_SAMA7G5_PIPELINE;
}

/* Gamma table with gamma 1/2.2 */
static const u32 isc_sama7g5_gamma_table[][GAMMA_ENTRIES] = {
	/* index 0 --> gamma bipartite */
	{
	      0x980,  0x4c0320,  0x650260,  0x7801e0,  0x8701a0,  0x940180,
	   0xa00160,  0xab0120,  0xb40120,  0xbd0120,  0xc60100,  0xce0100,
	   0xd600e0,  0xdd00e0,  0xe400e0,  0xeb00c0,  0xf100c0,  0xf700c0,
	   0xfd00c0, 0x10300a0, 0x10800c0, 0x10e00a0, 0x11300a0, 0x11800a0,
	  0x11d00a0, 0x12200a0, 0x12700a0, 0x12c0080, 0x13000a0, 0x1350080,
	  0x13900a0, 0x13e0080, 0x1420076, 0x17d0062, 0x1ae0054, 0x1d8004a,
	  0x1fd0044, 0x21f003e, 0x23e003a, 0x25b0036, 0x2760032, 0x28f0030,
	  0x2a7002e, 0x2be002c, 0x2d4002c, 0x2ea0028, 0x2fe0028, 0x3120026,
	  0x3250024, 0x3370024, 0x3490022, 0x35a0022, 0x36b0020, 0x37b0020,
	  0x38b0020, 0x39b001e, 0x3aa001e, 0x3b9001c, 0x3c7001c, 0x3d5001c,
	  0x3e3001c, 0x3f1001c, 0x3ff001a, 0x40c001a },
};

static int xisc_parse_dt(struct device *dev, struct isc_device *isc)
{
	struct device_node *np = dev->of_node;
	struct device_node *epn = NULL;
	struct isc_subdev_entity *subdev_entity;
	unsigned int flags;
	int ret;
	bool mipi_mode;

	INIT_LIST_HEAD(&isc->subdev_entities);

	mipi_mode = of_property_read_bool(np, "microchip,mipi-mode");

	while (1) {
		struct v4l2_fwnode_endpoint v4l2_epn = { .bus_type = 0 };

		epn = of_graph_get_next_endpoint(np, epn);
		if (!epn)
			return 0;

		ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(epn),
						 &v4l2_epn);
		if (ret) {
			ret = -EINVAL;
			dev_err(dev, "Could not parse the endpoint\n");
			break;
		}

		subdev_entity = devm_kzalloc(dev, sizeof(*subdev_entity),
					     GFP_KERNEL);
		if (!subdev_entity) {
			ret = -ENOMEM;
			break;
		}
		subdev_entity->epn = epn;

		flags = v4l2_epn.bus.parallel.flags;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 = ISC_PFE_CFG0_HPOL_LOW;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_VPOL_LOW;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_PPOL_LOW;

		if (v4l2_epn.bus_type == V4L2_MBUS_BT656)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_CCIR_CRC |
					ISC_PFE_CFG0_CCIR656;

		if (mipi_mode)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_MIPI;

		list_add_tail(&subdev_entity->list, &isc->subdev_entities);
	}
	of_node_put(epn);

	return ret;
}

static int microchip_xisc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct isc_device *isc;
	struct resource *res;
	void __iomem *io_base;
	struct isc_subdev_entity *subdev_entity;
	int irq;
	int ret;
	u32 ver;

	isc = devm_kzalloc(dev, sizeof(*isc), GFP_KERNEL);
	if (!isc)
		return -ENOMEM;

	platform_set_drvdata(pdev, isc);
	isc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	isc->regmap = devm_regmap_init_mmio(dev, io_base, &isc_regmap_config);
	if (IS_ERR(isc->regmap)) {
		ret = PTR_ERR(isc->regmap);
		dev_err(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, isc_interrupt, 0,
			       "microchip-sama7g5-xisc", isc);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			irq, ret);
		return ret;
	}

	isc->gamma_table = isc_sama7g5_gamma_table;
	isc->gamma_max = 0;

	isc->max_width = ISC_SAMA7G5_MAX_SUPPORT_WIDTH;
	isc->max_height = ISC_SAMA7G5_MAX_SUPPORT_HEIGHT;

	isc->config_dpc = isc_sama7g5_config_dpc;
	isc->config_csc = isc_sama7g5_config_csc;
	isc->config_cbc = isc_sama7g5_config_cbc;
	isc->config_cc = isc_sama7g5_config_cc;
	isc->config_gam = isc_sama7g5_config_gam;
	isc->config_rlp = isc_sama7g5_config_rlp;
	isc->config_ctrls = isc_sama7g5_config_ctrls;

	isc->adapt_pipeline = isc_sama7g5_adapt_pipeline;

	isc->offsets.csc = ISC_SAMA7G5_CSC_OFFSET;
	isc->offsets.cbc = ISC_SAMA7G5_CBC_OFFSET;
	isc->offsets.sub422 = ISC_SAMA7G5_SUB422_OFFSET;
	isc->offsets.sub420 = ISC_SAMA7G5_SUB420_OFFSET;
	isc->offsets.rlp = ISC_SAMA7G5_RLP_OFFSET;
	isc->offsets.his = ISC_SAMA7G5_HIS_OFFSET;
	isc->offsets.dma = ISC_SAMA7G5_DMA_OFFSET;
	isc->offsets.version = ISC_SAMA7G5_VERSION_OFFSET;
	isc->offsets.his_entry = ISC_SAMA7G5_HIS_ENTRY_OFFSET;

	isc->controller_formats = sama7g5_controller_formats;
	isc->controller_formats_size = ARRAY_SIZE(sama7g5_controller_formats);
	isc->formats_list = sama7g5_formats_list;
	isc->formats_list_size = ARRAY_SIZE(sama7g5_formats_list);

	/* sama7g5-isc RAM access port is full AXI4 - 32 bits per beat */
	isc->dcfg = ISC_DCFG_YMBSIZE_BEATS32 | ISC_DCFG_CMBSIZE_BEATS32;

	ret = isc_pipeline_init(isc);
	if (ret)
		return ret;

	isc->hclock = devm_clk_get(dev, "hclock");
	if (IS_ERR(isc->hclock)) {
		ret = PTR_ERR(isc->hclock);
		dev_err(dev, "failed to get hclock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(isc->hclock);
	if (ret) {
		dev_err(dev, "failed to enable hclock: %d\n", ret);
		return ret;
	}

	ret = isc_clk_init(isc);
	if (ret) {
		dev_err(dev, "failed to init isc clock: %d\n", ret);
		goto unprepare_hclk;
	}

	isc->ispck = isc->isc_clks[ISC_ISPCK].clk;

	ret = clk_prepare_enable(isc->ispck);
	if (ret) {
		dev_err(dev, "failed to enable ispck: %d\n", ret);
		goto unprepare_hclk;
	}

	/* ispck should be greater or equal to hclock */
	ret = clk_set_rate(isc->ispck, clk_get_rate(isc->hclock));
	if (ret) {
		dev_err(dev, "failed to set ispck rate: %d\n", ret);
		goto unprepare_clk;
	}

	ret = v4l2_device_register(dev, &isc->v4l2_dev);
	if (ret) {
		dev_err(dev, "unable to register v4l2 device.\n");
		goto unprepare_clk;
	}

	ret = xisc_parse_dt(dev, isc);
	if (ret) {
		dev_err(dev, "fail to parse device tree\n");
		goto unregister_v4l2_device;
	}

	if (list_empty(&isc->subdev_entities)) {
		dev_err(dev, "no subdev found\n");
		ret = -ENODEV;
		goto unregister_v4l2_device;
	}

	list_for_each_entry(subdev_entity, &isc->subdev_entities, list) {
		struct v4l2_async_subdev *asd;

		v4l2_async_notifier_init(&subdev_entity->notifier);

		asd = v4l2_async_notifier_add_fwnode_remote_subdev(
					&subdev_entity->notifier,
					of_fwnode_handle(subdev_entity->epn),
					struct v4l2_async_subdev);

		of_node_put(subdev_entity->epn);
		subdev_entity->epn = NULL;

		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			goto cleanup_subdev;
		}

		subdev_entity->notifier.ops = &isc_async_ops;

		ret = v4l2_async_notifier_register(&isc->v4l2_dev,
						   &subdev_entity->notifier);
		if (ret) {
			dev_err(dev, "fail to register async notifier\n");
			goto cleanup_subdev;
		}

		if (video_is_registered(&isc->video_dev))
			break;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_request_idle(dev);

	regmap_read(isc->regmap, ISC_VERSION + isc->offsets.version, &ver);
	dev_info(dev, "Microchip XISC version %x\n", ver);

	return 0;

cleanup_subdev:
	isc_subdev_cleanup(isc);

unregister_v4l2_device:
	v4l2_device_unregister(&isc->v4l2_dev);

unprepare_clk:
	clk_disable_unprepare(isc->ispck);
unprepare_hclk:
	clk_disable_unprepare(isc->hclock);

	isc_clk_cleanup(isc);

	return ret;
}

static int microchip_xisc_remove(struct platform_device *pdev)
{
	struct isc_device *isc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	isc_subdev_cleanup(isc);

	v4l2_device_unregister(&isc->v4l2_dev);

	clk_disable_unprepare(isc->ispck);
	clk_disable_unprepare(isc->hclock);

	isc_clk_cleanup(isc);

	return 0;
}

static int __maybe_unused xisc_runtime_suspend(struct device *dev)
{
	struct isc_device *isc = dev_get_drvdata(dev);

	clk_disable_unprepare(isc->ispck);
	clk_disable_unprepare(isc->hclock);

	return 0;
}

static int __maybe_unused xisc_runtime_resume(struct device *dev)
{
	struct isc_device *isc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(isc->hclock);
	if (ret)
		return ret;

	ret = clk_prepare_enable(isc->ispck);
	if (ret)
		clk_disable_unprepare(isc->hclock);

	return ret;
}

static const struct dev_pm_ops microchip_xisc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(xisc_runtime_suspend, xisc_runtime_resume, NULL)
};

static const struct of_device_id microchip_xisc_of_match[] = {
	{ .compatible = "microchip,sama7g5-isc" },
	{ }
};
MODULE_DEVICE_TABLE(of, microchip_xisc_of_match);

static struct platform_driver microchip_xisc_driver = {
	.probe	= microchip_xisc_probe,
	.remove	= microchip_xisc_remove,
	.driver	= {
		.name		= "microchip-sama7g5-xisc",
		.pm		= &microchip_xisc_dev_pm_ops,
		.of_match_table = of_match_ptr(microchip_xisc_of_match),
	},
};

module_platform_driver(microchip_xisc_driver);

MODULE_AUTHOR("Eugen Hristev <eugen.hristev@microchip.com>");
MODULE_DESCRIPTION("The V4L2 driver for Microchip-XISC");
MODULE_LICENSE("GPL v2");
