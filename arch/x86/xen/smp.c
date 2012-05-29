/*
 * Xen SMP support
 *
 * This file implements the Xen versions of smp_ops.  SMP under Xen is
 * very straightforward.  Bringing a CPU up is simply a matter of
 * loading its initial context and setting it running.
 *
 * IPIs are handled through the Xen event mechanism.
 *
 * Because virtual CPUs can be scheduled onto any real CPU, there's no
 * useful topology information for the kernel to make use of.  As a
 * result, all CPUs are treated as if they're single-core and
 * single-threaded.
 */
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/irq_work.h>

#include <asm/paravirt.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/cpu.h>

#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>

#include <asm/xen/interface.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/events.h>

#include <xen/hvc-console.h>
#include "xen-ops.h"
#include "mmu.h"

cpumask_var_t xen_cpu_initialized_map;

static DEFINE_PER_CPU(int, xen_resched_irq);
static DEFINE_PER_CPU(int, xen_callfunc_irq);
static DEFINE_PER_CPU(int, xen_callfuncsingle_irq);
static DEFINE_PER_CPU(int, xen_irq_work);
static DEFINE_PER_CPU(int, xen_debug_irq) = -1;

static irqreturn_t xen_call_function_interrupt(int irq, void *dev_id);
static irqreturn_t xen_call_function_single_interrupt(int irq, void *dev_id);
static irqreturn_t xen_irq_work_interrupt(int irq, void *dev_id);

/*
 * Reschedule call back.
 */
static irqreturn_t xen_reschedule_interrupt(int irq, void *dev_id)
{
	inc_irq_stat(irq_resched_count);
	scheduler_ipi();

	return IRQ_HANDLED;
}

static void __cpuinit cpu_bringup(void)
{
	int cpu;

	cpu_init();
	touch_softlockup_watchdog();
	preempt_disable();

	xen_enable_sysenter();
	xen_enable_syscall();

	cpu = smp_processor_id();
	smp_store_cpu_info(cpu);
	cpu_data(cpu).x86_max_cores = 1;
	set_cpu_sibling_map(cpu);

	xen_setup_cpu_clockevents();

	notify_cpu_starting(cpu);

	set_cpu_online(cpu, true);

	this_cpu_write(cpu_state, CPU_ONLINE);

	wmb();

	/* We can take interrupts now: we're officially "up". */
	local_irq_enable();

	wmb();			/* make sure everything is out */
}

static void __cpuinit cpu_bringup_and_idle(void)
{
	cpu_bringup();
	cpu_idle();
}

