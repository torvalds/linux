/*
 * ispcsi2.c
 *
 * TI OMAP3 ISP - CSI2 module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
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
#include <media/v4l2-common.h>
#include <linux/v4l2-mediabus.h>
#include <linux/mm.h>

#include "isp.h"
#include "ispreg.h"
#include "ispcsi2.h"

/*
 * csi2_if_enable - Enable CSI2 Receiver interface.
 * @enable: enable flag
 *
 */
static void csi2_if_enable(struct isp_device *isp,
			   struct isp_csi2_device *csi2, u8 enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &csi2->ctrl;

	isp_reg_clr_set(isp, csi2->regs1, ISPCSI2_CTRL, ISPCSI2_CTRL_IF_EN,
			enable ? ISPCSI2_CTRL_IF_EN : 0);

	currctrl->if_enable = enable;
}

/*
 * csi2_recv_config - CSI2 receiver module configuration.
 * @currctrl: isp_csi2_ctrl_cfg structure
 *
 */
static void csi2_recv_config(struct isp_device *isp,
			     struct isp_csi2_device *csi2,
			     struct isp_csi2_ctrl_cfg *currctrl)
{
	u32 reg;

	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTRL);

	if (currctrl->frame_mode)
		reg |= ISPCSI2_CTRL_FRAME;
	else
		reg &= ~ISPCSI2_CTRL_FRAME;

	if (currctrl->vp_clk_enable)
		reg |= ISPCSI2_CTRL_VP_CLK_EN;
	else
		reg &= ~ISPCSI2_CTRL_VP_CLK_EN;

	if (currctrl->vp_only_enable)
		reg |= ISPCSI2_CTRL_VP_ONLY_EN;
	else
		reg &= ~ISPCSI2_CTRL_VP_ONLY_EN;

	reg &= ~ISPCSI2_CTRL_VP_OUT_CTRL_MASK;
	reg |= currctrl->vp_out_ctrl << ISPCSI2_CTRL_VP_OUT_CTRL_SHIFT;

	if (currctrl->ecc_enable)
		reg |= ISPCSI2_CTRL_ECC_EN;
	else
		reg &= ~ISPCSI2_CTRL_ECC_EN;

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_CTRL);
}

static const unsigned int csi2_input_fmts[] = {
	V4L2_MBUS_FMT_SGRBG10_1X10,
	V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8,
	V4L2_MBUS_FMT_SRGGB10_1X10,
	V4L2_MBUS_FMT_SRGGB10_DPCM8_1X8,
	V4L2_MBUS_FMT_SBGGR10_1X10,
	V4L2_MBUS_FMT_SBGGR10_DPCM8_1X8,
	V4L2_MBUS_FMT_SGBRG10_1X10,
	V4L2_MBUS_FMT_SGBRG10_DPCM8_1X8,
};

/* To set the format on the CSI2 requires a mapping function that takes
 * the following inputs:
 * - 2 different formats (at this time)
 * - 2 destinations (mem, vp+mem) (vp only handled separately)
 * - 2 decompression options (on, off)
 * - 2 isp revisions (certain format must be handled differently on OMAP3630)
 * Output should be CSI2 frame format code
 * Array indices as follows: [format][dest][decompr][is_3630]
 * Not all combinations are valid. 0 means invalid.
 */
static const u16 __csi2_fmt_map[2][2][2][2] = {
	/* RAW10 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			{ CSI2_PIX_FMT_RAW10_EXP16, CSI2_PIX_FMT_RAW10_EXP16 },
			/* DPCM decompression */
			{ 0, 0 },
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			{ CSI2_PIX_FMT_RAW10_EXP16_VP,
			  CSI2_PIX_FMT_RAW10_EXP16_VP },
			/* DPCM decompression */
			{ 0, 0 },
		},
	},
	/* RAW10 DPCM8 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			{ CSI2_PIX_FMT_RAW8, CSI2_USERDEF_8BIT_DATA1 },
			/* DPCM decompression */
			{ CSI2_PIX_FMT_RAW8_DPCM10_EXP16,
			  CSI2_USERDEF_8BIT_DATA1_DPCM10 },
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			{ CSI2_PIX_FMT_RAW8_VP,
			  CSI2_PIX_FMT_RAW8_VP },
			/* DPCM decompression */
			{ CSI2_PIX_FMT_RAW8_DPCM10_VP,
			  CSI2_USERDEF_8BIT_DATA1_DPCM10_VP },
		},
	},
};

/*
 * csi2_ctx_map_format - Map CSI2 sink media bus format to CSI2 format ID
 * @csi2: ISP CSI2 device
 *
 * Returns CSI2 physical format id
 */
static u16 csi2_ctx_map_format(struct isp_csi2_device *csi2)
{
	const struct v4l2_mbus_framefmt *fmt = &csi2->formats[CSI2_PAD_SINK];
	int fmtidx, destidx, is_3630;

	switch (fmt->code) {
	case V4L2_MBUS_FMT_SGRBG10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SGBRG10_1X10:
		fmtidx = 0;
		break;
	case V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8:
	case V4L2_MBUS_FMT_SRGGB10_DPCM8_1X8:
	case V4L2_MBUS_FMT_SBGGR10_DPCM8_1X8:
	case V4L2_MBUS_FMT_SGBRG10_DPCM8_1X8:
		fmtidx = 1;
		break;
	default:
		WARN(1, KERN_ERR "CSI2: pixel format %08x unsupported!\n",
		     fmt->code);
		return 0;
	}

	if (!(csi2->output & CSI2_OUTPUT_CCDC) &&
	    !(csi2->output & CSI2_OUTPUT_MEMORY)) {
		/* Neither output enabled is a valid combination */
		return CSI2_PIX_FMT_OTHERS;
	}

	/* If we need to skip frames at the beginning of the stream disable the
	 * video port to avoid sending the skipped frames to the CCDC.
	 */
	destidx = csi2->frame_skip ? 0 : !!(csi2->output & CSI2_OUTPUT_CCDC);
	is_3630 = csi2->isp->revision == ISP_REVISION_15_0;

	return __csi2_fmt_map[fmtidx][destidx][csi2->dpcm_decompress][is_3630];
}

