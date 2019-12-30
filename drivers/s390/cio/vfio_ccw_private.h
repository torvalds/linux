/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Private stuff for vfio_ccw driver
 *
 * Copyright IBM Corp. 2017
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Cornelia Huck <cohuck@redhat.com>
 */

#ifndef _VFIO_CCW_PRIVATE_H_
#define _VFIO_CCW_PRIVATE_H_

#include <linux/completion.h>
#include <linux/eventfd.h>
#include <linux/workqueue.h>
#include <linux/vfio_ccw.h>
#include <asm/debug.h>

#include "css.h"
#include "vfio_ccw_cp.h"

#define VFIO_CCW_OFFSET_SHIFT   10
#define VFIO_CCW_OFFSET_TO_INDEX(off)	(off >> VFIO_CCW_OFFSET_SHIFT)
#define VFIO_CCW_INDEX_TO_OFFSET(index)	((u64)(index) << VFIO_CCW_OFFSET_SHIFT)
#define VFIO_CCW_OFFSET_MASK	(((u64)(1) << VFIO_CCW_OFFSET_SHIFT) - 1)

/* capability chain handling similar to vfio-pci */
struct vfio_ccw_private;
struct vfio_ccw_region;

struct vfio_ccw_regops {
	ssize_t	(*read)(struct vfio_ccw_private *private, char __user *buf,
			size_t count, loff_t *ppos);
	ssize_t	(*write)(struct vfio_ccw_private *private,
			 const char __user *buf, size_t count, loff_t *ppos);
	void	(*release)(struct vfio_ccw_private *private,
			   struct vfio_ccw_region *region);
};

struct vfio_ccw_region {
	u32				type;
	u32				subtype;
	const struct vfio_ccw_regops	*ops;
	void				*data;
	size_t				size;
	u32				flags;
};

int vfio_ccw_register_dev_region(struct vfio_ccw_private *private,
				 unsigned int subtype,
				 const struct vfio_ccw_regops *ops,
				 size_t size, u32 flags, void *data);

int vfio_ccw_register_async_dev_regions(struct vfio_ccw_private *private);

/**
 * struct vfio_ccw_private
 * @sch: pointer to the subchannel
 * @state: internal state of the device
 * @completion: synchronization helper of the I/O completion
 * @avail: available for creating a mediated device
 * @mdev: pointer to the mediated device
 * @nb: notifier for vfio events
 * @io_region: MMIO region to input/output I/O arguments/results
 * @io_mutex: protect against concurrent update of I/O regions
 * @region: additional regions for other subchannel operations
 * @cmd_region: MMIO region for asynchronous I/O commands other than START
 * @num_regions: number of additional regions
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
	struct ccw_io_region	*io_region;
	struct mutex		io_mutex;
	struct vfio_ccw_region *region;
	struct ccw_cmd_region	*cmd_region;
	int num_regions;

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
	VFIO_CCW_STATE_CP_PROCESSING,
	VFIO_CCW_STATE_CP_PENDING,
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
	VFIO_CCW_EVENT_ASYNC_REQ,
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
	trace_vfio_ccw_fsm_event(private->sch->schid, private->state, event);
	vfio_ccw_jumptable[private->state][event](private, event);
}

extern struct workqueue_struct *vfio_ccw_work_q;


/* s390 debug feature, similar to base cio */
extern debug_info_t *vfio_ccw_debug_msg_id;
extern debug_info_t *vfio_ccw_debug_trace_id;

#define VFIO_CCW_TRACE_EVENT(imp, txt) \
		debug_text_event(vfio_ccw_debug_trace_id, imp, txt)

#define VFIO_CCW_MSG_EVENT(imp, args...) \
		debug_sprintf_event(vfio_ccw_debug_msg_id, imp, ##args)

static inline void VFIO_CCW_HEX_EVENT(int level, void *data, int length)
{
	debug_event(vfio_ccw_debug_trace_id, level, data, length);
}

#endif
