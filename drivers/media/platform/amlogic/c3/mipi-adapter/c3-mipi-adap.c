// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

/*
 * Adapter Block Diagram
 * ---------------------
 *
 *                   +--------------------------------------------+
 *                   |                   Adapter                  |
 *                   |--------------------------------------------|
 * +------------+    |          |          |         |            |    +-----+
 * | MIPI CSI-2 |--->| Frontend -> DDR_RD0 -> PIXEL0 -> ALIGNMENT |--->| ISP |
 * +------------+    |          |          |         |            |    +-----+
 *                   +--------------------------------------------+
 *
 */

/* C3 adapter submodule definition */
enum {
	SUBMD_TOP,
	SUBMD_FD,
	SUBMD_RD,
};

#define ADAP_SUBMD_MASK		GENMASK(17, 16)
#define ADAP_SUBMD_SHIFT	16
#define ADAP_SUBMD(x)	(((x) & (ADAP_SUBMD_MASK)) >> (ADAP_SUBMD_SHIFT))
#define ADAP_REG_ADDR_MASK	GENMASK(15, 0)
#define ADAP_REG_ADDR(x)	((x) & (ADAP_REG_ADDR_MASK))
#define ADAP_REG_T(x)		((SUBMD_TOP << ADAP_SUBMD_SHIFT) | (x))
#define ADAP_REG_F(x)		((SUBMD_FD << ADAP_SUBMD_SHIFT) | (x))
#define ADAP_REG_R(x)		((SUBMD_RD << ADAP_SUBMD_SHIFT) | (x))

#define MIPI_ADAP_CLOCK_NUM_MAX				3
#define MIPI_ADAP_SUBDEV_NAME				"c3-mipi-adapter"

/* C3 MIPI adapter TOP register */
#define MIPI_TOP_CTRL0					ADAP_REG_T(0x00)
#define MIPI_TOP_CTRL0_RST_ADAPTER_MASK			BIT(1)
#define MIPI_TOP_CTRL0_RST_ADAPTER_APPLY		BIT(1)
#define MIPI_TOP_CTRL0_RST_ADAPTER_EXIT			(0 << 1)

#define MIPI_ADAPT_DE_CTRL0				ADAP_REG_T(0x40)
#define MIPI_ADAPT_DE_CTRL0_RD_BUS_BYPASS_MASK		BIT(3)
#define MIPI_ADAPT_DE_CTRL0_RD_BUS_BYPASS_EN		BIT(3)
#define MIPI_ADAPT_DE_CTRL0_RD_BUS_BYPASS_DIS		(0 << 3)
#define MIPI_ADAPT_DE_CTRL0_WR_BUS_BYPASS_MASK		BIT(7)
#define MIPI_ADAPT_DE_CTRL0_WR_BUS_BYPASS_EN		BIT(7)
#define MIPI_ADAPT_DE_CTRL0_WR_BUS_BYPASS_DIS		(0 << 7)

/* C3 MIPI adapter FRONTEND register */
#define CSI2_CLK_RESET					ADAP_REG_F(0x00)
#define CSI2_CLK_RESET_SW_RESET_MASK			BIT(0)
#define CSI2_CLK_RESET_SW_RESET_APPLY			BIT(0)
#define CSI2_CLK_RESET_SW_RESET_RELEASE			(0 << 0)
#define CSI2_CLK_RESET_CLK_ENABLE_MASK			BIT(1)
#define CSI2_CLK_RESET_CLK_ENABLE_EN			BIT(1)
#define CSI2_CLK_RESET_CLK_ENABLE_DIS			(0 << 1)

#define CSI2_GEN_CTRL0					ADAP_REG_F(0x04)
#define CSI2_GEN_CTRL0_VC0_MASK				BIT(0)
#define CSI2_GEN_CTRL0_VC0_EN				BIT(0)
#define CSI2_GEN_CTRL0_VC0_DIS				(0 << 0)
#define CSI2_GEN_CTRL0_ENABLE_PACKETS_MASK		GENMASK(20, 16)
#define CSI2_GEN_CTRL0_ENABLE_PACKETS_RAW		BIT(16)
#define CSI2_GEN_CTRL0_ENABLE_PACKETS_YUV		(2 << 16)

