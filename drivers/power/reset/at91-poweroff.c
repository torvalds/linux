/*
 * Atmel AT91 SAM9 SoCs reset code
 *
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 * Copyright (C) 2014 Free Electrons
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

#define AT91_SHDW_CR	0x00		/* Shut Down Control Register */
#define AT91_SHDW_SHDW		BIT(0)			/* Shut Down command */
#define AT91_SHDW_KEY		(0xa5 << 24)		/* KEY Password */

#define AT91_SHDW_MR	0x04		/* Shut Down Mode Register */
#define AT91_SHDW_WKMODE0	GENMASK(2, 0)		/* Wake-up 0 Mode Selection */
#define AT91_SHDW_CPTWK0_MAX	0xf			/* Maximum Counter On Wake Up 0 */
#define AT91_SHDW_CPTWK0	(AT91_SHDW_CPTWK0_MAX << 4) /* Counter On Wake Up 0 */
#define AT91_SHDW_CPTWK0_(x)	((x) << 4)
#define AT91_SHDW_RTTWKEN	BIT(16)			/* Real Time Timer Wake-up Enable */
#define AT91_SHDW_RTCWKEN	BIT(17)			/* Real Time Clock Wake-up Enable */

#define AT91_SHDW_SR	0x08		/* Shut Down Status Register */
#define AT91_SHDW_WAKEUP0	BIT(0)			/* Wake-up 0 Status */
#define AT91_SHDW_RTTWK		BIT(16)			/* Real-time Timer Wake-up */
#define AT91_SHDW_RTCWK		BIT(17)			/* Real-time Clock Wake-up [SAM9RL] */

enum wakeup_type {
	AT91_SHDW_WKMODE0_NONE		= 0,
	AT91_SHDW_WKMODE0_HIGH		= 1,
	AT91_SHDW_WKMODE0_LOW		= 2,
	AT91_SHDW_WKMODE0_ANYLEVEL	= 3,
};

static const char *shdwc_wakeup_modes[] = {
	[AT91_SHDW_WKMODE0_NONE]	= "none",
	[AT91_SHDW_WKMODE0_HIGH]	= "high",
	[AT91_SHDW_WKMODE0_LOW]		= "low",
	[AT91_SHDW_WKMODE0_ANYLEVEL]	= "any",
};

static void __iomem *at91_shdwc_base;
static struct clk *sclk;

static void __init at91_wakeup_status(void)
{
	u32 reg = readl(at91_shdwc_base + AT91_SHDW_SR);
	char *reason = "unknown";

	/* Simple power-on, just bail out */
	if (!reg)
		return;

	if (reg & AT91_SHDW_RTTWK)
		reason = "RTT";
	else if (reg & AT91_SHDW_RTCWK)
		reason = "RTC";

	pr_info("AT91: Wake-Up source: %s\n", reason);
}

static void at91_poweroff(void)
{
	writel(AT91_SHDW_KEY | AT91_SHDW_SHDW, at91_shdwc_base + AT91_SHDW_CR);
}

static int at91_poweroff_get_wakeup_mode(struct device_node *np)
{
	const char *pm;
	unsigned int i;
	int err;

	err = of_property_read_string(np, "atmel,wakeup-mode", &pm);
	if (err < 0)
		return AT91_SHDW_WKMODE0_ANYLEVEL;

	for (i = 0; i < ARRAY_SIZE(shdwc_wakeup_modes); i++)
		if (!strcasecmp(pm, shdwc_wakeup_modes[i]))
			return i;

	return -ENODEV;
}

static void at91_poweroff_dt_set_wakeup_mode(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int wakeup_mode;
	u32 mode = 0, tmp;

	wakeup_mode = at91_poweroff_get_wakeup_mode(np);
	if (wakeup_mode < 0) {
		dev_warn(&pdev->dev, "shdwc unknown wakeup mode\n");
		return;
	}

	if (!of_property_read_u32(np, "atmel,wakeup-counter", &tmp)) {
		if (tmp > AT91_SHDW_CPTWK0_MAX) {
			dev_warn(&pdev->dev,
				 "shdwc wakeup counter 0x%x > 0x%x reduce it to 0x%x\n",
				 tmp, AT91_SHDW_CPTWK0_MAX, AT91_SHDW_CPTWK0_MAX);
			tmp = AT91_SHDW_CPTWK0_MAX;
		}
		mode |= AT91_SHDW_CPTWK0_(tmp);
	}

	if (of_property_read_bool(np, "atmel,wakeup-rtc-timer"))
			mode |= AT91_SHDW_RTCWKEN;

	if (of_property_read_bool(np, "atmel,wakeup-rtt-timer"))
			mode |= AT91_SHDW_RTTWKEN;

	writel(wakeup_mode | mode, at91_shdwc_base + AT91_SHDW_MR);
}

static int __init at91_poweroff_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	at91_shdwc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(at91_shdwc_base)) {
		dev_err(&pdev->dev, "Could not map reset controller address\n");
		return PTR_ERR(at91_shdwc_base);
	}

	sclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sclk))
		return PTR_ERR(sclk);

	ret = clk_prepare_enable(sclk);
	if (ret) {
		dev_err(&pdev->dev, "Could not enable slow clock\n");
		return ret;
	}

	at91_wakeup_status();

	if (pdev->dev.of_node)
		at91_poweroff_dt_set_wakeup_mode(pdev);

	pm_power_off = at91_poweroff;

	return 0;
}

static int __exit at91_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == at91_poweroff)
		pm_power_off = NULL;

	clk_disable_unprepare(sclk);

	return 0;
}

static const struct of_device_id at91_poweroff_of_match[] = {
	{ .compatible = "atmel,at91sam9260-shdwc", },
	{ .compatible = "atmel,at91sam9rl-shdwc", },
	{ .compatible = "atmel,at91sam9x5-shdwc", },
	{ /*sentinel*/ }
};

static struct platform_driver at91_poweroff_driver = {
	.remove = __exit_p(at91_poweroff_remove),
	.driver = {
		.name = "at91-poweroff",
		.of_match_table = at91_poweroff_of_match,
	},
};
module_platform_driver_probe(at91_poweroff_driver, at91_poweroff_probe);

MODULE_AUTHOR("Atmel Corporation");
MODULE_DESCRIPTION("Shutdown driver for Atmel SoCs");
MODULE_LICENSE("GPL v2");
