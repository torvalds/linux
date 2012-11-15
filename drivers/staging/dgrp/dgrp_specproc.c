/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     James Puzzo  <jamesp at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 *
 *  Filename:
 *
 *     dgrp_specproc.c
 *
 *  Description:
 *
 *     Handle the "config" proc entry for the linux realport device driver
 *     and provide slots for the "net" and "mon" devices
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "dgrp_common.h"

static struct dgrp_proc_entry dgrp_table[];
static struct proc_dir_entry *dgrp_proc_dir_entry;

static int dgrp_add_id(long id);
static int dgrp_remove_nd(struct nd_struct *nd);
static void unregister_dgrp_device(struct proc_dir_entry *de);
static void register_dgrp_device(struct nd_struct *node,
				 struct proc_dir_entry *root,
				 void (*register_hook)(struct proc_dir_entry *de));

/* File operation declarations */
static int dgrp_gen_proc_open(struct inode *, struct file *);
static int dgrp_gen_proc_close(struct inode *, struct file *);
static int parse_write_config(char *);


static const struct file_operations dgrp_proc_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dgrp_gen_proc_open,
	.release = dgrp_gen_proc_close,
};

static struct inode_operations proc_inode_ops = {
	.permission = dgrp_inode_permission
};


static void register_proc_table(struct dgrp_proc_entry *,
				struct proc_dir_entry *);
static void unregister_proc_table(struct dgrp_proc_entry *,
				  struct proc_dir_entry *);

static struct dgrp_proc_entry dgrp_net_table[];
static struct dgrp_proc_entry dgrp_mon_table[];
static struct dgrp_proc_entry dgrp_ports_table[];
static struct dgrp_proc_entry dgrp_dpa_table[];

static ssize_t config_proc_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *pos);

static int nodeinfo_proc_open(struct inode *inode, struct file *file);
static int info_proc_open(struct inode *inode, struct file *file);
static int config_proc_open(struct inode *inode, struct file *file);

static struct file_operations config_proc_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = config_proc_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
	.write   = config_proc_write
};

static struct file_operations info_proc_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = info_proc_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

static struct file_operations nodeinfo_proc_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nodeinfo_proc_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

static struct dgrp_proc_entry dgrp_table[] = {
	{
		.id = DGRP_CONFIG,
		.name = "config",
		.mode = 0644,
		.proc_file_ops = &config_proc_file_ops,
	},
	{
		.id = DGRP_INFO,
		.name = "info",
		.mode = 0644,
		.proc_file_ops = &info_proc_file_ops,
	},
	{
		.id = DGRP_NODEINFO,
		.name = "nodeinfo",
		.mode = 0644,
		.proc_file_ops = &nodeinfo_proc_file_ops,
	},
	{
		.id = DGRP_NETDIR,
		.name = "net",
		.mode = 0500,
		.child = dgrp_net_table
	},
	{
		.id = DGRP_MONDIR,
		.name = "mon",
		.mode = 0500,
		.child = dgrp_mon_table
	},
	{
		.id = DGRP_PORTSDIR,
		.name = "ports",
		.mode = 0500,
		.child = dgrp_ports_table
	},
	{
		.id = DGRP_DPADIR,
		.name = "dpa",
		.mode = 0500,
		.child = dgrp_dpa_table
	}
};

static struct proc_dir_entry *net_entry_pointer;
static struct proc_dir_entry *mon_entry_pointer;
static struct proc_dir_entry *dpa_entry_pointer;
static struct proc_dir_entry *ports_entry_pointer;

static struct dgrp_proc_entry dgrp_net_table[] = {
	{0}
};

static struct dgrp_proc_entry dgrp_mon_table[] = {
	{0}
};

static struct dgrp_proc_entry dgrp_ports_table[] = {
	{0}
};

static struct dgrp_proc_entry dgrp_dpa_table[] = {
	{0}
};