#define CSI2_X_START_END_ISP				ADAP_REG_F(0x0c)
#define CSI2_X_START_END_ISP_X_START_MASK		GENMASK(15, 0)
#define CSI2_X_START_END_ISP_X_START(x)			((x) << 0)
#define CSI2_X_START_END_ISP_X_END_MASK			GENMASK(31, 16)
#define CSI2_X_START_END_ISP_X_END(x)			(((x) - 1) << 16)

#define CSI2_Y_START_END_ISP				ADAP_REG_F(0x10)
#define CSI2_Y_START_END_ISP_Y_START_MASK		GENMASK(15, 0)
#define CSI2_Y_START_END_ISP_Y_START(x)			((x) << 0)
#define CSI2_Y_START_END_ISP_Y_END_MASK			GENMASK(31, 16)
#define CSI2_Y_START_END_ISP_Y_END(x)			(((x) - 1) << 16)

#define CSI2_VC_MODE					ADAP_REG_F(0x1c)
#define CSI2_VC_MODE_VS_ISP_SEL_VC_MASK			GENMASK(19, 16)
#define CSI2_VC_MODE_VS_ISP_SEL_VC_0			BIT(16)
#define CSI2_VC_MODE_VS_ISP_SEL_VC_1			(2 << 16)
#define CSI2_VC_MODE_VS_ISP_SEL_VC_2			(4 << 16)
#define CSI2_VC_MODE_VS_ISP_SEL_VC_3			(8 << 16)
#define CSI2_VC_MODE_HS_ISP_SEL_VC_MASK			GENMASK(23, 20)
#define CSI2_VC_MODE_HS_ISP_SEL_VC_0			BIT(20)
#define CSI2_VC_MODE_HS_ISP_SEL_VC_1			(2 << 20)
#define CSI2_VC_MODE_HS_ISP_SEL_VC_2			(4 << 20)
#define CSI2_VC_MODE_HS_ISP_SEL_VC_3			(8 << 20)

/* C3 MIPI adapter READER register */
#define MIPI_ADAPT_DDR_RD0_CNTL0			ADAP_REG_R(0x00)
#define MIPI_ADAPT_DDR_RD0_CNTL0_MODULE_EN_MASK		BIT(0)
#define MIPI_ADAPT_DDR_RD0_CNTL0_MODULE_EN		BIT(0)
#define MIPI_ADAPT_DDR_RD0_CNTL0_MODULE_DIS		(0 << 0)

#define MIPI_ADAPT_DDR_RD0_CNTL1			ADAP_REG_R(0x04)
#define MIPI_ADAPT_DDR_RD0_CNTL1_PORT_SEL_MASK		GENMASK(31, 30)
#define MIPI_ADAPT_DDR_RD0_CNTL1_PORT_SEL_DIRECT_MODE	(0 << 30)
#define MIPI_ADAPT_DDR_RD0_CNTL1_PORT_SEL_DDR_MODE	BIT(30)

#define MIPI_ADAPT_PIXEL0_CNTL0				ADAP_REG_R(0x80)
#define MIPI_ADAPT_PIXEL0_CNTL0_WORK_MODE_MASK		GENMASK(17, 16)
#define MIPI_ADAPT_PIXEL0_CNTL0_WORK_MODE_RAW_DDR	(0 << 16)
#define MIPI_ADAPT_PIXEL0_CNTL0_WORK_MODE_RAW_DIRECT	BIT(16)
#define MIPI_ADAPT_PIXEL0_CNTL0_DATA_TYPE_MASK		GENMASK(25, 20)
#define MIPI_ADAPT_PIXEL0_CNTL0_DATA_TYPE(x)		((x) << 20)
#define MIPI_ADAPT_PIXEL0_CNTL0_START_EN_MASK		BIT(31)
#define MIPI_ADAPT_PIXEL0_CNTL0_START_EN		BIT(31)

#define MIPI_ADAPT_ALIG_CNTL0				ADAP_REG_R(0x100)
#define MIPI_ADAPT_ALIG_CNTL0_H_NUM_MASK		GENMASK(15, 0)
#define MIPI_ADAPT_ALIG_CNTL0_H_NUM(x)			((x) << 0)
#define MIPI_ADAPT_ALIG_CNTL0_V_NUM_MASK		GENMASK(31, 16)
#define MIPI_ADAPT_ALIG_CNTL0_V_NUM(x)			((x) << 16)

#define MIPI_ADAPT_ALIG_CNTL1				ADAP_REG_R(0x104)
#define MIPI_ADAPT_ALIG_CNTL1_HPE_NUM_MASK		GENMASK(31, 16)
#define MIPI_ADAPT_ALIG_CNTL1_HPE_NUM(x)		((x) << 16)

