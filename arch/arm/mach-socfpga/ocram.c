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
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#define ALTR_OCRAM_CLEAR_ECC          0x00000018
#define ALTR_OCRAM_ECC_EN             0x00000019

void socfpga_init_ocram_ecc(void)
{
	struct device_node *np;
	void __iomem *mapped_ocr_edac_addr;

	/* Find the OCRAM EDAC device tree node */
	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-ocram-ecc");
	if (!np) {
		pr_err("Unable to find socfpga-ocram-ecc\n");
		return;
	}

	mapped_ocr_edac_addr = of_iomap(np, 0);
	of_node_put(np);
	if (!mapped_ocr_edac_addr) {
		pr_err("Unable to map OCRAM ecc regs.\n");
		return;
	}

	/* Clear any pending OCRAM ECC interrupts, then enable ECC */
	writel(ALTR_OCRAM_CLEAR_ECC, mapped_ocr_edac_addr);
	writel(ALTR_OCRAM_ECC_EN, mapped_ocr_edac_addr);

	iounmap(mapped_ocr_edac_addr);
}
