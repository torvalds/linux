/*
 * ispccp2.c
 *
 * TI OMAP3 ISP - CCP2 module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>

#include "isp.h"
#include "ispreg.h"
#include "ispccp2.h"

/* Number of LCX channels */
#define CCP2_LCx_CHANS_NUM			3
/* Max/Min size for CCP2 video port */
#define ISPCCP2_DAT_START_MIN			0
#define ISPCCP2_DAT_START_MAX			4095
#define ISPCCP2_DAT_SIZE_MIN			0
#define ISPCCP2_DAT_SIZE_MAX			4095
#define ISPCCP2_VPCLK_FRACDIV			65536
#define ISPCCP2_LCx_CTRL_FORMAT_RAW8_DPCM10_VP	0x12
#define ISPCCP2_LCx_CTRL_FORMAT_RAW10_VP	0x16
/* Max/Min size for CCP2 memory channel */
#define ISPCCP2_LCM_HSIZE_COUNT_MIN		16
#define ISPCCP2_LCM_HSIZE_COUNT_MAX		8191
#define ISPCCP2_LCM_HSIZE_SKIP_MIN		0
#define ISPCCP2_LCM_HSIZE_SKIP_MAX		8191
#define ISPCCP2_LCM_VSIZE_MIN			1
#define ISPCCP2_LCM_VSIZE_MAX			8191
#define ISPCCP2_LCM_HWORDS_MIN			1
#define ISPCCP2_LCM_HWORDS_MAX			4095
#define ISPCCP2_LCM_CTRL_BURST_SIZE_32X		5
#define ISPCCP2_LCM_CTRL_READ_THROTTLE_FULL	0
#define ISPCCP2_LCM_CTRL_SRC_DECOMPR_DPCM10	2
#define ISPCCP2_LCM_CTRL_SRC_FORMAT_RAW8	2
#define ISPCCP2_LCM_CTRL_SRC_FORMAT_RAW10	3
#define ISPCCP2_LCM_CTRL_DST_FORMAT_RAW10	3
#define ISPCCP2_LCM_CTRL_DST_PORT_VP		0
#define ISPCCP2_LCM_CTRL_DST_PORT_MEM		1

/* Set only the required bits */
#define BIT_SET(var, shift, mask, val)			\
	do {						\
		var = ((var) & ~((mask) << (shift)))	\
			| ((val) << (shift));		\
	} while (0)

/*
 * ccp2_print_status - Print current CCP2 module register values.
 */
#define CCP2_PRINT_REGISTER(isp, name)\
	dev_dbg(isp->dev, "###CCP2 " #name "=0x%08x\n", \
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_##name))

static void ccp2_print_status(struct isp_ccp2_device *ccp2)
{
	struct isp_device *isp = to_isp_device(ccp2);

	dev_dbg(isp->dev, "-------------CCP2 Register dump-------------\n");

	CCP2_PRINT_REGISTER(isp, SYSCONFIG);
	CCP2_PRINT_REGISTER(isp, SYSSTATUS);
	CCP2_PRINT_REGISTER(isp, LC01_IRQENABLE);
	CCP2_PRINT_REGISTER(isp, LC01_IRQSTATUS);
	CCP2_PRINT_REGISTER(isp, LC23_IRQENABLE);
	CCP2_PRINT_REGISTER(isp, LC23_IRQSTATUS);
	CCP2_PRINT_REGISTER(isp, LCM_IRQENABLE);
	CCP2_PRINT_REGISTER(isp, LCM_IRQSTATUS);
	CCP2_PRINT_REGISTER(isp, CTRL);
	CCP2_PRINT_REGISTER(isp, LCx_CTRL(0));
	CCP2_PRINT_REGISTER(isp, LCx_CODE(0));
	CCP2_PRINT_REGISTER(isp, LCx_STAT_START(0));
	CCP2_PRINT_REGISTER(isp, LCx_STAT_SIZE(0));
	CCP2_PRINT_REGISTER(isp, LCx_SOF_ADDR(0));
	CCP2_PRINT_REGISTER(isp, LCx_EOF_ADDR(0));
	CCP2_PRINT_REGISTER(isp, LCx_DAT_START(0));
	CCP2_PRINT_REGISTER(isp, LCx_DAT_SIZE(0));
	CCP2_PRINT_REGISTER(isp, LCx_DAT_PING_ADDR(0));
	CCP2_PRINT_REGISTER(isp, LCx_DAT_PONG_ADDR(0));
	CCP2_PRINT_REGISTER(isp, LCx_DAT_OFST(0));
	CCP2_PRINT_REGISTER(isp, LCM_CTRL);
	CCP2_PRINT_REGISTER(isp, LCM_VSIZE);
	CCP2_PRINT_REGISTER(isp, LCM_HSIZE);
	CCP2_PRINT_REGISTER(isp, LCM_PREFETCH);
	CCP2_PRINT_REGISTER(isp, LCM_SRC_ADDR);
	CCP2_PRINT_REGISTER(isp, LCM_SRC_OFST);
	CCP2_PRINT_REGISTER(isp, LCM_DST_ADDR);
	CCP2_PRINT_REGISTER(isp, LCM_DST_OFST);

	dev_dbg(isp->dev, "--------------------------------------------\n");
}

/*
 * ccp2_reset - Reset the CCP2
 * @ccp2: pointer to ISP CCP2 device
 */
static void ccp2_reset(struct isp_ccp2_device *ccp2)
{
	struct isp_device *isp = to_isp_device(ccp2);
	int i = 0;

	/* Reset the CSI1/CCP2B and wait for reset to complete */
	isp_reg_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_SYSCONFIG,
		    ISPCCP2_SYSCONFIG_SOFT_RESET);
	while (!(isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_SYSSTATUS) &
		 ISPCCP2_SYSSTATUS_RESET_DONE)) {
		udelay(10);
		if (i++ > 10) {  /* try read 10 times */
			dev_warn(isp->dev,
				"omap3_isp: timeout waiting for ccp2 reset\n");
			break;
		}
	}
}

