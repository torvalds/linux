// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2005, 2011
 *
 * Author(s): Rolf Adelsberger,
 *	      Heiko Carstens <heiko.carstens@de.ibm.com>
 *	      Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/ftrace.h>
#include <linux/debug_locks.h>
#include <linux/suspend.h>
#include <asm/cio.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/smp.h>
#include <asm/reset.h>
#include <asm/ipl.h>
#include <asm/diag.h>
#include <asm/elf.h>
#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/os_info.h>
#include <asm/set_memory.h>
#include <asm/switch_to.h>
#include <asm/nmi.h>

typedef void (*relocate_kernel_t)(kimage_entry_t *, unsigned long);

extern const unsigned char relocate_kernel[];
extern const unsigned long long relocate_kernel_len;

#ifdef CONFIG_CRASH_DUMP

/*
 * PM notifier callback for kdump
 */
static int machine_kdump_pm_cb(struct notifier_block *nb, unsigned long action,
			       void *ptr)
{
	switch (action) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		if (kexec_crash_image)
			arch_kexec_unprotect_crashkres();
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		if (kexec_crash_image)
			arch_kexec_protect_crashkres();
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static int __init machine_kdump_pm_init(void)
{
	pm_notifier(machine_kdump_pm_cb, 0);
	return 0;
}
arch_initcall(machine_kdump_pm_init);

/*
 * Reset the system, copy boot CPU registers to absolute zero,
 * and jump to the kdump image
 */
static void __do_machine_kdump(void *image)
{
	int (*start_kdump)(int);
	unsigned long prefix;

	/* store_status() saved the prefix register to lowcore */
	prefix = (unsigned long) S390_lowcore.prefixreg_save_area;

	/* Now do the reset  */
	s390_reset_system();

	/*
	 * Copy dump CPU store status info to absolute zero.
	 * This need to be done *after* s390_reset_system set the
	 * prefix register of this CPU to zero
	 */
	memcpy((void *) __LC_FPREGS_SAVE_AREA,
	       (void *)(prefix + __LC_FPREGS_SAVE_AREA), 512);

	__load_psw_mask(PSW_MASK_BASE | PSW_DEFAULT_KEY | PSW_MASK_EA | PSW_MASK_BA);
	start_kdump = (void *)((struct kimage *) image)->start;
	start_kdump(1);

	/* Die if start_kdump returns */
	disabled_wait((unsigned long) __builtin_return_address(0));
}

/*
 * Start kdump: create a LGR log entry, store status of all CPUs and
 * branch to __do_machine_kdump.
 */
static noinline void __machine_kdump(void *image)
{
	struct mcesa *mcesa;
	union ctlreg2 cr2_old, cr2_new;
	int this_cpu, cpu;

	lgr_info_log();
	/* Get status of the other CPUs */
	this_cpu = smp_find_processor_id(stap());
	for_each_online_cpu(cpu) {
		if (cpu == this_cpu)
			continue;
		if (smp_store_status(cpu))
			continue;
	}
	/* Store status of the boot CPU */
	mcesa = (struct mcesa *)(S390_lowcore.mcesad & MCESA_ORIGIN_MASK);
	if (MACHINE_HAS_VX)
		save_vx_regs((__vector128 *) mcesa->vector_save_area);
	if (MACHINE_HAS_GS) {
		__ctl_store(cr2_old.val, 2, 2);
		cr2_new = cr2_old;
		cr2_new.gse = 1;
		__ctl_load(cr2_new.val, 2, 2);
		save_gs_cb((struct gs_cb *) mcesa->guarded_storage_save_area);
		__ctl_load(cr2_old.val, 2, 2);
	}
	/*
	 * To create a good backchain for this CPU in the dump store_status
	 * is passed the address of a function. The address is saved into
	 * the PSW save area of the boot CPU and the function is invoked as
	 * a tail call of store_status. The backchain in the dump will look
	 * like this:
	 *   restart_int_handler ->  __machine_kexec -> __do_machine_kdump
	 * The call to store_status() will not return.
	 */
	store_status(__do_machine_kdump, image);
}
#endif

/*
 * Check if kdump checksums are valid: We call purgatory with parameter "0"
 */
static bool kdump_csum_valid(struct kimage *image)
{
#ifdef CONFIG_CRASH_DUMP
	int (*start_kdump)(int) = (void *)image->start;
	int rc;

	__arch_local_irq_stnsm(0xfb); /* disable DAT */
	rc = start_kdump(0);
	__arch_local_irq_stosm(0x04); /* enable DAT */
	return rc == 0;
#else
	return false;
#endif
}

#ifdef CONFIG_CRASH_DUMP

void crash_free_reserved_phys_range(unsigned long begin, unsigned long end)
{
	unsigned long addr, size;

	for (addr = begin; addr < end; addr += PAGE_SIZE)
		free_reserved_page(pfn_to_page(addr >> PAGE_SHIFT));
	size = begin - crashk_res.start;
	if (size)
		os_info_crashkernel_add(crashk_res.start, size);
	else
		os_info_crashkernel_add(0, 0);
}

static void crash_protect_pages(int protect)
{
	unsigned long size;

	if (!crashk_res.end)
		return;
	size = resource_size(&crashk_res);
	if (protect)
		set_memory_ro(crashk_res.start, size >> PAGE_SHIFT);
	else
		set_memory_rw(crashk_res.start, size >> PAGE_SHIFT);
}

void arch_kexec_protect_crashkres(void)
{
	crash_protect_pages(1);
}

void arch_kexec_unprotect_crashkres(void)
{
	crash_protect_pages(0);
}

#endif

/*
 * Give back memory to hypervisor before new kdump is loaded
 */
static int machine_kexec_prepare_kdump(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (MACHINE_IS_VM)
		diag10_range(PFN_DOWN(crashk_res.start),
			     PFN_DOWN(crashk_res.end - crashk_res.start + 1));
	return 0;
#else
	return -EINVAL;
#endif
}

int machine_kexec_prepare(struct kimage *image)
{
	void *reboot_code_buffer;

	if (image->type == KEXEC_TYPE_CRASH)
		return machine_kexec_prepare_kdump();

	/* We don't support anything but the default image type for now. */
	if (image->type != KEXEC_TYPE_DEFAULT)
		return -EINVAL;

	/* Get the destination where the assembler code should be copied to.*/
	reboot_code_buffer = (void *) page_to_phys(image->control_code_page);

	/* Then copy it */
	memcpy(reboot_code_buffer, relocate_kernel, relocate_kernel_len);
	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
}

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_SYMBOL(lowcore_ptr);
	VMCOREINFO_SYMBOL(high_memory);
	VMCOREINFO_LENGTH(lowcore_ptr, NR_CPUS);
	mem_assign_absolute(S390_lowcore.vmcore_info, paddr_vmcoreinfo_note());
}

