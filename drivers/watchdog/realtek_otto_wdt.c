// SPDX-License-Identifier: GPL-2.0-only

/*
 * Realtek Otto MIPS platform watchdog
 *
 * Watchdog timer that will reset the system after timeout, using the selected
 * reset mode.
 *
 * Counter scaling and timeouts:
 * - Base prescale of (2 << 25), providing tick duration T_0: 168ms @ 200MHz
 * - PRESCALE: logarithmic prescaler adding a factor of {1, 2, 4, 8}
 * - Phase 1: Times out after (PHASE1 + 1) × PRESCALE × T_0
 *   Generates an interrupt, WDT cannot be stopped after phase 1
 * - Phase 2: starts after phase 1, times out after (PHASE2 + 1) × PRESCALE × T_0
 *   Resets the system according to RST_MODE
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

#define OTTO_WDT_REG_CNTR		0x0
#define OTTO_WDT_CNTR_PING		BIT(31)

#define OTTO_WDT_REG_INTR		0x4
#define OTTO_WDT_INTR_PHASE_1		BIT(31)
#define OTTO_WDT_INTR_PHASE_2		BIT(30)

#define OTTO_WDT_REG_CTRL		0x8
#define OTTO_WDT_CTRL_ENABLE		BIT(31)
#define OTTO_WDT_CTRL_PRESCALE		GENMASK(30, 29)
#define OTTO_WDT_CTRL_PHASE1		GENMASK(26, 22)
#define OTTO_WDT_CTRL_PHASE2		GENMASK(19, 15)
#define OTTO_WDT_CTRL_RST_MODE		GENMASK(1, 0)
#define OTTO_WDT_MODE_SOC		0
#define OTTO_WDT_MODE_CPU		1
#define OTTO_WDT_MODE_SOFTWARE		2
#define OTTO_WDT_CTRL_DEFAULT		OTTO_WDT_MODE_CPU

#define OTTO_WDT_PRESCALE_MAX		3

/*
 * One higher than the max values contained in PHASE{1,2}, since a value of 0
 * corresponds to one tick.
 */
#define OTTO_WDT_PHASE_TICKS_MAX	32

/*
 * The maximum reset delay is actually 2×32 ticks, but that would require large
 * pretimeout values for timeouts longer than 32 ticks. Limit the maximum timeout
 * to 32 + 1 to ensure small pretimeout values can be configured as expected.
 */
#define OTTO_WDT_TIMEOUT_TICKS_MAX	(OTTO_WDT_PHASE_TICKS_MAX + 1)

struct otto_wdt_ctrl {
	struct watchdog_device wdev;
	struct device *dev;
	void __iomem *base;
	unsigned int clk_rate_khz;
	int irq_phase1;
};

static int otto_wdt_start(struct watchdog_device *wdev)
{
	struct otto_wdt_ctrl *ctrl = watchdog_get_drvdata(wdev);
	u32 v;

	v = ioread32(ctrl->base + OTTO_WDT_REG_CTRL);
	v |= OTTO_WDT_CTRL_ENABLE;
	iowrite32(v, ctrl->base + OTTO_WDT_REG_CTRL);

	return 0;
}

static int otto_wdt_stop(struct watchdog_device *wdev)
{
	struct otto_wdt_ctrl *ctrl = watchdog_get_drvdata(wdev);
	u32 v;

	v = ioread32(ctrl->base + OTTO_WDT_REG_CTRL);
	v &= ~OTTO_WDT_CTRL_ENABLE;
	iowrite32(v, ctrl->base + OTTO_WDT_REG_CTRL);

	return 0;
}

static int otto_wdt_ping(struct watchdog_device *wdev)
{
	struct otto_wdt_ctrl *ctrl = watchdog_get_drvdata(wdev);

	iowrite32(OTTO_WDT_CNTR_PING, ctrl->base + OTTO_WDT_REG_CNTR);

	return 0;
}

static int otto_wdt_tick_ms(struct otto_wdt_ctrl *ctrl, int prescale)
{
	return DIV_ROUND_CLOSEST(1 << (25 + prescale), ctrl->clk_rate_khz);
}

/*
 * The timer asserts the PHASE1/PHASE2 IRQs when the number of ticks exceeds
 * the value stored in those fields. This means each phase will run for at least
 * one tick, so small values need to be clamped to correctly reflect the timeout.
 */
static inline unsigned int div_round_ticks(unsigned int val, unsigned int tick_duration,
		unsigned int min_ticks)
{
	return max(min_ticks, DIV_ROUND_UP(val, tick_duration));
}

