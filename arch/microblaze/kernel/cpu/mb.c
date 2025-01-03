/*
 * CPU-version specific code
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006-2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/initrd.h>

#include <linux/bug.h>
#include <asm/cpuinfo.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/page.h>
#include <linux/param.h>
#include <asm/pvr.h>
#include <asm/sections.h>
#include <asm/setup.h>

static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *fpga_family = "Unknown";
	char *cpu_ver = "Unknown";
	int i;

	/* Denormalised to get the fpga family string */
	for (i = 0; family_string_lookup[i].s != NULL; i++) {
		if (cpuinfo.fpga_family_code == family_string_lookup[i].k) {
			fpga_family = (char *)family_string_lookup[i].s;
			break;
		}
	}

	/* Denormalised to get the hw version string */
	for (i = 0; cpu_ver_lookup[i].s != NULL; i++) {
		if (cpuinfo.ver_code == cpu_ver_lookup[i].k) {
			cpu_ver = (char *)cpu_ver_lookup[i].s;
			break;
		}
	}

	seq_printf(m,
		   "CPU-Family:	MicroBlaze\n"
		   "FPGA-Arch:	%s\n"
		   "CPU-Ver:	%s, %s endian\n"
		   "CPU-MHz:	%d.%02d\n"
		   "BogoMips:	%lu.%02lu\n",
		   fpga_family,
		   cpu_ver,
		   cpuinfo.endian ? "little" : "big",
		   cpuinfo.cpu_clock_freq / 1000000,
		   cpuinfo.cpu_clock_freq % 1000000,
		   loops_per_jiffy / (500000 / HZ),
		   (loops_per_jiffy / (5000 / HZ)) % 100);

	seq_printf(m,
		   "HW:\n Shift:\t\t%s\n"
		   " MSR:\t\t%s\n"
		   " PCMP:\t\t%s\n"
		   " DIV:\t\t%s\n",
		   str_yes_no(cpuinfo.use_instr & PVR0_USE_BARREL_MASK),
		   str_yes_no(cpuinfo.use_instr & PVR2_USE_MSR_INSTR),
		   str_yes_no(cpuinfo.use_instr & PVR2_USE_PCMP_INSTR),
		   str_yes_no(cpuinfo.use_instr & PVR0_USE_DIV_MASK));

	seq_printf(m, " MMU:\t\t%x\n", cpuinfo.mmu);

	seq_printf(m,
		   " MUL:\t\t%s\n"
		   " FPU:\t\t%s\n",
		   (cpuinfo.use_mult & PVR2_USE_MUL64_MASK) ? "v2" :
		   (cpuinfo.use_mult & PVR0_USE_HW_MUL_MASK) ? "v1" : "no",
		   (cpuinfo.use_fpu & PVR2_USE_FPU2_MASK) ? "v2" :
		   (cpuinfo.use_fpu & PVR0_USE_FPU_MASK) ? "v1" : "no");

	seq_printf(m,
		   " Exc:\t\t%s%s%s%s%s%s%s%s\n",
		   (cpuinfo.use_exc & PVR2_OPCODE_0x0_ILL_MASK) ? "op0x0 " : "",
		   (cpuinfo.use_exc & PVR2_UNALIGNED_EXC_MASK) ? "unal " : "",
		   (cpuinfo.use_exc & PVR2_ILL_OPCODE_EXC_MASK) ? "ill " : "",
		   (cpuinfo.use_exc & PVR2_IOPB_BUS_EXC_MASK) ? "iopb " : "",
		   (cpuinfo.use_exc & PVR2_DOPB_BUS_EXC_MASK) ? "dopb " : "",
		   (cpuinfo.use_exc & PVR2_DIV_ZERO_EXC_MASK) ? "zero " : "",
		   (cpuinfo.use_exc & PVR2_FPU_EXC_MASK) ? "fpu " : "",
		   (cpuinfo.use_exc & PVR2_USE_FSL_EXC) ? "fsl " : "");

	seq_printf(m,
		   "Stream-insns:\t%sprivileged\n",
		   cpuinfo.mmu_privins ? "un" : "");

	if (cpuinfo.use_icache)
		seq_printf(m,
			   "Icache:\t\t%ukB\tline length:\t%dB\n",
			   cpuinfo.icache_size >> 10,
			   cpuinfo.icache_line_length);
	else
		seq_puts(m, "Icache:\t\tno\n");

	if (cpuinfo.use_dcache) {
		seq_printf(m,
			   "Dcache:\t\t%ukB\tline length:\t%dB\n",
			   cpuinfo.dcache_size >> 10,
			   cpuinfo.dcache_line_length);
		seq_puts(m, "Dcache-Policy:\t");
		if (cpuinfo.dcache_wb)
			seq_puts(m, "write-back\n");
		else
			seq_puts(m, "write-through\n");
	} else {
		seq_puts(m, "Dcache:\t\tno\n");
	}

	seq_printf(m,
		   "HW-Debug:\t%s\n",
		   str_yes_no(cpuinfo.hw_debug));

	seq_printf(m,
		   "PVR-USR1:\t%02x\n"
		   "PVR-USR2:\t%08x\n",
		   cpuinfo.pvr_user1,
		   cpuinfo.pvr_user2);

	seq_printf(m, "Page size:\t%lu\n", PAGE_SIZE);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	int i = *pos;

	return i < NR_CPUS ? (void *) (i + 1) : NULL;
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
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};