#define MIPI_ADAPT_ALIG_CNTL2				ADAP_REG_R(0x108)
#define MIPI_ADAPT_ALIG_CNTL2_VPE_NUM_MASK		GENMASK(31, 16)
#define MIPI_ADAPT_ALIG_CNTL2_VPE_NUM(x)		((x) << 16)

#define MIPI_ADAPT_ALIG_CNTL6				ADAP_REG_R(0x118)
#define MIPI_ADAPT_ALIG_CNTL6_PATH0_EN_MASK		BIT(0)
#define MIPI_ADAPT_ALIG_CNTL6_PATH0_EN			BIT(0)
#define MIPI_ADAPT_ALIG_CNTL6_PATH0_DIS			(0 << 0)
#define MIPI_ADAPT_ALIG_CNTL6_PIX0_DATA_MODE_MASK	BIT(4)
#define MIPI_ADAPT_ALIG_CNTL6_PIX0_DATA_MODE_DDR	(0 << 4)
#define MIPI_ADAPT_ALIG_CNTL6_PIX0_DATA_MODE_DIRECT	BIT(4)
#define MIPI_ADAPT_ALIG_CNTL6_DATA0_EN_MASK		BIT(12)
#define MIPI_ADAPT_ALIG_CNTL6_DATA0_EN			BIT(12)
#define MIPI_ADAPT_ALIG_CNTL6_DATA0_DIS			(0 << 12)

#define MIPI_ADAPT_ALIG_CNTL8				ADAP_REG_R(0x120)
#define MIPI_ADAPT_ALIG_CNTL8_FRMAE_CONTINUE_MASK	BIT(5)
#define MIPI_ADAPT_ALIG_CNTL8_FRMAE_CONTINUE_EN		BIT(5)
#define MIPI_ADAPT_ALIG_CNTL8_FRMAE_CONTINUE_DIS	(0 << 5)
#define MIPI_ADAPT_ALIG_CNTL8_EXCEED_DIS_MASK		BIT(12)
#define MIPI_ADAPT_ALIG_CNTL8_EXCEED_HOLD		(0 << 12)
#define MIPI_ADAPT_ALIG_CNTL8_EXCEED_NOT_HOLD		BIT(12)
#define MIPI_ADAPT_ALIG_CNTL8_START_EN_MASK		BIT(31)
#define MIPI_ADAPT_ALIG_CNTL8_START_EN			BIT(31)

#define MIPI_ADAP_MAX_WIDTH		2888
#define MIPI_ADAP_MIN_WIDTH		160
#define MIPI_ADAP_MAX_HEIGHT		2240
#define MIPI_ADAP_MIN_HEIGHT		120
#define MIPI_ADAP_DEFAULT_WIDTH		1920
#define MIPI_ADAP_DEFAULT_HEIGHT	1080
#define MIPI_ADAP_DEFAULT_FMT		MEDIA_BUS_FMT_SRGGB10_1X10

/* C3 MIPI adapter pad list */
enum {
	C3_MIPI_ADAP_PAD_SINK,
	C3_MIPI_ADAP_PAD_SRC,
	C3_MIPI_ADAP_PAD_MAX
};

/*
 * struct c3_adap_info - mipi adapter information
 *
 * @clocks: array of mipi adapter clock names
 * @clock_num: actual clock number
 */
struct c3_adap_info {
	char *clocks[MIPI_ADAP_CLOCK_NUM_MAX];
	u32 clock_num;
};

/*
 * struct c3_adap_device - mipi adapter platform device
 *
 * @dev: pointer to the struct device
 * @top: mipi adapter top register address
 * @fd: mipi adapter frontend register address
 * @rd: mipi adapter reader register address
 * @clks: array of MIPI adapter clocks
 * @sd: mipi adapter sub-device
 * @pads: mipi adapter sub-device pads
 * @notifier: notifier to register on the v4l2-async API
 * @src_sd: source sub-device pad
 * @info: version-specific MIPI adapter information
 */
struct c3_adap_device {
	struct device *dev;
	void __iomem *top;
	void __iomem *fd;
	void __iomem *rd;
	struct clk_bulk_data clks[MIPI_ADAP_CLOCK_NUM_MAX];

