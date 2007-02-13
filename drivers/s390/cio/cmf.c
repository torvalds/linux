/*
 * linux/drivers/s390/cio/cmf.c
 *
 * Linux on zSeries Channel Measurement Facility support
 *
 * Copyright 2000,2006 IBM Corporation
 *
 * Authors: Arnd Bergmann <arndb@de.ibm.com>
 *	    Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * original idea from Natarajan Krishnaswami <nkrishna@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/bootmem.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/timex.h>	/* get_clock() */

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/cmb.h>
#include <asm/div64.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "chsc.h"

/* parameter to enable cmf during boot, possible uses are:
 *  "s390cmf" -- enable cmf and allocate 2 MB of ram so measuring can be
 *               used on any subchannel
 *  "s390cmf=<num>" -- enable cmf and allocate enough memory to measure
 *                     <num> subchannel, where <num> is an integer
 *                     between 1 and 65535, default is 1024
 */
#define ARGSTRING "s390cmf"

/* indices for READCMB */
enum cmb_index {
 /* basic and exended format: */
	cmb_ssch_rsch_count,
	cmb_sample_count,
	cmb_device_connect_time,
	cmb_function_pending_time,
	cmb_device_disconnect_time,
	cmb_control_unit_queuing_time,
	cmb_device_active_only_time,
 /* extended format only: */
	cmb_device_busy_time,
	cmb_initial_command_response_time,
};

/**
 * enum cmb_format - types of supported measurement block formats
 *
 * @CMF_BASIC:      traditional channel measurement blocks supported
 * 		    by all machines that we run on
 * @CMF_EXTENDED:   improved format that was introduced with the z990
 * 		    machine
 * @CMF_AUTODETECT: default: use extended format when running on a z990
 *                  or later machine, otherwise fall back to basic format
 **/
enum cmb_format {
	CMF_BASIC,
	CMF_EXTENDED,
	CMF_AUTODETECT = -1,
};
/**
 * format - actual format for all measurement blocks
 *
 * The format module parameter can be set to a value of 0 (zero)
 * or 1, indicating basic or extended format as described for
 * enum cmb_format.
 */
static int format = CMF_AUTODETECT;
module_param(format, bool, 0444);

/**
 * struct cmb_operations - functions to use depending on cmb_format
 *
 * Most of these functions operate on a struct ccw_device. There is only
 * one instance of struct cmb_operations because the format of the measurement
 * data is guaranteed to be the same for every ccw_device.
 *
 * @alloc:	allocate memory for a channel measurement block,
 *		either with the help of a special pool or with kmalloc
 * @free:	free memory allocated with @alloc
 * @set:	enable or disable measurement
 * @readall:	read a measurement block in a common format
 * @reset:	clear the data in the associated measurement block and
 *		reset its time stamp
 * @align:	align an allocated block so that the hardware can use it
 */
struct cmb_operations {
	int (*alloc)  (struct ccw_device*);
	void(*free)   (struct ccw_device*);
	int (*set)    (struct ccw_device*, u32);
	u64 (*read)   (struct ccw_device*, int);
	int (*readall)(struct ccw_device*, struct cmbdata *);
	void (*reset) (struct ccw_device*);
	void * (*align) (void *);

	struct attribute_group *attr_group;
};
static struct cmb_operations *cmbops;

struct cmb_data {
	void *hw_block;   /* Pointer to block updated by hardware */
	void *last_block; /* Last changed block copied from hardware block */
	int size;	  /* Size of hw_block and last_block */
	unsigned long long last_update;  /* when last_block was updated */
};

/* our user interface is designed in terms of nanoseconds,
 * while the hardware measures total times in its own
 * unit.*/
static inline u64 time_to_nsec(u32 value)
{
	return ((u64)value) * 128000ull;
}

/*
 * Users are usually interested in average times,
 * not accumulated time.
 * This also helps us with atomicity problems
 * when reading sinlge values.
 */
static inline u64 time_to_avg_nsec(u32 value, u32 count)
{
	u64 ret;

	/* no samples yet, avoid division by 0 */
	if (count == 0)
		return 0;

	/* value comes in units of 128 µsec */
	ret = time_to_nsec(value);
	do_div(ret, count);

	return ret;
}

/* activate or deactivate the channel monitor. When area is NULL,
 * the monitor is deactivated. The channel monitor needs to
 * be active in order to measure subchannels, which also need
 * to be enabled. */
static inline void
cmf_activate(void *area, unsigned int onoff)
{
	register void * __gpr2 asm("2");
	register long __gpr1 asm("1");

	__gpr2 = area;
	__gpr1 = onoff ? 2 : 0;
	/* activate channel measurement */
	asm("schm" : : "d" (__gpr2), "d" (__gpr1) );
}

