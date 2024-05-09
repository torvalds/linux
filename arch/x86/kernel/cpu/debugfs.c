// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>

#include <asm/apic.h>
#include <asm/processor.h>

static int cpu_debug_show(struct seq_file *m, void *p)
{
	unsigned long cpu = (unsigned long)m->private;
	struct cpuinfo_x86 *c = per_cpu_ptr(&cpu_info, cpu);

	seq_printf(m, "online:              %d\n", cpu_online(cpu));
	if (!c->initialized)
		return 0;

	seq_printf(m, "initial_apicid:      %x\n", c->topo.initial_apicid);
	seq_printf(m, "apicid:              %x\n", c->topo.apicid);
	seq_printf(m, "pkg_id:              %u\n", c->topo.pkg_id);
	seq_printf(m, "die_id:              %u\n", c->topo.die_id);
	seq_printf(m, "cu_id:               %u\n", c->topo.cu_id);
	seq_printf(m, "core_id:             %u\n", c->topo.core_id);
	seq_printf(m, "logical_pkg_id:      %u\n", c->topo.logical_pkg_id);
	seq_printf(m, "logical_die_id:      %u\n", c->topo.logical_die_id);
	seq_printf(m, "llc_id:              %u\n", c->topo.llc_id);
	seq_printf(m, "l2c_id:              %u\n", c->topo.l2c_id);
	seq_printf(m, "max_cores:           %u\n", c->x86_max_cores);
	seq_printf(m, "max_die_per_pkg:     %u\n", __max_die_per_package);
	seq_printf(m, "smp_num_siblings:    %u\n", smp_num_siblings);
	return 0;
}

static int cpu_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, cpu_debug_show, inode->i_private);
}

static const struct file_operations dfs_cpu_ops = {
	.open		= cpu_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static __init int cpu_init_debugfs(void)
{
	struct dentry *dir, *base = debugfs_create_dir("topo", arch_debugfs_dir);
	unsigned long id;
	char name[24];

	dir = debugfs_create_dir("cpus", base);
	for_each_possible_cpu(id) {
		sprintf(name, "%lu", id);
		debugfs_create_file(name, 0444, dir, (void *)id, &dfs_cpu_ops);
	}
	return 0;
}
late_initcall(cpu_init_debugfs);
