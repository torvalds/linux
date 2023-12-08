// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2014 Mentor Graphics Inc.
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc.
 */
#include <linux/export.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <uapi/linux/v4l2-mediabus.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#include "ipu-prv.h"

struct ipu_csi {
	void __iomem *base;
	int id;
	u32 module;
	struct clk *clk_ipu;	/* IPU bus clock */
	spinlock_t lock;
	bool inuse;
	struct ipu_soc *ipu;
};

/* CSI Register Offsets */
#define CSI_SENS_CONF		0x0000
#define CSI_SENS_FRM_SIZE	0x0004
#define CSI_ACT_FRM_SIZE	0x0008
#define CSI_OUT_FRM_CTRL	0x000c
#define CSI_TST_CTRL		0x0010
#define CSI_CCIR_CODE_1		0x0014
#define CSI_CCIR_CODE_2		0x0018
#define CSI_CCIR_CODE_3		0x001c
#define CSI_MIPI_DI		0x0020
#define CSI_SKIP		0x0024
#define CSI_CPD_CTRL		0x0028
#define CSI_CPD_RC(n)		(0x002c + ((n)*4))
#define CSI_CPD_RS(n)		(0x004c + ((n)*4))
#define CSI_CPD_GRC(n)		(0x005c + ((n)*4))
#define CSI_CPD_GRS(n)		(0x007c + ((n)*4))
#define CSI_CPD_GBC(n)		(0x008c + ((n)*4))
#define CSI_CPD_GBS(n)		(0x00Ac + ((n)*4))
#define CSI_CPD_BC(n)		(0x00Bc + ((n)*4))
#define CSI_CPD_BS(n)		(0x00Dc + ((n)*4))
#define CSI_CPD_OFFSET1		0x00ec
#define CSI_CPD_OFFSET2		0x00f0

/* CSI Register Fields */
#define CSI_SENS_CONF_DATA_FMT_SHIFT		8
#define CSI_SENS_CONF_DATA_FMT_MASK		0x00000700
#define CSI_SENS_CONF_DATA_FMT_RGB_YUV444	0L
#define CSI_SENS_CONF_DATA_FMT_YUV422_YUYV	1L
#define CSI_SENS_CONF_DATA_FMT_YUV422_UYVY	2L
#define CSI_SENS_CONF_DATA_FMT_BAYER		3L
#define CSI_SENS_CONF_DATA_FMT_RGB565		4L
#define CSI_SENS_CONF_DATA_FMT_RGB555		5L
#define CSI_SENS_CONF_DATA_FMT_RGB444		6L
#define CSI_SENS_CONF_DATA_FMT_JPEG		7L

#define CSI_SENS_CONF_VSYNC_POL_SHIFT		0
#define CSI_SENS_CONF_HSYNC_POL_SHIFT		1
#define CSI_SENS_CONF_DATA_POL_SHIFT		2
#define CSI_SENS_CONF_PIX_CLK_POL_SHIFT		3
#define CSI_SENS_CONF_SENS_PRTCL_MASK		0x00000070
#define CSI_SENS_CONF_SENS_PRTCL_SHIFT		4
#define CSI_SENS_CONF_PACK_TIGHT_SHIFT		7
#define CSI_SENS_CONF_DATA_WIDTH_SHIFT		11
#define CSI_SENS_CONF_EXT_VSYNC_SHIFT		15
#define CSI_SENS_CONF_DIVRATIO_SHIFT		16

#define CSI_SENS_CONF_DIVRATIO_MASK		0x00ff0000
#define CSI_SENS_CONF_DATA_DEST_SHIFT		24
#define CSI_SENS_CONF_DATA_DEST_MASK		0x07000000
#define CSI_SENS_CONF_JPEG8_EN_SHIFT		27
#define CSI_SENS_CONF_JPEG_EN_SHIFT		28
#define CSI_SENS_CONF_FORCE_EOF_SHIFT		29
#define CSI_SENS_CONF_DATA_EN_POL_SHIFT		31

