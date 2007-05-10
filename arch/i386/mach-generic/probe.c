/* Copyright 2003 Andi Kleen, SuSE Labs. 
 * Subject to the GNU Public License, v.2 
 * 
 * Generic x86 APIC driver probe layer.
 */  
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/genapic.h>

extern struct genapic apic_summit;
extern struct genapic apic_bigsmp;
extern struct genapic apic_es7000;
extern struct genapic apic_default;

struct genapic *genapic = &apic_default;

struct genapic *apic_probe[] __initdata = { 
	&apic_summit,
	&apic_bigsmp, 
	&apic_es7000,
	&apic_default,	/* must be last */
	NULL,
};

static int cmdline_apic __initdata;
static int __init parse_apic(char *arg)
{
	int i;

	if (!arg)
		return -EINVAL;

	for (i = 0; apic_probe[i]; i++) {
		if (!strcmp(apic_probe[i]->name, arg)) {
			genapic = apic_probe[i];
			cmdline_apic = 1;
			return 0;
		}
	}

	/* Parsed again by __setup for debug/verbose */
	return 0;
}
early_param("apic", parse_apic);

void __init generic_bigsmp_probe(void)
{
	/*
	 * This routine is used to switch to bigsmp mode when
	 * - There is no apic= option specified by the user
	 * - generic_apic_probe() has choosen apic_default as the sub_arch
	 * - we find more than 8 CPUs in acpi LAPIC listing with xAPIC support
	 */

	if (!cmdline_apic && genapic == &apic_default)
		if (apic_bigsmp.probe()) {
			genapic = &apic_bigsmp;
			printk(KERN_INFO "Overriding APIC driver with %s\n",
			       genapic->name);
		}
}

void __init generic_apic_probe(void)
{ 
	if (!cmdline_apic) {
		int i;
		for (i = 0; apic_probe[i]; i++) {
			if (apic_probe[i]->probe()) {
				genapic = apic_probe[i];
				break;
			}
		}
		/* Not visible without early console */
		if (!apic_probe[i])
			panic("Didn't find an APIC driver");
	}
	printk(KERN_INFO "Using APIC driver %s\n", genapic->name);
} 

/* These functions can switch the APIC even after the initial ->probe() */

int __init mps_oem_check(struct mp_config_table *mpc, char *oem, char *productid)
{ 
	int i;
	for (i = 0; apic_probe[i]; ++i) { 
		if (apic_probe[i]->mps_oem_check(mpc,oem,productid)) { 
			if (!cmdline_apic) {
				genapic = apic_probe[i];
				printk(KERN_INFO "Switched to APIC driver `%s'.\n",
				       genapic->name);
			}
			return 1;
		} 
	} 
	return 0;
} 

int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	int i;
	for (i = 0; apic_probe[i]; ++i) { 
		if (apic_probe[i]->acpi_madt_oem_check(oem_id, oem_table_id)) { 
			if (!cmdline_apic) {
				genapic = apic_probe[i];
				printk(KERN_INFO "Switched to APIC driver `%s'.\n",
				       genapic->name);
			}
			return 1;
		} 
	} 
	return 0;	
}

int hard_smp_processor_id(void)
{
	return genapic->get_apic_id(*(unsigned long *)(APIC_BASE+APIC_ID));
}