	struct v4l2_subdev sd;
	struct media_pad pads[C3_MIPI_ADAP_PAD_MAX];
	struct v4l2_async_notifier notifier;
	struct media_pad *src_pad;

	const struct c3_adap_info *info;
};

/* Format helpers */

struct c3_adap_pix_format {
	u32 code;
	u8 type;
};

static const struct c3_adap_pix_format c3_mipi_adap_formats[] = {
	{ MEDIA_BUS_FMT_SBGGR10_1X10, MIPI_CSI2_DT_RAW10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, MIPI_CSI2_DT_RAW10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, MIPI_CSI2_DT_RAW10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, MIPI_CSI2_DT_RAW10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, MIPI_CSI2_DT_RAW12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, MIPI_CSI2_DT_RAW12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, MIPI_CSI2_DT_RAW12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, MIPI_CSI2_DT_RAW12 },
};

static const struct c3_adap_pix_format *c3_mipi_adap_find_format(u32 code)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(c3_mipi_adap_formats); i++)
		if (code == c3_mipi_adap_formats[i].code)
			return &c3_mipi_adap_formats[i];

	return NULL;
}

/* Hardware configuration */

static void c3_mipi_adap_update_bits(struct c3_adap_device *adap, u32 reg,
				     u32 mask, u32 val)
{
	void __iomem *addr;
	u32 orig, tmp;

	switch (ADAP_SUBMD(reg)) {
	case SUBMD_TOP:
		addr = adap->top + ADAP_REG_ADDR(reg);
		break;
	case SUBMD_FD:
		addr = adap->fd + ADAP_REG_ADDR(reg);
		break;
	case SUBMD_RD:
		addr = adap->rd + ADAP_REG_ADDR(reg);
		break;
	default:
		dev_err(adap->dev,
			"Invalid sub-module: %lu\n", ADAP_SUBMD(reg));
		return;
	}

	orig = readl(addr);
	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig)
		writel(tmp, addr);
}

/* Configure adapter top sub module */
static void c3_mipi_adap_cfg_top(struct c3_adap_device *adap)
{
	/* Reset adapter */
	c3_mipi_adap_update_bits(adap, MIPI_TOP_CTRL0,
				 MIPI_TOP_CTRL0_RST_ADAPTER_MASK,
				 MIPI_TOP_CTRL0_RST_ADAPTER_APPLY);
	c3_mipi_adap_update_bits(adap, MIPI_TOP_CTRL0,
				 MIPI_TOP_CTRL0_RST_ADAPTER_MASK,
				 MIPI_TOP_CTRL0_RST_ADAPTER_EXIT);

	/* Bypass decompress */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_DE_CTRL0,
				 MIPI_ADAPT_DE_CTRL0_RD_BUS_BYPASS_MASK,
				 MIPI_ADAPT_DE_CTRL0_RD_BUS_BYPASS_EN);
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_DE_CTRL0,
				 MIPI_ADAPT_DE_CTRL0_WR_BUS_BYPASS_MASK,
				 MIPI_ADAPT_DE_CTRL0_WR_BUS_BYPASS_EN);
}

/* Configure adapter frontend sub module */
static void c3_mipi_adap_cfg_frontend(struct c3_adap_device *adap,
				      struct v4l2_mbus_framefmt *fmt)
{
	/* Reset frontend module */
	c3_mipi_adap_update_bits(adap, CSI2_CLK_RESET,
				 CSI2_CLK_RESET_SW_RESET_MASK,
				 CSI2_CLK_RESET_SW_RESET_APPLY);
	c3_mipi_adap_update_bits(adap, CSI2_CLK_RESET,
				 CSI2_CLK_RESET_SW_RESET_MASK,
				 CSI2_CLK_RESET_SW_RESET_RELEASE);
	c3_mipi_adap_update_bits(adap, CSI2_CLK_RESET,
				 CSI2_CLK_RESET_CLK_ENABLE_MASK,
				 CSI2_CLK_RESET_CLK_ENABLE_EN);

	c3_mipi_adap_update_bits(adap, CSI2_X_START_END_ISP,
				 CSI2_X_START_END_ISP_X_START_MASK,
				 CSI2_X_START_END_ISP_X_START(0));
	c3_mipi_adap_update_bits(adap, CSI2_X_START_END_ISP,
				 CSI2_X_START_END_ISP_X_END_MASK,
				 CSI2_X_START_END_ISP_X_END(fmt->width));

