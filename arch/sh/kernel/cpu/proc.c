#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/machvec.h>
#include <asm/processor.h>

static const char *cpu_name[] = {
	[CPU_SH7201]	= "SH7201",
	[CPU_SH7203]	= "SH7203",	[CPU_SH7263]	= "SH7263",
	[CPU_SH7206]	= "SH7206",	[CPU_SH7619]	= "SH7619",
	[CPU_SH7705]	= "SH7705",	[CPU_SH7706]	= "SH7706",
	[CPU_SH7707]	= "SH7707",	[CPU_SH7708]	= "SH7708",
	[CPU_SH7709]	= "SH7709",	[CPU_SH7710]	= "SH7710",
	[CPU_SH7712]	= "SH7712",	[CPU_SH7720]	= "SH7720",
	[CPU_SH7721]	= "SH7721",	[CPU_SH7729]	= "SH7729",
	[CPU_SH7750]	= "SH7750",	[CPU_SH7750S]	= "SH7750S",
	[CPU_SH7750R]	= "SH7750R",	[CPU_SH7751]	= "SH7751",
	[CPU_SH7751R]	= "SH7751R",	[CPU_SH7760]	= "SH7760",
	[CPU_SH4_202]	= "SH4-202",	[CPU_SH4_501]	= "SH4-501",
	[CPU_SH7763]	= "SH7763",	[CPU_SH7770]	= "SH7770",
	[CPU_SH7780]	= "SH7780",	[CPU_SH7781]	= "SH7781",
	[CPU_SH7343]	= "SH7343",	[CPU_SH7785]	= "SH7785",
	[CPU_SH7786]	= "SH7786",	[CPU_SH7757]	= "SH7757",
	[CPU_SH7722]	= "SH7722",	[CPU_SHX3]	= "SH-X3",
	[CPU_SH5_101]	= "SH5-101",	[CPU_SH5_103]	= "SH5-103",
	[CPU_MXG]	= "MX-G",	[CPU_SH7723]	= "SH7723",
	[CPU_SH7366]	= "SH7366",	[CPU_SH7724]	= "SH7724",
	[CPU_SH_NONE]	= "Unknown"
};

const char *get_cpu_subtype(struct sh_cpuinfo *c)
{
	return cpu_name[c->type];
}
EXPORT_SYMBOL(get_cpu_subtype);

#ifdef CONFIG_PROC_FS
/* Symbolic CPU flags, keep in sync with asm/cpu-features.h */
static const char *cpu_flags[] = {
	"none", "fpu", "p2flush", "mmuassoc", "dsp", "perfctr",
	"ptea", "llsc", "l2", "op32", "pteaex", NULL
};

static void show_cpuflags(struct seq_file *m, struct sh_cpuinfo *c)
{
	unsigned long i;

	seq_printf(m, "cpu flags\t:");

	if (!c->flags) {
		seq_printf(m, " %s\n", cpu_flags[0]);
		return;
	}

	for (i = 0; cpu_flags[i]; i++)
		if ((c->flags & (1 << i)))
			seq_printf(m, " %s", cpu_flags[i+1]);

	seq_printf(m, "\n");
}

static void show_cacheinfo(struct seq_file *m, const char *type,
			   struct cache_info info)
{
	unsigned int cache_size;

	cache_size = info.ways * info.sets * info.linesz;

	seq_printf(m, "%s size\t: %2dKiB (%d-way)\n",
		   type, cache_size >> 10, info.ways);
}

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct sh_cpuinfo *c = v;
	unsigned int cpu = c - cpu_data;

	if (!cpu_online(cpu))
		return 0;

	if (cpu == 0)
		seq_printf(m, "machine\t\t: %s\n", get_system_type());
	else
		seq_printf(m, "\n");

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "cpu family\t: %s\n", init_utsname()->machine);
	seq_printf(m, "cpu type\t: %s\n", get_cpu_subtype(c));
	if (c->cut_major == -1)
		seq_printf(m, "cut\t\t: unknown\n");
	else if (c->cut_minor == -1)
		seq_printf(m, "cut\t\t: %d.x\n", c->cut_major);
	else
		seq_printf(m, "cut\t\t: %d.%d\n", c->cut_major, c->cut_minor);

	show_cpuflags(m, c);

	seq_printf(m, "cache type\t: ");

	/*
	 * Check for what type of cache we have, we support both the
	 * unified cache on the SH-2 and SH-3, as well as the harvard
	 * style cache on the SH-4.
	 */
	if (c->icache.flags & SH_CACHE_COMBINED) {
		seq_printf(m, "unified\n");
		show_cacheinfo(m, "cache", c->icache);
	} else {
		seq_printf(m, "split (harvard)\n");
		show_cacheinfo(m, "icache", c->icache);
		show_cacheinfo(m, "dcache", c->dcache);
	}

	/* Optional secondary cache */
	if (c->flags & CPU_HAS_L2_CACHE)
		show_cacheinfo(m, "scache", c->scache);

	seq_printf(m, "address sizes\t: %u bits physical\n", c->phys_bits);

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
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
const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
#endif /* CONFIG_PROC_FS */
