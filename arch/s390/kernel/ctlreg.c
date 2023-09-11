// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright IBM Corp. 1999, 2023
 */

#include <linux/spinlock.h>
#include <linux/smp.h>
#include <asm/abs_lowcore.h>
#include <asm/ctl_reg.h>

/*
 * ctl_lock guards access to global control register contents which
 * are kept in the control register save area within absolute lowcore
 * at physical address zero.
 */
static DEFINE_SPINLOCK(ctl_lock);

void ctlreg_lock(void)
	__acquires(&ctl_lock)
{
	spin_lock(&ctl_lock);
}

void ctlreg_unlock(void)
	__releases(&ctl_lock)
{
	spin_unlock(&ctl_lock);
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

	__ctl_store(regs, 0, 15);
	regs[pp->cr] &= pp->andval;
	regs[pp->cr] |= pp->orval;
	__ctl_load(regs, 0, 15);
}

void ctl_set_clear_bit(int cr, int bit, bool set)
{
	struct ctl_bit_parms pp = { .cr = cr, };
	struct lowcore *abs_lc;

	pp.orval  = set ? 1UL << bit : 0;
	pp.andval = set ? -1UL : ~(1UL << bit);
	ctlreg_lock();
	abs_lc = get_abs_lowcore();
	abs_lc->cregs_save_area[cr] &= pp.andval;
	abs_lc->cregs_save_area[cr] |= pp.orval;
	put_abs_lowcore(abs_lc);
	on_each_cpu(ctl_bit_callback, &pp, 1);
	ctlreg_unlock();
}
EXPORT_SYMBOL(ctl_set_clear_bit);
