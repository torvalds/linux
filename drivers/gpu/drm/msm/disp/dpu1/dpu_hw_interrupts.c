// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/slab.h>

#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_util.h"
#include "dpu_hw_mdss.h"

/**
 * Register offsets in MDSS register file for the interrupt registers
 * w.r.t. to the MDP base
 */
#define MDP_SSPP_TOP0_OFF		0x0
#define MDP_INTF_0_OFF			0x6A000
#define MDP_INTF_1_OFF			0x6A800
#define MDP_INTF_2_OFF			0x6B000
#define MDP_INTF_3_OFF			0x6B800
#define MDP_INTF_4_OFF			0x6C000
#define MDP_AD4_0_OFF			0x7C000
#define MDP_AD4_1_OFF			0x7D000
#define MDP_AD4_INTR_EN_OFF		0x41c
#define MDP_AD4_INTR_CLEAR_OFF		0x424
#define MDP_AD4_INTR_STATUS_OFF		0x420

/**
 * WB interrupt status bit definitions
 */
#define DPU_INTR_WB_0_DONE BIT(0)
#define DPU_INTR_WB_1_DONE BIT(1)
#define DPU_INTR_WB_2_DONE BIT(4)

/**
 * WDOG timer interrupt status bit definitions
 */
#define DPU_INTR_WD_TIMER_0_DONE BIT(2)
#define DPU_INTR_WD_TIMER_1_DONE BIT(3)
#define DPU_INTR_WD_TIMER_2_DONE BIT(5)
#define DPU_INTR_WD_TIMER_3_DONE BIT(6)
#define DPU_INTR_WD_TIMER_4_DONE BIT(7)

/**
 * Pingpong interrupt status bit definitions
 */
#define DPU_INTR_PING_PONG_0_DONE BIT(8)
#define DPU_INTR_PING_PONG_1_DONE BIT(9)
#define DPU_INTR_PING_PONG_2_DONE BIT(10)
#define DPU_INTR_PING_PONG_3_DONE BIT(11)
#define DPU_INTR_PING_PONG_0_RD_PTR BIT(12)
#define DPU_INTR_PING_PONG_1_RD_PTR BIT(13)
#define DPU_INTR_PING_PONG_2_RD_PTR BIT(14)
#define DPU_INTR_PING_PONG_3_RD_PTR BIT(15)
#define DPU_INTR_PING_PONG_0_WR_PTR BIT(16)
#define DPU_INTR_PING_PONG_1_WR_PTR BIT(17)
#define DPU_INTR_PING_PONG_2_WR_PTR BIT(18)
#define DPU_INTR_PING_PONG_3_WR_PTR BIT(19)
#define DPU_INTR_PING_PONG_0_AUTOREFRESH_DONE BIT(20)
#define DPU_INTR_PING_PONG_1_AUTOREFRESH_DONE BIT(21)
#define DPU_INTR_PING_PONG_2_AUTOREFRESH_DONE BIT(22)
#define DPU_INTR_PING_PONG_3_AUTOREFRESH_DONE BIT(23)

/**
 * Interface interrupt status bit definitions
 */
#define DPU_INTR_INTF_0_UNDERRUN BIT(24)
#define DPU_INTR_INTF_1_UNDERRUN BIT(26)
#define DPU_INTR_INTF_2_UNDERRUN BIT(28)
#define DPU_INTR_INTF_3_UNDERRUN BIT(30)
#define DPU_INTR_INTF_0_VSYNC BIT(25)
#define DPU_INTR_INTF_1_VSYNC BIT(27)
#define DPU_INTR_INTF_2_VSYNC BIT(29)
#define DPU_INTR_INTF_3_VSYNC BIT(31)

/**
 * Pingpong Secondary interrupt status bit definitions
 */
#define DPU_INTR_PING_PONG_S0_AUTOREFRESH_DONE BIT(0)
#define DPU_INTR_PING_PONG_S0_WR_PTR BIT(4)
#define DPU_INTR_PING_PONG_S0_RD_PTR BIT(8)
#define DPU_INTR_PING_PONG_S0_TEAR_DETECTED BIT(22)
#define DPU_INTR_PING_PONG_S0_TE_DETECTED BIT(28)

/**
 * Pingpong TEAR detection interrupt status bit definitions
 */
#define DPU_INTR_PING_PONG_0_TEAR_DETECTED BIT(16)
#define DPU_INTR_PING_PONG_1_TEAR_DETECTED BIT(17)
#define DPU_INTR_PING_PONG_2_TEAR_DETECTED BIT(18)
#define DPU_INTR_PING_PONG_3_TEAR_DETECTED BIT(19)

/**
 * Pingpong TE detection interrupt status bit definitions
 */
#define DPU_INTR_PING_PONG_0_TE_DETECTED BIT(24)
#define DPU_INTR_PING_PONG_1_TE_DETECTED BIT(25)
#define DPU_INTR_PING_PONG_2_TE_DETECTED BIT(26)
#define DPU_INTR_PING_PONG_3_TE_DETECTED BIT(27)

/**
 * Ctl start interrupt status bit definitions
 */
