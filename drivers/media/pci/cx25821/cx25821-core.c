/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/i2c.h>
#include <linux/slab.h>
#include "cx25821.h"
#include "cx25821-sram.h"
#include "cx25821-video.h"

MODULE_DESCRIPTION("Driver for Athena cards");
MODULE_AUTHOR("Shu Lin - Hiep Huynh");
MODULE_LICENSE("GPL");

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

static unsigned int card[] = {[0 ... (CX25821_MAXBOARDS - 1)] = UNSET };
module_param_array(card, int, NULL, 0444);
MODULE_PARM_DESC(card, "card type");

const struct sram_channel cx25821_sram_channels[] = {
	[SRAM_CH00] = {
		.i = SRAM_CH00,
		.name = "VID A",
		.cmds_start = VID_A_DOWN_CMDS,
		.ctrl_start = VID_A_IQ,
		.cdt = VID_A_CDT,
		.fifo_start = VID_A_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA1_PTR1,
		.ptr2_reg = DMA1_PTR2,
		.cnt1_reg = DMA1_CNT1,
		.cnt2_reg = DMA1_CNT2,
		.int_msk = VID_A_INT_MSK,
		.int_stat = VID_A_INT_STAT,
		.int_mstat = VID_A_INT_MSTAT,
		.dma_ctl = VID_DST_A_DMA_CTL,
		.gpcnt_ctl = VID_DST_A_GPCNT_CTL,
		.gpcnt = VID_DST_A_GPCNT,
		.vip_ctl = VID_DST_A_VIP_CTL,
		.pix_frmt = VID_DST_A_PIX_FRMT,
	},

	[SRAM_CH01] = {
		.i = SRAM_CH01,
		.name = "VID B",
		.cmds_start = VID_B_DOWN_CMDS,
		.ctrl_start = VID_B_IQ,
		.cdt = VID_B_CDT,
		.fifo_start = VID_B_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA2_PTR1,
		.ptr2_reg = DMA2_PTR2,
		.cnt1_reg = DMA2_CNT1,
		.cnt2_reg = DMA2_CNT2,
		.int_msk = VID_B_INT_MSK,
		.int_stat = VID_B_INT_STAT,
		.int_mstat = VID_B_INT_MSTAT,
		.dma_ctl = VID_DST_B_DMA_CTL,
		.gpcnt_ctl = VID_DST_B_GPCNT_CTL,
		.gpcnt = VID_DST_B_GPCNT,
		.vip_ctl = VID_DST_B_VIP_CTL,
		.pix_frmt = VID_DST_B_PIX_FRMT,
	},

	[SRAM_CH02] = {
		.i = SRAM_CH02,
		.name = "VID C",
		.cmds_start = VID_C_DOWN_CMDS,
		.ctrl_start = VID_C_IQ,
		.cdt = VID_C_CDT,
		.fifo_start = VID_C_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA3_PTR1,
		.ptr2_reg = DMA3_PTR2,
		.cnt1_reg = DMA3_CNT1,
		.cnt2_reg = DMA3_CNT2,
		.int_msk = VID_C_INT_MSK,
		.int_stat = VID_C_INT_STAT,
		.int_mstat = VID_C_INT_MSTAT,
		.dma_ctl = VID_DST_C_DMA_CTL,
		.gpcnt_ctl = VID_DST_C_GPCNT_CTL,
		.gpcnt = VID_DST_C_GPCNT,
		.vip_ctl = VID_DST_C_VIP_CTL,
		.pix_frmt = VID_DST_C_PIX_FRMT,
	},

	[SRAM_CH03] = {
		.i = SRAM_CH03,
		.name = "VID D",
		.cmds_start = VID_D_DOWN_CMDS,
		.ctrl_start = VID_D_IQ,
		.cdt = VID_D_CDT,
		.fifo_start = VID_D_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA4_PTR1,
		.ptr2_reg = DMA4_PTR2,
		.cnt1_reg = DMA4_CNT1,
		.cnt2_reg = DMA4_CNT2,
		.int_msk = VID_D_INT_MSK,
		.int_stat = VID_D_INT_STAT,
		.int_mstat = VID_D_INT_MSTAT,
		.dma_ctl = VID_DST_D_DMA_CTL,
		.gpcnt_ctl = VID_DST_D_GPCNT_CTL,
		.gpcnt = VID_DST_D_GPCNT,
		.vip_ctl = VID_DST_D_VIP_CTL,
		.pix_frmt = VID_DST_D_PIX_FRMT,
	},

	[SRAM_CH04] = {
		.i = SRAM_CH04,
		.name = "VID E",
		.cmds_start = VID_E_DOWN_CMDS,
		.ctrl_start = VID_E_IQ,
		.cdt = VID_E_CDT,
		.fifo_start = VID_E_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA5_PTR1,
		.ptr2_reg = DMA5_PTR2,
		.cnt1_reg = DMA5_CNT1,
		.cnt2_reg = DMA5_CNT2,
		.int_msk = VID_E_INT_MSK,
		.int_stat = VID_E_INT_STAT,
		.int_mstat = VID_E_INT_MSTAT,
		.dma_ctl = VID_DST_E_DMA_CTL,
		.gpcnt_ctl = VID_DST_E_GPCNT_CTL,
		.gpcnt = VID_DST_E_GPCNT,
		.vip_ctl = VID_DST_E_VIP_CTL,
		.pix_frmt = VID_DST_E_PIX_FRMT,
	},

	[SRAM_CH05] = {
		.i = SRAM_CH05,
		.name = "VID F",
		.cmds_start = VID_F_DOWN_CMDS,
		.ctrl_start = VID_F_IQ,
		.cdt = VID_F_CDT,
		.fifo_start = VID_F_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA6_PTR1,
		.ptr2_reg = DMA6_PTR2,
		.cnt1_reg = DMA6_CNT1,
		.cnt2_reg = DMA6_CNT2,
		.int_msk = VID_F_INT_MSK,
		.int_stat = VID_F_INT_STAT,
		.int_mstat = VID_F_INT_MSTAT,
		.dma_ctl = VID_DST_F_DMA_CTL,
		.gpcnt_ctl = VID_DST_F_GPCNT_CTL,
		.gpcnt = VID_DST_F_GPCNT,
		.vip_ctl = VID_DST_F_VIP_CTL,
		.pix_frmt = VID_DST_F_PIX_FRMT,
	},

	[SRAM_CH06] = {
		.i = SRAM_CH06,
		.name = "VID G",
		.cmds_start = VID_G_DOWN_CMDS,
		.ctrl_start = VID_G_IQ,
		.cdt = VID_G_CDT,
		.fifo_start = VID_G_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA7_PTR1,
		.ptr2_reg = DMA7_PTR2,
		.cnt1_reg = DMA7_CNT1,
		.cnt2_reg = DMA7_CNT2,
		.int_msk = VID_G_INT_MSK,
		.int_stat = VID_G_INT_STAT,
		.int_mstat = VID_G_INT_MSTAT,
		.dma_ctl = VID_DST_G_DMA_CTL,
		.gpcnt_ctl = VID_DST_G_GPCNT_CTL,
		.gpcnt = VID_DST_G_GPCNT,
		.vip_ctl = VID_DST_G_VIP_CTL,
		.pix_frmt = VID_DST_G_PIX_FRMT,
	},

	[SRAM_CH07] = {
		.i = SRAM_CH07,
		.name = "VID H",
		.cmds_start = VID_H_DOWN_CMDS,
		.ctrl_start = VID_H_IQ,
		.cdt = VID_H_CDT,
		.fifo_start = VID_H_DOWN_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA8_PTR1,
		.ptr2_reg = DMA8_PTR2,
		.cnt1_reg = DMA8_CNT1,
		.cnt2_reg = DMA8_CNT2,
		.int_msk = VID_H_INT_MSK,
		.int_stat = VID_H_INT_STAT,
		.int_mstat = VID_H_INT_MSTAT,
		.dma_ctl = VID_DST_H_DMA_CTL,
		.gpcnt_ctl = VID_DST_H_GPCNT_CTL,
		.gpcnt = VID_DST_H_GPCNT,
		.vip_ctl = VID_DST_H_VIP_CTL,
		.pix_frmt = VID_DST_H_PIX_FRMT,
	},

	[SRAM_CH08] = {
		.name = "audio from",
		.cmds_start = AUD_A_DOWN_CMDS,
		.ctrl_start = AUD_A_IQ,
		.cdt = AUD_A_CDT,
		.fifo_start = AUD_A_DOWN_CLUSTER_1,
		.fifo_size = AUDIO_CLUSTER_SIZE * 3,
		.ptr1_reg = DMA17_PTR1,
		.ptr2_reg = DMA17_PTR2,
		.cnt1_reg = DMA17_CNT1,
		.cnt2_reg = DMA17_CNT2,
	},

	[SRAM_CH09] = {
		.i = SRAM_CH09,
		.name = "VID Upstream I",
		.cmds_start = VID_I_UP_CMDS,
		.ctrl_start = VID_I_IQ,
		.cdt = VID_I_CDT,
		.fifo_start = VID_I_UP_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA15_PTR1,
		.ptr2_reg = DMA15_PTR2,
		.cnt1_reg = DMA15_CNT1,
		.cnt2_reg = DMA15_CNT2,
		.int_msk = VID_I_INT_MSK,
		.int_stat = VID_I_INT_STAT,
		.int_mstat = VID_I_INT_MSTAT,
		.dma_ctl = VID_SRC_I_DMA_CTL,
		.gpcnt_ctl = VID_SRC_I_GPCNT_CTL,
		.gpcnt = VID_SRC_I_GPCNT,

		.vid_fmt_ctl = VID_SRC_I_FMT_CTL,
		.vid_active_ctl1 = VID_SRC_I_ACTIVE_CTL1,
		.vid_active_ctl2 = VID_SRC_I_ACTIVE_CTL2,
		.vid_cdt_size = VID_SRC_I_CDT_SZ,
		.irq_bit = 8,
	},

	[SRAM_CH10] = {
		.i = SRAM_CH10,
		.name = "VID Upstream J",
		.cmds_start = VID_J_UP_CMDS,
		.ctrl_start = VID_J_IQ,
		.cdt = VID_J_CDT,
		.fifo_start = VID_J_UP_CLUSTER_1,
		.fifo_size = (VID_CLUSTER_SIZE << 2),
		.ptr1_reg = DMA16_PTR1,
		.ptr2_reg = DMA16_PTR2,
		.cnt1_reg = DMA16_CNT1,
		.cnt2_reg = DMA16_CNT2,
		.int_msk = VID_J_INT_MSK,
		.int_stat = VID_J_INT_STAT,
		.int_mstat = VID_J_INT_MSTAT,
		.dma_ctl = VID_SRC_J_DMA_CTL,
		.gpcnt_ctl = VID_SRC_J_GPCNT_CTL,
		.gpcnt = VID_SRC_J_GPCNT,

		.vid_fmt_ctl = VID_SRC_J_FMT_CTL,
		.vid_active_ctl1 = VID_SRC_J_ACTIVE_CTL1,
		.vid_active_ctl2 = VID_SRC_J_ACTIVE_CTL2,
		.vid_cdt_size = VID_SRC_J_CDT_SZ,
		.irq_bit = 9,
	},

	[SRAM_CH11] = {
		.i = SRAM_CH11,
		.name = "Audio Upstream Channel B",
		.cmds_start = AUD_B_UP_CMDS,
		.ctrl_start = AUD_B_IQ,
		.cdt = AUD_B_CDT,
		.fifo_start = AUD_B_UP_CLUSTER_1,
		.fifo_size = (AUDIO_CLUSTER_SIZE * 3),
		.ptr1_reg = DMA22_PTR1,
		.ptr2_reg = DMA22_PTR2,
		.cnt1_reg = DMA22_CNT1,
		.cnt2_reg = DMA22_CNT2,
		.int_msk = AUD_B_INT_MSK,
		.int_stat = AUD_B_INT_STAT,
		.int_mstat = AUD_B_INT_MSTAT,
		.dma_ctl = AUD_INT_DMA_CTL,
		.gpcnt_ctl = AUD_B_GPCNT_CTL,
		.gpcnt = AUD_B_GPCNT,
		.aud_length = AUD_B_LNGTH,
		.aud_cfg = AUD_B_CFG,
		.fld_aud_fifo_en = FLD_AUD_SRC_B_FIFO_EN,
		.fld_aud_risc_en = FLD_AUD_SRC_B_RISC_EN,
		.irq_bit = 11,
	},
};
EXPORT_SYMBOL(cx25821_sram_channels);