	c3_mipi_adap_update_bits(adap, CSI2_Y_START_END_ISP,
				 CSI2_Y_START_END_ISP_Y_START_MASK,
				 CSI2_Y_START_END_ISP_Y_START(0));
	c3_mipi_adap_update_bits(adap, CSI2_Y_START_END_ISP,
				 CSI2_Y_START_END_ISP_Y_END_MASK,
				 CSI2_Y_START_END_ISP_Y_END(fmt->height));

	/* Select VS and HS signal for direct path */
	c3_mipi_adap_update_bits(adap, CSI2_VC_MODE,
				 CSI2_VC_MODE_VS_ISP_SEL_VC_MASK,
				 CSI2_VC_MODE_VS_ISP_SEL_VC_0);
	c3_mipi_adap_update_bits(adap, CSI2_VC_MODE,
				 CSI2_VC_MODE_HS_ISP_SEL_VC_MASK,
				 CSI2_VC_MODE_HS_ISP_SEL_VC_0);

	/* Enable to receive RAW packet */
	c3_mipi_adap_update_bits(adap, CSI2_GEN_CTRL0,
				 CSI2_GEN_CTRL0_ENABLE_PACKETS_MASK,
				 CSI2_GEN_CTRL0_ENABLE_PACKETS_RAW);

	/* Enable virtual channel 0 */
	c3_mipi_adap_update_bits(adap, CSI2_GEN_CTRL0,
				 CSI2_GEN_CTRL0_VC0_MASK,
				 CSI2_GEN_CTRL0_VC0_EN);
}

static void c3_mipi_adap_cfg_rd0(struct c3_adap_device *adap)
{
	/* Select direct mode for DDR_RD0 mode */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_DDR_RD0_CNTL1,
				 MIPI_ADAPT_DDR_RD0_CNTL1_PORT_SEL_MASK,
				 MIPI_ADAPT_DDR_RD0_CNTL1_PORT_SEL_DIRECT_MODE);

	/* Data can't bypass DDR_RD0 in direct mode, so enable DDR_RD0 here */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_DDR_RD0_CNTL0,
				 MIPI_ADAPT_DDR_RD0_CNTL0_MODULE_EN_MASK,
				 MIPI_ADAPT_DDR_RD0_CNTL0_MODULE_EN);
}

static void c3_mipi_adap_cfg_pixel0(struct c3_adap_device *adap,
				    struct v4l2_mbus_framefmt *fmt)
{
	const struct c3_adap_pix_format *pix;

	pix = c3_mipi_adap_find_format(fmt->code);

	/* Set work mode and data type for PIXEL0 module */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_PIXEL0_CNTL0,
				 MIPI_ADAPT_PIXEL0_CNTL0_WORK_MODE_MASK,
				 MIPI_ADAPT_PIXEL0_CNTL0_WORK_MODE_RAW_DIRECT);
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_PIXEL0_CNTL0,
				 MIPI_ADAPT_PIXEL0_CNTL0_DATA_TYPE_MASK,
				 MIPI_ADAPT_PIXEL0_CNTL0_DATA_TYPE(pix->type));

	/* Start PIXEL0 module */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_PIXEL0_CNTL0,
				 MIPI_ADAPT_PIXEL0_CNTL0_START_EN_MASK,
				 MIPI_ADAPT_PIXEL0_CNTL0_START_EN);
}

