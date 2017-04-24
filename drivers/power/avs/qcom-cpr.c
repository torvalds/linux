/*
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/nvmem-consumer.h>
#include <linux/bitops.h>
#include <linux/regulator/qcom_smd-regulator.h>

/* Register Offsets for RB-CPR and Bit Definitions */

/* RBCPR Version Register */
#define REG_RBCPR_VERSION		0
#define RBCPR_VER_2			0x02

/* RBCPR Gate Count and Target Registers */
#define REG_RBCPR_GCNT_TARGET(n)	(0x60 + 4 * n)

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
#define REG_RBIF_IRQ_EN(n)		(0x100 + 4 * n)
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

/* RBCPR Clock Control Register */
#define RBCPR_CLK_SEL_MASK	BIT(-1)
#define RBCPR_CLK_SEL_19P2_MHZ	0
#define RBCPR_CLK_SEL_AHB_CLK	BIT(0)

/* CPR eFuse parameters */
#define CPR_FUSE_TARGET_QUOT_BITS_MASK	GENMASK(11, 0)

#define CPR_FUSE_MIN_QUOT_DIFF		50

#define SPEED_BIN_NONE			UINT_MAX

#define FUSE_REVISION_UNKNOWN		(-1)
#define FUSE_MAP_NO_MATCH		(-1)
#define FUSE_PARAM_MATCH_ANY		0xffffffff

enum vdd_mx_vmin_method {
	VDD_MX_VMIN_APC_CORNER_CEILING,
	VDD_MX_VMIN_FUSE_CORNER_MAP,
};

enum voltage_change_dir {
	NO_CHANGE,
	DOWN,
	UP,
};

struct qfprom_offset {
	u16 offset;
	u8 width;
	u8 shift;
};

struct cpr_fuse {
	struct qfprom_offset ring_osc;
	struct qfprom_offset init_voltage;
	struct qfprom_offset quotient;
	struct qfprom_offset quotient_offset;
};

struct fuse_corner_data {
	int ref_uV;
	int max_uV;
	int min_uV;
	int max_quot_scale;
	int quot_offset;
	int quot_scale;
	int max_volt_scale;
	int vdd_mx_req;
};

struct cpr_fuses {
	struct qfprom_offset redundant;
	u8 redundant_value;
	int init_voltage_step;
	struct fuse_corner_data *fuse_corner_data;
	struct cpr_fuse *cpr_fuse;
	struct qfprom_offset *disable;
};

struct pvs_bin {
	int *uV;
};

struct pvs_fuses {
	struct qfprom_offset redundant;
	u8 redundant_value;
	struct qfprom_offset *pvs_fuse;
	struct pvs_bin *pvs_bins;
};

struct corner_data {
	unsigned int fuse_corner;
	unsigned long freq;
};

struct freq_plan {
	u32 speed_bin;
	u32 pvs_version;
	const struct corner_data **plan;
};

struct fuse_conditional_min_volt {
	struct qfprom_offset redundant;
	u8 expected;
	int min_uV;
};

struct fuse_uplift_wa {
	struct qfprom_offset redundant;
	u8 expected;
	int uV;
	int *quot;
	int max_uV;
	int speed_bin;
};

struct corner_override {
	u32 speed_bin;
	u32 pvs_version;
	int *max_uV;
	int *min_uV;
};

struct corner_adjustment {
	u32 speed_bin;
	u32 pvs_version;
	u32 cpr_rev;
	u8 *ring_osc_idx;
	int *fuse_quot;
	int *fuse_quot_diff;
	int *fuse_quot_min;
	int *fuse_quot_offset;
	int *fuse_init_uV;
	int *quot;
	int *init_uV;
	bool disable_closed_loop;
};

struct cpr_desc {
	unsigned int num_fuse_corners;
	unsigned int num_corners;
	enum vdd_mx_vmin_method	vdd_mx_vmin_method;
	int min_diff_quot;
	int *step_quot;
	struct cpr_fuses cpr_fuses;
	struct qfprom_offset fuse_revision;
	struct qfprom_offset speed_bin;
	struct qfprom_offset pvs_version;
	struct corner_data *corner_data;
	struct freq_plan *freq_plans;
	size_t num_freq_plans;
	struct pvs_fuses *pvs_fuses;
	struct fuse_conditional_min_volt *min_volt_fuse;
	struct fuse_uplift_wa *uplift_wa;
	struct corner_override *corner_overrides;
	size_t num_corner_overrides;
	struct corner_adjustment *adjustments;
	size_t num_adjustments;
	bool reduce_to_fuse_uV;
	bool reduce_to_corner_uV;
};

struct acc_desc {
	unsigned int	enable_reg;
	u32		enable_mask;

	struct reg_sequence	*settings;
	struct reg_sequence	*override_settings;
	int			num_regs_per_fuse;

	struct qfprom_offset	override;
	u8			override_value;
};

struct fuse_corner {
	int min_uV;
	int max_uV;
	int uV;
	int quot;
	int step_quot;
	const struct reg_sequence *accs;
	int num_accs;
	int vdd_mx_req;
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
	unsigned int		num_fuse_corners;
	unsigned int		num_corners;

	unsigned int		nb_count;
	struct notifier_block	cpufreq_nb;
	bool			switching_opp;
	struct notifier_block	reg_nb;

	unsigned int		ref_clk_khz;
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
	enum vdd_mx_vmin_method	vdd_mx_vmin_method;

	struct device		*dev;
	struct mutex		lock;
	void __iomem		*base;
	struct corner		*corner;
	struct regulator	*vdd_apc;
	struct regulator	*vdd_mx;
	struct clk		*cpu_clk;
	struct device		*cpu_dev;
	struct regmap		*tcsr;
	bool			loop_disabled;
	bool			suspended;
	u32			gcnt;
	unsigned long		flags;
#define FLAGS_IGNORE_1ST_IRQ_STATUS	BIT(0)

	struct fuse_corner	*fuse_corners;
	struct corner		*corners;
};

static bool cpr_is_allowed(struct cpr_drv *drv)
{
	if (drv->loop_disabled) /* || disabled in software */
		return false;
	else
		return true;
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

	if (drv->suspended)
		return;

	/* Program Consecutive Up & Down */
	val = drv->timer_cons_down << RBIF_TIMER_ADJ_CONS_DOWN_SHIFT;
	val |= drv->timer_cons_up << RBIF_TIMER_ADJ_CONS_UP_SHIFT;
	mask = RBIF_TIMER_ADJ_CONS_UP_MASK | RBIF_TIMER_ADJ_CONS_DOWN_MASK;
	cpr_masked_write(drv, REG_RBIF_TIMER_ADJUST, mask, val);
	cpr_masked_write(drv, REG_RBCPR_CTL,
			RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN |
			RBCPR_CTL_SW_AUTO_CONT_ACK_EN,
			corner->save_ctl);
	cpr_irq_set(drv, corner->save_irq);

	if (cpr_is_allowed(drv) /*&& drv->vreg_enabled */ &&
	    corner->max_uV > corner->min_uV)
		val = RBCPR_CTL_LOOP_EN;
	else
		val = 0;
	cpr_ctl_modify(drv, RBCPR_CTL_LOOP_EN, val);
}

