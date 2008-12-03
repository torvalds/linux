/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996, 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/fpumacro.h>
#include <asm/cpudata.h>
#include <asm/spitfire.h>
#include <asm/oplib.h>

#include "entry.h"

DEFINE_PER_CPU(cpuinfo_sparc, __cpu_data) = { 0 };

struct cpu_chip_info {
	unsigned short	manuf;
	unsigned short	impl;
	const char	*cpu_name;
	const char	*fp_name;
};

static const struct cpu_chip_info cpu_chips[] = {
	{
		.manuf		= 0x17,
		.impl		= 0x10,
		.cpu_name	= "TI UltraSparc I   (SpitFire)",
		.fp_name	= "UltraSparc I integrated FPU",
	},
	{
		.manuf		= 0x22,
		.impl		= 0x10,
		.cpu_name	= "TI UltraSparc I   (SpitFire)",
		.fp_name	= "UltraSparc I integrated FPU",
	},
	{
		.manuf		= 0x17,
		.impl		= 0x11,
		.cpu_name	= "TI UltraSparc II  (BlackBird)",
		.fp_name	= "UltraSparc II integrated FPU",
	},
	{
		.manuf		= 0x17,
		.impl		= 0x12,
		.cpu_name	= "TI UltraSparc IIi (Sabre)",
		.fp_name	= "UltraSparc IIi integrated FPU",
	},
	{
		.manuf		= 0x17,
		.impl		= 0x13,
		.cpu_name	= "TI UltraSparc IIe (Hummingbird)",
		.fp_name	= "UltraSparc IIe integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x14,
		.cpu_name	= "TI UltraSparc III (Cheetah)",
		.fp_name	= "UltraSparc III integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x15,
		.cpu_name	= "TI UltraSparc III+ (Cheetah+)",
		.fp_name	= "UltraSparc III+ integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x16,
		.cpu_name	= "TI UltraSparc IIIi (Jalapeno)",
		.fp_name	= "UltraSparc IIIi integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x18,
		.cpu_name	= "TI UltraSparc IV (Jaguar)",
		.fp_name	= "UltraSparc IV integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x19,
		.cpu_name	= "TI UltraSparc IV+ (Panther)",
		.fp_name	= "UltraSparc IV+ integrated FPU",
	},
	{
		.manuf		= 0x3e,
		.impl		= 0x22,
		.cpu_name	= "TI UltraSparc IIIi+ (Serrano)",
		.fp_name	= "UltraSparc IIIi+ integrated FPU",
	},
};

#define NSPARCCHIPS ARRAY_SIZE(linux_sparc_chips)

const char *sparc_cpu_type;
const char *sparc_fpu_type;

static void __init sun4v_cpu_probe(void)
{
	switch (sun4v_chip_type) {
	case SUN4V_CHIP_NIAGARA1:
		sparc_cpu_type = "UltraSparc T1 (Niagara)";
		sparc_fpu_type = "UltraSparc T1 integrated FPU";
		break;

	case SUN4V_CHIP_NIAGARA2:
		sparc_cpu_type = "UltraSparc T2 (Niagara2)";
		sparc_fpu_type = "UltraSparc T2 integrated FPU";
		break;

	default:
		printk(KERN_WARNING "CPU: Unknown sun4v cpu type [%s]\n",
		       prom_cpu_compatible);
		sparc_cpu_type = "Unknown SUN4V CPU";
		sparc_fpu_type = "Unknown SUN4V FPU";
		break;
	}
}

static const struct cpu_chip_info * __init find_cpu_chip(unsigned short manuf,
							 unsigned short impl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cpu_chips); i++) {
		const struct cpu_chip_info *p = &cpu_chips[i];

		if (p->manuf == manuf && p->impl == impl)
			return p;
	}
	return NULL;
}

static int __init cpu_type_probe(void)
{
	if (tlb_type == hypervisor) {
		sun4v_cpu_probe();
	} else {
		unsigned long ver, manuf, impl;
		const struct cpu_chip_info *p;
	
		__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
	
		manuf = ((ver >> 48) & 0xffff);
		impl = ((ver >> 32) & 0xffff);

		p = find_cpu_chip(manuf, impl);
		if (p) {
			sparc_cpu_type = p->cpu_name;
			sparc_fpu_type = p->fp_name;
		} else {
			printk(KERN_ERR "CPU: Unknown chip, manuf[%lx] impl[%lx]\n",
			       manuf, impl);
			sparc_cpu_type = "Unknown CPU";
			sparc_fpu_type = "Unknown FPU";
		}
	}
	return 0;
}

arch_initcall(cpu_type_probe);