static void c3_mipi_adap_cfg_alig(struct c3_adap_device *adap,
				  struct v4l2_mbus_framefmt *fmt)
{
	/*
	 * ISP hardware requires the number of horizonal blanks greater than
	 * 64 cycles, so adding 64 here.
	 */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL0,
				 MIPI_ADAPT_ALIG_CNTL0_H_NUM_MASK,
				 MIPI_ADAPT_ALIG_CNTL0_H_NUM(fmt->width + 64));

	/*
	 * ISP hardware requires the number of vertical blanks greater than
	 * 40 lines, so adding 40 here.
	 */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL0,
				 MIPI_ADAPT_ALIG_CNTL0_V_NUM_MASK,
				 MIPI_ADAPT_ALIG_CNTL0_V_NUM(fmt->height + 40));

	/* End pixel in a line */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL1,
				 MIPI_ADAPT_ALIG_CNTL1_HPE_NUM_MASK,
				 MIPI_ADAPT_ALIG_CNTL1_HPE_NUM(fmt->width));

	/* End line in a frame */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL2,
				 MIPI_ADAPT_ALIG_CNTL2_VPE_NUM_MASK,
				 MIPI_ADAPT_ALIG_CNTL2_VPE_NUM(fmt->height));

	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL6,
				 MIPI_ADAPT_ALIG_CNTL6_PATH0_EN_MASK,
				 MIPI_ADAPT_ALIG_CNTL6_PATH0_EN);

	/* Select direct mode for ALIG module */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL6,
				 MIPI_ADAPT_ALIG_CNTL6_PIX0_DATA_MODE_MASK,
				 MIPI_ADAPT_ALIG_CNTL6_PIX0_DATA_MODE_DIRECT);

	/* Enable to send raw data */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL6,
				 MIPI_ADAPT_ALIG_CNTL6_DATA0_EN_MASK,
				 MIPI_ADAPT_ALIG_CNTL6_DATA0_EN);

	/* Set continue mode and disable hold counter */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL8,
				 MIPI_ADAPT_ALIG_CNTL8_FRMAE_CONTINUE_MASK,
				 MIPI_ADAPT_ALIG_CNTL8_FRMAE_CONTINUE_EN);
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL8,
				 MIPI_ADAPT_ALIG_CNTL8_EXCEED_DIS_MASK,
				 MIPI_ADAPT_ALIG_CNTL8_EXCEED_NOT_HOLD);

	/* Start ALIG module */
	c3_mipi_adap_update_bits(adap, MIPI_ADAPT_ALIG_CNTL8,
				 MIPI_ADAPT_ALIG_CNTL8_START_EN_MASK,
				 MIPI_ADAPT_ALIG_CNTL8_START_EN);
}

/* V4L2 subdev operations */

static int c3_mipi_adap_enable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       u32 pad, u64 streams_mask)
{
	struct c3_adap_device *adap = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;
	struct media_pad *sink_pad;
	struct v4l2_subdev *src_sd;
	int ret;

	sink_pad = &adap->pads[C3_MIPI_ADAP_PAD_SINK];
	adap->src_pad = media_pad_remote_pad_unique(sink_pad);
	if (IS_ERR(adap->src_pad)) {
		dev_dbg(adap->dev, "Failed to get source pad for MIPI adap\n");
		return -EPIPE;
	}

	src_sd = media_entity_to_v4l2_subdev(adap->src_pad->entity);

	pm_runtime_resume_and_get(adap->dev);

	fmt = v4l2_subdev_state_get_format(state, C3_MIPI_ADAP_PAD_SINK);

	c3_mipi_adap_cfg_top(adap);
	c3_mipi_adap_cfg_frontend(adap, fmt);
	c3_mipi_adap_cfg_rd0(adap);
	c3_mipi_adap_cfg_pixel0(adap, fmt);
	c3_mipi_adap_cfg_alig(adap, fmt);

	ret = v4l2_subdev_enable_streams(src_sd, adap->src_pad->index, BIT(0));
	if (ret) {
		pm_runtime_put(adap->dev);
		return ret;
	}

	return 0;
}

static int c3_mipi_adap_disable_streams(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					u32 pad, u64 streams_mask)
{
	struct c3_adap_device *adap = v4l2_get_subdevdata(sd);
	struct v4l2_subdev *src_sd;

	if (adap->src_pad) {
		src_sd = media_entity_to_v4l2_subdev(adap->src_pad->entity);
		v4l2_subdev_disable_streams(src_sd, adap->src_pad->index,
					    BIT(0));
	}
	adap->src_pad = NULL;

	pm_runtime_put(adap->dev);

	return 0;
}

static int c3_mipi_adap_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *fmt;

	switch (code->pad) {
	case C3_MIPI_ADAP_PAD_SINK:
		if (code->index >= ARRAY_SIZE(c3_mipi_adap_formats))
			return -EINVAL;

		code->code = c3_mipi_adap_formats[code->index].code;
		break;
	case C3_MIPI_ADAP_PAD_SRC:
		if (code->index)
			return -EINVAL;

		fmt = v4l2_subdev_state_get_format(state, code->pad);
		code->code = fmt->code;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int c3_mipi_adap_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;
	const struct c3_adap_pix_format *pix_format;

	if (format->pad != C3_MIPI_ADAP_PAD_SINK)
		return v4l2_subdev_get_fmt(sd, state, format);

	pix_format = c3_mipi_adap_find_format(format->format.code);
	if (!pix_format)
		pix_format = &c3_mipi_adap_formats[0];

	fmt = v4l2_subdev_state_get_format(state, format->pad);
	fmt->code = pix_format->code;
	fmt->width = clamp_t(u32, format->format.width,
			     MIPI_ADAP_MIN_WIDTH, MIPI_ADAP_MAX_WIDTH);
	fmt->height = clamp_t(u32, format->format.height,
			      MIPI_ADAP_MIN_HEIGHT, MIPI_ADAP_MAX_HEIGHT);
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	format->format = *fmt;

	/* Synchronize the format to source pad */
	fmt = v4l2_subdev_state_get_format(state, C3_MIPI_ADAP_PAD_SRC);
	*fmt = format->format;

	return 0;
}

