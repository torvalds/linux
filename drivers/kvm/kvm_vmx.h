#ifndef __KVM_VMX_H
#define __KVM_VMX_H

#ifdef __x86_64__
/*
 * avoid save/load MSR_SYSCALL_MASK and MSR_LSTAR by std vt
 * mechanism (cpu bug AA24)
 */
#define NR_BAD_MSRS 2
#else
#define NR_BAD_MSRS 0
#endif

#endif
