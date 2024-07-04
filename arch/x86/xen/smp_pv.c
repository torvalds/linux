// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sched/task_stack.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/irq_work.h>
#include <linux/tick.h>
#include <linux/nmi.h>
#include <linux/cpuhotplug.h>
#include <linux/stackprotector.h>
#include <linux/pgtable.h>

#include <asm/paravirt.h>
#include <asm/idtentry.h>
#include <asm/desc.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/io_apic.h>

#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/xenpmu.h>

#include <asm/spec-ctrl.h>
#include <asm/xen/interface.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/events.h>

#include <xen/hvc-console.h>
#include "xen-ops.h"
#include "mmu.h"
#include "smp.h"
#include "pmu.h"

cpumask_var_t xen_cpu_initialized_map;

static DEFINE_PER_CPU(struct xen_common_irq, xen_irq_work) = { .irq = -1 };
static DEFINE_PER_CPU(struct xen_common_irq, xen_pmu_irq) = { .irq = -1 };

static irqreturn_t xen_irq_work_interrupt(int irq, void *dev_id);

static void cpu_bringup(void)
{
	int cpu;

	cr4_init();
	cpuhp_ap_sync_alive();
	cpu_init();
	fpu__init_cpu();
	touch_softlockup_watchdog();

	/* PVH runs in ring 0 and allows us to do native syscalls. Yay! */
	if (!xen_feature(XENFEAT_supervisor_mode_kernel)) {
		xen_enable_sysenter();
		xen_enable_syscall();
	}
	cpu = smp_processor_id();
	smp_store_cpu_info(cpu);
	set_cpu_sibling_map(cpu);

	speculative_store_bypass_ht_init();

	xen_setup_cpu_clockevents();

	notify_cpu_starting(cpu);

	set_cpu_online(cpu, true);

	smp_mb();

	/* We can take interrupts now: we're officially "up". */
	local_irq_enable();
}

asmlinkage __visible void cpu_bringup_and_idle(void)
{
	cpu_bringup();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

void xen_smp_intr_free_pv(unsigned int cpu)
{
	kfree(per_cpu(xen_irq_work, cpu).name);
	per_cpu(xen_irq_work, cpu).name = NULL;
	if (per_cpu(xen_irq_work, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_irq_work, cpu).irq, NULL);
		per_cpu(xen_irq_work, cpu).irq = -1;
	}

	kfree(per_cpu(xen_pmu_irq, cpu).name);
	per_cpu(xen_pmu_irq, cpu).name = NULL;
	if (per_cpu(xen_pmu_irq, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_pmu_irq, cpu).irq, NULL);
		per_cpu(xen_pmu_irq, cpu).irq = -1;
	}
}

int xen_smp_intr_init_pv(unsigned int cpu)
{
	int rc;
	char *callfunc_name, *pmu_name;

	callfunc_name = kasprintf(GFP_KERNEL, "irqwork%d", cpu);
	per_cpu(xen_irq_work, cpu).name = callfunc_name;
	rc = bind_ipi_to_irqhandler(XEN_IRQ_WORK_VECTOR,
				    cpu,
				    xen_irq_work_interrupt,
				    IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_irq_work, cpu).irq = rc;

	if (is_xen_pmu) {
		pmu_name = kasprintf(GFP_KERNEL, "pmu%d", cpu);
		per_cpu(xen_pmu_irq, cpu).name = pmu_name;
		rc = bind_virq_to_irqhandler(VIRQ_XENPMU, cpu,
					     xen_pmu_irq_handler,
					     IRQF_PERCPU|IRQF_NOBALANCING,
					     pmu_name, NULL);
		if (rc < 0)
			goto fail;
		per_cpu(xen_pmu_irq, cpu).irq = rc;
	}

	return 0;

 fail:
	xen_smp_intr_free_pv(cpu);
	return rc;
}

static void __init xen_pv_smp_config(void)
{
	u32 apicid = 0;
	int i;

	topology_register_boot_apic(apicid);

	for (i = 0; i < nr_cpu_ids; i++)
		topology_register_apic(apicid++, CPU_ACPIID_INVALID, true);

	/* Pretend to be a proper enumerated system */
	smp_found_config = 1;
}

