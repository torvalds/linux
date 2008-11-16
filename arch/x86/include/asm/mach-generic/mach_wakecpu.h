#ifndef _ASM_X86_MACH_GENERIC_MACH_WAKECPU_H
#define _ASM_X86_MACH_GENERIC_MACH_WAKECPU_H

#define TRAMPOLINE_PHYS_LOW (genapic->trampoline_phys_low)
#define TRAMPOLINE_PHYS_HIGH (genapic->trampoline_phys_high)
#define wait_for_init_deassert (genapic->wait_for_init_deassert)
#define smp_callin_clear_local_apic (genapic->smp_callin_clear_local_apic)
#define store_NMI_vector (genapic->store_NMI_vector)
#define restore_NMI_vector (genapic->restore_NMI_vector)
#define inquire_remote_apic (genapic->inquire_remote_apic)

#endif /* _ASM_X86_MACH_GENERIC_MACH_APIC_H */
