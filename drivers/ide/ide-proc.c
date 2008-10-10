/*
 *  Copyright (C) 1997-1998	Mark Lord
 *  Copyright (C) 2003		Red Hat <alan@redhat.com>
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

#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ctype.h>
#include <linux/ide.h>
#include <linux/seq_file.h>

#include <asm/io.h>

static struct proc_dir_entry *proc_ide_root;

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
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
	case ide_rz1000:	name = "rz1000";	break;
	case ide_trm290:	name = "trm290";	break;
	case ide_cmd646:	name = "cmd646";	break;
	case ide_cy82c693:	name = "cy82c693";	break;
	case ide_4drives:	name = "4drives";	break;
	case ide_pmac:		name = "mac-io";	break;
	case ide_au1xxx:	name = "au1xxx";	break;
	case ide_palm3710:      name = "palm3710";      break;
	case ide_acorn:		name = "acorn";		break;
	default:		name = "(unknown)";	break;
	}
	len = sprintf(page, "%s\n", name);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_ide_read_mate
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	if (hwif && hwif->mate)
		len = sprintf(page, "%s\n", hwif->mate->name);
	else
		len = sprintf(page, "(none)\n");
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_ide_read_channel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	page[0] = hwif->channel ? '1' : '0';
	page[1] = '\n';
	len = 2;
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;
	int		err = 0;

	len = sprintf(page, "\n");

	if (drive) {
		__le16 *val = (__le16 *)page;

		err = taskfile_lib_get_identify(drive, page);
		if (!err) {
			char *out = (char *)page + SECTOR_SIZE;

			page = out;
			do {
				out += sprintf(out, "%04x%c",
					le16_to_cpup(val), (++i & 7) ? ' ' : '\n');
				val += 1;
			} while (i < SECTOR_SIZE / 2);
			len = out - page;
		}
	}
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
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

static const struct ide_devset *ide_find_setting(const struct ide_devset **st,
						 char *name)
{
	while (*st) {
		if (strcmp((*st)->name, name) == 0)
			break;
		st++;
	}
	return *st;
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
			    const struct ide_devset *setting)
{
	int val = -EINVAL;

	if ((setting->flags & S_READ)) {
		unsigned long flags;

		spin_lock_irqsave(&ide_lock, flags);
		val = setting->get(drive);
		spin_unlock_irqrestore(&ide_lock, flags);
	}

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
			     const struct ide_devset *setting, int val)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (setting->set && (setting->flags & S_NOLOCK))
		return setting->set(drive, val);
	if (!(setting->flags & S_WRITE))
		return -EPERM;
	if (val < setting->min || val > setting->max)
		return -EINVAL;
	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	setting->set(drive, val);
	spin_unlock_irq(&ide_lock);
	return 0;
}

static ide_devset_get(xfer_rate, current_speed);

static int set_xfer_rate (ide_drive_t *drive, int arg)
{
	ide_task_t task;
	int err;

	if (arg < XFER_PIO_0 || arg > XFER_UDMA_6)
		return -EINVAL;

	memset(&task, 0, sizeof(task));
	task.tf.command = ATA_CMD_SET_FEATURES;
	task.tf.feature = SETFEATURES_XFER;
	task.tf.nsect   = (u8)arg;
	task.tf_flags = IDE_TFLAG_OUT_FEATURE | IDE_TFLAG_OUT_NSECT |
			IDE_TFLAG_IN_NSECT;

	err = ide_no_data_taskfile(drive, &task);

	if (!err) {
		ide_set_xfer_rate(drive, (u8) arg);
		ide_driveid_update(drive);
	}
	return err;
}

ide_devset_rw_nolock(current_speed, 0, 70, xfer_rate);
ide_devset_rw_nolock(io_32bit, 0, 1 + (SUPPORT_VLB_SYNC << 1), io_32bit);
ide_devset_rw_nolock(keepsettings, 0, 1, ksettings);
ide_devset_rw_nolock(unmaskirq, 0, 1, unmaskirq);
ide_devset_rw_nolock(using_dma, 0, 1, using_dma);

ide_devset_w_nolock(pio_mode, 0, 255, pio_mode);

ide_devset_rw(init_speed, 0, 70, init_speed);
ide_devset_rw(nice1, 0, 1, nice1);
ide_devset_rw(number, 0, 3, dn);

static const struct ide_devset *ide_generic_settings[] = {
	&ide_devset_current_speed,
	&ide_devset_init_speed,
	&ide_devset_io_32bit,
	&ide_devset_keepsettings,
	&ide_devset_nice1,
	&ide_devset_number,
	&ide_devset_pio_mode,
	&ide_devset_unmaskirq,
	&ide_devset_using_dma,
	NULL
};

static void proc_ide_settings_warn(void)
{
	static int warned;

	if (warned)
		return;

	printk(KERN_WARNING "Warning: /proc/ide/hd?/settings interface is "
			    "obsolete, and will be removed soon!\n");
	warned = 1;
}

static int proc_ide_read_settings
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	const struct ide_devset *setting, **g, **d;
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len, rc, mul_factor, div_factor;

	proc_ide_settings_warn();

	mutex_lock(&ide_setting_mtx);
	g = ide_generic_settings;
	d = drive->settings;
	out += sprintf(out, "name\t\t\tvalue\t\tmin\t\tmax\t\tmode\n");
	out += sprintf(out, "----\t\t\t-----\t\t---\t\t---\t\t----\n");
	while (*g || (d && *d)) {
		/* read settings in the alphabetical order */
		if (*g && d && *d) {
			if (strcmp((*d)->name, (*g)->name) < 0)
				setting = *d++;
			else
				setting = *g++;
		} else if (d && *d) {
			setting = *d++;
		} else
			setting = *g++;
		mul_factor = setting->mulf ? setting->mulf(drive) : 1;
		div_factor = setting->divf ? setting->divf(drive) : 1;
		out += sprintf(out, "%-24s", setting->name);
		rc = ide_read_setting(drive, setting);
		if (rc >= 0)
			out += sprintf(out, "%-16d", rc * mul_factor / div_factor);
		else
			out += sprintf(out, "%-16s", "write-only");
		out += sprintf(out, "%-16d%-16d", (setting->min * mul_factor + div_factor - 1) / div_factor, setting->max * mul_factor / div_factor);
		if (setting->flags & S_READ)
			out += sprintf(out, "r");
		if (setting->flags & S_WRITE)
			out += sprintf(out, "w");
		out += sprintf(out, "\n");
	}
	len = out - page;
	mutex_unlock(&ide_setting_mtx);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

