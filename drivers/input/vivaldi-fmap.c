// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for ChromeOS Vivaldi keyboard function row mapping
 *
 * Copyright (C) 2022 Google, Inc
 */

#include <linux/export.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

/**
 * vivaldi_function_row_physmap_show - Print vivaldi function row physmap attribute
 * @data: The vivaldi function row map
 * @buf: Buffer to print the function row phsymap to
 */
ssize_t vivaldi_function_row_physmap_show(const struct vivaldi_data *data,
					  char *buf)
{
	ssize_t size = 0;
	int i;
	const u32 *physmap = data->function_row_physmap;

	if (!data->num_function_row_keys)
		return 0;

	for (i = 0; i < data->num_function_row_keys; i++)
		size += sysfs_emit_at(buf, size,
				      "%s%02X", size ? " " : "", physmap[i]);
	if (size)
		size += sysfs_emit_at(buf, size, "\n");

	return size;
}
EXPORT_SYMBOL_GPL(vivaldi_function_row_physmap_show);

MODULE_LICENSE("GPL");
