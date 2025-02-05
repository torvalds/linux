// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019, Linaro Limited
 */

#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/nvmem-consumer.h>

/* Register Offsets for RB-CPR and Bit Definitions */

/* RBCPR Version Register */
#define REG_RBCPR_VERSION		0
#define RBCPR_VER_2			0x02
#define FLAGS_IGNORE_1ST_IRQ_STATUS	BIT(0)

/* RBCPR Gate Count and Target Registers */
#define REG_RBCPR_GCNT_TARGET(n)	(0x60 + 4 * (n))

#define RBCPR_GCNT_TARGET_TARGET_SHIFT	0
#define RBCPR_GCNT_TARGET_TARGET_MASK	GENMASK(11, 0)
#define RBCPR_GCNT_TARGET_GCNT_SHIFT	12
#define RBCPR_GCNT_TARGET_GCNT_MASK	GENMASK(9, 0)

/* RBCPR Timer Control */
#define REG_RBCPR_TIMER_INTERVAL	0x44
#define REG_RBIF_TIMER_ADJUST		0x4c

#define RBIF_TIMER_ADJ_CONS_UP_MASK	GENMASK(3, 0)
#define RBIF_TIMER_ADJ_CONS_UP_SHIFT	0
#define RBIF_TIMER_ADJ_CONS_DOWN_MASK	GENMASK(3, 0)
#define RBIF_TIMER_ADJ_CONS_DOWN_SHIFT	4
#define RBIF_TIMER_ADJ_CLAMP_INT_MASK	GENMASK(7, 0)
#define RBIF_TIMER_ADJ_CLAMP_INT_SHIFT	8

/* RBCPR Config Register */
#define REG_RBIF_LIMIT			0x48
#define RBIF_LIMIT_CEILING_MASK		GENMASK(5, 0)
#define RBIF_LIMIT_CEILING_SHIFT	6
#define RBIF_LIMIT_FLOOR_BITS		6
#define RBIF_LIMIT_FLOOR_MASK		GENMASK(5, 0)

#define RBIF_LIMIT_CEILING_DEFAULT	RBIF_LIMIT_CEILING_MASK
#define RBIF_LIMIT_FLOOR_DEFAULT	0

#define REG_RBIF_SW_VLEVEL		0x94
#define RBIF_SW_VLEVEL_DEFAULT		0x20

#define REG_RBCPR_STEP_QUOT		0x80
#define RBCPR_STEP_QUOT_STEPQUOT_MASK	GENMASK(7, 0)
#define RBCPR_STEP_QUOT_IDLE_CLK_MASK	GENMASK(3, 0)
#define RBCPR_STEP_QUOT_IDLE_CLK_SHIFT	8

/* RBCPR Control Register */
#define REG_RBCPR_CTL			0x90

#define RBCPR_CTL_LOOP_EN			BIT(0)
#define RBCPR_CTL_TIMER_EN			BIT(3)
#define RBCPR_CTL_SW_AUTO_CONT_ACK_EN		BIT(5)
#define RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN	BIT(6)
#define RBCPR_CTL_COUNT_MODE			BIT(10)
#define RBCPR_CTL_UP_THRESHOLD_MASK	GENMASK(3, 0)
#define RBCPR_CTL_UP_THRESHOLD_SHIFT	24
#define RBCPR_CTL_DN_THRESHOLD_MASK	GENMASK(3, 0)
#define RBCPR_CTL_DN_THRESHOLD_SHIFT	28

/* RBCPR Ack/Nack Response */
#define REG_RBIF_CONT_ACK_CMD		0x98
#define REG_RBIF_CONT_NACK_CMD		0x9c

/* RBCPR Result status Register */
#define REG_RBCPR_RESULT_0		0xa0

#define RBCPR_RESULT0_BUSY_SHIFT	19
#define RBCPR_RESULT0_BUSY_MASK		BIT(RBCPR_RESULT0_BUSY_SHIFT)
#define RBCPR_RESULT0_ERROR_LT0_SHIFT	18
#define RBCPR_RESULT0_ERROR_SHIFT	6
#define RBCPR_RESULT0_ERROR_MASK	GENMASK(11, 0)
#define RBCPR_RESULT0_ERROR_STEPS_SHIFT	2
#define RBCPR_RESULT0_ERROR_STEPS_MASK	GENMASK(3, 0)
#define RBCPR_RESULT0_STEP_UP_SHIFT	1

/* RBCPR Interrupt Control Register */
#define REG_RBIF_IRQ_EN(n)		(0x100 + 4 * (n))
#define REG_RBIF_IRQ_CLEAR		0x110
#define REG_RBIF_IRQ_STATUS		0x114

#define CPR_INT_DONE		BIT(0)
#define CPR_INT_MIN		BIT(1)
#define CPR_INT_DOWN		BIT(2)
#define CPR_INT_MID		BIT(3)
#define CPR_INT_UP		BIT(4)
#define CPR_INT_MAX		BIT(5)
#define CPR_INT_CLAMP		BIT(6)
#define CPR_INT_ALL	(CPR_INT_DONE | CPR_INT_MIN | CPR_INT_DOWN | \
			CPR_INT_MID | CPR_INT_UP | CPR_INT_MAX | CPR_INT_CLAMP)
#define CPR_INT_DEFAULT	(CPR_INT_UP | CPR_INT_DOWN)

#define CPR_NUM_RING_OSC	8

/* CPR eFuse parameters */
#define CPR_FUSE_TARGET_QUOT_BITS_MASK	GENMASK(11, 0)

#define CPR_FUSE_MIN_QUOT_DIFF		50

#define FUSE_REVISION_UNKNOWN		(-1)

enum voltage_change_dir {
	NO_CHANGE,
	DOWN,
	UP,
};

struct cpr_fuse {
	char *ring_osc;
	char *init_voltage;
	char *quotient;
	char *quotient_offset;
};

struct fuse_corner_data {
	int ref_uV;
	int max_uV;
	int min_uV;
	int max_volt_scale;
	int max_quot_scale;
	/* fuse quot */
	int quot_offset;
	int quot_scale;
	int quot_adjust;
	/* fuse quot_offset */
	int quot_offset_scale;
	int quot_offset_adjust;
};

struct cpr_fuses {
	int init_voltage_step;
	int init_voltage_width;
	struct fuse_corner_data *fuse_corner_data;
};

struct corner_data {
	unsigned int fuse_corner;
	unsigned long freq;
};

struct cpr_desc {
	unsigned int num_fuse_corners;
	int min_diff_quot;
	int *step_quot;

	unsigned int		timer_delay_us;
	unsigned int		timer_cons_up;
	unsigned int		timer_cons_down;
	unsigned int		up_threshold;
	unsigned int		down_threshold;
	unsigned int		idle_clocks;
	unsigned int		gcnt_us;
	unsigned int		vdd_apc_step_up_limit;
	unsigned int		vdd_apc_step_down_limit;
	unsigned int		clamp_timer_interval;

	struct cpr_fuses cpr_fuses;
	bool reduce_to_fuse_uV;
	bool reduce_to_corner_uV;
};

struct acc_desc {
	unsigned int	enable_reg;
	u32		enable_mask;

	struct reg_sequence	*config;
	struct reg_sequence	*settings;
	int			num_regs_per_fuse;
};

struct cpr_acc_desc {
	const struct cpr_desc *cpr_desc;
	const struct acc_desc *acc_desc;
};

