/* /proc interface for AFS
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "internal.h"

static inline struct afs_net *afs_proc2net(struct file *f)
{
	return &__afs_net;
}

static inline struct afs_net *afs_seq2net(struct seq_file *m)
{
	return &__afs_net; // TODO: use seq_file_net(m)
}

static int afs_proc_cells_open(struct inode *inode, struct file *file);
static void *afs_proc_cells_start(struct seq_file *p, loff_t *pos);
static void *afs_proc_cells_next(struct seq_file *p, void *v, loff_t *pos);
static void afs_proc_cells_stop(struct seq_file *p, void *v);
static int afs_proc_cells_show(struct seq_file *m, void *v);
static ssize_t afs_proc_cells_write(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos);

static const struct seq_operations afs_proc_cells_ops = {
	.start	= afs_proc_cells_start,
	.next	= afs_proc_cells_next,
	.stop	= afs_proc_cells_stop,
	.show	= afs_proc_cells_show,
};

static const struct file_operations afs_proc_cells_fops = {
	.open		= afs_proc_cells_open,
	.read		= seq_read,
	.write		= afs_proc_cells_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static ssize_t afs_proc_rootcell_read(struct file *file, char __user *buf,
				      size_t size, loff_t *_pos);
static ssize_t afs_proc_rootcell_write(struct file *file,
				       const char __user *buf,
				       size_t size, loff_t *_pos);

static const struct file_operations afs_proc_rootcell_fops = {
	.read		= afs_proc_rootcell_read,
	.write		= afs_proc_rootcell_write,
	.llseek		= no_llseek,
};

static int afs_proc_cell_volumes_open(struct inode *inode, struct file *file);
static void *afs_proc_cell_volumes_start(struct seq_file *p, loff_t *pos);
static void *afs_proc_cell_volumes_next(struct seq_file *p, void *v,
					loff_t *pos);
static void afs_proc_cell_volumes_stop(struct seq_file *p, void *v);
static int afs_proc_cell_volumes_show(struct seq_file *m, void *v);

static const struct seq_operations afs_proc_cell_volumes_ops = {
	.start	= afs_proc_cell_volumes_start,
	.next	= afs_proc_cell_volumes_next,
	.stop	= afs_proc_cell_volumes_stop,
	.show	= afs_proc_cell_volumes_show,
};

static const struct file_operations afs_proc_cell_volumes_fops = {
	.open		= afs_proc_cell_volumes_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int afs_proc_cell_vlservers_open(struct inode *inode,
					struct file *file);
static void *afs_proc_cell_vlservers_start(struct seq_file *p, loff_t *pos);
static void *afs_proc_cell_vlservers_next(struct seq_file *p, void *v,
					  loff_t *pos);
static void afs_proc_cell_vlservers_stop(struct seq_file *p, void *v);
static int afs_proc_cell_vlservers_show(struct seq_file *m, void *v);

static const struct seq_operations afs_proc_cell_vlservers_ops = {
	.start	= afs_proc_cell_vlservers_start,
	.next	= afs_proc_cell_vlservers_next,
	.stop	= afs_proc_cell_vlservers_stop,
	.show	= afs_proc_cell_vlservers_show,
};

static const struct file_operations afs_proc_cell_vlservers_fops = {
	.open		= afs_proc_cell_vlservers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int afs_proc_cell_servers_open(struct inode *inode, struct file *file);
static void *afs_proc_cell_servers_start(struct seq_file *p, loff_t *pos);
static void *afs_proc_cell_servers_next(struct seq_file *p, void *v,
					loff_t *pos);
static void afs_proc_cell_servers_stop(struct seq_file *p, void *v);
static int afs_proc_cell_servers_show(struct seq_file *m, void *v);

static const struct seq_operations afs_proc_cell_servers_ops = {
	.start	= afs_proc_cell_servers_start,
	.next	= afs_proc_cell_servers_next,
	.stop	= afs_proc_cell_servers_stop,
	.show	= afs_proc_cell_servers_show,
};

static const struct file_operations afs_proc_cell_servers_fops = {
	.open		= afs_proc_cell_servers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * initialise the /proc/fs/afs/ directory
 */