void dgrp_unregister_proc(void)
{
	unregister_proc_table(dgrp_table, dgrp_proc_dir_entry);
	net_entry_pointer = NULL;
	mon_entry_pointer = NULL;
	dpa_entry_pointer = NULL;
	ports_entry_pointer = NULL;

	if (dgrp_proc_dir_entry) {
		remove_proc_entry(dgrp_proc_dir_entry->name,
				  dgrp_proc_dir_entry->parent);
		dgrp_proc_dir_entry = NULL;
	}

}

void dgrp_register_proc(void)
{
	/*
	 *	Register /proc/dgrp
	 */
	dgrp_proc_dir_entry = proc_create("dgrp", S_IFDIR, NULL,
					  &dgrp_proc_file_ops);
	register_proc_table(dgrp_table, dgrp_proc_dir_entry);
}

/*
 * /proc/sys support
 */
static int dgrp_proc_match(int len, const char *name, struct proc_dir_entry *de)
{
	if (!de || !de->low_ino)
		return 0;
	if (de->namelen != len)
		return 0;
	return !memcmp(name, de->name, len);
}


/*
 *  Scan the entries in table and add them all to /proc at the position
 *  referred to by "root"
 */
static void register_proc_table(struct dgrp_proc_entry *table,
				struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int len;
	mode_t mode;

	if (table == NULL)
		return;

	for (; table->id; table++) {
		/* Can't do anything without a proc name. */
		if (!table->name)
			continue;

		/* Maybe we can't do anything with it... */
		if (!table->proc_file_ops &&
		    !table->child) {
			pr_warn("dgrp: Can't register %s\n",
				table->name);
			continue;
		}

		len = strlen(table->name);
		mode = table->mode;

		de = NULL;
		if (!table->child)
			mode |= S_IFREG;
		else {
			mode |= S_IFDIR;
			for (de = root->subdir; de; de = de->next) {
				if (dgrp_proc_match(len, table->name, de))
					break;
			}
			/* If the subdir exists already, de is non-NULL */
		}

		if (!de) {
			de = create_proc_entry(table->name, mode, root);
			if (!de)
				continue;
			de->data = (void *) table;
			if (!table->child) {
				de->proc_iops = &proc_inode_ops;
				if (table->proc_file_ops)
					de->proc_fops = table->proc_file_ops;
				else
					de->proc_fops = &dgrp_proc_file_ops;
			}
		}
		table->de = de;
		if (de->mode & S_IFDIR)
			register_proc_table(table->child, de);

		if (table->id == DGRP_NETDIR)
			net_entry_pointer = de;

		if (table->id == DGRP_MONDIR)
			mon_entry_pointer = de;

		if (table->id == DGRP_DPADIR)
			dpa_entry_pointer = de;

		if (table->id == DGRP_PORTSDIR)
			ports_entry_pointer = de;
	}
}

/*
 * Unregister a /proc sysctl table and any subdirectories.
 */
static void unregister_proc_table(struct dgrp_proc_entry *table,
				  struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	struct nd_struct *tmp;

	if (table == NULL)
		return;

	list_for_each_entry(tmp, &nd_struct_list, list) {
		if ((table == dgrp_net_table) && (tmp->nd_net_de)) {
			unregister_dgrp_device(tmp->nd_net_de);
			dgrp_remove_node_class_sysfs_files(tmp);
		}

		if ((table == dgrp_mon_table) && (tmp->nd_mon_de))
			unregister_dgrp_device(tmp->nd_mon_de);

		if ((table == dgrp_dpa_table) && (tmp->nd_dpa_de))
			unregister_dgrp_device(tmp->nd_dpa_de);

		if ((table == dgrp_ports_table) && (tmp->nd_ports_de))
			unregister_dgrp_device(tmp->nd_ports_de);
	}

	for (; table->id; table++) {
		de = table->de;

		if (!de)
			continue;
		if (de->mode & S_IFDIR) {
			if (!table->child) {
				pr_alert("dgrp: malformed sysctl tree on free\n");
				continue;
			}
			unregister_proc_table(table->child, de);

	/* Don't unregister directories which still have entries */
			if (de->subdir)
				continue;
		}

		/* Don't unregister proc entries that are still being used.. */
		if ((atomic_read(&de->count)) != 1) {
			pr_alert("proc entry %s in use, not removing\n",
				de->name);
			continue;
		}

		remove_proc_entry(de->name, de->parent);
		table->de = NULL;
	}
}

