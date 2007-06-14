/*
 * The generic setup file for PMC-Sierra MSP processors
 *
 * Copyright 2005-2007 PMC-Sierra, Inc,
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/r4kcache.h>
#include <asm/reboot.h>
#include <asm/time.h>

#include <msp_prom.h>
#include <msp_regs.h>

#if defined(CONFIG_PMC_MSP7120_GW)
#include <msp_regops.h>
#include <msp_gpio.h>
#define MSP_BOARD_RESET_GPIO	9
#endif

extern void msp_timer_init(void);
extern void msp_serial_setup(void);
extern void pmctwiled_setup(void);

#if defined(CONFIG_PMC_MSP7120_EVAL) || \
    defined(CONFIG_PMC_MSP7120_GW) || \
    defined(CONFIG_PMC_MSP7120_FPGA)
/*
 * Performs the reset for MSP7120-based boards
 */
void msp7120_reset(void)
{
	void *start, *end, *iptr;
	register int i;

	/* Diasble all interrupts */
	local_irq_disable();
#ifdef CONFIG_SYS_SUPPORTS_MULTITHREADING
	dvpe();
#endif

	/* Cache the reset code of this function */
	__asm__ __volatile__ (
		"	.set	push				\n"
		"	.set	mips3				\n"
		"	la	%0,startpoint			\n"
		"	la	%1,endpoint			\n"
		"	.set	pop				\n"
		: "=r" (start), "=r" (end)
		:
	);

	for (iptr = (void *)((unsigned int)start & ~(L1_CACHE_BYTES - 1));
	     iptr < end; iptr += L1_CACHE_BYTES)
		cache_op(Fill, iptr);

	__asm__ __volatile__ (
		"startpoint:					\n"
	);

	/* Put the DDRC into self-refresh mode */
	DDRC_INDIRECT_WRITE(DDRC_CTL(10), 0xb, 1 << 16);

	/*
	 * IMPORTANT!
	 * DO NOT do anything from here on out that might even
	 * think about fetching from RAM - i.e., don't call any
	 * non-inlined functions, and be VERY sure that any inline
	 * functions you do call do NOT access any sort of RAM
	 * anywhere!
	 */

	/* Wait a bit for the DDRC to settle */
	for (i = 0; i < 100000000; i++);

#if defined(CONFIG_PMC_MSP7120_GW)
	/*
	 * Set GPIO 9 HI, (tied to board reset logic)
	 * GPIO 9 is the 4th GPIO of register 3
	 *
	 * NOTE: We cannot use the higher-level msp_gpio_mode()/out()
	 * as GPIO char driver may not be enabled and it would look up
	 * data inRAM!
	 */
	set_value_reg32(GPIO_CFG3_REG,
			basic_mode_mask(MSP_BOARD_RESET_GPIO),
			basic_mode(MSP_GPIO_OUTPUT, MSP_BOARD_RESET_GPIO));
	set_reg32(GPIO_DATA3_REG,
			basic_data_mask(MSP_BOARD_RESET_GPIO));

	/*
	 * In case GPIO9 doesn't reset the board (jumper configurable!)
	 * fallback to device reset below.
	 */
#endif
	/* Set bit 1 of the MSP7120 reset register */
	*RST_SET_REG = 0x00000001;

	__asm__ __volatile__ (
		"endpoint:					\n"
	);
}
#endif

void msp_restart(char *command)
{
	printk(KERN_WARNING "Now rebooting .......\n");

#if defined(CONFIG_PMC_MSP7120_EVAL) || \
    defined(CONFIG_PMC_MSP7120_GW) || \
    defined(CONFIG_PMC_MSP7120_FPGA)
	msp7120_reset();
#else
	/* No chip-specific reset code, just jump to the ROM reset vector */
	set_c0_status(ST0_BEV | ST0_ERL);
	change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
	flush_cache_all();
	write_c0_wired(0);

	__asm__ __volatile__("jr\t%0"::"r"(0xbfc00000));
#endif
}

void msp_halt(void)
{
	printk(KERN_WARNING "\n** You can safely turn off the power\n");
	while (1)
		/* If possible call official function to get CPU WARs */
		if (cpu_wait)
			(*cpu_wait)();
		else
			__asm__(".set\tmips3\n\t" "wait\n\t" ".set\tmips0");
}

void msp_power_off(void)
{
	msp_halt();
}

void __init plat_mem_setup(void)
{
	_machine_restart = msp_restart;
	_machine_halt = msp_halt;
	pm_power_off = msp_power_off;

	board_time_init = msp_timer_init;
}

void __init prom_init(void)
{
	unsigned long family;
	unsigned long revision;

	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	/*
	 * Someday we can use this with PMON2000 to get a
	 * platform call prom routines for output etc. without
	 * having to use grody hacks.  For now it's unused.
	 *
	 * struct callvectors *cv = (struct callvectors *) fw_arg3;
	 */
	family = identify_family();
	revision = identify_revision();

	switch (family)	{
	case FAMILY_FPGA:
		if (FPGA_IS_MSP4200(revision)) {
			/* Old-style revision ID */
			mips_machgroup = MACH_GROUP_MSP;
			mips_machtype = MACH_MSP4200_FPGA;
		} else {
			mips_machgroup = MACH_GROUP_MSP;
			mips_machtype = MACH_MSP_OTHER;
		}
		break;

	case FAMILY_MSP4200:
		mips_machgroup = MACH_GROUP_MSP;
#if defined(CONFIG_PMC_MSP4200_EVAL)
		mips_machtype  = MACH_MSP4200_EVAL;
#elif defined(CONFIG_PMC_MSP4200_GW)
		mips_machtype  = MACH_MSP4200_GW;
#else
		mips_machtype = MACH_MSP_OTHER;
#endif
		break;

	case FAMILY_MSP4200_FPGA:
		mips_machgroup = MACH_GROUP_MSP;
		mips_machtype  = MACH_MSP4200_FPGA;
		break;

	case FAMILY_MSP7100:
		mips_machgroup = MACH_GROUP_MSP;
#if defined(CONFIG_PMC_MSP7120_EVAL)
		mips_machtype = MACH_MSP7120_EVAL;
#elif defined(CONFIG_PMC_MSP7120_GW)
		mips_machtype = MACH_MSP7120_GW;
#else
		mips_machtype = MACH_MSP_OTHER;
#endif
		break;

	case FAMILY_MSP7100_FPGA:
		mips_machgroup = MACH_GROUP_MSP;
		mips_machtype  = MACH_MSP7120_FPGA;
		break;

	default:
		/* we don't recognize the machine */
		mips_machgroup = MACH_GROUP_UNKNOWN;
		mips_machtype  = MACH_UNKNOWN;
		break;
	}

	/* make sure we have the right initialization routine - sanity */
	if (mips_machgroup != MACH_GROUP_MSP) {
		ppfinit("Unknown machine group in a "
			"MSP initialization routine\n");
		panic("***Bogosity factor five***, exiting\n");
	}

	prom_init_cmdline();

	prom_meminit();

	/*
	 * Sub-system setup follows.
	 * Setup functions can  either be called here or using the
	 * subsys_initcall mechanism (i.e. see msp_pci_setup). The
	 * order in which they are called can be changed by using the
	 * link order in arch/mips/pmc-sierra/msp71xx/Makefile.
	 *
	 * NOTE: Please keep sub-system specific initialization code
	 * in separate specific files.
	 */
	msp_serial_setup();

#ifdef CONFIG_PMCTWILED
	/*
	 * Setup LED states before the subsys_initcall loads other
	 * dependant drivers/modules.
	 */
	pmctwiled_setup();
#endif
}