static int
set_schib(struct ccw_device *cdev, u32 mme, int mbfc, unsigned long address)
{
	int ret;
	int retry;
	struct subchannel *sch;
	struct schib *schib;

	sch = to_subchannel(cdev->dev.parent);
	schib = &sch->schib;
	/* msch can silently fail, so do it again if necessary */
	for (retry = 0; retry < 3; retry++) {
		/* prepare schib */
		stsch(sch->schid, schib);
		schib->pmcw.mme  = mme;
		schib->pmcw.mbfc = mbfc;
		/* address can be either a block address or a block index */
		if (mbfc)
			schib->mba = address;
		else
			schib->pmcw.mbi = address;

		/* try to submit it */
		switch(ret = msch_err(sch->schid, schib)) {
			case 0:
				break;
			case 1:
			case 2: /* in I/O or status pending */
				ret = -EBUSY;
				break;
			case 3: /* subchannel is no longer valid */
				ret = -ENODEV;
				break;
			default: /* msch caught an exception */
				ret = -EINVAL;
				break;
		}
		stsch(sch->schid, schib); /* restore the schib */

		if (ret)
			break;

		/* check if it worked */
		if (schib->pmcw.mme  == mme &&
		    schib->pmcw.mbfc == mbfc &&
		    (mbfc ? (schib->mba == address)
			  : (schib->pmcw.mbi == address)))
			return 0;

		ret = -EINVAL;
	}

	return ret;
}

struct set_schib_struct {
	u32 mme;
	int mbfc;
	unsigned long address;
	wait_queue_head_t wait;
	int ret;
	struct kref kref;
};

static void cmf_set_schib_release(struct kref *kref)
{
	struct set_schib_struct *set_data;

	set_data = container_of(kref, struct set_schib_struct, kref);
	kfree(set_data);
}

#define CMF_PENDING 1

static int set_schib_wait(struct ccw_device *cdev, u32 mme,
				int mbfc, unsigned long address)
{
	struct set_schib_struct *set_data;
	int ret;

	spin_lock_irq(cdev->ccwlock);
	if (!cdev->private->cmb) {
		ret = -ENODEV;
		goto out;
	}
	set_data = kzalloc(sizeof(struct set_schib_struct), GFP_ATOMIC);
	if (!set_data) {
		ret = -ENOMEM;
		goto out;
	}
	init_waitqueue_head(&set_data->wait);
	kref_init(&set_data->kref);
	set_data->mme = mme;
	set_data->mbfc = mbfc;
	set_data->address = address;

	ret = set_schib(cdev, mme, mbfc, address);
	if (ret != -EBUSY)
		goto out_put;

	if (cdev->private->state != DEV_STATE_ONLINE) {
		/* if the device is not online, don't even try again */
		ret = -EBUSY;
		goto out_put;
	}

	cdev->private->state = DEV_STATE_CMFCHANGE;
	set_data->ret = CMF_PENDING;
	cdev->private->cmb_wait = set_data;

	spin_unlock_irq(cdev->ccwlock);
	if (wait_event_interruptible(set_data->wait,
				     set_data->ret != CMF_PENDING)) {
		spin_lock_irq(cdev->ccwlock);
		if (set_data->ret == CMF_PENDING) {
			set_data->ret = -ERESTARTSYS;
			if (cdev->private->state == DEV_STATE_CMFCHANGE)
				cdev->private->state = DEV_STATE_ONLINE;
		}
		spin_unlock_irq(cdev->ccwlock);
	}
	spin_lock_irq(cdev->ccwlock);
	cdev->private->cmb_wait = NULL;
	ret = set_data->ret;
out_put:
	kref_put(&set_data->kref, cmf_set_schib_release);
out:
	spin_unlock_irq(cdev->ccwlock);
	return ret;
}

void retry_set_schib(struct ccw_device *cdev)
{
	struct set_schib_struct *set_data;

	set_data = cdev->private->cmb_wait;
	if (!set_data) {
		WARN_ON(1);
		return;
	}
	kref_get(&set_data->kref);
	set_data->ret = set_schib(cdev, set_data->mme, set_data->mbfc,
				  set_data->address);
	wake_up(&set_data->wait);
	kref_put(&set_data->kref, cmf_set_schib_release);
}

static int cmf_copy_block(struct ccw_device *cdev)
{
	struct subchannel *sch;
	void *reference_buf;
	void *hw_block;
	struct cmb_data *cmb_data;

	sch = to_subchannel(cdev->dev.parent);

	if (stsch(sch->schid, &sch->schib))
		return -ENODEV;

	if (sch->schib.scsw.fctl & SCSW_FCTL_START_FUNC) {
		/* Don't copy if a start function is in progress. */
		if ((!sch->schib.scsw.actl & SCSW_ACTL_SUSPENDED) &&
		    (sch->schib.scsw.actl &
		     (SCSW_ACTL_DEVACT | SCSW_ACTL_SCHACT)) &&
		    (!sch->schib.scsw.stctl & SCSW_STCTL_SEC_STATUS))
			return -EBUSY;
	}
	cmb_data = cdev->private->cmb;
	hw_block = cmbops->align(cmb_data->hw_block);
	if (!memcmp(cmb_data->last_block, hw_block, cmb_data->size))
		/* No need to copy. */
		return 0;
	reference_buf = kzalloc(cmb_data->size, GFP_ATOMIC);
	if (!reference_buf)
		return -ENOMEM;
	/* Ensure consistency of block copied from hardware. */
	do {
		memcpy(cmb_data->last_block, hw_block, cmb_data->size);
		memcpy(reference_buf, hw_block, cmb_data->size);
	} while (memcmp(cmb_data->last_block, reference_buf, cmb_data->size));
	cmb_data->last_update = get_clock();
	kfree(reference_buf);
	return 0;
}

