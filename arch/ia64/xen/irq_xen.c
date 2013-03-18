/******************************************************************************
 * arch/ia64/xen/irq_xen.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/cpu.h>

#include <xen/interface/xen.h>
#include <xen/interface/callback.h>
#include <xen/events.h>

#include <asm/xen/privop.h>

#include "irq_xen.h"

/***************************************************************************
 * pv_irq_ops
 * irq operations
 */

static int
xen_assign_irq_vector(int irq)
{
	struct physdev_irq irq_op;

	irq_op.irq = irq;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_alloc_irq_vector, &irq_op))
		return -ENOSPC;

	return irq_op.vector;
}

static void
xen_free_irq_vector(int vector)
{
	struct physdev_irq irq_op;

	if (vector < IA64_FIRST_DEVICE_VECTOR ||
	    vector > IA64_LAST_DEVICE_VECTOR)
		return;

	irq_op.vector = vector;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_free_irq_vector, &irq_op))
		printk(KERN_WARNING "%s: xen_free_irq_vector fail vector=%d\n",
		       __func__, vector);
}


static DEFINE_PER_CPU(int, xen_timer_irq) = -1;
static DEFINE_PER_CPU(int, xen_ipi_irq) = -1;
static DEFINE_PER_CPU(int, xen_resched_irq) = -1;
static DEFINE_PER_CPU(int, xen_cmc_irq) = -1;
static DEFINE_PER_CPU(int, xen_cmcp_irq) = -1;
static DEFINE_PER_CPU(int, xen_cpep_irq) = -1;
#define NAME_SIZE	15
static DEFINE_PER_CPU(char[NAME_SIZE], xen_timer_name);
static DEFINE_PER_CPU(char[NAME_SIZE], xen_ipi_name);
static DEFINE_PER_CPU(char[NAME_SIZE], xen_resched_name);
static DEFINE_PER_CPU(char[NAME_SIZE], xen_cmc_name);
static DEFINE_PER_CPU(char[NAME_SIZE], xen_cmcp_name);
static DEFINE_PER_CPU(char[NAME_SIZE], xen_cpep_name);
#undef NAME_SIZE

struct saved_irq {
	unsigned int irq;
	struct irqaction *action;
};
/* 16 should be far optimistic value, since only several percpu irqs
 * are registered early.
 */
#define MAX_LATE_IRQ	16
static struct saved_irq saved_percpu_irqs[MAX_LATE_IRQ];
static unsigned short late_irq_cnt;
static unsigned short saved_irq_cnt;
static int xen_slab_ready;

#ifdef CONFIG_SMP
#include <linux/sched.h>

/* Dummy stub. Though we may check XEN_RESCHEDULE_VECTOR before __do_IRQ,
 * it ends up to issue several memory accesses upon percpu data and
 * thus adds unnecessary traffic to other paths.
 */
static irqreturn_t
xen_dummy_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t
xen_resched_handler(int irq, void *dev_id)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

static struct irqaction xen_ipi_irqaction = {
	.handler =	handle_IPI,
	.flags =	IRQF_DISABLED,
	.name =		"IPI"
};

static struct irqaction xen_resched_irqaction = {
	.handler =	xen_resched_handler,
	.flags =	IRQF_DISABLED,
	.name =		"resched"
};

static struct irqaction xen_tlb_irqaction = {
	.handler =	xen_dummy_handler,
	.flags =	IRQF_DISABLED,
	.name =		"tlb_flush"
};
#endif

/*
 * This is xen version percpu irq registration, which needs bind
 * to xen specific evtchn sub-system. One trick here is that xen
 * evtchn binding interface depends on kmalloc because related
 * port needs to be freed at device/cpu down. So we cache the
 * registration on BSP before slab is ready and then deal them
 * at later point. For rest instances happening after slab ready,
 * we hook them to xen evtchn immediately.
 *
 * FIXME: MCA is not supported by far, and thus "nomca" boot param is
 * required.
 */
static void
__xen_register_percpu_irq(unsigned int cpu, unsigned int vec,
			struct irqaction *action, int save)
{
	int irq = 0;

