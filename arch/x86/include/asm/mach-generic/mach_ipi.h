#ifndef ASM_X86__MACH_GENERIC__MACH_IPI_H
#define ASM_X86__MACH_GENERIC__MACH_IPI_H

#include <asm/genapic.h>

#define send_IPI_mask (genapic->send_IPI_mask)
#define send_IPI_allbutself (genapic->send_IPI_allbutself)
#define send_IPI_all (genapic->send_IPI_all)

#endif /* ASM_X86__MACH_GENERIC__MACH_IPI_H */
