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
/*********************** FUNCTION PROTOTYPES *********************************/

/* written against the module interface */
static int __init  zfcp_module_init(void);

/*********************** KERNEL/MODULE PARAMETERS  ***************************/

/* declare driver module init/cleanup functions */
module_init(zfcp_module_init);

MODULE_AUTHOR("IBM Deutschland Entwicklung GmbH - linux390@de.ibm.com");
MODULE_DESCRIPTION
    ("FCP (SCSI over Fibre Channel) HBA driver for IBM System z9 and zSeries");
MODULE_LICENSE("GPL");

module_param(device, charp, 0400);
MODULE_PARM_DESC(device, "specify initial device");

/****************************************************************/
/************** Functions without logging ***********************/
/****************************************************************/

void
_zfcp_hex_dump(char *addr, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		printk("%02x", addr[i]);
		if ((i % 4) == 3)
			printk(" ");
		if ((i % 32) == 31)
			printk("\n");
	}
	if (((i-1) % 32) != 31)
		printk("\n");
}


/****************************************************************/
/****** Functions to handle the request ID hash table    ********/
/****************************************************************/

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

static void zfcp_reqlist_free(struct zfcp_adapter *adapter)
{
	kfree(adapter->req_list);
}

int zfcp_reqlist_isempty(struct zfcp_adapter *adapter)
{
	unsigned int idx;

	for (idx = 0; idx < REQUEST_LIST_SIZE; idx++)
		if (!list_empty(&adapter->req_list[idx]))
			return 0;
	return 1;
}

/****************************************************************/
/************** Uncategorised Functions *************************/
/****************************************************************/

/**
 * zfcp_device_setup - setup function
 * @str: pointer to parameter string
 *
 * Parse "device=..." parameter string.
 */
static int __init
zfcp_device_setup(char *devstr)
{
	char *tmp, *str;
	size_t len;

	if (!devstr)
		return 0;

	len = strlen(devstr) + 1;
	str = kmalloc(len, GFP_KERNEL);
	if (!str) {
		pr_err("zfcp: Could not allocate memory for "
		       "device parameter string, device not attached.\n");
		return 0;
	}
	memcpy(str, devstr, len);

	tmp = strchr(str, ',');
	if (!tmp)
		goto err_out;
	*tmp++ = '\0';
	strncpy(zfcp_data.init_busid, str, BUS_ID_SIZE);
	zfcp_data.init_busid[BUS_ID_SIZE-1] = '\0';

	zfcp_data.init_wwpn = simple_strtoull(tmp, &tmp, 0);
	if (*tmp++ != ',')
		goto err_out;
	if (*tmp == '\0')
		goto err_out;

	zfcp_data.init_fcp_lun = simple_strtoull(tmp, &tmp, 0);
	if (*tmp != '\0')
		goto err_out;
	kfree(str);
	return 1;

 err_out:
	pr_err("zfcp: Parse error for device parameter string %s, "
	       "device not attached.\n", str);
	kfree(str);
	return 0;
}

static void __init
zfcp_init_device_configure(void)
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

	if (adapter == NULL)
		goto out_adapter;
	port = zfcp_port_enqueue(adapter, zfcp_data.init_wwpn, 0, 0);
	if (!port)
		goto out_port;
	unit = zfcp_unit_enqueue(port, zfcp_data.init_fcp_lun);
	if (!unit)
		goto out_unit;
	up(&zfcp_data.config_sema);
	ccw_device_set_online(adapter->ccw_device);
	zfcp_erp_wait(adapter);
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

static int calc_alignment(int size)
{
	int align = 1;

	if (!size)
		return 0;

	while ((size - align) > 0)
		align <<= 1;

	return align;
}

