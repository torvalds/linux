/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/smp.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/mach-es7000/mach_apicdef.h>
#include <asm/mach-es7000/mach_apic.h>
#include <asm/mach-es7000/mach_ipi.h>
#include <asm/mach-es7000/mach_mpparse.h>
#include <asm/mach-es7000/mach_wakecpu.h>

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

extern void es7000_sw_apic(void);
static void __init enable_apic_mode(void)
{
	es7000_sw_apic();
	return;
}

static __init int mps_oem_check(struct mp_config_table *mpc, char *oem,
		char *productid)
{
	if (mpc->mpc_oemptr) {
		struct mp_config_oemtable *oem_table =
			(struct mp_config_oemtable *)mpc->mpc_oemptr;
		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr;
	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (es7000_check_dsdt())
			return parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			return 1;
		}
	}
	return 0;
}
#else
static int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif

struct genapic __initdata_refok apic_es7000 = APIC_INIT("es7000", probe_es7000);
