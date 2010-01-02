/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
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
#include "compat.h"

static uint8_t vis_format = DOT_DRAW;

static struct proc_dir_entry *proc_batman_dir, *proc_interface_file;
static struct proc_dir_entry *proc_orig_interval_file, *proc_originators_file;
static struct proc_dir_entry *proc_transt_local_file;
static struct proc_dir_entry *proc_transt_global_file;
static struct proc_dir_entry *proc_vis_file, *proc_vis_format_file;
static struct proc_dir_entry *proc_aggr_file;

static int proc_interfaces_read(struct seq_file *seq, void *offset)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		seq_printf(seq, "[%8s] %s %s \n",
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
	int not_copied = 0, if_num = 0;
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

	hardif_add_interface(if_string, if_num);

	if ((atomic_read(&module_state) == MODULE_INACTIVE) &&
	    (hardif_get_active_if_num() > 0))
		activate_module();

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();
		goto end;
	}
	rcu_read_unlock();

	num_ifs = if_num + 1;
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

static int proc_originators_read(struct seq_file *seq, void *offset)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	int batman_count = 0;
	char orig_str[ETH_STR_LEN], router_str[ETH_STR_LEN];

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();
		seq_printf(seq, "BATMAN disabled - please specify interfaces to enable it \n");
		goto end;
	}

	if (((struct batman_if *)if_list.next)->if_active != IF_ACTIVE) {
		rcu_read_unlock();
		seq_printf(seq, "BATMAN disabled - primary interface not active \n");
		goto end;
	}

	seq_printf(seq,
		   "  %-14s (%s/%i) %17s [%10s]: %20s ... [B.A.T.M.A.N. adv %s%s, MainIF/MAC: %s/%s] \n",
		   "Originator", "#", TQ_MAX_VALUE, "Nexthop", "outgoingIF",
		   "Potential nexthops", SOURCE_VERSION, REVISION_VERSION_STR,
		   ((struct batman_if *)if_list.next)->dev,
		   ((struct batman_if *)if_list.next)->addr_str);

	rcu_read_unlock();
	spin_lock(&orig_hash_lock);

	while (hash_iterate(orig_hash, &hashit)) {

		orig_node = hashit.bucket->data;

		if (!orig_node->router)
			continue;

		if (orig_node->router->tq_avg == 0)
			continue;

		batman_count++;

		addr_to_string(orig_str, orig_node->orig);
		addr_to_string(router_str, orig_node->router->addr);

		seq_printf(seq, "%-17s	(%3i) %17s [%10s]:",
			   orig_str, orig_node->router->tq_avg,
			   router_str, orig_node->router->if_incoming->dev);

		list_for_each_entry(neigh_node, &orig_node->neigh_list, list) {
			addr_to_string(orig_str, neigh_node->addr);
			seq_printf(seq, " %17s (%3i)",
				   orig_str, neigh_node->tq_avg);
		}

		seq_printf(seq, "\n");

	}

	spin_unlock(&orig_hash_lock);

	if (batman_count == 0)
		seq_printf(seq, "No batman nodes in range ... \n");

end:
	return 0;
}

static int proc_originators_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_originators_read, NULL);
}

static int proc_transt_local_read(struct seq_file *seq, void *offset)
{
	char *buf;

	buf = kmalloc(4096, GFP_KERNEL);
	if (!buf)
		return 0;

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();
		seq_printf(seq, "BATMAN disabled - please specify interfaces to enable it \n");
		goto end;
	}

	rcu_read_unlock();

	seq_printf(seq, "Locally retrieved addresses (from %s) announced via HNA:\n", soft_device->name);

	hna_local_fill_buffer_text(buf, 4096);
	seq_printf(seq, "%s", buf);

end:
	kfree(buf);
	return 0;
}

static int proc_transt_local_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_transt_local_read, NULL);
}

static int proc_transt_global_read(struct seq_file *seq, void *offset)
{
	char *buf;

	buf = kmalloc(4096, GFP_KERNEL);
	if (!buf)
		return 0;

	rcu_read_lock();
	if (list_empty(&if_list)) {
		rcu_read_unlock();
		seq_printf(seq, "BATMAN disabled - please specify interfaces to enable it \n");
		goto end;
	}
	rcu_read_unlock();


	seq_printf(seq, "Globally announced HNAs received via the mesh (translation table):\n");

	hna_global_fill_buffer_text(buf, 4096);
	seq_printf(seq, "%s", buf);

end:
	kfree(buf);
	return 0;
}

