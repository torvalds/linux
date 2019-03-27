/*
 * Wi-Fi Direct - P2P module
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef P2P_H
#define P2P_H

#include "common/ieee802_11_defs.h"
#include "wps/wps.h"

/* P2P ASP Setup Capability */
#define P2PS_SETUP_NONE 0
#define P2PS_SETUP_NEW BIT(0)
#define P2PS_SETUP_CLIENT BIT(1)
#define P2PS_SETUP_GROUP_OWNER BIT(2)

#define P2PS_WILD_HASH_STR "org.wi-fi.wfds"
#define P2PS_HASH_LEN 6
#define P2P_MAX_QUERY_HASH 6
#define P2PS_FEATURE_CAPAB_CPT_MAX 2

/**
 * P2P_MAX_PREF_CHANNELS - Maximum number of preferred channels
 */
#define P2P_MAX_PREF_CHANNELS 100

/**
 * P2P_MAX_REG_CLASSES - Maximum number of regulatory classes
 */
#define P2P_MAX_REG_CLASSES 15

/**
 * P2P_MAX_REG_CLASS_CHANNELS - Maximum number of channels per regulatory class
 */
#define P2P_MAX_REG_CLASS_CHANNELS 20

/**
 * struct p2p_channels - List of supported channels
 */
struct p2p_channels {
	/**
	 * struct p2p_reg_class - Supported regulatory class
	 */
	struct p2p_reg_class {
		/**
		 * reg_class - Regulatory class (IEEE 802.11-2007, Annex J)
		 */
		u8 reg_class;

		/**
		 * channel - Supported channels
		 */
		u8 channel[P2P_MAX_REG_CLASS_CHANNELS];

		/**
		 * channels - Number of channel entries in use
		 */
		size_t channels;
	} reg_class[P2P_MAX_REG_CLASSES];

	/**
	 * reg_classes - Number of reg_class entries in use
	 */
	size_t reg_classes;
};

enum p2p_wps_method {
	WPS_NOT_READY, WPS_PIN_DISPLAY, WPS_PIN_KEYPAD, WPS_PBC, WPS_NFC,
	WPS_P2PS
};

/**
 * struct p2p_go_neg_results - P2P Group Owner Negotiation results
 */
struct p2p_go_neg_results {
	/**
	 * status - Negotiation result (Status Code)
	 *
	 * 0 (P2P_SC_SUCCESS) indicates success. Non-zero values indicate
	 * failed negotiation.
	 */
	int status;

	/**
	 * role_go - Whether local end is Group Owner
	 */
	int role_go;

	/**
	 * freq - Frequency of the group operational channel in MHz
	 */
	int freq;

	int ht40;

	int vht;

	u8 max_oper_chwidth;

	unsigned int vht_center_freq2;

	/**
	 * ssid - SSID of the group
	 */
	u8 ssid[SSID_MAX_LEN];

	/**
	 * ssid_len - Length of SSID in octets
	 */
	size_t ssid_len;

	/**
	 * psk - WPA pre-shared key (256 bits) (GO only)
	 */
	u8 psk[32];

	/**
	 * psk_set - Whether PSK field is configured (GO only)
	 */
	int psk_set;

	/**
	 * passphrase - WPA2-Personal passphrase for the group (GO only)
	 */
	char passphrase[64];

	/**
	 * peer_device_addr - P2P Device Address of the peer
	 */
	u8 peer_device_addr[ETH_ALEN];

	/**
	 * peer_interface_addr - P2P Interface Address of the peer
	 */
	u8 peer_interface_addr[ETH_ALEN];

	/**
	 * wps_method - WPS method to be used during provisioning
	 */
	enum p2p_wps_method wps_method;

#define P2P_MAX_CHANNELS 50

	/**
	 * freq_list - Zero-terminated list of possible operational channels
	 */
	int freq_list[P2P_MAX_CHANNELS];

	/**
	 * persistent_group - Whether the group should be made persistent
	 * 0 = not persistent
	 * 1 = persistent group without persistent reconnect
	 * 2 = persistent group with persistent reconnect
	 */
	int persistent_group;

	/**
	 * peer_config_timeout - Peer configuration timeout (in 10 msec units)
	 */
	unsigned int peer_config_timeout;
};

struct p2ps_provision {
	/**
	 * pd_seeker - P2PS provision discovery seeker role
	 */
	unsigned int pd_seeker:1;

	/**
	 * status - Remote returned provisioning status code
	 */
	int status;

	/**
	 * adv_id - P2PS Advertisement ID
	 */
	u32 adv_id;

	/**
	 * session_id - P2PS Session ID
	 */
	u32 session_id;

	/**
	 * method - WPS Method (to be) used to establish session
	 */
	u16 method;

	/**
	 * conncap - Connection Capabilities negotiated between P2P peers
	 */
	u8 conncap;

	/**
	 * role - Info about the roles to be used for this connection
	 */
	u8 role;

	/**
	 * session_mac - MAC address of the peer that started the session
	 */
	u8 session_mac[ETH_ALEN];

	/**
	 * adv_mac - MAC address of the peer advertised the service
	 */
	u8 adv_mac[ETH_ALEN];

	/**
	 * cpt_mask - Supported Coordination Protocol Transport mask
	 *
	 * A bitwise mask of supported ASP Coordination Protocol Transports.
	 * This property is set together and corresponds with cpt_priority.
	 */
	u8 cpt_mask;

	/**
	 * cpt_priority - Coordination Protocol Transport priority list
	 *
	 * Priorities of supported ASP Coordination Protocol Transports.
	 * This property is set together and corresponds with cpt_mask.
	 * The CPT priority list is 0 terminated.
	 */
	u8 cpt_priority[P2PS_FEATURE_CAPAB_CPT_MAX + 1];

	/**
	 * force_freq - The only allowed channel frequency in MHz or 0.
	 */
	unsigned int force_freq;

	/**
	 * pref_freq - Preferred operating frequency in MHz or 0.
	 */
	unsigned int pref_freq;

	/**
	 * info - Vendor defined extra Provisioning information
	 */
	char info[0];
};

struct p2ps_advertisement {
	struct p2ps_advertisement *next;

	/**
	 * svc_info - Pointer to (internal) Service defined information
	 */
	char *svc_info;

	/**
	 * id - P2PS Advertisement ID
	 */
	u32 id;

	/**
	 * config_methods - WPS Methods which are allowed for this service
	 */
	u16 config_methods;

	/**
	 * state - Current state of the service: 0 - Out Of Service, 1-255 Vendor defined
	 */
	u8 state;

	/**
	 * auto_accept - Automatically Accept provisioning request if possible.
	 */
	u8 auto_accept;

	/**
	 * hash - 6 octet Service Name has to match against incoming Probe Requests
	 */
	u8 hash[P2PS_HASH_LEN];

	/**
	 * cpt_mask - supported Coordination Protocol Transport mask
	 *
	 * A bitwise mask of supported ASP Coordination Protocol Transports.
	 * This property is set together and corresponds with cpt_priority.
	 */
	u8 cpt_mask;

	/**
	 * cpt_priority - Coordination Protocol Transport priority list
	 *
	 * Priorities of supported ASP Coordinatin Protocol Transports.
	 * This property is set together and corresponds with cpt_mask.
	 * The CPT priority list is 0 terminated.
	 */
	u8 cpt_priority[P2PS_FEATURE_CAPAB_CPT_MAX + 1];

	/**
	 * svc_name - NULL Terminated UTF-8 Service Name, and svc_info storage
	 */
	char svc_name[0];
};


struct p2p_data;

enum p2p_scan_type {
	P2P_SCAN_SOCIAL,
	P2P_SCAN_FULL,
	P2P_SCAN_SPECIFIC,
	P2P_SCAN_SOCIAL_PLUS_ONE
};

#define P2P_MAX_WPS_VENDOR_EXT 10

/**
 * struct p2p_peer_info - P2P peer information
 */
struct p2p_peer_info {
	/**
	 * p2p_device_addr - P2P Device Address of the peer
	 */
	u8 p2p_device_addr[ETH_ALEN];

	/**
	 * pri_dev_type - Primary Device Type
	 */
	u8 pri_dev_type[8];

	/**
	 * device_name - Device Name (0..32 octets encoded in UTF-8)
	 */
	char device_name[WPS_DEV_NAME_MAX_LEN + 1];

	/**
	 * manufacturer - Manufacturer (0..64 octets encoded in UTF-8)
	 */
	char manufacturer[WPS_MANUFACTURER_MAX_LEN + 1];

	/**
	 * model_name - Model Name (0..32 octets encoded in UTF-8)
	 */
	char model_name[WPS_MODEL_NAME_MAX_LEN + 1];

	/**
	 * model_number - Model Number (0..32 octets encoded in UTF-8)
	 */
	char model_number[WPS_MODEL_NUMBER_MAX_LEN + 1];

	/**
	 * serial_number - Serial Number (0..32 octets encoded in UTF-8)
	 */
	char serial_number[WPS_SERIAL_NUMBER_MAX_LEN + 1];

	/**
	 * level - Signal level
	 */
	int level;

	/**
	 * config_methods - WPS Configuration Methods
	 */
	u16 config_methods;

	/**
	 * dev_capab - Device Capabilities
	 */
	u8 dev_capab;

	/**
	 * group_capab - Group Capabilities
	 */
	u8 group_capab;

	/**
	 * wps_sec_dev_type_list - WPS secondary device type list
	 *
	 * This list includes from 0 to 16 Secondary Device Types as indicated
	 * by wps_sec_dev_type_list_len (8 * number of types).
	 */
	u8 wps_sec_dev_type_list[WPS_SEC_DEV_TYPE_MAX_LEN];

	/**
	 * wps_sec_dev_type_list_len - Length of secondary device type list
	 */
	size_t wps_sec_dev_type_list_len;

	struct wpabuf *wps_vendor_ext[P2P_MAX_WPS_VENDOR_EXT];

	/**
	 * wfd_subelems - Wi-Fi Display subelements from WFD IE(s)
	 */
	struct wpabuf *wfd_subelems;

	/**
	 * vendor_elems - Unrecognized vendor elements
	 *
	 * This buffer includes any other vendor element than P2P, WPS, and WFD
	 * IE(s) from the frame that was used to discover the peer.
	 */
	struct wpabuf *vendor_elems;

	/**
	 * p2ps_instance - P2PS Application Service Info
	 */
	struct wpabuf *p2ps_instance;
};

enum p2p_prov_disc_status {
	P2P_PROV_DISC_SUCCESS,
	P2P_PROV_DISC_TIMEOUT,
	P2P_PROV_DISC_REJECTED,
	P2P_PROV_DISC_TIMEOUT_JOIN,
	P2P_PROV_DISC_INFO_UNAVAILABLE,
};

struct p2p_channel {
	u8 op_class;
	u8 chan;
};

/**
 * struct p2p_config - P2P configuration
 *
 * This configuration is provided to the P2P module during initialization with
 * p2p_init().
 */
struct p2p_config {
	/**
	 * country - Country code to use in P2P operations
	 */
	char country[3];

	/**
	 * reg_class - Regulatory class for own listen channel
	 */
	u8 reg_class;

	/**
	 * channel - Own listen channel
	 */
	u8 channel;

	/**
	 * channel_forced - the listen channel was forced by configuration
	 *                  or by control interface and cannot be overridden
	 */
	u8 channel_forced;

	/**
	 * Regulatory class for own operational channel
	 */
	u8 op_reg_class;

	/**
	 * op_channel - Own operational channel
	 */
	u8 op_channel;

	/**
	 * cfg_op_channel - Whether op_channel is hardcoded in configuration
	 */
	u8 cfg_op_channel;

	/**
	 * channels - Own supported regulatory classes and channels
	 *
	 * List of supposerted channels per regulatory class. The regulatory
	 * classes are defined in IEEE Std 802.11-2007 Annex J and the
	 * numbering of the clases depends on the configured country code.
	 */
	struct p2p_channels channels;

	/**
	 * cli_channels - Additional client channels
	 *
	 * This list of channels (if any) will be used when advertising local
	 * channels during GO Negotiation or Invitation for the cases where the
	 * local end may become the client. This may allow the peer to become a
	 * GO on additional channels if it supports these options. The main use
	 * case for this is to include passive-scan channels on devices that may
	 * not know their current location and have configured most channels to
	 * not allow initiation of radition (i.e., another device needs to take
	 * master responsibilities).
	 */
	struct p2p_channels cli_channels;

	/**
	 * num_pref_chan - Number of pref_chan entries
	 */
	unsigned int num_pref_chan;

	/**
	 * pref_chan - Preferred channels for GO Negotiation
	 */
	struct p2p_channel *pref_chan;

	/**
	 * pri_dev_type - Primary Device Type (see WPS)
	 */
	u8 pri_dev_type[8];

	/**
	 * P2P_SEC_DEVICE_TYPES - Maximum number of secondary device types
	 */
#define P2P_SEC_DEVICE_TYPES 5

	/**
	 * sec_dev_type - Optional secondary device types
	 */
	u8 sec_dev_type[P2P_SEC_DEVICE_TYPES][8];

	/**
	 * num_sec_dev_types - Number of sec_dev_type entries
	 */
	size_t num_sec_dev_types;

	/**
	 * dev_addr - P2P Device Address
	 */
	u8 dev_addr[ETH_ALEN];

	/**
	 * dev_name - Device Name
	 */
	char *dev_name;

	char *manufacturer;
	char *model_name;
	char *model_number;
	char *serial_number;

	u8 uuid[16];
	u16 config_methods;

	/**
	 * concurrent_operations - Whether concurrent operations are supported
	 */
	int concurrent_operations;

	/**
	 * max_peers - Maximum number of discovered peers to remember
	 *
	 * If more peers are discovered, older entries will be removed to make
	 * room for the new ones.
	 */
	size_t max_peers;

	/**
	 * p2p_intra_bss - Intra BSS communication is supported
	 */
	int p2p_intra_bss;

	/**
	 * ssid_postfix - Postfix data to add to the SSID
	 *
	 * This data will be added to the end of the SSID after the
	 * DIRECT-<random two octets> prefix.
	 */
	u8 ssid_postfix[SSID_MAX_LEN - 9];

	/**
	 * ssid_postfix_len - Length of the ssid_postfix data
	 */
	size_t ssid_postfix_len;

	/**
	 * max_listen - Maximum listen duration in ms
	 */
	unsigned int max_listen;

	/**
	 * passphrase_len - Passphrase length (8..63)
	 *
	 * This parameter controls the length of the random passphrase that is
	 * generated at the GO.
	 */
	unsigned int passphrase_len;

	/**
	 * cb_ctx - Context to use with callback functions
	 */
	void *cb_ctx;

	/**
	 * debug_print - Debug print
	 * @ctx: Callback context from cb_ctx
	 * @level: Debug verbosity level (MSG_*)
	 * @msg: Debug message
	 */
	void (*debug_print)(void *ctx, int level, const char *msg);


	/* Callbacks to request lower layer driver operations */

	/**
	 * p2p_scan - Request a P2P scan/search
	 * @ctx: Callback context from cb_ctx
	 * @type: Scan type
	 * @freq: Specific frequency (MHz) to scan or 0 for no restriction
	 * @num_req_dev_types: Number of requested device types
	 * @req_dev_types: Array containing requested device types
	 * @dev_id: Device ID to search for or %NULL to find all devices
	 * @pw_id: Device Password ID
	 * Returns: 0 on success, -1 on failure
	 *
	 * This callback function is used to request a P2P scan or search
	 * operation to be completed. Type type argument specifies which type
	 * of scan is to be done. @P2P_SCAN_SOCIAL indicates that only the
	 * social channels (1, 6, 11) should be scanned. @P2P_SCAN_FULL
	 * indicates that all channels are to be scanned. @P2P_SCAN_SPECIFIC
	 * request a scan of a single channel specified by freq.
	 * @P2P_SCAN_SOCIAL_PLUS_ONE request scan of all the social channels
	 * plus one extra channel specified by freq.
	 *
	 * The full scan is used for the initial scan to find group owners from
	 * all. The other types are used during search phase scan of the social
	 * channels (with potential variation if the Listen channel of the
	 * target peer is known or if other channels are scanned in steps).
	 *
	 * The scan results are returned after this call by calling
	 * p2p_scan_res_handler() for each scan result that has a P2P IE and
	 * then calling p2p_scan_res_handled() to indicate that all scan
	 * results have been indicated.
	 */
	int (*p2p_scan)(void *ctx, enum p2p_scan_type type, int freq,
			unsigned int num_req_dev_types,
			const u8 *req_dev_types, const u8 *dev_id, u16 pw_id);

	/**
	 * send_probe_resp - Transmit a Probe Response frame
	 * @ctx: Callback context from cb_ctx
	 * @buf: Probe Response frame (including the header and body)
	 * @freq: Forced frequency (in MHz) to use or 0.
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is used to reply to Probe Request frames that were
	 * indicated with a call to p2p_probe_req_rx(). The response is to be
	 * sent on the same channel, unless otherwise specified, or to be
	 * dropped if the driver is not listening to Probe Request frames
	 * anymore.
	 *
	 * Alternatively, the responsibility for building the Probe Response
	 * frames in Listen state may be in another system component in which
	 * case this function need to be implemented (i.e., the function
	 * pointer can be %NULL). The WPS and P2P IEs to be added for Probe
	 * Response frames in such a case are available from the
	 * start_listen() callback. It should be noted that the received Probe
	 * Request frames must be indicated by calling p2p_probe_req_rx() even
	 * if this send_probe_resp() is not used.
	 */
	int (*send_probe_resp)(void *ctx, const struct wpabuf *buf,
			       unsigned int freq);

	/**
	 * send_action - Transmit an Action frame
	 * @ctx: Callback context from cb_ctx
	 * @freq: Frequency in MHz for the channel on which to transmit
	 * @dst: Destination MAC address (Address 1)
	 * @src: Source MAC address (Address 2)
	 * @bssid: BSSID (Address 3)
	 * @buf: Frame body (starting from Category field)
	 * @len: Length of buf in octets
	 * @wait_time: How many msec to wait for a response frame
	 * Returns: 0 on success, -1 on failure
	 *
	 * The Action frame may not be transmitted immediately and the status
	 * of the transmission must be reported by calling
	 * p2p_send_action_cb() once the frame has either been transmitted or
	 * it has been dropped due to excessive retries or other failure to
	 * transmit.
	 */
	int (*send_action)(void *ctx, unsigned int freq, const u8 *dst,
			   const u8 *src, const u8 *bssid, const u8 *buf,
			   size_t len, unsigned int wait_time);

	/**
	 * send_action_done - Notify that Action frame sequence was completed
	 * @ctx: Callback context from cb_ctx
	 *
	 * This function is called when the Action frame sequence that was
	 * started with send_action() has been completed, i.e., when there is
	 * no need to wait for a response from the destination peer anymore.
	 */
	void (*send_action_done)(void *ctx);

	/**
	 * start_listen - Start Listen state
	 * @ctx: Callback context from cb_ctx
	 * @freq: Frequency of the listen channel in MHz
	 * @duration: Duration for the Listen state in milliseconds
	 * @probe_resp_ie: IE(s) to be added to Probe Response frames
	 * Returns: 0 on success, -1 on failure
	 *
	 * This Listen state may not start immediately since the driver may
	 * have other pending operations to complete first. Once the Listen
	 * state has started, p2p_listen_cb() must be called to notify the P2P
	 * module. Once the Listen state is stopped, p2p_listen_end() must be
	 * called to notify the P2P module that the driver is not in the Listen
	 * state anymore.
	 *
	 * If the send_probe_resp() is not used for generating the response,
	 * the IEs from probe_resp_ie need to be added to the end of the Probe
	 * Response frame body. If send_probe_resp() is used, the probe_resp_ie
	 * information can be ignored.
	 */
	int (*start_listen)(void *ctx, unsigned int freq,
			    unsigned int duration,
			    const struct wpabuf *probe_resp_ie);
	/**
	 * stop_listen - Stop Listen state
	 * @ctx: Callback context from cb_ctx
	 *
	 * This callback can be used to stop a Listen state operation that was
	 * previously requested with start_listen().
	 */
	void (*stop_listen)(void *ctx);

	/**
	 * get_noa - Get current Notice of Absence attribute payload
	 * @ctx: Callback context from cb_ctx
	 * @interface_addr: P2P Interface Address of the GO
	 * @buf: Buffer for returning NoA
	 * @buf_len: Buffer length in octets
	 * Returns: Number of octets used in buf, 0 to indicate no NoA is being
	 * advertized, or -1 on failure
	 *
	 * This function is used to fetch the current Notice of Absence
	 * attribute value from GO.
	 */
	int (*get_noa)(void *ctx, const u8 *interface_addr, u8 *buf,
		       size_t buf_len);

	/* Callbacks to notify events to upper layer management entity */

	/**
	 * dev_found - Notification of a found P2P Device
	 * @ctx: Callback context from cb_ctx
	 * @addr: Source address of the message triggering this notification
	 * @info: P2P peer information
	 * @new_device: Inform if the peer is newly found
	 *
	 * This callback is used to notify that a new P2P Device has been
	 * found. This may happen, e.g., during Search state based on scan
	 * results or during Listen state based on receive Probe Request and
	 * Group Owner Negotiation Request.
	 */
	void (*dev_found)(void *ctx, const u8 *addr,
			  const struct p2p_peer_info *info,
			  int new_device);

	/**
	 * dev_lost - Notification of a lost P2P Device
	 * @ctx: Callback context from cb_ctx
	 * @dev_addr: P2P Device Address of the lost P2P Device
	 *
	 * This callback is used to notify that a P2P Device has been deleted.
	 */
	void (*dev_lost)(void *ctx, const u8 *dev_addr);

	/**
	 * find_stopped - Notification of a p2p_find operation stopping
	 * @ctx: Callback context from cb_ctx
	 */
	void (*find_stopped)(void *ctx);