/*
 * csi2_set_outaddr - Set memory address to save output image
 * @csi2: Pointer to ISP CSI2a device.
 * @addr: ISP MMU Mapped 32-bit memory address aligned on 32 byte boundary.
 *
 * Sets the memory address where the output will be saved.
 *
 * Returns 0 if successful, or -EINVAL if the address is not in the 32 byte
 * boundary.
 */
static void csi2_set_outaddr(struct isp_csi2_device *csi2, u32 addr)
{
	struct isp_device *isp = csi2->isp;
	struct isp_csi2_ctx_cfg *ctx = &csi2->contexts[0];

	ctx->ping_addr = addr;
	ctx->pong_addr = addr;
	isp_reg_writel(isp, ctx->ping_addr,
		       csi2->regs1, ISPCSI2_CTX_DAT_PING_ADDR(ctx->ctxnum));
	isp_reg_writel(isp, ctx->pong_addr,
		       csi2->regs1, ISPCSI2_CTX_DAT_PONG_ADDR(ctx->ctxnum));
}

/*
 * is_usr_def_mapping - Checks whether USER_DEF_MAPPING should
 *			be enabled by CSI2.
 * @format_id: mapped format id
 *
 */
static inline int is_usr_def_mapping(u32 format_id)
{
	return (format_id & 0x40) ? 1 : 0;
}

/*
 * csi2_ctx_enable - Enable specified CSI2 context
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @enable: enable
 *
 */
static void csi2_ctx_enable(struct isp_device *isp,
			    struct isp_csi2_device *csi2, u8 ctxnum, u8 enable)
{
	struct isp_csi2_ctx_cfg *ctx = &csi2->contexts[ctxnum];
	unsigned int skip = 0;
	u32 reg;

	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTX_CTRL1(ctxnum));

	if (enable) {
		if (csi2->frame_skip)
			skip = csi2->frame_skip;
		else if (csi2->output & CSI2_OUTPUT_MEMORY)
			skip = 1;

		reg &= ~ISPCSI2_CTX_CTRL1_COUNT_MASK;
		reg |= ISPCSI2_CTX_CTRL1_COUNT_UNLOCK
		    |  (skip << ISPCSI2_CTX_CTRL1_COUNT_SHIFT)
		    |  ISPCSI2_CTX_CTRL1_CTX_EN;
	} else {
		reg &= ~ISPCSI2_CTX_CTRL1_CTX_EN;
	}

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_CTX_CTRL1(ctxnum));
	ctx->enabled = enable;
}

/*
 * csi2_ctx_config - CSI2 context configuration.
 * @ctx: context configuration
 *
 */
static void csi2_ctx_config(struct isp_device *isp,
			    struct isp_csi2_device *csi2,
			    struct isp_csi2_ctx_cfg *ctx)
{
	u32 reg;

	/* Set up CSI2_CTx_CTRL1 */
	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTX_CTRL1(ctx->ctxnum));

	if (ctx->eof_enabled)
		reg |= ISPCSI2_CTX_CTRL1_EOF_EN;
	else
		reg &= ~ISPCSI2_CTX_CTRL1_EOF_EN;

	if (ctx->eol_enabled)
		reg |= ISPCSI2_CTX_CTRL1_EOL_EN;
	else
		reg &= ~ISPCSI2_CTX_CTRL1_EOL_EN;

	if (ctx->checksum_enabled)
		reg |= ISPCSI2_CTX_CTRL1_CS_EN;
	else
		reg &= ~ISPCSI2_CTX_CTRL1_CS_EN;

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_CTX_CTRL1(ctx->ctxnum));

	/* Set up CSI2_CTx_CTRL2 */
	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTX_CTRL2(ctx->ctxnum));

	reg &= ~(ISPCSI2_CTX_CTRL2_VIRTUAL_ID_MASK);
	reg |= ctx->virtual_id << ISPCSI2_CTX_CTRL2_VIRTUAL_ID_SHIFT;

	reg &= ~(ISPCSI2_CTX_CTRL2_FORMAT_MASK);
	reg |= ctx->format_id << ISPCSI2_CTX_CTRL2_FORMAT_SHIFT;

	if (ctx->dpcm_decompress) {
		if (ctx->dpcm_predictor)
			reg |= ISPCSI2_CTX_CTRL2_DPCM_PRED;
		else
			reg &= ~ISPCSI2_CTX_CTRL2_DPCM_PRED;
	}

	if (is_usr_def_mapping(ctx->format_id)) {
		reg &= ~ISPCSI2_CTX_CTRL2_USER_DEF_MAP_MASK;
		reg |= 2 << ISPCSI2_CTX_CTRL2_USER_DEF_MAP_SHIFT;
	}

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_CTX_CTRL2(ctx->ctxnum));

	/* Set up CSI2_CTx_CTRL3 */
	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTX_CTRL3(ctx->ctxnum));
	reg &= ~(ISPCSI2_CTX_CTRL3_ALPHA_MASK);
	reg |= (ctx->alpha << ISPCSI2_CTX_CTRL3_ALPHA_SHIFT);

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_CTX_CTRL3(ctx->ctxnum));

	/* Set up CSI2_CTx_DAT_OFST */
	reg = isp_reg_readl(isp, csi2->regs1,
			    ISPCSI2_CTX_DAT_OFST(ctx->ctxnum));
	reg &= ~ISPCSI2_CTX_DAT_OFST_OFST_MASK;
	reg |= ctx->data_offset << ISPCSI2_CTX_DAT_OFST_OFST_SHIFT;
	isp_reg_writel(isp, reg, csi2->regs1,
		       ISPCSI2_CTX_DAT_OFST(ctx->ctxnum));

	isp_reg_writel(isp, ctx->ping_addr,
		       csi2->regs1, ISPCSI2_CTX_DAT_PING_ADDR(ctx->ctxnum));

	isp_reg_writel(isp, ctx->pong_addr,
		       csi2->regs1, ISPCSI2_CTX_DAT_PONG_ADDR(ctx->ctxnum));
}

