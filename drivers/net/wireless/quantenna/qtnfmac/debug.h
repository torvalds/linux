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

#ifndef _QTN_FMAC_DEBUG_H_
#define _QTN_FMAC_DEBUG_H_

#include <linux/debugfs.h>

#include "core.h"
#include "bus.h"

#ifdef CONFIG_DEBUG_FS

void qtnf_debugfs_init(struct qtnf_bus *bus, const char *name);
void qtnf_debugfs_remove(struct qtnf_bus *bus);
void qtnf_debugfs_add_entry(struct qtnf_bus *bus, const char *name,
			    int (*fn)(struct seq_file *seq, void *data));

#else

static inline void qtnf_debugfs_init(struct qtnf_bus *bus, const char *name)
{
}

static inline void qtnf_debugfs_remove(struct qtnf_bus *bus)
{
}

static inline void
qtnf_debugfs_add_entry(struct qtnf_bus *bus, const char *name,
		       int (*fn)(struct seq_file *seq, void *data))
{
}

#endif /* CONFIG_DEBUG_FS */

#endif /* _QTN_FMAC_DEBUG_H_ */
