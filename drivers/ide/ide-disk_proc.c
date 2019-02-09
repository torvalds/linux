// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/ide.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/seq_file.h>

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

static int idedisk_cache_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t	*drive = (ide_drive_t *) m->private;

	if (drive->dev_flags & IDE_DFLAG_ID_READ)
		seq_printf(m, "%i\n", drive->id[ATA_ID_BUF_SIZE] / 2);
	else
		seq_printf(m, "(none)\n");
	return 0;
}

static int idedisk_capacity_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t*drive = (ide_drive_t *)m->private;

	seq_printf(m, "%llu\n", (long long)ide_gd_capacity(drive));
	return 0;
}

static int __idedisk_proc_show(struct seq_file *m, ide_drive_t *drive, u8 sub_cmd)
{
	u8 *buf;

	buf = kmalloc(SECTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	(void)smart_enable(drive);

	if (get_smart_data(drive, buf, sub_cmd) == 0) {
		__le16 *val = (__le16 *)buf;
		int i;

		for (i = 0; i < SECTOR_SIZE / 2; i++) {
			seq_printf(m, "%04x%c", le16_to_cpu(val[i]),
					(i % 8) == 7 ? '\n' : ' ');
		}
	}
	kfree(buf);
	return 0;
}

static int idedisk_sv_proc_show(struct seq_file *m, void *v)
{
	return __idedisk_proc_show(m, m->private, ATA_SMART_READ_VALUES);
}

static int idedisk_st_proc_show(struct seq_file *m, void *v)
{
	return __idedisk_proc_show(m, m->private, ATA_SMART_READ_THRESHOLDS);
}

ide_proc_entry_t ide_disk_proc[] = {
	{ "cache",	  S_IFREG|S_IRUGO, idedisk_cache_proc_show	},
	{ "capacity",	  S_IFREG|S_IRUGO, idedisk_capacity_proc_show	},
	{ "geometry",	  S_IFREG|S_IRUGO, ide_geometry_proc_show	},
	{ "smart_values", S_IFREG|S_IRUSR, idedisk_sv_proc_show		},
	{ "smart_thresholds", S_IFREG|S_IRUSR, idedisk_st_proc_show	},
	{}
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
