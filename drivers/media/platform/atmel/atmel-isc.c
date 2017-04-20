/*
 * Atmel Image Sensor Controller (ISC) driver
 *
 * Copyright (C) 2016 Atmel
 *
 * Author: Songjun Wu <songjun.wu@microchip.com>
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "atmel-isc-regs.h"

#define ATMEL_ISC_NAME		"atmel_isc"

#define ISC_MAX_SUPPORT_WIDTH   2592
#define ISC_MAX_SUPPORT_HEIGHT  1944

#define ISC_CLK_MAX_DIV		255

enum isc_clk_id {
	ISC_ISPCK = 0,
	ISC_MCK = 1,
};

struct isc_clk {
	struct clk_hw   hw;
	struct clk      *clk;
	struct regmap   *regmap;
	u8		id;
	u8		parent_id;
	u32		div;
	struct device	*dev;
};

#define to_isc_clk(hw) container_of(hw, struct isc_clk, hw)

struct isc_buffer {
	struct vb2_v4l2_buffer  vb;
	struct list_head	list;
};

struct isc_subdev_entity {
	struct v4l2_subdev		*sd;
	struct v4l2_async_subdev	*asd;
	struct v4l2_async_notifier      notifier;
	struct v4l2_subdev_pad_config	*config;

	u32 pfe_cfg0;

	struct list_head list;
};

/*
 * struct isc_format - ISC media bus format information
 * @fourcc:		Fourcc code for this format
 * @mbus_code:		V4L2 media bus format code.
 * @bpp:		Bits per pixel (when stored in memory)
 * @reg_bps:		reg value for bits per sample
 *			(when transferred over a bus)
 * @pipeline:		pipeline switch
 * @sd_support:		Subdev supports this format
 * @isc_support:	ISC can convert raw format to this format
 */
struct isc_format {
	u32	fourcc;
	u32	mbus_code;
	u8	bpp;

	u32	reg_bps;
	u32	reg_bay_cfg;
	u32	reg_rlp_mode;
	u32	reg_dcfg_imode;
	u32	reg_dctrl_dview;

	u32	pipeline;

	bool	sd_support;
	bool	isc_support;
};


#define HIST_ENTRIES		512
#define HIST_BAYER		(ISC_HIS_CFG_MODE_B + 1)

enum{
	HIST_INIT = 0,
	HIST_ENABLED,
	HIST_DISABLED,
};

struct isc_ctrls {
	struct v4l2_ctrl_handler handler;

	u32 brightness;
	u32 contrast;
	u8 gamma_index;
	u8 awb;

	u32 r_gain;
	u32 b_gain;

	u32 hist_entry[HIST_ENTRIES];
	u32 hist_count[HIST_BAYER];
	u8 hist_id;
	u8 hist_stat;
};

#define ISC_PIPE_LINE_NODE_NUM	11

struct isc_device {
	struct regmap		*regmap;
	struct clk		*hclock;
	struct clk		*ispck;
	struct isc_clk		isc_clks[2];

	struct device		*dev;
	struct v4l2_device	v4l2_dev;
	struct video_device	video_dev;

	struct vb2_queue	vb2_vidq;
	spinlock_t		dma_queue_lock;
	struct list_head	dma_queue;
	struct isc_buffer	*cur_frm;
	unsigned int		sequence;
	bool			stop;
	struct completion	comp;

	struct v4l2_format	fmt;
	struct isc_format	**user_formats;
	unsigned int		num_user_formats;
	const struct isc_format	*current_fmt;
	const struct isc_format	*raw_fmt;

	struct isc_ctrls	ctrls;
	struct work_struct	awb_work;

	struct mutex		lock;

	struct regmap_field	*pipeline[ISC_PIPE_LINE_NODE_NUM];

	struct isc_subdev_entity	*current_subdev;
	struct list_head		subdev_entities;
};

#define RAW_FMT_IND_START    0
#define RAW_FMT_IND_END      11
#define ISC_FMT_IND_START    12
#define ISC_FMT_IND_END      14

