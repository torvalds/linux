#include <linux/kernel.h>
#include <linux/ide.h>

#include "ide-floppy.h"

static int proc_idefloppy_read_capacity(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page, "%llu\n", (long long)ide_gd_capacity(drive));
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

ide_proc_entry_t ide_floppy_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO, proc_idefloppy_read_capacity,	NULL },
	{ "geometry",	S_IFREG|S_IRUGO, proc_ide_read_geometry,	NULL },
	{ NULL, 0, NULL, NULL }
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
