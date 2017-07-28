/*
 * vlv2_plat_clock.c - VLV2 platform clock driver
 * Copyright (C) 2013 Intel Corporation
 *
 * Author: Asutosh Pathak <asutosh.pathak@intel.com>
 * Author: Chandra Sekhar Anagani <chandra.sekhar.anagani@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include "../../include/linux/vlv2_plat_clock.h"

/* NOTE: Most of below constants could come from platform data.
 * To be fixed when appropriate ACPI support comes.
 */
#define VLV2_PMC_CLK_BASE_ADDRESS	0xfed03060
#define PLT_CLK_CTL_OFFSET(x)		(0x04 * (x))

#define CLK_CONFG_BIT_POS		0
#define CLK_CONFG_BIT_LEN		2
#define CLK_CONFG_D3_GATED		0
#define CLK_CONFG_FORCE_ON		1
#define CLK_CONFG_FORCE_OFF		2

#define CLK_FREQ_TYPE_BIT_POS		2
#define CLK_FREQ_TYPE_BIT_LEN		1
#define CLK_FREQ_TYPE_XTAL		0	/* 25 MHz */
#define CLK_FREQ_TYPE_PLL		1	/* 19.2 MHz */

#define MAX_CLK_COUNT			5

/* Helper macros to manipulate bitfields */
#define REG_MASK(n)		(((1 << (n##_BIT_LEN)) - 1) << (n##_BIT_POS))
#define REG_SET_FIELD(r, n, v)	(((r) & ~REG_MASK(n)) | \
				 (((v) << (n##_BIT_POS)) & REG_MASK(n)))
#define REG_GET_FIELD(r, n)	(((r) & REG_MASK(n)) >> n##_BIT_POS)
/*
 * vlv2 platform has 6 platform clocks, controlled by 4 byte registers
 * Total size required for mapping is 6*4 = 24 bytes
 */
#define PMC_MAP_SIZE			24

static DEFINE_MUTEX(clk_mutex);
static void __iomem *pmc_base;

/*
 * vlv2_plat_set_clock_freq - Set clock frequency to a specified platform clock
 * @clk_num: Platform clock number (i.e. 0, 1, 2, ...,5)
 * @freq_type: Clock frequency (0-25 MHz(XTAL), 1-19.2 MHz(PLL) )
 */
int vlv2_plat_set_clock_freq(int clk_num, int freq_type)
{
	void __iomem *addr;

	if (clk_num < 0 || clk_num >= MAX_CLK_COUNT) {
		pr_err("Clock number out of range (%d)\n", clk_num);
		return -EINVAL;
	}

	if (freq_type != CLK_FREQ_TYPE_XTAL &&
	    freq_type != CLK_FREQ_TYPE_PLL) {
		pr_err("wrong clock type\n");
		return -EINVAL;
	}

	if (!pmc_base) {
		pr_err("memio map is not set\n");
		return -EINVAL;
	}

	addr = pmc_base + PLT_CLK_CTL_OFFSET(clk_num);

	mutex_lock(&clk_mutex);
	writel(REG_SET_FIELD(readl(addr), CLK_FREQ_TYPE, freq_type), addr);
	mutex_unlock(&clk_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(vlv2_plat_set_clock_freq);

/*
 * vlv2_plat_get_clock_freq - Get the status of specified platform clock
 * @clk_num: Platform clock number (i.e. 0, 1, 2, ...,5)
 *
 * Returns 0 for 25 MHz(XTAL) and 1 for 19.2 MHz(PLL)
 */
int vlv2_plat_get_clock_freq(int clk_num)
{
	u32 ret;

	if (clk_num < 0 || clk_num >= MAX_CLK_COUNT) {
		pr_err("Clock number out of range (%d)\n", clk_num);
		return -EINVAL;
	}

	if (!pmc_base) {
		pr_err("memio map is not set\n");
		return -EINVAL;
	}

	mutex_lock(&clk_mutex);
	ret = REG_GET_FIELD(readl(pmc_base + PLT_CLK_CTL_OFFSET(clk_num)),
			    CLK_FREQ_TYPE);
	mutex_unlock(&clk_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(vlv2_plat_get_clock_freq);

/*
 * vlv2_plat_configure_clock - Configure the specified platform clock
 * @clk_num: Platform clock number (i.e. 0, 1, 2, ...,5)
 * @conf:      Clock gating:
 *		0   - Clock gated on D3 state
 *		1   - Force on
 *		2,3 - Force off
 */
int vlv2_plat_configure_clock(int clk_num, u32 conf)
{
	void __iomem *addr;

	if (clk_num < 0 || clk_num >= MAX_CLK_COUNT) {
		pr_err("Clock number out of range (%d)\n", clk_num);
		return -EINVAL;
	}

	if (conf != CLK_CONFG_D3_GATED &&
	    conf != CLK_CONFG_FORCE_ON &&
	    conf != CLK_CONFG_FORCE_OFF) {
		pr_err("Invalid clock configuration requested\n");
		return -EINVAL;
	}

	if (!pmc_base) {
		pr_err("memio map is not set\n");
		return -EINVAL;
	}

	addr = pmc_base + PLT_CLK_CTL_OFFSET(clk_num);

	mutex_lock(&clk_mutex);
	writel(REG_SET_FIELD(readl(addr), CLK_CONFG, conf), addr);
	mutex_unlock(&clk_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(vlv2_plat_configure_clock);

/*
 * vlv2_plat_get_clock_status - Get the status of specified platform clock
 * @clk_num: Platform clock number (i.e. 0, 1, 2, ...,5)
 *
 * Returns 1 - On, 0 - Off
 */
int vlv2_plat_get_clock_status(int clk_num)
{
	int ret;

	if (clk_num < 0 || clk_num >= MAX_CLK_COUNT) {
		pr_err("Clock number out of range (%d)\n", clk_num);
		return -EINVAL;
	}

	if (!pmc_base) {
		pr_err("memio map is not set\n");
		return -EINVAL;
	}

	mutex_lock(&clk_mutex);
	ret = (int)REG_GET_FIELD(readl(pmc_base + PLT_CLK_CTL_OFFSET(clk_num)),
				 CLK_CONFG);
	mutex_unlock(&clk_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(vlv2_plat_get_clock_status);

static int vlv2_plat_clk_probe(struct platform_device *pdev)
{
	int i = 0;

	pmc_base = ioremap_nocache(VLV2_PMC_CLK_BASE_ADDRESS, PMC_MAP_SIZE);
	if (!pmc_base) {
		dev_err(&pdev->dev, "I/O memory remapping failed\n");
		return -ENOMEM;
	}

	/* Initialize all clocks as disabled */
	for (i = 0; i < MAX_CLK_COUNT; i++)
		vlv2_plat_configure_clock(i, CLK_CONFG_FORCE_OFF);

	dev_info(&pdev->dev, "vlv2_plat_clk initialized\n");
	return 0;
}

static const struct platform_device_id vlv2_plat_clk_id[] = {
	{"vlv2_plat_clk", 0},
	{}
};

static int vlv2_resume(struct device *device)
{
	int i;

	/* Initialize all clocks as disabled */
	for (i = 0; i < MAX_CLK_COUNT; i++)
		vlv2_plat_configure_clock(i, CLK_CONFG_FORCE_OFF);

	return 0;
}

static int vlv2_suspend(struct device *device)
{
	return 0;
}

static const struct dev_pm_ops vlv2_pm_ops = {
	.suspend = vlv2_suspend,
	.resume = vlv2_resume,
};

static struct platform_driver vlv2_plat_clk_driver = {
	.probe = vlv2_plat_clk_probe,
	.id_table = vlv2_plat_clk_id,
	.driver = {
		.name = "vlv2_plat_clk",
		.pm = &vlv2_pm_ops,
	},
};

static int __init vlv2_plat_clk_init(void)
{
	return platform_driver_register(&vlv2_plat_clk_driver);
}
arch_initcall(vlv2_plat_clk_init);
