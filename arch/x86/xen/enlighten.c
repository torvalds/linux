#include <linux/cpu.h>
#include <linux/kexec.h>

#include <xen/features.h>
#include <xen/page.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/cpu.h>
#include <asm/e820/api.h> 

#include "xen-ops.h"
#include "smp.h"
#include "pmu.h"

EXPORT_SYMBOL_GPL(hypercall_page);

/*
 * Pointer to the xen_vcpu_info structure or
 * &HYPERVISOR_shared_info->vcpu_info[cpu]. See xen_hvm_init_shared_info
 * and xen_vcpu_setup for details. By default it points to share_info->vcpu_info
 * but if the hypervisor supports VCPUOP_register_vcpu_info then it can point
 * to xen_vcpu_info. The pointer is used in __xen_evtchn_do_upcall to
 * acknowledge pending events.
 * Also more subtly it is used by the patched version of irq enable/disable
 * e.g. xen_irq_enable_direct and xen_iret in PV mode.
 *
 * The desire to be able to do those mask/unmask operations as a single
 * instruction by using the per-cpu offset held in %gs is the real reason
 * vcpu info is in a per-cpu pointer and the original reason for this
 * hypercall.
 *
 */
DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);

/*
 * Per CPU pages used if hypervisor supports VCPUOP_register_vcpu_info
 * hypercall. This can be used both in PV and PVHVM mode. The structure
 * overrides the default per_cpu(xen_vcpu, cpu) value.
 */
DEFINE_PER_CPU(struct vcpu_info, xen_vcpu_info);

/* Linux <-> Xen vCPU id mapping */
DEFINE_PER_CPU(uint32_t, xen_vcpu_id);
EXPORT_PER_CPU_SYMBOL(xen_vcpu_id);

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);

unsigned long *machine_to_phys_mapping = (void *)MACH2PHYS_VIRT_START;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned long  machine_to_phys_nr;
EXPORT_SYMBOL(machine_to_phys_nr);

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

struct shared_info xen_dummy_shared_info;

__read_mostly int xen_have_vector_callback;
EXPORT_SYMBOL_GPL(xen_have_vector_callback);

/*
 * Point at some empty memory to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
struct shared_info *HYPERVISOR_shared_info = &xen_dummy_shared_info;

/*
 * Flag to determine whether vcpu info placement is available on all
 * VCPUs.  We assume it is to start with, and then set it to zero on
 * the first failure.  This is because it can succeed on some VCPUs
 * and not others, since it can involve hypervisor memory allocation,
 * or because the guest failed to guarantee all the appropriate
 * constraints on all VCPUs (ie buffer can't cross a page boundary).
 *
 * Note that any particular CPU may be using a placed vcpu structure,
 * but we can only optimise if the all are.
 *
 * 0: not available, 1: available
 */
int xen_have_vcpu_info_placement = 1;

static int xen_cpu_up_online(unsigned int cpu)
{
	xen_init_lock_cpu(cpu);
	return 0;
}

int xen_cpuhp_setup(int (*cpu_up_prepare_cb)(unsigned int),
		    int (*cpu_dead_cb)(unsigned int))
{
	int rc;

	rc = cpuhp_setup_state_nocalls(CPUHP_XEN_PREPARE,
				       "x86/xen/guest:prepare",
				       cpu_up_prepare_cb, cpu_dead_cb);
	if (rc >= 0) {
		rc = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					       "x86/xen/guest:online",
					       xen_cpu_up_online, NULL);
		if (rc < 0)
			cpuhp_remove_state_nocalls(CPUHP_XEN_PREPARE);
	}

	return rc >= 0 ? 0 : rc;
}

static int xen_vcpu_setup_restore(int cpu)
{
	int rc = 0;

	/* Any per_cpu(xen_vcpu) is stale, so reset it */
	xen_vcpu_info_reset(cpu);

	/*
	 * For PVH and PVHVM, setup online VCPUs only. The rest will
	 * be handled by hotplug.
	 */
	if (xen_pv_domain() ||
	    (xen_hvm_domain() && cpu_online(cpu))) {
		rc = xen_vcpu_setup(cpu);
	}

	return rc;
}

/*
 * On restore, set the vcpu placement up again.
 * If it fails, then we're in a bad state, since
 * we can't back out from using it...
 */
void xen_vcpu_restore(void)
{
	int cpu, rc;

	for_each_possible_cpu(cpu) {
		bool other_cpu = (cpu != smp_processor_id());
		bool is_up;

		if (xen_vcpu_nr(cpu) == XEN_VCPU_ID_INVALID)
			continue;

		/* Only Xen 4.5 and higher support this. */
		is_up = HYPERVISOR_vcpu_op(VCPUOP_is_up,
					   xen_vcpu_nr(cpu), NULL) > 0;

		if (other_cpu && is_up &&
		    HYPERVISOR_vcpu_op(VCPUOP_down, xen_vcpu_nr(cpu), NULL))
			BUG();

		if (xen_pv_domain() || xen_feature(XENFEAT_hvm_safe_pvclock))
			xen_setup_runstate_info(cpu);

		rc = xen_vcpu_setup_restore(cpu);
		if (rc)
			pr_emerg_once("vcpu restore failed for cpu=%d err=%d. "
					"System will hang.\n", cpu, rc);
		/*
		 * In case xen_vcpu_setup_restore() fails, do not bring up the
		 * VCPU. This helps us avoid the resulting OOPS when the VCPU
		 * accesses pvclock_vcpu_time via xen_vcpu (which is NULL.)
		 * Note that this does not improve the situation much -- now the
		 * VM hangs instead of OOPSing -- with the VCPUs that did not
		 * fail, spinning in stop_machine(), waiting for the failed
		 * VCPUs to come up.
		 */
		if (other_cpu && is_up && (rc == 0) &&
		    HYPERVISOR_vcpu_op(VCPUOP_up, xen_vcpu_nr(cpu), NULL))
			BUG();
	}
}

