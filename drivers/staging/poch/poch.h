/*
 * User-space DMA and UIO based Redrapids Pocket Change CardBus driver
 *
 * Copyright 2008 Vijay Kumar <vijaykumar@bravegnu.org>
 *
 * Part of userspace API. Should be moved to a header file in
 * include/linux for final version.
 *
 */

#include <linux/types.h>

struct poch_counters {
	__u32 fifo_empty;
	__u32 fifo_overflow;
	__u32 pll_unlock;
};

struct poch_consume {
	__u32 __user *offsets;
	__u32 nfetch;
	__u32 nflush;
};

#define POCH_IOC_NUM			'9'

#define POCH_IOC_TRANSFER_START		_IO(POCH_IOC_NUM, 0)
#define POCH_IOC_TRANSFER_STOP		_IO(POCH_IOC_NUM, 1)
#define POCH_IOC_GET_COUNTERS		_IOR(POCH_IOC_NUM, 2, \
					     struct poch_counters)
#define POCH_IOC_SYNC_GROUP_FOR_USER	_IO(POCH_IOC_NUM, 3)
#define POCH_IOC_SYNC_GROUP_FOR_DEVICE	_IO(POCH_IOC_NUM, 4)

#define POCH_IOC_CONSUME		_IOWR(POCH_IOC_NUM, 5, \
					      struct poch_consume)
