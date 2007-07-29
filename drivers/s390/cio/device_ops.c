/*
 *  drivers/s390/cio/device_ops.c
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *               Cornelia Huck (cornelia.huck@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <asm/ccwdev.h>
#include <asm/idals.h>
#include <asm/chpid.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "chsc.h"
#include "device.h"
#include "chp.h"

int ccw_device_set_options_mask(struct ccw_device *cdev, unsigned long flags)
{
       /*
	* The flag usage is mutal exclusive ...
	*/
	if ((flags & CCWDEV_EARLY_NOTIFICATION) &&
	    (flags & CCWDEV_REPORT_ALL))
		return -EINVAL;
	cdev->private->options.fast = (flags & CCWDEV_EARLY_NOTIFICATION) != 0;
	cdev->private->options.repall = (flags & CCWDEV_REPORT_ALL) != 0;
	cdev->private->options.pgroup = (flags & CCWDEV_DO_PATHGROUP) != 0;
	cdev->private->options.force = (flags & CCWDEV_ALLOW_FORCE) != 0;
	return 0;
}

int ccw_device_set_options(struct ccw_device *cdev, unsigned long flags)
{
       /*
	* The flag usage is mutal exclusive ...
	*/
	if (((flags & CCWDEV_EARLY_NOTIFICATION) &&
	    (flags & CCWDEV_REPORT_ALL)) ||
	    ((flags & CCWDEV_EARLY_NOTIFICATION) &&
	     cdev->private->options.repall) ||
	    ((flags & CCWDEV_REPORT_ALL) &&
	     cdev->private->options.fast))
		return -EINVAL;
	cdev->private->options.fast |= (flags & CCWDEV_EARLY_NOTIFICATION) != 0;
	cdev->private->options.repall |= (flags & CCWDEV_REPORT_ALL) != 0;
	cdev->private->options.pgroup |= (flags & CCWDEV_DO_PATHGROUP) != 0;
	cdev->private->options.force |= (flags & CCWDEV_ALLOW_FORCE) != 0;
	return 0;
}

void ccw_device_clear_options(struct ccw_device *cdev, unsigned long flags)
{
	cdev->private->options.fast &= (flags & CCWDEV_EARLY_NOTIFICATION) == 0;
	cdev->private->options.repall &= (flags & CCWDEV_REPORT_ALL) == 0;
	cdev->private->options.pgroup &= (flags & CCWDEV_DO_PATHGROUP) == 0;
	cdev->private->options.force &= (flags & CCWDEV_ALLOW_FORCE) == 0;
}

int
ccw_device_clear(struct ccw_device *cdev, unsigned long intparm)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state == DEV_STATE_NOT_OPER)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE &&
	    cdev->private->state != DEV_STATE_W4SENSE)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	ret = cio_clear(sch);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}

int
ccw_device_start_key(struct ccw_device *cdev, struct ccw1 *cpa,
		     unsigned long intparm, __u8 lpm, __u8 key,
		     unsigned long flags)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	if (cdev->private->state == DEV_STATE_NOT_OPER)
		return -ENODEV;
	if (cdev->private->state == DEV_STATE_VERIFY ||
	    cdev->private->state == DEV_STATE_CLEAR_VERIFY) {
		/* Remember to fake irb when finished. */
		if (!cdev->private->flags.fake_irb) {
			cdev->private->flags.fake_irb = 1;
			cdev->private->intparm = intparm;
			return 0;
		} else
			/* There's already a fake I/O around. */
			return -EBUSY;
	}
	if (cdev->private->state != DEV_STATE_ONLINE ||
	    ((sch->schib.scsw.stctl & SCSW_STCTL_PRIM_STATUS) &&
	     !(sch->schib.scsw.stctl & SCSW_STCTL_SEC_STATUS)) ||
	    cdev->private->flags.doverify)
		return -EBUSY;
	ret = cio_set_options (sch, flags);
	if (ret)
		return ret;
	/* Adjust requested path mask to excluded varied off paths. */
	if (lpm) {
		lpm &= sch->opm;
		if (lpm == 0)
			return -EACCES;
	}
	ret = cio_start_key (sch, cpa, lpm, key);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}


