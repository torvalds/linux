/* drivers/nubus/proc.c: Proc FS interface for NuBus.

   By David Huggins-Daines <dhd@debian.org>

   Much code and many ideas from drivers/pci/proc.c:
   Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>

   This is initially based on the Zorro and PCI interfaces.  However,
   it works somewhat differently.  The intent is to provide a
   structure in /proc analogous to the structure of the NuBus ROM
   resources.

   Therefore each NuBus device is in fact a directory, which may in
   turn contain subdirectories.  The "files" correspond to NuBus
   resource records.  For those types of records which we know how to
   convert to formats that are meaningful to userspace (mostly just
   icons) these files will provide "cooked" data.  Otherwise they will
   simply provide raw access (read-only of course) to the ROM.  */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/nubus.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>

static int
get_nubus_dev_info(char *buf, char **start, off_t pos, int count)
{
	struct nubus_dev *dev = nubus_devices;
	off_t at = 0;
	int len, cnt;

	cnt = 0;
	while (dev && count > cnt) {
		len = sprintf(buf, "%x\t%04x %04x %04x %04x",
			      dev->board->slot,
			      dev->category,
			      dev->type,
			      dev->dr_sw,
			      dev->dr_hw);
		len += sprintf(buf+len,
			       "\t%08lx",
			       dev->board->slot_addr);
		buf[len++] = '\n';
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		dev = dev->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_nubus_dir;

static void nubus_proc_subdir(struct nubus_dev* dev,
			      struct proc_dir_entry* parent,
			      struct nubus_dir* dir)
{
	struct nubus_dirent ent;

	/* Some of these are directories, others aren't */
	while (nubus_readdir(dir, &ent) != -1) {
		char name[8];
		struct proc_dir_entry* e;
		
		sprintf(name, "%x", ent.type);
		e = create_proc_entry(name, S_IFREG | S_IRUGO |
				      S_IWUSR, parent);
		if (!e) return;
	}
}

/* Can't do this recursively since the root directory is structured
   somewhat differently from the subdirectories */
static void nubus_proc_populate(struct nubus_dev* dev,
				struct proc_dir_entry* parent,
				struct nubus_dir* root)
{
	struct nubus_dirent ent;

	/* We know these are all directories (board resource + one or
	   more functional resources) */
	while (nubus_readdir(root, &ent) != -1) {
		char name[8];
		struct proc_dir_entry* e;
		struct nubus_dir dir;
		
		sprintf(name, "%x", ent.type);
		e = proc_mkdir(name, parent);
		if (!e) return;

		/* And descend */
		if (nubus_get_subdir(&ent, &dir) == -1) {
			/* This shouldn't happen */
			printk(KERN_ERR "NuBus root directory node %x:%x has no subdir!\n",
			       dev->board->slot, ent.type);
			continue;
		} else {
			nubus_proc_subdir(dev, e, &dir);
		}
	}
}

int nubus_proc_attach_device(struct nubus_dev *dev)
{
	struct proc_dir_entry *e;
	struct nubus_dir root;
	char name[8];

	if (dev == NULL) {
		printk(KERN_ERR
		       "NULL pointer in nubus_proc_attach_device, shoot the programmer!\n");
		return -1;
	}
		
	if (dev->board == NULL) {
		printk(KERN_ERR
		       "NULL pointer in nubus_proc_attach_device, shoot the programmer!\n");
		printk("dev = %p, dev->board = %p\n", dev, dev->board);
		return -1;
	}
		
	/* Create a directory */
	sprintf(name, "%x", dev->board->slot);
	e = dev->procdir = proc_mkdir(name, proc_bus_nubus_dir);
	if (!e)
		return -ENOMEM;

	/* Now recursively populate it with files */
	nubus_get_root_dir(dev->board, &root);
	nubus_proc_populate(dev, e, &root);

	return 0;
}
EXPORT_SYMBOL(nubus_proc_attach_device);

/* FIXME: this is certainly broken! */
int nubus_proc_detach_device(struct nubus_dev *dev)
{
	struct proc_dir_entry *e;

	if ((e = dev->procdir)) {
		if (atomic_read(&e->count))
			return -EBUSY;
		remove_proc_entry(e->name, proc_bus_nubus_dir);
		dev->procdir = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(nubus_proc_detach_device);

void __init proc_bus_nubus_add_devices(void)
{
	struct nubus_dev *dev;
	
	for(dev = nubus_devices; dev; dev = dev->next)
		nubus_proc_attach_device(dev);
}

void __init nubus_proc_init(void)
{
	if (!MACH_IS_MAC)
		return;
	proc_bus_nubus_dir = proc_mkdir("bus/nubus", NULL);
	create_proc_info_entry("devices", 0, proc_bus_nubus_dir,
				get_nubus_dev_info);
	proc_bus_nubus_add_devices();
}
