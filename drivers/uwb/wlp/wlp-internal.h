/*
 * WiMedia Logical Link Control Protocol (WLP)
 * Internal API
 *
 * Copyright (C) 2007 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __WLP_INTERNAL_H__
#define __WLP_INTERNAL_H__

/**
 * State of WSS connection
 *
 * A device needs to connect to a neighbor in an activated WSS before data
 * can be transmitted. The spec also distinguishes between a new connection
 * attempt and a connection attempt after previous connection attempts. The
 * state WLP_WSS_CONNECT_FAILED is used for this scenario. See WLP 0.99
 * [7.2.6]
 */
enum wlp_wss_connect {
	WLP_WSS_UNCONNECTED = 0,
	WLP_WSS_CONNECTED,
	WLP_WSS_CONNECT_FAILED,
};

extern struct kobj_type wss_ktype;
extern struct attribute_group wss_attr_group;

/* This should be changed to a dynamic array where entries are sorted
 * by eth_addr and search is done in a binary form
 *
 * Although thinking twice about it: this technologie's maximum reach
 * is 10 meters...unless you want to pack too much stuff in around
 * your radio controller/WLP device, the list will probably not be
 * too big.
 *
 * In any case, there is probably some data structure in the kernel
 * than we could reused for that already.
 *
 * The below structure is really just good while we support one WSS per
 * host.
 */
struct wlp_eda_node {
	struct list_head list_node;
	unsigned char eth_addr[ETH_ALEN];
	struct uwb_dev_addr dev_addr;
	struct wlp_wss *wss;
	unsigned char virt_addr[ETH_ALEN];
	u8 tag;
	enum wlp_wss_connect state;
};

typedef int (*wlp_eda_for_each_f)(struct wlp *, struct wlp_eda_node *, void *);

extern void wlp_eda_init(struct wlp_eda *);
extern void wlp_eda_release(struct wlp_eda *);
extern int wlp_eda_create_node(struct wlp_eda *,
			       const unsigned char eth_addr[ETH_ALEN],
			       const struct uwb_dev_addr *);
extern void wlp_eda_rm_node(struct wlp_eda *, const struct uwb_dev_addr *);
extern int wlp_eda_update_node(struct wlp_eda *,
			       const struct uwb_dev_addr *,
			       struct wlp_wss *,
			       const unsigned char virt_addr[ETH_ALEN],
			       const u8, const enum wlp_wss_connect);
extern int wlp_eda_update_node_state(struct wlp_eda *,
				     const struct uwb_dev_addr *,
				     const enum wlp_wss_connect);

extern int wlp_copy_eda_node(struct wlp_eda *, struct uwb_dev_addr *,
			     struct wlp_eda_node *);
extern int wlp_eda_for_each(struct wlp_eda *, wlp_eda_for_each_f , void *);
extern int wlp_eda_for_virtual(struct wlp_eda *,
			       const unsigned char eth_addr[ETH_ALEN],
			       struct uwb_dev_addr *,
			       wlp_eda_for_each_f , void *);


extern void wlp_remove_neighbor_tmp_info(struct wlp_neighbor_e *);

extern size_t wlp_wss_key_print(char *, size_t, u8 *);

/* Function called when no more references to WSS exists */
extern void wlp_wss_release(struct kobject *);

extern void wlp_wss_reset(struct wlp_wss *);
extern int wlp_wss_create_activate(struct wlp_wss *, struct wlp_uuid *,
				   char *, unsigned, unsigned);
extern int wlp_wss_enroll_activate(struct wlp_wss *, struct wlp_uuid *,
				   struct uwb_dev_addr *);
extern ssize_t wlp_discover(struct wlp *);

extern int wlp_enroll_neighbor(struct wlp *, struct wlp_neighbor_e *,
			       struct wlp_wss *, struct wlp_uuid *);
extern int wlp_wss_is_active(struct wlp *, struct wlp_wss *,
			     struct uwb_dev_addr *);

struct wlp_assoc_conn_ctx {
	struct work_struct ws;
	struct wlp *wlp;
	struct sk_buff *skb;
	struct wlp_eda_node eda_entry;
};