int afs_proc_init(struct afs_net *net)
{
	_enter("");

	net->proc_afs = proc_mkdir("fs/afs", NULL);
	if (!net->proc_afs)
		goto error_dir;

	if (!proc_create("cells", 0644, net->proc_afs, &afs_proc_cells_fops) ||
	    !proc_create("rootcell", 0644, net->proc_afs, &afs_proc_rootcell_fops))
		goto error_tree;

	_leave(" = 0");
	return 0;

error_tree:
	proc_remove(net->proc_afs);
error_dir:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/afs/ directory
 */
void afs_proc_cleanup(struct afs_net *net)
{
	proc_remove(net->proc_afs);
	net->proc_afs = NULL;
}

/*
 * open "/proc/fs/afs/cells" which provides a summary of extant cells
 */
static int afs_proc_cells_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &afs_proc_cells_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE_DATA(inode);

	return 0;
}

/*
 * set up the iterator to start reading from the cells list and return the
 * first item
 */
static void *afs_proc_cells_start(struct seq_file *m, loff_t *_pos)
{
	struct afs_net *net = afs_seq2net(m);

	down_read(&net->proc_cells_sem);
	return seq_list_start_head(&net->proc_cells, *_pos);
}

/*
 * move to next cell in cells list
 */
static void *afs_proc_cells_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct afs_net *net = afs_seq2net(m);

	return seq_list_next(v, &net->proc_cells, pos);
}

/*
 * clean up after reading from the cells list
 */
static void afs_proc_cells_stop(struct seq_file *m, void *v)
{
	struct afs_net *net = afs_seq2net(m);

	up_read(&net->proc_cells_sem);
}

/*
 * display a header line followed by a load of cell lines
 */
static int afs_proc_cells_show(struct seq_file *m, void *v)
{
	struct afs_cell *cell = list_entry(v, struct afs_cell, proc_link);
	struct afs_net *net = afs_seq2net(m);

	if (v == &net->proc_cells) {
		/* display header on line 1 */
		seq_puts(m, "USE NAME\n");
		return 0;
	}

	/* display one cell per line on subsequent lines */
	seq_printf(m, "%3d %s\n",
		   atomic_read(&cell->usage), cell->name);
	return 0;
}

/*
 * handle writes to /proc/fs/afs/cells
 * - to add cells: echo "add <cellname> <IP>[:<IP>][:<IP>]"
 */
