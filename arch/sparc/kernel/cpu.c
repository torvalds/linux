/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>

#include <asm/spitfire.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/head.h>
#include <asm/psr.h>
#include <asm/mbus.h>
#include <asm/cpudata.h>

#include "kernel.h"
#include "entry.h"

DEFINE_PER_CPU(cpuinfo_sparc, __cpu_data) = { 0 };
EXPORT_PER_CPU_SYMBOL(__cpu_data);

int ncpus_probed;
unsigned int fsr_storage;

struct cpu_info {
	int psr_vers;
	const char *name;
	const char *pmu_name;
};

struct fpu_info {
	int fp_vers;
	const char *name;
};

#define NOCPU 8
#define NOFPU 8

struct manufacturer_info {
	int psr_impl;
	struct cpu_info cpu_info[NOCPU];
	struct fpu_info fpu_info[NOFPU];
};

#define CPU(ver, _name) \
{ .psr_vers = ver, .name = _name }

#define CPU_PMU(ver, _name, _pmu_name)	\
{ .psr_vers = ver, .name = _name, .pmu_name = _pmu_name }

#define FPU(ver, _name) \
{ .fp_vers = ver, .name = _name }

static const struct manufacturer_info __initconst manufacturer_info[] = {
{
	0,
	/* Sun4/100, 4/200, SLC */
	.cpu_info = {
		CPU(0, "Fujitsu  MB86900/1A or LSI L64831 SparcKIT-40"),
		/* borned STP1012PGA */
		CPU(4,  "Fujitsu  MB86904"),
		CPU(5, "Fujitsu TurboSparc MB86907"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0, "Fujitsu MB86910 or Weitek WTL1164/5"),
		FPU(1, "Fujitsu MB86911 or Weitek WTL1164/5 or LSI L64831"),
		FPU(2, "LSI Logic L64802 or Texas Instruments ACT8847"),
		/* SparcStation SLC, SparcStation1 */
		FPU(3, "Weitek WTL3170/2"),
		/* SPARCstation-5 */
		FPU(4, "Lsi Logic/Meiko L64804 or compatible"),
		FPU(-1, NULL)
	}
},{
	1,
	.cpu_info = {
		/* SparcStation2, SparcServer 490 & 690 */
		CPU(0, "LSI Logic Corporation - L64811"),
		/* SparcStation2 */
		CPU(1, "Cypress/ROSS CY7C601"),
		/* Embedded controller */
		CPU(3, "Cypress/ROSS CY7C611"),
		/* Ross Technologies HyperSparc */
		CPU(0xf, "ROSS HyperSparc RT620"),
		CPU(0xe, "ROSS HyperSparc RT625 or RT626"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0, "ROSS HyperSparc combined IU/FPU"),
		FPU(1, "Lsi Logic L64814"),
		FPU(2, "Texas Instruments TMS390-C602A"),
		FPU(3, "Cypress CY7C602 FPU"),
		FPU(-1, NULL)
	}
},{
	2,
	.cpu_info = {
		/* ECL Implementation, CRAY S-MP Supercomputer... AIEEE! */
		/* Someone please write the code to support this beast! ;) */
		CPU(0, "Bipolar Integrated Technology - B5010"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(-1, NULL)
	}
},{
	3,
	.cpu_info = {
		CPU(0, "LSI Logic Corporation - unknown-type"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(-1, NULL)
	}
},{
	PSR_IMPL_TI,
	.cpu_info = {
		CPU(0, "Texas Instruments, Inc. - SuperSparc-(II)"),
		/* SparcClassic  --  borned STP1010TAB-50*/
		CPU(1, "Texas Instruments, Inc. - MicroSparc"),
		CPU(2, "Texas Instruments, Inc. - MicroSparc II"),
		CPU(3, "Texas Instruments, Inc. - SuperSparc 51"),
		CPU(4, "Texas Instruments, Inc. - SuperSparc 61"),
		CPU(5, "Texas Instruments, Inc. - unknown"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		/* SuperSparc 50 module */
		FPU(0, "SuperSparc on-chip FPU"),
		/* SparcClassic */
		FPU(4, "TI MicroSparc on chip FPU"),
		FPU(-1, NULL)
	}
},{
	5,
	.cpu_info = {
		CPU(0, "Matsushita - MN10501"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0, "Matsushita MN10501"),
		FPU(-1, NULL)
	}
},{
	6,
	.cpu_info = {
		CPU(0, "Philips Corporation - unknown"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(-1, NULL)
	}
},{
	7,
	.cpu_info = {
		CPU(0, "Harvest VLSI Design Center, Inc. - unknown"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(-1, NULL)
	}
},{
	8,
	.cpu_info = {
		CPU(0, "Systems and Processes Engineering Corporation (SPEC)"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(-1, NULL)
	}
},{
	9,
	.cpu_info = {
		/* Gallium arsenide 200MHz, BOOOOGOOOOMIPS!!! */
		CPU(0, "Fujitsu or Weitek Power-UP"),
		CPU(1, "Fujitsu or Weitek Power-UP"),
		CPU(2, "Fujitsu or Weitek Power-UP"),
		CPU(3, "Fujitsu or Weitek Power-UP"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(3, "Fujitsu or Weitek on-chip FPU"),
		FPU(-1, NULL)
	}
},{
	PSR_IMPL_LEON,		/* Aeroflex Gaisler */
	.cpu_info = {
		CPU(3, "LEON"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(2, "GRFPU"),
		FPU(3, "GRFPU-Lite"),
		FPU(-1, NULL)
	}
},{
	0x17,
	.cpu_info = {
		CPU_PMU(0x10, "TI UltraSparc I   (SpitFire)", "ultra12"),
		CPU_PMU(0x11, "TI UltraSparc II  (BlackBird)", "ultra12"),
		CPU_PMU(0x12, "TI UltraSparc IIi (Sabre)", "ultra12"),
		CPU_PMU(0x13, "TI UltraSparc IIe (Hummingbird)", "ultra12"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0x10, "UltraSparc I integrated FPU"),
		FPU(0x11, "UltraSparc II integrated FPU"),
		FPU(0x12, "UltraSparc IIi integrated FPU"),
		FPU(0x13, "UltraSparc IIe integrated FPU"),
		FPU(-1, NULL)
	}
},{
	0x22,
	.cpu_info = {
		CPU_PMU(0x10, "TI UltraSparc I   (SpitFire)", "ultra12"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0x10, "UltraSparc I integrated FPU"),
		FPU(-1, NULL)
	}
},{
	0x3e,
	.cpu_info = {
		CPU_PMU(0x14, "TI UltraSparc III (Cheetah)", "ultra3"),
		CPU_PMU(0x15, "TI UltraSparc III+ (Cheetah+)", "ultra3+"),
		CPU_PMU(0x16, "TI UltraSparc IIIi (Jalapeno)", "ultra3i"),
		CPU_PMU(0x18, "TI UltraSparc IV (Jaguar)", "ultra3+"),
		CPU_PMU(0x19, "TI UltraSparc IV+ (Panther)", "ultra4+"),
		CPU_PMU(0x22, "TI UltraSparc IIIi+ (Serrano)", "ultra3i"),
		CPU(-1, NULL)
	},
	.fpu_info = {
		FPU(0x14, "UltraSparc III integrated FPU"),
		FPU(0x15, "UltraSparc III+ integrated FPU"),
		FPU(0x16, "UltraSparc IIIi integrated FPU"),
		FPU(0x18, "UltraSparc IV integrated FPU"),
		FPU(0x19, "UltraSparc IV+ integrated FPU"),
		FPU(0x22, "UltraSparc IIIi+ integrated FPU"),
		FPU(-1, NULL)
	}
}};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */

static const char *sparc_cpu_type;
static const char *sparc_fpu_type;
const char *sparc_pmu_type;


static void __init set_cpu_and_fpu(int psr_impl, int psr_vers, int fpu_vers)
{
	const struct manufacturer_info *manuf;
	int i;

	sparc_cpu_type = NULL;
	sparc_fpu_type = NULL;
	sparc_pmu_type = NULL;
	manuf = NULL;

	for (i = 0; i < ARRAY_SIZE(manufacturer_info); i++)
	{
		if (psr_impl == manufacturer_info[i].psr_impl) {
			manuf = &manufacturer_info[i];
			break;
		}
	}
	if (manuf != NULL)
	{
		const struct cpu_info *cpu;
		const struct fpu_info *fpu;

		cpu = &manuf->cpu_info[0];
		while (cpu->psr_vers != -1)
		{
			if (cpu->psr_vers == psr_vers) {
				sparc_cpu_type = cpu->name;
				sparc_pmu_type = cpu->pmu_name;
				sparc_fpu_type = "No FPU";
				break;
			}
			cpu++;
		}
		fpu =  &manuf->fpu_info[0];
		while (fpu->fp_vers != -1)
		{
			if (fpu->fp_vers == fpu_vers) {
				sparc_fpu_type = fpu->name;
				break;
			}
			fpu++;
		}
	}
	if (sparc_cpu_type == NULL)
	{
		printk(KERN_ERR "CPU: Unknown chip, impl[0x%x] vers[0x%x]\n",
		       psr_impl, psr_vers);
		sparc_cpu_type = "Unknown CPU";
	}
	if (sparc_fpu_type == NULL)
	{
		printk(KERN_ERR "FPU: Unknown chip, impl[0x%x] vers[0x%x]\n",
		       psr_impl, fpu_vers);
		sparc_fpu_type = "Unknown FPU";
	}
	if (sparc_pmu_type == NULL)
		sparc_pmu_type = "Unknown PMU";
}

#ifdef CONFIG_SPARC32
static int show_cpuinfo(struct seq_file *m, void *__unused)
{
	seq_printf(m,
		   "cpu\t\t: %s\n"
		   "fpu\t\t: %s\n"
		   "promlib\t\t: Version %d Revision %d\n"
		   "prom\t\t: %d.%d\n"
		   "type\t\t: %s\n"
		   "ncpus probed\t: %d\n"
		   "ncpus active\t: %d\n"
#ifndef CONFIG_SMP
		   "CPU0Bogo\t: %lu.%02lu\n"
		   "CPU0ClkTck\t: %ld\n"
#endif
		   ,
		   sparc_cpu_type,
		   sparc_fpu_type ,
		   romvec->pv_romvers,
		   prom_rev,
		   romvec->pv_printrev >> 16,
		   romvec->pv_printrev & 0xffff,
		   &cputypval[0],
		   ncpus_probed,
		   num_online_cpus()
#ifndef CONFIG_SMP
		   , cpu_data(0).udelay_val/(500000/HZ),
		   (cpu_data(0).udelay_val/(5000/HZ)) % 100,
		   cpu_data(0).clock_tick
#endif
		);

#ifdef CONFIG_SMP
	smp_bogo(m);
#endif
	mmu_info(m);
#ifdef CONFIG_SMP
	smp_info(m);
#endif
	return 0;
}
#endif /* CONFIG_SPARC32 */

#ifdef CONFIG_SPARC64
unsigned int dcache_parity_tl1_occurred;
unsigned int icache_parity_tl1_occurred;


static int show_cpuinfo(struct seq_file *m, void *__unused)
{
	seq_printf(m,
		   "cpu\t\t: %s\n"
		   "fpu\t\t: %s\n"
		   "pmu\t\t: %s\n"
		   "prom\t\t: %s\n"
		   "type\t\t: %s\n"
		   "ncpus probed\t: %d\n"
		   "ncpus active\t: %d\n"
		   "D$ parity tl1\t: %u\n"
		   "I$ parity tl1\t: %u\n"
#ifndef CONFIG_SMP
		   "Cpu0ClkTck\t: %016lx\n"
#endif
		   ,
		   sparc_cpu_type,
		   sparc_fpu_type,
		   sparc_pmu_type,
		   prom_version,
		   ((tlb_type == hypervisor) ?
		    "sun4v" :
		    "sun4u"),
		   ncpus_probed,
		   num_online_cpus(),
		   dcache_parity_tl1_occurred,
		   icache_parity_tl1_occurred
#ifndef CONFIG_SMP
		   , cpu_data(0).clock_tick
#endif
		);
	cpucap_info(m);
#ifdef CONFIG_SMP
	smp_bogo(m);
#endif
	mmu_info(m);
#ifdef CONFIG_SMP
	smp_info(m);
#endif
	return 0;
}
#endif /* CONFIG_SPARC64 */

static void *c_start(struct seq_file *m, loff_t *pos)
{
	/* The pointer we are returning is arbitrary,
	 * it just has to be non-NULL and not IS_ERR
	 * in the success case.
	 */
	return *pos == 0 ? &c_start : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

#ifdef CONFIG_SPARC32
static int __init cpu_type_probe(void)
{
	int psr_impl, psr_vers, fpu_vers;
	int psr;

	psr_impl = ((get_psr() >> PSR_IMPL_SHIFT) & PSR_IMPL_SHIFTED_MASK);
	psr_vers = ((get_psr() >> PSR_VERS_SHIFT) & PSR_VERS_SHIFTED_MASK);

	psr = get_psr();
	put_psr(psr | PSR_EF);

	if (psr_impl == PSR_IMPL_LEON)
		fpu_vers = get_psr() & PSR_EF ? ((get_fsr() >> 17) & 0x7) : 7;
	else
		fpu_vers = ((get_fsr() >> 17) & 0x7);

	put_psr(psr);

	set_cpu_and_fpu(psr_impl, psr_vers, fpu_vers);

	return 0;
}
#endif /* CONFIG_SPARC32 */

#ifdef CONFIG_SPARC64
static void __init sun4v_cpu_probe(void)
{
	switch (sun4v_chip_type) {
	case SUN4V_CHIP_NIAGARA1:
		sparc_cpu_type = "UltraSparc T1 (Niagara)";
		sparc_fpu_type = "UltraSparc T1 integrated FPU";
		sparc_pmu_type = "niagara";
		break;

	case SUN4V_CHIP_NIAGARA2:
		sparc_cpu_type = "UltraSparc T2 (Niagara2)";
		sparc_fpu_type = "UltraSparc T2 integrated FPU";
		sparc_pmu_type = "niagara2";
		break;

	case SUN4V_CHIP_NIAGARA3:
		sparc_cpu_type = "UltraSparc T3 (Niagara3)";
		sparc_fpu_type = "UltraSparc T3 integrated FPU";
		sparc_pmu_type = "niagara3";
		break;

	case SUN4V_CHIP_NIAGARA4:
		sparc_cpu_type = "UltraSparc T4 (Niagara4)";
		sparc_fpu_type = "UltraSparc T4 integrated FPU";
		sparc_pmu_type = "niagara4";
		break;

	case SUN4V_CHIP_NIAGARA5:
		sparc_cpu_type = "UltraSparc T5 (Niagara5)";
		sparc_fpu_type = "UltraSparc T5 integrated FPU";
		sparc_pmu_type = "niagara5";
		break;

	case SUN4V_CHIP_SPARC_M6:
		sparc_cpu_type = "SPARC-M6";
		sparc_fpu_type = "SPARC-M6 integrated FPU";
		sparc_pmu_type = "sparc-m6";
		break;

	case SUN4V_CHIP_SPARC_M7:
		sparc_cpu_type = "SPARC-M7";
		sparc_fpu_type = "SPARC-M7 integrated FPU";
		sparc_pmu_type = "sparc-m7";
		break;

	case SUN4V_CHIP_SPARC_SN:
		sparc_cpu_type = "SPARC-SN";
		sparc_fpu_type = "SPARC-SN integrated FPU";
		sparc_pmu_type = "sparc-sn";
		break;

	case SUN4V_CHIP_SPARC64X:
		sparc_cpu_type = "SPARC64-X";
		sparc_fpu_type = "SPARC64-X integrated FPU";
		sparc_pmu_type = "sparc64-x";
		break;

	default:
		printk(KERN_WARNING "CPU: Unknown sun4v cpu type [%s]\n",
		       prom_cpu_compatible);
		sparc_cpu_type = "Unknown SUN4V CPU";
		sparc_fpu_type = "Unknown SUN4V FPU";
		sparc_pmu_type = "Unknown SUN4V PMU";
		break;
	}
}

static int __init cpu_type_probe(void)
{
	if (tlb_type == hypervisor) {
		sun4v_cpu_probe();
	} else {
		unsigned long ver;
		int manuf, impl;

		__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));

		manuf = ((ver >> 48) & 0xffff);
		impl = ((ver >> 32) & 0xffff);
		set_cpu_and_fpu(manuf, impl, impl);
	}
	return 0;
}
#endif /* CONFIG_SPARC64 */

early_initcall(cpu_type_probe);
