/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020-2021 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_FLOWER_H_
#define _PRESTERA_FLOWER_H_

#include <net/pkt_cls.h>

struct prestera_switch;
struct prestera_flow_block;

int prestera_flower_replace(struct prestera_flow_block *block,
			    struct flow_cls_offload *f);
void prestera_flower_destroy(struct prestera_flow_block *block,
			     struct flow_cls_offload *f);
int prestera_flower_stats(struct prestera_flow_block *block,
			  struct flow_cls_offload *f);
int prestera_flower_tmplt_create(struct prestera_flow_block *block,
				 struct flow_cls_offload *f);
void prestera_flower_tmplt_destroy(struct prestera_flow_block *block,
				   struct flow_cls_offload *f);
void prestera_flower_template_cleanup(struct prestera_flow_block *block);

#endif /* _PRESTERA_FLOWER_H_ */
