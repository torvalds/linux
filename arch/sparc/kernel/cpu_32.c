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

struct cpu_iu_info {
	int psr_impl;
	int psr_vers;
	char *cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
	int psr_impl;
	int fp_vers;
	char *fp_name;
};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */
#define CPU_FP(psr, ver, name) \
{ .psr_impl = (psr), .fp_vers = (ver), .fp_name = (name) }
static struct cpu_fp_info linux_sparc_fpu[] = {
	CPU_FP(0, 0, "Fujitsu MB86910 or Weitek WTL1164/5"),
	CPU_FP(0, 1, "Fujitsu MB86911 or Weitek WTL1164/5 or LSI L64831"),
	CPU_FP(0, 2, "LSI Logic L64802 or Texas Instruments ACT8847"),
	/* SparcStation SLC, SparcStation1 */
	CPU_FP(0, 3, "Weitek WTL3170/2"),
	/* SPARCstation-5 */
	CPU_FP(0, 4, "Lsi Logic/Meiko L64804 or compatible"),
	CPU_FP(0, 5, "reserved"),
	CPU_FP(0, 6, "reserved"),
	CPU_FP(0, 7, "No FPU"),
	CPU_FP(1, 0, "ROSS HyperSparc combined IU/FPU"),
	CPU_FP(1, 1, "Lsi Logic L64814"),
	CPU_FP(1, 2, "Texas Instruments TMS390-C602A"),
	CPU_FP(1, 3, "Cypress CY7C602 FPU"),
	CPU_FP(1, 4, "reserved"),
	CPU_FP(1, 5, "reserved"),
	CPU_FP(1, 6, "reserved"),
	CPU_FP(1, 7, "No FPU"),
	CPU_FP(2, 0, "BIT B5010 or B5110/20 or B5210"),
	CPU_FP(2, 1, "reserved"),
	CPU_FP(2, 2, "reserved"),
	CPU_FP(2, 3, "reserved"),
	CPU_FP(2, 4, "reserved"),
	CPU_FP(2, 5, "reserved"),
	CPU_FP(2, 6, "reserved"),
	CPU_FP(2, 7, "No FPU"),
	/* SuperSparc 50 module */
	CPU_FP(4, 0, "SuperSparc on-chip FPU"),
	/* SparcClassic */
	CPU_FP(4, 4, "TI MicroSparc on chip FPU"),
	CPU_FP(5, 0, "Matsushita MN10501"),
	CPU_FP(5, 1, "reserved"),
	CPU_FP(5, 2, "reserved"),
	CPU_FP(5, 3, "reserved"),
	CPU_FP(5, 4, "reserved"),
	CPU_FP(5, 5, "reserved"),
	CPU_FP(5, 6, "reserved"),
	CPU_FP(5, 7, "No FPU"),
	CPU_FP(9, 3, "Fujitsu or Weitek on-chip FPU"),
};

#define NSPARCFPU  ARRAY_SIZE(linux_sparc_fpu)