#define MAX_LEN	30

static int proc_ide_write_settings(struct file *file, const char __user *buffer,
				   unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		name[MAX_LEN + 1];
	int		for_real = 0, mul_factor, div_factor;
	unsigned long	n;

	const struct ide_devset *setting;
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
	printk("proc_ide_write_settings(): parse error\n");
	return -EINVAL;
}

int proc_ide_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf(page, "%llu\n", (long long)0x7fffffff);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

EXPORT_SYMBOL_GPL(proc_ide_read_capacity);

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	out += sprintf(out, "physical     %d/%d/%d\n",
			drive->cyl, drive->head, drive->sect);
	out += sprintf(out, "logical      %d/%d/%d\n",
			drive->bios_cyl, drive->bios_head, drive->bios_sect);

	len = out - page;
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

EXPORT_SYMBOL(proc_ide_read_geometry);

static int proc_ide_read_dmodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*m = (char *)&drive->id[ATA_ID_PROD];
	int		len;

	len = sprintf(page, "%.40s\n", m[0] ? m : "(none)");
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int proc_ide_read_driver
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	struct device	*dev = &drive->gendev;
	ide_driver_t	*ide_drv;
	int		len;

	if (dev->driver) {
		ide_drv = container_of(dev->driver, ide_driver_t, gen_driver);
		len = sprintf(page, "%s version %s\n",
				dev->driver->name, ide_drv->version);
	} else
		len = sprintf(page, "ide-default version 0.9.newide\n");
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static int ide_replace_subdriver(ide_drive_t *drive, const char *driver)
{
	struct device *dev = &drive->gendev;
	int ret = 1;
	int err;

	device_release_driver(dev);
	/* FIXME: device can still be in use by previous driver */
	strlcpy(drive->driver_req, driver, sizeof(drive->driver_req));
	err = device_attach(dev);
	if (err < 0)
		printk(KERN_WARNING "IDE: %s: device_attach error: %d\n",
			__func__, err);
	drive->driver_req[0] = 0;
	if (dev->driver == NULL) {
		err = device_attach(dev);
		if (err < 0)
			printk(KERN_WARNING
				"IDE: %s: device_attach(2) error: %d\n",
				__func__, err);
	}
	if (dev->driver && !strcmp(dev->driver->name, driver))
		ret = 0;

	return ret;
}

static int proc_ide_write_driver
	(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char name[32];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (count > 31)
		count = 31;
	if (copy_from_user(name, buffer, count))
		return -EFAULT;
	name[count] = '\0';
	if (ide_replace_subdriver(drive, name))
		return -EINVAL;
	return count;
}

