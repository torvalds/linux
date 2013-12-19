#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ide.h>
#include <linux/seq_file.h>

#include "ide-floppy.h"

static int idefloppy_capacity_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t*drive = (ide_drive_t *)m->private;

	seq_printf(m, "%llu\n", (long long)ide_gd_capacity(drive));
	return 0;
}

static int idefloppy_capacity_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, idefloppy_capacity_proc_show, PDE_DATA(inode));
}

static const struct file_operations idefloppy_capacity_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= idefloppy_capacity_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

ide_proc_entry_t ide_floppy_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO, &idefloppy_capacity_proc_fops	},
	{ "geometry",	S_IFREG|S_IRUGO, &ide_geometry_proc_fops	},
	{}
};

ide_devset_rw_field(bios_cyl, bios_cyl);
ide_devset_rw_field(bios_head, bios_head);
ide_devset_rw_field(bios_sect, bios_sect);
ide_devset_rw_field(ticks, pc_delay);

const struct ide_proc_devset ide_floppy_settings[] = {
	IDE_PROC_DEVSET(bios_cyl,  0, 1023),
	IDE_PROC_DEVSET(bios_head, 0,  255),
	IDE_PROC_DEVSET(bios_sect, 0,   63),
	IDE_PROC_DEVSET(ticks,	   0,  255),
	{ NULL },
};
