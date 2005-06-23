/*
 * linux/drivers/s390/cio/cmf.c ($Revision: 1.16 $)
 *
 * Linux on zSeries Channel Measurement Facility support
 *
 * Copyright 2000,2003 IBM Corporation
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
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

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/cmb.h>

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
 * all these functions operate on a struct cmf_device. There is only
 * one instance of struct cmb_operations because all cmf_device
 * objects are guaranteed to be of the same type.
 *
 * @alloc:	allocate memory for a channel measurement block,
 *		either with the help of a special pool or with kmalloc
 * @free:	free memory allocated with @alloc
 * @set:	enable or disable measurement
 * @readall:	read a measurement block in a common format
 * @reset:	clear the data in the associated measurement block and
 *		reset its time stamp
 */
struct cmb_operations {
	int (*alloc)  (struct ccw_device*);
	void(*free)   (struct ccw_device*);
	int (*set)    (struct ccw_device*, u32);
	u64 (*read)   (struct ccw_device*, int);
	int (*readall)(struct ccw_device*, struct cmbdata *);
	void (*reset) (struct ccw_device*);

	struct attribute_group *attr_group;
};
static struct cmb_operations *cmbops;

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
		stsch(sch->irq, schib);
		schib->pmcw.mme  = mme;
		schib->pmcw.mbfc = mbfc;
		/* address can be either a block address or a block index */
		if (mbfc)
			schib->mba = address;
		else
			schib->pmcw.mbi = address;

		/* try to submit it */
		switch(ret = msch_err(sch->irq, schib)) {
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
		stsch(sch->irq, schib); /* restore the schib */

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
};

static int set_schib_wait(struct ccw_device *cdev, u32 mme,
				int mbfc, unsigned long address)
{
	struct set_schib_struct s = {
		.mme = mme,
		.mbfc = mbfc,
		.address = address,
		.wait = __WAIT_QUEUE_HEAD_INITIALIZER(s.wait),
	};

	spin_lock_irq(cdev->ccwlock);
	s.ret = set_schib(cdev, mme, mbfc, address);
	if (s.ret != -EBUSY) {
		goto out_nowait;
	}

	if (cdev->private->state != DEV_STATE_ONLINE) {
		s.ret = -EBUSY;
		/* if the device is not online, don't even try again */
		goto out_nowait;
	}
	cdev->private->state = DEV_STATE_CMFCHANGE;
	cdev->private->cmb_wait = &s;
	s.ret = 1;

	spin_unlock_irq(cdev->ccwlock);
	if (wait_event_interruptible(s.wait, s.ret != 1)) {
		spin_lock_irq(cdev->ccwlock);
		if (s.ret == 1) {
			s.ret = -ERESTARTSYS;
			cdev->private->cmb_wait = 0;
			if (cdev->private->state == DEV_STATE_CMFCHANGE)
				cdev->private->state = DEV_STATE_ONLINE;
		}
		spin_unlock_irq(cdev->ccwlock);
	}
	return s.ret;

out_nowait:
	spin_unlock_irq(cdev->ccwlock);
	return s.ret;
}

void retry_set_schib(struct ccw_device *cdev)
{
	struct set_schib_struct *s;

	s = cdev->private->cmb_wait;
	cdev->private->cmb_wait = 0;
	if (!s) {
		WARN_ON(1);
		return;
	}
	s->ret = set_schib(cdev, s->mme, s->mbfc, s->address);
	wake_up(&s->wait);
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
static inline int
alloc_cmb_single (struct ccw_device *cdev)
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
	 * remains sorted by ->cmb pointers */
	cmb = cmb_area.mem;
	list_for_each_entry(node, &cmb_area.list, cmb_list) {
		if ((struct cmb*)node->cmb > cmb)
			break;
		cmb++;
	}
	if (cmb - cmb_area.mem >= cmb_area.num_channels) {
		ret = -ENOMEM;
		goto out;
	}

	/* insert new cmb */
	list_add_tail(&cdev->private->cmb_list, &node->cmb_list);
	cdev->private->cmb = cmb;
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
	ret = alloc_cmb_single(cdev);
out:
	spin_unlock(&cmb_area.lock);

	return ret;
}

