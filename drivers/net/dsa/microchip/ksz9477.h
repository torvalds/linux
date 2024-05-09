/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip KSZ9477 series Header file
 *
 * Copyright (C) 2017-2022 Microchip Technology Inc.
 */

#ifndef __KSZ9477_H
#define __KSZ9477_H

#include <net/dsa.h>
#include "ksz_common.h"

int ksz9477_setup(struct dsa_switch *ds);
u32 ksz9477_get_port_addr(int port, int offset);
void ksz9477_cfg_port_member(struct ksz_device *dev, int port, u8 member);
void ksz9477_flush_dyn_mac_table(struct ksz_device *dev, int port);
void ksz9477_port_setup(struct ksz_device *dev, int port, bool cpu_port);
int ksz9477_set_ageing_time(struct ksz_device *dev, unsigned int msecs);
int ksz9477_r_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 *data);
int ksz9477_w_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 val);
void ksz9477_r_mib_cnt(struct ksz_device *dev, int port, u16 addr, u64 *cnt);
void ksz9477_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
		       u64 *dropped, u64 *cnt);
void ksz9477_freeze_mib(struct ksz_device *dev, int port, bool freeze);
void ksz9477_port_init_cnt(struct ksz_device *dev, int port);
int ksz9477_port_vlan_filtering(struct ksz_device *dev, int port,
				bool flag, struct netlink_ext_ack *extack);
int ksz9477_port_vlan_add(struct ksz_device *dev, int port,
			  const struct switchdev_obj_port_vlan *vlan,
			  struct netlink_ext_ack *extack);
int ksz9477_port_vlan_del(struct ksz_device *dev, int port,
			  const struct switchdev_obj_port_vlan *vlan);
int ksz9477_port_mirror_add(struct ksz_device *dev, int port,
			    struct dsa_mall_mirror_tc_entry *mirror,
			    bool ingress, struct netlink_ext_ack *extack);
void ksz9477_port_mirror_del(struct ksz_device *dev, int port,
			     struct dsa_mall_mirror_tc_entry *mirror);
void ksz9477_get_caps(struct ksz_device *dev, int port,
		      struct phylink_config *config);
int ksz9477_fdb_dump(struct ksz_device *dev, int port,
		     dsa_fdb_dump_cb_t *cb, void *data);
int ksz9477_fdb_add(struct ksz_device *dev, int port,
		    const unsigned char *addr, u16 vid, struct dsa_db db);
int ksz9477_fdb_del(struct ksz_device *dev, int port,
		    const unsigned char *addr, u16 vid, struct dsa_db db);
int ksz9477_mdb_add(struct ksz_device *dev, int port,
		    const struct switchdev_obj_port_mdb *mdb, struct dsa_db db);
int ksz9477_mdb_del(struct ksz_device *dev, int port,
		    const struct switchdev_obj_port_mdb *mdb, struct dsa_db db);
int ksz9477_change_mtu(struct ksz_device *dev, int port, int mtu);
void ksz9477_config_cpu_port(struct dsa_switch *ds);
int ksz9477_tc_cbs_set_cinc(struct ksz_device *dev, int port, u32 val);
int ksz9477_enable_stp_addr(struct ksz_device *dev);
int ksz9477_reset_switch(struct ksz_device *dev);
int ksz9477_switch_init(struct ksz_device *dev);
void ksz9477_switch_exit(struct ksz_device *dev);
void ksz9477_port_queue_split(struct ksz_device *dev, int port);
void ksz9477_hsr_join(struct dsa_switch *ds, int port, struct net_device *hsr);
void ksz9477_hsr_leave(struct dsa_switch *ds, int port, struct net_device *hsr);
void ksz9477_get_wol(struct ksz_device *dev, int port,
		     struct ethtool_wolinfo *wol);
int ksz9477_set_wol(struct ksz_device *dev, int port,
		    struct ethtool_wolinfo *wol);
void ksz9477_wol_pre_shutdown(struct ksz_device *dev, bool *wol_enabled);

int ksz9477_port_acl_init(struct ksz_device *dev, int port);
void ksz9477_port_acl_free(struct ksz_device *dev, int port);
int ksz9477_cls_flower_add(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);
int ksz9477_cls_flower_del(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);

#define KSZ9477_ACL_ENTRY_SIZE		18
#define KSZ9477_ACL_MAX_ENTRIES		16

struct ksz9477_acl_entry {
	u8 entry[KSZ9477_ACL_ENTRY_SIZE];
	unsigned long cookie;
	u32 prio;
};

struct ksz9477_acl_entries {
	struct ksz9477_acl_entry entries[KSZ9477_ACL_MAX_ENTRIES];
	int entries_count;
};

struct ksz9477_acl_priv {
	struct ksz9477_acl_entries acles;
};

void ksz9477_acl_remove_entries(struct ksz_device *dev, int port,
				struct ksz9477_acl_entries *acles,
				unsigned long cookie);
int ksz9477_acl_write_list(struct ksz_device *dev, int port);
int ksz9477_sort_acl_entries(struct ksz_device *dev, int port);
void ksz9477_acl_action_rule_cfg(u8 *entry, bool force_prio, u8 prio_val);
void ksz9477_acl_processing_rule_set_action(u8 *entry, u8 action_idx);
void ksz9477_acl_match_process_l2(struct ksz_device *dev, int port,
				  u16 ethtype, u8 *src_mac, u8 *dst_mac,
				  unsigned long cookie, u32 prio);

#endif
