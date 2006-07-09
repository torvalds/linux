/*
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * arch/mips/ddb5xxx/ddb5477/setup.c
 *     Setup file for DDB5477.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/param.h>	/* for HZ */
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>
#include <linux/pm.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/traps.h>
#include <asm/debug.h>

#include <asm/ddb5xxx/ddb5xxx.h>

#include "lcd44780.h"


#define	USE_CPU_COUNTER_TIMER	/* whether we use cpu counter */

#define	SP_TIMER_BASE			DDB_SPT1CTRL_L
#define	SP_TIMER_IRQ			VRC5477_IRQ_SPT1

static int bus_frequency = CONFIG_DDB5477_BUS_FREQUENCY*1000;

static void ddb_machine_restart(char *command)
{
	static void (*back_to_prom) (void) = (void (*)(void)) 0xbfc00000;

	u32 t;

	/* PCI cold reset */
	ddb_pci_reset_bus();

	/* CPU cold reset */
	t = ddb_in32(DDB_CPUSTAT);
	db_assert((t&1));
	ddb_out32(DDB_CPUSTAT, t);

	/* Call the PROM */
	back_to_prom();
}

static void ddb_machine_halt(void)
{
	printk("DDB Vrc-5477 halted.\n");
	while (1);
}

static void ddb_machine_power_off(void)
{
	printk("DDB Vrc-5477 halted. Please turn off the power.\n");
	while (1);
}

extern void rtc_ds1386_init(unsigned long base);

static unsigned int __init detect_bus_frequency(unsigned long rtc_base)
{
	unsigned int freq;
	unsigned char c;
	unsigned int t1, t2;
	unsigned i;

	ddb_out32(SP_TIMER_BASE, 0xffffffff);
	ddb_out32(SP_TIMER_BASE+4, 0x1);
	ddb_out32(SP_TIMER_BASE+8, 0xffffffff);

	/* check if rtc is running */
	c= *(volatile unsigned char*)rtc_base;
	for(i=0; (c == *(volatile unsigned char*)rtc_base) && (i<100000000); i++);
	if (c == *(volatile unsigned char*)rtc_base) {
		printk("Failed to detect bus frequency.  Use default 83.3MHz.\n");
		return 83333000;
	}

	c= *(volatile unsigned char*)rtc_base;
	while (c == *(volatile unsigned char*)rtc_base);
	/* we are now at the turn of 1/100th second, if no error. */
	t1 = ddb_in32(SP_TIMER_BASE+8);

	for (i=0; i< 10; i++) {
		c= *(volatile unsigned char*)rtc_base;
		while (c == *(volatile unsigned char*)rtc_base);
		/* we are now at the turn of another 1/100th second */
		t2 = ddb_in32(SP_TIMER_BASE+8);
	}

	ddb_out32(SP_TIMER_BASE+4, 0x0);	/* disable it again */

	freq = (t1 - t2)*10;
	printk("DDB bus frequency detection : %u \n", freq);
	return freq;
}

static void __init ddb_time_init(void)
{
	unsigned long rtc_base;
	unsigned int i;

	/* we have ds1396 RTC chip */
	if (mips_machtype == MACH_NEC_ROCKHOPPER
		||  mips_machtype == MACH_NEC_ROCKHOPPERII) {
		rtc_base = KSEG1ADDR(DDB_LCS2_BASE);
	} else {
		rtc_base = KSEG1ADDR(DDB_LCS1_BASE);
	}
	rtc_ds1386_init(rtc_base);

	/* do we need to do run-time detection of bus speed? */
	if (bus_frequency == 0) {
		bus_frequency = detect_bus_frequency(rtc_base);
	}

	/* mips_hpt_frequency is 1/2 of the cpu core freq */
	i =  (read_c0_config() >> 28 ) & 7;
	if ((current_cpu_data.cputype == CPU_R5432) && (i == 3))
		i = 4;
	mips_hpt_frequency = bus_frequency*(i+4)/4;
}

