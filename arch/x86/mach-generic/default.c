/* 
 * Default generic APIC driver. This handles up to 8 CPUs.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/mach-default/mach_apicdef.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/mach-default/mach_apic.h>
#include <asm/mach-default/mach_ipi.h>
#include <asm/mach-default/mach_mpparse.h>

/* should be called last. */
static int probe_default(void)
{ 
	return 1;
} 

struct genapic apic_default = APIC_INIT("default", probe_default); 