static struct isc_format isc_formats[] = {
	{ V4L2_PIX_FMT_SBGGR8, MEDIA_BUS_FMT_SBGGR8_1X8, 8,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_DAT8,
	  ISC_DCFG_IMODE_PACKED8, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGBRG8, MEDIA_BUS_FMT_SGBRG8_1X8, 8,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_GBGB, ISC_RLP_CFG_MODE_DAT8,
	  ISC_DCFG_IMODE_PACKED8, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGRBG8, MEDIA_BUS_FMT_SGRBG8_1X8, 8,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_GRGR, ISC_RLP_CFG_MODE_DAT8,
	  ISC_DCFG_IMODE_PACKED8, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SRGGB8, MEDIA_BUS_FMT_SRGGB8_1X8, 8,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_RGRG, ISC_RLP_CFG_MODE_DAT8,
	  ISC_DCFG_IMODE_PACKED8, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },

	{ V4L2_PIX_FMT_SBGGR10, MEDIA_BUS_FMT_SBGGR10_1X10, 16,
	  ISC_PFG_CFG0_BPS_TEN, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_DAT10,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGBRG10, MEDIA_BUS_FMT_SGBRG10_1X10, 16,
	  ISC_PFG_CFG0_BPS_TEN, ISC_BAY_CFG_GBGB, ISC_RLP_CFG_MODE_DAT10,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGRBG10, MEDIA_BUS_FMT_SGRBG10_1X10, 16,
	  ISC_PFG_CFG0_BPS_TEN, ISC_BAY_CFG_GRGR, ISC_RLP_CFG_MODE_DAT10,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SRGGB10, MEDIA_BUS_FMT_SRGGB10_1X10, 16,
	  ISC_PFG_CFG0_BPS_TEN, ISC_BAY_CFG_RGRG, ISC_RLP_CFG_MODE_DAT10,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },

	{ V4L2_PIX_FMT_SBGGR12, MEDIA_BUS_FMT_SBGGR12_1X12, 16,
	  ISC_PFG_CFG0_BPS_TWELVE, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_DAT12,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGBRG12, MEDIA_BUS_FMT_SGBRG12_1X12, 16,
	  ISC_PFG_CFG0_BPS_TWELVE, ISC_BAY_CFG_GBGB, ISC_RLP_CFG_MODE_DAT12,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SGRBG12, MEDIA_BUS_FMT_SGRBG12_1X12, 16,
	  ISC_PFG_CFG0_BPS_TWELVE, ISC_BAY_CFG_GRGR, ISC_RLP_CFG_MODE_DAT12,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
	{ V4L2_PIX_FMT_SRGGB12, MEDIA_BUS_FMT_SRGGB12_1X12, 16,
	  ISC_PFG_CFG0_BPS_TWELVE, ISC_BAY_CFG_RGRG, ISC_RLP_CFG_MODE_DAT12,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },

	{ V4L2_PIX_FMT_YUV420, 0x0, 12,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_YYCC,
	  ISC_DCFG_IMODE_YC420P, ISC_DCTRL_DVIEW_PLANAR, 0x7fb,
	  false, false },
	{ V4L2_PIX_FMT_YUV422P, 0x0, 16,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_YYCC,
	  ISC_DCFG_IMODE_YC422P, ISC_DCTRL_DVIEW_PLANAR, 0x3fb,
	  false, false },
	{ V4L2_PIX_FMT_RGB565, MEDIA_BUS_FMT_RGB565_2X8_LE, 16,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_RGB565,
	  ISC_DCFG_IMODE_PACKED16, ISC_DCTRL_DVIEW_PACKED, 0x7b,
	  false, false },

	{ V4L2_PIX_FMT_YUYV, MEDIA_BUS_FMT_YUYV8_2X8, 16,
	  ISC_PFE_CFG0_BPS_EIGHT, ISC_BAY_CFG_BGBG, ISC_RLP_CFG_MODE_DAT8,
	  ISC_DCFG_IMODE_PACKED8, ISC_DCTRL_DVIEW_PACKED, 0x0,
	  false, false },
};

#define GAMMA_MAX	2
#define GAMMA_ENTRIES	64

/* Gamma table with gamma 1/2.2 */
static const u32 isc_gamma_table[GAMMA_MAX + 1][GAMMA_ENTRIES] = {
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

static unsigned int sensor_preferred = 1;
module_param(sensor_preferred, uint, 0644);
MODULE_PARM_DESC(sensor_preferred,
		 "Sensor is preferred to output the specified format (1-on 0-off), default 1");

static int isc_clk_enable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;
	struct regmap *regmap = isc_clk->regmap;

	dev_dbg(isc_clk->dev, "ISC CLK: %s, div = %d, parent id = %d\n",
		__func__, isc_clk->div, isc_clk->parent_id);

	regmap_update_bits(regmap, ISC_CLKCFG,
			   ISC_CLKCFG_DIV_MASK(id) | ISC_CLKCFG_SEL_MASK(id),
			   (isc_clk->div << ISC_CLKCFG_DIV_SHIFT(id)) |
			   (isc_clk->parent_id << ISC_CLKCFG_SEL_SHIFT(id)));

	regmap_write(regmap, ISC_CLKEN, ISC_CLK(id));

	return 0;
}

static void isc_clk_disable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;