#define CSI_DATA_DEST_IC			2
#define CSI_DATA_DEST_IDMAC			4

#define CSI_CCIR_ERR_DET_EN			0x01000000
#define CSI_HORI_DOWNSIZE_EN			0x80000000
#define CSI_VERT_DOWNSIZE_EN			0x40000000
#define CSI_TEST_GEN_MODE_EN			0x01000000

#define CSI_HSC_MASK				0x1fff0000
#define CSI_HSC_SHIFT				16
#define CSI_VSC_MASK				0x00000fff
#define CSI_VSC_SHIFT				0

#define CSI_TEST_GEN_R_MASK			0x000000ff
#define CSI_TEST_GEN_R_SHIFT			0
#define CSI_TEST_GEN_G_MASK			0x0000ff00
#define CSI_TEST_GEN_G_SHIFT			8
#define CSI_TEST_GEN_B_MASK			0x00ff0000
#define CSI_TEST_GEN_B_SHIFT			16

#define CSI_MAX_RATIO_SKIP_SMFC_MASK		0x00000007
#define CSI_MAX_RATIO_SKIP_SMFC_SHIFT		0
#define CSI_SKIP_SMFC_MASK			0x000000f8
#define CSI_SKIP_SMFC_SHIFT			3
#define CSI_ID_2_SKIP_MASK			0x00000300
#define CSI_ID_2_SKIP_SHIFT			8

#define CSI_COLOR_FIRST_ROW_MASK		0x00000002
#define CSI_COLOR_FIRST_COMP_MASK		0x00000001

/* MIPI CSI-2 data types */
#define MIPI_DT_YUV420		0x18 /* YYY.../UYVY.... */
#define MIPI_DT_YUV420_LEGACY	0x1a /* UYY.../VYY...   */
#define MIPI_DT_YUV422		0x1e /* UYVY...         */
#define MIPI_DT_RGB444		0x20
#define MIPI_DT_RGB555		0x21
#define MIPI_DT_RGB565		0x22
#define MIPI_DT_RGB666		0x23
#define MIPI_DT_RGB888		0x24
#define MIPI_DT_RAW6		0x28
#define MIPI_DT_RAW7		0x29
#define MIPI_DT_RAW8		0x2a
#define MIPI_DT_RAW10		0x2b
#define MIPI_DT_RAW12		0x2c
#define MIPI_DT_RAW14		0x2d

/*
 * Bitfield of CSI bus signal polarities and modes.
 */
struct ipu_csi_bus_config {
	unsigned data_width:4;
	unsigned clk_mode:3;
	unsigned ext_vsync:1;
	unsigned vsync_pol:1;
	unsigned hsync_pol:1;
	unsigned pixclk_pol:1;
	unsigned data_pol:1;
	unsigned sens_clksrc:1;
	unsigned pack_tight:1;
	unsigned force_eof:1;
	unsigned data_en_pol:1;

	unsigned data_fmt;
	unsigned mipi_dt;
};

/*
 * Enumeration of CSI data bus widths.
 */
enum ipu_csi_data_width {
	IPU_CSI_DATA_WIDTH_4   = 0,
	IPU_CSI_DATA_WIDTH_8   = 1,
	IPU_CSI_DATA_WIDTH_10  = 3,
	IPU_CSI_DATA_WIDTH_12  = 5,
	IPU_CSI_DATA_WIDTH_16  = 9,
};

/*
 * Enumeration of CSI clock modes.
 */
enum ipu_csi_clk_mode {
	IPU_CSI_CLK_MODE_GATED_CLK,
	IPU_CSI_CLK_MODE_NONGATED_CLK,
	IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE,
	IPU_CSI_CLK_MODE_CCIR656_INTERLACED,
	IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR,
	IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR,
	IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR,
	IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR,
};

