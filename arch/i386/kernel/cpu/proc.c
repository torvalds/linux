#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/string.h>
#include <asm/semaphore.h>
#include <linux/seq_file.h>

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
	static char *x86_cap_flags[] = {
		/* Intel-defined */
	        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	        "cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	        "pat", "pse36", "pn", "clflush", NULL, "dts", "acpi", "mmx",
	        "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe",

		/* AMD-defined */
		"pni", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "syscall", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "mp", "nx", NULL, "mmxext", NULL,
		NULL, "fxsr_opt", NULL, NULL, NULL, "lm", "3dnowext", "3dnow",

		/* Transmeta-defined */
		"recovery", "longrun", NULL, "lrti", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Other (Linux-defined) */
		"cxmmx", "k6_mtrr", "cyrix_arr", "centaur_mcr",
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Intel-defined (#2) */
		"pni", NULL, NULL, "monitor", "ds_cpl", NULL, NULL, "est",
		"tm2", NULL, "cid", NULL, NULL, "cx16", "xtpr", NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* VIA/Cyrix/Centaur-defined */
		NULL, NULL, "rng", "rng_en", NULL, NULL, "ace", "ace_en",
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* AMD-defined (#2) */
		"lahf_lm", "cmp_legacy", NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	};
	struct cpuinfo_x86 *c = v;
	int i, n = c - cpu_data;
	int fpu_exception;

#ifdef CONFIG_SMP
	if (!cpu_online(n))
		return 0;
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
		seq_printf(m, "cpu MHz\t\t: %lu.%03lu\n",
			cpu_khz / 1000, (cpu_khz % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0)
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);
#ifdef CONFIG_X86_HT
	seq_printf(m, "physical id\t: %d\n", phys_proc_id[n]);
	seq_printf(m, "siblings\t: %d\n", c->x86_num_cores * smp_num_siblings);
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

	seq_printf(m, "\nbogomips\t: %lu.%02lu\n\n",
		     c->loops_per_jiffy/(500000/HZ),
		     (c->loops_per_jiffy/(5000/HZ)) % 100);
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
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
