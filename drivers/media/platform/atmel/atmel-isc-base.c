// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip Image Sensor Controller (ISC) common driver base
 *
 * Copyright (C) 2016-2019 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
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
#include <linux/atmel-isc-media.h>

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

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

static unsigned int sensor_preferred = 1;
module_param(sensor_preferred, uint, 0644);
MODULE_PARM_DESC(sensor_preferred,
		 "Sensor is preferred to output the specified format (1-on 0-off), default 1");

/* This is a list of the formats that the ISC can *output* */
const struct isc_format controller_formats[] = {
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
struct isc_format formats_list[] = {
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

/* Gamma table with gamma 1/2.2 */
const u32 isc_gamma_table[GAMMA_MAX + 1][GAMMA_ENTRIES] = {
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

#define ISC_IS_FORMAT_RAW(mbus_code) \
	(((mbus_code) & 0xf000) == 0x3000)

#define ISC_IS_FORMAT_GREY(mbus_code) \
	(((mbus_code) == MEDIA_BUS_FMT_Y10_1X10) | \
	(((mbus_code) == MEDIA_BUS_FMT_Y8_1X8)))

static inline void isc_update_v4l2_ctrls(struct isc_device *isc)
{
	struct isc_ctrls *ctrls = &isc->ctrls;

	/* In here we set the v4l2 controls w.r.t. our pipeline config */
	v4l2_ctrl_s_ctrl(isc->r_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_R]);
	v4l2_ctrl_s_ctrl(isc->b_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_B]);
	v4l2_ctrl_s_ctrl(isc->gr_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_GR]);
	v4l2_ctrl_s_ctrl(isc->gb_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_GB]);

	v4l2_ctrl_s_ctrl(isc->r_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_R]);
	v4l2_ctrl_s_ctrl(isc->b_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_B]);
	v4l2_ctrl_s_ctrl(isc->gr_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_GR]);
	v4l2_ctrl_s_ctrl(isc->gb_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_GB]);
}

static inline void isc_update_awb_ctrls(struct isc_device *isc)
{
	struct isc_ctrls *ctrls = &isc->ctrls;

	/* In here we set our actual hw pipeline config */

	regmap_write(isc->regmap, ISC_WB_O_RGR,
		     ((ctrls->offset[ISC_HIS_CFG_MODE_R])) |
		     ((ctrls->offset[ISC_HIS_CFG_MODE_GR]) << 16));
	regmap_write(isc->regmap, ISC_WB_O_BGB,
		     ((ctrls->offset[ISC_HIS_CFG_MODE_B])) |
		     ((ctrls->offset[ISC_HIS_CFG_MODE_GB]) << 16));
	regmap_write(isc->regmap, ISC_WB_G_RGR,
		     ctrls->gain[ISC_HIS_CFG_MODE_R] |
		     (ctrls->gain[ISC_HIS_CFG_MODE_GR] << 16));
	regmap_write(isc->regmap, ISC_WB_G_BGB,
		     ctrls->gain[ISC_HIS_CFG_MODE_B] |
		     (ctrls->gain[ISC_HIS_CFG_MODE_GB] << 16));
}

static inline void isc_reset_awb_ctrls(struct isc_device *isc)
{
	unsigned int c;

	for (c = ISC_HIS_CFG_MODE_GR; c <= ISC_HIS_CFG_MODE_B; c++) {
		/* gains have a fixed point at 9 decimals */
		isc->ctrls.gain[c] = 1 << 9;
		/* offsets are in 2's complements */
		isc->ctrls.offset[c] = 0;
	}
}

static int isc_wait_clk_stable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	struct regmap *regmap = isc_clk->regmap;
	unsigned long timeout = jiffies + usecs_to_jiffies(1000);
	unsigned int status;

	while (time_before(jiffies, timeout)) {
		regmap_read(regmap, ISC_CLKSR, &status);
		if (!(status & ISC_CLKSR_SIP))
			return 0;

		usleep_range(10, 250);
	}

	return -ETIMEDOUT;
}

static int isc_clk_prepare(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	int ret;

	if (isc_clk->id == ISC_ISPCK) {
		ret = pm_runtime_resume_and_get(isc_clk->dev);
		if (ret < 0)
			return ret;
	}

	return isc_wait_clk_stable(hw);
}

static void isc_clk_unprepare(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	isc_wait_clk_stable(hw);

	if (isc_clk->id == ISC_ISPCK)
		pm_runtime_put_sync(isc_clk->dev);
}

static int isc_clk_enable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;
	struct regmap *regmap = isc_clk->regmap;
	unsigned long flags;
	unsigned int status;

	dev_dbg(isc_clk->dev, "ISC CLK: %s, div = %d, parent id = %d\n",
		__func__, isc_clk->div, isc_clk->parent_id);

	spin_lock_irqsave(&isc_clk->lock, flags);
	regmap_update_bits(regmap, ISC_CLKCFG,
			   ISC_CLKCFG_DIV_MASK(id) | ISC_CLKCFG_SEL_MASK(id),
			   (isc_clk->div << ISC_CLKCFG_DIV_SHIFT(id)) |
			   (isc_clk->parent_id << ISC_CLKCFG_SEL_SHIFT(id)));

	regmap_write(regmap, ISC_CLKEN, ISC_CLK(id));
	spin_unlock_irqrestore(&isc_clk->lock, flags);

	regmap_read(regmap, ISC_CLKSR, &status);
	if (status & ISC_CLK(id))
		return 0;
	else
		return -EINVAL;
}

static void isc_clk_disable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;
	unsigned long flags;

	spin_lock_irqsave(&isc_clk->lock, flags);
	regmap_write(isc_clk->regmap, ISC_CLKDIS, ISC_CLK(id));
	spin_unlock_irqrestore(&isc_clk->lock, flags);
}

static int isc_clk_is_enabled(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 status;
	int ret;

	if (isc_clk->id == ISC_ISPCK) {
		ret = pm_runtime_resume_and_get(isc_clk->dev);
		if (ret < 0)
			return 0;
	}

	regmap_read(isc_clk->regmap, ISC_CLKSR, &status);

	if (isc_clk->id == ISC_ISPCK)
		pm_runtime_put_sync(isc_clk->dev);

	return status & ISC_CLK(isc_clk->id) ? 1 : 0;
}

static unsigned long
isc_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	return DIV_ROUND_CLOSEST(parent_rate, isc_clk->div + 1);
}

static int isc_clk_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	long best_rate = -EINVAL;
	int best_diff = -1;
	unsigned int i, div;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_hw *parent;
		unsigned long parent_rate;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		if (!parent_rate)
			continue;

		for (div = 1; div < ISC_CLK_MAX_DIV + 2; div++) {
			unsigned long rate;
			int diff;

			rate = DIV_ROUND_CLOSEST(parent_rate, div);
			diff = abs(req->rate - rate);

			if (best_diff < 0 || best_diff > diff) {
				best_rate = rate;
				best_diff = diff;
				req->best_parent_rate = parent_rate;
				req->best_parent_hw = parent;
			}

			if (!best_diff || rate < req->rate)
				break;
		}

		if (!best_diff)
			break;
	}

	dev_dbg(isc_clk->dev,
		"ISC CLK: %s, best_rate = %ld, parent clk: %s @ %ld\n",
		__func__, best_rate,
		__clk_get_name((req->best_parent_hw)->clk),
		req->best_parent_rate);

	if (best_rate < 0)
		return best_rate;

	req->rate = best_rate;

	return 0;
}

