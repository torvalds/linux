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
#include <linux/sched/task_stack.h>
#include <linux/panic_notifier.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/efi.h>
#include <linux/kdebug.h>
#include <linux/kmsg_dump.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/dma-map-ops.h>
#include <linux/set_memory.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

/*
 * hv_root_partition, ms_hyperv and hv_nested are defined here with other
 * Hyper-V specific globals so they are shared across all architectures and are
 * built only when CONFIG_HYPERV is defined.  But on x86,
 * ms_hyperv_init_platform() is built even when CONFIG_HYPERV is not
 * defined, and it uses these three variables.  So mark them as __weak
 * here, allowing for an overriding definition in the module containing
 * ms_hyperv_init_platform().
 */
bool __weak hv_root_partition;
EXPORT_SYMBOL_GPL(hv_root_partition);

bool __weak hv_nested;
EXPORT_SYMBOL_GPL(hv_nested);

struct ms_hyperv_info __weak ms_hyperv;
EXPORT_SYMBOL_GPL(ms_hyperv);

u32 *hv_vp_index;
EXPORT_SYMBOL_GPL(hv_vp_index);

u32 hv_max_vp_index;
EXPORT_SYMBOL_GPL(hv_max_vp_index);

void * __percpu *hyperv_pcpu_input_arg;
EXPORT_SYMBOL_GPL(hyperv_pcpu_input_arg);

void * __percpu *hyperv_pcpu_output_arg;
EXPORT_SYMBOL_GPL(hyperv_pcpu_output_arg);

static void hv_kmsg_dump_unregister(void);

static struct ctl_table_header *hv_ctl_table_hdr;

/*
 * Hyper-V specific initialization and shutdown code that is
 * common across all architectures.  Called from architecture
 * specific initialization functions.
 */

void __init hv_common_free(void)
{
	unregister_sysctl_table(hv_ctl_table_hdr);
	hv_ctl_table_hdr = NULL;

	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE)
		hv_kmsg_dump_unregister();

	kfree(hv_vp_index);
	hv_vp_index = NULL;

	free_percpu(hyperv_pcpu_output_arg);
	hyperv_pcpu_output_arg = NULL;

	free_percpu(hyperv_pcpu_input_arg);
	hyperv_pcpu_input_arg = NULL;
}

/*
 * Functions for allocating and freeing memory with size and
 * alignment HV_HYP_PAGE_SIZE. These functions are needed because
 * the guest page size may not be the same as the Hyper-V page
 * size. We depend upon kmalloc() aligning power-of-two size
 * allocations to the allocation size boundary, so that the
 * allocated memory appears to Hyper-V as a page of the size
 * it expects.
 */