static void
free_cmb(struct ccw_device *cdev)
{
	struct ccw_device_private *priv;

	priv = cdev->private;

	spin_lock(&cmb_area.lock);
	spin_lock_irq(cdev->ccwlock);

	if (list_empty(&priv->cmb_list)) {
		/* already freed */
		goto out;
	}

	priv->cmb = NULL;
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

static int
set_cmb(struct ccw_device *cdev, u32 mme)
{
	u16 offset;

	if (!cdev->private->cmb)
		return -EINVAL;

	offset = mme ? (struct cmb *)cdev->private->cmb - cmb_area.mem : 0;

	return set_schib_wait(cdev, mme, 0, offset);
}

static u64
read_cmb (struct ccw_device *cdev, int index)
{
	/* yes, we have to put it on the stack
	 * because the cmb must only be accessed
	 * atomically, e.g. with mvc */
	struct cmb cmb;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return 0;
	}

	cmb = *(struct cmb*)cdev->private->cmb;
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	switch (index) {
	case cmb_ssch_rsch_count:
		return cmb.ssch_rsch_count;
	case cmb_sample_count:
		return cmb.sample_count;
	case cmb_device_connect_time:
		val = cmb.device_connect_time;
		break;
	case cmb_function_pending_time:
		val = cmb.function_pending_time;
		break;
	case cmb_device_disconnect_time:
		val = cmb.device_disconnect_time;
		break;
	case cmb_control_unit_queuing_time:
		val = cmb.control_unit_queuing_time;
		break;
	case cmb_device_active_only_time:
		val = cmb.device_active_only_time;
		break;
	default:
		return 0;
	}
	return time_to_avg_nsec(val, cmb.sample_count);
}

static int
readall_cmb (struct ccw_device *cdev, struct cmbdata *data)
{
	/* yes, we have to put it on the stack
	 * because the cmb must only be accessed
	 * atomically, e.g. with mvc */
	struct cmb cmb;
	unsigned long flags;
	u64 time;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return -ENODEV;
	}

	cmb = *(struct cmb*)cdev->private->cmb;
	time = get_clock() - cdev->private->cmb_start_time;
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	memset(data, 0, sizeof(struct cmbdata));

	/* we only know values before device_busy_time */
	data->size = offsetof(struct cmbdata, device_busy_time);

	/* convert to nanoseconds */
	data->elapsed_time = (time * 1000) >> 12;

	/* copy data to new structure */
	data->ssch_rsch_count = cmb.ssch_rsch_count;
	data->sample_count = cmb.sample_count;

	/* time fields are converted to nanoseconds while copying */
	data->device_connect_time = time_to_nsec(cmb.device_connect_time);
	data->function_pending_time = time_to_nsec(cmb.function_pending_time);
	data->device_disconnect_time = time_to_nsec(cmb.device_disconnect_time);
	data->control_unit_queuing_time
		= time_to_nsec(cmb.control_unit_queuing_time);
	data->device_active_only_time
		= time_to_nsec(cmb.device_active_only_time);

	return 0;
}

static void
reset_cmb(struct ccw_device *cdev)
{
	struct cmb *cmb;
	spin_lock_irq(cdev->ccwlock);
	cmb = cdev->private->cmb;
	if (cmb)
		memset (cmb, 0, sizeof (*cmb));
	cdev->private->cmb_start_time = get_clock();
	spin_unlock_irq(cdev->ccwlock);
}

static struct attribute_group cmf_attr_group;

static struct cmb_operations cmbops_basic = {
	.alloc	= alloc_cmb,
	.free	= free_cmb,
	.set	= set_cmb,
	.read	= read_cmb,
	.readall    = readall_cmb,
	.reset	    = reset_cmb,
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

static int
alloc_cmbe (struct ccw_device *cdev)
{
	struct cmbe *cmbe;
	cmbe = kmalloc (sizeof (*cmbe) * 2, GFP_KERNEL);
	if (!cmbe)
		return -ENOMEM;

	spin_lock_irq(cdev->ccwlock);
	if (cdev->private->cmb) {
		kfree(cmbe);
		spin_unlock_irq(cdev->ccwlock);
		return -EBUSY;
	}

	cdev->private->cmb = cmbe;
	spin_unlock_irq(cdev->ccwlock);

	/* activate global measurement if this is the first channel */
	spin_lock(&cmb_area.lock);
	if (list_empty(&cmb_area.list))
		cmf_activate(NULL, 1);
	list_add_tail(&cdev->private->cmb_list, &cmb_area.list);
	spin_unlock(&cmb_area.lock);

	return 0;
}

static void
free_cmbe (struct ccw_device *cdev)
{
	spin_lock_irq(cdev->ccwlock);
	if (cdev->private->cmb)
		kfree(cdev->private->cmb);
	cdev->private->cmb = NULL;
	spin_unlock_irq(cdev->ccwlock);

	/* deactivate global measurement if this is the last channel */
	spin_lock(&cmb_area.lock);
	list_del_init(&cdev->private->cmb_list);
	if (list_empty(&cmb_area.list))
		cmf_activate(NULL, 0);
	spin_unlock(&cmb_area.lock);
}

static int
set_cmbe(struct ccw_device *cdev, u32 mme)
{
	unsigned long mba;

	if (!cdev->private->cmb)
		return -EINVAL;
	mba = mme ? (unsigned long) cmbe_align(cdev->private->cmb) : 0;

	return set_schib_wait(cdev, mme, 1, mba);
}


u64
read_cmbe (struct ccw_device *cdev, int index)
{
	/* yes, we have to put it on the stack
	 * because the cmb must only be accessed
	 * atomically, e.g. with mvc */
	struct cmbe cmb;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return 0;
	}

