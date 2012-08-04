/*
 * zfcp device driver
 *
 * Module interface and handling of zfcp data structures.
 *
 * Copyright IBM Corp. 2002, 2010
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
#include <linux/slab.h>
#include <linux/module.h>
#include "zfcp_ext.h"
#include "zfcp_fc.h"
#include "zfcp_reqlist.h"

#define ZFCP_BUS_ID_SIZE	20

MODULE_AUTHOR("IBM Deutschland Entwicklung GmbH - linux390@de.ibm.com");
MODULE_DESCRIPTION("FCP HBA driver");
MODULE_LICENSE("GPL");

static char *init_device;
module_param_named(device, init_device, charp, 0400);
MODULE_PARM_DESC(device, "specify initial device");

static struct kmem_cache * __init zfcp_cache_hw_align(const char *name,
						      unsigned long size)
{
	return kmem_cache_create(name, size, roundup_pow_of_two(size), 0, NULL);
}

static void __init zfcp_init_device_configure(char *busid, u64 wwpn, u64 lun)
{
	struct ccw_device *cdev;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;

	cdev = get_ccwdev_by_busid(&zfcp_ccw_driver, busid);
	if (!cdev)
		return;

	if (ccw_device_set_online(cdev))
		goto out_ccw_device;

	adapter = zfcp_ccw_adapter_by_cdev(cdev);
	if (!adapter)
		goto out_ccw_device;

	port = zfcp_get_port_by_wwpn(adapter, wwpn);
	if (!port)
		goto out_port;
	flush_work(&port->rport_work);

	zfcp_unit_add(port, lun);
	put_device(&port->dev);

out_port:
	zfcp_ccw_adapter_put(adapter);
out_ccw_device:
	put_device(&cdev->dev);
	return;
}

static void __init zfcp_init_device_setup(char *devstr)
{
	char *token;
	char *str, *str_saved;
	char busid[ZFCP_BUS_ID_SIZE];
	u64 wwpn, lun;

	/* duplicate devstr and keep the original for sysfs presentation*/
	str_saved = kstrdup(devstr, GFP_KERNEL);
	str = str_saved;
	if (!str)
		return;

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

	kfree(str_saved);
	zfcp_init_device_configure(busid, wwpn, lun);
	return;

err_out:
	kfree(str_saved);
	pr_err("%s is not a valid SCSI device\n", devstr);
}

static int __init zfcp_module_init(void)
{
	int retval = -ENOMEM;

	zfcp_fsf_qtcb_cache = zfcp_cache_hw_align("zfcp_fsf_qtcb",
						  sizeof(struct fsf_qtcb));
	if (!zfcp_fsf_qtcb_cache)
		goto out_qtcb_cache;

	zfcp_fc_req_cache = zfcp_cache_hw_align("zfcp_fc_req",
						sizeof(struct zfcp_fc_req));
	if (!zfcp_fc_req_cache)
		goto out_fc_cache;

	zfcp_scsi_transport_template =
		fc_attach_transport(&zfcp_transport_functions);
	if (!zfcp_scsi_transport_template)
		goto out_transport;
	scsi_transport_reserve_device(zfcp_scsi_transport_template,
				      sizeof(struct zfcp_scsi_dev));


	retval = misc_register(&zfcp_cfdc_misc);
	if (retval) {
		pr_err("Registering the misc device zfcp_cfdc failed\n");
		goto out_misc;
	}

	retval = ccw_driver_register(&zfcp_ccw_driver);
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
	fc_release_transport(zfcp_scsi_transport_template);
out_transport:
	kmem_cache_destroy(zfcp_fc_req_cache);
out_fc_cache:
	kmem_cache_destroy(zfcp_fsf_qtcb_cache);
out_qtcb_cache:
	return retval;
}

module_init(zfcp_module_init);

static void __exit zfcp_module_exit(void)
{
	ccw_driver_unregister(&zfcp_ccw_driver);
	misc_deregister(&zfcp_cfdc_misc);
	fc_release_transport(zfcp_scsi_transport_template);
	kmem_cache_destroy(zfcp_fc_req_cache);
	kmem_cache_destroy(zfcp_fsf_qtcb_cache);
}

