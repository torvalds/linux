#ifndef _ASM_X86_MACH_GENERIC_MACH_APICDEF_H
#define _ASM_X86_MACH_GENERIC_MACH_APICDEF_H

#ifndef APIC_DEFINITION
#include <asm/genapic.h>

#define GET_APIC_ID (genapic->get_apic_id)
#define APIC_ID_MASK (genapic->apic_id_mask)
#endif

#endif /* _ASM_X86_MACH_GENERIC_MACH_APICDEF_H */
