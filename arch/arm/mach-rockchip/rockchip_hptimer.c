// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/io.h>

#include "rkpm_helpers.h"
#include "rockchip_hptimer.h"

/* hp timer regs */
#define TIMER_HP_REVISION		0x0
#define TIMER_HP_CTRL			0x4
#define TIMER_HP_INT_EN			0x8
#define TIMER_HP_T24_GCD		0xc
#define TIMER_HP_T32_GCD		0x10
#define TIMER_HP_LOAD_COUNT0		0x14
#define TIMER_HP_LOAD_COUNT1		0x18
#define TIMER_HP_T24_DELAT_COUNT0	0x1c
#define TIMER_HP_T24_DELAT_COUNT1	0x20
#define TIMER_HP_CURR_32K_VALUE0	0x24
#define TIMER_HP_CURR_32K_VALUE1	0x28
#define TIMER_HP_CURR_TIMER_VALUE0	0x2c
#define TIMER_HP_CURR_TIMER_VALUE1	0x30
#define TIMER_HP_T24_32BEGIN0		0x34
#define TIMER_HP_T24_32BEGIN1		0x38
#define TIMER_HP_T32_24END0		0x3c
#define TIMER_HP_T32_24END1		0x40
#define TIMER_HP_BEGIN_END_VALID	0x44
#define TIMER_HP_SYNC_REQ		0x48
#define TIMER_HP_INTR_STATUS		0x4c

/* hptimer ctlr */
enum rk_hptimer_ctlr_reg {
	RK_HPTIMER_CTRL_EN = 0,
	RK_HPTIMER_CTRL_MODE = 1,
	RK_HPTIMER_CTRL_CNT_MODE = 3,
};

/* hptimer int */
enum rk_hptimer_int_id_t {
	RK_HPTIMER_INT_REACH = 0,
	RK_HPTIMER_INT_ADJ_DONE = 1,
	RK_HPTIMER_INT_SYNC = 2,
};

#define T24M_GCD		0xb71b
#define T32K_GCD		0x40

#define HPTIMER_WAIT_MAX_US	1000000

static void rk_hptimer_clear_int_st(void __iomem *base, enum rk_hptimer_int_id_t id)
{
	writel_relaxed(BIT(id), base + TIMER_HP_INTR_STATUS);
}

static int rk_hptimer_wait_int_st(void __iomem *base,
				  enum rk_hptimer_int_id_t id,
				  u64 wait_us)
{
	while (!(readl_relaxed(base + TIMER_HP_INTR_STATUS) & BIT(id)) &&
	       --wait_us > 0)
		rkpm_raw_udelay(1);
	dsb();

	if (wait_us == 0) {
		rkpm_printstr("can't wait hptimer int:");
		rkpm_printdec(id);
		rkpm_printch('-');
		rkpm_printhex(readl_relaxed(base + TIMER_HP_INTR_STATUS));
		rkpm_printch('\n');
		return -1;
	} else {
		return 0;
	}
}

static int rk_hptimer_wait_begin_end_valid(void __iomem *base, u64 wait_us)
{
	while ((readl_relaxed(base + TIMER_HP_BEGIN_END_VALID) & 0x3) != 0x3 &&
	       --wait_us > 0)
		rkpm_raw_udelay(1);
	dsb();

	if (wait_us == 0) {
		rkpm_printstr("can't wait hptimer begin_end valid:");
		rkpm_printhex(readl_relaxed(base + TIMER_HP_BEGIN_END_VALID));
		rkpm_printch('\n');
		return -1;
	} else {
		return 0;
	}
}

static u64 rk_hptimer_get_soft_adjust_delt_cnt(void __iomem *base)
{
	u64 begin, end, delt;

	if (rk_hptimer_wait_begin_end_valid(base, HPTIMER_WAIT_MAX_US))
		return 0;

	begin = (u64)readl_relaxed(base + TIMER_HP_T24_32BEGIN0) |
		(u64)readl_relaxed(base + TIMER_HP_T24_32BEGIN1) << 32;
	end = (u64)readl_relaxed(base + TIMER_HP_T32_24END0) |
	      (u64)readl_relaxed(base + TIMER_HP_T32_24END1) << 32;
	delt = (end - begin) * T24M_GCD / T32K_GCD;

	writel_relaxed(0x3, base + TIMER_HP_BEGIN_END_VALID);

	return delt;
}

