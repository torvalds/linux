/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZPROTOCOL_H
#define _OZPROTOCOL_H

#define PACKED __packed

#define OZ_ETHERTYPE 0x892e

/* Status codes
 */
#define OZ_STATUS_SUCCESS		0
#define OZ_STATUS_INVALID_PARAM		1
#define OZ_STATUS_TOO_MANY_PDS		2
#define OZ_STATUS_NOT_ALLOWED		4
#define OZ_STATUS_SESSION_MISMATCH	5
#define OZ_STATUS_SESSION_TEARDOWN	6

/* This is the generic element header.
   Every element starts with this.
 */
struct oz_elt {
	u8 type;
	u8 length;
} PACKED;

#define oz_next_elt(__elt)	\
	(struct oz_elt *)((u8 *)((__elt) + 1) + (__elt)->length)

/* Protocol element IDs.
 */
#define OZ_ELT_CONNECT_REQ	0x06
#define OZ_ELT_CONNECT_RSP	0x07
#define OZ_ELT_DISCONNECT	0x08
#define OZ_ELT_UPDATE_PARAM_REQ	0x11
#define OZ_ELT_FAREWELL_REQ	0x12
#define OZ_ELT_APP_DATA		0x31

/* This is the Ozmo header which is the first Ozmo specific part
 * of a frame and comes after the MAC header.
 */
struct oz_hdr {
	u8	control;
	u8	last_pkt_num;
	u32	pkt_num;
} PACKED;

#define OZ_PROTOCOL_VERSION	0x1
/* Bits in the control field. */
#define OZ_VERSION_MASK		0xc
#define OZ_VERSION_SHIFT	2
#define OZ_F_ACK		0x10
#define OZ_F_ISOC		0x20
#define OZ_F_MORE_DATA		0x40
#define OZ_F_ACK_REQUESTED	0x80

#define oz_get_prot_ver(__x)	(((__x) & OZ_VERSION_MASK) >> OZ_VERSION_SHIFT)

/* Used to select the bits of packet number to put in the last_pkt_num.
 */
#define OZ_LAST_PN_MASK		0x00ff

#define OZ_LAST_PN_HALF_CYCLE	127

#define OZ_LATENCY_MASK		0xc0
#define OZ_ONE_MS_LATENCY	0x40
#define OZ_TEN_MS_LATENCY	0x80

/* Connect request data structure.
 */
struct oz_elt_connect_req {
	u8	mode;
	u8	resv1[16];
	u8	pd_info;
	u8	session_id;
	u8	presleep;
	u8	ms_isoc_latency;
	u8	host_vendor;
	u8	keep_alive;
	u16	apps;
	u8	max_len_div16;
	u8	ms_per_isoc;
	u8	resv3[2];
} PACKED;

/* mode field bits.
 */
#define OZ_MODE_POLLED		0x0
#define OZ_MODE_TRIGGERED	0x1
#define OZ_MODE_MASK		0xf
#define OZ_F_ISOC_NO_ELTS	0x40
#define OZ_F_ISOC_ANYTIME	0x80
#define OZ_NO_ELTS_ANYTIME	0xc0

/* Keep alive field.
 */
#define OZ_KALIVE_TYPE_MASK	0xc0
#define OZ_KALIVE_VALUE_MASK	0x3f
#define OZ_KALIVE_SPECIAL	0x00
#define OZ_KALIVE_SECS		0x40
#define OZ_KALIVE_MINS		0x80
#define OZ_KALIVE_HOURS		0xc0

/* Connect response data structure.
 */
struct oz_elt_connect_rsp {
	u8	mode;
	u8	status;
	u8	resv1[3];
	u8	session_id;
	u16	apps;
	u32	resv2;
} PACKED;

struct oz_elt_farewell {
	u8	ep_num;
	u8	index;
	u8	report[1];
} PACKED;

struct oz_elt_update_param {
	u8	resv1[16];
	u8	presleep;
	u8	resv2;
	u8	host_vendor;
	u8	keepalive;
} PACKED;

/* Header common to all application elements.
 */
struct oz_app_hdr {
	u8	app_id;
	u8	elt_seq_num;
} PACKED;

/* Values for app_id.
 */
#define OZ_APPID_USB				0x1
#define OZ_APPID_UNUSED1			0x2
#define OZ_APPID_UNUSED2			0x3
#define OZ_APPID_SERIAL				0x4
#define OZ_APPID_MAX				OZ_APPID_SERIAL
#define OZ_NB_APPS				(OZ_APPID_MAX+1)

