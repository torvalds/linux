/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_HW_H_
#define _PRESTERA_HW_H_

#include <linux/types.h>

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

struct prestera_switch;
struct prestera_port;
struct prestera_port_stats;
struct prestera_port_caps;
enum prestera_event_type;
struct prestera_event;

typedef void (*prestera_event_cb_t)
	(struct prestera_switch *sw, struct prestera_event *evt, void *arg);

struct prestera_rxtx_params;

/* Switch API */
int prestera_hw_switch_init(struct prestera_switch *sw);
void prestera_hw_switch_fini(struct prestera_switch *sw);
int prestera_hw_switch_mac_set(struct prestera_switch *sw, const char *mac);

/* Port API */
int prestera_hw_port_info_get(const struct prestera_port *port,
			      u32 *dev_id, u32 *hw_id, u16 *fp_id);
int prestera_hw_port_state_set(const struct prestera_port *port,
			       bool admin_state);
int prestera_hw_port_mtu_set(const struct prestera_port *port, u32 mtu);
int prestera_hw_port_mtu_get(const struct prestera_port *port, u32 *mtu);
int prestera_hw_port_mac_set(const struct prestera_port *port, const char *mac);
int prestera_hw_port_mac_get(const struct prestera_port *port, char *mac);
int prestera_hw_port_cap_get(const struct prestera_port *port,
			     struct prestera_port_caps *caps);
int prestera_hw_port_remote_cap_get(const struct prestera_port *port,
				    u64 *link_mode_bitmap);
int prestera_hw_port_remote_fc_get(const struct prestera_port *port,
				   bool *pause, bool *asym_pause);
int prestera_hw_port_type_get(const struct prestera_port *port, u8 *type);
int prestera_hw_port_fec_get(const struct prestera_port *port, u8 *fec);
int prestera_hw_port_fec_set(const struct prestera_port *port, u8 fec);
int prestera_hw_port_autoneg_set(const struct prestera_port *port,
				 bool autoneg, u64 link_modes, u8 fec);
int prestera_hw_port_autoneg_restart(struct prestera_port *port);
int prestera_hw_port_duplex_get(const struct prestera_port *port, u8 *duplex);
int prestera_hw_port_stats_get(const struct prestera_port *port,
			       struct prestera_port_stats *stats);
int prestera_hw_port_link_mode_set(const struct prestera_port *port, u32 mode);
int prestera_hw_port_link_mode_get(const struct prestera_port *port, u32 *mode);
int prestera_hw_port_mdix_get(const struct prestera_port *port, u8 *status,
			      u8 *admin_mode);
int prestera_hw_port_mdix_set(const struct prestera_port *port, u8 mode);
int prestera_hw_port_speed_get(const struct prestera_port *port, u32 *speed);

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
int prestera_hw_rxtx_port_init(struct prestera_port *port);

#endif /* _PRESTERA_HW_H_ */
