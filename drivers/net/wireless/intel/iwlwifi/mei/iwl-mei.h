/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2024 Intel Corporation
 */

#ifndef __iwl_mei_h__
#define __iwl_mei_h__

#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>

/**
 * DOC: Introduction
 *
 * iwlmei is the kernel module that is in charge of the communication between
 * the iwlwifi driver and the CSME firmware's WLAN driver. This communication
 * uses the SAP protocol defined in another file.
 * iwlwifi can request or release ownership on the WiFi device through iwlmei.
 * iwlmei may notify iwlwifi about certain events: what filter iwlwifi should
 * use to passthrough inbound packets to the CSME firmware for example. iwlmei
 * may also use iwlwifi to send traffic. This means that we need communication
 * from iwlmei to iwlwifi and the other way around.
 */

/**
 * DOC: Life cycle
 *
 * iwlmei exports symbols that are needed by iwlwifi so that iwlmei will always
 * be loaded when iwlwifi is alive. iwlwifi registers itself to iwlmei and
 * provides the pointers to the functions that iwlmei calls whenever needed.
 * iwlwifi calls iwlmei through direct and context-free function calls.
 * It is assumed that only one device is accessible to the CSME firmware and
 * under the scope of iwlmei so that it is valid not to have any context passed
 * to iwlmei's functions.
 *
 * There are cases in which iwlmei can't access the CSME firmware, because the
 * CSME firmware is undergoing a reset, or the mei bus decided to unbind the
 * device. In those cases, iwlmei will need not to send requests over the mei
 * bus. Instead, it needs to cache the requests from iwlwifi and fulfill them
 * when the mei bus is available again.
 *
 * iwlmei can call iwlwifi as long as iwlwifi is registered to iwlmei. When
 * iwlwifi goes down (the PCI device is unbound, or the iwlwifi is unloaded)
 * iwlwifi needs to unregister from iwlmei.
 */

/**
 * DOC: Memory layout
 *
 * Since iwlwifi calls iwlmei without any context, iwlmei needs to hold a
 * global pointer to its data (which is in the mei client device's private
 * data area). If there was no bind on the mei bus, this pointer is NULL and
 * iwlmei knows not access to the CSME firmware upon requests from iwlwifi.
 *
 * iwlmei needs to cache requests from iwlwifi when there is no mei client
 * device available (when iwlmei has been removed from the mei bus). In this
 * case, all iwlmei's data that resides in the mei client device's private data
 * area is unavailable. For this specific case, a separate caching area is
 * needed.
 */

/**
 * DOC: Concurrency
 *
 * iwlwifi can call iwlmei at any time. iwlmei will take care to synchronize
 * the calls from iwlwifi with its internal flows. iwlwifi must not call iwlmei
 * in flows that cannot sleep. Moreover, iwlwifi must not call iwlmei in flows
 * that originated from iwlmei.
 */

/**
 * DOC: Probe and remove from mei bus driver
 *
 * When the mei bus driver enumerates its devices, it calls the iwlmei's probe
 * function which will send the %SAP_ME_MSG_START message. The probe completes
 * before the response (%SAP_ME_MSG_START_OK) is received. This response will
 * be handle by the Rx path. Once it arrives, the connection to the CSME
 * firmware is considered established and iwlwifi's requests can be treated
 * against the CSME firmware.
 *
 * When the mei bus driver removes the device, iwlmei loses all the data that
 * was attached to the mei client device. It clears the global pointer to the
 * mei client device since it is not available anymore. This will cause all the
 * requests coming from iwlwifi to be cached. This flow takes the global mutex
 * to be synchronized with all the requests coming from iwlwifi.
 */

