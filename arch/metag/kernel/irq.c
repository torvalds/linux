/*
 * Linux/Meta general interrupt handling code
 *
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irqchip/metag-ext.h>
#include <linux/irqchip/metag.h>
#include <linux/irqdomain.h>
#include <linux/ratelimit.h>

#include <asm/core_reg.h>
#include <asm/mach/arch.h>
#include <asm/uaccess.h>

#ifdef CONFIG_4KSTACKS
union irq_ctx {
	struct thread_info      tinfo;
	u32                     stack[THREAD_SIZE/sizeof(u32)];
};

static union irq_ctx *hardirq_ctx[NR_CPUS] __read_mostly;
static union irq_ctx *softirq_ctx[NR_CPUS] __read_mostly;
#endif

static struct irq_domain *root_domain;

static unsigned int startup_meta_irq(struct irq_data *data)
{
	tbi_startup_interrupt(data->hwirq);
	return 0;
}

static void shutdown_meta_irq(struct irq_data *data)
{
	tbi_shutdown_interrupt(data->hwirq);
}

void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
#ifdef CONFIG_4KSTACKS
	struct irq_desc *desc;
	union irq_ctx *curctx, *irqctx;
	u32 *isp;
#endif

	irq_enter();

	irq = irq_linear_revmap(root_domain, irq);

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		unsigned long sp;

		sp = __core_reg_get(A0StP);
		sp &= THREAD_SIZE - 1;

		if (unlikely(sp > (THREAD_SIZE - 1024)))
			pr_err("Stack overflow in do_IRQ: %ld\n", sp);
	}
#endif


#ifdef CONFIG_4KSTACKS
	curctx = (union irq_ctx *) current_thread_info();
	irqctx = hardirq_ctx[smp_processor_id()];

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (curctx != irqctx) {
		/* build the stack frame on the IRQ stack */
		isp = (u32 *) ((char *)irqctx + sizeof(struct thread_info));
		irqctx->tinfo.task = curctx->tinfo.task;

		/*
		 * Copy the softirq bits in preempt_count so that the
		 * softirq checks work in the hardirq context.
		 */
		irqctx->tinfo.preempt_count =
			(irqctx->tinfo.preempt_count & ~SOFTIRQ_MASK) |
			(curctx->tinfo.preempt_count & SOFTIRQ_MASK);

		desc = irq_to_desc(irq);

		asm volatile (
			"MOV   D0.5,%0\n"
			"MOV   D1Ar1,%1\n"
			"MOV   D1RtP,%2\n"
			"SWAP  A0StP,D0.5\n"
			"SWAP  PC,D1RtP\n"
			"MOV   A0StP,D0.5\n"
			:
			: "r" (isp), "r" (desc), "r" (desc->handle_irq)
			: "memory", "cc", "D1Ar1", "D0Ar2", "D1Ar3", "D0Ar4",
			  "D1Ar5", "D0Ar6", "D0Re0", "D1Re0", "D0.4", "D1RtP",
			  "D0.5"
			);
	} else
#endif
		generic_handle_irq(irq);

	irq_exit();

	set_irq_regs(old_regs);
}

#ifdef CONFIG_4KSTACKS

static char softirq_stack[NR_CPUS * THREAD_SIZE] __page_aligned_bss;

static char hardirq_stack[NR_CPUS * THREAD_SIZE] __page_aligned_bss;

/*
 * allocate per-cpu stacks for hardirq and for softirq processing
 */
void irq_ctx_init(int cpu)
{
	union irq_ctx *irqctx;

	if (hardirq_ctx[cpu])
		return;

	irqctx = (union irq_ctx *) &hardirq_stack[cpu * THREAD_SIZE];
	irqctx->tinfo.task              = NULL;
	irqctx->tinfo.cpu               = cpu;
	irqctx->tinfo.preempt_count     = HARDIRQ_OFFSET;
	irqctx->tinfo.addr_limit        = MAKE_MM_SEG(0);

	hardirq_ctx[cpu] = irqctx;

	irqctx = (union irq_ctx *) &softirq_stack[cpu * THREAD_SIZE];
	irqctx->tinfo.task              = NULL;
	irqctx->tinfo.cpu               = cpu;
	irqctx->tinfo.preempt_count     = 0;
	irqctx->tinfo.addr_limit        = MAKE_MM_SEG(0);

	softirq_ctx[cpu] = irqctx;

	pr_info("CPU %u irqstacks, hard=%p soft=%p\n",
		cpu, hardirq_ctx[cpu], softirq_ctx[cpu]);
}