	/**
	 * go_neg_req_rx - Notification of a receive GO Negotiation Request
	 * @ctx: Callback context from cb_ctx
	 * @src: Source address of the message triggering this notification
	 * @dev_passwd_id: WPS Device Password ID
	 * @go_intent: Peer's GO Intent
	 *
	 * This callback is used to notify that a P2P Device is requesting
	 * group owner negotiation with us, but we do not have all the
	 * necessary information to start GO Negotiation. This indicates that
	 * the local user has not authorized the connection yet by providing a
	 * PIN or PBC button press. This information can be provided with a
	 * call to p2p_connect().
	 */
	void (*go_neg_req_rx)(void *ctx, const u8 *src, u16 dev_passwd_id,
			      u8 go_intent);

	/**
	 * go_neg_completed - Notification of GO Negotiation results
	 * @ctx: Callback context from cb_ctx
	 * @res: GO Negotiation results
	 *
	 * This callback is used to notify that Group Owner Negotiation has
	 * been completed. Non-zero struct p2p_go_neg_results::status indicates
	 * failed negotiation. In case of success, this function is responsible
	 * for creating a new group interface (or using the existing interface
	 * depending on driver features), setting up the group interface in
	 * proper mode based on struct p2p_go_neg_results::role_go and
	 * initializing WPS provisioning either as a Registrar (if GO) or as an
	 * Enrollee. Successful WPS provisioning must be indicated by calling
	 * p2p_wps_success_cb(). The callee is responsible for timing out group
	 * formation if WPS provisioning cannot be completed successfully
	 * within 15 seconds.
	 */
	void (*go_neg_completed)(void *ctx, struct p2p_go_neg_results *res);

	/**
	 * sd_request - Callback on Service Discovery Request
	 * @ctx: Callback context from cb_ctx
	 * @freq: Frequency (in MHz) of the channel
	 * @sa: Source address of the request
	 * @dialog_token: Dialog token
	 * @update_indic: Service Update Indicator from the source of request
	 * @tlvs: P2P Service Request TLV(s)
	 * @tlvs_len: Length of tlvs buffer in octets
	 *
	 * This callback is used to indicate reception of a service discovery
	 * request. Response to the query must be indicated by calling
	 * p2p_sd_response() with the context information from the arguments to
	 * this callback function.
	 *
	 * This callback handler can be set to %NULL to indicate that service
	 * discovery is not supported.
	 */
	void (*sd_request)(void *ctx, int freq, const u8 *sa, u8 dialog_token,
			   u16 update_indic, const u8 *tlvs, size_t tlvs_len);

	/**
	 * sd_response - Callback on Service Discovery Response
	 * @ctx: Callback context from cb_ctx
	 * @sa: Source address of the request
	 * @update_indic: Service Update Indicator from the source of response
	 * @tlvs: P2P Service Response TLV(s)
	 * @tlvs_len: Length of tlvs buffer in octets
	 *
	 * This callback is used to indicate reception of a service discovery
	 * response. This callback handler can be set to %NULL if no service
	 * discovery requests are used. The information provided with this call
	 * is replies to the queries scheduled with p2p_sd_request().
	 */
	void (*sd_response)(void *ctx, const u8 *sa, u16 update_indic,
			    const u8 *tlvs, size_t tlvs_len);

	/**
	 * prov_disc_req - Callback on Provisiong Discovery Request
	 * @ctx: Callback context from cb_ctx
	 * @peer: Source address of the request
	 * @config_methods: Requested WPS Config Method
	 * @dev_addr: P2P Device Address of the found P2P Device
	 * @pri_dev_type: Primary Device Type
	 * @dev_name: Device Name
	 * @supp_config_methods: Supported configuration Methods
	 * @dev_capab: Device Capabilities
	 * @group_capab: Group Capabilities
	 * @group_id: P2P Group ID (or %NULL if not included)
	 * @group_id_len: Length of P2P Group ID
	 *
	 * This callback is used to indicate reception of a Provision Discovery
	 * Request frame that the P2P module accepted.
	 */
	void (*prov_disc_req)(void *ctx, const u8 *peer, u16 config_methods,
			      const u8 *dev_addr, const u8 *pri_dev_type,
			      const char *dev_name, u16 supp_config_methods,
			      u8 dev_capab, u8 group_capab,
			      const u8 *group_id, size_t group_id_len);

	/**
	 * prov_disc_resp - Callback on Provisiong Discovery Response
	 * @ctx: Callback context from cb_ctx
	 * @peer: Source address of the response
	 * @config_methods: Value from p2p_prov_disc_req() or 0 on failure
	 *
	 * This callback is used to indicate reception of a Provision Discovery
	 * Response frame for a pending request scheduled with
	 * p2p_prov_disc_req(). This callback handler can be set to %NULL if
	 * provision discovery is not used.
	 */
	void (*prov_disc_resp)(void *ctx, const u8 *peer, u16 config_methods);

	/**
	 * prov_disc_fail - Callback on Provision Discovery failure
	 * @ctx: Callback context from cb_ctx
	 * @peer: Source address of the response
	 * @status: Cause of failure, will not be %P2P_PROV_DISC_SUCCESS
	 * @adv_id: If non-zero, then the adv_id of the PD Request
	 * @adv_mac: P2P Device Address of the advertizer
	 * @deferred_session_resp: Deferred session response sent by advertizer
	 *
	 * This callback is used to indicate either a failure or no response
	 * to an earlier provision discovery request.
	 *
	 * This callback handler can be set to %NULL if provision discovery
	 * is not used or failures do not need to be indicated.
	 */
	void (*prov_disc_fail)(void *ctx, const u8 *peer,
			       enum p2p_prov_disc_status status,
			       u32 adv_id, const u8 *adv_mac,
			       const char *deferred_session_resp);

	/**
	 * invitation_process - Optional callback for processing Invitations
	 * @ctx: Callback context from cb_ctx
	 * @sa: Source address of the Invitation Request
	 * @bssid: P2P Group BSSID from the request or %NULL if not included
	 * @go_dev_addr: GO Device Address from P2P Group ID
	 * @ssid: SSID from P2P Group ID
	 * @ssid_len: Length of ssid buffer in octets
	 * @go: Variable for returning whether the local end is GO in the group
	 * @group_bssid: Buffer for returning P2P Group BSSID (if local end GO)
	 * @force_freq: Variable for returning forced frequency for the group
	 * @persistent_group: Whether this is an invitation to reinvoke a
	 *	persistent group (instead of invitation to join an active
	 *	group)
	 * @channels: Available operating channels for the group
	 * @dev_pw_id: Device Password ID for NFC static handover or -1 if not
	 *	used
	 * Returns: Status code (P2P_SC_*)
	 *
	 * This optional callback can be used to implement persistent reconnect
	 * by allowing automatic restarting of persistent groups without user
	 * interaction. If this callback is not implemented (i.e., is %NULL),
	 * the received Invitation Request frames are replied with
	 * %P2P_SC_REQ_RECEIVED status and indicated to upper layer with the
	 * invitation_result() callback.
	 *
	 * If the requested parameters are acceptable and the group is known,
	 * %P2P_SC_SUCCESS may be returned. If the requested group is unknown,
	 * %P2P_SC_FAIL_UNKNOWN_GROUP should be returned. %P2P_SC_REQ_RECEIVED
	 * can be returned if there is not enough data to provide immediate
	 * response, i.e., if some sort of user interaction is needed. The
	 * invitation_received() callback will be called in that case
	 * immediately after this call.
	 */
	u8 (*invitation_process)(void *ctx, const u8 *sa, const u8 *bssid,
				 const u8 *go_dev_addr, const u8 *ssid,
				 size_t ssid_len, int *go, u8 *group_bssid,
				 int *force_freq, int persistent_group,
				 const struct p2p_channels *channels,
				 int dev_pw_id);

	/**
	 * invitation_received - Callback on Invitation Request RX
	 * @ctx: Callback context from cb_ctx
	 * @sa: Source address of the Invitation Request
	 * @bssid: P2P Group BSSID or %NULL if not received
	 * @ssid: SSID of the group
	 * @ssid_len: Length of ssid in octets
	 * @go_dev_addr: GO Device Address
	 * @status: Response Status
	 * @op_freq: Operational frequency for the group
	 *
	 * This callback is used to indicate sending of an Invitation Response
	 * for a received Invitation Request. If status == 0 (success), the
	 * upper layer code is responsible for starting the group. status == 1
	 * indicates need to get user authorization for the group. Other status
	 * values indicate that the invitation request was rejected.
	 */
	void (*invitation_received)(void *ctx, const u8 *sa, const u8 *bssid,
				    const u8 *ssid, size_t ssid_len,
				    const u8 *go_dev_addr, u8 status,
				    int op_freq);

	/**
	 * invitation_result - Callback on Invitation result
	 * @ctx: Callback context from cb_ctx
	 * @status: Negotiation result (Status Code)
	 * @bssid: P2P Group BSSID or %NULL if not received
	 * @channels: Available operating channels for the group
	 * @addr: Peer address
	 * @freq: Frequency (in MHz) indicated during invitation or 0
	 * @peer_oper_freq: Operating frequency (in MHz) advertized by the peer
	 * during invitation or 0
	 *
	 * This callback is used to indicate result of an Invitation procedure
	 * started with a call to p2p_invite(). The indicated status code is
	 * the value received from the peer in Invitation Response with 0
	 * (P2P_SC_SUCCESS) indicating success or -1 to indicate a timeout or a
	 * local failure in transmitting the Invitation Request.
	 */
	void (*invitation_result)(void *ctx, int status, const u8 *bssid,
				  const struct p2p_channels *channels,
				  const u8 *addr, int freq, int peer_oper_freq);

	/**
	 * go_connected - Check whether we are connected to a GO
	 * @ctx: Callback context from cb_ctx
	 * @dev_addr: P2P Device Address of a GO
	 * Returns: 1 if we are connected as a P2P client to the specified GO
	 * or 0 if not.
	 */
	int (*go_connected)(void *ctx, const u8 *dev_addr);

	/**
	 * presence_resp - Callback on Presence Response
	 * @ctx: Callback context from cb_ctx
	 * @src: Source address (GO's P2P Interface Address)
	 * @status: Result of the request (P2P_SC_*)
	 * @noa: Returned NoA value
	 * @noa_len: Length of the NoA buffer in octets
	 */
	void (*presence_resp)(void *ctx, const u8 *src, u8 status,
			      const u8 *noa, size_t noa_len);

	/**
	 * is_concurrent_session_active - Check whether concurrent session is
	 * active on other virtual interfaces
	 * @ctx: Callback context from cb_ctx
	 * Returns: 1 if concurrent session is active on other virtual interface
	 * or 0 if not.
	 */
	int (*is_concurrent_session_active)(void *ctx);

	/**
	 * is_p2p_in_progress - Check whether P2P operation is in progress
	 * @ctx: Callback context from cb_ctx
	 * Returns: 1 if P2P operation (e.g., group formation) is in progress
	 * or 0 if not.
	 */
	int (*is_p2p_in_progress)(void *ctx);