static int cx25821_risc_decode(u32 risc)
{
	static const char * const instr[16] = {
		[RISC_SYNC >> 28] = "sync",
		[RISC_WRITE >> 28] = "write",
		[RISC_WRITEC >> 28] = "writec",
		[RISC_READ >> 28] = "read",
		[RISC_READC >> 28] = "readc",
		[RISC_JUMP >> 28] = "jump",
		[RISC_SKIP >> 28] = "skip",
		[RISC_WRITERM >> 28] = "writerm",
		[RISC_WRITECM >> 28] = "writecm",
		[RISC_WRITECR >> 28] = "writecr",
	};
	static const int incr[16] = {
		[RISC_WRITE >> 28] = 3,
		[RISC_JUMP >> 28] = 3,
		[RISC_SKIP >> 28] = 1,
		[RISC_SYNC >> 28] = 1,
		[RISC_WRITERM >> 28] = 3,
		[RISC_WRITECM >> 28] = 3,
		[RISC_WRITECR >> 28] = 4,
	};
	static const char * const bits[] = {
		"12", "13", "14", "resync",
		"cnt0", "cnt1", "18", "19",
		"20", "21", "22", "23",
		"irq1", "irq2", "eol", "sol",
	};
	int i;

	pr_cont("0x%08x [ %s",
		risc, instr[risc >> 28] ? instr[risc >> 28] : "INVALID");
	for (i = ARRAY_SIZE(bits) - 1; i >= 0; i--) {
		if (risc & (1 << (i + 12)))
			pr_cont(" %s", bits[i]);
	}
	pr_cont(" count=%d ]\n", risc & 0xfff);
	return incr[risc >> 28] ? incr[risc >> 28] : 1;
}

