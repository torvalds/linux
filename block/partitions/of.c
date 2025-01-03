// SPDX-License-Identifier: GPL-2.0

#include <linux/blkdev.h>
#include <linux/major.h>
#include <linux/of.h>
#include <linux/string.h>
#include "check.h"

static int validate_of_partition(struct device_node *np, int slot)
{
	u64 offset, size;
	int len;

	const __be32 *reg = of_get_property(np, "reg", &len);
	int a_cells = of_n_addr_cells(np);
	int s_cells = of_n_size_cells(np);

	/* Make sure reg len match the expected addr and size cells */
	if (len / sizeof(*reg) != a_cells + s_cells)
		return -EINVAL;

	/* Validate offset conversion from bytes to sectors */
	offset = of_read_number(reg, a_cells);
	if (offset % SECTOR_SIZE)
		return -EINVAL;

	/* Validate size conversion from bytes to sectors */
	size = of_read_number(reg + a_cells, s_cells);
	if (!size || size % SECTOR_SIZE)
		return -EINVAL;

	return 0;
}

static void add_of_partition(struct parsed_partitions *state, int slot,
			     struct device_node *np)
{
	struct partition_meta_info *info;
	char tmp[sizeof(info->volname) + 4];
	const char *partname;
	int len;

	const __be32 *reg = of_get_property(np, "reg", &len);
	int a_cells = of_n_addr_cells(np);
	int s_cells = of_n_size_cells(np);

	/* Convert bytes to sector size */
	u64 offset = of_read_number(reg, a_cells) / SECTOR_SIZE;
	u64 size = of_read_number(reg + a_cells, s_cells) / SECTOR_SIZE;

	put_partition(state, slot, offset, size);

	if (of_property_read_bool(np, "read-only"))
		state->parts[slot].flags |= ADDPART_FLAG_READONLY;

	/*
	 * Follow MTD label logic, search for label property,
	 * fallback to node name if not found.
	 */
	info = &state->parts[slot].info;
	partname = of_get_property(np, "label", &len);
	if (!partname)
		partname = of_get_property(np, "name", &len);
	strscpy(info->volname, partname, sizeof(info->volname));

	snprintf(tmp, sizeof(tmp), "(%s)", info->volname);
	strlcat(state->pp_buf, tmp, PAGE_SIZE);
}

int of_partition(struct parsed_partitions *state)
{
	struct device *ddev = disk_to_dev(state->disk);
	struct device_node *np;
	int slot;

	struct device_node *partitions_np = of_node_get(ddev->of_node);

	if (!partitions_np ||
	    !of_device_is_compatible(partitions_np, "fixed-partitions"))
		return 0;

	slot = 1;
	/* Validate parition offset and size */
	for_each_child_of_node(partitions_np, np) {
		if (validate_of_partition(np, slot)) {
			of_node_put(np);
			of_node_put(partitions_np);

			return -1;
		}

		slot++;
	}

	slot = 1;
	for_each_child_of_node(partitions_np, np) {
		if (slot >= state->limit) {
			of_node_put(np);
			break;
		}

		add_of_partition(state, slot, np);

		slot++;
	}

	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	return 1;
}