static int __init
zfcp_module_init(void)
{
	int retval = -ENOMEM;
	int size, align;

	size = sizeof(struct zfcp_fsf_req_qtcb);
	align = calc_alignment(size);
	zfcp_data.fsf_req_qtcb_cache =
		kmem_cache_create("zfcp_fsf", size, align, 0, NULL);
	if (!zfcp_data.fsf_req_qtcb_cache)
		goto out;

	size = sizeof(struct fsf_status_read_buffer);
	align = calc_alignment(size);
	zfcp_data.sr_buffer_cache =
		kmem_cache_create("zfcp_sr", size, align, 0, NULL);
	if (!zfcp_data.sr_buffer_cache)
		goto out_sr_cache;

	size = sizeof(struct zfcp_gid_pn_data);
	align = calc_alignment(size);
	zfcp_data.gid_pn_cache =
		kmem_cache_create("zfcp_gid", size, align, 0, NULL);
	if (!zfcp_data.gid_pn_cache)
		goto out_gid_cache;

	/* initialize adapter list */
	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);

	/* initialize adapters to be removed list head */
	INIT_LIST_HEAD(&zfcp_data.adapter_remove_lh);

	zfcp_data.scsi_transport_template =
		fc_attach_transport(&zfcp_transport_functions);
	if (!zfcp_data.scsi_transport_template)
		goto out_transport;

	retval = misc_register(&zfcp_cfdc_misc);
	if (retval != 0) {
		pr_err("zfcp: registration of misc device zfcp_cfdc failed\n");
		goto out_misc;
	}

	/* Initialise proc semaphores */
	sema_init(&zfcp_data.config_sema, 1);

	/* initialise configuration rw lock */
	rwlock_init(&zfcp_data.config_lock);

	/* setup dynamic I/O */
	retval = zfcp_ccw_register();
	if (retval) {
		pr_err("zfcp: Registration with common I/O layer failed.\n");
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

/****************************************************************/
/****** Functions for configuration/set-up of structures ********/
/****************************************************************/

/**
 * zfcp_get_unit_by_lun - find unit in unit list of port by FCP LUN
 * @port: pointer to port to search for unit
 * @fcp_lun: FCP LUN to search for
 * Traverse list of all units of a port and return pointer to a unit
 * with the given FCP LUN.
 */
struct zfcp_unit *
zfcp_get_unit_by_lun(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;
	int found = 0;

	list_for_each_entry(unit, &port->unit_list_head, list) {
		if ((unit->fcp_lun == fcp_lun) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status))
		{
			found = 1;
			break;
		}
	}
	return found ? unit : NULL;
}

/**
 * zfcp_get_port_by_wwpn - find port in port list of adapter by wwpn
 * @adapter: pointer to adapter to search for port
 * @wwpn: wwpn to search for
 * Traverse list of all ports of an adapter and return pointer to a port
 * with the given wwpn.
 */
struct zfcp_port *
zfcp_get_port_by_wwpn(struct zfcp_adapter *adapter, wwn_t wwpn)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->wwpn == wwpn) &&
		    !(atomic_read(&port->status) &
		      (ZFCP_STATUS_PORT_NO_WWPN | ZFCP_STATUS_COMMON_REMOVE))) {
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/**
 * zfcp_get_port_by_did - find port in port list of adapter by d_id
 * @adapter: pointer to adapter to search for port
 * @d_id: d_id to search for
 * Traverse list of all ports of an adapter and return pointer to a port
 * with the given d_id.
 */
struct zfcp_port *
zfcp_get_port_by_did(struct zfcp_adapter *adapter, u32 d_id)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->d_id == d_id) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status))
		{
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/**
 * zfcp_get_adapter_by_busid - find adpater in adapter list by bus_id
 * @bus_id: bus_id to search for
 * Traverse list of all adapters and return pointer to an adapter
 * with the given bus_id.
 */
struct zfcp_adapter *
zfcp_get_adapter_by_busid(char *bus_id)
{
	struct zfcp_adapter *adapter;
	int found = 0;

	list_for_each_entry(adapter, &zfcp_data.adapter_list_head, list) {
		if ((strncmp(bus_id, zfcp_get_busid_by_adapter(adapter),
			     BUS_ID_SIZE) == 0) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE,
				      &adapter->status)){
			found = 1;
			break;
		}
	}
	return found ? adapter : NULL;
}

/**
 * zfcp_unit_enqueue - enqueue unit to unit list of a port.
 * @port: pointer to port where unit is added
 * @fcp_lun: FCP LUN of unit to be enqueued
 * Return: pointer to enqueued unit on success, NULL on error
 * Locks: config_sema must be held to serialize changes to the unit list
 *
 * Sets up some unit internal structures and creates sysfs entry.
 */