module_exit(zfcp_module_exit);

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
	unsigned long flags;
	struct zfcp_port *port;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		if (port->wwpn == wwpn) {
			if (!get_device(&port->dev))
				port = NULL;
			read_unlock_irqrestore(&adapter->port_list_lock, flags);
			return port;
		}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
	return NULL;
}

static int zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
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
		mempool_create_slab_pool(4, zfcp_fsf_qtcb_cache);
	if (!adapter->pool.qtcb_pool)
		return -ENOMEM;

	BUILD_BUG_ON(sizeof(struct fsf_status_read_buffer) > PAGE_SIZE);
	adapter->pool.sr_data =
		mempool_create_page_pool(FSF_STATUS_READS_RECOM, 0);
	if (!adapter->pool.sr_data)
		return -ENOMEM;

	adapter->pool.gid_pn =
		mempool_create_slab_pool(1, zfcp_fc_req_cache);
	if (!adapter->pool.gid_pn)
		return -ENOMEM;

	return 0;
}

static void zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
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
	if (adapter->pool.sr_data)
		mempool_destroy(adapter->pool.sr_data);
	if (adapter->pool.gid_pn)
		mempool_destroy(adapter->pool.gid_pn);
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
			if (atomic_read(&adapter->stat_miss) >=
			    adapter->stat_read_buf_num) {
				zfcp_erp_adapter_reopen(adapter, 0, "axsref1");
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
 * Returns:	struct zfcp_adapter*
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 */
struct zfcp_adapter *zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	struct zfcp_adapter *adapter;

	if (!get_device(&ccw_device->dev))
		return ERR_PTR(-ENODEV);

	adapter = kzalloc(sizeof(struct zfcp_adapter), GFP_KERNEL);
	if (!adapter) {
		put_device(&ccw_device->dev);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&adapter->ref);

	ccw_device->handler = NULL;
	adapter->ccw_device = ccw_device;

	INIT_WORK(&adapter->stat_work, _zfcp_status_read_scheduler);
	INIT_WORK(&adapter->scan_work, zfcp_fc_scan_ports);
	INIT_WORK(&adapter->ns_up_work, zfcp_fc_sym_name_update);

	if (zfcp_qdio_setup(adapter))
		goto failed;

	if (zfcp_allocate_low_mem_buffers(adapter))
		goto failed;

	adapter->req_list = zfcp_reqlist_alloc();
	if (!adapter->req_list)
		goto failed;

	if (zfcp_dbf_adapter_register(adapter))
		goto failed;

	if (zfcp_setup_adapter_work_queue(adapter))
		goto failed;

	if (zfcp_fc_gs_setup(adapter))
		goto failed;

	rwlock_init(&adapter->port_list_lock);
	INIT_LIST_HEAD(&adapter->port_list);

	INIT_LIST_HEAD(&adapter->events.list);
	INIT_WORK(&adapter->events.work, zfcp_fc_post_event);
	spin_lock_init(&adapter->events.list_lock);

	init_waitqueue_head(&adapter->erp_ready_wq);
	init_waitqueue_head(&adapter->erp_done_wqh);

	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);

	rwlock_init(&adapter->erp_lock);
	rwlock_init(&adapter->abort_lock);

	if (zfcp_erp_thread_setup(adapter))
		goto failed;

	adapter->service_level.seq_print = zfcp_print_sl;

	dev_set_drvdata(&ccw_device->dev, adapter);

	if (sysfs_create_group(&ccw_device->dev.kobj,
			       &zfcp_sysfs_adapter_attrs))
		goto failed;

	/* report size limit per scatter-gather segment */
	adapter->dma_parms.max_segment_size = ZFCP_QDIO_SBALE_LEN;
	adapter->ccw_device->dev.dma_parms = &adapter->dma_parms;

	if (!zfcp_scsi_adapter_register(adapter))
		return adapter;

failed:
	zfcp_adapter_unregister(adapter);
	return ERR_PTR(-ENOMEM);
}