static int isc_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	isc_clk->parent_id = index;

	return 0;
}

static u8 isc_clk_get_parent(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	return isc_clk->parent_id;
}

static int isc_clk_set_rate(struct clk_hw *hw,
			     unsigned long rate,
			     unsigned long parent_rate)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 div;

	if (!rate)
		return -EINVAL;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (div > (ISC_CLK_MAX_DIV + 1) || !div)
		return -EINVAL;

	isc_clk->div = div - 1;

	return 0;
}

static const struct clk_ops isc_clk_ops = {
	.prepare	= isc_clk_prepare,
	.unprepare	= isc_clk_unprepare,
	.enable		= isc_clk_enable,
	.disable	= isc_clk_disable,
	.is_enabled	= isc_clk_is_enabled,
	.recalc_rate	= isc_clk_recalc_rate,
	.determine_rate	= isc_clk_determine_rate,
	.set_parent	= isc_clk_set_parent,
	.get_parent	= isc_clk_get_parent,
	.set_rate	= isc_clk_set_rate,
};

static int isc_clk_register(struct isc_device *isc, unsigned int id)
{
	struct regmap *regmap = isc->regmap;
	struct device_node *np = isc->dev->of_node;
	struct isc_clk *isc_clk;
	struct clk_init_data init;
	const char *clk_name = np->name;
	const char *parent_names[3];
	int num_parents;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents < 1 || num_parents > 3)
		return -EINVAL;

	if (num_parents > 2 && id == ISC_ISPCK)
		num_parents = 2;

	of_clk_parent_fill(np, parent_names, num_parents);

	if (id == ISC_MCK)
		of_property_read_string(np, "clock-output-names", &clk_name);
	else
		clk_name = "isc-ispck";

	init.parent_names	= parent_names;
	init.num_parents	= num_parents;
	init.name		= clk_name;
	init.ops		= &isc_clk_ops;
	init.flags		= CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;

	isc_clk = &isc->isc_clks[id];
	isc_clk->hw.init	= &init;
	isc_clk->regmap		= regmap;
	isc_clk->id		= id;
	isc_clk->dev		= isc->dev;
	spin_lock_init(&isc_clk->lock);

	isc_clk->clk = clk_register(isc->dev, &isc_clk->hw);
	if (IS_ERR(isc_clk->clk)) {
		dev_err(isc->dev, "%s: clock register fail\n", clk_name);
		return PTR_ERR(isc_clk->clk);
	} else if (id == ISC_MCK)
		of_clk_add_provider(np, of_clk_src_simple_get, isc_clk->clk);

	return 0;
}

int isc_clk_init(struct isc_device *isc)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++)
		isc->isc_clks[i].clk = ERR_PTR(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++) {
		ret = isc_clk_register(isc, i);
		if (ret)
			return ret;
	}

	return 0;
}

void isc_clk_cleanup(struct isc_device *isc)
{
	unsigned int i;

	of_clk_del_provider(isc->dev->of_node);

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++) {
		struct isc_clk *isc_clk = &isc->isc_clks[i];

		if (!IS_ERR(isc_clk->clk))
			clk_unregister(isc_clk->clk);
	}
}

static int isc_queue_setup(struct vb2_queue *vq,
			    unsigned int *nbuffers, unsigned int *nplanes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	unsigned int size = isc->fmt.fmt.pix.sizeimage;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int isc_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct isc_device *isc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = isc->fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(&isc->v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	vbuf->field = isc->fmt.fmt.pix.field;

	return 0;
}

static void isc_start_dma(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 sizeimage = isc->fmt.fmt.pix.sizeimage;
	u32 dctrl_dview;
	dma_addr_t addr0;
	u32 h, w;

	h = isc->fmt.fmt.pix.height;
	w = isc->fmt.fmt.pix.width;

	/*
	 * In case the sensor is not RAW, it will output a pixel (12-16 bits)
	 * with two samples on the ISC Data bus (which is 8-12)
	 * ISC will count each sample, so, we need to multiply these values
	 * by two, to get the real number of samples for the required pixels.
	 */
	if (!ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code)) {
		h <<= 1;
		w <<= 1;
	}

	/*
	 * We limit the column/row count that the ISC will output according
	 * to the configured resolution that we want.
	 * This will avoid the situation where the sensor is misconfigured,
	 * sending more data, and the ISC will just take it and DMA to memory,
	 * causing corruption.
	 */
	regmap_write(regmap, ISC_PFE_CFG1,
		     (ISC_PFE_CFG1_COLMIN(0) & ISC_PFE_CFG1_COLMIN_MASK) |
		     (ISC_PFE_CFG1_COLMAX(w - 1) & ISC_PFE_CFG1_COLMAX_MASK));

	regmap_write(regmap, ISC_PFE_CFG2,
		     (ISC_PFE_CFG2_ROWMIN(0) & ISC_PFE_CFG2_ROWMIN_MASK) |
		     (ISC_PFE_CFG2_ROWMAX(h - 1) & ISC_PFE_CFG2_ROWMAX_MASK));

	regmap_update_bits(regmap, ISC_PFE_CFG0,
			   ISC_PFE_CFG0_COLEN | ISC_PFE_CFG0_ROWEN,
			   ISC_PFE_CFG0_COLEN | ISC_PFE_CFG0_ROWEN);

	addr0 = vb2_dma_contig_plane_dma_addr(&isc->cur_frm->vb.vb2_buf, 0);
	regmap_write(regmap, ISC_DAD0, addr0);

	switch (isc->config.fourcc) {
	case V4L2_PIX_FMT_YUV420:
		regmap_write(regmap, ISC_DAD1, addr0 + (sizeimage * 2) / 3);
		regmap_write(regmap, ISC_DAD2, addr0 + (sizeimage * 5) / 6);
		break;
	case V4L2_PIX_FMT_YUV422P:
		regmap_write(regmap, ISC_DAD1, addr0 + sizeimage / 2);
		regmap_write(regmap, ISC_DAD2, addr0 + (sizeimage * 3) / 4);
		break;
	default:
		break;
	}

	dctrl_dview = isc->config.dctrl_dview;

	regmap_write(regmap, ISC_DCTRL, dctrl_dview | ISC_DCTRL_IE_IS);
	spin_lock(&isc->awb_lock);
	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_CAPTURE);
	spin_unlock(&isc->awb_lock);
}

