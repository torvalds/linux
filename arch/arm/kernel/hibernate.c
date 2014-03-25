/*
 * Hibernation support specific for ARM
 *
 * Derived from work on ARM hibernation support by:
 *
 * Ubuntu project, hibernation support for mach-dove
 * Copyright (C) 2010 Nokia Corporation (Hiroshi Doyu)
 * Copyright (C) 2010 Texas Instruments, Inc. (Teerth Reddy et al.)
 *  https://lkml.org/lkml/2010/6/18/4
 *  https://lists.linux-foundation.org/pipermail/linux-pm/2010-June/027422.html
 *  https://patchwork.kernel.org/patch/96442/
 *
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <asm/system_misc.h>
#include <asm/idmap.h>
#include <asm/suspend.h>
#include <asm/memory.h>

extern const void __nosave_begin, __nosave_end;

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = virt_to_pfn(&__nosave_begin);
	unsigned long nosave_end_pfn = virt_to_pfn(&__nosave_end - 1);

	return (pfn >= nosave_begin_pfn) && (pfn <= nosave_end_pfn);
}

void notrace save_processor_state(void)
{
	WARN_ON(num_online_cpus() != 1);
	local_fiq_disable();
}

void notrace restore_processor_state(void)
{
	local_fiq_enable();
}

/*
 * Snapshot kernel memory and reset the system.
 *
 * swsusp_save() is executed in the suspend finisher so that the CPU
 * context pointer and memory are part of the saved image, which is
 * required by the resume kernel image to restart execution from
 * swsusp_arch_suspend().
 *
 * soft_restart is not technically needed, but is used to get success
 * returned from cpu_suspend.
 *
 * When soft reboot completes, the hibernation snapshot is written out.
 */
static int notrace arch_save_image(unsigned long unused)
{
	int ret;

	ret = swsusp_save();
	if (ret == 0)
		soft_restart(virt_to_phys(cpu_resume));
	return ret;
}

/*
 * Save the current CPU state before suspend / poweroff.
 */
int notrace swsusp_arch_suspend(void)
{
	return cpu_suspend(0, arch_save_image);
}

/*
 * Restore page contents for physical pages that were in use during loading
 * hibernation image.  Switch to idmap_pgd so the physical page tables
 * are overwritten with the same contents.
 */
static void notrace arch_restore_image(void *unused)
{
	struct pbe *pbe;

	cpu_switch_mm(idmap_pgd, &init_mm);
	for (pbe = restore_pblist; pbe; pbe = pbe->next)
		copy_page(pbe->orig_address, pbe->address);

	soft_restart(virt_to_phys(cpu_resume));
}

static u64 resume_stack[PAGE_SIZE/2/sizeof(u64)] __nosavedata;

/*
 * Resume from the hibernation image.
 * Due to the kernel heap / data restore, stack contents change underneath
 * and that would make function calls impossible; switch to a temporary
 * stack within the nosave region to avoid that problem.
 */
int swsusp_arch_resume(void)
{
	extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);
	call_with_stack(arch_restore_image, 0,
		resume_stack + ARRAY_SIZE(resume_stack));
	return 0;
}
