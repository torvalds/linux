// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Image Sensor Controller (ISC) driver
 *
 * Copyright (C) 2016-2019 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 *
 * Sensor-->PFE-->WB-->CFA-->CC-->GAM-->CSC-->CBC-->SUB-->RLP-->DMA
 *
 * ISC video pipeline integrates the following submodules:
 * PFE: Parallel Front End to sample the camera sensor input stream
 *  WB: Programmable white balance in the Bayer domain
 * CFA: Color filter array interpolation module
 *  CC: Programmable color correction
 * GAM: Gamma correction
 * CSC: Programmable color space conversion
 * CBC: Contrast and Brightness control
 * SUB: This module performs YCbCr444 to YCbCr420 chrominance subsampling
 * RLP: This module performs rounding, range limiting
 *      and packing of the incoming data
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

#define ISC_SAMA5D2_MAX_SUPPORT_WIDTH   2592
#define ISC_SAMA5D2_MAX_SUPPORT_HEIGHT  1944

#define ISC_SAMA5D2_PIPELINE \
	(WB_ENABLE | CFA_ENABLE | CC_ENABLE | GAM_ENABLES | CSC_ENABLE | \
	CBC_ENABLE | SUB422_ENABLE | SUB420_ENABLE)

/* This is a list of the formats that the ISC can *output* */
static const struct isc_format sama5d2_controller_formats[] = {
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
};

/* This is a list of formats that the ISC can receive as *input* */
static struct isc_format sama5d2_formats_list[] = {
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

static void isc_sama5d2_config_csc(struct isc_device *isc)
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

static void isc_sama5d2_config_cbc(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;

	regmap_write(regmap, ISC_CBC_BRIGHT + isc->offsets.cbc,
		     isc->ctrls.brightness);
	regmap_write(regmap, ISC_CBC_CONTRAST + isc->offsets.cbc,
		     isc->ctrls.contrast);
}

static void isc_sama5d2_config_cc(struct isc_device *isc)
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

static void isc_sama5d2_config_ctrls(struct isc_device *isc,
				     const struct v4l2_ctrl_ops *ops)
{
	struct isc_ctrls *ctrls = &isc->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;

	ctrls->contrast = 256;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -2048, 2047, 1, 256);
}

static void isc_sama5d2_config_dpc(struct isc_device *isc)
{
	/* This module is not present on sama5d2 pipeline */
}

static void isc_sama5d2_config_gam(struct isc_device *isc)
{
	/* No specific gamma configuration */
}

static void isc_sama5d2_config_rlp(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 rlp_mode = isc->config.rlp_cfg_mode;

	/*
	 * In sama5d2, the YUV planar modes and the YUYV modes are treated
	 * in the same way in RLP register.
	 * Normally, YYCC mode should be Luma(n) - Color B(n) - Color R (n)
	 * and YCYC should be Luma(n + 1) - Color B (n) - Luma (n) - Color R (n)
	 * but in sama5d2, the YCYC mode does not exist, and YYCC must be
	 * selected for both planar and interleaved modes, as in fact
	 * both modes are supported.
	 *
	 * Thus, if the YCYC mode is selected, replace it with the
	 * sama5d2-compliant mode which is YYCC .
	 */
	if ((rlp_mode & ISC_RLP_CFG_MODE_YCYC) == ISC_RLP_CFG_MODE_YCYC) {
		rlp_mode &= ~ISC_RLP_CFG_MODE_MASK;
		rlp_mode |= ISC_RLP_CFG_MODE_YYCC;
	}

	regmap_update_bits(regmap, ISC_RLP_CFG + isc->offsets.rlp,
			   ISC_RLP_CFG_MODE_MASK, rlp_mode);
}

static void isc_sama5d2_adapt_pipeline(struct isc_device *isc)
{
	isc->try_config.bits_pipeline &= ISC_SAMA5D2_PIPELINE;
}

