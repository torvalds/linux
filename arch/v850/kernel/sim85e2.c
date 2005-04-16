/*
 * arch/v850/kernel/sim85e2.c -- Machine-specific stuff for
 *	V850E2 RTL simulator
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/machdep.h>

#include "mach.h"


/* There are 4 possible areas we can use:

     IRAM (1MB) is fast for instruction fetches, but slow for data
     DRAM (1020KB) is fast for data, but slow for instructions
     ERAM is cached, so should be fast for both insns and data
     SDRAM is external DRAM, similar to ERAM
*/

#define INIT_MEMC_FOR_SDRAM
#define USE_SDRAM_AREA
#define KERNEL_IN_SDRAM_AREA

#define DCACHE_MODE	V850E2_CACHE_BTSC_DCM_WT
/*#define DCACHE_MODE	V850E2_CACHE_BTSC_DCM_WB_ALLOC*/

#ifdef USE_SDRAM_AREA
#define RAM_START 	SDRAM_ADDR
#define RAM_END		(SDRAM_ADDR + SDRAM_SIZE)
#else
/* When we use DRAM, we need to account for the fact that the end of it is
   used for R0_RAM.  */
#define RAM_START	DRAM_ADDR
#define RAM_END		R0_RAM_ADDR
#endif


extern void memcons_setup (void);


#ifdef KERNEL_IN_SDRAM_AREA
#define EARLY_INIT_SECTION_ATTR __attribute__ ((section (".early.text")))
#else
#define EARLY_INIT_SECTION_ATTR __init
#endif

void EARLY_INIT_SECTION_ATTR mach_early_init (void)
{
	/* The sim85e2 simulator tracks `undefined' values, so to make
	   debugging easier, we begin by zeroing out all otherwise
	   undefined registers.  This is not strictly necessary.

	   The registers we zero are:
	       Every GPR except:
	           stack-pointer (r3)
		   task-pointer (r16)
		   our return addr (r31)
	       Every system register (SPR) that we know about except for
	       the PSW (SPR 5), which we zero except for the
	       disable-interrupts bit.
	*/

	/* GPRs */
	asm volatile ("             mov r0, r1 ; mov r0, r2              ");
	asm volatile ("mov r0, r4 ; mov r0, r5 ; mov r0, r6 ; mov r0, r7 ");
	asm volatile ("mov r0, r8 ; mov r0, r9 ; mov r0, r10; mov r0, r11");
	asm volatile ("mov r0, r12; mov r0, r13; mov r0, r14; mov r0, r15");
	asm volatile ("             mov r0, r17; mov r0, r18; mov r0, r19");
	asm volatile ("mov r0, r20; mov r0, r21; mov r0, r22; mov r0, r23");
	asm volatile ("mov r0, r24; mov r0, r25; mov r0, r26; mov r0, r27");
	asm volatile ("mov r0, r28; mov r0, r29; mov r0, r30");

	/* SPRs */
	asm volatile ("ldsr r0, 0;  ldsr r0, 1;  ldsr r0, 2;  ldsr r0, 3");
	asm volatile ("ldsr r0, 4");
	asm volatile ("addi 0x20, r0, r1; ldsr r1, 5"); /* PSW */
	asm volatile ("ldsr r0, 16; ldsr r0, 17; ldsr r0, 18; ldsr r0, 19");
	asm volatile ("ldsr r0, 20");


#ifdef INIT_MEMC_FOR_SDRAM
	/* Settings for SDRAM controller.  */
	V850E2_VSWC   = 0x0042;
	V850E2_BSC    = 0x9286;
	V850E2_BCT(0) = 0xb000;	/* was: 0 */
	V850E2_BCT(1) = 0x000b;
	V850E2_ASC    = 0;
	V850E2_LBS    = 0xa9aa;	/* was: 0xaaaa */
	V850E2_LBC(0) = 0;
	V850E2_LBC(1) = 0;	/* was: 0x3 */
	V850E2_BCC    = 0;
	V850E2_RFS(4) = 0x800a;	/* was: 0xf109 */
	V850E2_SCR(4) = 0x2091;	/* was: 0x20a1 */
	V850E2_RFS(3) = 0x800c;
	V850E2_SCR(3) = 0x20a1;
	V850E2_DWC(0) = 0;
	V850E2_DWC(1) = 0;
#endif

#if 0
#ifdef CONFIG_V850E2_SIM85E2S
	/* Turn on the caches.  */
	V850E2_CACHE_BTSC = V850E2_CACHE_BTSC_ICM | DCACHE_MODE;
	V850E2_BHC  = 0x1010;
#elif CONFIG_V850E2_SIM85E2C
	V850E2_CACHE_BTSC |= (V850E2_CACHE_BTSC_ICM | V850E2_CACHE_BTSC_DCM0);
	V850E2_BUSM_BHC = 0xFFFF;
#endif
#else
	V850E2_BHC  = 0;
#endif

	/* Don't stop the simulator at `halt' instructions.  */
	SIM85E2_NOTHAL = 1;

	/* Ensure that the simulator halts on a panic, instead of going
	   into an infinite loop inside the panic function.  */
	panic_timeout = -1;
}

void __init mach_setup (char **cmdline)
{
	memcons_setup ();
}

void mach_get_physical_ram (unsigned long *ram_start, unsigned long *ram_len)
{
	*ram_start = RAM_START;
	*ram_len = RAM_END - RAM_START;
}

void __init mach_sched_init (struct irqaction *timer_action)
{
	/* The simulator actually cycles through all interrupts
	   periodically.  We just pay attention to IRQ0, which gives us
	   1/64 the rate of the periodic interrupts.  */
	setup_irq (0, timer_action);
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

/* Interrupts */

struct v850e_intc_irq_init irq_inits[] = {
	{ "IRQ", 0, NUM_MACH_IRQS, 1, 7 },
	{ 0 }
};
struct hw_interrupt_type hw_itypes[1];

/* Initialize interrupts.  */
void __init mach_init_irqs (void)
{
	v850e_intc_init_irq_types (irq_inits, hw_itypes);
}


void machine_halt (void) __attribute__ ((noreturn));
void machine_halt (void)
{
	SIM85E2_SIMFIN = 0;	/* Halt immediately.  */
	for (;;) {}
}

EXPORT_SYMBOL(machine_halt);

void machine_restart (char *__unused)
{
	machine_halt ();
}

EXPORT_SYMBOL(machine_restart);

void machine_power_off (void)
{
	machine_halt ();
}

EXPORT_SYMBOL(machine_power_off);
