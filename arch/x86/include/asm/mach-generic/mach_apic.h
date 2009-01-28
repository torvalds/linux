#ifndef _ASM_X86_MACH_GENERIC_MACH_APIC_H
#define _ASM_X86_MACH_GENERIC_MACH_APIC_H

#include <asm/genapic.h>

#define cpu_mask_to_apicid (apic->cpu_mask_to_apicid)
#define cpu_mask_to_apicid_and (apic->cpu_mask_to_apicid_and)
#define enable_apic_mode (apic->enable_apic_mode)
#define phys_pkg_id (apic->phys_pkg_id)
#define wakeup_secondary_cpu (apic->wakeup_cpu)

extern void generic_bigsmp_probe(void);

#endif /* _ASM_X86_MACH_GENERIC_MACH_APIC_H */
