/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "debug.h"

#undef pr_fmt
#define pr_fmt(fmt)	"qtnfmac dbg: %s: " fmt, __func__

void qtnf_debugfs_init(struct qtnf_bus *bus, const char *name)
{
	bus->dbg_dir = debugfs_create_dir(name, NULL);

	if (IS_ERR_OR_NULL(bus->dbg_dir)) {
		pr_warn("failed to create debugfs root dir\n");
		bus->dbg_dir = NULL;
	}
}

void qtnf_debugfs_remove(struct qtnf_bus *bus)
{
	debugfs_remove_recursive(bus->dbg_dir);
	bus->dbg_dir = NULL;
}

void qtnf_debugfs_add_entry(struct qtnf_bus *bus, const char *name,
			    int (*fn)(struct seq_file *seq, void *data))
{
	struct dentry *entry;

	entry = debugfs_create_devm_seqfile(bus->dev, name, bus->dbg_dir, fn);
	if (IS_ERR_OR_NULL(entry))
		pr_warn("failed to add entry (%s)\n", name);
}
