/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File aq_rss.h: Receive Side Scaling definitions. */

#ifndef AQ_RSS_H
#define AQ_RSS_H

#include "aq_common.h"
#include "aq_cfg.h"

struct aq_rss_parameters {
	u16 base_cpu_number;
	u16 indirection_table_size;
	u16 hash_secret_key_size;
	u32 hash_secret_key[AQ_CFG_RSS_HASHKEY_SIZE / sizeof(u32)];
	u8 indirection_table[AQ_CFG_RSS_INDIRECTION_TABLE_MAX];
};

#endif /* AQ_RSS_H */
