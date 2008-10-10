/*
 * zfcp device driver
 *
 * Module interface and handling of zfcp data structures.
 *
 * Copyright IBM Corporation 2002, 2008
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

#include <linux/miscdevice.h>
#include "zfcp_ext.h"

static char *device;

MODULE_AUTHOR("IBM Deutschland Entwicklung GmbH - linux390@de.ibm.com");
MODULE_DESCRIPTION("FCP HBA driver");
MODULE_LICENSE("GPL");

module_param(device, charp, 0400);
MODULE_PARM_DESC(device, "specify initial device");

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

static int __init zfcp_device_setup(char *devstr)
{
	char *token;
	char *str;

	if (!devstr)
		return 0;

	/* duplicate devstr and keep the original for sysfs presentation*/
	str = kmalloc(strlen(devstr) + 1, GFP_KERNEL);
	if (!str)
		return 0;

	strcpy(str, devstr);

	token = strsep(&str, ",");
	if (!token || strlen(token) >= BUS_ID_SIZE)
		goto err_out;
	strncpy(zfcp_data.init_busid, token, BUS_ID_SIZE);

	token = strsep(&str, ",");
	if (!token || strict_strtoull(token, 0,
				(unsigned long long *) &zfcp_data.init_wwpn))
		goto err_out;

	token = strsep(&str, ",");
	if (!token || strict_strtoull(token, 0,
				(unsigned long long *) &zfcp_data.init_fcp_lun))
		goto err_out;

	kfree(str);
	return 1;

 err_out:
	kfree(str);
	pr_err("zfcp: %s is not a valid SCSI device\n", devstr);
	return 0;
}

static void __init zfcp_init_device_configure(void)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	down(&zfcp_data.config_sema);
	read_lock_irq(&zfcp_data.config_lock);
	adapter = zfcp_get_adapter_by_busid(zfcp_data.init_busid);
	if (adapter)
		zfcp_adapter_get(adapter);
	read_unlock_irq(&zfcp_data.config_lock);

	if (!adapter)
		goto out_adapter;
	port = zfcp_port_enqueue(adapter, zfcp_data.init_wwpn, 0, 0);
	if (IS_ERR(port))
		goto out_port;
	unit = zfcp_unit_enqueue(port, zfcp_data.init_fcp_lun);
	if (IS_ERR(unit))
		goto out_unit;
	up(&zfcp_data.config_sema);
	ccw_device_set_online(adapter->ccw_device);

	zfcp_erp_wait(adapter);
	wait_event(adapter->erp_done_wqh,
		   !(atomic_read(&unit->status) &
				ZFCP_STATUS_UNIT_SCSI_WORK_PENDING));

	down(&zfcp_data.config_sema);
	zfcp_unit_put(unit);
out_unit:
	zfcp_port_put(port);
out_port:
	zfcp_adapter_put(adapter);
out_adapter:
	up(&zfcp_data.config_sema);
	return;
}

static struct kmem_cache *zfcp_cache_create(int size, char *name)
{
	int align = 1;
	while ((size - align) > 0)
		align <<= 1;
	return kmem_cache_create(name , size, align, 0, NULL);
}

static int __init zfcp_module_init(void)
{
	int retval = -ENOMEM;

	zfcp_data.fsf_req_qtcb_cache = zfcp_cache_create(
			sizeof(struct zfcp_fsf_req_qtcb), "zfcp_fsf");
	if (!zfcp_data.fsf_req_qtcb_cache)
		goto out;

	zfcp_data.sr_buffer_cache = zfcp_cache_create(
			sizeof(struct fsf_status_read_buffer), "zfcp_sr");
	if (!zfcp_data.sr_buffer_cache)
		goto out_sr_cache;

	zfcp_data.gid_pn_cache = zfcp_cache_create(
			sizeof(struct zfcp_gid_pn_data), "zfcp_gid");
	if (!zfcp_data.gid_pn_cache)
		goto out_gid_cache;

	zfcp_data.work_queue = create_singlethread_workqueue("zfcp_wq");

	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);
	sema_init(&zfcp_data.config_sema, 1);
	rwlock_init(&zfcp_data.config_lock);

	zfcp_data.scsi_transport_template =
		fc_attach_transport(&zfcp_transport_functions);
	if (!zfcp_data.scsi_transport_template)
		goto out_transport;

	retval = misc_register(&zfcp_cfdc_misc);
	if (retval) {
		pr_err("zfcp: Registering the misc device zfcp_cfdc failed\n");
		goto out_misc;
	}

	retval = zfcp_ccw_register();
	if (retval) {
		pr_err("zfcp: The zfcp device driver could not register with "
		       "the common I/O layer\n");
		goto out_ccw_register;
	}

	if (zfcp_device_setup(device))
		zfcp_init_device_configure();

	goto out;

