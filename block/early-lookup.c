// SPDX-License-Identifier: GPL-2.0-only
/*
 * Code for looking up block devices in the early boot code before mounting the
 * root file system.  Unfortunately currently also abused in a few other places.
 */
#include <linux/blkdev.h>
#include <linux/ctype.h>

struct uuidcmp {
	const char *uuid;
	int len;
};

/**
 * match_dev_by_uuid - callback for finding a partition using its uuid
 * @dev:	device passed in by the caller
 * @data:	opaque pointer to the desired struct uuidcmp to match
 *
 * Returns 1 if the device matches, and 0 otherwise.
 */
static int match_dev_by_uuid(struct device *dev, const void *data)
{
	struct block_device *bdev = dev_to_bdev(dev);
	const struct uuidcmp *cmp = data;

	if (!bdev->bd_meta_info ||
	    strncasecmp(cmp->uuid, bdev->bd_meta_info->uuid, cmp->len))
		return 0;
	return 1;
}

/**
 * devt_from_partuuid - looks up the dev_t of a partition by its UUID
 * @uuid_str:	char array containing ascii UUID
 *
 * The function will return the first partition which contains a matching
 * UUID value in its partition_meta_info struct.  This does not search
 * by filesystem UUIDs.
 *
 * If @uuid_str is followed by a "/PARTNROFF=%d", then the number will be
 * extracted and used as an offset from the partition identified by the UUID.
 *
 * Returns the matching dev_t on success or 0 on failure.
 */
static int devt_from_partuuid(const char *uuid_str, dev_t *devt)
{
	struct uuidcmp cmp;
	struct device *dev = NULL;
	int offset = 0;
	char *slash;

	cmp.uuid = uuid_str;

	slash = strchr(uuid_str, '/');
	/* Check for optional partition number offset attributes. */
	if (slash) {
		char c = 0;

		/* Explicitly fail on poor PARTUUID syntax. */
		if (sscanf(slash + 1, "PARTNROFF=%d%c", &offset, &c) != 1)
			goto out_invalid;
		cmp.len = slash - uuid_str;
	} else {
		cmp.len = strlen(uuid_str);
	}

	if (!cmp.len)
		goto out_invalid;

	dev = class_find_device(&block_class, NULL, &cmp, &match_dev_by_uuid);
	if (!dev)
		return -ENODEV;

	if (offset) {
		/*
		 * Attempt to find the requested partition by adding an offset
		 * to the partition number found by UUID.
		 */
		*devt = part_devt(dev_to_disk(dev),
				  dev_to_bdev(dev)->bd_partno + offset);
	} else {
		*devt = dev->devt;
	}

	put_device(dev);
	return 0;

out_invalid:
	pr_err("VFS: PARTUUID= is invalid.\n"
	       "Expected PARTUUID=<valid-uuid-id>[/PARTNROFF=%%d]\n");
	return -EINVAL;
}

/**
 * match_dev_by_label - callback for finding a partition using its label
 * @dev:	device passed in by the caller
 * @data:	opaque pointer to the label to match
 *
 * Returns 1 if the device matches, and 0 otherwise.
 */
static int match_dev_by_label(struct device *dev, const void *data)
{
	struct block_device *bdev = dev_to_bdev(dev);
	const char *label = data;

	if (!bdev->bd_meta_info || strcmp(label, bdev->bd_meta_info->volname))
		return 0;
	return 1;
}

static int devt_from_partlabel(const char *label, dev_t *devt)
{
	struct device *dev;

	dev = class_find_device(&block_class, NULL, label, &match_dev_by_label);
	if (!dev)
		return -ENODEV;
	*devt = dev->devt;
	put_device(dev);
	return 0;
}

static int devt_from_devname(const char *name, dev_t *devt)
{
	int part;
	char s[32];
	char *p;

	if (strlen(name) > 31)
		return -EINVAL;
	strcpy(s, name);
	for (p = s; *p; p++) {
		if (*p == '/')
			*p = '!';
	}

	*devt = blk_lookup_devt(s, 0);
	if (*devt)
		return 0;

	/*
	 * Try non-existent, but valid partition, which may only exist after
	 * opening the device, like partitioned md devices.
	 */
	while (p > s && isdigit(p[-1]))
		p--;
	if (p == s || !*p || *p == '0')
		return -EINVAL;

	/* try disk name without <part number> */
	part = simple_strtoul(p, NULL, 10);
	*p = '\0';
	*devt = blk_lookup_devt(s, part);
	if (*devt)
		return 0;

	/* try disk name without p<part number> */
	if (p < s + 2 || !isdigit(p[-2]) || p[-1] != 'p')
		return -EINVAL;
	p[-1] = '\0';
	*devt = blk_lookup_devt(s, part);
	if (*devt)
		return 0;
	return -EINVAL;
}

static int devt_from_devnum(const char *name, dev_t *devt)
{
	unsigned maj, min, offset;
	char *p, dummy;

	if (sscanf(name, "%u:%u%c", &maj, &min, &dummy) == 2 ||
	    sscanf(name, "%u:%u:%u:%c", &maj, &min, &offset, &dummy) == 3) {
		*devt = MKDEV(maj, min);
		if (maj != MAJOR(*devt) || min != MINOR(*devt))
			return -EINVAL;
	} else {
		*devt = new_decode_dev(simple_strtoul(name, &p, 16));
		if (*p)
			return -EINVAL;
	}

	return 0;
}

/*
 *	Convert a name into device number.  We accept the following variants:
 *
 *	1) <hex_major><hex_minor> device number in hexadecimal represents itself
 *         no leading 0x, for example b302.
 *	3) /dev/<disk_name> represents the device number of disk
 *	4) /dev/<disk_name><decimal> represents the device number
 *         of partition - device number of disk plus the partition number
 *	5) /dev/<disk_name>p<decimal> - same as the above, that form is
 *	   used when disk name of partitioned disk ends on a digit.
 *	6) PARTUUID=00112233-4455-6677-8899-AABBCCDDEEFF representing the
 *	   unique id of a partition if the partition table provides it.
 *	   The UUID may be either an EFI/GPT UUID, or refer to an MSDOS
 *	   partition using the format SSSSSSSS-PP, where SSSSSSSS is a zero-
 *	   filled hex representation of the 32-bit "NT disk signature", and PP
 *	   is a zero-filled hex representation of the 1-based partition number.
 *	7) PARTUUID=<UUID>/PARTNROFF=<int> to select a partition in relation to
 *	   a partition with a known unique id.
 *	8) <major>:<minor> major and minor number of the device separated by
 *	   a colon.
 *	9) PARTLABEL=<name> with name being the GPT partition label.
 *	   MSDOS partitions do not support labels!
 *
 *	If name doesn't have fall into the categories above, we return (0,0).
 *	block_class is used to check if something is a disk name. If the disk
 *	name contains slashes, the device name has them replaced with
 *	bangs.
 */
int early_lookup_bdev(const char *name, dev_t *devt)
{
	if (strncmp(name, "PARTUUID=", 9) == 0)
		return devt_from_partuuid(name + 9, devt);
	if (strncmp(name, "PARTLABEL=", 10) == 0)
		return devt_from_partlabel(name + 10, devt);
	if (strncmp(name, "/dev/", 5) == 0)
		return devt_from_devname(name + 5, devt);
	return devt_from_devnum(name, devt);
}
EXPORT_SYMBOL_GPL(early_lookup_bdev);