/* Gamma table with gamma 1/2.2 */
static const u32 isc_sama5d2_gamma_table[][GAMMA_ENTRIES] = {
	/* 0 --> gamma 1/1.8 */
	{      0x65,  0x66002F,  0x950025,  0xBB0020,  0xDB001D,  0xF8001A,
	  0x1130018, 0x12B0017, 0x1420016, 0x1580014, 0x16D0013, 0x1810012,
	  0x1940012, 0x1A60012, 0x1B80011, 0x1C90010, 0x1DA0010, 0x1EA000F,
	  0x1FA000F, 0x209000F, 0x218000F, 0x227000E, 0x235000E, 0x243000E,
	  0x251000E, 0x25F000D, 0x26C000D, 0x279000D, 0x286000D, 0x293000C,
	  0x2A0000C, 0x2AC000C, 0x2B8000C, 0x2C4000C, 0x2D0000B, 0x2DC000B,
	  0x2E7000B, 0x2F3000B, 0x2FE000B, 0x309000B, 0x314000B, 0x31F000A,
	  0x32A000A, 0x334000B, 0x33F000A, 0x349000A, 0x354000A, 0x35E000A,
	  0x368000A, 0x372000A, 0x37C000A, 0x386000A, 0x3900009, 0x399000A,
	  0x3A30009, 0x3AD0009, 0x3B60009, 0x3BF000A, 0x3C90009, 0x3D20009,
	  0x3DB0009, 0x3E40009, 0x3ED0009, 0x3F60009 },

	/* 1 --> gamma 1/2 */
	{      0x7F,  0x800034,  0xB50028,  0xDE0021, 0x100001E, 0x11E001B,
	  0x1390019, 0x1520017, 0x16A0015, 0x1800014, 0x1940014, 0x1A80013,
	  0x1BB0012, 0x1CD0011, 0x1DF0010, 0x1EF0010, 0x200000F, 0x20F000F,
	  0x21F000E, 0x22D000F, 0x23C000E, 0x24A000E, 0x258000D, 0x265000D,
	  0x273000C, 0x27F000D, 0x28C000C, 0x299000C, 0x2A5000C, 0x2B1000B,
	  0x2BC000C, 0x2C8000B, 0x2D3000C, 0x2DF000B, 0x2EA000A, 0x2F5000A,
	  0x2FF000B, 0x30A000A, 0x314000B, 0x31F000A, 0x329000A, 0x333000A,
	  0x33D0009, 0x3470009, 0x350000A, 0x35A0009, 0x363000A, 0x36D0009,
	  0x3760009, 0x37F0009, 0x3880009, 0x3910009, 0x39A0009, 0x3A30009,
	  0x3AC0008, 0x3B40009, 0x3BD0008, 0x3C60008, 0x3CE0008, 0x3D60009,
	  0x3DF0008, 0x3E70008, 0x3EF0008, 0x3F70008 },

	/* 2 --> gamma 1/2.2 */
	{      0x99,  0x9B0038,  0xD4002A,  0xFF0023, 0x122001F, 0x141001B,
	  0x15D0019, 0x1760017, 0x18E0015, 0x1A30015, 0x1B80013, 0x1CC0012,
	  0x1DE0011, 0x1F00010, 0x2010010, 0x2110010, 0x221000F, 0x230000F,
	  0x23F000E, 0x24D000E, 0x25B000D, 0x269000C, 0x276000C, 0x283000C,
	  0x28F000C, 0x29B000C, 0x2A7000C, 0x2B3000B, 0x2BF000B, 0x2CA000B,
	  0x2D5000B, 0x2E0000A, 0x2EB000A, 0x2F5000A, 0x2FF000A, 0x30A000A,
	  0x3140009, 0x31E0009, 0x327000A, 0x3310009, 0x33A0009, 0x3440009,
	  0x34D0009, 0x3560009, 0x35F0009, 0x3680008, 0x3710008, 0x3790009,
	  0x3820008, 0x38A0008, 0x3930008, 0x39B0008, 0x3A30008, 0x3AB0008,
	  0x3B30008, 0x3BB0008, 0x3C30008, 0x3CB0007, 0x3D20008, 0x3DA0007,
	  0x3E20007, 0x3E90007, 0x3F00008, 0x3F80007 },
};

static int isc_parse_dt(struct device *dev, struct isc_device *isc)
{
	struct device_node *np = dev->of_node;
	struct device_node *epn = NULL;
	struct isc_subdev_entity *subdev_entity;
	unsigned int flags;
	int ret;

	INIT_LIST_HEAD(&isc->subdev_entities);

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

		list_add_tail(&subdev_entity->list, &isc->subdev_entities);
	}
	of_node_put(epn);

	return ret;
}

