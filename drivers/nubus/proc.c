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
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>

#include <linux/uaccess.h>
#include <asm/byteorder.h>

static int
nubus_devices_proc_show(struct seq_file *m, void *v)
{
	struct nubus_dev *dev = nubus_devices;

	while (dev) {
		seq_printf(m, "%x\t%04x %04x %04x %04x",
			      dev->board->slot,
			      dev->category,
			      dev->type,
			      dev->dr_sw,
			      dev->dr_hw);
		seq_printf(m, "\t%08lx\n", dev->board->slot_addr);
		dev = dev->next;
	}
	return 0;
}

static int nubus_devices_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nubus_devices_proc_show, NULL);
}

static const struct file_operations nubus_devices_proc_fops = {
	.open		= nubus_devices_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct proc_dir_entry *proc_bus_nubus_dir;

static const struct file_operations nubus_proc_subdir_fops = {
#warning Need to set some I/O handlers here
};

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
		e = proc_create(name, S_IFREG | S_IRUGO | S_IWUSR, parent,
				&nubus_proc_subdir_fops);
		if (!e)
			return;
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

/*
 * /proc/nubus stuff
 */
static int nubus_proc_show(struct seq_file *m, void *v)
{
	const struct nubus_board *board = v;

	/* Display header on line 1 */
	if (v == SEQ_START_TOKEN)
		seq_puts(m, "Nubus devices found:\n");
	else
		seq_printf(m, "Slot %X: %s\n", board->slot, board->name);
	return 0;
}

static void *nubus_proc_start(struct seq_file *m, loff_t *_pos)
{
	struct nubus_board *board;
	unsigned pos;

	if (*_pos > LONG_MAX)
		return NULL;
	pos = *_pos;
	if (pos == 0)
		return SEQ_START_TOKEN;
	for (board = nubus_boards; board; board = board->next)
		if (--pos == 0)
			break;
	return board;
}

static void *nubus_proc_next(struct seq_file *p, void *v, loff_t *_pos)
{
	/* Walk the list of NuBus boards */
	struct nubus_board *board = v;

	++*_pos;
	if (v == SEQ_START_TOKEN)
		board = nubus_boards;
	else if (board)
		board = board->next;
	return board;
}

static void nubus_proc_stop(struct seq_file *p, void *v)
{
}

static const struct seq_operations nubus_proc_seqops = {
	.start	= nubus_proc_start,
	.next	= nubus_proc_next,
	.stop	= nubus_proc_stop,
	.show	= nubus_proc_show,
};

static int nubus_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nubus_proc_seqops);
}

static const struct file_operations nubus_proc_fops = {
	.open		= nubus_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

void __init proc_bus_nubus_add_devices(void)
{
	struct nubus_dev *dev;
	
	for(dev = nubus_devices; dev; dev = dev->next)
		nubus_proc_attach_device(dev);
}

void __init nubus_proc_init(void)
{
	proc_create("nubus", 0, NULL, &nubus_proc_fops);
	if (!MACH_IS_MAC)
		return;
	proc_bus_nubus_dir = proc_mkdir("bus/nubus", NULL);
	proc_create("devices", 0, proc_bus_nubus_dir, &nubus_devices_proc_fops);
	proc_bus_nubus_add_devices();
}
