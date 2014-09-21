/*
 * TI OMAP4 ISS V4L2 Driver - CSI PHY module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <linux/v4l2-mediabus.h>
#include <linux/mm.h>

#include "iss.h"
#include "iss_regs.h"
#include "iss_csi2.h"

/*
 * csi2_if_enable - Enable CSI2 Receiver interface.
 * @enable: enable flag
 *
 */
static void csi2_if_enable(struct iss_csi2_device *csi2, u8 enable)
{
	struct iss_csi2_ctrl_cfg *currctrl = &csi2->ctrl;

	iss_reg_update(csi2->iss, csi2->regs1, CSI2_CTRL, CSI2_CTRL_IF_EN,
		       enable ? CSI2_CTRL_IF_EN : 0);

	currctrl->if_enable = enable;
}

/*
 * csi2_recv_config - CSI2 receiver module configuration.
 * @currctrl: iss_csi2_ctrl_cfg structure
 *
 */
static void csi2_recv_config(struct iss_csi2_device *csi2,
			     struct iss_csi2_ctrl_cfg *currctrl)
{
	u32 reg = 0;

	if (currctrl->frame_mode)
		reg |= CSI2_CTRL_FRAME;
	else
		reg &= ~CSI2_CTRL_FRAME;

	if (currctrl->vp_clk_enable)
		reg |= CSI2_CTRL_VP_CLK_EN;
	else
		reg &= ~CSI2_CTRL_VP_CLK_EN;

	if (currctrl->vp_only_enable)
		reg |= CSI2_CTRL_VP_ONLY_EN;
	else
		reg &= ~CSI2_CTRL_VP_ONLY_EN;

	reg &= ~CSI2_CTRL_VP_OUT_CTRL_MASK;
	reg |= currctrl->vp_out_ctrl << CSI2_CTRL_VP_OUT_CTRL_SHIFT;

	if (currctrl->ecc_enable)
		reg |= CSI2_CTRL_ECC_EN;
	else
		reg &= ~CSI2_CTRL_ECC_EN;

	/*
	 * Set MFlag assertion boundaries to:
	 * Low: 4/8 of FIFO size
	 * High: 6/8 of FIFO size
	 */
	reg &= ~(CSI2_CTRL_MFLAG_LEVH_MASK | CSI2_CTRL_MFLAG_LEVL_MASK);
	reg |= (2 << CSI2_CTRL_MFLAG_LEVH_SHIFT) |
	       (4 << CSI2_CTRL_MFLAG_LEVL_SHIFT);

	/* Generation of 16x64-bit bursts (Recommended) */
	reg |= CSI2_CTRL_BURST_SIZE_EXPAND;

	/* Do Non-Posted writes (Recommended) */
	reg |= CSI2_CTRL_NON_POSTED_WRITE;

	/*
	 * Enforce Little endian for all formats, including:
	 * YUV4:2:2 8-bit and YUV4:2:0 Legacy
	 */
	reg |= CSI2_CTRL_ENDIANNESS;

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTRL, reg);
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
	V4L2_MBUS_FMT_SBGGR8_1X8,
	V4L2_MBUS_FMT_SGBRG8_1X8,
	V4L2_MBUS_FMT_SGRBG8_1X8,
	V4L2_MBUS_FMT_SRGGB8_1X8,
	V4L2_MBUS_FMT_UYVY8_1X16,
	V4L2_MBUS_FMT_YUYV8_1X16,
};

/* To set the format on the CSI2 requires a mapping function that takes
 * the following inputs:
 * - 3 different formats (at this time)
 * - 2 destinations (mem, vp+mem) (vp only handled separately)
 * - 2 decompression options (on, off)
 * Output should be CSI2 frame format code
 * Array indices as follows: [format][dest][decompr]
 * Not all combinations are valid. 0 means invalid.
 */
static const u16 __csi2_fmt_map[][2][2] = {
	/* RAW10 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_RAW10_EXP16,
			/* DPCM decompression */
			0,
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_RAW10_EXP16_VP,
			/* DPCM decompression */
			0,
		},
	},
	/* RAW10 DPCM8 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			CSI2_USERDEF_8BIT_DATA1,
			/* DPCM decompression */
			CSI2_USERDEF_8BIT_DATA1_DPCM10,
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_RAW8_VP,
			/* DPCM decompression */
			CSI2_USERDEF_8BIT_DATA1_DPCM10_VP,
		},
	},
	/* RAW8 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_RAW8,
			/* DPCM decompression */
			0,
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_RAW8_VP,
			/* DPCM decompression */
			0,
		},
	},
	/* YUV422 formats */
	{
		/* Output to memory */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_YUV422_8BIT,
			/* DPCM decompression */
			0,
		},
		/* Output to both */
		{
			/* No DPCM decompression */
			CSI2_PIX_FMT_YUV422_8BIT_VP16,
			/* DPCM decompression */
			0,
		},
	},
};

