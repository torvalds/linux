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

/*
 * Display the list of cells known to the namespace.
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
	seq_printf(m, "%3u %s\n", atomic_read(&cell->usage), cell->name);
	return 0;
}

static void *afs_proc_cells_start(struct seq_file *m, loff_t *_pos)
	__acquires(rcu)
{
	rcu_read_lock();
	return seq_list_start_head(&afs_seq2net(m)->proc_cells, *_pos);
}

static void *afs_proc_cells_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &afs_seq2net(m)->proc_cells, pos);
}

static void afs_proc_cells_stop(struct seq_file *m, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

static const struct seq_operations afs_proc_cells_ops = {
	.start	= afs_proc_cells_start,
	.next	= afs_proc_cells_next,
	.stop	= afs_proc_cells_stop,
	.show	= afs_proc_cells_show,
};

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

		cell = afs_lookup_cell(net, name, strlen(name), args, true);
		if (IS_ERR(cell)) {
			ret = PTR_ERR(cell);
			goto done;
		}

		if (test_and_set_bit(AFS_CELL_FL_NO_GC, &cell->flags))
			afs_put_cell(net, cell);
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

static int afs_proc_cells_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &afs_proc_cells_ops);
}

static const struct file_operations afs_proc_cells_fops = {
	.open		= afs_proc_cells_open,
	.read		= seq_read,
	.write		= afs_proc_cells_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * Read the name of the current workstation cell.
 */
static ssize_t afs_proc_rootcell_read(struct file *file, char __user *buf,
				      size_t size, loff_t *_pos)
{
	struct afs_cell *cell;
	struct afs_net *net = afs_proc2net(file);
	unsigned int seq = 0;
	char name[AFS_MAXCELLNAME + 1];
	int len;

	if (*_pos > 0)
		return 0;
	if (!rcu_access_pointer(net->ws_cell))
		return 0;

	rcu_read_lock();
	do {
		read_seqbegin_or_lock(&net->cells_lock, &seq);
		len = 0;
		cell = rcu_dereference_raw(net->ws_cell);
		if (cell) {
			len = cell->name_len;
			memcpy(name, cell->name, len);
		}
	} while (need_seqretry(&net->cells_lock, seq));
	done_seqretry(&net->cells_lock, seq);
	rcu_read_unlock();

	if (!len)
		return 0;

	name[len++] = '\n';
	if (len > size)
		len = size;
	if (copy_to_user(buf, name, len) != 0)
		return -EFAULT;
	*_pos = 1;
	return len;
}

/*
 * Set the current workstation cell and optionally supply its list of volume
 * location servers.
 *
 *	echo "cell.name:192.168.231.14" >/proc/fs/afs/rootcell
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

	ret = -EINVAL;
	if (kbuf[0] == '.')
		goto out;
	if (memchr(kbuf, '/', size))
		goto out;

	/* trim to first NL */
	s = memchr(kbuf, '\n', size);
	if (s)
		*s = 0;

	/* determine command to perform */
	_debug("rootcell=%s", kbuf);

	ret = afs_cell_init(net, kbuf);
	if (ret >= 0)
		ret = size;	/* consume everything, always */

out:
	kfree(kbuf);
	_leave(" = %d", ret);
	return ret;
}

static const struct file_operations afs_proc_rootcell_fops = {
	.read		= afs_proc_rootcell_read,
	.write		= afs_proc_rootcell_write,
	.llseek		= no_llseek,
};

static const char afs_vol_types[3][3] = {
	[AFSVL_RWVOL]	= "RW",
	[AFSVL_ROVOL]	= "RO",
	[AFSVL_BACKVOL]	= "BK",
};

/*
 * Display the list of volumes known to a cell.
 */
static int afs_proc_cell_volumes_show(struct seq_file *m, void *v)
{
	struct afs_cell *cell = PDE_DATA(file_inode(m->file));
	struct afs_volume *vol = list_entry(v, struct afs_volume, proc_link);

	/* Display header on line 1 */
	if (v == &cell->proc_volumes) {
		seq_puts(m, "USE VID      TY\n");
		return 0;
	}

	seq_printf(m, "%3d %08x %s\n",
		   atomic_read(&vol->usage), vol->vid,
		   afs_vol_types[vol->type]);

	return 0;
}