struct copy_block_struct {
	wait_queue_head_t wait;
	int ret;
	struct kref kref;
};

static void cmf_copy_block_release(struct kref *kref)
{
	struct copy_block_struct *copy_block;

	copy_block = container_of(kref, struct copy_block_struct, kref);
	kfree(copy_block);
}

static int cmf_cmb_copy_wait(struct ccw_device *cdev)
{
	struct copy_block_struct *copy_block;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		ret = -ENODEV;
		goto out;
	}
	copy_block = kzalloc(sizeof(struct copy_block_struct), GFP_ATOMIC);
	if (!copy_block) {
		ret = -ENOMEM;
		goto out;
	}
	init_waitqueue_head(&copy_block->wait);
	kref_init(&copy_block->kref);

	ret = cmf_copy_block(cdev);
	if (ret != -EBUSY)
		goto out_put;

	if (cdev->private->state != DEV_STATE_ONLINE) {
		ret = -EBUSY;
		goto out_put;
	}

	cdev->private->state = DEV_STATE_CMFUPDATE;
	copy_block->ret = CMF_PENDING;
	cdev->private->cmb_wait = copy_block;

	spin_unlock_irqrestore(cdev->ccwlock, flags);
	if (wait_event_interruptible(copy_block->wait,
				     copy_block->ret != CMF_PENDING)) {
		spin_lock_irqsave(cdev->ccwlock, flags);
		if (copy_block->ret == CMF_PENDING) {
			copy_block->ret = -ERESTARTSYS;
			if (cdev->private->state == DEV_STATE_CMFUPDATE)
				cdev->private->state = DEV_STATE_ONLINE;
		}
		spin_unlock_irqrestore(cdev->ccwlock, flags);
	}
	spin_lock_irqsave(cdev->ccwlock, flags);
	cdev->private->cmb_wait = NULL;
	ret = copy_block->ret;
out_put:
	kref_put(&copy_block->kref, cmf_copy_block_release);
out:
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	return ret;
}

void cmf_retry_copy_block(struct ccw_device *cdev)
{
	struct copy_block_struct *copy_block;

	copy_block = cdev->private->cmb_wait;
	if (!copy_block) {
		WARN_ON(1);
		return;
	}
	kref_get(&copy_block->kref);
	copy_block->ret = cmf_copy_block(cdev);
	wake_up(&copy_block->wait);
	kref_put(&copy_block->kref, cmf_copy_block_release);
}

static void cmf_generic_reset(struct ccw_device *cdev)
{
	struct cmb_data *cmb_data;

	spin_lock_irq(cdev->ccwlock);
	cmb_data = cdev->private->cmb;
	if (cmb_data) {
		memset(cmb_data->last_block, 0, cmb_data->size);
		/*
		 * Need to reset hw block as well to make the hardware start
		 * from 0 again.
		 */
		memset(cmbops->align(cmb_data->hw_block), 0, cmb_data->size);
		cmb_data->last_update = 0;
	}
	cdev->private->cmb_start_time = get_clock();
	spin_unlock_irq(cdev->ccwlock);
}

/**
 * struct cmb_area - container for global cmb data
 *
 * @mem:	pointer to CMBs (only in basic measurement mode)
 * @list:	contains a linked list of all subchannels
 * @lock:	protect concurrent access to @mem and @list
 */
struct cmb_area {
	struct cmb *mem;
	struct list_head list;
	int num_channels;
	spinlock_t lock;
};

static struct cmb_area cmb_area = {
	.lock = SPIN_LOCK_UNLOCKED,
	.list = LIST_HEAD_INIT(cmb_area.list),
	.num_channels  = 1024,
};


/* ****** old style CMB handling ********/

/** int maxchannels
 *
 * Basic channel measurement blocks are allocated in one contiguous
 * block of memory, which can not be moved as long as any channel
 * is active. Therefore, a maximum number of subchannels needs to
 * be defined somewhere. This is a module parameter, defaulting to
 * a resonable value of 1024, or 32 kb of memory.
 * Current kernels don't allow kmalloc with more than 128kb, so the
 * maximum is 4096
 */

