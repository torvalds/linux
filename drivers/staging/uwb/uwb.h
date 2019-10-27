/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ultra Wide Band
 * UWB API
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * FIXME: doc: overview of the API, different parts and pointers
 */

#ifndef __LINUX__UWB_H__
#define __LINUX__UWB_H__

#include <linux/limits.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <asm/page.h>
#include "include/spec.h"

struct uwb_dev;
struct uwb_beca_e;
struct uwb_rc;
struct uwb_rsv;
struct uwb_dbg;

/**
 * struct uwb_dev - a UWB Device
 * @rc: UWB Radio Controller that discovered the device (kind of its
 *     parent).
 * @bce: a beacon cache entry for this device; or NULL if the device
 *     is a local radio controller.
 * @mac_addr: the EUI-48 address of this device.
 * @dev_addr: the current DevAddr used by this device.
 * @beacon_slot: the slot number the beacon is using.
 * @streams: bitmap of streams allocated to reservations targeted at
 *     this device.  For an RC, this is the streams allocated for
 *     reservations targeted at DevAddrs.
 *
 * A UWB device may either by a neighbor or part of a local radio
 * controller.
 */
struct uwb_dev {
	struct mutex mutex;
	struct list_head list_node;
	struct device dev;
	struct uwb_rc *rc;		/* radio controller */
	struct uwb_beca_e *bce;		/* Beacon Cache Entry */

	struct uwb_mac_addr mac_addr;
	struct uwb_dev_addr dev_addr;
	int beacon_slot;
	DECLARE_BITMAP(streams, UWB_NUM_STREAMS);
	DECLARE_BITMAP(last_availability_bm, UWB_NUM_MAS);
};
#define to_uwb_dev(d) container_of(d, struct uwb_dev, dev)

/**
 * UWB HWA/WHCI Radio Control {Command|Event} Block context IDs
 *
 * RC[CE]Bs have a 'context ID' field that matches the command with
 * the event received to confirm it.
 *
 * Maximum number of context IDs
 */
enum { UWB_RC_CTX_MAX = 256 };


/** Notification chain head for UWB generated events to listeners */
struct uwb_notifs_chain {
	struct list_head list;
	struct mutex mutex;
};

/* Beacon cache list */
struct uwb_beca {
	struct list_head list;
	size_t entries;
	struct mutex mutex;
};

/* Event handling thread. */
struct uwbd {
	int pid;
	struct task_struct *task;
	wait_queue_head_t wq;
	struct list_head event_list;
	spinlock_t event_list_lock;
};

/**
 * struct uwb_mas_bm - a bitmap of all MAS in a superframe
 * @bm: a bitmap of length #UWB_NUM_MAS
 */
struct uwb_mas_bm {
	DECLARE_BITMAP(bm, UWB_NUM_MAS);
	DECLARE_BITMAP(unsafe_bm, UWB_NUM_MAS);
	int safe;
	int unsafe;
};

/**
 * uwb_rsv_state - UWB Reservation state.
 *
 * NONE - reservation is not active (no DRP IE being transmitted).
 *
 * Owner reservation states:
 *
 * INITIATED - owner has sent an initial DRP request.
 * PENDING - target responded with pending Reason Code.
 * MODIFIED - reservation manager is modifying an established
 * reservation with a different MAS allocation.
 * ESTABLISHED - the reservation has been successfully negotiated.
 *
 * Target reservation states:
 *
 * DENIED - request is denied.
 * ACCEPTED - request is accepted.
 * PENDING - PAL has yet to make a decision to whether to accept or
 * deny.
 *
 * FIXME: further target states TBD.
 */
enum uwb_rsv_state {
	UWB_RSV_STATE_NONE = 0,
	UWB_RSV_STATE_O_INITIATED,
	UWB_RSV_STATE_O_PENDING,
	UWB_RSV_STATE_O_MODIFIED,
	UWB_RSV_STATE_O_ESTABLISHED,
	UWB_RSV_STATE_O_TO_BE_MOVED,
	UWB_RSV_STATE_O_MOVE_EXPANDING,
	UWB_RSV_STATE_O_MOVE_COMBINING,
	UWB_RSV_STATE_O_MOVE_REDUCING,
	UWB_RSV_STATE_T_ACCEPTED,
	UWB_RSV_STATE_T_DENIED,
	UWB_RSV_STATE_T_CONFLICT,
	UWB_RSV_STATE_T_PENDING,
	UWB_RSV_STATE_T_EXPANDING_ACCEPTED,
	UWB_RSV_STATE_T_EXPANDING_CONFLICT,
	UWB_RSV_STATE_T_EXPANDING_PENDING,
	UWB_RSV_STATE_T_EXPANDING_DENIED,
	UWB_RSV_STATE_T_RESIZED,

