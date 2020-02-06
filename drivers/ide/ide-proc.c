// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1997-1998	Mark Lord
 *  Copyright (C) 2003		Red Hat
 *
 *  Some code was moved here from ide.c, see it for original copyrights.
 */

/*
 * This is the /proc/ide/ filesystem implementation.
 *
 * Drive/Driver settings can be retrieved by reading the drive's
 * "settings" files.  e.g.    "cat /proc/ide0/hda/settings"
 * To write a new value "val" into a specific setting "name", use:
 *   echo "name:val" >/proc/ide/ide0/hda/settings
 */

#include <linux/module.h>

#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ctype.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/io.h>

static struct proc_dir_entry *proc_ide_root;

static int ide_imodel_proc_show(struct seq_file *m, void *v)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) m->private;
	const char	*name;

	switch (hwif->chipset) {
	case ide_generic:	name = "generic";	break;
	case ide_pci:		name = "pci";		break;
	case ide_cmd640:	name = "cmd640";	break;
	case ide_dtc2278:	name = "dtc2278";	break;
	case ide_ali14xx:	name = "ali14xx";	break;
	case ide_qd65xx:	name = "qd65xx";	break;
	case ide_umc8672:	name = "umc8672";	break;
	case ide_ht6560b:	name = "ht6560b";	break;
	case ide_4drives:	name = "4drives";	break;
	case ide_pmac:		name = "mac-io";	break;
	case ide_au1xxx:	name = "au1xxx";	break;
	case ide_palm3710:      name = "palm3710";      break;
	case ide_acorn:		name = "acorn";		break;
	default:		name = "(unknown)";	break;
	}
	seq_printf(m, "%s\n", name);
	return 0;
}

static int ide_mate_proc_show(struct seq_file *m, void *v)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) m->private;

	if (hwif && hwif->mate)
		seq_printf(m, "%s\n", hwif->mate->name);
	else
		seq_printf(m, "(none)\n");
	return 0;
}

static int ide_channel_proc_show(struct seq_file *m, void *v)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) m->private;

	seq_printf(m, "%c\n", hwif->channel ? '1' : '0');
	return 0;
}

static int ide_identify_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t *drive = (ide_drive_t *)m->private;
	u8 *buf;

	if (!drive) {
		seq_putc(m, '\n');
		return 0;
	}

	buf = kmalloc(SECTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (taskfile_lib_get_identify(drive, buf) == 0) {
		__le16 *val = (__le16 *)buf;
		int i;

		for (i = 0; i < SECTOR_SIZE / 2; i++) {
			seq_printf(m, "%04x%c", le16_to_cpu(val[i]),
					(i % 8) == 7 ? '\n' : ' ');
		}
	} else
		seq_putc(m, buf[0]);
	kfree(buf);
	return 0;
}

/**
 *	ide_find_setting	-	find a specific setting
 *	@st: setting table pointer
 *	@name: setting name
 *
 *	Scan's the setting table for a matching entry and returns
 *	this or NULL if no entry is found. The caller must hold the
 *	setting semaphore
 */

static
const struct ide_proc_devset *ide_find_setting(const struct ide_proc_devset *st,
					       char *name)
{
	while (st->name) {
		if (strcmp(st->name, name) == 0)
			break;
		st++;
	}
	return st->name ? st : NULL;
}

/**
 *	ide_read_setting	-	read an IDE setting
 *	@drive: drive to read from
 *	@setting: drive setting
 *
 *	Read a drive setting and return the value. The caller
 *	must hold the ide_setting_mtx when making this call.
 *
 *	BUGS: the data return and error are the same return value
 *	so an error -EINVAL and true return of the same value cannot
 *	be told apart
 */

static int ide_read_setting(ide_drive_t *drive,
			    const struct ide_proc_devset *setting)
{
	const struct ide_devset *ds = setting->setting;
	int val = -EINVAL;

	if (ds->get)
		val = ds->get(drive);

	return val;
}

/**
 *	ide_write_setting	-	read an IDE setting
 *	@drive: drive to read from
 *	@setting: drive setting
 *	@val: value
 *
 *	Write a drive setting if it is possible. The caller
 *	must hold the ide_setting_mtx when making this call.
 *
 *	BUGS: the data return and error are the same return value
 *	so an error -EINVAL and true return of the same value cannot
 *	be told apart
 *
 *	FIXME:  This should be changed to enqueue a special request
 *	to the driver to change settings, and then wait on a sema for completion.
 *	The current scheme of polling is kludgy, though safe enough.
 */

