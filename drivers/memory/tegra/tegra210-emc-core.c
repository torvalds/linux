// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/mc.h>

#include "tegra210-emc.h"
#include "tegra210-mc.h"

/* CLK_RST_CONTROLLER_CLK_SOURCE_EMC */
#define EMC_CLK_EMC_2X_CLK_SRC_SHIFT			29
#define EMC_CLK_EMC_2X_CLK_SRC_MASK			\
	(0x7 << EMC_CLK_EMC_2X_CLK_SRC_SHIFT)
#define EMC_CLK_SOURCE_PLLM_LJ				0x4
#define EMC_CLK_SOURCE_PLLMB_LJ				0x5
#define EMC_CLK_FORCE_CC_TRIGGER			BIT(27)
#define EMC_CLK_MC_EMC_SAME_FREQ			BIT(16)
#define EMC_CLK_EMC_2X_CLK_DIVISOR_SHIFT		0
#define EMC_CLK_EMC_2X_CLK_DIVISOR_MASK			\
	(0xff << EMC_CLK_EMC_2X_CLK_DIVISOR_SHIFT)

/* CLK_RST_CONTROLLER_CLK_SOURCE_EMC_DLL */
#define DLL_CLK_EMC_DLL_CLK_SRC_SHIFT			29
#define DLL_CLK_EMC_DLL_CLK_SRC_MASK			\
	(0x7 << DLL_CLK_EMC_DLL_CLK_SRC_SHIFT)
#define DLL_CLK_EMC_DLL_DDLL_CLK_SEL_SHIFT		10
#define DLL_CLK_EMC_DLL_DDLL_CLK_SEL_MASK		\
	(0x3 << DLL_CLK_EMC_DLL_DDLL_CLK_SEL_SHIFT)
#define PLLM_VCOA					0
#define PLLM_VCOB					1
#define EMC_DLL_SWITCH_OUT				2
#define DLL_CLK_EMC_DLL_CLK_DIVISOR_SHIFT		0
#define DLL_CLK_EMC_DLL_CLK_DIVISOR_MASK		\
	(0xff << DLL_CLK_EMC_DLL_CLK_DIVISOR_SHIFT)

/* MC_EMEM_ARB_MISC0 */
#define MC_EMEM_ARB_MISC0_EMC_SAME_FREQ			BIT(27)

/* EMC_DATA_BRLSHFT_X */
#define EMC0_EMC_DATA_BRLSHFT_0_INDEX	2
#define EMC1_EMC_DATA_BRLSHFT_0_INDEX	3
#define EMC0_EMC_DATA_BRLSHFT_1_INDEX	4
#define EMC1_EMC_DATA_BRLSHFT_1_INDEX	5