struct zfcp_unit *
zfcp_unit_enqueue(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;

	/*
	 * check that there is no unit with this FCP_LUN already in list
	 * and enqueue it.
	 * Note: Unlike for the adapter and the port, this is an error
	 */
	read_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	read_unlock_irq(&zfcp_data.config_lock);
	if (unit)
		return NULL;

	unit = kzalloc(sizeof (struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return NULL;

	/* initialise reference count stuff */
	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);

	unit->port = port;
	unit->fcp_lun = fcp_lun;

	/* setup for sysfs registration */
	snprintf(unit->sysfs_device.bus_id, BUS_ID_SIZE, "0x%016llx", fcp_lun);
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
		kfree(unit);
		return NULL;
	}

	if (zfcp_sysfs_unit_create_files(&unit->sysfs_device)) {
		device_unregister(&unit->sysfs_device);
		return NULL;
	}

	zfcp_unit_get(unit);
	unit->scsi_lun = scsilun_to_int((struct scsi_lun *)&unit->fcp_lun);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&unit->list, &port->unit_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);
	write_unlock_irq(&zfcp_data.config_lock);

	port->units++;
	zfcp_port_get(port);

	return unit;
}

void
zfcp_unit_dequeue(struct zfcp_unit *unit)
{
	zfcp_unit_wait(unit);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&unit->list);
	write_unlock_irq(&zfcp_data.config_lock);
	unit->port->units--;
	zfcp_port_put(unit->port);
	zfcp_sysfs_unit_remove_files(&unit->sysfs_device);
	device_unregister(&unit->sysfs_device);
}

/*
 * Allocates a combined QTCB/fsf_req buffer for erp actions and fcp/SCSI
 * commands.
 * It also genrates fcp-nameserver request/response buffer and unsolicited
 * status read fsf_req buffers.
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
	adapter->pool.fsf_req_erp =
		mempool_create_slab_pool(ZFCP_POOL_FSF_REQ_ERP_NR,
					 zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_erp)
		return -ENOMEM;

	adapter->pool.fsf_req_scsi =
		mempool_create_slab_pool(ZFCP_POOL_FSF_REQ_SCSI_NR,
					 zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_scsi)
		return -ENOMEM;

	adapter->pool.fsf_req_abort =
		mempool_create_slab_pool(ZFCP_POOL_FSF_REQ_ABORT_NR,
					 zfcp_data.fsf_req_qtcb_cache);
	if (!adapter->pool.fsf_req_abort)
		return -ENOMEM;

	adapter->pool.fsf_req_status_read =
		mempool_create_kmalloc_pool(ZFCP_POOL_STATUS_READ_NR,
					    sizeof(struct zfcp_fsf_req));
	if (!adapter->pool.fsf_req_status_read)
		return -ENOMEM;

	adapter->pool.data_status_read =
		mempool_create_slab_pool(ZFCP_POOL_STATUS_READ_NR,
					 zfcp_data.sr_buffer_cache);
	if (!adapter->pool.data_status_read)
		return -ENOMEM;

	adapter->pool.data_gid_pn =
		mempool_create_slab_pool(ZFCP_POOL_DATA_GID_PN_NR,
					 zfcp_data.gid_pn_cache);
	if (!adapter->pool.data_gid_pn)
		return -ENOMEM;

	return 0;
}

/**
 * zfcp_free_low_mem_buffers - free memory pools of an adapter
 * @adapter: pointer to zfcp_adapter for which memory pools should be freed
 * locking:  zfcp_data.config_sema must be held
 */
static void
zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
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

static void zfcp_dummy_release(struct device *dev)
{
	return;
}

int zfcp_status_read_refill(struct zfcp_adapter *adapter)
{
	while (atomic_read(&adapter->stat_miss) > 0)
		if (zfcp_fsf_status_read(adapter, ZFCP_WAIT_FOR_SBAL))
			break;
	else
		atomic_dec(&adapter->stat_miss);

	if (ZFCP_STATUS_READS_RECOM <= atomic_read(&adapter->stat_miss)) {
		zfcp_erp_adapter_reopen(adapter, 0, 103, NULL);
		return 1;
	}
	return 0;
}

static void _zfcp_status_read_scheduler(struct work_struct *work)
{
	zfcp_status_read_refill(container_of(work, struct zfcp_adapter,
					     stat_work));
}

static int zfcp_nameserver_enqueue(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	port = zfcp_port_enqueue(adapter, 0, ZFCP_STATUS_PORT_WKA,
				 ZFCP_DID_DIRECTORY_SERVICE);
	if (!port)
		return -ENXIO;
	zfcp_port_put(port);

	return 0;
}

