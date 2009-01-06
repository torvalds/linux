/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/es7000/apicdef.h>
#include <linux/smp.h>
#include <asm/es7000/apic.h>
#include <asm/es7000/ipi.h>
#include <asm/es7000/mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

void __init es7000_update_genapic_to_cluster(void)
{
	genapic->target_cpus = target_cpus_cluster;
	genapic->int_delivery_mode = INT_DELIVERY_MODE_CLUSTER;
	genapic->int_dest_mode = INT_DEST_MODE_CLUSTER;
	genapic->no_balance_irq = NO_BALANCE_IRQ_CLUSTER;

	genapic->init_apic_ldr = init_apic_ldr_cluster;

	genapic->cpu_mask_to_apicid = cpu_mask_to_apicid_cluster;
}

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

static __init int
mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;
		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt)
			ret = parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}
	return ret;
}
#else
static int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif

static void vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}

struct genapic __initdata_refok apic_es7000 = APIC_INIT("es7000", probe_es7000);