/*
 * csi2_timing_config - CSI2 timing configuration.
 * @timing: csi2_timing_cfg structure
 */
static void csi2_timing_config(struct isp_device *isp,
			       struct isp_csi2_device *csi2,
			       struct isp_csi2_timing_cfg *timing)
{
	u32 reg;

	reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_TIMING);

	if (timing->force_rx_mode)
		reg |= ISPCSI2_TIMING_FORCE_RX_MODE_IO(timing->ionum);
	else
		reg &= ~ISPCSI2_TIMING_FORCE_RX_MODE_IO(timing->ionum);

	if (timing->stop_state_16x)
		reg |= ISPCSI2_TIMING_STOP_STATE_X16_IO(timing->ionum);
	else
		reg &= ~ISPCSI2_TIMING_STOP_STATE_X16_IO(timing->ionum);

	if (timing->stop_state_4x)
		reg |= ISPCSI2_TIMING_STOP_STATE_X4_IO(timing->ionum);
	else
		reg &= ~ISPCSI2_TIMING_STOP_STATE_X4_IO(timing->ionum);

	reg &= ~ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_MASK(timing->ionum);
	reg |= timing->stop_state_counter <<
	       ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_SHIFT(timing->ionum);

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_TIMING);
}

/*
 * csi2_irq_ctx_set - Enables CSI2 Context IRQs.
 * @enable: Enable/disable CSI2 Context interrupts
 */
static void csi2_irq_ctx_set(struct isp_device *isp,
			     struct isp_csi2_device *csi2, int enable)
{
	u32 reg = ISPCSI2_CTX_IRQSTATUS_FE_IRQ;
	int i;

	if (csi2->use_fs_irq)
		reg |= ISPCSI2_CTX_IRQSTATUS_FS_IRQ;

	for (i = 0; i < 8; i++) {
		isp_reg_writel(isp, reg, csi2->regs1,
			       ISPCSI2_CTX_IRQSTATUS(i));
		if (enable)
			isp_reg_set(isp, csi2->regs1, ISPCSI2_CTX_IRQENABLE(i),
				    reg);
		else
			isp_reg_clr(isp, csi2->regs1, ISPCSI2_CTX_IRQENABLE(i),
				    reg);
	}
}

/*
 * csi2_irq_complexio1_set - Enables CSI2 ComplexIO IRQs.
 * @enable: Enable/disable CSI2 ComplexIO #1 interrupts
 */
static void csi2_irq_complexio1_set(struct isp_device *isp,
				    struct isp_csi2_device *csi2, int enable)
{
	u32 reg;
	reg = ISPCSI2_PHY_IRQENABLE_STATEALLULPMEXIT |
		ISPCSI2_PHY_IRQENABLE_STATEALLULPMENTER |
		ISPCSI2_PHY_IRQENABLE_STATEULPM5 |
		ISPCSI2_PHY_IRQENABLE_ERRCONTROL5 |
		ISPCSI2_PHY_IRQENABLE_ERRESC5 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTSYNCHS5 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTHS5 |
		ISPCSI2_PHY_IRQENABLE_STATEULPM4 |
		ISPCSI2_PHY_IRQENABLE_ERRCONTROL4 |
		ISPCSI2_PHY_IRQENABLE_ERRESC4 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTSYNCHS4 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTHS4 |
		ISPCSI2_PHY_IRQENABLE_STATEULPM3 |
		ISPCSI2_PHY_IRQENABLE_ERRCONTROL3 |
		ISPCSI2_PHY_IRQENABLE_ERRESC3 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTSYNCHS3 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTHS3 |
		ISPCSI2_PHY_IRQENABLE_STATEULPM2 |
		ISPCSI2_PHY_IRQENABLE_ERRCONTROL2 |
		ISPCSI2_PHY_IRQENABLE_ERRESC2 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTSYNCHS2 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTHS2 |
		ISPCSI2_PHY_IRQENABLE_STATEULPM1 |
		ISPCSI2_PHY_IRQENABLE_ERRCONTROL1 |
		ISPCSI2_PHY_IRQENABLE_ERRESC1 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTSYNCHS1 |
		ISPCSI2_PHY_IRQENABLE_ERRSOTHS1;
	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_PHY_IRQSTATUS);
	if (enable)
		reg |= isp_reg_readl(isp, csi2->regs1, ISPCSI2_PHY_IRQENABLE);
	else
		reg = 0;
	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_PHY_IRQENABLE);
}

