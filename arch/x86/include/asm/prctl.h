#ifndef _ASM_X86_PRCTL_H
#define _ASM_X86_PRCTL_H

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

#ifdef CONFIG_X86_64
extern long sys_arch_prctl(int, unsigned long);
#endif /* CONFIG_X86_64 */

#endif /* _ASM_X86_PRCTL_H */
