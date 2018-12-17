// SPDX-License-Identifier: GPL-2.0
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

struct workqueue_struct *vfio_ccw_work_q;
static struct kmem_cache *vfio_ccw_io_region;

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
			flush_workqueue(vfio_ccw_work_q);
			ret = cio_cancel_halt_clear(sch, &iretry);
		};

		ret = cio_disable_subchannel(sch);
	} while (ret == -EBUSY);
out_unlock:
	private->state = VFIO_CCW_STATE_NOT_OPER;
	spin_unlock_irq(sch->lock);
	return ret;
}

static void vfio_ccw_sch_io_todo(struct work_struct *work)
{
	struct vfio_ccw_private *private;
	struct irb *irb;

	private = container_of(work, struct vfio_ccw_private, io_work);
	irb = &private->irb;

	if (scsw_is_solicited(&irb->scsw)) {
		cp_update_scsw(&private->cp, &irb->scsw);
		cp_free(&private->cp);
	}
	memcpy(private->io_region->irb_area, irb, sizeof(*irb));

	if (private->io_trigger)
		eventfd_signal(private->io_trigger, 1);

	if (private->mdev)
		private->state = VFIO_CCW_STATE_IDLE;
}

/*
 * Css driver callbacks
 */
static void vfio_ccw_sch_irq(struct subchannel *sch)
{
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);

	inc_irq_stat(IRQIO_CIO);
	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_INTERRUPT);
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

	private->io_region = kmem_cache_zalloc(vfio_ccw_io_region,
					       GFP_KERNEL | GFP_DMA);
	if (!private->io_region) {
		kfree(private);
		return -ENOMEM;
	}

	private->sch = sch;
	dev_set_drvdata(&sch->dev, private);

	spin_lock_irq(sch->lock);
	private->state = VFIO_CCW_STATE_NOT_OPER;
	sch->isc = VFIO_CCW_ISC;
	ret = cio_enable_subchannel(sch, (u32)(unsigned long)sch);
	spin_unlock_irq(sch->lock);
	if (ret)
		goto out_free;

	INIT_WORK(&private->io_work, vfio_ccw_sch_io_todo);
	atomic_set(&private->avail, 1);
	private->state = VFIO_CCW_STATE_STANDBY;

	ret = vfio_ccw_mdev_reg(sch);
	if (ret)
		goto out_disable;

	return 0;

out_disable:
	cio_disable_subchannel(sch);
out_free:
	dev_set_drvdata(&sch->dev, NULL);
	kmem_cache_free(vfio_ccw_io_region, private->io_region);
	kfree(private);
	return ret;
}

static int vfio_ccw_sch_remove(struct subchannel *sch)
{
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);

	vfio_ccw_sch_quiesce(sch);

	vfio_ccw_mdev_unreg(sch);

	dev_set_drvdata(&sch->dev, NULL);

	kmem_cache_free(vfio_ccw_io_region, private->io_region);
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
	struct vfio_ccw_private *private = dev_get_drvdata(&sch->dev);
	unsigned long flags;
	int rc = -EAGAIN;

	spin_lock_irqsave(sch->lock, flags);
	if (!device_is_registered(&sch->dev))
		goto out_unlock;

	if (work_pending(&sch->todo_work))
		goto out_unlock;

	if (cio_update_schib(sch)) {
		vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_NOT_OPER);
		rc = 0;
		goto out_unlock;
	}

	private = dev_get_drvdata(&sch->dev);
	if (private->state == VFIO_CCW_STATE_NOT_OPER) {
		private->state = private->mdev ? VFIO_CCW_STATE_IDLE :
				 VFIO_CCW_STATE_STANDBY;
	}
	rc = 0;

out_unlock:
	spin_unlock_irqrestore(sch->lock, flags);

	return rc;
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

	vfio_ccw_work_q = create_singlethread_workqueue("vfio-ccw");
	if (!vfio_ccw_work_q)
		return -ENOMEM;

	vfio_ccw_io_region = kmem_cache_create_usercopy("vfio_ccw_io_region",
					sizeof(struct ccw_io_region), 0,
					SLAB_ACCOUNT, 0,
					sizeof(struct ccw_io_region), NULL);
	if (!vfio_ccw_io_region) {
		destroy_workqueue(vfio_ccw_work_q);
		return -ENOMEM;
	}

	isc_register(VFIO_CCW_ISC);
	ret = css_driver_register(&vfio_ccw_sch_driver);
	if (ret) {
		isc_unregister(VFIO_CCW_ISC);
		kmem_cache_destroy(vfio_ccw_io_region);
		destroy_workqueue(vfio_ccw_work_q);
	}

	return ret;
}

static void __exit vfio_ccw_sch_exit(void)
{
	css_driver_unregister(&vfio_ccw_sch_driver);
	isc_unregister(VFIO_CCW_ISC);
	kmem_cache_destroy(vfio_ccw_io_region);
	destroy_workqueue(vfio_ccw_work_q);
}
module_init(vfio_ccw_sch_init);
module_exit(vfio_ccw_sch_exit);

MODULE_LICENSE("GPL v2");