	regmap_write(isc_clk->regmap, ISC_CLKDIS, ISC_CLK(id));
}

static int isc_clk_is_enabled(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 status;

	regmap_read(isc_clk->regmap, ISC_CLKSR, &status);

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

	isc_clk->clk = clk_register(isc->dev, &isc_clk->hw);
	if (IS_ERR(isc_clk->clk)) {
		dev_err(isc->dev, "%s: clock register fail\n", clk_name);
		return PTR_ERR(isc_clk->clk);
	} else if (id == ISC_MCK)
		of_clk_add_provider(np, of_clk_src_simple_get, isc_clk->clk);

	return 0;
}

static int isc_clk_init(struct isc_device *isc)
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

static void isc_clk_cleanup(struct isc_device *isc)
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

static inline bool sensor_is_preferred(const struct isc_format *isc_fmt)
{
	return (sensor_preferred && isc_fmt->sd_support) ||
		!isc_fmt->isc_support;
}

static void isc_start_dma(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	struct v4l2_pix_format *pixfmt = &isc->fmt.fmt.pix;
	u32 sizeimage = pixfmt->sizeimage;
	u32 dctrl_dview;
	dma_addr_t addr0;

	addr0 = vb2_dma_contig_plane_dma_addr(&isc->cur_frm->vb.vb2_buf, 0);
	regmap_write(regmap, ISC_DAD0, addr0);

	switch (pixfmt->pixelformat) {
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

	if (sensor_is_preferred(isc->current_fmt))
		dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
	else
		dctrl_dview = isc->current_fmt->reg_dctrl_dview;

	regmap_write(regmap, ISC_DCTRL, dctrl_dview | ISC_DCTRL_IE_IS);
	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_CAPTURE);
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

	bay_cfg = isc->raw_fmt->reg_bay_cfg;

	regmap_write(regmap, ISC_WB_CFG, bay_cfg);
	regmap_write(regmap, ISC_WB_O_RGR, 0x0);
	regmap_write(regmap, ISC_WB_O_BGR, 0x0);
	regmap_write(regmap, ISC_WB_G_RGR, ctrls->r_gain | (0x1 << 25));
	regmap_write(regmap, ISC_WB_G_BGR, ctrls->b_gain | (0x1 << 25));

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
		v4l2_warn(&isc->v4l2_dev, "Time out to update profie\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void isc_set_histogram(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (ctrls->awb && (ctrls->hist_stat != HIST_ENABLED)) {
		regmap_write(regmap, ISC_HIS_CFG, ISC_HIS_CFG_MODE_R |
		      (isc->raw_fmt->reg_bay_cfg << ISC_HIS_CFG_BAYSEL_SHIFT) |
		      ISC_HIS_CFG_RAR);
		regmap_write(regmap, ISC_HIS_CTRL, ISC_HIS_CTRL_EN);
		regmap_write(regmap, ISC_INTEN, ISC_INT_HISDONE);
		ctrls->hist_id = ISC_HIS_CFG_MODE_R;
		isc_update_profile(isc);
		regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

		ctrls->hist_stat = HIST_ENABLED;
	} else if (!ctrls->awb && (ctrls->hist_stat != HIST_DISABLED)) {
		regmap_write(regmap, ISC_INTDIS, ISC_INT_HISDONE);
		regmap_write(regmap, ISC_HIS_CTRL, ISC_HIS_CTRL_DIS);

		ctrls->hist_stat = HIST_DISABLED;
	}
}