/**
 * DOC: Driver load when CSME owns the device
 *
 * When the driver (iwlwifi) is loaded while CSME owns the device,
 * it'll ask CSME to release the device through HW registers. CSME
 * will release the device only in the case that there is no connection
 * through the mei bus. If there is a mei bus connection, CSME will refuse
 * to release the ownership on the device through the HW registers. In that
 * case, iwlwifi must first request ownership using the SAP protocol.
 *
 * Once iwlwifi will request ownership through the SAP protocol, CSME will
 * grant the ownership on the device through the HW registers as well.
 * In order to request ownership over SAP, we first need to have an interface
 * which means that we need to register to mac80211.
 * This can't happen before we get the NVM that contains all the capabilities
 * of the device. Reading the NVM usually requires the load the firmware, but
 * this is impossible as long as we don't have ownership on the device.
 * In order to solve this chicken and egg problem, the host driver can get
 * the NVM through CSME which owns the device. It can send
 * %SAP_MSG_NOTIF_GET_NVM, which will be replied by %SAP_MSG_NOTIF_NVM with
 * the NVM's content that the host driver needs.
 */

/**
 * DOC: CSME behavior regarding the ownership requests
 *
 * The ownership requests from the host can come in two different ways:
 *  - the HW registers in iwl_pcie_set_hw_ready
 *  - using the Software Arbitration Protocol (SAP)
 *
 * The host can ask CSME who owns the device with %SAP_MSG_NOTIF_WHO_OWNS_NIC,
 * and it can request ownership with %SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP.
 * The host will first use %SAP_MSG_NOTIF_WHO_OWNS_NIC to know what state
 * CSME is in. In case CSME thinks it owns the device, the host can ask for
 * ownership with %SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP.
 *
 * Here the table that describes CSME's behavior upon ownership request:
 *
 * +-------------------+------------+--------------+-----------------------------+------------+
 * | State             | HW reg bit | Reply for    | Event                       | HW reg bit |
 * |                   | before     | WHO_OWNS_NIC |                             | after      |
 * +===================+============+==============+=============================+============+
 * | WiAMT not         | 0          | Host         | HW register or              | 0          |
 * | operational       | Host owner |              | HOST_ASKS_FOR_NIC_OWNERSHIP | Host owner |
 * +-------------------+------------+--------------+-----------------------------+------------+
 * | Operational &     | 1          | N/A          | HW register                 | 0          |
 * | SAP down &        | CSME owner |              |                             | Host owner |
 * | no session active |            |              |                             |            |
 * +-------------------+------------+--------------+-----------------------------+------------+
 * | Operational &     | 1          | CSME         | HW register                 | 1          |
 * | SAP up            | CSME owner |              |                             | CSME owner |
 * +-------------------+------------+--------------+-----------------------------+------------+
 * | Operational &     | 1          | CSME         | HOST_ASKS_FOR_NIC_OWNERSHIP | 0          |
 * | SAP up            | CSME owner |              |                             | Host owner |
 * +-------------------+------------+--------------+-----------------------------+------------+
 */

/**
 * DOC: Driver load when CSME is associated and a session is active
 *
 * A "session" is active when CSME is associated to an access point and the
 * link is used to attach a remote driver or to control the system remotely.
 * When a session is active, we want to make sure it won't disconnect when we
 * take ownership on the device.
 * In this case, the driver can get the device, but it'll need to make
 * sure that it'll connect to the exact same AP (same BSSID).
 * In order to do so, CSME will send the connection parameters through
 * SAP and then the host can check if it can connect to this same AP.
 * If yes, it can request ownership through SAP and connect quickly without
 * scanning all the channels, but just probing the AP on the channel that
 * CSME was connected to.
 * In order to signal this specific scenario to iwlwifi, iwlmei will
 * immediately require iwlwifi to report RF-Kill to the network stack. This
 * RF-Kill will prevent the stack from getting the device, and it has a reason
 * that tells the userspace that the device is in RF-Kill because it is not
 * owned by the host. Once the userspace has configured the right profile,
 * it'll be able to let iwlmei know that it can request ownership over SAP
 * which will remove the RF-Kill, and finally allow the host to connect.
 * The host has then 3 seconds to connect (including DHCP). Had the host
 * failed to connect within those 3 seconds, CSME will take the device back.
 */

