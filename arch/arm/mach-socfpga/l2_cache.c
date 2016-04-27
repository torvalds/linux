/*
 * Copyright Altera Corporation (C) 2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

void socfpga_init_l2_ecc(void)
{
	struct device_node *np;
	void __iomem *mapped_l2_edac_addr;

	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-l2-ecc");
	if (!np) {
		pr_err("Unable to find socfpga-l2-ecc in dtb\n");
		return;
	}

	mapped_l2_edac_addr = of_iomap(np, 0);
	of_node_put(np);
	if (!mapped_l2_edac_addr) {
		pr_err("Unable to find L2 ECC mapping in dtb\n");
		return;
	}

	/* Enable ECC */
	writel(0x01, mapped_l2_edac_addr);
	iounmap(mapped_l2_edac_addr);
}