static void *afs_proc_cell_volumes_start(struct seq_file *m, loff_t *_pos)
	__acquires(cell->proc_lock)
{
	struct afs_cell *cell = PDE_DATA(file_inode(m->file));

	read_lock(&cell->proc_lock);
	return seq_list_start_head(&cell->proc_volumes, *_pos);
}

static void *afs_proc_cell_volumes_next(struct seq_file *p, void *v,
					loff_t *_pos)
{
	struct afs_cell *cell = PDE_DATA(file_inode(p->file));

	return seq_list_next(v, &cell->proc_volumes, _pos);
}

static void afs_proc_cell_volumes_stop(struct seq_file *p, void *v)
	__releases(cell->proc_lock)
{
	struct afs_cell *cell = PDE_DATA(file_inode(p->file));

	read_unlock(&cell->proc_lock);
}

static const struct seq_operations afs_proc_cell_volumes_ops = {
	.start	= afs_proc_cell_volumes_start,
	.next	= afs_proc_cell_volumes_next,
	.stop	= afs_proc_cell_volumes_stop,
	.show	= afs_proc_cell_volumes_show,
};

/*
 * Display the list of Volume Location servers we're using for a cell.
 */
static int afs_proc_cell_vlservers_show(struct seq_file *m, void *v)
{
	struct sockaddr_rxrpc *addr = v;

	/* display header on line 1 */
	if (v == (void *)1) {
		seq_puts(m, "ADDRESS\n");
		return 0;
	}

	/* display one cell per line on subsequent lines */
	seq_printf(m, "%pISp\n", &addr->transport);
	return 0;
}

static void *afs_proc_cell_vlservers_start(struct seq_file *m, loff_t *_pos)
	__acquires(rcu)
{
	struct afs_addr_list *alist;
	struct afs_cell *cell = PDE_DATA(file_inode(m->file));
	loff_t pos = *_pos;

	rcu_read_lock();

	alist = rcu_dereference(cell->vl_addrs);

	/* allow for the header line */
	if (!pos)
		return (void *) 1;
	pos--;

	if (!alist || pos >= alist->nr_addrs)
		return NULL;

	return alist->addrs + pos;
}

static void *afs_proc_cell_vlservers_next(struct seq_file *p, void *v,
					  loff_t *_pos)
{
	struct afs_addr_list *alist;
	struct afs_cell *cell = PDE_DATA(file_inode(p->file));
	loff_t pos;

	alist = rcu_dereference(cell->vl_addrs);

	pos = *_pos;
	(*_pos)++;
	if (!alist || pos >= alist->nr_addrs)
		return NULL;

	return alist->addrs + pos;
}

static void afs_proc_cell_vlservers_stop(struct seq_file *p, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

static const struct seq_operations afs_proc_cell_vlservers_ops = {
	.start	= afs_proc_cell_vlservers_start,
	.next	= afs_proc_cell_vlservers_next,
	.stop	= afs_proc_cell_vlservers_stop,
	.show	= afs_proc_cell_vlservers_show,
};

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

static const struct file_operations afs_proc_cell_vlservers_fops = {
	.open		= afs_proc_cell_vlservers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * Display the list of fileservers we're using within a namespace.
 */
static int afs_proc_servers_show(struct seq_file *m, void *v)
{
	struct afs_server *server;
	struct afs_addr_list *alist;

	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "UUID                                 USE ADDR\n");
		return 0;
	}

	server = list_entry(v, struct afs_server, proc_link);
	alist = rcu_dereference(server->addresses);
	seq_printf(m, "%pU %3d %pISp\n",
		   &server->uuid,
		   atomic_read(&server->usage),
		   &alist->addrs[alist->index].transport);
	return 0;
}

static void *afs_proc_servers_start(struct seq_file *m, loff_t *_pos)
	__acquires(rcu)
{
	rcu_read_lock();
	return seq_hlist_start_head_rcu(&afs_seq2net(m)->fs_proc, *_pos);
}

static void *afs_proc_servers_next(struct seq_file *m, void *v, loff_t *_pos)
{
	return seq_hlist_next_rcu(v, &afs_seq2net(m)->fs_proc, _pos);
}

static void afs_proc_servers_stop(struct seq_file *p, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

static const struct seq_operations afs_proc_servers_ops = {
	.start	= afs_proc_servers_start,
	.next	= afs_proc_servers_next,
	.stop	= afs_proc_servers_stop,
	.show	= afs_proc_servers_show,
};

static int afs_proc_servers_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &afs_proc_servers_ops);
}