/**
 * DOC: Datapath
 *
 * CSME can transmit packets, through the netdev that it gets from the wifi
 * driver. It'll send packet in the 802.3 format and simply call
 * dev_queue_xmit.
 *
 * For Rx, iwlmei registers a Rx handler that it attaches to the netdev. iwlmei
 * may catch packets and send them to CSME, it can then either drop them so
 * that they are invisible to user space, or let them go the user space.
 *
 * Packets transmitted by the user space do not need to be forwarded to CSME
 * with the exception of the DHCP request. In order to know what IP is used
 * by the user space, CSME needs to get the DHCP request. See
 * iwl_mei_tx_copy_to_csme().
 */

/**
 * enum iwl_mei_nvm_caps - capabilities for MEI NVM
 * @MEI_NVM_CAPS_LARI_SUPPORT: Lari is supported
 * @MEI_NVM_CAPS_11AX_SUPPORT: 11AX is supported
 */
enum iwl_mei_nvm_caps {
	MEI_NVM_CAPS_LARI_SUPPORT	= BIT(0),
	MEI_NVM_CAPS_11AX_SUPPORT	= BIT(1),
};

/**
 * struct iwl_mei_nvm - used to pass the NVM from CSME
 * @hw_addr: The MAC address
 * @n_hw_addrs: The number of MAC addresses
 * @reserved: For alignment.
 * @radio_cfg: The radio configuration.
 * @caps: See &enum iwl_mei_nvm_caps.
 * @nvm_version: The version of the NVM.
 * @channels: The data for each channel.
 *
 * If a field is added, it must correspond to the SAP structure.
 */
struct iwl_mei_nvm {
	u8 hw_addr[ETH_ALEN];
	u8 n_hw_addrs;
	u8 reserved;
	u32 radio_cfg;
	u32 caps;
	u32 nvm_version;
	u32 channels[110];
};

/**
 * enum iwl_mei_pairwise_cipher - cipher for UCAST key
 * @IWL_MEI_CIPHER_NONE: none
 * @IWL_MEI_CIPHER_TKIP: tkip
 * @IWL_MEI_CIPHER_CCMP: ccmp
 * @IWL_MEI_CIPHER_GCMP: gcmp
 * @IWL_MEI_CIPHER_GCMP_256: gcmp 256
 *
 * Note that those values are dictated by the CSME firmware API (see sap.h)
 */
enum iwl_mei_pairwise_cipher {
	IWL_MEI_CIPHER_NONE	= 0,
	IWL_MEI_CIPHER_TKIP	= 2,
	IWL_MEI_CIPHER_CCMP	= 4,
	IWL_MEI_CIPHER_GCMP	= 8,
	IWL_MEI_CIPHER_GCMP_256 = 9,
};

/**
 * enum iwl_mei_akm_auth - a combination of AKM and AUTH method
 * @IWL_MEI_AKM_AUTH_OPEN: No encryption
 * @IWL_MEI_AKM_AUTH_RSNA: 1X profile
 * @IWL_MEI_AKM_AUTH_RSNA_PSK: PSK profile
 * @IWL_MEI_AKM_AUTH_SAE: SAE profile
 *
 * Note that those values are dictated by the CSME firmware API (see sap.h)
 */
enum iwl_mei_akm_auth {
	IWL_MEI_AKM_AUTH_OPEN		= 0,
	IWL_MEI_AKM_AUTH_RSNA		= 6,
	IWL_MEI_AKM_AUTH_RSNA_PSK	= 7,
	IWL_MEI_AKM_AUTH_SAE		= 9,
};

/**
 * struct iwl_mei_conn_info - connection info
 * @lp_state: link protection state
 * @auth_mode: authentication mode
 * @ssid_len: the length of SSID
 * @ssid: the SSID
 * @pairwise_cipher: the cipher used for unicast packets
 * @channel: the associated channel
 * @band: the associated band
 * @bssid: the BSSID
 */
struct iwl_mei_conn_info {
	u8 lp_state;
	u8 auth_mode;
	u8 ssid_len;
	u8 channel;
	u8 band;
	u8 pairwise_cipher;
	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
};

/**
 * struct iwl_mei_colloc_info - collocated AP info
 * @channel: the channel of the collocated AP
 * @bssid: the BSSID of the collocated AP
 */
struct iwl_mei_colloc_info {
	u8 channel;
	u8 bssid[ETH_ALEN];
};