static int xen_smp_intr_init(unsigned int cpu)
{
	int rc;
	const char *resched_name, *callfunc_name, *debug_name;

	resched_name = kasprintf(GFP_KERNEL, "resched%d", cpu);
	rc = bind_ipi_to_irqhandler(XEN_RESCHEDULE_VECTOR,
				    cpu,
				    xen_reschedule_interrupt,
				    IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				    resched_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_resched_irq, cpu) = rc;

	callfunc_name = kasprintf(GFP_KERNEL, "callfunc%d", cpu);
	rc = bind_ipi_to_irqhandler(XEN_CALL_FUNCTION_VECTOR,
				    cpu,
				    xen_call_function_interrupt,
				    IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_callfunc_irq, cpu) = rc;

	debug_name = kasprintf(GFP_KERNEL, "debug%d", cpu);
	rc = bind_virq_to_irqhandler(VIRQ_DEBUG, cpu, xen_debug_interrupt,
				     IRQF_DISABLED | IRQF_PERCPU | IRQF_NOBALANCING,
				     debug_name, NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_debug_irq, cpu) = rc;

	callfunc_name = kasprintf(GFP_KERNEL, "callfuncsingle%d", cpu);
	rc = bind_ipi_to_irqhandler(XEN_CALL_FUNCTION_SINGLE_VECTOR,
				    cpu,
				    xen_call_function_single_interrupt,
				    IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_callfuncsingle_irq, cpu) = rc;

	callfunc_name = kasprintf(GFP_KERNEL, "irqwork%d", cpu);
	rc = bind_ipi_to_irqhandler(XEN_IRQ_WORK_VECTOR,
				    cpu,
				    xen_irq_work_interrupt,
				    IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_irq_work, cpu) = rc;

	return 0;

 fail:
	if (per_cpu(xen_resched_irq, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(xen_resched_irq, cpu), NULL);
	if (per_cpu(xen_callfunc_irq, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(xen_callfunc_irq, cpu), NULL);
	if (per_cpu(xen_debug_irq, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(xen_debug_irq, cpu), NULL);
	if (per_cpu(xen_callfuncsingle_irq, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(xen_callfuncsingle_irq, cpu),
				       NULL);
	if (per_cpu(xen_irq_work, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(xen_irq_work, cpu), NULL);

	return rc;
}

static void __init xen_fill_possible_map(void)
{
	int i, rc;

	if (xen_initial_domain())
		return;

	for (i = 0; i < nr_cpu_ids; i++) {
		rc = HYPERVISOR_vcpu_op(VCPUOP_is_up, i, NULL);
		if (rc >= 0) {
			num_processors++;
			set_cpu_possible(i, true);
		}
	}
}

static void __init xen_filter_cpu_maps(void)
{
	int i, rc;
	unsigned int subtract = 0;

	if (!xen_initial_domain())
		return;

	num_processors = 0;
	disabled_cpus = 0;
	for (i = 0; i < nr_cpu_ids; i++) {
		rc = HYPERVISOR_vcpu_op(VCPUOP_is_up, i, NULL);
		if (rc >= 0) {
			num_processors++;
			set_cpu_possible(i, true);
		} else {
			set_cpu_possible(i, false);
			set_cpu_present(i, false);
			subtract++;
		}
	}
#ifdef CONFIG_HOTPLUG_CPU
	/* This is akin to using 'nr_cpus' on the Linux command line.
	 * Which is OK as when we use 'dom0_max_vcpus=X' we can only
	 * have up to X, while nr_cpu_ids is greater than X. This
	 * normally is not a problem, except when CPU hotplugging
	 * is involved and then there might be more than X CPUs
	 * in the guest - which will not work as there is no
	 * hypercall to expand the max number of VCPUs an already
	 * running guest has. So cap it up to X. */
	if (subtract)
		nr_cpu_ids = nr_cpu_ids - subtract;
#endif

}

static void __init xen_smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != 0);
	native_smp_prepare_boot_cpu();

	/* We've switched to the "real" per-cpu gdt, so make sure the
	   old memory can be recycled */
	make_lowmem_page_readwrite(xen_initial_gdt);

	xen_filter_cpu_maps();
	xen_setup_vcpu_info_placement();
}

static void __init xen_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned cpu;
	unsigned int i;

	if (skip_ioapic_setup) {
		char *m = (max_cpus == 0) ?
			"The nosmp parameter is incompatible with Xen; " \
			"use Xen dom0_max_vcpus=1 parameter" :
			"The noapic parameter is incompatible with Xen";

		xen_raw_printk(m);
		panic(m);
	}
	xen_init_lock_cpu(0);

	smp_store_cpu_info(0);
	cpu_data(0).x86_max_cores = 1;

	for_each_possible_cpu(i) {
		zalloc_cpumask_var(&per_cpu(cpu_sibling_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_core_map, i), GFP_KERNEL);
		zalloc_cpumask_var(&per_cpu(cpu_llc_shared_map, i), GFP_KERNEL);
	}
	set_cpu_sibling_map(0);

	if (xen_smp_intr_init(0))
		BUG();

	if (!alloc_cpumask_var(&xen_cpu_initialized_map, GFP_KERNEL))
		panic("could not allocate xen_cpu_initialized_map\n");

	cpumask_copy(xen_cpu_initialized_map, cpumask_of(0));

	/* Restrict the possible_map according to max_cpus. */
	while ((num_possible_cpus() > 1) && (num_possible_cpus() > max_cpus)) {
		for (cpu = nr_cpu_ids - 1; !cpu_possible(cpu); cpu--)
			continue;
		set_cpu_possible(cpu, false);
	}

	for_each_possible_cpu(cpu)
		set_cpu_present(cpu, true);
}

static int __cpuinit
cpu_initialize_context(unsigned int cpu, struct task_struct *idle)
{
	struct vcpu_guest_context *ctxt;
	struct desc_struct *gdt;
	unsigned long gdt_mfn;

	if (cpumask_test_and_set_cpu(cpu, xen_cpu_initialized_map))
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (ctxt == NULL)
		return -ENOMEM;

	gdt = get_cpu_gdt_table(cpu);

	ctxt->flags = VGCF_IN_KERNEL;
	ctxt->user_regs.ds = __USER_DS;
	ctxt->user_regs.es = __USER_DS;
	ctxt->user_regs.ss = __KERNEL_DS;
#ifdef CONFIG_X86_32
	ctxt->user_regs.fs = __KERNEL_PERCPU;
	ctxt->user_regs.gs = __KERNEL_STACK_CANARY;
#else
	ctxt->gs_base_kernel = per_cpu_offset(cpu);
#endif
	ctxt->user_regs.eip = (unsigned long)cpu_bringup_and_idle;
	ctxt->user_regs.eflags = 0x1000; /* IOPL_RING1 */

	memset(&ctxt->fpu_ctxt, 0, sizeof(ctxt->fpu_ctxt));

	xen_copy_trap_info(ctxt->trap_ctxt);

	ctxt->ldt_ents = 0;

	BUG_ON((unsigned long)gdt & ~PAGE_MASK);

	gdt_mfn = arbitrary_virt_to_mfn(gdt);
	make_lowmem_page_readonly(gdt);
	make_lowmem_page_readonly(mfn_to_virt(gdt_mfn));

	ctxt->gdt_frames[0] = gdt_mfn;
	ctxt->gdt_ents      = GDT_ENTRIES;

	ctxt->user_regs.cs = __KERNEL_CS;
	ctxt->user_regs.esp = idle->thread.sp0 - sizeof(struct pt_regs);

	ctxt->kernel_ss = __KERNEL_DS;
	ctxt->kernel_sp = idle->thread.sp0;

#ifdef CONFIG_X86_32
	ctxt->event_callback_cs     = __KERNEL_CS;
	ctxt->failsafe_callback_cs  = __KERNEL_CS;
#endif
	ctxt->event_callback_eip    = (unsigned long)xen_hypervisor_callback;
	ctxt->failsafe_callback_eip = (unsigned long)xen_failsafe_callback;

	per_cpu(xen_cr3, cpu) = __pa(swapper_pg_dir);
	ctxt->ctrlreg[3] = xen_pfn_to_cr3(virt_to_mfn(swapper_pg_dir));

	if (HYPERVISOR_vcpu_op(VCPUOP_initialise, cpu, ctxt))
		BUG();

	kfree(ctxt);
	return 0;
}

static int __cpuinit xen_cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int rc;

	per_cpu(current_task, cpu) = idle;
#ifdef CONFIG_X86_32
	irq_ctx_init(cpu);
#else
	clear_tsk_thread_flag(idle, TIF_FORK);
	per_cpu(kernel_stack, cpu) =
		(unsigned long)task_stack_page(idle) -
		KERNEL_STACK_OFFSET + THREAD_SIZE;
#endif
	xen_setup_runstate_info(cpu);
	xen_setup_timer(cpu);
	xen_init_lock_cpu(cpu);

	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;

	/* make sure interrupts start blocked */
	per_cpu(xen_vcpu, cpu)->evtchn_upcall_mask = 1;

	rc = cpu_initialize_context(cpu, idle);
	if (rc)
		return rc;

	if (num_online_cpus() == 1)
		alternatives_smp_switch(1);

	rc = xen_smp_intr_init(cpu);
	if (rc)
		return rc;

	rc = HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL);
	BUG_ON(rc);

	while(per_cpu(cpu_state, cpu) != CPU_ONLINE) {
		HYPERVISOR_sched_op(SCHEDOP_yield, NULL);
		barrier();
	}

	return 0;
}

