#ifndef _ARM_KEXEC_H
#define _ARM_KEXEC_H

#ifdef CONFIG_KEXEC

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE	4096

#define KEXEC_ARCH KEXEC_ARCH_ARM

#define KEXEC_ARM_ATAGS_OFFSET  0x1000
#define KEXEC_ARM_ZIMAGE_OFFSET 0x8000

#ifndef __ASSEMBLY__

struct kimage;
/* Provide a dummy definition to avoid build failures. */
static inline void crash_setup_regs(struct pt_regs *newregs,
                                        struct pt_regs *oldregs) { }

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_KEXEC */

#endif /* _ARM_KEXEC_H */
