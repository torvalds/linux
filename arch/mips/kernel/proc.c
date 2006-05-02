/*
 *  linux/arch/mips/kernel/proc.c
 *
 *  Copyright (C) 1995, 1996, 2001  Ralf Baechle
 *  Copyright (C) 2001, 2004  MIPS Technologies, Inc.
 *  Copyright (C) 2004  Maciej W. Rozycki
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/watch.h>

unsigned int vced_count, vcei_count;

static const char *cpu_name[] = {
	[CPU_UNKNOWN]	= "unknown",
	[CPU_R2000]	= "R2000",
	[CPU_R3000]	= "R3000",
	[CPU_R3000A]	= "R3000A",
	[CPU_R3041]	= "R3041",
	[CPU_R3051]	= "R3051",
	[CPU_R3052]	= "R3052",
	[CPU_R3081]	= "R3081",
	[CPU_R3081E]	= "R3081E",
	[CPU_R4000PC]	= "R4000PC",
	[CPU_R4000SC]	= "R4000SC",
	[CPU_R4000MC]	= "R4000MC",
        [CPU_R4200]	= "R4200",
	[CPU_R4400PC]	= "R4400PC",
	[CPU_R4400SC]	= "R4400SC",
	[CPU_R4400MC]	= "R4400MC",
	[CPU_R4600]	= "R4600",
	[CPU_R6000]	= "R6000",
        [CPU_R6000A]	= "R6000A",
	[CPU_R8000]	= "R8000",
	[CPU_R10000]	= "R10000",
	[CPU_R12000]	= "R12000",
	[CPU_R4300]	= "R4300",
	[CPU_R4650]	= "R4650",
	[CPU_R4700]	= "R4700",
	[CPU_R5000]	= "R5000",
        [CPU_R5000A]	= "R5000A",
	[CPU_R4640]	= "R4640",
	[CPU_NEVADA]	= "Nevada",
	[CPU_RM7000]	= "RM7000",
	[CPU_RM9000]	= "RM9000",
	[CPU_R5432]	= "R5432",
	[CPU_4KC]	= "MIPS 4Kc",
        [CPU_5KC]	= "MIPS 5Kc",
	[CPU_R4310]	= "R4310",
	[CPU_SB1]	= "SiByte SB1",
	[CPU_SB1A]	= "SiByte SB1A",
	[CPU_TX3912]	= "TX3912",
	[CPU_TX3922]	= "TX3922",
	[CPU_TX3927]	= "TX3927",
	[CPU_AU1000]	= "Au1000",
	[CPU_AU1500]	= "Au1500",
	[CPU_AU1100]	= "Au1100",
	[CPU_AU1550]	= "Au1550",
	[CPU_AU1200]	= "Au1200",
	[CPU_4KEC]	= "MIPS 4KEc",
	[CPU_4KSC]	= "MIPS 4KSc",
	[CPU_VR41XX]	= "NEC Vr41xx",
	[CPU_R5500]	= "R5500",
	[CPU_TX49XX]	= "TX49xx",
	[CPU_20KC]	= "MIPS 20Kc",
	[CPU_24K]	= "MIPS 24K",
	[CPU_25KF]	= "MIPS 25Kf",
	[CPU_34K]	= "MIPS 34K",
	[CPU_74K]	= "MIPS 74K",
	[CPU_VR4111]	= "NEC VR4111",
	[CPU_VR4121]	= "NEC VR4121",
	[CPU_VR4122]	= "NEC VR4122",
	[CPU_VR4131]	= "NEC VR4131",
	[CPU_VR4133]	= "NEC VR4133",
	[CPU_VR4181]	= "NEC VR4181",
	[CPU_VR4181A]	= "NEC VR4181A",
	[CPU_SR71000]	= "Sandcraft SR71000",
	[CPU_PR4450]	= "Philips PR4450",
};


static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned int version = current_cpu_data.processor_id;
	unsigned int fp_vers = current_cpu_data.fpu_id;
	unsigned long n = (unsigned long) v - 1;
	char fmt [64];

#ifdef CONFIG_SMP
	if (!cpu_isset(n, cpu_online_map))
		return 0;
#endif

	/*
	 * For the first processor also print the system type
	 */
	if (n == 0)
		seq_printf(m, "system type\t\t: %s\n", get_system_type());

	seq_printf(m, "processor\t\t: %ld\n", n);
	sprintf(fmt, "cpu model\t\t: %%s V%%d.%%d%s\n",
	        cpu_has_fpu ? "  FPU V%d.%d" : "");
	seq_printf(m, fmt, cpu_name[current_cpu_data.cputype <= CPU_LAST ?
	                            current_cpu_data.cputype : CPU_UNKNOWN],
	                           (version >> 4) & 0x0f, version & 0x0f,
	                           (fp_vers >> 4) & 0x0f, fp_vers & 0x0f);
	seq_printf(m, "BogoMIPS\t\t: %lu.%02lu\n",
	              cpu_data[n].udelay_val / (500000/HZ),
	              (cpu_data[n].udelay_val / (5000/HZ)) % 100);
	seq_printf(m, "wait instruction\t: %s\n", cpu_wait ? "yes" : "no");
	seq_printf(m, "microsecond timers\t: %s\n",
	              cpu_has_counter ? "yes" : "no");
	seq_printf(m, "tlb_entries\t\t: %d\n", current_cpu_data.tlbsize);
	seq_printf(m, "extra interrupt vector\t: %s\n",
	              cpu_has_divec ? "yes" : "no");
	seq_printf(m, "hardware watchpoint\t: %s\n",
	              cpu_has_watch ? "yes" : "no");
	seq_printf(m, "ASEs implemented\t:%s%s%s%s%s%s\n",
		      cpu_has_mips16 ? " mips16" : "",
		      cpu_has_mdmx ? " mdmx" : "",
		      cpu_has_mips3d ? " mips3d" : "",
		      cpu_has_smartmips ? " smartmips" : "",
		      cpu_has_dsp ? " dsp" : "",
		      cpu_has_mipsmt ? " mt" : ""
		);

	sprintf(fmt, "VCE%%c exceptions\t\t: %s\n",
	        cpu_has_vce ? "%u" : "not available");
	seq_printf(m, fmt, 'D', vced_count);
	seq_printf(m, fmt, 'I', vcei_count);
	seq_printf(m, "\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	unsigned long i = *pos;

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

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