static int ide_write_setting(ide_drive_t *drive,
			     const struct ide_proc_devset *setting, int val)
{
	const struct ide_devset *ds = setting->setting;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!ds->set)
		return -EPERM;
	if ((ds->flags & DS_SYNC)
	    && (val < setting->min || val > setting->max))
		return -EINVAL;
	return ide_devset_execute(drive, ds, val);
}

ide_devset_get(xfer_rate, current_speed);

static int set_xfer_rate (ide_drive_t *drive, int arg)
{
	struct ide_cmd cmd;

	if (arg < XFER_PIO_0 || arg > XFER_UDMA_6)
		return -EINVAL;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tf.command = ATA_CMD_SET_FEATURES;
	cmd.tf.feature = SETFEATURES_XFER;
	cmd.tf.nsect   = (u8)arg;
	cmd.valid.out.tf = IDE_VALID_FEATURE | IDE_VALID_NSECT;
	cmd.valid.in.tf  = IDE_VALID_NSECT;
	cmd.tf_flags   = IDE_TFLAG_SET_XFER;

	return ide_no_data_taskfile(drive, &cmd);
}

ide_devset_rw(current_speed, xfer_rate);
ide_devset_rw_field(init_speed, init_speed);
ide_devset_rw_flag(nice1, IDE_DFLAG_NICE1);
ide_devset_ro_field(number, dn);

static const struct ide_proc_devset ide_generic_settings[] = {
	IDE_PROC_DEVSET(current_speed, 0, 70),
	IDE_PROC_DEVSET(init_speed, 0, 70),
	IDE_PROC_DEVSET(io_32bit,  0, 1 + (SUPPORT_VLB_SYNC << 1)),
	IDE_PROC_DEVSET(keepsettings, 0, 1),
	IDE_PROC_DEVSET(nice1, 0, 1),
	IDE_PROC_DEVSET(number, 0, 3),
	IDE_PROC_DEVSET(pio_mode, 0, 255),
	IDE_PROC_DEVSET(unmaskirq, 0, 1),
	IDE_PROC_DEVSET(using_dma, 0, 1),
	{ NULL },
};

static void proc_ide_settings_warn(void)
{
	printk_once(KERN_WARNING "Warning: /proc/ide/hd?/settings interface is "
			    "obsolete, and will be removed soon!\n");
}

static int ide_settings_proc_show(struct seq_file *m, void *v)
{
	const struct ide_proc_devset *setting, *g, *d;
	const struct ide_devset *ds;
	ide_drive_t	*drive = (ide_drive_t *) m->private;
	int		rc, mul_factor, div_factor;

	proc_ide_settings_warn();

	mutex_lock(&ide_setting_mtx);
	g = ide_generic_settings;
	d = drive->settings;
	seq_printf(m, "name\t\t\tvalue\t\tmin\t\tmax\t\tmode\n");
	seq_printf(m, "----\t\t\t-----\t\t---\t\t---\t\t----\n");
	while (g->name || (d && d->name)) {
		/* read settings in the alphabetical order */
		if (g->name && d && d->name) {
			if (strcmp(d->name, g->name) < 0)
				setting = d++;
			else
				setting = g++;
		} else if (d && d->name) {
			setting = d++;
		} else
			setting = g++;
		mul_factor = setting->mulf ? setting->mulf(drive) : 1;
		div_factor = setting->divf ? setting->divf(drive) : 1;
		seq_printf(m, "%-24s", setting->name);
		rc = ide_read_setting(drive, setting);
		if (rc >= 0)
			seq_printf(m, "%-16d", rc * mul_factor / div_factor);
		else
			seq_printf(m, "%-16s", "write-only");
		seq_printf(m, "%-16d%-16d", (setting->min * mul_factor + div_factor - 1) / div_factor, setting->max * mul_factor / div_factor);
		ds = setting->setting;
		if (ds->get)
			seq_printf(m, "r");
		if (ds->set)
			seq_printf(m, "w");
		seq_printf(m, "\n");
	}
	mutex_unlock(&ide_setting_mtx);
	return 0;
}

static int ide_settings_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ide_settings_proc_show, PDE_DATA(inode));
}

#define MAX_LEN	30