/*
 * ccp2_pwr_cfg - Configure the power mode settings
 * @ccp2: pointer to ISP CCP2 device
 */
static void ccp2_pwr_cfg(struct isp_ccp2_device *ccp2)
{
	struct isp_device *isp = to_isp_device(ccp2);

	isp_reg_writel(isp, ISPCCP2_SYSCONFIG_MSTANDBY_MODE_SMART |
			((isp->revision == ISP_REVISION_15_0 && isp->autoidle) ?
			  ISPCCP2_SYSCONFIG_AUTO_IDLE : 0),
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_SYSCONFIG);
}

/*
 * ccp2_if_enable - Enable CCP2 interface.
 * @ccp2: pointer to ISP CCP2 device
 * @enable: enable/disable flag
 */
static void ccp2_if_enable(struct isp_ccp2_device *ccp2, u8 enable)
{
	struct isp_device *isp = to_isp_device(ccp2);
	int i;

	if (enable && ccp2->vdds_csib)
		regulator_enable(ccp2->vdds_csib);

	/* Enable/Disable all the LCx channels */
	for (i = 0; i < CCP2_LCx_CHANS_NUM; i++)
		isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCx_CTRL(i),
				ISPCCP2_LCx_CTRL_CHAN_EN,
				enable ? ISPCCP2_LCx_CTRL_CHAN_EN : 0);

	/* Enable/Disable ccp2 interface in ccp2 mode */
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL,
			ISPCCP2_CTRL_MODE | ISPCCP2_CTRL_IF_EN,
			enable ? (ISPCCP2_CTRL_MODE | ISPCCP2_CTRL_IF_EN) : 0);

	if (!enable && ccp2->vdds_csib)
		regulator_disable(ccp2->vdds_csib);
}

/*
 * ccp2_mem_enable - Enable CCP2 memory interface.
 * @ccp2: pointer to ISP CCP2 device
 * @enable: enable/disable flag
 */
static void ccp2_mem_enable(struct isp_ccp2_device *ccp2, u8 enable)
{
	struct isp_device *isp = to_isp_device(ccp2);

	if (enable)
		ccp2_if_enable(ccp2, 0);

	/* Enable/Disable ccp2 interface in ccp2 mode */
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL,
			ISPCCP2_CTRL_MODE, enable ? ISPCCP2_CTRL_MODE : 0);

	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_CTRL,
			ISPCCP2_LCM_CTRL_CHAN_EN,
			enable ? ISPCCP2_LCM_CTRL_CHAN_EN : 0);
}

/*
 * ccp2_phyif_config - Initialize CCP2 phy interface config
 * @ccp2: Pointer to ISP CCP2 device
 * @config: CCP2 platform data
 *
 * Configure the CCP2 physical interface module from platform data.
 *
 * Returns -EIO if strobe is chosen in CSI1 mode, or 0 on success.
 */
static int ccp2_phyif_config(struct isp_ccp2_device *ccp2,
			     const struct isp_ccp2_platform_data *pdata)
{
	struct isp_device *isp = to_isp_device(ccp2);
	u32 val;

	/* CCP2B mode */
	val = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL) |
			    ISPCCP2_CTRL_IO_OUT_SEL | ISPCCP2_CTRL_MODE;
	/* Data/strobe physical layer */
	BIT_SET(val, ISPCCP2_CTRL_PHY_SEL_SHIFT, ISPCCP2_CTRL_PHY_SEL_MASK,
		pdata->phy_layer);
	BIT_SET(val, ISPCCP2_CTRL_INV_SHIFT, ISPCCP2_CTRL_INV_MASK,
		pdata->strobe_clk_pol);
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL);

	val = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL);
	if (!(val & ISPCCP2_CTRL_MODE)) {
		if (pdata->ccp2_mode == ISP_CCP2_MODE_CCP2)
			dev_warn(isp->dev, "OMAP3 CCP2 bus not available\n");
		if (pdata->phy_layer == ISP_CCP2_PHY_DATA_STROBE)
			/* Strobe mode requires CCP2 */
			return -EIO;
	}

	return 0;
}