static void isc_set_pipeline(struct isc_device *isc, u32 pipeline)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 val, bay_cfg;
	const u32 *gamma;
	unsigned int i;

	/* WB-->CFA-->CC-->GAM-->CSC-->CBC-->SUB422-->SUB420 */
	for (i = 0; i < ISC_PIPE_LINE_NODE_NUM; i++) {
		val = pipeline & BIT(i) ? 1 : 0;
		regmap_field_write(isc->pipeline[i], val);
	}

	if (!pipeline)
		return;

	bay_cfg = isc->config.sd_format->cfa_baycfg;

	regmap_write(regmap, ISC_WB_CFG, bay_cfg);
	isc_update_awb_ctrls(isc);
	isc_update_v4l2_ctrls(isc);

	regmap_write(regmap, ISC_CFA_CFG, bay_cfg | ISC_CFA_CFG_EITPOL);

	gamma = &isc_gamma_table[ctrls->gamma_index][0];
	regmap_bulk_write(regmap, ISC_GAM_BENTRY, gamma, GAMMA_ENTRIES);
	regmap_bulk_write(regmap, ISC_GAM_GENTRY, gamma, GAMMA_ENTRIES);
	regmap_bulk_write(regmap, ISC_GAM_RENTRY, gamma, GAMMA_ENTRIES);

	/* Convert RGB to YUV */
	regmap_write(regmap, ISC_CSC_YR_YG, 0x42 | (0x81 << 16));
	regmap_write(regmap, ISC_CSC_YB_OY, 0x19 | (0x10 << 16));
	regmap_write(regmap, ISC_CSC_CBR_CBG, 0xFDA | (0xFB6 << 16));
	regmap_write(regmap, ISC_CSC_CBB_OCB, 0x70 | (0x80 << 16));
	regmap_write(regmap, ISC_CSC_CRR_CRG, 0x70 | (0xFA2 << 16));
	regmap_write(regmap, ISC_CSC_CRB_OCR, 0xFEE | (0x80 << 16));

	regmap_write(regmap, ISC_CBC_BRIGHT, ctrls->brightness);
	regmap_write(regmap, ISC_CBC_CONTRAST, ctrls->contrast);
}

static int isc_update_profile(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 sr;
	int counter = 100;

	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_UPPRO);

	regmap_read(regmap, ISC_CTRLSR, &sr);
	while ((sr & ISC_CTRL_UPPRO) && counter--) {
		usleep_range(1000, 2000);
		regmap_read(regmap, ISC_CTRLSR, &sr);
	}

	if (counter < 0) {
		v4l2_warn(&isc->v4l2_dev, "Time out to update profile\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void isc_set_histogram(struct isc_device *isc, bool enable)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (enable) {
		regmap_write(regmap, ISC_HIS_CFG,
			     ISC_HIS_CFG_MODE_GR |
			     (isc->config.sd_format->cfa_baycfg
					<< ISC_HIS_CFG_BAYSEL_SHIFT) |
					ISC_HIS_CFG_RAR);
		regmap_write(regmap, ISC_HIS_CTRL, ISC_HIS_CTRL_EN);
		regmap_write(regmap, ISC_INTEN, ISC_INT_HISDONE);
		ctrls->hist_id = ISC_HIS_CFG_MODE_GR;
		isc_update_profile(isc);
		regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

		ctrls->hist_stat = HIST_ENABLED;
	} else {
		regmap_write(regmap, ISC_INTDIS, ISC_INT_HISDONE);
		regmap_write(regmap, ISC_HIS_CTRL, ISC_HIS_CTRL_DIS);

		ctrls->hist_stat = HIST_DISABLED;
	}
}

static int isc_configure(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 pfe_cfg0, rlp_mode, dcfg, mask, pipeline;
	struct isc_subdev_entity *subdev = isc->current_subdev;

	pfe_cfg0 = isc->config.sd_format->pfe_cfg0_bps;
	rlp_mode = isc->config.rlp_cfg_mode;
	pipeline = isc->config.bits_pipeline;

	dcfg = isc->config.dcfg_imode |
		       ISC_DCFG_YMBSIZE_BEATS8 | ISC_DCFG_CMBSIZE_BEATS8;

	pfe_cfg0  |= subdev->pfe_cfg0 | ISC_PFE_CFG0_MODE_PROGRESSIVE;
	mask = ISC_PFE_CFG0_BPS_MASK | ISC_PFE_CFG0_HPOL_LOW |
	       ISC_PFE_CFG0_VPOL_LOW | ISC_PFE_CFG0_PPOL_LOW |
	       ISC_PFE_CFG0_MODE_MASK | ISC_PFE_CFG0_CCIR_CRC |
		   ISC_PFE_CFG0_CCIR656;

	regmap_update_bits(regmap, ISC_PFE_CFG0, mask, pfe_cfg0);

	regmap_update_bits(regmap, ISC_RLP_CFG, ISC_RLP_CFG_MODE_MASK,
			   rlp_mode);

	regmap_write(regmap, ISC_DCFG, dcfg);

	/* Set the pipeline */
	isc_set_pipeline(isc, pipeline);

	/*
	 * The current implemented histogram is available for RAW R, B, GB, GR
	 * channels. We need to check if sensor is outputting RAW BAYER
	 */
	if (isc->ctrls.awb &&
	    ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
		isc_set_histogram(isc, true);
	else
		isc_set_histogram(isc, false);

	/* Update profile */
	return isc_update_profile(isc);
}

static int isc_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	struct regmap *regmap = isc->regmap;
	struct isc_buffer *buf;
	unsigned long flags;
	int ret;

	/* Enable stream on the sub device */
	ret = v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		v4l2_err(&isc->v4l2_dev, "stream on failed in subdev %d\n",
			 ret);
		goto err_start_stream;
	}

	ret = pm_runtime_resume_and_get(isc->dev);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev, "RPM resume failed in subdev %d\n",
			 ret);
		goto err_pm_get;
	}

	ret = isc_configure(isc);
	if (unlikely(ret))
		goto err_configure;

	/* Enable DMA interrupt */
	regmap_write(regmap, ISC_INTEN, ISC_INT_DDONE);

	spin_lock_irqsave(&isc->dma_queue_lock, flags);

	isc->sequence = 0;
	isc->stop = false;
	reinit_completion(&isc->comp);

	isc->cur_frm = list_first_entry(&isc->dma_queue,
					struct isc_buffer, list);
	list_del(&isc->cur_frm->list);

	isc_start_dma(isc);

	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);

	/* if we streaming from RAW, we can do one-shot white balance adj */
	if (ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
		v4l2_ctrl_activate(isc->do_wb_ctrl, true);

	return 0;

err_configure:
	pm_runtime_put_sync(isc->dev);
err_pm_get:
	v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 0);

err_start_stream:
	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	list_for_each_entry(buf, &isc->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);

	return ret;
}

static void isc_stop_streaming(struct vb2_queue *vq)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	unsigned long flags;
	struct isc_buffer *buf;
	int ret;

	v4l2_ctrl_activate(isc->do_wb_ctrl, false);

	isc->stop = true;

	/* Wait until the end of the current frame */
	if (isc->cur_frm && !wait_for_completion_timeout(&isc->comp, 5 * HZ))
		v4l2_err(&isc->v4l2_dev,
			 "Timeout waiting for end of the capture\n");

	/* Disable DMA interrupt */
	regmap_write(isc->regmap, ISC_INTDIS, ISC_INT_DDONE);

	pm_runtime_put_sync(isc->dev);

	/* Disable stream on the sub device */
	ret = v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD)
		v4l2_err(&isc->v4l2_dev, "stream off failed in subdev\n");

	/* Release all active buffers */
	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	if (unlikely(isc->cur_frm)) {
		vb2_buffer_done(&isc->cur_frm->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		isc->cur_frm = NULL;
	}
	list_for_each_entry(buf, &isc->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);
}

static void isc_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct isc_buffer *buf = container_of(vbuf, struct isc_buffer, vb);
	struct isc_device *isc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	if (!isc->cur_frm && list_empty(&isc->dma_queue) &&
		vb2_is_streaming(vb->vb2_queue)) {
		isc->cur_frm = buf;
		isc_start_dma(isc);
	} else
		list_add_tail(&buf->list, &isc->dma_queue);
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);
}

