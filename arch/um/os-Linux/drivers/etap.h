/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __DRIVERS_ETAP_H
#define __DRIVERS_ETAP_H

#include <net_user.h>

struct ethertap_data {
	char *dev_name;
	char *gate_addr;
	int data_fd;
	int control_fd;
	void *dev;
};

extern const struct net_user_info ethertap_user_info;

#endif