/*
 * ccp2_vp_config - Initialize CCP2 video port interface.
 * @ccp2: Pointer to ISP CCP2 device
 * @vpclk_div: Video port divisor
 *
 * Configure the CCP2 video port with the given clock divisor. The valid divisor
 * values depend on the ISP revision:
 *
 * - revision 1.0 and 2.0	1 to 4
 * - revision 15.0		1 to 65536
 *
 * The exact divisor value used might differ from the requested value, as ISP
 * revision 15.0 represent the divisor by 65536 divided by an integer.
 */
static void ccp2_vp_config(struct isp_ccp2_device *ccp2,
			   unsigned int vpclk_div)
{
	struct isp_device *isp = to_isp_device(ccp2);
	u32 val;

	/* ISPCCP2_CTRL Video port */
	val = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL);
	val |= ISPCCP2_CTRL_VP_ONLY_EN;	/* Disable the memory write port */

	if (isp->revision == ISP_REVISION_15_0) {
		vpclk_div = clamp_t(unsigned int, vpclk_div, 1, 65536);
		vpclk_div = min(ISPCCP2_VPCLK_FRACDIV / vpclk_div, 65535U);
		BIT_SET(val, ISPCCP2_CTRL_VPCLK_DIV_SHIFT,
			ISPCCP2_CTRL_VPCLK_DIV_MASK, vpclk_div);
	} else {
		vpclk_div = clamp_t(unsigned int, vpclk_div, 1, 4);
		BIT_SET(val, ISPCCP2_CTRL_VP_OUT_CTRL_SHIFT,
			ISPCCP2_CTRL_VP_OUT_CTRL_MASK, vpclk_div - 1);
	}

	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL);
}

/*
 * ccp2_lcx_config - Initialize CCP2 logical channel interface.
 * @ccp2: Pointer to ISP CCP2 device
 * @config: Pointer to ISP LCx config structure.
 *
 * This will analyze the parameters passed by the interface config
 * and configure CSI1/CCP2 logical channel
 *
 */
static void ccp2_lcx_config(struct isp_ccp2_device *ccp2,
			    struct isp_interface_lcx_config *config)
{
	struct isp_device *isp = to_isp_device(ccp2);
	u32 val, format;

	switch (config->format) {
	case V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8:
		format = ISPCCP2_LCx_CTRL_FORMAT_RAW8_DPCM10_VP;
		break;
	case V4L2_MBUS_FMT_SGRBG10_1X10:
	default:
		format = ISPCCP2_LCx_CTRL_FORMAT_RAW10_VP;	/* RAW10+VP */
		break;
	}
	/* ISPCCP2_LCx_CTRL logical channel #0 */
	val = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCx_CTRL(0))
			    | (ISPCCP2_LCx_CTRL_REGION_EN); /* Region */

	if (isp->revision == ISP_REVISION_15_0) {
		/* CRC */
		BIT_SET(val, ISPCCP2_LCx_CTRL_CRC_SHIFT_15_0,
			ISPCCP2_LCx_CTRL_CRC_MASK,
			config->crc);
		/* Format = RAW10+VP or RAW8+DPCM10+VP*/
		BIT_SET(val, ISPCCP2_LCx_CTRL_FORMAT_SHIFT_15_0,
			ISPCCP2_LCx_CTRL_FORMAT_MASK_15_0, format);
	} else {
		BIT_SET(val, ISPCCP2_LCx_CTRL_CRC_SHIFT,
			ISPCCP2_LCx_CTRL_CRC_MASK,
			config->crc);

		BIT_SET(val, ISPCCP2_LCx_CTRL_FORMAT_SHIFT,
			ISPCCP2_LCx_CTRL_FORMAT_MASK, format);
	}
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCx_CTRL(0));

	/* ISPCCP2_DAT_START for logical channel #0 */
	isp_reg_writel(isp, config->data_start << ISPCCP2_LCx_DAT_SHIFT,
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCx_DAT_START(0));

	/* ISPCCP2_DAT_SIZE for logical channel #0 */
	isp_reg_writel(isp, config->data_size << ISPCCP2_LCx_DAT_SHIFT,
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCx_DAT_SIZE(0));

	/* Enable error IRQs for logical channel #0 */
	val = ISPCCP2_LC01_IRQSTATUS_LC0_FIFO_OVF_IRQ |
	      ISPCCP2_LC01_IRQSTATUS_LC0_CRC_IRQ |
	      ISPCCP2_LC01_IRQSTATUS_LC0_FSP_IRQ |
	      ISPCCP2_LC01_IRQSTATUS_LC0_FW_IRQ |
	      ISPCCP2_LC01_IRQSTATUS_LC0_FSC_IRQ |
	      ISPCCP2_LC01_IRQSTATUS_LC0_SSC_IRQ;

	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LC01_IRQSTATUS);
	isp_reg_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LC01_IRQENABLE, val);
}

/*
 * ccp2_if_configure - Configure ccp2 with data from sensor
 * @ccp2: Pointer to ISP CCP2 device
 *
 * Return 0 on success or a negative error code
 */