/*
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 *
 * FIXME: Use -ENOMEM as return code for allocation failures
 *
 * returns:	0             if a new adapter was successfully enqueued
 *              ZFCP_KNOWN    if an adapter with this devno was already present
 *		-ENOMEM       if alloc failed
 * locks:	config_sema must be held to serialise changes to the adapter list
 */
struct zfcp_adapter *
zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	/*
	 * Note: It is safe to release the list_lock, as any list changes
	 * are protected by the config_sema, which must be held to get here
	 */

	/* try to allocate new adapter data structure (zeroed) */
	adapter = kzalloc(sizeof (struct zfcp_adapter), GFP_KERNEL);
	if (!adapter)
		goto out;

	ccw_device->handler = NULL;

	/* save ccw_device pointer */
	adapter->ccw_device = ccw_device;

	if (zfcp_qdio_allocate(adapter))
		goto qdio_allocate_failed;

	if (zfcp_allocate_low_mem_buffers(adapter))
		goto failed_low_mem_buffers;

	/* initialise reference count stuff */
	atomic_set(&adapter->refcount, 0);
	init_waitqueue_head(&adapter->remove_wq);

	/* initialise list of ports */
	INIT_LIST_HEAD(&adapter->port_list_head);

	/* initialise list of ports to be removed */
	INIT_LIST_HEAD(&adapter->port_remove_lh);

	/* initialize list of fsf requests */
	spin_lock_init(&adapter->req_list_lock);
	if (zfcp_reqlist_alloc(adapter))
		goto failed_low_mem_buffers;

	/* initialize debug locks */

	spin_lock_init(&adapter->hba_dbf_lock);
	spin_lock_init(&adapter->san_dbf_lock);
	spin_lock_init(&adapter->scsi_dbf_lock);
	spin_lock_init(&adapter->rec_dbf_lock);

	if (zfcp_adapter_debug_register(adapter))
		goto debug_register_failed;

	/* initialize error recovery stuff */

	rwlock_init(&adapter->erp_lock);
	sema_init(&adapter->erp_ready_sem, 0);
	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);

	/* initialize abort lock */
	rwlock_init(&adapter->abort_lock);

	/* initialise some erp stuff */
	init_waitqueue_head(&adapter->erp_thread_wqh);
	init_waitqueue_head(&adapter->erp_done_wqh);

	/* initialize lock of associated request queue */
	rwlock_init(&adapter->req_q.lock);
	INIT_WORK(&adapter->stat_work, _zfcp_status_read_scheduler);
	INIT_WORK(&adapter->scan_work, _zfcp_scan_ports_later);

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	dev_set_drvdata(&ccw_device->dev, adapter);

	if (zfcp_sysfs_adapter_create_files(&ccw_device->dev))
		goto sysfs_failed;

	adapter->generic_services.parent = &adapter->ccw_device->dev;
	adapter->generic_services.release = zfcp_dummy_release;
	snprintf(adapter->generic_services.bus_id, BUS_ID_SIZE,
		 "generic_services");

	if (device_register(&adapter->generic_services))
		goto generic_services_failed;

	/* put allocated adapter at list tail */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_data.adapters++;

	zfcp_nameserver_enqueue(adapter);

	goto out;

 generic_services_failed:
	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
 sysfs_failed:
	zfcp_adapter_debug_unregister(adapter);
 debug_register_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
	zfcp_reqlist_free(adapter);
 failed_low_mem_buffers:
	zfcp_free_low_mem_buffers(adapter);
 qdio_allocate_failed:
	zfcp_qdio_free(adapter);
	kfree(adapter);
	adapter = NULL;
 out:
	return adapter;
}

/*
 * returns:	0 - struct zfcp_adapter  data structure successfully removed
 *		!0 - struct zfcp_adapter  data structure could not be removed
 *			(e.g. still used)
 * locks:	adapter list write lock is assumed to be held by caller
 */