/*
 * csi2_irq_status_set - Enables CSI2 Status IRQs.
 * @enable: Enable/disable CSI2 Status interrupts
 */
static void csi2_irq_status_set(struct isp_device *isp,
				struct isp_csi2_device *csi2, int enable)
{
	u32 reg;
	reg = ISPCSI2_IRQSTATUS_OCP_ERR_IRQ |
		ISPCSI2_IRQSTATUS_SHORT_PACKET_IRQ |
		ISPCSI2_IRQSTATUS_ECC_CORRECTION_IRQ |
		ISPCSI2_IRQSTATUS_ECC_NO_CORRECTION_IRQ |
		ISPCSI2_IRQSTATUS_COMPLEXIO2_ERR_IRQ |
		ISPCSI2_IRQSTATUS_COMPLEXIO1_ERR_IRQ |
		ISPCSI2_IRQSTATUS_FIFO_OVF_IRQ |
		ISPCSI2_IRQSTATUS_CONTEXT(0);
	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_IRQSTATUS);
	if (enable)
		reg |= isp_reg_readl(isp, csi2->regs1, ISPCSI2_IRQENABLE);
	else
		reg = 0;

	isp_reg_writel(isp, reg, csi2->regs1, ISPCSI2_IRQENABLE);
}

/*
 * omap3isp_csi2_reset - Resets the CSI2 module.
 *
 * Must be called with the phy lock held.
 *
 * Returns 0 if successful, or -EBUSY if power command didn't respond.
 */
int omap3isp_csi2_reset(struct isp_csi2_device *csi2)
{
	struct isp_device *isp = csi2->isp;
	u8 soft_reset_retries = 0;
	u32 reg;
	int i;

	if (!csi2->available)
		return -ENODEV;

	if (csi2->phy->phy_in_use)
		return -EBUSY;

	isp_reg_set(isp, csi2->regs1, ISPCSI2_SYSCONFIG,
		    ISPCSI2_SYSCONFIG_SOFT_RESET);

	do {
		reg = isp_reg_readl(isp, csi2->regs1, ISPCSI2_SYSSTATUS) &
				    ISPCSI2_SYSSTATUS_RESET_DONE;
		if (reg == ISPCSI2_SYSSTATUS_RESET_DONE)
			break;
		soft_reset_retries++;
		if (soft_reset_retries < 5)
			udelay(100);
	} while (soft_reset_retries < 5);

	if (soft_reset_retries == 5) {
		printk(KERN_ERR "CSI2: Soft reset try count exceeded!\n");
		return -EBUSY;
	}

	if (isp->revision == ISP_REVISION_15_0)
		isp_reg_set(isp, csi2->regs1, ISPCSI2_PHY_CFG,
			    ISPCSI2_PHY_CFG_RESET_CTRL);

	i = 100;
	do {
		reg = isp_reg_readl(isp, csi2->phy->phy_regs, ISPCSIPHY_REG1)
		    & ISPCSIPHY_REG1_RESET_DONE_CTRLCLK;
		if (reg == ISPCSIPHY_REG1_RESET_DONE_CTRLCLK)
			break;
		udelay(100);
	} while (--i > 0);

	if (i == 0) {
		printk(KERN_ERR
		       "CSI2: Reset for CSI2_96M_FCLK domain Failed!\n");
		return -EBUSY;
	}

	if (isp->autoidle)
		isp_reg_clr_set(isp, csi2->regs1, ISPCSI2_SYSCONFIG,
				ISPCSI2_SYSCONFIG_MSTANDBY_MODE_MASK |
				ISPCSI2_SYSCONFIG_AUTO_IDLE,
				ISPCSI2_SYSCONFIG_MSTANDBY_MODE_SMART |
				((isp->revision == ISP_REVISION_15_0) ?
				 ISPCSI2_SYSCONFIG_AUTO_IDLE : 0));
	else
		isp_reg_clr_set(isp, csi2->regs1, ISPCSI2_SYSCONFIG,
				ISPCSI2_SYSCONFIG_MSTANDBY_MODE_MASK |
				ISPCSI2_SYSCONFIG_AUTO_IDLE,
				ISPCSI2_SYSCONFIG_MSTANDBY_MODE_NO);

	return 0;
}

