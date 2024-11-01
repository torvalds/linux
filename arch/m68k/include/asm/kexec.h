/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_KEXEC_H
#define _ASM_M68K_KEXEC_H

#ifdef CONFIG_KEXEC

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE	4096

#define KEXEC_ARCH KEXEC_ARCH_68K

#ifndef __ASSEMBLY__

static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	/* Dummy implementation for now */
}

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_KEXEC */

#endif /* _ASM_M68K_KEXEC_H */
