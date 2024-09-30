// SPDX-License-Identifier: GPL-2.0
/*
 * VFIO based Physical Subchannel device driver
 *
 * Copyright IBM Corp. 2017
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Cornelia Huck <cohuck@redhat.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mdev.h>

#include <asm/isc.h>

#include "chp.h"
#include "ioasm.h"
#include "css.h"
#include "vfio_ccw_private.h"

struct workqueue_struct *vfio_ccw_work_q;
struct kmem_cache *vfio_ccw_io_region;
struct kmem_cache *vfio_ccw_cmd_region;
struct kmem_cache *vfio_ccw_schib_region;
struct kmem_cache *vfio_ccw_crw_region;

debug_info_t *vfio_ccw_debug_msg_id;
debug_info_t *vfio_ccw_debug_trace_id;

/*
 * Helpers
 */
int vfio_ccw_sch_quiesce(struct subchannel *sch)
{
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);
	DECLARE_COMPLETION_ONSTACK(completion);
	int iretry, ret = 0;

	/*
	 * Probably an impossible situation, after being called through
	 * FSM callbacks. But in the event it did, register a warning
	 * and return as if things were fine.
	 */
	if (WARN_ON(!private))
		return 0;

	iretry = 255;
	do {

		ret = cio_cancel_halt_clear(sch, &iretry);

		if (ret == -EIO) {
			pr_err("vfio_ccw: could not quiesce subchannel 0.%x.%04x!\n",
			       sch->schid.ssid, sch->schid.sch_no);
			break;
		}

		/*
		 * Flush all I/O and wait for
		 * cancel/halt/clear completion.
		 */
		private->completion = &completion;
		spin_unlock_irq(&sch->lock);

		if (ret == -EBUSY)
			wait_for_completion_timeout(&completion, 3*HZ);

		private->completion = NULL;
		flush_workqueue(vfio_ccw_work_q);
		spin_lock_irq(&sch->lock);
		ret = cio_disable_subchannel(sch);
	} while (ret == -EBUSY);

	return ret;
}

void vfio_ccw_sch_io_todo(struct work_struct *work)
{
	struct vfio_ccw_private *private;
	struct irb *irb;
	bool is_final;
	bool cp_is_finished = false;

	private = container_of(work, struct vfio_ccw_private, io_work);
	irb = &private->irb;

	is_final = !(scsw_actl(&irb->scsw) &
		     (SCSW_ACTL_DEVACT | SCSW_ACTL_SCHACT));
	if (scsw_is_solicited(&irb->scsw)) {
		cp_update_scsw(&private->cp, &irb->scsw);
		if (is_final && private->state == VFIO_CCW_STATE_CP_PENDING) {
			cp_free(&private->cp);
			cp_is_finished = true;
		}
	}
	mutex_lock(&private->io_mutex);
	memcpy(private->io_region->irb_area, irb, sizeof(*irb));
	mutex_unlock(&private->io_mutex);

	/*
	 * Reset to IDLE only if processing of a channel program
	 * has finished. Do not overwrite a possible processing
	 * state if the interrupt was unsolicited, or if the final
	 * interrupt was for HSCH or CSCH.
	 */
	if (cp_is_finished)
		private->state = VFIO_CCW_STATE_IDLE;

	if (private->io_trigger)
		eventfd_signal(private->io_trigger);
}

void vfio_ccw_crw_todo(struct work_struct *work)
{
	struct vfio_ccw_private *private;

	private = container_of(work, struct vfio_ccw_private, crw_work);

	if (!list_empty(&private->crw) && private->crw_trigger)
		eventfd_signal(private->crw_trigger);
}

/*
 * Css driver callbacks
 */
static void vfio_ccw_sch_irq(struct subchannel *sch)
{
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);

	/*
	 * The subchannel should still be disabled at this point,
	 * so an interrupt would be quite surprising. As with an
	 * interrupt while the FSM is closed, let's attempt to
	 * disable the subchannel again.
	 */
	if (!private) {
		VFIO_CCW_MSG_EVENT(2, "sch %x.%x.%04x: unexpected interrupt\n",
				   sch->schid.cssid, sch->schid.ssid,
				   sch->schid.sch_no);

		cio_disable_subchannel(sch);
		return;
	}

	inc_irq_stat(IRQIO_CIO);
	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_INTERRUPT);
}

static void vfio_ccw_free_parent(struct device *dev)
{
	struct vfio_ccw_parent *parent = container_of(dev, struct vfio_ccw_parent, dev);

	kfree(parent);
}