static void xen_smp_cpus_done(unsigned int max_cpus)
{
}

#ifdef CONFIG_HOTPLUG_CPU
static int xen_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();
	if (cpu == 0)
		return -EBUSY;

	cpu_disable_common();

	load_cr3(swapper_pg_dir);
	return 0;
}

static void xen_cpu_die(unsigned int cpu)
{
	while (HYPERVISOR_vcpu_op(VCPUOP_is_up, cpu, NULL)) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ/10);
	}
	unbind_from_irqhandler(per_cpu(xen_resched_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_callfunc_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_debug_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_callfuncsingle_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_irq_work, cpu), NULL);
	xen_uninit_lock_cpu(cpu);
	xen_teardown_timer(cpu);

	if (num_online_cpus() == 1)
		alternatives_smp_switch(0);
}

static void __cpuinit xen_play_dead(void) /* used only with HOTPLUG_CPU */
{
	play_dead_common();
	HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL);
	cpu_bringup();
	/*
	 * Balance out the preempt calls - as we are running in cpu_idle
	 * loop which has been called at bootup from cpu_bringup_and_idle.
	 * The cpucpu_bringup_and_idle called cpu_bringup which made a
	 * preempt_disable() So this preempt_enable will balance it out.
	 */
	preempt_enable();
}

#else /* !CONFIG_HOTPLUG_CPU */
static int xen_cpu_disable(void)
{
	return -ENOSYS;
}

