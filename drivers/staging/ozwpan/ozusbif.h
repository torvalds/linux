/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZUSBIF_H
#define _OZUSBIF_H

#include <linux/usb.h>

/* Reference counting functions.
 */
void oz_usb_get(void *hpd);
void oz_usb_put(void *hpd);

/* Stream functions.
 */
int oz_usb_stream_create(void *hpd, u8 ep_num);
int oz_usb_stream_delete(void *hpd, u8 ep_num);

/* Request functions.
 */
int oz_usb_control_req(void *hpd, u8 req_id, struct usb_ctrlrequest *setup,
		u8 *data, int data_len);
int oz_usb_get_desc_req(void *hpd, u8 req_id, u8 req_type, u8 desc_type,
	u8 index, u16 windex, int offset, int len);
int oz_usb_send_isoc(void *hpd, u8 ep_num, struct urb *urb);
void oz_usb_request_heartbeat(void *hpd);

/* Confirmation functions.
 */
void oz_hcd_get_desc_cnf(void *hport, u8 req_id, int status,
	u8 *desc, int length, int offset, int total_size);
void oz_hcd_control_cnf(void *hport, u8 req_id, u8 rcode,
	u8 *data, int data_len);

/* Indication functions.
 */
void oz_hcd_data_ind(void *hport, u8 endpoint, u8 *data, int data_len);

int oz_hcd_heartbeat(void *hport);

#endif /* _OZUSBIF_H */