/* USB header common to all elements for the  USB application.
 * This header extends the oz_app_hdr and comes directly after
 * the element header in a USB application.
 */
struct oz_usb_hdr {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
} PACKED;



/* USB requests element subtypes (type field of hs_usb_hdr).
 */
#define OZ_GET_DESC_REQ			1
#define OZ_GET_DESC_RSP			2
#define OZ_SET_CONFIG_REQ		3
#define OZ_SET_CONFIG_RSP		4
#define OZ_SET_INTERFACE_REQ		5
#define OZ_SET_INTERFACE_RSP		6
#define OZ_VENDOR_CLASS_REQ		7
#define OZ_VENDOR_CLASS_RSP		8
#define OZ_GET_STATUS_REQ		9
#define OZ_GET_STATUS_RSP		10
#define OZ_CLEAR_FEATURE_REQ		11
#define OZ_CLEAR_FEATURE_RSP		12
#define OZ_SET_FEATURE_REQ		13
#define OZ_SET_FEATURE_RSP		14
#define OZ_GET_CONFIGURATION_REQ	15
#define OZ_GET_CONFIGURATION_RSP	16
#define OZ_GET_INTERFACE_REQ		17
#define OZ_GET_INTERFACE_RSP		18
#define OZ_SYNCH_FRAME_REQ		19
#define OZ_SYNCH_FRAME_RSP		20
#define OZ_USB_ENDPOINT_DATA		23

#define OZ_REQD_D2H			0x80

struct oz_get_desc_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u16	offset;
	u16	size;
	u8	req_type;
	u8	desc_type;
	u16	w_index;
	u8	index;
} PACKED;

/* Values for desc_type field.
*/
#define OZ_DESC_DEVICE			0x01
#define OZ_DESC_CONFIG			0x02
#define OZ_DESC_STRING			0x03

/* Values for req_type field.
 */
#define OZ_RECP_MASK			0x1F
#define OZ_RECP_DEVICE			0x00
#define OZ_RECP_INTERFACE		0x01
#define OZ_RECP_ENDPOINT		0x02

#define OZ_REQT_MASK			0x60
#define OZ_REQT_STD			0x00
#define OZ_REQT_CLASS			0x20
#define OZ_REQT_VENDOR			0x40

struct oz_get_desc_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u16	offset;
	u16	total_size;
	u8	rcode;
	u8	data[1];
} PACKED;

struct oz_feature_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	recipient;
	u8	index;
	u16	feature;
} PACKED;

struct oz_feature_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	rcode;
} PACKED;

struct oz_set_config_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	index;
} PACKED;

struct oz_set_config_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	rcode;
} PACKED;

struct oz_set_interface_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	index;
	u8	alternative;
} PACKED;

struct oz_set_interface_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	rcode;
} PACKED;

struct oz_get_interface_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	index;
} PACKED;

struct oz_get_interface_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	rcode;
	u8	alternative;
} PACKED;

struct oz_vendor_class_req {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	req_type;
	u8	request;
	u16	value;
	u16	index;
	u8	data[1];
} PACKED;

struct oz_vendor_class_rsp {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	req_id;
	u8	rcode;
	u8	data[1];
} PACKED;

struct oz_data {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	endpoint;
	u8	format;
} PACKED;

struct oz_isoc_fixed {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	endpoint;
	u8	format;
	u8	unit_size;
	u8	frame_number;
	u8	data[1];
} PACKED;

struct oz_multiple_fixed {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	endpoint;
	u8	format;
	u8	unit_size;
	u8	data[1];
} PACKED;

struct oz_fragmented {
	u8	app_id;
	u8	elt_seq_num;
	u8	type;
	u8	endpoint;
	u8	format;
	u16	total_size;
	u16	offset;
	u8	data[1];
} PACKED;

/* Note: the following does not get packaged in an element in the same way
 * that other data formats are packaged. Instead the data is put in a frame
 * directly after the oz_header and is the only permitted data in such a
 * frame. The length of the data is directly determined from the frame size.
 */
struct oz_isoc_large {
	u8	endpoint;
	u8	format;
	u8	ms_data;
	u8	frame_number;
} PACKED;

#define OZ_DATA_F_TYPE_MASK		0xF
#define OZ_DATA_F_MULTIPLE_FIXED	0x1
#define OZ_DATA_F_MULTIPLE_VAR		0x2
#define OZ_DATA_F_ISOC_FIXED		0x3
#define OZ_DATA_F_ISOC_VAR		0x4
#define OZ_DATA_F_FRAGMENTED		0x5
#define OZ_DATA_F_ISOC_LARGE		0x7

#endif /* _OZPROTOCOL_H */
