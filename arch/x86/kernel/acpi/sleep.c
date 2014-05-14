/*
 * sleep.c - x86-specific ACPI sleep support.
 *
 *  Copyright (C) 2001-2003 Patrick Mochel
 *  Copyright (C) 2001-2003 Pavel Machek <pavel@ucw.cz>
 */

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/dmi.h>
#include <linux/cpumask.h>
#include <asm/segment.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/realmode.h>

#include "../../realmode/rm/wakeup.h"
#include "sleep.h"

unsigned long acpi_realmode_flags;

#if defined(CONFIG_SMP) && defined(CONFIG_64BIT)
static char temp_stack[4096];
#endif

/**
 * x86_acpi_enter_sleep_state - enter sleep state
 * @state: Sleep state to enter.
 *
 * Wrapper around acpi_enter_sleep_state() to be called by assmebly.
 */
acpi_status asmlinkage __visible x86_acpi_enter_sleep_state(u8 state)
{
	return acpi_enter_sleep_state(state);
}

/**
 * x86_acpi_suspend_lowlevel - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int x86_acpi_suspend_lowlevel(void)
{
	struct wakeup_header *header =
		(struct wakeup_header *) __va(real_mode_header->wakeup_header);

	if (header->signature != WAKEUP_HEADER_SIGNATURE) {
		printk(KERN_ERR "wakeup header does not match\n");
		return -EINVAL;
	}

	header->video_mode = saved_video_mode;

	header->pmode_behavior = 0;

#ifndef CONFIG_64BIT
	native_store_gdt((struct desc_ptr *)&header->pmode_gdt);

	/*
	 * We have to check that we can write back the value, and not
	 * just read it.  At least on 90 nm Pentium M (Family 6, Model
	 * 13), reading an invalid MSR is not guaranteed to trap, see
	 * Erratum X4 in "Intel Pentium M Processor on 90 nm Process
	 * with 2-MB L2 Cache and Intel® Processor A100 and A110 on 90
	 * nm process with 512-KB L2 Cache Specification Update".
	 */
	if (!rdmsr_safe(MSR_EFER,
			&header->pmode_efer_low,
			&header->pmode_efer_high) &&
	    !wrmsr_safe(MSR_EFER,
			header->pmode_efer_low,
			header->pmode_efer_high))
		header->pmode_behavior |= (1 << WAKEUP_BEHAVIOR_RESTORE_EFER);
#endif /* !CONFIG_64BIT */

	header->pmode_cr0 = read_cr0();
	if (__this_cpu_read(cpu_info.cpuid_level) >= 0) {
		header->pmode_cr4 = read_cr4();
		header->pmode_behavior |= (1 << WAKEUP_BEHAVIOR_RESTORE_CR4);
	}
	if (!rdmsr_safe(MSR_IA32_MISC_ENABLE,
			&header->pmode_misc_en_low,
			&header->pmode_misc_en_high) &&
	    !wrmsr_safe(MSR_IA32_MISC_ENABLE,
			header->pmode_misc_en_low,
			header->pmode_misc_en_high))
		header->pmode_behavior |=
			(1 << WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE);
	header->realmode_flags = acpi_realmode_flags;
	header->real_magic = 0x12345678;

#ifndef CONFIG_64BIT
	header->pmode_entry = (u32)&wakeup_pmode_return;
	header->pmode_cr3 = (u32)__pa_symbol(initial_page_table);
	saved_magic = 0x12345678;
#else /* CONFIG_64BIT */
#ifdef CONFIG_SMP
	stack_start = (unsigned long)temp_stack + sizeof(temp_stack);
	early_gdt_descr.address =
			(unsigned long)get_cpu_gdt_table(smp_processor_id());
	initial_gs = per_cpu_offset(smp_processor_id());
#endif
	initial_code = (unsigned long)wakeup_long64;
       saved_magic = 0x123456789abcdef0L;
#endif /* CONFIG_64BIT */

	do_suspend_lowlevel();
	return 0;
}

static int __init acpi_sleep_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "s3_bios", 7) == 0)
			acpi_realmode_flags |= 1;
		if (strncmp(str, "s3_mode", 7) == 0)
			acpi_realmode_flags |= 2;
		if (strncmp(str, "s3_beep", 7) == 0)
			acpi_realmode_flags |= 4;
#ifdef CONFIG_HIBERNATION
		if (strncmp(str, "s4_nohwsig", 10) == 0)
			acpi_no_s4_hw_signature();
#endif
		if (strncmp(str, "nonvs", 5) == 0)
			acpi_nvs_nosave();
		if (strncmp(str, "nonvs_s3", 8) == 0)
			acpi_nvs_nosave_s3();
		if (strncmp(str, "old_ordering", 12) == 0)
			acpi_old_suspend_ordering();
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_sleep=", acpi_sleep_setup);
