/*
 * zfcp device driver
 *
 * Module interface and handling of zfcp data structures.
 *
 * Copyright IBM Corporation 2002, 2009
 */

/*
 * Driver authors:
 *            Martin Peschke (originator of the driver)
 *            Raimund Schroeder
 *            Aron Zeh
 *            Wolfgang Taphorn
 *            Stefan Bader
 *            Heiko Carstens (kernel 2.6 port of the driver)
 *            Andreas Herrmann
 *            Maxim Shchetynin
 *            Volker Sameske
 *            Ralph Wuerthner
 *            Michael Loehr
 *            Swen Schillig
 *            Christof Schmitt
 *            Martin Petermann
 *            Sven Schuetz
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/miscdevice.h>
#include <linux/seq_file.h>
#include "zfcp_ext.h"

#define ZFCP_BUS_ID_SIZE	20

MODULE_AUTHOR("IBM Deutschland Entwicklung GmbH - linux390@de.ibm.com");
MODULE_DESCRIPTION("FCP HBA driver");
MODULE_LICENSE("GPL");

static char *init_device;
module_param_named(device, init_device, charp, 0400);
MODULE_PARM_DESC(device, "specify initial device");

static struct kmem_cache *zfcp_cache_hw_align(const char *name,
					      unsigned long size)
{
	return kmem_cache_create(name, size, roundup_pow_of_two(size), 0, NULL);
}

static int zfcp_reqlist_alloc(struct zfcp_adapter *adapter)
{
	int idx;

	adapter->req_list = kcalloc(REQUEST_LIST_SIZE, sizeof(struct list_head),
				    GFP_KERNEL);
	if (!adapter->req_list)
		return -ENOMEM;

	for (idx = 0; idx < REQUEST_LIST_SIZE; idx++)
		INIT_LIST_HEAD(&adapter->req_list[idx]);
	return 0;
}

/**
 * zfcp_reqlist_isempty - is the request list empty
 * @adapter: pointer to struct zfcp_adapter
 *
 * Returns: true if list is empty, false otherwise
 */
int zfcp_reqlist_isempty(struct zfcp_adapter *adapter)
{
	unsigned int idx;

	for (idx = 0; idx < REQUEST_LIST_SIZE; idx++)
		if (!list_empty(&adapter->req_list[idx]))
			return 0;
	return 1;
}

static void __init zfcp_init_device_configure(char *busid, u64 wwpn, u64 lun)
{
	struct ccw_device *ccwdev;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	ccwdev = get_ccwdev_by_busid(&zfcp_ccw_driver, busid);
	if (!ccwdev)
		return;

	if (ccw_device_set_online(ccwdev))
		goto out_ccwdev;

	mutex_lock(&zfcp_data.config_mutex);
	adapter = dev_get_drvdata(&ccwdev->dev);
	if (!adapter)
		goto out_unlock;
	zfcp_adapter_get(adapter);

	port = zfcp_get_port_by_wwpn(adapter, wwpn);
	if (!port)
		goto out_port;

	zfcp_port_get(port);
	unit = zfcp_unit_enqueue(port, lun);
	if (IS_ERR(unit))
		goto out_unit;
	mutex_unlock(&zfcp_data.config_mutex);

	zfcp_erp_unit_reopen(unit, 0, "auidc_1", NULL);
	zfcp_erp_wait(adapter);
	flush_work(&unit->scsi_work);

	mutex_lock(&zfcp_data.config_mutex);
	zfcp_unit_put(unit);
out_unit:
	zfcp_port_put(port);
out_port:
	zfcp_adapter_put(adapter);
out_unlock:
	mutex_unlock(&zfcp_data.config_mutex);
out_ccwdev:
	put_device(&ccwdev->dev);
	return;
}

