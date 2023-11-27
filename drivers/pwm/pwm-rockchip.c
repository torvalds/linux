// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM driver for Rockchip SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/pwm-rockchip.h>
#include <linux/time.h>
#include "pwm-rockchip-irq-callbacks.h"

#define PWM_MAX_CHANNEL_NUM	8

/*
 * regs for pwm v1-v3
 */
#define PWM_CTRL_TIMER_EN	(1 << 0)
#define PWM_CTRL_OUTPUT_EN	(1 << 3)

#define PWM_ENABLE		(1 << 0)
#define PWM_MODE_SHIFT		1
#define PWM_MODE_MASK		(0x3 << PWM_MODE_SHIFT)
#define PWM_ONESHOT		(0 << PWM_MODE_SHIFT)
#define PWM_CONTINUOUS		(1 << PWM_MODE_SHIFT)
#define PWM_CAPTURE		(2 << PWM_MODE_SHIFT)
#define PWM_DUTY_POSITIVE	(1 << 3)
#define PWM_DUTY_NEGATIVE	(0 << 3)
#define PWM_INACTIVE_NEGATIVE	(0 << 4)
#define PWM_INACTIVE_POSITIVE	(1 << 4)
#define PWM_POLARITY_MASK	(PWM_DUTY_POSITIVE | PWM_INACTIVE_POSITIVE)
#define PWM_OUTPUT_LEFT		(0 << 5)
#define PWM_OUTPUT_CENTER	(1 << 5)
#define PWM_LOCK_EN		(1 << 6)
#define PWM_LP_DISABLE		(0 << 8)
#define PWM_CLK_SEL_SHIFT	9
#define PWM_CLK_SEL_MASK	(1 << PWM_CLK_SEL_SHIFT)
#define PWM_SEL_NO_SCALED_CLOCK	(0 << PWM_CLK_SEL_SHIFT)
#define PWM_SEL_SCALED_CLOCK	(1 << PWM_CLK_SEL_SHIFT)
#define PWM_PRESCELE_SHIFT	12
#define PWM_PRESCALE_MASK	(0x3 << PWM_PRESCELE_SHIFT)
#define PWM_SCALE_SHIFT		16
#define PWM_SCALE_MASK		(0xff << PWM_SCALE_SHIFT)

#define PWM_ONESHOT_COUNT_SHIFT	24
#define PWM_ONESHOT_COUNT_MASK	(0xff << PWM_ONESHOT_COUNT_SHIFT)

#define PWM_REG_INTSTS(n)	((3 - (n)) * 0x10 + 0x10)
#define PWM_REG_INT_EN(n)	((3 - (n)) * 0x10 + 0x14)

#define PWM_CH_INT(n)		BIT(n)

/*
 * regs for pwm v4
 */
#define HIWORD_UPDATE(v, l, h)	(((v) << (l)) | (GENMASK(h, l) << 16))

/* VERSION_ID */
#define CHANNEL_NUM_SUPPORT_SHIFT	0
#define CHANNEL_NUM_SUPPORT_MASK	(0xf << CHANNEL_NUM_SUPPORT_SHIFT)
#define CHANNLE_INDEX_SHIFT		4
#define CHANNLE_INDEX_MASK		(0xf << CHANNLE_INDEX_SHIFT)
#define IR_TRANS_SUPPORT		BIT(8)
#define POWER_KEY_SUPPORT		BIT(9)
#define FREQ_METER_SUPPORT		BIT(10)
#define COUNTER_SUPPORT			BIT(11)
#define WAVE_SUPPORT			BIT(12)
#define FILTER_SUPPORT			BIT(13)
#define MINOR_VERSION_SHIFT		16
#define MINOR_VERSION_MASK		(0xff << MINOR_VERSION_SHIFT)
#define MAIN_VERSION_SHIFT		24
#define MAIN_VERSION_MASK		(0xff << MAIN_VERSION_SHIFT)
/* PWM_ENABLE */
#define PWM_ENABLE_V4			(0x3 << 0)
#define PWM_CLK_EN(v)			HIWORD_UPDATE(v, 0, 0)
#define PWM_EN(v)			HIWORD_UPDATE(v, 1, 1)
#define PWM_CTRL_UPDATE_EN(v)		HIWORD_UPDATE(v, 2, 2)
#define PWM_GLOBAL_JOIN_EN(v)		HIWORD_UPDATE(v, 4, 4)
/* PWM_CLK_CTRL */
#define CLK_PRESCALE(v)			HIWORD_UPDATE(v, 0, 2)
#define CLK_SCALE(v)			HIWORD_UPDATE(v, 4, 12)
#define CLK_SRC_SEL(v)			HIWORD_UPDATE(v, 13, 14)
#define CLK_GLOBAL_SEL(v)		HIWORD_UPDATE(v, 15, 15)
/* PWM_CTRL */
#define PWM_MODE(v)			HIWORD_UPDATE(v, 0, 1)
#define ONESHOT_MODE			0
#define CONTINUOUS_MODE			1
#define CAPTURE_MODE			2
#define PWM_POLARITY(v)			HIWORD_UPDATE(v, 2, 3)
#define DUTY_NEGATIVE			(0 << 0)
#define DUTY_POSITIVE			(1 << 0)
#define INACTIVE_NEGATIVE		(0 << 1)
#define INACTIVE_POSITIVE		(1 << 1)
#define PWM_ALIGNED_INVALID(v)		HIWORD_UPDATE(v, 5, 5)
#define PWM_IN_SEL(v)			HIWORD_UPDATE(v, 6, 8)
/* PWM_RPT */
#define FIRST_DIMENSIONAL_SHIFT		0
#define SECOND_DIMENSINAL_SHIFT		16
/* INTSTS*/
#define CAP_LPR_INTSTS_SHIFT		0
#define CAP_HPR_INTSTS_SHIFT		1
#define ONESHOT_END_INTSTS_SHIFT	2
#define RELOAD_INTSTS_SHIFT		3
#define FREQ_INTSTS_SHIFT		4
#define PWR_INTSTS_SHIFT		5
#define IR_TRANS_END_INTSTS_SHIFT	6
#define WAVE_MAX_INT_SHIFT		7
#define WAVE_MIDDLE_INT_SHIFT		8
#define CAP_LPR_INT			BIT(CAP_LPR_INTSTS_SHIFT)
#define CAP_HPR_INT			BIT(CAP_HPR_INTSTS_SHIFT)
#define ONESHOT_END_INT			BIT(ONESHOT_END_INTSTS_SHIFT)
#define RELOAD_INT			BIT(RELOAD_INTSTS_SHIFT)
#define FREQ_INT			BIT(FREQ_INTSTS_SHIFT)
#define PWR_INT				BIT(PWR_INTSTS_SHIFT)
#define IR_TRANS_END_INT		BIT(IR_TRANS_END_INTSTS_SHIFT)
#define WAVE_MAX_INT			BIT(WAVE_MAX_INT_SHIFT)
#define WAVE_MIDDLE_INT			BIT(WAVE_MIDDLE_INT_SHIFT)
/* INT_EN */
#define CAP_LPR_INT_EN(v)		HIWORD_UPDATE(v, 0, 0)
#define CAP_HPR_INT_EN(v)		HIWORD_UPDATE(v, 1, 1)
#define ONESHOT_END_INT_EN(v)		HIWORD_UPDATE(v, 2, 2)
#define RELOAD_INT_EN(v)		HIWORD_UPDATE(v, 3, 3)
#define FREQ_INT_EN(v)			HIWORD_UPDATE(v, 4, 4)
#define PWR_INT_EN(v)			HIWORD_UPDATE(v, 5, 5)
#define IR_TRANS_END_INT_EN(v)		HIWORD_UPDATE(v, 6, 6)
#define WAVE_MAX_INT_EN(v)		HIWORD_UPDATE(v, 7, 7)
#define WAVE_MIDDLE_INT_EN(v)		HIWORD_UPDATE(v, 8, 8)
/* WAVE_MEM_ARBITER */
#define WAVE_MEM_ARBITER		0x80
#define WAVE_MEM_GRANT_SHIFT		0
#define WAVE_MEM_READ_LOCK_SHIFT	16
/* WAVE_MEM_STATUS */
#define WAVE_MEM_STATUS			0x84
#define WAVE_MEM_STATUS_SHIFT		0
/* WAVE_CTRL */
#define WAVE_CTRL			0x88
#define WAVE_DUTY_EN(v)			HIWORD_UPDATE(v, 0, 0)
#define WAVE_PERIOD_EN(v)		HIWORD_UPDATE(v, 1, 1)
#define WAVE_WIDTH_MODE(v)		HIWORD_UPDATE(v, 2, 2)
#define WAVE_UPDATE_MODE(v)		HIWORD_UPDATE(v, 3, 3)
#define WAVE_MEM_CLK_SEL(v)		HIWORD_UPDATE(v, 4, 5)
#define WAVE_DUTY_AMPLIFY(v)		HIWORD_UPDATE(v, 6, 10)
#define WAVE_PERIOD_AMPLIFY(v)		HIWORD_UPDATE(v, 11, 15)
/* WAVE_MAX */
#define WAVE_MAX			0x8c
#define WAVE_DUTY_MAX_SHIFT		0
#define WAVE_PERIOD_MAX_SHIFT		16
/* WAVE_MIN */
#define WAVE_MIN			0x90
#define WAVE_DUTY_MIN_SHIFT		0
#define WAVE_PERIOD_MIN_SHIFT		16
/* WAVE_OFFSET */
#define WAVE_OFFSET			0x94
#define WAVE_OFFSET_SHIFT		0
/* WAVE_MIDDLE */
#define WAVE_MIDDLE			0x98
#define WAVE_MIDDLE_SHIFT		0
/* WAVE_HOLD */
#define WAVE_HOLD			0x9c
#define MAX_HOLD_SHIFT			0
#define MIN_HOLD_SHIFT			8
#define MIDDLE_HOLD_SHIFT		16
/* GLOBAL_ARBITER */
#define GLOBAL_ARBITER			0xc0
#define GLOBAL_GRANT_SHIFT		0
#define GLOBAL_READ_LOCK_SHIFT		16
/* GLOBAL_CTRL */
#define GLOBAL_CTRL			0xc4
#define GLOBAL_PWM_EN(v)		HIWORD_UPDATE(v, 0, 0)
#define GLOBAL_PWM_UPDATE_EN(v)		HIWORD_UPDATE(v, 1, 1)
/* FREQ_ARBITER */
#define FREQ_ARBITER			0x1c0
#define FREQ_GRANT_SHIFT		0
#define FREQ_READ_LOCK_SHIFT		16
/* FREQ_CTRL */
#define FREQ_CTRL			0x1c4
#define FREQ_EN(v)			HIWORD_UPDATE(v, 0, 0)
#define FREQ_CLK_SEL(v)			HIWORD_UPDATE(v, 1, 2)
#define FREQ_CHANNEL_SEL(v)		HIWORD_UPDATE(v, 3, 5)
#define FREQ_CLK_SWITCH_MODE(v)		HIWORD_UPDATE(v, 6, 6)
#define FREQ_TIMIER_CLK_SEL(v)		HIWORD_UPDATE(v, 7, 7)
/* FREQ_TIMER_VALUE */
#define FREQ_TIMER_VALUE		0x1c8
/* FREQ_RESULT_VALUE */
#define FREQ_RESULT_VALUE		0x1cc
/* COUNTER_ARBITER */
#define COUNTER_ARBITER			0x200
#define COUNTER_GRANT_SHIFT		0
#define COUNTER_READ_LOCK_SHIFT		16
/* COUNTER_CTRL */
#define COUNTER_CTRL			0x204
#define COUNTER_EN(v)			HIWORD_UPDATE(v, 0, 0)
#define COUNTER_CLK_SEL(v)		HIWORD_UPDATE(v, 1, 2)
#define COUNTER_CHANNEL_SEL(v)		HIWORD_UPDATE(v, 3, 5)
#define COUNTER_CLR(v)			HIWORD_UPDATE(v, 6, 6)
/* COUNTER_LOW */
#define COUNTER_LOW			0x208
/* COUNTER_HIGH */
#define COUNTER_HIGH			0x20c
/* WAVE_MEM */
#define WAVE_MEM			0x400

