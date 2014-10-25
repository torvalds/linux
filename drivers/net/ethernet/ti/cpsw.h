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

struct cpsw_slave_data {
	char		phy_id[MII_BUS_ID_SIZE];
	int		phy_if;
	u8		mac_addr[ETH_ALEN];
	u16		dual_emac_res_vlan;	/* Reserved VLAN for DualEMAC */
};

struct cpsw_platform_data {
	struct cpsw_slave_data	*slave_data;
	u32	ss_reg_ofs;	/* Subsystem control register offset */
	u32	channels;	/* number of cpdma channels (symmetric) */
	u32	slaves;		/* number of slave cpgmac ports */
	u32	active_slave; /* time stamping, ethtool and SIOCGMIIPHY slave */
	u32	cpts_clock_mult;  /* convert input clock ticks to nanoseconds */
	u32	cpts_clock_shift; /* convert input clock ticks to nanoseconds */
	u32	ale_entries;	/* ale table size */
	u32	bd_ram_size;  /*buffer descriptor ram size */
	u32	rx_descs;	/* Number of Rx Descriptios */
	u32	mac_control;	/* Mac control register */
	u16	default_vlan;	/* Def VLAN for ALE lookup in VLAN aware mode*/
	bool	dual_emac;	/* Enable Dual EMAC mode */
};

void cpsw_phy_sel(struct device *dev, phy_interface_t phy_mode, int slave);

#endif /* __CPSW_H__ */
