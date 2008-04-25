/*
 * Machine specific setup for xen
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pm.h>

#include <asm/elf.h>
#include <asm/vdso.h>
#include <asm/e820.h>
#include <asm/setup.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/interface/callback.h>
#include <xen/interface/physdev.h>
#include <xen/features.h>

#include "xen-ops.h"
#include "vdso.h"

/* These are code, but not functions.  Defined in entry.S */
extern const char xen_hypervisor_callback[];
extern const char xen_failsafe_callback[];

unsigned long *phys_to_machine_mapping;
EXPORT_SYMBOL(phys_to_machine_mapping);

/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 **/

char * __init xen_memory_setup(void)
{
	unsigned long max_pfn = xen_start_info->nr_pages;

	e820.nr_map = 0;
	add_memory_region(0, LOWMEMSIZE(), E820_RAM);
	add_memory_region(HIGH_MEMORY, PFN_PHYS(max_pfn)-HIGH_MEMORY, E820_RAM);

	return "Xen";
}

static void xen_idle(void)
{
	local_irq_disable();

	if (need_resched())
		local_irq_enable();
	else {
		current_thread_info()->status &= ~TS_POLLING;
		smp_mb__after_clear_bit();
		safe_halt();
		current_thread_info()->status |= TS_POLLING;
	}
}

/*
 * Set the bit indicating "nosegneg" library variants should be used.
 */
static void __init fiddle_vdso(void)
{
	extern const char vdso32_default_start;
	u32 *mask = VDSO32_SYMBOL(&vdso32_default_start, NOTE_MASK);
	*mask |= 1 << VDSO_NOTE_NONEGSEG_BIT;
}

void xen_enable_sysenter(void)
{
	int cpu = smp_processor_id();
	extern void xen_sysenter_target(void);
	/* Mask events on entry, even though they get enabled immediately */
	static struct callback_register sysenter = {
		.type = CALLBACKTYPE_sysenter,
		.address = { __KERNEL_CS, (unsigned long)xen_sysenter_target },
		.flags = CALLBACKF_mask_events,
	};

	if (!boot_cpu_has(X86_FEATURE_SEP) ||
	    HYPERVISOR_callback_op(CALLBACKOP_register, &sysenter) != 0) {
		clear_cpu_cap(&cpu_data(cpu), X86_FEATURE_SEP);
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_SEP);
	}
}

void __init xen_arch_setup(void)
{
	struct physdev_set_iopl set_iopl;
	int rc;

	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_4gb_segments);
	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);

	if (!xen_feature(XENFEAT_auto_translated_physmap))
		HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_pae_extended_cr3);

	HYPERVISOR_set_callbacks(__KERNEL_CS, (unsigned long)xen_hypervisor_callback,
				 __KERNEL_CS, (unsigned long)xen_failsafe_callback);

	xen_enable_sysenter();

	set_iopl.iopl = 1;
	rc = HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);
	if (rc != 0)
		printk(KERN_INFO "physdev_op failed %d\n", rc);

#ifdef CONFIG_ACPI
	if (!(xen_start_info->flags & SIF_INITDOMAIN)) {
		printk(KERN_INFO "ACPI in unprivileged domain disabled\n");
		disable_acpi();
	}
#endif

	memcpy(boot_command_line, xen_start_info->cmd_line,
	       MAX_GUEST_CMDLINE > COMMAND_LINE_SIZE ?
	       COMMAND_LINE_SIZE : MAX_GUEST_CMDLINE);

	pm_idle = xen_idle;

#ifdef CONFIG_SMP
	/* fill cpus_possible with all available cpus */
	xen_fill_possible_map();
#endif

	paravirt_disable_iospace();

	fiddle_vdso();
}
