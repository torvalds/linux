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

#include "css.h"

/**
 * struct vfio_ccw_private
 * @sch: pointer to the subchannel
 * @completion: synchronization helper of the I/O completion
 * @avail: available for creating a mediated device
 * @mdev: pointer to the mediated device
 * @nb: notifier for vfio events
 */
struct vfio_ccw_private {
	struct subchannel	*sch;
	struct completion	*completion;
	atomic_t		avail;
	struct mdev_device	*mdev;
	struct notifier_block	nb;
} __aligned(8);

extern int vfio_ccw_mdev_reg(struct subchannel *sch);
extern void vfio_ccw_mdev_unreg(struct subchannel *sch);

extern int vfio_ccw_sch_quiesce(struct subchannel *sch);

#endif
