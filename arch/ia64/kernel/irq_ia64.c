/*
 * linux/arch/ia64/kernel/irq_ia64.c
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *  6/10/99: Updated to bring in sync with x86 version to facilitate
 *	     support for SMP and different interrupt controllers.
 *
 * 09/15/00 Goutham Rao <goutham.rao@intel.com> Implemented pci_irq_to_vector
 *                      PCI to vector allocation routine.
 * 04/14/2004 Ashok Raj <ashok.raj@intel.com>
 *						Added CPU Hotplug handling for IPF.
 */

#include <linux/module.h>

#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/random.h>	/* for rand_initialize_irq() */
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/bitops.h>
#include <linux/irq.h>

#include <asm/delay.h>
#include <asm/intrinsics.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_PERFMON
# include <asm/perfmon.h>
#endif

#define IRQ_DEBUG	0

#define IRQ_VECTOR_UNASSIGNED	(0)

#define IRQ_UNUSED		(0)
#define IRQ_USED		(1)
#define IRQ_RSVD		(2)

/* These can be overridden in platform_irq_init */
int ia64_first_device_vector = IA64_DEF_FIRST_DEVICE_VECTOR;
int ia64_last_device_vector = IA64_DEF_LAST_DEVICE_VECTOR;

/* default base addr of IPI table */
void __iomem *ipi_base_addr = ((void __iomem *)
			       (__IA64_UNCACHED_OFFSET | IA64_IPI_DEFAULT_BASE_ADDR));

static cpumask_t vector_allocation_domain(int cpu);

/*
 * Legacy IRQ to IA-64 vector translation table.
 */
__u8 isa_irq_to_vector_map[16] = {
	/* 8259 IRQ translation, first 16 entries */
	0x2f, 0x20, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29,
	0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21
};
EXPORT_SYMBOL(isa_irq_to_vector_map);

DEFINE_SPINLOCK(vector_lock);

struct irq_cfg irq_cfg[NR_IRQS] __read_mostly = {
	[0 ... NR_IRQS - 1] = {
		.vector = IRQ_VECTOR_UNASSIGNED,
		.domain = CPU_MASK_NONE
	}
};

DEFINE_PER_CPU(int[IA64_NUM_VECTORS], vector_irq) = {
	[0 ... IA64_NUM_VECTORS - 1] = IA64_SPURIOUS_INT_VECTOR
};

static cpumask_t vector_table[IA64_NUM_VECTORS] = {
	[0 ... IA64_NUM_VECTORS - 1] = CPU_MASK_NONE
};

static int irq_status[NR_IRQS] = {
	[0 ... NR_IRQS -1] = IRQ_UNUSED
};

int check_irq_used(int irq)
{
	if (irq_status[irq] == IRQ_USED)
		return 1;

	return -1;
}

static inline int find_unassigned_irq(void)
{
	int irq;

	for (irq = IA64_FIRST_DEVICE_VECTOR; irq < NR_IRQS; irq++)
		if (irq_status[irq] == IRQ_UNUSED)
			return irq;
	return -ENOSPC;
}

static inline int find_unassigned_vector(cpumask_t domain)
{
	cpumask_t mask;
	int pos, vector;

	cpus_and(mask, domain, cpu_online_map);
	if (cpus_empty(mask))
		return -EINVAL;

	for (pos = 0; pos < IA64_NUM_DEVICE_VECTORS; pos++) {
		vector = IA64_FIRST_DEVICE_VECTOR + pos;
		cpus_and(mask, domain, vector_table[vector]);
		if (!cpus_empty(mask))
			continue;
		return vector;
	}
	return -ENOSPC;
}

static int __bind_irq_vector(int irq, int vector, cpumask_t domain)
{
	cpumask_t mask;
	int cpu;
	struct irq_cfg *cfg = &irq_cfg[irq];

	BUG_ON((unsigned)irq >= NR_IRQS);
	BUG_ON((unsigned)vector >= IA64_NUM_VECTORS);

	cpus_and(mask, domain, cpu_online_map);
	if (cpus_empty(mask))
		return -EINVAL;
	if ((cfg->vector == vector) && cpus_equal(cfg->domain, domain))
		return 0;
	if (cfg->vector != IRQ_VECTOR_UNASSIGNED)
		return -EBUSY;
	for_each_cpu_mask(cpu, mask)
		per_cpu(vector_irq, cpu)[vector] = irq;
	cfg->vector = vector;
	cfg->domain = domain;
	irq_status[irq] = IRQ_USED;
	cpus_or(vector_table[vector], vector_table[vector], domain);
	return 0;
}

