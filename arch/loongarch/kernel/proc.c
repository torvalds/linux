// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/idle.h>
#include <asm/processor.h>
#include <asm/time.h>

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long n = (unsigned long) v - 1;
	unsigned int isa = cpu_data[n].isa_level;
	unsigned int version = cpu_data[n].processor_id & 0xff;
	unsigned int fp_version = cpu_data[n].fpu_vers;

#ifdef CONFIG_SMP
	if (!cpu_online(n))
		return 0;
#endif

	/*
	 * For the first processor also print the system type
	 */
	if (n == 0)
		seq_printf(m, "system type\t\t: %s\n\n", get_system_type());

	seq_printf(m, "processor\t\t: %ld\n", n);
	seq_printf(m, "package\t\t\t: %d\n", cpu_data[n].package);
	seq_printf(m, "core\t\t\t: %d\n", cpu_data[n].core);
	seq_printf(m, "global_id\t\t: %d\n", cpu_data[n].global_id);
	seq_printf(m, "CPU Family\t\t: %s\n", __cpu_family[n]);
	seq_printf(m, "Model Name\t\t: %s\n", __cpu_full_name[n]);
	seq_printf(m, "CPU Revision\t\t: 0x%02x\n", version);
	seq_printf(m, "FPU Revision\t\t: 0x%02x\n", fp_version);
	seq_printf(m, "CPU MHz\t\t\t: %llu.%02llu\n",
		      cpu_clock_freq / 1000000, (cpu_clock_freq / 10000) % 100);
	seq_printf(m, "BogoMIPS\t\t: %llu.%02llu\n",
		      (lpj_fine * cpu_clock_freq / const_clock_freq) / (500000/HZ),
		      ((lpj_fine * cpu_clock_freq / const_clock_freq) / (5000/HZ)) % 100);
	seq_printf(m, "TLB Entries\t\t: %d\n", cpu_data[n].tlbsize);
	seq_printf(m, "Address Sizes\t\t: %d bits physical, %d bits virtual\n",
		      cpu_pabits + 1, cpu_vabits + 1);

	seq_printf(m, "ISA\t\t\t:");
	if (isa & LOONGARCH_CPU_ISA_LA32R)
		seq_printf(m, " loongarch32r");
	if (isa & LOONGARCH_CPU_ISA_LA32S)
		seq_printf(m, " loongarch32s");
	if (isa & LOONGARCH_CPU_ISA_LA64)
		seq_printf(m, " loongarch64");
	seq_printf(m, "\n");

	seq_printf(m, "Features\t\t:");
	if (cpu_has_cpucfg)	seq_printf(m, " cpucfg");
	if (cpu_has_lam)	seq_printf(m, " lam");
	if (cpu_has_ual)	seq_printf(m, " ual");
	if (cpu_has_fpu)	seq_printf(m, " fpu");
	if (cpu_has_lsx)	seq_printf(m, " lsx");
	if (cpu_has_lasx)	seq_printf(m, " lasx");
	if (cpu_has_crc32)	seq_printf(m, " crc32");
	if (cpu_has_complex)	seq_printf(m, " complex");
	if (cpu_has_crypto)	seq_printf(m, " crypto");
	if (cpu_has_ptw)	seq_printf(m, " ptw");
	if (cpu_has_lspw)	seq_printf(m, " lspw");
	if (cpu_has_lvz)	seq_printf(m, " lvz");
	if (cpu_has_lbt_x86)	seq_printf(m, " lbt_x86");
	if (cpu_has_lbt_arm)	seq_printf(m, " lbt_arm");
	if (cpu_has_lbt_mips)	seq_printf(m, " lbt_mips");
	seq_printf(m, "\n");

	seq_printf(m, "Hardware Watchpoint\t: %s", str_yes_no(cpu_has_watch));
	if (cpu_has_watch) {
		seq_printf(m, ", iwatch count: %d, dwatch count: %d",
		      cpu_data[n].watch_ireg_count, cpu_data[n].watch_dreg_count);
	}

	seq_printf(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	unsigned long i = *pos;

	return i < nr_cpu_ids ? (void *)(i + 1) : NULL;
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
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
