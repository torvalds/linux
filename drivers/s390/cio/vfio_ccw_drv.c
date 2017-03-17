/*
 * VFIO based Physical Subchannel device driver
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/mdev.h>

#include <asm/isc.h>

#include "ioasm.h"
#include "css.h"
#include "vfio_ccw_private.h"

/*
 * Helpers
 */
int vfio_ccw_sch_quiesce(struct subchannel *sch)
{
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);
	DECLARE_COMPLETION_ONSTACK(completion);
	int iretry, ret = 0;

	spin_lock_irq(sch->lock);
	if (!sch->schib.pmcw.ena)
		goto out_unlock;
	ret = cio_disable_subchannel(sch);
	if (ret != -EBUSY)
		goto out_unlock;

	do {
		iretry = 255;

		ret = cio_cancel_halt_clear(sch, &iretry);
		while (ret == -EBUSY) {
			/*
			 * Flush all I/O and wait for
			 * cancel/halt/clear completion.
			 */
			private->completion = &completion;
			spin_unlock_irq(sch->lock);

			wait_for_completion_timeout(&completion, 3*HZ);

			spin_lock_irq(sch->lock);
			private->completion = NULL;
			ret = cio_cancel_halt_clear(sch, &iretry);
		};

		ret = cio_disable_subchannel(sch);
	} while (ret == -EBUSY);

out_unlock:
	spin_unlock_irq(sch->lock);
	return ret;
}

static int doing_io(struct vfio_ccw_private *private, u32 intparm)
{
	return (private->intparm == intparm);
}

static int vfio_ccw_sch_io_helper(struct vfio_ccw_private *private)
{
	struct subchannel *sch;
	union orb *orb;
	int ccode;
	__u8 lpm;
	u32 intparm;

	sch = private->sch;

	orb = cp_get_orb(&private->cp, (u32)(addr_t)sch, sch->lpm);

	/* Issue "Start Subchannel" */
	ccode = ssch(sch->schid, orb);

	switch (ccode) {
	case 0:
		/*
		 * Initialize device status information
		 */
		sch->schib.scsw.cmd.actl |= SCSW_ACTL_START_PEND;
		break;
	case 1:		/* Status pending */
	case 2:		/* Busy */
		return -EBUSY;
	case 3:		/* Device/path not operational */
	{
		lpm = orb->cmd.lpm;
		if (lpm != 0)
			sch->lpm &= ~lpm;
		else
			sch->lpm = 0;

		if (cio_update_schib(sch))
			return -ENODEV;

		return sch->lpm ? -EACCES : -ENODEV;
	}
	default:
		return ccode;
	}

	intparm = (u32)(addr_t)sch;
	private->intparm = 0;
	wait_event(private->wait_q, doing_io(private, intparm));

	if (scsw_is_solicited(&private->irb.scsw))
		cp_update_scsw(&private->cp, &private->irb.scsw);

	return 0;
}

/* Deal with the ccw command request from the userspace. */
int vfio_ccw_sch_cmd_request(struct vfio_ccw_private *private)
{
	struct mdev_device *mdev = private->mdev;
	union orb *orb;
	union scsw *scsw = &private->scsw;
	struct irb *irb = &private->irb;
	struct ccw_io_region *io_region = &private->io_region;
	int ret;

	memcpy(scsw, io_region->scsw_area, sizeof(*scsw));

	if (scsw->cmd.fctl & SCSW_FCTL_START_FUNC) {
		orb = (union orb *)io_region->orb_area;

		ret = cp_init(&private->cp, mdev_dev(mdev), orb);
		if (ret)
			return ret;

		ret = cp_prefetch(&private->cp);
		if (ret) {
			cp_free(&private->cp);
			return ret;
		}

		/* Start channel program and wait for I/O interrupt. */
		ret = vfio_ccw_sch_io_helper(private);
		if (!ret) {
			/* Get irb info and copy it to irb_area. */
			memcpy(io_region->irb_area, irb, sizeof(*irb));
		}

		cp_free(&private->cp);
	} else if (scsw->cmd.fctl & SCSW_FCTL_HALT_FUNC) {
		/* XXX: Handle halt. */
		ret = -EOPNOTSUPP;
	} else if (scsw->cmd.fctl & SCSW_FCTL_CLEAR_FUNC) {
		/* XXX: Handle clear. */
		ret = -EOPNOTSUPP;
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/*
 * Sysfs interfaces
 */
static ssize_t chpids_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct chsc_ssd_info *ssd = &sch->ssd_info;
	ssize_t ret = 0;
	int chp;
	int mask;

	for (chp = 0; chp < 8; chp++) {
		mask = 0x80 >> chp;
		if (ssd->path_mask & mask)
			ret += sprintf(buf + ret, "%02x ", ssd->chpid[chp].id);
		else
			ret += sprintf(buf + ret, "00 ");
	}
	ret += sprintf(buf+ret, "\n");
	return ret;
}

static ssize_t pimpampom_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct pmcw *pmcw = &sch->schib.pmcw;

	return sprintf(buf, "%02x %02x %02x\n",
		       pmcw->pim, pmcw->pam, pmcw->pom);
}