int bind_irq_vector(int irq, int vector, cpumask_t domain)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&vector_lock, flags);
	ret = __bind_irq_vector(irq, vector, domain);
	spin_unlock_irqrestore(&vector_lock, flags);
	return ret;
}

static void __clear_irq_vector(int irq)
{
	int vector, cpu;
	cpumask_t mask;
	cpumask_t domain;
	struct irq_cfg *cfg = &irq_cfg[irq];

	BUG_ON((unsigned)irq >= NR_IRQS);
	BUG_ON(cfg->vector == IRQ_VECTOR_UNASSIGNED);
	vector = cfg->vector;
	domain = cfg->domain;
	cpus_and(mask, cfg->domain, cpu_online_map);
	for_each_cpu_mask(cpu, mask)
		per_cpu(vector_irq, cpu)[vector] = IA64_SPURIOUS_INT_VECTOR;
	cfg->vector = IRQ_VECTOR_UNASSIGNED;
	cfg->domain = CPU_MASK_NONE;
	irq_status[irq] = IRQ_UNUSED;
	cpus_andnot(vector_table[vector], vector_table[vector], domain);
}

static void clear_irq_vector(int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq);
	spin_unlock_irqrestore(&vector_lock, flags);
}

int
assign_irq_vector (int irq)
{
	unsigned long flags;
	int vector, cpu;
	cpumask_t domain;

	vector = -ENOSPC;

	spin_lock_irqsave(&vector_lock, flags);
	for_each_online_cpu(cpu) {
		domain = vector_allocation_domain(cpu);
		vector = find_unassigned_vector(domain);
		if (vector >= 0)
			break;
	}
	if (vector < 0)
		goto out;
	if (irq == AUTO_ASSIGN)
		irq = vector;
	BUG_ON(__bind_irq_vector(irq, vector, domain));
 out:
	spin_unlock_irqrestore(&vector_lock, flags);
	return vector;
}

void
free_irq_vector (int vector)
{
	if (vector < IA64_FIRST_DEVICE_VECTOR ||
	    vector > IA64_LAST_DEVICE_VECTOR)
		return;
	clear_irq_vector(vector);
}

int
reserve_irq_vector (int vector)
{
	if (vector < IA64_FIRST_DEVICE_VECTOR ||
	    vector > IA64_LAST_DEVICE_VECTOR)
		return -EINVAL;
	return !!bind_irq_vector(vector, vector, CPU_MASK_ALL);
}

/*
 * Initialize vector_irq on a new cpu. This function must be called
 * with vector_lock held.
 */
void __setup_vector_irq(int cpu)
{
	int irq, vector;

	/* Clear vector_irq */
	for (vector = 0; vector < IA64_NUM_VECTORS; ++vector)
		per_cpu(vector_irq, cpu)[vector] = IA64_SPURIOUS_INT_VECTOR;
	/* Mark the inuse vectors */
	for (irq = 0; irq < NR_IRQS; ++irq) {
		if (!cpu_isset(cpu, irq_cfg[irq].domain))
			continue;
		vector = irq_to_vector(irq);
		per_cpu(vector_irq, cpu)[vector] = irq;
	}
}

#if defined(CONFIG_SMP) && (defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_DIG))
static enum vector_domain_type {
	VECTOR_DOMAIN_NONE,
	VECTOR_DOMAIN_PERCPU
} vector_domain_type = VECTOR_DOMAIN_NONE;

static cpumask_t vector_allocation_domain(int cpu)
{
	if (vector_domain_type == VECTOR_DOMAIN_PERCPU)
		return cpumask_of_cpu(cpu);
	return CPU_MASK_ALL;
}

static int __init parse_vector_domain(char *arg)
{
	if (!arg)
		return -EINVAL;
	if (!strcmp(arg, "percpu")) {
		vector_domain_type = VECTOR_DOMAIN_PERCPU;
		no_int_routing = 1;
	}
	return 0;
}
early_param("vector", parse_vector_domain);
#else
static cpumask_t vector_allocation_domain(int cpu)
{
	return CPU_MASK_ALL;
}
#endif


void destroy_and_reserve_irq(unsigned int irq)
{
	unsigned long flags;

	dynamic_irq_cleanup(irq);

	spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq);
	irq_status[irq] = IRQ_RSVD;
	spin_unlock_irqrestore(&vector_lock, flags);
}

