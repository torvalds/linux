#ifndef _ASM_ARM64_VMALLOC_H
#define _ASM_ARM64_VMALLOC_H

#include <asm/cpufeature.h>

#define arch_disable_lazy_vunmap cpus_have_const_cap(ARM64_WORKAROUND_NO_DMA_ALIAS)

#endif /* _ASM_ARM64_VMALLOC_H */
