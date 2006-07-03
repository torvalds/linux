/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
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

DEFINE_PER_CPU(cpuinfo_sparc, __cpu_data) = { 0 };

struct cpu_iu_info {
  short manuf;
  short impl;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  short manuf;
  short impl;
  char fpu_vers;
  char* fp_name;
};

struct cpu_fp_info linux_sparc_fpu[] = {
  { 0x17, 0x10, 0, "UltraSparc I integrated FPU"},
  { 0x22, 0x10, 0, "UltraSparc I integrated FPU"},
  { 0x17, 0x11, 0, "UltraSparc II integrated FPU"},
  { 0x17, 0x12, 0, "UltraSparc IIi integrated FPU"},
  { 0x17, 0x13, 0, "UltraSparc IIe integrated FPU"},
  { 0x3e, 0x14, 0, "UltraSparc III integrated FPU"},
  { 0x3e, 0x15, 0, "UltraSparc III+ integrated FPU"},
  { 0x3e, 0x16, 0, "UltraSparc IIIi integrated FPU"},
  { 0x3e, 0x18, 0, "UltraSparc IV integrated FPU"},
  { 0x3e, 0x19, 0, "UltraSparc IV+ integrated FPU"},
  { 0x3e, 0x22, 0, "UltraSparc IIIi+ integrated FPU"},
};

#define NSPARCFPU  ARRAY_SIZE(linux_sparc_fpu)

struct cpu_iu_info linux_sparc_chips[] = {
  { 0x17, 0x10, "TI UltraSparc I   (SpitFire)"},
  { 0x22, 0x10, "TI UltraSparc I   (SpitFire)"},
  { 0x17, 0x11, "TI UltraSparc II  (BlackBird)"},
  { 0x17, 0x12, "TI UltraSparc IIi (Sabre)"},
  { 0x17, 0x13, "TI UltraSparc IIe (Hummingbird)"},
  { 0x3e, 0x14, "TI UltraSparc III (Cheetah)"},
  { 0x3e, 0x15, "TI UltraSparc III+ (Cheetah+)"},
  { 0x3e, 0x16, "TI UltraSparc IIIi (Jalapeno)"},
  { 0x3e, 0x18, "TI UltraSparc IV (Jaguar)"},
  { 0x3e, 0x19, "TI UltraSparc IV+ (Panther)"},
  { 0x3e, 0x22, "TI UltraSparc IIIi+ (Serrano)"},
};

#define NSPARCCHIPS  ARRAY_SIZE(linux_sparc_chips)

char *sparc_cpu_type = "cpu-oops";
char *sparc_fpu_type = "fpu-oops";

unsigned int fsr_storage;

void __init cpu_probe(void)
{
	unsigned long ver, fpu_vers, manuf, impl, fprs;
	int i;
	
	if (tlb_type == hypervisor) {
		sparc_cpu_type = "UltraSparc T1 (Niagara)";
		sparc_fpu_type = "UltraSparc T1 integrated FPU";
		return;
	}

	fprs = fprs_read();
	fprs_write(FPRS_FEF);
	__asm__ __volatile__ ("rdpr %%ver, %0; stx %%fsr, [%1]"
			      : "=&r" (ver)
			      : "r" (&fpu_vers));
	fprs_write(fprs);
	
	manuf = ((ver >> 48) & 0xffff);
	impl = ((ver >> 32) & 0xffff);

	fpu_vers = ((fpu_vers >> 17) & 0x7);

retry:
	for (i = 0; i < NSPARCCHIPS; i++) {
		if (linux_sparc_chips[i].manuf == manuf) {
			if (linux_sparc_chips[i].impl == impl) {
				sparc_cpu_type =
					linux_sparc_chips[i].cpu_name;
				break;
			}
		}
	}

	if (i == NSPARCCHIPS) {
 		/* Maybe it is a cheetah+ derivative, report it as cheetah+
 		 * in that case until we learn the real names.
 		 */
 		if (manuf == 0x3e &&
 		    impl > 0x15) {
 			impl = 0x15;
 			goto retry;
 		} else {
 			printk("DEBUG: manuf[%lx] impl[%lx]\n",
 			       manuf, impl);
 		}
		sparc_cpu_type = "Unknown CPU";
	}

	for (i = 0; i < NSPARCFPU; i++) {
		if (linux_sparc_fpu[i].manuf == manuf &&
		    linux_sparc_fpu[i].impl == impl) {
			if (linux_sparc_fpu[i].fpu_vers == fpu_vers) {
				sparc_fpu_type =
					linux_sparc_fpu[i].fp_name;
				break;
			}
		}
	}

	if (i == NSPARCFPU) {
 		printk("DEBUG: manuf[%lx] impl[%lx] fsr.vers[%lx]\n",
 		       manuf, impl, fpu_vers);
		sparc_fpu_type = "Unknown FPU";
	}
}