	UWB_RSV_STATE_LAST,
};

enum uwb_rsv_target_type {
	UWB_RSV_TARGET_DEV,
	UWB_RSV_TARGET_DEVADDR,
};

/**
 * struct uwb_rsv_target - the target of a reservation.
 *
 * Reservations unicast and targeted at a single device
 * (UWB_RSV_TARGET_DEV); or (e.g., in the case of WUSB) targeted at a
 * specific (private) DevAddr (UWB_RSV_TARGET_DEVADDR).
 */
struct uwb_rsv_target {
	enum uwb_rsv_target_type type;
	union {
		struct uwb_dev *dev;
		struct uwb_dev_addr devaddr;
	};
};

struct uwb_rsv_move {
	struct uwb_mas_bm final_mas;
	struct uwb_ie_drp *companion_drp_ie;
	struct uwb_mas_bm companion_mas;
};

/*
 * Number of streams reserved for reservations targeted at DevAddrs.
 */
#define UWB_NUM_GLOBAL_STREAMS 1

typedef void (*uwb_rsv_cb_f)(struct uwb_rsv *rsv);

/**
 * struct uwb_rsv - a DRP reservation
 *
 * Data structure management:
 *
 * @rc:             the radio controller this reservation is for
 *                  (as target or owner)
 * @rc_node:        a list node for the RC
 * @pal_node:       a list node for the PAL
 *
 * Owner and target parameters:
 *
 * @owner:          the UWB device owning this reservation
 * @target:         the target UWB device
 * @type:           reservation type
 *
 * Owner parameters:
 *
 * @max_mas:        maxiumum number of MAS
 * @min_mas:        minimum number of MAS
 * @sparsity:       owner selected sparsity
 * @is_multicast:   true iff multicast
 *
 * @callback:       callback function when the reservation completes
 * @pal_priv:       private data for the PAL making the reservation
 *
 * Reservation status:
 *
 * @status:         negotiation status
 * @stream:         stream index allocated for this reservation
 * @tiebreaker:     conflict tiebreaker for this reservation
 * @mas:            reserved MAS
 * @drp_ie:         the DRP IE
 * @ie_valid:       true iff the DRP IE matches the reservation parameters
 *
 * DRP reservations are uniquely identified by the owner, target and
 * stream index.  However, when using a DevAddr as a target (e.g., for
 * a WUSB cluster reservation) the responses may be received from
 * devices with different DevAddrs.  In this case, reservations are
 * uniquely identified by just the stream index.  A number of stream
 * indexes (UWB_NUM_GLOBAL_STREAMS) are reserved for this.
 */
struct uwb_rsv {
	struct uwb_rc *rc;
	struct list_head rc_node;
	struct list_head pal_node;
	struct kref kref;

	struct uwb_dev *owner;
	struct uwb_rsv_target target;
	enum uwb_drp_type type;
	int max_mas;
	int min_mas;
	int max_interval;
	bool is_multicast;

	uwb_rsv_cb_f callback;
	void *pal_priv;

	enum uwb_rsv_state state;
	bool needs_release_companion_mas;
	u8 stream;
	u8 tiebreaker;
	struct uwb_mas_bm mas;
	struct uwb_ie_drp *drp_ie;
	struct uwb_rsv_move mv;
	bool ie_valid;
	struct timer_list timer;
	struct work_struct handle_timeout_work;
};

static const
struct uwb_mas_bm uwb_mas_bm_zero = { .bm = { 0 } };

static inline void uwb_mas_bm_copy_le(void *dst, const struct uwb_mas_bm *mas)
{
	bitmap_copy_le(dst, mas->bm, UWB_NUM_MAS);
}

