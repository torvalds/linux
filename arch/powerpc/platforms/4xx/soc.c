// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IBM/AMCC PPC4xx SoC setup code
 *
 * Copyright 2008 DENX Software Engineering, Stefan Roese <sr@denx.de>
 *
 * L2 cache routines cloned from arch/ppc/syslib/ibm440gx_common.c which is:
 *   Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *   Copyright (c) 2003 - 2006 Zultys Technologies
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/reg.h>
#include <asm/ppc4xx.h>

static u32 dcrbase_l2c;

/*
 * L2-cache
 */

/* Issue L2C diagnostic command */
static inline u32 l2c_diag(u32 addr)
{
	mtdcr(dcrbase_l2c + DCRN_L2C0_ADDR, addr);
	mtdcr(dcrbase_l2c + DCRN_L2C0_CMD, L2C_CMD_DIAG);
	while (!(mfdcr(dcrbase_l2c + DCRN_L2C0_SR) & L2C_SR_CC))
		;

	return mfdcr(dcrbase_l2c + DCRN_L2C0_DATA);
}

static irqreturn_t l2c_error_handler(int irq, void *dev)
{
	u32 sr = mfdcr(dcrbase_l2c + DCRN_L2C0_SR);

	if (sr & L2C_SR_CPE) {
		/* Read cache trapped address */
		u32 addr = l2c_diag(0x42000000);
		printk(KERN_EMERG "L2C: Cache Parity Error, addr[16:26] = 0x%08x\n",
		       addr);
	}
	if (sr & L2C_SR_TPE) {
		/* Read tag trapped address */
		u32 addr = l2c_diag(0x82000000) >> 16;
		printk(KERN_EMERG "L2C: Tag Parity Error, addr[16:26] = 0x%08x\n",
		       addr);
	}

	/* Clear parity errors */
	if (sr & (L2C_SR_CPE | L2C_SR_TPE)){
		mtdcr(dcrbase_l2c + DCRN_L2C0_ADDR, 0);
		mtdcr(dcrbase_l2c + DCRN_L2C0_CMD, L2C_CMD_CCP | L2C_CMD_CTE);
	} else {
		printk(KERN_EMERG "L2C: LRU error\n");
	}

	return IRQ_HANDLED;
}