static int vfio_ccw_sch_probe(struct subchannel *sch)
{
	struct pmcw *pmcw = &sch->schib.pmcw;
	struct vfio_ccw_parent *parent;
	int ret = -ENOMEM;

	if (pmcw->qf) {
		dev_warn(&sch->dev, "vfio: ccw: does not support QDIO: %s\n",
			 dev_name(&sch->dev));
		return -ENODEV;
	}

	parent = kzalloc(struct_size(parent, mdev_types, 1), GFP_KERNEL);
	if (!parent)
		return -ENOMEM;

	dev_set_name(&parent->dev, "parent");
	parent->dev.parent = &sch->dev;
	parent->dev.release = &vfio_ccw_free_parent;
	ret = device_register(&parent->dev);
	if (ret)
		goto out_free;

	dev_set_drvdata(&sch->dev, parent);

	parent->mdev_type.sysfs_name = "io";
	parent->mdev_type.pretty_name = "I/O subchannel (Non-QDIO)";
	parent->mdev_types[0] = &parent->mdev_type;
	ret = mdev_register_parent(&parent->parent, &sch->dev,
				   &vfio_ccw_mdev_driver,
				   parent->mdev_types, 1);
	if (ret)
		goto out_unreg;

	VFIO_CCW_MSG_EVENT(4, "bound to subchannel %x.%x.%04x\n",
			   sch->schid.cssid, sch->schid.ssid,
			   sch->schid.sch_no);
	return 0;

out_unreg:
	device_del(&parent->dev);
out_free:
	put_device(&parent->dev);
	dev_set_drvdata(&sch->dev, NULL);
	return ret;
}

static void vfio_ccw_sch_remove(struct subchannel *sch)
{
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);

	mdev_unregister_parent(&parent->parent);

	device_unregister(&parent->dev);
	dev_set_drvdata(&sch->dev, NULL);

	VFIO_CCW_MSG_EVENT(4, "unbound from subchannel %x.%x.%04x\n",
			   sch->schid.cssid, sch->schid.ssid,
			   sch->schid.sch_no);
}

static void vfio_ccw_sch_shutdown(struct subchannel *sch)
{
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);

	if (!private)
		return;

	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_CLOSE);
	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_NOT_OPER);
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
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);
	unsigned long flags;
	int rc = -EAGAIN;

	spin_lock_irqsave(&sch->lock, flags);
	if (!device_is_registered(&sch->dev))
		goto out_unlock;

	if (work_pending(&sch->todo_work))
		goto out_unlock;

	rc = 0;

	if (cio_update_schib(sch)) {
		if (private)
			vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_NOT_OPER);
	}

out_unlock:
	spin_unlock_irqrestore(&sch->lock, flags);

	return rc;
}

static void vfio_ccw_queue_crw(struct vfio_ccw_private *private,
			       unsigned int rsc,
			       unsigned int erc,
			       unsigned int rsid)
{
	struct vfio_ccw_crw *crw;

	/*
	 * If unable to allocate a CRW, just drop the event and
	 * carry on.  The guest will either see a later one or
	 * learn when it issues its own store subchannel.
	 */
	crw = kzalloc(sizeof(*crw), GFP_ATOMIC);
	if (!crw)
		return;

	/*
	 * Build the CRW based on the inputs given to us.
	 */
	crw->crw.rsc = rsc;
	crw->crw.erc = erc;
	crw->crw.rsid = rsid;

	list_add_tail(&crw->next, &private->crw);
	queue_work(vfio_ccw_work_q, &private->crw_work);
}

static int vfio_ccw_chp_event(struct subchannel *sch,
			      struct chp_link *link, int event)
{
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);
	int mask = chp_ssd_get_mask(&sch->ssd_info, link);
	int retry = 255;

	if (!private || !mask)
		return 0;

	trace_vfio_ccw_chp_event(sch->schid, mask, event);
	VFIO_CCW_MSG_EVENT(2, "sch %x.%x.%04x: mask=0x%x event=%d\n",
			   sch->schid.cssid,
			   sch->schid.ssid, sch->schid.sch_no,
			   mask, event);

	if (cio_update_schib(sch))
		return -ENODEV;

	switch (event) {
	case CHP_VARY_OFF:
		/* Path logically turned off */
		sch->opm &= ~mask;
		sch->lpm &= ~mask;
		if (sch->schib.pmcw.lpum & mask)
			cio_cancel_halt_clear(sch, &retry);
		break;
	case CHP_OFFLINE:
		/* Path is gone */
		if (sch->schib.pmcw.lpum & mask)
			cio_cancel_halt_clear(sch, &retry);
		vfio_ccw_queue_crw(private, CRW_RSC_CPATH, CRW_ERC_PERRN,
				   link->chpid.id);
		break;
	case CHP_VARY_ON:
		/* Path logically turned on */
		sch->opm |= mask;
		sch->lpm |= mask;
		break;
	case CHP_ONLINE:
		/* Path became available */
		sch->lpm |= mask & sch->opm;
		vfio_ccw_queue_crw(private, CRW_RSC_CPATH, CRW_ERC_INIT,
				   link->chpid.id);
		break;
	}

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
	.chp_event = vfio_ccw_chp_event,
};

