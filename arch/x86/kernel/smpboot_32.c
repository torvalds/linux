/*
 *	x86 SMP booting functions
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium,
 *	Pentium Pro and Pentium-II/Xeon MP machines.
 *	Original development of Linux SMP code supported by Caldera.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 *
 *	Fixes
 *		Felix Koop	:	NR_CPUS used properly
 *		Jose Renau	:	Handle single CPU case.
 *		Alan Cox	:	By repeated request 8) - Total BogoMIPS report.
 *		Greg Wright	:	Fix for kernel stacks panic.
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *	Matthias Sattler	:	Changes for 2.1 kernel map.
 *	Michel Lespinasse	:	Changes for 2.1 kernel map.
 *	Michael Chastain	:	Change trampoline.S to gnu as.
 *		Alan Cox	:	Dumb bug: 'B' step PPro's are fine
 *		Ingo Molnar	:	Added APIC timers, based on code
 *					from Jose Renau
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Tigran Aivazian	:	fixed "0.00 in /proc/uptime on SMP" bug.
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs
 *		Martin J. Bligh	: 	Added support for multi-quad systems
 *		Dave Jones	:	Report invalid combinations of Athlon CPUs.
*		Rusty Russell	:	Hacked into shape for new "hotplug" boot process. */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/nmi.h>

#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/arch_hooks.h>
#include <asm/nmi.h>

#include <mach_apic.h>
#include <mach_wakecpu.h>
#include <smpboot_hooks.h>
#include <asm/vmi.h>
#include <asm/mtrr.h>

/* which logical CPU number maps to which CPU (physical APIC ID) */
u16 x86_cpu_to_apicid_init[NR_CPUS] __initdata =
			{ [0 ... NR_CPUS-1] = BAD_APICID };
void *x86_cpu_to_apicid_early_ptr;
DEFINE_PER_CPU(u16, x86_cpu_to_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_cpu_to_apicid);

u16 x86_bios_cpu_apicid_init[NR_CPUS] __initdata
				= { [0 ... NR_CPUS-1] = BAD_APICID };
void *x86_bios_cpu_apicid_early_ptr;
DEFINE_PER_CPU(u16, x86_bios_cpu_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

u8 apicid_2_node[MAX_APICID];

/* Where the IO area was mapped on multiquad, always 0 otherwise */
void *xquad_portio;
#ifdef CONFIG_X86_NUMAQ
EXPORT_SYMBOL(xquad_portio);
#endif
