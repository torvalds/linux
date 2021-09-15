// SPDX-License-Identifier: GPL-2.0

/*
 * Architecture neutral utility routines for interacting with
 * Hyper-V. This file is specifically for code that must be
 * built-in to the kernel image when CONFIG_HYPERV is set
 * (vs. being in a module) because it is called from architecture
 * specific code under arch/.
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Michael Kelley <mikelley@microsoft.com>
 */

#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/bitfield.h>
#include <linux/cpumask.h>
#include <linux/panic_notifier.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

/*
 * hv_root_partition and ms_hyperv are defined here with other Hyper-V
 * specific globals so they are shared across all architectures and are
 * built only when CONFIG_HYPERV is defined.  But on x86,
 * ms_hyperv_init_platform() is built even when CONFIG_HYPERV is not
 * defined, and it uses these two variables.  So mark them as __weak
 * here, allowing for an overriding definition in the module containing
 * ms_hyperv_init_platform().
 */
bool __weak hv_root_partition;
EXPORT_SYMBOL_GPL(hv_root_partition);

struct ms_hyperv_info __weak ms_hyperv;
EXPORT_SYMBOL_GPL(ms_hyperv);

u32 *hv_vp_index;
EXPORT_SYMBOL_GPL(hv_vp_index);

u32 hv_max_vp_index;
EXPORT_SYMBOL_GPL(hv_max_vp_index);

void  __percpu **hyperv_pcpu_input_arg;
EXPORT_SYMBOL_GPL(hyperv_pcpu_input_arg);

void  __percpu **hyperv_pcpu_output_arg;
EXPORT_SYMBOL_GPL(hyperv_pcpu_output_arg);

/*
 * Hyper-V specific initialization and shutdown code that is
 * common across all architectures.  Called from architecture
 * specific initialization functions.
 */

void __init hv_common_free(void)
{
	kfree(hv_vp_index);
	hv_vp_index = NULL;

	free_percpu(hyperv_pcpu_output_arg);
	hyperv_pcpu_output_arg = NULL;

	free_percpu(hyperv_pcpu_input_arg);
	hyperv_pcpu_input_arg = NULL;
}

int __init hv_common_init(void)
{
	int i;

	/*
	 * Hyper-V expects to get crash register data or kmsg when
	 * crash enlightment is available and system crashes. Set
	 * crash_kexec_post_notifiers to be true to make sure that
	 * calling crash enlightment interface before running kdump
	 * kernel.
	 */
	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE)
		crash_kexec_post_notifiers = true;

	/*
	 * Allocate the per-CPU state for the hypercall input arg.
	 * If this allocation fails, we will not be able to setup
	 * (per-CPU) hypercall input page and thus this failure is
	 * fatal on Hyper-V.
	 */
	hyperv_pcpu_input_arg = alloc_percpu(void  *);
	BUG_ON(!hyperv_pcpu_input_arg);

	/* Allocate the per-CPU state for output arg for root */
	if (hv_root_partition) {
		hyperv_pcpu_output_arg = alloc_percpu(void *);
		BUG_ON(!hyperv_pcpu_output_arg);
	}

	hv_vp_index = kmalloc_array(num_possible_cpus(), sizeof(*hv_vp_index),
				    GFP_KERNEL);
	if (!hv_vp_index) {
		hv_common_free();
		return -ENOMEM;
	}

	for (i = 0; i < num_possible_cpus(); i++)
		hv_vp_index[i] = VP_INVAL;

	return 0;
}

/*
 * Hyper-V specific initialization and die code for
 * individual CPUs that is common across all architectures.
 * Called by the CPU hotplug mechanism.
 */

int hv_common_cpu_init(unsigned int cpu)
{
	void **inputarg, **outputarg;
	u64 msr_vp_index;
	gfp_t flags;
	int pgcount = hv_root_partition ? 2 : 1;

	/* hv_cpu_init() can be called with IRQs disabled from hv_resume() */
	flags = irqs_disabled() ? GFP_ATOMIC : GFP_KERNEL;

	inputarg = (void **)this_cpu_ptr(hyperv_pcpu_input_arg);
	*inputarg = kmalloc(pgcount * HV_HYP_PAGE_SIZE, flags);
	if (!(*inputarg))
		return -ENOMEM;

	if (hv_root_partition) {
		outputarg = (void **)this_cpu_ptr(hyperv_pcpu_output_arg);
		*outputarg = (char *)(*inputarg) + HV_HYP_PAGE_SIZE;
	}

	msr_vp_index = hv_get_register(HV_REGISTER_VP_INDEX);

	hv_vp_index[cpu] = msr_vp_index;

	if (msr_vp_index > hv_max_vp_index)
		hv_max_vp_index = msr_vp_index;

	return 0;
}