static struct isc_format *find_format_by_fourcc(struct isc_device *isc,
						 unsigned int fourcc)
{
	unsigned int num_formats = isc->num_user_formats;
	struct isc_format *fmt;
	unsigned int i;

	for (i = 0; i < num_formats; i++) {
		fmt = isc->user_formats[i];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static const struct vb2_ops isc_vb2_ops = {
	.queue_setup		= isc_queue_setup,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.buf_prepare		= isc_buffer_prepare,
	.start_streaming	= isc_start_streaming,
	.stop_streaming		= isc_stop_streaming,
	.buf_queue		= isc_buffer_queue,
};

static int isc_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct isc_device *isc = video_drvdata(file);

	strscpy(cap->driver, ATMEL_ISC_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Atmel Image Sensor Controller", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", isc->v4l2_dev.name);

	return 0;
}

static int isc_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	u32 index = f->index;
	u32 i, supported_index;

	if (index < ARRAY_SIZE(controller_formats)) {
		f->pixelformat = controller_formats[index].fourcc;
		return 0;
	}

	index -= ARRAY_SIZE(controller_formats);

	i = 0;
	supported_index = 0;

	for (i = 0; i < ARRAY_SIZE(formats_list); i++) {
		if (!ISC_IS_FORMAT_RAW(formats_list[i].mbus_code) ||
		    !formats_list[i].sd_support)
			continue;
		if (supported_index == index) {
			f->pixelformat = formats_list[i].fourcc;
			return 0;
		}
		supported_index++;
	}

	return -EINVAL;
}

static int isc_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct isc_device *isc = video_drvdata(file);

	*fmt = isc->fmt;

	return 0;
}

/*
 * Checks the current configured format, if ISC can output it,
 * considering which type of format the ISC receives from the sensor
 */
static int isc_try_validate_formats(struct isc_device *isc)
{
	int ret;
	bool bayer = false, yuv = false, rgb = false, grey = false;

	/* all formats supported by the RLP module are OK */
	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		ret = 0;
		bayer = true;
		break;

	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUYV:
		ret = 0;
		yuv = true;
		break;

	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_ARGB555:
		ret = 0;
		rgb = true;
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
		ret = 0;
		grey = true;
		break;
	default:
	/* any other different formats are not supported */
		ret = -EINVAL;
	}
	v4l2_dbg(1, debug, &isc->v4l2_dev,
		 "Format validation, requested rgb=%u, yuv=%u, grey=%u, bayer=%u\n",
		 rgb, yuv, grey, bayer);

	/* we cannot output RAW if we do not receive RAW */
	if ((bayer) && !ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code))
		return -EINVAL;

	/* we cannot output GREY if we do not receive RAW/GREY */
	if (grey && !ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code) &&
	    !ISC_IS_FORMAT_GREY(isc->try_config.sd_format->mbus_code))
		return -EINVAL;

	return ret;
}

/*
 * Configures the RLP and DMA modules, depending on the output format
 * configured for the ISC.
 * If direct_dump == true, just dump raw data 8/16 bits depending on format.
 */
static int isc_try_configure_rlp_dma(struct isc_device *isc, bool direct_dump)
{
	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 8;
		break;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT10;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT12;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_RGB565:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_RGB565;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_ARGB444:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB444;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_ARGB555:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB555;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB32;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 32;
		break;
	case V4L2_PIX_FMT_YUV420:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YYCC;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_YC420P;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PLANAR;
		isc->try_config.bpp = 12;
		break;
	case V4L2_PIX_FMT_YUV422P:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YYCC;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_YC422P;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PLANAR;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_YUYV:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YYCC;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	case V4L2_PIX_FMT_GREY:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DATY8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 8;
		break;
	case V4L2_PIX_FMT_Y10:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DATY10;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		break;
	default:
		return -EINVAL;
	}

	if (direct_dump) {
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		return 0;
	}

	return 0;
}

/*
 * Configuring pipeline modules, depending on which format the ISC outputs
 * and considering which format it has as input from the sensor.
 */
static int isc_try_configure_pipeline(struct isc_device *isc)
{
	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				WB_ENABLE | GAM_ENABLES;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUV420:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				SUB420_ENABLE | SUB422_ENABLE | CBC_ENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUV422P:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				SUB422_ENABLE | CBC_ENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUYV:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				SUB422_ENABLE | CBC_ENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_GREY:
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
		/* if sensor format is RAW, we convert inside ISC */
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				CBC_ENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	default:
		isc->try_config.bits_pipeline = 0x0;
	}
	return 0;
}

static void isc_try_fse(struct isc_device *isc,
			struct v4l2_subdev_pad_config *pad_cfg)
{
	int ret;
	struct v4l2_subdev_frame_size_enum fse = {};

	/*
	 * If we do not know yet which format the subdev is using, we cannot
	 * do anything.
	 */
	if (!isc->try_config.sd_format)
		return;

	fse.code = isc->try_config.sd_format->mbus_code;
	fse.which = V4L2_SUBDEV_FORMAT_TRY;

	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, enum_frame_size,
			       pad_cfg, &fse);
	/*
	 * Attempt to obtain format size from subdev. If not available,
	 * just use the maximum ISC can receive.
	 */
	if (ret) {
		pad_cfg->try_crop.width = ISC_MAX_SUPPORT_WIDTH;
		pad_cfg->try_crop.height = ISC_MAX_SUPPORT_HEIGHT;
	} else {
		pad_cfg->try_crop.width = fse.max_width;
		pad_cfg->try_crop.height = fse.max_height;
	}
}