static inline int i2c_slave_did_ack(struct i2c_adapter *i2c_adap)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	return cx_read(bus->reg_stat) & 0x01;
}

static void cx25821_registers_init(struct cx25821_dev *dev)
{
	u32 tmp;

	/* enable RUN_RISC in Pecos */
	cx_write(DEV_CNTRL2, 0x20);

	/* Set the master PCI interrupt masks to enable video, audio, MBIF,
	 * and GPIO interrupts
	 * I2C interrupt masking is handled by the I2C objects themselves. */
	cx_write(PCI_INT_MSK, 0x2001FFFF);

	tmp = cx_read(RDR_TLCTL0);
	tmp &= ~FLD_CFG_RCB_CK_EN;	/* Clear the RCB_CK_EN bit */
	cx_write(RDR_TLCTL0, tmp);

	/* PLL-A setting for the Audio Master Clock */
	cx_write(PLL_A_INT_FRAC, 0x9807A58B);

	/* PLL_A_POST = 0x1C, PLL_A_OUT_TO_PIN = 0x1 */
	cx_write(PLL_A_POST_STAT_BIST, 0x8000019C);

	/* clear reset bit [31] */
	tmp = cx_read(PLL_A_INT_FRAC);
	cx_write(PLL_A_INT_FRAC, tmp & 0x7FFFFFFF);

	/* PLL-B setting for Mobilygen Host Bus Interface */
	cx_write(PLL_B_INT_FRAC, 0x9883A86F);

	/* PLL_B_POST = 0xD, PLL_B_OUT_TO_PIN = 0x0 */
	cx_write(PLL_B_POST_STAT_BIST, 0x8000018D);

	/* clear reset bit [31] */
	tmp = cx_read(PLL_B_INT_FRAC);
	cx_write(PLL_B_INT_FRAC, tmp & 0x7FFFFFFF);

	/* PLL-C setting for video upstream channel */
	cx_write(PLL_C_INT_FRAC, 0x96A0EA3F);

	/* PLL_C_POST = 0x3, PLL_C_OUT_TO_PIN = 0x0 */
	cx_write(PLL_C_POST_STAT_BIST, 0x80000103);

	/* clear reset bit [31] */
	tmp = cx_read(PLL_C_INT_FRAC);
	cx_write(PLL_C_INT_FRAC, tmp & 0x7FFFFFFF);

	/* PLL-D setting for audio upstream channel */
	cx_write(PLL_D_INT_FRAC, 0x98757F5B);

	/* PLL_D_POST = 0x13, PLL_D_OUT_TO_PIN = 0x0 */
	cx_write(PLL_D_POST_STAT_BIST, 0x80000113);

	/* clear reset bit [31] */
	tmp = cx_read(PLL_D_INT_FRAC);
	cx_write(PLL_D_INT_FRAC, tmp & 0x7FFFFFFF);

	/* This selects the PLL C clock source for the video upstream channel
	 * I and J */
	tmp = cx_read(VID_CH_CLK_SEL);
	cx_write(VID_CH_CLK_SEL, (tmp & 0x00FFFFFF) | 0x24000000);

	/* 656/VIP SRC Upstream Channel I & J and 7 - Host Bus Interface for
	 * channel A-C
	 * select 656/VIP DST for downstream Channel A - C */
	tmp = cx_read(VID_CH_MODE_SEL);
	/* cx_write( VID_CH_MODE_SEL, tmp | 0x1B0001FF); */
	cx_write(VID_CH_MODE_SEL, tmp & 0xFFFFFE00);

	/* enables 656 port I and J as output */
	tmp = cx_read(CLK_RST);
	/* use external ALT_PLL_REF pin as its reference clock instead */
	tmp |= FLD_USE_ALT_PLL_REF;
	cx_write(CLK_RST, tmp & ~(FLD_VID_I_CLK_NOE | FLD_VID_J_CLK_NOE));

	mdelay(100);
}

