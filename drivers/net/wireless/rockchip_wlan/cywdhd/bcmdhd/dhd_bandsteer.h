/*
 * Band Steering logic
 *
 * Feature by which dualband capable PEERs will be
 * forced move on 5GHz interface
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $ Copyright Cypress Semiconductor $
 *
 * $Id: dhd_bandsteer.h 710124 2019-02-20 13:15:23Z $
 */

#ifndef _dhd_bandsteer_h_
#define _dhd_bandsteer_h_

/* Local Types */
typedef struct dhd_bandsteer_context dhd_bandsteer_context_t;

typedef struct dhd_bandsteer_iface_info {
	s32 bssidx;
	s32 channel;
	struct ether_addr macaddr;
	struct net_device *ndev;
} dhd_bandsteer_iface_info_t;

typedef struct dhd_bandsteer_mac_entry {
	struct list_head list;	// Pointer to head of the list
	uint32 dhd_bandsteer_status;	// Usefull in timer call back
	dhd_bandsteer_context_t *dhd_bandsteer_cntx;
	timer_list_compat_t dhd_bandsteer_timer;	// Callback to Advance BS STATEMACHINE
	uint8 wnm_frame_counter;
	struct ether_addr mac_addr;
} dhd_bandsteer_mac_entry_t;

struct dhd_bandsteer_context {
	struct list_head dhd_bandsteer_monitor_list;
	uint8 ifidx_5g;
	dhd_bandsteer_iface_info_t bsd_ifaces[2]; /* idx 0 5G, 1 2G */
	void *dhd_pub;
};

/* Local Types ends */

/* ********************** Function declaration *********************** */
void dhd_bandsteer_process_disassoc(dhd_bandsteer_context_t *dhd_bandsteer_cntx,
	const wl_event_msg_t *e);
s32 dhd_bandsteer_module_init(struct net_device *ndev, bool ap, bool p2p);
s32 dhd_bandsteer_module_deinit(struct net_device *ndev, bool ap, bool p2p);
void dhd_bandsteer_schedule_work_on_timeout(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac);
void dhd_bandsteer_workqueue_wrapper(void *handle, void *event_info, u8 event);
s32 dhd_bandsteer_get_ifaces(void *pub, void *ifaces);
s32 dhd_bandsteer_trigger_bandsteer(struct net_device *, uint8 *);
/* ********************** Function declartion ends ****************** */
#endif /*  _dhd_bandsteer_h_ */