#define DPU_INTR_CTL_0_START BIT(9)
#define DPU_INTR_CTL_1_START BIT(10)
#define DPU_INTR_CTL_2_START BIT(11)
#define DPU_INTR_CTL_3_START BIT(12)
#define DPU_INTR_CTL_4_START BIT(13)

/**
 * Concurrent WB overflow interrupt status bit definitions
 */
#define DPU_INTR_CWB_2_OVERFLOW BIT(14)
#define DPU_INTR_CWB_3_OVERFLOW BIT(15)

/**
 * Histogram VIG done interrupt status bit definitions
 */
#define DPU_INTR_HIST_VIG_0_DONE BIT(0)
#define DPU_INTR_HIST_VIG_1_DONE BIT(4)
#define DPU_INTR_HIST_VIG_2_DONE BIT(8)
#define DPU_INTR_HIST_VIG_3_DONE BIT(10)

/**
 * Histogram VIG reset Sequence done interrupt status bit definitions
 */
#define DPU_INTR_HIST_VIG_0_RSTSEQ_DONE BIT(1)
#define DPU_INTR_HIST_VIG_1_RSTSEQ_DONE BIT(5)
#define DPU_INTR_HIST_VIG_2_RSTSEQ_DONE BIT(9)
#define DPU_INTR_HIST_VIG_3_RSTSEQ_DONE BIT(11)

/**
 * Histogram DSPP done interrupt status bit definitions
 */
#define DPU_INTR_HIST_DSPP_0_DONE BIT(12)
#define DPU_INTR_HIST_DSPP_1_DONE BIT(16)
#define DPU_INTR_HIST_DSPP_2_DONE BIT(20)
#define DPU_INTR_HIST_DSPP_3_DONE BIT(22)

/**
 * Histogram DSPP reset Sequence done interrupt status bit definitions
 */
#define DPU_INTR_HIST_DSPP_0_RSTSEQ_DONE BIT(13)
#define DPU_INTR_HIST_DSPP_1_RSTSEQ_DONE BIT(17)
#define DPU_INTR_HIST_DSPP_2_RSTSEQ_DONE BIT(21)
#define DPU_INTR_HIST_DSPP_3_RSTSEQ_DONE BIT(23)

/**
 * INTF interrupt status bit definitions
 */
#define DPU_INTR_VIDEO_INTO_STATIC BIT(0)
#define DPU_INTR_VIDEO_OUTOF_STATIC BIT(1)
#define DPU_INTR_DSICMD_0_INTO_STATIC BIT(2)
#define DPU_INTR_DSICMD_0_OUTOF_STATIC BIT(3)
#define DPU_INTR_DSICMD_1_INTO_STATIC BIT(4)
#define DPU_INTR_DSICMD_1_OUTOF_STATIC BIT(5)
#define DPU_INTR_DSICMD_2_INTO_STATIC BIT(6)
#define DPU_INTR_DSICMD_2_OUTOF_STATIC BIT(7)
#define DPU_INTR_PROG_LINE BIT(8)

/**
 * AD4 interrupt status bit definitions
 */
#define DPU_INTR_BACKLIGHT_UPDATED BIT(0)
/**
 * struct dpu_intr_reg - array of DPU register sets
 * @clr_off:	offset to CLEAR reg
 * @en_off:	offset to ENABLE reg
 * @status_off:	offset to STATUS reg
 */
struct dpu_intr_reg {
	u32 clr_off;
	u32 en_off;
	u32 status_off;
};

/**
 * struct dpu_irq_type - maps each irq with i/f
 * @intr_type:		type of interrupt listed in dpu_intr_type
 * @instance_idx:	instance index of the associated HW block in DPU
 * @irq_mask:		corresponding bit in the interrupt status reg
 * @reg_idx:		which reg set to use
 */
struct dpu_irq_type {
	u32 intr_type;
	u32 instance_idx;
	u32 irq_mask;
	u32 reg_idx;
};

/**
 * List of DPU interrupt registers
 */
static const struct dpu_intr_reg dpu_intr_set[] = {
	{
		MDP_SSPP_TOP0_OFF+INTR_CLEAR,
		MDP_SSPP_TOP0_OFF+INTR_EN,
		MDP_SSPP_TOP0_OFF+INTR_STATUS
	},
	{
		MDP_SSPP_TOP0_OFF+INTR2_CLEAR,
		MDP_SSPP_TOP0_OFF+INTR2_EN,
		MDP_SSPP_TOP0_OFF+INTR2_STATUS
	},
	{
		MDP_SSPP_TOP0_OFF+HIST_INTR_CLEAR,
		MDP_SSPP_TOP0_OFF+HIST_INTR_EN,
		MDP_SSPP_TOP0_OFF+HIST_INTR_STATUS
	},
	{
		MDP_INTF_0_OFF+INTF_INTR_CLEAR,
		MDP_INTF_0_OFF+INTF_INTR_EN,
		MDP_INTF_0_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_1_OFF+INTF_INTR_CLEAR,
		MDP_INTF_1_OFF+INTF_INTR_EN,
		MDP_INTF_1_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_2_OFF+INTF_INTR_CLEAR,
		MDP_INTF_2_OFF+INTF_INTR_EN,
		MDP_INTF_2_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_3_OFF+INTF_INTR_CLEAR,
		MDP_INTF_3_OFF+INTF_INTR_EN,
		MDP_INTF_3_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_4_OFF+INTF_INTR_CLEAR,
		MDP_INTF_4_OFF+INTF_INTR_EN,
		MDP_INTF_4_OFF+INTF_INTR_STATUS
	},
	{
		MDP_AD4_0_OFF + MDP_AD4_INTR_CLEAR_OFF,
		MDP_AD4_0_OFF + MDP_AD4_INTR_EN_OFF,
		MDP_AD4_0_OFF + MDP_AD4_INTR_STATUS_OFF,
	},
	{
		MDP_AD4_1_OFF + MDP_AD4_INTR_CLEAR_OFF,
		MDP_AD4_1_OFF + MDP_AD4_INTR_EN_OFF,
		MDP_AD4_1_OFF + MDP_AD4_INTR_STATUS_OFF,
	}
};