static inline void isc_get_param(const struct isc_format *fmt,
				  u32 *rlp_mode, u32 *dcfg)
{
	*dcfg = ISC_DCFG_YMBSIZE_BEATS8;

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		*rlp_mode = fmt->reg_rlp_mode;
		*dcfg |= fmt->reg_dcfg_imode;
		break;
	default:
		*rlp_mode = ISC_RLP_CFG_MODE_DAT8;
		*dcfg |= ISC_DCFG_IMODE_PACKED8;
		break;
	}
}

static int isc_configure(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	const struct isc_format *current_fmt = isc->current_fmt;
	struct isc_subdev_entity *subdev = isc->current_subdev;
	u32 pfe_cfg0, rlp_mode, dcfg, mask, pipeline;

	if (sensor_is_preferred(current_fmt)) {
		pfe_cfg0 = current_fmt->reg_bps;
		pipeline = 0x0;
		isc_get_param(current_fmt, &rlp_mode, &dcfg);
		isc->ctrls.hist_stat = HIST_INIT;
	} else {
		pfe_cfg0  = isc->raw_fmt->reg_bps;
		pipeline = current_fmt->pipeline;
		rlp_mode = current_fmt->reg_rlp_mode;
		dcfg = current_fmt->reg_dcfg_imode | ISC_DCFG_YMBSIZE_BEATS8 |
		       ISC_DCFG_CMBSIZE_BEATS8;
	}

	pfe_cfg0  |= subdev->pfe_cfg0 | ISC_PFE_CFG0_MODE_PROGRESSIVE;
	mask = ISC_PFE_CFG0_BPS_MASK | ISC_PFE_CFG0_HPOL_LOW |
	       ISC_PFE_CFG0_VPOL_LOW | ISC_PFE_CFG0_PPOL_LOW |
	       ISC_PFE_CFG0_MODE_MASK;

	regmap_update_bits(regmap, ISC_PFE_CFG0, mask, pfe_cfg0);

	regmap_update_bits(regmap, ISC_RLP_CFG, ISC_RLP_CFG_MODE_MASK,
			   rlp_mode);

	regmap_write(regmap, ISC_DCFG, dcfg);

	/* Set the pipeline */
	isc_set_pipeline(isc, pipeline);

	if (pipeline)
		isc_set_histogram(isc);

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
		v4l2_err(&isc->v4l2_dev, "stream on failed in subdev\n");
		goto err_start_stream;
	}

	pm_runtime_get_sync(isc->dev);

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

	return 0;

err_configure:
	pm_runtime_put_sync(isc->dev);

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

static struct vb2_ops isc_vb2_ops = {
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

	strcpy(cap->driver, ATMEL_ISC_NAME);
	strcpy(cap->card, "Atmel Image Sensor Controller");
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", isc->v4l2_dev.name);

	return 0;
}

static int isc_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct isc_device *isc = video_drvdata(file);
	u32 index = f->index;

	if (index >= isc->num_user_formats)
		return -EINVAL;

	f->pixelformat = isc->user_formats[index]->fourcc;

	return 0;
}

static int isc_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct isc_device *isc = video_drvdata(file);

	*fmt = isc->fmt;

	return 0;
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

