// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus SVC code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 */

#ifndef __SVC_H
#define __SVC_H

#define GB_SVC_CPORT_FLAG_E2EFC		BIT(0)
#define GB_SVC_CPORT_FLAG_CSD_N		BIT(1)
#define GB_SVC_CPORT_FLAG_CSV_N		BIT(2)

enum gb_svc_state {
	GB_SVC_STATE_RESET,
	GB_SVC_STATE_PROTOCOL_VERSION,
	GB_SVC_STATE_SVC_HELLO,
};

enum gb_svc_watchdog_bite {
	GB_SVC_WATCHDOG_BITE_RESET_UNIPRO = 0,
	GB_SVC_WATCHDOG_BITE_PANIC_KERNEL,
};

struct gb_svc_watchdog;

struct svc_debugfs_pwrmon_rail {
	u8 id;
	struct gb_svc *svc;
};

struct gb_svc {
	struct device		dev;

	struct gb_host_device	*hd;
	struct gb_connection	*connection;
	enum gb_svc_state	state;
	struct ida		device_id_map;
	struct workqueue_struct	*wq;

	u16 endo_id;
	u8 ap_intf_id;

	u8 protocol_major;
	u8 protocol_minor;

	struct gb_svc_watchdog	*watchdog;
	enum gb_svc_watchdog_bite action;

	struct dentry *debugfs_dentry;
	struct svc_debugfs_pwrmon_rail *pwrmon_rails;
};
#define to_gb_svc(d) container_of(d, struct gb_svc, dev)

struct gb_svc *gb_svc_create(struct gb_host_device *hd);
int gb_svc_add(struct gb_svc *svc);
void gb_svc_del(struct gb_svc *svc);
void gb_svc_put(struct gb_svc *svc);

int gb_svc_pwrmon_intf_sample_get(struct gb_svc *svc, u8 intf_id,
				  u8 measurement_type, u32 *value);
int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id);
int gb_svc_route_create(struct gb_svc *svc, u8 intf1_id, u8 dev1_id,
			       u8 intf2_id, u8 dev2_id);
void gb_svc_route_destroy(struct gb_svc *svc, u8 intf1_id, u8 intf2_id);
int gb_svc_connection_create(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			     u8 intf2_id, u16 cport2_id, u8 cport_flags);
void gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			       u8 intf2_id, u16 cport2_id);
int gb_svc_intf_eject(struct gb_svc *svc, u8 intf_id);
int gb_svc_intf_vsys_set(struct gb_svc *svc, u8 intf_id, bool enable);
int gb_svc_intf_refclk_set(struct gb_svc *svc, u8 intf_id, bool enable);
int gb_svc_intf_unipro_set(struct gb_svc *svc, u8 intf_id, bool enable);
int gb_svc_intf_activate(struct gb_svc *svc, u8 intf_id, u8 *intf_type);
int gb_svc_intf_resume(struct gb_svc *svc, u8 intf_id);

int gb_svc_dme_peer_get(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 *value);
int gb_svc_dme_peer_set(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 value);
int gb_svc_intf_set_power_mode(struct gb_svc *svc, u8 intf_id, u8 hs_series,
			       u8 tx_mode, u8 tx_gear, u8 tx_nlanes,
			       u8 tx_amplitude, u8 tx_hs_equalizer,
			       u8 rx_mode, u8 rx_gear, u8 rx_nlanes,
			       u8 flags, u32 quirks,
			       struct gb_svc_l2_timer_cfg *local,
			       struct gb_svc_l2_timer_cfg *remote);
int gb_svc_intf_set_power_mode_hibernate(struct gb_svc *svc, u8 intf_id);
int gb_svc_ping(struct gb_svc *svc);
int gb_svc_watchdog_create(struct gb_svc *svc);
void gb_svc_watchdog_destroy(struct gb_svc *svc);
bool gb_svc_watchdog_enabled(struct gb_svc *svc);
int gb_svc_watchdog_enable(struct gb_svc *svc);
int gb_svc_watchdog_disable(struct gb_svc *svc);

int gb_svc_protocol_init(void);
void gb_svc_protocol_exit(void);

#endif /* __SVC_H */