/*
 * csi2_ctx_map_format - Map CSI2 sink media bus format to CSI2 format ID
 * @csi2: ISS CSI2 device
 *
 * Returns CSI2 physical format id
 */
static u16 csi2_ctx_map_format(struct iss_csi2_device *csi2)
{
	const struct v4l2_mbus_framefmt *fmt = &csi2->formats[CSI2_PAD_SINK];
	int fmtidx, destidx;

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
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SGBRG8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
		fmtidx = 2;
		break;
	case V4L2_MBUS_FMT_UYVY8_1X16:
	case V4L2_MBUS_FMT_YUYV8_1X16:
		fmtidx = 3;
		break;
	default:
		WARN(1, KERN_ERR "CSI2: pixel format %08x unsupported!\n",
		     fmt->code);
		return 0;
	}

	if (!(csi2->output & CSI2_OUTPUT_IPIPEIF) &&
	    !(csi2->output & CSI2_OUTPUT_MEMORY)) {
		/* Neither output enabled is a valid combination */
		return CSI2_PIX_FMT_OTHERS;
	}

	/* If we need to skip frames at the beginning of the stream disable the
	 * video port to avoid sending the skipped frames to the IPIPEIF.
	 */
	destidx = csi2->frame_skip ? 0 : !!(csi2->output & CSI2_OUTPUT_IPIPEIF);

	return __csi2_fmt_map[fmtidx][destidx][csi2->dpcm_decompress];
}

/*
 * csi2_set_outaddr - Set memory address to save output image
 * @csi2: Pointer to ISS CSI2a device.
 * @addr: 32-bit memory address aligned on 32 byte boundary.
 *
 * Sets the memory address where the output will be saved.
 *
 * Returns 0 if successful, or -EINVAL if the address is not in the 32 byte
 * boundary.
 */
static void csi2_set_outaddr(struct iss_csi2_device *csi2, u32 addr)
{
	struct iss_csi2_ctx_cfg *ctx = &csi2->contexts[0];

	ctx->ping_addr = addr;
	ctx->pong_addr = addr;
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_PING_ADDR(ctx->ctxnum),
		      ctx->ping_addr);
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_PONG_ADDR(ctx->ctxnum),
		      ctx->pong_addr);
}

/*
 * is_usr_def_mapping - Checks whether USER_DEF_MAPPING should
 *			be enabled by CSI2.
 * @format_id: mapped format id
 *
 */
static inline int is_usr_def_mapping(u32 format_id)
{
	return (format_id & 0xf0) == 0x40 ? 1 : 0;
}

/*
 * csi2_ctx_enable - Enable specified CSI2 context
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @enable: enable
 *
 */
static void csi2_ctx_enable(struct iss_csi2_device *csi2, u8 ctxnum, u8 enable)
{
	struct iss_csi2_ctx_cfg *ctx = &csi2->contexts[ctxnum];
	u32 reg;

	reg = iss_reg_read(csi2->iss, csi2->regs1, CSI2_CTX_CTRL1(ctxnum));

	if (enable) {
		unsigned int skip = 0;

		if (csi2->frame_skip)
			skip = csi2->frame_skip;
		else if (csi2->output & CSI2_OUTPUT_MEMORY)
			skip = 1;

		reg &= ~CSI2_CTX_CTRL1_COUNT_MASK;
		reg |= CSI2_CTX_CTRL1_COUNT_UNLOCK
		    |  (skip << CSI2_CTX_CTRL1_COUNT_SHIFT)
		    |  CSI2_CTX_CTRL1_CTX_EN;
	} else {
		reg &= ~CSI2_CTX_CTRL1_CTX_EN;
	}

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_CTRL1(ctxnum), reg);
	ctx->enabled = enable;
}

/*
 * csi2_ctx_config - CSI2 context configuration.
 * @ctx: context configuration
 *
 */