int cx25821_sram_channel_setup(struct cx25821_dev *dev,
			       const struct sram_channel *ch,
			       unsigned int bpl, u32 risc)
{
	unsigned int i, lines;
	u32 cdt;

	if (ch->cmds_start == 0) {
		cx_write(ch->ptr1_reg, 0);
		cx_write(ch->ptr2_reg, 0);
		cx_write(ch->cnt2_reg, 0);
		cx_write(ch->cnt1_reg, 0);
		return 0;
	}

	bpl = (bpl + 7) & ~7;	/* alignment */
	cdt = ch->cdt;
	lines = ch->fifo_size / bpl;

	if (lines > 4)
		lines = 4;

	BUG_ON(lines < 2);

	cx_write(8 + 0, RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	cx_write(8 + 4, 8);
	cx_write(8 + 8, 0);

	/* write CDT */
	for (i = 0; i < lines; i++) {
		cx_write(cdt + 16 * i, ch->fifo_start + bpl * i);
		cx_write(cdt + 16 * i + 4, 0);
		cx_write(cdt + 16 * i + 8, 0);
		cx_write(cdt + 16 * i + 12, 0);
	}

	/* init the first cdt buffer */
	for (i = 0; i < 128; i++)
		cx_write(ch->fifo_start + 4 * i, i);

	/* write CMDS */
	if (ch->jumponly)
		cx_write(ch->cmds_start + 0, 8);
	else
		cx_write(ch->cmds_start + 0, risc);

	cx_write(ch->cmds_start + 4, 0);	/* 64 bits 63-32 */
	cx_write(ch->cmds_start + 8, cdt);
	cx_write(ch->cmds_start + 12, (lines * 16) >> 3);
	cx_write(ch->cmds_start + 16, ch->ctrl_start);

	if (ch->jumponly)
		cx_write(ch->cmds_start + 20, 0x80000000 | (64 >> 2));
	else
		cx_write(ch->cmds_start + 20, 64 >> 2);

	for (i = 24; i < 80; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt2_reg, (lines * 16) >> 3);
	cx_write(ch->cnt1_reg, (bpl >> 3) - 1);

	return 0;
}

int cx25821_sram_channel_setup_audio(struct cx25821_dev *dev,
				     const struct sram_channel *ch,
				     unsigned int bpl, u32 risc)
{
	unsigned int i, lines;
	u32 cdt;

	if (ch->cmds_start == 0) {
		cx_write(ch->ptr1_reg, 0);
		cx_write(ch->ptr2_reg, 0);
		cx_write(ch->cnt2_reg, 0);
		cx_write(ch->cnt1_reg, 0);
		return 0;
	}

	bpl = (bpl + 7) & ~7;	/* alignment */
	cdt = ch->cdt;
	lines = ch->fifo_size / bpl;

	if (lines > 3)
		lines = 3;	/* for AUDIO */

	BUG_ON(lines < 2);

	cx_write(8 + 0, RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	cx_write(8 + 4, 8);
	cx_write(8 + 8, 0);

	/* write CDT */
	for (i = 0; i < lines; i++) {
		cx_write(cdt + 16 * i, ch->fifo_start + bpl * i);
		cx_write(cdt + 16 * i + 4, 0);
		cx_write(cdt + 16 * i + 8, 0);
		cx_write(cdt + 16 * i + 12, 0);
	}

	/* write CMDS */
	if (ch->jumponly)
		cx_write(ch->cmds_start + 0, 8);
	else
		cx_write(ch->cmds_start + 0, risc);

	cx_write(ch->cmds_start + 4, 0);	/* 64 bits 63-32 */
	cx_write(ch->cmds_start + 8, cdt);
	cx_write(ch->cmds_start + 12, (lines * 16) >> 3);
	cx_write(ch->cmds_start + 16, ch->ctrl_start);

	/* IQ size */
	if (ch->jumponly)
		cx_write(ch->cmds_start + 20, 0x80000000 | (64 >> 2));
	else
		cx_write(ch->cmds_start + 20, 64 >> 2);

	/* zero out */
	for (i = 24; i < 80; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt2_reg, (lines * 16) >> 3);
	cx_write(ch->cnt1_reg, (bpl >> 3) - 1);

	return 0;
}
EXPORT_SYMBOL(cx25821_sram_channel_setup_audio);

void cx25821_sram_channel_dump(struct cx25821_dev *dev, const struct sram_channel *ch)
{
	static char *name[] = {
		"init risc lo",
		"init risc hi",
		"cdt base",
		"cdt size",
		"iq base",
		"iq size",
		"risc pc lo",
		"risc pc hi",
		"iq wr ptr",
		"iq rd ptr",
		"cdt current",
		"pci target lo",
		"pci target hi",
		"line / byte",
	};
	u32 risc;
	unsigned int i, j, n;

	pr_warn("%s: %s - dma channel status dump\n", dev->name, ch->name);
	for (i = 0; i < ARRAY_SIZE(name); i++)
		pr_warn("cmds + 0x%2x:   %-15s: 0x%08x\n",
			i * 4, name[i], cx_read(ch->cmds_start + 4 * i));

	j = i * 4;
	for (i = 0; i < 4;) {
		risc = cx_read(ch->cmds_start + 4 * (i + 14));
		pr_warn("cmds + 0x%2x:   risc%d: ", j + i * 4, i);
		i += cx25821_risc_decode(risc);
	}

	for (i = 0; i < (64 >> 2); i += n) {
		risc = cx_read(ch->ctrl_start + 4 * i);
		/* No consideration for bits 63-32 */

		pr_warn("ctrl + 0x%2x (0x%08x): iq %x: ",
			i * 4, ch->ctrl_start + 4 * i, i);
		n = cx25821_risc_decode(risc);
		for (j = 1; j < n; j++) {
			risc = cx_read(ch->ctrl_start + 4 * (i + j));
			pr_warn("ctrl + 0x%2x :   iq %x: 0x%08x [ arg #%d ]\n",
				4 * (i + j), i + j, risc, j);
		}
	}

	pr_warn("        :   fifo: 0x%08x -> 0x%x\n",
		ch->fifo_start, ch->fifo_start + ch->fifo_size);
	pr_warn("        :   ctrl: 0x%08x -> 0x%x\n",
		ch->ctrl_start, ch->ctrl_start + 6 * 16);
	pr_warn("        :   ptr1_reg: 0x%08x\n",
		cx_read(ch->ptr1_reg));
	pr_warn("        :   ptr2_reg: 0x%08x\n",
		cx_read(ch->ptr2_reg));
	pr_warn("        :   cnt1_reg: 0x%08x\n",
		cx_read(ch->cnt1_reg));
	pr_warn("        :   cnt2_reg: 0x%08x\n",
		cx_read(ch->cnt2_reg));
}

void cx25821_sram_channel_dump_audio(struct cx25821_dev *dev,
				     const struct sram_channel *ch)
{
	static const char * const name[] = {
		"init risc lo",
		"init risc hi",
		"cdt base",
		"cdt size",
		"iq base",
		"iq size",
		"risc pc lo",
		"risc pc hi",
		"iq wr ptr",
		"iq rd ptr",
		"cdt current",
		"pci target lo",
		"pci target hi",
		"line / byte",
	};

	u32 risc, value, tmp;
	unsigned int i, j, n;

	pr_info("\n%s: %s - dma Audio channel status dump\n",
		dev->name, ch->name);

	for (i = 0; i < ARRAY_SIZE(name); i++)
		pr_info("%s: cmds + 0x%2x:   %-15s: 0x%08x\n",
			dev->name, i * 4, name[i],
			cx_read(ch->cmds_start + 4 * i));

	j = i * 4;
	for (i = 0; i < 4;) {
		risc = cx_read(ch->cmds_start + 4 * (i + 14));
		pr_warn("cmds + 0x%2x:   risc%d: ", j + i * 4, i);
		i += cx25821_risc_decode(risc);
	}

	for (i = 0; i < (64 >> 2); i += n) {
		risc = cx_read(ch->ctrl_start + 4 * i);
		/* No consideration for bits 63-32 */

		pr_warn("ctrl + 0x%2x (0x%08x): iq %x: ",
			i * 4, ch->ctrl_start + 4 * i, i);
		n = cx25821_risc_decode(risc);

		for (j = 1; j < n; j++) {
			risc = cx_read(ch->ctrl_start + 4 * (i + j));
			pr_warn("ctrl + 0x%2x :   iq %x: 0x%08x [ arg #%d ]\n",
				4 * (i + j), i + j, risc, j);
		}
	}

	pr_warn("        :   fifo: 0x%08x -> 0x%x\n",
		ch->fifo_start, ch->fifo_start + ch->fifo_size);
	pr_warn("        :   ctrl: 0x%08x -> 0x%x\n",
		ch->ctrl_start, ch->ctrl_start + 6 * 16);
	pr_warn("        :   ptr1_reg: 0x%08x\n",
		cx_read(ch->ptr1_reg));
	pr_warn("        :   ptr2_reg: 0x%08x\n",
		cx_read(ch->ptr2_reg));
	pr_warn("        :   cnt1_reg: 0x%08x\n",
		cx_read(ch->cnt1_reg));
	pr_warn("        :   cnt2_reg: 0x%08x\n",
		cx_read(ch->cnt2_reg));

	for (i = 0; i < 4; i++) {
		risc = cx_read(ch->cmds_start + 56 + (i * 4));
		pr_warn("instruction %d = 0x%x\n", i, risc);
	}

	/* read data from the first cdt buffer */
	risc = cx_read(AUD_A_CDT);
	pr_warn("\nread cdt loc=0x%x\n", risc);
	for (i = 0; i < 8; i++) {
		n = cx_read(risc + i * 4);
		pr_cont("0x%x ", n);
	}
	pr_cont("\n\n");

	value = cx_read(CLK_RST);
	CX25821_INFO(" CLK_RST = 0x%x\n\n", value);

	value = cx_read(PLL_A_POST_STAT_BIST);
	CX25821_INFO(" PLL_A_POST_STAT_BIST = 0x%x\n\n", value);
	value = cx_read(PLL_A_INT_FRAC);
	CX25821_INFO(" PLL_A_INT_FRAC = 0x%x\n\n", value);

	value = cx_read(PLL_B_POST_STAT_BIST);
	CX25821_INFO(" PLL_B_POST_STAT_BIST = 0x%x\n\n", value);
	value = cx_read(PLL_B_INT_FRAC);
	CX25821_INFO(" PLL_B_INT_FRAC = 0x%x\n\n", value);

	value = cx_read(PLL_C_POST_STAT_BIST);
	CX25821_INFO(" PLL_C_POST_STAT_BIST = 0x%x\n\n", value);
	value = cx_read(PLL_C_INT_FRAC);
	CX25821_INFO(" PLL_C_INT_FRAC = 0x%x\n\n", value);

	value = cx_read(PLL_D_POST_STAT_BIST);
	CX25821_INFO(" PLL_D_POST_STAT_BIST = 0x%x\n\n", value);
	value = cx_read(PLL_D_INT_FRAC);
	CX25821_INFO(" PLL_D_INT_FRAC = 0x%x\n\n", value);

	value = cx25821_i2c_read(&dev->i2c_bus[0], AFE_AB_DIAG_CTRL, &tmp);
	CX25821_INFO(" AFE_AB_DIAG_CTRL (0x10900090) = 0x%x\n\n", value);
}
EXPORT_SYMBOL(cx25821_sram_channel_dump_audio);

static void cx25821_shutdown(struct cx25821_dev *dev)
{
	int i;

	/* disable RISC controller */
	cx_write(DEV_CNTRL2, 0);

	/* Disable Video A/B activity */
	for (i = 0; i < VID_CHANNEL_NUM; i++) {
		cx_write(dev->channels[i].sram_channels->dma_ctl, 0);
		cx_write(dev->channels[i].sram_channels->int_msk, 0);
	}

	for (i = VID_UPSTREAM_SRAM_CHANNEL_I;
		i <= VID_UPSTREAM_SRAM_CHANNEL_J; i++) {
		cx_write(dev->channels[i].sram_channels->dma_ctl, 0);
		cx_write(dev->channels[i].sram_channels->int_msk, 0);
	}

	/* Disable Audio activity */
	cx_write(AUD_INT_DMA_CTL, 0);

	/* Disable Serial port */
	cx_write(UART_CTL, 0);

	/* Disable Interrupts */
	cx_write(PCI_INT_MSK, 0);
	cx_write(AUD_A_INT_MSK, 0);
}

void cx25821_set_pixel_format(struct cx25821_dev *dev, int channel_select,
			      u32 format)
{
	if (channel_select <= 7 && channel_select >= 0) {
		cx_write(dev->channels[channel_select].sram_channels->pix_frmt,
				format);
	}
	dev->channels[channel_select].pixel_formats = format;
}

static void cx25821_set_vip_mode(struct cx25821_dev *dev,
				 const struct sram_channel *ch)
{
	cx_write(ch->pix_frmt, PIXEL_FRMT_422);
	cx_write(ch->vip_ctl, PIXEL_ENGINE_VIP1);
}

static void cx25821_initialize(struct cx25821_dev *dev)
{
	int i;

	dprintk(1, "%s()\n", __func__);

	cx25821_shutdown(dev);
	cx_write(PCI_INT_STAT, 0xffffffff);

	for (i = 0; i < VID_CHANNEL_NUM; i++)
		cx_write(dev->channels[i].sram_channels->int_stat, 0xffffffff);

	cx_write(AUD_A_INT_STAT, 0xffffffff);
	cx_write(AUD_B_INT_STAT, 0xffffffff);
	cx_write(AUD_C_INT_STAT, 0xffffffff);
	cx_write(AUD_D_INT_STAT, 0xffffffff);
	cx_write(AUD_E_INT_STAT, 0xffffffff);

	cx_write(CLK_DELAY, cx_read(CLK_DELAY) & 0x80000000);
	cx_write(PAD_CTRL, 0x12);	/* for I2C */
	cx25821_registers_init(dev);	/* init Pecos registers */
	mdelay(100);

	for (i = 0; i < VID_CHANNEL_NUM; i++) {
		cx25821_set_vip_mode(dev, dev->channels[i].sram_channels);
		cx25821_sram_channel_setup(dev, dev->channels[i].sram_channels,
						1440, 0);
		dev->channels[i].pixel_formats = PIXEL_FRMT_422;
		dev->channels[i].use_cif_resolution = 0;
	}

	/* Probably only affect Downstream */
	for (i = VID_UPSTREAM_SRAM_CHANNEL_I;
		i <= VID_UPSTREAM_SRAM_CHANNEL_J; i++) {
		dev->channels[i].pixel_formats = PIXEL_FRMT_422;
		cx25821_set_vip_mode(dev, dev->channels[i].sram_channels);
	}

	cx25821_sram_channel_setup_audio(dev,
			dev->channels[SRAM_CH08].sram_channels, 128, 0);

	cx25821_gpio_init(dev);
}

static int cx25821_get_resources(struct cx25821_dev *dev)
{
	if (request_mem_region(pci_resource_start(dev->pci, 0),
				pci_resource_len(dev->pci, 0), dev->name))
		return 0;

	pr_err("%s: can't get MMIO memory @ 0x%llx\n",
		dev->name, (unsigned long long)pci_resource_start(dev->pci, 0));

	return -EBUSY;
}

static void cx25821_dev_checkrevision(struct cx25821_dev *dev)
{
	dev->hwrevision = cx_read(RDR_CFG2) & 0xff;

	pr_info("Hardware revision = 0x%02x\n", dev->hwrevision);
}

static void cx25821_iounmap(struct cx25821_dev *dev)
{
	if (dev == NULL)
		return;

	/* Releasing IO memory */
	if (dev->lmmio != NULL) {
		iounmap(dev->lmmio);
		dev->lmmio = NULL;
	}
}

static int cx25821_dev_setup(struct cx25821_dev *dev)
{
	static unsigned int cx25821_devcount;
	int i;

	mutex_init(&dev->lock);

	dev->nr = ++cx25821_devcount;
	sprintf(dev->name, "cx25821[%d]", dev->nr);

	if (dev->pci->device != 0x8210) {
		pr_info("%s(): Exiting. Incorrect Hardware device = 0x%02x\n",
			__func__, dev->pci->device);
		return -ENODEV;
	}
	pr_info("Athena Hardware device = 0x%02x\n", dev->pci->device);

	/* Apply a sensible clock frequency for the PCIe bridge */
	dev->clk_freq = 28000000;
	for (i = 0; i < MAX_VID_CHANNEL_NUM; i++) {
		dev->channels[i].dev = dev;
		dev->channels[i].id = i;
		dev->channels[i].sram_channels = &cx25821_sram_channels[i];
	}

	if (dev->nr > 1)
		CX25821_INFO("dev->nr > 1!");

	/* board config */
	dev->board = 1;		/* card[dev->nr]; */
	dev->_max_num_decoders = MAX_DECODERS;

	dev->pci_bus = dev->pci->bus->number;
	dev->pci_slot = PCI_SLOT(dev->pci->devfn);
	dev->pci_irqmask = 0x001f00;

	/* External Master 1 Bus */
	dev->i2c_bus[0].nr = 0;
	dev->i2c_bus[0].dev = dev;
	dev->i2c_bus[0].reg_stat = I2C1_STAT;
	dev->i2c_bus[0].reg_ctrl = I2C1_CTRL;
	dev->i2c_bus[0].reg_addr = I2C1_ADDR;
	dev->i2c_bus[0].reg_rdata = I2C1_RDATA;
	dev->i2c_bus[0].reg_wdata = I2C1_WDATA;
	dev->i2c_bus[0].i2c_period = (0x07 << 24);	/* 1.95MHz */

	if (cx25821_get_resources(dev) < 0) {
		pr_err("%s: No more PCIe resources for subsystem: %04x:%04x\n",
		       dev->name, dev->pci->subsystem_vendor,
		       dev->pci->subsystem_device);

		cx25821_devcount--;
		return -EBUSY;
	}

	/* PCIe stuff */
	dev->base_io_addr = pci_resource_start(dev->pci, 0);

	if (!dev->base_io_addr) {
		CX25821_ERR("No PCI Memory resources, exiting!\n");
		return -ENODEV;
	}

	dev->lmmio = ioremap(dev->base_io_addr, pci_resource_len(dev->pci, 0));

	if (!dev->lmmio) {
		CX25821_ERR("ioremap failed, maybe increasing __VMALLOC_RESERVE in page.h\n");
		cx25821_iounmap(dev);
		return -ENOMEM;
	}

	dev->bmmio = (u8 __iomem *) dev->lmmio;

	pr_info("%s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
		dev->name, dev->pci->subsystem_vendor,
		dev->pci->subsystem_device, cx25821_boards[dev->board].name,
		dev->board, card[dev->nr] == dev->board ?
		"insmod option" : "autodetected");

	/* init hardware */
	cx25821_initialize(dev);

	cx25821_i2c_register(&dev->i2c_bus[0]);
/*  cx25821_i2c_register(&dev->i2c_bus[1]);
 *  cx25821_i2c_register(&dev->i2c_bus[2]); */

	if (medusa_video_init(dev) < 0)
		CX25821_ERR("%s(): Failed to initialize medusa!\n", __func__);

	cx25821_video_register(dev);

	cx25821_dev_checkrevision(dev);
	return 0;
}

void cx25821_dev_unregister(struct cx25821_dev *dev)
{
	int i;

	if (!dev->base_io_addr)
		return;

	release_mem_region(dev->base_io_addr, pci_resource_len(dev->pci, 0));

	for (i = 0; i < MAX_VID_CAP_CHANNEL_NUM - 1; i++) {
		if (i == SRAM_CH08) /* audio channel */
			continue;
		/*
		 * TODO: enable when video output is properly
		 * supported.
		if (i == SRAM_CH09 || i == SRAM_CH10)
			cx25821_free_mem_upstream(&dev->channels[i]);
		 */
		cx25821_video_unregister(dev, i);
	}

	cx25821_i2c_unregister(&dev->i2c_bus[0]);
	cx25821_iounmap(dev);
}
EXPORT_SYMBOL(cx25821_dev_unregister);

int cx25821_riscmem_alloc(struct pci_dev *pci,
		       struct cx25821_riscmem *risc,
		       unsigned int size)
{
	__le32 *cpu;
	dma_addr_t dma = 0;

	if (NULL != risc->cpu && risc->size < size)
		pci_free_consistent(pci, risc->size, risc->cpu, risc->dma);
	if (NULL == risc->cpu) {
		cpu = pci_zalloc_consistent(pci, size, &dma);
		if (NULL == cpu)
			return -ENOMEM;
		risc->cpu  = cpu;
		risc->dma  = dma;
		risc->size = size;
	}
	return 0;
}
EXPORT_SYMBOL(cx25821_riscmem_alloc);

static __le32 *cx25821_risc_field(__le32 * rp, struct scatterlist *sglist,
				  unsigned int offset, u32 sync_line,
				  unsigned int bpl, unsigned int padding,
				  unsigned int lines, bool jump)
{
	struct scatterlist *sg;
	unsigned int line, todo;

	if (jump) {
		*(rp++) = cpu_to_le32(RISC_JUMP);
		*(rp++) = cpu_to_le32(0);
		*(rp++) = cpu_to_le32(0); /* bits 63-32 */
	}

	/* sync instruction */
	if (sync_line != NO_SYNC_LINE)
		*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg = sg_next(sg);
		}
		if (bpl <= sg_dma_len(sg) - offset) {
			/* fits into current chunk */
			*(rp++) = cpu_to_le32(RISC_WRITE | RISC_SOL | RISC_EOL |
					bpl);
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			offset += bpl;
		} else {
			/* scanline needs to be split */
			todo = bpl;
			*(rp++) = cpu_to_le32(RISC_WRITE | RISC_SOL |
					(sg_dma_len(sg) - offset));
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			todo -= (sg_dma_len(sg) - offset);
			offset = 0;
			sg = sg_next(sg);
			while (todo > sg_dma_len(sg)) {
				*(rp++) = cpu_to_le32(RISC_WRITE |
						sg_dma_len(sg));
				*(rp++) = cpu_to_le32(sg_dma_address(sg));
				*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
				todo -= sg_dma_len(sg);
				sg = sg_next(sg);
			}
			*(rp++) = cpu_to_le32(RISC_WRITE | RISC_EOL | todo);
			*(rp++) = cpu_to_le32(sg_dma_address(sg));
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			offset += todo;
		}

		offset += padding;
	}

	return rp;
}

