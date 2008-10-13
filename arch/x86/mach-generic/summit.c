/*
 * APIC driver for the IBM "Summit" chipset.
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
#include <asm/summit/apicdef.h>
#include <linux/smp.h>
#include <asm/summit/apic.h>
#include <asm/summit/ipi.h>
#include <asm/summit/mpparse.h>

static int probe_summit(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

struct genapic apic_summit = APIC_INIT("summit", probe_summit);
