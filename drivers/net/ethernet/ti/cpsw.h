/* Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __CPSW_H__
#define __CPSW_H__

#include <linux/if_ether.h>
#include <linux/phy.h>

void cpsw_phy_sel(struct device *dev, phy_interface_t phy_mode, int slave);
int ti_cm_get_macid(struct device *dev, int slave, u8 *mac_addr);

#endif /* __CPSW_H__ */