#define CPU_INFO(psr, ver, name) \
{ .psr_impl = (psr), .psr_vers = (ver), .cpu_name = (name) }
static struct cpu_iu_info linux_sparc_chips[] = {
	/* Sun4/100, 4/200, SLC */
	CPU_INFO(0, 0, "Fujitsu  MB86900/1A or LSI L64831 SparcKIT-40"),
	/* borned STP1012PGA */
	CPU_INFO(0, 4, "Fujitsu  MB86904"),
	CPU_INFO(0, 5, "Fujitsu TurboSparc MB86907"),
	/* SparcStation2, SparcServer 490 & 690 */
	CPU_INFO(1, 0, "LSI Logic Corporation - L64811"),
	/* SparcStation2 */
	CPU_INFO(1, 1, "Cypress/ROSS CY7C601"),
	/* Embedded controller */
	CPU_INFO(1, 3, "Cypress/ROSS CY7C611"),
	/* Ross Technologies HyperSparc */
	CPU_INFO(1, 0xf, "ROSS HyperSparc RT620"),
	CPU_INFO(1, 0xe, "ROSS HyperSparc RT625 or RT626"),
	/* ECL Implementation, CRAY S-MP Supercomputer... AIEEE! */
	/* Someone please write the code to support this beast! ;) */
	CPU_INFO(2, 0, "Bipolar Integrated Technology - B5010"),
	CPU_INFO(3, 0, "LSI Logic Corporation - unknown-type"),
	CPU_INFO(4, 0, "Texas Instruments, Inc. - SuperSparc-(II)"),
	/* SparcClassic  --  borned STP1010TAB-50*/
	CPU_INFO(4, 1, "Texas Instruments, Inc. - MicroSparc"),
	CPU_INFO(4, 2, "Texas Instruments, Inc. - MicroSparc II"),
	CPU_INFO(4, 3, "Texas Instruments, Inc. - SuperSparc 51"),
	CPU_INFO(4, 4, "Texas Instruments, Inc. - SuperSparc 61"),
	CPU_INFO(4, 5, "Texas Instruments, Inc. - unknown"),
	CPU_INFO(5, 0, "Matsushita - MN10501"),
	CPU_INFO(6, 0, "Philips Corporation - unknown"),
	CPU_INFO(7, 0, "Harvest VLSI Design Center, Inc. - unknown"),
	/* Gallium arsenide 200MHz, BOOOOGOOOOMIPS!!! */
	CPU_INFO(8, 0, "Systems and Processes Engineering Corporation (SPEC)"),
	CPU_INFO(9, 0, "Fujitsu or Weitek Power-UP"),
	CPU_INFO(9, 1, "Fujitsu or Weitek Power-UP"),
	CPU_INFO(9, 2, "Fujitsu or Weitek Power-UP"),
	CPU_INFO(9, 3, "Fujitsu or Weitek Power-UP"),
	CPU_INFO(0xa, 0, "UNKNOWN CPU-VENDOR/TYPE"),
	CPU_INFO(0xb, 0, "UNKNOWN CPU-VENDOR/TYPE"),
	CPU_INFO(0xc, 0, "UNKNOWN CPU-VENDOR/TYPE"),
	CPU_INFO(0xd, 0, "UNKNOWN CPU-VENDOR/TYPE"),
	CPU_INFO(0xe, 0, "UNKNOWN CPU-VENDOR/TYPE"),
	CPU_INFO(0xf, 0, "UNKNOWN CPU-VENDOR/TYPE"),
};

#define NSPARCCHIPS  ARRAY_SIZE(linux_sparc_chips)

const char *sparc_cpu_type;
const char *sparc_fpu_type;

unsigned int fsr_storage;

void __cpuinit cpu_probe(void)
{
	int psr_impl, psr_vers, fpu_vers;
	int i, psr;

	psr_impl = ((get_psr() >> 28) & 0xf);
	psr_vers = ((get_psr() >> 24) & 0xf);

	psr = get_psr();
	put_psr(psr | PSR_EF);
	fpu_vers = ((get_fsr() >> 17) & 0x7);
	put_psr(psr);

	for (i = 0; i < NSPARCCHIPS; i++) {
		if (linux_sparc_chips[i].psr_impl == psr_impl)
			if (linux_sparc_chips[i].psr_vers == psr_vers) {
				sparc_cpu_type = linux_sparc_chips[i].cpu_name;
				break;
			}
	}

	if (i == NSPARCCHIPS)
	{
		printk(KERN_ERR "CPU: Unknown chip, impl[0x%x] vers[0x%x]\n",
		       psr_impl, psr_vers);
		sparc_cpu_type = "Unknown CPU";
	}

	for (i = 0; i < NSPARCFPU; i++) {
		if (linux_sparc_fpu[i].psr_impl == psr_impl)
			if (linux_sparc_fpu[i].fp_vers == fpu_vers) {
				sparc_fpu_type = linux_sparc_fpu[i].fp_name;
				break;
			}
	}

	if (i == NSPARCFPU) {
		printk(KERN_ERR "FPU: Unknown chip, impl[0x%x] vers[0x%x]\n",
		       psr_impl, fpu_vers);
		sparc_fpu_type = "Unknown FPU";
	}
}
