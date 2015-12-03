/*
 * Greybus SVC code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __SVC_H
#define __SVC_H

enum gb_svc_state {
	GB_SVC_STATE_RESET,
	GB_SVC_STATE_PROTOCOL_VERSION,
	GB_SVC_STATE_SVC_HELLO,
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
};
#define to_gb_svc(d) container_of(d, struct gb_svc, d)

int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id);
int gb_svc_connection_create(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			     u8 intf2_id, u16 cport2_id, bool boot_over_unipro);
void gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			       u8 intf2_id, u16 cport2_id);
int gb_svc_dme_peer_get(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 *value);
int gb_svc_dme_peer_set(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 value);

int gb_svc_protocol_init(void);
void gb_svc_protocol_exit(void);

#endif /* __SVC_H */