static void __init zfcp_init_device_setup(char *devstr)
{
	char *token;
	char *str;
	char busid[ZFCP_BUS_ID_SIZE];
	u64 wwpn, lun;

	/* duplicate devstr and keep the original for sysfs presentation*/
	str = kmalloc(strlen(devstr) + 1, GFP_KERNEL);
	if (!str)
		return;

	strcpy(str, devstr);

	token = strsep(&str, ",");
	if (!token || strlen(token) >= ZFCP_BUS_ID_SIZE)
		goto err_out;
	strncpy(busid, token, ZFCP_BUS_ID_SIZE);

	token = strsep(&str, ",");
	if (!token || strict_strtoull(token, 0, (unsigned long long *) &wwpn))
		goto err_out;

	token = strsep(&str, ",");
	if (!token || strict_strtoull(token, 0, (unsigned long long *) &lun))
		goto err_out;

	kfree(str);
	zfcp_init_device_configure(busid, wwpn, lun);
	return;

 err_out:
	kfree(str);
	pr_err("%s is not a valid SCSI device\n", devstr);
}

static int __init zfcp_module_init(void)
{
	int retval = -ENOMEM;

	zfcp_data.gpn_ft_cache = zfcp_cache_hw_align("zfcp_gpn",
					sizeof(struct ct_iu_gpn_ft_req));
	if (!zfcp_data.gpn_ft_cache)
		goto out;

	zfcp_data.qtcb_cache = zfcp_cache_hw_align("zfcp_qtcb",
					sizeof(struct fsf_qtcb));
	if (!zfcp_data.qtcb_cache)
		goto out_qtcb_cache;

	zfcp_data.sr_buffer_cache = zfcp_cache_hw_align("zfcp_sr",
					sizeof(struct fsf_status_read_buffer));
	if (!zfcp_data.sr_buffer_cache)
		goto out_sr_cache;

	zfcp_data.gid_pn_cache = zfcp_cache_hw_align("zfcp_gid",
					sizeof(struct zfcp_gid_pn_data));
	if (!zfcp_data.gid_pn_cache)
		goto out_gid_cache;

	mutex_init(&zfcp_data.config_mutex);
	rwlock_init(&zfcp_data.config_lock);

	zfcp_data.scsi_transport_template =
		fc_attach_transport(&zfcp_transport_functions);
	if (!zfcp_data.scsi_transport_template)
		goto out_transport;

	retval = misc_register(&zfcp_cfdc_misc);
	if (retval) {
		pr_err("Registering the misc device zfcp_cfdc failed\n");
		goto out_misc;
	}

	retval = zfcp_ccw_register();
	if (retval) {
		pr_err("The zfcp device driver could not register with "
		       "the common I/O layer\n");
		goto out_ccw_register;
	}

	if (init_device)
		zfcp_init_device_setup(init_device);
	return 0;

out_ccw_register:
	misc_deregister(&zfcp_cfdc_misc);
out_misc:
	fc_release_transport(zfcp_data.scsi_transport_template);
out_transport:
	kmem_cache_destroy(zfcp_data.gid_pn_cache);
out_gid_cache:
	kmem_cache_destroy(zfcp_data.sr_buffer_cache);
out_sr_cache:
	kmem_cache_destroy(zfcp_data.qtcb_cache);
out_qtcb_cache:
	kmem_cache_destroy(zfcp_data.gpn_ft_cache);
out:
	return retval;
}

module_init(zfcp_module_init);

/**
 * zfcp_get_unit_by_lun - find unit in unit list of port by FCP LUN
 * @port: pointer to port to search for unit
 * @fcp_lun: FCP LUN to search for
 *
 * Returns: pointer to zfcp_unit or NULL
 */
struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	list_for_each_entry(unit, &port->unit_list_head, list)
		if ((unit->fcp_lun == fcp_lun) &&
		    !(atomic_read(&unit->status) & ZFCP_STATUS_COMMON_REMOVE))
		    return unit;
	return NULL;
}

/**
 * zfcp_get_port_by_wwpn - find port in port list of adapter by wwpn
 * @adapter: pointer to adapter to search for port
 * @wwpn: wwpn to search for
 *
 * Returns: pointer to zfcp_port or NULL
 */
struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *adapter,
					u64 wwpn)
{
	struct zfcp_port *port;

	list_for_each_entry(port, &adapter->port_list_head, list)
		if ((port->wwpn == wwpn) &&
		    !(atomic_read(&port->status) & ZFCP_STATUS_COMMON_REMOVE))
			return port;
	return NULL;
}

static void zfcp_sysfs_unit_release(struct device *dev)
{
	kfree(container_of(dev, struct zfcp_unit, sysfs_device));
}

/**
 * zfcp_unit_enqueue - enqueue unit to unit list of a port.
 * @port: pointer to port where unit is added
 * @fcp_lun: FCP LUN of unit to be enqueued
 * Returns: pointer to enqueued unit on success, ERR_PTR on error
 * Locks: config_mutex must be held to serialize changes to the unit list
 *
 * Sets up some unit internal structures and creates sysfs entry.
 */
struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	read_lock_irq(&zfcp_data.config_lock);
	if (zfcp_get_unit_by_lun(port, fcp_lun)) {
		read_unlock_irq(&zfcp_data.config_lock);
		return ERR_PTR(-EINVAL);
	}
	read_unlock_irq(&zfcp_data.config_lock);

	unit = kzalloc(sizeof(struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return ERR_PTR(-ENOMEM);

	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);
	INIT_WORK(&unit->scsi_work, zfcp_scsi_scan);

	unit->port = port;
	unit->fcp_lun = fcp_lun;

	if (dev_set_name(&unit->sysfs_device, "0x%016llx",
			 (unsigned long long) fcp_lun)) {
		kfree(unit);
		return ERR_PTR(-ENOMEM);
	}
	unit->sysfs_device.parent = &port->sysfs_device;
	unit->sysfs_device.release = zfcp_sysfs_unit_release;
	dev_set_drvdata(&unit->sysfs_device, unit);

	/* mark unit unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);

	spin_lock_init(&unit->latencies.lock);
	unit->latencies.write.channel.min = 0xFFFFFFFF;
	unit->latencies.write.fabric.min = 0xFFFFFFFF;
	unit->latencies.read.channel.min = 0xFFFFFFFF;
	unit->latencies.read.fabric.min = 0xFFFFFFFF;
	unit->latencies.cmd.channel.min = 0xFFFFFFFF;
	unit->latencies.cmd.fabric.min = 0xFFFFFFFF;

	if (device_register(&unit->sysfs_device)) {
		put_device(&unit->sysfs_device);
		return ERR_PTR(-EINVAL);
	}

	if (sysfs_create_group(&unit->sysfs_device.kobj,
			       &zfcp_sysfs_unit_attrs)) {
		device_unregister(&unit->sysfs_device);
		return ERR_PTR(-EINVAL);
	}

	zfcp_unit_get(unit);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&unit->list, &port->unit_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);

	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_port_get(port);

	return unit;
}

/**
 * zfcp_unit_dequeue - dequeue unit
 * @unit: pointer to zfcp_unit
 *
 * waits until all work is done on unit and removes it then from the unit->list
 * of the associated port.
 */
void zfcp_unit_dequeue(struct zfcp_unit *unit)
{
	wait_event(unit->remove_wq, atomic_read(&unit->refcount) == 0);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&unit->list);
	write_unlock_irq(&zfcp_data.config_lock);
	zfcp_port_put(unit->port);
	sysfs_remove_group(&unit->sysfs_device.kobj, &zfcp_sysfs_unit_attrs);
	device_unregister(&unit->sysfs_device);
}

