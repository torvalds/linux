// SPDX-License-Identifier: GPL-2.0
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

ide_proc_entry_t ide_floppy_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO, idefloppy_capacity_proc_show	},
	{ "geometry",	S_IFREG|S_IRUGO, ide_geometry_proc_show		},
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