struct rockchip_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	struct clk *pclk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *active_state;
	struct delayed_work pwm_work;
	const struct rockchip_pwm_data *data;
	struct resource *res;
	struct dentry *debugfs;
	void __iomem *base;
	unsigned long clk_rate;
	bool vop_pwm_en; /* indicate voppwm mirror register state */
	bool center_aligned;
	bool oneshot_en;
	bool wave_en;
	bool global_ctrl_grant;
	bool freq_meter_support;
	bool counter_support;
	bool wave_support;
	int channel_id;
	int irq;
	u8 main_version;
	u8 capture_cnt;
};

struct rockchip_pwm_regs {
	unsigned long duty;
	unsigned long period;
	unsigned long cntr;
	unsigned long ctrl;
	unsigned long version;

	unsigned long enable;
	unsigned long clk_ctrl;
	unsigned long offset;
	unsigned long rpt;
	unsigned long hpr;
	unsigned long lpr;
	unsigned long intsts;
	unsigned long int_en;
	unsigned long int_mask;
};

struct rockchip_pwm_funcs {
	int (*enable)(struct pwm_chip *chip, struct pwm_device *pwm, bool enable);
	void (*config)(struct pwm_chip *chip, struct pwm_device *pwm,
		       const struct pwm_state *state);
	void (*set_capture)(struct pwm_chip *chip, struct pwm_device *pwm, bool enable);
	int (*get_capture_result)(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_capture *catpure_res);
	int (*set_counter)(struct pwm_chip *chip, struct pwm_device *pwm, bool enable);
	int (*get_counter_result)(struct pwm_chip *chip, struct pwm_device *pwm,
				  unsigned long *counter_res, bool is_clear);
	int (*set_freq_meter)(struct pwm_chip *chip, struct pwm_device *pwm,
			      bool enable, unsigned long delay_ms);
	int (*get_freq_meter_result)(struct pwm_chip *chip, struct pwm_device *pwm,
				     unsigned long delay_ms, unsigned long *freq_hz);
	int (*global_ctrl)(struct pwm_chip *chip, struct pwm_device *pwm,
			   enum rockchip_pwm_global_ctrl_cmd cmd);
	int (*set_wave_table)(struct pwm_chip *chip, struct pwm_device *pwm,
			      struct rockchip_pwm_wave_table *table_config,
			      enum rockchip_pwm_wave_table_width_mode width_mode);
	int (*set_wave)(struct pwm_chip *chip, struct pwm_device *pwm,
			struct rockchip_pwm_wave_config *config);
	irqreturn_t (*irq_handler)(int irq, void *data);
};

struct rockchip_pwm_data {
	struct rockchip_pwm_regs regs;
	struct rockchip_pwm_funcs funcs;
	unsigned int prescaler;
	bool supports_polarity;
	bool supports_lock;
	bool vop_pwm;
	u8 main_version;
	u32 enable_conf;
	u32 enable_conf_mask;
	u32 oneshot_cnt_max;
	u32 oneshot_rpt_max;
	u32 wave_table_max;
};

static inline struct rockchip_pwm_chip *to_rockchip_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct rockchip_pwm_chip, chip);
}

static void rockchip_pwm_get_state(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	u64 tmp;
	u32 val;
	u32 dclk_div = 1;
	int ret;

	if (!pc->oneshot_en) {
		ret = clk_enable(pc->pclk);
		if (ret)
			return;
	}

	if (pc->main_version < 4)
		dclk_div = pc->oneshot_en ? 2 : 1;

	tmp = readl_relaxed(pc->base + pc->data->regs.period);
	tmp *= dclk_div * pc->data->prescaler * NSEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	tmp = readl_relaxed(pc->base + pc->data->regs.duty);
	tmp *= dclk_div * pc->data->prescaler * NSEC_PER_SEC;
	state->duty_cycle =  DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	if (pc->main_version >= 4) {
		val = readl_relaxed(pc->base + pc->data->regs.enable);
	} else {
		val = readl_relaxed(pc->base + pc->data->regs.ctrl);
		if (pc->oneshot_en)
			enable_conf &= ~PWM_CONTINUOUS;
	}
	state->enabled = (val & enable_conf) == enable_conf;

	if (pc->data->supports_polarity && !(val & PWM_DUTY_POSITIVE))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	if (!pc->oneshot_en)
		clk_disable(pc->pclk);
}