static int zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
	/* must only be called with zfcp_data.config_mutex taken */
	adapter->pool.erp_req =
		mempool_create_kmalloc_pool(1, sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.erp_req)
		return -ENOMEM;

	adapter->pool.gid_pn_req =
		mempool_create_kmalloc_pool(1, sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.gid_pn_req)
		return -ENOMEM;

	adapter->pool.scsi_req =
		mempool_create_kmalloc_pool(1, sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.scsi_req)
		return -ENOMEM;

	adapter->pool.scsi_abort =
		mempool_create_kmalloc_pool(1, sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.scsi_abort)
		return -ENOMEM;

	adapter->pool.status_read_req =
		mempool_create_kmalloc_pool(FSF_STATUS_READS_RECOM,
					    sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.status_read_req)
		return -ENOMEM;

	adapter->pool.qtcb_pool =
		mempool_create_slab_pool(4, zfcp_data.qtcb_cache);
	if (!adapter->pool.qtcb_pool)
		return -ENOMEM;

	adapter->pool.status_read_data =
		mempool_create_slab_pool(FSF_STATUS_READS_RECOM,
					 zfcp_data.sr_buffer_cache);
	if (!adapter->pool.status_read_data)
		return -ENOMEM;

	adapter->pool.gid_pn_data =
		mempool_create_slab_pool(1, zfcp_data.gid_pn_cache);
	if (!adapter->pool.gid_pn_data)
		return -ENOMEM;

	return 0;
}

static void zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
	/* zfcp_data.config_mutex must be held */
	if (adapter->pool.erp_req)
		mempool_destroy(adapter->pool.erp_req);
	if (adapter->pool.scsi_req)
		mempool_destroy(adapter->pool.scsi_req);
	if (adapter->pool.scsi_abort)
		mempool_destroy(adapter->pool.scsi_abort);
	if (adapter->pool.qtcb_pool)
		mempool_destroy(adapter->pool.qtcb_pool);
	if (adapter->pool.status_read_req)
		mempool_destroy(adapter->pool.status_read_req);
	if (adapter->pool.status_read_data)
		mempool_destroy(adapter->pool.status_read_data);
	if (adapter->pool.gid_pn_data)
		mempool_destroy(adapter->pool.gid_pn_data);
}

/**
 * zfcp_status_read_refill - refill the long running status_read_requests
 * @adapter: ptr to struct zfcp_adapter for which the buffers should be refilled
 *
 * Returns: 0 on success, 1 otherwise
 *
 * if there are 16 or more status_read requests missing an adapter_reopen
 * is triggered
 */
int zfcp_status_read_refill(struct zfcp_adapter *adapter)
{
	while (atomic_read(&adapter->stat_miss) > 0)
		if (zfcp_fsf_status_read(adapter->qdio)) {
			if (atomic_read(&adapter->stat_miss) >= 16) {
				zfcp_erp_adapter_reopen(adapter, 0, "axsref1",
							NULL);
				return 1;
			}
			break;
		} else
			atomic_dec(&adapter->stat_miss);
	return 0;
}

static void _zfcp_status_read_scheduler(struct work_struct *work)
{
	zfcp_status_read_refill(container_of(work, struct zfcp_adapter,
					     stat_work));
}

static void zfcp_print_sl(struct seq_file *m, struct service_level *sl)
{
	struct zfcp_adapter *adapter =
		container_of(sl, struct zfcp_adapter, service_level);

	seq_printf(m, "zfcp: %s microcode level %x\n",
		   dev_name(&adapter->ccw_device->dev),
		   adapter->fsf_lic_version);
}

static int zfcp_setup_adapter_work_queue(struct zfcp_adapter *adapter)
{
	char name[TASK_COMM_LEN];

	snprintf(name, sizeof(name), "zfcp_q_%s",
		 dev_name(&adapter->ccw_device->dev));
	adapter->work_queue = create_singlethread_workqueue(name);

	if (adapter->work_queue)
		return 0;
	return -ENOMEM;
}

static void zfcp_destroy_adapter_work_queue(struct zfcp_adapter *adapter)
{
	if (adapter->work_queue)
		destroy_workqueue(adapter->work_queue);
	adapter->work_queue = NULL;

}

/**
 * zfcp_adapter_enqueue - enqueue a new adapter to the list
 * @ccw_device: pointer to the struct cc_device
 *
 * Returns:	0             if a new adapter was successfully enqueued
 *		-ENOMEM       if alloc failed
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 * locks: config_mutex must be held to serialize changes to the adapter list
 */
int zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	/*
	 * Note: It is safe to release the list_lock, as any list changes
	 * are protected by the config_mutex, which must be held to get here
	 */

	adapter = kzalloc(sizeof(struct zfcp_adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	ccw_device->handler = NULL;
	adapter->ccw_device = ccw_device;
	atomic_set(&adapter->refcount, 0);

	if (zfcp_qdio_setup(adapter))
		goto qdio_failed;

	if (zfcp_allocate_low_mem_buffers(adapter))
		goto low_mem_buffers_failed;

	if (zfcp_reqlist_alloc(adapter))
		goto low_mem_buffers_failed;

	if (zfcp_dbf_adapter_register(adapter))
		goto debug_register_failed;

	if (zfcp_setup_adapter_work_queue(adapter))
		goto work_queue_failed;

	if (zfcp_fc_gs_setup(adapter))
		goto generic_services_failed;

	init_waitqueue_head(&adapter->remove_wq);
	init_waitqueue_head(&adapter->erp_ready_wq);
	init_waitqueue_head(&adapter->erp_done_wqh);

	INIT_LIST_HEAD(&adapter->port_list_head);
	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);

	spin_lock_init(&adapter->req_list_lock);

	rwlock_init(&adapter->erp_lock);
	rwlock_init(&adapter->abort_lock);

	if (zfcp_erp_thread_setup(adapter))
		goto erp_thread_failed;

	INIT_WORK(&adapter->stat_work, _zfcp_status_read_scheduler);
	INIT_WORK(&adapter->scan_work, _zfcp_fc_scan_ports_later);

	adapter->service_level.seq_print = zfcp_print_sl;

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	dev_set_drvdata(&ccw_device->dev, adapter);

	if (sysfs_create_group(&ccw_device->dev.kobj,
			       &zfcp_sysfs_adapter_attrs))
		goto sysfs_failed;

	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	if (!zfcp_adapter_scsi_register(adapter))
		return 0;

sysfs_failed:
	zfcp_erp_thread_kill(adapter);
erp_thread_failed:
	zfcp_fc_gs_destroy(adapter);
generic_services_failed:
	zfcp_destroy_adapter_work_queue(adapter);
work_queue_failed:
	zfcp_dbf_adapter_unregister(adapter->dbf);
debug_register_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
	kfree(adapter->req_list);
low_mem_buffers_failed:
	zfcp_free_low_mem_buffers(adapter);
qdio_failed:
	zfcp_qdio_destroy(adapter->qdio);
	kfree(adapter);
	return -ENOMEM;
}

/**
 * zfcp_adapter_dequeue - remove the adapter from the resource list
 * @adapter: pointer to struct zfcp_adapter which should be removed
 * locks:	adapter list write lock is assumed to be held by caller
 */
void zfcp_adapter_dequeue(struct zfcp_adapter *adapter)
{
	int retval = 0;
	unsigned long flags;

	cancel_work_sync(&adapter->stat_work);
	zfcp_fc_wka_ports_force_offline(adapter->gs);
	sysfs_remove_group(&adapter->ccw_device->dev.kobj,
			   &zfcp_sysfs_adapter_attrs);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	retval = zfcp_reqlist_isempty(adapter);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);
	if (!retval)
		return;

	zfcp_fc_gs_destroy(adapter);
	zfcp_erp_thread_kill(adapter);
	zfcp_destroy_adapter_work_queue(adapter);
	zfcp_dbf_adapter_unregister(adapter->dbf);
	zfcp_free_low_mem_buffers(adapter);
	zfcp_qdio_destroy(adapter->qdio);
	kfree(adapter->req_list);
	kfree(adapter->fc_stats);
	kfree(adapter->stats_reset_data);
	kfree(adapter);
}

static void zfcp_sysfs_port_release(struct device *dev)
{
	kfree(container_of(dev, struct zfcp_port, sysfs_device));
}