static int proc_transt_global_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_transt_global_read, NULL);
}

/* insert interface to the list of interfaces of one originator */

static void proc_vis_insert_interface(const uint8_t *interface,
				      struct vis_if_list **if_entry,
				      bool primary)
{
	/* Did we get an empty list? (then insert imediately) */
	if (*if_entry == NULL) {
		*if_entry = kmalloc(sizeof(struct vis_if_list), GFP_KERNEL);
		if (*if_entry == NULL)
			return;

		(*if_entry)->primary = primary;
		(*if_entry)->next = NULL;
		memcpy((*if_entry)->addr, interface, ETH_ALEN);
	} else {
		struct vis_if_list *head_if_entry = *if_entry;
		/* Do we already have this interface in our list? */
		while (!compare_orig((*if_entry)->addr, (void *)interface)) {

			/* Or did we reach the end (then append the interface) */
			if ((*if_entry)->next == NULL) {
				(*if_entry)->next = kmalloc(sizeof(struct vis_if_list), GFP_KERNEL);
				if ((*if_entry)->next == NULL)
					return;

				memcpy((*if_entry)->next->addr, interface, ETH_ALEN);
				(*if_entry)->next->primary = primary;
				(*if_entry)->next->next = NULL;
				break;
			}
			*if_entry = (*if_entry)->next;
		}
		/* Rewind the list to its head */
		*if_entry = head_if_entry;
	}
}
/* read an entry  */

static void proc_vis_read_entry(struct seq_file *seq,
				struct vis_info_entry *entry,
				struct vis_if_list **if_entry,
				uint8_t *vis_orig,
				uint8_t current_format,
				uint8_t first_line)
{
	char from[40];
	char to[40];
	int int_part, frac_part;

	addr_to_string(to, entry->dest);
	if (entry->quality == 0) {
#ifndef VIS_SUBCLUSTERS_DISABLED
		proc_vis_insert_interface(vis_orig, if_entry, true);
#endif /* VIS_SUBCLUSTERS_DISABLED */
		addr_to_string(from, vis_orig);
		if (current_format == DOT_DRAW) {
			seq_printf(seq, "\t\"%s\" -> \"%s\" [label=\"HNA\"]\n",
				   from, to);
		} else {
			seq_printf(seq,
				   "%s\t{ router : \"%s\", gateway   : \"%s\", label : \"HNA\" }",
				   (first_line ? "" : ",\n"), from, to);
		}
	} else {
#ifndef VIS_SUBCLUSTERS_DISABLED
		proc_vis_insert_interface(entry->src, if_entry, compare_orig(entry->src, vis_orig));
#endif /* VIS_SUBCLUSTERS_DISABLED */
		addr_to_string(from, entry->src);

		/* kernel has no printf-support for %f? it'd be better to return
		 * this in float. */

		int_part = TQ_MAX_VALUE / entry->quality;
		frac_part = 1000 * TQ_MAX_VALUE / entry->quality - int_part * 1000;

		if (current_format == DOT_DRAW) {
			seq_printf(seq,
				   "\t\"%s\" -> \"%s\" [label=\"%d.%d\"]\n",
				   from, to, int_part, frac_part);
		} else {
			seq_printf(seq,
				   "%s\t{ router : \"%s\", neighbor : \"%s\", label : %d.%d }",
				   (first_line ? "" : ",\n"), from, to, int_part, frac_part);
		}
	}
}