static void csi2_ctx_config(struct iss_csi2_device *csi2,
			    struct iss_csi2_ctx_cfg *ctx)
{
	u32 reg = 0;

	/* Set up CSI2_CTx_CTRL1 */
	if (ctx->eof_enabled)
		reg = CSI2_CTX_CTRL1_EOF_EN;

	if (ctx->eol_enabled)
		reg |= CSI2_CTX_CTRL1_EOL_EN;

	if (ctx->checksum_enabled)
		reg |= CSI2_CTX_CTRL1_CS_EN;

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_CTRL1(ctx->ctxnum), reg);

	/* Set up CSI2_CTx_CTRL2 */
	reg = ctx->virtual_id << CSI2_CTX_CTRL2_VIRTUAL_ID_SHIFT;
	reg |= ctx->format_id << CSI2_CTX_CTRL2_FORMAT_SHIFT;

	if (ctx->dpcm_decompress && ctx->dpcm_predictor)
		reg |= CSI2_CTX_CTRL2_DPCM_PRED;

	if (is_usr_def_mapping(ctx->format_id))
		reg |= 2 << CSI2_CTX_CTRL2_USER_DEF_MAP_SHIFT;

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_CTRL2(ctx->ctxnum), reg);

	/* Set up CSI2_CTx_CTRL3 */
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_CTRL3(ctx->ctxnum),
		      ctx->alpha << CSI2_CTX_CTRL3_ALPHA_SHIFT);

	/* Set up CSI2_CTx_DAT_OFST */
	iss_reg_update(csi2->iss, csi2->regs1, CSI2_CTX_DAT_OFST(ctx->ctxnum),
		       CSI2_CTX_DAT_OFST_MASK, ctx->data_offset);

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_PING_ADDR(ctx->ctxnum),
		      ctx->ping_addr);
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_PONG_ADDR(ctx->ctxnum),
		      ctx->pong_addr);
}

/*
 * csi2_timing_config - CSI2 timing configuration.
 * @timing: csi2_timing_cfg structure
 */
static void csi2_timing_config(struct iss_csi2_device *csi2,
			       struct iss_csi2_timing_cfg *timing)
{
	u32 reg;

	reg = iss_reg_read(csi2->iss, csi2->regs1, CSI2_TIMING);

	if (timing->force_rx_mode)
		reg |= CSI2_TIMING_FORCE_RX_MODE_IO1;
	else
		reg &= ~CSI2_TIMING_FORCE_RX_MODE_IO1;

	if (timing->stop_state_16x)
		reg |= CSI2_TIMING_STOP_STATE_X16_IO1;
	else
		reg &= ~CSI2_TIMING_STOP_STATE_X16_IO1;

	if (timing->stop_state_4x)
		reg |= CSI2_TIMING_STOP_STATE_X4_IO1;
	else
		reg &= ~CSI2_TIMING_STOP_STATE_X4_IO1;

	reg &= ~CSI2_TIMING_STOP_STATE_COUNTER_IO1_MASK;
	reg |= timing->stop_state_counter <<
	       CSI2_TIMING_STOP_STATE_COUNTER_IO1_SHIFT;

	iss_reg_write(csi2->iss, csi2->regs1, CSI2_TIMING, reg);
}

/*
 * csi2_irq_ctx_set - Enables CSI2 Context IRQs.
 * @enable: Enable/disable CSI2 Context interrupts
 */
static void csi2_irq_ctx_set(struct iss_csi2_device *csi2, int enable)
{
	u32 reg = CSI2_CTX_IRQ_FE;
	int i;

	if (csi2->use_fs_irq)
		reg |= CSI2_CTX_IRQ_FS;

	for (i = 0; i < 8; i++) {
		iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_IRQSTATUS(i),
			      reg);
		if (enable)
			iss_reg_set(csi2->iss, csi2->regs1,
				    CSI2_CTX_IRQENABLE(i), reg);
		else
			iss_reg_clr(csi2->iss, csi2->regs1,
				    CSI2_CTX_IRQENABLE(i), reg);
	}
}

/*
 * csi2_irq_complexio1_set - Enables CSI2 ComplexIO IRQs.
 * @enable: Enable/disable CSI2 ComplexIO #1 interrupts
 */