void irq_ctx_exit(int cpu)
{
	hardirq_ctx[smp_processor_id()] = NULL;
}

extern asmlinkage void __do_softirq(void);

void do_softirq_own_stack(void)
{
	struct thread_info *curctx;
	union irq_ctx *irqctx;
	u32 *isp;

	curctx = current_thread_info();
	irqctx = softirq_ctx[smp_processor_id()];
	irqctx->tinfo.task = curctx->task;

	/* build the stack frame on the softirq stack */
	isp = (u32 *) ((char *)irqctx + sizeof(struct thread_info));

	asm volatile (
		"MOV   D0.5,%0\n"
		"SWAP  A0StP,D0.5\n"
		"CALLR D1RtP,___do_softirq\n"
		"MOV   A0StP,D0.5\n"
		:
		: "r" (isp)
		: "memory", "cc", "D1Ar1", "D0Ar2", "D1Ar3", "D0Ar4",
		  "D1Ar5", "D0Ar6", "D0Re0", "D1Re0", "D0.4", "D1RtP",
		  "D0.5"
		);
}
#endif

static struct irq_chip meta_irq_type = {
	.name = "META-IRQ",
	.irq_startup = startup_meta_irq,
	.irq_shutdown = shutdown_meta_irq,
};

/**
 * tbisig_map() - Map a TBI signal number to a virtual IRQ number.
 * @hw:		Number of the TBI signal. Must be in range.
 *
 * Returns:	The virtual IRQ number of the TBI signal number IRQ specified by
 *		@hw.
 */
int tbisig_map(unsigned int hw)
{
	return irq_create_mapping(root_domain, hw);
}

/**
 * metag_tbisig_map() - map a tbi signal to a Linux virtual IRQ number
 * @d:		root irq domain
 * @irq:	virtual irq number
 * @hw:		hardware irq number (TBI signal number)
 *
 * This sets up a virtual irq for a specified TBI signal number.
 */
static int metag_tbisig_map(struct irq_domain *d, unsigned int irq,
			    irq_hw_number_t hw)
{
#ifdef CONFIG_SMP
	irq_set_chip_and_handler(irq, &meta_irq_type, handle_percpu_irq);
#else
	irq_set_chip_and_handler(irq, &meta_irq_type, handle_simple_irq);
#endif
	return 0;
}

static const struct irq_domain_ops metag_tbisig_domain_ops = {
	.map = metag_tbisig_map,
};

/*
 * void init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */
void __init init_IRQ(void)
{
	root_domain = irq_domain_add_linear(NULL, 32,
					    &metag_tbisig_domain_ops, NULL);
	if (unlikely(!root_domain))
		panic("init_IRQ: cannot add root IRQ domain");

	irq_ctx_init(smp_processor_id());

	init_internal_IRQ();
	init_external_IRQ();

	if (machine_desc->init_irq)
		machine_desc->init_irq();
}

int __init arch_probe_nr_irqs(void)
{
	if (machine_desc->nr_irqs)
		nr_irqs = machine_desc->nr_irqs;
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * The CPU has been marked offline.  Migrate IRQs off this CPU.  If
 * the affinity settings do not allow other CPUs, force them onto any
 * available CPU.
 */
void migrate_irqs(void)
{
	unsigned int i, cpu = smp_processor_id();

	for_each_active_irq(i) {
		struct irq_data *data = irq_get_irq_data(i);
		struct cpumask *mask;
		unsigned int newcpu;

		if (irqd_is_per_cpu(data))
			continue;

		mask = irq_data_get_affinity_mask(data);
		if (!cpumask_test_cpu(cpu, mask))
			continue;

		newcpu = cpumask_any_and(mask, cpu_online_mask);

		if (newcpu >= nr_cpu_ids) {
			pr_info_ratelimited("IRQ%u no longer affine to CPU%u\n",
					    i, cpu);

			cpumask_setall(mask);
		}
		irq_set_affinity(i, mask);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */
