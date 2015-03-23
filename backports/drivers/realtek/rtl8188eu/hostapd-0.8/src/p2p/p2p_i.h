/*
 * P2P - Internal definitions for P2P module
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef P2P_I_H
#define P2P_I_H

#include "utils/list.h"
#include "p2p.h"

/* TODO: add removal of expired P2P device entries */

enum p2p_go_state {
	UNKNOWN_GO,
	LOCAL_GO,
	REMOTE_GO
};

/**
 * struct p2p_device - P2P Device data (internal to P2P module)
 */
struct p2p_device {
	struct dl_list list;
	struct os_time last_seen;
	int listen_freq;
	int level;
	enum p2p_wps_method wps_method;

	struct p2p_peer_info info;

	/*
	 * If the peer was discovered based on an interface address (e.g., GO
	 * from Beacon/Probe Response), the interface address is stored here.
	 * p2p_device_addr must still be set in such a case to the unique
	 * identifier for the P2P Device.
	 */
	u8 interface_addr[ETH_ALEN];

	/*
	 * P2P Device Address of the GO in whose group this P2P Device is a
	 * client.
	 */
	u8 member_in_go_dev[ETH_ALEN];

	/*
	 * P2P Interface Address of the GO in whose group this P2P Device is a
	 * client.
	 */
	u8 member_in_go_iface[ETH_ALEN];

	int go_neg_req_sent;
	enum p2p_go_state go_state;
	u8 dialog_token;
	u8 intended_addr[ETH_ALEN];

	char country[3];
	struct p2p_channels channels;
	int oper_freq;
	u8 oper_ssid[32];
	size_t oper_ssid_len;

	/**
	 * req_config_methods - Pending provisioning discovery methods
	 */
	u16 req_config_methods;

#define P2P_DEV_PROBE_REQ_ONLY BIT(0)
#define P2P_DEV_REPORTED BIT(1)
#define P2P_DEV_NOT_YET_READY BIT(2)
#define P2P_DEV_SD_INFO BIT(3)
#define P2P_DEV_SD_SCHEDULE BIT(4)
#define P2P_DEV_PD_PEER_DISPLAY BIT(5)
#define P2P_DEV_PD_PEER_KEYPAD BIT(6)
#define P2P_DEV_USER_REJECTED BIT(7)
#define P2P_DEV_PEER_WAITING_RESPONSE BIT(8)
#define P2P_DEV_PREFER_PERSISTENT_GROUP BIT(9)
#define P2P_DEV_WAIT_GO_NEG_RESPONSE BIT(10)
#define P2P_DEV_WAIT_GO_NEG_CONFIRM BIT(11)
#define P2P_DEV_GROUP_CLIENT_ONLY BIT(12)
#define P2P_DEV_FORCE_FREQ BIT(13)
#define P2P_DEV_PD_FOR_JOIN BIT(14)
#define P2P_DEV_REPORTED_ONCE BIT(15)
	unsigned int flags;

	int status; /* enum p2p_status_code */
	unsigned int wait_count;
	unsigned int connect_reqs;
	unsigned int invitation_reqs;

	u16 ext_listen_period;
	u16 ext_listen_interval;

	u8 go_timeout;
	u8 client_timeout;
};

struct p2p_sd_query {
	struct p2p_sd_query *next;
	u8 peer[ETH_ALEN];
	int for_all_peers;
	struct wpabuf *tlvs;
};

struct p2p_pending_action_tx {
	unsigned int freq;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	size_t len;
	unsigned int wait_time;
	/* Followed by len octets of the frame */
};

/**
 * struct p2p_data - P2P module data (internal to P2P module)
 */
struct p2p_data {
	/**
	 * cfg - P2P module configuration
	 *
	 * This is included in the same memory allocation with the
	 * struct p2p_data and as such, must not be freed separately.
	 */
	struct p2p_config *cfg;

	/**
	 * state - The current P2P state
	 */
	enum p2p_state {
		/**
		 * P2P_IDLE - Idle
		 */
		P2P_IDLE,

		/**
		 * P2P_SEARCH - Search (Device Discovery)
		 */
		P2P_SEARCH,

		/**
		 * P2P_CONNECT - Trying to start GO Negotiation
		 */
		P2P_CONNECT,

		/**
		 * P2P_CONNECT_LISTEN - Listen during GO Negotiation start
		 */
		P2P_CONNECT_LISTEN,

		/**
		 * P2P_GO_NEG - In GO Negotiation
		 */
		P2P_GO_NEG,

		/**
		 * P2P_LISTEN_ONLY - Listen only
		 */
		P2P_LISTEN_ONLY,

		/**
		 * P2P_WAIT_PEER_CONNECT - Waiting peer in List for GO Neg
		 */
		P2P_WAIT_PEER_CONNECT,

		/**
		 * P2P_WAIT_PEER_IDLE - Waiting peer idle for GO Neg
		 */
		P2P_WAIT_PEER_IDLE,

		/**
		 * P2P_SD_DURING_FIND - Service Discovery during find
		 */
		P2P_SD_DURING_FIND,

		/**
		 * P2P_PROVISIONING - Provisioning (during group formation)
		 */
		P2P_PROVISIONING,

		/**
		 * P2P_PD_DURING_FIND - Provision Discovery during find
		 */
		P2P_PD_DURING_FIND,

		/**
		 * P2P_INVITE - Trying to start Invite
		 */
		P2P_INVITE,

		/**
		 * P2P_INVITE_LISTEN - Listen during Invite
		 */
		P2P_INVITE_LISTEN,
	} state;

	/**
	 * min_disc_int - minDiscoverableInterval
	 */
	int min_disc_int;

	/**
	 * max_disc_int - maxDiscoverableInterval
	 */
	int max_disc_int;

	/**
	 * devices - List of known P2P Device peers
	 */
	struct dl_list devices;

	/**
	 * go_neg_peer - Pointer to GO Negotiation peer
	 */
	struct p2p_device *go_neg_peer;

	/**
	 * invite_peer - Pointer to Invite peer
	 */
	struct p2p_device *invite_peer;

	const u8 *invite_go_dev_addr;
	u8 invite_go_dev_addr_buf[ETH_ALEN];

	/**
	 * sd_peer - Pointer to Service Discovery peer
	 */
	struct p2p_device *sd_peer;

	/**
	 * sd_query - Pointer to Service Discovery query
	 */
	struct p2p_sd_query *sd_query;

	/* GO Negotiation data */

	/**
	 * intended_addr - Local Intended P2P Interface Address
	 *
	 * This address is used during group owner negotiation as the Intended
	 * P2P Interface Address and the group interface will be created with
	 * address as the local address in case of successfully completed
	 * negotiation.
	 */
	u8 intended_addr[ETH_ALEN];

	/**
	 * go_intent - Local GO Intent to be used during GO Negotiation
	 */
	u8 go_intent;

	/**
	 * next_tie_breaker - Next tie-breaker value to use in GO Negotiation
	 */
	u8 next_tie_breaker;

	/**
	 * ssid - Selected SSID for GO Negotiation (if local end will be GO)
	 */
	u8 ssid[32];

	/**
	 * ssid_len - ssid length in octets
	 */
	size_t ssid_len;

	/**
	 * Regulatory class for own operational channel
	 */
	u8 op_reg_class;

	/**
	 * op_channel - Own operational channel
	 */
	u8 op_channel;

	/**
	 * channels - Own supported regulatory classes and channels
	 *
	 * List of supposerted channels per regulatory class. The regulatory
	 * classes are defined in IEEE Std 802.11-2007 Annex J and the
	 * numbering of the clases depends on the configured country code.
	 */
	struct p2p_channels channels;

	enum p2p_pending_action_state {
		P2P_NO_PENDING_ACTION,
		P2P_PENDING_GO_NEG_REQUEST,
		P2P_PENDING_GO_NEG_RESPONSE,
		P2P_PENDING_GO_NEG_RESPONSE_FAILURE,
		P2P_PENDING_GO_NEG_CONFIRM,
		P2P_PENDING_SD,
		P2P_PENDING_PD,
		P2P_PENDING_INVITATION_REQUEST,
		P2P_PENDING_INVITATION_RESPONSE,
		P2P_PENDING_DEV_DISC_REQUEST,
		P2P_PENDING_DEV_DISC_RESPONSE,
		P2P_PENDING_GO_DISC_REQ
	} pending_action_state;

	unsigned int pending_listen_freq;
	unsigned int pending_listen_sec;
	unsigned int pending_listen_usec;

	u8 dev_capab;

	int in_listen;
	int drv_in_listen;

	/**
	 * sd_queries - Pending service discovery queries
	 */
	struct p2p_sd_query *sd_queries;

	/**
	 * srv_update_indic - Service Update Indicator for local services
	 */
	u16 srv_update_indic;

	struct wpabuf *sd_resp; /* Fragmented SD response */
	u8 sd_resp_addr[ETH_ALEN];
	u8 sd_resp_dialog_token;
	size_t sd_resp_pos; /* Offset in sd_resp */
	u8 sd_frag_id;

	struct wpabuf *sd_rx_resp; /* Reassembled SD response */
	u16 sd_rx_update_indic;

	/* P2P Invitation data */
	enum p2p_invite_role inv_role;
	u8 inv_bssid[ETH_ALEN];
	int inv_bssid_set;
	u8 inv_ssid[32];
	size_t inv_ssid_len;
	u8 inv_sa[ETH_ALEN];
	u8 inv_group_bssid[ETH_ALEN];
	u8 *inv_group_bssid_ptr;
	u8 inv_go_dev_addr[ETH_ALEN];
	u8 inv_status;
	int inv_op_freq;
	int inv_persistent;

	enum p2p_discovery_type find_type;
	u8 last_prog_scan_class;
	u8 last_prog_scan_chan;
	int p2p_scan_running;
	enum p2p_after_scan {
		P2P_AFTER_SCAN_NOTHING,
		P2P_AFTER_SCAN_LISTEN,
		P2P_AFTER_SCAN_CONNECT
	} start_after_scan;
	u8 after_scan_peer[ETH_ALEN];
	struct p2p_pending_action_tx *after_scan_tx;

	/* Requested device types for find/search */
	unsigned int num_req_dev_types;
	u8 *req_dev_types;

	struct p2p_group **groups;
	size_t num_groups;

	struct p2p_device *pending_client_disc_go;
	u8 pending_client_disc_addr[ETH_ALEN];
	u8 pending_dev_disc_dialog_token;
	u8 pending_dev_disc_addr[ETH_ALEN];
	int pending_dev_disc_freq;
	unsigned int pending_client_disc_freq;

	int ext_listen_only;
	unsigned int ext_listen_period;
	unsigned int ext_listen_interval;
	unsigned int ext_listen_interval_sec;
	unsigned int ext_listen_interval_usec;

	u8 peer_filter[ETH_ALEN];

	int cross_connect;

	int best_freq_24;
	int best_freq_5;
	int best_freq_overall;

	/**
	 * wps_vendor_ext - WPS Vendor Extensions to add
	 */
	struct wpabuf *wps_vendor_ext[P2P_MAX_WPS_VENDOR_EXT];
};

/**
 * struct p2p_message - Parsed P2P message (or P2P IE)
 */
struct p2p_message {
	struct wpabuf *p2p_attributes;
	struct wpabuf *wps_attributes;

	u8 dialog_token;

	const u8 *capability;
	const u8 *go_intent;
	const u8 *status;
	const u8 *listen_channel;
	const u8 *operating_channel;
	const u8 *channel_list;
	u8 channel_list_len;
	const u8 *config_timeout;
	const u8 *intended_addr;
	const u8 *group_bssid;
	const u8 *invitation_flags;

	const u8 *group_info;
	size_t group_info_len;

	const u8 *group_id;
	size_t group_id_len;

	const u8 *device_id;

	const u8 *manageability;

	const u8 *noa;
	size_t noa_len;

	const u8 *ext_listen_timing;

	const u8 *minor_reason_code;

	/* P2P Device Info */
	const u8 *p2p_device_info;
	size_t p2p_device_info_len;
	const u8 *p2p_device_addr;
	const u8 *pri_dev_type;
	u8 num_sec_dev_types;
	char device_name[33];
	u16 config_methods;

	/* WPS IE */
	u16 dev_password_id;
	u16 wps_config_methods;
	const u8 *wps_pri_dev_type;
	const u8 *wps_sec_dev_type_list;
	size_t wps_sec_dev_type_list_len;
	const u8 *wps_vendor_ext[P2P_MAX_WPS_VENDOR_EXT];
	size_t wps_vendor_ext_len[P2P_MAX_WPS_VENDOR_EXT];
	const u8 *manufacturer;
	size_t manufacturer_len;
	const u8 *model_name;
	size_t model_name_len;
	const u8 *model_number;
	size_t model_number_len;
	const u8 *serial_number;
	size_t serial_number_len;

	/* DS Parameter Set IE */
	const u8 *ds_params;

	/* SSID IE */
	const u8 *ssid;
};


#define P2P_MAX_GROUP_ENTRIES 50