static int c3_mipi_adap_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *src_fmt;

	sink_fmt = v4l2_subdev_state_get_format(state, C3_MIPI_ADAP_PAD_SINK);
	src_fmt = v4l2_subdev_state_get_format(state, C3_MIPI_ADAP_PAD_SRC);

	sink_fmt->width = MIPI_ADAP_DEFAULT_WIDTH;
	sink_fmt->height = MIPI_ADAP_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MIPI_ADAP_DEFAULT_FMT;
	sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
	sink_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	sink_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	sink_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	*src_fmt = *sink_fmt;

	return 0;
}

static const struct v4l2_subdev_pad_ops c3_mipi_adap_pad_ops = {
	.enum_mbus_code = c3_mipi_adap_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = c3_mipi_adap_set_fmt,
	.enable_streams = c3_mipi_adap_enable_streams,
	.disable_streams = c3_mipi_adap_disable_streams,
};

static const struct v4l2_subdev_ops c3_mipi_adap_subdev_ops = {
	.pad = &c3_mipi_adap_pad_ops,
};

static const struct v4l2_subdev_internal_ops c3_mipi_adap_internal_ops = {
	.init_state = c3_mipi_adap_init_state,
};

/* Media entity operations */
static const struct media_entity_operations c3_mipi_adap_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* PM runtime */

static int c3_mipi_adap_runtime_suspend(struct device *dev)
{
	struct c3_adap_device *adap = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(adap->info->clock_num, adap->clks);

	return 0;
}

static int c3_mipi_adap_runtime_resume(struct device *dev)
{
	struct c3_adap_device *adap = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(adap->info->clock_num, adap->clks);
}

static const struct dev_pm_ops c3_mipi_adap_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
			    pm_runtime_force_resume)
	RUNTIME_PM_OPS(c3_mipi_adap_runtime_suspend,
		       c3_mipi_adap_runtime_resume, NULL)
};

/* Probe/remove & platform driver */