module_param_named(maxchannels, cmb_area.num_channels, uint, 0444);

/**
 * struct cmb - basic channel measurement block
 *
 * cmb as used by the hardware the fields are described in z/Architecture
 * Principles of Operation, chapter 17.
 * The area to be a contiguous array and may not be reallocated or freed.
 * Only one cmb area can be present in the system.
 */
struct cmb {
	u16 ssch_rsch_count;
	u16 sample_count;
	u32 device_connect_time;
	u32 function_pending_time;
	u32 device_disconnect_time;
	u32 control_unit_queuing_time;
	u32 device_active_only_time;
	u32 reserved[2];
};

/* insert a single device into the cmb_area list
 * called with cmb_area.lock held from alloc_cmb
 */
static int alloc_cmb_single(struct ccw_device *cdev,
			    struct cmb_data *cmb_data)
{
	struct cmb *cmb;
	struct ccw_device_private *node;
	int ret;

	spin_lock_irq(cdev->ccwlock);
	if (!list_empty(&cdev->private->cmb_list)) {
		ret = -EBUSY;
		goto out;
	}

	/* find first unused cmb in cmb_area.mem.
	 * this is a little tricky: cmb_area.list
	 * remains sorted by ->cmb->hw_data pointers */
	cmb = cmb_area.mem;
	list_for_each_entry(node, &cmb_area.list, cmb_list) {
		struct cmb_data *data;
		data = node->cmb;
		if ((struct cmb*)data->hw_block > cmb)
			break;
		cmb++;
	}
	if (cmb - cmb_area.mem >= cmb_area.num_channels) {
		ret = -ENOMEM;
		goto out;
	}

	/* insert new cmb */
	list_add_tail(&cdev->private->cmb_list, &node->cmb_list);
	cmb_data->hw_block = cmb;
	cdev->private->cmb = cmb_data;
	ret = 0;
out:
	spin_unlock_irq(cdev->ccwlock);
	return ret;
}

static int
alloc_cmb (struct ccw_device *cdev)
{
	int ret;
	struct cmb *mem;
	ssize_t size;
	struct cmb_data *cmb_data;

	/* Allocate private cmb_data. */
	cmb_data = kzalloc(sizeof(struct cmb_data), GFP_KERNEL);
	if (!cmb_data)
		return -ENOMEM;

	cmb_data->last_block = kzalloc(sizeof(struct cmb), GFP_KERNEL);
	if (!cmb_data->last_block) {
		kfree(cmb_data);
		return -ENOMEM;
	}
	cmb_data->size = sizeof(struct cmb);
	spin_lock(&cmb_area.lock);

	if (!cmb_area.mem) {
		/* there is no user yet, so we need a new area */
		size = sizeof(struct cmb) * cmb_area.num_channels;
		WARN_ON(!list_empty(&cmb_area.list));

		spin_unlock(&cmb_area.lock);
		mem = (void*)__get_free_pages(GFP_KERNEL | GFP_DMA,
				 get_order(size));
		spin_lock(&cmb_area.lock);

		if (cmb_area.mem) {
			/* ok, another thread was faster */
			free_pages((unsigned long)mem, get_order(size));
		} else if (!mem) {
			/* no luck */
			ret = -ENOMEM;
			goto out;
		} else {
			/* everything ok */
			memset(mem, 0, size);
			cmb_area.mem = mem;
			cmf_activate(cmb_area.mem, 1);
		}
	}

	/* do the actual allocation */
	ret = alloc_cmb_single(cdev, cmb_data);
out:
	spin_unlock(&cmb_area.lock);
	if (ret) {
		kfree(cmb_data->last_block);
		kfree(cmb_data);
	}
	return ret;
}

static void free_cmb(struct ccw_device *cdev)
{
	struct ccw_device_private *priv;
	struct cmb_data *cmb_data;

	spin_lock(&cmb_area.lock);
	spin_lock_irq(cdev->ccwlock);

	priv = cdev->private;

	if (list_empty(&priv->cmb_list)) {
		/* already freed */
		goto out;
	}

	cmb_data = priv->cmb;
	priv->cmb = NULL;
	if (cmb_data)
		kfree(cmb_data->last_block);
	kfree(cmb_data);
	list_del_init(&priv->cmb_list);

	if (list_empty(&cmb_area.list)) {
		ssize_t size;
		size = sizeof(struct cmb) * cmb_area.num_channels;
		cmf_activate(NULL, 0);
		free_pages((unsigned long)cmb_area.mem, get_order(size));
		cmb_area.mem = NULL;
	}
out:
	spin_unlock_irq(cdev->ccwlock);
	spin_unlock(&cmb_area.lock);
}

static int set_cmb(struct ccw_device *cdev, u32 mme)
{
	u16 offset;
	struct cmb_data *cmb_data;
	unsigned long flags;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return -EINVAL;
	}
	cmb_data = cdev->private->cmb;
	offset = mme ? (struct cmb *)cmb_data->hw_block - cmb_area.mem : 0;
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	return set_schib_wait(cdev, mme, 0, offset);
}

