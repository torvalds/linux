/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_KEXEC_H
#define _ASM_PARISC_KEXEC_H

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE	4096

#define KEXEC_ARCH KEXEC_ARCH_PARISC
#define ARCH_HAS_KIMAGE_ARCH

#ifndef __ASSEMBLER__

struct kimage_arch {
	unsigned long initrd_start;
	unsigned long initrd_end;
	unsigned long cmdline;
};

static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	/* Dummy implementation for now */
}

#endif /* __ASSEMBLER__ */

#endif /* _ASM_PARISC_KEXEC_H */