int cx25821_risc_buffer(struct pci_dev *pci, struct cx25821_riscmem *risc,
			struct scatterlist *sglist, unsigned int top_offset,
			unsigned int bottom_offset, unsigned int bpl,
			unsigned int padding, unsigned int lines)
{
	u32 instructions;
	u32 fields;
	__le32 *rp;
	int rc;

	fields = 0;
	if (UNSET != top_offset)
		fields++;
	if (UNSET != bottom_offset)
		fields++;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + syncs + jump (all 3 dwords).  Padding
	   can cause next bpl to start close to a page border.  First DMA
	   region may be smaller than PAGE_SIZE */
	/* write and jump need and extra dword */
	instructions = fields * (1 + ((bpl + padding) * lines) / PAGE_SIZE +
			lines);
	instructions += 5;
	rc = cx25821_riscmem_alloc(pci, risc, instructions * 12);

	if (rc < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;

	if (UNSET != top_offset) {
		rp = cx25821_risc_field(rp, sglist, top_offset, 0, bpl, padding,
					lines, true);
	}

	if (UNSET != bottom_offset) {
		rp = cx25821_risc_field(rp, sglist, bottom_offset, 0x200, bpl,
					padding, lines, UNSET == top_offset);
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	BUG_ON((risc->jmp - risc->cpu + 3) * sizeof(*risc->cpu) > risc->size);

	return 0;
}

static __le32 *cx25821_risc_field_audio(__le32 * rp, struct scatterlist *sglist,
					unsigned int offset, u32 sync_line,
					unsigned int bpl, unsigned int padding,
					unsigned int lines, unsigned int lpi)
{
	struct scatterlist *sg;
	unsigned int line, todo, sol;

	/* sync instruction */
	if (sync_line != NO_SYNC_LINE)
		*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg = sg_next(sg);
		}

		if (lpi && line > 0 && !(line % lpi))
			sol = RISC_SOL | RISC_IRQ1 | RISC_CNT_INC;
		else
			sol = RISC_SOL;

		if (bpl <= sg_dma_len(sg) - offset) {
			/* fits into current chunk */
			*(rp++) = cpu_to_le32(RISC_WRITE | sol | RISC_EOL |
					bpl);
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			offset += bpl;
		} else {
			/* scanline needs to be split */
			todo = bpl;
			*(rp++) = cpu_to_le32(RISC_WRITE | sol |
					(sg_dma_len(sg) - offset));
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			todo -= (sg_dma_len(sg) - offset);
			offset = 0;
			sg = sg_next(sg);
			while (todo > sg_dma_len(sg)) {
				*(rp++) = cpu_to_le32(RISC_WRITE |
						sg_dma_len(sg));
				*(rp++) = cpu_to_le32(sg_dma_address(sg));
				*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
				todo -= sg_dma_len(sg);
				sg = sg_next(sg);
			}
			*(rp++) = cpu_to_le32(RISC_WRITE | RISC_EOL | todo);
			*(rp++) = cpu_to_le32(sg_dma_address(sg));
			*(rp++) = cpu_to_le32(0);	/* bits 63-32 */
			offset += todo;
		}
		offset += padding;
	}

	return rp;
}