struct fuse_corner {
	int min_uV;
	int max_uV;
	int uV;
	int quot;
	int step_quot;
	const struct reg_sequence *accs;
	int num_accs;
	unsigned long max_freq;
	u8 ring_osc_idx;
};

struct corner {
	int min_uV;
	int max_uV;
	int uV;
	int last_uV;
	int quot_adjust;
	u32 save_ctl;
	u32 save_irq;
	unsigned long freq;
	struct fuse_corner *fuse_corner;
};

struct cpr_drv {
	unsigned int		num_corners;
	unsigned int		ref_clk_khz;

	struct generic_pm_domain pd;
	struct device		*dev;
	struct device		*attached_cpu_dev;
	struct mutex		lock;
	void __iomem		*base;
	struct corner		*corner;
	struct regulator	*vdd_apc;
	struct clk		*cpu_clk;
	struct regmap		*tcsr;
	bool			loop_disabled;
	u32			gcnt;
	unsigned long		flags;

	struct fuse_corner	*fuse_corners;
	struct corner		*corners;

	const struct cpr_desc *desc;
	const struct acc_desc *acc_desc;
	const struct cpr_fuse *cpr_fuses;

	struct dentry *debugfs;
};

static bool cpr_is_allowed(struct cpr_drv *drv)
{
	return !drv->loop_disabled;
}

static void cpr_write(struct cpr_drv *drv, u32 offset, u32 value)
{
	writel_relaxed(value, drv->base + offset);
}

static u32 cpr_read(struct cpr_drv *drv, u32 offset)
{
	return readl_relaxed(drv->base + offset);
}

static void
cpr_masked_write(struct cpr_drv *drv, u32 offset, u32 mask, u32 value)
{
	u32 val;

	val = readl_relaxed(drv->base + offset);
	val &= ~mask;
	val |= value & mask;
	writel_relaxed(val, drv->base + offset);
}

static void cpr_irq_clr(struct cpr_drv *drv)
{
	cpr_write(drv, REG_RBIF_IRQ_CLEAR, CPR_INT_ALL);
}

static void cpr_irq_clr_nack(struct cpr_drv *drv)
{
	cpr_irq_clr(drv);
	cpr_write(drv, REG_RBIF_CONT_NACK_CMD, 1);
}

static void cpr_irq_clr_ack(struct cpr_drv *drv)
{
	cpr_irq_clr(drv);
	cpr_write(drv, REG_RBIF_CONT_ACK_CMD, 1);
}

static void cpr_irq_set(struct cpr_drv *drv, u32 int_bits)
{
	cpr_write(drv, REG_RBIF_IRQ_EN(0), int_bits);
}

static void cpr_ctl_modify(struct cpr_drv *drv, u32 mask, u32 value)
{
	cpr_masked_write(drv, REG_RBCPR_CTL, mask, value);
}

static void cpr_ctl_enable(struct cpr_drv *drv, struct corner *corner)
{
	u32 val, mask;
	const struct cpr_desc *desc = drv->desc;

	/* Program Consecutive Up & Down */
	val = desc->timer_cons_down << RBIF_TIMER_ADJ_CONS_DOWN_SHIFT;
	val |= desc->timer_cons_up << RBIF_TIMER_ADJ_CONS_UP_SHIFT;
	mask = RBIF_TIMER_ADJ_CONS_UP_MASK | RBIF_TIMER_ADJ_CONS_DOWN_MASK;
	cpr_masked_write(drv, REG_RBIF_TIMER_ADJUST, mask, val);
	cpr_masked_write(drv, REG_RBCPR_CTL,
			 RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN |
			 RBCPR_CTL_SW_AUTO_CONT_ACK_EN,
			 corner->save_ctl);
	cpr_irq_set(drv, corner->save_irq);

	if (cpr_is_allowed(drv) && corner->max_uV > corner->min_uV)
		val = RBCPR_CTL_LOOP_EN;
	else
		val = 0;
	cpr_ctl_modify(drv, RBCPR_CTL_LOOP_EN, val);
}

static void cpr_ctl_disable(struct cpr_drv *drv)
{
	cpr_irq_set(drv, 0);
	cpr_ctl_modify(drv, RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN |
		       RBCPR_CTL_SW_AUTO_CONT_ACK_EN, 0);
	cpr_masked_write(drv, REG_RBIF_TIMER_ADJUST,
			 RBIF_TIMER_ADJ_CONS_UP_MASK |
			 RBIF_TIMER_ADJ_CONS_DOWN_MASK, 0);
	cpr_irq_clr(drv);
	cpr_write(drv, REG_RBIF_CONT_ACK_CMD, 1);
	cpr_write(drv, REG_RBIF_CONT_NACK_CMD, 1);
	cpr_ctl_modify(drv, RBCPR_CTL_LOOP_EN, 0);
}

static bool cpr_ctl_is_enabled(struct cpr_drv *drv)
{
	u32 reg_val;

	reg_val = cpr_read(drv, REG_RBCPR_CTL);
	return reg_val & RBCPR_CTL_LOOP_EN;
}

static bool cpr_ctl_is_busy(struct cpr_drv *drv)
{
	u32 reg_val;

	reg_val = cpr_read(drv, REG_RBCPR_RESULT_0);
	return reg_val & RBCPR_RESULT0_BUSY_MASK;
}

static void cpr_corner_save(struct cpr_drv *drv, struct corner *corner)
{
	corner->save_ctl = cpr_read(drv, REG_RBCPR_CTL);
	corner->save_irq = cpr_read(drv, REG_RBIF_IRQ_EN(0));
}

static void cpr_corner_restore(struct cpr_drv *drv, struct corner *corner)
{
	u32 gcnt, ctl, irq, ro_sel, step_quot;
	struct fuse_corner *fuse = corner->fuse_corner;
	const struct cpr_desc *desc = drv->desc;
	int i;

	ro_sel = fuse->ring_osc_idx;
	gcnt = drv->gcnt;
	gcnt |= fuse->quot - corner->quot_adjust;

	/* Program the step quotient and idle clocks */
	step_quot = desc->idle_clocks << RBCPR_STEP_QUOT_IDLE_CLK_SHIFT;
	step_quot |= fuse->step_quot & RBCPR_STEP_QUOT_STEPQUOT_MASK;
	cpr_write(drv, REG_RBCPR_STEP_QUOT, step_quot);

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(drv, REG_RBCPR_GCNT_TARGET(i), 0);

	cpr_write(drv, REG_RBCPR_GCNT_TARGET(ro_sel), gcnt);
	ctl = corner->save_ctl;
	cpr_write(drv, REG_RBCPR_CTL, ctl);
	irq = corner->save_irq;
	cpr_irq_set(drv, irq);
	dev_dbg(drv->dev, "gcnt = %#08x, ctl = %#08x, irq = %#08x\n", gcnt,
		ctl, irq);
}

static void cpr_set_acc(struct regmap *tcsr, struct fuse_corner *f,
			struct fuse_corner *end)
{
	if (f == end)
		return;

	if (f < end) {
		for (f += 1; f <= end; f++)
			regmap_multi_reg_write(tcsr, f->accs, f->num_accs);
	} else {
		for (f -= 1; f >= end; f--)
			regmap_multi_reg_write(tcsr, f->accs, f->num_accs);
	}
}

static int cpr_pre_voltage(struct cpr_drv *drv,
			   struct fuse_corner *fuse_corner,
			   enum voltage_change_dir dir)
{
	struct fuse_corner *prev_fuse_corner = drv->corner->fuse_corner;

