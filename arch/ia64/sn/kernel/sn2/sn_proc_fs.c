/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2005 Silicon Graphics, Inc. All rights reserved.
 */
#include <linux/config.h>
#include <asm/uaccess.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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
	seq_printf(s, "0x%lx\n", sn_partition_serial_number_val());
	return 0;
}

static int licenseID_open(struct inode *inode, struct file *file)
{
	return single_open(file, licenseID_show, NULL);
}

/*
 * Enable forced interrupt by default.
 * When set, the sn interrupt handler writes the force interrupt register on
 * the bridge chip.  The hardware will then send an interrupt message if the
 * interrupt line is active.  This mimics a level sensitive interrupt.
 */
extern int sn_force_interrupt_flag;

static int sn_force_interrupt_show(struct seq_file *s, void *p)
{
	seq_printf(s, "Force interrupt is %s\n",
		sn_force_interrupt_flag ? "enabled" : "disabled");
	return 0;
}

static ssize_t sn_force_interrupt_write_proc(struct file *file,
		const char __user *buffer, size_t count, loff_t *data)
{
	char val;

	if (copy_from_user(&val, buffer, 1))
		return -EFAULT;

	sn_force_interrupt_flag = (val == '0') ? 0 : 1;
	return count;
}

static int sn_force_interrupt_open(struct inode *inode, struct file *file)
{
	return single_open(file, sn_force_interrupt_show, NULL);
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

static struct proc_dir_entry *sn_procfs_create_entry(
	const char *name, struct proc_dir_entry *parent,
	int (*openfunc)(struct inode *, struct file *),
	int (*releasefunc)(struct inode *, struct file *))
{
	struct proc_dir_entry *e = create_proc_entry(name, 0444, parent);

	if (e) {
		e->proc_fops = (struct file_operations *)kmalloc(
			sizeof(struct file_operations), GFP_KERNEL);
		if (e->proc_fops) {
			memset(e->proc_fops, 0, sizeof(struct file_operations));
			e->proc_fops->open = openfunc;
			e->proc_fops->read = seq_read;
			e->proc_fops->llseek = seq_lseek;
			e->proc_fops->release = releasefunc;
		}
	}

	return e;
}

/* /proc/sgi_sn/sn_topology uses seq_file, see sn_hwperf.c */
extern int sn_topology_open(struct inode *, struct file *);
extern int sn_topology_release(struct inode *, struct file *);

void register_sn_procfs(void)
{
	static struct proc_dir_entry *sgi_proc_dir = NULL;
	struct proc_dir_entry *e;

	BUG_ON(sgi_proc_dir != NULL);
	if (!(sgi_proc_dir = proc_mkdir("sgi_sn", NULL)))
		return;

	sn_procfs_create_entry("partition_id", sgi_proc_dir,
		partition_id_open, single_release);

	sn_procfs_create_entry("system_serial_number", sgi_proc_dir,
		system_serial_number_open, single_release);

	sn_procfs_create_entry("licenseID", sgi_proc_dir, 
		licenseID_open, single_release);

	e = sn_procfs_create_entry("sn_force_interrupt", sgi_proc_dir, 
		sn_force_interrupt_open, single_release);
	if (e) 
		e->proc_fops->write = sn_force_interrupt_write_proc;

	sn_procfs_create_entry("coherence_id", sgi_proc_dir, 
		coherence_id_open, single_release);
	
	sn_procfs_create_entry("sn_topology", sgi_proc_dir,
		sn_topology_open, sn_topology_release);
}

#endif /* CONFIG_PROC_FS */