static const struct file_operations afs_proc_servers_fops = {
	.open		= afs_proc_servers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * Display the list of strings that may be substituted for the @sys pathname
 * macro.
 */
static int afs_proc_sysname_show(struct seq_file *m, void *v)
{
	struct afs_net *net = afs_seq2net(m);
	struct afs_sysnames *sysnames = net->sysnames;
	unsigned int i = (unsigned long)v - 1;

	if (i < sysnames->nr)
		seq_printf(m, "%s\n", sysnames->subs[i]);
	return 0;
}

static void *afs_proc_sysname_start(struct seq_file *m, loff_t *pos)
	__acquires(&net->sysnames_lock)
{
	struct afs_net *net = afs_seq2net(m);
	struct afs_sysnames *names = net->sysnames;

	read_lock(&net->sysnames_lock);

	if (*pos >= names->nr)
		return NULL;
	return (void *)(unsigned long)(*pos + 1);
}

static void *afs_proc_sysname_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct afs_net *net = afs_seq2net(m);
	struct afs_sysnames *names = net->sysnames;

	*pos += 1;
	if (*pos >= names->nr)
		return NULL;
	return (void *)(unsigned long)(*pos + 1);
}

static void afs_proc_sysname_stop(struct seq_file *m, void *v)
	__releases(&net->sysnames_lock)
{
	struct afs_net *net = afs_seq2net(m);

	read_unlock(&net->sysnames_lock);
}

static const struct seq_operations afs_proc_sysname_ops = {
	.start	= afs_proc_sysname_start,
	.next	= afs_proc_sysname_next,
	.stop	= afs_proc_sysname_stop,
	.show	= afs_proc_sysname_show,
};

/*
 * Allow the @sys substitution to be configured.
 */
static ssize_t afs_proc_sysname_write(struct file *file,
				      const char __user *buf,
				      size_t size, loff_t *_pos)
{
	struct afs_sysnames *sysnames;
	struct seq_file *m = file->private_data;
	char *kbuf = NULL, *s, *p, *sub;
	int ret, len;

	sysnames = m->private;
	if (!sysnames)
		return -EINVAL;
	if (sysnames->error)
		return sysnames->error;

	if (size >= PAGE_SIZE - 1) {
		sysnames->error = -EINVAL;
		return -EINVAL;
	}
	if (size == 0)
		return 0;

	kbuf = memdup_user_nul(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	inode_lock(file_inode(file));

	p = kbuf;
	while ((s = strsep(&p, " \t\n"))) {
		len = strlen(s);
		if (len == 0)
			continue;
		ret = -ENAMETOOLONG;
		if (len >= AFSNAMEMAX)
			goto error;

		if (len >= 4 &&
		    s[len - 4] == '@' &&
		    s[len - 3] == 's' &&
		    s[len - 2] == 'y' &&
		    s[len - 1] == 's')
			/* Protect against recursion */
			goto invalid;

		if (s[0] == '.' &&
		    (len < 2 || (len == 2 && s[1] == '.')))
			goto invalid;

		if (memchr(s, '/', len))
			goto invalid;

		ret = -EFBIG;
		if (sysnames->nr >= AFS_NR_SYSNAME)
			goto out;

		if (strcmp(s, afs_init_sysname) == 0) {
			sub = (char *)afs_init_sysname;
		} else {
			ret = -ENOMEM;
			sub = kmemdup(s, len + 1, GFP_KERNEL);
			if (!sub)
				goto out;
		}

		sysnames->subs[sysnames->nr] = sub;
		sysnames->nr++;
	}

	ret = size;	/* consume everything, always */
out:
	inode_unlock(file_inode(file));
	kfree(kbuf);
	return ret;

invalid:
	ret = -EINVAL;
error:
	sysnames->error = ret;
	goto out;
}

void afs_put_sysnames(struct afs_sysnames *sysnames)
{
	int i;

	if (sysnames && refcount_dec_and_test(&sysnames->usage)) {
		for (i = 0; i < sysnames->nr; i++)
			if (sysnames->subs[i] != afs_init_sysname &&
			    sysnames->subs[i] != sysnames->blank)
				kfree(sysnames->subs[i]);
	}
}

static int afs_proc_sysname_release(struct inode *inode, struct file *file)
{
	struct afs_sysnames *sysnames, *kill = NULL;
	struct seq_file *m = file->private_data;
	struct afs_net *net = afs_seq2net(m);

	sysnames = m->private;
	if (sysnames) {
		if (!sysnames->error) {
			kill = sysnames;
			if (sysnames->nr == 0) {
				sysnames->subs[0] = sysnames->blank;
				sysnames->nr++;
			}
			write_lock(&net->sysnames_lock);
			kill = net->sysnames;
			net->sysnames = sysnames;
			write_unlock(&net->sysnames_lock);
		}
		afs_put_sysnames(kill);
	}

	return seq_release(inode, file);
}

/*
 * Handle opening of /proc/fs/afs/sysname.  If it is opened for writing, we
 * assume the caller wants to change the substitution list and we allocate a
 * buffer to hold the list.
 */
static int afs_proc_sysname_open(struct inode *inode, struct file *file)
{
	struct afs_sysnames *sysnames;
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &afs_proc_sysname_ops);
	if (ret < 0)
		return ret;

	if (file->f_mode & FMODE_WRITE) {
		sysnames = kzalloc(sizeof(*sysnames), GFP_KERNEL);
		if (!sysnames) {
			seq_release(inode, file);
			return -ENOMEM;
		}

		refcount_set(&sysnames->usage, 1);
		m = file->private_data;
		m->private = sysnames;
	}

	return 0;
}