/**
 * struct uwb_drp_avail - a radio controller's view of MAS usage
 * @global:   MAS unused by neighbors (excluding reservations targeted
 *            or owned by the local radio controller) or the beaon period
 * @local:    MAS unused by local established reservations
 * @pending:  MAS unused by local pending reservations
 * @ie:       DRP Availability IE to be included in the beacon
 * @ie_valid: true iff @ie is valid and does not need to regenerated from
 *            @global and @local
 *
 * Each radio controller maintains a view of MAS usage or
 * availability. MAS available for a new reservation are determined
 * from the intersection of @global, @local, and @pending.
 *
 * The radio controller must transmit a DRP Availability IE that's the
 * intersection of @global and @local.
 *
 * A set bit indicates the MAS is unused and available.
 *
 * rc->rsvs_mutex should be held before accessing this data structure.
 *
 * [ECMA-368] section 17.4.3.
 */
struct uwb_drp_avail {
	DECLARE_BITMAP(global, UWB_NUM_MAS);
	DECLARE_BITMAP(local, UWB_NUM_MAS);
	DECLARE_BITMAP(pending, UWB_NUM_MAS);
	struct uwb_ie_drp_avail ie;
	bool ie_valid;
};

struct uwb_drp_backoff_win {
	u8 window;
	u8 n;
	int total_expired;
	struct timer_list timer;
	bool can_reserve_extra_mases;
};

const char *uwb_rsv_state_str(enum uwb_rsv_state state);
const char *uwb_rsv_type_str(enum uwb_drp_type type);

struct uwb_rsv *uwb_rsv_create(struct uwb_rc *rc, uwb_rsv_cb_f cb,
			       void *pal_priv);
void uwb_rsv_destroy(struct uwb_rsv *rsv);

int uwb_rsv_establish(struct uwb_rsv *rsv);
int uwb_rsv_modify(struct uwb_rsv *rsv,
		   int max_mas, int min_mas, int sparsity);
void uwb_rsv_terminate(struct uwb_rsv *rsv);

void uwb_rsv_accept(struct uwb_rsv *rsv, uwb_rsv_cb_f cb, void *pal_priv);

void uwb_rsv_get_usable_mas(struct uwb_rsv *orig_rsv, struct uwb_mas_bm *mas);

/**
 * Radio Control Interface instance
 *
 *
 * Life cycle rules: those of the UWB Device.
 *
 * @index:    an index number for this radio controller, as used in the
 *            device name.
 * @version:  version of protocol supported by this device
 * @priv:     Backend implementation; rw with uwb_dev.dev.sem taken.
 * @cmd:      Backend implementation to execute commands; rw and call
 *            only  with uwb_dev.dev.sem taken.
 * @reset:    Hardware reset of radio controller and any PAL controllers.
 * @filter:   Backend implementation to manipulate data to and from device
 *            to be compliant to specification assumed by driver (WHCI
 *            0.95).
 *
 *            uwb_dev.dev.mutex is used to execute commands and update
 *            the corresponding structures; can't use a spinlock
 *            because rc->cmd() can sleep.
 * @ies:         This is a dynamically allocated array cacheing the
 *               IEs (settable by the host) that the beacon of this
 *               radio controller is currently sending.
 *
 *               In reality, we store here the full command we set to
 *               the radio controller (which is basically a command
 *               prefix followed by all the IEs the beacon currently
 *               contains). This way we don't have to realloc and
 *               memcpy when setting it.
 *
 *               We set this up in uwb_rc_ie_setup(), where we alloc
 *               this struct, call get_ie() [so we know which IEs are
 *               currently being sent, if any].
 *
 * @ies_capacity:Amount of space (in bytes) allocated in @ies. The
 *               amount used is given by sizeof(*ies) plus ies->wIELength
 *               (which is a little endian quantity all the time).
 * @ies_mutex:   protect the IE cache
 * @dbg:         information for the debug interface
 */
struct uwb_rc {
	struct uwb_dev uwb_dev;
	int index;
	u16 version;

	struct module *owner;
	void *priv;
	int (*start)(struct uwb_rc *rc);
	void (*stop)(struct uwb_rc *rc);
	int (*cmd)(struct uwb_rc *, const struct uwb_rccb *, size_t);
	int (*reset)(struct uwb_rc *rc);
	int (*filter_cmd)(struct uwb_rc *, struct uwb_rccb **, size_t *);
	int (*filter_event)(struct uwb_rc *, struct uwb_rceb **, const size_t,
			    size_t *, size_t *);

