// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include "de.h"

void qtnf_defs_init(struct qtnf_bus *bus, const char *name)
{
	bus->dbg_dir = defs_create_dir(name, NULL);
}

void qtnf_defs_remove(struct qtnf_bus *bus)
{
	defs_remove_recursive(bus->dbg_dir);
	bus->dbg_dir = NULL;
}

void qtnf_defs_add_entry(struct qtnf_bus *bus, const char *name,
			    int (*fn)(struct seq_file *seq, void *data))
{
	defs_create_devm_seqfile(bus->dev, name, bus->dbg_dir, fn);
}