	if (drv->tcsr && dir == DOWN)
		cpr_set_acc(drv->tcsr, prev_fuse_corner, fuse_corner);

	return 0;
}

static int cpr_post_voltage(struct cpr_drv *drv,
			    struct fuse_corner *fuse_corner,
			    enum voltage_change_dir dir)
{
	struct fuse_corner *prev_fuse_corner = drv->corner->fuse_corner;

	if (drv->tcsr && dir == UP)
		cpr_set_acc(drv->tcsr, prev_fuse_corner, fuse_corner);

	return 0;
}

static int cpr_scale_voltage(struct cpr_drv *drv, struct corner *corner,
			     int new_uV, enum voltage_change_dir dir)
{
	int ret;
	struct fuse_corner *fuse_corner = corner->fuse_corner;

	ret = cpr_pre_voltage(drv, fuse_corner, dir);
	if (ret)
		return ret;

	ret = regulator_set_voltage(drv->vdd_apc, new_uV, new_uV);
	if (ret) {
		dev_err_ratelimited(drv->dev, "failed to set apc voltage %d\n",
				    new_uV);
		return ret;
	}

	ret = cpr_post_voltage(drv, fuse_corner, dir);
	if (ret)
		return ret;

	return 0;
}

static unsigned int cpr_get_cur_perf_state(struct cpr_drv *drv)
{
	return drv->corner ? drv->corner - drv->corners + 1 : 0;
}

static int cpr_scale(struct cpr_drv *drv, enum voltage_change_dir dir)
{
	u32 val, error_steps, reg_mask;
	int last_uV, new_uV, step_uV, ret;
	struct corner *corner;
	const struct cpr_desc *desc = drv->desc;

	if (dir != UP && dir != DOWN)
		return 0;

	step_uV = regulator_get_linear_step(drv->vdd_apc);
	if (!step_uV)
		return -EINVAL;

	corner = drv->corner;

	val = cpr_read(drv, REG_RBCPR_RESULT_0);

	error_steps = val >> RBCPR_RESULT0_ERROR_STEPS_SHIFT;
	error_steps &= RBCPR_RESULT0_ERROR_STEPS_MASK;
	last_uV = corner->last_uV;

	if (dir == UP) {
		if (desc->clamp_timer_interval &&
		    error_steps < desc->up_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(desc->up_threshold,
					  desc->vdd_apc_step_up_limit);
		}

		if (last_uV >= corner->max_uV) {
			cpr_irq_clr_nack(drv);

			/* Maximize the UP threshold */
			reg_mask = RBCPR_CTL_UP_THRESHOLD_MASK;
			reg_mask <<= RBCPR_CTL_UP_THRESHOLD_SHIFT;
			val = reg_mask;
			cpr_ctl_modify(drv, reg_mask, val);

			/* Disable UP interrupt */
			cpr_irq_set(drv, CPR_INT_DEFAULT & ~CPR_INT_UP);

			return 0;
		}

		if (error_steps > desc->vdd_apc_step_up_limit)
			error_steps = desc->vdd_apc_step_up_limit;

		/* Calculate new voltage */
		new_uV = last_uV + error_steps * step_uV;
		new_uV = min(new_uV, corner->max_uV);

		dev_dbg(drv->dev,
			"UP: -> new_uV: %d last_uV: %d perf state: %u\n",
			new_uV, last_uV, cpr_get_cur_perf_state(drv));
	} else {
		if (desc->clamp_timer_interval &&
		    error_steps < desc->down_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(desc->down_threshold,
					  desc->vdd_apc_step_down_limit);
		}

		if (last_uV <= corner->min_uV) {
			cpr_irq_clr_nack(drv);

			/* Enable auto nack down */
			reg_mask = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;
			val = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;

			cpr_ctl_modify(drv, reg_mask, val);

			/* Disable DOWN interrupt */
			cpr_irq_set(drv, CPR_INT_DEFAULT & ~CPR_INT_DOWN);

			return 0;
		}

		if (error_steps > desc->vdd_apc_step_down_limit)
			error_steps = desc->vdd_apc_step_down_limit;

		/* Calculate new voltage */
		new_uV = last_uV - error_steps * step_uV;
		new_uV = max(new_uV, corner->min_uV);

		dev_dbg(drv->dev,
			"DOWN: -> new_uV: %d last_uV: %d perf state: %u\n",
			new_uV, last_uV, cpr_get_cur_perf_state(drv));
	}

	ret = cpr_scale_voltage(drv, corner, new_uV, dir);
	if (ret) {
		cpr_irq_clr_nack(drv);
		return ret;
	}
	drv->corner->last_uV = new_uV;

	if (dir == UP) {
		/* Disable auto nack down */
		reg_mask = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;
		val = 0;
	} else {
		/* Restore default threshold for UP */
		reg_mask = RBCPR_CTL_UP_THRESHOLD_MASK;
		reg_mask <<= RBCPR_CTL_UP_THRESHOLD_SHIFT;
		val = desc->up_threshold;
		val <<= RBCPR_CTL_UP_THRESHOLD_SHIFT;
	}

	cpr_ctl_modify(drv, reg_mask, val);

	/* Re-enable default interrupts */
	cpr_irq_set(drv, CPR_INT_DEFAULT);

	/* Ack */
	cpr_irq_clr_ack(drv);

	return 0;
}

static irqreturn_t cpr_irq_handler(int irq, void *dev)
{
	struct cpr_drv *drv = dev;
	const struct cpr_desc *desc = drv->desc;
	irqreturn_t ret = IRQ_HANDLED;
	u32 val;

	mutex_lock(&drv->lock);

	val = cpr_read(drv, REG_RBIF_IRQ_STATUS);
	if (drv->flags & FLAGS_IGNORE_1ST_IRQ_STATUS)
		val = cpr_read(drv, REG_RBIF_IRQ_STATUS);

	dev_dbg(drv->dev, "IRQ_STATUS = %#02x\n", val);

	if (!cpr_ctl_is_enabled(drv)) {
		dev_dbg(drv->dev, "CPR is disabled\n");
		ret = IRQ_NONE;
	} else if (cpr_ctl_is_busy(drv) && !desc->clamp_timer_interval) {
		dev_dbg(drv->dev, "CPR measurement is not ready\n");
	} else if (!cpr_is_allowed(drv)) {
		val = cpr_read(drv, REG_RBCPR_CTL);
		dev_err_ratelimited(drv->dev,
				    "Interrupt broken? RBCPR_CTL = %#02x\n",
				    val);
		ret = IRQ_NONE;
	} else {
		/*
		 * Following sequence of handling is as per each IRQ's
		 * priority
		 */
		if (val & CPR_INT_UP) {
			cpr_scale(drv, UP);
		} else if (val & CPR_INT_DOWN) {
			cpr_scale(drv, DOWN);
		} else if (val & CPR_INT_MIN) {
			cpr_irq_clr_nack(drv);
		} else if (val & CPR_INT_MAX) {
			cpr_irq_clr_nack(drv);
		} else if (val & CPR_INT_MID) {
			/* RBCPR_CTL_SW_AUTO_CONT_ACK_EN is enabled */
			dev_dbg(drv->dev, "IRQ occurred for Mid Flag\n");
		} else {
			dev_dbg(drv->dev,
				"IRQ occurred for unknown flag (%#08x)\n", val);
		}

		/* Save register values for the corner */
		cpr_corner_save(drv, drv->corner);
	}

	mutex_unlock(&drv->lock);

	return ret;
}