static int proc_ide_read_media
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	const char	*media;
	int		len;

	switch (drive->media) {
	case ide_disk:		media = "disk\n";	break;
	case ide_cdrom:		media = "cdrom\n";	break;
	case ide_tape:		media = "tape\n";	break;
	case ide_floppy:	media = "floppy\n";	break;
	case ide_optical:	media = "optical\n";	break;
	default:		media = "UNKNOWN\n";	break;
	}
	strcpy(page, media);
	len = strlen(media);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static ide_proc_entry_t generic_drive_entries[] = {
	{ "driver",	S_IFREG|S_IRUGO,	 proc_ide_read_driver,
						 proc_ide_write_driver },
	{ "identify",	S_IFREG|S_IRUSR,	 proc_ide_read_identify, NULL },
	{ "media",	S_IFREG|S_IRUGO,	 proc_ide_read_media,    NULL },
	{ "model",	S_IFREG|S_IRUGO,	 proc_ide_read_dmodel,   NULL },
	{ "settings",	S_IFREG|S_IRUSR|S_IWUSR, proc_ide_read_settings,
						 proc_ide_write_settings },
	{ NULL,	0, NULL, NULL }
};

static void ide_add_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p, void *data)
{
	struct proc_dir_entry *ent;

	if (!dir || !p)
		return;
	while (p->name != NULL) {
		ent = create_proc_entry(p->name, p->mode, dir);
		if (!ent) return;
		ent->data = data;
		ent->read_proc = p->read_proc;
		ent->write_proc = p->write_proc;
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

void ide_proc_register_driver(ide_drive_t *drive, ide_driver_t *driver)
{
	mutex_lock(&ide_setting_mtx);
	drive->settings = driver->settings;
	mutex_unlock(&ide_setting_mtx);

	ide_add_proc_entries(drive->proc, driver->proc, drive);
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
 *	Takes ide_setting_mtx and ide_lock.
 *	Caller must hold none of the locks.
 */

void ide_proc_unregister_driver(ide_drive_t *drive, ide_driver_t *driver)
{
	unsigned long flags;

	ide_remove_proc_entries(drive->proc, driver->proc);

	mutex_lock(&ide_setting_mtx);
	spin_lock_irqsave(&ide_lock, flags);
	/*
	 * ide_setting_mtx protects the settings list
	 * ide_lock protects the use of settings
	 *
	 * so we need to hold both, ide_settings_sem because we want to
	 * modify the settings list, and ide_lock because we cannot take
	 * a setting out that is being used.
	 *
	 * OTOH both ide_{read,write}_setting are only ever used under
	 * ide_setting_mtx.
	 */
	drive->settings = NULL;
	spin_unlock_irqrestore(&ide_lock, flags);
	mutex_unlock(&ide_setting_mtx);
}
EXPORT_SYMBOL(ide_proc_unregister_driver);

void ide_proc_port_register_devices(ide_hwif_t *hwif)
{
	int	d;
	struct proc_dir_entry *ent;
	struct proc_dir_entry *parent = hwif->proc;
	char name[64];

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];

		if (!drive->present)
			continue;
		if (drive->proc)
			continue;

		drive->proc = proc_mkdir(drive->name, parent);
		if (drive->proc)
			ide_add_proc_entries(drive->proc, generic_drive_entries, drive);
		sprintf(name, "ide%d/%s", (drive->name[2]-'a')/2, drive->name);
		ent = proc_symlink(drive->name, proc_ide_root, name);
		if (!ent) return;
	}
}

void ide_proc_unregister_device(ide_drive_t *drive)
{
	if (drive->proc) {
		ide_remove_proc_entries(drive->proc, generic_drive_entries);
		remove_proc_entry(drive->name, proc_ide_root);
		remove_proc_entry(drive->name, drive->hwif->proc);
		drive->proc = NULL;
	}
}

static ide_proc_entry_t hwif_entries[] = {
	{ "channel",	S_IFREG|S_IRUGO,	proc_ide_read_channel,	NULL },
	{ "mate",	S_IFREG|S_IRUGO,	proc_ide_read_mate,	NULL },
	{ "model",	S_IFREG|S_IRUGO,	proc_ide_read_imodel,	NULL },
	{ NULL,	0, NULL, NULL }
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
	ide_driver_t *ide_drv = container_of(drv, ide_driver_t, gen_driver);
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

static int ide_drivers_open(struct inode *inode, struct file *file)
{
	return single_open(file, &ide_drivers_show, NULL);
}

static const struct file_operations ide_drivers_operations = {
	.owner		= THIS_MODULE,
	.open		= ide_drivers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void proc_ide_create(void)
{
	proc_ide_root = proc_mkdir("ide", NULL);

	if (!proc_ide_root)
		return;

	proc_create("drivers", 0, proc_ide_root, &ide_drivers_operations);
}

void proc_ide_destroy(void)
{
	remove_proc_entry("drivers", proc_ide_root);
	remove_proc_entry("ide", NULL);
}
