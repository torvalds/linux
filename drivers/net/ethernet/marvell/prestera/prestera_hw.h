/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_HW_H_
#define _PRESTERA_HW_H_

#include <linux/types.h>
#include "prestera_acl.h"

enum prestera_accept_frm_type {
	PRESTERA_ACCEPT_FRAME_TYPE_TAGGED,
	PRESTERA_ACCEPT_FRAME_TYPE_UNTAGGED,
	PRESTERA_ACCEPT_FRAME_TYPE_ALL,
};

enum prestera_fdb_flush_mode {
	PRESTERA_FDB_FLUSH_MODE_DYNAMIC = BIT(0),
	PRESTERA_FDB_FLUSH_MODE_STATIC = BIT(1),
	PRESTERA_FDB_FLUSH_MODE_ALL = PRESTERA_FDB_FLUSH_MODE_DYNAMIC
					| PRESTERA_FDB_FLUSH_MODE_STATIC,
};

enum {
	PRESTERA_MAC_MODE_INTERNAL,
	PRESTERA_MAC_MODE_SGMII,
	PRESTERA_MAC_MODE_1000BASE_X,
	PRESTERA_MAC_MODE_KR,
	PRESTERA_MAC_MODE_KR2,
	PRESTERA_MAC_MODE_KR4,
	PRESTERA_MAC_MODE_CR,
	PRESTERA_MAC_MODE_CR2,
	PRESTERA_MAC_MODE_CR4,
	PRESTERA_MAC_MODE_SR_LR,
	PRESTERA_MAC_MODE_SR_LR2,
	PRESTERA_MAC_MODE_SR_LR4,

	PRESTERA_MAC_MODE_MAX
};

enum {
	PRESTERA_LINK_MODE_10baseT_Half,
	PRESTERA_LINK_MODE_10baseT_Full,
	PRESTERA_LINK_MODE_100baseT_Half,
	PRESTERA_LINK_MODE_100baseT_Full,
	PRESTERA_LINK_MODE_1000baseT_Half,
	PRESTERA_LINK_MODE_1000baseT_Full,
	PRESTERA_LINK_MODE_1000baseX_Full,
	PRESTERA_LINK_MODE_1000baseKX_Full,
	PRESTERA_LINK_MODE_2500baseX_Full,
	PRESTERA_LINK_MODE_10GbaseKR_Full,
	PRESTERA_LINK_MODE_10GbaseSR_Full,
	PRESTERA_LINK_MODE_10GbaseLR_Full,
	PRESTERA_LINK_MODE_20GbaseKR2_Full,
	PRESTERA_LINK_MODE_25GbaseCR_Full,
	PRESTERA_LINK_MODE_25GbaseKR_Full,
	PRESTERA_LINK_MODE_25GbaseSR_Full,
	PRESTERA_LINK_MODE_40GbaseKR4_Full,
	PRESTERA_LINK_MODE_40GbaseCR4_Full,
	PRESTERA_LINK_MODE_40GbaseSR4_Full,
	PRESTERA_LINK_MODE_50GbaseCR2_Full,
	PRESTERA_LINK_MODE_50GbaseKR2_Full,
	PRESTERA_LINK_MODE_50GbaseSR2_Full,
	PRESTERA_LINK_MODE_100GbaseKR4_Full,
	PRESTERA_LINK_MODE_100GbaseSR4_Full,
	PRESTERA_LINK_MODE_100GbaseCR4_Full,

	PRESTERA_LINK_MODE_MAX
};

enum {
	PRESTERA_PORT_TYPE_NONE,
	PRESTERA_PORT_TYPE_TP,
	PRESTERA_PORT_TYPE_AUI,
	PRESTERA_PORT_TYPE_MII,
	PRESTERA_PORT_TYPE_FIBRE,
	PRESTERA_PORT_TYPE_BNC,
	PRESTERA_PORT_TYPE_DA,
	PRESTERA_PORT_TYPE_OTHER,