/**
 * zfcp_port_enqueue - enqueue port to port list of adapter
 * @adapter: adapter where remote port is added
 * @wwpn: WWPN of the remote port to be enqueued
 * @status: initial status for the port
 * @d_id: destination id of the remote port to be enqueued
 * Returns: pointer to enqueued port on success, ERR_PTR on error
 * Locks: config_mutex must be held to serialize changes to the port list
 *
 * All port internal structures are set up and the sysfs entry is generated.
 * d_id is used to enqueue ports with a well known address like the Directory
 * Service for nameserver lookup.
 */
struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *adapter, u64 wwpn,
				     u32 status, u32 d_id)
{
	struct zfcp_port *port;

	read_lock_irq(&zfcp_data.config_lock);
	if (zfcp_get_port_by_wwpn(adapter, wwpn)) {
		read_unlock_irq(&zfcp_data.config_lock);
		return ERR_PTR(-EINVAL);
	}
	read_unlock_irq(&zfcp_data.config_lock);

	port = kzalloc(sizeof(struct zfcp_port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&port->remove_wq);
	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_WORK(&port->gid_pn_work, zfcp_fc_port_did_lookup);
	INIT_WORK(&port->test_link_work, zfcp_fc_link_test_work);
	INIT_WORK(&port->rport_work, zfcp_scsi_rport_work);

	port->adapter = adapter;
	port->d_id = d_id;
	port->wwpn = wwpn;
	port->rport_task = RPORT_NONE;

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(status | ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set(&port->refcount, 0);

	if (dev_set_name(&port->sysfs_device, "0x%016llx",
			 (unsigned long long)wwpn)) {
		kfree(port);
		return ERR_PTR(-ENOMEM);
	}
	port->sysfs_device.parent = &adapter->ccw_device->dev;
	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	if (device_register(&port->sysfs_device)) {
		put_device(&port->sysfs_device);
		return ERR_PTR(-EINVAL);
	}

	if (sysfs_create_group(&port->sysfs_device.kobj,
			       &zfcp_sysfs_port_attrs)) {
		device_unregister(&port->sysfs_device);
		return ERR_PTR(-EINVAL);
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&port->list, &adapter->port_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);

	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_adapter_get(adapter);
	return port;
}

/**
 * zfcp_port_dequeue - dequeues a port from the port list of the adapter
 * @port: pointer to struct zfcp_port which should be removed
 */
void zfcp_port_dequeue(struct zfcp_port *port)
{
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	write_unlock_irq(&zfcp_data.config_lock);
	wait_event(port->remove_wq, atomic_read(&port->refcount) == 0);
	cancel_work_sync(&port->rport_work); /* usually not necessary */
	zfcp_adapter_put(port->adapter);
	sysfs_remove_group(&port->sysfs_device.kobj, &zfcp_sysfs_port_attrs);
	device_unregister(&port->sysfs_device);
}

/**
 * zfcp_sg_free_table - free memory used by scatterlists
 * @sg: pointer to scatterlist
 * @count: number of scatterlist which are to be free'ed
 * the scatterlist are expected to reference pages always
 */
void zfcp_sg_free_table(struct scatterlist *sg, int count)
{
	int i;

	for (i = 0; i < count; i++, sg++)
		if (sg)
			free_page((unsigned long) sg_virt(sg));
		else
			break;
}

/**
 * zfcp_sg_setup_table - init scatterlist and allocate, assign buffers
 * @sg: pointer to struct scatterlist
 * @count: number of scatterlists which should be assigned with buffers
 * of size page
 *
 * Returns: 0 on success, -ENOMEM otherwise
 */
int zfcp_sg_setup_table(struct scatterlist *sg, int count)
{
	void *addr;
	int i;

	sg_init_table(sg, count);
	for (i = 0; i < count; i++, sg++) {
		addr = (void *) get_zeroed_page(GFP_KERNEL);
		if (!addr) {
			zfcp_sg_free_table(sg, i);
			return -ENOMEM;
		}
		sg_set_buf(sg, addr, PAGE_SIZE);
	}
	return 0;
}
