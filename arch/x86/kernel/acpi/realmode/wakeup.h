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
	u16 _jmp1;		/* ljmpl opcode, 32-bit only */
	u32 pmode_entry;	/* Protected mode resume point, 32-bit only */
	u16 _jmp2;		/* CS value, 32-bit only */
	u32 pmode_cr0;		/* Protected mode cr0 */
	u32 pmode_cr3;		/* Protected mode cr3 */
	u32 pmode_cr4;		/* Protected mode cr4 */
	u32 pmode_efer_low;	/* Protected mode EFER */
	u32 pmode_efer_high;
	u64 pmode_gdt;
	u32 realmode_flags;
	u32 real_magic;
	u16 trampoline_segment;	/* segment with trampoline code, 64-bit only */
	u32 signature;		/* To check we have correct structure */
} __attribute__((__packed__));

extern struct wakeup_header wakeup_header;
#endif

#define HEADER_OFFSET 0x3f00
#define WAKEUP_SIZE   0x4000

#endif /* ARCH_X86_KERNEL_ACPI_RM_WAKEUP_H */