static int dgrp_gen_proc_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de;
	struct dgrp_proc_entry *entry;
	int ret = 0;

	de = (struct proc_dir_entry *) PDE(file->f_dentry->d_inode);
	if (!de || !de->data) {
		ret = -ENXIO;
		goto done;
	}

	entry = (struct dgrp_proc_entry *) de->data;
	if (!entry) {
		ret = -ENXIO;
		goto done;
	}

	down(&entry->excl_sem);

	if (entry->excl_cnt)
		ret = -EBUSY;
	else
		entry->excl_cnt++;

	up(&entry->excl_sem);

done:
	return ret;
}

static int dgrp_gen_proc_close(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de;
	struct dgrp_proc_entry *entry;

	de = (struct proc_dir_entry *) PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		goto done;

	entry = (struct dgrp_proc_entry *) de->data;
	if (!entry)
		goto done;

	down(&entry->excl_sem);

	if (entry->excl_cnt)
		entry->excl_cnt = 0;

	up(&entry->excl_sem);

done:
	return 0;
}

static void *config_proc_start(struct seq_file *m, loff_t *pos)
{
	return seq_list_start_head(&nd_struct_list, *pos);
}

static void *config_proc_next(struct seq_file *p, void *v, loff_t *pos)
{
	return seq_list_next(v, &nd_struct_list, pos);
}

static void config_proc_stop(struct seq_file *m, void *v)
{
}

static int config_proc_show(struct seq_file *m, void *v)
{
	struct nd_struct *nd;
	char tmp_id[4];

	if (v == &nd_struct_list) {
		seq_puts(m, "#-----------------------------------------------------------------------------\n");
		seq_puts(m, "#                        Avail\n");
		seq_puts(m, "# ID  Major  State       Ports\n");
		return 0;
	}

	nd = list_entry(v, struct nd_struct, list);

	ID_TO_CHAR(nd->nd_ID, tmp_id);

	seq_printf(m, "  %-2.2s  %-5ld  %-10.10s  %-5d\n",
		   tmp_id,
		   nd->nd_major,
		   ND_STATE_STR(nd->nd_state),
		   nd->nd_chan_count);

	return 0;
}

static const struct seq_operations proc_config_ops = {
	.start = config_proc_start,
	.next  = config_proc_next,
	.stop  = config_proc_stop,
	.show  = config_proc_show
};

static int config_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_config_ops);
}


/*
 *  When writing configuration information, each "record" (i.e. each
 *  write) is treated as an independent request.  See the "parse"
 *  description for more details.
 */
static ssize_t config_proc_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *pos)
{
	ssize_t retval;
	char *inbuf, *sp;
	char *line, *ldelim;

	if (count > 32768)
		return -EINVAL;

	inbuf = sp = vzalloc(count + 1);
	if (!inbuf)
		return -ENOMEM;

	if (copy_from_user(inbuf, buffer, count)) {
		retval = -EFAULT;
		goto done;
	}

	inbuf[count] = 0;

	ldelim = "\n";

	line = strpbrk(sp, ldelim);
	while (line) {
		*line = 0;
		retval = parse_write_config(sp);
		if (retval)
			goto done;

		sp = line + 1;
		line = strpbrk(sp, ldelim);
	}

	retval = count;
done:
	vfree(inbuf);
	return retval;
}

/*
 *  ------------------------------------------------------------------------
 *
 *  The following are the functions to parse input
 *
 *  ------------------------------------------------------------------------
 */
static inline char *skip_past_ws(const char *str)
{
	while ((*str) && !isspace(*str))
		++str;

	return skip_spaces(str);
}