static int isc_try_fmt(struct isc_device *isc, struct v4l2_format *f,
			struct isc_format **current_fmt, u32 *code)
{
	struct isc_format *isc_fmt;
	struct v4l2_pix_format *pixfmt = &f->fmt.pix;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	u32 mbus_code;
	int ret;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	isc_fmt = find_format_by_fourcc(isc, pixfmt->pixelformat);
	if (!isc_fmt) {
		v4l2_warn(&isc->v4l2_dev, "Format 0x%x not found\n",
			  pixfmt->pixelformat);
		isc_fmt = isc->user_formats[isc->num_user_formats - 1];
		pixfmt->pixelformat = isc_fmt->fourcc;
	}

	/* Limit to Atmel ISC hardware capabilities */
	if (pixfmt->width > ISC_MAX_SUPPORT_WIDTH)
		pixfmt->width = ISC_MAX_SUPPORT_WIDTH;
	if (pixfmt->height > ISC_MAX_SUPPORT_HEIGHT)
		pixfmt->height = ISC_MAX_SUPPORT_HEIGHT;

	if (sensor_is_preferred(isc_fmt))
		mbus_code = isc_fmt->mbus_code;
	else
		mbus_code = isc->raw_fmt->mbus_code;

	v4l2_fill_mbus_format(&format.format, pixfmt, mbus_code);
	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, set_fmt,
			       isc->current_subdev->config, &format);
	if (ret < 0)
		return ret;

	v4l2_fill_pix_format(pixfmt, &format.format);

	pixfmt->field = V4L2_FIELD_NONE;
	pixfmt->bytesperline = (pixfmt->width * isc_fmt->bpp) >> 3;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;

	if (current_fmt)
		*current_fmt = isc_fmt;

	if (code)
		*code = mbus_code;

	return 0;
}

static int isc_set_fmt(struct isc_device *isc, struct v4l2_format *f)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct isc_format *current_fmt;
	u32 mbus_code;
	int ret;

	ret = isc_try_fmt(isc, f, &current_fmt, &mbus_code);
	if (ret)
		return ret;

	v4l2_fill_mbus_format(&format.format, &f->fmt.pix, mbus_code);
	ret = v4l2_subdev_call(isc->current_subdev->sd, pad,
			       set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	isc->fmt = *f;
	isc->current_fmt = current_fmt;

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

	return isc_try_fmt(isc, f, NULL, NULL);
}

static int isc_enum_input(struct file *file, void *priv,
			   struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = 0;
	strcpy(inp->name, "Camera");

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

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return v4l2_subdev_call(isc->current_subdev->sd, video, g_parm, a);
}

static int isc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct isc_device *isc = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return v4l2_subdev_call(isc->current_subdev->sd, video, s_parm, a);
}

static int isc_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct isc_device *isc = video_drvdata(file);
	const struct isc_format *isc_fmt;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	isc_fmt = find_format_by_fourcc(isc, fsize->pixel_format);
	if (!isc_fmt)
		return -EINVAL;

	if (sensor_is_preferred(isc_fmt))
		fse.code = isc_fmt->mbus_code;
	else
		fse.code = isc->raw_fmt->mbus_code;

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
	const struct isc_format *isc_fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	isc_fmt = find_format_by_fourcc(isc, fival->pixel_format);
	if (!isc_fmt)
		return -EINVAL;

	if (sensor_is_preferred(isc_fmt))
		fie.code = isc_fmt->mbus_code;
	else
		fie.code = isc->raw_fmt->mbus_code;

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

static irqreturn_t isc_interrupt(int irq, void *dev_id)
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

static void isc_hist_count(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 *hist_count = &ctrls->hist_count[ctrls->hist_id];
	u32 *hist_entry = &ctrls->hist_entry[0];
	u32 i;

	regmap_bulk_read(regmap, ISC_HIS_ENTRY, hist_entry, HIST_ENTRIES);

	*hist_count = 0;
	for (i = 0; i < HIST_ENTRIES; i++)
		*hist_count += i * (*hist_entry++);
}

