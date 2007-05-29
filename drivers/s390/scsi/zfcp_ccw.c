/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "zfcp_ext.h"

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_CONFIG

static int zfcp_ccw_probe(struct ccw_device *);
static void zfcp_ccw_remove(struct ccw_device *);
static int zfcp_ccw_set_online(struct ccw_device *);
static int zfcp_ccw_set_offline(struct ccw_device *);
static int zfcp_ccw_notify(struct ccw_device *, int);
static void zfcp_ccw_shutdown(struct device *);

static struct ccw_device_id zfcp_ccw_device_id[] = {
	{CCW_DEVICE_DEVTYPE(ZFCP_CONTROL_UNIT_TYPE,
			    ZFCP_CONTROL_UNIT_MODEL,
			    ZFCP_DEVICE_TYPE,
			    ZFCP_DEVICE_MODEL)},
	{CCW_DEVICE_DEVTYPE(ZFCP_CONTROL_UNIT_TYPE,
			    ZFCP_CONTROL_UNIT_MODEL,
			    ZFCP_DEVICE_TYPE,
			    ZFCP_DEVICE_MODEL_PRIV)},
	{},
};

static struct ccw_driver zfcp_ccw_driver = {
	.owner       = THIS_MODULE,
	.name        = ZFCP_NAME,
	.ids         = zfcp_ccw_device_id,
	.probe       = zfcp_ccw_probe,
	.remove      = zfcp_ccw_remove,
	.set_online  = zfcp_ccw_set_online,
	.set_offline = zfcp_ccw_set_offline,
	.notify      = zfcp_ccw_notify,
	.driver      = {
		.shutdown = zfcp_ccw_shutdown,
	},
};

MODULE_DEVICE_TABLE(ccw, zfcp_ccw_device_id);

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
static int
zfcp_ccw_probe(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int retval = 0;

	down(&zfcp_data.config_sema);
	adapter = zfcp_adapter_enqueue(ccw_device);
	if (!adapter)
		retval = -EINVAL;
	else
		ZFCP_LOG_DEBUG("Probed adapter %s\n",
			       zfcp_get_busid_by_adapter(adapter));
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
static void
zfcp_ccw_remove(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port, *p;
	struct zfcp_unit *unit, *u;

	ccw_device_set_offline(ccw_device);
	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

	ZFCP_LOG_DEBUG("Removing adapter %s\n",
		       zfcp_get_busid_by_adapter(adapter));
	write_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry_safe(port, p, &adapter->port_list_head, list) {
		list_for_each_entry_safe(unit, u, &port->unit_list_head, list) {
			list_move(&unit->list, &port->unit_remove_lh);
			atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE,
					&unit->status);
		}
		list_move(&port->list, &adapter->port_remove_lh);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	}
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	write_unlock_irq(&zfcp_data.config_lock);

	list_for_each_entry_safe(port, p, &adapter->port_remove_lh, list) {
		list_for_each_entry_safe(unit, u, &port->unit_remove_lh, list) {
			zfcp_unit_dequeue(unit);
		}
		zfcp_port_dequeue(port);
	}
	zfcp_adapter_wait(adapter);
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
static int
zfcp_ccw_set_online(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;
	int retval;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);

	retval = zfcp_adapter_debug_register(adapter);
	if (retval)
		goto out;
	retval = zfcp_erp_thread_setup(adapter);
	if (retval) {
		ZFCP_LOG_INFO("error: start of error recovery thread for "
			      "adapter %s failed\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto out_erp_thread;
	}

	retval = zfcp_adapter_scsi_register(adapter);
	if (retval)
		goto out_scsi_register;

	/* initialize request counter */
	BUG_ON(!zfcp_reqlist_isempty(adapter));
	adapter->req_no = 0;

	zfcp_erp_modify_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING,
				       ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
	zfcp_erp_wait(adapter);
	goto out;

 out_scsi_register:
	zfcp_erp_thread_kill(adapter);
 out_erp_thread:
	zfcp_adapter_debug_unregister(adapter);
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
static int
zfcp_ccw_set_offline(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);
	zfcp_erp_adapter_shutdown(adapter, 0);
	zfcp_erp_wait(adapter);
	zfcp_erp_thread_kill(adapter);
	zfcp_adapter_debug_unregister(adapter);
	up(&zfcp_data.config_sema);
	return 0;
}

/**
 * zfcp_ccw_notify
 * @ccw_device: pointer to belonging ccw device
 * @event: indicates if adapter was detached or attached
 *
 * This function gets called by the common i/o layer if an adapter has gone
 * or reappeared.
 */
static int
zfcp_ccw_notify(struct ccw_device *ccw_device, int event)
{
	struct zfcp_adapter *adapter;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(&ccw_device->dev);
	switch (event) {
	case CIO_GONE:
		ZFCP_LOG_NORMAL("adapter %s: device gone\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf,1,"dev_gone");
		zfcp_erp_adapter_shutdown(adapter, 0);
		break;
	case CIO_NO_PATH:
		ZFCP_LOG_NORMAL("adapter %s: no path\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf,1,"no_path");
		zfcp_erp_adapter_shutdown(adapter, 0);
		break;
	case CIO_OPER:
		ZFCP_LOG_NORMAL("adapter %s: operational again\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf,1,"dev_oper");
		zfcp_erp_modify_adapter_status(adapter,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_COMMON_ERP_FAILED);
		break;
	}
	zfcp_erp_wait(adapter);
	up(&zfcp_data.config_sema);
	return 1;
}

/**
 * zfcp_ccw_register - ccw register function
 *
 * Registers the driver at the common i/o layer. This function will be called
 * at module load time/system start.
 */
int __init
zfcp_ccw_register(void)
{
	int retval;

	retval = ccw_driver_register(&zfcp_ccw_driver);
	if (retval)
		goto out;
	retval = zfcp_sysfs_driver_create_files(&zfcp_ccw_driver.driver);
	if (retval)
		ccw_driver_unregister(&zfcp_ccw_driver);
 out:
	return retval;
}

/**
 * zfcp_ccw_shutdown - gets called on reboot/shutdown
 *
 * Makes sure that QDIO queues are down when the system gets stopped.
 */
static void
zfcp_ccw_shutdown(struct device *dev)
{
	struct zfcp_adapter *adapter;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(dev);
	zfcp_erp_adapter_shutdown(adapter, 0);
	zfcp_erp_wait(adapter);
	up(&zfcp_data.config_sema);
}

#undef ZFCP_LOG_AREA
