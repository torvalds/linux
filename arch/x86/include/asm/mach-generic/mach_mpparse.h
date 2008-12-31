#ifndef _ASM_X86_MACH_GENERIC_MACH_MPPARSE_H
#define _ASM_X86_MACH_GENERIC_MACH_MPPARSE_H


extern int mps_oem_check(struct mp_config_table *mpc, char *oem,
			 char *productid);

extern int acpi_madt_oem_check(char *oem_id, char *oem_table_id);

#endif /* _ASM_X86_MACH_GENERIC_MACH_MPPARSE_H */
