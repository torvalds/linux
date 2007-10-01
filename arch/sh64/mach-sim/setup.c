/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/mach-sim/setup.c
 *
 * ST50 Simulator Platform Support
 *
 * This file handles the architecture-dependent parts of initialization
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * lethal@linux-sh.org:          15th May 2003
 *    Use the generic procfs cpuinfo interface, just return a valid board name.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/platform.h>
#include <asm/irq.h>

/*
 * Platform Dependent Interrupt Priorities.
 */

/* Using defaults defined in irq.h */
#define	RES NO_PRIORITY		/* Disabled */
#define IR0 IRL0_PRIORITY	/* IRLs */
#define IR1 IRL1_PRIORITY
#define IR2 IRL2_PRIORITY
#define IR3 IRL3_PRIORITY
#define PCA INTA_PRIORITY	/* PCI Ints */
#define PCB INTB_PRIORITY
#define PCC INTC_PRIORITY
#define PCD INTD_PRIORITY
#define SER TOP_PRIORITY
#define ERR TOP_PRIORITY
#define PW0 TOP_PRIORITY
#define PW1 TOP_PRIORITY
#define PW2 TOP_PRIORITY
#define PW3 TOP_PRIORITY
#define DM0 NO_PRIORITY		/* DMA Ints */
#define DM1 NO_PRIORITY
#define DM2 NO_PRIORITY
#define DM3 NO_PRIORITY
#define DAE NO_PRIORITY
#define TU0 TIMER_PRIORITY	/* TMU Ints */
#define TU1 NO_PRIORITY
#define TU2 NO_PRIORITY
#define TI2 NO_PRIORITY
#define ATI NO_PRIORITY		/* RTC Ints */
#define PRI NO_PRIORITY
#define CUI RTC_PRIORITY
#define ERI SCIF_PRIORITY	/* SCIF Ints */
#define RXI SCIF_PRIORITY
#define BRI SCIF_PRIORITY
#define TXI SCIF_PRIORITY
#define ITI TOP_PRIORITY	/* WDT Ints */

/*
 * Platform dependent structures: maps and parms block.
 */
struct resource io_resources[] = {
	/* Nothing yet .. */
};

struct resource kram_resources[] = {
	/* These must be last in the array */
	{ .name = "Kernel code", .start = 0, .end = 0 },
	/* These must be last in the array */
	{ .name = "Kernel data", .start = 0, .end = 0 }
};

struct resource xram_resources[] = {
	/* Nothing yet .. */
};

struct resource rom_resources[] = {
	/* Nothing yet .. */
};

struct sh64_platform platform_parms = {
	.readonly_rootfs =	1,
	.initial_root_dev =	0x0100,
	.loader_type =		1,
	.io_res_p =		io_resources,
	.io_res_count =		ARRAY_SIZE(io_resources),
	.kram_res_p =		kram_resources,
	.kram_res_count =	ARRAY_SIZE(kram_resources),
	.xram_res_p =		xram_resources,
	.xram_res_count =	ARRAY_SIZE(xram_resources),
	.rom_res_p =		rom_resources,
	.rom_res_count =	ARRAY_SIZE(rom_resources),
};

int platform_int_priority[NR_IRQS] = {
	IR0, IR1, IR2, IR3, PCA, PCB, PCC, PCD,	/* IRQ  0- 7 */
	RES, RES, RES, RES, SER, ERR, PW3, PW2,	/* IRQ  8-15 */
	PW1, PW0, DM0, DM1, DM2, DM3, DAE, RES,	/* IRQ 16-23 */
	RES, RES, RES, RES, RES, RES, RES, RES,	/* IRQ 24-31 */
	TU0, TU1, TU2, TI2, ATI, PRI, CUI, ERI,	/* IRQ 32-39 */
	RXI, BRI, TXI, RES, RES, RES, RES, RES,	/* IRQ 40-47 */
	RES, RES, RES, RES, RES, RES, RES, RES,	/* IRQ 48-55 */
	RES, RES, RES, RES, RES, RES, RES, ITI,	/* IRQ 56-63 */
};

void __init platform_setup(void)
{
	/* Simulator platform leaves the decision to head.S */
	platform_parms.fpu_flags = fpu_in_use;
}

void __init platform_monitor(void)
{
	/* Nothing yet .. */
}

void __init platform_reserve(void)
{
	/* Nothing yet .. */
}

const char *get_system_type(void)
{
	return "SH-5 Simulator";
}
