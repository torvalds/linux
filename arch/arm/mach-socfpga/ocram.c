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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include "core.h"

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

/* Arria10 OCRAM Section */
#define ALTR_A10_ECC_CTRL_OFST          0x08
#define ALTR_A10_OCRAM_ECC_EN_CTL       (BIT(1) | BIT(0))
#define ALTR_A10_ECC_INITA              BIT(16)

#define ALTR_A10_ECC_INITSTAT_OFST      0x0C
#define ALTR_A10_ECC_INITCOMPLETEA      BIT(0)
#define ALTR_A10_ECC_INITCOMPLETEB      BIT(8)

#define ALTR_A10_ECC_ERRINTEN_OFST      0x10
#define ALTR_A10_ECC_SERRINTEN          BIT(0)

#define ALTR_A10_ECC_INTSTAT_OFST       0x20
#define ALTR_A10_ECC_SERRPENA           BIT(0)
#define ALTR_A10_ECC_DERRPENA           BIT(8)
#define ALTR_A10_ECC_ERRPENA_MASK       (ALTR_A10_ECC_SERRPENA | \
					 ALTR_A10_ECC_DERRPENA)
/* ECC Manager Defines */
#define A10_SYSMGR_ECC_INTMASK_SET_OFST   0x94
#define A10_SYSMGR_ECC_INTMASK_CLR_OFST   0x98
#define A10_SYSMGR_ECC_INTMASK_OCRAM      BIT(1)

#define ALTR_A10_ECC_INIT_WATCHDOG_10US   10000

static inline void ecc_set_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	value |= bit_mask;
	writel(value, ioaddr);
}

static inline void ecc_clear_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	value &= ~bit_mask;
	writel(value, ioaddr);
}

static inline int ecc_test_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	return (value & bit_mask) ? 1 : 0;
}

/*
 * This function uses the memory initialization block in the Arria10 ECC
 * controller to initialize/clear the entire memory data and ECC data.
 */
static int altr_init_memory_port(void __iomem *ioaddr)
{
	int limit = ALTR_A10_ECC_INIT_WATCHDOG_10US;

	ecc_set_bits(ALTR_A10_ECC_INITA, (ioaddr + ALTR_A10_ECC_CTRL_OFST));
	while (limit--) {
		if (ecc_test_bits(ALTR_A10_ECC_INITCOMPLETEA,
				  (ioaddr + ALTR_A10_ECC_INITSTAT_OFST)))
			break;
		udelay(1);
	}
	if (limit < 0)
		return -EBUSY;

	/* Clear any pending ECC interrupts */
	writel(ALTR_A10_ECC_ERRPENA_MASK,
	       (ioaddr + ALTR_A10_ECC_INTSTAT_OFST));

	return 0;
}

void socfpga_init_arria10_ocram_ecc(void)
{
	struct device_node *np;
	int ret = 0;
	void __iomem *ecc_block_base;

	if (!sys_manager_base_addr) {
		pr_err("SOCFPGA: sys-mgr is not initialized\n");
		return;
	}

	/* Find the OCRAM EDAC device tree node */
	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-a10-ocram-ecc");
	if (!np) {
		pr_err("Unable to find socfpga-a10-ocram-ecc\n");
		return;
	}

	/* Map the ECC Block */
	ecc_block_base = of_iomap(np, 0);
	of_node_put(np);
	if (!ecc_block_base) {
		pr_err("Unable to map OCRAM ECC block\n");
		return;
	}

	/* Disable ECC */
	writel(ALTR_A10_OCRAM_ECC_EN_CTL,
	       sys_manager_base_addr + A10_SYSMGR_ECC_INTMASK_SET_OFST);
	ecc_clear_bits(ALTR_A10_ECC_SERRINTEN,
		       (ecc_block_base + ALTR_A10_ECC_ERRINTEN_OFST));
	ecc_clear_bits(ALTR_A10_OCRAM_ECC_EN_CTL,
		       (ecc_block_base + ALTR_A10_ECC_CTRL_OFST));

	/* Ensure all writes complete */
	wmb();

	/* Use HW initialization block to initialize memory for ECC */
	ret = altr_init_memory_port(ecc_block_base);
	if (ret) {
		pr_err("ECC: cannot init OCRAM PORTA memory\n");
		goto exit;
	}

	/* Enable ECC */
	ecc_set_bits(ALTR_A10_OCRAM_ECC_EN_CTL,
		     (ecc_block_base + ALTR_A10_ECC_CTRL_OFST));
	ecc_set_bits(ALTR_A10_ECC_SERRINTEN,
		     (ecc_block_base + ALTR_A10_ECC_ERRINTEN_OFST));
	writel(ALTR_A10_OCRAM_ECC_EN_CTL,
	       sys_manager_base_addr + A10_SYSMGR_ECC_INTMASK_CLR_OFST);

	/* Ensure all writes complete */
	wmb();
exit:
	iounmap(ecc_block_base);
}
