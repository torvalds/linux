/*
 *  linux/drivers/ide/ide-proc.c	Version 1.05	Mar 05, 2003
 *
 *  Copyright (C) 1997-1998	Mark Lord
 *  Copyright (C) 2003		Red Hat <alan@redhat.com>
 */

/*
 * This is the /proc/ide/ filesystem implementation.
 *
 * Drive/Driver settings can be retrieved by reading the drive's
 * "settings" files.  e.g.    "cat /proc/ide0/hda/settings"
 * To write a new value "val" into a specific setting "name", use:
 *   echo "name:val" >/proc/ide/ide0/hda/settings
 *
 * Also useful, "cat /proc/ide0/hda/[identify, smart_values,
 * smart_thresholds, capabilities]" will issue an IDENTIFY /
 * PACKET_IDENTIFY / SMART_READ_VALUES / SMART_READ_THRESHOLDS /
 * SENSE CAPABILITIES command to /dev/hda, and then dump out the
 * returned data as 256 16-bit words.  The "hdparm" utility will
 * be updated someday soon to use this mechanism.
 *
 */

#include <linux/module.h>

#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ctype.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/seq_file.h>

#include <asm/io.h>

static int proc_ide_read_imodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;
	const char	*name;

	/*
	 * Neither ide_unknown nor ide_forced should be set at this point.
	 */
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
		default:		name = "(unknown)";	break;
	}
	len = sprintf(page, "%s\n", name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_mate
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	if (hwif && hwif->mate && hwif->mate->present)
		len = sprintf(page, "%s\n", hwif->mate->name);
	else
		len = sprintf(page, "(none)\n");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_channel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_hwif_t	*hwif = (ide_hwif_t *) data;
	int		len;

	page[0] = hwif->channel ? '1' : '0';
	page[1] = '\n';
	len = 2;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_ide_read_identify
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;
	int		err = 0;

	len = sprintf(page, "\n");

	if (drive) {
		unsigned short *val = (unsigned short *) page;

		err = taskfile_lib_get_identify(drive, page);
		if (!err) {
			char *out = ((char *)page) + (SECTOR_WORDS * 4);
			page = out;
			do {
				out += sprintf(out, "%04x%c",
					le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
				val += 1;
			} while (i < (SECTOR_WORDS * 2));
			len = out - page;
		}
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static void proc_ide_settings_warn(void)
{
	static int warned = 0;

	if (warned)
		return;

	printk(KERN_WARNING "Warning: /proc/ide/hd?/settings interface is "
			    "obsolete, and will be removed soon!\n");
	warned = 1;
}

static int proc_ide_read_settings
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	ide_settings_t	*setting = (ide_settings_t *) drive->settings;
	char		*out = page;
	int		len, rc, mul_factor, div_factor;

	proc_ide_settings_warn();

	down(&ide_setting_sem);
	out += sprintf(out, "name\t\t\tvalue\t\tmin\t\tmax\t\tmode\n");
	out += sprintf(out, "----\t\t\t-----\t\t---\t\t---\t\t----\n");
	while(setting) {
		mul_factor = setting->mul_factor;
		div_factor = setting->div_factor;
		out += sprintf(out, "%-24s", setting->name);
		if ((rc = ide_read_setting(drive, setting)) >= 0)
			out += sprintf(out, "%-16d", rc * mul_factor / div_factor);
		else
			out += sprintf(out, "%-16s", "write-only");
		out += sprintf(out, "%-16d%-16d", (setting->min * mul_factor + div_factor - 1) / div_factor, setting->max * mul_factor / div_factor);
		if (setting->rw & SETTING_READ)
			out += sprintf(out, "r");
		if (setting->rw & SETTING_WRITE)
			out += sprintf(out, "w");
		out += sprintf(out, "\n");
		setting = setting->next;
	}
	len = out - page;
	up(&ide_setting_sem);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

#define MAX_LEN	30

static int proc_ide_write_settings(struct file *file, const char __user *buffer,
				   unsigned long count, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		name[MAX_LEN + 1];
	int		for_real = 0;
	unsigned long	n;
	ide_settings_t	*setting;
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

			down(&ide_setting_sem);
			setting = ide_find_setting_by_name(drive, name);
			if (!setting)
			{
				up(&ide_setting_sem);
				goto parse_error;
			}
			if (for_real)
				ide_write_setting(drive, setting, val * setting->div_factor / setting->mul_factor);
			up(&ide_setting_sem);
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
	int len = sprintf(page,"%llu\n", (long long)0x7fffffff);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

EXPORT_SYMBOL_GPL(proc_ide_read_capacity);

int proc_ide_read_geometry
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	out += sprintf(out,"physical     %d/%d/%d\n",
			drive->cyl, drive->head, drive->sect);
	out += sprintf(out,"logical      %d/%d/%d\n",
			drive->bios_cyl, drive->bios_head, drive->bios_sect);

	len = out - page;
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

EXPORT_SYMBOL(proc_ide_read_geometry);

static int proc_ide_read_dmodel
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	struct hd_driveid *id = drive->id;
	int		len;

	len = sprintf(page, "%.40s\n",
		(id && id->model[0]) ? (char *)id->model : "(none)");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
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
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
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
			__FUNCTION__, err);
	drive->driver_req[0] = 0;
	if (dev->driver == NULL) {
		err = device_attach(dev);
		if (err < 0)
			printk(KERN_WARNING
				"IDE: %s: device_attach(2) error: %d\n",
				__FUNCTION__, err);
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
		case ide_disk:	media = "disk\n";
				break;
		case ide_cdrom:	media = "cdrom\n";
				break;
		case ide_tape:	media = "tape\n";
				break;
		case ide_floppy:media = "floppy\n";
				break;
		case ide_optical:media = "optical\n";
				break;
		default:	media = "UNKNOWN\n";
				break;
	}
	strcpy(page,media);
	len = strlen(media);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t generic_drive_entries[] = {
	{ "driver",	S_IFREG|S_IRUGO,	proc_ide_read_driver,	proc_ide_write_driver },
	{ "identify",	S_IFREG|S_IRUSR,	proc_ide_read_identify,	NULL },
	{ "media",	S_IFREG|S_IRUGO,	proc_ide_read_media,	NULL },
	{ "model",	S_IFREG|S_IRUGO,	proc_ide_read_dmodel,	NULL },
	{ "settings",	S_IFREG|S_IRUSR|S_IWUSR,proc_ide_read_settings,	proc_ide_write_settings },
	{ NULL,	0, NULL, NULL }
};

void ide_add_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p, void *data)
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

void ide_remove_proc_entries(struct proc_dir_entry *dir, ide_proc_entry_t *p)
{
	if (!dir || !p)
		return;
	while (p->name != NULL) {
		remove_proc_entry(p->name, dir);
		p++;
	}
}

static void create_proc_ide_drives(ide_hwif_t *hwif)
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
		sprintf(name,"ide%d/%s", (drive->name[2]-'a')/2, drive->name);
		ent = proc_symlink(drive->name, proc_ide_root, name);
		if (!ent) return;
	}
}

