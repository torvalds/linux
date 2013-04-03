/*
 * Device driver for s390 storage class memory.
 *
 * Copyright IBM Corp. 2012
 * Author(s): Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "scm_block"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/eadm.h>
#include "scm_blk.h"

static void scm_notify(struct scm_device *scmdev, enum scm_event event)
{
	struct scm_blk_dev *bdev = dev_get_drvdata(&scmdev->dev);

	switch (event) {
	case SCM_CHANGE:
		pr_info("%lu: The capabilities of the SCM increment changed\n",
			(unsigned long) scmdev->address);
		SCM_LOG(2, "State changed");
		SCM_LOG_STATE(2, scmdev);
		break;
	case SCM_AVAIL:
		SCM_LOG(2, "Increment available");
		SCM_LOG_STATE(2, scmdev);
		scm_blk_set_available(bdev);
		break;
	}
}

static int scm_probe(struct scm_device *scmdev)
{
	struct scm_blk_dev *bdev;
	int ret;

	SCM_LOG(2, "probe");
	SCM_LOG_STATE(2, scmdev);

	if (scmdev->attrs.oper_state != OP_STATE_GOOD)
		return -EINVAL;

	bdev = kzalloc(sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	dev_set_drvdata(&scmdev->dev, bdev);
	ret = scm_blk_dev_setup(bdev, scmdev);
	if (ret) {
		dev_set_drvdata(&scmdev->dev, NULL);
		kfree(bdev);
		goto out;
	}

out:
	return ret;
}

static int scm_remove(struct scm_device *scmdev)
{
	struct scm_blk_dev *bdev = dev_get_drvdata(&scmdev->dev);

	scm_blk_dev_cleanup(bdev);
	dev_set_drvdata(&scmdev->dev, NULL);
	kfree(bdev);

	return 0;
}

static struct scm_driver scm_drv = {
	.drv = {
		.name = "scm_block",
		.owner = THIS_MODULE,
	},
	.notify = scm_notify,
	.probe = scm_probe,
	.remove = scm_remove,
	.handler = scm_blk_irq,
};

int __init scm_drv_init(void)
{
	return scm_driver_register(&scm_drv);
}

void scm_drv_cleanup(void)
{
	scm_driver_unregister(&scm_drv);
}
