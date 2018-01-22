/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Private stuff for vfio_ccw driver
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 */

#ifndef _VFIO_CCW_PRIVATE_H_
#define _VFIO_CCW_PRIVATE_H_

#include <linux/completion.h>
#include <linux/eventfd.h>
#include <linux/workqueue.h>
#include <linux/vfio_ccw.h>

#include "css.h"
#include "vfio_ccw_cp.h"

/**
 * struct vfio_ccw_private
 * @sch: pointer to the subchannel
 * @state: internal state of the device
 * @completion: synchronization helper of the I/O completion
 * @avail: available for creating a mediated device
 * @mdev: pointer to the mediated device
 * @nb: notifier for vfio events
 * @io_region: MMIO region to input/output I/O arguments/results
 * @cp: channel program for the current I/O operation
 * @irb: irb info received from interrupt
 * @scsw: scsw info
 * @io_trigger: eventfd ctx for signaling userspace I/O results
 * @io_work: work for deferral process of I/O handling
 */
struct vfio_ccw_private {
	struct subchannel	*sch;
	int			state;
	struct completion	*completion;
	atomic_t		avail;
	struct mdev_device	*mdev;
	struct notifier_block	nb;
	struct ccw_io_region	io_region;

	struct channel_program	cp;
	struct irb		irb;
	union scsw		scsw;

	struct eventfd_ctx	*io_trigger;
	struct work_struct	io_work;
} __aligned(8);

extern int vfio_ccw_mdev_reg(struct subchannel *sch);
extern void vfio_ccw_mdev_unreg(struct subchannel *sch);

extern int vfio_ccw_sch_quiesce(struct subchannel *sch);

/*
 * States of the device statemachine.
 */
enum vfio_ccw_state {
	VFIO_CCW_STATE_NOT_OPER,
	VFIO_CCW_STATE_STANDBY,
	VFIO_CCW_STATE_IDLE,
	VFIO_CCW_STATE_BOXED,
	VFIO_CCW_STATE_BUSY,
	/* last element! */
	NR_VFIO_CCW_STATES
};

/*
 * Asynchronous events of the device statemachine.
 */
enum vfio_ccw_event {
	VFIO_CCW_EVENT_NOT_OPER,
	VFIO_CCW_EVENT_IO_REQ,
	VFIO_CCW_EVENT_INTERRUPT,
	/* last element! */
	NR_VFIO_CCW_EVENTS
};

/*
 * Action called through jumptable.
 */
typedef void (fsm_func_t)(struct vfio_ccw_private *, enum vfio_ccw_event);
extern fsm_func_t *vfio_ccw_jumptable[NR_VFIO_CCW_STATES][NR_VFIO_CCW_EVENTS];

static inline void vfio_ccw_fsm_event(struct vfio_ccw_private *private,
				     int event)
{
	vfio_ccw_jumptable[private->state][event](private, event);
}

extern struct workqueue_struct *vfio_ccw_work_q;

#endif