static int csi2_configure(struct isp_csi2_device *csi2)
{
	const struct isp_v4l2_subdevs_group *pdata;
	struct isp_device *isp = csi2->isp;
	struct isp_csi2_timing_cfg *timing = &csi2->timing[0];
	struct v4l2_subdev *sensor;
	struct media_pad *pad;

	/*
	 * CSI2 fields that can be updated while the context has
	 * been enabled or the interface has been enabled are not
	 * updated dynamically currently. So we do not allow to
	 * reconfigure if either has been enabled
	 */
	if (csi2->contexts[0].enabled || csi2->ctrl.if_enable)
		return -EBUSY;

	pad = media_entity_remote_source(&csi2->pads[CSI2_PAD_SINK]);
	sensor = media_entity_to_v4l2_subdev(pad->entity);
	pdata = sensor->host_priv;

	csi2->frame_skip = 0;
	v4l2_subdev_call(sensor, sensor, g_skip_frames, &csi2->frame_skip);

	csi2->ctrl.vp_out_ctrl = pdata->bus.csi2.vpclk_div;
	csi2->ctrl.frame_mode = ISP_CSI2_FRAME_IMMEDIATE;
	csi2->ctrl.ecc_enable = pdata->bus.csi2.crc;

	timing->ionum = 1;
	timing->force_rx_mode = 1;
	timing->stop_state_16x = 1;
	timing->stop_state_4x = 1;
	timing->stop_state_counter = 0x1FF;

	/*
	 * The CSI2 receiver can't do any format conversion except DPCM
	 * decompression, so every set_format call configures both pads
	 * and enables DPCM decompression as a special case:
	 */
	if (csi2->formats[CSI2_PAD_SINK].code !=
	    csi2->formats[CSI2_PAD_SOURCE].code)
		csi2->dpcm_decompress = true;
	else
		csi2->dpcm_decompress = false;

	csi2->contexts[0].format_id = csi2_ctx_map_format(csi2);

	if (csi2->video_out.bpl_padding == 0)
		csi2->contexts[0].data_offset = 0;
	else
		csi2->contexts[0].data_offset = csi2->video_out.bpl_value;

	/*
	 * Enable end of frame and end of line signals generation for
	 * context 0. These signals are generated from CSI2 receiver to
	 * qualify the last pixel of a frame and the last pixel of a line.
	 * Without enabling the signals CSI2 receiver writes data to memory
	 * beyond buffer size and/or data line offset is not handled correctly.
	 */
	csi2->contexts[0].eof_enabled = 1;
	csi2->contexts[0].eol_enabled = 1;

	csi2_irq_complexio1_set(isp, csi2, 1);
	csi2_irq_ctx_set(isp, csi2, 1);
	csi2_irq_status_set(isp, csi2, 1);

	/* Set configuration (timings, format and links) */
	csi2_timing_config(isp, csi2, timing);
	csi2_recv_config(isp, csi2, &csi2->ctrl);
	csi2_ctx_config(isp, csi2, &csi2->contexts[0]);

	return 0;
}

/*
 * csi2_print_status - Prints CSI2 debug information.
 */