static void destroy_proc_ide_device(ide_hwif_t *hwif, ide_drive_t *drive)
{
	if (drive->proc) {
		ide_remove_proc_entries(drive->proc, generic_drive_entries);
		remove_proc_entry(drive->name, proc_ide_root);
		remove_proc_entry(drive->name, hwif->proc);
		drive->proc = NULL;
	}
}

static void destroy_proc_ide_drives(ide_hwif_t *hwif)
{
	int	d;

	for (d = 0; d < MAX_DRIVES; d++) {
		ide_drive_t *drive = &hwif->drives[d];
		if (drive->proc)
			destroy_proc_ide_device(hwif, drive);
	}
}

static ide_proc_entry_t hwif_entries[] = {
	{ "channel",	S_IFREG|S_IRUGO,	proc_ide_read_channel,	NULL },
	{ "mate",	S_IFREG|S_IRUGO,	proc_ide_read_mate,	NULL },
	{ "model",	S_IFREG|S_IRUGO,	proc_ide_read_imodel,	NULL },
	{ NULL,	0, NULL, NULL }
};

void create_proc_ide_interfaces(void)
{
	int	h;

	for (h = 0; h < MAX_HWIFS; h++) {
		ide_hwif_t *hwif = &ide_hwifs[h];

		if (!hwif->present)
			continue;
		if (!hwif->proc) {
			hwif->proc = proc_mkdir(hwif->name, proc_ide_root);
			if (!hwif->proc)
				return;
			ide_add_proc_entries(hwif->proc, hwif_entries, hwif);
		}
		create_proc_ide_drives(hwif);
	}
}

EXPORT_SYMBOL(create_proc_ide_interfaces);

#ifdef CONFIG_BLK_DEV_IDEPCI
void ide_pci_create_host_proc(const char *name, get_info_t *get_info)
{
	create_proc_info_entry(name, 0, proc_ide_root, get_info);
}

EXPORT_SYMBOL_GPL(ide_pci_create_host_proc);
#endif

void destroy_proc_ide_interface(ide_hwif_t *hwif)
{
	if (hwif->proc) {
		destroy_proc_ide_drives(hwif);
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
			__FUNCTION__, err);
	return 0;
}

static int ide_drivers_open(struct inode *inode, struct file *file)
{
	return single_open(file, &ide_drivers_show, NULL);
}

static const struct file_operations ide_drivers_operations = {
	.open		= ide_drivers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void proc_ide_create(void)
{
	struct proc_dir_entry *entry;

	if (!proc_ide_root)
		return;

	create_proc_ide_interfaces();

	entry = create_proc_entry("drivers", 0, proc_ide_root);
	if (entry)
		entry->proc_fops = &ide_drivers_operations;
}

void proc_ide_destroy(void)
{
	remove_proc_entry("drivers", proc_ide_root);
	remove_proc_entry("ide", NULL);
}
