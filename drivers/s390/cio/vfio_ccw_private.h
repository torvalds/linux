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
 */
struct vfio_ccw_private {
	struct subchannel	*sch;
	struct completion	*completion;
} __aligned(8);

#endif
