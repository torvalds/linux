/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk-provider.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

void socfpga_init_l2_ecc(void)
{
	struct device_node *np;
	void __iomem  *mapped_l2_edac_addr;

	np = of_find_compatible_node(NULL, NULL, "altr,l2-edac");
	if (!np) {
		pr_err("SOCFPGA: Unable to find altr,l2-edac in dtb\n");
		return;
	}

	mapped_l2_edac_addr = of_iomap(np, 0);
	if (!mapped_l2_edac_addr) {
		pr_err("SOCFPGA: Unable to find L2 ECC mapping in dtb\n");
		return;
	}

	/* Enable ECC */
	writel(0x01, mapped_l2_edac_addr);

	pr_alert("SOCFPGA: Success Initializing L2 cache ECC");

	return;
}