static ssize_t ide_settings_proc_write(struct file *file, const char __user *buffer,
				       size_t count, loff_t *pos)
{
	ide_drive_t	*drive = PDE_DATA(file_inode(file));
	char		name[MAX_LEN + 1];
	int		for_real = 0, mul_factor, div_factor;
	unsigned long	n;

	const struct ide_proc_devset *setting;
	char *buf, *s;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	proc_ide_settings_warn();

	if (count >= PAGE_SIZE)
		return -EINVAL;

	s = buf = (char *)__get_free_page(GFP_USER);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		free_page((unsigned long)buf);
		return -EFAULT;
	}

	buf[count] = '\0';

	/*
	 * Skip over leading whitespace
	 */
	while (count && isspace(*s)) {
		--count;
		++s;
	}
	/*
	 * Do one full pass to verify all parameters,
	 * then do another to actually write the new settings.
	 */
	do {
		char *p = s;
		n = count;
		while (n > 0) {
			unsigned val;
			char *q = p;

			while (n > 0 && *p != ':') {
				--n;
				p++;
			}
			if (*p != ':')
				goto parse_error;
			if (p - q > MAX_LEN)
				goto parse_error;
			memcpy(name, q, p - q);
			name[p - q] = 0;

			if (n > 0) {
				--n;
				p++;
			} else
				goto parse_error;

			val = simple_strtoul(p, &q, 10);
			n -= q - p;
			p = q;
			if (n > 0 && !isspace(*p))
				goto parse_error;
			while (n > 0 && isspace(*p)) {
				--n;
				++p;
			}

			mutex_lock(&ide_setting_mtx);
			/* generic settings first, then driver specific ones */
			setting = ide_find_setting(ide_generic_settings, name);
			if (!setting) {
				if (drive->settings)
					setting = ide_find_setting(drive->settings, name);
				if (!setting) {
					mutex_unlock(&ide_setting_mtx);
					goto parse_error;
				}
			}
			if (for_real) {
				mul_factor = setting->mulf ? setting->mulf(drive) : 1;
				div_factor = setting->divf ? setting->divf(drive) : 1;
				ide_write_setting(drive, setting, val * div_factor / mul_factor);
			}
			mutex_unlock(&ide_setting_mtx);
		}
	} while (!for_real++);
	free_page((unsigned long)buf);
	return count;
parse_error:
	free_page((unsigned long)buf);
	printk("%s(): parse error\n", __func__);
	return -EINVAL;
}

static const struct file_operations ide_settings_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ide_settings_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ide_settings_proc_write,
};

int ide_capacity_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%llu\n", (long long)0x7fffffff);
	return 0;
}
EXPORT_SYMBOL_GPL(ide_capacity_proc_show);

int ide_geometry_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t	*drive = (ide_drive_t *) m->private;

	seq_printf(m, "physical     %d/%d/%d\n",
			drive->cyl, drive->head, drive->sect);
	seq_printf(m, "logical      %d/%d/%d\n",
			drive->bios_cyl, drive->bios_head, drive->bios_sect);
	return 0;
}
EXPORT_SYMBOL(ide_geometry_proc_show);

static int ide_dmodel_proc_show(struct seq_file *seq, void *v)
{
	ide_drive_t	*drive = (ide_drive_t *) seq->private;
	char		*m = (char *)&drive->id[ATA_ID_PROD];

	seq_printf(seq, "%.40s\n", m[0] ? m : "(none)");
	return 0;
}

static int ide_driver_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t		*drive = (ide_drive_t *)m->private;
	struct device		*dev = &drive->gendev;
	struct ide_driver	*ide_drv;

	if (dev->driver) {
		ide_drv = to_ide_driver(dev->driver);
		seq_printf(m, "%s version %s\n",
				dev->driver->name, ide_drv->version);
	} else
		seq_printf(m, "ide-default version 0.9.newide\n");
	return 0;
}

static int ide_media_proc_show(struct seq_file *m, void *v)
{
	ide_drive_t	*drive = (ide_drive_t *) m->private;
	const char	*media;

	switch (drive->media) {
	case ide_disk:		media = "disk\n";	break;
	case ide_cdrom:		media = "cdrom\n";	break;
	case ide_tape:		media = "tape\n";	break;
	case ide_floppy:	media = "floppy\n";	break;
	case ide_optical:	media = "optical\n";	break;
	default:		media = "UNKNOWN\n";	break;
	}
	seq_puts(m, media);
	return 0;
}

static int ide_media_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ide_media_proc_show, PDE_DATA(inode));
}

static const struct file_operations ide_media_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ide_media_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ide_proc_entry_t generic_drive_entries[] = {
	{ "driver",	S_IFREG|S_IRUGO,	 ide_driver_proc_show	},
	{ "identify",	S_IFREG|S_IRUSR,	 ide_identify_proc_show	},
	{ "media",	S_IFREG|S_IRUGO,	 ide_media_proc_show	},
	{ "model",	S_IFREG|S_IRUGO,	 ide_dmodel_proc_show	},
	{}
};

static void ide_add_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p, void *data)
{
	struct proc_dir_entry *ent;

	if (!dir || !p)
		return;
	while (p->name != NULL) {
		ent = proc_create_single_data(p->name, p->mode, dir, p->show, data);
		if (!ent) return;
		p++;
	}
}