static int cpr_enable(struct cpr_drv *drv)
{
	int ret;

	ret = regulator_enable(drv->vdd_apc);
	if (ret)
		return ret;

	mutex_lock(&drv->lock);

	if (cpr_is_allowed(drv) && drv->corner) {
		cpr_irq_clr(drv);
		cpr_corner_restore(drv, drv->corner);
		cpr_ctl_enable(drv, drv->corner);
	}

	mutex_unlock(&drv->lock);

	return 0;
}

static int cpr_disable(struct cpr_drv *drv)
{
	mutex_lock(&drv->lock);

	if (cpr_is_allowed(drv)) {
		cpr_ctl_disable(drv);
		cpr_irq_clr(drv);
	}

	mutex_unlock(&drv->lock);

	return regulator_disable(drv->vdd_apc);
}

static int cpr_config(struct cpr_drv *drv)
{
	int i;
	u32 val, gcnt;
	struct corner *corner;
	const struct cpr_desc *desc = drv->desc;

	/* Disable interrupt and CPR */
	cpr_write(drv, REG_RBIF_IRQ_EN(0), 0);
	cpr_write(drv, REG_RBCPR_CTL, 0);

	/* Program the default HW ceiling, floor and vlevel */
	val = (RBIF_LIMIT_CEILING_DEFAULT & RBIF_LIMIT_CEILING_MASK)
		<< RBIF_LIMIT_CEILING_SHIFT;
	val |= RBIF_LIMIT_FLOOR_DEFAULT & RBIF_LIMIT_FLOOR_MASK;
	cpr_write(drv, REG_RBIF_LIMIT, val);
	cpr_write(drv, REG_RBIF_SW_VLEVEL, RBIF_SW_VLEVEL_DEFAULT);

	/*
	 * Clear the target quotient value and gate count of all
	 * ring oscillators
	 */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(drv, REG_RBCPR_GCNT_TARGET(i), 0);

	/* Init and save gcnt */
	gcnt = (drv->ref_clk_khz * desc->gcnt_us) / 1000;
	gcnt = gcnt & RBCPR_GCNT_TARGET_GCNT_MASK;
	gcnt <<= RBCPR_GCNT_TARGET_GCNT_SHIFT;
	drv->gcnt = gcnt;

	/* Program the delay count for the timer */
	val = (drv->ref_clk_khz * desc->timer_delay_us) / 1000;
	cpr_write(drv, REG_RBCPR_TIMER_INTERVAL, val);
	dev_dbg(drv->dev, "Timer count: %#0x (for %d us)\n", val,
		desc->timer_delay_us);

	/* Program Consecutive Up & Down */
	val = desc->timer_cons_down << RBIF_TIMER_ADJ_CONS_DOWN_SHIFT;
	val |= desc->timer_cons_up << RBIF_TIMER_ADJ_CONS_UP_SHIFT;
	val |= desc->clamp_timer_interval << RBIF_TIMER_ADJ_CLAMP_INT_SHIFT;
	cpr_write(drv, REG_RBIF_TIMER_ADJUST, val);

	/* Program the control register */
	val = desc->up_threshold << RBCPR_CTL_UP_THRESHOLD_SHIFT;
	val |= desc->down_threshold << RBCPR_CTL_DN_THRESHOLD_SHIFT;
	val |= RBCPR_CTL_TIMER_EN | RBCPR_CTL_COUNT_MODE;
	val |= RBCPR_CTL_SW_AUTO_CONT_ACK_EN;
	cpr_write(drv, REG_RBCPR_CTL, val);

	for (i = 0; i < drv->num_corners; i++) {
		corner = &drv->corners[i];
		corner->save_ctl = val;
		corner->save_irq = CPR_INT_DEFAULT;
	}

	cpr_irq_set(drv, CPR_INT_DEFAULT);

	val = cpr_read(drv, REG_RBCPR_VERSION);
	if (val <= RBCPR_VER_2)
		drv->flags |= FLAGS_IGNORE_1ST_IRQ_STATUS;

	return 0;
}

static int cpr_set_performance_state(struct generic_pm_domain *domain,
				     unsigned int state)
{
	struct cpr_drv *drv = container_of(domain, struct cpr_drv, pd);
	struct corner *corner, *end;
	enum voltage_change_dir dir;
	int ret, new_uV;

	guard(mutex)(&drv->lock);

	dev_dbg(drv->dev, "%s: setting perf state: %u (prev state: %u)\n",
		__func__, state, cpr_get_cur_perf_state(drv));

	/*
	 * Determine new corner we're going to.
	 * Remove one since lowest performance state is 1.
	 */
	corner = drv->corners + state - 1;
	end = &drv->corners[drv->num_corners - 1];
	if (corner > end || corner < drv->corners)
		return -EINVAL;

	/* Determine direction */
	if (drv->corner > corner)
		dir = DOWN;
	else if (drv->corner < corner)
		dir = UP;
	else
		dir = NO_CHANGE;

	if (cpr_is_allowed(drv))
		new_uV = corner->last_uV;
	else
		new_uV = corner->uV;

	if (cpr_is_allowed(drv))
		cpr_ctl_disable(drv);

	ret = cpr_scale_voltage(drv, corner, new_uV, dir);
	if (ret)
		return ret;

	if (cpr_is_allowed(drv)) {
		cpr_irq_clr(drv);
		if (drv->corner != corner)
			cpr_corner_restore(drv, corner);
		cpr_ctl_enable(drv, corner);
	}

	drv->corner = corner;

	return 0;
}

static int
cpr_populate_ring_osc_idx(struct cpr_drv *drv)
{
	struct fuse_corner *fuse = drv->fuse_corners;
	struct fuse_corner *end = fuse + drv->desc->num_fuse_corners;
	const struct cpr_fuse *fuses = drv->cpr_fuses;
	u32 data;
	int ret;

	for (; fuse < end; fuse++, fuses++) {
		ret = nvmem_cell_read_variable_le_u32(drv->dev, fuses->ring_osc, &data);
		if (ret)
			return ret;
		fuse->ring_osc_idx = data;
	}

	return 0;
}

static int cpr_read_fuse_uV(const struct cpr_desc *desc,
			    const struct fuse_corner_data *fdata,
			    const char *init_v_efuse,
			    int step_volt,
			    struct cpr_drv *drv)
{
	int step_size_uV, steps, uV;
	u32 bits = 0;
	int ret;

	ret = nvmem_cell_read_variable_le_u32(drv->dev, init_v_efuse, &bits);
	if (ret)
		return ret;

	steps = bits & ~BIT(desc->cpr_fuses.init_voltage_width - 1);
	/* Not two's complement.. instead highest bit is sign bit */
	if (bits & BIT(desc->cpr_fuses.init_voltage_width - 1))
		steps = -steps;

	step_size_uV = desc->cpr_fuses.init_voltage_step;

	uV = fdata->ref_uV + steps * step_size_uV;
	return DIV_ROUND_UP(uV, step_volt) * step_volt;
}

