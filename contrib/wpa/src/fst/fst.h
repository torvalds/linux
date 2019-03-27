/*
 * FST module - interface definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FST_H
#define FST_H

#ifdef CONFIG_FST

#include "common/defs.h"
#include "fst/fst_ctrl_iface.h"

/* FST module hostap integration API */

#define US_IN_MS           1000
#define LLT_UNIT_US        32 /* See 10.32.2.2  Transitioning between states */

#define FST_LLT_MS_TO_VAL(m) (((u32) (m)) * US_IN_MS / LLT_UNIT_US)
#define FST_LLT_VAL_TO_MS(v) (((u32) (v)) * LLT_UNIT_US / US_IN_MS)

#define FST_MAX_LLT_MS       FST_LLT_VAL_TO_MS(-1)
#define FST_MAX_PRIO_VALUE   ((u8) -1)
#define FST_MAX_GROUP_ID_LEN IFNAMSIZ

#define FST_DEFAULT_LLT_CFG_VALUE 50

struct hostapd_hw_modes;
struct ieee80211_mgmt;
struct fst_iface;
struct fst_group;
struct fst_session;
struct fst_get_peer_ctx;
struct fst_ctrl_handle;

struct fst_wpa_obj {
	void *ctx;

	/**
	 * get_bssid - Get BSSID of the interface
	 * @ctx: User context %ctx
	 * Returns: BSSID for success, %NULL for failure.
	 *
	 * NOTE: For AP it returns the own BSSID, while for STA - the BSSID of
	 * the associated AP.
	 */
	const u8 * (*get_bssid)(void *ctx);

	/**
	 * get_channel_info - Get current channel info
	 * @ctx: User context %ctx
	 * @hw_mode: OUT, current HW mode
	 * @channel: OUT, current channel
	 */
	void (*get_channel_info)(void *ctx, enum hostapd_hw_mode *hw_mode,
				 u8 *channel);

	/**
	 * get_hw_modes - Get hardware modes
	 * @ctx: User context %ctx
	 * @modes: OUT, pointer on array of hw modes
	 *
	 * Returns: Number of hw modes available.
	 */
	int (*get_hw_modes)(void *ctx, struct hostapd_hw_modes **modes);

	/**
	 * set_ies - Set interface's MB IE
	 * @ctx: User context %ctx
	 * @fst_ies: MB IE buffer (owned by FST module)
	 */
	void (*set_ies)(void *ctx, const struct wpabuf *fst_ies);

	/**
	 * send_action - Send FST Action frame via the interface
	 * @ctx: User context %ctx
	 * @addr: Address of the destination STA
	 * @data: Action frame buffer
	 * Returns: 0 for success, negative error code for failure.
	 */
	int (*send_action)(void *ctx, const u8 *addr, struct wpabuf *data);

	/**
	 * get_mb_ie - Get last MB IE received from STA
	 * @ctx: User context %ctx
	 * @addr: Address of the STA
	 * Returns: MB IE buffer, %NULL if no MB IE received from the STA
	 */
	const struct wpabuf * (*get_mb_ie)(void *ctx, const u8 *addr);

	/**
	 * update_mb_ie - Update last MB IE received from STA
	 * @ctx: User context %ctx
	 * @addr: Address of the STA
	 * @buf: Buffer that contains the MB IEs data
	 * @size: Size of data in %buf
	 */
	void (*update_mb_ie)(void *ctx, const u8 *addr,
			     const u8 *buf, size_t size);

	/**
	 * get_peer_first - Get MAC address of the 1st connected STA
	 * @ctx: User context %ctx
	 * @get_ctx: Context to be used for %get_peer_next call
	 * @mb_only: %TRUE if only multi-band capable peer should be reported
	 * Returns: Address of the 1st connected STA, %NULL if no STAs connected
	 */
	const u8 * (*get_peer_first)(void *ctx,
				     struct fst_get_peer_ctx **get_ctx,
				     Boolean mb_only);
	/**
	 * get_peer_next - Get MAC address of the next connected STA
	 * @ctx: User context %ctx
	 * @get_ctx: Context received from %get_peer_first or previous
	 *           %get_peer_next call
	 * @mb_only: %TRUE if only multi-band capable peer should be reported
	 * Returns: Address of the next connected STA, %NULL if no more STAs
	 *          connected
	 */
	const u8 * (*get_peer_next)(void *ctx,
				    struct fst_get_peer_ctx **get_ctx,
				    Boolean mb_only);
};

/**
 * fst_global_init - Global FST module initiator
 * Returns: 0 for success, negative error code for failure.
 * Note: The purpose of this function is to allocate and initiate global
 *       FST module data structures (linked lists, static data etc.)
 *       This function should be called prior to the 1st %fst_attach call.
 */
