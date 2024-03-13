// SPDX-License-Identifier: GPL-2.0
/*
 * ip22-mc.c: Routines for manipulating SGI Memory Controller.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1999 Andrew R. Baker (andrewb@uab.edu) - Indigo2 changes
 * Copyright (C) 2003 Ladislav Michl  (ladis@linux-mips.org)
 * Copyright (C) 2004 Peter Fuerst    (pf@net.alphadv.de) - IP28
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/sgialib.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

struct sgimc_regs *sgimc;

EXPORT_SYMBOL(sgimc);

static inline unsigned long get_bank_addr(unsigned int memconfig)
{
	return (memconfig & SGIMC_MCONFIG_BASEADDR) << ((sgimc->systemid & SGIMC_SYSID_MASKREV) >= 5 ? 24 : 22);
}

static inline unsigned long get_bank_size(unsigned int memconfig)
{
	return ((memconfig & SGIMC_MCONFIG_RMASK) + 0x0100) << ((sgimc->systemid & SGIMC_SYSID_MASKREV) >= 5 ? 16 : 14);
}

static inline unsigned int get_bank_config(int bank)
{
	unsigned int res = bank > 1 ? sgimc->mconfig1 : sgimc->mconfig0;
	return bank % 2 ? res & 0xffff : res >> 16;
}

#if defined(CONFIG_SGI_IP28) || defined(CONFIG_32BIT)
static void __init probe_memory(void)
{
	/* prom detects all usable memory */
}
#else
/*
 * Detect installed memory, which PROM misses
 */
static void __init probe_memory(void)
{
	unsigned long addr, size;
	int i;

	printk(KERN_INFO "MC: Probing memory configuration:\n");
	for (i = 0; i < 4; i++) {
		unsigned int tmp = get_bank_config(i);
		if (!(tmp & SGIMC_MCONFIG_BVALID))
			continue;

		size = get_bank_size(tmp);
		addr = get_bank_addr(tmp);
		printk(KERN_INFO " bank%d: %3ldM @ %08lx\n",
			i, size / 1024 / 1024, addr);

		if (addr >= SGIMC_SEG1_BADDR)
			memblock_add(addr, size);
	}
}
#endif

void __init sgimc_init(void)
{
	u32 tmp;

	/* ioremap can't fail */
	sgimc = (struct sgimc_regs *)
		ioremap(SGIMC_BASE, sizeof(struct sgimc_regs));

	printk(KERN_INFO "MC: SGI memory controller Revision %d\n",
	       (int) sgimc->systemid & SGIMC_SYSID_MASKREV);

	/* Place the MC into a known state.  This must be done before
	 * interrupts are first enabled etc.
	 */

	/* Step 0: Make sure we turn off the watchdog in case it's
	 *	   still running (which might be the case after a
	 *	   soft reboot).
	 */
	tmp = sgimc->cpuctrl0;
	tmp &= ~SGIMC_CCTRL0_WDOG;
	sgimc->cpuctrl0 = tmp;

	/* Step 1: The CPU/GIO error status registers will not latch
	 *	   up a new error status until the register has been
	 *	   cleared by the cpu.	These status registers are
	 *	   cleared by writing any value to them.
	 */
	sgimc->cstat = sgimc->gstat = 0;

	/* Step 2: Enable all parity checking in cpu control register
	 *	   zero.
	 */
	/* don't touch parity settings for IP28 */
	tmp = sgimc->cpuctrl0;
#ifndef CONFIG_SGI_IP28
	tmp |= SGIMC_CCTRL0_EPERRGIO | SGIMC_CCTRL0_EPERRMEM;
#endif
	tmp |= SGIMC_CCTRL0_R4KNOCHKPARR;
	sgimc->cpuctrl0 = tmp;

	/* Step 3: Setup the MC write buffer depth, this is controlled
	 *	   in cpu control register 1 in the lower 4 bits.
	 */
	tmp = sgimc->cpuctrl1;
	tmp &= ~0xf;
	tmp |= 0xd;
	sgimc->cpuctrl1 = tmp;

	/* Step 4: Initialize the RPSS divider register to run as fast
	 *	   as it can correctly operate.	 The register is laid
	 *	   out as follows:
	 *
	 *	   ----------------------------------------
	 *	   |  RESERVED	|   INCREMENT	| DIVIDER |
	 *	   ----------------------------------------
	 *	    31	      16 15	       8 7	 0
	 *
	 *	   DIVIDER determines how often a 'tick' happens,
	 *	   INCREMENT determines by how the RPSS increment
	 *	   registers value increases at each 'tick'. Thus,
	 *	   for IP22 we get INCREMENT=1, DIVIDER=1 == 0x101
	 */
	sgimc->divider = 0x101;

	/* Step 5: Initialize GIO64 arbitrator configuration register.
	 *
	 * NOTE: HPC init code in sgihpc_init() must run before us because
	 *	 we need to know Guiness vs. FullHouse and the board
	 *	 revision on this machine. You have been warned.
	 */

	/* First the basic invariants across all GIO64 implementations. */
	tmp = sgimc->giopar & SGIMC_GIOPAR_GFX64; /* keep gfx 64bit settings */
	tmp |= SGIMC_GIOPAR_HPC64;	/* All 1st HPC's interface at 64bits */
	tmp |= SGIMC_GIOPAR_ONEBUS;	/* Only one physical GIO bus exists */

	if (ip22_is_fullhouse()) {
		/* Fullhouse specific settings. */
		if (SGIOC_SYSID_BOARDREV(sgioc->sysid) < 2) {
			tmp |= SGIMC_GIOPAR_HPC264;	/* 2nd HPC at 64bits */
			tmp |= SGIMC_GIOPAR_PLINEEXP0;	/* exp0 pipelines */
			tmp |= SGIMC_GIOPAR_MASTEREXP1; /* exp1 masters */
			tmp |= SGIMC_GIOPAR_RTIMEEXP0;	/* exp0 is realtime */
		} else {
			tmp |= SGIMC_GIOPAR_HPC264;	/* 2nd HPC 64bits */
			tmp |= SGIMC_GIOPAR_PLINEEXP0;	/* exp[01] pipelined */
			tmp |= SGIMC_GIOPAR_PLINEEXP1;
			tmp |= SGIMC_GIOPAR_MASTEREISA; /* EISA masters */
		}
	} else {
		/* Guiness specific settings. */
		tmp |= SGIMC_GIOPAR_EISA64;	/* MC talks to EISA at 64bits */
		tmp |= SGIMC_GIOPAR_MASTEREISA; /* EISA bus can act as master */
	}
	sgimc->giopar = tmp;	/* poof */

	probe_memory();
}

#ifdef CONFIG_SGI_IP28
void __init prom_cleanup(void)
{
	u32 mconfig1;
	unsigned long flags;
	spinlock_t lock;

	/*
	 * because ARCS accesses memory uncached we wait until ARCS
	 * isn't needed any longer, before we switch from slow to
	 * normal mode
	 */
	spin_lock_irqsave(&lock, flags);
	mconfig1 = sgimc->mconfig1;
	/* map ECC register */
	sgimc->mconfig1 = (mconfig1 & 0xffff0000) | 0x2060;
	iob();
	/* switch to normal mode */
	*(unsigned long *)PHYS_TO_XKSEG_UNCACHED(0x60000000) = 0;
	iob();
	/* reduce WR_COL */
	sgimc->cmacc = (sgimc->cmacc & ~0xf) | 4;
	iob();
	/* restore old config */
	sgimc->mconfig1 = mconfig1;
	iob();
	spin_unlock_irqrestore(&lock, flags);
}
#endif
