/**
 * @file backtrace.c
 *
 * @remark Copyright 2004 Silicon Graphics Inc.  All Rights Reserved.
 * @remark Read the file COPYING
 *
 * @author Greg Banks <gnb@melbourne.sgi.com>
 * @author Keith Owens <kaos@melbourne.sgi.com>
 * Based on work done for the ia64 port of the SGI kernprof patch, which is
 *    Copyright (c) 2003-2004 Silicon Graphics Inc.  All Rights Reserved.
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/system.h>

/*
 * For IA64 we need to perform a complex little dance to get both
 * the struct pt_regs and a synthetic struct switch_stack in place
 * to allow the unwind code to work.  This dance requires our unwind
 * using code to be called from a function called from unw_init_running().
 * There we only get a single void* data pointer, so use this struct
 * to hold all the data we need during the unwind.
 */
typedef struct
{
	unsigned int depth;
	struct pt_regs *regs;
	struct unw_frame_info frame;
	u64 *prev_pfs_loc;	/* state for WAR for old spinlock ool code */
} ia64_backtrace_t;

#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
/*
 * Returns non-zero if the PC is in the spinlock contention out-of-line code
 * with non-standard calling sequence (on older compilers).
 */
static __inline__ int in_old_ool_spinlock_code(unsigned long pc)
{
	extern const char ia64_spinlock_contention_pre3_4[] __attribute__ ((weak));
	extern const char ia64_spinlock_contention_pre3_4_end[] __attribute__ ((weak));
	unsigned long sc_start = (unsigned long)ia64_spinlock_contention_pre3_4;
	unsigned long sc_end = (unsigned long)ia64_spinlock_contention_pre3_4_end;
	return (sc_start && sc_end && pc >= sc_start && pc < sc_end);
}
#else
/* Newer spinlock code does a proper br.call and works fine with the unwinder */
#define in_old_ool_spinlock_code(pc)	0
#endif

/* Returns non-zero if the PC is in the Interrupt Vector Table */
static __inline__ int in_ivt_code(unsigned long pc)
{
	extern char ia64_ivt[];
	return (pc >= (u_long)ia64_ivt && pc < (u_long)ia64_ivt+32768);
}

/*
 * Unwind to next stack frame.
 */
static __inline__ int next_frame(ia64_backtrace_t *bt)
{
	/*
	 * Avoid unsightly console message from unw_unwind() when attempting
	 * to unwind through the Interrupt Vector Table which has no unwind
	 * information.
	 */
	if (in_ivt_code(bt->frame.ip))
		return 0;

	/*
	 * WAR for spinlock contention from leaf functions.  ia64_spinlock_contention_pre3_4
	 * has ar.pfs == r0.  Leaf functions do not modify ar.pfs so ar.pfs remains
	 * as 0, stopping the backtrace.  Record the previous ar.pfs when the current
	 * IP is in ia64_spinlock_contention_pre3_4 then unwind, if pfs_loc has not changed
	 * after unwind then use pt_regs.ar_pfs which is where the real ar.pfs is for
	 * leaf functions.
	 */
	if (bt->prev_pfs_loc && bt->regs && bt->frame.pfs_loc == bt->prev_pfs_loc)
		bt->frame.pfs_loc = &bt->regs->ar_pfs;
	bt->prev_pfs_loc = (in_old_ool_spinlock_code(bt->frame.ip) ? bt->frame.pfs_loc : NULL);

	return unw_unwind(&bt->frame) == 0;
}


static void do_ia64_backtrace(struct unw_frame_info *info, void *vdata)
{
	ia64_backtrace_t *bt = vdata;
	struct switch_stack *sw;
	int count = 0;
	u_long pc, sp;

	sw = (struct switch_stack *)(info+1);
	/* padding from unw_init_running */
	sw = (struct switch_stack *)(((unsigned long)sw + 15) & ~15);

	unw_init_frame_info(&bt->frame, current, sw);

	/* skip over interrupt frame and oprofile calls */
	do {
		unw_get_sp(&bt->frame, &sp);
		if (sp >= (u_long)bt->regs)
			break;
		if (!next_frame(bt))
			return;
	} while (count++ < 200);

	/* finally, grab the actual sample */
	while (bt->depth-- && next_frame(bt)) {
		unw_get_ip(&bt->frame, &pc);
		oprofile_add_trace(pc);
		if (unw_is_intr_frame(&bt->frame)) {
			/*
			 * Interrupt received on kernel stack; this can
			 * happen when timer interrupt fires while processing
			 * a softirq from the tail end of a hardware interrupt
			 * which interrupted a system call.  Don't laugh, it
			 * happens!  Splice the backtrace into two parts to
			 * avoid spurious cycles in the gprof output.
			 */
			/* TODO: split rather than drop the 2nd half */
			break;
		}
	}
}

void
ia64_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	ia64_backtrace_t bt;
	unsigned long flags;

	/*
	 * On IA64 there is little hope of getting backtraces from
	 * user space programs -- the problems of getting the unwind
	 * information from arbitrary user programs are extreme.
	 */
	if (user_mode(regs))
		return;

	bt.depth = depth;
	bt.regs = regs;
	bt.prev_pfs_loc = NULL;
	local_irq_save(flags);
	unw_init_running(do_ia64_backtrace, &bt);
	local_irq_restore(flags);
}
