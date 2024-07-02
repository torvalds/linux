/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip KSZ8XXX series register access
 *
 * Copyright (C) 2020 Pengutronix, Michael Grzeschik <kernel@pengutronix.de>
 */

#ifndef __KSZ8XXX_H
#define __KSZ8XXX_H

#include <linux/types.h>
#include <net/dsa.h>
#include "ksz_common.h"

int ksz8_setup(struct dsa_switch *ds);
u32 ksz8_get_port_addr(int port, int offset);
void ksz8_cfg_port_member(struct ksz_device *dev, int port, u8 member);
void ksz8_flush_dyn_mac_table(struct ksz_device *dev, int port);
void ksz8_port_setup(struct ksz_device *dev, int port, bool cpu_port);
int ksz8_r_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 *val);
int ksz8_w_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 val);
void ksz8_r_mib_cnt(struct ksz_device *dev, int port, u16 addr, u64 *cnt);
void ksz8_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
		    u64 *dropped, u64 *cnt);
void ksz8_freeze_mib(struct ksz_device *dev, int port, bool freeze);
void ksz8_port_init_cnt(struct ksz_device *dev, int port);
int ksz8_fdb_dump(struct ksz_device *dev, int port,
		  dsa_fdb_dump_cb_t *cb, void *data);
int ksz8_fdb_add(struct ksz_device *dev, int port, const unsigned char *addr,
		 u16 vid, struct dsa_db db);
int ksz8_fdb_del(struct ksz_device *dev, int port, const unsigned char *addr,
		 u16 vid, struct dsa_db db);
int ksz8_mdb_add(struct ksz_device *dev, int port,
		 const struct switchdev_obj_port_mdb *mdb, struct dsa_db db);
int ksz8_mdb_del(struct ksz_device *dev, int port,
		 const struct switchdev_obj_port_mdb *mdb, struct dsa_db db);
int ksz8_port_vlan_filtering(struct ksz_device *dev, int port, bool flag,
			     struct netlink_ext_ack *extack);
int ksz8_port_vlan_add(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_vlan *vlan,
		       struct netlink_ext_ack *extack);
int ksz8_port_vlan_del(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_vlan *vlan);
int ksz8_port_mirror_add(struct ksz_device *dev, int port,
			 struct dsa_mall_mirror_tc_entry *mirror,
			 bool ingress, struct netlink_ext_ack *extack);
void ksz8_port_mirror_del(struct ksz_device *dev, int port,
			  struct dsa_mall_mirror_tc_entry *mirror);
void ksz8_get_caps(struct ksz_device *dev, int port,
		   struct phylink_config *config);
void ksz8_config_cpu_port(struct dsa_switch *ds);
int ksz8_enable_stp_addr(struct ksz_device *dev);
int ksz8_reset_switch(struct ksz_device *dev);
int ksz8_switch_init(struct ksz_device *dev);
void ksz8_switch_exit(struct ksz_device *dev);
int ksz8_change_mtu(struct ksz_device *dev, int port, int mtu);
void ksz8_phylink_mac_link_up(struct phylink_config *config,
			      struct phy_device *phydev, unsigned int mode,
			      phy_interface_t interface, int speed, int duplex,
			      bool tx_pause, bool rx_pause);
int ksz8_all_queues_split(struct ksz_device *dev, int queues);

#endif