	spinlock_t neh_lock;		/* protects neh_* and ctx_* */
	struct list_head neh_list;	/* Open NE handles */
	unsigned long ctx_bm[UWB_RC_CTX_MAX / 8 / sizeof(unsigned long)];
	u8 ctx_roll;

	int beaconing;			/* Beaconing state [channel number] */
	int beaconing_forced;
	int scanning;
	enum uwb_scan_type scan_type:3;
	unsigned ready:1;
	struct uwb_notifs_chain notifs_chain;
	struct uwb_beca uwb_beca;

	struct uwbd uwbd;

	struct uwb_drp_backoff_win bow;
	struct uwb_drp_avail drp_avail;
	struct list_head reservations;
	struct list_head cnflt_alien_list;
	struct uwb_mas_bm cnflt_alien_bitmap;
	struct mutex rsvs_mutex;
	spinlock_t rsvs_lock;
	struct workqueue_struct *rsv_workq;

	struct delayed_work rsv_update_work;
	struct delayed_work rsv_alien_bp_work;
	int set_drp_ie_pending;
	struct mutex ies_mutex;
	struct uwb_rc_cmd_set_ie *ies;
	size_t ies_capacity;

	struct list_head pals;
	int active_pals;

	struct uwb_dbg *dbg;
};


/**
 * struct uwb_pal - a UWB PAL
 * @name:    descriptive name for this PAL (wusbhc, wlp, etc.).
 * @device:  a device for the PAL.  Used to link the PAL and the radio
 *           controller in sysfs.
 * @rc:      the radio controller the PAL uses.
 * @channel_changed: called when the channel used by the radio changes.
 *           A channel of -1 means the channel has been stopped.
 * @new_rsv: called when a peer requests a reservation (may be NULL if
 *           the PAL cannot accept reservation requests).
 * @channel: channel being used by the PAL; 0 if the PAL isn't using
 *           the radio; -1 if the PAL wishes to use the radio but
 *           cannot.
 * @debugfs_dir: a debugfs directory which the PAL can use for its own
 *           debugfs files.
 *
 * A Protocol Adaptation Layer (PAL) is a user of the WiMedia UWB
 * radio platform (e.g., WUSB, WLP or Bluetooth UWB AMP).
 *
 * The PALs using a radio controller must register themselves to
 * permit the UWB stack to coordinate usage of the radio between the
 * various PALs or to allow PALs to response to certain requests from
 * peers.
 *
 * A struct uwb_pal should be embedded in a containing structure
 * belonging to the PAL and initialized with uwb_pal_init()).  Fields
 * should be set appropriately by the PAL before registering the PAL
 * with uwb_pal_register().
 */
struct uwb_pal {
	struct list_head node;
	const char *name;
	struct device *device;
	struct uwb_rc *rc;

	void (*channel_changed)(struct uwb_pal *pal, int channel);
	void (*new_rsv)(struct uwb_pal *pal, struct uwb_rsv *rsv);

	int channel;
	struct dentry *debugfs_dir;
};

void uwb_pal_init(struct uwb_pal *pal);
int uwb_pal_register(struct uwb_pal *pal);
void uwb_pal_unregister(struct uwb_pal *pal);

int uwb_radio_start(struct uwb_pal *pal);
void uwb_radio_stop(struct uwb_pal *pal);

/*
 * General public API
 *
 * This API can be used by UWB device drivers or by those implementing
 * UWB Radio Controllers
 */
struct uwb_dev *uwb_dev_get_by_devaddr(struct uwb_rc *rc,
				       const struct uwb_dev_addr *devaddr);
struct uwb_dev *uwb_dev_get_by_rc(struct uwb_dev *, struct uwb_rc *);
static inline void uwb_dev_get(struct uwb_dev *uwb_dev)
{
	get_device(&uwb_dev->dev);
}
static inline void uwb_dev_put(struct uwb_dev *uwb_dev)
{
	put_device(&uwb_dev->dev);
}
struct uwb_dev *uwb_dev_try_get(struct uwb_rc *rc, struct uwb_dev *uwb_dev);