	/**
	 * Determine if we have a persistent group we share with remote peer
	 * and allocate interface for this group if needed
	 * @ctx: Callback context from cb_ctx
	 * @addr: Peer device address to search for
	 * @ssid: Persistent group SSID or %NULL if any
	 * @ssid_len: Length of @ssid
	 * @go_dev_addr: Buffer for returning GO P2P Device Address
	 * @ret_ssid: Buffer for returning group SSID
	 * @ret_ssid_len: Buffer for returning length of @ssid
	 * @intended_iface_addr: Buffer for returning intended iface address
	 * Returns: 1 if a matching persistent group was found, 0 otherwise
	 */
	int (*get_persistent_group)(void *ctx, const u8 *addr, const u8 *ssid,
				    size_t ssid_len, u8 *go_dev_addr,
				    u8 *ret_ssid, size_t *ret_ssid_len,
				    u8 *intended_iface_addr);

	/**
	 * Get information about a possible local GO role
	 * @ctx: Callback context from cb_ctx
	 * @intended_addr: Buffer for returning intended GO interface address
	 * @ssid: Buffer for returning group SSID
	 * @ssid_len: Buffer for returning length of @ssid
	 * @group_iface: Buffer for returning whether a separate group interface
	 *	would be used
	 * @freq: Variable for returning the current operating frequency of a
	 *	currently running P2P GO.
	 * Returns: 1 if GO info found, 0 otherwise
	 *
	 * This is used to compose New Group settings (SSID, and intended
	 * address) during P2PS provisioning if results of provisioning *might*
	 * result in our being an autonomous GO.
	 */
	int (*get_go_info)(void *ctx, u8 *intended_addr,
			   u8 *ssid, size_t *ssid_len, int *group_iface,
			   unsigned int *freq);

	/**
	 * remove_stale_groups - Remove stale P2PS groups
	 *
	 * Because P2PS stages *potential* GOs, and remote devices can remove
	 * credentials unilaterally, we need to make sure we don't let stale
	 * unusable groups build up.
	 */
	int (*remove_stale_groups)(void *ctx, const u8 *peer, const u8 *go,
				   const u8 *ssid, size_t ssid_len);

	/**
	 * p2ps_prov_complete - P2PS provisioning complete
	 *
	 * When P2PS provisioning completes (successfully or not) we must
	 * transmit all of the results to the upper layers.
	 */
	void (*p2ps_prov_complete)(void *ctx, u8 status, const u8 *dev,
				   const u8 *adv_mac, const u8 *ses_mac,
				   const u8 *grp_mac, u32 adv_id, u32 ses_id,
				   u8 conncap, int passwd_id,
				   const u8 *persist_ssid,
				   size_t persist_ssid_size, int response_done,
				   int prov_start, const char *session_info,
				   const u8 *feat_cap, size_t feat_cap_len,
				   unsigned int freq, const u8 *group_ssid,
				   size_t group_ssid_len);

	/**
	 * prov_disc_resp_cb - Callback for indicating completion of PD Response
	 * @ctx: Callback context from cb_ctx
	 * Returns: 1 if operation was started, 0 otherwise
	 *
	 * This callback can be used to perform any pending actions after
	 * provisioning. It is mainly used for P2PS pending group creation.
	 */
	int (*prov_disc_resp_cb)(void *ctx);

	/**
	 * p2ps_group_capability - Determine group capability
	 * @ctx: Callback context from cb_ctx
	 * @incoming: Peer requested roles, expressed with P2PS_SETUP_* bitmap.
	 * @role: Local roles, expressed with P2PS_SETUP_* bitmap.
	 * @force_freq: Variable for returning forced frequency for the group.
	 * @pref_freq: Variable for returning preferred frequency for the group.
	 * Returns: P2PS_SETUP_* bitmap of group capability result.
	 *
	 * This function can be used to determine group capability and
	 * frequencies based on information from P2PS PD exchange and the
	 * current state of ongoing groups and driver capabilities.
	 */
	u8 (*p2ps_group_capability)(void *ctx, u8 incoming, u8 role,
				    unsigned int *force_freq,
				    unsigned int *pref_freq);

	/**
	 * get_pref_freq_list - Get preferred frequency list for an interface
	 * @ctx: Callback context from cb_ctx
	 * @go: Whether the use if for GO role
	 * @len: Length of freq_list in entries (both IN and OUT)
	 * @freq_list: Buffer for returning the preferred frequencies (MHz)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function can be used to query the preferred frequency list from
	 * the driver specific to a particular interface type.
	 */
	int (*get_pref_freq_list)(void *ctx, int go,
				  unsigned int *len, unsigned int *freq_list);
};


/* P2P module initialization/deinitialization */

/**
 * p2p_init - Initialize P2P module
 * @cfg: P2P module configuration
 * Returns: Pointer to private data or %NULL on failure
 *
 * This function is used to initialize global P2P module context (one per
 * device). The P2P module will keep a copy of the configuration data, so the
 * caller does not need to maintain this structure. However, the callback
 * functions and the context parameters to them must be kept available until
 * the P2P module is deinitialized with p2p_deinit().
 */
struct p2p_data * p2p_init(const struct p2p_config *cfg);

/**
 * p2p_deinit - Deinitialize P2P module
 * @p2p: P2P module context from p2p_init()
 */
void p2p_deinit(struct p2p_data *p2p);

/**
 * p2p_flush - Flush P2P module state
 * @p2p: P2P module context from p2p_init()
 *
 * This command removes the P2P module state like peer device entries.
 */
void p2p_flush(struct p2p_data *p2p);

/**
 * p2p_unauthorize - Unauthorize the specified peer device
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P peer entry to be unauthorized
 * Returns: 0 on success, -1 on failure
 *
 * This command removes any connection authorization from the specified P2P
 * peer device address. This can be used, e.g., to cancel effect of a previous
 * p2p_authorize() or p2p_connect() call that has not yet resulted in completed
 * GO Negotiation.
 */
int p2p_unauthorize(struct p2p_data *p2p, const u8 *addr);

/**
 * p2p_set_dev_name - Set device name
 * @p2p: P2P module context from p2p_init()
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to update the P2P module configuration with
 * information that was not available at the time of the p2p_init() call.
 */
int p2p_set_dev_name(struct p2p_data *p2p, const char *dev_name);

int p2p_set_manufacturer(struct p2p_data *p2p, const char *manufacturer);
int p2p_set_model_name(struct p2p_data *p2p, const char *model_name);
int p2p_set_model_number(struct p2p_data *p2p, const char *model_number);
int p2p_set_serial_number(struct p2p_data *p2p, const char *serial_number);

void p2p_set_config_methods(struct p2p_data *p2p, u16 config_methods);
void p2p_set_uuid(struct p2p_data *p2p, const u8 *uuid);

/**
 * p2p_set_pri_dev_type - Set primary device type
 * @p2p: P2P module context from p2p_init()
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to update the P2P module configuration with
 * information that was not available at the time of the p2p_init() call.
 */
int p2p_set_pri_dev_type(struct p2p_data *p2p, const u8 *pri_dev_type);

/**
 * p2p_set_sec_dev_types - Set secondary device types
 * @p2p: P2P module context from p2p_init()
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to update the P2P module configuration with
 * information that was not available at the time of the p2p_init() call.
 */
int p2p_set_sec_dev_types(struct p2p_data *p2p, const u8 dev_types[][8],
			  size_t num_dev_types);

int p2p_set_country(struct p2p_data *p2p, const char *country);


/* Commands from upper layer management entity */

enum p2p_discovery_type {
	P2P_FIND_START_WITH_FULL,
	P2P_FIND_ONLY_SOCIAL,
	P2P_FIND_PROGRESSIVE
};

/**
 * p2p_find - Start P2P Find (Device Discovery)
 * @p2p: P2P module context from p2p_init()
 * @timeout: Timeout for find operation in seconds or 0 for no timeout
 * @type: Device Discovery type
 * @num_req_dev_types: Number of requested device types
 * @req_dev_types: Requested device types array, must be an array
 *	containing num_req_dev_types * WPS_DEV_TYPE_LEN bytes; %NULL if no
 *	requested device types.
 * @dev_id: Device ID to search for or %NULL to find all devices
 * @search_delay: Extra delay in milliseconds between search iterations
 * @seek_count: Number of ASP Service Strings in the seek_string array
 * @seek_string: ASP Service Strings to query for in Probe Requests
 * @freq: Requested first scan frequency (in MHz) to modify type ==
 *	P2P_FIND_START_WITH_FULL behavior. 0 = Use normal full scan.
 *	If p2p_find is already in progress, this parameter is ignored and full
 *	scan will be executed.
 * Returns: 0 on success, -1 on failure
 */
int p2p_find(struct p2p_data *p2p, unsigned int timeout,
	     enum p2p_discovery_type type,
	     unsigned int num_req_dev_types, const u8 *req_dev_types,
	     const u8 *dev_id, unsigned int search_delay,
	     u8 seek_count, const char **seek_string, int freq);

/**
 * p2p_notify_scan_trigger_status - Indicate scan trigger status
 * @p2p: P2P module context from p2p_init()
 * @status: 0 on success, -1 on failure
 */
void p2p_notify_scan_trigger_status(struct p2p_data *p2p, int status);

/**
 * p2p_stop_find - Stop P2P Find (Device Discovery)
 * @p2p: P2P module context from p2p_init()
 */
void p2p_stop_find(struct p2p_data *p2p);

/**
 * p2p_stop_find_for_freq - Stop P2P Find for next oper on specific freq
 * @p2p: P2P module context from p2p_init()
 * @freq: Frequency in MHz for next operation
 *
 * This is like p2p_stop_find(), but Listen state is not stopped if we are
 * already on the same frequency.
 */
void p2p_stop_find_for_freq(struct p2p_data *p2p, int freq);

/**
 * p2p_listen - Start P2P Listen state for specified duration
 * @p2p: P2P module context from p2p_init()
 * @timeout: Listen state duration in milliseconds
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to request the P2P module to keep the device
 * discoverable on the listen channel for an extended set of time. At least in
 * its current form, this is mainly used for testing purposes and may not be of
 * much use for normal P2P operations.
 */
int p2p_listen(struct p2p_data *p2p, unsigned int timeout);

/**
 * p2p_stop_listen - Stop P2P Listen
 * @p2p: P2P module context from p2p_init()
 */
void p2p_stop_listen(struct p2p_data *p2p);

/**
 * p2p_connect - Start P2P group formation (GO negotiation)
 * @p2p: P2P module context from p2p_init()
 * @peer_addr: MAC address of the peer P2P client
 * @wps_method: WPS method to be used in provisioning
 * @go_intent: Local GO intent value (1..15)
 * @own_interface_addr: Intended interface address to use with the group
 * @force_freq: The only allowed channel frequency in MHz or 0
 * @persistent_group: Whether to create a persistent group (0 = no, 1 =
 * persistent group without persistent reconnect, 2 = persistent group with
 * persistent reconnect)
 * @force_ssid: Forced SSID for the group if we become GO or %NULL to generate
 *	a new SSID
 * @force_ssid_len: Length of $force_ssid buffer
 * @pd_before_go_neg: Whether to send Provision Discovery prior to GO
 *	Negotiation as an interoperability workaround when initiating group
 *	formation
 * @pref_freq: Preferred operating frequency in MHz or 0 (this is only used if
 *	force_freq == 0)
 * Returns: 0 on success, -1 on failure
 */