static int isc_try_fmt(struct isc_device *isc, struct v4l2_format *f,
			u32 *code)
{
	int i;
	struct isc_format *sd_fmt = NULL, *direct_fmt = NULL;
	struct v4l2_pix_format *pixfmt = &f->fmt.pix;
	struct v4l2_subdev_pad_config pad_cfg = {};
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	u32 mbus_code;
	int ret;
	bool rlp_dma_direct_dump = false;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* Step 1: find a RAW format that is supported */
	for (i = 0; i < isc->num_user_formats; i++) {
		if (ISC_IS_FORMAT_RAW(isc->user_formats[i]->mbus_code)) {
			sd_fmt = isc->user_formats[i];
			break;
		}
	}
	/* Step 2: We can continue with this RAW format, or we can look
	 * for better: maybe sensor supports directly what we need.
	 */
	direct_fmt = find_format_by_fourcc(isc, pixfmt->pixelformat);

	/* Step 3: We have both. We decide given the module parameter which
	 * one to use.
	 */
	if (direct_fmt && sd_fmt && sensor_preferred)
		sd_fmt = direct_fmt;

	/* Step 4: we do not have RAW but we have a direct format. Use it. */
	if (direct_fmt && !sd_fmt)
		sd_fmt = direct_fmt;

	/* Step 5: if we are using a direct format, we need to package
	 * everything as 8 bit data and just dump it
	 */
	if (sd_fmt == direct_fmt)
		rlp_dma_direct_dump = true;

	/* Step 6: We have no format. This can happen if the userspace
	 * requests some weird/invalid format.
	 * In this case, default to whatever we have
	 */
	if (!sd_fmt && !direct_fmt) {
		sd_fmt = isc->user_formats[isc->num_user_formats - 1];
		v4l2_dbg(1, debug, &isc->v4l2_dev,
			 "Sensor not supporting %.4s, using %.4s\n",
			 (char *)&pixfmt->pixelformat, (char *)&sd_fmt->fourcc);
	}

	if (!sd_fmt) {
		ret = -EINVAL;
		goto isc_try_fmt_err;
	}

	/* Step 7: Print out what we decided for debugging */
	v4l2_dbg(1, debug, &isc->v4l2_dev,
		 "Preferring to have sensor using format %.4s\n",
		 (char *)&sd_fmt->fourcc);

	/* Step 8: at this moment we decided which format the subdev will use */
	isc->try_config.sd_format = sd_fmt;

	/* Limit to Atmel ISC hardware capabilities */
	if (pixfmt->width > ISC_MAX_SUPPORT_WIDTH)
		pixfmt->width = ISC_MAX_SUPPORT_WIDTH;
	if (pixfmt->height > ISC_MAX_SUPPORT_HEIGHT)
		pixfmt->height = ISC_MAX_SUPPORT_HEIGHT;

	/*
	 * The mbus format is the one the subdev outputs.
	 * The pixels will be transferred in this format Sensor -> ISC
	 */
	mbus_code = sd_fmt->mbus_code;

	/*
	 * Validate formats. If the required format is not OK, default to raw.
	 */

	isc->try_config.fourcc = pixfmt->pixelformat;

	if (isc_try_validate_formats(isc)) {
		pixfmt->pixelformat = isc->try_config.fourcc = sd_fmt->fourcc;
		/* Re-try to validate the new format */
		ret = isc_try_validate_formats(isc);
		if (ret)
			goto isc_try_fmt_err;
	}

	ret = isc_try_configure_rlp_dma(isc, rlp_dma_direct_dump);
	if (ret)
		goto isc_try_fmt_err;

	ret = isc_try_configure_pipeline(isc);
	if (ret)
		goto isc_try_fmt_err;

	/* Obtain frame sizes if possible to have crop requirements ready */
	isc_try_fse(isc, &pad_cfg);

	v4l2_fill_mbus_format(&format.format, pixfmt, mbus_code);
	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, set_fmt,
			       &pad_cfg, &format);
	if (ret < 0)
		goto isc_try_fmt_subdev_err;

	v4l2_fill_pix_format(pixfmt, &format.format);

	pixfmt->field = V4L2_FIELD_NONE;
	pixfmt->bytesperline = (pixfmt->width * isc->try_config.bpp) >> 3;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;

	if (code)
		*code = mbus_code;

	return 0;

isc_try_fmt_err:
	v4l2_err(&isc->v4l2_dev, "Could not find any possible format for a working pipeline\n");
isc_try_fmt_subdev_err:
	memset(&isc->try_config, 0, sizeof(isc->try_config));

	return ret;
}

static int isc_set_fmt(struct isc_device *isc, struct v4l2_format *f)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	u32 mbus_code = 0;
	int ret;

	ret = isc_try_fmt(isc, f, &mbus_code);
	if (ret)
		return ret;

	v4l2_fill_mbus_format(&format.format, &f->fmt.pix, mbus_code);
	ret = v4l2_subdev_call(isc->current_subdev->sd, pad,
			       set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	isc->fmt = *f;

	if (isc->try_config.sd_format && isc->config.sd_format &&
	    isc->try_config.sd_format != isc->config.sd_format) {
		isc->ctrls.hist_stat = HIST_INIT;
		isc_reset_awb_ctrls(isc);
		isc_update_v4l2_ctrls(isc);
	}
	/* make the try configuration active */
	isc->config = isc->try_config;

	v4l2_dbg(1, debug, &isc->v4l2_dev, "New ISC configuration in place\n");

	return 0;
}

static int isc_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct isc_device *isc = video_drvdata(file);

	if (vb2_is_streaming(&isc->vb2_vidq))
		return -EBUSY;

	return isc_set_fmt(isc, f);
}

static int isc_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct isc_device *isc = video_drvdata(file);

	return isc_try_fmt(isc, f, NULL);
}

static int isc_enum_input(struct file *file, void *priv,
			   struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = 0;
	strscpy(inp->name, "Camera", sizeof(inp->name));

	return 0;
}

static int isc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int isc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

static int isc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct isc_device *isc = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), isc->current_subdev->sd, a);
}

static int isc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct isc_device *isc = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), isc->current_subdev->sd, a);
}

static int isc_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev_frame_size_enum fse = {
		.code = isc->config.sd_format->mbus_code,
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret = -EINVAL;
	int i;

	for (i = 0; i < isc->num_user_formats; i++)
		if (isc->user_formats[i]->fourcc == fsize->pixel_format)
			ret = 0;

	for (i = 0; i < ARRAY_SIZE(controller_formats); i++)
		if (controller_formats[i].fourcc == fsize->pixel_format)
			ret = 0;

	if (ret)
		return ret;

	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, enum_frame_size,
			       NULL, &fse);
	if (ret)
		return ret;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.max_width;
	fsize->discrete.height = fse.max_height;

	return 0;
}

