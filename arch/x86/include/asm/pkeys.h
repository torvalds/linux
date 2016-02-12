#ifndef _ASM_X86_PKEYS_H
#define _ASM_X86_PKEYS_H

#define arch_max_pkey() (boot_cpu_has(X86_FEATURE_OSPKE) ? 16 : 1)

#endif /*_ASM_X86_PKEYS_H */