/**
 * Callback function for 'uwb_{dev,rc}_foreach()'.
 *
 * @dev:  Linux device instance
 *        'uwb_dev = container_of(dev, struct uwb_dev, dev)'
 * @priv: Data passed by the caller to 'uwb_{dev,rc}_foreach()'.
 *
 * @returns: 0 to continue the iterations, any other val to stop
 *           iterating and return the value to the caller of
 *           _foreach().
 */
typedef int (*uwb_dev_for_each_f)(struct device *dev, void *priv);
int uwb_dev_for_each(struct uwb_rc *rc, uwb_dev_for_each_f func, void *priv);

struct uwb_rc *uwb_rc_alloc(void);
struct uwb_rc *uwb_rc_get_by_dev(const struct uwb_dev_addr *);
struct uwb_rc *uwb_rc_get_by_grandpa(const struct device *);
void uwb_rc_put(struct uwb_rc *rc);

typedef void (*uwb_rc_cmd_cb_f)(struct uwb_rc *rc, void *arg,
                                struct uwb_rceb *reply, ssize_t reply_size);

int uwb_rc_cmd_async(struct uwb_rc *rc, const char *cmd_name,
		     struct uwb_rccb *cmd, size_t cmd_size,
		     u8 expected_type, u16 expected_event,
		     uwb_rc_cmd_cb_f cb, void *arg);
ssize_t uwb_rc_cmd(struct uwb_rc *rc, const char *cmd_name,
		   struct uwb_rccb *cmd, size_t cmd_size,
		   struct uwb_rceb *reply, size_t reply_size);
ssize_t uwb_rc_vcmd(struct uwb_rc *rc, const char *cmd_name,
		    struct uwb_rccb *cmd, size_t cmd_size,
		    u8 expected_type, u16 expected_event,
		    struct uwb_rceb **preply);

size_t __uwb_addr_print(char *, size_t, const unsigned char *, int);

int uwb_rc_dev_addr_set(struct uwb_rc *, const struct uwb_dev_addr *);
int uwb_rc_dev_addr_get(struct uwb_rc *, struct uwb_dev_addr *);
int uwb_rc_mac_addr_set(struct uwb_rc *, const struct uwb_mac_addr *);
int uwb_rc_mac_addr_get(struct uwb_rc *, struct uwb_mac_addr *);
int __uwb_mac_addr_assigned_check(struct device *, void *);
int __uwb_dev_addr_assigned_check(struct device *, void *);

/* Print in @buf a pretty repr of @addr */
static inline size_t uwb_dev_addr_print(char *buf, size_t buf_size,
					const struct uwb_dev_addr *addr)
{
	return __uwb_addr_print(buf, buf_size, addr->data, 0);
}

/* Print in @buf a pretty repr of @addr */
static inline size_t uwb_mac_addr_print(char *buf, size_t buf_size,
					const struct uwb_mac_addr *addr)
{
	return __uwb_addr_print(buf, buf_size, addr->data, 1);
}

/* @returns 0 if device addresses @addr2 and @addr1 are equal */
static inline int uwb_dev_addr_cmp(const struct uwb_dev_addr *addr1,
				   const struct uwb_dev_addr *addr2)
{
	return memcmp(addr1, addr2, sizeof(*addr1));
}

/* @returns 0 if MAC addresses @addr2 and @addr1 are equal */
static inline int uwb_mac_addr_cmp(const struct uwb_mac_addr *addr1,
				   const struct uwb_mac_addr *addr2)
{
	return memcmp(addr1, addr2, sizeof(*addr1));
}

/* @returns !0 if a MAC @addr is a broadcast address */
static inline int uwb_mac_addr_bcast(const struct uwb_mac_addr *addr)
{
	struct uwb_mac_addr bcast = {
		.data = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
	};
	return !uwb_mac_addr_cmp(addr, &bcast);
}

/* @returns !0 if a MAC @addr is all zeroes*/
static inline int uwb_mac_addr_unset(const struct uwb_mac_addr *addr)
{
	struct uwb_mac_addr unset = {
		.data = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
	};
	return !uwb_mac_addr_cmp(addr, &unset);
}

