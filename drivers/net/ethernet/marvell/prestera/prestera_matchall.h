/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2022 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_MATCHALL_H_
#define _PRESTERA_MATCHALL_H_

#include <net/pkt_cls.h>

struct prestera_flow_block;

int prestera_mall_replace(struct prestera_flow_block *block,
			  struct tc_cls_matchall_offload *f);
void prestera_mall_destroy(struct prestera_flow_block *block);
int prestera_mall_prio_get(struct prestera_flow_block *block,
			   u32 *prio_min, u32 *prio_max);

#endif /* _PRESTERA_MATCHALL_H_ */