	PRESTERA_PORT_TYPE_MAX
};

enum {
	PRESTERA_PORT_TCVR_COPPER,
	PRESTERA_PORT_TCVR_SFP,

	PRESTERA_PORT_TCVR_MAX
};

enum {
	PRESTERA_PORT_FEC_OFF,
	PRESTERA_PORT_FEC_BASER,
	PRESTERA_PORT_FEC_RS,

	PRESTERA_PORT_FEC_MAX
};

enum {
	PRESTERA_PORT_DUPLEX_HALF,
	PRESTERA_PORT_DUPLEX_FULL,
};

enum {
	PRESTERA_STP_DISABLED,
	PRESTERA_STP_BLOCK_LISTEN,
	PRESTERA_STP_LEARN,
	PRESTERA_STP_FORWARD,
};

enum {
	PRESTERA_POLICER_TYPE_INGRESS,
	PRESTERA_POLICER_TYPE_EGRESS
};

enum prestera_hw_cpu_code_cnt_t {
	PRESTERA_HW_CPU_CODE_CNT_TYPE_DROP = 0,
	PRESTERA_HW_CPU_CODE_CNT_TYPE_TRAP = 1,
};

enum prestera_hw_vtcam_direction_t {
	PRESTERA_HW_VTCAM_DIR_INGRESS = 0,
	PRESTERA_HW_VTCAM_DIR_EGRESS = 1,
};

enum {
	PRESTERA_HW_COUNTER_CLIENT_INGRESS_LOOKUP_0 = 0,
	PRESTERA_HW_COUNTER_CLIENT_INGRESS_LOOKUP_1 = 1,
	PRESTERA_HW_COUNTER_CLIENT_INGRESS_LOOKUP_2 = 2,
	PRESTERA_HW_COUNTER_CLIENT_EGRESS_LOOKUP = 3,
};

struct prestera_switch;
struct prestera_port;
struct prestera_port_stats;
struct prestera_port_caps;
enum prestera_event_type;
struct prestera_event;

typedef void (*prestera_event_cb_t)
	(struct prestera_switch *sw, struct prestera_event *evt, void *arg);

struct prestera_rxtx_params;
struct prestera_acl_hw_action_info;
struct prestera_acl_iface;
struct prestera_counter_stats;
struct prestera_iface;
struct prestera_flood_domain;
struct prestera_mdb_entry;

/* Switch API */
int prestera_hw_switch_init(struct prestera_switch *sw);
void prestera_hw_switch_fini(struct prestera_switch *sw);
int prestera_hw_switch_ageing_set(struct prestera_switch *sw, u32 ageing_ms);
int prestera_hw_switch_mac_set(struct prestera_switch *sw, const char *mac);

/* Port API */
int prestera_hw_port_info_get(const struct prestera_port *port,
			      u32 *dev_id, u32 *hw_id, u16 *fp_id);

int prestera_hw_port_mac_mode_get(const struct prestera_port *port,
				  u32 *mode, u32 *speed, u8 *duplex, u8 *fec);
int prestera_hw_port_mac_mode_set(const struct prestera_port *port,
				  bool admin, u32 mode, u8 inband,
				  u32 speed, u8 duplex, u8 fec);
int prestera_hw_port_phy_mode_get(const struct prestera_port *port,
				  u8 *mdix, u64 *lmode_bmap,
				  bool *fc_pause, bool *fc_asym);
int prestera_hw_port_phy_mode_set(const struct prestera_port *port,
				  bool admin, bool adv, u32 mode, u64 modes,
				  u8 mdix);

int prestera_hw_port_mtu_set(const struct prestera_port *port, u32 mtu);
int prestera_hw_port_mtu_get(const struct prestera_port *port, u32 *mtu);
int prestera_hw_port_mac_set(const struct prestera_port *port, const char *mac);
int prestera_hw_port_mac_get(const struct prestera_port *port, char *mac);
int prestera_hw_port_cap_get(const struct prestera_port *port,
			     struct prestera_port_caps *caps);
