// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtual concat MTD device driver
 *
 * Copyright (C) 2018 Bernhard Frauendienst
 * Author: Bernhard Frauendienst <kernel@nospam.obeliks.de>
 */

#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include "mtdcore.h"
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/mtd/concat.h>

#define CONCAT_PROP "part-concat-next"
#define CONCAT_POSTFIX "concat"
#define MIN_DEV_PER_CONCAT 1

static LIST_HEAD(concat_node_list);

/**
 * struct mtd_virt_concat_node - components of a concatenation
 * @head: List handle
 * @count: Number of nodes
 * @nodes: Pointer to the nodes (partitions) to concatenate
 * @concat: Concatenation container
 */
struct mtd_virt_concat_node {
	struct list_head head;
	unsigned int count;
	struct mtd_concat *concat;
	struct device_node *nodes[] __counted_by(count);
};

/**
 * mtd_is_part_concat - Check if the device is already part
 *                       of a concatenated device
 * @dev:        pointer to 'device_node'
 *
 * Return: true if the device is already part of a concatenation,
 *         false otherwise.
 */
static bool mtd_is_part_concat(struct device_node *dev)
{
	struct mtd_virt_concat_node *item;
	int idx;

	list_for_each_entry(item, &concat_node_list, head) {
		for (idx = 0; idx < item->count; idx++) {
			if (item->nodes[idx] == dev)
				return true;
		}
	}
	return false;
}

static void mtd_virt_concat_put_mtd_devices(struct mtd_concat *concat)
{
	int i;

	for (i = 0; i < concat->num_subdev; i++)
		put_mtd_device(concat->subdev[i]);
}

void mtd_virt_concat_destroy_joins(void)
{
	struct mtd_virt_concat_node *item, *tmp;
	struct mtd_info *mtd;

	list_for_each_entry_safe(item, tmp, &concat_node_list, head) {
		mtd = &item->concat->mtd;
		if (item->concat) {
			mtd_device_unregister(mtd);
			kfree(mtd->name);
			mtd_concat_destroy(mtd);
			mtd_virt_concat_put_mtd_devices(item->concat);
		}
	}
}

/**
 * mtd_virt_concat_destroy - Destroy the concat that includes the mtd object
 * @mtd:        pointer to 'mtd_info'
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_destroy(struct mtd_info *mtd)
{
	struct mtd_info *child, *master = mtd_get_master(mtd);
	struct mtd_virt_concat_node *item, *tmp;
	struct mtd_concat *concat;
	int idx, ret = 0;
	bool is_mtd_found;

	list_for_each_entry_safe(item, tmp, &concat_node_list, head) {
		is_mtd_found = false;

		/* Find the concat item that hold the mtd device */
		for (idx = 0; idx < item->count; idx++) {
			if (item->nodes[idx] == mtd->dev.of_node) {
				is_mtd_found = true;
				break;
			}
		}
		if (!is_mtd_found)
			continue;
		concat = item->concat;

		/*
		 * Since this concatenated device is being removed, retrieve
		 * all MTD devices that are part of it and register them
		 * individually.
		 */
		for (idx = 0; idx < concat->num_subdev; idx++) {
			child = concat->subdev[idx];
			if (child->dev.of_node != mtd->dev.of_node) {
				ret = add_mtd_device(child);
				if (ret)
					goto out;
			}
		}
		/* Destroy the concat */
		if (concat->mtd.name) {
			del_mtd_device(&concat->mtd);
			kfree(concat->mtd.name);
			mtd_concat_destroy(&concat->mtd);
			mtd_virt_concat_put_mtd_devices(item->concat);
		}

		for (idx = 0; idx < item->count; idx++)
			of_node_put(item->nodes[idx]);

		kfree(item);
	}
	return 0;
out:
	mutex_lock(&master->master.partitions_lock);
	list_del(&child->part.node);
	mutex_unlock(&master->master.partitions_lock);
	kfree(mtd->name);
	kfree(mtd);

	return ret;
}

/**
 * mtd_virt_concat_create_item - Create a concat item
 * @parts:        pointer to 'device_node'
 * @count:        number of mtd devices that make up
 *                the concatenated device.
 *
 * Return: 0 on success, -error otherwise.
 */