/* @returns !0 if the address is in use. */
static inline unsigned __uwb_dev_addr_assigned(struct uwb_rc *rc,
					       struct uwb_dev_addr *addr)
{
	return uwb_dev_for_each(rc, __uwb_dev_addr_assigned_check, addr);
}

/*
 * UWB Radio Controller API
 *
 * This API is used (in addition to the general API) to implement UWB
 * Radio Controllers.
 */
void uwb_rc_init(struct uwb_rc *);
int uwb_rc_add(struct uwb_rc *, struct device *dev, void *rc_priv);
void uwb_rc_rm(struct uwb_rc *);
void uwb_rc_neh_grok(struct uwb_rc *, void *, size_t);
void uwb_rc_neh_error(struct uwb_rc *, int);
void uwb_rc_reset_all(struct uwb_rc *rc);
void uwb_rc_pre_reset(struct uwb_rc *rc);
int uwb_rc_post_reset(struct uwb_rc *rc);

/**
 * uwb_rsv_is_owner - is the owner of this reservation the RC?
 * @rsv: the reservation
 */
static inline bool uwb_rsv_is_owner(struct uwb_rsv *rsv)
{
	return rsv->owner == &rsv->rc->uwb_dev;
}

/**
 * enum uwb_notifs - UWB events that can be passed to any listeners
 * @UWB_NOTIF_ONAIR: a new neighbour has joined the beacon group.
 * @UWB_NOTIF_OFFAIR: a neighbour has left the beacon group.
 *
 * Higher layers can register callback functions with the radio
 * controller using uwb_notifs_register(). The radio controller
 * maintains a list of all registered handlers and will notify all
 * nodes when an event occurs.
 */
enum uwb_notifs {
	UWB_NOTIF_ONAIR,
	UWB_NOTIF_OFFAIR,
};

/* Callback function registered with UWB */
struct uwb_notifs_handler {
	struct list_head list_node;
	void (*cb)(void *, struct uwb_dev *, enum uwb_notifs);
	void *data;
};

int uwb_notifs_register(struct uwb_rc *, struct uwb_notifs_handler *);
int uwb_notifs_deregister(struct uwb_rc *, struct uwb_notifs_handler *);


/**
 * UWB radio controller Event Size Entry (for creating entry tables)
 *
 * WUSB and WHCI define events and notifications, and they might have
 * fixed or variable size.
 *
 * Each event/notification has a size which is not necessarily known
 * in advance based on the event code. As well, vendor specific
 * events/notifications will have a size impossible to determine
 * unless we know about the device's specific details.
 *
 * It was way too smart of the spec writers not to think that it would
 * be impossible for a generic driver to skip over vendor specific
 * events/notifications if there are no LENGTH fields in the HEADER of
 * each message...the transaction size cannot be counted on as the
 * spec does not forbid to pack more than one event in a single
 * transaction.
 *
 * Thus, we guess sizes with tables (or for events, when you know the
 * size ahead of time you can use uwb_rc_neh_extra_size*()). We
 * register tables with the known events and their sizes, and then we
 * traverse those tables. For those with variable length, we provide a
 * way to lookup the size inside the event/notification's
 * payload. This allows device-specific event size tables to be
 * registered.
 *
 * @size:   Size of the payload
 *
 * @offset: if != 0, at offset @offset-1 starts a field with a length
 *          that has to be added to @size. The format of the field is
 *          given by @type.
 *
 * @type:   Type and length of the offset field. Most common is LE 16
 *          bits (that's why that is zero); others are there mostly to
 *          cover for bugs and weirdos.
 */
struct uwb_est_entry {
	size_t size;
	unsigned offset;
	enum { UWB_EST_16 = 0, UWB_EST_8 = 1 } type;
};

int uwb_est_register(u8 type, u8 code_high, u16 vendor, u16 product,
		     const struct uwb_est_entry *, size_t entries);
int uwb_est_unregister(u8 type, u8 code_high, u16 vendor, u16 product,
		       const struct uwb_est_entry *, size_t entries);
ssize_t uwb_est_find_size(struct uwb_rc *rc, const struct uwb_rceb *rceb,
			  size_t len);

/* -- Misc */

enum {
	EDC_MAX_ERRORS = 10,
	EDC_ERROR_TIMEFRAME = HZ,
};

