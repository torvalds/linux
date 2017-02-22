/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2005 Silicon Graphics, Inc. All rights reserved.
 */

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <asm/sn/sn_sal.h>

static int partition_id_show(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", sn_partition_id);
	return 0;
}

static int partition_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, partition_id_show, NULL);
}

static int system_serial_number_show(struct seq_file *s, void *p)
{
	seq_printf(s, "%s\n", sn_system_serial_number());
	return 0;
}

static int system_serial_number_open(struct inode *inode, struct file *file)
{
	return single_open(file, system_serial_number_show, NULL);
}

static int licenseID_show(struct seq_file *s, void *p)
{
	seq_printf(s, "0x%llx\n", sn_partition_serial_number_val());
	return 0;
}

static int licenseID_open(struct inode *inode, struct file *file)
{
	return single_open(file, licenseID_show, NULL);
}

static int coherence_id_show(struct seq_file *s, void *p)
{
	seq_printf(s, "%d\n", partition_coherence_id());

	return 0;
}

static int coherence_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, coherence_id_show, NULL);
}

/* /proc/sgi_sn/sn_topology uses seq_file, see sn_hwperf.c */
extern int sn_topology_open(struct inode *, struct file *);
extern int sn_topology_release(struct inode *, struct file *);

static const struct file_operations proc_partition_id_fops = {
	.open		= partition_id_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_system_sn_fops = {
	.open		= system_serial_number_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_license_id_fops = {
	.open		= licenseID_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_coherence_id_fops = {
	.open		= coherence_id_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_sn_topo_fops = {
	.open		= sn_topology_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= sn_topology_release,
};

void register_sn_procfs(void)
{
	static struct proc_dir_entry *sgi_proc_dir = NULL;

	BUG_ON(sgi_proc_dir != NULL);
	if (!(sgi_proc_dir = proc_mkdir("sgi_sn", NULL)))
		return;

	proc_create("partition_id", 0444, sgi_proc_dir,
		    &proc_partition_id_fops);
	proc_create("system_serial_number", 0444, sgi_proc_dir,
		    &proc_system_sn_fops);
	proc_create("licenseID", 0444, sgi_proc_dir, &proc_license_id_fops);
	proc_create("coherence_id", 0444, sgi_proc_dir,
		    &proc_coherence_id_fops);
	proc_create("sn_topology", 0444, sgi_proc_dir, &proc_sn_topo_fops);
}

#endif /* CONFIG_PROC_FS */
