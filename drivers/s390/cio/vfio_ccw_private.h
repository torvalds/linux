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
extern int vfio_ccw_sch_cmd_request(struct vfio_ccw_private *private);

#endif
