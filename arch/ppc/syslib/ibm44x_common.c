/*
 * PPC44x system library
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/time.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/initrd.h>

#include <asm/ibm44x.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/ppc4xx_pic.h>
#include <asm/param.h>
#include <asm/bootinfo.h>
#include <asm/ppcboot.h>

#include <syslib/gen550.h>

/* Global Variables */
bd_t __res;

phys_addr_t fixup_bigphys_addr(phys_addr_t addr, phys_addr_t size)
{
	phys_addr_t page_4gb = 0;

        /*
	 * Trap the least significant 32-bit portions of an
	 * address in the 440's 36-bit address space.  Fix
	 * them up with the appropriate ERPN
	 */
	if ((addr >= PPC44x_IO_LO) && (addr <= PPC44x_IO_HI))
		page_4gb = PPC44x_IO_PAGE;
	else if ((addr >= PPC44x_PCI0CFG_LO) && (addr <= PPC44x_PCI0CFG_HI))
		page_4gb = PPC44x_PCICFG_PAGE;
#ifdef CONFIG_440SP
	else if ((addr >= PPC44x_PCI1CFG_LO) && (addr <= PPC44x_PCI1CFG_HI))
		page_4gb = PPC44x_PCICFG_PAGE;
	else if ((addr >= PPC44x_PCI2CFG_LO) && (addr <= PPC44x_PCI2CFG_HI))
		page_4gb = PPC44x_PCICFG_PAGE;
#endif
	else if ((addr >= PPC44x_PCIMEM_LO) && (addr <= PPC44x_PCIMEM_HI))
		page_4gb = PPC44x_PCIMEM_PAGE;

	return (page_4gb | addr);
};
EXPORT_SYMBOL(fixup_bigphys_addr);

void __init ibm44x_calibrate_decr(unsigned int freq)
{
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
}

extern void abort(void);

static void ibm44x_restart(char *cmd)
{
	local_irq_disable();
	abort();
}

static void ibm44x_power_off(void)
{
	local_irq_disable();
	for(;;);
}

static void ibm44x_halt(void)
{
	local_irq_disable();
	for(;;);
}

/*
 * Read the 44x memory controller to get size of system memory.
 */
static unsigned long __init ibm44x_find_end_of_memory(void)
{
	u32 i, bank_config;
	u32 mem_size = 0;

	for (i=0; i<4; i++)
	{
		switch (i)
		{
			case 0:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B0CR);
				break;
			case 1:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B1CR);
				break;
			case 2:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B2CR);
				break;
			case 3:
				mtdcr(DCRN_SDRAM0_CFGADDR, SDRAM0_B3CR);
				break;
		}

		bank_config = mfdcr(DCRN_SDRAM0_CFGDATA);

		if (!(bank_config & SDRAM_CONFIG_BANK_ENABLE))
			continue;
		switch (SDRAM_CONFIG_BANK_SIZE(bank_config))
		{
			case SDRAM_CONFIG_SIZE_8M:
				mem_size += PPC44x_MEM_SIZE_8M;
				break;
			case SDRAM_CONFIG_SIZE_16M:
				mem_size += PPC44x_MEM_SIZE_16M;
				break;
			case SDRAM_CONFIG_SIZE_32M:
				mem_size += PPC44x_MEM_SIZE_32M;
				break;
			case SDRAM_CONFIG_SIZE_64M:
				mem_size += PPC44x_MEM_SIZE_64M;
				break;
			case SDRAM_CONFIG_SIZE_128M:
				mem_size += PPC44x_MEM_SIZE_128M;
				break;
			case SDRAM_CONFIG_SIZE_256M:
				mem_size += PPC44x_MEM_SIZE_256M;
				break;
			case SDRAM_CONFIG_SIZE_512M:
				mem_size += PPC44x_MEM_SIZE_512M;
				break;
		}
	}
	return mem_size;
}

void __init ibm44x_platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
				 unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/*
	 * If we were passed in a board information, copy it into the
	 * residual data area.
	 */
	if (r3)
		__res = *(bd_t *)(r3 + KERNELBASE);

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif  /* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
		*(char *) (r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *) (r6 + KERNELBASE));
	}

	ppc_md.init_IRQ = ppc4xx_pic_init;
	ppc_md.find_end_of_memory = ibm44x_find_end_of_memory;
	ppc_md.restart = ibm44x_restart;
	ppc_md.power_off = ibm44x_power_off;
	ppc_md.halt = ibm44x_halt;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif /* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef CONFIG_KGDB
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif

	/*
	 * The Abatron BDI JTAG debugger does not tolerate others
	 * mucking with the debug registers.
	 */
#if !defined(CONFIG_BDI_SWITCH)
	/* Enable internal debug mode */
        mtspr(SPRN_DBCR0, (DBCR0_IDM));

	/* Clear any residual debug events */
	mtspr(SPRN_DBSR, 0xffffffff);
#endif
}

/* Called from machine_check_exception */
void platform_machine_check(struct pt_regs *regs)
{
#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
	printk("PLB0: BEAR=0x%08x%08x ACR=  0x%08x BESR= 0x%08x%08x\n",
	       mfdcr(DCRN_PLB0_BEARH), mfdcr(DCRN_PLB0_BEARL),
	       mfdcr(DCRN_PLB0_ACR), mfdcr(DCRN_PLB0_BESRH),
	       mfdcr(DCRN_PLB0_BESRL));
	printk("PLB1: BEAR=0x%08x%08x ACR=  0x%08x BESR= 0x%08x%08x\n",
	       mfdcr(DCRN_PLB1_BEARH), mfdcr(DCRN_PLB1_BEARL),
	       mfdcr(DCRN_PLB1_ACR), mfdcr(DCRN_PLB1_BESRH),
	       mfdcr(DCRN_PLB1_BESRL));
#else
    	printk("PLB0: BEAR=0x%08x%08x ACR=  0x%08x BESR= 0x%08x\n",
		mfdcr(DCRN_PLB0_BEARH), mfdcr(DCRN_PLB0_BEARL),
		mfdcr(DCRN_PLB0_ACR),  mfdcr(DCRN_PLB0_BESR));
#endif
	printk("POB0: BEAR=0x%08x%08x BESR0=0x%08x BESR1=0x%08x\n",
		mfdcr(DCRN_POB0_BEARH), mfdcr(DCRN_POB0_BEARL),
		mfdcr(DCRN_POB0_BESR0), mfdcr(DCRN_POB0_BESR1));
	printk("OPB0: BEAR=0x%08x%08x BSTAT=0x%08x\n",
		mfdcr(DCRN_OPB0_BEARH), mfdcr(DCRN_OPB0_BEARL),
		mfdcr(DCRN_OPB0_BSTAT));
}