int p2p_connect(struct p2p_data *p2p, const u8 *peer_addr,
		enum p2p_wps_method wps_method,
		int go_intent, const u8 *own_interface_addr,
		unsigned int force_freq, int persistent_group,
		const u8 *force_ssid, size_t force_ssid_len,
		int pd_before_go_neg, unsigned int pref_freq, u16 oob_pw_id);

/**
 * p2p_authorize - Authorize P2P group formation (GO negotiation)
 * @p2p: P2P module context from p2p_init()
 * @peer_addr: MAC address of the peer P2P client
 * @wps_method: WPS method to be used in provisioning
 * @go_intent: Local GO intent value (1..15)
 * @own_interface_addr: Intended interface address to use with the group
 * @force_freq: The only allowed channel frequency in MHz or 0
 * @persistent_group: Whether to create a persistent group (0 = no, 1 =
 * persistent group without persistent reconnect, 2 = persistent group with
 * persistent reconnect)
 * @force_ssid: Forced SSID for the group if we become GO or %NULL to generate
 *	a new SSID
 * @force_ssid_len: Length of $force_ssid buffer
 * @pref_freq: Preferred operating frequency in MHz or 0 (this is only used if
 *	force_freq == 0)
 * Returns: 0 on success, -1 on failure
 *
 * This is like p2p_connect(), but the actual group negotiation is not
 * initiated automatically, i.e., the other end is expected to do that.
 */
int p2p_authorize(struct p2p_data *p2p, const u8 *peer_addr,
		  enum p2p_wps_method wps_method,
		  int go_intent, const u8 *own_interface_addr,
		  unsigned int force_freq, int persistent_group,
		  const u8 *force_ssid, size_t force_ssid_len,
		  unsigned int pref_freq, u16 oob_pw_id);

/**
 * p2p_reject - Reject peer device (explicitly block connection attempts)
 * @p2p: P2P module context from p2p_init()
 * @peer_addr: MAC address of the peer P2P client
 * Returns: 0 on success, -1 on failure
 */
int p2p_reject(struct p2p_data *p2p, const u8 *peer_addr);

/**
 * p2p_prov_disc_req - Send Provision Discovery Request
 * @p2p: P2P module context from p2p_init()
 * @peer_addr: MAC address of the peer P2P client
 * @p2ps_prov: Provisioning info for P2PS
 * @config_methods: WPS Config Methods value (only one bit set)
 * @join: Whether this is used by a client joining an active group
 * @force_freq: Forced TX frequency for the frame (mainly for the join case)
 * @user_initiated_pd: Flag to indicate if initiated by user or not
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to request a discovered P2P peer to display a PIN
 * (config_methods = WPS_CONFIG_DISPLAY) or be prepared to enter a PIN from us
 * (config_methods = WPS_CONFIG_KEYPAD). The Provision Discovery Request frame
 * is transmitted once immediately and if no response is received, the frame
 * will be sent again whenever the target device is discovered during device
 * dsicovery (start with a p2p_find() call). Response from the peer is
 * indicated with the p2p_config::prov_disc_resp() callback.
 */
int p2p_prov_disc_req(struct p2p_data *p2p, const u8 *peer_addr,
		      struct p2ps_provision *p2ps_prov, u16 config_methods,
		      int join, int force_freq,
		      int user_initiated_pd);

/**
 * p2p_sd_request - Schedule a service discovery query
 * @p2p: P2P module context from p2p_init()
 * @dst: Destination peer or %NULL to apply for all peers
 * @tlvs: P2P Service Query TLV(s)
 * Returns: Reference to the query or %NULL on failure
 *
 * Response to the query is indicated with the p2p_config::sd_response()
 * callback.
 */
void * p2p_sd_request(struct p2p_data *p2p, const u8 *dst,
		      const struct wpabuf *tlvs);

#ifdef CONFIG_WIFI_DISPLAY
void * p2p_sd_request_wfd(struct p2p_data *p2p, const u8 *dst,
			  const struct wpabuf *tlvs);
#endif /* CONFIG_WIFI_DISPLAY */

/**
 * p2p_sd_cancel_request - Cancel a pending service discovery query
 * @p2p: P2P module context from p2p_init()
 * @req: Query reference from p2p_sd_request()
 * Returns: 0 if request for cancelled; -1 if not found
 */
int p2p_sd_cancel_request(struct p2p_data *p2p, void *req);

/**
 * p2p_sd_response - Send response to a service discovery query
 * @p2p: P2P module context from p2p_init()
 * @freq: Frequency from p2p_config::sd_request() callback
 * @dst: Destination address from p2p_config::sd_request() callback
 * @dialog_token: Dialog token from p2p_config::sd_request() callback
 * @resp_tlvs: P2P Service Response TLV(s)
 *
 * This function is called as a response to the request indicated with
 * p2p_config::sd_request() callback.
 */
void p2p_sd_response(struct p2p_data *p2p, int freq, const u8 *dst,
		     u8 dialog_token, const struct wpabuf *resp_tlvs);

/**
 * p2p_sd_service_update - Indicate a change in local services
 * @p2p: P2P module context from p2p_init()
 *
 * This function needs to be called whenever there is a change in availability
 * of the local services. This will increment the Service Update Indicator
 * value which will be used in SD Request and Response frames.
 */
void p2p_sd_service_update(struct p2p_data *p2p);


enum p2p_invite_role {
	P2P_INVITE_ROLE_GO,
	P2P_INVITE_ROLE_ACTIVE_GO,
	P2P_INVITE_ROLE_CLIENT
};

/**
 * p2p_invite - Invite a P2P Device into a group
 * @p2p: P2P module context from p2p_init()
 * @peer: Device Address of the peer P2P Device
 * @role: Local role in the group
 * @bssid: Group BSSID or %NULL if not known
 * @ssid: Group SSID
 * @ssid_len: Length of ssid in octets
 * @force_freq: The only allowed channel frequency in MHz or 0
 * @go_dev_addr: Forced GO Device Address or %NULL if none
 * @persistent_group: Whether this is to reinvoke a persistent group
 * @pref_freq: Preferred operating frequency in MHz or 0 (this is only used if
 *	force_freq == 0)
 * @dev_pw_id: Device Password ID from OOB Device Password (NFC) static handover
 *	case or -1 if not used
 * Returns: 0 on success, -1 on failure
 */
int p2p_invite(struct p2p_data *p2p, const u8 *peer, enum p2p_invite_role role,
	       const u8 *bssid, const u8 *ssid, size_t ssid_len,
	       unsigned int force_freq, const u8 *go_dev_addr,
	       int persistent_group, unsigned int pref_freq, int dev_pw_id);

/**
 * p2p_presence_req - Request GO presence
 * @p2p: P2P module context from p2p_init()
 * @go_interface_addr: GO P2P Interface Address
 * @own_interface_addr: Own P2P Interface Address for this group
 * @freq: Group operating frequence (in MHz)
 * @duration1: Preferred presence duration in microseconds
 * @interval1: Preferred presence interval in microseconds
 * @duration2: Acceptable presence duration in microseconds
 * @interval2: Acceptable presence interval in microseconds
 * Returns: 0 on success, -1 on failure
 *
 * If both duration and interval values are zero, the parameter pair is not
 * specified (i.e., to remove Presence Request, use duration1 = interval1 = 0).
 */
int p2p_presence_req(struct p2p_data *p2p, const u8 *go_interface_addr,
		     const u8 *own_interface_addr, unsigned int freq,
		     u32 duration1, u32 interval1, u32 duration2,
		     u32 interval2);

/**
 * p2p_ext_listen - Set Extended Listen Timing
 * @p2p: P2P module context from p2p_init()
 * @freq: Group operating frequence (in MHz)
 * @period: Availability period in milliseconds (1-65535; 0 to disable)
 * @interval: Availability interval in milliseconds (1-65535; 0 to disable)
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to enable or disable (period = interval = 0)
 * Extended Listen Timing. When enabled, the P2P Device will become
 * discoverable (go into Listen State) every @interval milliseconds for at
 * least @period milliseconds.
 */
int p2p_ext_listen(struct p2p_data *p2p, unsigned int period,
		   unsigned int interval);

/* Event notifications from upper layer management operations */

/**
 * p2p_wps_success_cb - Report successfully completed WPS provisioning
 * @p2p: P2P module context from p2p_init()
 * @mac_addr: Peer address
 *
 * This function is used to report successfully completed WPS provisioning
 * during group formation in both GO/Registrar and client/Enrollee roles.
 */
void p2p_wps_success_cb(struct p2p_data *p2p, const u8 *mac_addr);

/**
 * p2p_group_formation_failed - Report failed WPS provisioning
 * @p2p: P2P module context from p2p_init()
 *
 * This function is used to report failed group formation. This can happen
 * either due to failed WPS provisioning or due to 15 second timeout during
 * the provisioning phase.
 */
void p2p_group_formation_failed(struct p2p_data *p2p);

/**
 * p2p_get_provisioning_info - Get any stored provisioning info
 * @p2p: P2P module context from p2p_init()
 * @addr: Peer P2P Device Address
 * Returns: WPS provisioning information (WPS config method) or 0 if no
 * information is available
 *
 * This function is used to retrieve stored WPS provisioning info for the given
 * peer.
 */
u16 p2p_get_provisioning_info(struct p2p_data *p2p, const u8 *addr);

/**
 * p2p_clear_provisioning_info - Clear any stored provisioning info
 * @p2p: P2P module context from p2p_init()
 * @iface_addr: Peer P2P Device Address
 *
 * This function is used to clear stored WPS provisioning info for the given
 * peer.
 */
void p2p_clear_provisioning_info(struct p2p_data *p2p, const u8 *addr);


/* Event notifications from lower layer driver operations */

/**
 * enum p2p_probe_req_status
 *
 * @P2P_PREQ_MALFORMED: frame was not well-formed
 * @P2P_PREQ_NOT_LISTEN: device isn't in listen state, frame ignored
 * @P2P_PREQ_NOT_P2P: frame was not a P2P probe request
 * @P2P_PREQ_P2P_NOT_PROCESSED: frame was P2P but wasn't processed
 * @P2P_PREQ_P2P_PROCESSED: frame has been processed by P2P
 */
enum p2p_probe_req_status {
	P2P_PREQ_MALFORMED,
	P2P_PREQ_NOT_LISTEN,
	P2P_PREQ_NOT_P2P,
	P2P_PREQ_NOT_PROCESSED,
	P2P_PREQ_PROCESSED
};

