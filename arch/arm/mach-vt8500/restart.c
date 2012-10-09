/* linux/arch/arm/mach-vt8500/restart.c
 *
 * Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define LEGACY_PMC_BASE		0xD8130000
#define WMT_PRIZM_PMSR_REG	0x60

static void __iomem *pmc_base;

void wmt_setup_restart(void)
{
	struct device_node *np;

	/*
	 * Check if Power Mgmt Controller node is present in device tree. If no
	 * device tree node, use the legacy PMSR value (valid for all current
	 * SoCs).
	 */
	np = of_find_compatible_node(NULL, NULL, "wmt,prizm-pmc");
	if (np) {
		pmc_base = of_iomap(np, 0);

		if (!pmc_base)
			pr_err("%s:of_iomap(pmc) failed\n", __func__);

		of_node_put(np);
	} else {
		pmc_base = ioremap(LEGACY_PMC_BASE, 0x1000);
		if (!pmc_base) {
			pr_err("%s:ioremap(rstc) failed\n", __func__);
			return;
		}
	}
}

void wmt_restart(char mode, const char *cmd)
{
	if (pmc_base)
		writel(1, pmc_base + WMT_PRIZM_PMSR_REG);
}
