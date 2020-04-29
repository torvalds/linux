// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include "debug.h"

void qtnf_debugfs_init(struct qtnf_bus *bus, const char *name)
{
	struct dentry *parent = qtnf_get_debugfs_dir();

	bus->dbg_dir = debugfs_create_dir(name, parent);
}

void qtnf_debugfs_remove(struct qtnf_bus *bus)
{
	debugfs_remove_recursive(bus->dbg_dir);
	bus->dbg_dir = NULL;
}

void qtnf_debugfs_add_entry(struct qtnf_bus *bus, const char *name,
			    int (*fn)(struct seq_file *seq, void *data))
{
	debugfs_create_devm_seqfile(bus->dev, name, bus->dbg_dir, fn);
}