int hv_common_cpu_die(unsigned int cpu)
{
	unsigned long flags;
	void **inputarg, **outputarg;
	void *mem;

	local_irq_save(flags);

	inputarg = (void **)this_cpu_ptr(hyperv_pcpu_input_arg);
	mem = *inputarg;
	*inputarg = NULL;

	if (hv_root_partition) {
		outputarg = (void **)this_cpu_ptr(hyperv_pcpu_output_arg);
		*outputarg = NULL;
	}

	local_irq_restore(flags);

	kfree(mem);

	return 0;
}

/* Bit mask of the extended capability to query: see HV_EXT_CAPABILITY_xxx */
bool hv_query_ext_cap(u64 cap_query)
{
	/*
	 * The address of the 'hv_extended_cap' variable will be used as an
	 * output parameter to the hypercall below and so it should be
	 * compatible with 'virt_to_phys'. Which means, it's address should be
	 * directly mapped. Use 'static' to keep it compatible; stack variables
	 * can be virtually mapped, making them incompatible with
	 * 'virt_to_phys'.
	 * Hypercall input/output addresses should also be 8-byte aligned.
	 */
	static u64 hv_extended_cap __aligned(8);
	static bool hv_extended_cap_queried;
	u64 status;

	/*
	 * Querying extended capabilities is an extended hypercall. Check if the
	 * partition supports extended hypercall, first.
	 */
	if (!(ms_hyperv.priv_high & HV_ENABLE_EXTENDED_HYPERCALLS))
		return false;

	/* Extended capabilities do not change at runtime. */
	if (hv_extended_cap_queried)
		return hv_extended_cap & cap_query;

	status = hv_do_hypercall(HV_EXT_CALL_QUERY_CAPABILITIES, NULL,
				 &hv_extended_cap);

	/*
	 * The query extended capabilities hypercall should not fail under
	 * any normal circumstances. Avoid repeatedly making the hypercall, on
	 * error.
	 */
	hv_extended_cap_queried = true;
	if (!hv_result_success(status)) {
		pr_err("Hyper-V: Extended query capabilities hypercall failed 0x%llx\n",
		       status);
		return false;
	}

	return hv_extended_cap & cap_query;
}
EXPORT_SYMBOL_GPL(hv_query_ext_cap);

bool hv_is_hibernation_supported(void)
{
	return !hv_root_partition && acpi_sleep_state_supported(ACPI_STATE_S4);
}
EXPORT_SYMBOL_GPL(hv_is_hibernation_supported);

/*
 * Default function to read the Hyper-V reference counter, independent
 * of whether Hyper-V enlightened clocks/timers are being used. But on
 * architectures where it is used, Hyper-V enlightenment code in
 * hyperv_timer.c may override this function.
 */
static u64 __hv_read_ref_counter(void)
{
	return hv_get_register(HV_REGISTER_TIME_REF_COUNT);
}

u64 (*hv_read_reference_counter)(void) = __hv_read_ref_counter;
EXPORT_SYMBOL_GPL(hv_read_reference_counter);

/* These __weak functions provide default "no-op" behavior and
 * may be overridden by architecture specific versions. Architectures
 * for which the default "no-op" behavior is sufficient can leave
 * them unimplemented and not be cluttered with a bunch of stub
 * functions in arch-specific code.
 */

bool __weak hv_is_isolation_supported(void)
{
	return false;
}
EXPORT_SYMBOL_GPL(hv_is_isolation_supported);

void __weak hv_setup_vmbus_handler(void (*handler)(void))
{
}
EXPORT_SYMBOL_GPL(hv_setup_vmbus_handler);

void __weak hv_remove_vmbus_handler(void)
{
}
EXPORT_SYMBOL_GPL(hv_remove_vmbus_handler);

void __weak hv_setup_kexec_handler(void (*handler)(void))
{
}
EXPORT_SYMBOL_GPL(hv_setup_kexec_handler);

void __weak hv_remove_kexec_handler(void)
{
}
EXPORT_SYMBOL_GPL(hv_remove_kexec_handler);

void __weak hv_setup_crash_handler(void (*handler)(struct pt_regs *regs))
{
}
EXPORT_SYMBOL_GPL(hv_setup_crash_handler);

void __weak hv_remove_crash_handler(void)
{
}
EXPORT_SYMBOL_GPL(hv_remove_crash_handler);

void __weak hyperv_cleanup(void)
{
}
EXPORT_SYMBOL_GPL(hyperv_cleanup);
