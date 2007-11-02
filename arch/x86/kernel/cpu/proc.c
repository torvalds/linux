#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/string.h>
#include <asm/semaphore.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	/* 
	 * These flag bits must match the definitions in <asm/cpufeature.h>.
	 * NULL means this bit is undefined or reserved; either way it doesn't
	 * have meaning as far as Linux is concerned.  Note that it's important
	 * to realize there is a difference between this table and CPUID -- if
	 * applications want to get the raw CPUID data, they should access
	 * /dev/cpu/<cpu_nr>/cpuid instead.
	 */
	static const char * const x86_cap_flags[] = {
		/* Intel-defined */
	        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	        "cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	        "pat", "pse36", "pn", "clflush", NULL, "dts", "acpi", "mmx",
	        "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe",

		/* AMD-defined */
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "syscall", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "mp", "nx", NULL, "mmxext", NULL,
		NULL, "fxsr_opt", "pdpe1gb", "rdtscp", NULL, "lm",
		"3dnowext", "3dnow",

		/* Transmeta-defined */
		"recovery", "longrun", NULL, "lrti", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Other (Linux-defined) */
		"cxmmx", "k6_mtrr", "cyrix_arr", "centaur_mcr",
		NULL, NULL, NULL, NULL,
		"constant_tsc", "up", NULL, "arch_perfmon",
		"pebs", "bts", NULL, "sync_rdtsc",
		"rep_good", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Intel-defined (#2) */
		"pni", NULL, NULL, "monitor", "ds_cpl", "vmx", "smx", "est",
		"tm2", "ssse3", "cid", NULL, NULL, "cx16", "xtpr", NULL,
		NULL, NULL, "dca", "sse4_1", "sse4_2", NULL, NULL, "popcnt",
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* VIA/Cyrix/Centaur-defined */
		NULL, NULL, "rng", "rng_en", NULL, NULL, "ace", "ace_en",
		"ace2", "ace2_en", "phe", "phe_en", "pmm", "pmm_en", NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* AMD-defined (#2) */
		"lahf_lm", "cmp_legacy", "svm", "extapic",
		"cr8_legacy", "abm", "sse4a", "misalignsse",
		"3dnowprefetch", "osvw", "ibs", "sse5",
		"skinit", "wdt", NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Auxiliary (Linux-defined) */
		"ida", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	};
	static const char * const x86_power_flags[] = {
		"ts",	/* temperature sensor */
		"fid",  /* frequency id control */
		"vid",  /* voltage id control */
		"ttp",  /* thermal trip */
		"tm",
		"stc",
		"100mhzsteps",
		"hwpstate",
		"",	/* constant_tsc - moved to flags */
		/* nothing */
	};
	struct cpuinfo_x86 *c = v;
	int i, n = 0;
	int fpu_exception;

#ifdef CONFIG_SMP
	if (!cpu_online(n))
		return 0;
	n = c->cpu_index;
#endif
	seq_printf(m, "processor\t: %d\n"
		"vendor_id\t: %s\n"
		"cpu family\t: %d\n"
		"model\t\t: %d\n"
		"model name\t: %s\n",
		n,
		c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
		c->x86,
		c->x86_model,
		c->x86_model_id[0] ? c->x86_model_id : "unknown");

	if (c->x86_mask || c->cpuid_level >= 0)
		seq_printf(m, "stepping\t: %d\n", c->x86_mask);
	else
		seq_printf(m, "stepping\t: unknown\n");

	if ( cpu_has(c, X86_FEATURE_TSC) ) {
		unsigned int freq = cpufreq_quick_get(n);
		if (!freq)
			freq = cpu_khz;
		seq_printf(m, "cpu MHz\t\t: %u.%03u\n",
			freq / 1000, (freq % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0)
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);
#ifdef CONFIG_X86_HT
	if (c->x86_max_cores * smp_num_siblings > 1) {
		seq_printf(m, "physical id\t: %d\n", c->phys_proc_id);
		seq_printf(m, "siblings\t: %d\n",
				cpus_weight(per_cpu(cpu_core_map, n)));
		seq_printf(m, "core id\t\t: %d\n", c->cpu_core_id);
		seq_printf(m, "cpu cores\t: %d\n", c->booted_cores);
	}
#endif
	
	/* We use exception 16 if we have hardware math and we've either seen it or the CPU claims it is internal */
	fpu_exception = c->hard_math && (ignore_fpu_irq || cpu_has_fpu);
	seq_printf(m, "fdiv_bug\t: %s\n"
			"hlt_bug\t\t: %s\n"
			"f00f_bug\t: %s\n"
			"coma_bug\t: %s\n"
			"fpu\t\t: %s\n"
			"fpu_exception\t: %s\n"
			"cpuid level\t: %d\n"
			"wp\t\t: %s\n"
			"flags\t\t:",
		     c->fdiv_bug ? "yes" : "no",
		     c->hlt_works_ok ? "no" : "yes",
		     c->f00f_bug ? "yes" : "no",
		     c->coma_bug ? "yes" : "no",
		     c->hard_math ? "yes" : "no",
		     fpu_exception ? "yes" : "no",
		     c->cpuid_level,
		     c->wp_works_ok ? "yes" : "no");

	for ( i = 0 ; i < 32*NCAPINTS ; i++ )
		if ( test_bit(i, c->x86_capability) &&
		     x86_cap_flags[i] != NULL )
			seq_printf(m, " %s", x86_cap_flags[i]);

	for (i = 0; i < 32; i++)
		if (c->x86_power & (1 << i)) {
			if (i < ARRAY_SIZE(x86_power_flags) &&
			    x86_power_flags[i])
				seq_printf(m, "%s%s",
					   x86_power_flags[i][0]?" ":"",
					   x86_power_flags[i]);
			else
				seq_printf(m, " [%d]", i);
		}

	seq_printf(m, "\nbogomips\t: %lu.%02lu\n",
		     c->loops_per_jiffy/(500000/HZ),
		     (c->loops_per_jiffy/(5000/HZ)) % 100);
	seq_printf(m, "clflush size\t: %u\n\n", c->x86_clflush_size);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)	/* just in case, cpu 0 is not the first */
		*pos = first_cpu(cpu_possible_map);
	if ((*pos) < NR_CPUS && cpu_possible(*pos))
		return &cpu_data(*pos);
	return NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	*pos = next_cpu(*pos, cpu_possible_map);
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