static int atmel_isc_probe(struct platform_device *pdev)
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
			       "atmel-sama5d2-isc", isc);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			irq, ret);
		return ret;
	}

	isc->gamma_table = isc_sama5d2_gamma_table;
	isc->gamma_max = 2;

	isc->max_width = ISC_SAMA5D2_MAX_SUPPORT_WIDTH;
	isc->max_height = ISC_SAMA5D2_MAX_SUPPORT_HEIGHT;

	isc->config_dpc = isc_sama5d2_config_dpc;
	isc->config_csc = isc_sama5d2_config_csc;
	isc->config_cbc = isc_sama5d2_config_cbc;
	isc->config_cc = isc_sama5d2_config_cc;
	isc->config_gam = isc_sama5d2_config_gam;
	isc->config_rlp = isc_sama5d2_config_rlp;
	isc->config_ctrls = isc_sama5d2_config_ctrls;

	isc->adapt_pipeline = isc_sama5d2_adapt_pipeline;

	isc->offsets.csc = ISC_SAMA5D2_CSC_OFFSET;
	isc->offsets.cbc = ISC_SAMA5D2_CBC_OFFSET;
	isc->offsets.sub422 = ISC_SAMA5D2_SUB422_OFFSET;
	isc->offsets.sub420 = ISC_SAMA5D2_SUB420_OFFSET;
	isc->offsets.rlp = ISC_SAMA5D2_RLP_OFFSET;
	isc->offsets.his = ISC_SAMA5D2_HIS_OFFSET;
	isc->offsets.dma = ISC_SAMA5D2_DMA_OFFSET;
	isc->offsets.version = ISC_SAMA5D2_VERSION_OFFSET;
	isc->offsets.his_entry = ISC_SAMA5D2_HIS_ENTRY_OFFSET;

	isc->controller_formats = sama5d2_controller_formats;
	isc->controller_formats_size = ARRAY_SIZE(sama5d2_controller_formats);
	isc->formats_list = sama5d2_formats_list;
	isc->formats_list_size = ARRAY_SIZE(sama5d2_formats_list);

	/* sama5d2-isc - 8 bits per beat */
	isc->dcfg = ISC_DCFG_YMBSIZE_BEATS8 | ISC_DCFG_CMBSIZE_BEATS8;

	/* sama5d2-isc : ISPCK is required and mandatory */
	isc->ispck_required = true;

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
	ret = v4l2_device_register(dev, &isc->v4l2_dev);
	if (ret) {
		dev_err(dev, "unable to register v4l2 device.\n");
		goto unprepare_clk;
	}

	ret = isc_parse_dt(dev, isc);
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
		struct fwnode_handle *fwnode =
			of_fwnode_handle(subdev_entity->epn);

		v4l2_async_nf_init(&subdev_entity->notifier);

		asd = v4l2_async_nf_add_fwnode_remote(&subdev_entity->notifier,
						      fwnode,
						      struct v4l2_async_subdev);

		of_node_put(subdev_entity->epn);
		subdev_entity->epn = NULL;

		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			goto cleanup_subdev;
		}

		subdev_entity->notifier.ops = &isc_async_ops;

		ret = v4l2_async_nf_register(&isc->v4l2_dev,
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

	isc->ispck = isc->isc_clks[ISC_ISPCK].clk;

	ret = clk_prepare_enable(isc->ispck);
	if (ret) {
		dev_err(dev, "failed to enable ispck: %d\n", ret);
		goto cleanup_subdev;
	}

	/* ispck should be greater or equal to hclock */
	ret = clk_set_rate(isc->ispck, clk_get_rate(isc->hclock));
	if (ret) {
		dev_err(dev, "failed to set ispck rate: %d\n", ret);
		goto unprepare_clk;
	}

	regmap_read(isc->regmap, ISC_VERSION + isc->offsets.version, &ver);
	dev_info(dev, "Microchip ISC version %x\n", ver);

	return 0;

unprepare_clk:
	clk_disable_unprepare(isc->ispck);

cleanup_subdev:
	isc_subdev_cleanup(isc);

unregister_v4l2_device:
	v4l2_device_unregister(&isc->v4l2_dev);

unprepare_hclk:
	clk_disable_unprepare(isc->hclock);

	isc_clk_cleanup(isc);

	return ret;
}

static int atmel_isc_remove(struct platform_device *pdev)
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

static int __maybe_unused isc_runtime_suspend(struct device *dev)
{
	struct isc_device *isc = dev_get_drvdata(dev);

	clk_disable_unprepare(isc->ispck);
	clk_disable_unprepare(isc->hclock);

	return 0;
}

static int __maybe_unused isc_runtime_resume(struct device *dev)
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

static const struct dev_pm_ops atmel_isc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(isc_runtime_suspend, isc_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id atmel_isc_of_match[] = {
	{ .compatible = "atmel,sama5d2-isc" },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_isc_of_match);
#endif

static struct platform_driver atmel_isc_driver = {
	.probe	= atmel_isc_probe,
	.remove	= atmel_isc_remove,
	.driver	= {
		.name		= "atmel-sama5d2-isc",
		.pm		= &atmel_isc_dev_pm_ops,
		.of_match_table = of_match_ptr(atmel_isc_of_match),
	},
};

module_platform_driver(atmel_isc_driver);

MODULE_AUTHOR("Songjun Wu");
MODULE_DESCRIPTION("The V4L2 driver for Atmel-ISC");
MODULE_LICENSE("GPL v2");
