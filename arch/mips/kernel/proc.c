/*
 *  Copyright (C) 1995, 1996, 2001  Ralf Baechle
 *  Copyright (C) 2001, 2004  MIPS Technologies, Inc.
 *  Copyright (C) 2004  Maciej W. Rozycki
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/mips_machine.h>

unsigned int vced_count, vcei_count;

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long n = (unsigned long) v - 1;
	unsigned int version = cpu_data[n].processor_id;
	unsigned int fp_vers = cpu_data[n].fpu_id;
	char fmt [64];
	int i;

#ifdef CONFIG_SMP
	if (!cpu_online(n))
		return 0;
#endif

	/*
	 * For the first processor also print the system type
	 */
	if (n == 0) {
		seq_printf(m, "system type\t\t: %s\n", get_system_type());
		if (mips_get_machine_name())
			seq_printf(m, "machine\t\t\t: %s\n",
				   mips_get_machine_name());
	}

	seq_printf(m, "processor\t\t: %ld\n", n);
	sprintf(fmt, "cpu model\t\t: %%s V%%d.%%d%s\n",
		      cpu_data[n].options & MIPS_CPU_FPU ? "  FPU V%d.%d" : "");
	seq_printf(m, fmt, __cpu_name[n],
		      (version >> 4) & 0x0f, version & 0x0f,
		      (fp_vers >> 4) & 0x0f, fp_vers & 0x0f);
	seq_printf(m, "BogoMIPS\t\t: %u.%02u\n",
		      cpu_data[n].udelay_val / (500000/HZ),
		      (cpu_data[n].udelay_val / (5000/HZ)) % 100);
	seq_printf(m, "wait instruction\t: %s\n", cpu_wait ? "yes" : "no");
	seq_printf(m, "microsecond timers\t: %s\n",
		      cpu_has_counter ? "yes" : "no");
	seq_printf(m, "tlb_entries\t\t: %d\n", cpu_data[n].tlbsize);
	seq_printf(m, "extra interrupt vector\t: %s\n",
		      cpu_has_divec ? "yes" : "no");
	seq_printf(m, "hardware watchpoint\t: %s",
		      cpu_has_watch ? "yes, " : "no\n");
	if (cpu_has_watch) {
		seq_printf(m, "count: %d, address/irw mask: [",
		      cpu_data[n].watch_reg_count);
		for (i = 0; i < cpu_data[n].watch_reg_count; i++)
			seq_printf(m, "%s0x%04x", i ? ", " : "" ,
				cpu_data[n].watch_reg_masks[i]);
		seq_printf(m, "]\n");
	}
	seq_printf(m, "ASEs implemented\t:%s%s%s%s%s%s\n",
		      cpu_has_mips16 ? " mips16" : "",
		      cpu_has_mdmx ? " mdmx" : "",
		      cpu_has_mips3d ? " mips3d" : "",
		      cpu_has_smartmips ? " smartmips" : "",
		      cpu_has_dsp ? " dsp" : "",
		      cpu_has_mipsmt ? " mt" : ""
		);
	seq_printf(m, "shadow register sets\t: %d\n",
		      cpu_data[n].srsets);
	seq_printf(m, "kscratch registers\t: %d\n",
		      hweight8(cpu_data[n].kscratch_mask));
	seq_printf(m, "core\t\t\t: %d\n", cpu_data[n].core);

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

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