	cmb = *cmbe_align(cdev->private->cmb);
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	switch (index) {
	case cmb_ssch_rsch_count:
		return cmb.ssch_rsch_count;
	case cmb_sample_count:
		return cmb.sample_count;
	case cmb_device_connect_time:
		val = cmb.device_connect_time;
		break;
	case cmb_function_pending_time:
		val = cmb.function_pending_time;
		break;
	case cmb_device_disconnect_time:
		val = cmb.device_disconnect_time;
		break;
	case cmb_control_unit_queuing_time:
		val = cmb.control_unit_queuing_time;
		break;
	case cmb_device_active_only_time:
		val = cmb.device_active_only_time;
		break;
	case cmb_device_busy_time:
		val = cmb.device_busy_time;
		break;
	case cmb_initial_command_response_time:
		val = cmb.initial_command_response_time;
		break;
	default:
		return 0;
	}
	return time_to_avg_nsec(val, cmb.sample_count);
}

static int
readall_cmbe (struct ccw_device *cdev, struct cmbdata *data)
{
	/* yes, we have to put it on the stack
	 * because the cmb must only be accessed
	 * atomically, e.g. with mvc */
	struct cmbe cmb;
	unsigned long flags;
	u64 time;

	spin_lock_irqsave(cdev->ccwlock, flags);
	if (!cdev->private->cmb) {
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		return -ENODEV;
	}

	cmb = *cmbe_align(cdev->private->cmb);
	time = get_clock() - cdev->private->cmb_start_time;
	spin_unlock_irqrestore(cdev->ccwlock, flags);

	memset (data, 0, sizeof(struct cmbdata));

	/* we only know values before device_busy_time */
	data->size = offsetof(struct cmbdata, device_busy_time);

	/* conver to nanoseconds */
	data->elapsed_time = (time * 1000) >> 12;

	/* copy data to new structure */
	data->ssch_rsch_count = cmb.ssch_rsch_count;
	data->sample_count = cmb.sample_count;

	/* time fields are converted to nanoseconds while copying */
	data->device_connect_time = time_to_nsec(cmb.device_connect_time);
	data->function_pending_time = time_to_nsec(cmb.function_pending_time);
	data->device_disconnect_time = time_to_nsec(cmb.device_disconnect_time);
	data->control_unit_queuing_time
		= time_to_nsec(cmb.control_unit_queuing_time);
	data->device_active_only_time
		= time_to_nsec(cmb.device_active_only_time);
	data->device_busy_time = time_to_nsec(cmb.device_busy_time);
	data->initial_command_response_time
		= time_to_nsec(cmb.initial_command_response_time);

	return 0;
}

static void
reset_cmbe(struct ccw_device *cdev)
{
	struct cmbe *cmb;
	spin_lock_irq(cdev->ccwlock);
	cmb = cmbe_align(cdev->private->cmb);
	if (cmb)
		memset (cmb, 0, sizeof (*cmb));
	cdev->private->cmb_start_time = get_clock();
	spin_unlock_irq(cdev->ccwlock);
}

static struct attribute_group cmf_attr_group_ext;

static struct cmb_operations cmbops_extended = {
	.alloc	    = alloc_cmbe,
	.free	    = free_cmbe,
	.set	    = set_cmbe,
	.read	    = read_cmbe,
	.readall    = readall_cmbe,
	.reset	    = reset_cmbe,
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

	cdev = to_ccwdev(dev);
	interval  = get_clock() - cdev->private->cmb_start_time;
	count = cmf_read(cdev, cmb_sample_count);
	if (count)
		interval /= count;
	else
		interval = -1;
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
	if (ret)
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
	0,
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
	0,
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
