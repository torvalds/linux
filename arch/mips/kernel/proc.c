// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1995, 1996, 2001  Ralf Baechle
 *  Copyright (C) 2001, 2004  MIPS Technologies, Inc.
 *  Copyright (C) 2004	Maciej W. Rozycki
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/idle.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/prom.h>

unsigned int vced_count, vcei_count;

/*
 * No lock; only written during early bootup by CPU 0.
 */
static RAW_NOTIFIER_HEAD(proc_cpuinfo_chain);

int __ref register_proc_cpuinfo_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&proc_cpuinfo_chain, nb);
}

int proc_cpuinfo_notifier_call_chain(unsigned long val, void *v)
{
	return raw_notifier_call_chain(&proc_cpuinfo_chain, val, v);
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct proc_cpuinfo_notifier_args proc_cpuinfo_notifier_args;
	unsigned long n = (unsigned long) v - 1;
	unsigned int version = cpu_data[n].processor_id;
	unsigned int fp_vers = cpu_data[n].fpu_id;
	char fmt[64];
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
			seq_printf(m, "%s0x%04x", i ? ", " : "",
				cpu_data[n].watch_reg_masks[i]);
		seq_puts(m, "]\n");
	}

	seq_puts(m, "isa\t\t\t:");
	if (cpu_has_mips_1)
		seq_puts(m, " mips1");
	if (cpu_has_mips_2)
		seq_puts(m, " mips2");
	if (cpu_has_mips_3)
		seq_puts(m, " mips3");
	if (cpu_has_mips_4)
		seq_puts(m, " mips4");
	if (cpu_has_mips_5)
		seq_puts(m, " mips5");
	if (cpu_has_mips32r1)
		seq_puts(m, " mips32r1");
	if (cpu_has_mips32r2)
		seq_puts(m, " mips32r2");
	if (cpu_has_mips32r5)
		seq_puts(m, " mips32r5");
	if (cpu_has_mips32r6)
		seq_puts(m, " mips32r6");
	if (cpu_has_mips64r1)
		seq_puts(m, " mips64r1");
	if (cpu_has_mips64r2)
		seq_puts(m, " mips64r2");
	if (cpu_has_mips64r5)
		seq_puts(m, " mips64r5");
	if (cpu_has_mips64r6)
		seq_puts(m, " mips64r6");
	seq_puts(m, "\n");

	seq_puts(m, "ASEs implemented\t:");
	if (cpu_has_mips16)
		seq_puts(m, " mips16");
	if (cpu_has_mips16e2)
		seq_puts(m, " mips16e2");
	if (cpu_has_mdmx)
		seq_puts(m, " mdmx");
	if (cpu_has_mips3d)
		seq_puts(m, " mips3d");
	if (cpu_has_smartmips)
		seq_puts(m, " smartmips");
	if (cpu_has_dsp)
		seq_puts(m, " dsp");
	if (cpu_has_dsp2)
		seq_puts(m, " dsp2");
	if (cpu_has_dsp3)
		seq_puts(m, " dsp3");
	if (cpu_has_mipsmt)
		seq_puts(m, " mt");
	if (cpu_has_mmips)
		seq_puts(m, " micromips");
	if (cpu_has_vz)
		seq_puts(m, " vz");
	if (cpu_has_msa)
		seq_puts(m, " msa");
	if (cpu_has_eva)
		seq_puts(m, " eva");
	if (cpu_has_htw)
		seq_puts(m, " htw");
	if (cpu_has_xpa)
		seq_puts(m, " xpa");
	if (cpu_has_loongson_mmi)
		seq_puts(m, " loongson-mmi");
	if (cpu_has_loongson_cam)
		seq_puts(m, " loongson-cam");
	if (cpu_has_loongson_ext)
		seq_puts(m, " loongson-ext");
	if (cpu_has_loongson_ext2)
		seq_puts(m, " loongson-ext2");
	seq_puts(m, "\n");

	if (cpu_has_mmips) {
		seq_printf(m, "micromips kernel\t: %s\n",
		      (read_c0_config3() & MIPS_CONF3_ISA_OE) ?  "yes" : "no");
	}

	seq_puts(m, "Options implemented\t:");
	if (cpu_has_tlb)
		seq_puts(m, " tlb");
	if (cpu_has_ftlb)
		seq_puts(m, " ftlb");
	if (cpu_has_tlbinv)
		seq_puts(m, " tlbinv");
	if (cpu_has_segments)
		seq_puts(m, " segments");
	if (cpu_has_rixiex)
		seq_puts(m, " rixiex");
	if (cpu_has_ldpte)
		seq_puts(m, " ldpte");
	if (cpu_has_maar)
		seq_puts(m, " maar");
	if (cpu_has_rw_llb)
		seq_puts(m, " rw_llb");
	if (cpu_has_4kex)
		seq_puts(m, " 4kex");
	if (cpu_has_3k_cache)
		seq_puts(m, " 3k_cache");
	if (cpu_has_4k_cache)
		seq_puts(m, " 4k_cache");
	if (cpu_has_octeon_cache)
		seq_puts(m, " octeon_cache");
	if (raw_cpu_has_fpu)
		seq_puts(m, " fpu");
	if (cpu_has_32fpr)
		seq_puts(m, " 32fpr");
	if (cpu_has_cache_cdex_p)
		seq_puts(m, " cache_cdex_p");
	if (cpu_has_cache_cdex_s)
		seq_puts(m, " cache_cdex_s");
	if (cpu_has_prefetch)
		seq_puts(m, " prefetch");
	if (cpu_has_mcheck)
		seq_puts(m, " mcheck");
	if (cpu_has_ejtag)
		seq_puts(m, " ejtag");
	if (cpu_has_llsc)
		seq_puts(m, " llsc");
	if (cpu_has_guestctl0ext)
		seq_puts(m, " guestctl0ext");
	if (cpu_has_guestctl1)
		seq_puts(m, " guestctl1");
	if (cpu_has_guestctl2)
		seq_puts(m, " guestctl2");
	if (cpu_has_guestid)
		seq_puts(m, " guestid");
	if (cpu_has_drg)
		seq_puts(m, " drg");
	if (cpu_has_rixi)
		seq_puts(m, " rixi");
	if (cpu_has_lpa)
		seq_puts(m, " lpa");
	if (cpu_has_mvh)
		seq_puts(m, " mvh");
	if (cpu_has_vtag_icache)
		seq_puts(m, " vtag_icache");
	if (cpu_has_dc_aliases)
		seq_puts(m, " dc_aliases");
	if (cpu_has_ic_fills_f_dc)
		seq_puts(m, " ic_fills_f_dc");
	if (cpu_has_pindexed_dcache)
		seq_puts(m, " pindexed_dcache");
	if (cpu_has_userlocal)
		seq_puts(m, " userlocal");
	if (cpu_has_nofpuex)
		seq_puts(m, " nofpuex");
	if (cpu_has_vint)
		seq_puts(m, " vint");
	if (cpu_has_veic)
		seq_puts(m, " veic");
	if (cpu_has_inclusive_pcaches)
		seq_puts(m, " inclusive_pcaches");
	if (cpu_has_perf_cntr_intr_bit)
		seq_puts(m, " perf_cntr_intr_bit");
	if (cpu_has_ufr)
		seq_puts(m, " ufr");
	if (cpu_has_fre)
		seq_puts(m, " fre");
	if (cpu_has_cdmm)
		seq_puts(m, " cdmm");
	if (cpu_has_small_pages)
		seq_puts(m, " small_pages");
	if (cpu_has_nan_legacy)
		seq_puts(m, " nan_legacy");
	if (cpu_has_nan_2008)
		seq_puts(m, " nan_2008");
	if (cpu_has_ebase_wg)
		seq_puts(m, " ebase_wg");
	if (cpu_has_badinstr)
		seq_puts(m, " badinstr");
	if (cpu_has_badinstrp)
		seq_puts(m, " badinstrp");
	if (cpu_has_contextconfig)
		seq_puts(m, " contextconfig");
	if (cpu_has_perf)
		seq_puts(m, " perf");
	if (cpu_has_mac2008_only)
		seq_puts(m, " mac2008_only");
	if (cpu_has_ftlbparex)
		seq_puts(m, " ftlbparex");
	if (cpu_has_gsexcex)
		seq_puts(m, " gsexcex");
	if (cpu_has_shared_ftlb_ram)
		seq_puts(m, " shared_ftlb_ram");
	if (cpu_has_shared_ftlb_entries)
		seq_puts(m, " shared_ftlb_entries");
	if (cpu_has_mipsmt_pertccounters)
		seq_puts(m, " mipsmt_pertccounters");
	if (cpu_has_mmid)
		seq_puts(m, " mmid");
	if (cpu_has_mm_sysad)
		seq_puts(m, " mm_sysad");
	if (cpu_has_mm_full)
		seq_puts(m, " mm_full");
	seq_puts(m, "\n");

	seq_printf(m, "shadow register sets\t: %d\n",
		      cpu_data[n].srsets);
	seq_printf(m, "kscratch registers\t: %d\n",
		      hweight8(cpu_data[n].kscratch_mask));
	seq_printf(m, "package\t\t\t: %d\n", cpu_data[n].package);
	seq_printf(m, "core\t\t\t: %d\n", cpu_core(&cpu_data[n]));

#if defined(CONFIG_MIPS_MT_SMP) || defined(CONFIG_CPU_MIPSR6)
	if (cpu_has_mipsmt)
		seq_printf(m, "VPE\t\t\t: %d\n", cpu_vpe_id(&cpu_data[n]));
	else if (cpu_has_vp)
		seq_printf(m, "VP\t\t\t: %d\n", cpu_vpe_id(&cpu_data[n]));
#endif

	sprintf(fmt, "VCE%%c exceptions\t\t: %s\n",
		      cpu_has_vce ? "%u" : "not available");
	seq_printf(m, fmt, 'D', vced_count);
	seq_printf(m, fmt, 'I', vcei_count);

	proc_cpuinfo_notifier_args.m = m;
	proc_cpuinfo_notifier_args.n = n;

	raw_notifier_call_chain(&proc_cpuinfo_chain, 0,
				&proc_cpuinfo_notifier_args);

	seq_puts(m, "\n");

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
