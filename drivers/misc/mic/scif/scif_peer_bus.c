// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Intel SCIF driver.
 */
#include "scif_main.h"
#include "../bus/scif_bus.h"
#include "scif_peer_bus.h"

static inline struct scif_peer_dev *
dev_to_scif_peer(struct device *dev)
{
	return container_of(dev, struct scif_peer_dev, dev);
}

struct bus_type scif_peer_bus = {
	.name  = "scif_peer_bus",
};

static void scif_peer_release_dev(struct device *d)
{
	struct scif_peer_dev *sdev = dev_to_scif_peer(d);
	struct scif_dev *scifdev = &scif_dev[sdev->dnode];

	scif_cleanup_scifdev(scifdev);
	kfree(sdev);
}

static int scif_peer_initialize_device(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev;
	int ret;

	spdev = kzalloc(sizeof(*spdev), GFP_KERNEL);
	if (!spdev) {
		ret = -ENOMEM;
		goto err;
	}

	spdev->dev.parent = scifdev->sdev->dev.parent;
	spdev->dev.release = scif_peer_release_dev;
	spdev->dnode = scifdev->node;
	spdev->dev.bus = &scif_peer_bus;
	dev_set_name(&spdev->dev, "scif_peer-dev%u", spdev->dnode);

	device_initialize(&spdev->dev);
	get_device(&spdev->dev);
	rcu_assign_pointer(scifdev->spdev, spdev);

	mutex_lock(&scif_info.conflock);
	scif_info.total++;
	scif_info.maxid = max_t(u32, spdev->dnode, scif_info.maxid);
	mutex_unlock(&scif_info.conflock);
	return 0;
err:
	dev_err(&scifdev->sdev->dev,
		"dnode %d: initialize_device rc %d\n", scifdev->node, ret);
	return ret;
}

static int scif_peer_add_device(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev = rcu_dereference(scifdev->spdev);
	char pool_name[16];
	int ret;

	ret = device_add(&spdev->dev);
	put_device(&spdev->dev);
	if (ret) {
		dev_err(&scifdev->sdev->dev,
			"dnode %d: peer device_add failed\n", scifdev->node);
		goto put_spdev;
	}

	scnprintf(pool_name, sizeof(pool_name), "scif-%d", spdev->dnode);
	scifdev->signal_pool = dmam_pool_create(pool_name, &scifdev->sdev->dev,
						sizeof(struct scif_status), 1,
						0);
	if (!scifdev->signal_pool) {
		dev_err(&scifdev->sdev->dev,
			"dnode %d: dmam_pool_create failed\n", scifdev->node);
		ret = -ENOMEM;
		goto del_spdev;
	}
	dev_dbg(&spdev->dev, "Added peer dnode %d\n", spdev->dnode);
	return 0;
del_spdev:
	device_del(&spdev->dev);
put_spdev:
	RCU_INIT_POINTER(scifdev->spdev, NULL);
	synchronize_rcu();
	put_device(&spdev->dev);

	mutex_lock(&scif_info.conflock);
	scif_info.total--;
	mutex_unlock(&scif_info.conflock);
	return ret;
}

void scif_add_peer_device(struct work_struct *work)
{
	struct scif_dev *scifdev = container_of(work, struct scif_dev,
						peer_add_work);

	scif_peer_add_device(scifdev);
}

/*
 * Peer device registration is split into a device_initialize and a device_add.
 * The reason for doing this is as follows: First, peer device registration
 * itself cannot be done in the message processing thread and must be delegated
 * to another workqueue, otherwise if SCIF client probe, called during peer
 * device registration, calls scif_connect(..), it will block the message
 * processing thread causing a deadlock. Next, device_initialize is done in the
 * "top-half" message processing thread and device_add in the "bottom-half"
 * workqueue. If this is not done, SCIF_CNCT_REQ message processing executing
 * concurrently with SCIF_INIT message processing is unable to get a reference
 * on the peer device, thereby failing the connect request.
 */
void scif_peer_register_device(struct scif_dev *scifdev)
{
	int ret;

	mutex_lock(&scifdev->lock);
	ret = scif_peer_initialize_device(scifdev);
	if (ret)
		goto exit;
	schedule_work(&scifdev->peer_add_work);
exit:
	mutex_unlock(&scifdev->lock);
}

int scif_peer_unregister_device(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev;

	mutex_lock(&scifdev->lock);
	/* Flush work to ensure device register is complete */
	flush_work(&scifdev->peer_add_work);

	/*
	 * Continue holding scifdev->lock since theoretically unregister_device
	 * can be called simultaneously from multiple threads
	 */
	spdev = rcu_dereference(scifdev->spdev);
	if (!spdev) {
		mutex_unlock(&scifdev->lock);
		return -ENODEV;
	}

	RCU_INIT_POINTER(scifdev->spdev, NULL);
	synchronize_rcu();
	mutex_unlock(&scifdev->lock);

	dev_dbg(&spdev->dev, "Removing peer dnode %d\n", spdev->dnode);
	device_unregister(&spdev->dev);

	mutex_lock(&scif_info.conflock);
	scif_info.total--;
	mutex_unlock(&scif_info.conflock);
	return 0;
}

int scif_peer_bus_init(void)
{
	return bus_register(&scif_peer_bus);
}

void scif_peer_bus_exit(void)
{
	bus_unregister(&scif_peer_bus);
}