static int ccp2_if_configure(struct isp_ccp2_device *ccp2)
{
	const struct isp_v4l2_subdevs_group *pdata;
	struct v4l2_mbus_framefmt *format;
	struct media_pad *pad;
	struct v4l2_subdev *sensor;
	u32 lines = 0;
	int ret;

	ccp2_pwr_cfg(ccp2);

	pad = media_entity_remote_source(&ccp2->pads[CCP2_PAD_SINK]);
	sensor = media_entity_to_v4l2_subdev(pad->entity);
	pdata = sensor->host_priv;

	ret = ccp2_phyif_config(ccp2, &pdata->bus.ccp2);
	if (ret < 0)
		return ret;

	ccp2_vp_config(ccp2, pdata->bus.ccp2.vpclk_div + 1);

	v4l2_subdev_call(sensor, sensor, g_skip_top_lines, &lines);

	format = &ccp2->formats[CCP2_PAD_SINK];

	ccp2->if_cfg.data_start = lines;
	ccp2->if_cfg.crc = pdata->bus.ccp2.crc;
	ccp2->if_cfg.format = format->code;
	ccp2->if_cfg.data_size = format->height;

	ccp2_lcx_config(ccp2, &ccp2->if_cfg);

	return 0;
}

static int ccp2_adjust_bandwidth(struct isp_ccp2_device *ccp2)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&ccp2->subdev.entity);
	struct isp_device *isp = to_isp_device(ccp2);
	const struct v4l2_mbus_framefmt *ofmt = &ccp2->formats[CCP2_PAD_SOURCE];
	unsigned long l3_ick = pipe->l3_ick;
	struct v4l2_fract *timeperframe;
	unsigned int vpclk_div = 2;
	unsigned int value;
	u64 bound;
	u64 area;

	/* Compute the minimum clock divisor, based on the pipeline maximum
	 * data rate. This is an absolute lower bound if we don't want SBL
	 * overflows, so round the value up.
	 */
	vpclk_div = max_t(unsigned int, DIV_ROUND_UP(l3_ick, pipe->max_rate),
			  vpclk_div);

	/* Compute the maximum clock divisor, based on the requested frame rate.
	 * This is a soft lower bound to achieve a frame rate equal or higher
	 * than the requested value, so round the value down.
	 */
	timeperframe = &pipe->max_timeperframe;

	if (timeperframe->numerator) {
		area = ofmt->width * ofmt->height;
		bound = div_u64(area * timeperframe->denominator,
				timeperframe->numerator);
		value = min_t(u64, bound, l3_ick);
		vpclk_div = max_t(unsigned int, l3_ick / value, vpclk_div);
	}

	dev_dbg(isp->dev, "%s: minimum clock divisor = %u\n", __func__,
		vpclk_div);

	return vpclk_div;
}

/*
 * ccp2_mem_configure - Initialize CCP2 memory input/output interface
 * @ccp2: Pointer to ISP CCP2 device
 * @config: Pointer to ISP mem interface config structure
 *
 * This will analyze the parameters passed by the interface config
 * structure, and configure the respective registers for proper
 * CSI1/CCP2 memory input.
 */
static void ccp2_mem_configure(struct isp_ccp2_device *ccp2,
			       struct isp_interface_mem_config *config)
{
	struct isp_device *isp = to_isp_device(ccp2);
	u32 sink_pixcode = ccp2->formats[CCP2_PAD_SINK].code;
	u32 source_pixcode = ccp2->formats[CCP2_PAD_SOURCE].code;
	unsigned int dpcm_decompress = 0;
	u32 val, hwords;

	if (sink_pixcode != source_pixcode &&
	    sink_pixcode == V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8)
		dpcm_decompress = 1;

	ccp2_pwr_cfg(ccp2);

	/* Hsize, Skip */
	isp_reg_writel(isp, ISPCCP2_LCM_HSIZE_SKIP_MIN |
		       (config->hsize_count << ISPCCP2_LCM_HSIZE_SHIFT),
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_HSIZE);

	/* Vsize, no. of lines */
	isp_reg_writel(isp, config->vsize_count << ISPCCP2_LCM_VSIZE_SHIFT,
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_VSIZE);

	if (ccp2->video_in.bpl_padding == 0)
		config->src_ofst = 0;
	else
		config->src_ofst = ccp2->video_in.bpl_value;

	isp_reg_writel(isp, config->src_ofst, OMAP3_ISP_IOMEM_CCP2,
		       ISPCCP2_LCM_SRC_OFST);

	/* Source and Destination formats */
	val = ISPCCP2_LCM_CTRL_DST_FORMAT_RAW10 <<
	      ISPCCP2_LCM_CTRL_DST_FORMAT_SHIFT;

	if (dpcm_decompress) {
		/* source format is RAW8 */
		val |= ISPCCP2_LCM_CTRL_SRC_FORMAT_RAW8 <<
		       ISPCCP2_LCM_CTRL_SRC_FORMAT_SHIFT;

		/* RAW8 + DPCM10 - simple predictor */
		val |= ISPCCP2_LCM_CTRL_SRC_DPCM_PRED;

		/* enable source DPCM decompression */
		val |= ISPCCP2_LCM_CTRL_SRC_DECOMPR_DPCM10 <<
		       ISPCCP2_LCM_CTRL_SRC_DECOMPR_SHIFT;
	} else {
		/* source format is RAW10 */
		val |= ISPCCP2_LCM_CTRL_SRC_FORMAT_RAW10 <<
		       ISPCCP2_LCM_CTRL_SRC_FORMAT_SHIFT;
	}