static void csi2_irq_complexio1_set(struct iss_csi2_device *csi2, int enable)
{
	u32 reg;

	reg = CSI2_COMPLEXIO_IRQ_STATEALLULPMEXIT |
		CSI2_COMPLEXIO_IRQ_STATEALLULPMENTER |
		CSI2_COMPLEXIO_IRQ_STATEULPM5 |
		CSI2_COMPLEXIO_IRQ_ERRCONTROL5 |
		CSI2_COMPLEXIO_IRQ_ERRESC5 |
		CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS5 |
		CSI2_COMPLEXIO_IRQ_ERRSOTHS5 |
		CSI2_COMPLEXIO_IRQ_STATEULPM4 |
		CSI2_COMPLEXIO_IRQ_ERRCONTROL4 |
		CSI2_COMPLEXIO_IRQ_ERRESC4 |
		CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS4 |
		CSI2_COMPLEXIO_IRQ_ERRSOTHS4 |
		CSI2_COMPLEXIO_IRQ_STATEULPM3 |
		CSI2_COMPLEXIO_IRQ_ERRCONTROL3 |
		CSI2_COMPLEXIO_IRQ_ERRESC3 |
		CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS3 |
		CSI2_COMPLEXIO_IRQ_ERRSOTHS3 |
		CSI2_COMPLEXIO_IRQ_STATEULPM2 |
		CSI2_COMPLEXIO_IRQ_ERRCONTROL2 |
		CSI2_COMPLEXIO_IRQ_ERRESC2 |
		CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS2 |
		CSI2_COMPLEXIO_IRQ_ERRSOTHS2 |
		CSI2_COMPLEXIO_IRQ_STATEULPM1 |
		CSI2_COMPLEXIO_IRQ_ERRCONTROL1 |
		CSI2_COMPLEXIO_IRQ_ERRESC1 |
		CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS1 |
		CSI2_COMPLEXIO_IRQ_ERRSOTHS1;
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_COMPLEXIO_IRQSTATUS, reg);
	if (enable)
		iss_reg_set(csi2->iss, csi2->regs1, CSI2_COMPLEXIO_IRQENABLE,
			    reg);
	else
		iss_reg_write(csi2->iss, csi2->regs1, CSI2_COMPLEXIO_IRQENABLE,
			      0);
}

/*
 * csi2_irq_status_set - Enables CSI2 Status IRQs.
 * @enable: Enable/disable CSI2 Status interrupts
 */
static void csi2_irq_status_set(struct iss_csi2_device *csi2, int enable)
{
	u32 reg;

	reg = CSI2_IRQ_OCP_ERR |
		CSI2_IRQ_SHORT_PACKET |
		CSI2_IRQ_ECC_CORRECTION |
		CSI2_IRQ_ECC_NO_CORRECTION |
		CSI2_IRQ_COMPLEXIO_ERR |
		CSI2_IRQ_FIFO_OVF |
		CSI2_IRQ_CONTEXT0;
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_IRQSTATUS, reg);
	if (enable)
		iss_reg_set(csi2->iss, csi2->regs1, CSI2_IRQENABLE, reg);
	else
		iss_reg_write(csi2->iss, csi2->regs1, CSI2_IRQENABLE, 0);
}

/*
 * omap4iss_csi2_reset - Resets the CSI2 module.
 *
 * Must be called with the phy lock held.
 *
 * Returns 0 if successful, or -EBUSY if power command didn't respond.
 */
int omap4iss_csi2_reset(struct iss_csi2_device *csi2)
{
	unsigned int timeout;

	if (!csi2->available)
		return -ENODEV;

	if (csi2->phy->phy_in_use)
		return -EBUSY;

	iss_reg_set(csi2->iss, csi2->regs1, CSI2_SYSCONFIG,
		    CSI2_SYSCONFIG_SOFT_RESET);

	timeout = iss_poll_condition_timeout(
		iss_reg_read(csi2->iss, csi2->regs1, CSI2_SYSSTATUS) &
		CSI2_SYSSTATUS_RESET_DONE, 500, 100, 200);
	if (timeout) {
		dev_err(csi2->iss->dev, "CSI2: Soft reset timeout!\n");
		return -EBUSY;
	}

	iss_reg_set(csi2->iss, csi2->regs1, CSI2_COMPLEXIO_CFG,
		    CSI2_COMPLEXIO_CFG_RESET_CTRL);

	timeout = iss_poll_condition_timeout(
		iss_reg_read(csi2->iss, csi2->phy->phy_regs, REGISTER1) &
		REGISTER1_RESET_DONE_CTRLCLK, 10000, 100, 500);
	if (timeout) {
		dev_err(csi2->iss->dev, "CSI2: CSI2_96M_FCLK reset timeout!\n");
		return -EBUSY;
	}

	iss_reg_update(csi2->iss, csi2->regs1, CSI2_SYSCONFIG,
		       CSI2_SYSCONFIG_MSTANDBY_MODE_MASK |
		       CSI2_SYSCONFIG_AUTO_IDLE,
		       CSI2_SYSCONFIG_MSTANDBY_MODE_NO);

	return 0;
}