static irqreturn_t rockchip_pwm_irq_v1(int irq, void *data)
{
	struct rockchip_pwm_chip *pc = data;
	struct pwm_state state;
	unsigned int id = pc->channel_id;
	int val;

	if (id > 3)
		return IRQ_NONE;
	val = readl_relaxed(pc->base + PWM_REG_INTSTS(id));

	if ((val & PWM_CH_INT(id)) == 0)
		return IRQ_NONE;

	writel_relaxed(PWM_CH_INT(id), pc->base + PWM_REG_INTSTS(id));

	/*
	 * Set pwm state to disabled when the oneshot mode finished.
	 */
	pwm_get_state(&pc->chip.pwms[0], &state);
	state.enabled = false;
	pwm_apply_state(&pc->chip.pwms[0], &state);

	rockchip_pwm_oneshot_callback(&pc->chip.pwms[0], &state);

	return IRQ_HANDLED;
}

static void rockchip_pwm_config_v1(struct pwm_chip *chip, struct pwm_device *pwm,
				   const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	unsigned long period, duty, delay_ns;
	unsigned long flags;
	u64 div;
	u32 ctrl;
	u8 dclk_div = 1;

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (state->oneshot_count > 0 && state->oneshot_count <= pc->data->oneshot_cnt_max)
		dclk_div = 2;
#endif

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	div = (u64)pc->clk_rate * state->period;
	period = DIV_ROUND_CLOSEST_ULL(div, dclk_div * pc->data->prescaler * NSEC_PER_SEC);

	div = (u64)pc->clk_rate * state->duty_cycle;
	duty = DIV_ROUND_CLOSEST_ULL(div, dclk_div * pc->data->prescaler * NSEC_PER_SEC);

	if (pc->data->supports_lock) {
		div = (u64)10 * NSEC_PER_SEC * dclk_div * pc->data->prescaler;
		delay_ns = DIV_ROUND_UP_ULL(div, pc->clk_rate);
	}

	local_irq_save(flags);

	ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	if (pc->data->vop_pwm) {
		if (pc->vop_pwm_en)
			ctrl |= PWM_ENABLE;
		else
			ctrl &= ~PWM_ENABLE;
	}

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (state->oneshot_count > 0 && state->oneshot_count <= pc->data->oneshot_cnt_max) {
		u32 int_ctrl;

		/*
		 * This is a workaround, an uncertain waveform will be
		 * generated after oneshot ends. It is needed to enable
		 * the dclk scale function to resolve it. It doesn't
		 * matter what the scale factor is, just make sure the
		 * scale function is turned on, for which we set scale
		 * factor to 2.
		 */
		ctrl &= ~PWM_SCALE_MASK;
		ctrl |= (dclk_div / 2) << PWM_SCALE_SHIFT;
		ctrl &= ~PWM_CLK_SEL_MASK;
		ctrl |= PWM_SEL_SCALED_CLOCK;

		pc->oneshot_en = true;
		ctrl &= ~PWM_MODE_MASK;
		ctrl |= PWM_ONESHOT;

		ctrl &= ~PWM_ONESHOT_COUNT_MASK;
		ctrl |= (state->oneshot_count - 1) << PWM_ONESHOT_COUNT_SHIFT;

		if (pc->irq >= 0) {
			int_ctrl = readl_relaxed(pc->base + PWM_REG_INT_EN(pc->channel_id));
			int_ctrl |= PWM_CH_INT(pc->channel_id);
			writel_relaxed(int_ctrl, pc->base + PWM_REG_INT_EN(pc->channel_id));
		}
	} else {
		u32 int_ctrl;

		ctrl &= ~PWM_SCALE_MASK;
		ctrl &= ~PWM_CLK_SEL_MASK;
		ctrl |= PWM_SEL_NO_SCALED_CLOCK;

		if (state->oneshot_count)
			dev_err(chip->dev, "Oneshot_count must be between 1 and %d.\n",
				pc->data->oneshot_cnt_max);

		pc->oneshot_en = false;
		ctrl &= ~PWM_MODE_MASK;
		ctrl |= PWM_CONTINUOUS;

		ctrl &= ~PWM_ONESHOT_COUNT_MASK;

		int_ctrl = readl_relaxed(pc->base + PWM_REG_INT_EN(pc->channel_id));
		int_ctrl &= ~PWM_CH_INT(pc->channel_id);
		writel_relaxed(int_ctrl, pc->base + PWM_REG_INT_EN(pc->channel_id));
	}
#endif

	/*
	 * Lock the period and duty of previous configuration, then
	 * change the duty and period, that would not be effective.
	 */
	if (pc->data->supports_lock) {
		ctrl |= PWM_LOCK_EN;
		writel_relaxed(ctrl, pc->base + pc->data->regs.ctrl);
	}

	writel(period, pc->base + pc->data->regs.period);
	writel(duty, pc->base + pc->data->regs.duty);

	if (pc->data->supports_polarity) {
		ctrl &= ~PWM_POLARITY_MASK;
		if (state->polarity == PWM_POLARITY_INVERSED)
			ctrl |= PWM_DUTY_NEGATIVE | PWM_INACTIVE_POSITIVE;
		else
			ctrl |= PWM_DUTY_POSITIVE | PWM_INACTIVE_NEGATIVE;
	}

	/*
	 * Unlock and set polarity at the same time, the configuration of duty,
	 * period and polarity would be effective together at next period. It
	 * takes 10 dclk cycles to make sure lock works before unlocking.
	 */
	if (pc->data->supports_lock) {
		ctrl &= ~PWM_LOCK_EN;
		ndelay(delay_ns);
	}

	writel(ctrl, pc->base + pc->data->regs.ctrl);
	local_irq_restore(flags);
}

static int rockchip_pwm_enable_v1(struct pwm_chip *chip, struct pwm_device *pwm, bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	int ret;
	u32 val;

	if (enable) {
		ret = clk_enable(pc->clk);
		if (ret)
			return ret;
	}

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);
	val &= ~pc->data->enable_conf_mask;

	if (PWM_OUTPUT_CENTER & pc->data->enable_conf_mask) {
		if (pc->center_aligned)
			val |= PWM_OUTPUT_CENTER;
	}

	if (enable) {
		val |= enable_conf;
		if (pc->oneshot_en)
			val &= ~PWM_CONTINUOUS;
	} else {
		val &= ~enable_conf;
	}

	writel_relaxed(val, pc->base + pc->data->regs.ctrl);
	if (pc->data->vop_pwm)
		pc->vop_pwm_en = enable;

	if (!enable)
		clk_disable(pc->clk);

	return 0;
}

static irqreturn_t rockchip_pwm_irq_v4(int irq, void *data)
{
	struct rockchip_pwm_chip *pc = data;
	int val;
	irqreturn_t ret = IRQ_NONE;

	val = readl_relaxed(pc->base + pc->data->regs.intsts);
#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (val & ONESHOT_END_INT) {
		struct pwm_state state;

		writel_relaxed(ONESHOT_END_INT, pc->base + pc->data->regs.intsts);

		/*
		 * Set pwm state to disabled when the oneshot mode finished.
		 */
		pwm_get_state(&pc->chip.pwms[0], &state);
		state.enabled = false;
		state.oneshot_count = 0;
		state.oneshot_repeat = 0;
		pwm_apply_state(&pc->chip.pwms[0], &state);

		rockchip_pwm_oneshot_callback(&pc->chip.pwms[0], &state);

		ret = IRQ_HANDLED;
	}
