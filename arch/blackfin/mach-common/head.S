/*
 * Common Blackfin startup code
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/blackfin.h>
#include <asm/thread_info.h>
#include <asm/trace.h>
#include <asm/asm-offsets.h>

__INIT

ENTRY(__init_clear_bss)
	r2 = r2 - r1;
	cc = r2 == 0;
	if cc jump .L_bss_done;
	r2 >>= 2;
	p1 = r1;
	p2 = r2;
	lsetup (1f, 1f) lc0 = p2;
1:	[p1++] = r0;
.L_bss_done:
	rts;
ENDPROC(__init_clear_bss)

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

	/* Optimization register tricks: keep a base value in the
	 * reserved P registers so we use the load/store with an
	 * offset syntax.  R0 = [P5 + <constant>];
	 *   P5 - core MMR base
	 *   R6 - 0
	 */
	r6 = 0;
	p5.l = 0;
	p5.h = hi(COREMMR_BASE);

	/* Zero out registers required by Blackfin ABI */

	/* Disable circular buffers */
	L0 = r6;
	L1 = r6;
	L2 = r6;
	L3 = r6;

	/* Disable hardware loops in case we were started by 'go' */
	LC0 = r6;
	LC1 = r6;

	/*
	 * Clear ITEST_COMMAND and DTEST_COMMAND registers,
	 * Leaving these as non-zero can confuse the emulator
	 */
	[p5 + (DTEST_COMMAND - COREMMR_BASE)] = r6;
	[p5 + (ITEST_COMMAND - COREMMR_BASE)] = r6;
	CSYNC;

	trace_buffer_init(p0,r0);

	/* Turn off the icache */
	r1 = [p5 + (IMEM_CONTROL - COREMMR_BASE)];
	BITCLR (r1, ENICPLB_P);
	[p5 + (IMEM_CONTROL - COREMMR_BASE)] = r1;
	SSYNC;

	/* Turn off the dcache */
	r1 = [p5 + (DMEM_CONTROL - COREMMR_BASE)];
	BITCLR (r1, ENDCPLB_P);
	[p5 + (DMEM_CONTROL - COREMMR_BASE)] = r1;
	SSYNC;

	/* in case of double faults, save a few things */
	p1.l = _initial_pda;
	p1.h = _initial_pda;
	r4 = RETX;
#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* Only save these if we are storing them,
	 * This happens here, since L1 gets clobbered
	 * below
	 */
	GET_PDA(p0, r0);
	r0 = [p0 + PDA_DF_RETX];
	r1 = [p0 + PDA_DF_DCPLB];
	r2 = [p0 + PDA_DF_ICPLB];
	r3 = [p0 + PDA_DF_SEQSTAT];
	[p1 + PDA_INIT_DF_RETX] = r0;
	[p1 + PDA_INIT_DF_DCPLB] = r1;
	[p1 + PDA_INIT_DF_ICPLB] = r2;
	[p1 + PDA_INIT_DF_SEQSTAT] = r3;
#endif
	[p1 + PDA_INIT_RETX] = r4;

	/* Initialize stack pointer */
	sp.l = _init_thread_union + THREAD_SIZE;
	sp.h = _init_thread_union + THREAD_SIZE;
	fp = sp;
	usp = sp;

#ifdef CONFIG_EARLY_PRINTK
	call _init_early_exception_vectors;
	r0 = (EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
	sti r0;
#endif

	r0 = r6;
	/* Zero out all of the fun bss regions */
#if L1_DATA_A_LENGTH > 0
	r1.l = __sbss_l1;
	r1.h = __sbss_l1;
	r2.l = __ebss_l1;
	r2.h = __ebss_l1;
	call __init_clear_bss
#endif
#if L1_DATA_B_LENGTH > 0
	r1.l = __sbss_b_l1;
	r1.h = __sbss_b_l1;
	r2.l = __ebss_b_l1;
	r2.h = __ebss_b_l1;
	call __init_clear_bss
#endif
#if L2_LENGTH > 0
	r1.l = __sbss_l2;
	r1.h = __sbss_l2;
	r2.l = __ebss_l2;
	r2.h = __ebss_l2;
	call __init_clear_bss
#endif
	r1.l = ___bss_start;
	r1.h = ___bss_start;
	r2.l = ___bss_stop;
	r2.h = ___bss_stop;
	call __init_clear_bss

	/* Put The Code for PLL Programming and SDRAM Programming in L1 ISRAM */
	call _bfin_relocate_l1_mem;

#ifdef CONFIG_ROMKERNEL
	call _bfin_relocate_xip_data;
#endif

#ifdef CONFIG_BFIN_KERNEL_CLOCK
	/* Only use on-chip scratch space for stack when absolutely required
	 * to avoid Anomaly 05000227 ... we know the init_clocks() func only
	 * uses L1 text and stack space and no other memory region.
	 */
# define KERNEL_CLOCK_STACK (L1_SCRATCH_START + L1_SCRATCH_LENGTH - 12)
	sp.l = lo(KERNEL_CLOCK_STACK);
	sp.h = hi(KERNEL_CLOCK_STACK);
	call _init_clocks;
	sp = usp;	/* usp hasn't been touched, so restore from there */
#endif

	/* This section keeps the processor in supervisor mode
	 * during kernel boot.  Switches to user mode at end of boot.
	 * See page 3-9 of Hardware Reference manual for documentation.
	 */

	/* EVT15 = _real_start */

	p1.l = _real_start;
	p1.h = _real_start;
	[p5 + (EVT15 - COREMMR_BASE)] = p1;
	csync;

#ifdef CONFIG_EARLY_PRINTK
	r0 = (EVT_IVG15 | EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU) (z);
#else
	r0 = EVT_IVG15 (z);
#endif
	sti r0;

	raise 15;
#ifdef CONFIG_EARLY_PRINTK
	p0.l = _early_trap;
	p0.h = _early_trap;
#else
	p0.l = .LWAIT_HERE;
	p0.h = .LWAIT_HERE;
#endif
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

	/* Pass the u-boot arguments to the global value command line */
	R0 = R7;
	call _cmdline_init;

	sp += -12 + 4; /* +4 is for reti loading above */
	call _init_pda
	sp += 12;
	jump.l _start_kernel;
ENDPROC(_real_start)

__FINIT
