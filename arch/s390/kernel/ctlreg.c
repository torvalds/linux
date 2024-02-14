// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright IBM Corp. 1999, 2023
 */

#include <linux/irqflags.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cache.h>
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

static bool system_ctlreg_area_init __ro_after_init;

void __init system_ctlreg_init_save_area(struct lowcore *lc)
{
	struct lowcore *abs_lc;

	abs_lc = get_abs_lowcore();
	__local_ctl_store(0, 15, lc->cregs_save_area);
	__local_ctl_store(0, 15, abs_lc->cregs_save_area);
	put_abs_lowcore(abs_lc);
	system_ctlreg_area_init = true;
}

struct ctlreg_parms {
	unsigned long andval;
	unsigned long orval;
	unsigned long val;
	int request;
	int cr;
};

static void ctlreg_callback(void *info)
{
	struct ctlreg_parms *pp = info;
	struct ctlreg regs[16];

	__local_ctl_store(0, 15, regs);
	if (pp->request == CTLREG_LOAD) {
		regs[pp->cr].val = pp->val;
	} else {
		regs[pp->cr].val &= pp->andval;
		regs[pp->cr].val |= pp->orval;
	}
	__local_ctl_load(0, 15, regs);
}

static void system_ctlreg_update(void *info)
{
	unsigned long flags;

	if (system_state == SYSTEM_BOOTING) {
		/*
		 * For very early calls do not call on_each_cpu()
		 * since not everything might be setup.
		 */
		local_irq_save(flags);
		ctlreg_callback(info);
		local_irq_restore(flags);
	} else {
		on_each_cpu(ctlreg_callback, info, 1);
	}
}

void system_ctlreg_modify(unsigned int cr, unsigned long data, int request)
{
	struct ctlreg_parms pp = { .cr = cr, .request = request, };
	struct lowcore *abs_lc;

	switch (request) {
	case CTLREG_SET_BIT:
		pp.orval  = 1UL << data;
		pp.andval = -1UL;
		break;
	case CTLREG_CLEAR_BIT:
		pp.orval  = 0;
		pp.andval = ~(1UL << data);
		break;
	case CTLREG_LOAD:
		pp.val = data;
		break;
	}
	if (system_ctlreg_area_init) {
		system_ctlreg_lock();
		abs_lc = get_abs_lowcore();
		if (request == CTLREG_LOAD) {
			abs_lc->cregs_save_area[cr].val = pp.val;
		} else {
			abs_lc->cregs_save_area[cr].val &= pp.andval;
			abs_lc->cregs_save_area[cr].val |= pp.orval;
		}
		put_abs_lowcore(abs_lc);
		system_ctlreg_update(&pp);
		system_ctlreg_unlock();
	} else {
		system_ctlreg_update(&pp);
	}
}
EXPORT_SYMBOL(system_ctlreg_modify);
