/*
 * Definitions for the wakeup data structure at the head of the
 * wakeup code.
 */

#ifndef ARCH_X86_KERNEL_ACPI_RM_WAKEUP_H
#define ARCH_X86_KERNEL_ACPI_RM_WAKEUP_H

#ifndef __ASSEMBLY__
#include <linux/types.h>

/* This must match data at wakeup.S */
struct wakeup_header {
	u16 video_mode;		/* Video mode number */
	u32 pmode_entry;	/* Protected mode resume point, 32-bit only */
	u16 pmode_cs;
	u32 pmode_cr0;		/* Protected mode cr0 */
	u32 pmode_cr3;		/* Protected mode cr3 */
	u32 pmode_cr4;		/* Protected mode cr4 */
	u32 pmode_efer_low;	/* Protected mode EFER */
	u32 pmode_efer_high;
	u64 pmode_gdt;
	u32 pmode_misc_en_low;	/* Protected mode MISC_ENABLE */
	u32 pmode_misc_en_high;
	u32 pmode_behavior;	/* Wakeup routine behavior flags */
	u32 realmode_flags;
	u32 real_magic;
	u32 signature;		/* To check we have correct structure */
} __attribute__((__packed__));

extern struct wakeup_header wakeup_header;
#endif

#define WAKEUP_HEADER_OFFSET	8
#define WAKEUP_HEADER_SIGNATURE 0x51ee1111

/* Wakeup behavior bits */
#define WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE     0
#define WAKEUP_BEHAVIOR_RESTORE_CR4		1
#define WAKEUP_BEHAVIOR_RESTORE_EFER		2

#endif /* ARCH_X86_KERNEL_ACPI_RM_WAKEUP_H */