out_ccw_register:
	misc_deregister(&zfcp_cfdc_misc);
out_misc:
	fc_release_transport(zfcp_data.scsi_transport_template);
out_transport:
	kmem_cache_destroy(zfcp_data.gid_pn_cache);
out_gid_cache:
	kmem_cache_destroy(zfcp_data.sr_buffer_cache);
out_sr_cache:
	kmem_cache_destroy(zfcp_data.fsf_req_qtcb_cache);
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
		if ((port->wwpn == wwpn) && !(atomic_read(&port->status) &
		      (ZFCP_STATUS_PORT_NO_WWPN | ZFCP_STATUS_COMMON_REMOVE)))
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
 * Locks: config_sema must be held to serialize changes to the unit list
 *
 * Sets up some unit internal structures and creates sysfs entry.
 */
struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	unit = kzalloc(sizeof(struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return ERR_PTR(-ENOMEM);

	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);

	unit->port = port;
	unit->fcp_lun = fcp_lun;

	dev_set_name(&unit->sysfs_device, "0x%016llx",
		     (unsigned long long) fcp_lun);
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

	read_lock_irq(&zfcp_data.config_lock);
	if (zfcp_get_unit_by_lun(port, fcp_lun)) {
		read_unlock_irq(&zfcp_data.config_lock);
		goto err_out_free;
	}
	read_unlock_irq(&zfcp_data.config_lock);

	if (device_register(&unit->sysfs_device))
		goto err_out_free;

	if (sysfs_create_group(&unit->sysfs_device.kobj,
			       &zfcp_sysfs_unit_attrs)) {
		device_unregister(&unit->sysfs_device);
		return ERR_PTR(-EIO);
	}

	zfcp_unit_get(unit);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&unit->list, &port->unit_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);

	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_port_get(port);

	return unit;

err_out_free:
	kfree(unit);
	return ERR_PTR(-EINVAL);
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
	/* must only be called with zfcp_data.config_sema taken */
	adapter->pool.fsf_req_erp =
		mempool_create_slab_pool(1, zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_erp)
		return -ENOMEM;

	adapter->pool.fsf_req_scsi =
		mempool_create_slab_pool(1, zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_scsi)
		return -ENOMEM;

	adapter->pool.fsf_req_abort =
		mempool_create_slab_pool(1, zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_abort)
		return -ENOMEM;

	adapter->pool.fsf_req_status_read =
		mempool_create_kmalloc_pool(FSF_STATUS_READS_RECOM,
					    sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.fsf_req_status_read)
		return -ENOMEM;

	adapter->pool.data_status_read =
		mempool_create_slab_pool(FSF_STATUS_READS_RECOM,
					 zfcp_data.sr_buffer_cache);
	if (!adapter->pool.data_status_read)
		return -ENOMEM;

	adapter->pool.data_gid_pn =
		mempool_create_slab_pool(1, zfcp_data.gid_pn_cache);
	if (!adapter->pool.data_gid_pn)
		return -ENOMEM;

	return 0;
}

