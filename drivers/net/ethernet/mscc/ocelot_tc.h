/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_TC_H_
#define _MSCC_OCELOT_TC_H_

#include <linux/netdevice.h>

struct ocelot_port_tc {
	bool block_shared;
	unsigned long offload_cnt;

	unsigned long police_id;
};

int ocelot_setup_tc(struct net_device *dev, enum tc_setup_type type,
		    void *type_data);

#endif /* _MSCC_OCELOT_TC_H_ */