static DEVICE_ATTR(chpids, 0444, chpids_show, NULL);
static DEVICE_ATTR(pimpampom, 0444, pimpampom_show, NULL);

static struct attribute *vfio_subchannel_attrs[] = {
	&dev_attr_chpids.attr,
	&dev_attr_pimpampom.attr,
	NULL,
};

static struct attribute_group vfio_subchannel_attr_group = {
	.attrs = vfio_subchannel_attrs,
};

/*
 * Css driver callbacks
 */
static void vfio_ccw_sch_irq(struct subchannel *sch)
{
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);
	struct irb *irb;

	inc_irq_stat(IRQIO_CIO);

	if (!private)
		return;

	irb = this_cpu_ptr(&cio_irb);
	memcpy(&private->irb, irb, sizeof(*irb));
	private->intparm = (u32)(addr_t)sch;
	wake_up(&private->wait_q);

	if (private->completion)
		complete(private->completion);
}

static int vfio_ccw_sch_probe(struct subchannel *sch)
{
	struct pmcw *pmcw = &sch->schib.pmcw;
	struct vfio_ccw_private *private;
	int ret;

	if (pmcw->qf) {
		dev_warn(&sch->dev, "vfio: ccw: does not support QDIO: %s\n",
			 dev_name(&sch->dev));
		return -ENODEV;
	}

	private = kzalloc(sizeof(*private), GFP_KERNEL | GFP_DMA);
	if (!private)
		return -ENOMEM;
	private->sch = sch;
	dev_set_drvdata(&sch->dev, private);

	spin_lock_irq(sch->lock);
	sch->isc = VFIO_CCW_ISC;
	ret = cio_enable_subchannel(sch, (u32)(unsigned long)sch);
	spin_unlock_irq(sch->lock);
	if (ret)
		goto out_free;

	ret = sysfs_create_group(&sch->dev.kobj, &vfio_subchannel_attr_group);
	if (ret)
		goto out_disable;

	ret = vfio_ccw_mdev_reg(sch);
	if (ret)
		goto out_rm_group;

	init_waitqueue_head(&private->wait_q);
	atomic_set(&private->avail, 1);

	return 0;

out_rm_group:
	sysfs_remove_group(&sch->dev.kobj, &vfio_subchannel_attr_group);
out_disable:
	cio_disable_subchannel(sch);
out_free:
	dev_set_drvdata(&sch->dev, NULL);
	kfree(private);
	return ret;
}

static int vfio_ccw_sch_remove(struct subchannel *sch)
{
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);

	vfio_ccw_sch_quiesce(sch);

	vfio_ccw_mdev_unreg(sch);

	sysfs_remove_group(&sch->dev.kobj, &vfio_subchannel_attr_group);

	dev_set_drvdata(&sch->dev, NULL);

	kfree(private);

	return 0;
}

static void vfio_ccw_sch_shutdown(struct subchannel *sch)
{
	vfio_ccw_sch_quiesce(sch);
}

/**
 * vfio_ccw_sch_event - process subchannel event
 * @sch: subchannel
 * @process: non-zero if function is called in process context
 *
 * An unspecified event occurred for this subchannel. Adjust data according
 * to the current operational state of the subchannel. Return zero when the
 * event has been handled sufficiently or -EAGAIN when this function should
 * be called again in process context.
 */
static int vfio_ccw_sch_event(struct subchannel *sch, int process)
{
	unsigned long flags;

	spin_lock_irqsave(sch->lock, flags);
	if (!device_is_registered(&sch->dev))
		goto out_unlock;

	if (work_pending(&sch->todo_work))
		goto out_unlock;

	if (cio_update_schib(sch)) {
		/* Not operational. */
		css_sched_sch_todo(sch, SCH_TODO_UNREG);

		/*
		 * TODO:
		 * Probably we should send the machine check to the guest.
		 */
		goto out_unlock;
	}

out_unlock:
	spin_unlock_irqrestore(sch->lock, flags);

	return 0;
}

static struct css_device_id vfio_ccw_sch_ids[] = {
	{ .match_flags = 0x1, .type = SUBCHANNEL_TYPE_IO, },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(css, vfio_ccw_sch_ids);

static struct css_driver vfio_ccw_sch_driver = {
	.drv = {
		.name = "vfio_ccw",
		.owner = THIS_MODULE,
	},
	.subchannel_type = vfio_ccw_sch_ids,
	.irq = vfio_ccw_sch_irq,
	.probe = vfio_ccw_sch_probe,
	.remove = vfio_ccw_sch_remove,
	.shutdown = vfio_ccw_sch_shutdown,
	.sch_event = vfio_ccw_sch_event,
};

static int __init vfio_ccw_sch_init(void)
{
	int ret;

	isc_register(VFIO_CCW_ISC);
	ret = css_driver_register(&vfio_ccw_sch_driver);
	if (ret)
		isc_unregister(VFIO_CCW_ISC);

	return ret;
}

static void __exit vfio_ccw_sch_exit(void)
{
	css_driver_unregister(&vfio_ccw_sch_driver);
	isc_unregister(VFIO_CCW_ISC);
}
module_init(vfio_ccw_sch_init);
module_exit(vfio_ccw_sch_exit);

MODULE_LICENSE("GPL v2");