static int mtd_virt_concat_create_item(struct device_node *parts,
				       unsigned int count)
{
	struct mtd_virt_concat_node *item;
	struct mtd_concat *concat;
	int i;

	for (i = 0; i < (count - 1); i++) {
		if (mtd_is_part_concat(of_parse_phandle(parts, CONCAT_PROP, i)))
			return 0;
	}

	item = kzalloc_flex(*item, nodes, count, GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->count = count;

	/*
	 * The partition in which "part-concat-next" property
	 * is defined is the first device in the list of concat
	 * devices.
	 */
	item->nodes[0] = parts;

	for (i = 1; i < count; i++)
		item->nodes[i] = of_parse_phandle(parts, CONCAT_PROP, (i - 1));

	concat = kzalloc_flex(*concat, subdev, count, GFP_KERNEL);
	if (!concat) {
		kfree(item);
		return -ENOMEM;
	}

	item->concat = concat;

	list_add_tail(&item->head, &concat_node_list);

	return 0;
}

void mtd_virt_concat_destroy_items(void)
{
	struct mtd_virt_concat_node *item, *temp;
	int i;

	list_for_each_entry_safe(item, temp, &concat_node_list, head) {
		for (i = 0; i < item->count; i++)
			of_node_put(item->nodes[i]);

		kfree(item);
	}
}

/**
 * mtd_virt_concat_add - Add a mtd device to the concat list
 * @mtd:        pointer to 'mtd_info'
 *
 * Return: true on success, false otherwise.
 */
bool mtd_virt_concat_add(struct mtd_info *mtd)
{
	struct mtd_virt_concat_node *item;
	struct mtd_concat *concat;
	int idx;

	list_for_each_entry(item, &concat_node_list, head) {
		concat = item->concat;
		for (idx = 0; idx < item->count; idx++) {
			if (item->nodes[idx] == mtd->dev.of_node) {
				concat->subdev[concat->num_subdev++] = mtd;
				return true;
			}
		}
	}
	return false;
}

/**
 * mtd_virt_concat_node_create - List all the concatenations found in DT
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_node_create(void)
{
	struct device_node *parts = NULL;
	int ret = 0, count = 0;

	/* List all the concatenations found in DT */
	do {
		parts = of_find_node_with_property(parts, CONCAT_PROP);
		if (!of_device_is_available(parts))
			continue;

		if (mtd_is_part_concat(parts))
			continue;

		count = of_count_phandle_with_args(parts, CONCAT_PROP, NULL);
		if (count < MIN_DEV_PER_CONCAT)
			continue;

		/*
		 * The partition in which "part-concat-next" property is defined
		 * is also part of the concat device, so increament count by 1.
		 */
		count++;

		ret = mtd_virt_concat_create_item(parts, count);
		if (ret) {
			of_node_put(parts);
			goto destroy_items;
		}
	} while (parts);

	return ret;

destroy_items:
	mtd_virt_concat_destroy_items();

	return ret;
}

/**
 * mtd_virt_concat_create_join - Create and register the concatenated
 *                                 MTD device.
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_create_join(void)
{
	struct mtd_virt_concat_node *item;
	struct mtd_concat *concat;
	struct mtd_info *mtd;
	ssize_t name_sz;
	int ret, idx;
	char *name;

	list_for_each_entry(item, &concat_node_list, head) {
		concat = item->concat;
		/*
		 * Check if item->count != concat->num_subdev, it indicates
		 * that the MTD information for all devices included in the
		 * concatenation are not handy, concat MTD device can't be
		 * created hence switch to next concat device.
		 */
		if (item->count != concat->num_subdev) {
			continue;
		} else {
			/* Calculate the legth of the name of the virtual device */
			for (idx = 0, name_sz = 0; idx < concat->num_subdev; idx++)
				name_sz += (strlen(concat->subdev[idx]->name) + 1);
			name_sz += strlen(CONCAT_POSTFIX);
			name = kmalloc(name_sz + 1, GFP_KERNEL);
			if (!name) {
				mtd_virt_concat_put_mtd_devices(concat);
				return -ENOMEM;
			}

			ret = 0;
			for (idx = 0; idx < concat->num_subdev; idx++) {
				ret += sprintf((name + ret), "%s-",
					       concat->subdev[idx]->name);
			}
			sprintf((name + ret), CONCAT_POSTFIX);

			if (concat->mtd.name) {
				ret = memcmp(concat->mtd.name, name, name_sz);
				if (ret == 0)
					continue;
			}
			mtd = mtd_concat_create(concat->subdev, concat->num_subdev, name);
			if (!mtd) {
				kfree(name);
				return -ENXIO;
			}
			concat->mtd = *mtd;
			/* Arbitrary set the first device as parent */
			concat->mtd.dev.parent = concat->subdev[0]->dev.parent;
			concat->mtd.dev = concat->subdev[0]->dev;

			/* Add the mtd device */
			ret = add_mtd_device(&concat->mtd);
			if (ret)
				goto destroy_concat;
		}
	}

	return 0;

destroy_concat:
	mtd_concat_destroy(mtd);

	return ret;
}