	/* Burst size to 32x64 */
	val |= ISPCCP2_LCM_CTRL_BURST_SIZE_32X <<
	       ISPCCP2_LCM_CTRL_BURST_SIZE_SHIFT;

	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_CTRL);

	/* Prefetch setup */
	if (dpcm_decompress)
		hwords = (ISPCCP2_LCM_HSIZE_SKIP_MIN +
			  config->hsize_count) >> 3;
	else
		hwords = (ISPCCP2_LCM_HSIZE_SKIP_MIN +
			  config->hsize_count) >> 2;

	isp_reg_writel(isp, hwords << ISPCCP2_LCM_PREFETCH_SHIFT,
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_PREFETCH);

	/* Video port */
	isp_reg_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_CTRL,
		    ISPCCP2_CTRL_IO_OUT_SEL | ISPCCP2_CTRL_MODE);
	ccp2_vp_config(ccp2, ccp2_adjust_bandwidth(ccp2));

	/* Clear LCM interrupts */
	isp_reg_writel(isp, ISPCCP2_LCM_IRQSTATUS_OCPERROR_IRQ |
		       ISPCCP2_LCM_IRQSTATUS_EOF_IRQ,
		       OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_IRQSTATUS);

	/* Enable LCM interupts */
	isp_reg_set(isp, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_IRQENABLE,
		    ISPCCP2_LCM_IRQSTATUS_EOF_IRQ |
		    ISPCCP2_LCM_IRQSTATUS_OCPERROR_IRQ);
}

/*
 * ccp2_set_inaddr - Sets memory address of input frame.
 * @ccp2: Pointer to ISP CCP2 device
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Configures the memory address from which the input frame is to be read.
 */
static void ccp2_set_inaddr(struct isp_ccp2_device *ccp2, u32 addr)
{
	struct isp_device *isp = to_isp_device(ccp2);

	isp_reg_writel(isp, addr, OMAP3_ISP_IOMEM_CCP2, ISPCCP2_LCM_SRC_ADDR);
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void ccp2_isr_buffer(struct isp_ccp2_device *ccp2)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&ccp2->subdev.entity);
	struct isp_buffer *buffer;

	buffer = omap3isp_video_buffer_next(&ccp2->video_in);
	if (buffer != NULL)
		ccp2_set_inaddr(ccp2, buffer->isp_addr);

	pipe->state |= ISP_PIPELINE_IDLE_INPUT;

	if (ccp2->state == ISP_PIPELINE_STREAM_SINGLESHOT) {
		if (isp_pipeline_ready(pipe))
			omap3isp_pipeline_set_stream(pipe,
						ISP_PIPELINE_STREAM_SINGLESHOT);
	}
}

/*
 * omap3isp_ccp2_isr - Handle ISP CCP2 interrupts
 * @ccp2: Pointer to ISP CCP2 device
 *
 * This will handle the CCP2 interrupts
 */
void omap3isp_ccp2_isr(struct isp_ccp2_device *ccp2)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&ccp2->subdev.entity);
	struct isp_device *isp = to_isp_device(ccp2);
	static const u32 ISPCCP2_LC01_ERROR =
		ISPCCP2_LC01_IRQSTATUS_LC0_FIFO_OVF_IRQ |
		ISPCCP2_LC01_IRQSTATUS_LC0_CRC_IRQ |
		ISPCCP2_LC01_IRQSTATUS_LC0_FSP_IRQ |
		ISPCCP2_LC01_IRQSTATUS_LC0_FW_IRQ |
		ISPCCP2_LC01_IRQSTATUS_LC0_FSC_IRQ |
		ISPCCP2_LC01_IRQSTATUS_LC0_SSC_IRQ;
	u32 lcx_irqstatus, lcm_irqstatus;

	/* First clear the interrupts */
	lcx_irqstatus = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2,
				      ISPCCP2_LC01_IRQSTATUS);
	isp_reg_writel(isp, lcx_irqstatus, OMAP3_ISP_IOMEM_CCP2,
		       ISPCCP2_LC01_IRQSTATUS);

	lcm_irqstatus = isp_reg_readl(isp, OMAP3_ISP_IOMEM_CCP2,
				      ISPCCP2_LCM_IRQSTATUS);
	isp_reg_writel(isp, lcm_irqstatus, OMAP3_ISP_IOMEM_CCP2,
		       ISPCCP2_LCM_IRQSTATUS);
	/* Errors */
	if (lcx_irqstatus & ISPCCP2_LC01_ERROR) {
		pipe->error = true;
		dev_dbg(isp->dev, "CCP2 err:%x\n", lcx_irqstatus);
		return;
	}

	if (lcm_irqstatus & ISPCCP2_LCM_IRQSTATUS_OCPERROR_IRQ) {
		pipe->error = true;
		dev_dbg(isp->dev, "CCP2 OCP err:%x\n", lcm_irqstatus);
	}

	if (omap3isp_module_sync_is_stopping(&ccp2->wait, &ccp2->stopping))
		return;

	/* Handle queued buffers on frame end interrupts */
	if (lcm_irqstatus & ISPCCP2_LCM_IRQSTATUS_EOF_IRQ)
		ccp2_isr_buffer(ccp2);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static const unsigned int ccp2_fmts[] = {
	V4L2_MBUS_FMT_SGRBG10_1X10,
	V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8,
};