void
zfcp_adapter_dequeue(struct zfcp_adapter *adapter)
{
	int retval = 0;
	unsigned long flags;

	cancel_work_sync(&adapter->scan_work);
	cancel_work_sync(&adapter->stat_work);
	zfcp_adapter_scsi_unregister(adapter);
	device_unregister(&adapter->generic_services);
	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	retval = zfcp_reqlist_isempty(adapter);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);
	if (!retval) {
		retval = -EBUSY;
		goto out;
	}

	zfcp_adapter_debug_unregister(adapter);

	/* remove specified adapter data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&adapter->list);
	write_unlock_irq(&zfcp_data.config_lock);

	/* decrease number of adapters in list */
	zfcp_data.adapters--;

	zfcp_qdio_free(adapter);

	zfcp_free_low_mem_buffers(adapter);
	zfcp_reqlist_free(adapter);
	kfree(adapter->fc_stats);
	kfree(adapter->stats_reset_data);
	kfree(adapter);
 out:
	return;
}

/**
 * zfcp_port_enqueue - enqueue port to port list of adapter
 * @adapter: adapter where remote port is added
 * @wwpn: WWPN of the remote port to be enqueued
 * @status: initial status for the port
 * @d_id: destination id of the remote port to be enqueued
 * Return: pointer to enqueued port on success, NULL on error
 * Locks: config_sema must be held to serialize changes to the port list
 *
 * All port internal structures are set up and the sysfs entry is generated.
 * d_id is used to enqueue ports with a well known address like the Directory
 * Service for nameserver lookup.
 */
struct zfcp_port *
zfcp_port_enqueue(struct zfcp_adapter *adapter, wwn_t wwpn, u32 status,
		  u32 d_id)
{
	struct zfcp_port *port;
	int check_wwpn;

	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);
	/*
	 * check that there is no port with this WWPN already in list
	 */
	if (check_wwpn) {
		read_lock_irq(&zfcp_data.config_lock);
		port = zfcp_get_port_by_wwpn(adapter, wwpn);
		read_unlock_irq(&zfcp_data.config_lock);
		if (port)
			return NULL;
	}

	port = kzalloc(sizeof (struct zfcp_port), GFP_KERNEL);
	if (!port)
		return NULL;

	/* initialise reference count stuff */
	atomic_set(&port->refcount, 0);
	init_waitqueue_head(&port->remove_wq);

	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_LIST_HEAD(&port->unit_remove_lh);

	port->adapter = adapter;

	if (check_wwpn)
		port->wwpn = wwpn;

	atomic_set_mask(status, &port->status);

	/* setup for sysfs registration */
	if (status & ZFCP_STATUS_PORT_WKA) {
		switch (d_id) {
		case ZFCP_DID_DIRECTORY_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "directory");
			break;
		case ZFCP_DID_MANAGEMENT_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "management");
			break;
		case ZFCP_DID_KEY_DISTRIBUTION_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "key_distribution");
			break;
		case ZFCP_DID_ALIAS_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "alias");
			break;
		case ZFCP_DID_TIME_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "time");
			break;
		default:
			kfree(port);
			return NULL;
		}
		port->sysfs_device.parent = &adapter->generic_services;
	} else {
		snprintf(port->sysfs_device.bus_id,
			 BUS_ID_SIZE, "0x%016llx", wwpn);
		port->sysfs_device.parent = &adapter->ccw_device->dev;
	}

	port->d_id = d_id;

	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);

	if (device_register(&port->sysfs_device)) {
		kfree(port);
		return NULL;
	}

	if (zfcp_sysfs_port_create_files(&port->sysfs_device, status)) {
		device_unregister(&port->sysfs_device);
		return NULL;
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&port->list, &adapter->port_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);
	if (d_id == ZFCP_DID_DIRECTORY_SERVICE)
		if (!adapter->nameserver_port)
			adapter->nameserver_port = port;
	adapter->ports++;
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_adapter_get(adapter);

	return port;
}

void
zfcp_port_dequeue(struct zfcp_port *port)
{
	zfcp_port_wait(port);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	port->adapter->ports--;
	write_unlock_irq(&zfcp_data.config_lock);
	if (port->rport)
		fc_remote_port_delete(port->rport);
	port->rport = NULL;
	zfcp_adapter_put(port->adapter);
	zfcp_sysfs_port_remove_files(&port->sysfs_device,
				     atomic_read(&port->status));
	device_unregister(&port->sysfs_device);
}

void zfcp_sg_free_table(struct scatterlist *sg, int count)
{
	int i;

	for (i = 0; i < count; i++, sg++)
		if (sg)
			free_page((unsigned long) sg_virt(sg));
		else
			break;
}

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
