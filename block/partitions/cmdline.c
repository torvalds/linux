// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 HUAWEI
 * Author: Cai Zhiyong <caizhiyong@huawei.com>
 *
 * Read block device partition table from the command line.
 * Typically used for fixed block (eMMC) embedded devices.
 * It has no MBR, so saves storage space. Bootloader can be easily accessed
 * by absolute address of data on the block device.
 * Users can easily change the partition.
 *
 * The format for the command line is just like mtdparts.
 *
 * For further information, see "Documentation/block/cmdline-partition.rst"
 *
 */
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "check.h"


/* partition flags */
#define PF_RDONLY                   0x01 /* Device is read only */
#define PF_POWERUP_LOCK             0x02 /* Always locked after reset */

struct cmdline_subpart {
	char name[BDEVNAME_SIZE]; /* partition name, such as 'rootfs' */
	sector_t from;
	sector_t size;
	int flags;
	struct cmdline_subpart *next_subpart;
};

struct cmdline_parts {
	char name[BDEVNAME_SIZE]; /* block device, such as 'mmcblk0' */
	unsigned int nr_subparts;
	struct cmdline_subpart *subpart;
	struct cmdline_parts *next_parts;
};

static int parse_subpart(struct cmdline_subpart **subpart, char *partdef)
{
	int ret = 0;
	struct cmdline_subpart *new_subpart;

	*subpart = NULL;

	new_subpart = kzalloc(sizeof(struct cmdline_subpart), GFP_KERNEL);
	if (!new_subpart)
		return -ENOMEM;

	if (*partdef == '-') {
		new_subpart->size = (sector_t)(~0ULL);
		partdef++;
	} else {
		new_subpart->size = (sector_t)memparse(partdef, &partdef);
		if (new_subpart->size < (sector_t)PAGE_SIZE) {
			pr_warn("cmdline partition size is invalid.");
			ret = -EINVAL;
			goto fail;
		}
	}

	if (*partdef == '@') {
		partdef++;
		new_subpart->from = (sector_t)memparse(partdef, &partdef);
	} else {
		new_subpart->from = (sector_t)(~0ULL);
	}

	if (*partdef == '(') {
		int length;
		char *next = strchr(++partdef, ')');

		if (!next) {
			pr_warn("cmdline partition format is invalid.");
			ret = -EINVAL;
			goto fail;
		}

		length = min_t(int, next - partdef,
			       sizeof(new_subpart->name) - 1);
		strncpy(new_subpart->name, partdef, length);
		new_subpart->name[length] = '\0';

		partdef = ++next;
	} else
		new_subpart->name[0] = '\0';

	new_subpart->flags = 0;

	if (!strncmp(partdef, "ro", 2)) {
		new_subpart->flags |= PF_RDONLY;
		partdef += 2;
	}

	if (!strncmp(partdef, "lk", 2)) {
		new_subpart->flags |= PF_POWERUP_LOCK;
		partdef += 2;
	}

	*subpart = new_subpart;
	return 0;
fail:
	kfree(new_subpart);
	return ret;
}

static void free_subpart(struct cmdline_parts *parts)
{
	struct cmdline_subpart *subpart;

	while (parts->subpart) {
		subpart = parts->subpart;
		parts->subpart = subpart->next_subpart;
		kfree(subpart);
	}
}

static int parse_parts(struct cmdline_parts **parts, const char *bdevdef)
{
	int ret = -EINVAL;
	char *next;
	int length;
	struct cmdline_subpart **next_subpart;
	struct cmdline_parts *newparts;
	char buf[BDEVNAME_SIZE + 32 + 4];

	*parts = NULL;

	newparts = kzalloc(sizeof(struct cmdline_parts), GFP_KERNEL);
	if (!newparts)
		return -ENOMEM;

	next = strchr(bdevdef, ':');
	if (!next) {
		pr_warn("cmdline partition has no block device.");
		goto fail;
	}

	length = min_t(int, next - bdevdef, sizeof(newparts->name) - 1);
	strncpy(newparts->name, bdevdef, length);
	newparts->name[length] = '\0';
	newparts->nr_subparts = 0;

	next_subpart = &newparts->subpart;

	while (next && *(++next)) {
		bdevdef = next;
		next = strchr(bdevdef, ',');

		length = (!next) ? (sizeof(buf) - 1) :
			min_t(int, next - bdevdef, sizeof(buf) - 1);

		strncpy(buf, bdevdef, length);
		buf[length] = '\0';

		ret = parse_subpart(next_subpart, buf);
		if (ret)
			goto fail;

		newparts->nr_subparts++;
		next_subpart = &(*next_subpart)->next_subpart;
	}

	if (!newparts->subpart) {
		pr_warn("cmdline partition has no valid partition.");
		ret = -EINVAL;
		goto fail;
	}

	*parts = newparts;

	return 0;
fail:
	free_subpart(newparts);
	kfree(newparts);
	return ret;
}

static void cmdline_parts_free(struct cmdline_parts **parts)
{
	struct cmdline_parts *next_parts;

	while (*parts) {
		next_parts = (*parts)->next_parts;
		free_subpart(*parts);
		kfree(*parts);
		*parts = next_parts;
	}
}