static int c3_mipi_adap_subdev_init(struct c3_adap_device *adap)
{
	struct v4l2_subdev *sd = &adap->sd;
	int ret;

	v4l2_subdev_init(sd, &c3_mipi_adap_subdev_ops);
	sd->owner = THIS_MODULE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &c3_mipi_adap_internal_ops;
	snprintf(sd->name, sizeof(sd->name), "%s", MIPI_ADAP_SUBDEV_NAME);

	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &c3_mipi_adap_entity_ops;

	sd->dev = adap->dev;
	v4l2_set_subdevdata(sd, adap);

	adap->pads[C3_MIPI_ADAP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	adap->pads[C3_MIPI_ADAP_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, C3_MIPI_ADAP_PAD_MAX,
				     adap->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret) {
		media_entity_cleanup(&sd->entity);
		return ret;
	}

	return 0;
}

static void c3_mipi_adap_subdev_deinit(struct c3_adap_device *adap)
{
	v4l2_subdev_cleanup(&adap->sd);
	media_entity_cleanup(&adap->sd.entity);
}

/* Subdev notifier register */
static int c3_mipi_adap_notify_bound(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *sd,
				     struct v4l2_async_connection *asc)
{
	struct c3_adap_device *adap = v4l2_get_subdevdata(notifier->sd);
	struct media_pad *sink = &adap->sd.entity.pads[C3_MIPI_ADAP_PAD_SINK];

	return v4l2_create_fwnode_links_to_pad(sd, sink, MEDIA_LNK_FL_ENABLED |
					       MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations c3_mipi_adap_notify_ops = {
	.bound = c3_mipi_adap_notify_bound,
};

static int c3_mipi_adap_async_register(struct c3_adap_device *adap)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_subdev_nf_init(&adap->notifier, &adap->sd);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(adap->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENOTCONN;

	asc = v4l2_async_nf_add_fwnode_remote(&adap->notifier, ep,
					      struct v4l2_async_connection);
	if (IS_ERR(asc)) {
		ret = PTR_ERR(asc);
		goto err_put_handle;
	}

	adap->notifier.ops = &c3_mipi_adap_notify_ops;
	ret = v4l2_async_nf_register(&adap->notifier);
	if (ret)
		goto err_cleanup_nf;

	ret = v4l2_async_register_subdev(&adap->sd);
	if (ret)
		goto err_unregister_nf;

	fwnode_handle_put(ep);

	return 0;

err_unregister_nf:
	v4l2_async_nf_unregister(&adap->notifier);
err_cleanup_nf:
	v4l2_async_nf_cleanup(&adap->notifier);
err_put_handle:
	fwnode_handle_put(ep);
	return ret;
}

static void c3_mipi_adap_async_unregister(struct c3_adap_device *adap)
{
	v4l2_async_unregister_subdev(&adap->sd);
	v4l2_async_nf_unregister(&adap->notifier);
	v4l2_async_nf_cleanup(&adap->notifier);
}

static int c3_mipi_adap_ioremap_resource(struct c3_adap_device *adap)
{
	struct device *dev = adap->dev;
	struct platform_device *pdev = to_platform_device(dev);

	adap->top = devm_platform_ioremap_resource_byname(pdev, "top");
	if (IS_ERR(adap->top))
		return PTR_ERR(adap->top);

	adap->fd = devm_platform_ioremap_resource_byname(pdev, "fd");
	if (IS_ERR(adap->fd))
		return PTR_ERR(adap->fd);

	adap->rd = devm_platform_ioremap_resource_byname(pdev, "rd");
	if (IS_ERR(adap->rd))
		return PTR_ERR(adap->rd);

	return 0;
}

static int c3_mipi_adap_get_clocks(struct c3_adap_device *adap)
{
	const struct c3_adap_info *info = adap->info;

	for (unsigned int i = 0; i < info->clock_num; i++)
		adap->clks[i].id = info->clocks[i];

	return devm_clk_bulk_get(adap->dev, info->clock_num, adap->clks);
}

static int c3_mipi_adap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct c3_adap_device *adap;
	int ret;

	adap = devm_kzalloc(dev, sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	adap->info = of_device_get_match_data(dev);
	adap->dev = dev;

	ret = c3_mipi_adap_ioremap_resource(adap);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to ioremap resource\n");

	ret = c3_mipi_adap_get_clocks(adap);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	platform_set_drvdata(pdev, adap);

	pm_runtime_enable(dev);

	ret = c3_mipi_adap_subdev_init(adap);
	if (ret)
		goto err_disable_runtime_pm;

	ret = c3_mipi_adap_async_register(adap);
	if (ret)
		goto err_deinit_subdev;

	return 0;

err_deinit_subdev:
	c3_mipi_adap_subdev_deinit(adap);
err_disable_runtime_pm:
	pm_runtime_disable(dev);
	return ret;
};

static void c3_mipi_adap_remove(struct platform_device *pdev)
{
	struct c3_adap_device *adap = platform_get_drvdata(pdev);

	c3_mipi_adap_async_unregister(adap);
	c3_mipi_adap_subdev_deinit(adap);

	pm_runtime_disable(&pdev->dev);
};

static const struct c3_adap_info c3_mipi_adap_info = {
	.clocks = {"vapb", "isp0"},
	.clock_num = 2
};

static const struct of_device_id c3_mipi_adap_of_match[] = {
	{
		.compatible = "amlogic,c3-mipi-adapter",
		.data = &c3_mipi_adap_info
	},
	{ },
};
MODULE_DEVICE_TABLE(of, c3_mipi_adap_of_match);

static struct platform_driver c3_mipi_adap_driver = {
	.probe = c3_mipi_adap_probe,
	.remove = c3_mipi_adap_remove,
	.driver = {
		.name = "c3-mipi-adapter",
		.of_match_table = c3_mipi_adap_of_match,
		.pm = pm_ptr(&c3_mipi_adap_pm_ops),
	},
};

module_platform_driver(c3_mipi_adap_driver);

MODULE_AUTHOR("Keke Li <keke.li@amlogic.com>");
MODULE_DESCRIPTION("Amlogic C3 MIPI adapter");
MODULE_LICENSE("GPL");
