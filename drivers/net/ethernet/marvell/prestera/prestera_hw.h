/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_HW_H_
#define _PRESTERA_HW_H_

#include <linux/types.h>

enum {
	PRESTERA_PORT_TYPE_NONE,
	PRESTERA_PORT_TYPE_TP,

	PRESTERA_PORT_TYPE_MAX
};

enum {
	PRESTERA_PORT_FEC_OFF,

	PRESTERA_PORT_FEC_MAX
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
int prestera_hw_port_autoneg_set(const struct prestera_port *port,
				 bool autoneg, u64 link_modes, u8 fec);
int prestera_hw_port_stats_get(const struct prestera_port *port,
			       struct prestera_port_stats *stats);

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
