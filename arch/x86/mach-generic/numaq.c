/*
 * APIC driver for the IBM NUMAQ chipset.
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
#include <asm/numaq/apicdef.h>
#include <linux/smp.h>
#include <asm/numaq/apic.h>
#include <asm/numaq/ipi.h>
#include <asm/numaq/mpparse.h>
#include <asm/numaq/wakecpu.h>
#include <asm/numaq.h>

static int mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	numaq_mps_oem_check(mpc, oem, productid);
	return found_numaq;
}

static int probe_numaq(void)
{
	/* already know from get_memcfg_numaq() */
	return found_numaq;
}

/* Hook from generic ACPI tables.c */
static int acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}

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

struct genapic apic_numaq = APIC_INIT("NUMAQ", probe_numaq);