void __init plat_timer_setup(struct irqaction *irq)
{
#if defined(USE_CPU_COUNTER_TIMER)

        /* we are using the cpu counter for timer interrupts */
	setup_irq(CPU_IRQ_BASE + 7, irq);

#else

	/* if we use Special purpose timer 1 */
	ddb_out32(SP_TIMER_BASE, bus_frequency/HZ);
	ddb_out32(SP_TIMER_BASE+4, 0x1);
	setup_irq(SP_TIMER_IRQ, irq);

#endif
}

static void ddb5477_board_init(void);

extern struct pci_controller ddb5477_ext_controller;
extern struct pci_controller ddb5477_io_controller;

void __init plat_mem_setup(void)
{
	/* initialize board - we don't trust the loader */
        ddb5477_board_init();

	set_io_port_base(KSEG1ADDR(DDB_PCI_IO_BASE));

	board_time_init = ddb_time_init;

	_machine_restart = ddb_machine_restart;
	_machine_halt = ddb_machine_halt;
	pm_power_off = ddb_machine_power_off;

	/* setup resource limits */
	ioport_resource.end = DDB_PCI0_IO_SIZE + DDB_PCI1_IO_SIZE - 1;
	iomem_resource.end = 0xffffffff;

	/* Reboot on panic */
	panic_timeout = 180;

	register_pci_controller (&ddb5477_ext_controller);
	register_pci_controller (&ddb5477_io_controller);
}