int prestera_hw_port_type_get(const struct prestera_port *port, u8 *type);
int prestera_hw_port_autoneg_restart(struct prestera_port *port);
int prestera_hw_port_stats_get(const struct prestera_port *port,
			       struct prestera_port_stats *stats);
int prestera_hw_port_speed_get(const struct prestera_port *port, u32 *speed);
int prestera_hw_port_learning_set(struct prestera_port *port, bool enable);
int prestera_hw_port_uc_flood_set(const struct prestera_port *port, bool flood);
int prestera_hw_port_mc_flood_set(const struct prestera_port *port, bool flood);
int prestera_hw_port_accept_frm_type(struct prestera_port *port,
				     enum prestera_accept_frm_type type);
/* Vlan API */
int prestera_hw_vlan_create(struct prestera_switch *sw, u16 vid);
int prestera_hw_vlan_delete(struct prestera_switch *sw, u16 vid);
int prestera_hw_vlan_port_set(struct prestera_port *port, u16 vid,
			      bool is_member, bool untagged);
int prestera_hw_vlan_port_vid_set(struct prestera_port *port, u16 vid);
int prestera_hw_vlan_port_stp_set(struct prestera_port *port, u16 vid, u8 state);

/* FDB API */
int prestera_hw_fdb_add(struct prestera_port *port, const unsigned char *mac,
			u16 vid, bool dynamic);
int prestera_hw_fdb_del(struct prestera_port *port, const unsigned char *mac,
			u16 vid);
int prestera_hw_fdb_flush_port(struct prestera_port *port, u32 mode);
int prestera_hw_fdb_flush_vlan(struct prestera_switch *sw, u16 vid, u32 mode);
int prestera_hw_fdb_flush_port_vlan(struct prestera_port *port, u16 vid,
				    u32 mode);

/* Bridge API */
int prestera_hw_bridge_create(struct prestera_switch *sw, u16 *bridge_id);
int prestera_hw_bridge_delete(struct prestera_switch *sw, u16 bridge_id);
int prestera_hw_bridge_port_add(struct prestera_port *port, u16 bridge_id);
int prestera_hw_bridge_port_delete(struct prestera_port *port, u16 bridge_id);

/* vTCAM API */
int prestera_hw_vtcam_create(struct prestera_switch *sw,
			     u8 lookup, const u32 *keymask, u32 *vtcam_id,
			     enum prestera_hw_vtcam_direction_t direction);
int prestera_hw_vtcam_rule_add(struct prestera_switch *sw, u32 vtcam_id,
			       u32 prio, void *key, void *keymask,
			       struct prestera_acl_hw_action_info *act,
			       u8 n_act, u32 *rule_id);
int prestera_hw_vtcam_rule_del(struct prestera_switch *sw,
			       u32 vtcam_id, u32 rule_id);
int prestera_hw_vtcam_destroy(struct prestera_switch *sw, u32 vtcam_id);
int prestera_hw_vtcam_iface_bind(struct prestera_switch *sw,
				 struct prestera_acl_iface *iface,
				 u32 vtcam_id, u16 pcl_id);
int prestera_hw_vtcam_iface_unbind(struct prestera_switch *sw,
				   struct prestera_acl_iface *iface,
				   u32 vtcam_id);

/* Counter API */
int prestera_hw_counter_trigger(struct prestera_switch *sw, u32 block_id);
int prestera_hw_counter_abort(struct prestera_switch *sw);
int prestera_hw_counters_get(struct prestera_switch *sw, u32 idx,
			     u32 *len, bool *done,
			     struct prestera_counter_stats *stats);
int prestera_hw_counter_block_get(struct prestera_switch *sw,
				  u32 client, u32 *block_id, u32 *offset,
				  u32 *num_counters);