static int isc_enum_frameintervals(struct file *file, void *fh,
				    struct v4l2_frmivalenum *fival)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev_frame_interval_enum fie = {
		.code = isc->config.sd_format->mbus_code,
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret = -EINVAL;
	unsigned int i;

	for (i = 0; i < isc->num_user_formats; i++)
		if (isc->user_formats[i]->fourcc == fival->pixel_format)
			ret = 0;

	for (i = 0; i < ARRAY_SIZE(controller_formats); i++)
		if (controller_formats[i].fourcc == fival->pixel_format)
			ret = 0;

	if (ret)
		return ret;

	ret = v4l2_subdev_call(isc->current_subdev->sd, pad,
			       enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static const struct v4l2_ioctl_ops isc_ioctl_ops = {
	.vidioc_querycap		= isc_querycap,
	.vidioc_enum_fmt_vid_cap	= isc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= isc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= isc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= isc_try_fmt_vid_cap,

	.vidioc_enum_input		= isc_enum_input,
	.vidioc_g_input			= isc_g_input,
	.vidioc_s_input			= isc_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_g_parm			= isc_g_parm,
	.vidioc_s_parm			= isc_s_parm,
	.vidioc_enum_framesizes		= isc_enum_framesizes,
	.vidioc_enum_frameintervals	= isc_enum_frameintervals,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int isc_open(struct file *file)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev *sd = isc->current_subdev->sd;
	int ret;

	if (mutex_lock_interruptible(&isc->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	if (!v4l2_fh_is_singular_file(file))
		goto unlock;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_fh_release(file);
		goto unlock;
	}

	ret = isc_set_fmt(isc, &isc->fmt);
	if (ret) {
		v4l2_subdev_call(sd, core, s_power, 0);
		v4l2_fh_release(file);
	}

unlock:
	mutex_unlock(&isc->lock);
	return ret;
}

static int isc_release(struct file *file)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev *sd = isc->current_subdev->sd;
	bool fh_singular;
	int ret;

	mutex_lock(&isc->lock);

	fh_singular = v4l2_fh_is_singular_file(file);

	ret = _vb2_fop_release(file, NULL);

	if (fh_singular)
		v4l2_subdev_call(sd, core, s_power, 0);

	mutex_unlock(&isc->lock);

	return ret;
}

static const struct v4l2_file_operations isc_fops = {
	.owner		= THIS_MODULE,
	.open		= isc_open,
	.release	= isc_release,
	.unlocked_ioctl	= video_ioctl2,
	.read		= vb2_fop_read,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

irqreturn_t isc_interrupt(int irq, void *dev_id)
{
	struct isc_device *isc = (struct isc_device *)dev_id;
	struct regmap *regmap = isc->regmap;
	u32 isc_intsr, isc_intmask, pending;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(regmap, ISC_INTSR, &isc_intsr);
	regmap_read(regmap, ISC_INTMASK, &isc_intmask);

	pending = isc_intsr & isc_intmask;

	if (likely(pending & ISC_INT_DDONE)) {
		spin_lock(&isc->dma_queue_lock);
		if (isc->cur_frm) {
			struct vb2_v4l2_buffer *vbuf = &isc->cur_frm->vb;
			struct vb2_buffer *vb = &vbuf->vb2_buf;

			vb->timestamp = ktime_get_ns();
			vbuf->sequence = isc->sequence++;
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
			isc->cur_frm = NULL;
		}

		if (!list_empty(&isc->dma_queue) && !isc->stop) {
			isc->cur_frm = list_first_entry(&isc->dma_queue,
						     struct isc_buffer, list);
			list_del(&isc->cur_frm->list);

			isc_start_dma(isc);
		}

		if (isc->stop)
			complete(&isc->comp);

		ret = IRQ_HANDLED;
		spin_unlock(&isc->dma_queue_lock);
	}

	if (pending & ISC_INT_HISDONE) {
		schedule_work(&isc->awb_work);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static void isc_hist_count(struct isc_device *isc, u32 *min, u32 *max)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 *hist_count = &ctrls->hist_count[ctrls->hist_id];
	u32 *hist_entry = &ctrls->hist_entry[0];
	u32 i;

	*min = 0;
	*max = HIST_ENTRIES;

	regmap_bulk_read(regmap, ISC_HIS_ENTRY, hist_entry, HIST_ENTRIES);

	*hist_count = 0;
	/*
	 * we deliberately ignore the end of the histogram,
	 * the most white pixels
	 */
	for (i = 1; i < HIST_ENTRIES; i++) {
		if (*hist_entry && !*min)
			*min = i;
		if (*hist_entry)
			*max = i;
		*hist_count += i * (*hist_entry++);
	}

	if (!*min)
		*min = 1;
}

static void isc_wb_update(struct isc_ctrls *ctrls)
{
	u32 *hist_count = &ctrls->hist_count[0];
	u32 c, offset[4];
	u64 avg = 0;
	/* We compute two gains, stretch gain and grey world gain */
	u32 s_gain[4], gw_gain[4];

	/*
	 * According to Grey World, we need to set gains for R/B to normalize
	 * them towards the green channel.
	 * Thus we want to keep Green as fixed and adjust only Red/Blue
	 * Compute the average of the both green channels first
	 */
	avg = (u64)hist_count[ISC_HIS_CFG_MODE_GR] +
		(u64)hist_count[ISC_HIS_CFG_MODE_GB];
	avg >>= 1;

	/* Green histogram is null, nothing to do */
	if (!avg)
		return;

	for (c = ISC_HIS_CFG_MODE_GR; c <= ISC_HIS_CFG_MODE_B; c++) {
		/*
		 * the color offset is the minimum value of the histogram.
		 * we stretch this color to the full range by substracting
		 * this value from the color component.
		 */
		offset[c] = ctrls->hist_minmax[c][HIST_MIN_INDEX];
		/*
		 * The offset is always at least 1. If the offset is 1, we do
		 * not need to adjust it, so our result must be zero.
		 * the offset is computed in a histogram on 9 bits (0..512)
		 * but the offset in register is based on
		 * 12 bits pipeline (0..4096).
		 * we need to shift with the 3 bits that the histogram is
		 * ignoring
		 */
		ctrls->offset[c] = (offset[c] - 1) << 3;

		/*
		 * the offset is then taken and converted to 2's complements,
		 * and must be negative, as we subtract this value from the
		 * color components
		 */
		ctrls->offset[c] = -ctrls->offset[c];

		/*
		 * the stretch gain is the total number of histogram bins
		 * divided by the actual range of color component (Max - Min)
		 * If we compute gain like this, the actual color component
		 * will be stretched to the full histogram.
		 * We need to shift 9 bits for precision, we have 9 bits for
		 * decimals
		 */
		s_gain[c] = (HIST_ENTRIES << 9) /
			(ctrls->hist_minmax[c][HIST_MAX_INDEX] -
			ctrls->hist_minmax[c][HIST_MIN_INDEX] + 1);

		/*
		 * Now we have to compute the gain w.r.t. the average.
		 * Add/lose gain to the component towards the average.
		 * If it happens that the component is zero, use the
		 * fixed point value : 1.0 gain.
		 */
		if (hist_count[c])
			gw_gain[c] = div_u64(avg << 9, hist_count[c]);
		else
			gw_gain[c] = 1 << 9;

		/* multiply both gains and adjust for decimals */
		ctrls->gain[c] = s_gain[c] * gw_gain[c];
		ctrls->gain[c] >>= 9;
	}
}

static void isc_awb_work(struct work_struct *w)
{
	struct isc_device *isc =
		container_of(w, struct isc_device, awb_work);
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 hist_id = ctrls->hist_id;
	u32 baysel;
	unsigned long flags;
	u32 min, max;
	int ret;

	/* streaming is not active anymore */
	if (isc->stop)
		return;

	if (ctrls->hist_stat != HIST_ENABLED)
		return;

	isc_hist_count(isc, &min, &max);
	ctrls->hist_minmax[hist_id][HIST_MIN_INDEX] = min;
	ctrls->hist_minmax[hist_id][HIST_MAX_INDEX] = max;

	if (hist_id != ISC_HIS_CFG_MODE_B) {
		hist_id++;
	} else {
		isc_wb_update(ctrls);
		hist_id = ISC_HIS_CFG_MODE_GR;
	}

	ctrls->hist_id = hist_id;
	baysel = isc->config.sd_format->cfa_baycfg << ISC_HIS_CFG_BAYSEL_SHIFT;

	ret = pm_runtime_resume_and_get(isc->dev);
	if (ret < 0)
		return;

	/*
	 * only update if we have all the required histograms and controls
	 * if awb has been disabled, we need to reset registers as well.
	 */
	if (hist_id == ISC_HIS_CFG_MODE_GR || ctrls->awb == ISC_WB_NONE) {
		/*
		 * It may happen that DMA Done IRQ will trigger while we are
		 * updating white balance registers here.
		 * In that case, only parts of the controls have been updated.
		 * We can avoid that by locking the section.
		 */
		spin_lock_irqsave(&isc->awb_lock, flags);
		isc_update_awb_ctrls(isc);
		spin_unlock_irqrestore(&isc->awb_lock, flags);

		/*
		 * if we are doing just the one time white balance adjustment,
		 * we are basically done.
		 */
		if (ctrls->awb == ISC_WB_ONETIME) {
			v4l2_info(&isc->v4l2_dev,
				  "Completed one time white-balance adjustment.\n");
			/* update the v4l2 controls values */
			isc_update_v4l2_ctrls(isc);
			ctrls->awb = ISC_WB_NONE;
		}
	}
	regmap_write(regmap, ISC_HIS_CFG, hist_id | baysel | ISC_HIS_CFG_RAR);
	isc_update_profile(isc);
	/* if awb has been disabled, we don't need to start another histogram */
	if (ctrls->awb)
		regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

	pm_runtime_put_sync(isc->dev);
}

static int isc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrls->brightness = ctrl->val & ISC_CBC_BRIGHT_MASK;
		break;
	case V4L2_CID_CONTRAST:
		ctrls->contrast = ctrl->val & ISC_CBC_CONTRAST_MASK;
		break;
	case V4L2_CID_GAMMA:
		ctrls->gamma_index = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops isc_ctrl_ops = {
	.s_ctrl	= isc_s_ctrl,
};

static int isc_s_awb_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		if (ctrl->val == 1)
			ctrls->awb = ISC_WB_AUTO;
		else
			ctrls->awb = ISC_WB_NONE;

		/* we did not configure ISC yet */
		if (!isc->config.sd_format)
			break;

		/* configure the controls with new values from v4l2 */
		if (ctrl->cluster[ISC_CTRL_R_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_R] = isc->r_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_B_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_B] = isc->b_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GR_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_GR] = isc->gr_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GB_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_GB] = isc->gb_gain_ctrl->val;

		if (ctrl->cluster[ISC_CTRL_R_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_R] = isc->r_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_B_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_B] = isc->b_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GR_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_GR] = isc->gr_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GB_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_GB] = isc->gb_off_ctrl->val;

		isc_update_awb_ctrls(isc);

		if (vb2_is_streaming(&isc->vb2_vidq)) {
			/*
			 * If we are streaming, we can update profile to
			 * have the new settings in place.
			 */
			isc_update_profile(isc);
		} else {
			/*
			 * The auto cluster will activate automatically this
			 * control. This has to be deactivated when not
			 * streaming.
			 */
			v4l2_ctrl_activate(isc->do_wb_ctrl, false);
		}

		/* if we have autowhitebalance on, start histogram procedure */
		if (ctrls->awb == ISC_WB_AUTO &&
		    vb2_is_streaming(&isc->vb2_vidq) &&
		    ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
			isc_set_histogram(isc, true);

		/*
		 * for one time whitebalance adjustment, check the button,
		 * if it's pressed, perform the one time operation.
		 */
		if (ctrls->awb == ISC_WB_NONE &&
		    ctrl->cluster[ISC_CTRL_DO_WB]->is_new &&
		    !(ctrl->cluster[ISC_CTRL_DO_WB]->flags &
		    V4L2_CTRL_FLAG_INACTIVE)) {
			ctrls->awb = ISC_WB_ONETIME;
			isc_set_histogram(isc, true);
			v4l2_dbg(1, debug, &isc->v4l2_dev,
				 "One time white-balance started.\n");
		}
		return 0;
	}
	return 0;
}

