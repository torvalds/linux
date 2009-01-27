#ifndef _ASM_X86_GENAPIC_64_H
#define _ASM_X86_GENAPIC_64_H

extern struct genapic *genapic;

extern struct genapic apic_flat;
extern struct genapic apic_physflat;
extern struct genapic apic_x2apic_cluster;
extern struct genapic apic_x2apic_phys;
extern int acpi_madt_oem_check(char *, char *);

extern void apic_send_IPI_self(int vector);

extern struct genapic apic_x2apic_uv_x;
DECLARE_PER_CPU(int, x2apic_extra_bits);

extern void setup_apic_routing(void);

#endif /* _ASM_X86_GENAPIC_64_H */