extern int wlp_wss_connect_prep(struct wlp *, struct wlp_eda_node *, void *);
extern int wlp_wss_send_copy(struct wlp *, struct wlp_eda_node *, void *);


/* Message handling */
struct wlp_assoc_frame_ctx {
	struct work_struct ws;
	struct wlp *wlp;
	struct sk_buff *skb;
	struct uwb_dev_addr src;
};

extern int wlp_wss_prep_hdr(struct wlp *, struct wlp_eda_node *, void *);
extern void wlp_handle_d1_frame(struct work_struct *);
extern int wlp_parse_d2_frame_to_cache(struct wlp *, struct sk_buff *,
				       struct wlp_neighbor_e *);
extern int wlp_parse_d2_frame_to_enroll(struct wlp_wss *, struct sk_buff *,
					struct wlp_neighbor_e *,
					struct wlp_uuid *);
extern void wlp_handle_c1_frame(struct work_struct *);
extern void wlp_handle_c3_frame(struct work_struct *);
extern int wlp_parse_c3c4_frame(struct wlp *, struct sk_buff *,
				struct wlp_uuid *, u8 *,
				struct uwb_mac_addr *);
extern int wlp_parse_f0(struct wlp *, struct sk_buff *);
extern int wlp_send_assoc_frame(struct wlp *, struct wlp_wss *,
				struct uwb_dev_addr *, enum wlp_assoc_type);
extern ssize_t wlp_get_version(struct wlp *, struct wlp_attr_version *,
			       u8 *, ssize_t);
extern ssize_t wlp_get_wssid(struct wlp *, struct wlp_attr_wssid *,
			     struct wlp_uuid *, ssize_t);
extern int __wlp_alloc_device_info(struct wlp *);
extern int __wlp_setup_device_info(struct wlp *);

extern struct wlp_wss_attribute wss_attribute_properties;
extern struct wlp_wss_attribute wss_attribute_members;
extern struct wlp_wss_attribute wss_attribute_state;

static inline
size_t wlp_wss_uuid_print(char *buf, size_t bufsize, struct wlp_uuid *uuid)
{
	size_t result;

	result = scnprintf(buf, bufsize,
			  "%02x:%02x:%02x:%02x:%02x:%02x:"
			  "%02x:%02x:%02x:%02x:%02x:%02x:"
			  "%02x:%02x:%02x:%02x",
			  uuid->data[0], uuid->data[1],
			  uuid->data[2], uuid->data[3],
			  uuid->data[4], uuid->data[5],
			  uuid->data[6], uuid->data[7],
			  uuid->data[8], uuid->data[9],
			  uuid->data[10], uuid->data[11],
			  uuid->data[12], uuid->data[13],
			  uuid->data[14], uuid->data[15]);
	return result;
}

/**
 * FIXME: How should a nonce be displayed?
 */
static inline
size_t wlp_wss_nonce_print(char *buf, size_t bufsize, struct wlp_nonce *nonce)
{
	size_t result;

	result = scnprintf(buf, bufsize,
			  "%02x %02x %02x %02x %02x %02x "
			  "%02x %02x %02x %02x %02x %02x "
			  "%02x %02x %02x %02x",
			  nonce->data[0], nonce->data[1],
			  nonce->data[2], nonce->data[3],
			  nonce->data[4], nonce->data[5],
			  nonce->data[6], nonce->data[7],
			  nonce->data[8], nonce->data[9],
			  nonce->data[10], nonce->data[11],
			  nonce->data[12], nonce->data[13],
			  nonce->data[14], nonce->data[15]);
	return result;
}


static inline
void wlp_session_cb(struct wlp *wlp)
{
	struct completion *completion = wlp->session->cb_priv;
	complete(completion);
}

static inline
int wlp_uuid_is_set(struct wlp_uuid *uuid)
{
	struct wlp_uuid zero_uuid = { .data = { 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00} };

	if (!memcmp(uuid, &zero_uuid, sizeof(*uuid)))
		return 0;
	return 1;
}

#endif /* __WLP_INTERNAL_H__ */