static void ide_remove_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p)
{
	if (!dir || !p)
		return;
	while (p->name != NULL) {
		remove_proc_entry(p->name, dir);
		p++;
	}
}

void ide_proc_register_driver(ide_drive_t *drive, struct ide_driver *driver)
{
	mutex_lock(&ide_setting_mtx);
	drive->settings = driver->proc_devsets(drive);
	mutex_unlock(&ide_setting_mtx);

	ide_add_proc_entries(drive->proc, driver->proc_entries(drive), drive);
}

EXPORT_SYMBOL(ide_proc_register_driver);

/**
 *	ide_proc_unregister_driver	-	remove driver specific data
 *	@drive: drive
 *	@driver: driver
 *
 *	Clean up the driver specific /proc files and IDE settings
 *	for a given drive.
 *
 *	Takes ide_setting_mtx.
 */

void ide_proc_unregister_driver(ide_drive_t *drive, struct ide_driver *driver)
{
	ide_remove_proc_entries(drive->proc, driver->proc_entries(drive));

	mutex_lock(&ide_setting_mtx);
	/*
	 * ide_setting_mtx protects both the settings list and the use
	 * of settings (we cannot take a setting out that is being used).
	 */
	drive->settings = NULL;
	mutex_unlock(&ide_setting_mtx);
}
EXPORT_SYMBOL(ide_proc_unregister_driver);

void ide_proc_port_register_devices(ide_hwif_t *hwif)
{
	struct proc_dir_entry *ent;
	struct proc_dir_entry *parent = hwif->proc;
	ide_drive_t *drive;
	char name[64];
	int i;

	ide_port_for_each_dev(i, drive, hwif) {
		if ((drive->dev_flags & IDE_DFLAG_PRESENT) == 0)
			continue;

		drive->proc = proc_mkdir(drive->name, parent);
		if (drive->proc) {
			ide_add_proc_entries(drive->proc, generic_drive_entries, drive);
			proc_create_data("settings", S_IFREG|S_IRUSR|S_IWUSR,
					drive->proc, &ide_settings_proc_fops,
					drive);
		}
		sprintf(name, "ide%d/%s", (drive->name[2]-'a')/2, drive->name);
		ent = proc_symlink(drive->name, proc_ide_root, name);
		if (!ent) return;
	}
}

void ide_proc_unregister_device(ide_drive_t *drive)
{
	if (drive->proc) {
		remove_proc_entry("settings", drive->proc);
		ide_remove_proc_entries(drive->proc, generic_drive_entries);
		remove_proc_entry(drive->name, proc_ide_root);
		remove_proc_entry(drive->name, drive->hwif->proc);
		drive->proc = NULL;
	}
}

static ide_proc_entry_t hwif_entries[] = {
	{ "channel",	S_IFREG|S_IRUGO,	ide_channel_proc_show	},
	{ "mate",	S_IFREG|S_IRUGO,	ide_mate_proc_show	},
	{ "model",	S_IFREG|S_IRUGO,	ide_imodel_proc_show	},
	{}
};

void ide_proc_register_port(ide_hwif_t *hwif)
{
	if (!hwif->proc) {
		hwif->proc = proc_mkdir(hwif->name, proc_ide_root);

		if (!hwif->proc)
			return;

		ide_add_proc_entries(hwif->proc, hwif_entries, hwif);
	}
}

void ide_proc_unregister_port(ide_hwif_t *hwif)
{
	if (hwif->proc) {
		ide_remove_proc_entries(hwif->proc, hwif_entries);
		remove_proc_entry(hwif->name, proc_ide_root);
		hwif->proc = NULL;
	}
}

static int proc_print_driver(struct device_driver *drv, void *data)
{
	struct ide_driver *ide_drv = to_ide_driver(drv);
	struct seq_file *s = data;

	seq_printf(s, "%s version %s\n", drv->name, ide_drv->version);

	return 0;
}

static int ide_drivers_show(struct seq_file *s, void *p)
{
	int err;

	err = bus_for_each_drv(&ide_bus_type, NULL, s, proc_print_driver);
	if (err < 0)
		printk(KERN_WARNING "IDE: %s: bus_for_each_drv error: %d\n",
			__func__, err);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ide_drivers);

void proc_ide_create(void)
{
	proc_ide_root = proc_mkdir("ide", NULL);

	if (!proc_ide_root)
		return;

	proc_create("drivers", 0, proc_ide_root, &ide_drivers_fops);
}

void proc_ide_destroy(void)
{
	remove_proc_entry("drivers", proc_ide_root);
	remove_proc_entry("ide", NULL);
}