int fst_global_init(void);

/**
 * fst_global_deinit - Global FST module de-initiator
 * Note: The purpose of this function is to deallocate and de-initiate global
 *       FST module data structures (linked lists, static data etc.)
 */
void fst_global_deinit(void);

/**
 * struct fst_ctrl - Notification interface for FST module
 */
struct fst_ctrl {
	/**
	 * init - Initialize the notification interface
	 * Returns: 0 for success, negative error code for failure.
	 */
	int (*init)(void);

	/**
	 * deinit - Deinitialize the notification interface
	 */
	void (*deinit)(void);

	/**
	 * on_group_created - Notify about FST group creation
	 * Returns: 0 for success, negative error code for failure.
	 */
	int (*on_group_created)(struct fst_group *g);

	/**
	 * on_group_deleted - Notify about FST group deletion
	 */
	void (*on_group_deleted)(struct fst_group *g);

	/**
	 * on_iface_added - Notify about interface addition
	 * Returns: 0 for success, negative error code for failure.
	 */
	int (*on_iface_added)(struct fst_iface *i);

	/**
	 * on_iface_removed - Notify about interface removal
	 */
	void (*on_iface_removed)(struct fst_iface *i);

	/**
	 * on_session_added - Notify about FST session addition
	 * Returns: 0 for success, negative error code for failure.
	 */
	int (*on_session_added)(struct fst_session *s);

	/**
	 * on_session_removed - Notify about FST session removal
	 */
	void (*on_session_removed)(struct fst_session *s);

	/**
	 * on_event - Notify about FST event
	 * @event_type: Event type
	 * @i: Interface object that relates to the event or NULL
	 * @g: Group object that relates to the event or NULL
	 * @extra - Event specific data (see fst_ctrl_iface.h for more info)
	 */
	void (*on_event)(enum fst_event_type event_type, struct fst_iface *i,
			 struct fst_session *s,
			 const union fst_event_extra *extra);
};

struct fst_ctrl_handle * fst_global_add_ctrl(const struct fst_ctrl *ctrl);
void fst_global_del_ctrl(struct fst_ctrl_handle *h);

/**
 * NOTE: These values have to be read from configuration file
 */
struct fst_iface_cfg {
	char group_id[FST_MAX_GROUP_ID_LEN + 1];
	u8 priority;
	u32 llt;
};

/**
 * fst_attach - Attach interface to an FST group according to configuration read
 * @ifname: Interface name
 * @own_addr: Own interface MAC address
 * @iface_obj: Callbacks to be used by FST module to communicate with
 *             hostapd/wpa_supplicant
 * @cfg: FST-related interface configuration read from the configuration file
 * Returns: FST interface object for success, %NULL for failure.
 */
struct fst_iface * fst_attach(const char *ifname,
			      const u8 *own_addr,
			      const struct fst_wpa_obj *iface_obj,
			      const struct fst_iface_cfg *cfg);

/**
 * fst_detach - Detach an interface
 * @iface: FST interface object
 */
void fst_detach(struct fst_iface *iface);

/* FST module inputs */
/**
 * fst_rx_action - FST Action frames handler
 * @iface: FST interface object
 * @mgmt: Action frame arrived
 * @len: Action frame length
 */
void fst_rx_action(struct fst_iface *iface, const struct ieee80211_mgmt *mgmt,
		   size_t len);

/**
 * fst_notify_peer_connected - FST STA connect handler
 * @iface: FST interface object
 * @addr: Address of the connected STA
 */
void fst_notify_peer_connected(struct fst_iface *iface, const u8 *addr);

/**
 * fst_notify_peer_disconnected - FST STA disconnect handler
 * @iface: FST interface object
 * @addr: Address of the disconnected STA
 */
void fst_notify_peer_disconnected(struct fst_iface *iface, const u8 *addr);

/* FST module auxiliary routines */

/**
 * fst_are_ifaces_aggregated - Determines whether 2 interfaces belong to the
 *                             same FST group
 * @iface1: 1st FST interface object
 * @iface1: 2nd FST interface object
 *
 * Returns: %TRUE if the interfaces belong to the same FST group,
 *          %FALSE otherwise
 */
Boolean fst_are_ifaces_aggregated(struct fst_iface *iface1,
				  struct fst_iface *iface2);

#else /* CONFIG_FST */

static inline int fst_global_init(void)
{
	return 0;
}

static inline int fst_global_start(void)
{
	return 0;
}

static inline void fst_global_stop(void)
{
}

static inline void fst_global_deinit(void)
{
}

#endif /* CONFIG_FST */

#endif /* FST_H */
