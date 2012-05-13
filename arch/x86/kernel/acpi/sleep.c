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

#include "realmode/wakeup.h"
#include "sleep.h"

unsigned long acpi_realmode_flags;

#if defined(CONFIG_SMP) && defined(CONFIG_64BIT)
static char temp_stack[4096];
#endif

asmlinkage void acpi_enter_s3(void)
{
	acpi_enter_sleep_state(3, wake_sleep_flags);
}
/**
 * acpi_suspend_lowlevel - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_suspend_lowlevel(void)
{
	struct wakeup_header *header;
	/* address in low memory of the wakeup routine. */
	char *acpi_realmode;

	acpi_realmode = TRAMPOLINE_SYM(acpi_wakeup_code);

	header = (struct wakeup_header *)(acpi_realmode + WAKEUP_HEADER_OFFSET);
	if (header->signature != WAKEUP_HEADER_SIGNATURE) {
		printk(KERN_ERR "wakeup header does not match\n");
		return -EINVAL;
	}

	header->video_mode = saved_video_mode;

	header->wakeup_jmp_seg = acpi_wakeup_address >> 4;

	/*
	 * Set up the wakeup GDT.  We set these up as Big Real Mode,
	 * that is, with limits set to 4 GB.  At least the Lenovo
	 * Thinkpad X61 is known to need this for the video BIOS
	 * initialization quirk to work; this is likely to also
	 * be the case for other laptops or integrated video devices.
	 */

	/* GDT[0]: GDT self-pointer */
	header->wakeup_gdt[0] =
		(u64)(sizeof(header->wakeup_gdt) - 1) +
		((u64)__pa(&header->wakeup_gdt) << 16);
	/* GDT[1]: big real mode-like code segment */
	header->wakeup_gdt[1] =
		GDT_ENTRY(0x809b, acpi_wakeup_address, 0xfffff);
	/* GDT[2]: big real mode-like data segment */
	header->wakeup_gdt[2] =
		GDT_ENTRY(0x8093, acpi_wakeup_address, 0xfffff);

#ifndef CONFIG_64BIT
	store_gdt((struct desc_ptr *)&header->pmode_gdt);

	if (rdmsr_safe(MSR_EFER, &header->pmode_efer_low,
		       &header->pmode_efer_high))
		header->pmode_efer_low = header->pmode_efer_high = 0;
#endif /* !CONFIG_64BIT */

	header->pmode_cr0 = read_cr0();
	header->pmode_cr4 = read_cr4_safe();
	header->pmode_behavior = 0;
	if (!rdmsr_safe(MSR_IA32_MISC_ENABLE,
			&header->pmode_misc_en_low,
			&header->pmode_misc_en_high))
		header->pmode_behavior |=
			(1 << WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE);
	header->realmode_flags = acpi_realmode_flags;
	header->real_magic = 0x12345678;

#ifndef CONFIG_64BIT
	header->pmode_entry = (u32)&wakeup_pmode_return;
	header->pmode_cr3 = (u32)__pa(&initial_page_table);
	saved_magic = 0x12345678;
#else /* CONFIG_64BIT */
	header->trampoline_segment = trampoline_address() >> 4;
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
		if (strncmp(str, "old_ordering", 12) == 0)
			acpi_old_suspend_ordering();
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_sleep=", acpi_sleep_setup);
