/*
 * Common bits for X2APIC cluster/physical modes.
 */

#ifndef _ASM_X86_X2APIC_H
#define _ASM_X86_X2APIC_H

#include <asm/apic.h>
#include <asm/ipi.h>
#include <linux/cpumask.h>

static int x2apic_apic_id_valid(int apicid)
{
	return 1;
}

static int x2apic_apic_id_registered(void)
{
	return 1;
}

static void
__x2apic_send_IPI_dest(unsigned int apicid, int vector, unsigned int dest)
{
	unsigned long cfg = __prepare_ICR(0, vector, dest);
	native_x2apic_icr_write(cfg, apicid);
}

static unsigned int x2apic_get_apic_id(unsigned long id)
{
	return id;
}

static unsigned long x2apic_set_apic_id(unsigned int id)
{
	return id;
}

static int x2apic_phys_pkg_id(int initial_apicid, int index_msb)
{
	return initial_apicid >> index_msb;
}

static void x2apic_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

#endif /* _ASM_X86_X2APIC_H */