static int isc_g_volatile_awb_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	switch (ctrl->id) {
	/* being a cluster, this id will be called for every control */
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->cluster[ISC_CTRL_R_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_R];
		ctrl->cluster[ISC_CTRL_B_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_B];
		ctrl->cluster[ISC_CTRL_GR_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_GR];
		ctrl->cluster[ISC_CTRL_GB_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_GB];

		ctrl->cluster[ISC_CTRL_R_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_R];
		ctrl->cluster[ISC_CTRL_B_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_B];
		ctrl->cluster[ISC_CTRL_GR_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_GR];
		ctrl->cluster[ISC_CTRL_GB_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_GB];
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops isc_awb_ops = {
	.s_ctrl = isc_s_awb_ctrl,
	.g_volatile_ctrl = isc_g_volatile_awb_ctrl,
};

#define ISC_CTRL_OFF(_name, _id, _name_str) \
	static const struct v4l2_ctrl_config _name = { \
		.ops = &isc_awb_ops, \
		.id = _id, \
		.name = _name_str, \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.flags = V4L2_CTRL_FLAG_SLIDER, \
		.min = -4095, \
		.max = 4095, \
		.step = 1, \
		.def = 0, \
	}

ISC_CTRL_OFF(isc_r_off_ctrl, ISC_CID_R_OFFSET, "Red Component Offset");
ISC_CTRL_OFF(isc_b_off_ctrl, ISC_CID_B_OFFSET, "Blue Component Offset");
ISC_CTRL_OFF(isc_gr_off_ctrl, ISC_CID_GR_OFFSET, "Green Red Component Offset");
ISC_CTRL_OFF(isc_gb_off_ctrl, ISC_CID_GB_OFFSET, "Green Blue Component Offset");

#define ISC_CTRL_GAIN(_name, _id, _name_str) \
	static const struct v4l2_ctrl_config _name = { \
		.ops = &isc_awb_ops, \
		.id = _id, \
		.name = _name_str, \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.flags = V4L2_CTRL_FLAG_SLIDER, \
		.min = 0, \
		.max = 8191, \
		.step = 1, \
		.def = 512, \
	}

ISC_CTRL_GAIN(isc_r_gain_ctrl, ISC_CID_R_GAIN, "Red Component Gain");
ISC_CTRL_GAIN(isc_b_gain_ctrl, ISC_CID_B_GAIN, "Blue Component Gain");
ISC_CTRL_GAIN(isc_gr_gain_ctrl, ISC_CID_GR_GAIN, "Green Red Component Gain");
ISC_CTRL_GAIN(isc_gb_gain_ctrl, ISC_CID_GB_GAIN, "Green Blue Component Gain");

static int isc_ctrl_init(struct isc_device *isc)
{
	const struct v4l2_ctrl_ops *ops = &isc_ctrl_ops;
	struct isc_ctrls *ctrls = &isc->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	ctrls->hist_stat = HIST_INIT;
	isc_reset_awb_ctrls(isc);

	ret = v4l2_ctrl_handler_init(hdl, 13);
	if (ret < 0)
		return ret;

	ctrls->brightness = 0;
	ctrls->contrast = 256;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS, -1024, 1023, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -2048, 2047, 1, 256);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAMMA, 0, GAMMA_MAX, 1, 2);
	isc->awb_ctrl = v4l2_ctrl_new_std(hdl, &isc_awb_ops,
					  V4L2_CID_AUTO_WHITE_BALANCE,
					  0, 1, 1, 1);

	/* do_white_balance is a button, so min,max,step,default are ignored */
	isc->do_wb_ctrl = v4l2_ctrl_new_std(hdl, &isc_awb_ops,
					    V4L2_CID_DO_WHITE_BALANCE,
					    0, 0, 0, 0);

	if (!isc->do_wb_ctrl) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	v4l2_ctrl_activate(isc->do_wb_ctrl, false);

	isc->r_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_r_gain_ctrl, NULL);
	isc->b_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_b_gain_ctrl, NULL);
	isc->gr_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gr_gain_ctrl, NULL);
	isc->gb_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gb_gain_ctrl, NULL);
	isc->r_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_r_off_ctrl, NULL);
	isc->b_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_b_off_ctrl, NULL);
	isc->gr_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gr_off_ctrl, NULL);
	isc->gb_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gb_off_ctrl, NULL);

	/*
	 * The cluster is in auto mode with autowhitebalance enabled
	 * and manual mode otherwise.
	 */
	v4l2_ctrl_auto_cluster(10, &isc->awb_ctrl, 0, true);

	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