static void __init xen_pv_smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != 0);
	native_smp_prepare_boot_cpu();

	if (!xen_feature(XENFEAT_writable_page_tables))
		/* We've switched to the "real" per-cpu gdt, so make
		 * sure the old memory can be recycled. */
		make_lowmem_page_readwrite(xen_initial_gdt);

	xen_setup_vcpu_info_placement();

	/*
	 * The alternative logic (which patches the unlock/lock) runs before
	 * the smp bootup up code is activated. Hence we need to set this up
	 * the core kernel is being patched. Otherwise we will have only
	 * modules patched but not core code.
	 */
	xen_init_spinlocks();
}

static void __init xen_pv_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned cpu;

	if (ioapic_is_disabled) {
		char *m = (max_cpus == 0) ?
			"The nosmp parameter is incompatible with Xen; " \
			"use Xen dom0_max_vcpus=1 parameter" :
			"The noapic parameter is incompatible with Xen";

		xen_raw_printk(m);
		panic(m);
	}
	xen_init_lock_cpu(0);

	smp_prepare_cpus_common();

	speculative_store_bypass_ht_init();

	xen_pmu_init(0);

	if (xen_smp_intr_init(0) || xen_smp_intr_init_pv(0))
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

static int
cpu_initialize_context(unsigned int cpu, struct task_struct *idle)
{
	struct vcpu_guest_context *ctxt;
	struct desc_struct *gdt;
	unsigned long gdt_mfn;

	if (cpumask_test_and_set_cpu(cpu, xen_cpu_initialized_map))
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (ctxt == NULL) {
		cpumask_clear_cpu(cpu, xen_cpu_initialized_map);
		return -ENOMEM;
	}

	gdt = get_cpu_gdt_rw(cpu);

	/*
	 * Bring up the CPU in cpu_bringup_and_idle() with the stack
	 * pointing just below where pt_regs would be if it were a normal
	 * kernel entry.
	 */
	ctxt->user_regs.eip = (unsigned long)asm_cpu_bringup_and_idle;
	ctxt->flags = VGCF_IN_KERNEL;
	ctxt->user_regs.eflags = 0x1000; /* IOPL_RING1 */
	ctxt->user_regs.ds = __USER_DS;
	ctxt->user_regs.es = __USER_DS;
	ctxt->user_regs.ss = __KERNEL_DS;
	ctxt->user_regs.cs = __KERNEL_CS;
	ctxt->user_regs.esp = (unsigned long)task_pt_regs(idle);

	xen_copy_trap_info(ctxt->trap_ctxt);

	BUG_ON((unsigned long)gdt & ~PAGE_MASK);

	gdt_mfn = arbitrary_virt_to_mfn(gdt);
	make_lowmem_page_readonly(gdt);
	make_lowmem_page_readonly(mfn_to_virt(gdt_mfn));

	ctxt->gdt_frames[0] = gdt_mfn;
	ctxt->gdt_ents      = GDT_ENTRIES;

	/*
	 * Set SS:SP that Xen will use when entering guest kernel mode
	 * from guest user mode.  Subsequent calls to load_sp0() can
	 * change this value.
	 */
	ctxt->kernel_ss = __KERNEL_DS;
	ctxt->kernel_sp = task_top_of_stack(idle);

	ctxt->gs_base_kernel = per_cpu_offset(cpu);
	ctxt->event_callback_eip    =
		(unsigned long)xen_asm_exc_xen_hypervisor_callback;
	ctxt->failsafe_callback_eip =
		(unsigned long)xen_failsafe_callback;
	per_cpu(xen_cr3, cpu) = __pa(swapper_pg_dir);

	ctxt->ctrlreg[3] = xen_pfn_to_cr3(virt_to_gfn(swapper_pg_dir));
	if (HYPERVISOR_vcpu_op(VCPUOP_initialise, xen_vcpu_nr(cpu), ctxt))
		BUG();

	kfree(ctxt);
	return 0;
}

static int xen_pv_kick_ap(unsigned int cpu, struct task_struct *idle)
{
	int rc;

	rc = common_cpu_up(cpu, idle);
	if (rc)
		return rc;

	xen_setup_runstate_info(cpu);

	/* make sure interrupts start blocked */
	per_cpu(xen_vcpu, cpu)->evtchn_upcall_mask = 1;

	rc = cpu_initialize_context(cpu, idle);
	if (rc)
		return rc;

	xen_pmu_init(cpu);

	/*
	 * Why is this a BUG? If the hypercall fails then everything can be
	 * rolled back, no?
	 */
	BUG_ON(HYPERVISOR_vcpu_op(VCPUOP_up, xen_vcpu_nr(cpu), NULL));

	return 0;
}

