// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright IBM Corp. 1999, 2023
 */

#include <linux/spinlock.h>
#include <linux/smp.h>
#include <asm/abs_lowcore.h>
#include <asm/ctlreg.h>

/*
 * ctl_lock guards access to global control register contents which
 * are kept in the control register save area within absolute lowcore
 * at physical address zero.
 */
static DEFINE_SPINLOCK(system_ctl_lock);

void system_ctlreg_lock(void)
	__acquires(&system_ctl_lock)
{
	spin_lock(&system_ctl_lock);
}

void system_ctlreg_unlock(void)
	__releases(&system_ctl_lock)
{
	spin_unlock(&system_ctl_lock);
}

struct ctl_bit_parms {
	unsigned long orval;
	unsigned long andval;
	int cr;
};

static void ctl_bit_callback(void *info)
{
	struct ctl_bit_parms *pp = info;
	unsigned long regs[16];

	__local_ctl_store(0, 15, regs);
	regs[pp->cr] &= pp->andval;
	regs[pp->cr] |= pp->orval;
	__local_ctl_load(0, 15, regs);
}

void system_ctl_set_clear_bit(unsigned int cr, unsigned int bit, bool set)
{
	struct ctl_bit_parms pp = { .cr = cr, };
	struct lowcore *abs_lc;

	pp.orval  = set ? 1UL << bit : 0;
	pp.andval = set ? -1UL : ~(1UL << bit);
	system_ctlreg_lock();
	abs_lc = get_abs_lowcore();
	abs_lc->cregs_save_area[cr] &= pp.andval;
	abs_lc->cregs_save_area[cr] |= pp.orval;
	put_abs_lowcore(abs_lc);
	on_each_cpu(ctl_bit_callback, &pp, 1);
	system_ctlreg_unlock();
}
EXPORT_SYMBOL(system_ctl_set_clear_bit);