static u64 read_cmb (struct ccw_device *cdev, int index)
{
	struct cmb *cmb;
	u32 val;
	int ret;
	unsigned long flags;

	ret = cmf_cmb_copy_wait(cdev);
	if (ret < 0)
		return 0;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		ret = 0;
		goto out;
	}
	cmb = ((struct cmb_data *)cdev->private->cmb)->last_block;

	switch (index) {
	case cmb_ssch_rsch_count:
		ret = cmb->ssch_rsch_count;
		goto out;
	case cmb_sample_count:
		ret = cmb->sample_count;
		goto out;
	case cmb_device_connect_time:
		val = cmb->device_connect_time;
		break;
	case cmb_function_pending_time:
		val = cmb->function_pending_time;
		break;
	case cmb_device_disconnect_time:
		val = cmb->device_disconnect_time;
		break;
	case cmb_control_unit_queuing_time:
		val = cmb->control_unit_queuing_time;
		break;
	case cmb_device_active_only_time:
		val = cmb->device_active_only_time;
		break;
	default:
		ret = 0;
		goto out;
	}
	ret = time_to_avg_nsec(val, cmb->sample_count);
out:
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	return ret;
}

static int readall_cmb (struct ccw_device *cdev, struct cmbdata *data)
{
	struct cmb *cmb;
	struct cmb_data *cmb_data;
	u64 time;
	unsigned long flags;
	int ret;

	ret = cmf_cmb_copy_wait(cdev);
	if (ret < 0)
		return ret;
	spin_lock_irqsave(cdev->ccwlock, flags);
	cmb_data = cdev->private->cmb;
	if (!cmb_data) {
		ret = -ENODEV;
		goto out;
	}
	if (cmb_data->last_update == 0) {
		ret = -EAGAIN;
		goto out;
	}
	cmb = cmb_data->last_block;
	time = cmb_data->last_update - cdev->private->cmb_start_time;

	memset(data, 0, sizeof(struct cmbdata));

	/* we only know values before device_busy_time */
	data->size = offsetof(struct cmbdata, device_busy_time);

	/* convert to nanoseconds */
	data->elapsed_time = (time * 1000) >> 12;

	/* copy data to new structure */
	data->ssch_rsch_count = cmb->ssch_rsch_count;
	data->sample_count = cmb->sample_count;

	/* time fields are converted to nanoseconds while copying */
	data->device_connect_time = time_to_nsec(cmb->device_connect_time);
	data->function_pending_time = time_to_nsec(cmb->function_pending_time);
	data->device_disconnect_time =
		time_to_nsec(cmb->device_disconnect_time);
	data->control_unit_queuing_time
		= time_to_nsec(cmb->control_unit_queuing_time);
	data->device_active_only_time
		= time_to_nsec(cmb->device_active_only_time);
	ret = 0;
out:
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	return ret;
}

static void reset_cmb(struct ccw_device *cdev)
{
	cmf_generic_reset(cdev);
}

static void * align_cmb(void *area)
{
	return area;
}

static struct attribute_group cmf_attr_group;

static struct cmb_operations cmbops_basic = {
	.alloc	= alloc_cmb,
	.free	= free_cmb,
	.set	= set_cmb,
	.read	= read_cmb,
	.readall    = readall_cmb,
	.reset	    = reset_cmb,
	.align	    = align_cmb,
	.attr_group = &cmf_attr_group,
};

/* ******** extended cmb handling ********/

/**
 * struct cmbe - extended channel measurement block
 *
 * cmb as used by the hardware, may be in any 64 bit physical location,
 * the fields are described in z/Architecture Principles of Operation,
 * third edition, chapter 17.
 */
struct cmbe {
	u32 ssch_rsch_count;
	u32 sample_count;
	u32 device_connect_time;
	u32 function_pending_time;
	u32 device_disconnect_time;
	u32 control_unit_queuing_time;
	u32 device_active_only_time;
	u32 device_busy_time;
	u32 initial_command_response_time;
	u32 reserved[7];
};

/* kmalloc only guarantees 8 byte alignment, but we need cmbe
 * pointers to be naturally aligned. Make sure to allocate
 * enough space for two cmbes */
static inline struct cmbe* cmbe_align(struct cmbe *c)
{
	unsigned long addr;
	addr = ((unsigned long)c + sizeof (struct cmbe) - sizeof(long)) &
				 ~(sizeof (struct cmbe) - sizeof(long));
	return (struct cmbe*)addr;
}