/* error density counter */
struct edc {
	unsigned long timestart;
	u16 errorcount;
};

static inline
void edc_init(struct edc *edc)
{
	edc->timestart = jiffies;
}

/* Called when an error occurred.
 * This is way to determine if the number of acceptable errors per time
 * period has been exceeded. It is not accurate as there are cases in which
 * this scheme will not work, for example if there are periodic occurrences
 * of errors that straddle updates to the start time. This scheme is
 * sufficient for our usage.
 *
 * @returns 1 if maximum acceptable errors per timeframe has been exceeded.
 */
static inline int edc_inc(struct edc *err_hist, u16 max_err, u16 timeframe)
{
	unsigned long now;

	now = jiffies;
	if (now - err_hist->timestart > timeframe) {
		err_hist->errorcount = 1;
		err_hist->timestart = now;
	} else if (++err_hist->errorcount > max_err) {
			err_hist->errorcount = 0;
			err_hist->timestart = now;
			return 1;
	}
	return 0;
}


/* Information Element handling */

struct uwb_ie_hdr *uwb_ie_next(void **ptr, size_t *len);
int uwb_rc_ie_add(struct uwb_rc *uwb_rc, const struct uwb_ie_hdr *ies, size_t size);
int uwb_rc_ie_rm(struct uwb_rc *uwb_rc, enum uwb_ie element_id);

/*
 * Transmission statistics
 *
 * UWB uses LQI and RSSI (one byte values) for reporting radio signal
 * strength and line quality indication. We do quick and dirty
 * averages of those. They are signed values, btw.
 *
 * For 8 bit quantities, we keep the min, the max, an accumulator
 * (@sigma) and a # of samples. When @samples gets to 255, we compute
 * the average (@sigma / @samples), place it in @sigma and reset
 * @samples to 1 (so we use it as the first sample).
 *
 * Now, statistically speaking, probably I am kicking the kidneys of
 * some books I have in my shelves collecting dust, but I just want to
 * get an approx, not the Nobel.
 *
 * LOCKING: there is no locking per se, but we try to keep a lockless
 * schema. Only _add_samples() modifies the values--as long as you
 * have other locking on top that makes sure that no two calls of
 * _add_sample() happen at the same time, then we are fine. Now, for
 * resetting the values we just set @samples to 0 and that makes the
 * next _add_sample() to start with defaults. Reading the values in
 * _show() currently can race, so you need to make sure the calls are
 * under the same lock that protects calls to _add_sample(). FIXME:
 * currently unlocked (It is not ultraprecise but does the trick. Bite
 * me).
 */
struct stats {
	s8 min, max;
	s16 sigma;
	atomic_t samples;
};

static inline
void stats_init(struct stats *stats)
{
	atomic_set(&stats->samples, 0);
	wmb();
}

static inline
void stats_add_sample(struct stats *stats, s8 sample)
{
	s8 min, max;
	s16 sigma;
	unsigned samples = atomic_read(&stats->samples);
	if (samples == 0) {	/* it was zero before, so we initialize */
		min = 127;
		max = -128;
		sigma = 0;
	} else {
		min = stats->min;
		max = stats->max;
		sigma = stats->sigma;
	}

	if (sample < min)	/* compute new values */
		min = sample;
	else if (sample > max)
		max = sample;
	sigma += sample;

	stats->min = min;	/* commit */
	stats->max = max;
	stats->sigma = sigma;
	if (atomic_add_return(1, &stats->samples) > 255) {
		/* wrapped around! reset */
		stats->sigma = sigma / 256;
		atomic_set(&stats->samples, 1);
	}
}

static inline ssize_t stats_show(struct stats *stats, char *buf)
{
	int min, max, avg;
	int samples = atomic_read(&stats->samples);
	if (samples == 0)
		min = max = avg = 0;
	else {
		min = stats->min;
		max = stats->max;
		avg = stats->sigma / samples;
	}
	return scnprintf(buf, PAGE_SIZE, "%d %d %d\n", min, max, avg);
}

static inline ssize_t stats_store(struct stats *stats, const char *buf,
				  size_t size)
{
	stats_init(stats);
	return size;
}

#endif /* #ifndef __LINUX__UWB_H__ */
