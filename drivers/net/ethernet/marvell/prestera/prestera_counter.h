/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2021 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_COUNTER_H_
#define _PRESTERA_COUNTER_H_

#include <linux/types.h>

struct prestera_counter_stats {
	u64 packets;
	u64 bytes;
};

struct prestera_switch;
struct prestera_counter;
struct prestera_counter_block;

int prestera_counter_init(struct prestera_switch *sw);
void prestera_counter_fini(struct prestera_switch *sw);

int prestera_counter_get(struct prestera_counter *counter, u32 client,
			 struct prestera_counter_block **block,
			 u32 *counter_id);
void prestera_counter_put(struct prestera_counter *counter,
			  struct prestera_counter_block *block, u32 counter_id);
int prestera_counter_stats_get(struct prestera_counter *counter,
			       struct prestera_counter_block *block,
			       u32 counter_id, u64 *packets, u64 *bytes);

#endif /* _PRESTERA_COUNTER_H_ */
