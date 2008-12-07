/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/head.h>
#include <asm/psr.h>
#include <asm/mbus.h>
#include <asm/cpudata.h>

#include "kernel.h"

DEFINE_PER_CPU(cpuinfo_sparc, __cpu_data) = { 0 };

struct cpu_info {
	int psr_vers;
	const char *name;
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
	4,
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
}};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */

const char *sparc_cpu_type;
const char *sparc_fpu_type;

unsigned int fsr_storage;

static void set_cpu_and_fpu(int psr_impl, int psr_vers, int fpu_vers)
{
	sparc_cpu_type = NULL;
	sparc_fpu_type = NULL;
	if (psr_impl < ARRAY_SIZE(manufacturer_info))
	{
		const struct cpu_info *cpu;
		const struct fpu_info *fpu;

		cpu = &manufacturer_info[psr_impl].cpu_info[0];
		while (cpu->psr_vers != -1)
		{
			if (cpu->psr_vers == psr_vers) {
				sparc_cpu_type = cpu->name;
				sparc_fpu_type = "No FPU";
				break;
			}
			cpu++;
		}
		fpu =  &manufacturer_info[psr_impl].fpu_info[0];
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
}

void __cpuinit cpu_probe(void)
{
	int psr_impl, psr_vers, fpu_vers;
	int psr;

	psr_impl = ((get_psr() >> 28) & 0xf);
	psr_vers = ((get_psr() >> 24) & 0xf);

	psr = get_psr();
	put_psr(psr | PSR_EF);
	fpu_vers = ((get_fsr() >> 17) & 0x7);
	put_psr(psr);

	set_cpu_and_fpu(psr_impl, psr_vers, fpu_vers);
}
