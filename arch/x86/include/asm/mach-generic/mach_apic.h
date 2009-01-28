#ifndef _ASM_X86_MACH_GENERIC_MACH_APIC_H
#define _ASM_X86_MACH_GENERIC_MACH_APIC_H

#include <asm/genapic.h>

#define wakeup_secondary_cpu (apic->wakeup_cpu)

extern void generic_bigsmp_probe(void);

#endif /* _ASM_X86_MACH_GENERIC_MACH_APIC_H */