static void __init ddb5477_board_init(void)
{
	/* ----------- setup PDARs ------------ */

	/* SDRAM should have been set */
	db_assert(ddb_in32(DDB_SDRAM0) ==
		    ddb_calc_pdar(DDB_SDRAM_BASE, board_ram_size, 32, 0, 1));

	/* SDRAM1 should be turned off.  What is this for anyway ? */
	db_assert( (ddb_in32(DDB_SDRAM1) & 0xf) == 0);

	/* Setup local bus. */

	/* Flash U12 PDAR and timing. */
	ddb_set_pdar(DDB_LCS0, DDB_LCS0_BASE, DDB_LCS0_SIZE, 16, 0, 0);
	ddb_out32(DDB_LCST0, 0x00090842);

	/* We need to setup LCS1 and LCS2 differently based on the
	   board_version */
	if (mips_machtype == MACH_NEC_ROCKHOPPER) {
		/* Flash U13 PDAR and timing. */
		ddb_set_pdar(DDB_LCS1, DDB_LCS1_BASE, DDB_LCS1_SIZE, 16, 0, 0);
		ddb_out32(DDB_LCST1, 0x00090842);

		/* EPLD (NVRAM, switch, LCD, and mezzanie). */
		ddb_set_pdar(DDB_LCS2, DDB_LCS2_BASE, DDB_LCS2_SIZE, 8, 0, 0);
	} else {
		/* misc */
		ddb_set_pdar(DDB_LCS1, DDB_LCS1_BASE, DDB_LCS1_SIZE, 8, 0, 0);
		/* mezzanie (?) */
		ddb_set_pdar(DDB_LCS2, DDB_LCS2_BASE, DDB_LCS2_SIZE, 16, 0, 0);
	}

	/* verify VRC5477 base addr */
	db_assert(ddb_in32(DDB_VRC5477) ==
		  ddb_calc_pdar(DDB_VRC5477_BASE, DDB_VRC5477_SIZE, 32, 0, 1));

	/* verify BOOT ROM addr */
	db_assert(ddb_in32(DDB_BOOTCS) ==
		  ddb_calc_pdar(DDB_BOOTCS_BASE, DDB_BOOTCS_SIZE, 8, 0, 0));

	/* setup PCI windows - window0 for MEM/config, window1 for IO */
	ddb_set_pdar(DDB_PCIW0, DDB_PCI0_MEM_BASE, DDB_PCI0_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_PCIW1, DDB_PCI0_IO_BASE, DDB_PCI0_IO_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_IOPCIW0, DDB_PCI1_MEM_BASE, DDB_PCI1_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_IOPCIW1, DDB_PCI1_IO_BASE, DDB_PCI1_IO_SIZE, 32, 0, 1);

	/* ------------ reset PCI bus and BARs ----------------- */
	ddb_pci_reset_bus();

	ddb_out32(DDB_BARM010, 0x00000008);
	ddb_out32(DDB_BARM011, 0x00000008);

	ddb_out32(DDB_BARC0, 0xffffffff);
	ddb_out32(DDB_BARM230, 0xffffffff);
	ddb_out32(DDB_BAR00, 0xffffffff);
	ddb_out32(DDB_BAR10, 0xffffffff);
	ddb_out32(DDB_BAR20, 0xffffffff);
	ddb_out32(DDB_BAR30, 0xffffffff);
	ddb_out32(DDB_BAR40, 0xffffffff);
	ddb_out32(DDB_BAR50, 0xffffffff);
	ddb_out32(DDB_BARB0, 0xffffffff);

	ddb_out32(DDB_BARC1, 0xffffffff);
	ddb_out32(DDB_BARM231, 0xffffffff);
	ddb_out32(DDB_BAR01, 0xffffffff);
	ddb_out32(DDB_BAR11, 0xffffffff);
	ddb_out32(DDB_BAR21, 0xffffffff);
	ddb_out32(DDB_BAR31, 0xffffffff);
	ddb_out32(DDB_BAR41, 0xffffffff);
	ddb_out32(DDB_BAR51, 0xffffffff);
	ddb_out32(DDB_BARB1, 0xffffffff);

	/*
	 * We use pci master register 0  for memory space / config space
	 * And we use register 1 for IO space.
	 * Note that for memory space, we bump up the pci base address
	 * so that we have 1:1 mapping between PCI memory and cpu physical.
	 * For PCI IO space, it starts from 0 in PCI IO space but with
	 * DDB_xx_IO_BASE in CPU physical address space.
	 */
	ddb_set_pmr(DDB_PCIINIT00, DDB_PCICMD_MEM, DDB_PCI0_MEM_BASE,
		    DDB_PCI_ACCESS_32);
	ddb_set_pmr(DDB_PCIINIT10, DDB_PCICMD_IO, 0, DDB_PCI_ACCESS_32);

	ddb_set_pmr(DDB_PCIINIT01, DDB_PCICMD_MEM, DDB_PCI1_MEM_BASE,
		    DDB_PCI_ACCESS_32);
	ddb_set_pmr(DDB_PCIINIT11, DDB_PCICMD_IO, DDB_PCI0_IO_SIZE,
                    DDB_PCI_ACCESS_32);


	/* PCI cross window should be set properly */
	ddb_set_pdar(DDB_BARP00, DDB_PCI1_MEM_BASE, DDB_PCI1_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP10, DDB_PCI1_IO_BASE, DDB_PCI1_IO_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP01, DDB_PCI0_MEM_BASE, DDB_PCI0_MEM_SIZE, 32, 0, 1);
	ddb_set_pdar(DDB_BARP11, DDB_PCI0_IO_BASE, DDB_PCI0_IO_SIZE, 32, 0, 1);

	if (mips_machtype == MACH_NEC_ROCKHOPPER
	   ||  mips_machtype == MACH_NEC_ROCKHOPPERII) {
		/* Disable bus diagnostics. */
		ddb_out32(DDB_PCICTL0_L, 0);
		ddb_out32(DDB_PCICTL0_H, 0);
		ddb_out32(DDB_PCICTL1_L, 0);
		ddb_out32(DDB_PCICTL1_H, 0);
	}

	if (mips_machtype == MACH_NEC_ROCKHOPPER) {
		u16			vid;
		struct pci_bus		bus;
		struct pci_dev		dev_m1533;
		extern struct pci_ops 	ddb5477_ext_pci_ops;

		bus.parent      = NULL;    /* we scan the top level only */
		bus.ops         = &ddb5477_ext_pci_ops;
		dev_m1533.bus         = &bus;
		dev_m1533.sysdata     = NULL;
		dev_m1533.devfn       = 7*8;     // slot 7: M1533 SouthBridge.
		pci_read_config_word(&dev_m1533, 0, &vid);
		if (vid == PCI_VENDOR_ID_AL) {
			printk("Changing mips_machtype to MACH_NEC_ROCKHOPPERII\n");
			mips_machtype = MACH_NEC_ROCKHOPPERII;
		}
	}

	/* enable USB input buffers */
	ddb_out32(DDB_PIBMISC, 0x00000007);

	/* For dual-function pins, make them all non-GPIO */
	ddb_out32(DDB_GIUFUNSEL, 0x0);
	// ddb_out32(DDB_GIUFUNSEL, 0xfe0fcfff);  /* NEC recommanded value */

	if (mips_machtype == MACH_NEC_ROCKHOPPERII) {

		/* enable IDE controller on Ali chip (south bridge) */
		u8			temp8;
		struct pci_bus		bus;
		struct pci_dev		dev_m1533;
		struct pci_dev		dev_m5229;
		extern struct pci_ops 	ddb5477_ext_pci_ops;

		/* Setup M1535 registers */
		bus.parent      = NULL;    /* we scan the top level only */
		bus.ops         = &ddb5477_ext_pci_ops;
		dev_m1533.bus         = &bus;
		dev_m1533.sysdata     = NULL;
		dev_m1533.devfn       = 7*8;     // slot 7: M1533 SouthBridge.

		/* setup IDE controller
		 * enable IDE controller (bit 6 - 1)
		 * IDE IDSEL to be addr:A15 (bit 4:5 - 11)
		 * disable IDE ATA Secondary Bus Signal Pad Control (bit 3 - 0)
		 * enable IDE ATA Primary Bus Signal Pad Control (bit 2 - 1)
		 */
		pci_write_config_byte(&dev_m1533, 0x58, 0x74);

		/*
		 * positive decode (bit6 -0)
		 * enable IDE controler interrupt (bit 4 -1)
		 * setup SIRQ to point to IRQ 14 (bit 3:0 - 1101)
		 */
		pci_write_config_byte(&dev_m1533, 0x44, 0x1d);

		/* Setup M5229 registers */
		dev_m5229.bus = &bus;
		dev_m5229.sysdata = NULL;
		dev_m5229.devfn = 4*8;  	// slot 4 (AD15): M5229 IDE

		/*
		 * enable IDE in the M5229 config register 0x50 (bit 0 - 1)
		 * M5229 IDSEL is addr:15; see above setting
		 */
		pci_read_config_byte(&dev_m5229, 0x50, &temp8);
		pci_write_config_byte(&dev_m5229, 0x50, temp8 | 0x1);

		/*
		 * enable bus master (bit 2)  and IO decoding  (bit 0)
		 */
		pci_read_config_byte(&dev_m5229, 0x04, &temp8);
		pci_write_config_byte(&dev_m5229, 0x04, temp8 | 0x5);

		/*
		 * enable native, copied from arch/ppc/k2boot/head.S
		 * TODO - need volatile, need to be portable
		 */
		pci_write_config_byte(&dev_m5229, 0x09, 0xef);

		/* Set Primary Channel Command Block Timing */
		pci_write_config_byte(&dev_m5229, 0x59, 0x31);

		/*
		 * Enable primary channel 40-pin cable
		 * M5229 register 0x4a (bit 0)
		 */
		pci_read_config_byte(&dev_m5229, 0x4a, &temp8);
		pci_write_config_byte(&dev_m5229, 0x4a, temp8 | 0x1);
	}

	if (mips_machtype == MACH_NEC_ROCKHOPPER
	   ||  mips_machtype == MACH_NEC_ROCKHOPPERII) {
		printk("lcd44780: initializing\n");
		lcd44780_init();
		lcd44780_puts("MontaVista Linux");
	}
}