static int csi2_configure(struct iss_csi2_device *csi2)
{
	const struct iss_v4l2_subdevs_group *pdata;
	struct iss_csi2_timing_cfg *timing = &csi2->timing[0];
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

	pad = media_entity_remote_pad(&csi2->pads[CSI2_PAD_SINK]);
	sensor = media_entity_to_v4l2_subdev(pad->entity);
	pdata = sensor->host_priv;

	csi2->frame_skip = 0;
	v4l2_subdev_call(sensor, sensor, g_skip_frames, &csi2->frame_skip);

	csi2->ctrl.vp_out_ctrl = pdata->bus.csi2.vpclk_div;
	csi2->ctrl.frame_mode = ISS_CSI2_FRAME_IMMEDIATE;
	csi2->ctrl.ecc_enable = pdata->bus.csi2.crc;

	timing->force_rx_mode = 1;
	timing->stop_state_16x = 1;
	timing->stop_state_4x = 1;
	timing->stop_state_counter = 0x1ff;

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

	csi2_irq_complexio1_set(csi2, 1);
	csi2_irq_ctx_set(csi2, 1);
	csi2_irq_status_set(csi2, 1);

	/* Set configuration (timings, format and links) */
	csi2_timing_config(csi2, timing);
	csi2_recv_config(csi2, &csi2->ctrl);
	csi2_ctx_config(csi2, &csi2->contexts[0]);

	return 0;
}

/*
 * csi2_print_status - Prints CSI2 debug information.
 */