static void cpr_ctl_disable(struct cpr_drv *drv)
{
	if (drv->suspended)
		return;

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
	int i;

	ro_sel = fuse->ring_osc_idx;
	gcnt = drv->gcnt;
	gcnt |= fuse->quot - corner->quot_adjust;

	/* Program the step quotient and idle clocks */
	step_quot = drv->idle_clocks << RBCPR_STEP_QUOT_IDLE_CLK_SHIFT;
	step_quot |= fuse->step_quot;
	cpr_write(drv, REG_RBCPR_STEP_QUOT, step_quot);

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(drv, REG_RBCPR_GCNT_TARGET(i), 0);

	cpr_write(drv, REG_RBCPR_GCNT_TARGET(ro_sel), gcnt);
	ctl = corner->save_ctl;
	cpr_write(drv, REG_RBCPR_CTL, ctl);
	irq = corner->save_irq;
	cpr_irq_set(drv, irq);
	dev_dbg(drv->dev, "gcnt = 0x%08x, ctl = 0x%08x, irq = 0x%08x\n", gcnt,
		ctl, irq);
}

static int
cpr_mx_get(struct cpr_drv *drv, struct fuse_corner *fuse, int apc_volt)
{
	switch (drv->vdd_mx_vmin_method) {
	case VDD_MX_VMIN_APC_CORNER_CEILING:
		return fuse->max_uV;
	case VDD_MX_VMIN_FUSE_CORNER_MAP:
		return fuse->vdd_mx_req;
	}

	dev_warn(drv->dev, "Failed to get mx\n");
	return 0;
}

static void cpr_set_acc(struct regmap *tcsr, struct fuse_corner *f,
			struct fuse_corner *end)
{
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
			   enum voltage_change_dir dir, int vdd_mx_vmin)
{
	int ret = 0;
	struct fuse_corner *prev_fuse_corner = drv->corner->fuse_corner;

	if (drv->tcsr && dir == DOWN)
		cpr_set_acc(drv->tcsr, prev_fuse_corner, fuse_corner);

	if (vdd_mx_vmin && dir == UP)
		ret = qcom_rpm_set_corner(drv->vdd_mx, vdd_mx_vmin);

	return ret;
}

static int cpr_post_voltage(struct cpr_drv *drv,
			    struct fuse_corner *fuse_corner,
			    enum voltage_change_dir dir, int vdd_mx_vmin)
{
	int ret = 0;
	struct fuse_corner *prev_fuse_corner = drv->corner->fuse_corner;

	if (drv->tcsr && dir == UP)
		cpr_set_acc(drv->tcsr, prev_fuse_corner, fuse_corner);

	if (vdd_mx_vmin && dir == DOWN)
		ret = qcom_rpm_set_corner(drv->vdd_mx, vdd_mx_vmin);

	return ret;
}

static int cpr_regulator_notifier(struct notifier_block *nb,
				   unsigned long event, void *d)
{
	struct cpr_drv *drv = container_of(nb, struct cpr_drv, reg_nb);
	u32 val, mask;
	int last_uV, new_uV;

	switch (event) {
	case REGULATOR_EVENT_VOLTAGE_CHANGE:
		new_uV = (int)(uintptr_t)d;
		break;
	default:
		return NOTIFY_OK;
	}

	mutex_lock(&drv->lock);

	last_uV = drv->corner->last_uV;

	if (drv->switching_opp) {
		goto unlock;
	} else if (last_uV < new_uV) {
		/* Disable auto nack down */
		mask = RBCPR_CTL_SW_AUTO_CONT_NACK_DN_EN;
		val = 0;
	} else if (last_uV > new_uV) {
		/* Restore default threshold for UP */
		mask = RBCPR_CTL_UP_THRESHOLD_MASK;
		mask <<= RBCPR_CTL_UP_THRESHOLD_SHIFT;
		val = drv->up_threshold;
		val <<= RBCPR_CTL_UP_THRESHOLD_SHIFT;
	} else { /* Somehow it's the same? */
		goto unlock;
	}

	cpr_ctl_modify(drv, mask, val);

	/* Re-enable default interrupts */
	cpr_irq_set(drv, CPR_INT_DEFAULT);

	/* Ack */
	cpr_irq_clr_ack(drv);

	/* Save register values for the corner */
	cpr_corner_save(drv, drv->corner);
	drv->corner->last_uV = new_uV;
unlock:
	mutex_unlock(&drv->lock);

	return NOTIFY_OK;
}

