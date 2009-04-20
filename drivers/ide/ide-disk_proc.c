#include <linux/kernel.h>
#include <linux/ide.h>

#include "ide-disk.h"

static int smart_enable(ide_drive_t *drive)
{
	struct ide_cmd cmd;
	struct ide_taskfile *tf = &cmd.tf;

	memset(&cmd, 0, sizeof(cmd));
	tf->feature = ATA_SMART_ENABLE;
	tf->lbam    = ATA_SMART_LBAM_PASS;
	tf->lbah    = ATA_SMART_LBAH_PASS;
	tf->command = ATA_CMD_SMART;
	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;

	return ide_no_data_taskfile(drive, &cmd);
}

static int get_smart_data(ide_drive_t *drive, u8 *buf, u8 sub_cmd)
{
	struct ide_cmd cmd;
	struct ide_taskfile *tf = &cmd.tf;

	memset(&cmd, 0, sizeof(cmd));
	tf->feature = sub_cmd;
	tf->nsect   = 0x01;
	tf->lbam    = ATA_SMART_LBAM_PASS;
	tf->lbah    = ATA_SMART_LBAH_PASS;
	tf->command = ATA_CMD_SMART;
	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;
	cmd.protocol = ATA_PROT_PIO;

	return ide_raw_taskfile(drive, &cmd, buf, 1);
}

static int proc_idedisk_read_cache
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	if (drive->dev_flags & IDE_DFLAG_ID_READ)
		len = sprintf(out, "%i\n", drive->id[ATA_ID_BUF_SIZE] / 2);
	else
		len = sprintf(out, "(none)\n");

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page, "%llu\n", (long long)ide_gd_capacity(drive));

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_smart(char *page, char **start, off_t off,
				   int count, int *eof, void *data, u8 sub_cmd)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	(void)smart_enable(drive);

	if (get_smart_data(drive, page, sub_cmd) == 0) {
		unsigned short *val = (unsigned short *) page;
		char *out = (char *)val + SECTOR_SIZE;

		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val),
				       (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < SECTOR_SIZE / 2);
		len = out - page;
	}

	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_idedisk_read_sv
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_idedisk_read_smart(page, start, off, count, eof, data,
				       ATA_SMART_READ_VALUES);
}

static int proc_idedisk_read_st
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_idedisk_read_smart(page, start, off, count, eof, data,
				       ATA_SMART_READ_THRESHOLDS);
}

ide_proc_entry_t ide_disk_proc[] = {
	{ "cache",	  S_IFREG|S_IRUGO, proc_idedisk_read_cache,    NULL },
	{ "capacity",	  S_IFREG|S_IRUGO, proc_idedisk_read_capacity, NULL },
	{ "geometry",	  S_IFREG|S_IRUGO, proc_ide_read_geometry,     NULL },
	{ "smart_values", S_IFREG|S_IRUSR, proc_idedisk_read_sv,       NULL },
	{ "smart_thresholds", S_IFREG|S_IRUSR, proc_idedisk_read_st,   NULL },
	{ NULL, 0, NULL, NULL }
};

ide_devset_rw_field(bios_cyl, bios_cyl);
ide_devset_rw_field(bios_head, bios_head);
ide_devset_rw_field(bios_sect, bios_sect);
ide_devset_rw_field(failures, failures);
ide_devset_rw_field(lun, lun);
ide_devset_rw_field(max_failures, max_failures);

const struct ide_proc_devset ide_disk_settings[] = {
	IDE_PROC_DEVSET(acoustic,	0,   254),
	IDE_PROC_DEVSET(address,	0,     2),
	IDE_PROC_DEVSET(bios_cyl,	0, 65535),
	IDE_PROC_DEVSET(bios_head,	0,   255),
	IDE_PROC_DEVSET(bios_sect,	0,    63),
	IDE_PROC_DEVSET(failures,	0, 65535),
	IDE_PROC_DEVSET(lun,		0,     7),
	IDE_PROC_DEVSET(max_failures,	0, 65535),
	IDE_PROC_DEVSET(multcount,	0,    16),
	IDE_PROC_DEVSET(nowerr,		0,     1),
	IDE_PROC_DEVSET(wcache,		0,     1),
	{ NULL },
};