int cx25821_risc_databuffer_audio(struct pci_dev *pci,
				  struct cx25821_riscmem *risc,
				  struct scatterlist *sglist,
				  unsigned int bpl,
				  unsigned int lines, unsigned int lpi)
{
	u32 instructions;
	__le32 *rp;
	int rc;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + syncs + jump (all 2 dwords).  Here
	   there is no padding and no sync.  First DMA region may be smaller
	   than PAGE_SIZE */
	/* Jump and write need an extra dword */
	instructions = 1 + (bpl * lines) / PAGE_SIZE + lines;
	instructions += 1;

	rc = cx25821_riscmem_alloc(pci, risc, instructions * 12);
	if (rc < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	rp = cx25821_risc_field_audio(rp, sglist, 0, NO_SYNC_LINE, bpl, 0,
				      lines, lpi);

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	return 0;
}
EXPORT_SYMBOL(cx25821_risc_databuffer_audio);

void cx25821_free_buffer(struct cx25821_dev *dev, struct cx25821_buffer *buf)
{
	BUG_ON(in_interrupt());
	if (WARN_ON(buf->risc.size == 0))
		return;
	pci_free_consistent(dev->pci,
			buf->risc.size, buf->risc.cpu, buf->risc.dma);
	memset(&buf->risc, 0, sizeof(buf->risc));
}

static irqreturn_t cx25821_irq(int irq, void *dev_id)
{
	struct cx25821_dev *dev = dev_id;
	u32 pci_status;
	u32 vid_status;
	int i, handled = 0;
	u32 mask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

	pci_status = cx_read(PCI_INT_STAT);

	if (pci_status == 0)
		goto out;

	for (i = 0; i < VID_CHANNEL_NUM; i++) {
		if (pci_status & mask[i]) {
			vid_status = cx_read(dev->channels[i].
				sram_channels->int_stat);

			if (vid_status)
				handled += cx25821_video_irq(dev, i,
						vid_status);

			cx_write(PCI_INT_STAT, mask[i]);
		}
	}

out:
	return IRQ_RETVAL(handled);
}

void cx25821_print_irqbits(char *name, char *tag, char **strings,
			   int len, u32 bits, u32 mask)
{
	unsigned int i;

	printk(KERN_DEBUG pr_fmt("%s: %s [0x%x]"), name, tag, bits);

	for (i = 0; i < len; i++) {
		if (!(bits & (1 << i)))
			continue;
		if (strings[i])
			pr_cont(" %s", strings[i]);
		else
			pr_cont(" %d", i);
		if (!(mask & (1 << i)))
			continue;
		pr_cont("*");
	}
	pr_cont("\n");
}
EXPORT_SYMBOL(cx25821_print_irqbits);

struct cx25821_dev *cx25821_dev_get(struct pci_dev *pci)
{
	struct cx25821_dev *dev = pci_get_drvdata(pci);
	return dev;
}
EXPORT_SYMBOL(cx25821_dev_get);

static int cx25821_initdev(struct pci_dev *pci_dev,
			   const struct pci_device_id *pci_id)
{
	struct cx25821_dev *dev;
	int err = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	err = v4l2_device_register(&pci_dev->dev, &dev->v4l2_dev);
	if (err < 0)
		goto fail_free;

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;

		pr_info("pci enable failed!\n");

		goto fail_unregister_device;
	}

	err = cx25821_dev_setup(dev);
	if (err)
		goto fail_unregister_pci;

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &dev->pci_lat);
	pr_info("%s/0: found at %s, rev: %d, irq: %d, latency: %d, mmio: 0x%llx\n",
		dev->name, pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
		dev->pci_lat, (unsigned long long)dev->base_io_addr);

	pci_set_master(pci_dev);
	err = pci_set_dma_mask(pci_dev, 0xffffffff);
	if (err) {
		pr_err("%s/0: Oops: no 32bit PCI DMA ???\n", dev->name);
		err = -EIO;
		goto fail_irq;
	}

	err = request_irq(pci_dev->irq, cx25821_irq,
			IRQF_SHARED, dev->name, dev);

	if (err < 0) {
		pr_err("%s: can't get IRQ %d\n", dev->name, pci_dev->irq);
		goto fail_irq;
	}

	return 0;