static int cpr_scale(struct cpr_drv *drv, enum voltage_change_dir dir)
{
	u32 val, error_steps, reg_mask;
	int last_uV, new_uV, step_uV;
	struct corner *corner;

	//step_uV = regulator_get_linear_step(drv->vdd_apc);
	step_uV = 12500; /*TODO: Get step volt here */
	corner = drv->corner;

	val = cpr_read(drv, REG_RBCPR_RESULT_0);

	error_steps = val >> RBCPR_RESULT0_ERROR_STEPS_SHIFT;
	error_steps &= RBCPR_RESULT0_ERROR_STEPS_MASK;
	last_uV = corner->last_uV;

	if (dir == UP) {
		if (drv->clamp_timer_interval &&
		    error_steps < drv->up_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(drv->up_threshold,
					  drv->vdd_apc_step_up_limit);
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

		if (error_steps > drv->vdd_apc_step_up_limit)
			error_steps = drv->vdd_apc_step_up_limit;

		/* Calculate new voltage */
		new_uV = last_uV + error_steps * step_uV;
		new_uV = min(new_uV, corner->max_uV);
	} else if (dir == DOWN) {
		if (drv->clamp_timer_interval
				&& error_steps < drv->down_threshold) {
			/*
			 * Handle the case where another measurement started
			 * after the interrupt was triggered due to a core
			 * exiting from power collapse.
			 */
			error_steps = max(drv->down_threshold,
					  drv->vdd_apc_step_down_limit);
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

		if (error_steps > drv->vdd_apc_step_down_limit)
			error_steps = drv->vdd_apc_step_down_limit;

		/* Calculate new voltage */
		new_uV = last_uV - error_steps * step_uV;
		new_uV = max(new_uV, corner->min_uV);
	}

	return new_uV;
}

static irqreturn_t cpr_irq_handler(int irq, void *dev)
{
	struct cpr_drv *drv = dev;
	u32 val;
	int new_uV = 0;
	struct corner *corner;

	mutex_lock(&drv->lock);

	val = cpr_read(drv, REG_RBIF_IRQ_STATUS);
	if (drv->flags & FLAGS_IGNORE_1ST_IRQ_STATUS)
		val = cpr_read(drv, REG_RBIF_IRQ_STATUS);

	dev_dbg(drv->dev, "IRQ_STATUS = %#02x\n", val);

	if (!cpr_ctl_is_enabled(drv)) {
		dev_dbg(drv->dev, "CPR is disabled\n");
		goto unlock;
	} else if (cpr_ctl_is_busy(drv) && !drv->clamp_timer_interval) {
		dev_dbg(drv->dev, "CPR measurement is not ready\n");
		goto unlock;
	} else if (!cpr_is_allowed(drv)) {
		val = cpr_read(drv, REG_RBCPR_CTL);
		dev_err_ratelimited(drv->dev,
				    "Interrupt broken? RBCPR_CTL = %#02x\n",
				    val);
		goto unlock;
	}

	/* Following sequence of handling is as per each IRQ's priority */
	if (val & CPR_INT_UP) {
		new_uV = cpr_scale(drv, UP);
	} else if (val & CPR_INT_DOWN) {
		new_uV = cpr_scale(drv, DOWN);
	} else if (val & CPR_INT_MIN) {
		cpr_irq_clr_nack(drv);
	} else if (val & CPR_INT_MAX) {
		cpr_irq_clr_nack(drv);
	} else if (val & CPR_INT_MID) {
		/* RBCPR_CTL_SW_AUTO_CONT_ACK_EN is enabled */
		dev_dbg(drv->dev, "IRQ occurred for Mid Flag\n");
	} else {
		dev_dbg(drv->dev, "IRQ occurred for unknown flag (%#08x)\n",
			val);
	}

	/* Save register values for the corner */
	corner = drv->corner;
	cpr_corner_save(drv, corner);
unlock:
	mutex_unlock(&drv->lock);

	if (new_uV)
		dev_pm_opp_adjust_voltage(drv->cpu_dev, corner->freq, new_uV);

	return IRQ_HANDLED;
}

/*
 * TODO: Register for hotplug notifier and turn on/off CPR when CPUs are offline
 */
static int cpr_enable(struct cpr_drv *drv)
{
	int ret;

	/* Enable dependency power before vdd_apc */
	if (drv->vdd_mx) {
		ret = regulator_enable(drv->vdd_mx);
		if (ret)
			return ret;
	}

	ret = regulator_enable(drv->vdd_apc);
	if (ret)
		return ret;

	mutex_lock(&drv->lock);
	//drv->vreg_enabled = true;
	if (cpr_is_allowed(drv) && drv->corner) {
		cpr_irq_clr(drv);
		cpr_corner_restore(drv, drv->corner);
		cpr_ctl_enable(drv, drv->corner);
	}
	mutex_unlock(&drv->lock);

	return 0;
}
/*
static int cpr_disable(struct cpr_drv *drv)
{
	int ret;

	ret = regulator_disable(drv->vdd_apc);
	if (ret)
		return ret;

	if (drv->vdd_mx)
		ret = regulator_disable(drv->vdd_mx);
	if (ret)
		return ret;

	mutex_lock(&drv->lock);
	//drv->vreg_enabled = false;
	if (cpr_is_allowed(drv))
		cpr_ctl_disable(drv);
	mutex_unlock(&drv->lock);

	return 0;
}
*/


#ifdef CONFIG_PM_SLEEP
static int cpr_suspend(struct device *dev)
{
	struct cpr_drv *drv = platform_get_drvdata(to_platform_device(dev));

	if (cpr_is_allowed(drv)) {
		mutex_lock(&drv->lock);
		cpr_ctl_disable(drv);
		cpr_irq_clr(drv);
		drv->suspended = true;
		mutex_unlock(&drv->lock);
	}

	return 0;
}

static int cpr_resume(struct device *dev)
{
	struct cpr_drv *drv = platform_get_drvdata(to_platform_device(dev));

	if (cpr_is_allowed(drv)) {
		mutex_lock(&drv->lock);
		drv->suspended = false;
		cpr_irq_clr(drv);
		cpr_ctl_enable(drv, drv->corner);
		mutex_unlock(&drv->lock);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cpr_pm_ops, cpr_suspend, cpr_resume);

static int cpr_config(struct cpr_drv *drv)
{
	int i;
	u32 val, gcnt;
	struct corner *corner;

	/* Disable interrupt and CPR */
	cpr_write(drv, REG_RBIF_IRQ_EN(0), 0);
	cpr_write(drv, REG_RBCPR_CTL, 0);

	/* Program the default HW Ceiling, Floor and vlevel */
	val = RBIF_LIMIT_CEILING_DEFAULT << RBIF_LIMIT_CEILING_SHIFT;
	val |= RBIF_LIMIT_FLOOR_DEFAULT;
	cpr_write(drv, REG_RBIF_LIMIT, val);
	cpr_write(drv, REG_RBIF_SW_VLEVEL, RBIF_SW_VLEVEL_DEFAULT);

	/* Clear the target quotient value and gate count of all ROs */
	for (i = 0; i < CPR_NUM_RING_OSC; i++)
		cpr_write(drv, REG_RBCPR_GCNT_TARGET(i), 0);

	/* Init and save gcnt */
	gcnt = (drv->ref_clk_khz * drv->gcnt_us) / 1000;
	gcnt = gcnt & RBCPR_GCNT_TARGET_GCNT_MASK;
	gcnt <<= RBCPR_GCNT_TARGET_GCNT_SHIFT;
	drv->gcnt = gcnt;

	/* Program the delay count for the timer */
	val = (drv->ref_clk_khz * drv->timer_delay_us) / 1000;
	cpr_write(drv, REG_RBCPR_TIMER_INTERVAL, val);
	dev_dbg(drv->dev, "Timer count: 0x%0x (for %d us)\n", val,
		 drv->timer_delay_us);

	/* Program Consecutive Up & Down */
	val = drv->timer_cons_down << RBIF_TIMER_ADJ_CONS_DOWN_SHIFT;
	val |= drv->timer_cons_up << RBIF_TIMER_ADJ_CONS_UP_SHIFT;
	val |= drv->clamp_timer_interval << RBIF_TIMER_ADJ_CLAMP_INT_SHIFT;
	cpr_write(drv, REG_RBIF_TIMER_ADJUST, val);

	/* Program the control register */
	val = drv->up_threshold << RBCPR_CTL_UP_THRESHOLD_SHIFT;
	val |= drv->down_threshold << RBCPR_CTL_DN_THRESHOLD_SHIFT;
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

/* Called twice for each CPU in policy, one pre and one post event */
static int
cpr_cpufreq_notifier(struct notifier_block *nb, unsigned long event, void *f)
{
	struct cpr_drv *drv = container_of(nb, struct cpr_drv, cpufreq_nb);
	struct cpufreq_freqs *freqs = f;
	unsigned long old = freqs->old * 1000;
	unsigned long new = freqs->new * 1000;
	struct corner *corner, *end;
	enum voltage_change_dir dir;
	int ret = 0, new_uV;
	int vdd_mx_vmin = 0;
	struct fuse_corner *fuse_corner;

	/* Determine direction */
	if (old > new)
		dir = DOWN;
	else if (old < new)
		dir = UP;
	else
		dir = NO_CHANGE;

	/* Determine new corner we're going to */
	corner = drv->corners;
	end = &corner[drv->num_corners - 1];
	for (; corner <= end; corner++)
		if (corner->freq == new)
			break;

	if (corner > end)
		return -EINVAL;

	fuse_corner = corner->fuse_corner;

	if (cpr_is_allowed(drv)) {
		new_uV = corner->last_uV;
	} else {
		new_uV = corner->uV;
	}

	if (dir != NO_CHANGE && drv->vdd_mx)
		vdd_mx_vmin = cpr_mx_get(drv, fuse_corner, new_uV);

	mutex_lock(&drv->lock);
	if (event == CPUFREQ_PRECHANGE) {
		if (drv->nb_count++)
			goto unlock;

		if (cpr_is_allowed(drv))
			cpr_ctl_disable(drv);

		ret = cpr_pre_voltage(drv, fuse_corner, dir, vdd_mx_vmin);
		if (ret)
			goto unlock;

		drv->switching_opp = true;
	}

	if (event == CPUFREQ_POSTCHANGE) {
		if (--drv->nb_count)
			goto unlock;

		ret = cpr_post_voltage(drv, fuse_corner, dir, vdd_mx_vmin);
		if (ret)
			goto unlock;

		if (cpr_is_allowed(drv) /* && drv->vreg_enabled */) {
			cpr_irq_clr(drv);
			if (drv->corner != corner)
				cpr_corner_restore(drv, corner);
			cpr_ctl_enable(drv, corner);
		}

		drv->corner = corner;
		drv->switching_opp = false;
	}
unlock:
	mutex_unlock(&drv->lock);

	return ret;
}

static u32 cpr_read_efuse(void __iomem *prom, const struct qfprom_offset *efuse)
{
	u64 buffer = 0;
	u8 val;
	int i, num_bytes;

	num_bytes = DIV_ROUND_UP(efuse->width + efuse->shift, BITS_PER_BYTE);

	for (i = 0; i < num_bytes; i++) {
		val = readb_relaxed(prom + efuse->offset + i);
		buffer |= val << (i * BITS_PER_BYTE);
	}

	buffer >>= efuse->shift;
	buffer &= BIT(efuse->width) - 1;

	return buffer;
}

static void
cpr_populate_ring_osc_idx(const struct cpr_fuse *fuses, struct cpr_drv *drv,
			  void __iomem *prom)
{
	struct fuse_corner *fuse = drv->fuse_corners;
	struct fuse_corner *end = fuse + drv->num_fuse_corners;

	for (; fuse < end; fuse++, fuses++)
		fuse->ring_osc_idx = cpr_read_efuse(prom, &fuses->ring_osc);
}


static const struct corner_adjustment *cpr_find_adjustment(u32 speed_bin,
		u32 pvs_version, u32 cpr_rev, const struct cpr_desc *desc,
		const struct cpr_drv *drv)
{
	int i, j;
	u32 val, ro;
	struct corner_adjustment *a;

	for (i = 0; i < desc->num_adjustments; i++) {
		a = &desc->adjustments[i];

		if (a->speed_bin != speed_bin &&
		    a->speed_bin != FUSE_PARAM_MATCH_ANY)
			continue;
		if (a->pvs_version != pvs_version &&
		    a->pvs_version != FUSE_PARAM_MATCH_ANY)
			continue;
		if (a->cpr_rev != cpr_rev &&
		    a->cpr_rev != FUSE_PARAM_MATCH_ANY)
			continue;
		for (j = 0; j < drv->num_fuse_corners; j++) {
			val = a->ring_osc_idx[j];
			ro = drv->fuse_corners[j].ring_osc_idx;
			if (val != ro && val != FUSE_PARAM_MATCH_ANY)
				break;
		}
		if (j == drv->num_fuse_corners)
			return a;
	}

	return NULL;
}

static const int *cpr_get_pvs_uV(const struct cpr_desc *desc,
				 struct nvmem_device *qfprom)
{
	const struct qfprom_offset *pvs_efuse;
	const struct qfprom_offset *redun;
	unsigned int idx = 0;
	u8 expected;
	u32 bin;

	redun = &desc->pvs_fuses->redundant;
	expected = desc->pvs_fuses->redundant_value;
	if (redun->width)
		idx = !!(cpr_read_efuse(qfprom, redun) == expected);

	pvs_efuse = &desc->pvs_fuses->pvs_fuse[idx];
	bin = cpr_read_efuse(qfprom, pvs_efuse);

	return desc->pvs_fuses->pvs_bins[bin].uV;
}

static int cpr_read_fuse_uV(const struct cpr_desc *desc,
			    const struct fuse_corner_data *fdata,
			    const struct qfprom_offset *init_v_efuse,
			    struct nvmem_device *qfprom, int step_volt)
{
	int step_size_uV, steps, uV;
	u32 bits;

	bits = cpr_read_efuse(qfprom, init_v_efuse);
	steps = bits & ~BIT(init_v_efuse->width - 1);
	/* Not two's complement.. instead highest bit is sign bit */
	if (bits & BIT(init_v_efuse->width - 1))
		steps = -steps;

	step_size_uV = desc->cpr_fuses.init_voltage_step;

	uV = fdata->ref_uV + steps * step_size_uV;
	return DIV_ROUND_UP(uV, step_volt) * step_volt;
}

static void cpr_fuse_corner_init(struct cpr_drv *drv,
			const struct cpr_desc *desc,
			void __iomem *qfprom,
			const struct cpr_fuse *fuses, u32 speed,
			const struct corner_adjustment *adjustments,
			const struct acc_desc *acc_desc)
{
	int i;
	unsigned int step_volt;
	const struct fuse_corner_data *fdata;
	struct fuse_corner *fuse, *end, *prev;
	const struct qfprom_offset *redun;
	const struct fuse_conditional_min_volt *min_v;
	const struct fuse_uplift_wa *up;
	bool do_min_v = false, do_uplift = false;
	const int *pvs_uV = NULL;
	const int *adj_min;
	int uV, diff;
	u32 min_uV;
	u8 expected;
	const struct reg_sequence *accs;

	redun = &acc_desc->override;
	expected = acc_desc->override_value;
	if (redun->width && cpr_read_efuse(qfprom, redun) == expected)
		accs = acc_desc->override_settings;
	else
		accs = acc_desc->settings;

	/* Figure out if we should apply workarounds */
	min_v = desc->min_volt_fuse;
	do_min_v = min_v &&
		   cpr_read_efuse(qfprom, &min_v->redundant) == min_v->expected;
	if (do_min_v)
		min_uV = min_v->min_uV;

	up = desc->uplift_wa;
	if (!do_min_v && up)
		if (cpr_read_efuse(qfprom, &up->redundant) == up->expected)
			do_uplift = up->speed_bin == speed;

	/*
	 * The initial voltage for each fuse corner may be determined by one of
	 * two ways. Either initial voltages are encoded for each fuse corner
	 * in a dedicated fuse per fuse corner (fuses::init_voltage), or we
	 * use the PVS bin fuse to use a table of initial voltages (pvs_uV).
	 */
	if (fuses->init_voltage.width)
		//step_volt = regulator_get_linear_step(drv->vdd_apc);
		step_volt = 12500; /* TODO: Replace with ^ when apc_reg ready */
	else
		pvs_uV = cpr_get_pvs_uV(desc, qfprom);

	/* Populate fuse_corner members */
	adj_min = adjustments->fuse_quot_min;
	fuse = drv->fuse_corners;
	end = &fuse[drv->num_fuse_corners - 1];
	fdata = desc->cpr_fuses.fuse_corner_data;

	for (i = 0, prev = NULL; fuse <= end; fuse++, fuses++, i++, fdata++) {

		/* Populate uV */
		if (pvs_uV)
			uV = pvs_uV[i];
		else
			uV = cpr_read_fuse_uV(desc, fdata, &fuses->init_voltage,
					      qfprom, step_volt);

		if (adjustments->fuse_init_uV)
			uV += adjustments->fuse_init_uV[i];

		fuse->min_uV = fdata->min_uV;
		fuse->max_uV = fdata->max_uV;

		if (do_min_v) {
			if (fuse->max_uV < min_uV) {
				fuse->max_uV = min_uV;
				fuse->min_uV = min_uV;
			} else if (fuse->min_uV < min_uV) {
				fuse->min_uV = min_uV;
			}
		}

		fuse->uV = clamp(uV, fuse->min_uV, fuse->max_uV);

		if (fuse == end) {
			if (do_uplift) {
				end->uV += up->uV;
				end->uV = clamp(end->uV, 0, up->max_uV);
			}
			/*
			 * Allow the highest fuse corner's PVS voltage to
			 * define the ceiling voltage for that corner in order
			 * to support SoC's in which variable ceiling values
			 * are required.
			 */
			end->max_uV = max(end->max_uV, end->uV);
		}

		/* Populate target quotient by scaling */
		fuse->quot = cpr_read_efuse(qfprom, &fuses->quotient);
		fuse->quot *= fdata->quot_scale;
		fuse->quot += fdata->quot_offset;

		if (adjustments->fuse_quot) {
			fuse->quot += adjustments->fuse_quot[i];

			if (prev && adjustments->fuse_quot_diff) {
				diff = adjustments->fuse_quot_diff[i];
				if (fuse->quot - prev->quot <= diff)
					fuse->quot = prev->quot + adj_min[i];
			}
			prev = fuse;
		}

		if (do_uplift)
			fuse->quot += up->quot[i];

		fuse->step_quot = desc->step_quot[fuse->ring_osc_idx];

		/* Populate acc settings */
		fuse->accs = accs;
		fuse->num_accs = acc_desc->num_regs_per_fuse;
		accs += acc_desc->num_regs_per_fuse;

		/* Populate MX request */
		fuse->vdd_mx_req = fdata->vdd_mx_req;
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

		dev_dbg(drv->dev,
			 "fuse corner %d: [%d %d %d] RO%d quot %d squot %d\n",
			 i, fuse->min_uV, fuse->uV, fuse->max_uV,
			 fuse->ring_osc_idx, fuse->quot, fuse->step_quot);
	}
}

static struct device *cpr_get_cpu_device(struct device_node *of_node, int index)
{
	struct device_node *np;
	int cpu;

	np = of_parse_phandle(of_node, "qcom,cpr-cpus", index);
	if (!np)
		return NULL;

	for_each_possible_cpu(cpu)
		if (arch_find_n_match_cpu_physical_id(np, cpu, NULL))
			break;

	of_node_put(np);
	if (cpu >= nr_cpu_ids)
		return NULL;

	return get_cpu_device(cpu);
}

/*
 * Get the clock and regulator for the first CPU so we can update OPPs,
 * listen in on regulator voltage change events, and figure out the
 * boot OPP based on clock frequency.
 */
static int
cpr_get_cpu_resources(struct cpr_drv *drv, struct device_node *of_node)
{
	struct device *cpu_dev;

	cpu_dev = cpr_get_cpu_device(of_node, 0);
	if (!cpu_dev)
		return -EINVAL;

	drv->cpu_dev = cpu_dev;
	drv->vdd_apc = devm_regulator_get(cpu_dev, "cpu");
	if (IS_ERR(drv->vdd_apc))
		return PTR_ERR(drv->vdd_apc);
	drv->cpu_clk = devm_clk_get(cpu_dev, NULL);

	return PTR_ERR_OR_ZERO(drv->cpu_clk);
}

static int cpr_populate_opps(struct device_node *of_node, struct cpr_drv *drv,
			     const struct corner_data **plan)
{
	int j, ret;
	struct device *cpu_dev;
	struct corner *corner;
	const struct corner_data *p;

	cpu_dev = get_cpu_device(0);
	for (j = 0, corner = drv->corners; plan[j]; j++, corner++) {
		p = plan[j];
		ret = dev_pm_opp_add(cpu_dev, p->freq, corner->uV);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct corner_data **
find_freq_plan(const struct cpr_desc *desc, u32 speed_bin, u32 pvs_version)
{
	int i;
	const struct freq_plan *p;

	for (i = 0; i < desc->num_freq_plans; i++) {
		p = &desc->freq_plans[i];

		if (p->speed_bin != speed_bin &&
		    p->speed_bin != FUSE_PARAM_MATCH_ANY)
			continue;
		if (p->pvs_version != pvs_version &&
		    p->pvs_version != FUSE_PARAM_MATCH_ANY)
			continue;

		return p->plan;
	}

	return NULL;

}

static struct corner_override *find_corner_override(const struct cpr_desc *desc,
		u32 speed_bin, u32 pvs_version)
{
	int i;
	struct corner_override *o;

	for (i = 0; i < desc->num_corner_overrides; i++) {
		o = &desc->corner_overrides[i];

		if (o->speed_bin != speed_bin &&
		    o->speed_bin != FUSE_PARAM_MATCH_ANY)
			continue;
		if (o->pvs_version != pvs_version &&
		    o->pvs_version != FUSE_PARAM_MATCH_ANY)
			continue;

		return o;
	}

	return NULL;

}
static int cpr_calculate_scaling(const struct qfprom_offset *quot_offset,
				 struct nvmem_device *qfprom,
				 const struct fuse_corner_data *fdata,
				 int adj_quot_offset,
				 const struct corner *corner)
{
	int quot_diff;
	unsigned long freq_diff;
	int scaling;
	const struct fuse_corner *fuse, *prev_fuse;

	fuse = corner->fuse_corner;
	prev_fuse = fuse - 1;

	if (quot_offset->width) {
		quot_diff = cpr_read_efuse(qfprom, quot_offset);
		quot_diff *= fdata->quot_scale;
		quot_diff += adj_quot_offset;
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
	do_div(temp, f_high - f_low);

	/*
	 * max_volt_scale has units of uV/MHz while freq values
	 * have units of Hz.  Divide by 1000000 to convert to.
	 */
	temp_limit = f_diff * fdata->max_volt_scale;
	do_div(temp_limit, 1000000);

	uV = uV_high - min(temp, temp_limit);
	return roundup(uV, step_volt);
}

static void cpr_corner_init(struct cpr_drv *drv, const struct cpr_desc *desc,
			const struct cpr_fuse *fuses, u32 speed_bin,
			u32 pvs_version, void __iomem *qfprom,
			const struct corner_adjustment *adjustments,
			const struct corner_data **plan)
{
	int i, fnum, scaling;
	const struct qfprom_offset *quot_offset;
	struct fuse_corner *fuse, *prev_fuse;
	struct corner *corner, *end;
	const struct corner_data *cdata, *p;
	const struct fuse_corner_data *fdata;
	bool apply_scaling;
	const int *adj_quot_offset;
	unsigned long freq_corner, freq_diff, freq_diff_mhz;
	int step_volt = 12500; /* TODO: Get from regulator APIs */
	const struct corner_override *override;

	corner = drv->corners;
	end = &corner[drv->num_corners - 1];
	cdata = desc->corner_data;
	adj_quot_offset = adjustments->fuse_quot_offset;

	override = find_corner_override(desc, speed_bin, pvs_version);

	/*
	 * Store maximum frequency for each fuse corner based on the frequency
	 * plan
	 */
	for (i = 0; plan[i]; i++) {
		p = plan[i];
		freq_corner = p->freq;
		fnum = p->fuse_corner;
		fuse = &drv->fuse_corners[fnum];
		if (freq_corner > fuse->max_freq)
			fuse->max_freq = freq_corner;

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
		quot_offset = &fuses[fnum].quotient_offset;
		fuse = &drv->fuse_corners[fnum];
		if (fnum)
			prev_fuse = &drv->fuse_corners[fnum - 1];
		else
			prev_fuse = NULL;

		corner->fuse_corner = fuse;
		corner->freq = cdata[i].freq;
		corner->uV = fuse->uV;

		if (prev_fuse && cdata[i - 1].freq == prev_fuse->max_freq) {
			scaling = cpr_calculate_scaling(quot_offset, qfprom,
					fdata, adj_quot_offset ?
						adj_quot_offset[fnum] : 0,
					corner);
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

		if (adjustments->fuse_quot)
			corner->quot_adjust -= adjustments->fuse_quot[i];

		if (adjustments->init_uV)
			corner->uV += adjustments->init_uV[i];

		/* Load per corner ceiling and floor voltages if they exist. */
		if (override) {
			corner->max_uV = override->max_uV[i];
			corner->min_uV = override->min_uV[i];
		} else {
			corner->max_uV = fuse->max_uV;
			corner->min_uV = fuse->min_uV;
		}

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
}

static const struct cpr_fuse *
cpr_get_fuses(const struct cpr_desc *desc, void __iomem *qfprom)
{
	u32 expected = desc->cpr_fuses.redundant_value;
	const struct qfprom_offset *fuse = &desc->cpr_fuses.redundant;
	unsigned int idx;

	idx = !!(fuse->width && cpr_read_efuse(qfprom, fuse) == expected);

	return &desc->cpr_fuses.cpr_fuse[idx * desc->num_fuse_corners];
}

static bool cpr_is_close_loop_disabled(struct cpr_drv *drv,
		const struct cpr_desc *desc, void __iomem *qfprom,
		const struct cpr_fuse *fuses,
		const struct corner_adjustment *adj)
{
	const struct qfprom_offset *disable;
	unsigned int idx;
	struct fuse_corner *highest_fuse, *second_highest_fuse;
	int min_diff_quot, diff_quot;

	if (adj->disable_closed_loop)
		return true;

	if (!desc->cpr_fuses.disable)
		return false;

	/*
	 * Are the fuses the redundant ones? This avoids reading the fuse
	 * redundant bit again
	 */
	idx = !!(fuses == desc->cpr_fuses.cpr_fuse);
	disable = &desc->cpr_fuses.disable[idx];

	if (cpr_read_efuse(qfprom, disable))
		return true;

	if (!fuses->quotient_offset.width) {
		/*
		 * Check if the target quotients for the highest two fuse
		 * corners are too close together.
		 */
		highest_fuse = &drv->fuse_corners[drv->num_fuse_corners - 1];
		second_highest_fuse = highest_fuse - 1;

		min_diff_quot = desc->min_diff_quot;
		diff_quot = highest_fuse->quot - second_highest_fuse->quot;

		return diff_quot < min_diff_quot;
	}

	return false;
}

static int cpr_init_parameters(struct platform_device *pdev,
		struct cpr_drv *drv)
{
	struct device_node *of_node = pdev->dev.of_node;
	int ret;

	ret = of_property_read_u32(of_node, "qcom,cpr-ref-clk",
			  &drv->ref_clk_khz);
	if (ret)
		return ret;
	ret = of_property_read_u32(of_node, "qcom,cpr-timer-delay-us",
			  &drv->timer_delay_us);
	if (ret)
		return ret;
	ret = of_property_read_u32(of_node, "qcom,cpr-timer-cons-up",
			  &drv->timer_cons_up);
	if (ret)
		return ret;
	ret = of_property_read_u32(of_node, "qcom,cpr-timer-cons-down",
			  &drv->timer_cons_down);
	if (ret)
		return ret;
	drv->timer_cons_down &= RBIF_TIMER_ADJ_CONS_DOWN_MASK;

	ret = of_property_read_u32(of_node, "qcom,cpr-up-threshold",
			  &drv->up_threshold);
	drv->up_threshold &= RBCPR_CTL_UP_THRESHOLD_MASK;
	if (ret)
		return ret;

	ret = of_property_read_u32(of_node, "qcom,cpr-down-threshold",
			  &drv->down_threshold);
	drv->down_threshold &= RBCPR_CTL_DN_THRESHOLD_MASK;
	if (ret)
		return ret;

	ret = of_property_read_u32(of_node, "qcom,cpr-idle-clocks",
			  &drv->idle_clocks);
	drv->idle_clocks &= RBCPR_STEP_QUOT_IDLE_CLK_MASK;
	if (ret)
		return ret;

	ret = of_property_read_u32(of_node, "qcom,cpr-gcnt-us", &drv->gcnt_us);
	if (ret)
		return ret;
	ret = of_property_read_u32(of_node, "qcom,vdd-apc-step-up-limit",
			  &drv->vdd_apc_step_up_limit);
	if (ret)
		return ret;
	ret = of_property_read_u32(of_node, "qcom,vdd-apc-step-down-limit",
			  &drv->vdd_apc_step_down_limit);
	if (ret)
		return ret;

	ret = of_property_read_u32(of_node, "qcom,cpr-clamp-timer-interval",
				  &drv->clamp_timer_interval);
	if (ret && ret != -EINVAL)
		return ret;

	drv->clamp_timer_interval = min_t(unsigned int,
					   drv->clamp_timer_interval,
					   RBIF_TIMER_ADJ_CLAMP_INT_MASK);

	dev_dbg(drv->dev, "up threshold = %u, down threshold = %u\n",
		 drv->up_threshold, drv->down_threshold);

	return 0;
}

static int cpr_init_and_enable_corner(struct cpr_drv *drv)
{
	unsigned long rate;
	const struct corner *end;

	end = &drv->corners[drv->num_corners - 1];
	rate = clk_get_rate(drv->cpu_clk);

	for (drv->corner = drv->corners; drv->corner <= end; drv->corner++)
		if (drv->corner->freq == rate)
			break;

	if (drv->corner > end)
		return -EINVAL;

	return cpr_enable(drv);
}

static struct corner_data msm8916_corner_data[] = {
	/* [corner] -> { fuse corner, freq } */
	{ 0,  200000000 },
	{ 0,  400000000 },
	{ 1,  533330000 },
	{ 1,  800000000 },
	{ 2,  998400000 },
	{ 2, 1094400000 },
	{ 2, 1152000000 },
	{ 2, 1209600000 },
	{ 2, 1363200000 },
};

static const struct cpr_desc msm8916_desc = {
	.num_fuse_corners = 3,
	.vdd_mx_vmin_method = VDD_MX_VMIN_FUSE_CORNER_MAP,
	.min_diff_quot = CPR_FUSE_MIN_QUOT_DIFF,
	.step_quot = (int []){ 26, 26, 26, 26, 26, 26, 26, 26 },
	.cpr_fuses = {
		.init_voltage_step = 10000,
		.fuse_corner_data = (struct fuse_corner_data[]){
			/* ref_uV max_uV min_uV max_q q_off q_scl v_scl mx */
			{ 1050000, 1050000, 1050000,   0, 0, 1, 0, 3 },
			{ 1150000, 1150000, 1050000,   0, 0, 1, 0, 4 },
			{ 1350000, 1350000, 1162500, 650, 0, 1, 0, 6 },
		},
		.cpr_fuse = (struct cpr_fuse[]){
			{
				.ring_osc = { 222, 3, 6},
				.init_voltage = { 220, 6, 2 },
				.quotient = { 221, 12, 2 },
			},
			{
				.ring_osc = { 222, 3, 6},
				.init_voltage = { 218, 6, 2 },
				.quotient = { 219, 12, 0 },
			},
			{
				.ring_osc = { 222, 3, 6},
				.init_voltage = { 216, 6, 0 },
				.quotient = { 216, 12, 6 },
			},
		},
		.disable = &(struct qfprom_offset){ 223, 1, 1 },
	},
	.speed_bin = { 12, 3, 2 },
	.pvs_version = { 6, 2, 7 },
	.corner_data = msm8916_corner_data,
	.num_corners = ARRAY_SIZE(msm8916_corner_data),
	.num_freq_plans = 3,
	.freq_plans = (struct freq_plan[]){
		{
			.speed_bin = 0,
			.pvs_version = 0,
			.plan = (const struct corner_data* []){
				msm8916_corner_data + 0,
				msm8916_corner_data + 1,
				msm8916_corner_data + 2,
				msm8916_corner_data + 3,
				msm8916_corner_data + 4,
				msm8916_corner_data + 5,
				msm8916_corner_data + 6,
				msm8916_corner_data + 7,
				NULL
			},
		},
		{
			.speed_bin = 0,
			.pvs_version = 1,
			.plan = (const struct corner_data* []){
				msm8916_corner_data + 0,
				msm8916_corner_data + 1,
				msm8916_corner_data + 2,
				msm8916_corner_data + 3,
				msm8916_corner_data + 4,
				msm8916_corner_data + 5,
				msm8916_corner_data + 6,
				msm8916_corner_data + 7,
				NULL
			},
		},
		{
			.speed_bin = 2,
			.pvs_version = 0,
			.plan = (const struct corner_data* []){
				msm8916_corner_data + 0,
				msm8916_corner_data + 1,
				msm8916_corner_data + 2,
				msm8916_corner_data + 3,
				msm8916_corner_data + 4,
				msm8916_corner_data + 5,
				msm8916_corner_data + 6,
				msm8916_corner_data + 7,
				msm8916_corner_data + 8,
				NULL
			},
		},
	},
};

static const struct acc_desc msm8916_acc_desc = {
	.settings = (struct reg_sequence[]){
		{ 0xf000, 0 },
		{ 0xf000, 0x100 },
		{ 0xf000, 0x101 }
	},
	.override_settings = (struct reg_sequence[]){
		{ 0xf000, 0 },
		{ 0xf000, 0x100 },
		{ 0xf000, 0x100 }
	},
	.num_regs_per_fuse = 1,
	.override = { 6, 1, 4 },
	.override_value = 1,
};

static const struct of_device_id cpr_descs[] = {
	{ .compatible = "qcom,qfprom-msm8916", .data = &msm8916_desc },
	{ }
};

static const struct of_device_id acc_descs[] = {
	{ .compatible = "qcom,tcsr-msm8916", .data = &msm8916_acc_desc },
	{ }
};

static int cpr_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct cpr_drv *drv;
	const struct cpr_fuse *cpr_fuses;
	const struct corner_adjustment *adj;
	const struct corner_adjustment empty_adj = { };
	const struct corner_data **plan;
	size_t len;
	int irq, ret;
	const struct cpr_desc *desc;
	const struct acc_desc *acc_desc;
	const struct of_device_id *match;
	struct device_node *np;
	void __iomem *qfprom;
	u32 cpr_rev = FUSE_REVISION_UNKNOWN;
	u32 speed_bin = SPEED_BIN_NONE;
	u32 pvs_version = 0;
	struct platform_device *cpufreq_dt_pdev;

	np = of_parse_phandle(dev->of_node, "eeprom", 0);
	if (!np)
		return -ENODEV;

	match = of_match_node(cpr_descs, np);
	of_node_put(np);
	if (!match)
		return -EINVAL;
	desc = match->data;

	/* TODO: Get from eeprom API */
	qfprom = devm_ioremap(dev, 0x58000, 0x7000);
	if (!qfprom)
		return -ENOMEM;

	len = sizeof(*drv) +
	      sizeof(*drv->fuse_corners) * desc->num_fuse_corners +
	      sizeof(*drv->corners) * desc->num_corners;

	drv = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!drv)
		return -ENOMEM;
	drv->dev = dev;

	np = of_parse_phandle(dev->of_node, "acc-syscon", 0);
	if (!np)
		return -ENODEV;

	match = of_match_node(acc_descs, np);
	if (!match) {
		of_node_put(np);
		return -EINVAL;
	}

	acc_desc = match->data;
	drv->tcsr = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(drv->tcsr))
		return PTR_ERR(drv->tcsr);

	drv->num_fuse_corners = desc->num_fuse_corners;
	drv->num_corners = desc->num_corners;
	drv->fuse_corners = (struct fuse_corner *)(drv + 1);
	drv->corners = (struct corner *)(drv->fuse_corners +
			drv->num_fuse_corners);
	mutex_init(&drv->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drv->base))
		return PTR_ERR(drv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	ret = cpr_get_cpu_resources(drv, dev->of_node);
	if (ret)
		return ret;

	drv->vdd_mx = devm_regulator_get(dev, "vdd-mx");
	if (IS_ERR(drv->vdd_mx))
		return PTR_ERR(drv->vdd_mx);

	drv->vdd_mx_vmin_method = desc->vdd_mx_vmin_method;

	if (desc->fuse_revision.width)
		cpr_rev = cpr_read_efuse(qfprom, &desc->fuse_revision);
	if (desc->speed_bin.width)
		speed_bin = cpr_read_efuse(qfprom, &desc->speed_bin);
	if (desc->pvs_version.width)
		pvs_version = cpr_read_efuse(qfprom, &desc->pvs_version);

	plan = find_freq_plan(desc, speed_bin, pvs_version);
	if (!plan)
		return -EINVAL;

	cpr_fuses = cpr_get_fuses(desc, qfprom);
	cpr_populate_ring_osc_idx(cpr_fuses, drv, qfprom);

	adj = cpr_find_adjustment(speed_bin, pvs_version, cpr_rev, desc, drv);
	if (!adj)
		adj = &empty_adj;

	cpr_fuse_corner_init(drv, desc, qfprom, cpr_fuses, speed_bin, adj,
			     acc_desc);
	cpr_corner_init(drv, desc, cpr_fuses, speed_bin, pvs_version, qfprom,
			adj, plan);

	ret = cpr_populate_opps(dev->of_node, drv, plan);
	if (ret)
		return ret;

	/* Register cpufreq-dt driver after the OPPs are populated */
	cpufreq_dt_pdev = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	if (IS_ERR(cpufreq_dt_pdev)) {
		ret = PTR_ERR(cpufreq_dt_pdev);
		pr_err("%s error registering cpufreq-dt (%d)\n", __func__, ret);
		return ret;
	}

	drv->loop_disabled = cpr_is_close_loop_disabled(drv, desc, qfprom,
			cpr_fuses, adj);
	dev_dbg(drv->dev, "CPR closed loop is %sabled\n",
		 drv->loop_disabled ? "dis" : "en");

	ret = cpr_init_parameters(pdev, drv);
	if (ret)
		return ret;

	/* Configure CPR HW but keep it disabled */
	ret = cpr_config(drv);
	if (ret)
		return ret;

	/* Enable ACC if required */
	if (acc_desc->enable_mask)
		regmap_update_bits(drv->tcsr, acc_desc->enable_reg,
				   acc_desc->enable_mask,
				   acc_desc->enable_mask);

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
			cpr_irq_handler, IRQF_ONESHOT | IRQF_TRIGGER_RISING,
			"cpr", drv);
	if (ret)
		return ret;

	ret = cpr_init_and_enable_corner(drv);
	if (ret)
		return ret;

	drv->reg_nb.notifier_call = cpr_regulator_notifier;
	ret = regulator_register_notifier(drv->vdd_apc, &drv->reg_nb);
	if (ret)
		return ret;

	drv->cpufreq_nb.notifier_call = cpr_cpufreq_notifier;
	ret = cpufreq_register_notifier(&drv->cpufreq_nb,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		regulator_unregister_notifier(drv->vdd_apc, &drv->reg_nb);
		return ret;
	}

	platform_set_drvdata(pdev, drv);

	return 0;
}

static int cpr_remove(struct platform_device *pdev)
{
	struct cpr_drv *drv = platform_get_drvdata(pdev);

	if (cpr_is_allowed(drv)) {
		cpr_ctl_disable(drv);
		cpr_irq_set(drv, 0);
	}

	return 0;
}

static const struct of_device_id cpr_match_table[] = {
	{ .compatible = "qcom,cpr" },
	{ }
};
MODULE_DEVICE_TABLE(of, cpr_match_table);

static struct platform_driver cpr_driver = {
	.probe		= cpr_probe,
	.remove		= cpr_remove,
	.driver		= {
		.name	= "qcom-cpr",
		.of_match_table = cpr_match_table,
		.pm = &cpr_pm_ops,
	},
};
module_platform_driver(cpr_driver);

MODULE_DESCRIPTION("Core Power Reduction (CPR) driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-cpr");