/*
 * __ccp2_get_format - helper function for getting ccp2 format
 * @ccp2  : Pointer to ISP CCP2 device
 * @fh    : V4L2 subdev file handle
 * @pad   : pad number
 * @which : wanted subdev format
 * return format structure or NULL on error
 */
static struct v4l2_mbus_framefmt *
__ccp2_get_format(struct isp_ccp2_device *ccp2, struct v4l2_subdev_fh *fh,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &ccp2->formats[pad];
}

/*
 * ccp2_try_format - Handle try format by pad subdev method
 * @ccp2  : Pointer to ISP CCP2 device
 * @fh    : V4L2 subdev file handle
 * @pad   : pad num
 * @fmt   : pointer to v4l2 mbus format structure
 * @which : wanted subdev format
 */
static void ccp2_try_format(struct isp_ccp2_device *ccp2,
			       struct v4l2_subdev_fh *fh, unsigned int pad,
			       struct v4l2_mbus_framefmt *fmt,
			       enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *format;

	switch (pad) {
	case CCP2_PAD_SINK:
		if (fmt->code != V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8)
			fmt->code = V4L2_MBUS_FMT_SGRBG10_1X10;

		if (ccp2->input == CCP2_INPUT_SENSOR) {
			fmt->width = clamp_t(u32, fmt->width,
					     ISPCCP2_DAT_START_MIN,
					     ISPCCP2_DAT_START_MAX);
			fmt->height = clamp_t(u32, fmt->height,
					      ISPCCP2_DAT_SIZE_MIN,
					      ISPCCP2_DAT_SIZE_MAX);
		} else if (ccp2->input == CCP2_INPUT_MEMORY) {
			fmt->width = clamp_t(u32, fmt->width,
					     ISPCCP2_LCM_HSIZE_COUNT_MIN,
					     ISPCCP2_LCM_HSIZE_COUNT_MAX);
			fmt->height = clamp_t(u32, fmt->height,
					      ISPCCP2_LCM_VSIZE_MIN,
					      ISPCCP2_LCM_VSIZE_MAX);
		}
		break;

	case CCP2_PAD_SOURCE:
		/* Source format - copy sink format and change pixel code
		 * to SGRBG10_1X10 as we don't support CCP2 write to memory.
		 * When CCP2 write to memory feature will be added this
		 * should be changed properly.
		 */
		format = __ccp2_get_format(ccp2, fh, CCP2_PAD_SINK, which);
		memcpy(fmt, format, sizeof(*fmt));
		fmt->code = V4L2_MBUS_FMT_SGRBG10_1X10;
		break;
	}

	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

/*
 * ccp2_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @fh     : V4L2 subdev file handle
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int ccp2_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (code->pad == CCP2_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(ccp2_fmts))
			return -EINVAL;

		code->code = ccp2_fmts[code->index];
	} else {
		if (code->index != 0)
			return -EINVAL;

		format = __ccp2_get_format(ccp2, fh, CCP2_PAD_SINK,
					      V4L2_SUBDEV_FORMAT_TRY);
		code->code = format->code;
	}

	return 0;
}

static int ccp2_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	ccp2_try_format(ccp2, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	ccp2_try_format(ccp2, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * ccp2_get_format - Handle get format by pads subdev method
 * @sd    : pointer to v4l2 subdev structure
 * @fh    : V4L2 subdev file handle
 * @fmt   : pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int ccp2_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ccp2_get_format(ccp2, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * ccp2_set_format - Handle set format by pads subdev method
 * @sd    : pointer to v4l2 subdev structure
 * @fh    : V4L2 subdev file handle
 * @fmt   : pointer to v4l2 subdev format structure
 * returns zero
 */
static int ccp2_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ccp2_get_format(ccp2, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	ccp2_try_format(ccp2, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == CCP2_PAD_SINK) {
		format = __ccp2_get_format(ccp2, fh, CCP2_PAD_SOURCE,
					   fmt->which);
		*format = fmt->format;
		ccp2_try_format(ccp2, fh, CCP2_PAD_SOURCE, format, fmt->which);
	}

	return 0;
}

/*
 * ccp2_init_formats - Initialize formats on all pads
 * @sd: ISP CCP2 V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int ccp2_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = CCP2_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SGRBG10_1X10;
	format.format.width = 4096;
	format.format.height = 4096;
	ccp2_set_format(sd, fh, &format);

	return 0;
}

/*
 * ccp2_s_stream - Enable/Disable streaming on ccp2 subdev
 * @sd    : pointer to v4l2 subdev structure
 * @enable: 1 == Enable, 0 == Disable
 * return zero
 */
static int ccp2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);
	struct isp_device *isp = to_isp_device(ccp2);
	struct device *dev = to_device(ccp2);
	int ret;

	if (ccp2->state == ISP_PIPELINE_STREAM_STOPPED) {
		if (enable == ISP_PIPELINE_STREAM_STOPPED)
			return 0;
		atomic_set(&ccp2->stopping, 0);
	}

	switch (enable) {
	case ISP_PIPELINE_STREAM_CONTINUOUS:
		if (ccp2->phy) {
			ret = omap3isp_csiphy_acquire(ccp2->phy);
			if (ret < 0)
				return ret;
		}

		ccp2_if_configure(ccp2);
		ccp2_print_status(ccp2);

		/* Enable CSI1/CCP2 interface */
		ccp2_if_enable(ccp2, 1);
		break;

	case ISP_PIPELINE_STREAM_SINGLESHOT:
		if (ccp2->state != ISP_PIPELINE_STREAM_SINGLESHOT) {
			struct v4l2_mbus_framefmt *format;

			format = &ccp2->formats[CCP2_PAD_SINK];

			ccp2->mem_cfg.hsize_count = format->width;
			ccp2->mem_cfg.vsize_count = format->height;
			ccp2->mem_cfg.src_ofst = 0;

			ccp2_mem_configure(ccp2, &ccp2->mem_cfg);
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_CSI1_READ);
			ccp2_print_status(ccp2);
		}
		ccp2_mem_enable(ccp2, 1);
		break;

	case ISP_PIPELINE_STREAM_STOPPED:
		if (omap3isp_module_sync_idle(&sd->entity, &ccp2->wait,
					      &ccp2->stopping))
			dev_dbg(dev, "%s: module stop timeout.\n", sd->name);
		if (ccp2->input == CCP2_INPUT_MEMORY) {
			ccp2_mem_enable(ccp2, 0);
			omap3isp_sbl_disable(isp, OMAP3_ISP_SBL_CSI1_READ);
		} else if (ccp2->input == CCP2_INPUT_SENSOR) {
			/* Disable CSI1/CCP2 interface */
			ccp2_if_enable(ccp2, 0);
			if (ccp2->phy)
				omap3isp_csiphy_release(ccp2->phy);
		}
		break;
	}

	ccp2->state = enable;
	return 0;
}

