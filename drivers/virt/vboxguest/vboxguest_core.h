/* SPDX-License-Identifier: (GPL-2.0 OR CDDL-1.0) */
/* Copyright (C) 2010-2016 Oracle Corporation */

#ifndef __VBOXGUEST_CORE_H__
#define __VBOXGUEST_CORE_H__

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/vboxguest.h>
#include "vmmdev.h"

/*
 * The mainline kernel version (this version) of the vboxguest module
 * contained a bug where it defined VBGL_IOCTL_VMMDEV_REQUEST_BIG and
 * VBGL_IOCTL_LOG using _IOC(_IOC_READ | _IOC_WRITE, 'V', ...) instead
 * of _IO(V, ...) as the out of tree VirtualBox upstream version does.
 *
 * These _ALT definitions keep compatibility with the wrong defines the
 * mainline kernel version used for a while.
 * Note the VirtualBox userspace bits have always been built against
 * VirtualBox upstream's headers, so this is likely not necessary. But
 * we must never break our ABI so we keep these around to be 100% sure.
 */
#define VBG_IOCTL_VMMDEV_REQUEST_BIG_ALT _IOC(_IOC_READ | _IOC_WRITE, 'V', 3, 0)
#define VBG_IOCTL_LOG_ALT(s)             _IOC(_IOC_READ | _IOC_WRITE, 'V', 9, s)

struct vbg_session;

/** VBox guest memory balloon. */
struct vbg_mem_balloon {
	/** Work handling VMMDEV_EVENT_BALLOON_CHANGE_REQUEST events */
	struct work_struct work;
	/** Pre-allocated vmmdev_memballoon_info req for query */
	struct vmmdev_memballoon_info *get_req;
	/** Pre-allocated vmmdev_memballoon_change req for inflate / deflate */
	struct vmmdev_memballoon_change *change_req;
	/** The current number of chunks in the balloon. */
	u32 chunks;
	/** The maximum number of chunks in the balloon. */
	u32 max_chunks;
	/**
	 * Array of pointers to page arrays. A page * array is allocated for
	 * each chunk when inflating, and freed when the deflating.
	 */
	struct page ***pages;
};

/**
 * Per bit usage tracker for a u32 mask.
 *
 * Used for optimal handling of guest properties and event filter.
 */
struct vbg_bit_usage_tracker {
	/** Per bit usage counters. */
	u32 per_bit_usage[32];
	/** The current mask according to per_bit_usage. */
	u32 mask;
};

/** VBox guest device (data) extension. */
struct vbg_dev {
	struct device *dev;
	/** The base of the adapter I/O ports. */
	u16 io_port;
	/** Pointer to the mapping of the VMMDev adapter memory. */
	struct vmmdev_memory *mmio;
	/** Host version */
	char host_version[64];
	/** Host features */
	unsigned int host_features;
	/**
	 * Dummy page and vmap address for reserved kernel virtual-address
	 * space for the guest mappings, only used on hosts lacking vtx.
	 */
	struct page *guest_mappings_dummy_page;
	void *guest_mappings;
	/** Spinlock protecting pending_events. */
	spinlock_t event_spinlock;
	/** Preallocated struct vmmdev_events for the IRQ handler. */
	struct vmmdev_events *ack_events_req;
	/** Wait-for-event list for threads waiting for multiple events. */
	wait_queue_head_t event_wq;
	/** Mask of pending events. */
	u32 pending_events;
	/** Wait-for-event list for threads waiting on HGCM async completion. */
	wait_queue_head_t hgcm_wq;
	/** Pre-allocated hgcm cancel2 req. for cancellation on timeout */
	struct vmmdev_hgcm_cancel2 *cancel_req;
	/** Mutex protecting cancel_req accesses */
	struct mutex cancel_req_mutex;
	/** Pre-allocated mouse-status request for the input-device handling. */
	struct vmmdev_mouse_status *mouse_status_req;
	/** Input device for reporting abs mouse coordinates to the guest. */
	struct input_dev *input;

	/** Memory balloon information. */
	struct vbg_mem_balloon mem_balloon;

	/** Lock for session related items in vbg_dev and vbg_session */
	struct mutex session_mutex;
	/** Events we won't permit anyone to filter out. */
	u32 fixed_events;
	/**
	 * Usage counters for the host events (excludes fixed events),
	 * Protected by session_mutex.
	 */
	struct vbg_bit_usage_tracker event_filter_tracker;
	/**
	 * The event filter last reported to the host (or UINT32_MAX).
	 * Protected by session_mutex.
	 */
	u32 event_filter_host;

	/**
	 * Usage counters for guest capabilities. Indexed by capability bit
	 * number, one count per session using a capability.
	 * Protected by session_mutex.
	 */
	struct vbg_bit_usage_tracker guest_caps_tracker;
	/**
	 * The guest capabilities last reported to the host (or UINT32_MAX).
	 * Protected by session_mutex.
	 */
	u32 guest_caps_host;

	/**
	 * Heartbeat timer which fires with interval
	 * cNsHearbeatInterval and its handler sends
	 * VMMDEVREQ_GUEST_HEARTBEAT to VMMDev.
	 */
	struct timer_list heartbeat_timer;
	/** Heartbeat timer interval in ms. */
	int heartbeat_interval_ms;
	/** Preallocated VMMDEVREQ_GUEST_HEARTBEAT request. */
	struct vmmdev_request_header *guest_heartbeat_req;

	/** "vboxguest" char-device */
	struct miscdevice misc_device;
	/** "vboxuser" char-device */
	struct miscdevice misc_device_user;
};

/** The VBoxGuest per session data. */
struct vbg_session {
	/** Pointer to the device extension. */
	struct vbg_dev *gdev;

	/**
	 * Array containing HGCM client IDs associated with this session.
	 * These will be automatically disconnected when the session is closed.
	 * Protected by vbg_gdev.session_mutex.
	 */
	u32 hgcm_client_ids[64];
	/**
	 * Host events requested by the session.
	 * An event type requested in any guest session will be added to the
	 * host filter. Protected by vbg_gdev.session_mutex.
	 */
	u32 event_filter;
	/**
	 * Guest capabilities for this session.
	 * A capability claimed by any guest session will be reported to the
	 * host. Protected by vbg_gdev.session_mutex.
	 */
	u32 guest_caps;
	/** Does this session belong to a root process or a user one? */
	bool user_session;
	/** Set on CANCEL_ALL_WAITEVENTS, protected by vbg_devevent_spinlock. */
	bool cancel_waiters;
};

int  vbg_core_init(struct vbg_dev *gdev, u32 fixed_events);
void vbg_core_exit(struct vbg_dev *gdev);
struct vbg_session *vbg_core_open_session(struct vbg_dev *gdev, bool user);
void vbg_core_close_session(struct vbg_session *session);
int  vbg_core_ioctl(struct vbg_session *session, unsigned int req, void *data);
int  vbg_core_set_mouse_status(struct vbg_dev *gdev, u32 features);

irqreturn_t vbg_core_isr(int irq, void *dev_id);

void vbg_linux_mouse_event(struct vbg_dev *gdev);

/* Private (non exported) functions form vboxguest_utils.c */
void *vbg_req_alloc(size_t len, enum vmmdev_request_type req_type);
void vbg_req_free(void *req, size_t len);
int vbg_req_perform(struct vbg_dev *gdev, void *req);
int vbg_hgcm_call32(
	struct vbg_dev *gdev, u32 client_id, u32 function, u32 timeout_ms,
	struct vmmdev_hgcm_function_parameter32 *parm32, u32 parm_count,
	int *vbox_status);

#endif