static int cpr_fuse_corner_init(struct cpr_drv *drv)
{
	const struct cpr_desc *desc = drv->desc;
	const struct cpr_fuse *fuses = drv->cpr_fuses;
	const struct acc_desc *acc_desc = drv->acc_desc;
	int i;
	unsigned int step_volt;
	struct fuse_corner_data *fdata;
	struct fuse_corner *fuse, *end;
	int uV;
	const struct reg_sequence *accs;
	int ret;

	accs = acc_desc->settings;

	step_volt = regulator_get_linear_step(drv->vdd_apc);
	if (!step_volt)
		return -EINVAL;

	/* Populate fuse_corner members */
	fuse = drv->fuse_corners;
	end = &fuse[desc->num_fuse_corners - 1];
	fdata = desc->cpr_fuses.fuse_corner_data;

	for (i = 0; fuse <= end; fuse++, fuses++, i++, fdata++) {
		/*
		 * Update SoC voltages: platforms might choose a different
		 * regulators than the one used to characterize the algorithms
		 * (ie, init_voltage_step).
		 */
		fdata->min_uV = roundup(fdata->min_uV, step_volt);
		fdata->max_uV = roundup(fdata->max_uV, step_volt);

		/* Populate uV */
		uV = cpr_read_fuse_uV(desc, fdata, fuses->init_voltage,
				      step_volt, drv);
		if (uV < 0)
			return uV;

		fuse->min_uV = fdata->min_uV;
		fuse->max_uV = fdata->max_uV;
		fuse->uV = clamp(uV, fuse->min_uV, fuse->max_uV);

		if (fuse == end) {
			/*
			 * Allow the highest fuse corner's PVS voltage to
			 * define the ceiling voltage for that corner in order
			 * to support SoC's in which variable ceiling values
			 * are required.
			 */
			end->max_uV = max(end->max_uV, end->uV);
		}

		/* Populate target quotient by scaling */
		ret = nvmem_cell_read_variable_le_u32(drv->dev, fuses->quotient, &fuse->quot);
		if (ret)
			return ret;

		fuse->quot *= fdata->quot_scale;
		fuse->quot += fdata->quot_offset;
		fuse->quot += fdata->quot_adjust;
		fuse->step_quot = desc->step_quot[fuse->ring_osc_idx];

		/* Populate acc settings */
		fuse->accs = accs;
		fuse->num_accs = acc_desc->num_regs_per_fuse;
		accs += acc_desc->num_regs_per_fuse;
	}

	/*
	 * Restrict all fuse corner PVS voltages based upon per corner
	 * ceiling and floor voltages.
	 */
	for (fuse = drv->fuse_corners, i = 0; fuse <= end; fuse++, i++) {
		if (fuse->uV > fuse->max_uV)
			fuse->uV = fuse->max_uV;
		else if (fuse->uV < fuse->min_uV)
			fuse->uV = fuse->min_uV;

		ret = regulator_is_supported_voltage(drv->vdd_apc,
						     fuse->min_uV,
						     fuse->min_uV);
		if (!ret) {
			dev_err(drv->dev,
				"min uV: %d (fuse corner: %d) not supported by regulator\n",
				fuse->min_uV, i);
			return -EINVAL;
		}

		ret = regulator_is_supported_voltage(drv->vdd_apc,
						     fuse->max_uV,
						     fuse->max_uV);
		if (!ret) {
			dev_err(drv->dev,
				"max uV: %d (fuse corner: %d) not supported by regulator\n",
				fuse->max_uV, i);
			return -EINVAL;
		}

		dev_dbg(drv->dev,
			"fuse corner %d: [%d %d %d] RO%hhu quot %d squot %d\n",
			i, fuse->min_uV, fuse->uV, fuse->max_uV,
			fuse->ring_osc_idx, fuse->quot, fuse->step_quot);
	}

	return 0;
}

static int cpr_calculate_scaling(const char *quot_offset,
				 struct cpr_drv *drv,
				 const struct fuse_corner_data *fdata,
				 const struct corner *corner)
{
	u32 quot_diff = 0;
	unsigned long freq_diff;
	int scaling;
	const struct fuse_corner *fuse, *prev_fuse;
	int ret;

	fuse = corner->fuse_corner;
	prev_fuse = fuse - 1;

	if (quot_offset) {
		ret = nvmem_cell_read_variable_le_u32(drv->dev, quot_offset, &quot_diff);
		if (ret)
			return ret;

		quot_diff *= fdata->quot_offset_scale;
		quot_diff += fdata->quot_offset_adjust;
	} else {
		quot_diff = fuse->quot - prev_fuse->quot;
	}

	freq_diff = fuse->max_freq - prev_fuse->max_freq;
	freq_diff /= 1000000; /* Convert to MHz */
	scaling = 1000 * quot_diff / freq_diff;
	return min(scaling, fdata->max_quot_scale);
}

static int cpr_interpolate(const struct corner *corner, int step_volt,
			   const struct fuse_corner_data *fdata)
{
	unsigned long f_high, f_low, f_diff;
	int uV_high, uV_low, uV;
	u64 temp, temp_limit;
	const struct fuse_corner *fuse, *prev_fuse;

	fuse = corner->fuse_corner;
	prev_fuse = fuse - 1;

	f_high = fuse->max_freq;
	f_low = prev_fuse->max_freq;
	uV_high = fuse->uV;
	uV_low = prev_fuse->uV;
	f_diff = fuse->max_freq - corner->freq;

	/*
	 * Don't interpolate in the wrong direction. This could happen
	 * if the adjusted fuse voltage overlaps with the previous fuse's
	 * adjusted voltage.
	 */
	if (f_high <= f_low || uV_high <= uV_low || f_high <= corner->freq)
		return corner->uV;

	temp = f_diff * (uV_high - uV_low);
	temp = div64_ul(temp, f_high - f_low);

	/*
	 * max_volt_scale has units of uV/MHz while freq values
	 * have units of Hz.  Divide by 1000000 to convert to.
	 */
	temp_limit = f_diff * fdata->max_volt_scale;
	do_div(temp_limit, 1000000);

	uV = uV_high - min(temp, temp_limit);
	return roundup(uV, step_volt);
}

static unsigned int cpr_get_fuse_corner(struct dev_pm_opp *opp)
{
	struct device_node *np;
	unsigned int fuse_corner = 0;

	np = dev_pm_opp_get_of_node(opp);
	if (of_property_read_u32(np, "qcom,opp-fuse-level", &fuse_corner))
		pr_err("%s: missing 'qcom,opp-fuse-level' property\n",
		       __func__);

	of_node_put(np);

	return fuse_corner;
}

static unsigned long cpr_get_opp_hz_for_req(struct dev_pm_opp *ref,
					    struct device *cpu_dev)
{
	struct device_node *ref_np __free(device_node) = NULL;
	struct device_node *desc_np __free(device_node) =
		dev_pm_opp_of_get_opp_desc_node(cpu_dev);

	if (!desc_np)
		return 0;

	ref_np = dev_pm_opp_get_of_node(ref);
	if (!ref_np)
		return 0;

	for_each_available_child_of_node_scoped(desc_np, child_np) {
		struct device_node *child_req_np __free(device_node) =
			of_parse_phandle(child_np, "required-opps", 0);

		if (child_req_np == ref_np) {
			u64 rate = 0;

			of_property_read_u64(child_np, "opp-hz", &rate);
			return (unsigned long) rate;
		}
	}

	return 0;
}

