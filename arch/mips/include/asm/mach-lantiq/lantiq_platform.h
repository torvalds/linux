/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#ifndef _LANTIQ_PLATFORM_H__
#define _LANTIQ_PLATFORM_H__

#include <linux/socket.h>

/* struct used to pass info to network drivers */
struct ltq_eth_data {
	struct sockaddr mac;
	int mii_mode;
};

#endif