void zfcp_adapter_unregister(struct zfcp_adapter *adapter)
{
	struct ccw_device *cdev = adapter->ccw_device;

	cancel_work_sync(&adapter->scan_work);
	cancel_work_sync(&adapter->stat_work);
	cancel_work_sync(&adapter->ns_up_work);
	zfcp_destroy_adapter_work_queue(adapter);

	zfcp_fc_wka_ports_force_offline(adapter->gs);
	zfcp_scsi_adapter_unregister(adapter);
	sysfs_remove_group(&cdev->dev.kobj, &zfcp_sysfs_adapter_attrs);

	zfcp_erp_thread_kill(adapter);
	zfcp_dbf_adapter_unregister(adapter);
	zfcp_qdio_destroy(adapter->qdio);

	zfcp_ccw_adapter_put(adapter); /* final put to release */
}

/**
 * zfcp_adapter_release - remove the adapter from the resource list
 * @ref: pointer to struct kref
 * locks:	adapter list write lock is assumed to be held by caller
 */
void zfcp_adapter_release(struct kref *ref)
{
	struct zfcp_adapter *adapter = container_of(ref, struct zfcp_adapter,
						    ref);
	struct ccw_device *cdev = adapter->ccw_device;

	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	zfcp_fc_gs_destroy(adapter);
	zfcp_free_low_mem_buffers(adapter);
	kfree(adapter->req_list);
	kfree(adapter->fc_stats);
	kfree(adapter->stats_reset_data);
	kfree(adapter);
	put_device(&cdev->dev);
}

/**
 * zfcp_device_unregister - remove port, unit from system
 * @dev: reference to device which is to be removed
 * @grp: related reference to attribute group
 *
 * Helper function to unregister port, unit from system
 */
void zfcp_device_unregister(struct device *dev,
			    const struct attribute_group *grp)
{
	sysfs_remove_group(&dev->kobj, grp);
	device_unregister(dev);
}

static void zfcp_port_release(struct device *dev)
{
	struct zfcp_port *port = container_of(dev, struct zfcp_port, dev);

	zfcp_ccw_adapter_put(port->adapter);
	kfree(port);
}

/**
 * zfcp_port_enqueue - enqueue port to port list of adapter
 * @adapter: adapter where remote port is added
 * @wwpn: WWPN of the remote port to be enqueued
 * @status: initial status for the port
 * @d_id: destination id of the remote port to be enqueued
 * Returns: pointer to enqueued port on success, ERR_PTR on error
 *
 * All port internal structures are set up and the sysfs entry is generated.
 * d_id is used to enqueue ports with a well known address like the Directory
 * Service for nameserver lookup.
 */
struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *adapter, u64 wwpn,
				     u32 status, u32 d_id)
{
	struct zfcp_port *port;
	int retval = -ENOMEM;

	kref_get(&adapter->ref);

	port = zfcp_get_port_by_wwpn(adapter, wwpn);
	if (port) {
		put_device(&port->dev);
		retval = -EEXIST;
		goto err_out;
	}

	port = kzalloc(sizeof(struct zfcp_port), GFP_KERNEL);
	if (!port)
		goto err_out;

	rwlock_init(&port->unit_list_lock);
	INIT_LIST_HEAD(&port->unit_list);

	INIT_WORK(&port->gid_pn_work, zfcp_fc_port_did_lookup);
	INIT_WORK(&port->test_link_work, zfcp_fc_link_test_work);
	INIT_WORK(&port->rport_work, zfcp_scsi_rport_work);

	port->adapter = adapter;
	port->d_id = d_id;
	port->wwpn = wwpn;
	port->rport_task = RPORT_NONE;
	port->dev.parent = &adapter->ccw_device->dev;
	port->dev.release = zfcp_port_release;

	if (dev_set_name(&port->dev, "0x%016llx", (unsigned long long)wwpn)) {
		kfree(port);
		goto err_out;
	}
	retval = -EINVAL;

	if (device_register(&port->dev)) {
		put_device(&port->dev);
		goto err_out;
	}

	if (sysfs_create_group(&port->dev.kobj,
			       &zfcp_sysfs_port_attrs))
		goto err_out_put;

	write_lock_irq(&adapter->port_list_lock);
	list_add_tail(&port->list, &adapter->port_list);
	write_unlock_irq(&adapter->port_list_lock);

	atomic_set_mask(status | ZFCP_STATUS_COMMON_RUNNING, &port->status);

	return port;

err_out_put:
	device_unregister(&port->dev);
err_out:
	zfcp_ccw_adapter_put(adapter);
	return ERR_PTR(retval);
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