static int proc_vis_read(struct seq_file *seq, void *offset)
{
	HASHIT(hashit);
	struct vis_info *info;
	struct vis_info_entry *entries;
	struct vis_if_list *if_entries = NULL;
	int i;
	uint8_t current_format, first_line = 1;
#ifndef VIS_SUBCLUSTERS_DISABLED
	char tmp_addr_str[ETH_STR_LEN];
	struct vis_if_list *tmp_if_next;
#endif /* VIS_SUBCLUSTERS_DISABLED */

	current_format = vis_format;

	rcu_read_lock();
	if (list_empty(&if_list) || (!is_vis_server())) {
		rcu_read_unlock();
		if (current_format == DOT_DRAW)
			seq_printf(seq, "digraph {\n}\n");
		goto end;
	}

	rcu_read_unlock();

	if (current_format == DOT_DRAW)
		seq_printf(seq, "digraph {\n");

	spin_lock(&vis_hash_lock);
	while (hash_iterate(vis_hash, &hashit)) {
		info = hashit.bucket->data;
		entries = (struct vis_info_entry *)
			((char *)info + sizeof(struct vis_info));

		for (i = 0; i < info->packet.entries; i++) {
			proc_vis_read_entry(seq, &entries[i], &if_entries,
					    info->packet.vis_orig,
					    current_format, first_line);
			if (first_line)
				first_line = 0;
		}

#ifndef VIS_SUBCLUSTERS_DISABLED
		/* Generate subgraphs from the collected items */
		if (current_format == DOT_DRAW) {

			addr_to_string(tmp_addr_str, info->packet.vis_orig);
			seq_printf(seq, "\tsubgraph \"cluster_%s\" {\n", tmp_addr_str);
			while (if_entries != NULL) {

				addr_to_string(tmp_addr_str, if_entries->addr);
				if (if_entries->primary)
					seq_printf(seq, "\t\t\"%s\" [peripheries=2]\n", tmp_addr_str);
				else
					seq_printf(seq, "\t\t\"%s\"\n", tmp_addr_str);

				/* ... and empty the list while doing this */
				tmp_if_next = if_entries->next;
				kfree(if_entries);
				if_entries = tmp_if_next;
			}
			seq_printf(seq, "\t}\n");
		}
#endif /* VIS_SUBCLUSTERS_DISABLED */
	}
	spin_unlock(&vis_hash_lock);

	if (current_format == DOT_DRAW)
		seq_printf(seq, "}\n");
	else
		seq_printf(seq, "\n");
end:
	return 0;
}

/* setting the mode of the vis server by the user */
static ssize_t proc_vis_write(struct file *file, const char __user * buffer,
			      size_t count, loff_t *ppos)
{
	char *vis_mode_string;
	int not_copied = 0;

	vis_mode_string = kmalloc(count, GFP_KERNEL);

	if (!vis_mode_string)
		return -ENOMEM;

	not_copied = copy_from_user(vis_mode_string, buffer, count);
	vis_mode_string[count - not_copied - 1] = 0;

	if (strcmp(vis_mode_string, "client") == 0) {
		printk(KERN_INFO "batman-adv:Setting VIS mode to client\n");
		vis_set_mode(VIS_TYPE_CLIENT_UPDATE);
	} else if (strcmp(vis_mode_string, "server") == 0) {
		printk(KERN_INFO "batman-adv:Setting VIS mode to server\n");
		vis_set_mode(VIS_TYPE_SERVER_SYNC);
	} else
		printk(KERN_ERR "batman-adv:Unknown VIS mode: %s\n",
		       vis_mode_string);

	kfree(vis_mode_string);
	return count;
}

static int proc_vis_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vis_read, NULL);
}

static int proc_vis_format_read(struct seq_file *seq, void *offset)
{
	uint8_t current_format = vis_format;

	seq_printf(seq, "[%c] %s\n",
		   (current_format == DOT_DRAW) ? 'x' : ' ',
		   VIS_FORMAT_DD_NAME);
	seq_printf(seq, "[%c] %s\n",
		   (current_format == JSON) ? 'x' : ' ',
		   VIS_FORMAT_JSON_NAME);
	return 0;
}

static int proc_vis_format_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vis_format_read, NULL);
}

static ssize_t proc_vis_format_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	char *vis_format_string;
	int not_copied = 0;

	vis_format_string = kmalloc(count, GFP_KERNEL);

	if (!vis_format_string)
		return -ENOMEM;

	not_copied = copy_from_user(vis_format_string, buffer, count);
	vis_format_string[count - not_copied - 1] = 0;

	if (strcmp(vis_format_string, VIS_FORMAT_DD_NAME) == 0) {
		printk(KERN_INFO "batman-adv:Setting VIS output format to: %s\n",
		       VIS_FORMAT_DD_NAME);
		vis_format = DOT_DRAW;
	} else if (strcmp(vis_format_string, VIS_FORMAT_JSON_NAME) == 0) {
		printk(KERN_INFO "batman-adv:Setting VIS output format to: %s\n",
		       VIS_FORMAT_JSON_NAME);
		vis_format = JSON;
	} else
		printk(KERN_ERR "batman-adv:Unknown VIS output format: %s\n",
		       vis_format_string);

	kfree(vis_format_string);
	return count;
}

static int proc_aggr_read(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%i\n", atomic_read(&aggregation_enabled));

	return 0;
}