void xen_vcpu_info_reset(int cpu)
{
	if (xen_vcpu_nr(cpu) < MAX_VIRT_CPUS) {
		per_cpu(xen_vcpu, cpu) =
			&HYPERVISOR_shared_info->vcpu_info[xen_vcpu_nr(cpu)];
	} else {
		/* Set to NULL so that if somebody accesses it we get an OOPS */
		per_cpu(xen_vcpu, cpu) = NULL;
	}
}

int xen_vcpu_setup(int cpu)
{
	struct vcpu_register_vcpu_info info;
	int err;
	struct vcpu_info *vcpup;

	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	/*
	 * This path is called on PVHVM at bootup (xen_hvm_smp_prepare_boot_cpu)
	 * and at restore (xen_vcpu_restore). Also called for hotplugged
	 * VCPUs (cpu_init -> xen_hvm_cpu_prepare_hvm).
	 * However, the hypercall can only be done once (see below) so if a VCPU
	 * is offlined and comes back online then let's not redo the hypercall.
	 *
	 * For PV it is called during restore (xen_vcpu_restore) and bootup
	 * (xen_setup_vcpu_info_placement). The hotplug mechanism does not
	 * use this function.
	 */
	if (xen_hvm_domain()) {
		if (per_cpu(xen_vcpu, cpu) == &per_cpu(xen_vcpu_info, cpu))
			return 0;
	}

	if (xen_have_vcpu_info_placement) {
		vcpup = &per_cpu(xen_vcpu_info, cpu);
		info.mfn = arbitrary_virt_to_mfn(vcpup);
		info.offset = offset_in_page(vcpup);

		/*
		 * Check to see if the hypervisor will put the vcpu_info
		 * structure where we want it, which allows direct access via
		 * a percpu-variable.
		 * N.B. This hypercall can _only_ be called once per CPU.
		 * Subsequent calls will error out with -EINVAL. This is due to
		 * the fact that hypervisor has no unregister variant and this
		 * hypercall does not allow to over-write info.mfn and
		 * info.offset.
		 */
		err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info,
					 xen_vcpu_nr(cpu), &info);

		if (err) {
			pr_warn_once("register_vcpu_info failed: cpu=%d err=%d\n",
				     cpu, err);
			xen_have_vcpu_info_placement = 0;
		} else {
			/*
			 * This cpu is using the registered vcpu info, even if
			 * later ones fail to.
			 */
			per_cpu(xen_vcpu, cpu) = vcpup;
		}
	}

	if (!xen_have_vcpu_info_placement)
		xen_vcpu_info_reset(cpu);

	return ((per_cpu(xen_vcpu, cpu) == NULL) ? -ENODEV : 0);
}

void xen_reboot(int reason)
{
	struct sched_shutdown r = { .reason = reason };
	int cpu;

	for_each_online_cpu(cpu)
		xen_pmu_finish(cpu);

	if (HYPERVISOR_sched_op(SCHEDOP_shutdown, &r))
		BUG();
}

void xen_emergency_restart(void)
{
	xen_reboot(SHUTDOWN_reboot);
}

static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	if (!kexec_crash_loaded())
		xen_reboot(SHUTDOWN_crash);
	return NOTIFY_DONE;
}

static struct notifier_block xen_panic_block = {
	.notifier_call = xen_panic_event,
	.priority = INT_MIN
};

int xen_panic_handler_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);
	return 0;
}

void xen_pin_vcpu(int cpu)
{
	static bool disable_pinning;
	struct sched_pin_override pin_override;
	int ret;

	if (disable_pinning)
		return;

	pin_override.pcpu = cpu;
	ret = HYPERVISOR_sched_op(SCHEDOP_pin_override, &pin_override);

	/* Ignore errors when removing override. */
	if (cpu < 0)
		return;

	switch (ret) {
	case -ENOSYS:
		pr_warn("Unable to pin on physical cpu %d. In case of problems consider vcpu pinning.\n",
			cpu);
		disable_pinning = true;
		break;
	case -EPERM:
		WARN(1, "Trying to pin vcpu without having privilege to do so\n");
		disable_pinning = true;
		break;
	case -EINVAL:
	case -EBUSY:
		pr_warn("Physical cpu %d not available for pinning. Check Xen cpu configuration.\n",
			cpu);
		break;
	case 0:
		break;
	default:
		WARN(1, "rc %d while trying to pin vcpu\n", ret);
		disable_pinning = true;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
void xen_arch_register_cpu(int num)
{
	arch_register_cpu(num);
}
EXPORT_SYMBOL(xen_arch_register_cpu);

void xen_arch_unregister_cpu(int num)
{
	arch_unregister_cpu(num);
}
EXPORT_SYMBOL(xen_arch_unregister_cpu);
#endif