#define CSI2_PRINT_REGISTER(iss, regs, name)\
	dev_dbg(iss->dev, "###CSI2 " #name "=0x%08x\n", \
		iss_reg_read(iss, regs, CSI2_##name))

static void csi2_print_status(struct iss_csi2_device *csi2)
{
	struct iss_device *iss = csi2->iss;

	if (!csi2->available)
		return;

	dev_dbg(iss->dev, "-------------CSI2 Register dump-------------\n");

	CSI2_PRINT_REGISTER(iss, csi2->regs1, SYSCONFIG);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, SYSSTATUS);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, IRQENABLE);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, IRQSTATUS);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTRL);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, DBG_H);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, COMPLEXIO_CFG);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, COMPLEXIO_IRQSTATUS);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, SHORT_PACKET);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, COMPLEXIO_IRQENABLE);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, DBG_P);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, TIMING);
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_CTRL1(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_CTRL2(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_DAT_OFST(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_PING_ADDR(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_PONG_ADDR(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_IRQENABLE(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_IRQSTATUS(0));
	CSI2_PRINT_REGISTER(iss, csi2->regs1, CTX_CTRL3(0));

	dev_dbg(iss->dev, "--------------------------------------------\n");
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

/*
 * csi2_isr_buffer - Does buffer handling at end-of-frame
 * when writing to memory.
 */
static void csi2_isr_buffer(struct iss_csi2_device *csi2)
{
	struct iss_buffer *buffer;

	csi2_ctx_enable(csi2, 0, 0);

	buffer = omap4iss_video_buffer_next(&csi2->video_out);

	/*
	 * Let video queue operation restart engine if there is an underrun
	 * condition.
	 */
	if (buffer == NULL)
		return;

	csi2_set_outaddr(csi2, buffer->iss_addr);
	csi2_ctx_enable(csi2, 0, 1);
}

static void csi2_isr_ctx(struct iss_csi2_device *csi2,
			 struct iss_csi2_ctx_cfg *ctx)
{
	unsigned int n = ctx->ctxnum;
	u32 status;

	status = iss_reg_read(csi2->iss, csi2->regs1, CSI2_CTX_IRQSTATUS(n));
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_CTX_IRQSTATUS(n), status);

	/* Propagate frame number */
	if (status & CSI2_CTX_IRQ_FS) {
		struct iss_pipeline *pipe =
				     to_iss_pipeline(&csi2->subdev.entity);
		if (pipe->do_propagation)
			atomic_inc(&pipe->frame_number);
	}

	if (!(status & CSI2_CTX_IRQ_FE))
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
			csi2_ctx_config(csi2, ctx);
			csi2_ctx_enable(csi2, n, 1);
		}
		return;
	}

	if (csi2->output & CSI2_OUTPUT_MEMORY)
		csi2_isr_buffer(csi2);
}

/*
 * omap4iss_csi2_isr - CSI2 interrupt handling.
 */
void omap4iss_csi2_isr(struct iss_csi2_device *csi2)
{
	struct iss_pipeline *pipe = to_iss_pipeline(&csi2->subdev.entity);
	u32 csi2_irqstatus, cpxio1_irqstatus;
	struct iss_device *iss = csi2->iss;

	if (!csi2->available)
		return;

	csi2_irqstatus = iss_reg_read(csi2->iss, csi2->regs1, CSI2_IRQSTATUS);
	iss_reg_write(csi2->iss, csi2->regs1, CSI2_IRQSTATUS, csi2_irqstatus);

	/* Failure Cases */
	if (csi2_irqstatus & CSI2_IRQ_COMPLEXIO_ERR) {
		cpxio1_irqstatus = iss_reg_read(csi2->iss, csi2->regs1,
						CSI2_COMPLEXIO_IRQSTATUS);
		iss_reg_write(csi2->iss, csi2->regs1, CSI2_COMPLEXIO_IRQSTATUS,
			      cpxio1_irqstatus);
		dev_dbg(iss->dev, "CSI2: ComplexIO Error IRQ %x\n",
			cpxio1_irqstatus);
		pipe->error = true;
	}

	if (csi2_irqstatus & (CSI2_IRQ_OCP_ERR |
			      CSI2_IRQ_SHORT_PACKET |
			      CSI2_IRQ_ECC_NO_CORRECTION |
			      CSI2_IRQ_COMPLEXIO_ERR |
			      CSI2_IRQ_FIFO_OVF)) {
		dev_dbg(iss->dev,
			"CSI2 Err: OCP:%d SHORT:%d ECC:%d CPXIO:%d OVF:%d\n",
			csi2_irqstatus & CSI2_IRQ_OCP_ERR ? 1 : 0,
			csi2_irqstatus & CSI2_IRQ_SHORT_PACKET ? 1 : 0,
			csi2_irqstatus & CSI2_IRQ_ECC_NO_CORRECTION ? 1 : 0,
			csi2_irqstatus & CSI2_IRQ_COMPLEXIO_ERR ? 1 : 0,
			csi2_irqstatus & CSI2_IRQ_FIFO_OVF ? 1 : 0);
		pipe->error = true;
	}

	if (omap4iss_module_sync_is_stopping(&csi2->wait, &csi2->stopping))
		return;

	/* Successful cases */
	if (csi2_irqstatus & CSI2_IRQ_CONTEXT0)
		csi2_isr_ctx(csi2, &csi2->contexts[0]);

	if (csi2_irqstatus & CSI2_IRQ_ECC_CORRECTION)
		dev_dbg(iss->dev, "CSI2: ECC correction done\n");
}

/* -----------------------------------------------------------------------------
 * ISS video operations
 */

/*
 * csi2_queue - Queues the first buffer when using memory output
 * @video: The video node
 * @buffer: buffer to queue
 */
static int csi2_queue(struct iss_video *video, struct iss_buffer *buffer)
{
	struct iss_csi2_device *csi2 = container_of(video,
				struct iss_csi2_device, video_out);

	csi2_set_outaddr(csi2, buffer->iss_addr);

	/*
	 * If streaming was enabled before there was a buffer queued
	 * or underrun happened in the ISR, the hardware was not enabled
	 * and DMA queue flag ISS_VIDEO_DMAQUEUE_UNDERRUN is still set.
	 * Enable it now.
	 */
	if (csi2->video_out.dmaqueue_flags & ISS_VIDEO_DMAQUEUE_UNDERRUN) {
		/* Enable / disable context 0 and IRQs */
		csi2_if_enable(csi2, 1);
		csi2_ctx_enable(csi2, 0, 1);
		iss_video_dmaqueue_flags_clr(&csi2->video_out);
	}

	return 0;
}

static const struct iss_video_operations csi2_issvideo_ops = {
	.queue = csi2_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static struct v4l2_mbus_framefmt *
__csi2_get_format(struct iss_csi2_device *csi2, struct v4l2_subdev_fh *fh,
		  unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &csi2->formats[pad];
}

static void
csi2_try_format(struct iss_csi2_device *csi2, struct v4l2_subdev_fh *fh,
		unsigned int pad, struct v4l2_mbus_framefmt *fmt,
		enum v4l2_subdev_format_whence which)
{
	enum v4l2_mbus_pixelcode pixelcode;
	struct v4l2_mbus_framefmt *format;
	const struct iss_format_info *info;
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
		info = omap4iss_video_format_info(fmt->code);
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
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	const struct iss_format_info *info;

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
			info = omap4iss_video_format_info(format->code);
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
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
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
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
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
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
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

static int csi2_link_validate(struct v4l2_subdev *sd, struct media_link *link,
			      struct v4l2_subdev_format *source_fmt,
			      struct v4l2_subdev_format *sink_fmt)
{
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct iss_pipeline *pipe = to_iss_pipeline(&csi2->subdev.entity);
	int rval;

	pipe->external = media_entity_to_v4l2_subdev(link->source->entity);
	rval = omap4iss_get_external_info(pipe, link);
	if (rval < 0)
		return rval;

	return v4l2_subdev_link_validate_default(sd, link, source_fmt,
						 sink_fmt);
}

/*
 * csi2_init_formats - Initialize formats on all pads
 * @sd: ISS CSI2 V4L2 subdevice
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
 * @sd: ISS CSI2 V4L2 subdevice
 * @enable: ISS pipeline stream state
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int csi2_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct iss_device *iss = csi2->iss;
	struct iss_pipeline *pipe = to_iss_pipeline(&csi2->subdev.entity);
	struct iss_video *video_out = &csi2->video_out;
	int ret = 0;

	if (csi2->state == ISS_PIPELINE_STREAM_STOPPED) {
		if (enable == ISS_PIPELINE_STREAM_STOPPED)
			return 0;

		omap4iss_subclk_enable(iss, csi2->subclk);
	}

	switch (enable) {
	case ISS_PIPELINE_STREAM_CONTINUOUS: {
		ret = omap4iss_csiphy_config(iss, sd);
		if (ret < 0)
			return ret;

		if (omap4iss_csiphy_acquire(csi2->phy) < 0)
			return -ENODEV;
		csi2->use_fs_irq = pipe->do_propagation;
		csi2_configure(csi2);
		csi2_print_status(csi2);

		/*
		 * When outputting to memory with no buffer available, let the
		 * buffer queue handler start the hardware. A DMA queue flag
		 * ISS_VIDEO_DMAQUEUE_QUEUED will be set as soon as there is
		 * a buffer available.
		 */
		if (csi2->output & CSI2_OUTPUT_MEMORY &&
		    !(video_out->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_QUEUED))
			break;
		/* Enable context 0 and IRQs */
		atomic_set(&csi2->stopping, 0);
		csi2_ctx_enable(csi2, 0, 1);
		csi2_if_enable(csi2, 1);
		iss_video_dmaqueue_flags_clr(video_out);
		break;
	}
	case ISS_PIPELINE_STREAM_STOPPED:
		if (csi2->state == ISS_PIPELINE_STREAM_STOPPED)
			return 0;
		if (omap4iss_module_sync_idle(&sd->entity, &csi2->wait,
					      &csi2->stopping))
			ret = -ETIMEDOUT;
		csi2_ctx_enable(csi2, 0, 0);
		csi2_if_enable(csi2, 0);
		csi2_irq_ctx_set(csi2, 0);
		omap4iss_csiphy_release(csi2->phy);
		omap4iss_subclk_disable(iss, csi2->subclk);
		iss_video_dmaqueue_flags_clr(video_out);
		break;
	}

	csi2->state = enable;
	return ret;
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
	.link_validate = csi2_link_validate,
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
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(sd);
	struct iss_csi2_ctrl_cfg *ctrl = &csi2->ctrl;

	/*
	 * The ISS core doesn't support pipelines with multiple video outputs.
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
			if (csi2->output & ~CSI2_OUTPUT_IPIPEIF)
				return -EBUSY;
			csi2->output |= CSI2_OUTPUT_IPIPEIF;
		} else {
			csi2->output &= ~CSI2_OUTPUT_IPIPEIF;
		}
		break;

	default:
		/* Link from camera to CSI2 is fixed... */
		return -EINVAL;
	}

	ctrl->vp_only_enable = csi2->output & CSI2_OUTPUT_MEMORY ? false : true;
	ctrl->vp_clk_enable = !!(csi2->output & CSI2_OUTPUT_IPIPEIF);

	return 0;
}