static int cpr_corner_init(struct cpr_drv *drv)
{
	const struct cpr_desc *desc = drv->desc;
	const struct cpr_fuse *fuses = drv->cpr_fuses;
	int i, level, scaling = 0;
	unsigned int fnum, fc;
	const char *quot_offset;
	struct fuse_corner *fuse, *prev_fuse;
	struct corner *corner, *end;
	struct corner_data *cdata;
	const struct fuse_corner_data *fdata;
	bool apply_scaling;
	unsigned long freq_diff, freq_diff_mhz;
	unsigned long freq;
	int step_volt = regulator_get_linear_step(drv->vdd_apc);
	struct dev_pm_opp *opp;

	if (!step_volt)
		return -EINVAL;

	corner = drv->corners;
	end = &corner[drv->num_corners - 1];

	cdata = devm_kcalloc(drv->dev, drv->num_corners,
			     sizeof(struct corner_data),
			     GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	/*
	 * Store maximum frequency for each fuse corner based on the frequency
	 * plan
	 */
	for (level = 1; level <= drv->num_corners; level++) {
		opp = dev_pm_opp_find_level_exact(&drv->pd.dev, level);
		if (IS_ERR(opp))
			return -EINVAL;
		fc = cpr_get_fuse_corner(opp);
		if (!fc) {
			dev_pm_opp_put(opp);
			return -EINVAL;
		}
		fnum = fc - 1;
		freq = cpr_get_opp_hz_for_req(opp, drv->attached_cpu_dev);
		if (!freq) {
			dev_pm_opp_put(opp);
			return -EINVAL;
		}
		cdata[level - 1].fuse_corner = fnum;
		cdata[level - 1].freq = freq;

		fuse = &drv->fuse_corners[fnum];
		dev_dbg(drv->dev, "freq: %lu level: %u fuse level: %u\n",
			freq, dev_pm_opp_get_level(opp) - 1, fnum);
		if (freq > fuse->max_freq)
			fuse->max_freq = freq;
		dev_pm_opp_put(opp);
	}

	/*
	 * Get the quotient adjustment scaling factor, according to:
	 *
	 * scaling = min(1000 * (QUOT(corner_N) - QUOT(corner_N-1))
	 *		/ (freq(corner_N) - freq(corner_N-1)), max_factor)
	 *
	 * QUOT(corner_N):	quotient read from fuse for fuse corner N
	 * QUOT(corner_N-1):	quotient read from fuse for fuse corner (N - 1)
	 * freq(corner_N):	max frequency in MHz supported by fuse corner N
	 * freq(corner_N-1):	max frequency in MHz supported by fuse corner
	 *			 (N - 1)
	 *
	 * Then walk through the corners mapped to each fuse corner
	 * and calculate the quotient adjustment for each one using the
	 * following formula:
	 *
	 * quot_adjust = (freq_max - freq_corner) * scaling / 1000
	 *
	 * freq_max: max frequency in MHz supported by the fuse corner
	 * freq_corner: frequency in MHz corresponding to the corner
	 * scaling: calculated from above equation
	 *
	 *
	 *     +                           +
	 *     |                         v |
	 *   q |           f c           o |           f c
	 *   u |         c               l |         c
	 *   o |       f                 t |       f
	 *   t |     c                   a |     c
	 *     | c f                     g | c f
	 *     |                         e |
	 *     +---------------            +----------------
	 *       0 1 2 3 4 5 6               0 1 2 3 4 5 6
	 *          corner                      corner
	 *
	 *    c = corner
	 *    f = fuse corner
	 *
	 */
	for (apply_scaling = false, i = 0; corner <= end; corner++, i++) {
		fnum = cdata[i].fuse_corner;
		fdata = &desc->cpr_fuses.fuse_corner_data[fnum];
		quot_offset = fuses[fnum].quotient_offset;
		fuse = &drv->fuse_corners[fnum];
		if (fnum)
			prev_fuse = &drv->fuse_corners[fnum - 1];
		else
			prev_fuse = NULL;

		corner->fuse_corner = fuse;
		corner->freq = cdata[i].freq;
		corner->uV = fuse->uV;

		if (prev_fuse && cdata[i - 1].freq == prev_fuse->max_freq) {
			scaling = cpr_calculate_scaling(quot_offset, drv,
							fdata, corner);
			if (scaling < 0)
				return scaling;

			apply_scaling = true;
		} else if (corner->freq == fuse->max_freq) {
			/* This is a fuse corner; don't scale anything */
			apply_scaling = false;
		}

		if (apply_scaling) {
			freq_diff = fuse->max_freq - corner->freq;
			freq_diff_mhz = freq_diff / 1000000;
			corner->quot_adjust = scaling * freq_diff_mhz / 1000;

			corner->uV = cpr_interpolate(corner, step_volt, fdata);
		}

		corner->max_uV = fuse->max_uV;
		corner->min_uV = fuse->min_uV;
		corner->uV = clamp(corner->uV, corner->min_uV, corner->max_uV);
		corner->last_uV = corner->uV;

		/* Reduce the ceiling voltage if needed */
		if (desc->reduce_to_corner_uV && corner->uV < corner->max_uV)
			corner->max_uV = corner->uV;
		else if (desc->reduce_to_fuse_uV && fuse->uV < corner->max_uV)
			corner->max_uV = max(corner->min_uV, fuse->uV);

		dev_dbg(drv->dev, "corner %d: [%d %d %d] quot %d\n", i,
			corner->min_uV, corner->uV, corner->max_uV,
			fuse->quot - corner->quot_adjust);
	}

	return 0;
}

static const struct cpr_fuse *cpr_get_fuses(struct cpr_drv *drv)
{
	const struct cpr_desc *desc = drv->desc;
	struct cpr_fuse *fuses;
	int i;

	fuses = devm_kcalloc(drv->dev, desc->num_fuse_corners,
			     sizeof(struct cpr_fuse),
			     GFP_KERNEL);
	if (!fuses)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < desc->num_fuse_corners; i++) {
		char tbuf[32];

		snprintf(tbuf, 32, "cpr_ring_osc%d", i + 1);
		fuses[i].ring_osc = devm_kstrdup(drv->dev, tbuf, GFP_KERNEL);
		if (!fuses[i].ring_osc)
			return ERR_PTR(-ENOMEM);

		snprintf(tbuf, 32, "cpr_init_voltage%d", i + 1);
		fuses[i].init_voltage = devm_kstrdup(drv->dev, tbuf,
						     GFP_KERNEL);
		if (!fuses[i].init_voltage)
			return ERR_PTR(-ENOMEM);

		snprintf(tbuf, 32, "cpr_quotient%d", i + 1);
		fuses[i].quotient = devm_kstrdup(drv->dev, tbuf, GFP_KERNEL);
		if (!fuses[i].quotient)
			return ERR_PTR(-ENOMEM);

		snprintf(tbuf, 32, "cpr_quotient_offset%d", i + 1);
		fuses[i].quotient_offset = devm_kstrdup(drv->dev, tbuf,
							GFP_KERNEL);
		if (!fuses[i].quotient_offset)
			return ERR_PTR(-ENOMEM);
	}

	return fuses;
}

static void cpr_set_loop_allowed(struct cpr_drv *drv)
{
	drv->loop_disabled = false;
}

