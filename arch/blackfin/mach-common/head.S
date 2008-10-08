/*
 * Common Blackfin startup code
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/blackfin.h>
#include <asm/thread_info.h>
#include <asm/trace.h>

__INIT

#define INITIAL_STACK	(L1_SCRATCH_START + L1_SCRATCH_LENGTH - 12)

ENTRY(__start)
	/* R0: argument of command line string, passed from uboot, save it */
	R7 = R0;
	/* Enable Cycle Counter and Nesting Of Interrupts */
#ifdef CONFIG_BFIN_SCRATCH_REG_CYCLES
	R0 = SYSCFG_SNEN;
#else
	R0 = SYSCFG_SNEN | SYSCFG_CCEN;
#endif
	SYSCFG = R0;
	R0 = 0;

	/* Clear Out All the data and pointer Registers */
	R1 = R0;
	R2 = R0;
	R3 = R0;
	R4 = R0;
	R5 = R0;
	R6 = R0;

	P0 = R0;
	P1 = R0;
	P2 = R0;
	P3 = R0;
	P4 = R0;
	P5 = R0;

	LC0 = r0;
	LC1 = r0;
	L0 = r0;
	L1 = r0;
	L2 = r0;
	L3 = r0;

	/* Clear Out All the DAG Registers */
	B0 = r0;
	B1 = r0;
	B2 = r0;
	B3 = r0;

	I0 = r0;
	I1 = r0;
	I2 = r0;
	I3 = r0;

	M0 = r0;
	M1 = r0;
	M2 = r0;
	M3 = r0;

	trace_buffer_init(p0,r0);
	P0 = R1;
	R0 = R1;

	/* Turn off the icache */
	p0.l = LO(IMEM_CONTROL);
	p0.h = HI(IMEM_CONTROL);
	R1 = [p0];
	R0 = ~ENICPLB;
	R0 = R0 & R1;
	[p0] = R0;
	SSYNC;

	/* Turn off the dcache */
	p0.l = LO(DMEM_CONTROL);
	p0.h = HI(DMEM_CONTROL);
	R1 = [p0];
	R0 = ~ENDCPLB;
	R0 = R0 & R1;
	[p0] = R0;
	SSYNC;

	/* in case of double faults, save a few things */
	p0.l = _init_retx;
	p0.h = _init_retx;
	R0 = RETX;
	[P0] = R0;

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* Only save these if we are storing them,
	 * This happens here, since L1 gets clobbered
	 * below
	 */
	p0.l = _saved_retx;
	p0.h = _saved_retx;
	p1.l = _init_saved_retx;
	p1.h = _init_saved_retx;
	r0 = [p0];
	[p1] = r0;

	p0.l = _saved_dcplb_fault_addr;
	p0.h = _saved_dcplb_fault_addr;
	p1.l = _init_saved_dcplb_fault_addr;
	p1.h = _init_saved_dcplb_fault_addr;
	r0 = [p0];
	[p1] = r0;

	p0.l = _saved_icplb_fault_addr;
	p0.h = _saved_icplb_fault_addr;
	p1.l = _init_saved_icplb_fault_addr;
	p1.h = _init_saved_icplb_fault_addr;
	r0 = [p0];
	[p1] = r0;

	p0.l = _saved_seqstat;
	p0.h = _saved_seqstat;
	p1.l = _init_saved_seqstat;
	p1.h = _init_saved_seqstat;
	r0 = [p0];
	[p1] = r0;
#endif

	/* Initialize stack pointer */
	sp.l = lo(INITIAL_STACK);
	sp.h = hi(INITIAL_STACK);
	fp = sp;
	usp = sp;

#ifdef CONFIG_EARLY_PRINTK
	call _init_early_exception_vectors;
#endif

	/* Put The Code for PLL Programming and SDRAM Programming in L1 ISRAM */
	call _bfin_relocate_l1_mem;
#ifdef CONFIG_BFIN_KERNEL_CLOCK
	call _start_dma_code;
#endif

	/* This section keeps the processor in supervisor mode
	 * during kernel boot.  Switches to user mode at end of boot.
	 * See page 3-9 of Hardware Reference manual for documentation.
	 */

	/* EVT15 = _real_start */

	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _real_start;
	p1.h = _real_start;
	[p0] = p1;
	csync;

	p0.l = lo(IMASK);
	p0.h = hi(IMASK);
	p1.l = IMASK_IVG15;
	p1.h = 0x0;
	[p0] = p1;
	csync;

	raise 15;
	p0.l = .LWAIT_HERE;
	p0.h = .LWAIT_HERE;
	reti = p0;
#if ANOMALY_05000281
	nop; nop; nop;
#endif
	rti;

.LWAIT_HERE:
	jump .LWAIT_HERE;
ENDPROC(__start)

/* A little BF561 glue ... */
#ifndef WDOG_CTL
# define WDOG_CTL WDOGA_CTL
#endif

ENTRY(_real_start)
	/* Enable nested interrupts */
	[--sp] = reti;

	/* watchdog off for now */
	p0.l = lo(WDOG_CTL);
	p0.h = hi(WDOG_CTL);
	r0 = 0xAD6(z);
	w[p0] = r0;
	ssync;

	/* Zero out the bss region
	 * Note: this will fail if bss is 0 bytes ...
	 */
	r0 = 0 (z);
	r1.l = ___bss_start;
	r1.h = ___bss_start;
	r2.l = ___bss_stop;
	r2.h = ___bss_stop;
	r2 = r2 - r1;
	r2 >>= 2;
	p1 = r1;
	p2 = r2;
	lsetup (.L_clear_bss, .L_clear_bss) lc0 = p2;
.L_clear_bss:
	[p1++] = r0;

	/* In case there is a NULL pointer reference,
	 * zero out region before stext
	 */
	p1 = r0;
	r2.l = __stext;
	r2.h = __stext;
	r2 >>= 2;
	p2 = r2;
	lsetup (.L_clear_zero, .L_clear_zero) lc0 = p2;
.L_clear_zero:
	[p1++] = r0;

	/* Pass the u-boot arguments to the global value command line */
	R0 = R7;
	call _cmdline_init;

	/* Load the current thread pointer and stack */
	sp.l = _init_thread_union;
	sp.h = _init_thread_union;
	p1 = THREAD_SIZE (z);
	sp = sp + p1;
	usp = sp;
	fp = sp;
	jump.l _start_kernel;
ENDPROC(_real_start)

__FINIT