int
ccw_device_start_timeout_key(struct ccw_device *cdev, struct ccw1 *cpa,
			     unsigned long intparm, __u8 lpm, __u8 key,
			     unsigned long flags, int expires)
{
	int ret;

	if (!cdev)
		return -ENODEV;
	ccw_device_set_timeout(cdev, expires);
	ret = ccw_device_start_key(cdev, cpa, intparm, lpm, key, flags);
	if (ret != 0)
		ccw_device_set_timeout(cdev, 0);
	return ret;
}

int
ccw_device_start(struct ccw_device *cdev, struct ccw1 *cpa,
		 unsigned long intparm, __u8 lpm, unsigned long flags)
{
	return ccw_device_start_key(cdev, cpa, intparm, lpm,
				    PAGE_DEFAULT_KEY, flags);
}

int
ccw_device_start_timeout(struct ccw_device *cdev, struct ccw1 *cpa,
			 unsigned long intparm, __u8 lpm, unsigned long flags,
			 int expires)
{
	return ccw_device_start_timeout_key(cdev, cpa, intparm, lpm,
					    PAGE_DEFAULT_KEY, flags,
					    expires);
}


int
ccw_device_halt(struct ccw_device *cdev, unsigned long intparm)
{
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->private->state == DEV_STATE_NOT_OPER)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE &&
	    cdev->private->state != DEV_STATE_W4SENSE)
		return -EINVAL;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	ret = cio_halt(sch);
	if (ret == 0)
		cdev->private->intparm = intparm;
	return ret;
}

int
ccw_device_resume(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (!cdev)
		return -ENODEV;
	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return -ENODEV;
	if (cdev->private->state == DEV_STATE_NOT_OPER)
		return -ENODEV;
	if (cdev->private->state != DEV_STATE_ONLINE ||
	    !(sch->schib.scsw.actl & SCSW_ACTL_SUSPENDED))
		return -EINVAL;
	return cio_resume(sch);
}

/*
 * Pass interrupt to device driver.
 */
int
ccw_device_call_handler(struct ccw_device *cdev)
{
	struct subchannel *sch;
	unsigned int stctl;
	int ending_status;

	sch = to_subchannel(cdev->dev.parent);

	/*
	 * we allow for the device action handler if .
	 *  - we received ending status
	 *  - the action handler requested to see all interrupts
	 *  - we received an intermediate status
	 *  - fast notification was requested (primary status)
	 *  - unsolicited interrupts
	 */
	stctl = cdev->private->irb.scsw.stctl;
	ending_status = (stctl & SCSW_STCTL_SEC_STATUS) ||
		(stctl == (SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND)) ||
		(stctl == SCSW_STCTL_STATUS_PEND);
	if (!ending_status &&
	    !cdev->private->options.repall &&
	    !(stctl & SCSW_STCTL_INTER_STATUS) &&
	    !(cdev->private->options.fast &&
	      (stctl & SCSW_STCTL_PRIM_STATUS)))
		return 0;

	/* Clear pending timers for device driver initiated I/O. */
	if (ending_status)
		ccw_device_set_timeout(cdev, 0);
	/*
	 * Now we are ready to call the device driver interrupt handler.
	 */
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      &cdev->private->irb);

	/*
	 * Clear the old and now useless interrupt response block.
	 */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	return 1;
}

/*
 * Search for CIW command in extended sense data.
 */
struct ciw *
ccw_device_get_ciw(struct ccw_device *cdev, __u32 ct)
{
	int ciw_cnt;

	if (cdev->private->flags.esid == 0)
		return NULL;
	for (ciw_cnt = 0; ciw_cnt < MAX_CIWS; ciw_cnt++)
		if (cdev->private->senseid.ciw[ciw_cnt].ct == ct)
			return cdev->private->senseid.ciw + ciw_cnt;
	return NULL;
}

__u8
ccw_device_get_path_mask(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (!sch)
		return 0;
	else
		return sch->lpm;
}

/*
 * Try to break the lock on a boxed device.
 */