static void zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
	/* zfcp_data.config_sema must be held */
	if (adapter->pool.fsf_req_erp)
		mempool_destroy(adapter->pool.fsf_req_erp);
	if (adapter->pool.fsf_req_scsi)
		mempool_destroy(adapter->pool.fsf_req_scsi);
	if (adapter->pool.fsf_req_abort)
		mempool_destroy(adapter->pool.fsf_req_abort);
	if (adapter->pool.fsf_req_status_read)
		mempool_destroy(adapter->pool.fsf_req_status_read);
	if (adapter->pool.data_status_read)
		mempool_destroy(adapter->pool.data_status_read);
	if (adapter->pool.data_gid_pn)
		mempool_destroy(adapter->pool.data_gid_pn);
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
		if (zfcp_fsf_status_read(adapter)) {
			if (atomic_read(&adapter->stat_miss) >= 16) {
				zfcp_erp_adapter_reopen(adapter, 0, 103, NULL);
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

/**
 * zfcp_adapter_enqueue - enqueue a new adapter to the list
 * @ccw_device: pointer to the struct cc_device
 *
 * Returns:	0             if a new adapter was successfully enqueued
 *		-ENOMEM       if alloc failed
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 * locks:	config_sema must be held to serialise changes to the adapter list
 */
int zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	/*
	 * Note: It is safe to release the list_lock, as any list changes
	 * are protected by the config_sema, which must be held to get here
	 */

	adapter = kzalloc(sizeof(struct zfcp_adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	ccw_device->handler = NULL;
	adapter->ccw_device = ccw_device;
	atomic_set(&adapter->refcount, 0);

	if (zfcp_qdio_allocate(adapter))
		goto qdio_allocate_failed;

	if (zfcp_allocate_low_mem_buffers(adapter))
		goto failed_low_mem_buffers;

	if (zfcp_reqlist_alloc(adapter))
		goto failed_low_mem_buffers;

	if (zfcp_adapter_debug_register(adapter))
		goto debug_register_failed;

	init_waitqueue_head(&adapter->remove_wq);
	init_waitqueue_head(&adapter->erp_thread_wqh);
	init_waitqueue_head(&adapter->erp_done_wqh);

	INIT_LIST_HEAD(&adapter->port_list_head);
	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);

	spin_lock_init(&adapter->req_list_lock);

	spin_lock_init(&adapter->hba_dbf_lock);
	spin_lock_init(&adapter->san_dbf_lock);
	spin_lock_init(&adapter->scsi_dbf_lock);
	spin_lock_init(&adapter->rec_dbf_lock);
	spin_lock_init(&adapter->req_q_lock);

	rwlock_init(&adapter->erp_lock);
	rwlock_init(&adapter->abort_lock);

	sema_init(&adapter->erp_ready_sem, 0);

	INIT_WORK(&adapter->stat_work, _zfcp_status_read_scheduler);
	INIT_WORK(&adapter->scan_work, _zfcp_scan_ports_later);

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	dev_set_drvdata(&ccw_device->dev, adapter);

	if (sysfs_create_group(&ccw_device->dev.kobj,
			       &zfcp_sysfs_adapter_attrs))
		goto sysfs_failed;

	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_fc_nameserver_init(adapter);

	return 0;

sysfs_failed:
	zfcp_adapter_debug_unregister(adapter);
debug_register_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
	kfree(adapter->req_list);
failed_low_mem_buffers:
	zfcp_free_low_mem_buffers(adapter);
qdio_allocate_failed:
	zfcp_qdio_free(adapter);
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

	cancel_work_sync(&adapter->scan_work);
	cancel_work_sync(&adapter->stat_work);
	zfcp_adapter_scsi_unregister(adapter);
	sysfs_remove_group(&adapter->ccw_device->dev.kobj,
			   &zfcp_sysfs_adapter_attrs);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	retval = zfcp_reqlist_isempty(adapter);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);
	if (!retval)
		return;

	zfcp_adapter_debug_unregister(adapter);

	/* remove specified adapter data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&adapter->list);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_qdio_free(adapter);

	zfcp_free_low_mem_buffers(adapter);
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
 * Locks: config_sema must be held to serialize changes to the port list
 *
 * All port internal structures are set up and the sysfs entry is generated.
 * d_id is used to enqueue ports with a well known address like the Directory
 * Service for nameserver lookup.
 */
struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *adapter, u64 wwpn,
				     u32 status, u32 d_id)
{
	struct zfcp_port *port;
	int retval;

	port = kzalloc(sizeof(struct zfcp_port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&port->remove_wq);
	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_WORK(&port->gid_pn_work, zfcp_erp_port_strategy_open_lookup);

	port->adapter = adapter;
	port->d_id = d_id;
	port->wwpn = wwpn;

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(status | ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set(&port->refcount, 0);

	dev_set_name(&port->sysfs_device, "0x%016llx", wwpn);
	port->sysfs_device.parent = &adapter->ccw_device->dev;

	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	read_lock_irq(&zfcp_data.config_lock);
	if (!(status & ZFCP_STATUS_PORT_NO_WWPN))
		if (zfcp_get_port_by_wwpn(adapter, wwpn)) {
			read_unlock_irq(&zfcp_data.config_lock);
			goto err_out_free;
		}
	read_unlock_irq(&zfcp_data.config_lock);

	if (device_register(&port->sysfs_device))
		goto err_out_free;

	retval = sysfs_create_group(&port->sysfs_device.kobj,
				    &zfcp_sysfs_port_attrs);

	if (retval) {
		device_unregister(&port->sysfs_device);
		goto err_out;
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&port->list, &adapter->port_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);

	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_adapter_get(adapter);
	return port;

err_out_free:
	kfree(port);
err_out:
	return ERR_PTR(-EINVAL);
}

/**
 * zfcp_port_dequeue - dequeues a port from the port list of the adapter
 * @port: pointer to struct zfcp_port which should be removed
 */
void zfcp_port_dequeue(struct zfcp_port *port)
{
	wait_event(port->remove_wq, atomic_read(&port->refcount) == 0);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	write_unlock_irq(&zfcp_data.config_lock);
	if (port->rport)
		fc_remote_port_delete(port->rport);
	port->rport = NULL;
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
