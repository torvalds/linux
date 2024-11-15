// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>
#include <asm/cpufeature.h>
#include <asm/tdx.h>
#include "capabilities.h"
#include "tdx.h"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

static bool enable_tdx __ro_after_init;
module_param_named(tdx, enable_tdx, bool, 0444);

static enum cpuhp_state tdx_cpuhp_state;

static int tdx_online_cpu(unsigned int cpu)
{
	unsigned long flags;
	int r;

	/* Sanity check CPU is already in post-VMXON */
	WARN_ON_ONCE(!(cr4_read_shadow() & X86_CR4_VMXE));

	local_irq_save(flags);
	r = tdx_cpu_enable();
	local_irq_restore(flags);

	return r;
}

static void __do_tdx_cleanup(void)
{
	/*
	 * Once TDX module is initialized, it cannot be disabled and
	 * re-initialized again w/o runtime update (which isn't
	 * supported by kernel).  Only need to remove the cpuhp here.
	 * The TDX host core code tracks TDX status and can handle
	 * 'multiple enabling' scenario.
	 */
	WARN_ON_ONCE(!tdx_cpuhp_state);
	cpuhp_remove_state_nocalls_cpuslocked(tdx_cpuhp_state);
	tdx_cpuhp_state = 0;
}

static void __tdx_cleanup(void)
{
	cpus_read_lock();
	__do_tdx_cleanup();
	cpus_read_unlock();
}

static int __init __do_tdx_bringup(void)
{
	int r;

	/*
	 * TDX-specific cpuhp callback to call tdx_cpu_enable() on all
	 * online CPUs before calling tdx_enable(), and on any new
	 * going-online CPU to make sure it is ready for TDX guest.
	 */
	r = cpuhp_setup_state_cpuslocked(CPUHP_AP_ONLINE_DYN,
					 "kvm/cpu/tdx:online",
					 tdx_online_cpu, NULL);
	if (r < 0)
		return r;

	tdx_cpuhp_state = r;

	r = tdx_enable();
	if (r)
		__do_tdx_cleanup();

	return r;
}

static int __init __tdx_bringup(void)
{
	int r;

	/*
	 * Enabling TDX requires enabling hardware virtualization first,
	 * as making SEAMCALLs requires CPU being in post-VMXON state.
	 */
	r = kvm_enable_virtualization();
	if (r)
		return r;

	cpus_read_lock();
	r = __do_tdx_bringup();
	cpus_read_unlock();

	if (r)
		goto tdx_bringup_err;

	/*
	 * Leave hardware virtualization enabled after TDX is enabled
	 * successfully.  TDX CPU hotplug depends on this.
	 */
	return 0;
tdx_bringup_err:
	kvm_disable_virtualization();
	return r;
}

void tdx_cleanup(void)
{
	if (enable_tdx) {
		__tdx_cleanup();
		kvm_disable_virtualization();
	}
}

int __init tdx_bringup(void)
{
	int r;

	if (!enable_tdx)
		return 0;

	if (!cpu_feature_enabled(X86_FEATURE_TDX_HOST_PLATFORM)) {
		pr_err("tdx: no TDX private KeyIDs available\n");
		goto success_disable_tdx;
	}

	if (!enable_virt_at_load) {
		pr_err("tdx: tdx requires kvm.enable_virt_at_load=1\n");
		goto success_disable_tdx;
	}

	/*
	 * Ideally KVM should probe whether TDX module has been loaded
	 * first and then try to bring it up.  But TDX needs to use SEAMCALL
	 * to probe whether the module is loaded (there is no CPUID or MSR
	 * for that), and making SEAMCALL requires enabling virtualization
	 * first, just like the rest steps of bringing up TDX module.
	 *
	 * So, for simplicity do everything in __tdx_bringup(); the first
	 * SEAMCALL will return -ENODEV when the module is not loaded.  The
	 * only complication is having to make sure that initialization
	 * SEAMCALLs don't return TDX_SEAMCALL_VMFAILINVALID in other
	 * cases.
	 */
	r = __tdx_bringup();
	if (r) {
		/*
		 * Disable TDX only but don't fail to load module if
		 * the TDX module could not be loaded.  No need to print
		 * message saying "module is not loaded" because it was
		 * printed when the first SEAMCALL failed.
		 */
		if (r == -ENODEV)
			goto success_disable_tdx;

		enable_tdx = 0;
	}

	return r;

success_disable_tdx:
	enable_tdx = 0;
	return 0;
}