/**
 * p2p_probe_req_rx - Report reception of a Probe Request frame
 * @p2p: P2P module context from p2p_init()
 * @addr: Source MAC address
 * @dst: Destination MAC address if available or %NULL
 * @bssid: BSSID if available or %NULL
 * @ie: Information elements from the Probe Request frame body
 * @ie_len: Length of ie buffer in octets
 * @rx_freq: Probe Request frame RX frequency
 * @p2p_lo_started: Whether P2P Listen Offload is started
 * Returns: value indicating the type and status of the probe request
 */
enum p2p_probe_req_status
p2p_probe_req_rx(struct p2p_data *p2p, const u8 *addr, const u8 *dst,
		 const u8 *bssid, const u8 *ie, size_t ie_len,
		 unsigned int rx_freq, int p2p_lo_started);

/**
 * p2p_rx_action - Report received Action frame
 * @p2p: P2P module context from p2p_init()
 * @da: Destination address of the received Action frame
 * @sa: Source address of the received Action frame
 * @bssid: Address 3 of the received Action frame
 * @category: Category of the received Action frame
 * @data: Action frame body after the Category field
 * @len: Length of the data buffer in octets
 * @freq: Frequency (in MHz) on which the frame was received
 */
void p2p_rx_action(struct p2p_data *p2p, const u8 *da, const u8 *sa,
		   const u8 *bssid, u8 category,
		   const u8 *data, size_t len, int freq);

/**
 * p2p_scan_res_handler - Indicate a P2P scan results
 * @p2p: P2P module context from p2p_init()
 * @bssid: BSSID of the scan result
 * @freq: Frequency of the channel on which the device was found in MHz
 * @rx_time: Time when the result was received
 * @level: Signal level (signal strength of the received Beacon/Probe Response
 *	frame)
 * @ies: Pointer to IEs from the scan result
 * @ies_len: Length of the ies buffer
 * Returns: 0 to continue or 1 to stop scan result indication
 *
 * This function is called to indicate a scan result entry with P2P IE from a
 * scan requested with struct p2p_config::p2p_scan(). This can be called during
 * the actual scan process (i.e., whenever a new device is found) or as a
 * sequence of calls after the full scan has been completed. The former option
 * can result in optimized operations, but may not be supported by all
 * driver/firmware designs. The ies buffer need to include at least the P2P IE,
 * but it is recommended to include all IEs received from the device. The
 * caller does not need to check that the IEs contain a P2P IE before calling
 * this function since frames will be filtered internally if needed.
 *
 * This function will return 1 if it wants to stop scan result iteration (and
 * scan in general if it is still in progress). This is used to allow faster
 * start of a pending operation, e.g., to start a pending GO negotiation.
 */
int p2p_scan_res_handler(struct p2p_data *p2p, const u8 *bssid, int freq,
			 struct os_reltime *rx_time, int level, const u8 *ies,
			 size_t ies_len);

/**
 * p2p_scan_res_handled - Indicate end of scan results
 * @p2p: P2P module context from p2p_init()
 *
 * This function is called to indicate that all P2P scan results from a scan
 * have been reported with zero or more calls to p2p_scan_res_handler(). This
 * function must be called as a response to successful
 * struct p2p_config::p2p_scan() call if none of the p2p_scan_res_handler()
 * calls stopped iteration.
 */
void p2p_scan_res_handled(struct p2p_data *p2p);

enum p2p_send_action_result {
	P2P_SEND_ACTION_SUCCESS /* Frame was send and acknowledged */,
	P2P_SEND_ACTION_NO_ACK /* Frame was sent, but not acknowledged */,
	P2P_SEND_ACTION_FAILED /* Frame was not sent due to a failure */
};

/**
 * p2p_send_action_cb - Notify TX status of an Action frame
 * @p2p: P2P module context from p2p_init()
 * @freq: Channel frequency in MHz
 * @dst: Destination MAC address (Address 1)
 * @src: Source MAC address (Address 2)
 * @bssid: BSSID (Address 3)
 * @result: Result of the transmission attempt
 *
 * This function is used to indicate the result of an Action frame transmission
 * that was requested with struct p2p_config::send_action() callback.
 */
void p2p_send_action_cb(struct p2p_data *p2p, unsigned int freq, const u8 *dst,
			const u8 *src, const u8 *bssid,
			enum p2p_send_action_result result);

/**
 * p2p_listen_cb - Indicate the start of a requested Listen state
 * @p2p: P2P module context from p2p_init()
 * @freq: Listen channel frequency in MHz
 * @duration: Duration for the Listen state in milliseconds
 *
 * This function is used to indicate that a Listen state requested with
 * struct p2p_config::start_listen() callback has started.
 */
void p2p_listen_cb(struct p2p_data *p2p, unsigned int freq,
		   unsigned int duration);

/**
 * p2p_listen_end - Indicate the end of a requested Listen state
 * @p2p: P2P module context from p2p_init()
 * @freq: Listen channel frequency in MHz
 * Returns: 0 if no operations were started, 1 if an operation was started
 *
 * This function is used to indicate that a Listen state requested with
 * struct p2p_config::start_listen() callback has ended.
 */
int p2p_listen_end(struct p2p_data *p2p, unsigned int freq);

void p2p_deauth_notif(struct p2p_data *p2p, const u8 *bssid, u16 reason_code,
		      const u8 *ie, size_t ie_len);

void p2p_disassoc_notif(struct p2p_data *p2p, const u8 *bssid, u16 reason_code,
			const u8 *ie, size_t ie_len);


/* Per-group P2P state for GO */

struct p2p_group;

/**
 * struct p2p_group_config - P2P group configuration
 *
 * This configuration is provided to the P2P module during initialization of
 * the per-group information with p2p_group_init().
 */
struct p2p_group_config {
	/**
	 * persistent_group - Whether the group is persistent
	 * 0 = not a persistent group
	 * 1 = persistent group without persistent reconnect
	 * 2 = persistent group with persistent reconnect
	 */
	int persistent_group;

	/**
	 * interface_addr - P2P Interface Address of the group
	 */
	u8 interface_addr[ETH_ALEN];

	/**
	 * max_clients - Maximum number of clients in the group
	 */
	unsigned int max_clients;

	/**
	 * ssid - Group SSID
	 */
	u8 ssid[SSID_MAX_LEN];

	/**
	 * ssid_len - Length of SSID
	 */
	size_t ssid_len;

	/**
	 * freq - Operating channel of the group
	 */
	int freq;

	/**
	 * ip_addr_alloc - Whether IP address allocation within 4-way handshake
	 *	is supported
	 */
	int ip_addr_alloc;

	/**
	 * cb_ctx - Context to use with callback functions
	 */
	void *cb_ctx;

	/**
	 * ie_update - Notification of IE update
	 * @ctx: Callback context from cb_ctx
	 * @beacon_ies: P2P IE for Beacon frames or %NULL if no change
	 * @proberesp_ies: P2P Ie for Probe Response frames
	 *
	 * P2P module uses this callback function to notify whenever the P2P IE
	 * in Beacon or Probe Response frames should be updated based on group
	 * events.
	 *
	 * The callee is responsible for freeing the returned buffer(s) with
	 * wpabuf_free().
	 */
	void (*ie_update)(void *ctx, struct wpabuf *beacon_ies,
			  struct wpabuf *proberesp_ies);

	/**
	 * idle_update - Notification of changes in group idle state
	 * @ctx: Callback context from cb_ctx
	 * @idle: Whether the group is idle (no associated stations)
	 */
	void (*idle_update)(void *ctx, int idle);
};

/**
 * p2p_group_init - Initialize P2P group
 * @p2p: P2P module context from p2p_init()
 * @config: P2P group configuration (will be freed by p2p_group_deinit())
 * Returns: Pointer to private data or %NULL on failure
 *
 * This function is used to initialize per-group P2P module context. Currently,
 * this is only used to manage GO functionality and P2P clients do not need to
 * create an instance of this per-group information.
 */
struct p2p_group * p2p_group_init(struct p2p_data *p2p,
				  struct p2p_group_config *config);

/**
 * p2p_group_deinit - Deinitialize P2P group
 * @group: P2P group context from p2p_group_init()
 */
void p2p_group_deinit(struct p2p_group *group);

/**
 * p2p_group_notif_assoc - Notification of P2P client association with GO
 * @group: P2P group context from p2p_group_init()
 * @addr: Interface address of the P2P client
 * @ie: IEs from the (Re)association Request frame
 * @len: Length of the ie buffer in octets
 * Returns: 0 on success, -1 on failure
 */
int p2p_group_notif_assoc(struct p2p_group *group, const u8 *addr,
			  const u8 *ie, size_t len);

/**
 * p2p_group_assoc_resp_ie - Build P2P IE for (re)association response
 * @group: P2P group context from p2p_group_init()
 * @status: Status value (P2P_SC_SUCCESS if association succeeded)
 * Returns: P2P IE for (Re)association Response or %NULL on failure
 *
 * The caller is responsible for freeing the returned buffer with
 * wpabuf_free().
 */
struct wpabuf * p2p_group_assoc_resp_ie(struct p2p_group *group, u8 status);

/**
 * p2p_group_notif_disassoc - Notification of P2P client disassociation from GO
 * @group: P2P group context from p2p_group_init()
 * @addr: Interface address of the P2P client
 */
void p2p_group_notif_disassoc(struct p2p_group *group, const u8 *addr);

/**
 * p2p_group_notif_formation_done - Notification of completed group formation
 * @group: P2P group context from p2p_group_init()
 */
void p2p_group_notif_formation_done(struct p2p_group *group);

/**
 * p2p_group_notif_noa - Notification of NoA change
 * @group: P2P group context from p2p_group_init()
 * @noa: Notice of Absence attribute payload, %NULL if none
 * @noa_len: Length of noa buffer in octets
 * Returns: 0 on success, -1 on failure
 *
 * Notify the P2P group management about a new NoA contents. This will be
 * inserted into the P2P IEs in Beacon and Probe Response frames with rest of
 * the group information.
 */
int p2p_group_notif_noa(struct p2p_group *group, const u8 *noa,
			size_t noa_len);

/**
 * p2p_group_match_dev_type - Match device types in group with requested type
 * @group: P2P group context from p2p_group_init()
 * @wps: WPS TLVs from Probe Request frame (concatenated WPS IEs)
 * Returns: 1 on match, 0 on mismatch
 *
 * This function can be used to match the Requested Device Type attribute in
 * WPS IE with the device types of a group member for deciding whether a GO
 * should reply to a Probe Request frame. Match will be reported if the WPS IE
 * is not requested any specific device type.
 */
int p2p_group_match_dev_type(struct p2p_group *group, struct wpabuf *wps);

/**
 * p2p_group_match_dev_id - Match P2P Device Address in group with requested device id
 */
int p2p_group_match_dev_id(struct p2p_group *group, struct wpabuf *p2p);

/**
 * p2p_group_go_discover - Send GO Discoverability Request to a group client
 * @group: P2P group context from p2p_group_init()
 * Returns: 0 on success (frame scheduled); -1 if client was not found
 */
int p2p_group_go_discover(struct p2p_group *group, const u8 *dev_id,
			  const u8 *searching_dev, int rx_freq);