static int alloc_cmbe (struct ccw_device *cdev)
{
	struct cmbe *cmbe;
	struct cmb_data *cmb_data;
	int ret;

	cmbe = kzalloc (sizeof (*cmbe) * 2, GFP_KERNEL);
	if (!cmbe)
		return -ENOMEM;
	cmb_data = kzalloc(sizeof(struct cmb_data), GFP_KERNEL);
	if (!cmb_data) {
		ret = -ENOMEM;
		goto out_free;
	}
	cmb_data->last_block = kzalloc(sizeof(struct cmbe), GFP_KERNEL);
	if (!cmb_data->last_block) {
		ret = -ENOMEM;
		goto out_free;
	}
	cmb_data->size = sizeof(struct cmbe);
	spin_lock_irq(cdev->ccwlock);
	if (cdev->private->cmb) {
		spin_unlock_irq(cdev->ccwlock);
		ret = -EBUSY;
		goto out_free;
	}
	cmb_data->hw_block = cmbe;
	cdev->private->cmb = cmb_data;
	spin_unlock_irq(cdev->ccwlock);

	/* activate global measurement if this is the first channel */
	spin_lock(&cmb_area.lock);
	if (list_empty(&cmb_area.list))
		cmf_activate(NULL, 1);
	list_add_tail(&cdev->private->cmb_list, &cmb_area.list);
	spin_unlock(&cmb_area.lock);

	return 0;
out_free:
	if (cmb_data)
		kfree(cmb_data->last_block);
	kfree(cmb_data);
	kfree(cmbe);
	return ret;
}

static void free_cmbe (struct ccw_device *cdev)
{
	struct cmb_data *cmb_data;

	spin_lock_irq(cdev->ccwlock);
	cmb_data = cdev->private->cmb;
	cdev->private->cmb = NULL;
	if (cmb_data)
		kfree(cmb_data->last_block);
	kfree(cmb_data);
	spin_unlock_irq(cdev->ccwlock);

	/* deactivate global measurement if this is the last channel */
	spin_lock(&cmb_area.lock);
	list_del_init(&cdev->private->cmb_list);
	if (list_empty(&cmb_area.list))
		cmf_activate(NULL, 0);
	spin_unlock(&cmb_area.lock);
}

static int set_cmbe(struct ccw_device *cdev, u32 mme)
{
	unsigned long mba;
	struct cmb_data *cmb_data;
	unsigned long flags;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return -EINVAL;
	}
	cmb_data = cdev->private->cmb;
	mba = mme ? (unsigned long) cmbe_align(cmb_data->hw_block) : 0;
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	return set_schib_wait(cdev, mme, 1, mba);
}


static u64 read_cmbe (struct ccw_device *cdev, int index)
{
	struct cmbe *cmb;
	struct cmb_data *cmb_data;
	u32 val;
	int ret;
	unsigned long flags;

	ret = cmf_cmb_copy_wait(cdev);
	if (ret < 0)
		return 0;

	spin_lock_irqsave(cdev->ccwlock, flags);
	cmb_data = cdev->private->cmb;
	if (!cmb_data) {
		ret = 0;
		goto out;
	}
	cmb = cmb_data->last_block;

	switch (index) {
	case cmb_ssch_rsch_count:
		ret = cmb->ssch_rsch_count;
		goto out;
	case cmb_sample_count:
		ret = cmb->sample_count;
		goto out;
	case cmb_device_connect_time:
		val = cmb->device_connect_time;
		break;
	case cmb_function_pending_time:
		val = cmb->function_pending_time;
		break;
	case cmb_device_disconnect_time:
		val = cmb->device_disconnect_time;
		break;
	case cmb_control_unit_queuing_time:
		val = cmb->control_unit_queuing_time;
		break;
	case cmb_device_active_only_time:
		val = cmb->device_active_only_time;
		break;
	case cmb_device_busy_time:
		val = cmb->device_busy_time;
		break;
	case cmb_initial_command_response_time:
		val = cmb->initial_command_response_time;
		break;
	default:
		ret = 0;
		goto out;
	}
	ret = time_to_avg_nsec(val, cmb->sample_count);
out:
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	return ret;
}

static int readall_cmbe (struct ccw_device *cdev, struct cmbdata *data)
{
	struct cmbe *cmb;
	struct cmb_data *cmb_data;
	u64 time;
	unsigned long flags;
	int ret;

	ret = cmf_cmb_copy_wait(cdev);
	if (ret < 0)
		return ret;
	spin_lock_irqsave(cdev->ccwlock, flags);
	cmb_data = cdev->private->cmb;
	if (!cmb_data) {
		ret = -ENODEV;
		goto out;
	}
	if (cmb_data->last_update == 0) {
		ret = -EAGAIN;
		goto out;
	}
	time = cmb_data->last_update - cdev->private->cmb_start_time;

	memset (data, 0, sizeof(struct cmbdata));

	/* we only know values before device_busy_time */
	data->size = offsetof(struct cmbdata, device_busy_time);

	/* conver to nanoseconds */
	data->elapsed_time = (time * 1000) >> 12;

	cmb = cmb_data->last_block;
	/* copy data to new structure */
	data->ssch_rsch_count = cmb->ssch_rsch_count;
	data->sample_count = cmb->sample_count;

	/* time fields are converted to nanoseconds while copying */
	data->device_connect_time = time_to_nsec(cmb->device_connect_time);
	data->function_pending_time = time_to_nsec(cmb->function_pending_time);
	data->device_disconnect_time =
		time_to_nsec(cmb->device_disconnect_time);
	data->control_unit_queuing_time
		= time_to_nsec(cmb->control_unit_queuing_time);
	data->device_active_only_time
		= time_to_nsec(cmb->device_active_only_time);
	data->device_busy_time = time_to_nsec(cmb->device_busy_time);
	data->initial_command_response_time
		= time_to_nsec(cmb->initial_command_response_time);

	ret = 0;
out:
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	return ret;
}