static int __reassign_irq_vector(int irq, int cpu)
{
	struct irq_cfg *cfg = &irq_cfg[irq];
	int vector;
	cpumask_t domain;

	if (cfg->vector == IRQ_VECTOR_UNASSIGNED || !cpu_online(cpu))
		return -EINVAL;
	if (cpu_isset(cpu, cfg->domain))
		return 0;
	domain = vector_allocation_domain(cpu);
	vector = find_unassigned_vector(domain);
	if (vector < 0)
		return -ENOSPC;
	__clear_irq_vector(irq);
	BUG_ON(__bind_irq_vector(irq, vector, domain));
	return 0;
}

int reassign_irq_vector(int irq, int cpu)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&vector_lock, flags);
	ret = __reassign_irq_vector(irq, cpu);
	spin_unlock_irqrestore(&vector_lock, flags);
	return ret;
}

/*
 * Dynamic irq allocate and deallocation for MSI
 */
int create_irq(void)
{
	unsigned long flags;
	int irq, vector, cpu;
	cpumask_t domain;

	irq = vector = -ENOSPC;
	spin_lock_irqsave(&vector_lock, flags);
	for_each_online_cpu(cpu) {
		domain = vector_allocation_domain(cpu);
		vector = find_unassigned_vector(domain);
		if (vector >= 0)
			break;
	}
	if (vector < 0)
		goto out;
	irq = find_unassigned_irq();
	if (irq < 0)
		goto out;
	BUG_ON(__bind_irq_vector(irq, vector, domain));
 out:
	spin_unlock_irqrestore(&vector_lock, flags);
	if (irq >= 0)
		dynamic_irq_init(irq);
	return irq;
}

void destroy_irq(unsigned int irq)
{
	dynamic_irq_cleanup(irq);
	clear_irq_vector(irq);
}

#ifdef CONFIG_SMP
#	define IS_RESCHEDULE(vec)	(vec == IA64_IPI_RESCHEDULE)
#	define IS_LOCAL_TLB_FLUSH(vec)	(vec == IA64_IPI_LOCAL_TLB_FLUSH)
#else
#	define IS_RESCHEDULE(vec)	(0)
#	define IS_LOCAL_TLB_FLUSH(vec)	(0)
#endif
/*
 * That's where the IVT branches when we get an external
 * interrupt. This branches to the correct hardware IRQ handler via
 * function ptr.
 */
