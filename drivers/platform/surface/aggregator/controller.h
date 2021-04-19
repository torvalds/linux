/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Main SSAM/SSH controller structure and functionality.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _SURFACE_AGGREGATOR_CONTROLLER_H
#define _SURFACE_AGGREGATOR_CONTROLLER_H

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/serdev.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/serial_hub.h>

#include "ssh_request_layer.h"


/* -- Safe counters. -------------------------------------------------------- */

/**
 * struct ssh_seq_counter - Safe counter for SSH sequence IDs.
 * @value: The current counter value.
 */
struct ssh_seq_counter {
	u8 value;
};

/**
 * struct ssh_rqid_counter - Safe counter for SSH request IDs.
 * @value: The current counter value.
 */
struct ssh_rqid_counter {
	u16 value;
};


/* -- Event/notification system. -------------------------------------------- */

/**
 * struct ssam_nf_head - Notifier head for SSAM events.
 * @srcu: The SRCU struct for synchronization.
 * @head: List-head for notifier blocks registered under this head.
 */
struct ssam_nf_head {
	struct srcu_struct srcu;
	struct list_head head;
};

/**
 * struct ssam_nf - Notifier callback- and activation-registry for SSAM events.
 * @lock:     Lock guarding (de-)registration of notifier blocks. Note: This
 *            lock does not need to be held for notifier calls, only
 *            registration and deregistration.
 * @refcount: The root of the RB-tree used for reference-counting enabled
 *            events/notifications.
 * @head:     The list of notifier heads for event/notification callbacks.
 */
struct ssam_nf {
	struct mutex lock;
	struct rb_root refcount;
	struct ssam_nf_head head[SSH_NUM_EVENTS];
};


/* -- Event/async request completion system. -------------------------------- */

struct ssam_cplt;

/**
 * struct ssam_event_item - Struct for event queuing and completion.
 * @node:     The node in the queue.
 * @rqid:     The request ID of the event.
 * @ops:      Instance specific functions.
 * @ops.free: Callback for freeing this event item.
 * @event:    Actual event data.
 */
struct ssam_event_item {
	struct list_head node;
	u16 rqid;

	struct {
		void (*free)(struct ssam_event_item *event);
	} ops;

	struct ssam_event event;	/* must be last */
};

/**
 * struct ssam_event_queue - Queue for completing received events.
 * @cplt: Reference to the completion system on which this queue is active.
 * @lock: The lock for any operation on the queue.
 * @head: The list-head of the queue.
 * @work: The &struct work_struct performing completion work for this queue.
 */
struct ssam_event_queue {
	struct ssam_cplt *cplt;

	spinlock_t lock;
	struct list_head head;
	struct work_struct work;
};

/**
 * struct ssam_event_target - Set of queues for a single SSH target ID.
 * @queue: The array of queues, one queue per event ID.
 */
struct ssam_event_target {
	struct ssam_event_queue queue[SSH_NUM_EVENTS];
};

/**
 * struct ssam_cplt - SSAM event/async request completion system.
 * @dev:          The device with which this system is associated. Only used
 *                for logging.
 * @wq:           The &struct workqueue_struct on which all completion work
 *                items are queued.
 * @event:        Event completion management.
 * @event.target: Array of &struct ssam_event_target, one for each target.
 * @event.notif:  Notifier callbacks and event activation reference counting.
 */
struct ssam_cplt {
	struct device *dev;
	struct workqueue_struct *wq;

	struct {
		struct ssam_event_target target[SSH_NUM_TARGETS];
		struct ssam_nf notif;
	} event;
};


/* -- Main SSAM device structures. ------------------------------------------ */

/**
 * enum ssam_controller_state - State values for &struct ssam_controller.
 * @SSAM_CONTROLLER_UNINITIALIZED:
 *	The controller has not been initialized yet or has been deinitialized.
 * @SSAM_CONTROLLER_INITIALIZED:
 *	The controller is initialized, but has not been started yet.
 * @SSAM_CONTROLLER_STARTED:
 *	The controller has been started and is ready to use.
 * @SSAM_CONTROLLER_STOPPED:
 *	The controller has been stopped.
 * @SSAM_CONTROLLER_SUSPENDED:
 *	The controller has been suspended.
 */
enum ssam_controller_state {
	SSAM_CONTROLLER_UNINITIALIZED,
	SSAM_CONTROLLER_INITIALIZED,
	SSAM_CONTROLLER_STARTED,
	SSAM_CONTROLLER_STOPPED,
	SSAM_CONTROLLER_SUSPENDED,
};

/**
 * struct ssam_controller_caps - Controller device capabilities.
 * @ssh_power_profile:             SSH power profile.
 * @ssh_buffer_size:               SSH driver UART buffer size.
 * @screen_on_sleep_idle_timeout:  SAM UART screen-on sleep idle timeout.
 * @screen_off_sleep_idle_timeout: SAM UART screen-off sleep idle timeout.
 * @d3_closes_handle:              SAM closes UART handle in D3.
 *
 * Controller and SSH device capabilities found in ACPI.
 */