static ssize_t afs_proc_cells_write(struct file *file, const char __user *buf,
				    size_t size, loff_t *_pos)
{
	struct afs_net *net = afs_proc2net(file);
	char *kbuf, *name, *args;
	int ret;

	/* start by dragging the command into memory */
	if (size <= 1 || size >= PAGE_SIZE)
		return -EINVAL;

	kbuf = memdup_user_nul(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* trim to first NL */
	name = memchr(kbuf, '\n', size);
	if (name)
		*name = 0;

	/* split into command, name and argslist */
	name = strchr(kbuf, ' ');
	if (!name)
		goto inval;
	do {
		*name++ = 0;
	} while(*name == ' ');
	if (!*name)
		goto inval;

	args = strchr(name, ' ');
	if (!args)
		goto inval;
	do {
		*args++ = 0;
	} while(*args == ' ');
	if (!*args)
		goto inval;

	/* determine command to perform */
	_debug("cmd=%s name=%s args=%s", kbuf, name, args);

	if (strcmp(kbuf, "add") == 0) {
		struct afs_cell *cell;

		cell = afs_cell_create(net, name, strlen(name), args, false);
		if (IS_ERR(cell)) {
			ret = PTR_ERR(cell);
			goto done;
		}

		afs_put_cell(cell);
		printk("kAFS: Added new cell '%s'\n", name);
	} else {
		goto inval;
	}

	ret = size;

done:
	kfree(kbuf);
	_leave(" = %d", ret);
	return ret;

inval:
	ret = -EINVAL;
	printk("kAFS: Invalid Command on /proc/fs/afs/cells file\n");
	goto done;
}

static ssize_t afs_proc_rootcell_read(struct file *file, char __user *buf,
				      size_t size, loff_t *_pos)
{
	return 0;
}

/*
 * handle writes to /proc/fs/afs/rootcell
 * - to initialize rootcell: echo "cell.name:192.168.231.14"
 */
static ssize_t afs_proc_rootcell_write(struct file *file,
				       const char __user *buf,
				       size_t size, loff_t *_pos)
{
	struct afs_net *net = afs_proc2net(file);
	char *kbuf, *s;
	int ret;

	/* start by dragging the command into memory */
	if (size <= 1 || size >= PAGE_SIZE)
		return -EINVAL;

	kbuf = memdup_user_nul(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* trim to first NL */
	s = memchr(kbuf, '\n', size);
	if (s)
		*s = 0;

	/* determine command to perform */
	_debug("rootcell=%s", kbuf);

	ret = afs_cell_init(net, kbuf);
	if (ret >= 0)
		ret = size;	/* consume everything, always */

	kfree(kbuf);
	_leave(" = %d", ret);
	return ret;
}

/*
 * initialise /proc/fs/afs/<cell>/
 */
int afs_proc_cell_setup(struct afs_net *net, struct afs_cell *cell)
{
	struct proc_dir_entry *dir;

	_enter("%p{%s}", cell, cell->name);

	dir = proc_mkdir(cell->name, net->proc_afs);
	if (!dir)
		goto error_dir;

	if (!proc_create_data("servers", 0, dir,
			     &afs_proc_cell_servers_fops, cell) ||
	    !proc_create_data("vlservers", 0, dir,
			     &afs_proc_cell_vlservers_fops, cell) ||
	    !proc_create_data("volumes", 0, dir,
			     &afs_proc_cell_volumes_fops, cell))
		goto error_tree;

	_leave(" = 0");
	return 0;

error_tree:
	remove_proc_subtree(cell->name, net->proc_afs);
error_dir:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * remove /proc/fs/afs/<cell>/
 */
void afs_proc_cell_remove(struct afs_net *net, struct afs_cell *cell)
{
	_enter("");

	remove_proc_subtree(cell->name, net->proc_afs);

	_leave("");
}

/*
 * open "/proc/fs/afs/<cell>/volumes" which provides a summary of extant cells
 */
static int afs_proc_cell_volumes_open(struct inode *inode, struct file *file)
{
	struct afs_cell *cell;
	struct seq_file *m;
	int ret;

	cell = PDE_DATA(inode);
	if (!cell)
		return -ENOENT;

	ret = seq_open(file, &afs_proc_cell_volumes_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = cell;

	return 0;
}

/*
 * set up the iterator to start reading from the cells list and return the
 * first item
 */
static void *afs_proc_cell_volumes_start(struct seq_file *m, loff_t *_pos)
{
	struct afs_cell *cell = m->private;

	_enter("cell=%p pos=%Ld", cell, *_pos);

	/* lock the list against modification */
	down_read(&cell->vl_sem);
	return seq_list_start_head(&cell->vl_list, *_pos);
}

/*
 * move to next cell in cells list
 */
static void *afs_proc_cell_volumes_next(struct seq_file *p, void *v,
					loff_t *_pos)
{
	struct afs_cell *cell = p->private;

	_enter("cell=%p pos=%Ld", cell, *_pos);
	return seq_list_next(v, &cell->vl_list, _pos);
}

/*
 * clean up after reading from the cells list
 */
static void afs_proc_cell_volumes_stop(struct seq_file *p, void *v)
{
	struct afs_cell *cell = p->private;

	up_read(&cell->vl_sem);
}

static const char afs_vlocation_states[][4] = {
	[AFS_VL_NEW]			= "New",
	[AFS_VL_CREATING]		= "Crt",
	[AFS_VL_VALID]			= "Val",
	[AFS_VL_NO_VOLUME]		= "NoV",
	[AFS_VL_UPDATING]		= "Upd",
	[AFS_VL_VOLUME_DELETED]		= "Del",
	[AFS_VL_UNCERTAIN]		= "Unc",
};

/*
 * display a header line followed by a load of volume lines
 */
static int afs_proc_cell_volumes_show(struct seq_file *m, void *v)
{
	struct afs_cell *cell = m->private;
	struct afs_vlocation *vlocation =
		list_entry(v, struct afs_vlocation, link);

	/* display header on line 1 */
	if (v == &cell->vl_list) {
		seq_puts(m, "USE STT VLID[0]  VLID[1]  VLID[2]  NAME\n");
		return 0;
	}

	/* display one cell per line on subsequent lines */
	seq_printf(m, "%3d %s %08x %08x %08x %s\n",
		   atomic_read(&vlocation->usage),
		   afs_vlocation_states[vlocation->state],
		   vlocation->vldb.vid[0],
		   vlocation->vldb.vid[1],
		   vlocation->vldb.vid[2],
		   vlocation->vldb.name);

	return 0;
}

/*
 * open "/proc/fs/afs/<cell>/vlservers" which provides a list of volume
 * location server
 */
static int afs_proc_cell_vlservers_open(struct inode *inode, struct file *file)
{
	struct afs_cell *cell;
	struct seq_file *m;
	int ret;

	cell = PDE_DATA(inode);
	if (!cell)
		return -ENOENT;

	ret = seq_open(file, &afs_proc_cell_vlservers_ops);
	if (ret<0)
		return ret;

	m = file->private_data;
	m->private = cell;

	return 0;
}

/*
 * set up the iterator to start reading from the cells list and return the
 * first item
 */
static void *afs_proc_cell_vlservers_start(struct seq_file *m, loff_t *_pos)
{
	struct afs_cell *cell = m->private;
	loff_t pos = *_pos;

	_enter("cell=%p pos=%Ld", cell, *_pos);

	/* lock the list against modification */
	down_read(&cell->vl_sem);

	/* allow for the header line */
	if (!pos)
		return (void *) 1;
	pos--;

	if (pos >= cell->vl_naddrs)
		return NULL;

	return &cell->vl_addrs[pos];
}

/*
 * move to next cell in cells list
 */
static void *afs_proc_cell_vlservers_next(struct seq_file *p, void *v,
					  loff_t *_pos)
{
	struct afs_cell *cell = p->private;
	loff_t pos;

	_enter("cell=%p{nad=%u} pos=%Ld", cell, cell->vl_naddrs, *_pos);

	pos = *_pos;
	(*_pos)++;
	if (pos >= cell->vl_naddrs)
		return NULL;

	return &cell->vl_addrs[pos];
}

/*
 * clean up after reading from the cells list
 */
static void afs_proc_cell_vlservers_stop(struct seq_file *p, void *v)
{
	struct afs_cell *cell = p->private;

	up_read(&cell->vl_sem);
}

/*
 * display a header line followed by a load of volume lines
 */
static int afs_proc_cell_vlservers_show(struct seq_file *m, void *v)
{
	struct in_addr *addr = v;

	/* display header on line 1 */
	if (v == (struct in_addr *) 1) {
		seq_puts(m, "ADDRESS\n");
		return 0;
	}

	/* display one cell per line on subsequent lines */
	seq_printf(m, "%pI4\n", &addr->s_addr);
	return 0;
}

/*
 * open "/proc/fs/afs/<cell>/servers" which provides a summary of active
 * servers
 */
static int afs_proc_cell_servers_open(struct inode *inode, struct file *file)
{
	struct afs_cell *cell;
	struct seq_file *m;
	int ret;

	cell = PDE_DATA(inode);
	if (!cell)
		return -ENOENT;

	ret = seq_open(file, &afs_proc_cell_servers_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = cell;
	return 0;
}

/*
 * set up the iterator to start reading from the cells list and return the
 * first item
 */
static void *afs_proc_cell_servers_start(struct seq_file *m, loff_t *_pos)
	__acquires(m->private->servers_lock)
{
	struct afs_cell *cell = m->private;

	_enter("cell=%p pos=%Ld", cell, *_pos);

	/* lock the list against modification */
	read_lock(&cell->servers_lock);
	return seq_list_start_head(&cell->servers, *_pos);
}

/*
 * move to next cell in cells list
 */
static void *afs_proc_cell_servers_next(struct seq_file *p, void *v,
					loff_t *_pos)
{
	struct afs_cell *cell = p->private;

	_enter("cell=%p pos=%Ld", cell, *_pos);
	return seq_list_next(v, &cell->servers, _pos);
}

/*
 * clean up after reading from the cells list
 */
static void afs_proc_cell_servers_stop(struct seq_file *p, void *v)
	__releases(p->private->servers_lock)
{
	struct afs_cell *cell = p->private;

	read_unlock(&cell->servers_lock);
}

/*
 * display a header line followed by a load of volume lines
 */
static int afs_proc_cell_servers_show(struct seq_file *m, void *v)
{
	struct afs_cell *cell = m->private;
	struct afs_server *server = list_entry(v, struct afs_server, link);
	char ipaddr[20];

	/* display header on line 1 */
	if (v == &cell->servers) {
		seq_puts(m, "USE ADDR            STATE\n");
		return 0;
	}

	/* display one cell per line on subsequent lines */
	sprintf(ipaddr, "%pI4", &server->addr);
	seq_printf(m, "%3d %-15.15s %5d\n",
		   atomic_read(&server->usage), ipaddr, server->fs_state);

	return 0;
}
