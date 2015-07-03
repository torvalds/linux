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

struct gb_svc;

int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id);
int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id);
int gb_svc_connection_create(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
						u8 intf2_id, u16 cport2_id);
int gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
						u8 intf2_id, u16 cport2_id);

int gb_svc_protocol_init(void);
void gb_svc_protocol_exit(void);
#endif /* __SVC_H */
