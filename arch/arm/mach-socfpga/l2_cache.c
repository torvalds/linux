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

#include "core.h"

/* A10 System Manager L2 ECC Control register */
#define A10_MPU_CTRL_L2_ECC_OFST          0x0
#define A10_MPU_CTRL_L2_ECC_EN            BIT(0)

/* A10 System Manager Global IRQ Mask register */
#define A10_SYSMGR_ECC_INTMASK_CLR_OFST   0x98
#define A10_SYSMGR_ECC_INTMASK_CLR_L2     BIT(0)

/* A10 System Manager L2 ECC IRQ Clear register */
#define A10_SYSMGR_MPU_CLEAR_L2_ECC_OFST  0xA8
#define A10_SYSMGR_MPU_CLEAR_L2_ECC       (BIT(31) | BIT(15))

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

void socfpga_init_arria10_l2_ecc(void)
{
	struct device_node *np;
	void __iomem *mapped_l2_edac_addr;

	/* Find the L2 EDAC device tree node */
	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-a10-l2-ecc");
	if (!np) {
		pr_err("Unable to find socfpga-a10-l2-ecc in dtb\n");
		return;
	}

	mapped_l2_edac_addr = of_iomap(np, 0);
	of_node_put(np);
	if (!mapped_l2_edac_addr) {
		pr_err("Unable to find L2 ECC mapping in dtb\n");
		return;
	}

	if (!sys_manager_base_addr) {
		pr_err("System Manager not mapped for L2 ECC\n");
		goto exit;
	}
	/* Clear any pending IRQs */
	writel(A10_SYSMGR_MPU_CLEAR_L2_ECC, (sys_manager_base_addr +
	       A10_SYSMGR_MPU_CLEAR_L2_ECC_OFST));
	/* Enable ECC */
	writel(A10_SYSMGR_ECC_INTMASK_CLR_L2, sys_manager_base_addr +
	       A10_SYSMGR_ECC_INTMASK_CLR_OFST);
	writel(A10_MPU_CTRL_L2_ECC_EN, mapped_l2_edac_addr +
	       A10_MPU_CTRL_L2_ECC_OFST);
exit:
	iounmap(mapped_l2_edac_addr);
}