static ssize_t proc_aggr_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *ppos)
{
	char *aggr_string;
	int not_copied = 0;
	unsigned long aggregation_enabled_tmp;

	aggr_string = kmalloc(count, GFP_KERNEL);

	if (!aggr_string)
		return -ENOMEM;

	not_copied = copy_from_user(aggr_string, buffer, count);
	aggr_string[count - not_copied - 1] = 0;

	strict_strtoul(aggr_string, 10, &aggregation_enabled_tmp);

	if ((aggregation_enabled_tmp != 0) && (aggregation_enabled_tmp != 1)) {
		printk(KERN_ERR "batman-adv:Aggregation can only be enabled (1) or disabled (0), given value: %li\n", aggregation_enabled_tmp);
		goto end;
	}

	printk(KERN_INFO "batman-adv:Changing aggregation from: %s (%i) to: %s (%li)\n",
	       (atomic_read(&aggregation_enabled) == 1 ?
		"enabled" : "disabled"),
	       atomic_read(&aggregation_enabled),
	       (aggregation_enabled_tmp == 1 ? "enabled" : "disabled"),
	       aggregation_enabled_tmp);

	atomic_set(&aggregation_enabled, (unsigned)aggregation_enabled_tmp);
end:
	kfree(aggr_string);
	return count;
}

static int proc_aggr_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_aggr_read, NULL);
}

/* satisfying different prototypes ... */
static ssize_t proc_dummy_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations proc_aggr_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_aggr_open,
	.read		= seq_read,
	.write		= proc_aggr_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_vis_format_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_vis_format_open,
	.read		= seq_read,
	.write		= proc_vis_format_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_vis_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_vis_open,
	.read		= seq_read,
	.write		= proc_vis_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_originators_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_originators_open,
	.read		= seq_read,
	.write		= proc_dummy_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_transt_local_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_transt_local_open,
	.read		= seq_read,
	.write		= proc_dummy_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_transt_global_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_transt_global_open,
	.read		= seq_read,
	.write		= proc_dummy_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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
	if (proc_transt_global_file)
		remove_proc_entry(PROC_FILE_TRANST_GLOBAL, proc_batman_dir);

	if (proc_transt_local_file)
		remove_proc_entry(PROC_FILE_TRANST_LOCAL, proc_batman_dir);

	if (proc_originators_file)
		remove_proc_entry(PROC_FILE_ORIGINATORS, proc_batman_dir);

	if (proc_orig_interval_file)
		remove_proc_entry(PROC_FILE_ORIG_INTERVAL, proc_batman_dir);

	if (proc_interface_file)
		remove_proc_entry(PROC_FILE_INTERFACES, proc_batman_dir);

	if (proc_vis_file)
		remove_proc_entry(PROC_FILE_VIS, proc_batman_dir);

	if (proc_vis_format_file)
		remove_proc_entry(PROC_FILE_VIS_FORMAT, proc_batman_dir);

	if (proc_aggr_file)
		remove_proc_entry(PROC_FILE_AGGR, proc_batman_dir);

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

	proc_originators_file = create_proc_entry(PROC_FILE_ORIGINATORS,
						  S_IRUGO, proc_batman_dir);
	if (proc_originators_file) {
		proc_originators_file->proc_fops = &proc_originators_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_ORIGINATORS);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_transt_local_file = create_proc_entry(PROC_FILE_TRANST_LOCAL,
						   S_IRUGO, proc_batman_dir);
	if (proc_transt_local_file) {
		proc_transt_local_file->proc_fops = &proc_transt_local_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_TRANST_LOCAL);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_transt_global_file = create_proc_entry(PROC_FILE_TRANST_GLOBAL,
						    S_IRUGO, proc_batman_dir);
	if (proc_transt_global_file) {
		proc_transt_global_file->proc_fops = &proc_transt_global_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_TRANST_GLOBAL);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_vis_file = create_proc_entry(PROC_FILE_VIS, S_IWUSR | S_IRUGO,
					  proc_batman_dir);
	if (proc_vis_file) {
		proc_vis_file->proc_fops = &proc_vis_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_VIS);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_vis_format_file = create_proc_entry(PROC_FILE_VIS_FORMAT,
						 S_IWUSR | S_IRUGO,
						 proc_batman_dir);
	if (proc_vis_format_file) {
		proc_vis_format_file->proc_fops = &proc_vis_format_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_VIS_FORMAT);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_aggr_file = create_proc_entry(PROC_FILE_AGGR, S_IWUSR | S_IRUGO,
					   proc_batman_dir);
	if (proc_aggr_file) {
		proc_aggr_file->proc_fops = &proc_aggr_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_AGGR);
		cleanup_procfs();
		return -EFAULT;
	}

	return 0;
}


