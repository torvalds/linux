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
#include <linux/time.h>
#include "pwm-rockchip.h"

#define PWM_MAX_CHANNEL_NUM	4

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
#define PWM_ONESHOT_COUNT_MAX	256

#define PWM_REG_INTSTS(n)	((3 - (n)) * 0x10 + 0x10)
#define PWM_REG_INT_EN(n)	((3 - (n)) * 0x10 + 0x14)

#define PWM_CH_INT(n)		BIT(n)

struct rockchip_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	struct clk *pclk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *active_state;
	const struct rockchip_pwm_data *data;
	struct resource *res;
	struct dentry *debugfs;
	void __iomem *base;
	unsigned long clk_rate;
	bool vop_pwm_en; /* indicate voppwm mirror register state */
	bool center_aligned;
	bool oneshot_en;
	int channel_id;
	int irq;
};

struct rockchip_pwm_regs {
	unsigned long duty;
	unsigned long period;
	unsigned long cntr;
	unsigned long ctrl;
};

struct rockchip_pwm_data {
	struct rockchip_pwm_regs regs;
	unsigned int prescaler;
	bool supports_polarity;
	bool supports_lock;
	bool vop_pwm;
	u32 enable_conf;
	u32 enable_conf_mask;
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
	u32 dclk_div;
	int ret;

	if (!pc->oneshot_en) {
		ret = clk_enable(pc->pclk);
		if (ret)
			return;
	}

	dclk_div = pc->oneshot_en ? 2 : 1;

	tmp = readl_relaxed(pc->base + pc->data->regs.period);
	tmp *= dclk_div * pc->data->prescaler * NSEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	tmp = readl_relaxed(pc->base + pc->data->regs.duty);
	tmp *= dclk_div * pc->data->prescaler * NSEC_PER_SEC;
	state->duty_cycle =  DIV_ROUND_CLOSEST_ULL(tmp, pc->clk_rate);

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);
	if (pc->oneshot_en)
		enable_conf &= ~PWM_CONTINUOUS;
	state->enabled = (val & enable_conf) == enable_conf;

	if (pc->data->supports_polarity && !(val & PWM_DUTY_POSITIVE))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	if (!pc->oneshot_en)
		clk_disable(pc->pclk);
}

static irqreturn_t rockchip_pwm_oneshot_irq(int irq, void *data)
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

static void rockchip_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	unsigned long period, duty, delay_ns;
	unsigned long flags;
	u64 div;
	u32 ctrl;
	u8 dclk_div = 1;

#ifdef CONFIG_PWM_ROCKCHIP_ONESHOT
	if (state->oneshot_count > 0 && state->oneshot_count <= PWM_ONESHOT_COUNT_MAX)
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
	if (state->oneshot_count > 0 && state->oneshot_count <= PWM_ONESHOT_COUNT_MAX) {
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

		int_ctrl = readl_relaxed(pc->base + PWM_REG_INT_EN(pc->channel_id));
		int_ctrl |= PWM_CH_INT(pc->channel_id);
		writel_relaxed(int_ctrl, pc->base + PWM_REG_INT_EN(pc->channel_id));
	} else {
		u32 int_ctrl;

		ctrl &= ~PWM_SCALE_MASK;
		ctrl &= ~PWM_CLK_SEL_MASK;
		ctrl |= PWM_SEL_NO_SCALED_CLOCK;

		if (state->oneshot_count)
			dev_err(chip->dev, "Oneshot_count must be between 1 and 256.\n");

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

static int rockchip_pwm_enable(struct pwm_chip *chip,
			       struct pwm_device *pwm,
			       bool enable)
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
	.get_state = rockchip_pwm_get_state,
	.apply = rockchip_pwm_apply,
	.owner = THIS_MODULE,
};

static const struct rockchip_pwm_data pwm_data_v1 = {
	.regs = {
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
};

static const struct rockchip_pwm_data pwm_data_v2 = {
	.regs = {
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
};

static const struct rockchip_pwm_data pwm_data_vop = {
	.regs = {
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
};

static const struct rockchip_pwm_data pwm_data_v3 = {
	.regs = {
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
	u32 enable_conf, ctrl;
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

	pc->channel_id = rockchip_pwm_get_channel_id(pdev->dev.of_node->full_name);
	if (pc->channel_id < 0 || pc->channel_id >= PWM_MAX_CHANNEL_NUM) {
		dev_err(&pdev->dev, "Channel id is out of range: %d\n", pc->channel_id);
		ret = -EINVAL;
		goto err_pclk;
	}

	if (IS_ENABLED(CONFIG_PWM_ROCKCHIP_ONESHOT)) {
		pc->irq = platform_get_irq(pdev, 0);
		if (pc->irq < 0) {
			dev_err(&pdev->dev, "Get oneshot mode irq failed\n");
			ret = pc->irq;
			goto err_pclk;
		}

		ret = devm_request_irq(&pdev->dev, pc->irq, rockchip_pwm_oneshot_irq,
				       IRQF_NO_SUSPEND | IRQF_SHARED,
				       "rk_pwm_oneshot_irq", pc);
		if (ret) {
			dev_err(&pdev->dev, "Claim oneshot IRQ failed\n");
			goto err_pclk;
		}
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

	if (pc->data->supports_polarity) {
		pc->chip.of_xlate = of_pwm_xlate_with_flags;
		pc->chip.of_pwm_n_cells = 3;
	}

	enable_conf = pc->data->enable_conf;
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