static void xen_cpu_die(unsigned int cpu)
{
	BUG();
}

static void xen_play_dead(void)
{
	BUG();
}

#endif
static void stop_self(void *v)
{
	int cpu = smp_processor_id();

	/* make sure we're not pinning something down */
	load_cr3(swapper_pg_dir);
	/* should set up a minimal gdt */

	set_cpu_online(cpu, false);

	HYPERVISOR_vcpu_op(VCPUOP_down, cpu, NULL);
	BUG();
}

static void xen_stop_other_cpus(int wait)
{
	smp_call_function(stop_self, NULL, wait);
}

static void xen_smp_send_reschedule(int cpu)
{
	xen_send_IPI_one(cpu, XEN_RESCHEDULE_VECTOR);
}

static void __xen_send_IPI_mask(const struct cpumask *mask,
			      int vector)
{
	unsigned cpu;

	for_each_cpu_and(cpu, mask, cpu_online_mask)
		xen_send_IPI_one(cpu, vector);
}

static void xen_smp_send_call_function_ipi(const struct cpumask *mask)
{
	int cpu;

	__xen_send_IPI_mask(mask, XEN_CALL_FUNCTION_VECTOR);

	/* Make sure other vcpus get a chance to run if they need to. */
	for_each_cpu(cpu, mask) {
		if (xen_vcpu_stolen(cpu)) {
			HYPERVISOR_sched_op(SCHEDOP_yield, NULL);
			break;
		}
	}
}

static void xen_smp_send_call_function_single_ipi(int cpu)
{
	__xen_send_IPI_mask(cpumask_of(cpu),
			  XEN_CALL_FUNCTION_SINGLE_VECTOR);
}

static inline int xen_map_vector(int vector)
{
	int xen_vector;

	switch (vector) {
	case RESCHEDULE_VECTOR:
		xen_vector = XEN_RESCHEDULE_VECTOR;
		break;
	case CALL_FUNCTION_VECTOR:
		xen_vector = XEN_CALL_FUNCTION_VECTOR;
		break;
	case CALL_FUNCTION_SINGLE_VECTOR:
		xen_vector = XEN_CALL_FUNCTION_SINGLE_VECTOR;
		break;
	case IRQ_WORK_VECTOR:
		xen_vector = XEN_IRQ_WORK_VECTOR;
		break;
	default:
		xen_vector = -1;
		printk(KERN_ERR "xen: vector 0x%x is not implemented\n",
			vector);
	}

	return xen_vector;
}