/*
 * struct iwl_mei_ops - driver's operations called by iwlmei
 * Operations will not be called more than once concurrently.
 * It's not allowed to call iwlmei functions from this context.
 *
 * @me_conn_status: provide information about CSME's current connection.
 * @rfkill: called when the wifi driver should report a change in the rfkill
 *	status.
 * @roaming_forbidden: indicates whether roaming is forbidden.
 * @sap_connected: indicate that SAP is now connected. Will be called in case
 *	the wifi driver registered to iwlmei before SAP connection succeeded or
 *	when the SAP connection is re-established.
 * @nic_stolen: this means that device is no longer available. The device can
 *	still be used until the callback returns.
 */
struct iwl_mei_ops {
	void (*me_conn_status)(void *priv,
			       const struct iwl_mei_conn_info *conn_info);
	void (*rfkill)(void *priv, bool blocked, bool csme_taking_ownership);
	void (*roaming_forbidden)(void *priv, bool forbidden);
	void (*sap_connected)(void *priv);
	void (*nic_stolen)(void *priv);
};

#if IS_ENABLED(CONFIG_IWLMEI)

/**
 * iwl_mei_is_connected() - is the connection to the CSME firmware established?
 *
 * Return: true if we have a SAP connection
 */
bool iwl_mei_is_connected(void);

/**
 * iwl_mei_get_nvm() - returns the NVM for the device
 *
 * It is the caller's responsibility to free the memory returned
 * by this function.
 * This function blocks (sleeps) until the NVM is ready.
 *
 * Return: the NVM as received from CSME
 */
struct iwl_mei_nvm *iwl_mei_get_nvm(void);

/**
 * iwl_mei_get_ownership() - request ownership
 *
 * This function blocks until ownership is granted or timeout expired.
 *
 * Return: 0 in case we could get ownership on the device
 */
int iwl_mei_get_ownership(void);

/**
 * iwl_mei_set_rfkill_state() - set SW and HW RF kill states
 * @hw_rfkill: HW RF kill state.
 * @sw_rfkill: SW RF kill state.
 *
 * This function must be called when SW RF kill is issued by the user.
 */
void iwl_mei_set_rfkill_state(bool hw_rfkill, bool sw_rfkill);

/**
 * iwl_mei_set_nic_info() - set mac address
 * @mac_address: mac address to set
 * @nvm_address: NVM mac address to set
 *
 * This function must be called upon mac address change.
 */
void iwl_mei_set_nic_info(const u8 *mac_address, const u8 *nvm_address);

/**
 * iwl_mei_set_country_code() - set new country code
 * @mcc: the new applied MCC
 *
 * This function must be called upon country code update
 */
void iwl_mei_set_country_code(u16 mcc);

/**
 * iwl_mei_set_power_limit() - set TX power limit
 * @power_limit: pointer to an array of 10 elements (le16) represents the power
 * restrictions per chain.
 *
 * This function must be called upon power restrictions change
 */
void iwl_mei_set_power_limit(const __le16 *power_limit);

/**
 * iwl_mei_register() - register the wifi driver to iwlmei
 * @priv: a pointer to the wifi driver's context. Cannot be NULL.
 * @ops: the ops structure.
 *
 * Return: 0 unless something went wrong. It is illegal to call any
 * other API function before this function is called and succeeds.
 *
 * Only one wifi driver instance (wifi device instance really)
 * can register at a time.
 */
int iwl_mei_register(void *priv, const struct iwl_mei_ops *ops);

/**
 * iwl_mei_start_unregister() - unregister the wifi driver from iwlmei
 *
 * From this point on, iwlmei will not used the callbacks provided by
 * the driver, but the device is still usable.
 */
void iwl_mei_start_unregister(void);

/**
 * iwl_mei_unregister_complete() - complete the unregistration
 *
 * Must be called after iwl_mei_start_unregister. When this function returns,
 * the device is owned by CSME.
 */
void iwl_mei_unregister_complete(void);

