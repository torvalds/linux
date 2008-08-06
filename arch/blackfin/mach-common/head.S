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
#include <asm/trace.h>

/* A little BF561 glue ... */
#ifndef WDOG_CTL
# define WDOG_CTL WDOGA_CTL
#endif

__INIT

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
