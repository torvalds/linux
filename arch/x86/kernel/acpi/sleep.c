/*
 * sleep.c - x86-specific ACPI sleep support.
 *
 *  Copyright (C) 2001-2003 Patrick Mochel
 *  Copyright (C) 2001-2003 Pavel Machek <pavel@suse.cz>
 */

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/dmi.h>
#include <linux/cpumask.h>

#include <asm/smp.h>

/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address = 0;
unsigned long acpi_realmode_flags;
extern char wakeup_start, wakeup_end;

extern unsigned long acpi_copy_wakeup_routine(unsigned long);

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_save_state_mem(void)
{
	if (!acpi_wakeup_address) {
		printk(KERN_ERR "Could not allocate memory during boot, S3 disabled\n");
		return -ENOMEM;
	}
	memcpy((void *)acpi_wakeup_address, &wakeup_start,
	       &wakeup_end - &wakeup_start);
	acpi_copy_wakeup_routine(acpi_wakeup_address);

	return 0;
}

/*
 * acpi_restore_state - undo effects of acpi_save_state_mem
 */
void acpi_restore_state_mem(void)
{
}


/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page from the first 1MB of memory for the wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16MB pages, but not
 * <1MB pages.
 */
void __init acpi_reserve_bootmem(void)
{
	if ((&wakeup_end - &wakeup_start) > PAGE_SIZE*2) {
		printk(KERN_ERR
		       "ACPI: Wakeup code way too big, S3 disabled.\n");
		return;
	}

	acpi_wakeup_address = (unsigned long)alloc_bootmem_low(PAGE_SIZE*2);
	if (!acpi_wakeup_address)
		printk(KERN_ERR "ACPI: Cannot allocate lowmem, S3 disabled.\n");
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
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_sleep=", acpi_sleep_setup);