static int parse_id(char **c, char *cID)
{
	int tmp = **c;

	if (isalnum(tmp) || (tmp == '_'))
		cID[0] = tmp;
	else
		return -EINVAL;

	(*c)++; tmp = **c;

	if (isalnum(tmp) || (tmp == '_')) {
		cID[1] = tmp;
		(*c)++;
	} else
		cID[1] = 0;

	return 0;
}

static int parse_add_config(char *buf)
{
	char *c = buf;
	int  retval;
	char cID[2];
	long ID;

	c = skip_past_ws(c);

	retval = parse_id(&c, cID);
	if (retval < 0)
		return retval;

	ID = CHAR_TO_ID(cID);

	c = skip_past_ws(c);

	return dgrp_add_id(ID);
}

static int parse_del_config(char *buf)
{
	char *c = buf;
	int  retval;
	struct nd_struct *nd;
	char cID[2];
	long ID;
	long major;

	c = skip_past_ws(c);

	retval = parse_id(&c, cID);
	if (retval < 0)
		return retval;

	ID = CHAR_TO_ID(cID);

	c = skip_past_ws(c);

	retval = kstrtol(c, 10, &major);
	if (retval)
		return retval;

	nd = nd_struct_get(major);
	if (!nd)
		return -EINVAL;

	if ((nd->nd_major != major) || (nd->nd_ID != ID))
		return -EINVAL;

	return dgrp_remove_nd(nd);
}

static int parse_chg_config(char *buf)
{
	return -EINVAL;
}

/*
 *  The passed character buffer represents a single configuration request.
 *  If the first character is a "+", it is parsed as a request to add a
 *     PortServer
 *  If the first character is a "-", it is parsed as a request to delete a
 *     PortServer
 *  If the first character is a "*", it is parsed as a request to change a
 *     PortServer
 *  Any other character (including whitespace) causes the record to be
 *     ignored.
 */
static int parse_write_config(char *buf)
{
	int retval;

	switch (buf[0]) {
	case '+':
		retval = parse_add_config(buf);
		break;
	case '-':
		retval = parse_del_config(buf);
		break;
	case '*':
		retval = parse_chg_config(buf);
		break;
	default:
		retval = -EINVAL;
	}

	return retval;
}

static int info_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "version: %s\n", DIGI_VERSION);
	seq_puts(m, "register_with_sysfs: 1\n");
	seq_printf(m, "pollrate: 0x%08x\t(%d)\n",
		   dgrp_poll_tick, dgrp_poll_tick);

	return 0;
}

static int info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_proc_show, NULL);
}


static void *nodeinfo_start(struct seq_file *m, loff_t *pos)
{
	return seq_list_start_head(&nd_struct_list, *pos);
}

static void *nodeinfo_next(struct seq_file *p, void *v, loff_t *pos)
{
	return seq_list_next(v, &nd_struct_list, pos);
}

static void nodeinfo_stop(struct seq_file *m, void *v)
{
}

static int nodeinfo_show(struct seq_file *m, void *v)
{
	struct nd_struct *nd;
	char hwver[8];
	char swver[8];
	char tmp_id[4];

	if (v == &nd_struct_list) {
		seq_puts(m, "#-----------------------------------------------------------------------------\n");
		seq_puts(m, "#                 HW       HW   SW\n");
		seq_puts(m, "# ID  State       Version  ID   Version  Description\n");
		return 0;
	}

	nd = list_entry(v, struct nd_struct, list);

	ID_TO_CHAR(nd->nd_ID, tmp_id);

	if (nd->nd_state == NS_READY) {
		sprintf(hwver, "%d.%d", (nd->nd_hw_ver >> 8) & 0xff,
			nd->nd_hw_ver & 0xff);
		sprintf(swver, "%d.%d", (nd->nd_sw_ver >> 8) & 0xff,
			nd->nd_sw_ver & 0xff);
		seq_printf(m, "  %-2.2s  %-10.10s  %-7.7s  %-3d  %-7.7s  %-35.35s\n",
			   tmp_id,
			   ND_STATE_STR(nd->nd_state),
			   hwver,
			   nd->nd_hw_id,
			   swver,
			   nd->nd_ps_desc);

	} else {
		seq_printf(m, "  %-2.2s  %-10.10s\n",
			   tmp_id,
			   ND_STATE_STR(nd->nd_state));
	}

	return 0;
}