static void isc_wb_update(struct isc_ctrls *ctrls)
{
	u32 *hist_count = &ctrls->hist_count[0];
	u64 g_count = (u64)hist_count[ISC_HIS_CFG_MODE_GB] << 9;
	u32 hist_r = hist_count[ISC_HIS_CFG_MODE_R];
	u32 hist_b = hist_count[ISC_HIS_CFG_MODE_B];

	if (hist_r)
		ctrls->r_gain = div_u64(g_count, hist_r);

	if (hist_b)
		ctrls->b_gain = div_u64(g_count, hist_b);
}

static void isc_awb_work(struct work_struct *w)
{
	struct isc_device *isc =
		container_of(w, struct isc_device, awb_work);
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 hist_id = ctrls->hist_id;
	u32 baysel;

	if (ctrls->hist_stat != HIST_ENABLED)
		return;

	isc_hist_count(isc);

	if (hist_id != ISC_HIS_CFG_MODE_B) {
		hist_id++;
	} else {
		isc_wb_update(ctrls);
		hist_id = ISC_HIS_CFG_MODE_R;
	}

	ctrls->hist_id = hist_id;
	baysel = isc->raw_fmt->reg_bay_cfg << ISC_HIS_CFG_BAYSEL_SHIFT;

	pm_runtime_get_sync(isc->dev);

	regmap_write(regmap, ISC_HIS_CFG, hist_id | baysel | ISC_HIS_CFG_RAR);
	isc_update_profile(isc);
	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

	pm_runtime_put_sync(isc->dev);
}

static int isc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

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
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrls->awb = ctrl->val;
		if (ctrls->hist_stat != HIST_ENABLED) {
			ctrls->r_gain = 0x1 << 9;
			ctrls->b_gain = 0x1 << 9;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops isc_ctrl_ops = {
	.s_ctrl	= isc_s_ctrl,
};

static int isc_ctrl_init(struct isc_device *isc)
{
	const struct v4l2_ctrl_ops *ops = &isc_ctrl_ops;
	struct isc_ctrls *ctrls = &isc->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	ctrls->hist_stat = HIST_INIT;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret < 0)
		return ret;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS, -1024, 1023, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -2048, 2047, 1, 256);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAMMA, 0, GAMMA_MAX, 1, 2);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);

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
	if (isc->current_subdev->config)
		v4l2_subdev_free_pad_config(isc->current_subdev->config);
	v4l2_ctrl_handler_free(&isc->ctrls.handler);
}