#endif
	if (val & CAP_LPR_INT) {
		writel_relaxed(CAP_LPR_INT, pc->base + pc->data->regs.intsts);
		pc->capture_cnt++;

		ret = IRQ_HANDLED;
	} else if (val & CAP_HPR_INT) {
		writel_relaxed(CAP_HPR_INT, pc->base + pc->data->regs.intsts);
		pc->capture_cnt++;

		ret = IRQ_HANDLED;
	}

	/*
	 * Capture input waveform:
	 *    _______                 _______
	 *   |       |               |       |
	 * __|       |_______________|       |________
	 *   ^0      ^1              ^2
	 *
	 * At position 0, the LPR interrupt comes, and PERIOD_LPR reg shows
	 * the low polarity cycles which should be ignored. The effective
	 * high and low polarity cycles will be calculated in position 1 and
	 * position 2, where the HPR and LPR interrupts come again.
	 */
	if (pc->capture_cnt > 3) {
		writel_relaxed(CAP_LPR_INT | CAP_HPR_INT, pc->base + pc->data->regs.intsts);
		writel_relaxed(CAP_LPR_INT_EN(false) | CAP_HPR_INT_EN(false),
			       pc->base + pc->data->regs.int_en);
	}

	if (val & WAVE_MIDDLE_INT) {
		writel_relaxed(WAVE_MIDDLE_INT, pc->base + pc->data->regs.intsts);

		rockchip_pwm_wave_middle_callback(&pc->chip.pwms[0]);

		ret = IRQ_HANDLED;
	}

	if (val & WAVE_MAX_INT) {
		writel_relaxed(WAVE_MAX_INT, pc->base + pc->data->regs.intsts);

		rockchip_pwm_wave_max_callback(&pc->chip.pwms[0]);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static void rockchip_pwm_config_v4(struct pwm_chip *chip, struct pwm_device *pwm,
				   const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	unsigned long period, duty;
	u64 div = 0;
	u32 rpt = 0;
	u32 offset = 0;

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	div = (u64)pc->clk_rate * state->period;
	period = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);

	div = (u64)pc->clk_rate * state->duty_cycle;
	duty = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);

	writel_relaxed(period, pc->base + pc->data->regs.period);
	writel_relaxed(duty, pc->base + pc->data->regs.duty);

	if (pc->data->supports_polarity) {
		if (state->polarity == PWM_POLARITY_INVERSED)
			writel_relaxed(PWM_POLARITY(DUTY_NEGATIVE | INACTIVE_POSITIVE),
				       pc->base + pc->data->regs.ctrl);
		else
			writel_relaxed(PWM_POLARITY(DUTY_POSITIVE | INACTIVE_NEGATIVE),
				       pc->base + pc->data->regs.ctrl);
	}

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if ((state->oneshot_count > 0 && state->oneshot_count <= pc->data->oneshot_cnt_max) &&
	    (state->oneshot_repeat <= pc->data->oneshot_rpt_max)) {
		rpt |= (state->oneshot_count - 1) << FIRST_DIMENSIONAL_SHIFT;
		if (state->oneshot_repeat)
			rpt |= (state->oneshot_repeat - 1) << SECOND_DIMENSINAL_SHIFT;

		if (state->duty_offset > 0 &&
		    state->duty_offset <= (state->period - state->duty_cycle)) {
			div = (u64)pc->clk_rate * state->duty_offset;
			offset = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);
		} else if (state->duty_offset > (state->period - state->duty_cycle)) {
			dev_err(chip->dev, "Duty_offset must be between %lld and %lld.\n",
				state->duty_cycle, state->period);
		}

		pc->oneshot_en = true;
	} else {
		if (state->oneshot_count)
			dev_err(chip->dev, "Oneshot_count must be between 1 and %d.\n",
				pc->data->oneshot_cnt_max);

		pc->oneshot_en = false;
	}
#endif

	if (pc->oneshot_en) {
		writel_relaxed(PWM_MODE(ONESHOT_MODE) | PWM_ALIGNED_INVALID(true),
			       pc->base + pc->data->regs.ctrl);
		writel_relaxed(offset, pc->base + pc->data->regs.offset);
		writel_relaxed(rpt, pc->base + pc->data->regs.rpt);
		writel_relaxed(ONESHOT_END_INT_EN(true), pc->base + pc->data->regs.int_en);
	} else {
		writel_relaxed(PWM_MODE(CONTINUOUS_MODE) | PWM_ALIGNED_INVALID(false),
			       pc->base + pc->data->regs.ctrl);
		writel_relaxed(0, pc->base + pc->data->regs.offset);
		if (!pc->wave_en)
			writel_relaxed(0, pc->base + pc->data->regs.rpt);
		writel_relaxed(ONESHOT_END_INT_EN(false), pc->base + pc->data->regs.int_en);
	}

	writel_relaxed(PWM_CTRL_UPDATE_EN(true), pc->base + pc->data->regs.enable);
}

static int rockchip_pwm_enable_v4(struct pwm_chip *chip, struct pwm_device *pwm, bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	int ret;

	if (enable) {
		ret = clk_enable(pc->clk);
		if (ret)
			return ret;
	}

	writel_relaxed(PWM_EN(enable) | PWM_CLK_EN(enable), pc->base + pc->data->regs.enable);

	if (!enable)
		clk_disable(pc->clk);

	return 0;
}

static void rockchip_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);

	pc->data->funcs.config(chip, pwm, state);
}

static int rockchip_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm, bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);

	return pc->data->funcs.enable(chip, pwm, enable);
}

static int rockchip_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	struct pwm_state curstate;
	bool enabled;
	int ret = 0;

	if (!pc->oneshot_en) {
		ret = clk_enable(pc->pclk);
		if (ret)
			return ret;
	}

	pwm_get_state(pwm, &curstate);
	enabled = curstate.enabled;

	if (state->polarity != curstate.polarity && enabled &&
	    !pc->data->supports_lock) {
		ret = rockchip_pwm_enable(chip, pwm, false);
		if (ret)
			goto out;
		enabled = false;
	}

	rockchip_pwm_config(chip, pwm, state);
	if (state->enabled != enabled) {
		ret = rockchip_pwm_enable(chip, pwm, state->enabled);
		if (ret)
			goto out;
	}

	if (state->enabled)
		ret = pinctrl_select_state(pc->pinctrl, pc->active_state);
out:
	if (!pc->oneshot_en)
		clk_disable(pc->pclk);

	return ret;
}

static void rockchip_pwm_set_capture_v4(struct pwm_chip *chip, struct pwm_device *pwm,
					bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 channel_sel = 0;

	if (enable)
		channel_sel = pc->channel_id;

	pc->capture_cnt = 0;

	writel_relaxed(enable ? PWM_MODE(CAPTURE_MODE) : PWM_MODE(CONTINUOUS_MODE),
		       pc->base + pc->data->regs.ctrl);
	writel_relaxed(CAP_LPR_INT_EN(enable) | CAP_HPR_INT_EN(enable) | PWM_IN_SEL(channel_sel),
		       pc->base + pc->data->regs.int_en);
}

static int rockchip_pwm_get_capture_result_v4(struct pwm_chip *chip, struct pwm_device *pwm,
					      struct pwm_capture *capture_res)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 tmp;

	tmp = readl_relaxed(pc->base + pc->data->regs.hpr);
	tmp *= pc->data->prescaler * NSEC_PER_SEC;
	capture_res->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	tmp = readl_relaxed(pc->base + pc->data->regs.lpr);
	tmp *= pc->data->prescaler * NSEC_PER_SEC;
	capture_res->period =  DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate) + capture_res->duty_cycle;

	if (!capture_res->duty_cycle || !capture_res->period)
		return -EINVAL;

	writel_relaxed(0, pc->base + pc->data->regs.hpr);
	writel_relaxed(0, pc->base + pc->data->regs.lpr);

	return 0;
}

static u8 rockchip_pwm_get_capture_cnt(struct rockchip_pwm_chip *pc)
{
	return pc->capture_cnt;
}

