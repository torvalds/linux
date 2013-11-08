/*
 * zfcp device driver
 *
 * Registration and callback for the s390 common I/O layer.
 *
 * Copyright IBM Corporation 2002, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "zfcp_ext.h"
#include "zfcp_reqlist.h"

#define ZFCP_MODEL_PRIV 0x4

static DEFINE_SPINLOCK(zfcp_ccw_adapter_ref_lock);

struct zfcp_adapter *zfcp_ccw_adapter_by_cdev(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter;
	unsigned long flags;

	spin_lock_irqsave(&zfcp_ccw_adapter_ref_lock, flags);
	adapter = dev_get_drvdata(&cdev->dev);
	if (adapter)
		kref_get(&adapter->ref);
	spin_unlock_irqrestore(&zfcp_ccw_adapter_ref_lock, flags);
	return adapter;
}

void zfcp_ccw_adapter_put(struct zfcp_adapter *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&zfcp_ccw_adapter_ref_lock, flags);
	kref_put(&adapter->ref, zfcp_adapter_release);
	spin_unlock_irqrestore(&zfcp_ccw_adapter_ref_lock, flags);
}

static int zfcp_ccw_activate(struct ccw_device *cdev)

{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return 0;

	zfcp_erp_set_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"ccresu2");
	zfcp_erp_wait(adapter);
	flush_work(&adapter->scan_work);

	zfcp_ccw_adapter_put(adapter);

	return 0;
}

static struct ccw_device_id zfcp_ccw_device_id[] = {
	{ CCW_DEVICE_DEVTYPE(0x1731, 0x3, 0x1732, 0x3) },
	{ CCW_DEVICE_DEVTYPE(0x1731, 0x3, 0x1732, ZFCP_MODEL_PRIV) },
	{},
};
MODULE_DEVICE_TABLE(ccw, zfcp_ccw_device_id);

/**
 * zfcp_ccw_priv_sch - check if subchannel is privileged
 * @adapter: Adapter/Subchannel to check
 */
int zfcp_ccw_priv_sch(struct zfcp_adapter *adapter)
{
	return adapter->ccw_device->id.dev_model == ZFCP_MODEL_PRIV;
}

/**
 * zfcp_ccw_probe - probe function of zfcp driver
 * @cdev: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer for each FCP
 * device found on the current system. This is only a stub to make cio
 * work: To only allocate adapter resources for devices actually used,
 * the allocation is deferred to the first call to ccw_set_online.
 */
static int zfcp_ccw_probe(struct ccw_device *cdev)
{
	return 0;
}

/**
 * zfcp_ccw_remove - remove function of zfcp driver
 * @cdev: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and removes an adapter
 * from the system. Task of this function is to get rid of all units and
 * ports that belong to this adapter. And in addition all resources of this
 * adapter will be freed too.
 */
static void zfcp_ccw_remove(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port, *p;
	struct zfcp_unit *unit, *u;
	LIST_HEAD(unit_remove_lh);
	LIST_HEAD(port_remove_lh);

	ccw_device_set_offline(cdev);

	adapter = zfcp_ccw_adapter_by_cdev(cdev);
	if (!adapter)
		return;

	write_lock_irq(&adapter->port_list_lock);
	list_for_each_entry_safe(port, p, &adapter->port_list, list) {
		write_lock(&port->unit_list_lock);
		list_for_each_entry_safe(unit, u, &port->unit_list, list)
			list_move(&unit->list, &unit_remove_lh);
		write_unlock(&port->unit_list_lock);
		list_move(&port->list, &port_remove_lh);
	}
	write_unlock_irq(&adapter->port_list_lock);
	zfcp_ccw_adapter_put(adapter); /* put from zfcp_ccw_adapter_by_cdev */

	list_for_each_entry_safe(unit, u, &unit_remove_lh, list)
		zfcp_device_unregister(&unit->dev, &zfcp_sysfs_unit_attrs);

	list_for_each_entry_safe(port, p, &port_remove_lh, list)
		zfcp_device_unregister(&port->dev, &zfcp_sysfs_port_attrs);

	zfcp_adapter_unregister(adapter);
}

/**
 * zfcp_ccw_set_online - set_online function of zfcp driver
 * @cdev: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an
 * adapter into state online.  The first call will allocate all
 * adapter resources that will be retained until the device is removed
 * via zfcp_ccw_remove.
 *
 * Setting an fcp device online means that it will be registered with
 * the SCSI stack, that the QDIO queues will be set up and that the
 * adapter will be opened.
 */
static int zfcp_ccw_set_online(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter) {
		adapter = zfcp_adapter_enqueue(cdev);

		if (IS_ERR(adapter)) {
			dev_err(&cdev->dev,
				"Setting up data structures for the "
				"FCP adapter failed\n");
			return PTR_ERR(adapter);
		}
		kref_get(&adapter->ref);
	}

	/* initialize request counter */
	BUG_ON(!zfcp_reqlist_isempty(adapter->req_list));
	adapter->req_no = 0;

	zfcp_ccw_activate(cdev);
	zfcp_ccw_adapter_put(adapter);
	return 0;
}

/**
 * zfcp_ccw_set_offline - set_offline function of zfcp driver
 * @cdev: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state offline.
 */
static int zfcp_ccw_set_offline(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return 0;

	zfcp_erp_adapter_shutdown(adapter, 0, "ccsoff1");
	zfcp_erp_wait(adapter);

	zfcp_ccw_adapter_put(adapter);
	return 0;
}

/**
 * zfcp_ccw_notify - ccw notify function
 * @cdev: pointer to belonging ccw device
 * @event: indicates if adapter was detached or attached
 *
 * This function gets called by the common i/o layer if an adapter has gone
 * or reappeared.
 */
static int zfcp_ccw_notify(struct ccw_device *cdev, int event)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return 1;

	switch (event) {
	case CIO_GONE:
		dev_warn(&cdev->dev, "The FCP device has been detached\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti1");
		break;
	case CIO_NO_PATH:
		dev_warn(&cdev->dev,
			 "The CHPID for the FCP device is offline\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti2");
		break;
	case CIO_OPER:
		dev_info(&cdev->dev, "The FCP device is operational again\n");
		zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_RUNNING);
		zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
					"ccnoti4");
		break;
	case CIO_BOXED:
		dev_warn(&cdev->dev, "The FCP device did not respond within "
				     "the specified time\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti5");
		break;
	}

	zfcp_ccw_adapter_put(adapter);
	return 1;
}

/**
 * zfcp_ccw_shutdown - handle shutdown from cio
 * @cdev: device for adapter to shutdown.
 */
static void zfcp_ccw_shutdown(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return;

	zfcp_erp_adapter_shutdown(adapter, 0, "ccshut1");
	zfcp_erp_wait(adapter);
	zfcp_erp_thread_kill(adapter);

	zfcp_ccw_adapter_put(adapter);
}

struct ccw_driver zfcp_ccw_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "zfcp",
	},
	.ids         = zfcp_ccw_device_id,
	.probe       = zfcp_ccw_probe,
	.remove      = zfcp_ccw_remove,
	.set_online  = zfcp_ccw_set_online,
	.set_offline = zfcp_ccw_set_offline,
	.notify      = zfcp_ccw_notify,
	.shutdown    = zfcp_ccw_shutdown,
	.freeze      = zfcp_ccw_set_offline,
	.thaw	     = zfcp_ccw_activate,
	.restore     = zfcp_ccw_activate,
};