/**
 * IRQ mapping table - use for lookup an irq_idx in this table that have
 *                     a matching interface type and instance index.
 */
static const struct dpu_irq_type dpu_irq_map[] = {
	/* BEGIN MAP_RANGE: 0-31, INTR */
	/* irq_idx: 0-3 */
	{ DPU_IRQ_TYPE_WB_ROT_COMP, WB_0, DPU_INTR_WB_0_DONE, 0},
	{ DPU_IRQ_TYPE_WB_ROT_COMP, WB_1, DPU_INTR_WB_1_DONE, 0},
	{ DPU_IRQ_TYPE_WD_TIMER, WD_TIMER_0, DPU_INTR_WD_TIMER_0_DONE, 0},
	{ DPU_IRQ_TYPE_WD_TIMER, WD_TIMER_1, DPU_INTR_WD_TIMER_1_DONE, 0},
	/* irq_idx: 4-7 */
	{ DPU_IRQ_TYPE_WB_WFD_COMP, WB_2, DPU_INTR_WB_2_DONE, 0},
	{ DPU_IRQ_TYPE_WD_TIMER, WD_TIMER_2, DPU_INTR_WD_TIMER_2_DONE, 0},
	{ DPU_IRQ_TYPE_WD_TIMER, WD_TIMER_3, DPU_INTR_WD_TIMER_3_DONE, 0},
	{ DPU_IRQ_TYPE_WD_TIMER, WD_TIMER_4, DPU_INTR_WD_TIMER_4_DONE, 0},
	/* irq_idx: 8-11 */
	{ DPU_IRQ_TYPE_PING_PONG_COMP, PINGPONG_0,
		DPU_INTR_PING_PONG_0_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_COMP, PINGPONG_1,
		DPU_INTR_PING_PONG_1_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_COMP, PINGPONG_2,
		DPU_INTR_PING_PONG_2_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_COMP, PINGPONG_3,
		DPU_INTR_PING_PONG_3_DONE, 0},
	/* irq_idx: 12-15 */
	{ DPU_IRQ_TYPE_PING_PONG_RD_PTR, PINGPONG_0,
		DPU_INTR_PING_PONG_0_RD_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_RD_PTR, PINGPONG_1,
		DPU_INTR_PING_PONG_1_RD_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_RD_PTR, PINGPONG_2,
		DPU_INTR_PING_PONG_2_RD_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_RD_PTR, PINGPONG_3,
		DPU_INTR_PING_PONG_3_RD_PTR, 0},
	/* irq_idx: 16-19 */
	{ DPU_IRQ_TYPE_PING_PONG_WR_PTR, PINGPONG_0,
		DPU_INTR_PING_PONG_0_WR_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_WR_PTR, PINGPONG_1,
		DPU_INTR_PING_PONG_1_WR_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_WR_PTR, PINGPONG_2,
		DPU_INTR_PING_PONG_2_WR_PTR, 0},
	{ DPU_IRQ_TYPE_PING_PONG_WR_PTR, PINGPONG_3,
		DPU_INTR_PING_PONG_3_WR_PTR, 0},
	/* irq_idx: 20-23 */
	{ DPU_IRQ_TYPE_PING_PONG_AUTO_REF, PINGPONG_0,
		DPU_INTR_PING_PONG_0_AUTOREFRESH_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_AUTO_REF, PINGPONG_1,
		DPU_INTR_PING_PONG_1_AUTOREFRESH_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_AUTO_REF, PINGPONG_2,
		DPU_INTR_PING_PONG_2_AUTOREFRESH_DONE, 0},
	{ DPU_IRQ_TYPE_PING_PONG_AUTO_REF, PINGPONG_3,
		DPU_INTR_PING_PONG_3_AUTOREFRESH_DONE, 0},
	/* irq_idx: 24-27 */
	{ DPU_IRQ_TYPE_INTF_UNDER_RUN, INTF_0, DPU_INTR_INTF_0_UNDERRUN, 0},
	{ DPU_IRQ_TYPE_INTF_VSYNC, INTF_0, DPU_INTR_INTF_0_VSYNC, 0},
	{ DPU_IRQ_TYPE_INTF_UNDER_RUN, INTF_1, DPU_INTR_INTF_1_UNDERRUN, 0},
	{ DPU_IRQ_TYPE_INTF_VSYNC, INTF_1, DPU_INTR_INTF_1_VSYNC, 0},
	/* irq_idx: 28-31 */
	{ DPU_IRQ_TYPE_INTF_UNDER_RUN, INTF_2, DPU_INTR_INTF_2_UNDERRUN, 0},
	{ DPU_IRQ_TYPE_INTF_VSYNC, INTF_2, DPU_INTR_INTF_2_VSYNC, 0},
	{ DPU_IRQ_TYPE_INTF_UNDER_RUN, INTF_3, DPU_INTR_INTF_3_UNDERRUN, 0},
	{ DPU_IRQ_TYPE_INTF_VSYNC, INTF_3, DPU_INTR_INTF_3_VSYNC, 0},

	/* BEGIN MAP_RANGE: 32-64, INTR2 */
	/* irq_idx: 32-35 */
	{ DPU_IRQ_TYPE_PING_PONG_AUTO_REF, PINGPONG_S0,
		DPU_INTR_PING_PONG_S0_AUTOREFRESH_DONE, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	/* irq_idx: 36-39 */
	{ DPU_IRQ_TYPE_PING_PONG_WR_PTR, PINGPONG_S0,
		DPU_INTR_PING_PONG_S0_WR_PTR, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	/* irq_idx: 40 */
	{ DPU_IRQ_TYPE_PING_PONG_RD_PTR, PINGPONG_S0,
		DPU_INTR_PING_PONG_S0_RD_PTR, 1},
	/* irq_idx: 41-45 */
	{ DPU_IRQ_TYPE_CTL_START, CTL_0,
		DPU_INTR_CTL_0_START, 1},
	{ DPU_IRQ_TYPE_CTL_START, CTL_1,
		DPU_INTR_CTL_1_START, 1},
	{ DPU_IRQ_TYPE_CTL_START, CTL_2,
		DPU_INTR_CTL_2_START, 1},
	{ DPU_IRQ_TYPE_CTL_START, CTL_3,
		DPU_INTR_CTL_3_START, 1},
	{ DPU_IRQ_TYPE_CTL_START, CTL_4,
		DPU_INTR_CTL_4_START, 1},
	/* irq_idx: 46-47 */
	{ DPU_IRQ_TYPE_CWB_OVERFLOW, CWB_2, DPU_INTR_CWB_2_OVERFLOW, 1},
	{ DPU_IRQ_TYPE_CWB_OVERFLOW, CWB_3, DPU_INTR_CWB_3_OVERFLOW, 1},
	/* irq_idx: 48-51 */
	{ DPU_IRQ_TYPE_PING_PONG_TEAR_CHECK, PINGPONG_0,
		DPU_INTR_PING_PONG_0_TEAR_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TEAR_CHECK, PINGPONG_1,
		DPU_INTR_PING_PONG_1_TEAR_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TEAR_CHECK, PINGPONG_2,
		DPU_INTR_PING_PONG_2_TEAR_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TEAR_CHECK, PINGPONG_3,
		DPU_INTR_PING_PONG_3_TEAR_DETECTED, 1},
	/* irq_idx: 52-55 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TEAR_CHECK, PINGPONG_S0,
		DPU_INTR_PING_PONG_S0_TEAR_DETECTED, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	/* irq_idx: 56-59 */
	{ DPU_IRQ_TYPE_PING_PONG_TE_CHECK, PINGPONG_0,
		DPU_INTR_PING_PONG_0_TE_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TE_CHECK, PINGPONG_1,
		DPU_INTR_PING_PONG_1_TE_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TE_CHECK, PINGPONG_2,
		DPU_INTR_PING_PONG_2_TE_DETECTED, 1},
	{ DPU_IRQ_TYPE_PING_PONG_TE_CHECK, PINGPONG_3,
		DPU_INTR_PING_PONG_3_TE_DETECTED, 1},
	/* irq_idx: 60-63 */
	{ DPU_IRQ_TYPE_PING_PONG_TE_CHECK, PINGPONG_S0,
		DPU_INTR_PING_PONG_S0_TE_DETECTED, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 1},

	/* BEGIN MAP_RANGE: 64-95 HIST */
	/* irq_idx: 64-67 */
	{ DPU_IRQ_TYPE_HIST_VIG_DONE, SSPP_VIG0, DPU_INTR_HIST_VIG_0_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_VIG_RSTSEQ, SSPP_VIG0,
		DPU_INTR_HIST_VIG_0_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	/* irq_idx: 68-71 */
	{ DPU_IRQ_TYPE_HIST_VIG_DONE, SSPP_VIG1, DPU_INTR_HIST_VIG_1_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_VIG_RSTSEQ, SSPP_VIG1,
		DPU_INTR_HIST_VIG_1_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	/* irq_idx: 72-75 */
	{ DPU_IRQ_TYPE_HIST_VIG_DONE, SSPP_VIG2, DPU_INTR_HIST_VIG_2_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_VIG_RSTSEQ, SSPP_VIG2,
		DPU_INTR_HIST_VIG_2_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_VIG_DONE, SSPP_VIG3, DPU_INTR_HIST_VIG_3_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_VIG_RSTSEQ, SSPP_VIG3,
		DPU_INTR_HIST_VIG_3_RSTSEQ_DONE, 2},
	/* irq_idx: 76-79 */
	{ DPU_IRQ_TYPE_HIST_DSPP_DONE, DSPP_0, DPU_INTR_HIST_DSPP_0_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_DSPP_RSTSEQ, DSPP_0,
		DPU_INTR_HIST_DSPP_0_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	/* irq_idx: 80-83 */
	{ DPU_IRQ_TYPE_HIST_DSPP_DONE, DSPP_1, DPU_INTR_HIST_DSPP_1_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_DSPP_RSTSEQ, DSPP_1,
		DPU_INTR_HIST_DSPP_1_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	/* irq_idx: 84-87 */
	{ DPU_IRQ_TYPE_HIST_DSPP_DONE, DSPP_2, DPU_INTR_HIST_DSPP_2_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_DSPP_RSTSEQ, DSPP_2,
		DPU_INTR_HIST_DSPP_2_RSTSEQ_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_DSPP_DONE, DSPP_3, DPU_INTR_HIST_DSPP_3_DONE, 2},
	{ DPU_IRQ_TYPE_HIST_DSPP_RSTSEQ, DSPP_3,
		DPU_INTR_HIST_DSPP_3_RSTSEQ_DONE, 2},
	/* irq_idx: 88-91 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	/* irq_idx: 92-95 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 2},

	/* BEGIN MAP_RANGE: 96-127 INTF_0_INTR */
	/* irq_idx: 96-99 */
	{ DPU_IRQ_TYPE_SFI_VIDEO_IN, INTF_0,
		DPU_INTR_VIDEO_INTO_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_VIDEO_OUT, INTF_0,
		DPU_INTR_VIDEO_OUTOF_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_CMD_0_IN, INTF_0,
		DPU_INTR_DSICMD_0_INTO_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_CMD_0_OUT, INTF_0,
		DPU_INTR_DSICMD_0_OUTOF_STATIC, 3},
	/* irq_idx: 100-103 */
	{ DPU_IRQ_TYPE_SFI_CMD_1_IN, INTF_0,
		DPU_INTR_DSICMD_1_INTO_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_CMD_1_OUT, INTF_0,
		DPU_INTR_DSICMD_1_OUTOF_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_CMD_2_IN, INTF_0,
		DPU_INTR_DSICMD_2_INTO_STATIC, 3},
	{ DPU_IRQ_TYPE_SFI_CMD_2_OUT, INTF_0,
		DPU_INTR_DSICMD_2_OUTOF_STATIC, 3},
	/* irq_idx: 104-107 */
	{ DPU_IRQ_TYPE_PROG_LINE, INTF_0, DPU_INTR_PROG_LINE, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	/* irq_idx: 108-111 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	/* irq_idx: 112-115 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	/* irq_idx: 116-119 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	/* irq_idx: 120-123 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	/* irq_idx: 124-127 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 3},

	/* BEGIN MAP_RANGE: 128-159 INTF_1_INTR */
	/* irq_idx: 128-131 */
	{ DPU_IRQ_TYPE_SFI_VIDEO_IN, INTF_1,
		DPU_INTR_VIDEO_INTO_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_VIDEO_OUT, INTF_1,
		DPU_INTR_VIDEO_OUTOF_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_CMD_0_IN, INTF_1,
		DPU_INTR_DSICMD_0_INTO_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_CMD_0_OUT, INTF_1,
		DPU_INTR_DSICMD_0_OUTOF_STATIC, 4},
	/* irq_idx: 132-135 */
	{ DPU_IRQ_TYPE_SFI_CMD_1_IN, INTF_1,
		DPU_INTR_DSICMD_1_INTO_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_CMD_1_OUT, INTF_1,
		DPU_INTR_DSICMD_1_OUTOF_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_CMD_2_IN, INTF_1,
		DPU_INTR_DSICMD_2_INTO_STATIC, 4},
	{ DPU_IRQ_TYPE_SFI_CMD_2_OUT, INTF_1,
		DPU_INTR_DSICMD_2_OUTOF_STATIC, 4},
	/* irq_idx: 136-139 */
	{ DPU_IRQ_TYPE_PROG_LINE, INTF_1, DPU_INTR_PROG_LINE, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	/* irq_idx: 140-143 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	/* irq_idx: 144-147 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	/* irq_idx: 148-151 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	/* irq_idx: 152-155 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	/* irq_idx: 156-159 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 4},

	/* BEGIN MAP_RANGE: 160-191 INTF_2_INTR */
	/* irq_idx: 160-163 */
	{ DPU_IRQ_TYPE_SFI_VIDEO_IN, INTF_2,
		DPU_INTR_VIDEO_INTO_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_VIDEO_OUT, INTF_2,
		DPU_INTR_VIDEO_OUTOF_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_CMD_0_IN, INTF_2,
		DPU_INTR_DSICMD_0_INTO_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_CMD_0_OUT, INTF_2,
		DPU_INTR_DSICMD_0_OUTOF_STATIC, 5},
	/* irq_idx: 164-167 */
	{ DPU_IRQ_TYPE_SFI_CMD_1_IN, INTF_2,
		DPU_INTR_DSICMD_1_INTO_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_CMD_1_OUT, INTF_2,
		DPU_INTR_DSICMD_1_OUTOF_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_CMD_2_IN, INTF_2,
		DPU_INTR_DSICMD_2_INTO_STATIC, 5},
	{ DPU_IRQ_TYPE_SFI_CMD_2_OUT, INTF_2,
		DPU_INTR_DSICMD_2_OUTOF_STATIC, 5},
	/* irq_idx: 168-171 */
	{ DPU_IRQ_TYPE_PROG_LINE, INTF_2, DPU_INTR_PROG_LINE, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	/* irq_idx: 172-175 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	/* irq_idx: 176-179 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	/* irq_idx: 180-183 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	/* irq_idx: 184-187 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	/* irq_idx: 188-191 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 5},

	/* BEGIN MAP_RANGE: 192-223 INTF_3_INTR */
	/* irq_idx: 192-195 */
	{ DPU_IRQ_TYPE_SFI_VIDEO_IN, INTF_3,
		DPU_INTR_VIDEO_INTO_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_VIDEO_OUT, INTF_3,
		DPU_INTR_VIDEO_OUTOF_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_CMD_0_IN, INTF_3,
		DPU_INTR_DSICMD_0_INTO_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_CMD_0_OUT, INTF_3,
		DPU_INTR_DSICMD_0_OUTOF_STATIC, 6},
	/* irq_idx: 196-199 */
	{ DPU_IRQ_TYPE_SFI_CMD_1_IN, INTF_3,
		DPU_INTR_DSICMD_1_INTO_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_CMD_1_OUT, INTF_3,
		DPU_INTR_DSICMD_1_OUTOF_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_CMD_2_IN, INTF_3,
		DPU_INTR_DSICMD_2_INTO_STATIC, 6},
	{ DPU_IRQ_TYPE_SFI_CMD_2_OUT, INTF_3,
		DPU_INTR_DSICMD_2_OUTOF_STATIC, 6},
	/* irq_idx: 200-203 */
	{ DPU_IRQ_TYPE_PROG_LINE, INTF_3, DPU_INTR_PROG_LINE, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	/* irq_idx: 204-207 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	/* irq_idx: 208-211 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	/* irq_idx: 212-215 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	/* irq_idx: 216-219 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	/* irq_idx: 220-223 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 6},

	/* BEGIN MAP_RANGE: 224-255 INTF_4_INTR */
	/* irq_idx: 224-227 */
	{ DPU_IRQ_TYPE_SFI_VIDEO_IN, INTF_4,
		DPU_INTR_VIDEO_INTO_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_VIDEO_OUT, INTF_4,
		DPU_INTR_VIDEO_OUTOF_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_CMD_0_IN, INTF_4,
		DPU_INTR_DSICMD_0_INTO_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_CMD_0_OUT, INTF_4,
		DPU_INTR_DSICMD_0_OUTOF_STATIC, 7},
	/* irq_idx: 228-231 */
	{ DPU_IRQ_TYPE_SFI_CMD_1_IN, INTF_4,
		DPU_INTR_DSICMD_1_INTO_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_CMD_1_OUT, INTF_4,
		DPU_INTR_DSICMD_1_OUTOF_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_CMD_2_IN, INTF_4,
		DPU_INTR_DSICMD_2_INTO_STATIC, 7},
	{ DPU_IRQ_TYPE_SFI_CMD_2_OUT, INTF_4,
		DPU_INTR_DSICMD_2_OUTOF_STATIC, 7},
	/* irq_idx: 232-235 */
	{ DPU_IRQ_TYPE_PROG_LINE, INTF_4, DPU_INTR_PROG_LINE, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	/* irq_idx: 236-239 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	/* irq_idx: 240-243 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	/* irq_idx: 244-247 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	/* irq_idx: 248-251 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	/* irq_idx: 252-255 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 7},

	/* BEGIN MAP_RANGE: 256-287 AD4_0_INTR */
	/* irq_idx: 256-259 */
	{ DPU_IRQ_TYPE_AD4_BL_DONE, DSPP_0, DPU_INTR_BACKLIGHT_UPDATED, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 260-263 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 264-267 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 268-271 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 272-275 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 276-279 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 280-283 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	/* irq_idx: 284-287 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 8},

	/* BEGIN MAP_RANGE: 288-319 AD4_1_INTR */
	/* irq_idx: 288-291 */
	{ DPU_IRQ_TYPE_AD4_BL_DONE, DSPP_1, DPU_INTR_BACKLIGHT_UPDATED, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 292-295 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 296-299 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 300-303 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 304-307 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 308-311 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 312-315 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	/* irq_idx: 315-319 */
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
	{ DPU_IRQ_TYPE_RESERVED, 0, 0, 9},
};

static int dpu_hw_intr_irqidx_lookup(enum dpu_intr_type intr_type,
		u32 instance_idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu_irq_map); i++) {
		if (intr_type == dpu_irq_map[i].intr_type &&
			instance_idx == dpu_irq_map[i].instance_idx)
			return i;
	}

	pr_debug("IRQ lookup fail!! intr_type=%d, instance_idx=%d\n",
			intr_type, instance_idx);
	return -EINVAL;
}

static void dpu_hw_intr_dispatch_irq(struct dpu_hw_intr *intr,
		void (*cbfunc)(void *, int),
		void *arg)
{
	int reg_idx;
	int irq_idx;
	int start_idx;
	int end_idx;
	u32 irq_status;
	unsigned long irq_flags;

	if (!intr)
		return;

	/*
	 * The dispatcher will save the IRQ status before calling here.
	 * Now need to go through each IRQ status and find matching
	 * irq lookup index.
	 */
	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	for (reg_idx = 0; reg_idx < ARRAY_SIZE(dpu_intr_set); reg_idx++) {
		irq_status = intr->save_irq_status[reg_idx];

		/*
		 * Each Interrupt register has a range of 32 indexes, and
		 * that is static for dpu_irq_map.
		 */
		start_idx = reg_idx * 32;
		end_idx = start_idx + 32;

		if (!test_bit(reg_idx, &intr->irq_mask) ||
			start_idx >= ARRAY_SIZE(dpu_irq_map))
			continue;

		/*
		 * Search through matching intr status from irq map.
		 * start_idx and end_idx defined the search range in
		 * the dpu_irq_map.
		 */
		for (irq_idx = start_idx;
				(irq_idx < end_idx) && irq_status;
				irq_idx++)
			if ((irq_status & dpu_irq_map[irq_idx].irq_mask) &&
				(dpu_irq_map[irq_idx].reg_idx == reg_idx)) {
				/*
				 * Once a match on irq mask, perform a callback
				 * to the given cbfunc. cbfunc will take care
				 * the interrupt status clearing. If cbfunc is
				 * not provided, then the interrupt clearing
				 * is here.
				 */
				if (cbfunc)
					cbfunc(arg, irq_idx);
				else
					intr->ops.clear_intr_status_nolock(
							intr, irq_idx);

				/*
				 * When callback finish, clear the irq_status
				 * with the matching mask. Once irq_status
				 * is all cleared, the search can be stopped.
				 */
				irq_status &= ~dpu_irq_map[irq_idx].irq_mask;
			}
	}
	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);
}

static int dpu_hw_intr_enable_irq(struct dpu_hw_intr *intr, int irq_idx)
{
	int reg_idx;
	unsigned long irq_flags;
	const struct dpu_intr_reg *reg;
	const struct dpu_irq_type *irq;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (irq_idx < 0 || irq_idx >= ARRAY_SIZE(dpu_irq_map)) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	irq = &dpu_irq_map[irq_idx];
	reg_idx = irq->reg_idx;
	reg = &dpu_intr_set[reg_idx];

	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if (cache_irq_mask & irq->irq_mask) {
		dbgstr = "DPU IRQ already set:";
	} else {
		dbgstr = "DPU IRQ enabled:";

		cache_irq_mask |= irq->irq_mask;
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, irq->irq_mask);
		/* Enabling interrupts with the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}
	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	pr_debug("%s MASK:0x%.8x, CACHE-MASK:0x%.8x\n", dbgstr,
			irq->irq_mask, cache_irq_mask);

	return 0;
}

static int dpu_hw_intr_disable_irq_nolock(struct dpu_hw_intr *intr, int irq_idx)
{
	int reg_idx;
	const struct dpu_intr_reg *reg;
	const struct dpu_irq_type *irq;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (irq_idx < 0 || irq_idx >= ARRAY_SIZE(dpu_irq_map)) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	irq = &dpu_irq_map[irq_idx];
	reg_idx = irq->reg_idx;
	reg = &dpu_intr_set[reg_idx];

	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if ((cache_irq_mask & irq->irq_mask) == 0) {
		dbgstr = "DPU IRQ is already cleared:";
	} else {
		dbgstr = "DPU IRQ mask disable:";

		cache_irq_mask &= ~irq->irq_mask;
		/* Disable interrupts based on the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, irq->irq_mask);

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}

	pr_debug("%s MASK:0x%.8x, CACHE-MASK:0x%.8x\n", dbgstr,
			irq->irq_mask, cache_irq_mask);

	return 0;
}

static int dpu_hw_intr_disable_irq(struct dpu_hw_intr *intr, int irq_idx)
{
	unsigned long irq_flags;

	if (!intr)
		return -EINVAL;

	if (irq_idx < 0 || irq_idx >= ARRAY_SIZE(dpu_irq_map)) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	dpu_hw_intr_disable_irq_nolock(intr, irq_idx);
	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	return 0;
}

static int dpu_hw_intr_clear_irqs(struct dpu_hw_intr *intr)
{
	int i;

	if (!intr)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].clr_off, 0xffffffff);
	}

	/* ensure register writes go through */
	wmb();

	return 0;
}

static int dpu_hw_intr_disable_irqs(struct dpu_hw_intr *intr)
{
	int i;

	if (!intr)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].en_off, 0x00000000);
	}

	/* ensure register writes go through */
	wmb();

	return 0;
}

static void dpu_hw_intr_get_interrupt_statuses(struct dpu_hw_intr *intr)
{
	int i;
	u32 enable_mask;
	unsigned long irq_flags;

	if (!intr)
		return;

	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (!test_bit(i, &intr->irq_mask))
			continue;

		/* Read interrupt status */
		intr->save_irq_status[i] = DPU_REG_READ(&intr->hw,
				dpu_intr_set[i].status_off);

		/* Read enable mask */
		enable_mask = DPU_REG_READ(&intr->hw, dpu_intr_set[i].en_off);

		/* and clear the interrupt */
		if (intr->save_irq_status[i])
			DPU_REG_WRITE(&intr->hw, dpu_intr_set[i].clr_off,
					intr->save_irq_status[i]);

		/* Finally update IRQ status based on enable mask */
		intr->save_irq_status[i] &= enable_mask;
	}

	/* ensure register writes go through */
	wmb();

	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);
}

static void dpu_hw_intr_clear_intr_status_nolock(struct dpu_hw_intr *intr,
		int irq_idx)
{
	int reg_idx;

	if (!intr)
		return;

	reg_idx = dpu_irq_map[irq_idx].reg_idx;
	DPU_REG_WRITE(&intr->hw, dpu_intr_set[reg_idx].clr_off,
			dpu_irq_map[irq_idx].irq_mask);

	/* ensure register writes go through */
	wmb();
}

static u32 dpu_hw_intr_get_interrupt_status(struct dpu_hw_intr *intr,
		int irq_idx, bool clear)
{
	int reg_idx;
	unsigned long irq_flags;
	u32 intr_status;

	if (!intr)
		return 0;

	if (irq_idx >= ARRAY_SIZE(dpu_irq_map) || irq_idx < 0) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return 0;
	}

	spin_lock_irqsave(&intr->irq_lock, irq_flags);

	reg_idx = dpu_irq_map[irq_idx].reg_idx;
	intr_status = DPU_REG_READ(&intr->hw,
			dpu_intr_set[reg_idx].status_off) &
					dpu_irq_map[irq_idx].irq_mask;
	if (intr_status && clear)
		DPU_REG_WRITE(&intr->hw, dpu_intr_set[reg_idx].clr_off,
				intr_status);

	/* ensure register writes go through */
	wmb();

	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	return intr_status;
}

static void __setup_intr_ops(struct dpu_hw_intr_ops *ops)
{
	ops->irq_idx_lookup = dpu_hw_intr_irqidx_lookup;
	ops->enable_irq = dpu_hw_intr_enable_irq;
	ops->disable_irq = dpu_hw_intr_disable_irq;
	ops->dispatch_irqs = dpu_hw_intr_dispatch_irq;
	ops->clear_all_irqs = dpu_hw_intr_clear_irqs;
	ops->disable_all_irqs = dpu_hw_intr_disable_irqs;
	ops->get_interrupt_statuses = dpu_hw_intr_get_interrupt_statuses;
	ops->clear_intr_status_nolock = dpu_hw_intr_clear_intr_status_nolock;
	ops->get_interrupt_status = dpu_hw_intr_get_interrupt_status;
}

static void __intr_offset(struct dpu_mdss_cfg *m,
		void __iomem *addr, struct dpu_hw_blk_reg_map *hw)
{
	hw->base_off = addr;
	hw->blk_off = m->mdp[0].base;
	hw->hwversion = m->hwversion;
}

struct dpu_hw_intr *dpu_hw_intr_init(void __iomem *addr,
		struct dpu_mdss_cfg *m)
{
	struct dpu_hw_intr *intr;

	if (!addr || !m)
		return ERR_PTR(-EINVAL);

	intr = kzalloc(sizeof(*intr), GFP_KERNEL);
	if (!intr)
		return ERR_PTR(-ENOMEM);

	__intr_offset(m, addr, &intr->hw);
	__setup_intr_ops(&intr->ops);

	intr->irq_idx_tbl_size = ARRAY_SIZE(dpu_irq_map);

	intr->cache_irq_mask = kcalloc(ARRAY_SIZE(dpu_intr_set), sizeof(u32),
			GFP_KERNEL);
	if (intr->cache_irq_mask == NULL) {
		kfree(intr);
		return ERR_PTR(-ENOMEM);
	}

	intr->save_irq_status = kcalloc(ARRAY_SIZE(dpu_intr_set), sizeof(u32),
			GFP_KERNEL);
	if (intr->save_irq_status == NULL) {
		kfree(intr->cache_irq_mask);
		kfree(intr);
		return ERR_PTR(-ENOMEM);
	}

	intr->irq_mask = m->mdss_irqs;
	spin_lock_init(&intr->irq_lock);

	return intr;
}

void dpu_hw_intr_destroy(struct dpu_hw_intr *intr)
{
	if (intr) {
		kfree(intr->cache_irq_mask);
		kfree(intr->save_irq_status);
		kfree(intr);
	}
}