void machine_shutdown(void)
{
}

void machine_crash_shutdown(struct pt_regs *regs)
{
}

/*
 * Do normal kexec
 */
static void __do_machine_kexec(void *data)
{
	relocate_kernel_t data_mover;
	struct kimage *image = data;

	s390_reset_system();
	data_mover = (relocate_kernel_t) page_to_phys(image->control_code_page);

	__arch_local_irq_stnsm(0xfb); /* disable DAT - avoid no-execute */
	/* Call the moving routine */
	(*data_mover)(&image->head, image->start);

	/* Die if kexec returns */
	disabled_wait((unsigned long) __builtin_return_address(0));
}

/*
 * Reset system and call either kdump or normal kexec
 */
static void __machine_kexec(void *data)
{
	__arch_local_irq_stosm(0x04); /* enable DAT */
	pfault_fini();
	tracing_off();
	debug_locks_off();
#ifdef CONFIG_CRASH_DUMP
	if (((struct kimage *) data)->type == KEXEC_TYPE_CRASH)
		__machine_kdump(data);
#endif
	__do_machine_kexec(data);
}

/*
 * Do either kdump or normal kexec. In case of kdump we first ask
 * purgatory, if kdump checksums are valid.
 */
void machine_kexec(struct kimage *image)
{
	if (image->type == KEXEC_TYPE_CRASH && !kdump_csum_valid(image))
		return;
	tracer_disable();
	smp_send_stop();
	smp_call_ipl_cpu(__machine_kexec, image);
}