static void reset_cmbe(struct ccw_device *cdev)
{
	cmf_generic_reset(cdev);
}

static void * align_cmbe(void *area)
{
	return cmbe_align(area);
}

static struct attribute_group cmf_attr_group_ext;

static struct cmb_operations cmbops_extended = {
	.alloc	    = alloc_cmbe,
	.free	    = free_cmbe,
	.set	    = set_cmbe,
	.read	    = read_cmbe,
	.readall    = readall_cmbe,
	.reset	    = reset_cmbe,
	.align	    = align_cmbe,
	.attr_group = &cmf_attr_group_ext,
};


static ssize_t
cmb_show_attr(struct device *dev, char *buf, enum cmb_index idx)
{
	return sprintf(buf, "%lld\n",
		(unsigned long long) cmf_read(to_ccwdev(dev), idx));
}

static ssize_t
cmb_show_avg_sample_interval(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev;
	long interval;
	unsigned long count;
	struct cmb_data *cmb_data;

	cdev = to_ccwdev(dev);
	count = cmf_read(cdev, cmb_sample_count);
	spin_lock_irq(cdev->ccwlock);
	cmb_data = cdev->private->cmb;
	if (count) {
		interval = cmb_data->last_update -
			cdev->private->cmb_start_time;
		interval = (interval * 1000) >> 12;
		interval /= count;
	} else
		interval = -1;
	spin_unlock_irq(cdev->ccwlock);
	return sprintf(buf, "%ld\n", interval);
}

static ssize_t
cmb_show_avg_utilization(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cmbdata data;
	u64 utilization;
	unsigned long t, u;
	int ret;

	ret = cmf_readall(to_ccwdev(dev), &data);
	if (ret == -EAGAIN || ret == -ENODEV)
		/* No data (yet/currently) available to use for calculation. */
		return sprintf(buf, "n/a\n");
	else if (ret)
		return ret;

	utilization = data.device_connect_time +
		      data.function_pending_time +
		      data.device_disconnect_time;

	/* shift to avoid long long division */
	while (-1ul < (data.elapsed_time | utilization)) {
		utilization >>= 8;
		data.elapsed_time >>= 8;
	}

	/* calculate value in 0.1 percent units */
	t = (unsigned long) data.elapsed_time / 1000;
	u = (unsigned long) utilization / t;

	return sprintf(buf, "%02ld.%01ld%%\n", u/ 10, u - (u/ 10) * 10);
}