int prestera_hw_counter_block_release(struct prestera_switch *sw,
				      u32 block_id);
int prestera_hw_counter_clear(struct prestera_switch *sw, u32 block_id,
			      u32 counter_id);

/* SPAN API */
int prestera_hw_span_get(const struct prestera_port *port, u8 *span_id);
int prestera_hw_span_bind(const struct prestera_port *port, u8 span_id);
int prestera_hw_span_unbind(const struct prestera_port *port);
int prestera_hw_span_release(struct prestera_switch *sw, u8 span_id);

/* Router API */
int prestera_hw_rif_create(struct prestera_switch *sw,
			   struct prestera_iface *iif, u8 *mac, u16 *rif_id);
int prestera_hw_rif_delete(struct prestera_switch *sw, u16 rif_id,
			   struct prestera_iface *iif);

/* Virtual Router API */
int prestera_hw_vr_create(struct prestera_switch *sw, u16 *vr_id);
int prestera_hw_vr_delete(struct prestera_switch *sw, u16 vr_id);

/* LPM PI */
int prestera_hw_lpm_add(struct prestera_switch *sw, u16 vr_id,
			__be32 dst, u32 dst_len, u32 grp_id);
int prestera_hw_lpm_del(struct prestera_switch *sw, u16 vr_id,
			__be32 dst, u32 dst_len);

/* Event handlers */
int prestera_hw_event_handler_register(struct prestera_switch *sw,
				       enum prestera_event_type type,
				       prestera_event_cb_t fn,
				       void *arg);
void prestera_hw_event_handler_unregister(struct prestera_switch *sw,
					  enum prestera_event_type type,
					  prestera_event_cb_t fn);

/* RX/TX */
int prestera_hw_rxtx_init(struct prestera_switch *sw,
			  struct prestera_rxtx_params *params);

/* LAG API */
int prestera_hw_lag_member_add(struct prestera_port *port, u16 lag_id);
int prestera_hw_lag_member_del(struct prestera_port *port, u16 lag_id);
int prestera_hw_lag_member_enable(struct prestera_port *port, u16 lag_id,
				  bool enable);
int prestera_hw_lag_fdb_add(struct prestera_switch *sw, u16 lag_id,
			    const unsigned char *mac, u16 vid, bool dynamic);
int prestera_hw_lag_fdb_del(struct prestera_switch *sw, u16 lag_id,
			    const unsigned char *mac, u16 vid);
int prestera_hw_fdb_flush_lag(struct prestera_switch *sw, u16 lag_id,
			      u32 mode);
int prestera_hw_fdb_flush_lag_vlan(struct prestera_switch *sw,
				   u16 lag_id, u16 vid, u32 mode);

/* HW trap/drop counters API */
int
prestera_hw_cpu_code_counters_get(struct prestera_switch *sw, u8 code,
				  enum prestera_hw_cpu_code_cnt_t counter_type,
				  u64 *packet_count);

/* Policer API */
int prestera_hw_policer_create(struct prestera_switch *sw, u8 type,
			       u32 *policer_id);
int prestera_hw_policer_release(struct prestera_switch *sw,
				u32 policer_id);
int prestera_hw_policer_sr_tcm_set(struct prestera_switch *sw,
				   u32 policer_id, u64 cir, u32 cbs);

/* Flood domain / MDB API */
int prestera_hw_flood_domain_create(struct prestera_flood_domain *domain);
int prestera_hw_flood_domain_destroy(struct prestera_flood_domain *domain);
int prestera_hw_flood_domain_ports_set(struct prestera_flood_domain *domain);
int prestera_hw_flood_domain_ports_reset(struct prestera_flood_domain *domain);

int prestera_hw_mdb_create(struct prestera_mdb_entry *mdb);
int prestera_hw_mdb_destroy(struct prestera_mdb_entry *mdb);

#endif /* _PRESTERA_HW_H_ */