	if (xen_slab_ready) {
		switch (vec) {
		case IA64_TIMER_VECTOR:
			snprintf(per_cpu(xen_timer_name, cpu),
				 sizeof(per_cpu(xen_timer_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_virq_to_irqhandler(VIRQ_ITC, cpu,
				action->handler, action->flags,
				per_cpu(xen_timer_name, cpu), action->dev_id);
			per_cpu(xen_timer_irq, cpu) = irq;
			break;
		case IA64_IPI_RESCHEDULE:
			snprintf(per_cpu(xen_resched_name, cpu),
				 sizeof(per_cpu(xen_resched_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_ipi_to_irqhandler(XEN_RESCHEDULE_VECTOR, cpu,
				action->handler, action->flags,
				per_cpu(xen_resched_name, cpu), action->dev_id);
			per_cpu(xen_resched_irq, cpu) = irq;
			break;
		case IA64_IPI_VECTOR:
			snprintf(per_cpu(xen_ipi_name, cpu),
				 sizeof(per_cpu(xen_ipi_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_ipi_to_irqhandler(XEN_IPI_VECTOR, cpu,
				action->handler, action->flags,
				per_cpu(xen_ipi_name, cpu), action->dev_id);
			per_cpu(xen_ipi_irq, cpu) = irq;
			break;
		case IA64_CMC_VECTOR:
			snprintf(per_cpu(xen_cmc_name, cpu),
				 sizeof(per_cpu(xen_cmc_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_virq_to_irqhandler(VIRQ_MCA_CMC, cpu,
						action->handler,
						action->flags,
						per_cpu(xen_cmc_name, cpu),
						action->dev_id);
			per_cpu(xen_cmc_irq, cpu) = irq;
			break;
		case IA64_CMCP_VECTOR:
			snprintf(per_cpu(xen_cmcp_name, cpu),
				 sizeof(per_cpu(xen_cmcp_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_ipi_to_irqhandler(XEN_CMCP_VECTOR, cpu,
						action->handler,
						action->flags,
						per_cpu(xen_cmcp_name, cpu),
						action->dev_id);
			per_cpu(xen_cmcp_irq, cpu) = irq;
			break;
		case IA64_CPEP_VECTOR:
			snprintf(per_cpu(xen_cpep_name, cpu),
				 sizeof(per_cpu(xen_cpep_name, cpu)),
				 "%s%d", action->name, cpu);
			irq = bind_ipi_to_irqhandler(XEN_CPEP_VECTOR, cpu,
						action->handler,
						action->flags,
						per_cpu(xen_cpep_name, cpu),
						action->dev_id);
			per_cpu(xen_cpep_irq, cpu) = irq;
			break;
		case IA64_CPE_VECTOR:
		case IA64_MCA_RENDEZ_VECTOR:
		case IA64_PERFMON_VECTOR:
		case IA64_MCA_WAKEUP_VECTOR:
		case IA64_SPURIOUS_INT_VECTOR:
			/* No need to complain, these aren't supported. */
			break;
		default:
			printk(KERN_WARNING "Percpu irq %d is unsupported "
			       "by xen!\n", vec);
			break;
		}
		BUG_ON(irq < 0);

		if (irq > 0) {
			/*
			 * Mark percpu.  Without this, migrate_irqs() will
			 * mark the interrupt for migrations and trigger it
			 * on cpu hotplug.
			 */
			irq_set_status_flags(irq, IRQ_PER_CPU);
		}
	}

	/* For BSP, we cache registered percpu irqs, and then re-walk
	 * them when initializing APs
	 */
	if (!cpu && save) {
		BUG_ON(saved_irq_cnt == MAX_LATE_IRQ);
		saved_percpu_irqs[saved_irq_cnt].irq = vec;
		saved_percpu_irqs[saved_irq_cnt].action = action;
		saved_irq_cnt++;
		if (!xen_slab_ready)
			late_irq_cnt++;
	}
}

static void
xen_register_percpu_irq(ia64_vector vec, struct irqaction *action)
{
	__xen_register_percpu_irq(smp_processor_id(), vec, action, 1);
}

static void
xen_bind_early_percpu_irq(void)
{
	int i;

	xen_slab_ready = 1;
	/* There's no race when accessing this cached array, since only
	 * BSP will face with such step shortly
	 */
	for (i = 0; i < late_irq_cnt; i++)
		__xen_register_percpu_irq(smp_processor_id(),
					  saved_percpu_irqs[i].irq,
					  saved_percpu_irqs[i].action, 0);
}

/* FIXME: There's no obvious point to check whether slab is ready. So
 * a hack is used here by utilizing a late time hook.
 */

#ifdef CONFIG_HOTPLUG_CPU
static int unbind_evtchn_callback(struct notifier_block *nfb,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	if (action == CPU_DEAD) {
		/* Unregister evtchn.  */
		if (per_cpu(xen_cpep_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_cpep_irq, cpu),
					       NULL);
			per_cpu(xen_cpep_irq, cpu) = -1;
		}
		if (per_cpu(xen_cmcp_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_cmcp_irq, cpu),
					       NULL);
			per_cpu(xen_cmcp_irq, cpu) = -1;
		}
		if (per_cpu(xen_cmc_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_cmc_irq, cpu), NULL);
			per_cpu(xen_cmc_irq, cpu) = -1;
		}
		if (per_cpu(xen_ipi_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_ipi_irq, cpu), NULL);
			per_cpu(xen_ipi_irq, cpu) = -1;
		}
		if (per_cpu(xen_resched_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_resched_irq, cpu),
					       NULL);
			per_cpu(xen_resched_irq, cpu) = -1;
		}
		if (per_cpu(xen_timer_irq, cpu) >= 0) {
			unbind_from_irqhandler(per_cpu(xen_timer_irq, cpu),
					       NULL);
			per_cpu(xen_timer_irq, cpu) = -1;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block unbind_evtchn_notifier = {
	.notifier_call = unbind_evtchn_callback,
	.priority = 0
};
#endif

void xen_smp_intr_init_early(unsigned int cpu)
{
#ifdef CONFIG_SMP
	unsigned int i;

	for (i = 0; i < saved_irq_cnt; i++)
		__xen_register_percpu_irq(cpu, saved_percpu_irqs[i].irq,
					  saved_percpu_irqs[i].action, 0);
#endif
}

void xen_smp_intr_init(void)
{
#ifdef CONFIG_SMP
	unsigned int cpu = smp_processor_id();
	struct callback_register event = {
		.type = CALLBACKTYPE_event,
		.address = { .ip = (unsigned long)&xen_event_callback },
	};

	if (cpu == 0) {
		/* Initialization was already done for boot cpu.  */
#ifdef CONFIG_HOTPLUG_CPU
		/* Register the notifier only once.  */
		register_cpu_notifier(&unbind_evtchn_notifier);
#endif
		return;
	}

	/* This should be piggyback when setup vcpu guest context */
	BUG_ON(HYPERVISOR_callback_op(CALLBACKOP_register, &event));
#endif /* CONFIG_SMP */
}

void __init
xen_irq_init(void)
{
	struct callback_register event = {
		.type = CALLBACKTYPE_event,
		.address = { .ip = (unsigned long)&xen_event_callback },
	};

	xen_init_IRQ();
	BUG_ON(HYPERVISOR_callback_op(CALLBACKOP_register, &event));
	late_time_init = xen_bind_early_percpu_irq;
}

void
xen_platform_send_ipi(int cpu, int vector, int delivery_mode, int redirect)
{
#ifdef CONFIG_SMP
	/* TODO: we need to call vcpu_up here */
	if (unlikely(vector == ap_wakeup_vector)) {
		/* XXX
		 * This should be in __cpu_up(cpu) in ia64 smpboot.c
		 * like x86. But don't want to modify it,
		 * keep it untouched.
		 */
		xen_smp_intr_init_early(cpu);

		xen_send_ipi(cpu, vector);
		/* vcpu_prepare_and_up(cpu); */
		return;
	}
#endif

	switch (vector) {
	case IA64_IPI_VECTOR:
		xen_send_IPI_one(cpu, XEN_IPI_VECTOR);
		break;
	case IA64_IPI_RESCHEDULE:
		xen_send_IPI_one(cpu, XEN_RESCHEDULE_VECTOR);
		break;
	case IA64_CMCP_VECTOR:
		xen_send_IPI_one(cpu, XEN_CMCP_VECTOR);
		break;
	case IA64_CPEP_VECTOR:
		xen_send_IPI_one(cpu, XEN_CPEP_VECTOR);
		break;
	case IA64_TIMER_VECTOR: {
		/* this is used only once by check_sal_cache_flush()
		   at boot time */
		static int used = 0;
		if (!used) {
			xen_send_ipi(cpu, IA64_TIMER_VECTOR);
			used = 1;
			break;
		}
		/* fallthrough */
	}
	default:
		printk(KERN_WARNING "Unsupported IPI type 0x%x\n",
		       vector);
		notify_remote_via_irq(0); /* defaults to 0 irq */
		break;
	}
}

static void __init
xen_register_ipi(void)
{
#ifdef CONFIG_SMP
	register_percpu_irq(IA64_IPI_VECTOR, &xen_ipi_irqaction);
	register_percpu_irq(IA64_IPI_RESCHEDULE, &xen_resched_irqaction);
	register_percpu_irq(IA64_IPI_LOCAL_TLB_FLUSH, &xen_tlb_irqaction);
#endif
}

static void
xen_resend_irq(unsigned int vector)
{
	(void)resend_irq_on_evtchn(vector);
}

const struct pv_irq_ops xen_irq_ops __initconst = {
	.register_ipi = xen_register_ipi,

	.assign_irq_vector = xen_assign_irq_vector,
	.free_irq_vector = xen_free_irq_vector,
	.register_percpu_irq = xen_register_percpu_irq,

	.resend_irq = xen_resend_irq,
};