static const struct file_operations afs_proc_sysname_fops = {
	.open		= afs_proc_sysname_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= afs_proc_sysname_release,
	.write		= afs_proc_sysname_write,
};

/*
 * Display general per-net namespace statistics
 */
static int afs_proc_stats_show(struct seq_file *m, void *v)
{
	struct afs_net *net = afs_seq2net(m);

	seq_puts(m, "kAFS statistics\n");

	seq_printf(m, "dir-mgmt: look=%u reval=%u inval=%u relpg=%u\n",
		   atomic_read(&net->n_lookup),
		   atomic_read(&net->n_reval),
		   atomic_read(&net->n_inval),
		   atomic_read(&net->n_relpg));

	seq_printf(m, "dir-data: rdpg=%u\n",
		   atomic_read(&net->n_read_dir));

	seq_printf(m, "dir-edit: cr=%u rm=%u\n",
		   atomic_read(&net->n_dir_cr),
		   atomic_read(&net->n_dir_rm));

	seq_printf(m, "file-rd : n=%u nb=%lu\n",
		   atomic_read(&net->n_fetches),
		   atomic_long_read(&net->n_fetch_bytes));
	seq_printf(m, "file-wr : n=%u nb=%lu\n",
		   atomic_read(&net->n_stores),
		   atomic_long_read(&net->n_store_bytes));
	return 0;
}

/*
 * initialise /proc/fs/afs/<cell>/
 */
int afs_proc_cell_setup(struct afs_net *net, struct afs_cell *cell)
{
	struct proc_dir_entry *dir;

	_enter("%p{%s},%p", cell, cell->name, net->proc_afs);

	dir = proc_mkdir(cell->name, net->proc_afs);
	if (!dir)
		goto error_dir;

	if (!proc_create_seq_data("vlservers", 0, dir,
				  &afs_proc_cell_vlservers_ops, cell))
		goto error_tree;
	if (!proc_create_seq_data("volumes", 0, dir,
				  &afs_proc_cell_volumes_ops, cell))
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
 * initialise the /proc/fs/afs/ directory
 */
int afs_proc_init(struct afs_net *net)
{
	_enter("");

	net->proc_afs = proc_mkdir("fs/afs", NULL);
	if (!net->proc_afs)
		goto error_dir;

	if (!proc_create("cells", 0644, net->proc_afs, &afs_proc_cells_fops) ||
	    !proc_create("rootcell", 0644, net->proc_afs, &afs_proc_rootcell_fops) ||
	    !proc_create_seq("servers", 0644, net->proc_afs, &afs_proc_servers_ops) ||
	    !proc_create_single("stats", 0644, net->proc_afs, afs_proc_stats_show) ||
	    !proc_create("sysname", 0644, net->proc_afs, &afs_proc_sysname_fops))
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
