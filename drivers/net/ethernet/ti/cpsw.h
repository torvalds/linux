/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 */
#ifndef __CPSW_H__
#define __CPSW_H__

#include <linux/if_ether.h>
#include <linux/phy.h>

#define mac_hi(mac)	(((mac)[0] << 0) | ((mac)[1] << 8) |	\
			 ((mac)[2] << 16) | ((mac)[3] << 24))
#define mac_lo(mac)	(((mac)[4] << 0) | ((mac)[5] << 8))

#if IS_ENABLED(CONFIG_TI_CPSW_PHY_SEL)
void cpsw_phy_sel(struct device *dev, phy_interface_t phy_mode, int slave);
#else
static inline
void cpsw_phy_sel(struct device *dev, phy_interface_t phy_mode, int slave)
{}
#endif
int ti_cm_get_macid(struct device *dev, int slave, u8 *mac_addr);

#endif /* __CPSW_H__ */