static const struct seq_operations nodeinfo_ops = {
	.start = nodeinfo_start,
	.next  = nodeinfo_next,
	.stop  = nodeinfo_stop,
	.show  = nodeinfo_show
};

static int nodeinfo_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nodeinfo_ops);
}

/**
 * dgrp_add_id() -- creates new nd struct and adds it to list
 * @id: id of device to add
 */
static int dgrp_add_id(long id)
{
	struct nd_struct *nd;
	int ret;
	int i;

	nd = kzalloc(sizeof(struct nd_struct), GFP_KERNEL);
	if (!nd)
		return -ENOMEM;

	nd->nd_major = 0;
	nd->nd_ID = id;

	spin_lock_init(&nd->nd_lock);

	init_waitqueue_head(&nd->nd_tx_waitq);
	init_waitqueue_head(&nd->nd_mon_wqueue);
	init_waitqueue_head(&nd->nd_dpa_wqueue);
	for (i = 0; i < SEQ_MAX; i++)
		init_waitqueue_head(&nd->nd_seq_wque[i]);

	/* setup the structures to get the major number */
	ret = dgrp_tty_init(nd);
	if (ret)
		goto error_out;

	nd->nd_major = nd->nd_serial_ttdriver->major;

	ret = nd_struct_add(nd);
	if (ret)
		goto error_out;

	register_dgrp_device(nd, net_entry_pointer, dgrp_register_net_hook);
	register_dgrp_device(nd, mon_entry_pointer, dgrp_register_mon_hook);
	register_dgrp_device(nd, dpa_entry_pointer, dgrp_register_dpa_hook);
	register_dgrp_device(nd, ports_entry_pointer,
			      dgrp_register_ports_hook);

	return 0;

	/* FIXME this guy should free the tty driver stored in nd and destroy
	 * all channel ports */
error_out:
	kfree(nd);
	return ret;

}

static int dgrp_remove_nd(struct nd_struct *nd)
{
	int ret;

	/* Check to see if the selected structure is in use */
	if (nd->nd_tty_ref_cnt)
		return -EBUSY;

	if (nd->nd_net_de) {
		unregister_dgrp_device(nd->nd_net_de);
		dgrp_remove_node_class_sysfs_files(nd);
	}

	if (nd->nd_mon_de)
		unregister_dgrp_device(nd->nd_mon_de);

	if (nd->nd_ports_de)
		unregister_dgrp_device(nd->nd_ports_de);

	if (nd->nd_dpa_de)
		unregister_dgrp_device(nd->nd_dpa_de);

	dgrp_tty_uninit(nd);

	ret = nd_struct_del(nd);
	if (ret)
		return ret;

	kfree(nd);
	return 0;
}

static void register_dgrp_device(struct nd_struct *node,
				 struct proc_dir_entry *root,
				 void (*register_hook)(struct proc_dir_entry *de))
{
	char buf[3];
	struct proc_dir_entry *de;

	ID_TO_CHAR(node->nd_ID, buf);

	de = create_proc_entry(buf, 0600 | S_IFREG, root);
	if (!de)
		return;

	de->data = (void *) node;

	if (register_hook)
		register_hook(de);

}

static void unregister_dgrp_device(struct proc_dir_entry *de)
{
	if (!de)
		return;

	/* Don't unregister proc entries that are still being used.. */
	if ((atomic_read(&de->count)) != 1) {
		pr_alert("%s - proc entry %s in use. Not removing.\n",
			 __func__, de->name);
		return;
	}

	remove_proc_entry(de->name, de->parent);
	de = NULL;
}