static struct isc_format *find_format_by_code(unsigned int code, int *index)
{
	struct isc_format *fmt = &isc_formats[0];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isc_formats); i++) {
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
	struct v4l2_subdev_mbus_code_enum mbus_code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	fmt = &isc_formats[0];
	for (i = 0; i < ARRAY_SIZE(isc_formats); i++) {
		fmt->isc_support = false;
		fmt->sd_support = false;

		fmt++;
	}

	while (!v4l2_subdev_call(subdev, pad, enum_mbus_code,
	       NULL, &mbus_code)) {
		mbus_code.index++;
		fmt = find_format_by_code(mbus_code.code, &i);
		if (!fmt)
			continue;

		fmt->sd_support = true;

		if (i <= RAW_FMT_IND_END) {
			for (j = ISC_FMT_IND_START; j <= ISC_FMT_IND_END; j++)
				isc_formats[j].isc_support = true;

			isc->raw_fmt = fmt;
		}
	}

	fmt = &isc_formats[0];
	for (i = 0, num_fmts = 0; i < ARRAY_SIZE(isc_formats); i++) {
		if (fmt->isc_support || fmt->sd_support)
			num_fmts++;

		fmt++;
	}

	if (!num_fmts)
		return -ENXIO;

	isc->num_user_formats = num_fmts;
	isc->user_formats = devm_kcalloc(isc->dev,
					 num_fmts, sizeof(struct isc_format *),
					 GFP_KERNEL);
	if (!isc->user_formats) {
		v4l2_err(&isc->v4l2_dev, "could not allocate memory\n");
		return -ENOMEM;
	}

	fmt = &isc_formats[0];
	for (i = 0, j = 0; i < ARRAY_SIZE(isc_formats); i++) {
		if (fmt->isc_support || fmt->sd_support)
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

	ret = isc_try_fmt(isc, &f, NULL, NULL);
	if (ret)
		return ret;

	isc->current_fmt = isc->user_formats[0];
	isc->fmt = f;

	return 0;
}

static int isc_async_complete(struct v4l2_async_notifier *notifier)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	struct isc_subdev_entity *sd_entity;
	struct video_device *vdev = &isc->video_dev;
	struct vb2_queue *q = &isc->vb2_vidq;
	int ret;

	ret = v4l2_device_register_subdev_nodes(&isc->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev, "Failed to register subdev nodes\n");
		return ret;
	}

	isc->current_subdev = container_of(notifier,
					   struct isc_subdev_entity, notifier);
	sd_entity = isc->current_subdev;

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
		return ret;
	}

	/* Init video dma queues */
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_lock_init(&isc->dma_queue_lock);

	sd_entity->config = v4l2_subdev_alloc_pad_config(sd_entity->sd);
	if (sd_entity->config == NULL)
		return -ENOMEM;

	ret = isc_formats_init(isc);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "Init format failed: %d\n", ret);
		return ret;
	}

	ret = isc_set_default_fmt(isc);
	if (ret) {
		v4l2_err(&isc->v4l2_dev, "Could not set default format\n");
		return ret;
	}

	ret = isc_ctrl_init(isc);
	if (ret) {
		v4l2_err(&isc->v4l2_dev, "Init isc ctrols failed: %d\n", ret);
		return ret;
	}

	INIT_WORK(&isc->awb_work, isc_awb_work);

	/* Register video device */
	strlcpy(vdev->name, ATMEL_ISC_NAME, sizeof(vdev->name));
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

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		v4l2_err(&isc->v4l2_dev,
			 "video_register_device failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void isc_subdev_cleanup(struct isc_device *isc)
{
	struct isc_subdev_entity *subdev_entity;

	list_for_each_entry(subdev_entity, &isc->subdev_entities, list)
		v4l2_async_notifier_unregister(&subdev_entity->notifier);

	INIT_LIST_HEAD(&isc->subdev_entities);
}

static int isc_pipeline_init(struct isc_device *isc)
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

static int isc_parse_dt(struct device *dev, struct isc_device *isc)
{
	struct device_node *np = dev->of_node;
	struct device_node *epn = NULL, *rem;
	struct v4l2_of_endpoint v4l2_epn;
	struct isc_subdev_entity *subdev_entity;
	unsigned int flags;
	int ret;

	INIT_LIST_HEAD(&isc->subdev_entities);

	for (; ;) {
		epn = of_graph_get_next_endpoint(np, epn);
		if (!epn)
			break;

		rem = of_graph_get_remote_port_parent(epn);
		if (!rem) {
			dev_notice(dev, "Remote device at %s not found\n",
				   of_node_full_name(epn));
			continue;
		}

		ret = v4l2_of_parse_endpoint(epn, &v4l2_epn);
		if (ret) {
			of_node_put(rem);
			ret = -EINVAL;
			dev_err(dev, "Could not parse the endpoint\n");
			break;
		}

		subdev_entity = devm_kzalloc(dev,
					  sizeof(*subdev_entity), GFP_KERNEL);
		if (subdev_entity == NULL) {
			of_node_put(rem);
			ret = -ENOMEM;
			break;
		}

		subdev_entity->asd = devm_kzalloc(dev,
				     sizeof(*subdev_entity->asd), GFP_KERNEL);
		if (subdev_entity->asd == NULL) {
			of_node_put(rem);
			ret = -ENOMEM;
			break;
		}

		flags = v4l2_epn.bus.parallel.flags;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 = ISC_PFE_CFG0_HPOL_LOW;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_VPOL_LOW;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_PPOL_LOW;

		subdev_entity->asd->match_type = V4L2_ASYNC_MATCH_OF;
		subdev_entity->asd->match.of.node = rem;
		list_add_tail(&subdev_entity->list, &isc->subdev_entities);
	}

	of_node_put(epn);
	return ret;
}

/* regmap configuration */
#define ATMEL_ISC_REG_MAX    0xbfc
static const struct regmap_config isc_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= ATMEL_ISC_REG_MAX,
};