#define cmf_attr(name) \
static ssize_t show_ ## name (struct device * dev, struct device_attribute *attr, char * buf) \
{ return cmb_show_attr((dev), buf, cmb_ ## name); } \
static DEVICE_ATTR(name, 0444, show_ ## name, NULL);

#define cmf_attr_avg(name) \
static ssize_t show_avg_ ## name (struct device * dev, struct device_attribute *attr, char * buf) \
{ return cmb_show_attr((dev), buf, cmb_ ## name); } \
static DEVICE_ATTR(avg_ ## name, 0444, show_avg_ ## name, NULL);

cmf_attr(ssch_rsch_count);
cmf_attr(sample_count);
cmf_attr_avg(device_connect_time);
cmf_attr_avg(function_pending_time);
cmf_attr_avg(device_disconnect_time);
cmf_attr_avg(control_unit_queuing_time);
cmf_attr_avg(device_active_only_time);
cmf_attr_avg(device_busy_time);
cmf_attr_avg(initial_command_response_time);

static DEVICE_ATTR(avg_sample_interval, 0444, cmb_show_avg_sample_interval, NULL);
static DEVICE_ATTR(avg_utilization, 0444, cmb_show_avg_utilization, NULL);

static struct attribute *cmf_attributes[] = {
	&dev_attr_avg_sample_interval.attr,
	&dev_attr_avg_utilization.attr,
	&dev_attr_ssch_rsch_count.attr,
	&dev_attr_sample_count.attr,
	&dev_attr_avg_device_connect_time.attr,
	&dev_attr_avg_function_pending_time.attr,
	&dev_attr_avg_device_disconnect_time.attr,
	&dev_attr_avg_control_unit_queuing_time.attr,
	&dev_attr_avg_device_active_only_time.attr,
	NULL,
};

static struct attribute_group cmf_attr_group = {
	.name  = "cmf",
	.attrs = cmf_attributes,
};

static struct attribute *cmf_attributes_ext[] = {
	&dev_attr_avg_sample_interval.attr,
	&dev_attr_avg_utilization.attr,
	&dev_attr_ssch_rsch_count.attr,
	&dev_attr_sample_count.attr,
	&dev_attr_avg_device_connect_time.attr,
	&dev_attr_avg_function_pending_time.attr,
	&dev_attr_avg_device_disconnect_time.attr,
	&dev_attr_avg_control_unit_queuing_time.attr,
	&dev_attr_avg_device_active_only_time.attr,
	&dev_attr_avg_device_busy_time.attr,
	&dev_attr_avg_initial_command_response_time.attr,
	NULL,
};

static struct attribute_group cmf_attr_group_ext = {
	.name  = "cmf",
	.attrs = cmf_attributes_ext,
};

static ssize_t cmb_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_ccwdev(dev)->private->cmb ? 1 : 0);
}

static ssize_t cmb_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t c)
{
	struct ccw_device *cdev;
	int ret;

	cdev = to_ccwdev(dev);

	switch (buf[0]) {
	case '0':
		ret = disable_cmf(cdev);
		if (ret)
			printk(KERN_INFO "disable_cmf failed (%d)\n", ret);
		break;
	case '1':
		ret = enable_cmf(cdev);
		if (ret && ret != -EBUSY)
			printk(KERN_INFO "enable_cmf failed (%d)\n", ret);
		break;
	}

	return c;
}

DEVICE_ATTR(cmb_enable, 0644, cmb_enable_show, cmb_enable_store);

/* enable_cmf/disable_cmf: module interface for cmf (de)activation */
int
enable_cmf(struct ccw_device *cdev)
{
	int ret;

	ret = cmbops->alloc(cdev);
	cmbops->reset(cdev);
	if (ret)
		return ret;
	ret = cmbops->set(cdev, 2);
	if (ret) {
		cmbops->free(cdev);
		return ret;
	}
	ret = sysfs_create_group(&cdev->dev.kobj, cmbops->attr_group);
	if (!ret)
		return 0;
	cmbops->set(cdev, 0);  //FIXME: this can fail
	cmbops->free(cdev);
	return ret;
}

int
disable_cmf(struct ccw_device *cdev)
{
	int ret;

	ret = cmbops->set(cdev, 0);
	if (ret)
		return ret;
	cmbops->free(cdev);
	sysfs_remove_group(&cdev->dev.kobj, cmbops->attr_group);
	return ret;
}

u64
cmf_read(struct ccw_device *cdev, int index)
{
	return cmbops->read(cdev, index);
}

int
cmf_readall(struct ccw_device *cdev, struct cmbdata *data)
{
	return cmbops->readall(cdev, data);
}

/* Reenable cmf when a disconnected device becomes available again. */
int cmf_reenable(struct ccw_device *cdev)
{
	cmbops->reset(cdev);
	return cmbops->set(cdev, 2);
}

static int __init
init_cmf(void)
{
	char *format_string;
	char *detect_string = "parameter";

	/* We cannot really autoprobe this. If the user did not give a parameter,
	   see if we are running on z990 or up, otherwise fall back to basic mode. */

	if (format == CMF_AUTODETECT) {
		if (!css_characteristics_avail ||
		    !css_general_characteristics.ext_mb) {
			format = CMF_BASIC;
		} else {
			format = CMF_EXTENDED;
		}
		detect_string = "autodetected";
	} else {
		detect_string = "parameter";
	}

	switch (format) {
	case CMF_BASIC:
		format_string = "basic";
		cmbops = &cmbops_basic;
		if (cmb_area.num_channels > 4096 || cmb_area.num_channels < 1) {
			printk(KERN_ERR "Basic channel measurement facility"
					" can only use 1 to 4096 devices\n"
			       KERN_ERR "when the cmf driver is built"
					" as a loadable module\n");
			return 1;
		}
		break;
	case CMF_EXTENDED:
 		format_string = "extended";
		cmbops = &cmbops_extended;
		break;
	default:
		printk(KERN_ERR "Invalid format %d for channel "
			"measurement facility\n", format);
		return 1;
	}

	printk(KERN_INFO "Channel measurement facility using %s format (%s)\n",
		format_string, detect_string);
	return 0;
}

module_init(init_cmf);


MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("channel measurement facility base driver\n"
		   "Copyright 2003 IBM Corporation\n");

EXPORT_SYMBOL_GPL(enable_cmf);
EXPORT_SYMBOL_GPL(disable_cmf);
EXPORT_SYMBOL_GPL(cmf_read);
EXPORT_SYMBOL_GPL(cmf_readall);
