#ifndef _ASM_X86_MACH_DEFAULT_MACH_MPPARSE_H
#define _ASM_X86_MACH_DEFAULT_MACH_MPPARSE_H

static inline int
generic_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	return 0;
}

/* Hook from generic ACPI tables.c */
static inline int default_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}


#endif /* _ASM_X86_MACH_DEFAULT_MACH_MPPARSE_H */
