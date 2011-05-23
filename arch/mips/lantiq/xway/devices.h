/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#ifndef _LTQ_DEVICES_XWAY_H__
#define _LTQ_DEVICES_XWAY_H__

#include "../devices.h"
#include <linux/phy.h>

extern void ltq_register_gpio(void);
extern void ltq_register_gpio_stp(void);
extern void ltq_register_ase_asc(void);
extern void ltq_register_etop(struct ltq_eth_data *eth);

#endif
