/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_DYNAMIC_CONFIG_H
#define _SJA1105_DYNAMIC_CONFIG_H

#include "sja1105.h"
#include <linux/packing.h>

/* Special index that can be used for sja1105_dynamic_config_read */
#define SJA1105_SEARCH		-1

struct sja1105_dyn_cmd;

struct sja1105_dynamic_table_ops {
	/* This returns size_t just to keep same prototype as the
	 * static config ops, of which we are reusing some functions.
	 */
	size_t (*entry_packing)(void *buf, void *entry_ptr, enum packing_op op);
	void (*cmd_packing)(void *buf, struct sja1105_dyn_cmd *cmd,
			    enum packing_op op);
	size_t max_entry_count;
	size_t packed_size;
	u64 addr;
	u8 access;
};

struct sja1105_mgmt_entry {
	u64 tsreg;
	u64 takets;
	u64 macaddr;
	u64 destports;
	u64 enfport;
	u64 index;
};

extern struct sja1105_dynamic_table_ops sja1105et_dyn_ops[BLK_IDX_MAX_DYN];
extern struct sja1105_dynamic_table_ops sja1105pqrs_dyn_ops[BLK_IDX_MAX_DYN];

#endif
