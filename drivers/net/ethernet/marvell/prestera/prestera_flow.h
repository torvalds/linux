/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_FLOW_H_
#define _PRESTERA_FLOW_H_

#include <net/flow_offload.h>

struct prestera_port;

int prestera_flow_block_setup(struct prestera_port *port,
			      struct flow_block_offload *f);

#endif /* _PRESTERA_FLOW_H_ */