/* subdev video operations */
static const struct v4l2_subdev_video_ops ccp2_sd_video_ops = {
	.s_stream = ccp2_s_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops ccp2_sd_pad_ops = {
	.enum_mbus_code = ccp2_enum_mbus_code,
	.enum_frame_size = ccp2_enum_frame_size,
	.get_fmt = ccp2_get_format,
	.set_fmt = ccp2_set_format,
};

/* subdev operations */
static const struct v4l2_subdev_ops ccp2_sd_ops = {
	.video = &ccp2_sd_video_ops,
	.pad = &ccp2_sd_pad_ops,
};

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops ccp2_sd_internal_ops = {
	.open = ccp2_init_formats,
};

/* --------------------------------------------------------------------------
 * ISP ccp2 video device node
 */

/*
 * ccp2_video_queue - Queue video buffer.
 * @video : Pointer to isp video structure
 * @buffer: Pointer to isp_buffer structure
 * return -EIO or zero on success
 */
static int ccp2_video_queue(struct isp_video *video, struct isp_buffer *buffer)
{
	struct isp_ccp2_device *ccp2 = &video->isp->isp_ccp2;

	ccp2_set_inaddr(ccp2, buffer->isp_addr);
	return 0;
}

static const struct isp_video_operations ccp2_video_ops = {
	.queue = ccp2_video_queue,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * ccp2_link_setup - Setup ccp2 connections.
 * @entity : Pointer to media entity structure
 * @local  : Pointer to local pad array
 * @remote : Pointer to remote pad array
 * @flags  : Link flags
 * return -EINVAL on error or zero on success
 */
static int ccp2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_ccp2_device *ccp2 = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case CCP2_PAD_SINK | MEDIA_ENT_T_DEVNODE:
		/* read from memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (ccp2->input == CCP2_INPUT_SENSOR)
				return -EBUSY;
			ccp2->input = CCP2_INPUT_MEMORY;
		} else {
			if (ccp2->input == CCP2_INPUT_MEMORY)
				ccp2->input = CCP2_INPUT_NONE;
		}
		break;

	case CCP2_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* read from sensor/phy */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (ccp2->input == CCP2_INPUT_MEMORY)
				return -EBUSY;
			ccp2->input = CCP2_INPUT_SENSOR;
		} else {
			if (ccp2->input == CCP2_INPUT_SENSOR)
				ccp2->input = CCP2_INPUT_NONE;
		} break;

	case CCP2_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		/* write to video port/ccdc */
		if (flags & MEDIA_LNK_FL_ENABLED)
			ccp2->output = CCP2_OUTPUT_CCDC;
		else
			ccp2->output = CCP2_OUTPUT_NONE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* media operations */
static const struct media_entity_operations ccp2_media_ops = {
	.link_setup = ccp2_link_setup,
};

/*
 * omap3isp_ccp2_unregister_entities - Unregister media entities: subdev
 * @ccp2: Pointer to ISP CCP2 device
 */
void omap3isp_ccp2_unregister_entities(struct isp_ccp2_device *ccp2)
{
	v4l2_device_unregister_subdev(&ccp2->subdev);
	omap3isp_video_unregister(&ccp2->video_in);
}

/*
 * omap3isp_ccp2_register_entities - Register the subdev media entity
 * @ccp2: Pointer to ISP CCP2 device
 * @vdev: Pointer to v4l device
 * return negative error code or zero on success
 */

int omap3isp_ccp2_register_entities(struct isp_ccp2_device *ccp2,
				    struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &ccp2->subdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&ccp2->video_in, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap3isp_ccp2_unregister_entities(ccp2);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP ccp2 initialisation and cleanup
 */

/*
 * ccp2_init_entities - Initialize ccp2 subdev and media entity.
 * @ccp2: Pointer to ISP CCP2 device
 * return negative error code or zero on success
 */
static int ccp2_init_entities(struct isp_ccp2_device *ccp2)
{
	struct v4l2_subdev *sd = &ccp2->subdev;
	struct media_pad *pads = ccp2->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	ccp2->input = CCP2_INPUT_NONE;
	ccp2->output = CCP2_OUTPUT_NONE;

	v4l2_subdev_init(sd, &ccp2_sd_ops);
	sd->internal_ops = &ccp2_sd_internal_ops;
	strlcpy(sd->name, "OMAP3 ISP CCP2", sizeof(sd->name));
	sd->grp_id = 1 << 16;   /* group ID for isp subdevs */
	v4l2_set_subdevdata(sd, ccp2);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[CCP2_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[CCP2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	me->ops = &ccp2_media_ops;
	ret = media_entity_init(me, CCP2_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	ccp2_init_formats(sd, NULL);

	/*
	 * The CCP2 has weird line alignment requirements, possibly caused by
	 * DPCM8 decompression. Line length for data read from memory must be a
	 * multiple of 128 bits (16 bytes) in continuous mode (when no padding
	 * is present at end of lines). Additionally, if padding is used, the
	 * padded line length must be a multiple of 32 bytes. To simplify the
	 * implementation we use a fixed 32 bytes alignment regardless of the
	 * input format and width. If strict 128 bits alignment support is
	 * required ispvideo will need to be made aware of this special dual
	 * alignement requirements.
	 */
	ccp2->video_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ccp2->video_in.bpl_alignment = 32;
	ccp2->video_in.bpl_max = 0xffffffe0;
	ccp2->video_in.isp = to_isp_device(ccp2);
	ccp2->video_in.ops = &ccp2_video_ops;
	ccp2->video_in.capture_mem = PAGE_ALIGN(4096 * 4096) * 3;

	ret = omap3isp_video_init(&ccp2->video_in, "CCP2");
	if (ret < 0)
		goto error_video;

	/* Connect the video node to the ccp2 subdev. */
	ret = media_entity_create_link(&ccp2->video_in.video.entity, 0,
				       &ccp2->subdev.entity, CCP2_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap3isp_video_cleanup(&ccp2->video_in);
error_video:
	media_entity_cleanup(&ccp2->subdev.entity);
	return ret;
}

/*
 * omap3isp_ccp2_init - CCP2 initialization.
 * @isp : Pointer to ISP device
 * return negative error code or zero on success
 */
int omap3isp_ccp2_init(struct isp_device *isp)
{
	struct isp_ccp2_device *ccp2 = &isp->isp_ccp2;
	int ret;

	init_waitqueue_head(&ccp2->wait);

	/*
	 * On the OMAP34xx the CSI1 receiver is operated in the CSIb IO
	 * complex, which is powered by vdds_csib power rail. Hence the
	 * request for the regulator.
	 *
	 * On the OMAP36xx, the CCP2 uses the CSI PHY1 or PHY2, shared with
	 * the CSI2c or CSI2a receivers. The PHY then needs to be explicitly
	 * configured.
	 *
	 * TODO: Don't hardcode the usage of PHY1 (shared with CSI2c).
	 */
	if (isp->revision == ISP_REVISION_2_0) {
		ccp2->vdds_csib = regulator_get(isp->dev, "vdds_csib");
		if (IS_ERR(ccp2->vdds_csib)) {
			dev_dbg(isp->dev,
				"Could not get regulator vdds_csib\n");
			ccp2->vdds_csib = NULL;
		}
	} else if (isp->revision == ISP_REVISION_15_0) {
		ccp2->phy = &isp->isp_csiphy1;
	}

	ret = ccp2_init_entities(ccp2);
	if (ret < 0) {
		regulator_put(ccp2->vdds_csib);
		return ret;
	}

	ccp2_reset(ccp2);
	return 0;
}

/*
 * omap3isp_ccp2_cleanup - CCP2 un-initialization
 * @isp : Pointer to ISP device
 */
void omap3isp_ccp2_cleanup(struct isp_device *isp)
{
	struct isp_ccp2_device *ccp2 = &isp->isp_ccp2;

	omap3isp_video_cleanup(&ccp2->video_in);
	media_entity_cleanup(&ccp2->subdev.entity);

	regulator_put(ccp2->vdds_csib);
}