/* media operations */
static const struct media_entity_operations csi2_media_ops = {
	.link_setup = csi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

void omap4iss_csi2_unregister_entities(struct iss_csi2_device *csi2)
{
	v4l2_device_unregister_subdev(&csi2->subdev);
	omap4iss_video_unregister(&csi2->video_out);
}

int omap4iss_csi2_register_entities(struct iss_csi2_device *csi2,
				    struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &csi2->subdev);
	if (ret < 0)
		goto error;

	ret = omap4iss_video_register(&csi2->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap4iss_csi2_unregister_entities(csi2);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISS CSI2 initialisation and cleanup
 */

/*
 * csi2_init_entities - Initialize subdev and media entity.
 * @csi2: Pointer to csi2 structure.
 * return -ENOMEM or zero on success
 */
static int csi2_init_entities(struct iss_csi2_device *csi2, const char *subname)
{
	struct v4l2_subdev *sd = &csi2->subdev;
	struct media_pad *pads = csi2->pads;
	struct media_entity *me = &sd->entity;
	int ret;
	char name[V4L2_SUBDEV_NAME_SIZE];

	v4l2_subdev_init(sd, &csi2_ops);
	sd->internal_ops = &csi2_internal_ops;
	sprintf(name, "CSI2%s", subname);
	snprintf(sd->name, sizeof(sd->name), "OMAP4 ISS %s", name);

	sd->grp_id = 1 << 16;	/* group ID for iss subdevs */
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
	csi2->video_out.ops = &csi2_issvideo_ops;
	csi2->video_out.bpl_alignment = 32;
	csi2->video_out.bpl_zero_padding = 1;
	csi2->video_out.bpl_max = 0x1ffe0;
	csi2->video_out.iss = csi2->iss;
	csi2->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 3;

	ret = omap4iss_video_init(&csi2->video_out, name);
	if (ret < 0)
		goto error_video;

	/* Connect the CSI2 subdev to the video node. */
	ret = media_entity_create_link(&csi2->subdev.entity, CSI2_PAD_SOURCE,
				       &csi2->video_out.video.entity, 0, 0);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap4iss_video_cleanup(&csi2->video_out);
error_video:
	media_entity_cleanup(&csi2->subdev.entity);
	return ret;
}

/*
 * omap4iss_csi2_init - Routine for module driver init
 */
int omap4iss_csi2_init(struct iss_device *iss)
{
	struct iss_csi2_device *csi2a = &iss->csi2a;
	struct iss_csi2_device *csi2b = &iss->csi2b;
	int ret;

	csi2a->iss = iss;
	csi2a->available = 1;
	csi2a->regs1 = OMAP4_ISS_MEM_CSI2_A_REGS1;
	csi2a->phy = &iss->csiphy1;
	csi2a->subclk = OMAP4_ISS_SUBCLK_CSI2_A;
	csi2a->state = ISS_PIPELINE_STREAM_STOPPED;
	init_waitqueue_head(&csi2a->wait);

	ret = csi2_init_entities(csi2a, "a");
	if (ret < 0)
		return ret;

	csi2b->iss = iss;
	csi2b->available = 1;
	csi2b->regs1 = OMAP4_ISS_MEM_CSI2_B_REGS1;
	csi2b->phy = &iss->csiphy2;
	csi2b->subclk = OMAP4_ISS_SUBCLK_CSI2_B;
	csi2b->state = ISS_PIPELINE_STREAM_STOPPED;
	init_waitqueue_head(&csi2b->wait);

	ret = csi2_init_entities(csi2b, "b");
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * omap4iss_csi2_cleanup - Routine for module driver cleanup
 */
void omap4iss_csi2_cleanup(struct iss_device *iss)
{
	struct iss_csi2_device *csi2a = &iss->csi2a;
	struct iss_csi2_device *csi2b = &iss->csi2b;

	omap4iss_video_cleanup(&csi2a->video_out);
	media_entity_cleanup(&csi2a->subdev.entity);

	omap4iss_video_cleanup(&csi2b->video_out);
	media_entity_cleanup(&csi2b->subdev.entity);
}