struct p2p_group_info {
	unsigned int num_clients;
	struct p2p_client_info {
		const u8 *p2p_device_addr;
		const u8 *p2p_interface_addr;
		u8 dev_capab;
		u16 config_methods;
		const u8 *pri_dev_type;
		u8 num_sec_dev_types;
		const u8 *sec_dev_types;
		const char *dev_name;
		size_t dev_name_len;
	} client[P2P_MAX_GROUP_ENTRIES];
};


/* p2p_utils.c */
int p2p_random(char *buf, size_t len);
int p2p_channel_to_freq(const char *country, int reg_class, int channel);
int p2p_freq_to_channel(const char *country, unsigned int freq, u8 *reg_class,
			u8 *channel);
void p2p_channels_intersect(const struct p2p_channels *a,
			    const struct p2p_channels *b,
			    struct p2p_channels *res);
int p2p_channels_includes(const struct p2p_channels *channels, u8 reg_class,
			  u8 channel);

/* p2p_parse.c */
int p2p_parse_p2p_ie(const struct wpabuf *buf, struct p2p_message *msg);
int p2p_parse_ies(const u8 *data, size_t len, struct p2p_message *msg);
int p2p_parse(const u8 *data, size_t len, struct p2p_message *msg);
void p2p_parse_free(struct p2p_message *msg);
int p2p_attr_text(struct wpabuf *data, char *buf, char *end);
int p2p_group_info_parse(const u8 *gi, size_t gi_len,
			 struct p2p_group_info *info);

/* p2p_build.c */

struct p2p_noa_desc {
	u8 count_type;
	u32 duration;
	u32 interval;
	u32 start_time;
};

/* p2p_group.c */
const u8 * p2p_group_get_interface_addr(struct p2p_group *group);
u8 p2p_group_presence_req(struct p2p_group *group,
			  const u8 *client_interface_addr,
			  const u8 *noa, size_t noa_len);


void p2p_buf_add_action_hdr(struct wpabuf *buf, u8 subtype, u8 dialog_token);
void p2p_buf_add_public_action_hdr(struct wpabuf *buf, u8 subtype,
				   u8 dialog_token);
u8 * p2p_buf_add_ie_hdr(struct wpabuf *buf);
void p2p_buf_add_status(struct wpabuf *buf, u8 status);
void p2p_buf_add_device_info(struct wpabuf *buf, struct p2p_data *p2p,
			     struct p2p_device *peer);
void p2p_buf_add_device_id(struct wpabuf *buf, const u8 *dev_addr);
void p2p_buf_update_ie_hdr(struct wpabuf *buf, u8 *len);
void p2p_buf_add_capability(struct wpabuf *buf, u8 dev_capab, u8 group_capab);
void p2p_buf_add_go_intent(struct wpabuf *buf, u8 go_intent);
void p2p_buf_add_listen_channel(struct wpabuf *buf, const char *country,
				u8 reg_class, u8 channel);
void p2p_buf_add_operating_channel(struct wpabuf *buf, const char *country,
				   u8 reg_class, u8 channel);
void p2p_buf_add_channel_list(struct wpabuf *buf, const char *country,
			      struct p2p_channels *chan);
void p2p_buf_add_config_timeout(struct wpabuf *buf, u8 go_timeout,
				u8 client_timeout);
void p2p_buf_add_intended_addr(struct wpabuf *buf, const u8 *interface_addr);
void p2p_buf_add_group_bssid(struct wpabuf *buf, const u8 *bssid);
void p2p_buf_add_group_id(struct wpabuf *buf, const u8 *dev_addr,
			  const u8 *ssid, size_t ssid_len);
void p2p_buf_add_invitation_flags(struct wpabuf *buf, u8 flags);
void p2p_buf_add_noa(struct wpabuf *buf, u8 noa_index, u8 opp_ps, u8 ctwindow,
		     struct p2p_noa_desc *desc1, struct p2p_noa_desc *desc2);
void p2p_buf_add_ext_listen_timing(struct wpabuf *buf, u16 period,
				   u16 interval);
void p2p_buf_add_p2p_interface(struct wpabuf *buf, struct p2p_data *p2p);
void p2p_build_wps_ie(struct p2p_data *p2p, struct wpabuf *buf, u16 pw_id,
		      int all_attr);

/* p2p_sd.c */
struct p2p_sd_query * p2p_pending_sd_req(struct p2p_data *p2p,
					 struct p2p_device *dev);