struct ssam_controller_caps {
	u32 ssh_power_profile;
	u32 ssh_buffer_size;
	u32 screen_on_sleep_idle_timeout;
	u32 screen_off_sleep_idle_timeout;
	u32 d3_closes_handle:1;
};

/**
 * struct ssam_controller - SSAM controller device.
 * @kref:  Reference count of the controller.
 * @lock:  Main lock for the controller, used to guard state changes.
 * @state: Controller state.
 * @rtl:   Request transport layer for SSH I/O.
 * @cplt:  Completion system for SSH/SSAM events and asynchronous requests.
 * @counter:      Safe SSH message ID counters.
 * @counter.seq:  Sequence ID counter.
 * @counter.rqid: Request ID counter.
 * @irq:          Wakeup IRQ resources.
 * @irq.num:      The wakeup IRQ number.
 * @irq.wakeup_enabled: Whether wakeup by IRQ is enabled during suspend.
 * @caps: The controller device capabilities.
 */
struct ssam_controller {
	struct kref kref;

	struct rw_semaphore lock;
	enum ssam_controller_state state;

	struct ssh_rtl rtl;
	struct ssam_cplt cplt;

	struct {
		struct ssh_seq_counter seq;
		struct ssh_rqid_counter rqid;
	} counter;

	struct {
		int num;
		bool wakeup_enabled;
	} irq;

	struct ssam_controller_caps caps;
};

#define to_ssam_controller(ptr, member) \
	container_of(ptr, struct ssam_controller, member)

#define ssam_dbg(ctrl, fmt, ...)  rtl_dbg(&(ctrl)->rtl, fmt, ##__VA_ARGS__)
#define ssam_info(ctrl, fmt, ...) rtl_info(&(ctrl)->rtl, fmt, ##__VA_ARGS__)
#define ssam_warn(ctrl, fmt, ...) rtl_warn(&(ctrl)->rtl, fmt, ##__VA_ARGS__)
#define ssam_err(ctrl, fmt, ...)  rtl_err(&(ctrl)->rtl, fmt, ##__VA_ARGS__)

/**
 * ssam_controller_receive_buf() - Provide input-data to the controller.
 * @ctrl: The controller.
 * @buf:  The input buffer.
 * @n:    The number of bytes in the input buffer.
 *
 * Provide input data to be evaluated by the controller, which has been
 * received via the lower-level transport.
 *
 * Return: Returns the number of bytes consumed, or, if the packet transport
 * layer of the controller has been shut down, %-ESHUTDOWN.
 */
static inline
int ssam_controller_receive_buf(struct ssam_controller *ctrl,
				const unsigned char *buf, size_t n)
{
	return ssh_ptl_rx_rcvbuf(&ctrl->rtl.ptl, buf, n);
}

/**
 * ssam_controller_write_wakeup() - Notify the controller that the underlying
 * device has space available for data to be written.
 * @ctrl: The controller.
 */
static inline void ssam_controller_write_wakeup(struct ssam_controller *ctrl)
{
	ssh_ptl_tx_wakeup_transfer(&ctrl->rtl.ptl);
}

int ssam_controller_init(struct ssam_controller *ctrl, struct serdev_device *s);
int ssam_controller_start(struct ssam_controller *ctrl);
void ssam_controller_shutdown(struct ssam_controller *ctrl);
void ssam_controller_destroy(struct ssam_controller *ctrl);

int ssam_notifier_disable_registered(struct ssam_controller *ctrl);
void ssam_notifier_restore_registered(struct ssam_controller *ctrl);

int ssam_irq_setup(struct ssam_controller *ctrl);
void ssam_irq_free(struct ssam_controller *ctrl);
int ssam_irq_arm_for_wakeup(struct ssam_controller *ctrl);
void ssam_irq_disarm_wakeup(struct ssam_controller *ctrl);

void ssam_controller_lock(struct ssam_controller *c);
void ssam_controller_unlock(struct ssam_controller *c);

int ssam_get_firmware_version(struct ssam_controller *ctrl, u32 *version);
int ssam_ctrl_notif_display_off(struct ssam_controller *ctrl);
int ssam_ctrl_notif_display_on(struct ssam_controller *ctrl);
int ssam_ctrl_notif_d0_exit(struct ssam_controller *ctrl);
int ssam_ctrl_notif_d0_entry(struct ssam_controller *ctrl);

int ssam_controller_suspend(struct ssam_controller *ctrl);
int ssam_controller_resume(struct ssam_controller *ctrl);

int ssam_event_item_cache_init(void);
void ssam_event_item_cache_destroy(void);

#endif /* _SURFACE_AGGREGATOR_CONTROLLER_H */
