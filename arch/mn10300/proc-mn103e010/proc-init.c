/* MN103E010 Processor initialisation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/irq.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/irq.h>
#include <asm/rtc.h>
#include <asm/busctl-regs.h>

/*
 * initialise the on-silicon processor peripherals
 */
asmlinkage void __init processor_init(void)
{
	int loop;

	/* set up the exception table first */
	for (loop = 0x000; loop < 0x400; loop += 8)
		__set_intr_stub(loop, __common_exception);

	__set_intr_stub(EXCEP_ITLBMISS,		itlb_miss);
	__set_intr_stub(EXCEP_DTLBMISS,		dtlb_miss);
	__set_intr_stub(EXCEP_IAERROR,		itlb_aerror);
	__set_intr_stub(EXCEP_DAERROR,		dtlb_aerror);
	__set_intr_stub(EXCEP_BUSERROR,		raw_bus_error);
	__set_intr_stub(EXCEP_DOUBLE_FAULT,	double_fault);
	__set_intr_stub(EXCEP_FPU_DISABLED,	fpu_disabled);
	__set_intr_stub(EXCEP_SYSCALL0,		system_call);

	__set_intr_stub(EXCEP_NMI,		nmi_handler);
	__set_intr_stub(EXCEP_WDT,		nmi_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL0,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL1,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL2,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL3,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL4,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL5,	irq_handler);
	__set_intr_stub(EXCEP_IRQ_LEVEL6,	irq_handler);

	IVAR0 = EXCEP_IRQ_LEVEL0;
	IVAR1 = EXCEP_IRQ_LEVEL1;
	IVAR2 = EXCEP_IRQ_LEVEL2;
	IVAR3 = EXCEP_IRQ_LEVEL3;
	IVAR4 = EXCEP_IRQ_LEVEL4;
	IVAR5 = EXCEP_IRQ_LEVEL5;
	IVAR6 = EXCEP_IRQ_LEVEL6;

	mn10300_dcache_flush_inv();
	mn10300_icache_inv();

	/* disable all interrupts and set to priority 6 (lowest) */
	for (loop = 0; loop < NR_IRQS; loop++)
		GxICR(loop) = GxICR_LEVEL_6 | GxICR_DETECT;

	/* clear the timers */
	TM0MD	= 0;
	TM1MD	= 0;
	TM2MD	= 0;
	TM3MD	= 0;
	TM4MD	= 0;
	TM5MD	= 0;
	TM6MD	= 0;
	TM6MDA	= 0;
	TM6MDB	= 0;
	TM7MD	= 0;
	TM8MD	= 0;
	TM9MD	= 0;
	TM10MD	= 0;
	TM11MD	= 0;

	calibrate_clock();
}

/*
 * determine the memory size and base from the memory controller regs
 */
void __init get_mem_info(unsigned long *mem_base, unsigned long *mem_size)
{
	unsigned long base, size;

	*mem_base = 0;
	*mem_size = 0;

	base = SDBASE(0);
	if (base & SDBASE_CE) {
		size = (base & SDBASE_CBAM) << SDBASE_CBAM_SHIFT;
		size = ~size + 1;
		base &= SDBASE_CBA;

		printk(KERN_INFO "SDRAM[0]: %luMb @%08lx\n", size >> 20, base);
		*mem_size += size;
		*mem_base = base;
	}

	base = SDBASE(1);
	if (base & SDBASE_CE) {
		size = (base & SDBASE_CBAM) << SDBASE_CBAM_SHIFT;
		size = ~size + 1;
		base &= SDBASE_CBA;

		printk(KERN_INFO "SDRAM[1]: %luMb @%08lx\n", size >> 20, base);
		*mem_size += size;
		if (*mem_base == 0)
			*mem_base = base;
	}
}