static int __init ppc4xx_l2c_probe(void)
{
	struct device_node *np;
	u32 r;
	unsigned long flags;
	int irq;
	const u32 *dcrreg;
	u32 dcrbase_isram;
	int len;
	const u32 *prop;
	u32 l2_size;

	np = of_find_compatible_node(NULL, NULL, "ibm,l2-cache");
	if (!np)
		return 0;

	/* Get l2 cache size */
	prop = of_get_property(np, "cache-size", NULL);
	if (prop == NULL) {
		printk(KERN_ERR "%pOF: Can't get cache-size!\n", np);
		of_node_put(np);
		return -ENODEV;
	}
	l2_size = prop[0];

	/* Map DCRs */
	dcrreg = of_get_property(np, "dcr-reg", &len);
	if (!dcrreg || (len != 4 * sizeof(u32))) {
		printk(KERN_ERR "%pOF: Can't get DCR register base !", np);
		of_node_put(np);
		return -ENODEV;
	}
	dcrbase_isram = dcrreg[0];
	dcrbase_l2c = dcrreg[2];

	/* Get and map irq number from device tree */
	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		printk(KERN_ERR "irq_of_parse_and_map failed\n");
		of_node_put(np);
		return -ENODEV;
	}

	/* Install error handler */
	if (request_irq(irq, l2c_error_handler, 0, "L2C", 0) < 0) {
		printk(KERN_ERR "Cannot install L2C error handler"
		       ", cache is not enabled\n");
		of_node_put(np);
		return -ENODEV;
	}

	local_irq_save(flags);
	asm volatile ("sync" ::: "memory");

	/* Disable SRAM */
	mtdcr(dcrbase_isram + DCRN_SRAM0_DPC,
	      mfdcr(dcrbase_isram + DCRN_SRAM0_DPC) & ~SRAM_DPC_ENABLE);
	mtdcr(dcrbase_isram + DCRN_SRAM0_SB0CR,
	      mfdcr(dcrbase_isram + DCRN_SRAM0_SB0CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(dcrbase_isram + DCRN_SRAM0_SB1CR,
	      mfdcr(dcrbase_isram + DCRN_SRAM0_SB1CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(dcrbase_isram + DCRN_SRAM0_SB2CR,
	      mfdcr(dcrbase_isram + DCRN_SRAM0_SB2CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(dcrbase_isram + DCRN_SRAM0_SB3CR,
	      mfdcr(dcrbase_isram + DCRN_SRAM0_SB3CR) & ~SRAM_SBCR_BU_MASK);

	/* Enable L2_MODE without ICU/DCU */
	r = mfdcr(dcrbase_l2c + DCRN_L2C0_CFG) &
		~(L2C_CFG_ICU | L2C_CFG_DCU | L2C_CFG_SS_MASK);
	r |= L2C_CFG_L2M | L2C_CFG_SS_256;
	mtdcr(dcrbase_l2c + DCRN_L2C0_CFG, r);

	mtdcr(dcrbase_l2c + DCRN_L2C0_ADDR, 0);

	/* Hardware Clear Command */
	mtdcr(dcrbase_l2c + DCRN_L2C0_CMD, L2C_CMD_HCC);
	while (!(mfdcr(dcrbase_l2c + DCRN_L2C0_SR) & L2C_SR_CC))
		;

	/* Clear Cache Parity and Tag Errors */
	mtdcr(dcrbase_l2c + DCRN_L2C0_CMD, L2C_CMD_CCP | L2C_CMD_CTE);

	/* Enable 64G snoop region starting at 0 */
	r = mfdcr(dcrbase_l2c + DCRN_L2C0_SNP0) &
		~(L2C_SNP_BA_MASK | L2C_SNP_SSR_MASK);
	r |= L2C_SNP_SSR_32G | L2C_SNP_ESR;
	mtdcr(dcrbase_l2c + DCRN_L2C0_SNP0, r);

	r = mfdcr(dcrbase_l2c + DCRN_L2C0_SNP1) &
		~(L2C_SNP_BA_MASK | L2C_SNP_SSR_MASK);
	r |= 0x80000000 | L2C_SNP_SSR_32G | L2C_SNP_ESR;
	mtdcr(dcrbase_l2c + DCRN_L2C0_SNP1, r);

	asm volatile ("sync" ::: "memory");

	/* Enable ICU/DCU ports */
	r = mfdcr(dcrbase_l2c + DCRN_L2C0_CFG);
	r &= ~(L2C_CFG_DCW_MASK | L2C_CFG_PMUX_MASK | L2C_CFG_PMIM
	       | L2C_CFG_TPEI | L2C_CFG_CPEI | L2C_CFG_NAM | L2C_CFG_NBRM);
	r |= L2C_CFG_ICU | L2C_CFG_DCU | L2C_CFG_TPC | L2C_CFG_CPC | L2C_CFG_FRAN
		| L2C_CFG_CPIM | L2C_CFG_TPIM | L2C_CFG_LIM | L2C_CFG_SMCM;

	/* Check for 460EX/GT special handling */
	if (of_device_is_compatible(np, "ibm,l2-cache-460ex") ||
	    of_device_is_compatible(np, "ibm,l2-cache-460gt"))
		r |= L2C_CFG_RDBW;

	mtdcr(dcrbase_l2c + DCRN_L2C0_CFG, r);

	asm volatile ("sync; isync" ::: "memory");
	local_irq_restore(flags);

	printk(KERN_INFO "%dk L2-cache enabled\n", l2_size >> 10);

	of_node_put(np);
	return 0;
}
arch_initcall(ppc4xx_l2c_probe);

/*
 * Apply a system reset. Alternatively a board specific value may be
 * provided via the "reset-type" property in the cpu node.
 */
void ppc4xx_reset_system(char *cmd)
{
	struct device_node *np;
	u32 reset_type = DBCR0_RST_SYSTEM;
	const u32 *prop;

	np = of_get_cpu_node(0, NULL);
	if (np) {
		prop = of_get_property(np, "reset-type", NULL);

		/*
		 * Check if property exists and if it is in range:
		 * 1 - PPC4xx core reset
		 * 2 - PPC4xx chip reset
		 * 3 - PPC4xx system reset (default)
		 */
		if ((prop) && ((prop[0] >= 1) && (prop[0] <= 3)))
			reset_type = prop[0] << 28;
	}

	mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) | reset_type);

	while (1)
		;	/* Just in case the reset doesn't work */
}