static int otto_wdt_determine_timeouts(struct watchdog_device *wdev, unsigned int timeout,
		unsigned int pretimeout)
{
	struct otto_wdt_ctrl *ctrl = watchdog_get_drvdata(wdev);
	unsigned int pretimeout_ms = pretimeout * 1000;
	unsigned int timeout_ms = timeout * 1000;
	unsigned int prescale_next = 0;
	unsigned int phase1_ticks;
	unsigned int phase2_ticks;
	unsigned int total_ticks;
	unsigned int prescale;
	unsigned int tick_ms;
	u32 v;

	do {
		prescale = prescale_next;
		if (prescale > OTTO_WDT_PRESCALE_MAX)
			return -EINVAL;

		tick_ms = otto_wdt_tick_ms(ctrl, prescale);
		total_ticks = div_round_ticks(timeout_ms, tick_ms, 2);
		phase1_ticks = div_round_ticks(timeout_ms - pretimeout_ms, tick_ms, 1);
		phase2_ticks = total_ticks - phase1_ticks;

		prescale_next++;
	} while (phase1_ticks > OTTO_WDT_PHASE_TICKS_MAX
		|| phase2_ticks > OTTO_WDT_PHASE_TICKS_MAX);

	v = ioread32(ctrl->base + OTTO_WDT_REG_CTRL);

	v &= ~(OTTO_WDT_CTRL_PRESCALE | OTTO_WDT_CTRL_PHASE1 | OTTO_WDT_CTRL_PHASE2);
	v |= FIELD_PREP(OTTO_WDT_CTRL_PHASE1, phase1_ticks - 1);
	v |= FIELD_PREP(OTTO_WDT_CTRL_PHASE2, phase2_ticks - 1);
	v |= FIELD_PREP(OTTO_WDT_CTRL_PRESCALE, prescale);

	iowrite32(v, ctrl->base + OTTO_WDT_REG_CTRL);

	timeout_ms = total_ticks * tick_ms;
	ctrl->wdev.timeout = timeout_ms / 1000;

	pretimeout_ms = phase2_ticks * tick_ms;
	ctrl->wdev.pretimeout = pretimeout_ms / 1000;

	return 0;
}

static int otto_wdt_set_timeout(struct watchdog_device *wdev, unsigned int val)
{
	return otto_wdt_determine_timeouts(wdev, val, min(wdev->pretimeout, val - 1));
}

static int otto_wdt_set_pretimeout(struct watchdog_device *wdev, unsigned int val)
{
	return otto_wdt_determine_timeouts(wdev, wdev->timeout, val);
}

static int otto_wdt_restart(struct watchdog_device *wdev, unsigned long reboot_mode,
		void *data)
{
	struct otto_wdt_ctrl *ctrl = watchdog_get_drvdata(wdev);
	u32 reset_mode;
	u32 v;

	disable_irq(ctrl->irq_phase1);

	switch (reboot_mode) {
	case REBOOT_SOFT:
		reset_mode = OTTO_WDT_MODE_SOFTWARE;
		break;
	case REBOOT_WARM:
		reset_mode = OTTO_WDT_MODE_CPU;
		break;
	default:
		reset_mode = OTTO_WDT_MODE_SOC;
		break;
	}

	/* Configure for shortest timeout and wait for reset to occur */
	v = FIELD_PREP(OTTO_WDT_CTRL_RST_MODE, reset_mode) | OTTO_WDT_CTRL_ENABLE;
	iowrite32(v, ctrl->base + OTTO_WDT_REG_CTRL);

	mdelay(3 * otto_wdt_tick_ms(ctrl, 0));

	return 0;
}

static irqreturn_t otto_wdt_phase1_isr(int irq, void *dev_id)
{
	struct otto_wdt_ctrl *ctrl = dev_id;

	iowrite32(OTTO_WDT_INTR_PHASE_1, ctrl->base + OTTO_WDT_REG_INTR);
	dev_crit(ctrl->dev, "phase 1 timeout\n");
	watchdog_notify_pretimeout(&ctrl->wdev);

	return IRQ_HANDLED;
}

static const struct watchdog_ops otto_wdt_ops = {
	.owner = THIS_MODULE,
	.start = otto_wdt_start,
	.stop = otto_wdt_stop,
	.ping = otto_wdt_ping,
	.set_timeout = otto_wdt_set_timeout,
	.set_pretimeout = otto_wdt_set_pretimeout,
	.restart = otto_wdt_restart,
};

static const struct watchdog_info otto_wdt_info = {
	.identity = "Realtek Otto watchdog timer",
	.options = WDIOF_KEEPALIVEPING |
		WDIOF_MAGICCLOSE |
		WDIOF_SETTIMEOUT |
		WDIOF_PRETIMEOUT,
};

