/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/module.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/lppaca.h>
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/it_lp_reg_save.h>
#include <asm/paca.h>


/* This symbol is provided by the linker - let it fill in the paca
 * field correctly */
extern unsigned long __toc_start;

/*
 * iSeries structure which the hypervisor knows about - this structure
 * should not cross a page boundary.  The vpa_init/register_vpa call
 * is now known to fail if the lppaca structure crosses a page
 * boundary.  The lppaca is also used on POWER5 pSeries boxes.  The
 * lppaca is 640 bytes long, and cannot readily change since the
 * hypervisor knows its layout, so a 1kB alignment will suffice to
 * ensure that it doesn't cross a page boundary.
 */
struct lppaca lppaca[] = {
	[0 ... (NR_CPUS-1)] = {
		.desc = 0xd397d781,	/* "LpPa" */
		.size = sizeof(struct lppaca),
		.dyn_proc_status = 2,
		.decr_val = 0x00ff0000,
		.fpregs_in_use = 1,
		.end_of_quantum = 0xfffffffffffffffful,
		.slb_count = 64,
		.vmxregs_in_use = 0,
	},
};

/* The Paca is an array with one entry per processor.  Each contains an
 * lppaca, which contains the information shared between the
 * hypervisor and Linux.
 * On systems with hardware multi-threading, there are two threads
 * per processor.  The Paca array must contain an entry for each thread.
 * The VPD Areas will give a max logical processors = 2 * max physical
 * processors.  The processor VPD array needs one entry per physical
 * processor (not thread).
 */
#define PACA_INIT_COMMON(number)					    \
	.lppaca_ptr = &lppaca[number],					    \
	.lock_token = 0x8000,						    \
	.paca_index = (number),		/* Paca Index */		    \
	.kernel_toc = (unsigned long)(&__toc_start) + 0x8000UL,		    \
	.hw_cpu_id = 0xffff,

#ifdef CONFIG_PPC_ISERIES
#define PACA_INIT_ISERIES(number)					    \
	.reg_save_ptr = &iseries_reg_save[number],

#define PACA_INIT(number)						    \
{									    \
	PACA_INIT_COMMON(number)					    \
	PACA_INIT_ISERIES(number)					    \
}

#else
#define PACA_INIT(number)						    \
{									    \
	PACA_INIT_COMMON(number)					    \
}

#endif

struct paca_struct paca[] = {
	PACA_INIT(0),
#if NR_CPUS > 1
	PACA_INIT(  1), PACA_INIT(  2), PACA_INIT(  3),
#if NR_CPUS > 4
	PACA_INIT(  4), PACA_INIT(  5), PACA_INIT(  6), PACA_INIT(  7),
#if NR_CPUS > 8
	PACA_INIT(  8), PACA_INIT(  9), PACA_INIT( 10), PACA_INIT( 11),
	PACA_INIT( 12), PACA_INIT( 13), PACA_INIT( 14), PACA_INIT( 15),
	PACA_INIT( 16), PACA_INIT( 17), PACA_INIT( 18), PACA_INIT( 19),
	PACA_INIT( 20), PACA_INIT( 21), PACA_INIT( 22), PACA_INIT( 23),
	PACA_INIT( 24), PACA_INIT( 25), PACA_INIT( 26), PACA_INIT( 27),
	PACA_INIT( 28), PACA_INIT( 29), PACA_INIT( 30), PACA_INIT( 31),
#if NR_CPUS > 32
	PACA_INIT( 32), PACA_INIT( 33), PACA_INIT( 34), PACA_INIT( 35),
	PACA_INIT( 36), PACA_INIT( 37), PACA_INIT( 38), PACA_INIT( 39),
	PACA_INIT( 40), PACA_INIT( 41), PACA_INIT( 42), PACA_INIT( 43),
	PACA_INIT( 44), PACA_INIT( 45), PACA_INIT( 46), PACA_INIT( 47),
	PACA_INIT( 48), PACA_INIT( 49), PACA_INIT( 50), PACA_INIT( 51),
	PACA_INIT( 52), PACA_INIT( 53), PACA_INIT( 54), PACA_INIT( 55),
	PACA_INIT( 56), PACA_INIT( 57), PACA_INIT( 58), PACA_INIT( 59),
	PACA_INIT( 60), PACA_INIT( 61), PACA_INIT( 62), PACA_INIT( 63),
#if NR_CPUS > 64
	PACA_INIT( 64), PACA_INIT( 65), PACA_INIT( 66), PACA_INIT( 67),
	PACA_INIT( 68), PACA_INIT( 69), PACA_INIT( 70), PACA_INIT( 71),
	PACA_INIT( 72), PACA_INIT( 73), PACA_INIT( 74), PACA_INIT( 75),
	PACA_INIT( 76), PACA_INIT( 77), PACA_INIT( 78), PACA_INIT( 79),
	PACA_INIT( 80), PACA_INIT( 81), PACA_INIT( 82), PACA_INIT( 83),
	PACA_INIT( 84), PACA_INIT( 85), PACA_INIT( 86), PACA_INIT( 87),
	PACA_INIT( 88), PACA_INIT( 89), PACA_INIT( 90), PACA_INIT( 91),
	PACA_INIT( 92), PACA_INIT( 93), PACA_INIT( 94), PACA_INIT( 95),
	PACA_INIT( 96), PACA_INIT( 97), PACA_INIT( 98), PACA_INIT( 99),
	PACA_INIT(100), PACA_INIT(101), PACA_INIT(102), PACA_INIT(103),
	PACA_INIT(104), PACA_INIT(105), PACA_INIT(106), PACA_INIT(107),
	PACA_INIT(108), PACA_INIT(109), PACA_INIT(110), PACA_INIT(111),
	PACA_INIT(112), PACA_INIT(113), PACA_INIT(114), PACA_INIT(115),
	PACA_INIT(116), PACA_INIT(117), PACA_INIT(118), PACA_INIT(119),
	PACA_INIT(120), PACA_INIT(121), PACA_INIT(122), PACA_INIT(123),
	PACA_INIT(124), PACA_INIT(125), PACA_INIT(126), PACA_INIT(127),
#endif
#endif
#endif
#endif
#endif
};
EXPORT_SYMBOL(paca);