static int atmel_isc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct isc_device *isc;
	struct resource *res;
	void __iomem *io_base;
	struct isc_subdev_entity *subdev_entity;
	int irq;
	int ret;

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
	if (irq < 0) {
		ret = irq;
		dev_err(dev, "failed to get irq: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, irq, isc_interrupt, 0,
			       ATMEL_ISC_NAME, isc);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			irq, ret);
		return ret;
	}

	ret = isc_pipeline_init(isc);
	if (ret)
		return ret;

	isc->hclock = devm_clk_get(dev, "hclock");
	if (IS_ERR(isc->hclock)) {
		ret = PTR_ERR(isc->hclock);
		dev_err(dev, "failed to get hclock: %d\n", ret);
		return ret;
	}

	ret = isc_clk_init(isc);
	if (ret) {
		dev_err(dev, "failed to init isc clock: %d\n", ret);
		goto clean_isc_clk;
	}

	isc->ispck = isc->isc_clks[ISC_ISPCK].clk;

	/* ispck should be greater or equal to hclock */
	ret = clk_set_rate(isc->ispck, clk_get_rate(isc->hclock));
	if (ret) {
		dev_err(dev, "failed to set ispck rate: %d\n", ret);
		goto clean_isc_clk;
	}

	ret = v4l2_device_register(dev, &isc->v4l2_dev);
	if (ret) {
		dev_err(dev, "unable to register v4l2 device.\n");
		goto clean_isc_clk;
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
		subdev_entity->notifier.subdevs = &subdev_entity->asd;
		subdev_entity->notifier.num_subdevs = 1;
		subdev_entity->notifier.bound = isc_async_bound;
		subdev_entity->notifier.unbind = isc_async_unbind;
		subdev_entity->notifier.complete = isc_async_complete;

		ret = v4l2_async_notifier_register(&isc->v4l2_dev,
						   &subdev_entity->notifier);
		if (ret) {
			dev_err(dev, "fail to register async notifier\n");
			goto cleanup_subdev;
		}

		if (video_is_registered(&isc->video_dev))
			break;
	}

	pm_runtime_enable(dev);

	return 0;

cleanup_subdev:
	isc_subdev_cleanup(isc);

unregister_v4l2_device:
	v4l2_device_unregister(&isc->v4l2_dev);

clean_isc_clk:
	isc_clk_cleanup(isc);

	return ret;
}

static int atmel_isc_remove(struct platform_device *pdev)
{
	struct isc_device *isc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	isc_subdev_cleanup(isc);

	v4l2_device_unregister(&isc->v4l2_dev);

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

	return clk_prepare_enable(isc->ispck);
}

static const struct dev_pm_ops atmel_isc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(isc_runtime_suspend, isc_runtime_resume, NULL)
};

static const struct of_device_id atmel_isc_of_match[] = {
	{ .compatible = "atmel,sama5d2-isc" },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_isc_of_match);

static struct platform_driver atmel_isc_driver = {
	.probe	= atmel_isc_probe,
	.remove	= atmel_isc_remove,
	.driver	= {
		.name		= ATMEL_ISC_NAME,
		.pm		= &atmel_isc_dev_pm_ops,
		.of_match_table = of_match_ptr(atmel_isc_of_match),
	},
};

module_platform_driver(atmel_isc_driver);

MODULE_AUTHOR("Songjun Wu <songjun.wu@microchip.com>");
MODULE_DESCRIPTION("The V4L2 driver for Atmel-ISC");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("video");