static int cpr_init_parameters(struct cpr_drv *drv)
{
	const struct cpr_desc *desc = drv->desc;
	struct clk *clk;

	clk = clk_get(drv->dev, "ref");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	drv->ref_clk_khz = clk_get_rate(clk) / 1000;
	clk_put(clk);

	if (desc->timer_cons_up > RBIF_TIMER_ADJ_CONS_UP_MASK ||
	    desc->timer_cons_down > RBIF_TIMER_ADJ_CONS_DOWN_MASK ||
	    desc->up_threshold > RBCPR_CTL_UP_THRESHOLD_MASK ||
	    desc->down_threshold > RBCPR_CTL_DN_THRESHOLD_MASK ||
	    desc->idle_clocks > RBCPR_STEP_QUOT_IDLE_CLK_MASK ||
	    desc->clamp_timer_interval > RBIF_TIMER_ADJ_CLAMP_INT_MASK)
		return -EINVAL;

	dev_dbg(drv->dev, "up threshold = %u, down threshold = %u\n",
		desc->up_threshold, desc->down_threshold);

	return 0;
}

static int cpr_find_initial_corner(struct cpr_drv *drv)
{
	unsigned long rate;
	const struct corner *end;
	struct corner *iter;
	unsigned int i = 0;

	if (!drv->cpu_clk) {
		dev_err(drv->dev, "cannot get rate from NULL clk\n");
		return -EINVAL;
	}

	end = &drv->corners[drv->num_corners - 1];
	rate = clk_get_rate(drv->cpu_clk);

	/*
	 * Some bootloaders set a CPU clock frequency that is not defined
	 * in the OPP table. When running at an unlisted frequency,
	 * cpufreq_online() will change to the OPP which has the lowest
	 * frequency, at or above the unlisted frequency.
	 * Since cpufreq_online() always "rounds up" in the case of an
	 * unlisted frequency, this function always "rounds down" in case
	 * of an unlisted frequency. That way, when cpufreq_online()
	 * triggers the first ever call to cpr_set_performance_state(),
	 * it will correctly determine the direction as UP.
	 */
	for (iter = drv->corners; iter <= end; iter++) {
		if (iter->freq > rate)
			break;
		i++;
		if (iter->freq == rate) {
			drv->corner = iter;
			break;
		}
		if (iter->freq < rate)
			drv->corner = iter;
	}

	if (!drv->corner) {
		dev_err(drv->dev, "boot up corner not found\n");
		return -EINVAL;
	}

	dev_dbg(drv->dev, "boot up perf state: %u\n", i);

	return 0;
}

static const struct cpr_desc qcs404_cpr_desc = {
	.num_fuse_corners = 3,
	.min_diff_quot = CPR_FUSE_MIN_QUOT_DIFF,
	.step_quot = (int []){ 25, 25, 25, },
	.timer_delay_us = 5000,
	.timer_cons_up = 0,
	.timer_cons_down = 2,
	.up_threshold = 1,
	.down_threshold = 3,
	.idle_clocks = 15,
	.gcnt_us = 1,
	.vdd_apc_step_up_limit = 1,
	.vdd_apc_step_down_limit = 1,
	.cpr_fuses = {
		.init_voltage_step = 8000,
		.init_voltage_width = 6,
		.fuse_corner_data = (struct fuse_corner_data[]){
			/* fuse corner 0 */
			{
				.ref_uV = 1224000,
				.max_uV = 1224000,
				.min_uV = 1048000,
				.max_volt_scale = 0,
				.max_quot_scale = 0,
				.quot_offset = 0,
				.quot_scale = 1,
				.quot_adjust = 0,
				.quot_offset_scale = 5,
				.quot_offset_adjust = 0,
			},
			/* fuse corner 1 */
			{
				.ref_uV = 1288000,
				.max_uV = 1288000,
				.min_uV = 1048000,
				.max_volt_scale = 2000,
				.max_quot_scale = 1400,
				.quot_offset = 0,
				.quot_scale = 1,
				.quot_adjust = -20,
				.quot_offset_scale = 5,
				.quot_offset_adjust = 0,
			},
			/* fuse corner 2 */
			{
				.ref_uV = 1352000,
				.max_uV = 1384000,
				.min_uV = 1088000,
				.max_volt_scale = 2000,
				.max_quot_scale = 1400,
				.quot_offset = 0,
				.quot_scale = 1,
				.quot_adjust = 0,
				.quot_offset_scale = 5,
				.quot_offset_adjust = 0,
			},
		},
	},
};

static const struct acc_desc qcs404_acc_desc = {
	.settings = (struct reg_sequence[]){
		{ 0xb120, 0x1041040 },
		{ 0xb124, 0x41 },
		{ 0xb120, 0x0 },
		{ 0xb124, 0x0 },
		{ 0xb120, 0x0 },
		{ 0xb124, 0x0 },
	},
	.config = (struct reg_sequence[]){
		{ 0xb138, 0xff },
		{ 0xb130, 0x5555 },
	},
	.num_regs_per_fuse = 2,
};

static const struct cpr_acc_desc qcs404_cpr_acc_desc = {
	.cpr_desc = &qcs404_cpr_desc,
	.acc_desc = &qcs404_acc_desc,
};

static int cpr_power_off(struct generic_pm_domain *domain)
{
	struct cpr_drv *drv = container_of(domain, struct cpr_drv, pd);

	return cpr_disable(drv);
}

static int cpr_power_on(struct generic_pm_domain *domain)
{
	struct cpr_drv *drv = container_of(domain, struct cpr_drv, pd);

	return cpr_enable(drv);
}

static int cpr_pd_attach_dev(struct generic_pm_domain *domain,
			     struct device *dev)
{
	struct cpr_drv *drv = container_of(domain, struct cpr_drv, pd);
	const struct acc_desc *acc_desc = drv->acc_desc;
	int ret;

	guard(mutex)(&drv->lock);

	dev_dbg(drv->dev, "attach callback for: %s\n", dev_name(dev));

	/*
	 * This driver only supports scaling voltage for a CPU cluster
	 * where all CPUs in the cluster share a single regulator.
	 * Therefore, save the struct device pointer only for the first
	 * CPU device that gets attached. There is no need to do any
	 * additional initialization when further CPUs get attached.
	 */
	if (drv->attached_cpu_dev)
		return 0;

	/*
	 * cpr_scale_voltage() requires the direction (if we are changing
	 * to a higher or lower OPP). The first time
	 * cpr_set_performance_state() is called, there is no previous
	 * performance state defined. Therefore, we call
	 * cpr_find_initial_corner() that gets the CPU clock frequency
	 * set by the bootloader, so that we can determine the direction
	 * the first time cpr_set_performance_state() is called.
	 */
	drv->cpu_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(drv->cpu_clk))
		return dev_err_probe(drv->dev, PTR_ERR(drv->cpu_clk),
				     "could not get cpu clk\n");

	drv->attached_cpu_dev = dev;

	dev_dbg(drv->dev, "using cpu clk from: %s\n",
		dev_name(drv->attached_cpu_dev));

	/*
	 * Everything related to (virtual) corners has to be initialized
	 * here, when attaching to the power domain, since we need to know
	 * the maximum frequency for each fuse corner, and this is only
	 * available after the cpufreq driver has attached to us.
	 * The reason for this is that we need to know the highest
	 * frequency associated with each fuse corner.
	 */
	ret = dev_pm_opp_get_opp_count(&drv->pd.dev);
	if (ret < 0) {
		dev_err(drv->dev, "could not get OPP count\n");
		return ret;
	}
	drv->num_corners = ret;

	if (drv->num_corners < 2) {
		dev_err(drv->dev, "need at least 2 OPPs to use CPR\n");
		return -EINVAL;
	}

	drv->corners = devm_kcalloc(drv->dev, drv->num_corners,
				    sizeof(*drv->corners),
				    GFP_KERNEL);
	if (!drv->corners)
		return -ENOMEM;

	ret = cpr_corner_init(drv);
	if (ret)
		return ret;

	cpr_set_loop_allowed(drv);

	ret = cpr_init_parameters(drv);
	if (ret)
		return ret;

	/* Configure CPR HW but keep it disabled */
	ret = cpr_config(drv);
	if (ret)
		return ret;

	ret = cpr_find_initial_corner(drv);
	if (ret)
		return ret;

	if (acc_desc->config)
		regmap_multi_reg_write(drv->tcsr, acc_desc->config,
				       acc_desc->num_regs_per_fuse);

	/* Enable ACC if required */
	if (acc_desc->enable_mask)
		regmap_update_bits(drv->tcsr, acc_desc->enable_reg,
				   acc_desc->enable_mask,
				   acc_desc->enable_mask);

	dev_info(drv->dev, "driver initialized with %u OPPs\n",
		 drv->num_corners);

	return 0;
}