int
ccw_device_stlck(struct ccw_device *cdev)
{
	void *buf, *buf2;
	unsigned long flags;
	struct subchannel *sch;
	int ret;

	if (!cdev)
		return -ENODEV;

	if (cdev->drv && !cdev->private->options.force)
		return -EINVAL;

	sch = to_subchannel(cdev->dev.parent);
	
	CIO_TRACE_EVENT(2, "stl lock");
	CIO_TRACE_EVENT(2, cdev->dev.bus_id);

	buf = kmalloc(32*sizeof(char), GFP_DMA|GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	buf2 = kmalloc(32*sizeof(char), GFP_DMA|GFP_KERNEL);
	if (!buf2) {
		kfree(buf);
		return -ENOMEM;
	}
	spin_lock_irqsave(sch->lock, flags);
	ret = cio_enable_subchannel(sch, 3);
	if (ret)
		goto out_unlock;
	/*
	 * Setup ccw. We chain an unconditional reserve and a release so we
	 * only break the lock.
	 */
	cdev->private->iccws[0].cmd_code = CCW_CMD_STLCK;
	cdev->private->iccws[0].cda = (__u32) __pa(buf);
	cdev->private->iccws[0].count = 32;
	cdev->private->iccws[0].flags = CCW_FLAG_CC;
	cdev->private->iccws[1].cmd_code = CCW_CMD_RELEASE;
	cdev->private->iccws[1].cda = (__u32) __pa(buf2);
	cdev->private->iccws[1].count = 32;
	cdev->private->iccws[1].flags = 0;
	ret = cio_start(sch, cdev->private->iccws, 0);
	if (ret) {
		cio_disable_subchannel(sch); //FIXME: return code?
		goto out_unlock;
	}
	cdev->private->irb.scsw.actl |= SCSW_ACTL_START_PEND;
	spin_unlock_irqrestore(sch->lock, flags);
	wait_event(cdev->private->wait_q, cdev->private->irb.scsw.actl == 0);
	spin_lock_irqsave(sch->lock, flags);
	cio_disable_subchannel(sch); //FIXME: return code?
	if ((cdev->private->irb.scsw.dstat !=
	     (DEV_STAT_CHN_END|DEV_STAT_DEV_END)) ||
	    (cdev->private->irb.scsw.cstat != 0))
		ret = -EIO;
	/* Clear irb. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));
out_unlock:
	kfree(buf);
	kfree(buf2);
	spin_unlock_irqrestore(sch->lock, flags);
	return ret;
}

void *
ccw_device_get_chp_desc(struct ccw_device *cdev, int chp_no)
{
	struct subchannel *sch;
	struct chp_id chpid;

	sch = to_subchannel(cdev->dev.parent);
	chp_id_init(&chpid);
	chpid.id = sch->schib.pmcw.chpid[chp_no];
	return chp_get_chp_desc(chpid);
}

/**
 * ccw_device_get_id - obtain a ccw device id
 * @cdev: device to obtain the id for
 * @dev_id: where to fill in the values
 */
void ccw_device_get_id(struct ccw_device *cdev, struct ccw_dev_id *dev_id)
{
	*dev_id = cdev->private->dev_id;
}
EXPORT_SYMBOL(ccw_device_get_id);

// FIXME: these have to go:

int
_ccw_device_get_subchannel_number(struct ccw_device *cdev)
{
	return cdev->private->schid.sch_no;
}


MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccw_device_set_options_mask);
EXPORT_SYMBOL(ccw_device_set_options);
EXPORT_SYMBOL(ccw_device_clear_options);
EXPORT_SYMBOL(ccw_device_clear);
EXPORT_SYMBOL(ccw_device_halt);
EXPORT_SYMBOL(ccw_device_resume);
EXPORT_SYMBOL(ccw_device_start_timeout);
EXPORT_SYMBOL(ccw_device_start);
EXPORT_SYMBOL(ccw_device_start_timeout_key);
EXPORT_SYMBOL(ccw_device_start_key);
EXPORT_SYMBOL(ccw_device_get_ciw);
EXPORT_SYMBOL(ccw_device_get_path_mask);
EXPORT_SYMBOL(_ccw_device_get_subchannel_number);
EXPORT_SYMBOL_GPL(ccw_device_get_chp_desc);