void xen_send_IPI_mask(const struct cpumask *mask,
			      int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		__xen_send_IPI_mask(mask, xen_vector);
}

void xen_send_IPI_all(int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		__xen_send_IPI_mask(cpu_online_mask, xen_vector);
}

void xen_send_IPI_self(int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		xen_send_IPI_one(smp_processor_id(), xen_vector);
}

void xen_send_IPI_mask_allbutself(const struct cpumask *mask,
				int vector)
{
	unsigned cpu;
	unsigned int this_cpu = smp_processor_id();

	if (!(num_online_cpus() > 1))
		return;

	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		if (this_cpu == cpu)
			continue;

		xen_smp_send_call_function_single_ipi(cpu);
	}
}

void xen_send_IPI_allbutself(int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		xen_send_IPI_mask_allbutself(cpu_online_mask, xen_vector);
}

static irqreturn_t xen_call_function_interrupt(int irq, void *dev_id)
{
	irq_enter();
	generic_smp_call_function_interrupt();
	inc_irq_stat(irq_call_count);
	irq_exit();

	return IRQ_HANDLED;
}

static irqreturn_t xen_call_function_single_interrupt(int irq, void *dev_id)
{
	irq_enter();
	generic_smp_call_function_single_interrupt();
	inc_irq_stat(irq_call_count);
	irq_exit();

	return IRQ_HANDLED;
}

static irqreturn_t xen_irq_work_interrupt(int irq, void *dev_id)
{
	irq_enter();
	irq_work_run();
	inc_irq_stat(apic_irq_work_irqs);
	irq_exit();

	return IRQ_HANDLED;
}

static const struct smp_ops xen_smp_ops __initconst = {
	.smp_prepare_boot_cpu = xen_smp_prepare_boot_cpu,
	.smp_prepare_cpus = xen_smp_prepare_cpus,
	.smp_cpus_done = xen_smp_cpus_done,

	.cpu_up = xen_cpu_up,
	.cpu_die = xen_cpu_die,
	.cpu_disable = xen_cpu_disable,
	.play_dead = xen_play_dead,

	.stop_other_cpus = xen_stop_other_cpus,
	.smp_send_reschedule = xen_smp_send_reschedule,

	.send_call_func_ipi = xen_smp_send_call_function_ipi,
	.send_call_func_single_ipi = xen_smp_send_call_function_single_ipi,
};

void __init xen_smp_init(void)
{
	smp_ops = xen_smp_ops;
	xen_fill_possible_map();
	xen_init_spinlocks();
}

static void __init xen_hvm_smp_prepare_cpus(unsigned int max_cpus)
{
	native_smp_prepare_cpus(max_cpus);
	WARN_ON(xen_smp_intr_init(0));

	xen_init_lock_cpu(0);
}

static int __cpuinit xen_hvm_cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int rc;
	rc = native_cpu_up(cpu, tidle);
	WARN_ON (xen_smp_intr_init(cpu));
	return rc;
}

static void xen_hvm_cpu_die(unsigned int cpu)
{
	unbind_from_irqhandler(per_cpu(xen_resched_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_callfunc_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_debug_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_callfuncsingle_irq, cpu), NULL);
	unbind_from_irqhandler(per_cpu(xen_irq_work, cpu), NULL);
	native_cpu_die(cpu);
}

void __init xen_hvm_smp_init(void)
{
	if (!xen_have_vector_callback)
		return;
	smp_ops.smp_prepare_cpus = xen_hvm_smp_prepare_cpus;
	smp_ops.smp_send_reschedule = xen_smp_send_reschedule;
	smp_ops.cpu_up = xen_hvm_cpu_up;
	smp_ops.cpu_die = xen_hvm_cpu_die;
	smp_ops.send_call_func_ipi = xen_smp_send_call_function_ipi;
	smp_ops.send_call_func_single_ipi = xen_smp_send_call_function_single_ipi;
}