static int isc_async_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_subdev *asd)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	struct isc_subdev_entity *subdev_entity =
		container_of(notifier, struct isc_subdev_entity, notifier);

	if (video_is_registered(&isc->video_dev)) {
		v4l2_err(&isc->v4l2_dev, "only supports one sub-device.\n");
		return -EBUSY;
	}

	subdev_entity->sd = subdev;

	return 0;
}

static void isc_async_unbind(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	cancel_work_sync(&isc->awb_work);
	video_unregister_device(&isc->video_dev);
	v4l2_ctrl_handler_free(&isc->ctrls.handler);
}

static struct isc_format *find_format_by_code(unsigned int code, int *index)
{
	struct isc_format *fmt = &formats_list[0];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats_list); i++) {
		if (fmt->mbus_code == code) {
			*index = i;
			return fmt;
		}

		fmt++;
	}

	return NULL;
}

static int isc_formats_init(struct isc_device *isc)
{
	struct isc_format *fmt;
	struct v4l2_subdev *subdev = isc->current_subdev->sd;
	unsigned int num_fmts, i, j;
	u32 list_size = ARRAY_SIZE(formats_list);
	struct v4l2_subdev_mbus_code_enum mbus_code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	num_fmts = 0;
	while (!v4l2_subdev_call(subdev, pad, enum_mbus_code,
	       NULL, &mbus_code)) {
		mbus_code.index++;

		fmt = find_format_by_code(mbus_code.code, &i);
		if (!fmt) {
			v4l2_warn(&isc->v4l2_dev, "Mbus code %x not supported\n",
				  mbus_code.code);
			continue;
		}

		fmt->sd_support = true;
		num_fmts++;
	}

	if (!num_fmts)
		return -ENXIO;

	isc->num_user_formats = num_fmts;
	isc->user_formats = devm_kcalloc(isc->dev,
					 num_fmts, sizeof(*isc->user_formats),
					 GFP_KERNEL);
	if (!isc->user_formats)
		return -ENOMEM;

	fmt = &formats_list[0];
	for (i = 0, j = 0; i < list_size; i++) {
		if (fmt->sd_support)
			isc->user_formats[j++] = fmt;
		fmt++;
	}

	return 0;
}

static int isc_set_default_fmt(struct isc_device *isc)
{
	struct v4l2_format f = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width		= VGA_WIDTH,
			.height		= VGA_HEIGHT,
			.field		= V4L2_FIELD_NONE,
			.pixelformat	= isc->user_formats[0]->fourcc,
		},
	};
	int ret;

	ret = isc_try_fmt(isc, &f, NULL);
	if (ret)
		return ret;

	isc->fmt = f;
	return 0;
}

static int isc_async_complete(struct v4l2_async_notifier *notifier)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	struct video_device *vdev = &isc->video_dev;
	struct vb2_queue *q = &isc->vb2_vidq;
	int ret = 0;

	INIT_WORK(&isc->awb_work, isc_awb_work);

	ret = v4l2_device_register_subdev_nodes(&isc->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev, "Failed to register subdev nodes\n");
		return ret;
	}

	isc->current_subdev = container_of(notifier,
					   struct isc_subdev_entity, notifier);
	mutex_init(&isc->lock);
	init_completion(&isc->comp);

	/* Initialize videobuf2 queue */
	q->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes		= VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv		= isc;
	q->buf_struct_size	= sizeof(struct isc_buffer);
	q->ops			= &isc_vb2_ops;
	q->mem_ops		= &vb2_dma_contig_memops;
	q->timestamp_flags	= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock			= &isc->lock;
	q->min_buffers_needed	= 1;
	q->dev			= isc->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "vb2_queue_init() failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	/* Init video dma queues */
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_lock_init(&isc->dma_queue_lock);
	spin_lock_init(&isc->awb_lock);

	ret = isc_formats_init(isc);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "Init format failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	ret = isc_set_default_fmt(isc);
	if (ret) {
		v4l2_err(&isc->v4l2_dev, "Could not set default format\n");
		goto isc_async_complete_err;
	}

	ret = isc_ctrl_init(isc);
	if (ret) {
		v4l2_err(&isc->v4l2_dev, "Init isc ctrols failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	/* Register video device */
	strscpy(vdev->name, ATMEL_ISC_NAME, sizeof(vdev->name));
	vdev->release		= video_device_release_empty;
	vdev->fops		= &isc_fops;
	vdev->ioctl_ops		= &isc_ioctl_ops;
	vdev->v4l2_dev		= &isc->v4l2_dev;
	vdev->vfl_dir		= VFL_DIR_RX;
	vdev->queue		= q;
	vdev->lock		= &isc->lock;
	vdev->ctrl_handler	= &isc->ctrls.handler;
	vdev->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
	video_set_drvdata(vdev, isc);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "video_register_device failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	return 0;

isc_async_complete_err:
	mutex_destroy(&isc->lock);
	return ret;
}

const struct v4l2_async_notifier_operations isc_async_ops = {
	.bound = isc_async_bound,
	.unbind = isc_async_unbind,
	.complete = isc_async_complete,
};

void isc_subdev_cleanup(struct isc_device *isc)
{
	struct isc_subdev_entity *subdev_entity;

	list_for_each_entry(subdev_entity, &isc->subdev_entities, list) {
		v4l2_async_notifier_unregister(&subdev_entity->notifier);
		v4l2_async_notifier_cleanup(&subdev_entity->notifier);
	}

	INIT_LIST_HEAD(&isc->subdev_entities);
}

int isc_pipeline_init(struct isc_device *isc)
{
	struct device *dev = isc->dev;
	struct regmap *regmap = isc->regmap;
	struct regmap_field *regs;
	unsigned int i;

	/* WB-->CFA-->CC-->GAM-->CSC-->CBC-->SUB422-->SUB420 */
	const struct reg_field regfields[ISC_PIPE_LINE_NODE_NUM] = {
		REG_FIELD(ISC_WB_CTRL, 0, 0),
		REG_FIELD(ISC_CFA_CTRL, 0, 0),
		REG_FIELD(ISC_CC_CTRL, 0, 0),
		REG_FIELD(ISC_GAM_CTRL, 0, 0),
		REG_FIELD(ISC_GAM_CTRL, 1, 1),
		REG_FIELD(ISC_GAM_CTRL, 2, 2),
		REG_FIELD(ISC_GAM_CTRL, 3, 3),
		REG_FIELD(ISC_CSC_CTRL, 0, 0),
		REG_FIELD(ISC_CBC_CTRL, 0, 0),
		REG_FIELD(ISC_SUB422_CTRL, 0, 0),
		REG_FIELD(ISC_SUB420_CTRL, 0, 0),
	};

	for (i = 0; i < ISC_PIPE_LINE_NODE_NUM; i++) {
		regs = devm_regmap_field_alloc(dev, regmap, regfields[i]);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		isc->pipeline[i] =  regs;
	}

	return 0;
}

/* regmap configuration */
#define ATMEL_ISC_REG_MAX    0xbfc
const struct regmap_config isc_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= ATMEL_ISC_REG_MAX,
};

