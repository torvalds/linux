/*
 * Ultra Wide Band
 * UWB internal API
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
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
 * This contains most of the internal API for UWB. This is stuff used
 * across the stack that of course, is of no interest to the rest.
 *
 * Some parts might end up going public (like uwb_rc_*())...
 */

#ifndef __UWB_INTERNAL_H__
#define __UWB_INTERNAL_H__

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/uwb.h>
#include <linux/mutex.h>

struct uwb_beca_e;

/* General device API */
extern void uwb_dev_init(struct uwb_dev *uwb_dev);
extern int __uwb_dev_offair(struct uwb_dev *, struct uwb_rc *);
extern int uwb_dev_add(struct uwb_dev *uwb_dev, struct device *parent_dev,
		       struct uwb_rc *parent_rc);
extern void uwb_dev_rm(struct uwb_dev *uwb_dev);
extern void uwbd_dev_onair(struct uwb_rc *, struct uwb_beca_e *);
extern void uwbd_dev_offair(struct uwb_beca_e *);
void uwb_notify(struct uwb_rc *rc, struct uwb_dev *uwb_dev, enum uwb_notifs event);

/* General UWB Radio Controller Internal API */
extern struct uwb_rc *__uwb_rc_try_get(struct uwb_rc *);
static inline struct uwb_rc *__uwb_rc_get(struct uwb_rc *rc)
{
	uwb_dev_get(&rc->uwb_dev);
	return rc;
}

static inline void __uwb_rc_put(struct uwb_rc *rc)
{
	uwb_dev_put(&rc->uwb_dev);
}

extern int uwb_rc_reset(struct uwb_rc *rc);
extern int uwb_rc_beacon(struct uwb_rc *rc,
			 int channel, unsigned bpst_offset);
extern int uwb_rc_scan(struct uwb_rc *rc,
		       unsigned channel, enum uwb_scan_type type,
		       unsigned bpst_offset);
extern int uwb_rc_send_all_drp_ie(struct uwb_rc *rc);
extern ssize_t uwb_rc_print_IEs(struct uwb_rc *rc, char *, size_t);
extern void uwb_rc_ie_init(struct uwb_rc *);
extern void uwb_rc_ie_init(struct uwb_rc *);
extern ssize_t uwb_rc_ie_setup(struct uwb_rc *);
extern void uwb_rc_ie_release(struct uwb_rc *);
extern int uwb_rc_ie_add(struct uwb_rc *,
			 const struct uwb_ie_hdr *, size_t);
extern int uwb_rc_ie_rm(struct uwb_rc *, enum uwb_ie);
extern int uwb_rc_set_identification_ie(struct uwb_rc *);

extern const char *uwb_rc_strerror(unsigned code);

/*
 * Time to wait for a response to an RC command.
 *
 * Some commands can take a long time to response. e.g., START_BEACON
 * may scan for several superframes before joining an existing beacon
 * group and this can take around 600 ms.
 */
#define UWB_RC_CMD_TIMEOUT_MS 1000 /* ms */

/*
 * Notification/Event Handlers
 */

struct uwb_rc_neh;

void uwb_rc_neh_create(struct uwb_rc *rc);
void uwb_rc_neh_destroy(struct uwb_rc *rc);

struct uwb_rc_neh *uwb_rc_neh_add(struct uwb_rc *rc, struct uwb_rccb *cmd,
				  u8 expected_type, u16 expected_event,
				  uwb_rc_cmd_cb_f cb, void *arg);
void uwb_rc_neh_rm(struct uwb_rc *rc, struct uwb_rc_neh *neh);
void uwb_rc_neh_arm(struct uwb_rc *rc, struct uwb_rc_neh *neh);
void uwb_rc_neh_put(struct uwb_rc_neh *neh);

/* Event size tables */
extern int uwb_est_create(void);
extern void uwb_est_destroy(void);


/*
 * UWB Events & management daemon
 */

/**
 * enum uwb_event_type - types of UWB management daemon events
 *
 * The UWB management daemon (uwbd) can receive two types of events:
 *   UWB_EVT_TYPE_NOTIF - notification from the radio controller.
 *   UWB_EVT_TYPE_MSG   - a simple message.
 */
enum uwb_event_type {
	UWB_EVT_TYPE_NOTIF,
	UWB_EVT_TYPE_MSG,
};

/**
 * struct uwb_event_notif - an event for a radio controller notification
 * @size: Size of the buffer (ie: Guaranteed to contain at least
 *        a full 'struct uwb_rceb')
 * @rceb: Pointer to a kmalloced() event payload
 */
struct uwb_event_notif {
	size_t size;
	struct uwb_rceb *rceb;
};

/**
 * enum uwb_event_message - an event for a message for asynchronous processing
 *
 * UWB_EVT_MSG_RESET - reset the radio controller and all PAL hardware.
 */
enum uwb_event_message {
	UWB_EVT_MSG_RESET,
};

/**
 * UWB Event
 * @rc:         Radio controller that emitted the event (referenced)
 * @ts_jiffies: Timestamp, when was it received
 * @type:       This event's type.
 */
struct uwb_event {
	struct list_head list_node;
	struct uwb_rc *rc;
	unsigned long ts_jiffies;
	enum uwb_event_type type;
	union {
		struct uwb_event_notif notif;
		enum uwb_event_message message;
	};
};

extern void uwbd_start(void);
extern void uwbd_stop(void);
extern struct uwb_event *uwb_event_alloc(size_t, gfp_t gfp_mask);
extern void uwbd_event_queue(struct uwb_event *);
void uwbd_flush(struct uwb_rc *rc);

