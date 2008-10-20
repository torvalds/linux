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

static int mps_oem_check(struct mp_config_table *mpc, char *oem,
		char *productid)
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

struct genapic apic_numaq = APIC_INIT("NUMAQ", probe_numaq);
