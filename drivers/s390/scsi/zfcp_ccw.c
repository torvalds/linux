/*
 * zfcp device driver
 *
 * Registration and callback for the s390 common I/O layer.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "zfcp_ext.h"

#define ZFCP_MODEL_PRIV 0x4

static int zfcp_ccw_suspend(struct ccw_device *cdev)

{
	struct zfcp_adapter *adapter = dev_get_drvdata(&cdev->dev);

	if (!adapter)
		return 0;

	mutex_lock(&zfcp_data.config_mutex);

	zfcp_erp_adapter_shutdown(adapter, 0, "ccsusp1", NULL);
	zfcp_erp_wait(adapter);

	mutex_unlock(&zfcp_data.config_mutex);

	return 0;
}

static int zfcp_ccw_activate(struct ccw_device *cdev)

{
	struct zfcp_adapter *adapter = dev_get_drvdata(&cdev->dev);

	if (!adapter)
		return 0;

	zfcp_erp_modify_adapter_status(adapter, "ccresu1", NULL,
				       ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"ccresu2", NULL);
	zfcp_erp_wait(adapter);
	flush_work(&adapter->scan_work);

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
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer for each FCP
 * device found on the current system. This is only a stub to make cio
 * work: To only allocate adapter resources for devices actually used,
 * the allocation is deferred to the first call to ccw_set_online.
 */
static int zfcp_ccw_probe(struct ccw_device *ccw_device)
{
	return 0;
}

/**
 * zfcp_ccw_remove - remove function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and removes an adapter
 * from the system. Task of this function is to get rid of all units and
 * ports that belong to this adapter. And in addition all resources of this
 * adapter will be freed too.
 */
static void zfcp_ccw_remove(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port, *p;
	struct zfcp_unit *unit, *u;
	LIST_HEAD(unit_remove_lh);
	LIST_HEAD(port_remove_lh);

	ccw_device_set_offline(ccw_device);

	mutex_lock(&zfcp_data.config_mutex);
	adapter = dev_get_drvdata(&ccw_device->dev);
	if (!adapter)
		goto out;
	mutex_unlock(&zfcp_data.config_mutex);

	cancel_work_sync(&adapter->scan_work);

	mutex_lock(&zfcp_data.config_mutex);

	/* this also removes the scsi devices, so call it first */
	zfcp_adapter_scsi_unregister(adapter);

	write_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry_safe(port, p, &adapter->port_list_head, list) {
		list_for_each_entry_safe(unit, u, &port->unit_list_head, list) {
			list_move(&unit->list, &unit_remove_lh);
			atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE,
					&unit->status);
		}
		list_move(&port->list, &port_remove_lh);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	}
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	write_unlock_irq(&zfcp_data.config_lock);

	list_for_each_entry_safe(port, p, &port_remove_lh, list) {
		list_for_each_entry_safe(unit, u, &unit_remove_lh, list)
			zfcp_unit_dequeue(unit);
		zfcp_port_dequeue(port);
	}
	wait_event(adapter->remove_wq, atomic_read(&adapter->refcount) == 0);
	zfcp_adapter_dequeue(adapter);

out:
	mutex_unlock(&zfcp_data.config_mutex);
}

/**
 * zfcp_ccw_set_online - set_online function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
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
static int zfcp_ccw_set_online(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int ret = 0;

	mutex_lock(&zfcp_data.config_mutex);
	adapter = dev_get_drvdata(&ccw_device->dev);

	if (!adapter) {
		ret = zfcp_adapter_enqueue(ccw_device);
		if (ret) {
			dev_err(&ccw_device->dev,
				"Setting up data structures for the "
				"FCP adapter failed\n");
			goto out;
		}
		adapter = dev_get_drvdata(&ccw_device->dev);
	}

	/* initialize request counter */
	BUG_ON(!zfcp_reqlist_isempty(adapter));
	adapter->req_no = 0;

	zfcp_erp_modify_adapter_status(adapter, "ccsonl1", NULL,
				       ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"ccsonl2", NULL);
	zfcp_erp_wait(adapter);
out:
	mutex_unlock(&zfcp_data.config_mutex);
	if (!ret)
		flush_work(&adapter->scan_work);
	return ret;
}

/**
 * zfcp_ccw_set_offline - set_offline function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state offline.
 */
static int zfcp_ccw_set_offline(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	mutex_lock(&zfcp_data.config_mutex);
	adapter = dev_get_drvdata(&ccw_device->dev);
	zfcp_erp_adapter_shutdown(adapter, 0, "ccsoff1", NULL);
	zfcp_erp_wait(adapter);
	mutex_unlock(&zfcp_data.config_mutex);
	return 0;
}

/**
 * zfcp_ccw_notify - ccw notify function
 * @ccw_device: pointer to belonging ccw device
 * @event: indicates if adapter was detached or attached
 *
 * This function gets called by the common i/o layer if an adapter has gone
 * or reappeared.
 */
static int zfcp_ccw_notify(struct ccw_device *ccw_device, int event)
{
	struct zfcp_adapter *adapter = dev_get_drvdata(&ccw_device->dev);

	switch (event) {
	case CIO_GONE:
		dev_warn(&adapter->ccw_device->dev,
			 "The FCP device has been detached\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti1", NULL);
		break;
	case CIO_NO_PATH:
		dev_warn(&adapter->ccw_device->dev,
			 "The CHPID for the FCP device is offline\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti2", NULL);
		break;
	case CIO_OPER:
		dev_info(&adapter->ccw_device->dev,
			 "The FCP device is operational again\n");
		zfcp_erp_modify_adapter_status(adapter, "ccnoti3", NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
					"ccnoti4", NULL);
		break;
	case CIO_BOXED:
		dev_warn(&adapter->ccw_device->dev, "The FCP device "
			 "did not respond within the specified time\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti5", NULL);
		break;
	}
	return 1;
}

/**
 * zfcp_ccw_shutdown - handle shutdown from cio
 * @cdev: device for adapter to shutdown.
 */
static void zfcp_ccw_shutdown(struct ccw_device *cdev)
{
	struct zfcp_adapter *adapter;

	mutex_lock(&zfcp_data.config_mutex);
	adapter = dev_get_drvdata(&cdev->dev);
	if (!adapter)
		goto out;

	zfcp_erp_adapter_shutdown(adapter, 0, "ccshut1", NULL);
	zfcp_erp_wait(adapter);
	zfcp_erp_thread_kill(adapter);
out:
	mutex_unlock(&zfcp_data.config_mutex);
}

struct ccw_driver zfcp_ccw_driver = {
	.owner       = THIS_MODULE,
	.name        = "zfcp",
	.ids         = zfcp_ccw_device_id,
	.probe       = zfcp_ccw_probe,
	.remove      = zfcp_ccw_remove,
	.set_online  = zfcp_ccw_set_online,
	.set_offline = zfcp_ccw_set_offline,
	.notify      = zfcp_ccw_notify,
	.shutdown    = zfcp_ccw_shutdown,
	.freeze      = zfcp_ccw_suspend,
	.thaw	     = zfcp_ccw_activate,
	.restore     = zfcp_ccw_activate,
};

/**
 * zfcp_ccw_register - ccw register function
 *
 * Registers the driver at the common i/o layer. This function will be called
 * at module load time/system start.
 */
int __init zfcp_ccw_register(void)
{
	return ccw_driver_register(&zfcp_ccw_driver);
}