/* UWB event handlers */
extern int uwbd_evt_handle_rc_beacon(struct uwb_event *);
extern int uwbd_evt_handle_rc_beacon_size(struct uwb_event *);
extern int uwbd_evt_handle_rc_bpoie_change(struct uwb_event *);
extern int uwbd_evt_handle_rc_bp_slot_change(struct uwb_event *);
extern int uwbd_evt_handle_rc_drp(struct uwb_event *);
extern int uwbd_evt_handle_rc_drp_avail(struct uwb_event *);

int uwbd_msg_handle_reset(struct uwb_event *evt);


/*
 * Address management
 */
int uwb_rc_dev_addr_assign(struct uwb_rc *rc);
int uwbd_evt_handle_rc_dev_addr_conflict(struct uwb_event *evt);

/*
 * UWB Beacon Cache
 *
 * Each beacon we received is kept in a cache--when we receive that
 * beacon consistently, that means there is a new device that we have
 * to add to the system.
 */

extern unsigned long beacon_timeout_ms;

/** Beacon cache list */
struct uwb_beca {
	struct list_head list;
	size_t entries;
	struct mutex mutex;
};

extern struct uwb_beca uwb_beca;

/**
 * Beacon cache entry
 *
 * @jiffies_refresh: last time a beacon was  received that refreshed
 *                   this cache entry.
 * @uwb_dev: device connected to this beacon. This pointer is not
 *           safe, you need to get it with uwb_dev_try_get()
 *
 * @hits: how many time we have seen this beacon since last time we
 *        cleared it
 */
struct uwb_beca_e {
	struct mutex mutex;
	struct kref refcnt;
	struct list_head node;
	struct uwb_mac_addr *mac_addr;
	struct uwb_dev_addr dev_addr;
	u8 hits;
	unsigned long ts_jiffies;
	struct uwb_dev *uwb_dev;
	struct uwb_rc_evt_beacon *be;
	struct stats lqe_stats, rssi_stats;	/* radio statistics */
};
struct uwb_beacon_frame;
extern ssize_t uwb_bce_print_IEs(struct uwb_dev *, struct uwb_beca_e *,
				 char *, size_t);
extern struct uwb_beca_e *__uwb_beca_add(struct uwb_rc_evt_beacon *,
					 struct uwb_beacon_frame *,
					 unsigned long);

extern void uwb_bce_kfree(struct kref *_bce);
static inline void uwb_bce_get(struct uwb_beca_e *bce)
{
	kref_get(&bce->refcnt);
}
static inline void uwb_bce_put(struct uwb_beca_e *bce)
{
	kref_put(&bce->refcnt, uwb_bce_kfree);
}
extern void uwb_beca_purge(void);
extern void uwb_beca_release(void);

struct uwb_dev *uwb_dev_get_by_devaddr(struct uwb_rc *rc,
				       const struct uwb_dev_addr *devaddr);
struct uwb_dev *uwb_dev_get_by_macaddr(struct uwb_rc *rc,
				       const struct uwb_mac_addr *macaddr);

/* -- UWB Sysfs representation */
extern struct class uwb_rc_class;
extern struct device_attribute dev_attr_mac_address;
extern struct device_attribute dev_attr_beacon;
extern struct device_attribute dev_attr_scan;

/* -- DRP Bandwidth allocator: bandwidth allocations, reservations, DRP */
void uwb_rsv_init(struct uwb_rc *rc);
int uwb_rsv_setup(struct uwb_rc *rc);
void uwb_rsv_cleanup(struct uwb_rc *rc);

void uwb_rsv_set_state(struct uwb_rsv *rsv, enum uwb_rsv_state new_state);
void uwb_rsv_remove(struct uwb_rsv *rsv);
struct uwb_rsv *uwb_rsv_find(struct uwb_rc *rc, struct uwb_dev *src,
			     struct uwb_ie_drp *drp_ie);
void uwb_rsv_sched_update(struct uwb_rc *rc);

void uwb_drp_handle_timeout(struct uwb_rsv *rsv);
int uwb_drp_ie_update(struct uwb_rsv *rsv);
void uwb_drp_ie_to_bm(struct uwb_mas_bm *bm, const struct uwb_ie_drp *drp_ie);

void uwb_drp_avail_init(struct uwb_rc *rc);
int  uwb_drp_avail_reserve_pending(struct uwb_rc *rc, struct uwb_mas_bm *mas);
void uwb_drp_avail_reserve(struct uwb_rc *rc, struct uwb_mas_bm *mas);
void uwb_drp_avail_release(struct uwb_rc *rc, struct uwb_mas_bm *mas);
void uwb_drp_avail_ie_update(struct uwb_rc *rc);

/* -- PAL support */
void uwb_rc_pal_init(struct uwb_rc *rc);

/* -- Misc */

extern ssize_t uwb_mac_frame_hdr_print(char *, size_t,
				       const struct uwb_mac_frame_hdr *);

/* -- Debug interface */
void uwb_dbg_init(void);
void uwb_dbg_exit(void);
void uwb_dbg_add_rc(struct uwb_rc *rc);
void uwb_dbg_del_rc(struct uwb_rc *rc);

/* Workarounds for version specific stuff */

static inline void uwb_dev_lock(struct uwb_dev *uwb_dev)
{
	down(&uwb_dev->dev.sem);
}

static inline void uwb_dev_unlock(struct uwb_dev *uwb_dev)
{
	up(&uwb_dev->dev.sem);
}

#endif /* #ifndef __UWB_INTERNAL_H__ */
