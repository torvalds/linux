/*
 * Copyright 2004 Peter M. Jones <pjones@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */

#include <linux/list.h>
#include <linux/genhd.h>
#include <linux/spinlock.h>
#include <linux/capability.h>
#include <linux/bitops.h>

#include <scsi/scsi.h>
#include <linux/cdrom.h>

int blk_verify_command(struct blk_cmd_filter *filter,
		       unsigned char *cmd, fmode_t has_write_perm)
{
	/* root can do any command. */
	if (capable(CAP_SYS_RAWIO))
		return 0;

	/* if there's no filter set, assume we're filtering everything out */
	if (!filter)
		return -EPERM;

	/* Anybody who can open the device can do a read-safe command */
	if (test_bit(cmd[0], filter->read_ok))
		return 0;

	/* Write-safe commands require a writable open */
	if (test_bit(cmd[0], filter->write_ok) && has_write_perm)
		return 0;

	return -EPERM;
}
EXPORT_SYMBOL(blk_verify_command);

#if 0
/* and now, the sysfs stuff */
static ssize_t rcf_cmds_show(struct blk_cmd_filter *filter, char *page,
			     int rw)
{
	char *npage = page;
	unsigned long *okbits;
	int i;

	if (rw == READ)
		okbits = filter->read_ok;
	else
		okbits = filter->write_ok;

	for (i = 0; i < BLK_SCSI_MAX_CMDS; i++) {
		if (test_bit(i, okbits)) {
			npage += sprintf(npage, "0x%02x", i);
			if (i < BLK_SCSI_MAX_CMDS - 1)
				sprintf(npage++, " ");
		}
	}

	if (npage != page)
		npage += sprintf(npage, "\n");

	return npage - page;
}

static ssize_t rcf_readcmds_show(struct blk_cmd_filter *filter, char *page)
{
	return rcf_cmds_show(filter, page, READ);
}

static ssize_t rcf_writecmds_show(struct blk_cmd_filter *filter,
				 char *page)
{
	return rcf_cmds_show(filter, page, WRITE);
}

static ssize_t rcf_cmds_store(struct blk_cmd_filter *filter,
			      const char *page, size_t count, int rw)
{
	unsigned long okbits[BLK_SCSI_CMD_PER_LONG], *target_okbits;
	int cmd, set;
	char *p, *status;

	if (rw == READ) {
		memcpy(&okbits, filter->read_ok, sizeof(okbits));
		target_okbits = filter->read_ok;
	} else {
		memcpy(&okbits, filter->write_ok, sizeof(okbits));
		target_okbits = filter->write_ok;
	}

	while ((p = strsep((char **)&page, " ")) != NULL) {
		set = 1;

		if (p[0] == '+') {
			p++;
		} else if (p[0] == '-') {
			set = 0;
			p++;
		}

		cmd = simple_strtol(p, &status, 16);

		/* either of these cases means invalid input, so do nothing. */
		if ((status == p) || cmd >= BLK_SCSI_MAX_CMDS)
			return -EINVAL;

		if (set)
			__set_bit(cmd, okbits);
		else
			__clear_bit(cmd, okbits);
	}

	memcpy(target_okbits, okbits, sizeof(okbits));
	return count;
}

static ssize_t rcf_readcmds_store(struct blk_cmd_filter *filter,
				  const char *page, size_t count)
{
	return rcf_cmds_store(filter, page, count, READ);
}

static ssize_t rcf_writecmds_store(struct blk_cmd_filter *filter,
				   const char *page, size_t count)
{
	return rcf_cmds_store(filter, page, count, WRITE);
}

struct rcf_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_cmd_filter *, char *);
	ssize_t (*store)(struct blk_cmd_filter *, const char *, size_t);
};

static struct rcf_sysfs_entry rcf_readcmds_entry = {
	.attr = { .name = "read_table", .mode = S_IRUGO | S_IWUSR },
	.show = rcf_readcmds_show,
	.store = rcf_readcmds_store,
};

static struct rcf_sysfs_entry rcf_writecmds_entry = {
	.attr = {.name = "write_table", .mode = S_IRUGO | S_IWUSR },
	.show = rcf_writecmds_show,
	.store = rcf_writecmds_store,
};

static struct attribute *default_attrs[] = {
	&rcf_readcmds_entry.attr,
	&rcf_writecmds_entry.attr,
	NULL,
};

#define to_rcf(atr) container_of((atr), struct rcf_sysfs_entry, attr)

static ssize_t
rcf_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct rcf_sysfs_entry *entry = to_rcf(attr);
	struct blk_cmd_filter *filter;

	filter = container_of(kobj, struct blk_cmd_filter, kobj);
	if (entry->show)
		return entry->show(filter, page);

	return 0;
}

static ssize_t
rcf_attr_store(struct kobject *kobj, struct attribute *attr,
			const char *page, size_t length)
{
	struct rcf_sysfs_entry *entry = to_rcf(attr);
	struct blk_cmd_filter *filter;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (!entry->store)
		return -EINVAL;

	filter = container_of(kobj, struct blk_cmd_filter, kobj);
	return entry->store(filter, page, length);
}

static struct sysfs_ops rcf_sysfs_ops = {
	.show = rcf_attr_show,
	.store = rcf_attr_store,
};

static struct kobj_type rcf_ktype = {
	.sysfs_ops = &rcf_sysfs_ops,
	.default_attrs = default_attrs,
};

int blk_register_filter(struct gendisk *disk)
{
	int ret;
	struct blk_cmd_filter *filter = &disk->queue->cmd_filter;

	ret = kobject_init_and_add(&filter->kobj, &rcf_ktype,
				   &disk_to_dev(disk)->kobj,
				   "%s", "cmd_filter");
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(blk_register_filter);

void blk_unregister_filter(struct gendisk *disk)
{
	struct blk_cmd_filter *filter = &disk->queue->cmd_filter;

	kobject_put(&filter->kobj);
}
EXPORT_SYMBOL(blk_unregister_filter);
#endif