static int rockchip_pwm_capture(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_capture *capture_res, unsigned long timeout_ms)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	struct pwm_state curstate;
	u8 capture_cnt;
	int ret = 0;

	if (!pc->data->funcs.set_capture || !pc->data->funcs.get_capture_result) {
		dev_err(chip->dev, "Unsupported capture mode\n");
		return -EINVAL;
	}

	pwm_get_state(pwm, &curstate);
	if (curstate.enabled) {
		dev_err(chip->dev, "Failed to enable capture mode because PWM%d is busy\n",
			pc->channel_id);
		return -EBUSY;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	pc->data->funcs.set_capture(chip, pwm, true);
	ret = pc->data->funcs.enable(chip, pwm, true);
	if (ret) {
		dev_err(chip->dev, "Failed to enable capture mode\n");
		goto err_disable_pclk;
	}

	ret = readx_poll_timeout(rockchip_pwm_get_capture_cnt, pc, capture_cnt,
				 capture_cnt > 3, 0, timeout_ms * 1000);
	if (!ret) {
		dev_err(chip->dev, "Failed to wait for LPR/HPR interrupt\n");
		ret = -ETIMEDOUT;
	} else {
		ret = pc->data->funcs.get_capture_result(chip, pwm, capture_res);
		if (ret)
			dev_err(chip->dev, "Failed to get capture result\n");
	}

	pc->data->funcs.enable(chip, pwm, false);
	pc->data->funcs.set_capture(chip, pwm, false);

err_disable_pclk:
	clk_disable(pc->pclk);

	return ret;
}

static int rockchip_pwm_set_counter_v4(struct pwm_chip *chip, struct pwm_device *pwm,
				       bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 arbiter = 0;
	u32 channel_sel = 0;
	u32 val;

	if (enable) {
		arbiter = BIT(pc->channel_id) << COUNTER_READ_LOCK_SHIFT |
			  BIT(pc->channel_id) << COUNTER_GRANT_SHIFT,
		channel_sel = pc->channel_id;
	}

	writel_relaxed(arbiter, pc->base + COUNTER_ARBITER);
	if (enable) {
		val = readl_relaxed(pc->base + COUNTER_ARBITER);
		if (!(val & arbiter))
			return -EINVAL;
	}

	writel_relaxed(COUNTER_EN(enable) | COUNTER_CHANNEL_SEL(channel_sel),
		       pc->base + COUNTER_CTRL);

	return 0;
}

int rockchip_pwm_set_counter(struct pwm_device *pwm, bool enable)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	struct pwm_state curstate;
	int ret = 0;

	if (!pwm)
		return -EINVAL;

	chip = pwm->chip;
	pc = to_rockchip_pwm_chip(chip);

	if (!pc->counter_support ||
	    !pc->data->funcs.set_counter || !pc->data->funcs.get_counter_result) {
		dev_err(chip->dev, "Unsupported counter mode\n");
		return -EINVAL;
	}

	pwm_get_state(pwm, &curstate);
	if (curstate.enabled) {
		dev_err(chip->dev, "Failed to enable counter mode because PWM%d is busy\n",
			pc->channel_id);
		return -EBUSY;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = pc->data->funcs.set_counter(chip, pwm, enable);
	if (ret) {
		dev_err(chip->dev, "Failed to abtain counter arbitration for PWM%d\n",
			pc->channel_id);
		goto err_disable_pclk;
	}

err_disable_pclk:
	clk_disable(pc->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pwm_set_counter);

static int rockchip_pwm_get_counter_result_v4(struct pwm_chip *chip, struct pwm_device *pwm,
					      unsigned long *counter_res, bool is_clear)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 low, high;

	low = readl_relaxed(pc->base + COUNTER_LOW);
	high = readl_relaxed(pc->base + COUNTER_HIGH);

	*counter_res = (high << 32) | low;
	if (!*counter_res)
		return -EINVAL;

	if (is_clear)
		writel_relaxed(COUNTER_CLR(true), pc->base + COUNTER_CTRL);

	return 0;
}

int rockchip_pwm_get_counter_result(struct pwm_device *pwm,
				    unsigned long *counter_res, bool is_clear)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	int ret = 0;

	if (!pwm || !counter_res)
		return -EINVAL;

	chip = pwm->chip;
	pc = to_rockchip_pwm_chip(chip);

	if (!pc->counter_support ||
	    !pc->data->funcs.set_counter || !pc->data->funcs.get_counter_result) {
		dev_err(chip->dev, "Unsupported counter mode\n");
		return -EINVAL;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = pc->data->funcs.get_counter_result(chip, pwm, counter_res, is_clear);
	if (ret) {
		dev_err(chip->dev, "Failed to get counter result for PWM%d\n",
			pc->channel_id);
		goto err_disable_pclk;
	}

err_disable_pclk:
	clk_disable(pc->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pwm_get_counter_result);

static int rockchip_pwm_set_freq_meter_v4(struct pwm_chip *chip, struct pwm_device *pwm,
					  bool enable, unsigned long delay_ms)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 div = 0;
	u64 timer_val = 0;
	u32 arbiter = 0;
	u32 channel_sel = 0;
	u32 val;

	if (enable) {
		arbiter = BIT(pc->channel_id) << FREQ_READ_LOCK_SHIFT |
			  BIT(pc->channel_id) << FREQ_GRANT_SHIFT;
		channel_sel = pc->channel_id;

		div = (u64)pc->clk_rate * delay_ms;
		timer_val = DIV_ROUND_CLOSEST_ULL(div, MSEC_PER_SEC);
	}

	writel_relaxed(arbiter, pc->base + FREQ_ARBITER);
	if (enable) {
		val = readl_relaxed(pc->base + FREQ_ARBITER);
		if (!(val & arbiter))
			return -EINVAL;
	}

	writel_relaxed(FREQ_INT_EN(enable), pc->base + pc->data->regs.int_en);
	writel_relaxed(timer_val, pc->base + FREQ_TIMER_VALUE);
	writel_relaxed(FREQ_EN(enable) | FREQ_CHANNEL_SEL(channel_sel),
		       pc->base + FREQ_CTRL);

	return 0;
}

static int rockchip_pwm_get_freq_meter_result_v4(struct pwm_chip *chip, struct pwm_device *pwm,
						 unsigned long delay_ms, unsigned long *freq_hz)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	int ret;
	u32 val;

	ret = readl_relaxed_poll_timeout(pc->base + pc->data->regs.intsts, val, val & FREQ_INT,
					 0, delay_ms * 1000);
	if (!ret) {
		dev_err(chip->dev, "failed to wait for freq_meter interrupt\n");
		return -ETIMEDOUT;
	}

	*freq_hz = readl_relaxed(pc->base + FREQ_RESULT_VALUE);
	if (!*freq_hz)
		return -EINVAL;

	return 0;
}

