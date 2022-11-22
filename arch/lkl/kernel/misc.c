#include <linux/seq_file.h>

#ifdef CONFIG_PROC_FS
static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= cpuinfo_start,
	.next	= cpuinfo_next,
	.stop	= cpuinfo_stop,
	.show	= show_cpuinfo,
};
#endif
