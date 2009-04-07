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

/**
 * zfcp_ccw_probe - probe function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets up the initial
 * data structures for each fcp adapter, which was detected by the system.
 * Also the sysfs files for this adapter will be created by this function.
 * In addition the nameserver port will be added to the ports of the adapter
 * and its sysfs representation will be created too.
 */
static int zfcp_ccw_probe(struct ccw_device *ccw_device)
{
	int retval = 0;

	down(&zfcp_data.config_sema);
	if (zfcp_adapter_enqueue(ccw_device)) {
		dev_err(&ccw_device->dev,
			"Setting up data structures for the "
			"FCP adapter failed\n");
		retval = -EINVAL;
	}
	up(&zfcp_data.config_sema);
	return retval;
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
	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

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
		list_for_each_entry_safe(unit, u, &unit_remove_lh, list) {
			if (unit->device)
				scsi_remove_device(unit->device);
			zfcp_unit_dequeue(unit);
		}
		zfcp_port_dequeue(port);
	}
	wait_event(adapter->remove_wq, atomic_read(&adapter->refcount) == 0);
	zfcp_adapter_dequeue(adapter);

	up(&zfcp_data.config_sema);
}

/**
 * zfcp_ccw_set_online - set_online function of zfcp driver
 * @ccw_device: pointer to belonging ccw device
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state online. Setting an fcp device online means that it will be
 * registered with the SCSI stack, that the QDIO queues will be set up
 * and that the adapter will be opened (asynchronously).
 */
static int zfcp_ccw_set_online(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int retval;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

	retval = zfcp_erp_thread_setup(adapter);
	if (retval)
		goto out;

	/* initialize request counter */
	BUG_ON(!zfcp_reqlist_isempty(adapter));
	adapter->req_no = 0;
	zfcp_fc_nameserver_init(adapter);

	zfcp_erp_modify_adapter_status(adapter, "ccsonl1", NULL,
				       ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"ccsonl2", NULL);
	zfcp_erp_wait(adapter);
	up(&zfcp_data.config_sema);
	flush_work(&adapter->scan_work);
	return 0;

 out:
	up(&zfcp_data.config_sema);
	return retval;
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

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);
	zfcp_erp_adapter_shutdown(adapter, 0, "ccsoff1", NULL);
	zfcp_erp_wait(adapter);
	zfcp_erp_thread_kill(adapter);
	up(&zfcp_data.config_sema);
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
		dev_warn(&adapter->ccw_device->dev,
			 "The ccw device did not respond in time.\n");
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

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&cdev->dev);
	zfcp_erp_adapter_shutdown(adapter, 0, "ccshut1", NULL);
	zfcp_erp_wait(adapter);
	up(&zfcp_data.config_sema);
}

static struct ccw_device_id zfcp_ccw_device_id[] = {
	{ CCW_DEVICE_DEVTYPE(0x1731, 0x3, 0x1732, 0x3) },
	{ CCW_DEVICE_DEVTYPE(0x1731, 0x3, 0x1732, 0x4) }, /* priv. */
	{},
};

MODULE_DEVICE_TABLE(ccw, zfcp_ccw_device_id);

static struct ccw_driver zfcp_ccw_driver = {
	.owner       = THIS_MODULE,
	.name        = "zfcp",
	.ids         = zfcp_ccw_device_id,
	.probe       = zfcp_ccw_probe,
	.remove      = zfcp_ccw_remove,
	.set_online  = zfcp_ccw_set_online,
	.set_offline = zfcp_ccw_set_offline,
	.notify      = zfcp_ccw_notify,
	.shutdown    = zfcp_ccw_shutdown,
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

/**
 * zfcp_get_adapter_by_busid - find zfcp_adapter struct
 * @busid: bus id string of zfcp adapter to find
 */
struct zfcp_adapter *zfcp_get_adapter_by_busid(char *busid)
{
	struct ccw_device *ccw_device;
	struct zfcp_adapter *adapter = NULL;

	ccw_device = get_ccwdev_by_busid(&zfcp_ccw_driver, busid);
	if (ccw_device) {
		adapter = dev_get_drvdata(&ccw_device->dev);
		put_device(&ccw_device->dev);
	}
	return adapter;
}