int rockchip_pwm_set_freq_meter(struct pwm_device *pwm, unsigned long delay_ms,
				unsigned long *freq_hz)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	struct pwm_state curstate;
	int ret = 0;

	if (!pwm || !freq_hz)
		return -EINVAL;

	chip = pwm->chip;
	pc = to_rockchip_pwm_chip(chip);

	if (!pc->freq_meter_support ||
	    !pc->data->funcs.set_freq_meter || !pc->data->funcs.get_freq_meter_result) {
		dev_err(chip->dev, "Unsupported frequency meter mode\n");
		return -EINVAL;
	}

	pwm_get_state(pwm, &curstate);
	if (curstate.enabled) {
		dev_err(chip->dev, "Failed to enable frequency meter mode because PWM%d is busy\n",
			pc->channel_id);
		return -EBUSY;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = pc->data->funcs.set_freq_meter(chip, pwm, true, delay_ms);
	if (ret) {
		dev_err(chip->dev, "Failed to abtain frequency meter arbitration for PWM%d\n",
			pc->channel_id);
	} else {
		ret = pc->data->funcs.get_freq_meter_result(chip, pwm, delay_ms, freq_hz);
		if (ret) {
			dev_err(chip->dev, "Failed to get frequency meter result for PWM%d\n",
				pc->channel_id);
		}
	}
	pc->data->funcs.set_freq_meter(chip, pwm, false, 0);

	clk_disable(pc->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pwm_set_freq_meter);

static int rockchip_pwm_global_ctrl_v4(struct pwm_chip *chip, struct pwm_device *pwm,
				       enum rockchip_pwm_global_ctrl_cmd cmd)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 arbiter = 0;
	u32 val = 0;

	switch (cmd) {
	case PWM_GLOBAL_CTRL_JOIN:
		writel_relaxed(PWM_GLOBAL_JOIN_EN(true), pc->base + pc->data->regs.enable);
		writel_relaxed(CLK_GLOBAL_SEL(true), pc->base + pc->data->regs.clk_ctrl);
		break;
	case PWM_GLOBAL_CTRL_EXIT:
		writel_relaxed(PWM_GLOBAL_JOIN_EN(false), pc->base + pc->data->regs.enable);
		writel_relaxed(CLK_GLOBAL_SEL(false), pc->base + pc->data->regs.clk_ctrl);
		break;
	case PWM_GLOBAL_CTRL_GRANT:
		arbiter = BIT(pc->channel_id) << GLOBAL_READ_LOCK_SHIFT |
			  BIT(pc->channel_id) << GLOBAL_GRANT_SHIFT;

		writel_relaxed(arbiter, pc->base + GLOBAL_ARBITER);
		val = readl_relaxed(pc->base + GLOBAL_ARBITER);
		if (!(val & arbiter)) {
			dev_err(chip->dev, "Failed to abtain global ctrl arbitration for PWM%d\n",
				pc->channel_id);
			return -EINVAL;
		}

		pc->global_ctrl_grant = true;
		break;
	case PWM_GLOBAL_CTRL_RECLAIM:
		writel_relaxed(0, pc->base + GLOBAL_ARBITER);

		pc->global_ctrl_grant = false;
		break;
	case PWM_GLOBAL_CTRL_UPDATE:
		if (!pc->global_ctrl_grant) {
			dev_err(chip->dev, "CMD %d: get global ctrl arbitration first for PWM%d\n",
				cmd, pc->channel_id);
			return -EINVAL;
		}

		writel_relaxed(GLOBAL_PWM_UPDATE_EN(true), pc->base + GLOBAL_CTRL);
		break;
	case PWM_GLOBAL_CTRL_ENABLE:
		if (!pc->global_ctrl_grant) {
			dev_err(chip->dev, "CMD %d: get global ctrl arbitration first for PWM%d\n",
				cmd, pc->channel_id);
			return -EINVAL;
		}

		writel_relaxed(PWM_CLK_EN(true), pc->base + pc->data->regs.enable);
		writel_relaxed(GLOBAL_PWM_EN(true), pc->base + GLOBAL_CTRL);
		break;
	case PWM_GLOBAL_CTRL_DISABLE:
		if (!pc->global_ctrl_grant) {
			dev_err(chip->dev, "CMD %d: get global ctrl arbitration first for PWM%d\n",
				cmd, pc->channel_id);
			return -EINVAL;
		}

		writel_relaxed(PWM_CLK_EN(false), pc->base + pc->data->regs.enable);
		writel_relaxed(GLOBAL_PWM_EN(false), pc->base + GLOBAL_CTRL);
		break;
	default:
		dev_err(chip->dev, "Unsupported global ctrl cmd %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

int rockchip_pwm_global_ctrl(struct pwm_device *pwm, enum rockchip_pwm_global_ctrl_cmd cmd)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	struct pwm_state curstate;
	int ret = 0;

	if (!pwm)
		return -EINVAL;

	chip = pwm->chip;
	pc = to_rockchip_pwm_chip(chip);

	if (!pc->data->funcs.global_ctrl) {
		dev_err(chip->dev, "Unsupported global control\n");
		return -EINVAL;
	}

	pwm_get_state(pwm, &curstate);
	if (curstate.enabled) {
		dev_err(chip->dev, "Failed to execute global ctrl cmd %d because PWM%d is busy\n",
			cmd, pc->channel_id);
		return -EBUSY;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = pc->data->funcs.global_ctrl(chip, pwm, cmd);
	if (ret) {
		dev_err(chip->dev, "Failed to execute global ctrl cmd %d for PWM%d\n",
			cmd, pc->channel_id);
		goto err_disable_pclk;
	}

err_disable_pclk:
	clk_disable(pc->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pwm_global_ctrl);

static int rockchip_pwm_set_wave_table_v4(struct pwm_chip *chip, struct pwm_device *pwm,
					  struct rockchip_pwm_wave_table *table_config,
					  enum rockchip_pwm_wave_table_width_mode width_mode)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 table_val = 0;
	u64 div = 0;
	u32 arbiter = 0;
	u32 val;
	u16 table_max;
	int i;

	if (width_mode == PWM_WAVE_TABLE_16BITS_WIDTH)
		table_max = pc->data->wave_table_max / 2;
	else
		table_max = pc->data->wave_table_max;

	if (!table_config->table ||
	    table_config->offset > table_max || table_config->len > table_max) {
		dev_err(chip->dev, "The wave table to set is out of range for PWM%d\n",
			pc->channel_id);
		return -EINVAL;
	}

	arbiter = BIT(pc->channel_id) << WAVE_MEM_GRANT_SHIFT |
		  BIT(pc->channel_id) << WAVE_MEM_READ_LOCK_SHIFT;
	writel_relaxed(arbiter, pc->base + WAVE_MEM_ARBITER);

	val = readl_relaxed(pc->base + WAVE_MEM_ARBITER);
	if (!(val & arbiter)) {
		dev_err(chip->dev, "Failed to abtain wave memory arbitration for PWM%d\n",
			pc->channel_id);
		return -EINVAL;
	}

	if (width_mode == PWM_WAVE_TABLE_16BITS_WIDTH) {
		for (i = 0; i < table_config->len; i++) {
			div = (u64)pc->clk_rate * table_config->table[i];
			table_val = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);
			writel_relaxed(table_val & 0xff,
				       pc->base + WAVE_MEM + (table_config->offset + i) * 2 * 4);
			if (readl_poll_timeout(pc->base + WAVE_MEM_STATUS,
					       val, (val & BIT(WAVE_MEM_STATUS_SHIFT)),
					       1000, 10 * 1000)) {
				dev_err(chip->dev,
					"Wait for wave mem(offset 0x%08x) to update failed\n",
					(table_config->offset + i) * 2 * 4);
				return -ETIMEDOUT;
			}

			writel_relaxed((table_val >> 8) & 0xff,
				       pc->base + WAVE_MEM +
				       ((table_config->offset + i) * 2 + 1) * 4);
			if (readl_poll_timeout(pc->base + WAVE_MEM_STATUS,
					       val, (val & BIT(WAVE_MEM_STATUS_SHIFT)),
					       1000, 10 * 1000)) {
				dev_err(chip->dev,
					"Wait for wave mem(offset 0x%08x) to update failed\n",
					((table_config->offset + i) * 2 + 1) * 4);
				return -ETIMEDOUT;
			}
		}
	} else {
		for (i = 0; i < table_config->len; i++) {
			div = (u64)pc->clk_rate * table_config->table[i];
			table_val = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);
			writel_relaxed(table_val,
				       pc->base + WAVE_MEM + (table_config->offset + i) * 4);
			if (readl_poll_timeout(pc->base + WAVE_MEM_STATUS,
					       val, (val & BIT(WAVE_MEM_STATUS_SHIFT)),
					       1000, 10 * 1000)) {
				dev_err(chip->dev,
					"Wait for wave mem(offset 0x%08x) to update failed\n",
					(table_config->offset + i) * 4);
				return -ETIMEDOUT;
			}
		}
	}

	writel_relaxed(0, pc->base + WAVE_MEM_ARBITER);

	return 0;
}

static int rockchip_pwm_set_wave_v4(struct pwm_chip *chip, struct pwm_device *pwm,
				    struct rockchip_pwm_wave_config *config)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 ctrl = 0;
	u32 max_val = 0;
	u32 min_val = 0;
	u32 offset = 0;
	u32 middle = 0;
	u32 rpt = 0;
	u8 factor = 0;

	if (config->enable) {
		/*
		 * If the width mode is 16-bits mode, two 8-bits table units
		 * are combined into one 16-bits unit.
		 */
		if (config->width_mode == PWM_WAVE_TABLE_16BITS_WIDTH)
			factor = 2;
		else
			factor = 1;

		ctrl = WAVE_DUTY_EN(config->duty_en) |
		       WAVE_PERIOD_EN(config->period_en) |
		       WAVE_WIDTH_MODE(config->width_mode) |
		       WAVE_UPDATE_MODE(config->update_mode);
		max_val = config->duty_max * factor << WAVE_DUTY_MAX_SHIFT |
			  config->period_max * factor << WAVE_PERIOD_MAX_SHIFT;
		min_val = config->duty_min * factor << WAVE_DUTY_MIN_SHIFT |
			  config->period_min * factor << WAVE_PERIOD_MIN_SHIFT;
		offset = config->offset * factor << WAVE_OFFSET_SHIFT;
		middle = config->middle * factor << WAVE_MIDDLE_SHIFT;

		rpt = config->rpt << FIRST_DIMENSIONAL_SHIFT;
	} else {
		ctrl = WAVE_DUTY_EN(false) | WAVE_PERIOD_EN(false);
	}

	writel_relaxed(ctrl, pc->base + WAVE_CTRL);
	writel_relaxed(max_val, pc->base + WAVE_MAX);
	writel_relaxed(min_val, pc->base + WAVE_MIN);
	writel_relaxed(offset, pc->base + WAVE_OFFSET);
	writel_relaxed(middle, pc->base + WAVE_MIDDLE);

	writel_relaxed(rpt, pc->base + pc->data->regs.rpt);
	writel_relaxed(WAVE_MAX_INT_EN(config->enable) | WAVE_MIDDLE_INT_EN(config->enable),
		       pc->base + pc->data->regs.int_en);

	pc->wave_en = config->enable;

	return 0;
}