static void rk_hptimer_soft_adjust_req(void __iomem *base, u64 delt)
{
	if (delt == 0)
		return;

	writel_relaxed(delt & 0xffffffff, base + TIMER_HP_T24_DELAT_COUNT0);
	writel_relaxed((delt >> 32) & 0xffffffff, base + TIMER_HP_T24_DELAT_COUNT1);
	dsb();

	writel_relaxed(0x1, base + TIMER_HP_SYNC_REQ);
	dsb();
}

int rk_hptimer_is_enabled(void __iomem *base)
{
	return !!(readl_relaxed(base + TIMER_HP_CTRL) & BIT(RK_HPTIMER_CTRL_EN));
}

int rk_hptimer_get_mode(void __iomem *base)
{
	return (readl_relaxed(base + TIMER_HP_CTRL) >> RK_HPTIMER_CTRL_MODE) & 0x3;
}

u64 rk_hptimer_get_count(void __iomem *base)
{
	u64 cnt;

	cnt = (u64)readl_relaxed(base + TIMER_HP_CURR_TIMER_VALUE0) |
	      (u64)readl_relaxed(base + TIMER_HP_CURR_TIMER_VALUE1) << 32;

	return cnt;
}

int rk_hptimer_wait_mode(void __iomem *base, enum rk_hptimer_mode_t mode)
{
	if (mode == RK_HPTIMER_NORM_MODE)
		return 0;

	if (mode == RK_HPTIMER_HARD_ADJUST_MODE) {
		/* wait adjust done if hard_adjust_mode */
		if (rk_hptimer_wait_int_st(base, RK_HPTIMER_INT_ADJ_DONE,
					   HPTIMER_WAIT_MAX_US))
			return -1;

		rk_hptimer_clear_int_st(base, RK_HPTIMER_INT_ADJ_DONE);
	} else if (mode == RK_HPTIMER_SOFT_ADJUST_MODE) {
		/* wait 32k sync done */
		if (rk_hptimer_wait_int_st(base, RK_HPTIMER_INT_SYNC,
					   HPTIMER_WAIT_MAX_US))
			return -1;

		rk_hptimer_clear_int_st(base, RK_HPTIMER_INT_SYNC);
	}

	return 0;
}

void rk_hptimer_do_soft_adjust(void __iomem *base)
{
	u64 delt = rk_hptimer_get_soft_adjust_delt_cnt(base);

	rk_hptimer_soft_adjust_req(base, delt);

	rk_hptimer_wait_mode(base, RK_HPTIMER_SOFT_ADJUST_MODE);
}

void rk_hptimer_do_soft_adjust_no_wait(void __iomem *base)
{
	u64 delt = rk_hptimer_get_soft_adjust_delt_cnt(base);

	rk_hptimer_soft_adjust_req(base, delt);
}

void rk_hptimer_mode_init(void __iomem *base, enum rk_hptimer_mode_t mode)
{
	u64 old_cnt = rk_hptimer_get_count(base);
	u32 val;

	writel_relaxed(0x0, base + TIMER_HP_CTRL);
	writel_relaxed(0x0, base + TIMER_HP_INT_EN);
	writel_relaxed(0x7, base + TIMER_HP_INTR_STATUS);
	writel_relaxed(0x3, base + TIMER_HP_BEGIN_END_VALID);
	writel_relaxed(0xffffffff, base + TIMER_HP_LOAD_COUNT0);
	writel_relaxed(0xffffffff, base + TIMER_HP_LOAD_COUNT1);

	/* config T24/T32 GCD if hard_adjust_mode */
	if (mode == RK_HPTIMER_HARD_ADJUST_MODE) {
		writel_relaxed(T24M_GCD, base + TIMER_HP_T24_GCD);
		writel_relaxed(T32K_GCD, base + TIMER_HP_T32_GCD);
	}
	dsb();

	if (mode != RK_HPTIMER_NORM_MODE) {
		writel_relaxed(0x7, base + TIMER_HP_INT_EN);
		writel_relaxed(mode << RK_HPTIMER_CTRL_MODE, base + TIMER_HP_CTRL);
		dsb();
	}

	val = readl_relaxed(base + TIMER_HP_CTRL);
	writel_relaxed(val | BIT(RK_HPTIMER_CTRL_EN), base + TIMER_HP_CTRL);
	dsb();

	/* compensate old_cnt to hptimer if soft_adjust_mode */
	if (mode == RK_HPTIMER_SOFT_ADJUST_MODE)
		rk_hptimer_soft_adjust_req(base, old_cnt);

	if (rk_hptimer_wait_mode(base, mode))
		pr_err("%s: can't wait hptimer mode:%d\n", __func__, mode);
}
