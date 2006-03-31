/*
 * CBE Pervasive Monitor and Debug
 *
 * (C) Copyright IBM Corporation 2005
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          Michael N. Day (mnday@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/kallsyms.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/reg.h>

#include "pervasive.h"

static DEFINE_SPINLOCK(cbe_pervasive_lock);
struct cbe_pervasive {
	struct pmd_regs __iomem *regs;
	unsigned int thread;
};

/* can't use per_cpu from setup_arch */
static struct cbe_pervasive cbe_pervasive[NR_CPUS];

static void __init cbe_enable_pause_zero(void)
{
	unsigned long thread_switch_control;
	unsigned long temp_register;
	struct cbe_pervasive *p;
	int thread;

	spin_lock_irq(&cbe_pervasive_lock);
	p = &cbe_pervasive[smp_processor_id()];

	if (!cbe_pervasive->regs)
		goto out;

	pr_debug("Power Management: CPU %d\n", smp_processor_id());

	 /* Enable Pause(0) control bit */
	temp_register = in_be64(&p->regs->pm_control);

	out_be64(&p->regs->pm_control,
		 temp_register|PMD_PAUSE_ZERO_CONTROL);

	/* Enable DEC and EE interrupt request */
	thread_switch_control  = mfspr(SPRN_TSC_CELL);
	thread_switch_control |= TSC_CELL_EE_ENABLE | TSC_CELL_EE_BOOST;

	switch ((mfspr(SPRN_CTRLF) & CTRL_CT)) {
	case CTRL_CT0:
		thread_switch_control |= TSC_CELL_DEC_ENABLE_0;
		thread = 0;
		break;
	case CTRL_CT1:
		thread_switch_control |= TSC_CELL_DEC_ENABLE_1;
		thread = 1;
		break;
	default:
		printk(KERN_WARNING "%s: unknown configuration\n",
			__FUNCTION__);
		thread = -1;
		break;
	}

	if (p->thread != thread)
		printk(KERN_WARNING "%s: device tree inconsistant, "
				     "cpu %i: %d/%d\n", __FUNCTION__,
				     smp_processor_id(),
				     p->thread, thread);

	mtspr(SPRN_TSC_CELL, thread_switch_control);

out:
	spin_unlock_irq(&cbe_pervasive_lock);
}

static void cbe_idle(void)
{
	unsigned long ctrl;

	cbe_enable_pause_zero();

	while (1) {
		if (!need_resched()) {
			local_irq_disable();
			while (!need_resched()) {
				/* go into low thread priority */
				HMT_low();

				/*
				 * atomically disable thread execution
				 * and runlatch.
				 * External and Decrementer exceptions
				 * are still handled when the thread
				 * is disabled but now enter in
				 * cbe_system_reset_exception()
				 */
				ctrl = mfspr(SPRN_CTRLF);
				ctrl &= ~(CTRL_RUNLATCH | CTRL_TE);
				mtspr(SPRN_CTRLT, ctrl);
			}
			/* restore thread prio */
			HMT_medium();
			local_irq_enable();
		}

		/*
		 * turn runlatch on again before scheduling the
		 * process we just woke up
		 */
		ppc64_runlatch_on();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

static int cbe_system_reset_exception(struct pt_regs *regs)
{
	switch (regs->msr & SRR1_WAKEMASK) {
	case SRR1_WAKEEE:
		do_IRQ(regs);
		break;
	case SRR1_WAKEDEC:
		timer_interrupt(regs);
		break;
	case SRR1_WAKEMT:
		/* no action required */
		break;
	default:
		/* do system reset */
		return 0;
	}
	/* everything handled */
	return 1;
}

static int __init cbe_find_pmd_mmio(int cpu, struct cbe_pervasive *p)
{
	struct device_node *node;
	unsigned int *int_servers;
	char *addr;
	unsigned long real_address;
	unsigned int size;

	struct pmd_regs __iomem *pmd_mmio_area;
	int hardid, thread;
	int proplen;

	pmd_mmio_area = NULL;
	hardid = get_hard_smp_processor_id(cpu);
	for (node = NULL; (node = of_find_node_by_type(node, "cpu"));) {
		int_servers = (void *) get_property(node,
				"ibm,ppc-interrupt-server#s", &proplen);
		if (!int_servers) {
			printk(KERN_WARNING "%s misses "
				"ibm,ppc-interrupt-server#s property",
				node->full_name);
			continue;
		}
		for (thread = 0; thread < proplen / sizeof (int); thread++) {
			if (hardid == int_servers[thread]) {
				addr = get_property(node, "pervasive", NULL);
				goto found;
			}
		}
	}

	printk(KERN_WARNING "%s: CPU %d not found\n", __FUNCTION__, cpu);
	return -EINVAL;

found:
	real_address = *(unsigned long*) addr;
	addr += sizeof (unsigned long);
	size = *(unsigned int*) addr;

	pr_debug("pervasive area for CPU %d at %lx, size %x\n",
			cpu, real_address, size);
	p->regs = ioremap(real_address, size);
	p->thread = thread;
	return 0;
}

void __init cell_pervasive_init(void)
{
	struct cbe_pervasive *p;
	int cpu;
	int ret;

	if (!cpu_has_feature(CPU_FTR_PAUSE_ZERO))
		return;

	for_each_possible_cpu(cpu) {
		p = &cbe_pervasive[cpu];
		ret = cbe_find_pmd_mmio(cpu, p);
		if (ret)
			return;
	}

	ppc_md.idle_loop = cbe_idle;
	ppc_md.system_reset_exception = cbe_system_reset_exception;
}
