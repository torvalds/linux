/*
 * Atmel (Multi-port DDR-)SDRAM Controller driver
 *
 * Copyright (C) 2014 Atmel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

struct at91_ramc_caps {
	bool has_ddrck;
	bool has_mpddr_clk;
};

static const struct at91_ramc_caps at91rm9200_caps = { };

static const struct at91_ramc_caps at91sam9g45_caps = {
	.has_ddrck = 1,
	.has_mpddr_clk = 0,
};

static const struct at91_ramc_caps sama5d3_caps = {
	.has_ddrck = 1,
	.has_mpddr_clk = 1,
};

static const struct of_device_id atmel_ramc_of_match[] = {
	{ .compatible = "atmel,at91rm9200-sdramc", .data = &at91rm9200_caps, },
	{ .compatible = "atmel,at91sam9260-sdramc", .data = &at91rm9200_caps, },
	{ .compatible = "atmel,at91sam9g45-ddramc", .data = &at91sam9g45_caps, },
	{ .compatible = "atmel,sama5d3-ddramc", .data = &sama5d3_caps, },
	{},
};
MODULE_DEVICE_TABLE(of, atmel_ramc_of_match);

static int atmel_ramc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct at91_ramc_caps *caps;
	struct clk *clk;

	match = of_match_device(atmel_ramc_of_match, &pdev->dev);
	caps = match->data;

	if (caps->has_ddrck) {
		clk = devm_clk_get(&pdev->dev, "ddrck");
		if (IS_ERR(clk))
			return PTR_ERR(clk);
		clk_prepare_enable(clk);
	}

	if (caps->has_mpddr_clk) {
		clk = devm_clk_get(&pdev->dev, "mpddr");
		if (IS_ERR(clk)) {
			pr_err("AT91 RAMC: couldn't get mpddr clock\n");
			return PTR_ERR(clk);
		}
		clk_prepare_enable(clk);
	}

	return 0;
}

static struct platform_driver atmel_ramc_driver = {
	.probe		= atmel_ramc_probe,
	.driver		= {
		.name	= "atmel-ramc",
		.of_match_table = atmel_ramc_of_match,
	},
};

static int __init atmel_ramc_init(void)
{
	return platform_driver_register(&atmel_ramc_driver);
}
module_init(atmel_ramc_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@free-electrons.com>");
MODULE_DESCRIPTION("Atmel (Multi-port DDR-)SDRAM Controller");