void p2p_free_sd_queries(struct p2p_data *p2p);
void p2p_rx_gas_initial_req(struct p2p_data *p2p, const u8 *sa,
			    const u8 *data, size_t len, int rx_freq);
void p2p_rx_gas_initial_resp(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq);
void p2p_rx_gas_comeback_req(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq);
void p2p_rx_gas_comeback_resp(struct p2p_data *p2p, const u8 *sa,
			      const u8 *data, size_t len, int rx_freq);
int p2p_start_sd(struct p2p_data *p2p, struct p2p_device *dev);

/* p2p_go_neg.c */
int p2p_peer_channels_check(struct p2p_data *p2p, struct p2p_channels *own,
			    struct p2p_device *dev,
			    const u8 *channel_list, size_t channel_list_len);
void p2p_process_go_neg_req(struct p2p_data *p2p, const u8 *sa,
			    const u8 *data, size_t len, int rx_freq);
void p2p_process_go_neg_resp(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq);
void p2p_process_go_neg_conf(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len);
int p2p_connect_send(struct p2p_data *p2p, struct p2p_device *dev);

/* p2p_pd.c */
void p2p_process_prov_disc_req(struct p2p_data *p2p, const u8 *sa,
			       const u8 *data, size_t len, int rx_freq);
void p2p_process_prov_disc_resp(struct p2p_data *p2p, const u8 *sa,
				const u8 *data, size_t len);
int p2p_send_prov_disc_req(struct p2p_data *p2p, struct p2p_device *dev,
			   int join);

/* p2p_invitation.c */
void p2p_process_invitation_req(struct p2p_data *p2p, const u8 *sa,
				const u8 *data, size_t len, int rx_freq);
void p2p_process_invitation_resp(struct p2p_data *p2p, const u8 *sa,
				 const u8 *data, size_t len);
int p2p_invite_send(struct p2p_data *p2p, struct p2p_device *dev,
		    const u8 *go_dev_addr);
void p2p_invitation_req_cb(struct p2p_data *p2p, int success);
void p2p_invitation_resp_cb(struct p2p_data *p2p, int success);

/* p2p_dev_disc.c */
void p2p_process_dev_disc_req(struct p2p_data *p2p, const u8 *sa,
			      const u8 *data, size_t len, int rx_freq);
void p2p_dev_disc_req_cb(struct p2p_data *p2p, int success);
int p2p_send_dev_disc_req(struct p2p_data *p2p, struct p2p_device *dev);
void p2p_dev_disc_resp_cb(struct p2p_data *p2p, int success);
void p2p_process_dev_disc_resp(struct p2p_data *p2p, const u8 *sa,
			       const u8 *data, size_t len);
void p2p_go_disc_req_cb(struct p2p_data *p2p, int success);
void p2p_process_go_disc_req(struct p2p_data *p2p, const u8 *da, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq);

/* p2p.c */
void p2p_set_state(struct p2p_data *p2p, int new_state);
void p2p_set_timeout(struct p2p_data *p2p, unsigned int sec,
		     unsigned int usec);
void p2p_clear_timeout(struct p2p_data *p2p);
void p2p_continue_find(struct p2p_data *p2p);
struct p2p_device * p2p_add_dev_from_go_neg_req(struct p2p_data *p2p,
						const u8 *addr,
						struct p2p_message *msg);
void p2p_add_dev_info(struct p2p_data *p2p, const u8 *addr,
		      struct p2p_device *dev, struct p2p_message *msg);
struct p2p_device * p2p_get_device(struct p2p_data *p2p, const u8 *addr);
struct p2p_device * p2p_get_device_interface(struct p2p_data *p2p,
					     const u8 *addr);
void p2p_go_neg_failed(struct p2p_data *p2p, struct p2p_device *peer,
		       int status);
void p2p_go_complete(struct p2p_data *p2p, struct p2p_device *peer);
int p2p_match_dev_type(struct p2p_data *p2p, struct wpabuf *wps);
int dev_type_list_match(const u8 *dev_type, const u8 *req_dev_type[],
			size_t num_req_dev_type);
struct wpabuf * p2p_build_probe_resp_ies(struct p2p_data *p2p);
void p2p_build_ssid(struct p2p_data *p2p, u8 *ssid, size_t *ssid_len);
int p2p_send_action(struct p2p_data *p2p, unsigned int freq, const u8 *dst,
		    const u8 *src, const u8 *bssid, const u8 *buf,
		    size_t len, unsigned int wait_time);

#endif /* P2P_I_H */