int rockchip_pwm_set_wave(struct pwm_device *pwm, struct rockchip_pwm_wave_config *config)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	int ret = 0;

	if (!pwm || !config)
		return -EINVAL;

	chip = pwm->chip;
	pc = to_rockchip_pwm_chip(chip);

	if (!pc->wave_support ||
	    !pc->data->funcs.set_wave_table || !pc->data->funcs.set_wave) {
		dev_err(chip->dev, "Unsupported wave generator mode\n");
		return -EINVAL;
	}

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	if (config->duty_table) {
		ret = pc->data->funcs.set_wave_table(chip, pwm, config->duty_table,
						     config->width_mode);
		if (ret) {
			dev_err(chip->dev, "Failed to set wave duty table for PWM%d\n",
				pc->channel_id);
			goto err_disable_pclk;
		}
	}

	if (config->period_table) {
		ret = pc->data->funcs.set_wave_table(chip, pwm, config->period_table,
						     config->width_mode);
		if (ret) {
			dev_err(chip->dev, "Failed to set wave period table for PWM%d\n",
				pc->channel_id);
			goto err_disable_pclk;
		}
	}

	ret = pc->data->funcs.set_wave(chip, pwm, config);
	if (ret) {
		dev_err(chip->dev, "Failed to set wave generator for PWM%d\n", pc->channel_id);
		goto err_disable_pclk;
	}

err_disable_pclk:
	clk_disable(pc->pclk);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pwm_set_wave);

