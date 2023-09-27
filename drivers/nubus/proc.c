// SPDX-License-Identifier: GPL-2.0
/* drivers/nubus/proc.c: Proc FS interface for NuBus.

   By David Huggins-Daines <dhd@debian.org>

   Much code and many ideas from drivers/pci/proc.c:
   Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

   This is initially based on the Zorro and PCI interfaces.  However,
   it works somewhat differently.  The intent is to provide a
   structure in /proc analogous to the structure of the NuBus ROM
   resources.

   Therefore each board function gets a directory, which may in turn
   contain subdirectories.  Each slot resource is a file.  Unrecognized
   resources are empty files, since every resource ID requires a special
   case (e.g. if the resource ID implies a directory or block, then its
   value has to be interpreted as a slot ROM pointer etc.).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/nubus.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>

/*
 * /proc/bus/nubus/devices stuff
 */

static int
nubus_devices_proc_show(struct seq_file *m, void *v)
{
	struct nubus_rsrc *fres;

	for_each_func_rsrc(fres)
		seq_printf(m, "%x\t%04x %04x %04x %04x\t%08lx\n",
			   fres->board->slot, fres->category, fres->type,
			   fres->dr_sw, fres->dr_hw, fres->board->slot_addr);
	return 0;
}

static struct proc_dir_entry *proc_bus_nubus_dir;

/*
 * /proc/bus/nubus/x/ stuff
 */

struct proc_dir_entry *nubus_proc_add_board(struct nubus_board *board)
{
	char name[2];

	if (!proc_bus_nubus_dir)
		return NULL;
	snprintf(name, sizeof(name), "%x", board->slot);
	return proc_mkdir(name, proc_bus_nubus_dir);
}

/* The PDE private data for any directory under /proc/bus/nubus/x/
 * is the bytelanes value for the board in slot x.
 */

struct proc_dir_entry *nubus_proc_add_rsrc_dir(struct proc_dir_entry *procdir,
					       const struct nubus_dirent *ent,
					       struct nubus_board *board)
{
	char name[9];
	int lanes = board->lanes;

	if (!procdir)
		return NULL;
	snprintf(name, sizeof(name), "%x", ent->type);
	return proc_mkdir_data(name, 0555, procdir, (void *)lanes);
}

/* The PDE private data for a file under /proc/bus/nubus/x/ is a pointer to
 * an instance of the following structure, which gives the location and size
 * of the resource data in the slot ROM. For slot resources which hold only a
 * small integer, this integer value is stored directly and size is set to 0.
 * A NULL private data pointer indicates an unrecognized resource.
 */

struct nubus_proc_pde_data {
	unsigned char *res_ptr;
	unsigned int res_size;
};

static struct nubus_proc_pde_data *
nubus_proc_alloc_pde_data(unsigned char *ptr, unsigned int size)
{
	struct nubus_proc_pde_data *pde_data;

	pde_data = kmalloc(sizeof(*pde_data), GFP_KERNEL);
	if (!pde_data)
		return NULL;

	pde_data->res_ptr = ptr;
	pde_data->res_size = size;
	return pde_data;
}

static int nubus_proc_rsrc_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct nubus_proc_pde_data *pde_data;

	pde_data = PDE_DATA(inode);
	if (!pde_data)
		return 0;

	if (pde_data->res_size > m->size)
		return -EFBIG;

	if (pde_data->res_size) {
		int lanes = (int)proc_get_parent_data(inode);
		struct nubus_dirent ent;

		if (!lanes)
			return 0;

		ent.mask = lanes;
		ent.base = pde_data->res_ptr;
		ent.data = 0;
		nubus_seq_write_rsrc_mem(m, &ent, pde_data->res_size);
	} else {
		unsigned int data = (unsigned int)pde_data->res_ptr;

		seq_putc(m, data >> 16);
		seq_putc(m, data >> 8);
		seq_putc(m, data >> 0);
	}
	return 0;
}

static int nubus_rsrc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nubus_proc_rsrc_show, inode);
}

static const struct proc_ops nubus_rsrc_proc_ops = {
	.proc_open	= nubus_rsrc_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

void nubus_proc_add_rsrc_mem(struct proc_dir_entry *procdir,
			     const struct nubus_dirent *ent,
			     unsigned int size)
{
	char name[9];
	struct nubus_proc_pde_data *pde_data;

	if (!procdir)
		return;

	snprintf(name, sizeof(name), "%x", ent->type);
	if (size)
		pde_data = nubus_proc_alloc_pde_data(nubus_dirptr(ent), size);
	else
		pde_data = NULL;
	proc_create_data(name, S_IFREG | 0444, procdir,
			 &nubus_rsrc_proc_ops, pde_data);
}

void nubus_proc_add_rsrc(struct proc_dir_entry *procdir,
			 const struct nubus_dirent *ent)
{
	char name[9];
	unsigned char *data = (unsigned char *)ent->data;

	if (!procdir)
		return;

	snprintf(name, sizeof(name), "%x", ent->type);
	proc_create_data(name, S_IFREG | 0444, procdir,
			 &nubus_rsrc_proc_ops,
			 nubus_proc_alloc_pde_data(data, 0));
}

/*
 * /proc/nubus stuff
 */

void __init nubus_proc_init(void)
{
	proc_create_single("nubus", 0, NULL, nubus_proc_show);
	proc_bus_nubus_dir = proc_mkdir("bus/nubus", NULL);
	if (!proc_bus_nubus_dir)
		return;
	proc_create_single("devices", 0, proc_bus_nubus_dir,
			nubus_devices_proc_show);
}
