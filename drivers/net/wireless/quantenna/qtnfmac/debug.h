/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#ifndef _QTN_FMAC_DE_H_
#define _QTN_FMAC_DE_H_

#include <linux/defs.h>

#include "core.h"
#include "bus.h"

#ifdef CONFIG_DE_FS

void qtnf_defs_init(struct qtnf_bus *bus, const char *name);
void qtnf_defs_remove(struct qtnf_bus *bus);
void qtnf_defs_add_entry(struct qtnf_bus *bus, const char *name,
			    int (*fn)(struct seq_file *seq, void *data));

#else

static inline void qtnf_defs_init(struct qtnf_bus *bus, const char *name)
{
}

static inline void qtnf_defs_remove(struct qtnf_bus *bus)
{
}

static inline void
qtnf_defs_add_entry(struct qtnf_bus *bus, const char *name,
		       int (*fn)(struct seq_file *seq, void *data))
{
}

#endif /* CONFIG_DE_FS */

#endif /* _QTN_FMAC_DE_H_ */