/* Generic helper functions */

/**
 * p2p_ie_text - Build text format description of P2P IE
 * @p2p_ie: P2P IE
 * @buf: Buffer for returning text
 * @end: Pointer to the end of the buf area
 * Returns: Number of octets written to the buffer or -1 on failure
 *
 * This function can be used to parse P2P IE contents into text format
 * field=value lines.
 */
int p2p_ie_text(struct wpabuf *p2p_ie, char *buf, char *end);

/**
 * p2p_scan_result_text - Build text format description of P2P IE
 * @ies: Information elements from scan results
 * @ies_len: ies buffer length in octets
 * @buf: Buffer for returning text
 * @end: Pointer to the end of the buf area
 * Returns: Number of octets written to the buffer or -1 on failure
 *
 * This function can be used to parse P2P IE contents into text format
 * field=value lines.
 */
int p2p_scan_result_text(const u8 *ies, size_t ies_len, char *buf, char *end);

/**
 * p2p_parse_dev_addr_in_p2p_ie - Parse P2P Device Address from a concatenated
 * P2P IE
 * @p2p_ie: P2P IE
 * @dev_addr: Buffer for returning P2P Device Address
 * Returns: 0 on success or -1 if P2P Device Address could not be parsed
 */
int p2p_parse_dev_addr_in_p2p_ie(struct wpabuf *p2p_ie, u8 *dev_addr);

/**
 * p2p_parse_dev_addr - Parse P2P Device Address from P2P IE(s)
 * @ies: Information elements from scan results
 * @ies_len: ies buffer length in octets
 * @dev_addr: Buffer for returning P2P Device Address
 * Returns: 0 on success or -1 if P2P Device Address could not be parsed
 */
int p2p_parse_dev_addr(const u8 *ies, size_t ies_len, u8 *dev_addr);

/**
 * p2p_assoc_req_ie - Build P2P IE for (Re)Association Request frame
 * @p2p: P2P module context from p2p_init()
 * @bssid: BSSID
 * @buf: Buffer for writing the P2P IE
 * @len: Maximum buf length in octets
 * @p2p_group: Whether this is for association with a P2P GO
 * @p2p_ie: Reassembled P2P IE data from scan results or %NULL if none
 * Returns: Number of octets written into buf or -1 on failure
 */
int p2p_assoc_req_ie(struct p2p_data *p2p, const u8 *bssid, u8 *buf,
		     size_t len, int p2p_group, struct wpabuf *p2p_ie);

/**
 * p2p_scan_ie - Build P2P IE for Probe Request
 * @p2p: P2P module context from p2p_init()
 * @ies: Buffer for writing P2P IE
 * @dev_id: Device ID to search for or %NULL for any
 * @bands: Frequency bands used in the scan (enum wpa_radio_work_band bitmap)
 */
void p2p_scan_ie(struct p2p_data *p2p, struct wpabuf *ies, const u8 *dev_id,
		 unsigned int bands);

/**
 * p2p_scan_ie_buf_len - Get maximum buffer length needed for p2p_scan_ie
 * @p2p: P2P module context from p2p_init()
 * Returns: Number of octets that p2p_scan_ie() may add to the buffer
 */
size_t p2p_scan_ie_buf_len(struct p2p_data *p2p);

/**
 * p2p_go_params - Generate random P2P group parameters
 * @p2p: P2P module context from p2p_init()
 * @params: Buffer for parameters
 * Returns: 0 on success, -1 on failure
 */
int p2p_go_params(struct p2p_data *p2p, struct p2p_go_neg_results *params);

/**
 * p2p_get_group_capab - Get Group Capability from P2P IE data
 * @p2p_ie: P2P IE(s) contents
 * Returns: Group Capability
 */
u8 p2p_get_group_capab(const struct wpabuf *p2p_ie);

/**
 * p2p_get_cross_connect_disallowed - Does WLAN AP disallows cross connection
 * @p2p_ie: P2P IE(s) contents
 * Returns: 0 if cross connection is allow, 1 if not
 */
int p2p_get_cross_connect_disallowed(const struct wpabuf *p2p_ie);

/**
 * p2p_get_go_dev_addr - Get P2P Device Address from P2P IE data
 * @p2p_ie: P2P IE(s) contents
 * Returns: Pointer to P2P Device Address or %NULL if not included
 */
const u8 * p2p_get_go_dev_addr(const struct wpabuf *p2p_ie);

/**
 * p2p_get_peer_info - Get P2P peer information
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Device Address of the peer or %NULL to indicate the first peer
 * @next: Whether to select the peer entry following the one indicated by addr
 * Returns: Pointer to peer info or %NULL if not found
 */
const struct p2p_peer_info * p2p_get_peer_info(struct p2p_data *p2p,
					       const u8 *addr, int next);

/**
 * p2p_get_peer_info_txt - Get internal P2P peer information in text format
 * @info: Pointer to P2P peer info from p2p_get_peer_info()
 * @buf: Buffer for returning text
 * @buflen: Maximum buffer length
 * Returns: Number of octets written to the buffer or -1 on failure
 *
 * Note: This information is internal to the P2P module and subject to change.
 * As such, this should not really be used by external programs for purposes
 * other than debugging.
 */
int p2p_get_peer_info_txt(const struct p2p_peer_info *info,
			  char *buf, size_t buflen);

/**
 * p2p_peer_known - Check whether P2P peer is known
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Device Address of the peer
 * Returns: 1 if the specified device is in the P2P peer table or 0 if not
 */
int p2p_peer_known(struct p2p_data *p2p, const u8 *addr);

/**
 * p2p_set_client_discoverability - Set client discoverability capability
 * @p2p: P2P module context from p2p_init()
 * @enabled: Whether client discoverability will be enabled
 *
 * This function can be used to disable (and re-enable) client discoverability.
 * This capability is enabled by default and should not be disabled in normal
 * use cases, i.e., this is mainly for testing purposes.
 */
void p2p_set_client_discoverability(struct p2p_data *p2p, int enabled);

/**
 * p2p_set_managed_oper - Set managed P2P Device operations capability
 * @p2p: P2P module context from p2p_init()
 * @enabled: Whether managed P2P Device operations will be enabled
 */
void p2p_set_managed_oper(struct p2p_data *p2p, int enabled);

/**
 * p2p_config_get_random_social - Return a random social channel
 * @p2p: P2P config
 * @op_class: Selected operating class
 * @op_channel: Selected social channel
 * Returns: 0 on success, -1 on failure
 *
 * This function is used before p2p_init is called. A random social channel
 * from supports bands 2.4 GHz (channels 1,6,11) and 60 GHz (channel 2) is
 * returned on success.
 */
int p2p_config_get_random_social(struct p2p_config *p2p, u8 *op_class,
				 u8 *op_channel);

int p2p_set_listen_channel(struct p2p_data *p2p, u8 reg_class, u8 channel,
			   u8 forced);

u8 p2p_get_listen_channel(struct p2p_data *p2p);

int p2p_set_ssid_postfix(struct p2p_data *p2p, const u8 *postfix, size_t len);

int p2p_get_interface_addr(struct p2p_data *p2p, const u8 *dev_addr,
			   u8 *iface_addr);
int p2p_get_dev_addr(struct p2p_data *p2p, const u8 *iface_addr,
			   u8 *dev_addr);

void p2p_set_peer_filter(struct p2p_data *p2p, const u8 *addr);

/**
 * p2p_set_cross_connect - Set cross connection capability
 * @p2p: P2P module context from p2p_init()
 * @enabled: Whether cross connection will be enabled
 */
void p2p_set_cross_connect(struct p2p_data *p2p, int enabled);

int p2p_get_oper_freq(struct p2p_data *p2p, const u8 *iface_addr);

/**
 * p2p_set_intra_bss_dist - Set intra BSS distribution
 * @p2p: P2P module context from p2p_init()
 * @enabled: Whether intra BSS distribution will be enabled
 */
void p2p_set_intra_bss_dist(struct p2p_data *p2p, int enabled);

int p2p_channels_includes_freq(const struct p2p_channels *channels,
			       unsigned int freq);

int p2p_channels_to_freqs(const struct p2p_channels *channels,
			  int *freq_list, unsigned int max_len);

/**
 * p2p_supported_freq - Check whether channel is supported for P2P
 * @p2p: P2P module context from p2p_init()
 * @freq: Channel frequency in MHz
 * Returns: 0 if channel not usable for P2P, 1 if usable for P2P
 */
int p2p_supported_freq(struct p2p_data *p2p, unsigned int freq);

/**
 * p2p_supported_freq_go - Check whether channel is supported for P2P GO operation
 * @p2p: P2P module context from p2p_init()
 * @freq: Channel frequency in MHz
 * Returns: 0 if channel not usable for P2P, 1 if usable for P2P
 */
int p2p_supported_freq_go(struct p2p_data *p2p, unsigned int freq);

/**
 * p2p_supported_freq_cli - Check whether channel is supported for P2P client operation
 * @p2p: P2P module context from p2p_init()
 * @freq: Channel frequency in MHz
 * Returns: 0 if channel not usable for P2P, 1 if usable for P2P
 */
int p2p_supported_freq_cli(struct p2p_data *p2p, unsigned int freq);

/**
 * p2p_get_pref_freq - Get channel from preferred channel list
 * @p2p: P2P module context from p2p_init()
 * @channels: List of channels
 * Returns: Preferred channel
 */
unsigned int p2p_get_pref_freq(struct p2p_data *p2p,
			       const struct p2p_channels *channels);

void p2p_update_channel_list(struct p2p_data *p2p,
			     const struct p2p_channels *chan,
			     const struct p2p_channels *cli_chan);

/**
 * p2p_set_best_channels - Update best channel information
 * @p2p: P2P module context from p2p_init()
 * @freq_24: Frequency (MHz) of best channel in 2.4 GHz band
 * @freq_5: Frequency (MHz) of best channel in 5 GHz band
 * @freq_overall: Frequency (MHz) of best channel overall
 */
void p2p_set_best_channels(struct p2p_data *p2p, int freq_24, int freq_5,
			   int freq_overall);

/**
 * p2p_set_own_freq_preference - Set own preference for channel
 * @p2p: P2P module context from p2p_init()
 * @freq: Frequency (MHz) of the preferred channel or 0 if no preference
 *
 * This function can be used to set a preference on the operating channel based
 * on frequencies used on the other virtual interfaces that share the same
 * radio. If non-zero, this is used to try to avoid multi-channel concurrency.
 */
void p2p_set_own_freq_preference(struct p2p_data *p2p, int freq);

const u8 * p2p_get_go_neg_peer(struct p2p_data *p2p);

/**
 * p2p_get_group_num_members - Get number of members in group
 * @group: P2P group context from p2p_group_init()
 * Returns: Number of members in the group
 */
unsigned int p2p_get_group_num_members(struct p2p_group *group);

/**
 * p2p_client_limit_reached - Check if client limit is reached
 * @group: P2P group context from p2p_group_init()
 * Returns: 1 if no of clients limit reached
 */
