/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "proc.h"
#include "routing.h"
#include "translation-table.h"
#include "hard-interface.h"
#include "types.h"
#include "hash.h"
#include "vis.h"

static struct proc_dir_entry *proc_batman_dir, *proc_interface_file;
static struct proc_dir_entry *proc_orig_interval_file;

static int proc_interfaces_read(struct seq_file *seq, void *offset)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		seq_printf(seq, "[%8s] %s %s\n",
			   (batman_if->if_active == IF_ACTIVE ?
			    "active" : "inactive"),
			   batman_if->dev,
			   (batman_if->if_active == IF_ACTIVE ?
			    batman_if->addr_str : " "));
	}
	rcu_read_unlock();

	return 0;
}

static int proc_interfaces_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_interfaces_read, NULL);
}

static ssize_t proc_interfaces_write(struct file *instance,
				     const char __user *userbuffer,
				     size_t count, loff_t *data)
{
	char *if_string, *colon_ptr = NULL, *cr_ptr = NULL;
	int not_copied = 0, if_num = 0, add_success;
	struct batman_if *batman_if = NULL;

	if_string = kmalloc(count, GFP_KERNEL);

	if (!if_string)
		return -ENOMEM;

	if (count > IFNAMSIZ - 1) {
		printk(KERN_WARNING "batman-adv:Can't add interface: device name is too long\n");
		goto end;
	}

	not_copied = copy_from_user(if_string, userbuffer, count);
	if_string[count - not_copied - 1] = 0;

	colon_ptr = strchr(if_string, ':');
	if (colon_ptr)
		*colon_ptr = 0;

	if (!colon_ptr) {
		cr_ptr = strchr(if_string, '\n');
		if (cr_ptr)
			*cr_ptr = 0;
	}

	if (strlen(if_string) == 0) {
		shutdown_module();
		num_ifs = 0;
		goto end;
	}

	/* add interface */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (strncmp(batman_if->dev, if_string, count) == 0) {
			printk(KERN_ERR "batman-adv:Given interface is already active: %s\n", if_string);
			rcu_read_unlock();
			goto end;

		}

		if_num++;
	}
	rcu_read_unlock();

	add_success = hardif_add_interface(if_string, if_num);
	if (add_success < 0)
		goto end;

	num_ifs = if_num + 1;

	if ((atomic_read(&module_state) == MODULE_INACTIVE) &&
	    (hardif_get_active_if_num() > 0))
		activate_module();

	return count;
end:
	kfree(if_string);
	return count;
}

static int proc_orig_interval_read(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%i\n", atomic_read(&originator_interval));

	return 0;
}

static ssize_t proc_orig_interval_write(struct file *file,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	char *interval_string;
	int not_copied = 0;
	unsigned long originator_interval_tmp;
	int retval;

	interval_string = kmalloc(count, GFP_KERNEL);

	if (!interval_string)
		return -ENOMEM;

	not_copied = copy_from_user(interval_string, buffer, count);
	interval_string[count - not_copied - 1] = 0;

	retval = strict_strtoul(interval_string, 10, &originator_interval_tmp);
	if (retval) {
		printk(KERN_ERR "batman-adv:New originator interval invalid\n");
		goto end;
	}

	if (originator_interval_tmp <= JITTER * 2) {
		printk(KERN_WARNING "batman-adv:New originator interval too small: %li (min: %i)\n",
		       originator_interval_tmp, JITTER * 2);
		goto end;
	}

	printk(KERN_INFO "batman-adv:Changing originator interval from: %i to: %li\n",
	       atomic_read(&originator_interval), originator_interval_tmp);

	atomic_set(&originator_interval, originator_interval_tmp);

end:
	kfree(interval_string);
	return count;
}

static int proc_orig_interval_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_orig_interval_read, NULL);
}

static const struct file_operations proc_interfaces_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_interfaces_open,
	.read		= seq_read,
	.write		= proc_interfaces_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_orig_interval_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_orig_interval_open,
	.read		= seq_read,
	.write		= proc_orig_interval_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void cleanup_procfs(void)
{
	if (proc_orig_interval_file)
		remove_proc_entry(PROC_FILE_ORIG_INTERVAL, proc_batman_dir);

	if (proc_interface_file)
		remove_proc_entry(PROC_FILE_INTERFACES, proc_batman_dir);

	if (proc_batman_dir)
#ifdef __NET_NET_NAMESPACE_H
		remove_proc_entry(PROC_ROOT_DIR, init_net.proc_net);
#else
		remove_proc_entry(PROC_ROOT_DIR, proc_net);
#endif
}

int setup_procfs(void)
{
#ifdef __NET_NET_NAMESPACE_H
	proc_batman_dir = proc_mkdir(PROC_ROOT_DIR, init_net.proc_net);
#else
	proc_batman_dir = proc_mkdir(PROC_ROOT_DIR, proc_net);
#endif

	if (!proc_batman_dir) {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s' folder failed\n", PROC_ROOT_DIR);
		return -EFAULT;
	}

	proc_interface_file = create_proc_entry(PROC_FILE_INTERFACES,
						S_IWUSR | S_IRUGO,
						proc_batman_dir);
	if (proc_interface_file) {
		proc_interface_file->proc_fops = &proc_interfaces_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_INTERFACES);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_orig_interval_file = create_proc_entry(PROC_FILE_ORIG_INTERVAL,
						    S_IWUSR | S_IRUGO,
						    proc_batman_dir);
	if (proc_orig_interval_file) {
		proc_orig_interval_file->proc_fops = &proc_orig_interval_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_ORIG_INTERVAL);
		cleanup_procfs();
		return -EFAULT;
	}

	return 0;
}