void *hv_alloc_hyperv_page(void)
{
	BUILD_BUG_ON(PAGE_SIZE <  HV_HYP_PAGE_SIZE);

	if (PAGE_SIZE == HV_HYP_PAGE_SIZE)
		return (void *)__get_free_page(GFP_KERNEL);
	else
		return kmalloc(HV_HYP_PAGE_SIZE, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(hv_alloc_hyperv_page);

void *hv_alloc_hyperv_zeroed_page(void)
{
	if (PAGE_SIZE == HV_HYP_PAGE_SIZE)
		return (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	else
		return kzalloc(HV_HYP_PAGE_SIZE, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(hv_alloc_hyperv_zeroed_page);

void hv_free_hyperv_page(void *addr)
{
	if (PAGE_SIZE == HV_HYP_PAGE_SIZE)
		free_page((unsigned long)addr);
	else
		kfree(addr);
}
EXPORT_SYMBOL_GPL(hv_free_hyperv_page);

static void *hv_panic_page;

/*
 * Boolean to control whether to report panic messages over Hyper-V.
 *
 * It can be set via /proc/sys/kernel/hyperv_record_panic_msg
 */
static int sysctl_record_panic_msg = 1;

/*
 * sysctl option to allow the user to control whether kmsg data should be
 * reported to Hyper-V on panic.
 */
static struct ctl_table hv_ctl_table[] = {
	{
		.procname	= "hyperv_record_panic_msg",
		.data		= &sysctl_record_panic_msg,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
};

static int hv_die_panic_notify_crash(struct notifier_block *self,
				     unsigned long val, void *args);

static struct notifier_block hyperv_die_report_block = {
	.notifier_call = hv_die_panic_notify_crash,
};

static struct notifier_block hyperv_panic_report_block = {
	.notifier_call = hv_die_panic_notify_crash,
};

/*
 * The following callback works both as die and panic notifier; its
 * goal is to provide panic information to the hypervisor unless the
 * kmsg dumper is used [see hv_kmsg_dump()], which provides more
 * information but isn't always available.
 *
 * Notice that both the panic/die report notifiers are registered only
 * if we have the capability HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE set.
 */
static int hv_die_panic_notify_crash(struct notifier_block *self,
				     unsigned long val, void *args)
{
	struct pt_regs *regs;
	bool is_die;

	/* Don't notify Hyper-V unless we have a die oops event or panic. */
	if (self == &hyperv_panic_report_block) {
		is_die = false;
		regs = current_pt_regs();
	} else { /* die event */
		if (val != DIE_OOPS)
			return NOTIFY_DONE;

		is_die = true;
		regs = ((struct die_args *)args)->regs;
	}

	/*
	 * Hyper-V should be notified only once about a panic/die. If we will
	 * be calling hv_kmsg_dump() later with kmsg data, don't do the
	 * notification here.
	 */
	if (!sysctl_record_panic_msg || !hv_panic_page)
		hyperv_report_panic(regs, val, is_die);

	return NOTIFY_DONE;
}

/*
 * Callback from kmsg_dump. Grab as much as possible from the end of the kmsg
 * buffer and call into Hyper-V to transfer the data.
 */
static void hv_kmsg_dump(struct kmsg_dumper *dumper,
			 enum kmsg_dump_reason reason)
{
	struct kmsg_dump_iter iter;
	size_t bytes_written;

	/* We are only interested in panics. */
	if (reason != KMSG_DUMP_PANIC || !sysctl_record_panic_msg)
		return;

	/*
	 * Write dump contents to the page. No need to synchronize; panic should
	 * be single-threaded.
	 */
	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, hv_panic_page, HV_HYP_PAGE_SIZE,
			     &bytes_written);
	if (!bytes_written)
		return;
	/*
	 * P3 to contain the physical address of the panic page & P4 to
	 * contain the size of the panic data in that page. Rest of the
	 * registers are no-op when the NOTIFY_MSG flag is set.
	 */
	hv_set_msr(HV_MSR_CRASH_P0, 0);
	hv_set_msr(HV_MSR_CRASH_P1, 0);
	hv_set_msr(HV_MSR_CRASH_P2, 0);
	hv_set_msr(HV_MSR_CRASH_P3, virt_to_phys(hv_panic_page));
	hv_set_msr(HV_MSR_CRASH_P4, bytes_written);

	/*
	 * Let Hyper-V know there is crash data available along with
	 * the panic message.
	 */
	hv_set_msr(HV_MSR_CRASH_CTL,
		   (HV_CRASH_CTL_CRASH_NOTIFY |
		    HV_CRASH_CTL_CRASH_NOTIFY_MSG));
}

static struct kmsg_dumper hv_kmsg_dumper = {
	.dump = hv_kmsg_dump,
};

static void hv_kmsg_dump_unregister(void)
{
	kmsg_dump_unregister(&hv_kmsg_dumper);
	unregister_die_notifier(&hyperv_die_report_block);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &hyperv_panic_report_block);

	hv_free_hyperv_page(hv_panic_page);
	hv_panic_page = NULL;
}

static void hv_kmsg_dump_register(void)
{
	int ret;

	hv_panic_page = hv_alloc_hyperv_zeroed_page();
	if (!hv_panic_page) {
		pr_err("Hyper-V: panic message page memory allocation failed\n");
		return;
	}

	ret = kmsg_dump_register(&hv_kmsg_dumper);
	if (ret) {
		pr_err("Hyper-V: kmsg dump register error 0x%x\n", ret);
		hv_free_hyperv_page(hv_panic_page);
		hv_panic_page = NULL;
	}
}

int __init hv_common_init(void)
{
	int i;
	union hv_hypervisor_version_info version;

	/* Get information about the Hyper-V host version */
	if (!hv_get_hypervisor_version(&version))
		pr_info("Hyper-V: Host Build %d.%d.%d.%d-%d-%d\n",
			version.major_version, version.minor_version,
			version.build_number, version.service_number,
			version.service_pack, version.service_branch);

	if (hv_is_isolation_supported())
		sysctl_record_panic_msg = 0;

	/*
	 * Hyper-V expects to get crash register data or kmsg when
	 * crash enlightment is available and system crashes. Set
	 * crash_kexec_post_notifiers to be true to make sure that
	 * calling crash enlightment interface before running kdump
	 * kernel.
	 */
	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE) {
		u64 hyperv_crash_ctl;

		crash_kexec_post_notifiers = true;
		pr_info("Hyper-V: enabling crash_kexec_post_notifiers\n");

		/*
		 * Panic message recording (sysctl_record_panic_msg)
		 * is enabled by default in non-isolated guests and
		 * disabled by default in isolated guests; the panic
		 * message recording won't be available in isolated
		 * guests should the following registration fail.
		 */
		hv_ctl_table_hdr = register_sysctl("kernel", hv_ctl_table);
		if (!hv_ctl_table_hdr)
			pr_err("Hyper-V: sysctl table register error");

		/*
		 * Register for panic kmsg callback only if the right
		 * capability is supported by the hypervisor.
		 */
		hyperv_crash_ctl = hv_get_msr(HV_MSR_CRASH_CTL);
		if (hyperv_crash_ctl & HV_CRASH_CTL_CRASH_NOTIFY_MSG)
			hv_kmsg_dump_register();

		register_die_notifier(&hyperv_die_report_block);
		atomic_notifier_chain_register(&panic_notifier_list,
					       &hyperv_panic_report_block);
	}

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

void __init ms_hyperv_late_init(void)
{
	struct acpi_table_header *header;
	acpi_status status;
	u8 *randomdata;
	u32 length, i;

	/*
	 * Seed the Linux random number generator with entropy provided by
	 * the Hyper-V host in ACPI table OEM0.
	 */
	if (!IS_ENABLED(CONFIG_ACPI))
		return;

	status = acpi_get_table("OEM0", 0, &header);
	if (ACPI_FAILURE(status) || !header)
		return;

	/*
	 * Since the "OEM0" table name is for OEM specific usage, verify
	 * that what we're seeing purports to be from Microsoft.
	 */
	if (strncmp(header->oem_table_id, "MICROSFT", 8))
		goto error;

	/*
	 * Ensure the length is reasonable. Requiring at least 8 bytes and
	 * no more than 4K bytes is somewhat arbitrary and just protects
	 * against a malformed table. Hyper-V currently provides 64 bytes,
	 * but allow for a change in a later version.
	 */
	if (header->length < sizeof(*header) + 8 ||
	    header->length > sizeof(*header) + SZ_4K)
		goto error;

	length = header->length - sizeof(*header);
	randomdata = (u8 *)(header + 1);

	pr_debug("Hyper-V: Seeding rng with %d random bytes from ACPI table OEM0\n",
			length);

	add_bootloader_randomness(randomdata, length);

	/*
	 * To prevent the seed data from being visible in /sys/firmware/acpi,
	 * zero out the random data in the ACPI table and fixup the checksum.
	 * The zero'ing is done out of an abundance of caution in avoiding
	 * potential security risks to the rng. Similarly, reset the table
	 * length to just the header size so that a subsequent kexec doesn't
	 * try to use the zero'ed out random data.
	 */
	for (i = 0; i < length; i++) {
		header->checksum += randomdata[i];
		randomdata[i] = 0;
	}

	for (i = 0; i < sizeof(header->length); i++)
		header->checksum += ((u8 *)&header->length)[i];
	header->length = sizeof(*header);
	for (i = 0; i < sizeof(header->length); i++)
		header->checksum -= ((u8 *)&header->length)[i];

error:
	acpi_put_table(header);
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
	void *mem;
	int ret;

	/* hv_cpu_init() can be called with IRQs disabled from hv_resume() */
	flags = irqs_disabled() ? GFP_ATOMIC : GFP_KERNEL;

	inputarg = (void **)this_cpu_ptr(hyperv_pcpu_input_arg);

	/*
	 * hyperv_pcpu_input_arg and hyperv_pcpu_output_arg memory is already
	 * allocated if this CPU was previously online and then taken offline
	 */
	if (!*inputarg) {
		mem = kmalloc(pgcount * HV_HYP_PAGE_SIZE, flags);
		if (!mem)
			return -ENOMEM;

		if (hv_root_partition) {
			outputarg = (void **)this_cpu_ptr(hyperv_pcpu_output_arg);
			*outputarg = (char *)mem + HV_HYP_PAGE_SIZE;
		}

		if (!ms_hyperv.paravisor_present &&
		    (hv_isolation_type_snp() || hv_isolation_type_tdx())) {
			ret = set_memory_decrypted((unsigned long)mem, pgcount);
			if (ret) {
				/* It may be unsafe to free 'mem' */
				return ret;
			}

			memset(mem, 0x00, pgcount * HV_HYP_PAGE_SIZE);
		}

		/*
		 * In a fully enlightened TDX/SNP VM with more than 64 VPs, if
		 * hyperv_pcpu_input_arg is not NULL, set_memory_decrypted() ->
		 * ... -> cpa_flush()-> ... -> __send_ipi_mask_ex() tries to
		 * use hyperv_pcpu_input_arg as the hypercall input page, which
		 * must be a decrypted page in such a VM, but the page is still
		 * encrypted before set_memory_decrypted() returns. Fix this by
		 * setting *inputarg after the above set_memory_decrypted(): if
		 * hyperv_pcpu_input_arg is NULL, __send_ipi_mask_ex() returns
		 * HV_STATUS_INVALID_PARAMETER immediately, and the function
		 * hv_send_ipi_mask() falls back to orig_apic.send_IPI_mask(),
		 * which may be slightly slower than the hypercall, but still
		 * works correctly in such a VM.
		 */
		*inputarg = mem;
	}

	msr_vp_index = hv_get_msr(HV_MSR_VP_INDEX);

	hv_vp_index[cpu] = msr_vp_index;

	if (msr_vp_index > hv_max_vp_index)
		hv_max_vp_index = msr_vp_index;

	return 0;
}

int hv_common_cpu_die(unsigned int cpu)
{
	/*
	 * The hyperv_pcpu_input_arg and hyperv_pcpu_output_arg memory
	 * is not freed when the CPU goes offline as the hyperv_pcpu_input_arg
	 * may be used by the Hyper-V vPCI driver in reassigning interrupts
	 * as part of the offlining process.  The interrupt reassignment
	 * happens *after* the CPUHP_AP_HYPERV_ONLINE state has run and
	 * called this function.
	 *
	 * If a previously offlined CPU is brought back online again, the
	 * originally allocated memory is reused in hv_common_cpu_init().
	 */

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

void hv_setup_dma_ops(struct device *dev, bool coherent)
{
	/*
	 * Hyper-V does not offer a vIOMMU in the guest
	 * VM, so pass 0/NULL for the IOMMU settings
	 */
	arch_setup_dma_ops(dev, 0, 0, coherent);
}
EXPORT_SYMBOL_GPL(hv_setup_dma_ops);

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
	return hv_get_msr(HV_MSR_TIME_REF_COUNT);
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

bool __weak hv_isolation_type_snp(void)
{
	return false;
}
EXPORT_SYMBOL_GPL(hv_isolation_type_snp);

bool __weak hv_isolation_type_tdx(void)
{
	return false;
}
EXPORT_SYMBOL_GPL(hv_isolation_type_tdx);

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

u64 __weak hv_ghcb_hypercall(u64 control, void *input, void *output, u32 input_size)
{
	return HV_STATUS_INVALID_PARAMETER;
}
EXPORT_SYMBOL_GPL(hv_ghcb_hypercall);

u64 __weak hv_tdx_hypercall(u64 control, u64 param1, u64 param2)
{
	return HV_STATUS_INVALID_PARAMETER;
}
EXPORT_SYMBOL_GPL(hv_tdx_hypercall);
