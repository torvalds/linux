/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * u_phonet.h - interface to Phonet
 *
 * Copyright (C) 2007-2008 by Nokia Corporation
 */

#ifndef __U_PHONET_H
#define __U_PHONET_H

#include <linux/usb/composite.h>
#include <linux/usb/cdc.h>

struct f_phonet_opts {
	struct usb_function_instance func_inst;
	bool bound;
	struct net_device *net;
};

struct net_device *gphonet_setup_default(void);
void gphonet_set_gadget(struct net_device *net, struct usb_gadget *g);
int gphonet_register_netdev(struct net_device *net);
int phonet_bind_config(struct usb_configuration *c, struct net_device *dev);
void gphonet_cleanup(struct net_device *dev);

#endif /* __U_PHONET_H */
