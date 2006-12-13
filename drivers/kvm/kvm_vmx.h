#ifndef __KVM_VMX_H
#define __KVM_VMX_H

#ifdef CONFIG_X86_64
/*
 * avoid save/load MSR_SYSCALL_MASK and MSR_LSTAR by std vt
 * mechanism (cpu bug AA24)
 */
#define NR_BAD_MSRS 2
#else
#define NR_BAD_MSRS 0
#endif

#endif