static void xen_pv_poll_sync_state(void)
{
	HYPERVISOR_sched_op(SCHEDOP_yield, NULL);
}

#ifdef CONFIG_HOTPLUG_CPU
static int xen_pv_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();
	if (cpu == 0)
		return -EBUSY;

	cpu_disable_common();

	load_cr3(swapper_pg_dir);
	return 0;
}

static void xen_pv_cpu_die(unsigned int cpu)
{
	while (HYPERVISOR_vcpu_op(VCPUOP_is_up, xen_vcpu_nr(cpu), NULL)) {
		__set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/10);
	}
}

static void xen_pv_cleanup_dead_cpu(unsigned int cpu)
{
	xen_smp_intr_free(cpu);
	xen_uninit_lock_cpu(cpu);
	xen_teardown_timer(cpu);
	xen_pmu_finish(cpu);
}

static void __noreturn xen_pv_play_dead(void) /* used only with HOTPLUG_CPU */
{
	play_dead_common();
	HYPERVISOR_vcpu_op(VCPUOP_down, xen_vcpu_nr(smp_processor_id()), NULL);
	xen_cpu_bringup_again((unsigned long)task_pt_regs(current));
	BUG();
}

#else /* !CONFIG_HOTPLUG_CPU */
static int xen_pv_cpu_disable(void)
{
	return -ENOSYS;
}

static void xen_pv_cpu_die(unsigned int cpu)
{
	BUG();
}

static void xen_pv_cleanup_dead_cpu(unsigned int cpu)
{
	BUG();
}

static void __noreturn xen_pv_play_dead(void)
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

	HYPERVISOR_vcpu_op(VCPUOP_down, xen_vcpu_nr(cpu), NULL);
	BUG();
}

static void xen_pv_stop_other_cpus(int wait)
{
	smp_call_function(stop_self, NULL, wait);
}

static irqreturn_t xen_irq_work_interrupt(int irq, void *dev_id)
{
	irq_work_run();
	inc_irq_stat(apic_irq_work_irqs);

	return IRQ_HANDLED;
}

void __init xen_smp_count_cpus(void)
{
	unsigned int cpus;

	for (cpus = 0; cpus < nr_cpu_ids; cpus++) {
		if (HYPERVISOR_vcpu_op(VCPUOP_is_up, cpus, NULL) < 0)
			break;
	}

	pr_info("Xen PV: Detected %u vCPUS\n", cpus);
	if (cpus < nr_cpu_ids)
		set_nr_cpu_ids(cpus);
}

static const struct smp_ops xen_smp_ops __initconst = {
	.smp_prepare_boot_cpu = xen_pv_smp_prepare_boot_cpu,
	.smp_prepare_cpus = xen_pv_smp_prepare_cpus,
	.smp_cpus_done = xen_smp_cpus_done,

	.kick_ap_alive = xen_pv_kick_ap,
	.cpu_die = xen_pv_cpu_die,
	.cleanup_dead_cpu = xen_pv_cleanup_dead_cpu,
	.poll_sync_state = xen_pv_poll_sync_state,
	.cpu_disable = xen_pv_cpu_disable,
	.play_dead = xen_pv_play_dead,

	.stop_other_cpus = xen_pv_stop_other_cpus,
	.smp_send_reschedule = xen_smp_send_reschedule,

	.send_call_func_ipi = xen_smp_send_call_function_ipi,
	.send_call_func_single_ipi = xen_smp_send_call_function_single_ipi,
};

void __init xen_smp_init(void)
{
	smp_ops = xen_smp_ops;

	/* Avoid searching for BIOS MP tables */
	x86_init.mpparse.find_mptable		= x86_init_noop;
	x86_init.mpparse.early_parse_smp_cfg	= x86_init_noop;

	/* XEN/PV Dom0 has halfways sane topology information via CPUID/MADT */
	if (xen_initial_domain())
		x86_init.mpparse.parse_smp_cfg	= x86_init_noop;
	else
		x86_init.mpparse.parse_smp_cfg	= xen_pv_smp_config;
}