#ifdef CONFIG_DEBUG_FS
static int rockchip_pwm_debugfs_show(struct seq_file *s, void *data)
{
	struct rockchip_pwm_chip *pc = s->private;
	u32 regs_start;
	int i;
	int ret = 0;

	if (!pc->oneshot_en) {
		ret = clk_enable(pc->pclk);
		if (ret)
			return ret;
	}

	regs_start = (u32)pc->res->start - pc->channel_id * 0x10;
	for (i = 0; i < 0x40; i += 4) {
		seq_printf(s, "%08x:  %08x %08x %08x %08x\n", regs_start + i * 4,
			   readl_relaxed(pc->base + (4 * i)),
			   readl_relaxed(pc->base + (4 * (i + 1))),
			   readl_relaxed(pc->base + (4 * (i + 2))),
			   readl_relaxed(pc->base + (4 * (i + 3))));
	}

	if (!pc->oneshot_en)
		clk_disable(pc->pclk);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(rockchip_pwm_debugfs);

static inline void rockchip_pwm_debugfs_init(struct rockchip_pwm_chip *pc)
{
	pc->debugfs = debugfs_create_file(dev_name(pc->chip.dev),
					  S_IFREG | 0444, NULL, pc,
					  &rockchip_pwm_debugfs_fops);
}

static inline void rockchip_pwm_debugfs_deinit(struct rockchip_pwm_chip *pc)
{
	debugfs_remove(pc->debugfs);
}
#else
static inline void rockchip_pwm_debugfs_init(struct rockchip_pwm_chip *pc)
{
}

static inline void rockchip_pwm_debugfs_deinit(struct rockchip_pwm_chip *pc)
{
}
#endif

static const struct pwm_ops rockchip_pwm_ops = {
	.capture = rockchip_pwm_capture,
	.apply = rockchip_pwm_apply,
	.get_state = rockchip_pwm_get_state,
	.owner = THIS_MODULE,
};

static const struct rockchip_pwm_data pwm_data_v1 = {
	.main_version = 0x01,
	.regs = {
		.version = 0x5c,
		.duty = 0x04,
		.period = 0x08,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 2,
	.supports_polarity = false,
	.supports_lock = false,
	.vop_pwm = false,
	.enable_conf = PWM_CTRL_OUTPUT_EN | PWM_CTRL_TIMER_EN,
	.enable_conf_mask = BIT(1) | BIT(3),
	.oneshot_cnt_max = 0x100,
	.funcs = {
		.enable = rockchip_pwm_enable_v1,
		.config = rockchip_pwm_config_v1,
	},
};

static const struct rockchip_pwm_data pwm_data_v2 = {
	.main_version = 0x02,
	.regs = {
		.version = 0x5c,
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = false,
	.vop_pwm = false,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
	.enable_conf_mask = GENMASK(2, 0) | BIT(5) | BIT(8),
	.oneshot_cnt_max = 0x100,
	.funcs = {
		.enable = rockchip_pwm_enable_v1,
		.config = rockchip_pwm_config_v1,
	},
};

static const struct rockchip_pwm_data pwm_data_vop = {
	.main_version = 0x02,
	.regs = {
		.version = 0x5c,
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x0c,
		.ctrl = 0x00,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = false,
	.vop_pwm = true,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
	.enable_conf_mask = GENMASK(2, 0) | BIT(5) | BIT(8),
	.oneshot_cnt_max = 0x100,
	.funcs = {
		.enable = rockchip_pwm_enable_v1,
		.config = rockchip_pwm_config_v1,
	},
};

static const struct rockchip_pwm_data pwm_data_v3 = {
	.main_version = 0x03,
	.regs = {
		.version = 0x5c,
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = true,
	.vop_pwm = false,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
	.enable_conf_mask = GENMASK(2, 0) | BIT(5) | BIT(8),
	.oneshot_cnt_max = 0x100,
	.funcs = {
		.enable = rockchip_pwm_enable_v1,
		.config = rockchip_pwm_config_v1,
		.irq_handler = rockchip_pwm_irq_v1,
	},
};

static const struct rockchip_pwm_data pwm_data_v4 = {
	.main_version = 0x04,
	.regs = {
		.version = 0x0,
		.enable = 0x4,
		.clk_ctrl = 0x8,
		.ctrl = 0xc,
		.period = 0x10,
		.duty = 0x14,
		.offset = 0x18,
		.rpt = 0x1c,
		.hpr = 0x2c,
		.lpr = 0x30,
		.intsts = 0x70,
		.int_en = 0x74,
		.int_mask = 0x78,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = true,
	.vop_pwm = false,
	.oneshot_cnt_max = 0x10000,
	.oneshot_rpt_max = 0x10000,
	.wave_table_max = 0x300,
	.enable_conf = PWM_ENABLE_V4,
	.funcs = {
		.enable = rockchip_pwm_enable_v4,
		.config = rockchip_pwm_config_v4,
		.set_capture = rockchip_pwm_set_capture_v4,
		.get_capture_result = rockchip_pwm_get_capture_result_v4,
		.set_counter = rockchip_pwm_set_counter_v4,
		.get_counter_result = rockchip_pwm_get_counter_result_v4,
		.set_freq_meter = rockchip_pwm_set_freq_meter_v4,
		.get_freq_meter_result = rockchip_pwm_get_freq_meter_result_v4,
		.global_ctrl = rockchip_pwm_global_ctrl_v4,
		.set_wave_table = rockchip_pwm_set_wave_table_v4,
		.set_wave = rockchip_pwm_set_wave_v4,
		.irq_handler = rockchip_pwm_irq_v4,
	},
};

static const struct of_device_id rockchip_pwm_dt_ids[] = {
	{ .compatible = "rockchip,rk2928-pwm", .data = &pwm_data_v1},
	{ .compatible = "rockchip,rk3288-pwm", .data = &pwm_data_v2},
	{ .compatible = "rockchip,vop-pwm", .data = &pwm_data_vop},
	{ .compatible = "rockchip,rk3328-pwm", .data = &pwm_data_v3},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_pwm_dt_ids);

static int rockchip_pwm_get_channel_id(const char *name)
{
	int len = strlen(name);

	return name[len - 2] - '0';
}

static int rockchip_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct rockchip_pwm_chip *pc;
	struct resource *r;
	u32 enable_conf, ctrl, version;
	bool enabled;
	int ret, count;

	id = of_match_device(rockchip_pwm_dt_ids, &pdev->dev);
	if (!id)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "Failed to get pwm register\n");
		return -EINVAL;
	}
	pc->res = r;

	pc->base = devm_ioremap(&pdev->dev, pc->res->start,
				resource_size(pc->res));
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->clk = devm_clk_get(&pdev->dev, "pwm");
	if (IS_ERR(pc->clk)) {
		pc->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(pc->clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk),
					     "Can't get bus clk\n");
	}

	count = of_count_phandle_with_args(pdev->dev.of_node,
					   "clocks", "#clock-cells");
	if (count == 2)
		pc->pclk = devm_clk_get(&pdev->dev, "pclk");
	else
		pc->pclk = pc->clk;

	if (IS_ERR(pc->pclk)) {
		ret = PTR_ERR(pc->pclk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get APB clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(pc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't prepare enable bus clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(pc->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Can't prepare enable APB clk: %d\n", ret);
		goto err_clk;
	}

	pc->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pc->pinctrl)) {
		dev_err(&pdev->dev, "Get pinctrl failed!\n");
		ret = PTR_ERR(pc->pinctrl);
		goto err_pclk;
	}

	pc->active_state = pinctrl_lookup_state(pc->pinctrl, "active");
	if (IS_ERR(pc->active_state)) {
		dev_err(&pdev->dev, "No active pinctrl state\n");
		ret = PTR_ERR(pc->active_state);
		goto err_pclk;
	}

	platform_set_drvdata(pdev, pc);

	pc->data = id->data;
	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rockchip_pwm_ops;
	pc->chip.base = of_alias_get_id(pdev->dev.of_node, "pwm");
	pc->chip.npwm = 1;
	pc->clk_rate = clk_get_rate(pc->clk);
	pc->main_version = pc->data->main_version;
	if (pc->main_version >= 4) {
		version = readl_relaxed(pc->base + pc->data->regs.version);
		pc->channel_id = (version & CHANNLE_INDEX_MASK) >> CHANNLE_INDEX_SHIFT;
		pc->freq_meter_support = !!(version & FREQ_METER_SUPPORT);
		pc->counter_support = !!(version & COUNTER_SUPPORT);
		pc->wave_support = !!(version & WAVE_SUPPORT);
	} else {
		pc->channel_id = rockchip_pwm_get_channel_id(pdev->dev.of_node->full_name);
	}
	if (pc->channel_id < 0 || pc->channel_id >= PWM_MAX_CHANNEL_NUM) {
		dev_err(&pdev->dev, "Channel id is out of range: %d\n", pc->channel_id);
		ret = -EINVAL;
		goto err_pclk;
	}

	if (pc->data->funcs.irq_handler) {
		if (pc->main_version >= 4) {
			pc->irq = platform_get_irq(pdev, 0);
			if (pc->irq < 0) {
				dev_err(&pdev->dev, "Get irq failed\n");
				ret = pc->irq;
				goto err_pclk;
			}

			ret = devm_request_irq(&pdev->dev, pc->irq, pc->data->funcs.irq_handler,
					       IRQF_NO_SUSPEND, "rk_pwm_irq", pc);
			if (ret) {
				dev_err(&pdev->dev, "Claim IRQ failed\n");
				goto err_pclk;
			}
		} else {
			if (IS_ENABLED(CONFIG_PWM_ROCKCHIP_ONESHOT)) {
				pc->irq = platform_get_irq_optional(pdev, 0);
				if (pc->irq < 0) {
					dev_warn(&pdev->dev,
						 "Can't get oneshot mode irq and oneshot interrupt is unsupported\n");
				} else {
					ret = devm_request_irq(&pdev->dev, pc->irq,
							       pc->data->funcs.irq_handler,
							       IRQF_NO_SUSPEND | IRQF_SHARED,
							       "rk_pwm_oneshot_irq", pc);
					if (ret) {
						dev_err(&pdev->dev, "Claim oneshot IRQ failed\n");
						goto err_pclk;
					}
				}
			}
		}
	}

	if (pc->data->supports_polarity) {
		pc->chip.of_xlate = of_pwm_xlate_with_flags;
		pc->chip.of_pwm_n_cells = 3;
	}

	enable_conf = pc->data->enable_conf;
	if (pc->main_version >= 4)
		ctrl = readl_relaxed(pc->base + pc->data->regs.enable);
	else
		ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	enabled = (ctrl & enable_conf) == enable_conf;

	pc->center_aligned =
		device_property_read_bool(&pdev->dev, "center-aligned");

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		goto err_pclk;
	}

	rockchip_pwm_debugfs_init(pc);

	/* Keep the PWM clk enabled if the PWM appears to be up and running. */
	if (!enabled)
		clk_disable(pc->clk);

	clk_disable(pc->pclk);

	return 0;

err_pclk:
	clk_disable_unprepare(pc->pclk);
err_clk:
	clk_disable_unprepare(pc->clk);

	return ret;
}

static int rockchip_pwm_remove(struct platform_device *pdev)
{
	struct rockchip_pwm_chip *pc = platform_get_drvdata(pdev);
	struct pwm_state state;
	u32 val;

	rockchip_pwm_debugfs_deinit(pc);

	/*
	 * For oneshot mode, it is needed to wait for bit PWM_ENABLE
	 * to 0, which is automatic if all periods have been sent.
	 */
	pwm_get_state(&pc->chip.pwms[0], &state);
	if (state.enabled) {
		if (pc->oneshot_en) {
			if (readl_poll_timeout(pc->base + pc->data->regs.ctrl,
					       val, !(val & PWM_ENABLE), 1000, 10 * 1000))
				dev_err(&pdev->dev, "Wait for oneshot to complete failed\n");
		} else {
			state.enabled = false;
			pwm_apply_state(&pc->chip.pwms[0], &state);
		}
	}

	if (pc->oneshot_en)
		clk_disable(pc->pclk);
	clk_unprepare(pc->pclk);
	clk_unprepare(pc->clk);

	return pwmchip_remove(&pc->chip);
}

static struct platform_driver rockchip_pwm_driver = {
	.driver = {
		.name = "rockchip-pwm",
		.of_match_table = rockchip_pwm_dt_ids,
	},
	.probe = rockchip_pwm_probe,
	.remove = rockchip_pwm_remove,
};
#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
static int __init rockchip_pwm_driver_init(void)
{
	return platform_driver_register(&rockchip_pwm_driver);
}
subsys_initcall(rockchip_pwm_driver_init);

static void __exit rockchip_pwm_driver_exit(void)
{
	platform_driver_unregister(&rockchip_pwm_driver);
}
module_exit(rockchip_pwm_driver_exit);
#else
module_platform_driver(rockchip_pwm_driver);
#endif

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("Rockchip SoC PWM driver");
MODULE_LICENSE("GPL v2");
