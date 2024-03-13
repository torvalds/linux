/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_SPAN_H_
#define _PRESTERA_SPAN_H_

#include <net/pkt_cls.h>

#define PRESTERA_SPAN_INVALID_ID -1

struct prestera_port;
struct prestera_switch;
struct prestera_flow_block_binding;

int prestera_span_init(struct prestera_switch *sw);
void prestera_span_fini(struct prestera_switch *sw);

int prestera_span_rule_add(struct prestera_flow_block_binding *binding,
			   struct prestera_port *to_port,
			   bool ingress);
int prestera_span_rule_del(struct prestera_flow_block_binding *binding,
			   bool ingress);

#endif /* _PRESTERA_SPAN_H_ */