#define TRIM_REG(chan, rank, reg, byte)					\
	(((EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ## reg ##	\
	   _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte ## _MASK &	\
	   next->trim_regs[EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ##		\
				 rank ## _ ## reg ## _INDEX]) >>	\
	  EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ## reg ##	\
	  _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte ## _SHIFT)	\
	 +								\
	 (((EMC_DATA_BRLSHFT_ ## rank ## _RANK ## rank ## _BYTE ##	\
	    byte ## _DATA_BRLSHFT_MASK &				\
	    next->trim_perch_regs[EMC ## chan ##			\
			      _EMC_DATA_BRLSHFT_ ## rank ## _INDEX]) >>	\
	   EMC_DATA_BRLSHFT_ ## rank ## _RANK ## rank ## _BYTE ##	\
	   byte ## _DATA_BRLSHFT_SHIFT) * 64))

#define CALC_TEMP(rank, reg, byte1, byte2, n)				\
	(((new[n] << EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ##	\
	   reg ## _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte1 ## _SHIFT) & \
	  EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ## reg ##	\
	  _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte1 ## _MASK)	\
	 |								\
	 ((new[n + 1] << EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ##\
	   reg ## _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte2 ## _SHIFT) & \
	  EMC_PMACRO_OB_DDLL_LONG_DQ_RANK ## rank ## _ ## reg ##	\
	  _OB_DDLL_LONG_DQ_RANK ## rank ## _BYTE ## byte2 ## _MASK))

#define REFRESH_SPEEDUP(value, speedup) \
		(((value) & 0xffff0000) | ((value) & 0xffff) * (speedup))

#define LPDDR2_MR4_SRR GENMASK(2, 0)

static const struct tegra210_emc_sequence *tegra210_emc_sequences[] = {
	&tegra210_emc_r21021,
};

static const struct tegra210_emc_table_register_offsets
tegra210_emc_table_register_offsets = {
	.burst = {
		EMC_RC,
		EMC_RFC,
		EMC_RFCPB,
		EMC_REFCTRL2,
		EMC_RFC_SLR,
		EMC_RAS,
		EMC_RP,
		EMC_R2W,
		EMC_W2R,
		EMC_R2P,
		EMC_W2P,
		EMC_R2R,
		EMC_TPPD,
		EMC_CCDMW,
		EMC_RD_RCD,
		EMC_WR_RCD,
		EMC_RRD,
		EMC_REXT,
		EMC_WEXT,
		EMC_WDV_CHK,
		EMC_WDV,
		EMC_WSV,
		EMC_WEV,
		EMC_WDV_MASK,
		EMC_WS_DURATION,
		EMC_WE_DURATION,
		EMC_QUSE,
		EMC_QUSE_WIDTH,
		EMC_IBDLY,
		EMC_OBDLY,
		EMC_EINPUT,
		EMC_MRW6,
		EMC_EINPUT_DURATION,
		EMC_PUTERM_EXTRA,
		EMC_PUTERM_WIDTH,
		EMC_QRST,
		EMC_QSAFE,
		EMC_RDV,
		EMC_RDV_MASK,
		EMC_RDV_EARLY,
		EMC_RDV_EARLY_MASK,
		EMC_REFRESH,
		EMC_BURST_REFRESH_NUM,
		EMC_PRE_REFRESH_REQ_CNT,
		EMC_PDEX2WR,
		EMC_PDEX2RD,
		EMC_PCHG2PDEN,
		EMC_ACT2PDEN,
		EMC_AR2PDEN,
		EMC_RW2PDEN,
		EMC_CKE2PDEN,
		EMC_PDEX2CKE,
		EMC_PDEX2MRR,
		EMC_TXSR,
		EMC_TXSRDLL,
		EMC_TCKE,
		EMC_TCKESR,
		EMC_TPD,
		EMC_TFAW,
		EMC_TRPAB,
		EMC_TCLKSTABLE,
		EMC_TCLKSTOP,
		EMC_MRW7,
		EMC_TREFBW,
		EMC_ODT_WRITE,
		EMC_FBIO_CFG5,
		EMC_FBIO_CFG7,
		EMC_CFG_DIG_DLL,
		EMC_CFG_DIG_DLL_PERIOD,
		EMC_PMACRO_IB_RXRT,
		EMC_CFG_PIPE_1,
		EMC_CFG_PIPE_2,
		EMC_PMACRO_QUSE_DDLL_RANK0_4,
		EMC_PMACRO_QUSE_DDLL_RANK0_5,
		EMC_PMACRO_QUSE_DDLL_RANK1_4,
		EMC_PMACRO_QUSE_DDLL_RANK1_5,
		EMC_MRW8,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_2,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_3,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_5,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_2,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_3,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4,
		EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_5,
		EMC_PMACRO_DDLL_LONG_CMD_0,
		EMC_PMACRO_DDLL_LONG_CMD_1,
		EMC_PMACRO_DDLL_LONG_CMD_2,
		EMC_PMACRO_DDLL_LONG_CMD_3,
		EMC_PMACRO_DDLL_LONG_CMD_4,
		EMC_PMACRO_DDLL_SHORT_CMD_0,
		EMC_PMACRO_DDLL_SHORT_CMD_1,
		EMC_PMACRO_DDLL_SHORT_CMD_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE0_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE1_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE2_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE3_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE4_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE5_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE6_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE7_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD0_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD1_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD2_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD3_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE0_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE1_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE2_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE3_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE4_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE5_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE6_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE7_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD0_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD0_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD0_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD0_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD1_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD1_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD1_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD1_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD2_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD2_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD2_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD2_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD3_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD3_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD3_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_CMD3_3,
		EMC_TXDSRVTTGEN,
		EMC_FDPD_CTRL_DQ,
		EMC_FDPD_CTRL_CMD,
		EMC_FBIO_SPARE,
		EMC_ZCAL_INTERVAL,
		EMC_ZCAL_WAIT_CNT,
		EMC_MRS_WAIT_CNT,
		EMC_MRS_WAIT_CNT2,
		EMC_AUTO_CAL_CHANNEL,
		EMC_DLL_CFG_0,
		EMC_DLL_CFG_1,
		EMC_PMACRO_AUTOCAL_CFG_COMMON,
		EMC_PMACRO_ZCTRL,
		EMC_CFG,
		EMC_CFG_PIPE,
		EMC_DYN_SELF_REF_CONTROL,
		EMC_QPOP,
		EMC_DQS_BRLSHFT_0,
		EMC_DQS_BRLSHFT_1,
		EMC_CMD_BRLSHFT_2,
		EMC_CMD_BRLSHFT_3,
		EMC_PMACRO_PAD_CFG_CTRL,
		EMC_PMACRO_DATA_PAD_RX_CTRL,
		EMC_PMACRO_CMD_PAD_RX_CTRL,
		EMC_PMACRO_DATA_RX_TERM_MODE,
		EMC_PMACRO_CMD_RX_TERM_MODE,
		EMC_PMACRO_CMD_PAD_TX_CTRL,
		EMC_PMACRO_DATA_PAD_TX_CTRL,
		EMC_PMACRO_COMMON_PAD_TX_CTRL,
		EMC_PMACRO_VTTGEN_CTRL_0,
		EMC_PMACRO_VTTGEN_CTRL_1,
		EMC_PMACRO_VTTGEN_CTRL_2,
		EMC_PMACRO_BRICK_CTRL_RFU1,
		EMC_PMACRO_CMD_BRICK_CTRL_FDPD,
		EMC_PMACRO_BRICK_CTRL_RFU2,
		EMC_PMACRO_DATA_BRICK_CTRL_FDPD,
		EMC_PMACRO_BG_BIAS_CTRL_0,
		EMC_CFG_3,
		EMC_PMACRO_TX_PWRD_0,
		EMC_PMACRO_TX_PWRD_1,
		EMC_PMACRO_TX_PWRD_2,
		EMC_PMACRO_TX_PWRD_3,
		EMC_PMACRO_TX_PWRD_4,
		EMC_PMACRO_TX_PWRD_5,
		EMC_CONFIG_SAMPLE_DELAY,
		EMC_PMACRO_TX_SEL_CLK_SRC_0,
		EMC_PMACRO_TX_SEL_CLK_SRC_1,
		EMC_PMACRO_TX_SEL_CLK_SRC_2,
		EMC_PMACRO_TX_SEL_CLK_SRC_3,
		EMC_PMACRO_TX_SEL_CLK_SRC_4,
		EMC_PMACRO_TX_SEL_CLK_SRC_5,
		EMC_PMACRO_DDLL_BYPASS,
		EMC_PMACRO_DDLL_PWRD_0,
		EMC_PMACRO_DDLL_PWRD_1,
		EMC_PMACRO_DDLL_PWRD_2,
		EMC_PMACRO_CMD_CTRL_0,
		EMC_PMACRO_CMD_CTRL_1,
		EMC_PMACRO_CMD_CTRL_2,
		EMC_TR_TIMING_0,
		EMC_TR_DVFS,
		EMC_TR_CTRL_1,
		EMC_TR_RDV,
		EMC_TR_QPOP,
		EMC_TR_RDV_MASK,
		EMC_MRW14,
		EMC_TR_QSAFE,
		EMC_TR_QRST,
		EMC_TRAINING_CTRL,
		EMC_TRAINING_SETTLE,
		EMC_TRAINING_VREF_SETTLE,
		EMC_TRAINING_CA_FINE_CTRL,
		EMC_TRAINING_CA_CTRL_MISC,
		EMC_TRAINING_CA_CTRL_MISC1,
		EMC_TRAINING_CA_VREF_CTRL,
		EMC_TRAINING_QUSE_CORS_CTRL,
		EMC_TRAINING_QUSE_FINE_CTRL,
		EMC_TRAINING_QUSE_CTRL_MISC,
		EMC_TRAINING_QUSE_VREF_CTRL,
		EMC_TRAINING_READ_FINE_CTRL,
		EMC_TRAINING_READ_CTRL_MISC,
		EMC_TRAINING_READ_VREF_CTRL,
		EMC_TRAINING_WRITE_FINE_CTRL,
		EMC_TRAINING_WRITE_CTRL_MISC,
		EMC_TRAINING_WRITE_VREF_CTRL,
		EMC_TRAINING_MPC,
		EMC_MRW15,
	},
	.trim = {
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_2,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_3,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_2,
		EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_3,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE0_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE0_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE0_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE1_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE1_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE1_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE2_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE2_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE2_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE3_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE3_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE3_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE4_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE4_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE4_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE5_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE5_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE5_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE6_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE6_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE6_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE7_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE7_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK0_BYTE7_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE0_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE0_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE0_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE1_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE1_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE1_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE2_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE2_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE2_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE3_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE3_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE3_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE4_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE4_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE4_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE5_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE5_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE5_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE6_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE6_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE6_2,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE7_0,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE7_1,
		EMC_PMACRO_IB_DDLL_SHORT_DQ_RANK1_BYTE7_2,
		EMC_PMACRO_IB_VREF_DQS_0,
		EMC_PMACRO_IB_VREF_DQS_1,
		EMC_PMACRO_IB_VREF_DQ_0,
		EMC_PMACRO_IB_VREF_DQ_1,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE0_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE0_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE0_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE1_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE1_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE1_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE2_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE2_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE2_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE3_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE3_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE3_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE4_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE4_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE4_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE5_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE5_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE5_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE6_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE6_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE6_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE7_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE7_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_BYTE7_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD0_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD0_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD0_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD1_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD1_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD1_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD2_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD2_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD2_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD3_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD3_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK0_CMD3_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE0_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE0_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE0_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE1_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE1_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE1_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE2_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE2_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE2_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE3_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE3_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE3_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE4_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE4_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE4_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE5_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE5_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE5_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE6_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE6_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE6_2,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE7_0,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE7_1,
		EMC_PMACRO_OB_DDLL_SHORT_DQ_RANK1_BYTE7_2,
		EMC_PMACRO_QUSE_DDLL_RANK0_0,
		EMC_PMACRO_QUSE_DDLL_RANK0_1,
		EMC_PMACRO_QUSE_DDLL_RANK0_2,
		EMC_PMACRO_QUSE_DDLL_RANK0_3,
		EMC_PMACRO_QUSE_DDLL_RANK1_0,
		EMC_PMACRO_QUSE_DDLL_RANK1_1,
		EMC_PMACRO_QUSE_DDLL_RANK1_2,
		EMC_PMACRO_QUSE_DDLL_RANK1_3
	},
	.burst_mc = {
		MC_EMEM_ARB_CFG,
		MC_EMEM_ARB_OUTSTANDING_REQ,
		MC_EMEM_ARB_REFPB_HP_CTRL,
		MC_EMEM_ARB_REFPB_BANK_CTRL,
		MC_EMEM_ARB_TIMING_RCD,
		MC_EMEM_ARB_TIMING_RP,
		MC_EMEM_ARB_TIMING_RC,
		MC_EMEM_ARB_TIMING_RAS,
		MC_EMEM_ARB_TIMING_FAW,
		MC_EMEM_ARB_TIMING_RRD,
		MC_EMEM_ARB_TIMING_RAP2PRE,
		MC_EMEM_ARB_TIMING_WAP2PRE,
		MC_EMEM_ARB_TIMING_R2R,
		MC_EMEM_ARB_TIMING_W2W,
		MC_EMEM_ARB_TIMING_R2W,
		MC_EMEM_ARB_TIMING_CCDMW,
		MC_EMEM_ARB_TIMING_W2R,
		MC_EMEM_ARB_TIMING_RFCPB,
		MC_EMEM_ARB_DA_TURNS,
		MC_EMEM_ARB_DA_COVERS,
		MC_EMEM_ARB_MISC0,
		MC_EMEM_ARB_MISC1,
		MC_EMEM_ARB_MISC2,
		MC_EMEM_ARB_RING1_THROTTLE,
		MC_EMEM_ARB_DHYST_CTRL,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_0,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_1,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_2,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_3,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_4,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_5,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_6,
		MC_EMEM_ARB_DHYST_TIMEOUT_UTIL_7,
	},
	.la_scale = {
		MC_MLL_MPCORER_PTSA_RATE,
		MC_FTOP_PTSA_RATE,
		MC_PTSA_GRANT_DECREMENT,
		MC_LATENCY_ALLOWANCE_XUSB_0,
		MC_LATENCY_ALLOWANCE_XUSB_1,
		MC_LATENCY_ALLOWANCE_TSEC_0,
		MC_LATENCY_ALLOWANCE_SDMMCA_0,
		MC_LATENCY_ALLOWANCE_SDMMCAA_0,
		MC_LATENCY_ALLOWANCE_SDMMC_0,
		MC_LATENCY_ALLOWANCE_SDMMCAB_0,
		MC_LATENCY_ALLOWANCE_PPCS_0,
		MC_LATENCY_ALLOWANCE_PPCS_1,
		MC_LATENCY_ALLOWANCE_MPCORE_0,
		MC_LATENCY_ALLOWANCE_HC_0,
		MC_LATENCY_ALLOWANCE_HC_1,
		MC_LATENCY_ALLOWANCE_AVPC_0,
		MC_LATENCY_ALLOWANCE_GPU_0,
		MC_LATENCY_ALLOWANCE_GPU2_0,
		MC_LATENCY_ALLOWANCE_NVENC_0,
		MC_LATENCY_ALLOWANCE_NVDEC_0,
		MC_LATENCY_ALLOWANCE_VIC_0,
		MC_LATENCY_ALLOWANCE_VI2_0,
		MC_LATENCY_ALLOWANCE_ISP2_0,
		MC_LATENCY_ALLOWANCE_ISP2_1,
	},
	.burst_per_channel = {
		{ .bank = 0, .offset = EMC_MRW10, },
		{ .bank = 1, .offset = EMC_MRW10, },
		{ .bank = 0, .offset = EMC_MRW11, },
		{ .bank = 1, .offset = EMC_MRW11, },
		{ .bank = 0, .offset = EMC_MRW12, },
		{ .bank = 1, .offset = EMC_MRW12, },
		{ .bank = 0, .offset = EMC_MRW13, },
		{ .bank = 1, .offset = EMC_MRW13, },
	},
	.trim_per_channel = {
		{ .bank = 0, .offset = EMC_CMD_BRLSHFT_0, },
		{ .bank = 1, .offset = EMC_CMD_BRLSHFT_1, },
		{ .bank = 0, .offset = EMC_DATA_BRLSHFT_0, },
		{ .bank = 1, .offset = EMC_DATA_BRLSHFT_0, },
		{ .bank = 0, .offset = EMC_DATA_BRLSHFT_1, },
		{ .bank = 1, .offset = EMC_DATA_BRLSHFT_1, },
		{ .bank = 0, .offset = EMC_QUSE_BRLSHFT_0, },
		{ .bank = 1, .offset = EMC_QUSE_BRLSHFT_1, },
		{ .bank = 0, .offset = EMC_QUSE_BRLSHFT_2, },
		{ .bank = 1, .offset = EMC_QUSE_BRLSHFT_3, },
	},
	.vref_per_channel = {
		{
			.bank = 0,
			.offset = EMC_TRAINING_OPT_DQS_IB_VREF_RANK0,
		}, {
			.bank = 1,
			.offset = EMC_TRAINING_OPT_DQS_IB_VREF_RANK0,
		}, {
			.bank = 0,
			.offset = EMC_TRAINING_OPT_DQS_IB_VREF_RANK1,
		}, {
			.bank = 1,
			.offset = EMC_TRAINING_OPT_DQS_IB_VREF_RANK1,
		},
	},
};

static void tegra210_emc_train(struct timer_list *timer)
{
	struct tegra210_emc *emc = from_timer(emc, timer, training);
	unsigned long flags;

	if (!emc->last)
		return;

	spin_lock_irqsave(&emc->lock, flags);

	if (emc->sequence->periodic_compensation)
		emc->sequence->periodic_compensation(emc);

	spin_unlock_irqrestore(&emc->lock, flags);

	mod_timer(&emc->training,
		  jiffies + msecs_to_jiffies(emc->training_interval));
}

static void tegra210_emc_training_start(struct tegra210_emc *emc)
{
	mod_timer(&emc->training,
		  jiffies + msecs_to_jiffies(emc->training_interval));
}

static void tegra210_emc_training_stop(struct tegra210_emc *emc)
{
	del_timer(&emc->training);
}

static unsigned int tegra210_emc_get_temperature(struct tegra210_emc *emc)
{
	unsigned long flags;
	u32 value, max = 0;
	unsigned int i;

	spin_lock_irqsave(&emc->lock, flags);

	for (i = 0; i < emc->num_devices; i++) {
		value = tegra210_emc_mrr_read(emc, i, 4);

		if (value & BIT(7))
			dev_dbg(emc->dev,
				"sensor reading changed for device %u: %08x\n",
				i, value);

		value = FIELD_GET(LPDDR2_MR4_SRR, value);
		if (value > max)
			max = value;
	}

	spin_unlock_irqrestore(&emc->lock, flags);

	return max;
}

static void tegra210_emc_poll_refresh(struct timer_list *timer)
{
	struct tegra210_emc *emc = from_timer(emc, timer, refresh_timer);
	unsigned int temperature;

	if (!emc->debugfs.temperature)
		temperature = tegra210_emc_get_temperature(emc);
	else
		temperature = emc->debugfs.temperature;

	if (temperature == emc->temperature)
		goto reset;

	switch (temperature) {
	case 0 ... 3:
		/* temperature is fine, using regular refresh */
		dev_dbg(emc->dev, "switching to nominal refresh...\n");
		tegra210_emc_set_refresh(emc, TEGRA210_EMC_REFRESH_NOMINAL);
		break;

	case 4:
		dev_dbg(emc->dev, "switching to 2x refresh...\n");
		tegra210_emc_set_refresh(emc, TEGRA210_EMC_REFRESH_2X);
		break;

	case 5:
		dev_dbg(emc->dev, "switching to 4x refresh...\n");
		tegra210_emc_set_refresh(emc, TEGRA210_EMC_REFRESH_4X);
		break;

	case 6 ... 7:
		dev_dbg(emc->dev, "switching to throttle refresh...\n");
		tegra210_emc_set_refresh(emc, TEGRA210_EMC_REFRESH_THROTTLE);
		break;

	default:
		WARN(1, "invalid DRAM temperature state %u\n", temperature);
		return;
	}

	emc->temperature = temperature;

reset:
	if (atomic_read(&emc->refresh_poll) > 0) {
		unsigned int interval = emc->refresh_poll_interval;
		unsigned int timeout = msecs_to_jiffies(interval);

		mod_timer(&emc->refresh_timer, jiffies + timeout);
	}
}

static void tegra210_emc_poll_refresh_stop(struct tegra210_emc *emc)
{
	atomic_set(&emc->refresh_poll, 0);
	del_timer_sync(&emc->refresh_timer);
}

static void tegra210_emc_poll_refresh_start(struct tegra210_emc *emc)
{
	atomic_set(&emc->refresh_poll, 1);

	mod_timer(&emc->refresh_timer,
		  jiffies + msecs_to_jiffies(emc->refresh_poll_interval));
}

static int tegra210_emc_cd_max_state(struct thermal_cooling_device *cd,
				     unsigned long *state)
{
	*state = 1;

	return 0;
}

static int tegra210_emc_cd_get_state(struct thermal_cooling_device *cd,
				     unsigned long *state)
{
	struct tegra210_emc *emc = cd->devdata;

	*state = atomic_read(&emc->refresh_poll);

	return 0;
}

static int tegra210_emc_cd_set_state(struct thermal_cooling_device *cd,
				     unsigned long state)
{
	struct tegra210_emc *emc = cd->devdata;

	if (state == atomic_read(&emc->refresh_poll))
		return 0;

	if (state)
		tegra210_emc_poll_refresh_start(emc);
	else
		tegra210_emc_poll_refresh_stop(emc);

	return 0;
}

static struct thermal_cooling_device_ops tegra210_emc_cd_ops = {
	.get_max_state = tegra210_emc_cd_max_state,
	.get_cur_state = tegra210_emc_cd_get_state,
	.set_cur_state = tegra210_emc_cd_set_state,
};

static void tegra210_emc_set_clock(struct tegra210_emc *emc, u32 clksrc)
{
	emc->sequence->set_clock(emc, clksrc);

	if (emc->next->periodic_training)
		tegra210_emc_training_start(emc);
	else
		tegra210_emc_training_stop(emc);
}

static void tegra210_change_dll_src(struct tegra210_emc *emc,
				    u32 clksrc)
{
	u32 dll_setting = emc->next->dll_clk_src;
	u32 emc_clk_src;
	u32 emc_clk_div;

	emc_clk_src = (clksrc & EMC_CLK_EMC_2X_CLK_SRC_MASK) >>
		       EMC_CLK_EMC_2X_CLK_SRC_SHIFT;
	emc_clk_div = (clksrc & EMC_CLK_EMC_2X_CLK_DIVISOR_MASK) >>
		       EMC_CLK_EMC_2X_CLK_DIVISOR_SHIFT;

	dll_setting &= ~(DLL_CLK_EMC_DLL_CLK_SRC_MASK |
			 DLL_CLK_EMC_DLL_CLK_DIVISOR_MASK);
	dll_setting |= emc_clk_src << DLL_CLK_EMC_DLL_CLK_SRC_SHIFT;
	dll_setting |= emc_clk_div << DLL_CLK_EMC_DLL_CLK_DIVISOR_SHIFT;

	dll_setting &= ~DLL_CLK_EMC_DLL_DDLL_CLK_SEL_MASK;
	if (emc_clk_src == EMC_CLK_SOURCE_PLLMB_LJ)
		dll_setting |= (PLLM_VCOB <<
				DLL_CLK_EMC_DLL_DDLL_CLK_SEL_SHIFT);
	else if (emc_clk_src == EMC_CLK_SOURCE_PLLM_LJ)
		dll_setting |= (PLLM_VCOA <<
				DLL_CLK_EMC_DLL_DDLL_CLK_SEL_SHIFT);
	else
		dll_setting |= (EMC_DLL_SWITCH_OUT <<
				DLL_CLK_EMC_DLL_DDLL_CLK_SEL_SHIFT);

	tegra210_clk_emc_dll_update_setting(dll_setting);

	if (emc->next->clk_out_enb_x_0_clk_enb_emc_dll)
		tegra210_clk_emc_dll_enable(true);
	else
		tegra210_clk_emc_dll_enable(false);
}

int tegra210_emc_set_refresh(struct tegra210_emc *emc,
			     enum tegra210_emc_refresh refresh)
{
	struct tegra210_emc_timing *timings;
	unsigned long flags;

	if ((emc->dram_type != DRAM_TYPE_LPDDR2 &&
	     emc->dram_type != DRAM_TYPE_LPDDR4) ||
	    !emc->last)
		return -ENODEV;

	if (refresh > TEGRA210_EMC_REFRESH_THROTTLE)
		return -EINVAL;

	if (refresh == emc->refresh)
		return 0;

	spin_lock_irqsave(&emc->lock, flags);

	if (refresh == TEGRA210_EMC_REFRESH_THROTTLE && emc->derated)
		timings = emc->derated;
	else
		timings = emc->nominal;

	if (timings != emc->timings) {
		unsigned int index = emc->last - emc->timings;
		u32 clksrc;

		clksrc = emc->provider.configs[index].value |
			 EMC_CLK_FORCE_CC_TRIGGER;

		emc->next = &timings[index];
		emc->timings = timings;

		tegra210_emc_set_clock(emc, clksrc);
	} else {
		tegra210_emc_adjust_timing(emc, emc->last);
		tegra210_emc_timing_update(emc);

		if (refresh != TEGRA210_EMC_REFRESH_NOMINAL)
			emc_writel(emc, EMC_REF_REF_CMD, EMC_REF);
	}

	spin_unlock_irqrestore(&emc->lock, flags);

	return 0;
}

u32 tegra210_emc_mrr_read(struct tegra210_emc *emc, unsigned int chip,
			  unsigned int address)
{
	u32 value, ret = 0;
	unsigned int i;

	value = (chip & EMC_MRR_DEV_SEL_MASK) << EMC_MRR_DEV_SEL_SHIFT |
		(address & EMC_MRR_MA_MASK) << EMC_MRR_MA_SHIFT;
	emc_writel(emc, value, EMC_MRR);

	for (i = 0; i < emc->num_channels; i++)
		WARN(tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						  EMC_EMC_STATUS_MRR_DIVLD, 1),
		     "Timed out waiting for MRR %u (ch=%u)\n", address, i);

	for (i = 0; i < emc->num_channels; i++) {
		value = emc_channel_readl(emc, i, EMC_MRR);
		value &= EMC_MRR_DATA_MASK;

		ret = (ret << 16) | value;
	}

	return ret;
}

void tegra210_emc_do_clock_change(struct tegra210_emc *emc, u32 clksrc)
{
	int err;

	mc_readl(emc->mc, MC_EMEM_ADR_CFG);
	emc_readl(emc, EMC_INTSTATUS);

	tegra210_clk_emc_update_setting(clksrc);

	err = tegra210_emc_wait_for_update(emc, 0, EMC_INTSTATUS,
					   EMC_INTSTATUS_CLKCHANGE_COMPLETE,
					   true);
	if (err)
		dev_warn(emc->dev, "clock change completion error: %d\n", err);
}

struct tegra210_emc_timing *tegra210_emc_find_timing(struct tegra210_emc *emc,
						     unsigned long rate)
{
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++)
		if (emc->timings[i].rate * 1000UL == rate)
			return &emc->timings[i];

	return NULL;
}

int tegra210_emc_wait_for_update(struct tegra210_emc *emc, unsigned int channel,
				 unsigned int offset, u32 bit_mask, bool state)
{
	unsigned int i;
	u32 value;

	for (i = 0; i < EMC_STATUS_UPDATE_TIMEOUT; i++) {
		value = emc_channel_readl(emc, channel, offset);
		if (!!(value & bit_mask) == state)
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

void tegra210_emc_set_shadow_bypass(struct tegra210_emc *emc, int set)
{
	u32 emc_dbg = emc_readl(emc, EMC_DBG);

	if (set)
		emc_writel(emc, emc_dbg | EMC_DBG_WRITE_MUX_ACTIVE, EMC_DBG);
	else
		emc_writel(emc, emc_dbg & ~EMC_DBG_WRITE_MUX_ACTIVE, EMC_DBG);
}

u32 tegra210_emc_get_dll_state(struct tegra210_emc_timing *next)
{
	if (next->emc_emrs & 0x1)
		return 0;

	return 1;
}

void tegra210_emc_timing_update(struct tegra210_emc *emc)
{
	unsigned int i;
	int err = 0;

	emc_writel(emc, 0x1, EMC_TIMING_CONTROL);

	for (i = 0; i < emc->num_channels; i++) {
		err |= tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						    EMC_EMC_STATUS_TIMING_UPDATE_STALLED,
						    false);
	}

	if (err)
		dev_warn(emc->dev, "timing update error: %d\n", err);
}

unsigned long tegra210_emc_actual_osc_clocks(u32 in)
{
	if (in < 0x40)
		return in * 16;
	else if (in < 0x80)
		return 2048;
	else if (in < 0xc0)
		return 4096;
	else
		return 8192;
}

void tegra210_emc_start_periodic_compensation(struct tegra210_emc *emc)
{
	u32 mpc_req = 0x4b;

	emc_writel(emc, mpc_req, EMC_MPC);
	mpc_req = emc_readl(emc, EMC_MPC);
}

u32 tegra210_emc_compensate(struct tegra210_emc_timing *next, u32 offset)
{
	u32 temp = 0, rate = next->rate / 1000;
	s32 delta[4], delta_taps[4];
	s32 new[] = {
		TRIM_REG(0, 0, 0, 0),
		TRIM_REG(0, 0, 0, 1),
		TRIM_REG(0, 0, 1, 2),
		TRIM_REG(0, 0, 1, 3),

		TRIM_REG(1, 0, 2, 4),
		TRIM_REG(1, 0, 2, 5),
		TRIM_REG(1, 0, 3, 6),
		TRIM_REG(1, 0, 3, 7),

		TRIM_REG(0, 1, 0, 0),
		TRIM_REG(0, 1, 0, 1),
		TRIM_REG(0, 1, 1, 2),
		TRIM_REG(0, 1, 1, 3),

		TRIM_REG(1, 1, 2, 4),
		TRIM_REG(1, 1, 2, 5),
		TRIM_REG(1, 1, 3, 6),
		TRIM_REG(1, 1, 3, 7)
	};
	unsigned i;

	switch (offset) {
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3:
	case EMC_DATA_BRLSHFT_0:
		delta[0] = 128 * (next->current_dram_clktree[C0D0U0] -
				  next->trained_dram_clktree[C0D0U0]);
		delta[1] = 128 * (next->current_dram_clktree[C0D0U1] -
				  next->trained_dram_clktree[C0D0U1]);
		delta[2] = 128 * (next->current_dram_clktree[C1D0U0] -
				  next->trained_dram_clktree[C1D0U0]);
		delta[3] = 128 * (next->current_dram_clktree[C1D0U1] -
				  next->trained_dram_clktree[C1D0U1]);

		delta_taps[0] = (delta[0] * (s32)rate) / 1000000;
		delta_taps[1] = (delta[1] * (s32)rate) / 1000000;
		delta_taps[2] = (delta[2] * (s32)rate) / 1000000;
		delta_taps[3] = (delta[3] * (s32)rate) / 1000000;

		for (i = 0; i < 4; i++) {
			if ((delta_taps[i] > next->tree_margin) ||
			    (delta_taps[i] < (-1 * next->tree_margin))) {
				new[i * 2] = new[i * 2] + delta_taps[i];
				new[i * 2 + 1] = new[i * 2 + 1] +
							delta_taps[i];
			}
		}

		if (offset == EMC_DATA_BRLSHFT_0) {
			for (i = 0; i < 8; i++)
				new[i] = new[i] / 64;
		} else {
			for (i = 0; i < 8; i++)
				new[i] = new[i] % 64;
		}

		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2:
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3:
	case EMC_DATA_BRLSHFT_1:
		delta[0] = 128 * (next->current_dram_clktree[C0D1U0] -
				  next->trained_dram_clktree[C0D1U0]);
		delta[1] = 128 * (next->current_dram_clktree[C0D1U1] -
				  next->trained_dram_clktree[C0D1U1]);
		delta[2] = 128 * (next->current_dram_clktree[C1D1U0] -
				  next->trained_dram_clktree[C1D1U0]);
		delta[3] = 128 * (next->current_dram_clktree[C1D1U1] -
				  next->trained_dram_clktree[C1D1U1]);

		delta_taps[0] = (delta[0] * (s32)rate) / 1000000;
		delta_taps[1] = (delta[1] * (s32)rate) / 1000000;
		delta_taps[2] = (delta[2] * (s32)rate) / 1000000;
		delta_taps[3] = (delta[3] * (s32)rate) / 1000000;

		for (i = 0; i < 4; i++) {
			if ((delta_taps[i] > next->tree_margin) ||
			    (delta_taps[i] < (-1 * next->tree_margin))) {
				new[8 + i * 2] = new[8 + i * 2] +
							delta_taps[i];
				new[8 + i * 2 + 1] = new[8 + i * 2 + 1] +
							delta_taps[i];
			}
		}

		if (offset == EMC_DATA_BRLSHFT_1) {
			for (i = 0; i < 8; i++)
				new[i + 8] = new[i + 8] / 64;
		} else {
			for (i = 0; i < 8; i++)
				new[i + 8] = new[i + 8] % 64;
		}

		break;
	}

	switch (offset) {
	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0:
		temp = CALC_TEMP(0, 0, 0, 1, 0);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1:
		temp = CALC_TEMP(0, 1, 2, 3, 2);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2:
		temp = CALC_TEMP(0, 2, 4, 5, 4);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3:
		temp = CALC_TEMP(0, 3, 6, 7, 6);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0:
		temp = CALC_TEMP(1, 0, 0, 1, 8);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1:
		temp = CALC_TEMP(1, 1, 2, 3, 10);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2:
		temp = CALC_TEMP(1, 2, 4, 5, 12);
		break;

	case EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3:
		temp = CALC_TEMP(1, 3, 6, 7, 14);
		break;

	case EMC_DATA_BRLSHFT_0:
		temp = ((new[0] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE0_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE0_DATA_BRLSHFT_MASK) |
		       ((new[1] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE1_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE1_DATA_BRLSHFT_MASK) |
		       ((new[2] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE2_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE2_DATA_BRLSHFT_MASK) |
		       ((new[3] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE3_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE3_DATA_BRLSHFT_MASK) |
		       ((new[4] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE4_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE4_DATA_BRLSHFT_MASK) |
		       ((new[5] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE5_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE5_DATA_BRLSHFT_MASK) |
		       ((new[6] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE6_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE6_DATA_BRLSHFT_MASK) |
		       ((new[7] <<
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE7_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_0_RANK0_BYTE7_DATA_BRLSHFT_MASK);
		break;

	case EMC_DATA_BRLSHFT_1:
		temp = ((new[8] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE0_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE0_DATA_BRLSHFT_MASK) |
		       ((new[9] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE1_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE1_DATA_BRLSHFT_MASK) |
		       ((new[10] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE2_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE2_DATA_BRLSHFT_MASK) |
		       ((new[11] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE3_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE3_DATA_BRLSHFT_MASK) |
		       ((new[12] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE4_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE4_DATA_BRLSHFT_MASK) |
		       ((new[13] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE5_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE5_DATA_BRLSHFT_MASK) |
		       ((new[14] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE6_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE6_DATA_BRLSHFT_MASK) |
		       ((new[15] <<
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE7_DATA_BRLSHFT_SHIFT) &
			 EMC_DATA_BRLSHFT_1_RANK1_BYTE7_DATA_BRLSHFT_MASK);
		break;

	default:
		break;
	}

	return temp;
}

u32 tegra210_emc_dll_prelock(struct tegra210_emc *emc, u32 clksrc)
{
	unsigned int i;
	u32 value;

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_LOCK_LIMIT_MASK;
	value |= (3 << EMC_CFG_DIG_DLL_CFG_DLL_LOCK_LIMIT_SHIFT);
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_EN;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_MODE_MASK;
	value |= (3 << EMC_CFG_DIG_DLL_CFG_DLL_MODE_SHIFT);
	value |= EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_TRAFFIC;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_RW_UNTIL_LOCK;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_UNTIL_LOCK;
	emc_writel(emc, value, EMC_CFG_DIG_DLL);
	emc_writel(emc, 1, EMC_TIMING_CONTROL);

	for (i = 0; i < emc->num_channels; i++)
		tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
					     EMC_EMC_STATUS_TIMING_UPDATE_STALLED,
					     0);

	for (i = 0; i < emc->num_channels; i++) {
		while (true) {
			value = emc_channel_readl(emc, i, EMC_CFG_DIG_DLL);
			if ((value & EMC_CFG_DIG_DLL_CFG_DLL_EN) == 0)
				break;
		}
	}

	value = emc->next->burst_regs[EMC_DLL_CFG_0_INDEX];
	emc_writel(emc, value, EMC_DLL_CFG_0);

	value = emc_readl(emc, EMC_DLL_CFG_1);
	value &= EMC_DLL_CFG_1_DDLLCAL_CTRL_START_TRIM_MASK;

	if (emc->next->rate >= 400000 && emc->next->rate < 600000)
		value |= 150;
	else if (emc->next->rate >= 600000 && emc->next->rate < 800000)
		value |= 100;
	else if (emc->next->rate >= 800000 && emc->next->rate < 1000000)
		value |= 70;
	else if (emc->next->rate >= 1000000 && emc->next->rate < 1200000)
		value |= 30;
	else
		value |= 20;

	emc_writel(emc, value, EMC_DLL_CFG_1);

	tegra210_change_dll_src(emc, clksrc);

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value |= EMC_CFG_DIG_DLL_CFG_DLL_EN;
	emc_writel(emc, value, EMC_CFG_DIG_DLL);

	tegra210_emc_timing_update(emc);

	for (i = 0; i < emc->num_channels; i++) {
		while (true) {
			value = emc_channel_readl(emc, 0, EMC_CFG_DIG_DLL);
			if (value & EMC_CFG_DIG_DLL_CFG_DLL_EN)
				break;
		}
	}

	while (true) {
		value = emc_readl(emc, EMC_DIG_DLL_STATUS);

		if ((value & EMC_DIG_DLL_STATUS_DLL_PRIV_UPDATED) == 0)
			continue;

		if ((value & EMC_DIG_DLL_STATUS_DLL_LOCK) == 0)
			continue;

		break;
	}

	value = emc_readl(emc, EMC_DIG_DLL_STATUS);

	return value & EMC_DIG_DLL_STATUS_DLL_OUT_MASK;
}

u32 tegra210_emc_dvfs_power_ramp_up(struct tegra210_emc *emc, u32 clk,
				    bool flip_backward)
{
	u32 cmd_pad, dq_pad, rfu1, cfg5, common_tx, ramp_up_wait = 0;
	const struct tegra210_emc_timing *timing;

	if (flip_backward)
		timing = emc->last;
	else
		timing = emc->next;

	cmd_pad = timing->burst_regs[EMC_PMACRO_CMD_PAD_TX_CTRL_INDEX];
	dq_pad = timing->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX];
	rfu1 = timing->burst_regs[EMC_PMACRO_BRICK_CTRL_RFU1_INDEX];
	cfg5 = timing->burst_regs[EMC_FBIO_CFG5_INDEX];
	common_tx = timing->burst_regs[EMC_PMACRO_COMMON_PAD_TX_CTRL_INDEX];

	cmd_pad |= EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_DRVFORCEON;

	if (clk < 1000000 / DVFS_FGCG_MID_SPEED_THRESHOLD) {
		ccfifo_writel(emc, common_tx & 0xa,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL, 0);
		ccfifo_writel(emc, common_tx & 0xf,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL,
			      (100000 / clk) + 1);
		ramp_up_wait += 100000;
	} else {
		ccfifo_writel(emc, common_tx | 0x8,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL, 0);
	}

	if (clk < 1000000 / DVFS_FGCG_HIGH_SPEED_THRESHOLD) {
		if (clk < 1000000 / IOBRICK_DCC_THRESHOLD) {
			cmd_pad |=
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSP_TX_E_DCC |
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSN_TX_E_DCC;
			cmd_pad &=
				~(EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_E_DCC |
				  EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_CMD_TX_E_DCC);
			ccfifo_writel(emc, cmd_pad,
				      EMC_PMACRO_CMD_PAD_TX_CTRL,
				      (100000 / clk) + 1);
			ramp_up_wait += 100000;

			dq_pad |=
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSP_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSN_TX_E_DCC;
			dq_pad &=
			       ~(EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_TX_E_DCC |
				 EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_CMD_TX_E_DCC);
			ccfifo_writel(emc, dq_pad,
				      EMC_PMACRO_DATA_PAD_TX_CTRL, 0);
			ccfifo_writel(emc, rfu1 & 0xfe40fe40,
				      EMC_PMACRO_BRICK_CTRL_RFU1, 0);
		} else {
			ccfifo_writel(emc, rfu1 & 0xfe40fe40,
				      EMC_PMACRO_BRICK_CTRL_RFU1,
				      (100000 / clk) + 1);
			ramp_up_wait += 100000;
		}

		ccfifo_writel(emc, rfu1 & 0xfeedfeed,
			      EMC_PMACRO_BRICK_CTRL_RFU1, (100000 / clk) + 1);
		ramp_up_wait += 100000;

		if (clk < 1000000 / IOBRICK_DCC_THRESHOLD) {
			cmd_pad |=
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSP_TX_E_DCC |
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSN_TX_E_DCC |
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_E_DCC |
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_CMD_TX_E_DCC;
			ccfifo_writel(emc, cmd_pad,
				      EMC_PMACRO_CMD_PAD_TX_CTRL,
				      (100000 / clk) + 1);
			ramp_up_wait += 100000;

			dq_pad |=
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSP_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSN_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_CMD_TX_E_DCC;
			ccfifo_writel(emc, dq_pad,
				      EMC_PMACRO_DATA_PAD_TX_CTRL, 0);
			ccfifo_writel(emc, rfu1,
				      EMC_PMACRO_BRICK_CTRL_RFU1, 0);
		} else {
			ccfifo_writel(emc, rfu1,
				      EMC_PMACRO_BRICK_CTRL_RFU1,
				      (100000 / clk) + 1);
			ramp_up_wait += 100000;
		}

		ccfifo_writel(emc, cfg5 & ~EMC_FBIO_CFG5_CMD_TX_DIS,
			      EMC_FBIO_CFG5, (100000 / clk) + 10);
		ramp_up_wait += 100000 + (10 * clk);
	} else if (clk < 1000000 / DVFS_FGCG_MID_SPEED_THRESHOLD) {
		ccfifo_writel(emc, rfu1 | 0x06000600,
			      EMC_PMACRO_BRICK_CTRL_RFU1, (100000 / clk) + 1);
		ccfifo_writel(emc, cfg5 & ~EMC_FBIO_CFG5_CMD_TX_DIS,
			      EMC_FBIO_CFG5, (100000 / clk) + 10);
		ramp_up_wait += 100000 + 10 * clk;
	} else {
		ccfifo_writel(emc, rfu1 | 0x00000600,
			      EMC_PMACRO_BRICK_CTRL_RFU1, 0);
		ccfifo_writel(emc, cfg5 & ~EMC_FBIO_CFG5_CMD_TX_DIS,
			      EMC_FBIO_CFG5, 12);
		ramp_up_wait += 12 * clk;
	}

	cmd_pad &= ~EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_DRVFORCEON;
	ccfifo_writel(emc, cmd_pad, EMC_PMACRO_CMD_PAD_TX_CTRL, 5);

	return ramp_up_wait;
}

u32 tegra210_emc_dvfs_power_ramp_down(struct tegra210_emc *emc, u32 clk,
				      bool flip_backward)
{
	u32 ramp_down_wait = 0, cmd_pad, dq_pad, rfu1, cfg5, common_tx;
	const struct tegra210_emc_timing *entry;
	u32 seq_wait;

	if (flip_backward)
		entry = emc->next;
	else
		entry = emc->last;

	cmd_pad = entry->burst_regs[EMC_PMACRO_CMD_PAD_TX_CTRL_INDEX];
	dq_pad = entry->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX];
	rfu1 = entry->burst_regs[EMC_PMACRO_BRICK_CTRL_RFU1_INDEX];
	cfg5 = entry->burst_regs[EMC_FBIO_CFG5_INDEX];
	common_tx = entry->burst_regs[EMC_PMACRO_COMMON_PAD_TX_CTRL_INDEX];

	cmd_pad |= EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_DRVFORCEON;

	ccfifo_writel(emc, cmd_pad, EMC_PMACRO_CMD_PAD_TX_CTRL, 0);
	ccfifo_writel(emc, cfg5 | EMC_FBIO_CFG5_CMD_TX_DIS,
		      EMC_FBIO_CFG5, 12);
	ramp_down_wait = 12 * clk;

	seq_wait = (100000 / clk) + 1;

	if (clk < (1000000 / DVFS_FGCG_HIGH_SPEED_THRESHOLD)) {
		if (clk < (1000000 / IOBRICK_DCC_THRESHOLD)) {
			cmd_pad &=
				~(EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_E_DCC |
				  EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_CMD_TX_E_DCC);
			cmd_pad |=
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSP_TX_E_DCC |
				EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSN_TX_E_DCC;
			ccfifo_writel(emc, cmd_pad,
				      EMC_PMACRO_CMD_PAD_TX_CTRL, seq_wait);
			ramp_down_wait += 100000;

			dq_pad &=
			      ~(EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_CMD_TX_E_DCC);
			dq_pad |=
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSP_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSN_TX_E_DCC;
			ccfifo_writel(emc, dq_pad,
				      EMC_PMACRO_DATA_PAD_TX_CTRL, 0);
			ccfifo_writel(emc, rfu1 & ~0x01120112,
				      EMC_PMACRO_BRICK_CTRL_RFU1, 0);
		} else {
			ccfifo_writel(emc, rfu1 & ~0x01120112,
				      EMC_PMACRO_BRICK_CTRL_RFU1, seq_wait);
			ramp_down_wait += 100000;
		}

		ccfifo_writel(emc, rfu1 & ~0x01bf01bf,
			      EMC_PMACRO_BRICK_CTRL_RFU1, seq_wait);
		ramp_down_wait += 100000;

		if (clk < (1000000 / IOBRICK_DCC_THRESHOLD)) {
			cmd_pad &=
				~(EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_E_DCC |
				  EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_CMD_TX_E_DCC |
				  EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSP_TX_E_DCC |
				  EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSN_TX_E_DCC);
			ccfifo_writel(emc, cmd_pad,
				      EMC_PMACRO_CMD_PAD_TX_CTRL, seq_wait);
			ramp_down_wait += 100000;

			dq_pad &=
			      ~(EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_CMD_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSP_TX_E_DCC |
				EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSN_TX_E_DCC);
			ccfifo_writel(emc, dq_pad,
				      EMC_PMACRO_DATA_PAD_TX_CTRL, 0);
			ccfifo_writel(emc, rfu1 & ~0x07ff07ff,
				      EMC_PMACRO_BRICK_CTRL_RFU1, 0);
		} else {
			ccfifo_writel(emc, rfu1 & ~0x07ff07ff,
				      EMC_PMACRO_BRICK_CTRL_RFU1, seq_wait);
			ramp_down_wait += 100000;
		}
	} else {
		ccfifo_writel(emc, rfu1 & ~0xffff07ff,
			      EMC_PMACRO_BRICK_CTRL_RFU1, seq_wait + 19);
		ramp_down_wait += 100000 + (20 * clk);
	}

	if (clk < (1000000 / DVFS_FGCG_MID_SPEED_THRESHOLD)) {
		ramp_down_wait += 100000;
		ccfifo_writel(emc, common_tx & ~0x5,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL, seq_wait);
		ramp_down_wait += 100000;
		ccfifo_writel(emc, common_tx & ~0xf,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL, seq_wait);
		ramp_down_wait += 100000;
		ccfifo_writel(emc, 0, 0, seq_wait);
		ramp_down_wait += 100000;
	} else {
		ccfifo_writel(emc, common_tx & ~0xf,
			      EMC_PMACRO_COMMON_PAD_TX_CTRL, seq_wait);
	}

	return ramp_down_wait;
}

void tegra210_emc_reset_dram_clktree_values(struct tegra210_emc_timing *timing)
{
	timing->current_dram_clktree[C0D0U0] =
		timing->trained_dram_clktree[C0D0U0];
	timing->current_dram_clktree[C0D0U1] =
		timing->trained_dram_clktree[C0D0U1];
	timing->current_dram_clktree[C1D0U0] =
		timing->trained_dram_clktree[C1D0U0];
	timing->current_dram_clktree[C1D0U1] =
		timing->trained_dram_clktree[C1D0U1];
	timing->current_dram_clktree[C1D1U0] =
		timing->trained_dram_clktree[C1D1U0];
	timing->current_dram_clktree[C1D1U1] =
		timing->trained_dram_clktree[C1D1U1];
}

static void update_dll_control(struct tegra210_emc *emc, u32 value, bool state)
{
	unsigned int i;

	emc_writel(emc, value, EMC_CFG_DIG_DLL);
	tegra210_emc_timing_update(emc);

	for (i = 0; i < emc->num_channels; i++)
		tegra210_emc_wait_for_update(emc, i, EMC_CFG_DIG_DLL,
					     EMC_CFG_DIG_DLL_CFG_DLL_EN,
					     state);
}

void tegra210_emc_dll_disable(struct tegra210_emc *emc)
{
	u32 value;

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_EN;

	update_dll_control(emc, value, false);
}

void tegra210_emc_dll_enable(struct tegra210_emc *emc)
{
	u32 value;

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value |= EMC_CFG_DIG_DLL_CFG_DLL_EN;

	update_dll_control(emc, value, true);
}

void tegra210_emc_adjust_timing(struct tegra210_emc *emc,
				struct tegra210_emc_timing *timing)
{
	u32 dsr_cntrl = timing->burst_regs[EMC_DYN_SELF_REF_CONTROL_INDEX];
	u32 pre_ref = timing->burst_regs[EMC_PRE_REFRESH_REQ_CNT_INDEX];
	u32 ref = timing->burst_regs[EMC_REFRESH_INDEX];

	switch (emc->refresh) {
	case TEGRA210_EMC_REFRESH_NOMINAL:
	case TEGRA210_EMC_REFRESH_THROTTLE:
		break;

	case TEGRA210_EMC_REFRESH_2X:
		ref = REFRESH_SPEEDUP(ref, 2);
		pre_ref = REFRESH_SPEEDUP(pre_ref, 2);
		dsr_cntrl = REFRESH_SPEEDUP(dsr_cntrl, 2);
		break;

	case TEGRA210_EMC_REFRESH_4X:
		ref = REFRESH_SPEEDUP(ref, 4);
		pre_ref = REFRESH_SPEEDUP(pre_ref, 4);
		dsr_cntrl = REFRESH_SPEEDUP(dsr_cntrl, 4);
		break;

	default:
		dev_warn(emc->dev, "failed to set refresh: %d\n", emc->refresh);
		return;
	}

	emc_writel(emc, ref, emc->offsets->burst[EMC_REFRESH_INDEX]);
	emc_writel(emc, pre_ref,
		   emc->offsets->burst[EMC_PRE_REFRESH_REQ_CNT_INDEX]);
	emc_writel(emc, dsr_cntrl,
		   emc->offsets->burst[EMC_DYN_SELF_REF_CONTROL_INDEX]);
}

static int tegra210_emc_set_rate(struct device *dev,
				 const struct tegra210_clk_emc_config *config)
{
	struct tegra210_emc *emc = dev_get_drvdata(dev);
	struct tegra210_emc_timing *timing = NULL;
	unsigned long rate = config->rate;
	s64 last_change_delay;
	unsigned long flags;
	unsigned int i;

	if (rate == emc->last->rate * 1000UL)
		return 0;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate * 1000UL == rate) {
			timing = &emc->timings[i];
			break;
		}
	}

	if (!timing)
		return -EINVAL;

	if (rate > 204000000 && !timing->trained)
		return -EINVAL;

	emc->next = timing;
	last_change_delay = ktime_us_delta(ktime_get(), emc->clkchange_time);

	/* XXX use non-busy-looping sleep? */
	if ((last_change_delay >= 0) &&
	    (last_change_delay < emc->clkchange_delay))
		udelay(emc->clkchange_delay - (int)last_change_delay);

	spin_lock_irqsave(&emc->lock, flags);
	tegra210_emc_set_clock(emc, config->value);
	emc->clkchange_time = ktime_get();
	emc->last = timing;
	spin_unlock_irqrestore(&emc->lock, flags);

	return 0;
}

/*
 * debugfs interface
 *
 * The memory controller driver exposes some files in debugfs that can be used
 * to control the EMC frequency. The top-level directory can be found here:
 *
 *   /sys/kernel/debug/emc
 *
 * It contains the following files:
 *
 *   - available_rates: This file contains a list of valid, space-separated
 *     EMC frequencies.
 *
 *   - min_rate: Writing a value to this file sets the given frequency as the
 *       floor of the permitted range. If this is higher than the currently
 *       configured EMC frequency, this will cause the frequency to be
 *       increased so that it stays within the valid range.
 *
 *   - max_rate: Similarily to the min_rate file, writing a value to this file
 *       sets the given frequency as the ceiling of the permitted range. If
 *       the value is lower than the currently configured EMC frequency, this
 *       will cause the frequency to be decreased so that it stays within the
 *       valid range.
 */

static bool tegra210_emc_validate_rate(struct tegra210_emc *emc,
				       unsigned long rate)
{
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++)
		if (rate == emc->timings[i].rate * 1000UL)
			return true;

	return false;
}

static int tegra210_emc_debug_available_rates_show(struct seq_file *s,
						   void *data)
{
	struct tegra210_emc *emc = s->private;
	const char *prefix = "";
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++) {
		seq_printf(s, "%s%u", prefix, emc->timings[i].rate * 1000);
		prefix = " ";
	}

	seq_puts(s, "\n");

	return 0;
}

static int tegra210_emc_debug_available_rates_open(struct inode *inode,
						   struct file *file)
{
	return single_open(file, tegra210_emc_debug_available_rates_show,
			   inode->i_private);
}

static const struct file_operations tegra210_emc_debug_available_rates_fops = {
	.open = tegra210_emc_debug_available_rates_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int tegra210_emc_debug_min_rate_get(void *data, u64 *rate)
{
	struct tegra210_emc *emc = data;

	*rate = emc->debugfs.min_rate;

	return 0;
}

static int tegra210_emc_debug_min_rate_set(void *data, u64 rate)
{
	struct tegra210_emc *emc = data;
	int err;

	if (!tegra210_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_min_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.min_rate = rate;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(tegra210_emc_debug_min_rate_fops,
			tegra210_emc_debug_min_rate_get,
			tegra210_emc_debug_min_rate_set, "%llu\n");

static int tegra210_emc_debug_max_rate_get(void *data, u64 *rate)
{
	struct tegra210_emc *emc = data;

	*rate = emc->debugfs.max_rate;

	return 0;
}

static int tegra210_emc_debug_max_rate_set(void *data, u64 rate)
{
	struct tegra210_emc *emc = data;
	int err;

	if (!tegra210_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_max_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.max_rate = rate;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(tegra210_emc_debug_max_rate_fops,
			tegra210_emc_debug_max_rate_get,
			tegra210_emc_debug_max_rate_set, "%llu\n");

static int tegra210_emc_debug_temperature_get(void *data, u64 *temperature)
{
	struct tegra210_emc *emc = data;
	unsigned int value;

	if (!emc->debugfs.temperature)
		value = tegra210_emc_get_temperature(emc);
	else
		value = emc->debugfs.temperature;

	*temperature = value;

	return 0;
}

static int tegra210_emc_debug_temperature_set(void *data, u64 temperature)
{
	struct tegra210_emc *emc = data;

	if (temperature > 7)
		return -EINVAL;

	emc->debugfs.temperature = temperature;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(tegra210_emc_debug_temperature_fops,
			tegra210_emc_debug_temperature_get,
			tegra210_emc_debug_temperature_set, "%llu\n");

static void tegra210_emc_debugfs_init(struct tegra210_emc *emc)
{
	struct device *dev = emc->dev;
	unsigned int i;
	int err;

	emc->debugfs.min_rate = ULONG_MAX;
	emc->debugfs.max_rate = 0;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate * 1000UL < emc->debugfs.min_rate)
			emc->debugfs.min_rate = emc->timings[i].rate * 1000UL;

		if (emc->timings[i].rate * 1000UL > emc->debugfs.max_rate)
			emc->debugfs.max_rate = emc->timings[i].rate * 1000UL;
	}

	if (!emc->num_timings) {
		emc->debugfs.min_rate = clk_get_rate(emc->clk);
		emc->debugfs.max_rate = emc->debugfs.min_rate;
	}

	err = clk_set_rate_range(emc->clk, emc->debugfs.min_rate,
				 emc->debugfs.max_rate);
	if (err < 0) {
		dev_err(dev, "failed to set rate range [%lu-%lu] for %pC\n",
			emc->debugfs.min_rate, emc->debugfs.max_rate,
			emc->clk);
		return;
	}

	emc->debugfs.root = debugfs_create_dir("emc", NULL);

	debugfs_create_file("available_rates", 0444, emc->debugfs.root, emc,
			    &tegra210_emc_debug_available_rates_fops);
	debugfs_create_file("min_rate", 0644, emc->debugfs.root, emc,
			    &tegra210_emc_debug_min_rate_fops);
	debugfs_create_file("max_rate", 0644, emc->debugfs.root, emc,
			    &tegra210_emc_debug_max_rate_fops);
	debugfs_create_file("temperature", 0644, emc->debugfs.root, emc,
			    &tegra210_emc_debug_temperature_fops);
}

static void tegra210_emc_detect(struct tegra210_emc *emc)
{
	u32 value;

	/* probe the number of connected DRAM devices */
	value = mc_readl(emc->mc, MC_EMEM_ADR_CFG);

	if (value & MC_EMEM_ADR_CFG_EMEM_NUMDEV)
		emc->num_devices = 2;
	else
		emc->num_devices = 1;

	/* probe the type of DRAM */
	value = emc_readl(emc, EMC_FBIO_CFG5);
	emc->dram_type = value & 0x3;

	/* probe the number of channels */
	value = emc_readl(emc, EMC_FBIO_CFG7);

	if ((value & EMC_FBIO_CFG7_CH1_ENABLE) &&
	    (value & EMC_FBIO_CFG7_CH0_ENABLE))
		emc->num_channels = 2;
	else
		emc->num_channels = 1;
}

static int tegra210_emc_validate_timings(struct tegra210_emc *emc,
					 struct tegra210_emc_timing *timings,
					 unsigned int num_timings)
{
	unsigned int i;

	for (i = 0; i < num_timings; i++) {
		u32 min_volt = timings[i].min_volt;
		u32 rate = timings[i].rate;

		if (!rate)
			return -EINVAL;

		if ((i > 0) && ((rate <= timings[i - 1].rate) ||
		    (min_volt < timings[i - 1].min_volt)))
			return -EINVAL;

		if (timings[i].revision != timings[0].revision)
			continue;
	}

	return 0;
}

static int tegra210_emc_probe(struct platform_device *pdev)
{
	struct thermal_cooling_device *cd;
	unsigned long current_rate;
	struct tegra210_emc *emc;
	struct device_node *np;
	unsigned int i;
	int err;

	emc = devm_kzalloc(&pdev->dev, sizeof(*emc), GFP_KERNEL);
	if (!emc)
		return -ENOMEM;

	emc->clk = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(emc->clk))
		return PTR_ERR(emc->clk);

	platform_set_drvdata(pdev, emc);
	spin_lock_init(&emc->lock);
	emc->dev = &pdev->dev;

	emc->mc = devm_tegra_memory_controller_get(&pdev->dev);
	if (IS_ERR(emc->mc))
		return PTR_ERR(emc->mc);

	emc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(emc->regs))
		return PTR_ERR(emc->regs);

	for (i = 0; i < 2; i++) {
		emc->channel[i] = devm_platform_ioremap_resource(pdev, 1 + i);
		if (IS_ERR(emc->channel[i]))
			return PTR_ERR(emc->channel[i]);

	}

	tegra210_emc_detect(emc);
	np = pdev->dev.of_node;

	/* attach to the nominal and (optional) derated tables */
	err = of_reserved_mem_device_init_by_name(emc->dev, np, "nominal");
	if (err < 0) {
		dev_err(emc->dev, "failed to get nominal EMC table: %d\n", err);
		return err;
	}

	err = of_reserved_mem_device_init_by_name(emc->dev, np, "derated");
	if (err < 0 && err != -ENODEV) {
		dev_err(emc->dev, "failed to get derated EMC table: %d\n", err);
		goto release;
	}

	/* validate the tables */
	if (emc->nominal) {
		err = tegra210_emc_validate_timings(emc, emc->nominal,
						    emc->num_timings);
		if (err < 0)
			goto release;
	}

	if (emc->derated) {
		err = tegra210_emc_validate_timings(emc, emc->derated,
						    emc->num_timings);
		if (err < 0)
			goto release;
	}

	/* default to the nominal table */
	emc->timings = emc->nominal;

	/* pick the current timing based on the current EMC clock rate */
	current_rate = clk_get_rate(emc->clk) / 1000;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate == current_rate) {
			emc->last = &emc->timings[i];
			break;
		}
	}

	if (i == emc->num_timings) {
		dev_err(emc->dev, "no EMC table entry found for %lu kHz\n",
			current_rate);
		err = -ENOENT;
		goto release;
	}

	/* pick a compatible clock change sequence for the EMC table */
	for (i = 0; i < ARRAY_SIZE(tegra210_emc_sequences); i++) {
		const struct tegra210_emc_sequence *sequence =
				tegra210_emc_sequences[i];

		if (emc->timings[0].revision == sequence->revision) {
			emc->sequence = sequence;
			break;
		}
	}

	if (!emc->sequence) {
		dev_err(&pdev->dev, "sequence %u not supported\n",
			emc->timings[0].revision);
		err = -ENOTSUPP;
		goto release;
	}

	emc->offsets = &tegra210_emc_table_register_offsets;
	emc->refresh = TEGRA210_EMC_REFRESH_NOMINAL;

	emc->provider.owner = THIS_MODULE;
	emc->provider.dev = &pdev->dev;
	emc->provider.set_rate = tegra210_emc_set_rate;

	emc->provider.configs = devm_kcalloc(&pdev->dev, emc->num_timings,
					     sizeof(*emc->provider.configs),
					     GFP_KERNEL);
	if (!emc->provider.configs) {
		err = -ENOMEM;
		goto release;
	}

	emc->provider.num_configs = emc->num_timings;

	for (i = 0; i < emc->provider.num_configs; i++) {
		struct tegra210_emc_timing *timing = &emc->timings[i];
		struct tegra210_clk_emc_config *config =
				&emc->provider.configs[i];
		u32 value;

		config->rate = timing->rate * 1000UL;
		config->value = timing->clk_src_emc;

		value = timing->burst_mc_regs[MC_EMEM_ARB_MISC0_INDEX];

		if ((value & MC_EMEM_ARB_MISC0_EMC_SAME_FREQ) == 0)
			config->same_freq = false;
		else
			config->same_freq = true;
	}

	err = tegra210_clk_emc_attach(emc->clk, &emc->provider);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to attach to EMC clock: %d\n", err);
		goto release;
	}

	emc->clkchange_delay = 100;
	emc->training_interval = 100;
	dev_set_drvdata(emc->dev, emc);

	timer_setup(&emc->refresh_timer, tegra210_emc_poll_refresh,
		    TIMER_DEFERRABLE);
	atomic_set(&emc->refresh_poll, 0);
	emc->refresh_poll_interval = 1000;

	timer_setup(&emc->training, tegra210_emc_train, 0);

	tegra210_emc_debugfs_init(emc);

	cd = devm_thermal_of_cooling_device_register(emc->dev, np, "emc", emc,
						     &tegra210_emc_cd_ops);
	if (IS_ERR(cd)) {
		err = PTR_ERR(cd);
		dev_err(emc->dev, "failed to register cooling device: %d\n",
			err);
		goto detach;
	}

	return 0;

detach:
	debugfs_remove_recursive(emc->debugfs.root);
	tegra210_clk_emc_detach(emc->clk);
release:
	of_reserved_mem_device_release(emc->dev);

	return err;
}

static int tegra210_emc_remove(struct platform_device *pdev)
{
	struct tegra210_emc *emc = platform_get_drvdata(pdev);

	debugfs_remove_recursive(emc->debugfs.root);
	tegra210_clk_emc_detach(emc->clk);
	of_reserved_mem_device_release(emc->dev);

	return 0;
}

static int __maybe_unused tegra210_emc_suspend(struct device *dev)
{
	struct tegra210_emc *emc = dev_get_drvdata(dev);
	int err;

	err = clk_rate_exclusive_get(emc->clk);
	if (err < 0) {
		dev_err(emc->dev, "failed to acquire clock: %d\n", err);
		return err;
	}

	emc->resume_rate = clk_get_rate(emc->clk);

	clk_set_rate(emc->clk, 204000000);
	tegra210_clk_emc_detach(emc->clk);

	dev_dbg(dev, "suspending at %lu Hz\n", clk_get_rate(emc->clk));

	return 0;
}

static int __maybe_unused tegra210_emc_resume(struct device *dev)
{
	struct tegra210_emc *emc = dev_get_drvdata(dev);
	int err;

	err = tegra210_clk_emc_attach(emc->clk, &emc->provider);
	if (err < 0) {
		dev_err(dev, "failed to attach to EMC clock: %d\n", err);
		return err;
	}

	clk_set_rate(emc->clk, emc->resume_rate);
	clk_rate_exclusive_put(emc->clk);

	dev_dbg(dev, "resuming at %lu Hz\n", clk_get_rate(emc->clk));

	return 0;
}

static const struct dev_pm_ops tegra210_emc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra210_emc_suspend, tegra210_emc_resume)
};

static const struct of_device_id tegra210_emc_of_match[] = {
	{ .compatible = "nvidia,tegra210-emc", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra210_emc_of_match);

static struct platform_driver tegra210_emc_driver = {
	.driver = {
		.name = "tegra210-emc",
		.of_match_table = tegra210_emc_of_match,
		.pm = &tegra210_emc_pm_ops,
	},
	.probe = tegra210_emc_probe,
	.remove = tegra210_emc_remove,
};

module_platform_driver(tegra210_emc_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_AUTHOR("Joseph Lo <josephl@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra210 EMC driver");
MODULE_LICENSE("GPL v2");