static int cmdline_parts_parse(struct cmdline_parts **parts,
		const char *cmdline)
{
	int ret;
	char *buf;
	char *pbuf;
	char *next;
	struct cmdline_parts **next_parts;

	*parts = NULL;

	next = pbuf = buf = kstrdup(cmdline, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	next_parts = parts;

	while (next && *pbuf) {
		next = strchr(pbuf, ';');
		if (next)
			*next = '\0';

		ret = parse_parts(next_parts, pbuf);
		if (ret)
			goto fail;

		if (next)
			pbuf = ++next;

		next_parts = &(*next_parts)->next_parts;
	}

	if (!*parts) {
		pr_warn("cmdline partition has no valid partition.");
		ret = -EINVAL;
		goto fail;
	}

	ret = 0;
done:
	kfree(buf);
	return ret;

fail:
	cmdline_parts_free(parts);
	goto done;
}

static struct cmdline_parts *cmdline_parts_find(struct cmdline_parts *parts,
					 const char *bdev)
{
	while (parts && strncmp(bdev, parts->name, sizeof(parts->name)))
		parts = parts->next_parts;
	return parts;
}

static char *cmdline;
static struct cmdline_parts *bdev_parts;

static int add_part(int slot, struct cmdline_subpart *subpart,
		struct parsed_partitions *state)
{
	int label_min;
	struct partition_meta_info *info;
	char tmp[sizeof(info->volname) + 4];

	if (slot >= state->limit)
		return 1;

	put_partition(state, slot, subpart->from >> 9,
		      subpart->size >> 9);

	info = &state->parts[slot].info;

	label_min = min_t(int, sizeof(info->volname) - 1,
			  sizeof(subpart->name));
	strncpy(info->volname, subpart->name, label_min);
	info->volname[label_min] = '\0';

	snprintf(tmp, sizeof(tmp), "(%s)", info->volname);
	strlcat(state->pp_buf, tmp, PAGE_SIZE);

	state->parts[slot].has_info = true;

	return 0;
}

static int cmdline_parts_set(struct cmdline_parts *parts, sector_t disk_size,
		struct parsed_partitions *state)
{
	sector_t from = 0;
	struct cmdline_subpart *subpart;
	int slot = 1;

	for (subpart = parts->subpart; subpart;
	     subpart = subpart->next_subpart, slot++) {
		if (subpart->from == (sector_t)(~0ULL))
			subpart->from = from;
		else
			from = subpart->from;

		if (from >= disk_size)
			break;

		if (subpart->size > (disk_size - from))
			subpart->size = disk_size - from;

		from += subpart->size;

		if (add_part(slot, subpart, state))
			break;
	}

	return slot;
}

static int __init cmdline_parts_setup(char *s)
{
	cmdline = s;
	return 1;
}
__setup("blkdevparts=", cmdline_parts_setup);

static bool has_overlaps(sector_t from, sector_t size,
			 sector_t from2, sector_t size2)
{
	sector_t end = from + size;
	sector_t end2 = from2 + size2;

	if (from >= from2 && from < end2)
		return true;

	if (end > from2 && end <= end2)
		return true;

	if (from2 >= from && from2 < end)
		return true;

	if (end2 > from && end2 <= end)
		return true;

	return false;
}

static inline void overlaps_warns_header(void)
{
	pr_warn("Overlapping partitions are used in command line partitions.");
	pr_warn("Don't use filesystems on overlapping partitions:");
}

static void cmdline_parts_verifier(int slot, struct parsed_partitions *state)
{
	int i;
	bool header = true;

	for (; slot < state->limit && state->parts[slot].has_info; slot++) {
		for (i = slot+1; i < state->limit && state->parts[i].has_info;
		     i++) {
			if (has_overlaps(state->parts[slot].from,
					 state->parts[slot].size,
					 state->parts[i].from,
					 state->parts[i].size)) {
				if (header) {
					header = false;
					overlaps_warns_header();
				}
				pr_warn("%s[%llu,%llu] overlaps with "
					"%s[%llu,%llu].",
					state->parts[slot].info.volname,
					(u64)state->parts[slot].from << 9,
					(u64)state->parts[slot].size << 9,
					state->parts[i].info.volname,
					(u64)state->parts[i].from << 9,
					(u64)state->parts[i].size << 9);
			}
		}
	}
}

/*
 * Purpose: allocate cmdline partitions.
 * Returns:
 * -1 if unable to read the partition table
 *  0 if this isn't our partition table
 *  1 if successful
 */
int cmdline_partition(struct parsed_partitions *state)
{
	sector_t disk_size;
	struct cmdline_parts *parts;

	if (cmdline) {
		if (bdev_parts)
			cmdline_parts_free(&bdev_parts);

		if (cmdline_parts_parse(&bdev_parts, cmdline)) {
			cmdline = NULL;
			return -1;
		}
		cmdline = NULL;
	}

	if (!bdev_parts)
		return 0;

	parts = cmdline_parts_find(bdev_parts, state->disk->disk_name);
	if (!parts)
		return 0;

	disk_size = get_capacity(state->disk) << 9;

	cmdline_parts_set(parts, disk_size, state);
	cmdline_parts_verifier(1, state);

	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	return 1;
}