/**
 * iwl_mei_set_netdev() - sets the netdev for Tx / Rx.
 * @netdev: the net_device
 *
 * The caller should set the netdev to a non-NULL value when the
 * interface is added. Packets might be sent to the driver immediately
 * afterwards.
 * The caller should set the netdev to NULL when the interface is removed.
 * This function will call synchronize_net() after setting the netdev to NULL.
 * Only when this function returns, can the caller assume that iwlmei will
 * no longer inject packets into the netdev's Tx path.
 *
 * Context: This function can sleep and assumes rtnl_lock is taken.
 * The netdev must be set to NULL before iwl_mei_start_unregister() is called.
 */
void iwl_mei_set_netdev(struct net_device *netdev);

/**
 * iwl_mei_tx_copy_to_csme() - must be called for each packet sent by
 * the wifi driver.
 * @skb: the skb sent
 * @ivlen: the size of the IV that needs to be skipped after the MAC and
 *	before the SNAP header.
 *
 * This function doesn't take any lock, it simply tries to catch DHCP
 * packets sent by the wifi driver. If the packet is a DHCP packet, it
 * will send it to CSME. This function must not be called for virtual
 * interfaces that are not monitored by CSME, meaning it must be called
 * only for packets transmitted by the netdevice that was registered
 * with iwl_mei_set_netdev().
 */
void iwl_mei_tx_copy_to_csme(struct sk_buff *skb, unsigned int ivlen);

/**
 * iwl_mei_host_associated() - must be called when iwlwifi associated.
 * @conn_info: pointer to the connection info structure.
 * @colloc_info: pointer to the collocated AP info. This is relevant only in
 *	case of UHB associated AP, otherwise set to NULL.
 */
void iwl_mei_host_associated(const struct iwl_mei_conn_info *conn_info,
			     const struct iwl_mei_colloc_info *colloc_info);

/**
 * iwl_mei_host_disassociated() - must be called when iwlwifi disassociated.
 */
void iwl_mei_host_disassociated(void);

/**
 * iwl_mei_device_state() - must be called when the device changes up/down state
 * @up: true if the device is up, false otherwise.
 */
void iwl_mei_device_state(bool up);

/**
 * iwl_mei_pldr_req() - must be called before loading the fw
 *
 * Requests from the ME that it releases its potential bus access to
 * the WiFi NIC so that the device can safely undergo product reset.
 *
 * Return: 0 if the request was successful and the device can be
 *	reset, a negative error value otherwise
 */
int iwl_mei_pldr_req(void);

/**
 * iwl_mei_alive_notif() - must be called when alive notificaiton is received
 * @success: true if received alive notification, false if waiting for the
 *	notificaiton timed out.
 */
void iwl_mei_alive_notif(bool success);

#else

static inline bool iwl_mei_is_connected(void)
{ return false; }

static inline struct iwl_mei_nvm *iwl_mei_get_nvm(void)
{ return NULL; }

static inline int iwl_mei_get_ownership(void)
{ return 0; }

static inline void iwl_mei_set_rfkill_state(bool hw_rfkill, bool sw_rfkill)
{}

static inline void iwl_mei_set_nic_info(const u8 *mac_address, const u8 *nvm_address)
{}

static inline void iwl_mei_set_country_code(u16 mcc)
{}

static inline void iwl_mei_set_power_limit(const __le16 *power_limit)
{}

static inline int iwl_mei_register(void *priv,
				   const struct iwl_mei_ops *ops)
{ return -EOPNOTSUPP; }

static inline void iwl_mei_start_unregister(void)
{}

static inline void iwl_mei_unregister_complete(void)
{}

static inline void iwl_mei_set_netdev(struct net_device *netdev)
{}

static inline void iwl_mei_tx_copy_to_csme(struct sk_buff *skb,
					   unsigned int ivlen)
{}

static inline void iwl_mei_host_associated(const struct iwl_mei_conn_info *conn_info,
					   const struct iwl_mei_colloc_info *colloc_info)
{}

static inline void iwl_mei_host_disassociated(void)
{}

static inline void iwl_mei_device_state(bool up)
{}

static inline int iwl_mei_pldr_req(void)
{ return 0; }

static inline void iwl_mei_alive_notif(bool success)
{}

#endif /* CONFIG_IWLMEI */

#endif /* __iwl_mei_h__ */