#define CSI2_PRINT_REGISTER(isp, regs, name)\
	dev_dbg(isp->dev, "###CSI2 " #name "=0x%08x\n", \
		isp_reg_readl(isp, regs, ISPCSI2_##name))

static void csi2_print_status(struct isp_csi2_device *csi2)
{
	struct isp_device *isp = csi2->isp;

	if (!csi2->available)
		return;

	dev_dbg(isp->dev, "-------------CSI2 Register dump-------------\n");

	CSI2_PRINT_REGISTER(isp, csi2->regs1, SYSCONFIG);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, SYSSTATUS);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, IRQENABLE);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, IRQSTATUS);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTRL);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, DBG_H);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, GNQ);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, PHY_CFG);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, PHY_IRQSTATUS);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, SHORT_PACKET);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, PHY_IRQENABLE);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, DBG_P);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, TIMING);
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_CTRL1(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_CTRL2(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_DAT_OFST(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_DAT_PING_ADDR(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_DAT_PONG_ADDR(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_IRQENABLE(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_IRQSTATUS(0));
	CSI2_PRINT_REGISTER(isp, csi2->regs1, CTX_CTRL3(0));

	dev_dbg(isp->dev, "--------------------------------------------\n");
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

/*
 * csi2_isr_buffer - Does buffer handling at end-of-frame
 * when writing to memory.
 */
static void csi2_isr_buffer(struct isp_csi2_device *csi2)
{
	struct isp_device *isp = csi2->isp;
	struct isp_buffer *buffer;

	csi2_ctx_enable(isp, csi2, 0, 0);

	buffer = omap3isp_video_buffer_next(&csi2->video_out, 0);

	/*
	 * Let video queue operation restart engine if there is an underrun
	 * condition.
	 */
	if (buffer == NULL)
		return;

	csi2_set_outaddr(csi2, buffer->isp_addr);
	csi2_ctx_enable(isp, csi2, 0, 1);
}

static void csi2_isr_ctx(struct isp_csi2_device *csi2,
			 struct isp_csi2_ctx_cfg *ctx)
{
	struct isp_device *isp = csi2->isp;
	unsigned int n = ctx->ctxnum;
	u32 status;

	status = isp_reg_readl(isp, csi2->regs1, ISPCSI2_CTX_IRQSTATUS(n));
	isp_reg_writel(isp, status, csi2->regs1, ISPCSI2_CTX_IRQSTATUS(n));

	/* Propagate frame number */
	if (status & ISPCSI2_CTX_IRQSTATUS_FS_IRQ) {
		struct isp_pipeline *pipe =
				     to_isp_pipeline(&csi2->subdev.entity);
		if (pipe->do_propagation)
			atomic_inc(&pipe->frame_number);
	}

	if (!(status & ISPCSI2_CTX_IRQSTATUS_FE_IRQ))
		return;

	/* Skip interrupts until we reach the frame skip count. The CSI2 will be
	 * automatically disabled, as the frame skip count has been programmed
	 * in the CSI2_CTx_CTRL1::COUNT field, so reenable it.
	 *
	 * It would have been nice to rely on the FRAME_NUMBER interrupt instead
	 * but it turned out that the interrupt is only generated when the CSI2
	 * writes to memory (the CSI2_CTx_CTRL1::COUNT field is decreased
	 * correctly and reaches 0 when data is forwarded to the video port only
	 * but no interrupt arrives). Maybe a CSI2 hardware bug.
	 */
	if (csi2->frame_skip) {
		csi2->frame_skip--;
		if (csi2->frame_skip == 0) {
			ctx->format_id = csi2_ctx_map_format(csi2);
			csi2_ctx_config(isp, csi2, ctx);
			csi2_ctx_enable(isp, csi2, n, 1);
		}
		return;
	}

	if (csi2->output & CSI2_OUTPUT_MEMORY)
		csi2_isr_buffer(csi2);
}

/*
 * omap3isp_csi2_isr - CSI2 interrupt handling.
 *
 * Return -EIO on Transmission error
 */
int omap3isp_csi2_isr(struct isp_csi2_device *csi2)
{
	u32 csi2_irqstatus, cpxio1_irqstatus;
	struct isp_device *isp = csi2->isp;
	int retval = 0;

	if (!csi2->available)
		return -ENODEV;

	csi2_irqstatus = isp_reg_readl(isp, csi2->regs1, ISPCSI2_IRQSTATUS);
	isp_reg_writel(isp, csi2_irqstatus, csi2->regs1, ISPCSI2_IRQSTATUS);

	/* Failure Cases */
	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_COMPLEXIO1_ERR_IRQ) {
		cpxio1_irqstatus = isp_reg_readl(isp, csi2->regs1,
						 ISPCSI2_PHY_IRQSTATUS);
		isp_reg_writel(isp, cpxio1_irqstatus,
			       csi2->regs1, ISPCSI2_PHY_IRQSTATUS);
		dev_dbg(isp->dev, "CSI2: ComplexIO Error IRQ "
			"%x\n", cpxio1_irqstatus);
		retval = -EIO;
	}

	if (csi2_irqstatus & (ISPCSI2_IRQSTATUS_OCP_ERR_IRQ |
			      ISPCSI2_IRQSTATUS_SHORT_PACKET_IRQ |
			      ISPCSI2_IRQSTATUS_ECC_NO_CORRECTION_IRQ |
			      ISPCSI2_IRQSTATUS_COMPLEXIO2_ERR_IRQ |
			      ISPCSI2_IRQSTATUS_FIFO_OVF_IRQ)) {
		dev_dbg(isp->dev, "CSI2 Err:"
			" OCP:%d,"
			" Short_pack:%d,"
			" ECC:%d,"
			" CPXIO2:%d,"
			" FIFO_OVF:%d,"
			"\n",
			(csi2_irqstatus &
			 ISPCSI2_IRQSTATUS_OCP_ERR_IRQ) ? 1 : 0,
			(csi2_irqstatus &
			 ISPCSI2_IRQSTATUS_SHORT_PACKET_IRQ) ? 1 : 0,
			(csi2_irqstatus &
			 ISPCSI2_IRQSTATUS_ECC_NO_CORRECTION_IRQ) ? 1 : 0,
			(csi2_irqstatus &
			 ISPCSI2_IRQSTATUS_COMPLEXIO2_ERR_IRQ) ? 1 : 0,
			(csi2_irqstatus &
			 ISPCSI2_IRQSTATUS_FIFO_OVF_IRQ) ? 1 : 0);
		retval = -EIO;
	}

	if (omap3isp_module_sync_is_stopping(&csi2->wait, &csi2->stopping))
		return 0;

	/* Successful cases */
	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_CONTEXT(0))
		csi2_isr_ctx(csi2, &csi2->contexts[0]);

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_ECC_CORRECTION_IRQ)
		dev_dbg(isp->dev, "CSI2: ECC correction done\n");

	return retval;
}

/* -----------------------------------------------------------------------------
 * ISP video operations
 */

/*
 * csi2_queue - Queues the first buffer when using memory output
 * @video: The video node
 * @buffer: buffer to queue
 */
static int csi2_queue(struct isp_video *video, struct isp_buffer *buffer)
{
	struct isp_device *isp = video->isp;
	struct isp_csi2_device *csi2 = &isp->isp_csi2a;

	csi2_set_outaddr(csi2, buffer->isp_addr);

	/*
	 * If streaming was enabled before there was a buffer queued
	 * or underrun happened in the ISR, the hardware was not enabled
	 * and DMA queue flag ISP_VIDEO_DMAQUEUE_UNDERRUN is still set.
	 * Enable it now.
	 */
	if (csi2->video_out.dmaqueue_flags & ISP_VIDEO_DMAQUEUE_UNDERRUN) {
		/* Enable / disable context 0 and IRQs */
		csi2_if_enable(isp, csi2, 1);
		csi2_ctx_enable(isp, csi2, 0, 1);
		isp_video_dmaqueue_flags_clr(&csi2->video_out);
	}

	return 0;
}

static const struct isp_video_operations csi2_ispvideo_ops = {
	.queue = csi2_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static struct v4l2_mbus_framefmt *
__csi2_get_format(struct isp_csi2_device *csi2, struct v4l2_subdev_fh *fh,
		  unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &csi2->formats[pad];
}

static void
csi2_try_format(struct isp_csi2_device *csi2, struct v4l2_subdev_fh *fh,
		unsigned int pad, struct v4l2_mbus_framefmt *fmt,
		enum v4l2_subdev_format_whence which)
{
	enum v4l2_mbus_pixelcode pixelcode;
	struct v4l2_mbus_framefmt *format;
	const struct isp_format_info *info;
	unsigned int i;

	switch (pad) {
	case CSI2_PAD_SINK:
		/* Clamp the width and height to valid range (1-8191). */
		for (i = 0; i < ARRAY_SIZE(csi2_input_fmts); i++) {
			if (fmt->code == csi2_input_fmts[i])
				break;
		}

		/* If not found, use SGRBG10 as default */
		if (i >= ARRAY_SIZE(csi2_input_fmts))
			fmt->code = V4L2_MBUS_FMT_SGRBG10_1X10;

		fmt->width = clamp_t(u32, fmt->width, 1, 8191);
		fmt->height = clamp_t(u32, fmt->height, 1, 8191);
		break;

	case CSI2_PAD_SOURCE:
		/* Source format same as sink format, except for DPCM
		 * compression.
		 */
		pixelcode = fmt->code;
		format = __csi2_get_format(csi2, fh, CSI2_PAD_SINK, which);
		memcpy(fmt, format, sizeof(*fmt));

		/*
		 * Only Allow DPCM decompression, and check that the
		 * pattern is preserved
		 */
		info = omap3isp_video_format_info(fmt->code);
		if (info->uncompressed == pixelcode)
			fmt->code = pixelcode;
		break;
	}

	/* RGB, non-interlaced */
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->field = V4L2_FIELD_NONE;
}

/*
 * csi2_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @fh     : V4L2 subdev file handle
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int csi2_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	const struct isp_format_info *info;

	if (code->pad == CSI2_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(csi2_input_fmts))
			return -EINVAL;

		code->code = csi2_input_fmts[code->index];
	} else {
		format = __csi2_get_format(csi2, fh, CSI2_PAD_SINK,
					   V4L2_SUBDEV_FORMAT_TRY);
		switch (code->index) {
		case 0:
			/* Passthrough sink pad code */
			code->code = format->code;
			break;
		case 1:
			/* Uncompressed code */
			info = omap3isp_video_format_info(format->code);
			if (info->uncompressed == format->code)
				return -EINVAL;

			code->code = info->uncompressed;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int csi2_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csi2_try_format(csi2, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csi2_try_format(csi2, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * csi2_get_format - Handle get format by pads subdev method
 * @sd : pointer to v4l2 subdev structure
 * @fh : V4L2 subdev file handle
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int csi2_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csi2_get_format(csi2, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * csi2_set_format - Handle set format by pads subdev method
 * @sd : pointer to v4l2 subdev structure
 * @fh : V4L2 subdev file handle
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int csi2_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csi2_get_format(csi2, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	csi2_try_format(csi2, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == CSI2_PAD_SINK) {
		format = __csi2_get_format(csi2, fh, CSI2_PAD_SOURCE,
					   fmt->which);
		*format = fmt->format;
		csi2_try_format(csi2, fh, CSI2_PAD_SOURCE, format, fmt->which);
	}

	return 0;
}

/*
 * csi2_init_formats - Initialize formats on all pads
 * @sd: ISP CSI2 V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int csi2_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = CSI2_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SGRBG10_1X10;
	format.format.width = 4096;
	format.format.height = 4096;
	csi2_set_format(sd, fh, &format);

	return 0;
}

/*
 * csi2_set_stream - Enable/Disable streaming on the CSI2 module
 * @sd: ISP CSI2 V4L2 subdevice
 * @enable: ISP pipeline stream state
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int csi2_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct isp_device *isp = csi2->isp;
	struct isp_pipeline *pipe = to_isp_pipeline(&csi2->subdev.entity);
	struct isp_video *video_out = &csi2->video_out;

	switch (enable) {
	case ISP_PIPELINE_STREAM_CONTINUOUS:
		if (omap3isp_csiphy_acquire(csi2->phy) < 0)
			return -ENODEV;
		csi2->use_fs_irq = pipe->do_propagation;
		if (csi2->output & CSI2_OUTPUT_MEMORY)
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_CSI2A_WRITE);
		csi2_configure(csi2);
		csi2_print_status(csi2);

		/*
		 * When outputting to memory with no buffer available, let the
		 * buffer queue handler start the hardware. A DMA queue flag
		 * ISP_VIDEO_DMAQUEUE_QUEUED will be set as soon as there is
		 * a buffer available.
		 */
		if (csi2->output & CSI2_OUTPUT_MEMORY &&
		    !(video_out->dmaqueue_flags & ISP_VIDEO_DMAQUEUE_QUEUED))
			break;
		/* Enable context 0 and IRQs */
		atomic_set(&csi2->stopping, 0);
		csi2_ctx_enable(isp, csi2, 0, 1);
		csi2_if_enable(isp, csi2, 1);
		isp_video_dmaqueue_flags_clr(video_out);
		break;

	case ISP_PIPELINE_STREAM_STOPPED:
		if (csi2->state == ISP_PIPELINE_STREAM_STOPPED)
			return 0;
		if (omap3isp_module_sync_idle(&sd->entity, &csi2->wait,
					      &csi2->stopping))
			dev_dbg(isp->dev, "%s: module stop timeout.\n",
				sd->name);
		csi2_ctx_enable(isp, csi2, 0, 0);
		csi2_if_enable(isp, csi2, 0);
		csi2_irq_ctx_set(isp, csi2, 0);
		omap3isp_csiphy_release(csi2->phy);
		isp_video_dmaqueue_flags_clr(video_out);
		omap3isp_sbl_disable(isp, OMAP3_ISP_SBL_CSI2A_WRITE);
		break;
	}

	csi2->state = enable;
	return 0;
}

/* subdev video operations */
static const struct v4l2_subdev_video_ops csi2_video_ops = {
	.s_stream = csi2_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops csi2_pad_ops = {
	.enum_mbus_code = csi2_enum_mbus_code,
	.enum_frame_size = csi2_enum_frame_size,
	.get_fmt = csi2_get_format,
	.set_fmt = csi2_set_format,
};

/* subdev operations */
static const struct v4l2_subdev_ops csi2_ops = {
	.video = &csi2_video_ops,
	.pad = &csi2_pad_ops,
};

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops csi2_internal_ops = {
	.open = csi2_init_formats,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * csi2_link_setup - Setup CSI2 connections.
 * @entity : Pointer to media entity structure
 * @local  : Pointer to local pad array
 * @remote : Pointer to remote pad array
 * @flags  : Link flags
 * return -EINVAL or zero on success
 */
static int csi2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct isp_csi2_ctrl_cfg *ctrl = &csi2->ctrl;

	/*
	 * The ISP core doesn't support pipelines with multiple video outputs.
	 * Revisit this when it will be implemented, and return -EBUSY for now.
	 */

	switch (local->index | media_entity_type(remote->entity)) {
	case CSI2_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->output & ~CSI2_OUTPUT_MEMORY)
				return -EBUSY;
			csi2->output |= CSI2_OUTPUT_MEMORY;
		} else {
			csi2->output &= ~CSI2_OUTPUT_MEMORY;
		}
		break;

	case CSI2_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->output & ~CSI2_OUTPUT_CCDC)
				return -EBUSY;
			csi2->output |= CSI2_OUTPUT_CCDC;
		} else {
			csi2->output &= ~CSI2_OUTPUT_CCDC;
		}
		break;

	default:
		/* Link from camera to CSI2 is fixed... */
		return -EINVAL;
	}

	ctrl->vp_only_enable =
		(csi2->output & CSI2_OUTPUT_MEMORY) ? false : true;
	ctrl->vp_clk_enable = !!(csi2->output & CSI2_OUTPUT_CCDC);

	return 0;
}

/* media operations */
static const struct media_entity_operations csi2_media_ops = {
	.link_setup = csi2_link_setup,
};

/*
 * csi2_init_entities - Initialize subdev and media entity.
 * @csi2: Pointer to csi2 structure.
 * return -ENOMEM or zero on success
 */
static int csi2_init_entities(struct isp_csi2_device *csi2)
{
	struct v4l2_subdev *sd = &csi2->subdev;
	struct media_pad *pads = csi2->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	v4l2_subdev_init(sd, &csi2_ops);
	sd->internal_ops = &csi2_internal_ops;
	strlcpy(sd->name, "OMAP3 ISP CSI2a", sizeof(sd->name));

	sd->grp_id = 1 << 16;	/* group ID for isp subdevs */
	v4l2_set_subdevdata(sd, csi2);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[CSI2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	pads[CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK;

	me->ops = &csi2_media_ops;
	ret = media_entity_init(me, CSI2_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	csi2_init_formats(sd, NULL);

	/* Video device node */
	csi2->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	csi2->video_out.ops = &csi2_ispvideo_ops;
	csi2->video_out.bpl_alignment = 32;
	csi2->video_out.bpl_zero_padding = 1;
	csi2->video_out.bpl_max = 0x1ffe0;
	csi2->video_out.isp = csi2->isp;
	csi2->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 3;

	ret = omap3isp_video_init(&csi2->video_out, "CSI2a");
	if (ret < 0)
		return ret;

	/* Connect the CSI2 subdev to the video node. */
	ret = media_entity_create_link(&csi2->subdev.entity, CSI2_PAD_SOURCE,
				       &csi2->video_out.video.entity, 0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

void omap3isp_csi2_unregister_entities(struct isp_csi2_device *csi2)
{
	media_entity_cleanup(&csi2->subdev.entity);

	v4l2_device_unregister_subdev(&csi2->subdev);
	omap3isp_video_unregister(&csi2->video_out);
}

int omap3isp_csi2_register_entities(struct isp_csi2_device *csi2,
				    struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &csi2->subdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&csi2->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap3isp_csi2_unregister_entities(csi2);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP CSI2 initialisation and cleanup
 */

/*
 * omap3isp_csi2_cleanup - Routine for module driver cleanup
 */
void omap3isp_csi2_cleanup(struct isp_device *isp)
{
}

/*
 * omap3isp_csi2_init - Routine for module driver init
 */
int omap3isp_csi2_init(struct isp_device *isp)
{
	struct isp_csi2_device *csi2a = &isp->isp_csi2a;
	struct isp_csi2_device *csi2c = &isp->isp_csi2c;
	int ret;

	csi2a->isp = isp;
	csi2a->available = 1;
	csi2a->regs1 = OMAP3_ISP_IOMEM_CSI2A_REGS1;
	csi2a->regs2 = OMAP3_ISP_IOMEM_CSI2A_REGS2;
	csi2a->phy = &isp->isp_csiphy2;
	csi2a->state = ISP_PIPELINE_STREAM_STOPPED;
	init_waitqueue_head(&csi2a->wait);

	ret = csi2_init_entities(csi2a);
	if (ret < 0)
		goto fail;

	if (isp->revision == ISP_REVISION_15_0) {
		csi2c->isp = isp;
		csi2c->available = 1;
		csi2c->regs1 = OMAP3_ISP_IOMEM_CSI2C_REGS1;
		csi2c->regs2 = OMAP3_ISP_IOMEM_CSI2C_REGS2;
		csi2c->phy = &isp->isp_csiphy1;
		csi2c->state = ISP_PIPELINE_STREAM_STOPPED;
		init_waitqueue_head(&csi2c->wait);
	}

	return 0;
fail:
	omap3isp_csi2_cleanup(isp);
	return ret;
}