static int otto_wdt_probe_clk(struct otto_wdt_ctrl *ctrl)
{
	struct clk *clk;

	clk = devm_clk_get_enabled(ctrl->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(ctrl->dev, PTR_ERR(clk), "Failed to get clock\n");

	ctrl->clk_rate_khz = clk_get_rate(clk) / 1000;
	if (ctrl->clk_rate_khz == 0)
		return dev_err_probe(ctrl->dev, -ENXIO, "Failed to get clock rate\n");

	return 0;
}

static int otto_wdt_probe_reset_mode(struct otto_wdt_ctrl *ctrl)
{
	static const char *mode_property = "realtek,reset-mode";
	const struct fwnode_handle *node = ctrl->dev->fwnode;
	int mode_count;
	u32 mode;
	u32 v;

	if (!node)
		return -ENXIO;

	mode_count = fwnode_property_string_array_count(node, mode_property);
	if (mode_count < 0)
		return mode_count;
	else if (mode_count == 0)
		return 0;
	else if (mode_count != 1)
		return -EINVAL;

	if (fwnode_property_match_string(node, mode_property, "soc") == 0)
		mode = OTTO_WDT_MODE_SOC;
	else if (fwnode_property_match_string(node, mode_property, "cpu") == 0)
		mode = OTTO_WDT_MODE_CPU;
	else if (fwnode_property_match_string(node, mode_property, "software") == 0)
		mode = OTTO_WDT_MODE_SOFTWARE;
	else
		return -EINVAL;

	v = ioread32(ctrl->base + OTTO_WDT_REG_CTRL);
	v &= ~OTTO_WDT_CTRL_RST_MODE;
	v |= FIELD_PREP(OTTO_WDT_CTRL_RST_MODE, mode);
	iowrite32(v, ctrl->base + OTTO_WDT_REG_CTRL);

	return 0;
}

static int otto_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct otto_wdt_ctrl *ctrl;
	unsigned int max_tick_ms;
	int ret;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->dev = dev;
	ctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	/* Clear any old interrupts and reset initial state */
	iowrite32(OTTO_WDT_INTR_PHASE_1 | OTTO_WDT_INTR_PHASE_2,
			ctrl->base + OTTO_WDT_REG_INTR);
	iowrite32(OTTO_WDT_CTRL_DEFAULT, ctrl->base + OTTO_WDT_REG_CTRL);

	ret = otto_wdt_probe_clk(ctrl);
	if (ret)
		return ret;

	ctrl->irq_phase1 = platform_get_irq_byname(pdev, "phase1");
	if (ctrl->irq_phase1 < 0)
		return ctrl->irq_phase1;

	ret = devm_request_irq(dev, ctrl->irq_phase1, otto_wdt_phase1_isr, 0,
			"realtek-otto-wdt", ctrl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get IRQ for phase1\n");

	ret = otto_wdt_probe_reset_mode(ctrl);
	if (ret)
		return dev_err_probe(dev, ret, "Invalid reset mode specified\n");

	ctrl->wdev.parent = dev;
	ctrl->wdev.info = &otto_wdt_info;
	ctrl->wdev.ops = &otto_wdt_ops;

	/*
	 * Since pretimeout cannot be disabled, min. timeout is twice the
	 * subsystem resolution. Max. timeout is ca. 43s at a bus clock of 200MHz.
	 */
	ctrl->wdev.min_timeout = 2;
	max_tick_ms = otto_wdt_tick_ms(ctrl, OTTO_WDT_PRESCALE_MAX);
	ctrl->wdev.max_hw_heartbeat_ms = max_tick_ms * OTTO_WDT_TIMEOUT_TICKS_MAX;
	ctrl->wdev.timeout = min(30U, ctrl->wdev.max_hw_heartbeat_ms / 1000);

	watchdog_set_drvdata(&ctrl->wdev, ctrl);
	watchdog_init_timeout(&ctrl->wdev, 0, dev);
	watchdog_stop_on_reboot(&ctrl->wdev);
	watchdog_set_restart_priority(&ctrl->wdev, 128);

	ret = otto_wdt_determine_timeouts(&ctrl->wdev, ctrl->wdev.timeout, 1);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set timeout\n");

	return devm_watchdog_register_device(dev, &ctrl->wdev);
}

static const struct of_device_id otto_wdt_ids[] = {
	{ .compatible = "realtek,rtl8380-wdt" },
	{ .compatible = "realtek,rtl8390-wdt" },
	{ .compatible = "realtek,rtl9300-wdt" },
	{ .compatible = "realtek,rtl9310-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, otto_wdt_ids);

static struct platform_driver otto_wdt_driver = {
	.probe = otto_wdt_probe,
	.driver = {
		.name = "realtek-otto-watchdog",
		.of_match_table	= otto_wdt_ids,
	},
};
module_platform_driver(otto_wdt_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek Otto watchdog timer driver");
