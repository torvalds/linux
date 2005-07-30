/*
 *  arch/cris/arch-v32/drivers/nandflash.c
 *
 *  Copyright (c) 2004
 *
 *  Derived from drivers/mtd/nand/spia.c
 * 	  Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * $Id: nandflash.c,v 1.3 2005/06/01 10:57:12 starvik Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/arch/memmap.h>
#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/gio_defs.h>
#include <asm/arch/hwregs/bif_core_defs.h>
#include <asm/io.h>

#define CE_BIT 4
#define CLE_BIT 5
#define ALE_BIT 6
#define BY_BIT 7

static struct mtd_info *crisv32_mtd = NULL;
/*
 *	hardware specific access to control-lines
*/
static void crisv32_hwcontrol(struct mtd_info *mtd, int cmd)
{
	unsigned long flags;
	reg_gio_rw_pa_dout dout = REG_RD(gio, regi_gio, rw_pa_dout);

	local_irq_save(flags);
	switch(cmd){
		case NAND_CTL_SETCLE:
		     dout.data |= (1<<CLE_BIT);
		     break;
		case NAND_CTL_CLRCLE:
		     dout.data &= ~(1<<CLE_BIT);
		     break;
		case NAND_CTL_SETALE:
		     dout.data |= (1<<ALE_BIT);
		     break;
		case NAND_CTL_CLRALE:
		     dout.data &= ~(1<<ALE_BIT);
		     break;
		case NAND_CTL_SETNCE:
		     dout.data |= (1<<CE_BIT);
		     break;
		case NAND_CTL_CLRNCE:
		     dout.data &= ~(1<<CE_BIT);
		     break;
	}
	REG_WR(gio, regi_gio, rw_pa_dout, dout);
	local_irq_restore(flags);
}

/*
*	read device ready pin
*/
int crisv32_device_ready(struct mtd_info *mtd)
{
	reg_gio_r_pa_din din = REG_RD(gio, regi_gio, r_pa_din);
	return ((din.data & (1 << BY_BIT)) >> BY_BIT);
}

/*
 * Main initialization routine
 */
struct mtd_info* __init crisv32_nand_flash_probe (void)
{
	void __iomem *read_cs;
	void __iomem *write_cs;

	reg_bif_core_rw_grp3_cfg bif_cfg = REG_RD(bif_core, regi_bif_core, rw_grp3_cfg);
	reg_gio_rw_pa_oe pa_oe = REG_RD(gio, regi_gio, rw_pa_oe);
	struct nand_chip *this;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	crisv32_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct nand_chip),
				GFP_KERNEL);
	if (!crisv32_mtd) {
		printk ("Unable to allocate CRISv32 NAND MTD device structure.\n");
		err = -ENOMEM;
		return NULL;
	}

	read_cs = ioremap(MEM_CSP0_START | MEM_NON_CACHEABLE, 8192);
	write_cs = ioremap(MEM_CSP1_START | MEM_NON_CACHEABLE, 8192);

	if (!read_cs || !write_cs) {
		printk("CRISv32 NAND ioremap failed\n");
		err = -EIO;
		goto out_mtd;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&crisv32_mtd[1]);

	pa_oe.oe |= 1 << CE_BIT;
	pa_oe.oe |= 1 << ALE_BIT;
	pa_oe.oe |= 1 << CLE_BIT;
	pa_oe.oe &= ~ (1 << BY_BIT);
	REG_WR(gio, regi_gio, rw_pa_oe, pa_oe);

	bif_cfg.gated_csp0 = regk_bif_core_rd;
	bif_cfg.gated_csp1 = regk_bif_core_wr;
	REG_WR(bif_core, regi_bif_core, rw_grp3_cfg, bif_cfg);

	/* Initialize structures */
	memset((char *) crisv32_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	crisv32_mtd->priv = this;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = read_cs;
	this->IO_ADDR_W = write_cs;
	this->hwcontrol = crisv32_hwcontrol;
	this->dev_ready = crisv32_device_ready;
	/* 20 us command delay time */
	this->chip_delay = 20;
	this->eccmode = NAND_ECC_SOFT;

	/* Enable the following for a flash based bad block table */
	this->options = NAND_USE_FLASH_BBT;

	/* Scan to find existance of the device */
	if (nand_scan (crisv32_mtd, 1)) {
		err = -ENXIO;
		goto out_ior;
	}

	return crisv32_mtd;

out_ior:
	iounmap((void *)read_cs);
	iounmap((void *)write_cs);
out_mtd:
	kfree (crisv32_mtd);
        return NULL;
}