static inline u32 ipu_csi_read(struct ipu_csi *csi, unsigned offset)
{
	return readl(csi->base + offset);
}

static inline void ipu_csi_write(struct ipu_csi *csi, u32 value,
				 unsigned offset)
{
	writel(value, csi->base + offset);
}

/*
 * Set mclk division ratio for generating test mode mclk. Only used
 * for test generator.
 */
static int ipu_csi_set_testgen_mclk(struct ipu_csi *csi, u32 pixel_clk,
					u32 ipu_clk)
{
	u32 temp;
	int div_ratio;

	div_ratio = (ipu_clk / pixel_clk) - 1;

	if (div_ratio > 0xFF || div_ratio < 0) {
		dev_err(csi->ipu->dev,
			"value of pixel_clk extends normal range\n");
		return -EINVAL;
	}

	temp = ipu_csi_read(csi, CSI_SENS_CONF);
	temp &= ~CSI_SENS_CONF_DIVRATIO_MASK;
	ipu_csi_write(csi, temp | (div_ratio << CSI_SENS_CONF_DIVRATIO_SHIFT),
			  CSI_SENS_CONF);

	return 0;
}

/*
 * Find the CSI data format and data width for the given V4L2 media
 * bus pixel format code.
 */
static int mbus_code_to_bus_cfg(struct ipu_csi_bus_config *cfg, u32 mbus_code,
				enum v4l2_mbus_type mbus_type)
{
	switch (mbus_code) {
	case MEDIA_BUS_FMT_BGR565_2X8_BE:
	case MEDIA_BUS_FMT_BGR565_2X8_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		if (mbus_type == V4L2_MBUS_CSI2_DPHY)
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_RGB565;
		else
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		cfg->mipi_dt = MIPI_DT_RGB565;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_RGB444;
		cfg->mipi_dt = MIPI_DT_RGB444;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_RGB555;
		cfg->mipi_dt = MIPI_DT_RGB555;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_RGB_YUV444;
		cfg->mipi_dt = MIPI_DT_RGB888;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_UYVY8_2X8:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_UYVY;
		cfg->mipi_dt = MIPI_DT_YUV422;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_YUYV;
		cfg->mipi_dt = MIPI_DT_YUV422;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		if (mbus_type == V4L2_MBUS_BT656) {
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_UYVY;
			cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		} else {
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
			cfg->data_width = IPU_CSI_DATA_WIDTH_16;
		}
		cfg->mipi_dt = MIPI_DT_YUV422;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
		if (mbus_type == V4L2_MBUS_BT656) {
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_YUYV;
			cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		} else {
			cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
			cfg->data_width = IPU_CSI_DATA_WIDTH_16;
		}
		cfg->mipi_dt = MIPI_DT_YUV422;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		cfg->mipi_dt = MIPI_DT_RAW8;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE:
	case MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE:
	case MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		cfg->mipi_dt = MIPI_DT_RAW10;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_Y10_1X10:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		cfg->mipi_dt = MIPI_DT_RAW10;
		cfg->data_width = IPU_CSI_DATA_WIDTH_10;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_Y12_1X12:
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		cfg->mipi_dt = MIPI_DT_RAW12;
		cfg->data_width = IPU_CSI_DATA_WIDTH_12;
		break;
	case MEDIA_BUS_FMT_JPEG_1X8:
		/* TODO */
		cfg->data_fmt = CSI_SENS_CONF_DATA_FMT_JPEG;
		cfg->mipi_dt = MIPI_DT_RAW8;
		cfg->data_width = IPU_CSI_DATA_WIDTH_8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* translate alternate field mode based on given standard */
static inline enum v4l2_field
ipu_csi_translate_field(enum v4l2_field field, v4l2_std_id std)
{
	return (field != V4L2_FIELD_ALTERNATE) ? field :
		((std & V4L2_STD_525_60) ?
		 V4L2_FIELD_SEQ_BT : V4L2_FIELD_SEQ_TB);
}

/*
 * Fill a CSI bus config struct from mbus_config and mbus_framefmt.
 */
static int fill_csi_bus_cfg(struct ipu_csi_bus_config *csicfg,
			    const struct v4l2_mbus_config *mbus_cfg,
			    const struct v4l2_mbus_framefmt *mbus_fmt)
{
	int ret, is_bt1120;

	memset(csicfg, 0, sizeof(*csicfg));

	ret = mbus_code_to_bus_cfg(csicfg, mbus_fmt->code, mbus_cfg->type);
	if (ret < 0)
		return ret;

	switch (mbus_cfg->type) {
	case V4L2_MBUS_PARALLEL:
		csicfg->ext_vsync = 1;
		csicfg->vsync_pol = (mbus_cfg->bus.parallel.flags &
				     V4L2_MBUS_VSYNC_ACTIVE_LOW) ? 1 : 0;
		csicfg->hsync_pol = (mbus_cfg->bus.parallel.flags &
				     V4L2_MBUS_HSYNC_ACTIVE_LOW) ? 1 : 0;
		csicfg->pixclk_pol = (mbus_cfg->bus.parallel.flags &
				      V4L2_MBUS_PCLK_SAMPLE_FALLING) ? 1 : 0;
		csicfg->clk_mode = IPU_CSI_CLK_MODE_GATED_CLK;
		break;
	case V4L2_MBUS_BT656:
		csicfg->ext_vsync = 0;
		/* UYVY10_1X20 etc. should be supported as well */
		is_bt1120 = mbus_fmt->code == MEDIA_BUS_FMT_UYVY8_1X16 ||
			    mbus_fmt->code == MEDIA_BUS_FMT_YUYV8_1X16;
		if (V4L2_FIELD_HAS_BOTH(mbus_fmt->field) ||
		    mbus_fmt->field == V4L2_FIELD_ALTERNATE)
			csicfg->clk_mode = is_bt1120 ?
				IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR :
				IPU_CSI_CLK_MODE_CCIR656_INTERLACED;
		else
			csicfg->clk_mode = is_bt1120 ?
				IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR :
				IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE;
		break;
	case V4L2_MBUS_CSI2_DPHY:
		/*
		 * MIPI CSI-2 requires non gated clock mode, all other
		 * parameters are not applicable for MIPI CSI-2 bus.
		 */
		csicfg->clk_mode = IPU_CSI_CLK_MODE_NONGATED_CLK;
		break;
	default:
		/* will never get here, keep compiler quiet */
		break;
	}

	return 0;
}

static int
ipu_csi_set_bt_interlaced_codes(struct ipu_csi *csi,
				const struct v4l2_mbus_framefmt *infmt,
				const struct v4l2_mbus_framefmt *outfmt,
				v4l2_std_id std)
{
	enum v4l2_field infield, outfield;
	bool swap_fields;

	/* get translated field type of input and output */
	infield = ipu_csi_translate_field(infmt->field, std);
	outfield = ipu_csi_translate_field(outfmt->field, std);

	/*
	 * Write the H-V-F codes the CSI will match against the
	 * incoming data for start/end of active and blanking
	 * field intervals. If input and output field types are
	 * sequential but not the same (one is SEQ_BT and the other
	 * is SEQ_TB), swap the F-bit so that the CSI will capture
	 * field 1 lines before field 0 lines.
	 */
	swap_fields = (V4L2_FIELD_IS_SEQUENTIAL(infield) &&
		       V4L2_FIELD_IS_SEQUENTIAL(outfield) &&
		       infield != outfield);

	if (!swap_fields) {
		/*
		 * Field0BlankEnd  = 110, Field0BlankStart  = 010
		 * Field0ActiveEnd = 100, Field0ActiveStart = 000
		 * Field1BlankEnd  = 111, Field1BlankStart  = 011
		 * Field1ActiveEnd = 101, Field1ActiveStart = 001
		 */
		ipu_csi_write(csi, 0x40596 | CSI_CCIR_ERR_DET_EN,
			      CSI_CCIR_CODE_1);
		ipu_csi_write(csi, 0xD07DF, CSI_CCIR_CODE_2);
	} else {
		dev_dbg(csi->ipu->dev, "capture field swap\n");

		/* same as above but with F-bit inverted */
		ipu_csi_write(csi, 0xD07DF | CSI_CCIR_ERR_DET_EN,
			      CSI_CCIR_CODE_1);
		ipu_csi_write(csi, 0x40596, CSI_CCIR_CODE_2);
	}

	ipu_csi_write(csi, 0xFF0000, CSI_CCIR_CODE_3);

	return 0;
}


int ipu_csi_init_interface(struct ipu_csi *csi,
			   const struct v4l2_mbus_config *mbus_cfg,
			   const struct v4l2_mbus_framefmt *infmt,
			   const struct v4l2_mbus_framefmt *outfmt)
{
	struct ipu_csi_bus_config cfg;
	unsigned long flags;
	u32 width, height, data = 0;
	v4l2_std_id std;
	int ret;

	ret = fill_csi_bus_cfg(&cfg, mbus_cfg, infmt);
	if (ret < 0)
		return ret;

	/* set default sensor frame width and height */
	width = infmt->width;
	height = infmt->height;
	if (infmt->field == V4L2_FIELD_ALTERNATE)
		height *= 2;

	/* Set the CSI_SENS_CONF register remaining fields */
	data |= cfg.data_width << CSI_SENS_CONF_DATA_WIDTH_SHIFT |
		cfg.data_fmt << CSI_SENS_CONF_DATA_FMT_SHIFT |
		cfg.data_pol << CSI_SENS_CONF_DATA_POL_SHIFT |
		cfg.vsync_pol << CSI_SENS_CONF_VSYNC_POL_SHIFT |
		cfg.hsync_pol << CSI_SENS_CONF_HSYNC_POL_SHIFT |
		cfg.pixclk_pol << CSI_SENS_CONF_PIX_CLK_POL_SHIFT |
		cfg.ext_vsync << CSI_SENS_CONF_EXT_VSYNC_SHIFT |
		cfg.clk_mode << CSI_SENS_CONF_SENS_PRTCL_SHIFT |
		cfg.pack_tight << CSI_SENS_CONF_PACK_TIGHT_SHIFT |
		cfg.force_eof << CSI_SENS_CONF_FORCE_EOF_SHIFT |
		cfg.data_en_pol << CSI_SENS_CONF_DATA_EN_POL_SHIFT;

	spin_lock_irqsave(&csi->lock, flags);

	ipu_csi_write(csi, data, CSI_SENS_CONF);

	/* Set CCIR registers */

	switch (cfg.clk_mode) {
	case IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE:
		ipu_csi_write(csi, 0x40030, CSI_CCIR_CODE_1);
		ipu_csi_write(csi, 0xFF0000, CSI_CCIR_CODE_3);
		break;
	case IPU_CSI_CLK_MODE_CCIR656_INTERLACED:
		if (width == 720 && height == 480) {
			std = V4L2_STD_NTSC;
			height = 525;
		} else if (width == 720 && height == 576) {
			std = V4L2_STD_PAL;
			height = 625;
		} else {
			dev_err(csi->ipu->dev,
				"Unsupported interlaced video mode\n");
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = ipu_csi_set_bt_interlaced_codes(csi, infmt, outfmt, std);
		if (ret)
			goto out_unlock;
		break;
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR:
		ipu_csi_write(csi, 0x40030 | CSI_CCIR_ERR_DET_EN,
				   CSI_CCIR_CODE_1);
		ipu_csi_write(csi, 0xFF0000, CSI_CCIR_CODE_3);
		break;
	case IPU_CSI_CLK_MODE_GATED_CLK:
	case IPU_CSI_CLK_MODE_NONGATED_CLK:
		ipu_csi_write(csi, 0, CSI_CCIR_CODE_1);
		break;
	}

	/* Setup sensor frame size */
	ipu_csi_write(csi, (width - 1) | ((height - 1) << 16),
		      CSI_SENS_FRM_SIZE);

	dev_dbg(csi->ipu->dev, "CSI_SENS_CONF = 0x%08X\n",
		ipu_csi_read(csi, CSI_SENS_CONF));
	dev_dbg(csi->ipu->dev, "CSI_ACT_FRM_SIZE = 0x%08X\n",
		ipu_csi_read(csi, CSI_ACT_FRM_SIZE));

out_unlock:
	spin_unlock_irqrestore(&csi->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(ipu_csi_init_interface);

bool ipu_csi_is_interlaced(struct ipu_csi *csi)
{
	unsigned long flags;
	u32 sensor_protocol;

	spin_lock_irqsave(&csi->lock, flags);
	sensor_protocol =
		(ipu_csi_read(csi, CSI_SENS_CONF) &
		 CSI_SENS_CONF_SENS_PRTCL_MASK) >>
		CSI_SENS_CONF_SENS_PRTCL_SHIFT;
	spin_unlock_irqrestore(&csi->lock, flags);

	switch (sensor_protocol) {
	case IPU_CSI_CLK_MODE_GATED_CLK:
	case IPU_CSI_CLK_MODE_NONGATED_CLK:
	case IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE:
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR:
		return false;
	case IPU_CSI_CLK_MODE_CCIR656_INTERLACED:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR:
	case IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR:
		return true;
	default:
		dev_err(csi->ipu->dev,
			"CSI %d sensor protocol unsupported\n", csi->id);
		return false;
	}
}
EXPORT_SYMBOL_GPL(ipu_csi_is_interlaced);

void ipu_csi_get_window(struct ipu_csi *csi, struct v4l2_rect *w)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&csi->lock, flags);

	reg = ipu_csi_read(csi, CSI_ACT_FRM_SIZE);
	w->width = (reg & 0xFFFF) + 1;
	w->height = (reg >> 16 & 0xFFFF) + 1;

	reg = ipu_csi_read(csi, CSI_OUT_FRM_CTRL);
	w->left = (reg & CSI_HSC_MASK) >> CSI_HSC_SHIFT;
	w->top = (reg & CSI_VSC_MASK) >> CSI_VSC_SHIFT;

	spin_unlock_irqrestore(&csi->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_csi_get_window);

void ipu_csi_set_window(struct ipu_csi *csi, struct v4l2_rect *w)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&csi->lock, flags);

	ipu_csi_write(csi, (w->width - 1) | ((w->height - 1) << 16),
			  CSI_ACT_FRM_SIZE);

	reg = ipu_csi_read(csi, CSI_OUT_FRM_CTRL);
	reg &= ~(CSI_HSC_MASK | CSI_VSC_MASK);
	reg |= ((w->top << CSI_VSC_SHIFT) | (w->left << CSI_HSC_SHIFT));
	ipu_csi_write(csi, reg, CSI_OUT_FRM_CTRL);

	spin_unlock_irqrestore(&csi->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_csi_set_window);

void ipu_csi_set_downsize(struct ipu_csi *csi, bool horiz, bool vert)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&csi->lock, flags);

	reg = ipu_csi_read(csi, CSI_OUT_FRM_CTRL);
	reg &= ~(CSI_HORI_DOWNSIZE_EN | CSI_VERT_DOWNSIZE_EN);
	reg |= (horiz ? CSI_HORI_DOWNSIZE_EN : 0) |
	       (vert ? CSI_VERT_DOWNSIZE_EN : 0);
	ipu_csi_write(csi, reg, CSI_OUT_FRM_CTRL);

	spin_unlock_irqrestore(&csi->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_csi_set_downsize);

void ipu_csi_set_test_generator(struct ipu_csi *csi, bool active,
				u32 r_value, u32 g_value, u32 b_value,
				u32 pix_clk)
{
	unsigned long flags;
	u32 ipu_clk = clk_get_rate(csi->clk_ipu);
	u32 temp;

	spin_lock_irqsave(&csi->lock, flags);

	temp = ipu_csi_read(csi, CSI_TST_CTRL);

	if (!active) {
		temp &= ~CSI_TEST_GEN_MODE_EN;
		ipu_csi_write(csi, temp, CSI_TST_CTRL);
	} else {
		/* Set sensb_mclk div_ratio */
		ipu_csi_set_testgen_mclk(csi, pix_clk, ipu_clk);

		temp &= ~(CSI_TEST_GEN_R_MASK | CSI_TEST_GEN_G_MASK |
			  CSI_TEST_GEN_B_MASK);
		temp |= CSI_TEST_GEN_MODE_EN;
		temp |= (r_value << CSI_TEST_GEN_R_SHIFT) |
			(g_value << CSI_TEST_GEN_G_SHIFT) |
			(b_value << CSI_TEST_GEN_B_SHIFT);
		ipu_csi_write(csi, temp, CSI_TST_CTRL);
	}

	spin_unlock_irqrestore(&csi->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_csi_set_test_generator);

int ipu_csi_set_mipi_datatype(struct ipu_csi *csi, u32 vc,
			      struct v4l2_mbus_framefmt *mbus_fmt)
{
	struct ipu_csi_bus_config cfg;
	unsigned long flags;
	u32 temp;
	int ret;

	if (vc > 3)
		return -EINVAL;

	ret = mbus_code_to_bus_cfg(&cfg, mbus_fmt->code, V4L2_MBUS_CSI2_DPHY);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&csi->lock, flags);

	temp = ipu_csi_read(csi, CSI_MIPI_DI);
	temp &= ~(0xff << (vc * 8));
	temp |= (cfg.mipi_dt << (vc * 8));
	ipu_csi_write(csi, temp, CSI_MIPI_DI);

	spin_unlock_irqrestore(&csi->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_csi_set_mipi_datatype);

int ipu_csi_set_skip_smfc(struct ipu_csi *csi, u32 skip,
			  u32 max_ratio, u32 id)
{
	unsigned long flags;
	u32 temp;

	if (max_ratio > 5 || id > 3)
		return -EINVAL;

	spin_lock_irqsave(&csi->lock, flags);

	temp = ipu_csi_read(csi, CSI_SKIP);
	temp &= ~(CSI_MAX_RATIO_SKIP_SMFC_MASK | CSI_ID_2_SKIP_MASK |
		  CSI_SKIP_SMFC_MASK);
	temp |= (max_ratio << CSI_MAX_RATIO_SKIP_SMFC_SHIFT) |
		(id << CSI_ID_2_SKIP_SHIFT) |
		(skip << CSI_SKIP_SMFC_SHIFT);
	ipu_csi_write(csi, temp, CSI_SKIP);

	spin_unlock_irqrestore(&csi->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_csi_set_skip_smfc);

int ipu_csi_set_dest(struct ipu_csi *csi, enum ipu_csi_dest csi_dest)
{
	unsigned long flags;
	u32 csi_sens_conf, dest;

	if (csi_dest == IPU_CSI_DEST_IDMAC)
		dest = CSI_DATA_DEST_IDMAC;
	else
		dest = CSI_DATA_DEST_IC; /* IC or VDIC */

	spin_lock_irqsave(&csi->lock, flags);

	csi_sens_conf = ipu_csi_read(csi, CSI_SENS_CONF);
	csi_sens_conf &= ~CSI_SENS_CONF_DATA_DEST_MASK;
	csi_sens_conf |= (dest << CSI_SENS_CONF_DATA_DEST_SHIFT);
	ipu_csi_write(csi, csi_sens_conf, CSI_SENS_CONF);

	spin_unlock_irqrestore(&csi->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_csi_set_dest);

int ipu_csi_enable(struct ipu_csi *csi)
{
	ipu_module_enable(csi->ipu, csi->module);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_csi_enable);

int ipu_csi_disable(struct ipu_csi *csi)
{
	ipu_module_disable(csi->ipu, csi->module);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_csi_disable);

struct ipu_csi *ipu_csi_get(struct ipu_soc *ipu, int id)
{
	unsigned long flags;
	struct ipu_csi *csi, *ret;

	if (id > 1)
		return ERR_PTR(-EINVAL);

	csi = ipu->csi_priv[id];
	ret = csi;

	spin_lock_irqsave(&csi->lock, flags);

	if (csi->inuse) {
		ret = ERR_PTR(-EBUSY);
		goto unlock;
	}

	csi->inuse = true;
unlock:
	spin_unlock_irqrestore(&csi->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(ipu_csi_get);

void ipu_csi_put(struct ipu_csi *csi)
{
	unsigned long flags;

	spin_lock_irqsave(&csi->lock, flags);
	csi->inuse = false;
	spin_unlock_irqrestore(&csi->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_csi_put);

int ipu_csi_init(struct ipu_soc *ipu, struct device *dev, int id,
		 unsigned long base, u32 module, struct clk *clk_ipu)
{
	struct ipu_csi *csi;

	if (id > 1)
		return -ENODEV;

	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	ipu->csi_priv[id] = csi;

	spin_lock_init(&csi->lock);
	csi->module = module;
	csi->id = id;
	csi->clk_ipu = clk_ipu;
	csi->base = devm_ioremap(dev, base, PAGE_SIZE);
	if (!csi->base)
		return -ENOMEM;

	dev_dbg(dev, "CSI%d base: 0x%08lx remapped to %p\n",
		id, base, csi->base);
	csi->ipu = ipu;

	return 0;
}

void ipu_csi_exit(struct ipu_soc *ipu, int id)
{
}

void ipu_csi_dump(struct ipu_csi *csi)
{
	dev_dbg(csi->ipu->dev, "CSI_SENS_CONF:     %08x\n",
		ipu_csi_read(csi, CSI_SENS_CONF));
	dev_dbg(csi->ipu->dev, "CSI_SENS_FRM_SIZE: %08x\n",
		ipu_csi_read(csi, CSI_SENS_FRM_SIZE));
	dev_dbg(csi->ipu->dev, "CSI_ACT_FRM_SIZE:  %08x\n",
		ipu_csi_read(csi, CSI_ACT_FRM_SIZE));
	dev_dbg(csi->ipu->dev, "CSI_OUT_FRM_CTRL:  %08x\n",
		ipu_csi_read(csi, CSI_OUT_FRM_CTRL));
	dev_dbg(csi->ipu->dev, "CSI_TST_CTRL:      %08x\n",
		ipu_csi_read(csi, CSI_TST_CTRL));
	dev_dbg(csi->ipu->dev, "CSI_CCIR_CODE_1:   %08x\n",
		ipu_csi_read(csi, CSI_CCIR_CODE_1));
	dev_dbg(csi->ipu->dev, "CSI_CCIR_CODE_2:   %08x\n",
		ipu_csi_read(csi, CSI_CCIR_CODE_2));
	dev_dbg(csi->ipu->dev, "CSI_CCIR_CODE_3:   %08x\n",
		ipu_csi_read(csi, CSI_CCIR_CODE_3));
	dev_dbg(csi->ipu->dev, "CSI_MIPI_DI:       %08x\n",
		ipu_csi_read(csi, CSI_MIPI_DI));
	dev_dbg(csi->ipu->dev, "CSI_SKIP:          %08x\n",
		ipu_csi_read(csi, CSI_SKIP));
}
EXPORT_SYMBOL_GPL(ipu_csi_dump);