fail_irq:
	pr_info("cx25821_initdev() can't get IRQ !\n");
	cx25821_dev_unregister(dev);

fail_unregister_pci:
	pci_disable_device(pci_dev);
fail_unregister_device:
	v4l2_device_unregister(&dev->v4l2_dev);

fail_free:
	kfree(dev);
	return err;
}

static void cx25821_finidev(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct cx25821_dev *dev = get_cx25821(v4l2_dev);

	cx25821_shutdown(dev);
	pci_disable_device(pci_dev);

	/* unregister stuff */
	if (pci_dev->irq)
		free_irq(pci_dev->irq, dev);

	cx25821_dev_unregister(dev);
	v4l2_device_unregister(v4l2_dev);
	kfree(dev);
}

static const struct pci_device_id cx25821_pci_tbl[] = {
	{
		/* CX25821 Athena */
		.vendor = 0x14f1,
		.device = 0x8210,
		.subvendor = 0x14f1,
		.subdevice = 0x0920,
	}, {
		/* CX25821 No Brand */
		.vendor = 0x14f1,
		.device = 0x8210,
		.subvendor = 0x0000,
		.subdevice = 0x0000,
	}, {
		/* --- end of list --- */
	}
};

MODULE_DEVICE_TABLE(pci, cx25821_pci_tbl);

static struct pci_driver cx25821_pci_driver = {
	.name = "cx25821",
	.id_table = cx25821_pci_tbl,
	.probe = cx25821_initdev,
	.remove = cx25821_finidev,
	/* TODO */
	.suspend = NULL,
	.resume = NULL,
};

static int __init cx25821_init(void)
{
	pr_info("driver version %d.%d.%d loaded\n",
		(CX25821_VERSION_CODE >> 16) & 0xff,
		(CX25821_VERSION_CODE >> 8) & 0xff,
		CX25821_VERSION_CODE & 0xff);
	return pci_register_driver(&cx25821_pci_driver);
}

static void __exit cx25821_fini(void)
{
	pci_unregister_driver(&cx25821_pci_driver);
}

module_init(cx25821_init);
module_exit(cx25821_fini);
