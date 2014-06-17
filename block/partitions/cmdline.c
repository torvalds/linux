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
 * For further information, see "Documentation/block/cmdline-partition.txt"
 *
 */

#include <linux/cmdline-parser.h>

#include "check.h"
#include "cmdline.h"

static char *cmdline;
static struct cmdline_parts *bdev_parts;

static int add_part(int slot, struct cmdline_subpart *subpart, void *param)
{
	int label_min;
	struct partition_meta_info *info;
	char tmp[sizeof(info->volname) + 4];
	struct parsed_partitions *state = (struct parsed_partitions *)param;

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

static int __init cmdline_parts_setup(char *s)
{
	cmdline = s;
	return 1;
}
__setup("blkdevparts=", cmdline_parts_setup);

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
	char bdev[BDEVNAME_SIZE];
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

	bdevname(state->bdev, bdev);
	parts = cmdline_parts_find(bdev_parts, bdev);
	if (!parts)
		return 0;

	disk_size = get_capacity(state->bdev->bd_disk) << 9;

	cmdline_parts_set(parts, disk_size, 1, add_part, (void *)state);

	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	return 1;
}