static int cpr_debug_info_show(struct seq_file *s, void *unused)
{
	u32 gcnt, ro_sel, ctl, irq_status, reg, error_steps;
	u32 step_dn, step_up, error, error_lt0, busy;
	struct cpr_drv *drv = s->private;
	struct fuse_corner *fuse_corner;
	struct corner *corner;

	corner = drv->corner;
	fuse_corner = corner->fuse_corner;

	seq_printf(s, "corner, current_volt = %d uV\n",
		       corner->last_uV);

	ro_sel = fuse_corner->ring_osc_idx;
	gcnt = cpr_read(drv, REG_RBCPR_GCNT_TARGET(ro_sel));
	seq_printf(s, "rbcpr_gcnt_target (%u) = %#02X\n", ro_sel, gcnt);

	ctl = cpr_read(drv, REG_RBCPR_CTL);
	seq_printf(s, "rbcpr_ctl = %#02X\n", ctl);

	irq_status = cpr_read(drv, REG_RBIF_IRQ_STATUS);
	seq_printf(s, "rbcpr_irq_status = %#02X\n", irq_status);

	reg = cpr_read(drv, REG_RBCPR_RESULT_0);
	seq_printf(s, "rbcpr_result_0 = %#02X\n", reg);

	step_dn = reg & 0x01;
	step_up = (reg >> RBCPR_RESULT0_STEP_UP_SHIFT) & 0x01;
	seq_printf(s, "  [step_dn = %u", step_dn);

	seq_printf(s, ", step_up = %u", step_up);

	error_steps = (reg >> RBCPR_RESULT0_ERROR_STEPS_SHIFT)
				& RBCPR_RESULT0_ERROR_STEPS_MASK;
	seq_printf(s, ", error_steps = %u", error_steps);

	error = (reg >> RBCPR_RESULT0_ERROR_SHIFT) & RBCPR_RESULT0_ERROR_MASK;
	seq_printf(s, ", error = %u", error);

	error_lt0 = (reg >> RBCPR_RESULT0_ERROR_LT0_SHIFT) & 0x01;
	seq_printf(s, ", error_lt_0 = %u", error_lt0);

	busy = (reg >> RBCPR_RESULT0_BUSY_SHIFT) & 0x01;
	seq_printf(s, ", busy = %u]\n", busy);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cpr_debug_info);

static void cpr_debugfs_init(struct cpr_drv *drv)
{
	drv->debugfs = debugfs_create_dir("qcom_cpr", NULL);

	debugfs_create_file("debug_info", 0444, drv->debugfs,
			    drv, &cpr_debug_info_fops);
}

static int cpr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpr_drv *drv;
	int irq, ret;
	const struct cpr_acc_desc *data;
	struct device_node *np;
	u32 cpr_rev = FUSE_REVISION_UNKNOWN;

	data = of_device_get_match_data(dev);
	if (!data || !data->cpr_desc || !data->acc_desc)
		return -EINVAL;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;
	drv->dev = dev;
	drv->desc = data->cpr_desc;
	drv->acc_desc = data->acc_desc;

	drv->fuse_corners = devm_kcalloc(dev, drv->desc->num_fuse_corners,
					 sizeof(*drv->fuse_corners),
					 GFP_KERNEL);
	if (!drv->fuse_corners)
		return -ENOMEM;

	np = of_parse_phandle(dev->of_node, "acc-syscon", 0);
	if (!np)
		return -ENODEV;

	drv->tcsr = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(drv->tcsr))
		return PTR_ERR(drv->tcsr);

	drv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv->base))
		return PTR_ERR(drv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	drv->vdd_apc = devm_regulator_get(dev, "vdd-apc");
	if (IS_ERR(drv->vdd_apc))
		return PTR_ERR(drv->vdd_apc);

	/*
	 * Initialize fuse corners, since it simply depends
	 * on data in efuses.
	 * Everything related to (virtual) corners has to be
	 * initialized after attaching to the power domain,
	 * since it depends on the CPU's OPP table.
	 */
	ret = nvmem_cell_read_variable_le_u32(dev, "cpr_fuse_revision", &cpr_rev);
	if (ret)
		return ret;

	drv->cpr_fuses = cpr_get_fuses(drv);
	if (IS_ERR(drv->cpr_fuses))
		return PTR_ERR(drv->cpr_fuses);

	ret = cpr_populate_ring_osc_idx(drv);
	if (ret)
		return ret;

	ret = cpr_fuse_corner_init(drv);
	if (ret)
		return ret;

	mutex_init(&drv->lock);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					cpr_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"cpr", drv);
	if (ret)
		return ret;

	drv->pd.name = devm_kstrdup_const(dev, dev->of_node->full_name,
					  GFP_KERNEL);
	if (!drv->pd.name)
		return -EINVAL;

	drv->pd.power_off = cpr_power_off;
	drv->pd.power_on = cpr_power_on;
	drv->pd.set_performance_state = cpr_set_performance_state;
	drv->pd.attach_dev = cpr_pd_attach_dev;

	ret = pm_genpd_init(&drv->pd, NULL, true);
	if (ret)
		return ret;

	ret = of_genpd_add_provider_simple(dev->of_node, &drv->pd);
	if (ret)
		goto err_remove_genpd;

	platform_set_drvdata(pdev, drv);
	cpr_debugfs_init(drv);

	return 0;

err_remove_genpd:
	pm_genpd_remove(&drv->pd);
	return ret;
}

static void cpr_remove(struct platform_device *pdev)
{
	struct cpr_drv *drv = platform_get_drvdata(pdev);

	if (cpr_is_allowed(drv)) {
		cpr_ctl_disable(drv);
		cpr_irq_set(drv, 0);
	}

	of_genpd_del_provider(pdev->dev.of_node);
	pm_genpd_remove(&drv->pd);

	debugfs_remove_recursive(drv->debugfs);
}

static const struct of_device_id cpr_match_table[] = {
	{ .compatible = "qcom,qcs404-cpr", .data = &qcs404_cpr_acc_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, cpr_match_table);

static struct platform_driver cpr_driver = {
	.probe		= cpr_probe,
	.remove		= cpr_remove,
	.driver		= {
		.name	= "qcom-cpr",
		.of_match_table = cpr_match_table,
	},
};
module_platform_driver(cpr_driver);

MODULE_DESCRIPTION("Core Power Reduction (CPR) driver");
MODULE_LICENSE("GPL v2");
