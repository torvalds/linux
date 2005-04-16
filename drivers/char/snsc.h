/*
 * SN Platform system controller communication support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * This file contains macros and data types for communication with the
 * system controllers in SGI SN systems.
 */

#ifndef _SN_SYSCTL_H_
#define _SN_SYSCTL_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/sn/types.h>
#include <asm/semaphore.h>

#define CHUNKSIZE 127

/* This structure is used to track an open subchannel. */
struct subch_data_s {
	nasid_t sd_nasid;	/* node on which the subchannel was opened */
	int sd_subch;		/* subchannel number */
	spinlock_t sd_rlock;	/* monitor lock for rsv */
	spinlock_t sd_wlock;	/* monitor lock for wsv */
	wait_queue_head_t sd_rq;	/* wait queue for readers */
	wait_queue_head_t sd_wq;	/* wait queue for writers */
	struct semaphore sd_rbs;	/* semaphore for read buffer */
	struct semaphore sd_wbs;	/* semaphore for write buffer */

	char sd_rb[CHUNKSIZE];	/* read buffer */
	char sd_wb[CHUNKSIZE];	/* write buffer */
};

struct sysctl_data_s {
	struct cdev scd_cdev;	/* Character device info */
	nasid_t scd_nasid;	/* Node on which subchannels are opened. */
};

#endif /* _SN_SYSCTL_H_ */
