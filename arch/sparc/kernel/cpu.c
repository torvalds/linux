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

DEFINE_PER_CPU(cpuinfo_sparc, __cpu_data) = { 0 };

struct cpu_iu_info {
  int psr_impl;
  int psr_vers;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  int psr_impl;
  int fp_vers;
  char* fp_name;
};

/* In order to get the fpu type correct, you need to take the IDPROM's
 * machine type value into consideration too.  I will fix this.
 */
struct cpu_fp_info linux_sparc_fpu[] = {
  { 0, 0, "Fujitsu MB86910 or Weitek WTL1164/5"},
  { 0, 1, "Fujitsu MB86911 or Weitek WTL1164/5 or LSI L64831"},
  { 0, 2, "LSI Logic L64802 or Texas Instruments ACT8847"},
  /* SparcStation SLC, SparcStation1 */
  { 0, 3, "Weitek WTL3170/2"},
  /* SPARCstation-5 */
  { 0, 4, "Lsi Logic/Meiko L64804 or compatible"},
  { 0, 5, "reserved"},
  { 0, 6, "reserved"},
  { 0, 7, "No FPU"},
  { 1, 0, "ROSS HyperSparc combined IU/FPU"},
  { 1, 1, "Lsi Logic L64814"},
  { 1, 2, "Texas Instruments TMS390-C602A"},
  { 1, 3, "Cypress CY7C602 FPU"},
  { 1, 4, "reserved"},
  { 1, 5, "reserved"},
  { 1, 6, "reserved"},
  { 1, 7, "No FPU"},
  { 2, 0, "BIT B5010 or B5110/20 or B5210"},
  { 2, 1, "reserved"},
  { 2, 2, "reserved"},
  { 2, 3, "reserved"},
  { 2, 4, "reserved"},
  { 2, 5, "reserved"},
  { 2, 6, "reserved"},
  { 2, 7, "No FPU"},
  /* SuperSparc 50 module */
  { 4, 0, "SuperSparc on-chip FPU"},
  /* SparcClassic */
  { 4, 4, "TI MicroSparc on chip FPU"},
  { 5, 0, "Matsushita MN10501"},
  { 5, 1, "reserved"},
  { 5, 2, "reserved"},
  { 5, 3, "reserved"},
  { 5, 4, "reserved"},
  { 5, 5, "reserved"},
  { 5, 6, "reserved"},
  { 5, 7, "No FPU"},
  { 9, 3, "Fujitsu or Weitek on-chip FPU"},
};

#define NSPARCFPU  ARRAY_SIZE(linux_sparc_fpu)

struct cpu_iu_info linux_sparc_chips[] = {
  /* Sun4/100, 4/200, SLC */
  { 0, 0, "Fujitsu  MB86900/1A or LSI L64831 SparcKIT-40"},
  /* borned STP1012PGA */
  { 0, 4, "Fujitsu  MB86904"},
  { 0, 5, "Fujitsu TurboSparc MB86907"},
  /* SparcStation2, SparcServer 490 & 690 */
  { 1, 0, "LSI Logic Corporation - L64811"},
  /* SparcStation2 */
  { 1, 1, "Cypress/ROSS CY7C601"},
  /* Embedded controller */
  { 1, 3, "Cypress/ROSS CY7C611"},
  /* Ross Technologies HyperSparc */
  { 1, 0xf, "ROSS HyperSparc RT620"},
  { 1, 0xe, "ROSS HyperSparc RT625 or RT626"},
  /* ECL Implementation, CRAY S-MP Supercomputer... AIEEE! */
  /* Someone please write the code to support this beast! ;) */
  { 2, 0, "Bipolar Integrated Technology - B5010"},
  { 3, 0, "LSI Logic Corporation - unknown-type"},
  { 4, 0, "Texas Instruments, Inc. - SuperSparc-(II)"},
  /* SparcClassic  --  borned STP1010TAB-50*/
  { 4, 1, "Texas Instruments, Inc. - MicroSparc"},
  { 4, 2, "Texas Instruments, Inc. - MicroSparc II"},
  { 4, 3, "Texas Instruments, Inc. - SuperSparc 51"},
  { 4, 4, "Texas Instruments, Inc. - SuperSparc 61"},
  { 4, 5, "Texas Instruments, Inc. - unknown"},
  { 5, 0, "Matsushita - MN10501"},
  { 6, 0, "Philips Corporation - unknown"},
  { 7, 0, "Harvest VLSI Design Center, Inc. - unknown"},
  /* Gallium arsenide 200MHz, BOOOOGOOOOMIPS!!! */
  { 8, 0, "Systems and Processes Engineering Corporation (SPEC)"},
  { 9, 0, "Fujitsu or Weitek Power-UP"},
  { 9, 1, "Fujitsu or Weitek Power-UP"},
  { 9, 2, "Fujitsu or Weitek Power-UP"},
  { 9, 3, "Fujitsu or Weitek Power-UP"},
  { 0xa, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xb, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xc, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xd, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xe, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xf, 0, "UNKNOWN CPU-VENDOR/TYPE"},
};

#define NSPARCCHIPS  ARRAY_SIZE(linux_sparc_chips)

char *sparc_cpu_type;
char *sparc_fpu_type;

unsigned int fsr_storage;

void __init cpu_probe(void)
{
	int psr_impl, psr_vers, fpu_vers;
	int i, psr;

	psr_impl = ((get_psr()>>28)&0xf);
	psr_vers = ((get_psr()>>24)&0xf);

	psr = get_psr();
	put_psr(psr | PSR_EF);
	fpu_vers = ((get_fsr()>>17)&0x7);
	put_psr(psr);

	for(i = 0; i<NSPARCCHIPS; i++) {
		if(linux_sparc_chips[i].psr_impl == psr_impl)
			if(linux_sparc_chips[i].psr_vers == psr_vers) {
				sparc_cpu_type = linux_sparc_chips[i].cpu_name;
				break;
			}
	}

	if(i==NSPARCCHIPS)
		printk("DEBUG: psr.impl = 0x%x   psr.vers = 0x%x\n", psr_impl, 
			    psr_vers);

	for(i = 0; i<NSPARCFPU; i++) {
		if(linux_sparc_fpu[i].psr_impl == psr_impl)
			if(linux_sparc_fpu[i].fp_vers == fpu_vers) {
				sparc_fpu_type = linux_sparc_fpu[i].fp_name;
				break;
			}
	}

	if(i == NSPARCFPU) {
		printk("DEBUG: psr.impl = 0x%x  fsr.vers = 0x%x\n", psr_impl,
			    fpu_vers);
		sparc_fpu_type = linux_sparc_fpu[31].fp_name;
	}
}