int p2p_client_limit_reached(struct p2p_group *group);

/**
 * p2p_iterate_group_members - Iterate group members
 * @group: P2P group context from p2p_group_init()
 * @next: iteration pointer, must be a pointer to a void * that is set to %NULL
 *	on the first call and not modified later
 * Returns: A P2P Device Address for each call and %NULL for no more members
 */
const u8 * p2p_iterate_group_members(struct p2p_group *group, void **next);

/**
 * p2p_group_get_client_interface_addr - Get P2P Interface Address of a client in a group
 * @group: P2P group context from p2p_group_init()
 * @dev_addr: P2P Device Address of the client
 * Returns: P2P Interface Address of the client if found or %NULL if no match
 * found
 */
const u8 * p2p_group_get_client_interface_addr(struct p2p_group *group,
					       const u8 *dev_addr);

/**
 * p2p_group_get_dev_addr - Get a P2P Device Address of a client in a group
 * @group: P2P group context from p2p_group_init()
 * @addr: P2P Interface Address of the client
 * Returns: P2P Device Address of the client if found or %NULL if no match
 * found
 */
const u8 * p2p_group_get_dev_addr(struct p2p_group *group, const u8 *addr);

/**
 * p2p_group_is_client_connected - Check whether a specific client is connected
 * @group: P2P group context from p2p_group_init()
 * @addr: P2P Device Address of the client
 * Returns: 1 if client is connected or 0 if not
 */
int p2p_group_is_client_connected(struct p2p_group *group, const u8 *dev_addr);

/**
 * p2p_group_get_config - Get the group configuration
 * @group: P2P group context from p2p_group_init()
 * Returns: The group configuration pointer
 */
const struct p2p_group_config * p2p_group_get_config(struct p2p_group *group);

/**
 * p2p_loop_on_all_groups - Run the given callback on all groups
 * @p2p: P2P module context from p2p_init()
 * @group_callback: The callback function pointer
 * @user_data: Some user data pointer which can be %NULL
 *
 * The group_callback function can stop the iteration by returning 0.
 */
void p2p_loop_on_all_groups(struct p2p_data *p2p,
			    int (*group_callback)(struct p2p_group *group,
						  void *user_data),
			    void *user_data);

/**
 * p2p_get_peer_found - Get P2P peer info structure of a found peer
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Device Address of the peer or %NULL to indicate the first peer
 * @next: Whether to select the peer entry following the one indicated by addr
 * Returns: The first P2P peer info available or %NULL if no such peer exists
 */
const struct p2p_peer_info *
p2p_get_peer_found(struct p2p_data *p2p, const u8 *addr, int next);

/**
 * p2p_remove_wps_vendor_extensions - Remove WPS vendor extensions
 * @p2p: P2P module context from p2p_init()
 */
void p2p_remove_wps_vendor_extensions(struct p2p_data *p2p);

/**
 * p2p_add_wps_vendor_extension - Add a WPS vendor extension
 * @p2p: P2P module context from p2p_init()
 * @vendor_ext: The vendor extensions to add
 * Returns: 0 on success, -1 on failure
 *
 * The wpabuf structures in the array are owned by the P2P
 * module after this call.
 */
int p2p_add_wps_vendor_extension(struct p2p_data *p2p,
				 const struct wpabuf *vendor_ext);

/**
 * p2p_set_oper_channel - Set the P2P operating channel
 * @p2p: P2P module context from p2p_init()
 * @op_reg_class: Operating regulatory class to set
 * @op_channel: operating channel to set
 * @cfg_op_channel : Whether op_channel is hardcoded in configuration
 * Returns: 0 on success, -1 on failure
 */
int p2p_set_oper_channel(struct p2p_data *p2p, u8 op_reg_class, u8 op_channel,
			 int cfg_op_channel);

/**
 * p2p_set_pref_chan - Set P2P preferred channel list
 * @p2p: P2P module context from p2p_init()
 * @num_pref_chan: Number of entries in pref_chan list
 * @pref_chan: Preferred channels or %NULL to remove preferences
 * Returns: 0 on success, -1 on failure
 */
int p2p_set_pref_chan(struct p2p_data *p2p, unsigned int num_pref_chan,
		      const struct p2p_channel *pref_chan);

/**
 * p2p_set_no_go_freq - Set no GO channel ranges
 * @p2p: P2P module context from p2p_init()
 * @list: Channel ranges or %NULL to remove restriction
 * Returns: 0 on success, -1 on failure
 */
int p2p_set_no_go_freq(struct p2p_data *p2p,
		       const struct wpa_freq_range_list *list);

/**
 * p2p_in_progress - Check whether a P2P operation is progress
 * @p2p: P2P module context from p2p_init()
 * Returns: 0 if P2P module is idle, 1 if an operation is in progress but not
 * in search state, or 2 if search state operation is in progress
 */
int p2p_in_progress(struct p2p_data *p2p);

const char * p2p_wps_method_text(enum p2p_wps_method method);

/**
 * p2p_set_config_timeout - Set local config timeouts
 * @p2p: P2P module context from p2p_init()
 * @go_timeout: Time in 10 ms units it takes to start the GO mode
 * @client_timeout: Time in 10 ms units it takes to start the client mode
 */
void p2p_set_config_timeout(struct p2p_data *p2p, u8 go_timeout,
			    u8 client_timeout);

int p2p_set_wfd_ie_beacon(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_probe_req(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_probe_resp(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_assoc_req(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_invitation(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_prov_disc_req(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_prov_disc_resp(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_ie_go_neg(struct p2p_data *p2p, struct wpabuf *ie);
int p2p_set_wfd_dev_info(struct p2p_data *p2p, const struct wpabuf *elem);
int p2p_set_wfd_r2_dev_info(struct p2p_data *p2p, const struct wpabuf *elem);
int p2p_set_wfd_assoc_bssid(struct p2p_data *p2p, const struct wpabuf *elem);
int p2p_set_wfd_coupled_sink_info(struct p2p_data *p2p,
				  const struct wpabuf *elem);
struct wpabuf * wifi_display_encaps(struct wpabuf *subelems);

/**
 * p2p_set_disc_int - Set min/max discoverable interval for p2p_find
 * @p2p: P2P module context from p2p_init()
 * @min_disc_int: minDiscoverableInterval (in units of 100 TU); default 1
 * @max_disc_int: maxDiscoverableInterval (in units of 100 TU); default 3
 * @max_disc_tu: Maximum number of TUs (1.024 ms) for discoverable interval; or
 *	-1 not to limit
 * Returns: 0 on success, or -1 on failure
 *
 * This function can be used to configure minDiscoverableInterval and
 * maxDiscoverableInterval parameters for the Listen state during device
 * discovery (p2p_find). A random number of 100 TU units is picked for each
 * Listen state iteration from [min_disc_int,max_disc_int] range.
 *
 * max_disc_tu can be used to further limit the discoverable duration. However,
 * it should be noted that use of this parameter is not recommended since it
 * would not be compliant with the P2P specification.
 */
int p2p_set_disc_int(struct p2p_data *p2p, int min_disc_int, int max_disc_int,
		     int max_disc_tu);

/**
 * p2p_get_state_txt - Get current P2P state for debug purposes
 * @p2p: P2P module context from p2p_init()
 * Returns: Name of the current P2P module state
 *
 * It should be noted that the P2P module state names are internal information
 * and subject to change at any point, i.e., this information should be used
 * mainly for debugging purposes.
 */
const char * p2p_get_state_txt(struct p2p_data *p2p);

struct wpabuf * p2p_build_nfc_handover_req(struct p2p_data *p2p,
					   int client_freq,
					   const u8 *go_dev_addr,
					   const u8 *ssid, size_t ssid_len);
struct wpabuf * p2p_build_nfc_handover_sel(struct p2p_data *p2p,
					   int client_freq,
					   const u8 *go_dev_addr,
					   const u8 *ssid, size_t ssid_len);

struct p2p_nfc_params {
	int sel;
	const u8 *wsc_attr;
	size_t wsc_len;
	const u8 *p2p_attr;
	size_t p2p_len;

	enum {
		NO_ACTION, JOIN_GROUP, AUTH_JOIN, INIT_GO_NEG, RESP_GO_NEG,
		BOTH_GO, PEER_CLIENT
	} next_step;
	struct p2p_peer_info *peer;
	u8 oob_dev_pw[WPS_OOB_PUBKEY_HASH_LEN + 2 +
		      WPS_OOB_DEVICE_PASSWORD_LEN];
	size_t oob_dev_pw_len;
	int go_freq;
	u8 go_dev_addr[ETH_ALEN];
	u8 go_ssid[SSID_MAX_LEN];
	size_t go_ssid_len;
};

int p2p_process_nfc_connection_handover(struct p2p_data *p2p,
					struct p2p_nfc_params *params);

void p2p_set_authorized_oob_dev_pw_id(struct p2p_data *p2p, u16 dev_pw_id,
				      int go_intent,
				      const u8 *own_interface_addr);

int p2p_set_passphrase_len(struct p2p_data *p2p, unsigned int len);

void p2p_loop_on_known_peers(struct p2p_data *p2p,
			     void (*peer_callback)(struct p2p_peer_info *peer,
						   void *user_data),
			     void *user_data);

void p2p_set_vendor_elems(struct p2p_data *p2p, struct wpabuf **vendor_elem);

void p2p_set_intended_addr(struct p2p_data *p2p, const u8 *intended_addr);

struct p2ps_advertisement *
p2p_service_p2ps_id(struct p2p_data *p2p, u32 adv_id);
int p2p_service_add_asp(struct p2p_data *p2p, int auto_accept, u32 adv_id,
			const char *adv_str, u8 svc_state,
			u16 config_methods, const char *svc_info,
			const u8 *cpt_priority);
int p2p_service_del_asp(struct p2p_data *p2p, u32 adv_id);
void p2p_service_flush_asp(struct p2p_data *p2p);
struct p2ps_advertisement * p2p_get_p2ps_adv_list(struct p2p_data *p2p);

/**
 * p2p_expire_peers - Periodic cleanup function to expire peers
 * @p2p: P2P module context from p2p_init()
 *
 * This is a cleanup function that the entity calling p2p_init() is
 * expected to call periodically to clean up expired peer entries.
 */
void p2p_expire_peers(struct p2p_data *p2p);

void p2p_set_own_pref_freq_list(struct p2p_data *p2p,
				const unsigned int *pref_freq_list,
				unsigned int size);
void p2p_set_override_pref_op_chan(struct p2p_data *p2p, u8 op_class,
				   u8 chan);

/**
 * p2p_group_get_common_freqs - Get the group common frequencies
 * @group: P2P group context from p2p_group_init()
 * @common_freqs: On return will hold the group common frequencies
 * @num: On return will hold the number of group common frequencies
 * Returns: 0 on success, -1 otherwise
 */
int p2p_group_get_common_freqs(struct p2p_group *group, int *common_freqs,
			       unsigned int *num);

struct wpabuf * p2p_build_probe_resp_template(struct p2p_data *p2p,
					      unsigned int freq);

#endif /* P2P_H */