static int __init vfio_ccw_debug_init(void)
{
	vfio_ccw_debug_msg_id = debug_register("vfio_ccw_msg", 16, 1,
					       11 * sizeof(long));
	if (!vfio_ccw_debug_msg_id)
		goto out_unregister;
	debug_register_view(vfio_ccw_debug_msg_id, &debug_sprintf_view);
	debug_set_level(vfio_ccw_debug_msg_id, 2);
	vfio_ccw_debug_trace_id = debug_register("vfio_ccw_trace", 16, 1, 16);
	if (!vfio_ccw_debug_trace_id)
		goto out_unregister;
	debug_register_view(vfio_ccw_debug_trace_id, &debug_hex_ascii_view);
	debug_set_level(vfio_ccw_debug_trace_id, 2);
	return 0;

out_unregister:
	debug_unregister(vfio_ccw_debug_msg_id);
	debug_unregister(vfio_ccw_debug_trace_id);
	return -1;
}

static void vfio_ccw_debug_exit(void)
{
	debug_unregister(vfio_ccw_debug_msg_id);
	debug_unregister(vfio_ccw_debug_trace_id);
}

static void vfio_ccw_destroy_regions(void)
{
	kmem_cache_destroy(vfio_ccw_crw_region);
	kmem_cache_destroy(vfio_ccw_schib_region);
	kmem_cache_destroy(vfio_ccw_cmd_region);
	kmem_cache_destroy(vfio_ccw_io_region);
}

static int __init vfio_ccw_sch_init(void)
{
	int ret;

	ret = vfio_ccw_debug_init();
	if (ret)
		return ret;

	vfio_ccw_work_q = create_singlethread_workqueue("vfio-ccw");
	if (!vfio_ccw_work_q) {
		ret = -ENOMEM;
		goto out_regions;
	}

	vfio_ccw_io_region = kmem_cache_create_usercopy("vfio_ccw_io_region",
					sizeof(struct ccw_io_region), 0,
					SLAB_ACCOUNT, 0,
					sizeof(struct ccw_io_region), NULL);
	if (!vfio_ccw_io_region) {
		ret = -ENOMEM;
		goto out_regions;
	}

	vfio_ccw_cmd_region = kmem_cache_create_usercopy("vfio_ccw_cmd_region",
					sizeof(struct ccw_cmd_region), 0,
					SLAB_ACCOUNT, 0,
					sizeof(struct ccw_cmd_region), NULL);
	if (!vfio_ccw_cmd_region) {
		ret = -ENOMEM;
		goto out_regions;
	}

	vfio_ccw_schib_region = kmem_cache_create_usercopy("vfio_ccw_schib_region",
					sizeof(struct ccw_schib_region), 0,
					SLAB_ACCOUNT, 0,
					sizeof(struct ccw_schib_region), NULL);

	if (!vfio_ccw_schib_region) {
		ret = -ENOMEM;
		goto out_regions;
	}

	vfio_ccw_crw_region = kmem_cache_create_usercopy("vfio_ccw_crw_region",
					sizeof(struct ccw_crw_region), 0,
					SLAB_ACCOUNT, 0,
					sizeof(struct ccw_crw_region), NULL);

	if (!vfio_ccw_crw_region) {
		ret = -ENOMEM;
		goto out_regions;
	}

	ret = mdev_register_driver(&vfio_ccw_mdev_driver);
	if (ret)
		goto out_regions;

	isc_register(VFIO_CCW_ISC);
	ret = css_driver_register(&vfio_ccw_sch_driver);
	if (ret) {
		isc_unregister(VFIO_CCW_ISC);
		goto out_driver;
	}

	return ret;

out_driver:
	mdev_unregister_driver(&vfio_ccw_mdev_driver);
out_regions:
	vfio_ccw_destroy_regions();
	destroy_workqueue(vfio_ccw_work_q);
	vfio_ccw_debug_exit();
	return ret;
}

static void __exit vfio_ccw_sch_exit(void)
{
	css_driver_unregister(&vfio_ccw_sch_driver);
	mdev_unregister_driver(&vfio_ccw_mdev_driver);
	isc_unregister(VFIO_CCW_ISC);
	vfio_ccw_destroy_regions();
	destroy_workqueue(vfio_ccw_work_q);
	vfio_ccw_debug_exit();
}
module_init(vfio_ccw_sch_init);
module_exit(vfio_ccw_sch_exit);

MODULE_DESCRIPTION("VFIO based Subchannel device driver");
MODULE_LICENSE("GPL v2");
