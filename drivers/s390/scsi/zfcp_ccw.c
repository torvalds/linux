// SPDX-License-Identifier: GPL-2.0
/*
 * zfcp device driver
 *
 * Registration and callback for the s390 common I/O layer.
 *
 * Copyright IBM Corp. 2002, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
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

/**
 * zfcp_ccw_activate - activate adapter and wait for it to finish
 * @cdev: pointer to belonging ccw device
 * @clear: Status flags to clear.
 * @tag: s390dbf trace record tag
 */
static int zfcp_ccw_activate(struct ccw_device *cdev, int clear, char *tag)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return 0;

	zfcp_erp_clear_adapter_status(adapter, clear);
	zfcp_erp_set_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				tag);

	/*
	 * We want to scan ports here, with some random backoff and without
	 * rate limit. Recovery has already scheduled a port scan for us,
	 * but with both random delay and rate limit. Nevertheless we get
	 * what we want here by flushing the scheduled work after sleeping
	 * an equivalent random time.
	 * Let the port scan random delay elapse first. If recovery finishes
	 * up to that point in time, that would be perfect for both recovery
	 * and port scan. If not, i.e. recovery takes ages, there was no
	 * point in waiting a random delay on top of the time consumed by
	 * recovery.
	 */
	msleep(zfcp_fc_port_scan_backoff());
	zfcp_erp_wait(adapter);
	flush_delayed_work(&adapter->scan_work);

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
	list_for_each_entry(port, &adapter->port_list, list) {
		write_lock(&port->unit_list_lock);
		list_splice_init(&port->unit_list, &unit_remove_lh);
		write_unlock(&port->unit_list_lock);
	}
	list_splice_init(&adapter->port_list, &port_remove_lh);
	write_unlock_irq(&adapter->port_list_lock);
	zfcp_ccw_adapter_put(adapter); /* put from zfcp_ccw_adapter_by_cdev */

	list_for_each_entry_safe(unit, u, &unit_remove_lh, list)
		device_unregister(&unit->dev);

	list_for_each_entry_safe(port, p, &port_remove_lh, list)
		device_unregister(&port->dev);

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

	zfcp_ccw_activate(cdev, 0, "ccsonl1");

	/*
	 * We want to scan ports here, always, with some random delay and
	 * without rate limit - basically what zfcp_ccw_activate() has
	 * achieved for us. Not quite! That port scan depended on
	 * !no_auto_port_rescan. So let's cover the no_auto_port_rescan
	 * case here to make sure a port scan is done unconditionally.
	 * Since zfcp_ccw_activate() has waited the desired random time,
	 * we can immediately schedule and flush a port scan for the
	 * remaining cases.
	 */
	zfcp_fc_inverse_conditional_port_scan(adapter);
	flush_delayed_work(&adapter->scan_work);
	zfcp_ccw_adapter_put(adapter);
	return 0;
}

/**
 * zfcp_ccw_offline_sync - shut down adapter and wait for it to finish
 * @cdev: pointer to belonging ccw device
 * @set: Status flags to set.
 * @tag: s390dbf trace record tag
 *
 * This function gets called by the common i/o layer and sets an adapter
 * into state offline.
 */
static int zfcp_ccw_offline_sync(struct ccw_device *cdev, int set, char *tag)
{
	struct zfcp_adapter *adapter = zfcp_ccw_adapter_by_cdev(cdev);

	if (!adapter)
		return 0;

	zfcp_erp_set_adapter_status(adapter, set);
	zfcp_erp_adapter_shutdown(adapter, 0, tag);
	zfcp_erp_wait(adapter);

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
	return zfcp_ccw_offline_sync(cdev, 0, "ccsoff1");
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
		if (atomic_read(&adapter->status) &
		    ZFCP_STATUS_ADAPTER_SUSPENDED) { /* notification ignore */
			zfcp_dbf_hba_basic("ccnigo1", adapter);
			break;
		}
		dev_warn(&cdev->dev, "The FCP device has been detached\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti1");
		break;
	case CIO_NO_PATH:
		dev_warn(&cdev->dev,
			 "The CHPID for the FCP device is offline\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "ccnoti2");
		break;
	case CIO_OPER:
		if (atomic_read(&adapter->status) &
		    ZFCP_STATUS_ADAPTER_SUSPENDED) { /* notification ignore */
			zfcp_dbf_hba_basic("ccniop1", adapter);
			break;
		}
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

static int zfcp_ccw_suspend(struct ccw_device *cdev)
{
	zfcp_ccw_offline_sync(cdev, ZFCP_STATUS_ADAPTER_SUSPENDED, "ccsusp1");
	return 0;
}

static int zfcp_ccw_thaw(struct ccw_device *cdev)
{
	/* trace records for thaw and final shutdown during suspend
	   can only be found in system dump until the end of suspend
	   but not after resume because it's based on the memory image
	   right after the very first suspend (freeze) callback */
	zfcp_ccw_activate(cdev, 0, "ccthaw1");
	return 0;
}

static int zfcp_ccw_resume(struct ccw_device *cdev)
{
	zfcp_ccw_activate(cdev, ZFCP_STATUS_ADAPTER_SUSPENDED, "ccresu1");
	return 0;
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
	.freeze      = zfcp_ccw_suspend,
	.thaw	     = zfcp_ccw_thaw,
	.restore     = zfcp_ccw_resume,
};
