#ifndef _ARCH_X86_REALMODE_H
#define _ARCH_X86_REALMODE_H

#include <linux/types.h>
#include <asm/io.h>

/* This must match data at realmode.S */
struct real_mode_header {
	u32	text_start;
	u32	ro_end;
	/* SMP trampoline */
	u32	trampoline_start;
	u32	trampoline_status;
	u32	trampoline_header;
#ifdef CONFIG_X86_64
	u32	trampoline_pgd;
#endif
	/* ACPI S3 wakeup */
#ifdef CONFIG_ACPI_SLEEP
	u32	wakeup_start;
	u32	wakeup_header;
#endif
	/* APM/BIOS reboot */
	u32	machine_real_restart_asm;
#ifdef CONFIG_X86_64
	u32	machine_real_restart_seg;
#endif
};

/* This must match data at trampoline_32/64.S */
struct trampoline_header {
#ifdef CONFIG_X86_32
	u32 start;
	u16 gdt_pad;
	u16 gdt_limit;
	u32 gdt_base;
#else
	u64 start;
	u64 efer;
	u32 cr4;
#endif
};

extern struct real_mode_header *real_mode_header;
extern unsigned char real_mode_blob_end[];

extern unsigned long init_rsp;
extern unsigned long initial_code;
extern unsigned long initial_gs;

extern unsigned char real_mode_blob[];
extern unsigned char real_mode_relocs[];

#ifdef CONFIG_X86_32
extern unsigned char startup_32_smp[];
extern unsigned char boot_gdt[];
#else
extern unsigned char secondary_startup_64[];
#endif

void reserve_real_mode(void);
void setup_real_mode(void);

#endif /* _ARCH_X86_REALMODE_H */
