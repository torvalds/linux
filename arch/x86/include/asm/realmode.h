#ifndef _ARCH_X86_REALMODE_H
#define _ARCH_X86_REALMODE_H

#include <linux/types.h>
#include <asm/io.h>

/* This must match data at realmode.S */
struct real_mode_header {
	u32	text_start;
	u32	ro_end;
	u32	end;
	/* reboot */
#ifdef CONFIG_X86_32
	u32	machine_real_restart_asm;
#endif
} __attribute__((__packed__));

extern struct real_mode_header real_mode_header;
extern unsigned char *real_mode_base;

extern unsigned long init_rsp;
extern unsigned long initial_code;
extern unsigned long initial_gs;

extern unsigned char real_mode_blob[];
extern unsigned char real_mode_relocs[];

extern void __init setup_real_mode(void);

#endif /* _ARCH_X86_REALMODE_H */