void
ia64_handle_irq (ia64_vector vector, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned long saved_tpr;

#if IRQ_DEBUG
	{
		unsigned long bsp, sp;

		/*
		 * Note: if the interrupt happened while executing in
		 * the context switch routine (ia64_switch_to), we may
		 * get a spurious stack overflow here.  This is
		 * because the register and the memory stack are not
		 * switched atomically.
		 */
		bsp = ia64_getreg(_IA64_REG_AR_BSP);
		sp = ia64_getreg(_IA64_REG_SP);

		if ((sp - bsp) < 1024) {
			static unsigned char count;
			static long last_time;

			if (jiffies - last_time > 5*HZ)
				count = 0;
			if (++count < 5) {
				last_time = jiffies;
				printk("ia64_handle_irq: DANGER: less than "
				       "1KB of free stack space!!\n"
				       "(bsp=0x%lx, sp=%lx)\n", bsp, sp);
			}
		}
	}
#endif /* IRQ_DEBUG */

	/*
	 * Always set TPR to limit maximum interrupt nesting depth to
	 * 16 (without this, it would be ~240, which could easily lead
	 * to kernel stack overflows).
	 */
	irq_enter();
	saved_tpr = ia64_getreg(_IA64_REG_CR_TPR);
	ia64_srlz_d();
	while (vector != IA64_SPURIOUS_INT_VECTOR) {
		if (unlikely(IS_LOCAL_TLB_FLUSH(vector))) {
			smp_local_flush_tlb();
			kstat_this_cpu.irqs[vector]++;
		} else if (unlikely(IS_RESCHEDULE(vector)))
			kstat_this_cpu.irqs[vector]++;
		else {
			ia64_setreg(_IA64_REG_CR_TPR, vector);
			ia64_srlz_d();

			generic_handle_irq(local_vector_to_irq(vector));

			/*
			 * Disable interrupts and send EOI:
			 */
			local_irq_disable();
			ia64_setreg(_IA64_REG_CR_TPR, saved_tpr);
		}
		ia64_eoi();
		vector = ia64_get_ivr();
	}
	/*
	 * This must be done *after* the ia64_eoi().  For example, the keyboard softirq
	 * handler needs to be able to wait for further keyboard interrupts, which can't
	 * come through until ia64_eoi() has been done.
	 */
	irq_exit();
	set_irq_regs(old_regs);
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * This function emulates a interrupt processing when a cpu is about to be
 * brought down.
 */
void ia64_process_pending_intr(void)
{
	ia64_vector vector;
	unsigned long saved_tpr;
	extern unsigned int vectors_in_migration[NR_IRQS];

	vector = ia64_get_ivr();

	 irq_enter();
	 saved_tpr = ia64_getreg(_IA64_REG_CR_TPR);
	 ia64_srlz_d();

	 /*
	  * Perform normal interrupt style processing
	  */
	while (vector != IA64_SPURIOUS_INT_VECTOR) {
		if (unlikely(IS_LOCAL_TLB_FLUSH(vector))) {
			smp_local_flush_tlb();
			kstat_this_cpu.irqs[vector]++;
		} else if (unlikely(IS_RESCHEDULE(vector)))
			kstat_this_cpu.irqs[vector]++;
		else {
			struct pt_regs *old_regs = set_irq_regs(NULL);

			ia64_setreg(_IA64_REG_CR_TPR, vector);
			ia64_srlz_d();

			/*
			 * Now try calling normal ia64_handle_irq as it would have got called
			 * from a real intr handler. Try passing null for pt_regs, hopefully
			 * it will work. I hope it works!.
			 * Probably could shared code.
			 */
			vectors_in_migration[local_vector_to_irq(vector)]=0;
			generic_handle_irq(local_vector_to_irq(vector));
			set_irq_regs(old_regs);

			/*
			 * Disable interrupts and send EOI
			 */
			local_irq_disable();
			ia64_setreg(_IA64_REG_CR_TPR, saved_tpr);
		}
		ia64_eoi();
		vector = ia64_get_ivr();
	}
	irq_exit();
}
#endif


#ifdef CONFIG_SMP

static irqreturn_t dummy_handler (int irq, void *dev_id)
{
	BUG();
}
extern irqreturn_t handle_IPI (int irq, void *dev_id);

static struct irqaction ipi_irqaction = {
	.handler =	handle_IPI,
	.flags =	IRQF_DISABLED,
	.name =		"IPI"
};

static struct irqaction resched_irqaction = {
	.handler =	dummy_handler,
	.flags =	IRQF_DISABLED,
	.name =		"resched"
};

static struct irqaction tlb_irqaction = {
	.handler =	dummy_handler,
	.flags =	IRQF_DISABLED,
	.name =		"tlb_flush"
};

#endif

void
register_percpu_irq (ia64_vector vec, struct irqaction *action)
{
	irq_desc_t *desc;
	unsigned int irq;

	irq = vec;
	BUG_ON(bind_irq_vector(irq, vec, CPU_MASK_ALL));
	desc = irq_desc + irq;
	desc->status |= IRQ_PER_CPU;
	desc->chip = &irq_type_ia64_lsapic;
	if (action)
		setup_irq(irq, action);
}

void __init
init_IRQ (void)
{
	register_percpu_irq(IA64_SPURIOUS_INT_VECTOR, NULL);
#ifdef CONFIG_SMP
	register_percpu_irq(IA64_IPI_VECTOR, &ipi_irqaction);
	register_percpu_irq(IA64_IPI_RESCHEDULE, &resched_irqaction);
	register_percpu_irq(IA64_IPI_LOCAL_TLB_FLUSH, &tlb_irqaction);
#endif
#ifdef CONFIG_PERFMON
	pfm_init_percpu();
#endif
	platform_irq_init();
}

void
ia64_send_ipi (int cpu, int vector, int delivery_mode, int redirect)
{
	void __iomem *ipi_addr;
	unsigned long ipi_data;
	unsigned long phys_cpu_id;

#ifdef CONFIG_SMP
	phys_cpu_id = cpu_physical_id(cpu);
#else
	phys_cpu_id = (ia64_getreg(_IA64_REG_CR_LID) >> 16) & 0xffff;
#endif

	/*
	 * cpu number is in 8bit ID and 8bit EID
	 */

	ipi_data = (delivery_mode << 8) | (vector & 0xff);
	ipi_addr = ipi_base_addr + ((phys_cpu_id << 4) | ((redirect & 1) << 3));

	writeq(ipi_data, ipi_addr);
}
