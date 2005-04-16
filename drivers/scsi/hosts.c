/*
 *  hosts.c Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *          Copyright (C) 2002-2003 Christoph Hellwig
 *
 *  mid to lowlevel SCSI driver interface
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *  Added QLOGIC QLA1280 SCSI controller kernel host support. 
 *     August 4, 1999 Fred Lewis, Intel DuPont
 *
 *  Updated to reflect the new initialization scheme for the higher 
 *  level of scsi drivers (sd/sr/st)
 *  September 17, 2000 Torben Mathiasen <tmm@image.dk>
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/transport_class.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>

#include "scsi_priv.h"
#include "scsi_logging.h"


static int scsi_host_next_hn;		/* host_no for next new host */


static void scsi_host_cls_release(struct class_device *class_dev)
{
	put_device(&class_to_shost(class_dev)->shost_gendev);
}

static struct class shost_class = {
	.name		= "scsi_host",
	.release	= scsi_host_cls_release,
};

/**
 * scsi_host_cancel - cancel outstanding IO to this host
 * @shost:	pointer to struct Scsi_Host
 * recovery:	recovery requested to run.
 **/
void scsi_host_cancel(struct Scsi_Host *shost, int recovery)
{
	struct scsi_device *sdev;

	set_bit(SHOST_CANCEL, &shost->shost_state);
	shost_for_each_device(sdev, shost) {
		scsi_device_cancel(sdev, recovery);
	}
	wait_event(shost->host_wait, (!test_bit(SHOST_RECOVERY,
						&shost->shost_state)));
}

/**
 * scsi_remove_host - remove a scsi host
 * @shost:	a pointer to a scsi host to remove
 **/
void scsi_remove_host(struct Scsi_Host *shost)
{
	scsi_forget_host(shost);
	scsi_host_cancel(shost, 0);
	scsi_proc_host_rm(shost);

	set_bit(SHOST_DEL, &shost->shost_state);

	transport_unregister_device(&shost->shost_gendev);
	class_device_unregister(&shost->shost_classdev);
	device_del(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_remove_host);

/**
 * scsi_add_host - add a scsi host
 * @shost:	scsi host pointer to add
 * @dev:	a struct device of type scsi class
 *
 * Return value: 
 * 	0 on success / != 0 for error
 **/
int scsi_add_host(struct Scsi_Host *shost, struct device *dev)
{
	struct scsi_host_template *sht = shost->hostt;
	int error = -EINVAL;

	printk(KERN_INFO "scsi%d : %s\n", shost->host_no,
			sht->info ? sht->info(shost) : sht->name);

	if (!shost->can_queue) {
		printk(KERN_ERR "%s: can_queue = 0 no longer supported\n",
				sht->name);
		goto out;
	}

	if (!shost->shost_gendev.parent)
		shost->shost_gendev.parent = dev ? dev : &platform_bus;

	error = device_add(&shost->shost_gendev);
	if (error)
		goto out;

	set_bit(SHOST_ADD, &shost->shost_state);
	get_device(shost->shost_gendev.parent);

	error = class_device_add(&shost->shost_classdev);
	if (error)
		goto out_del_gendev;

	get_device(&shost->shost_gendev);

	if (shost->transportt->host_size &&
	    (shost->shost_data = kmalloc(shost->transportt->host_size,
					 GFP_KERNEL)) == NULL)
		goto out_del_classdev;

	if (shost->transportt->create_work_queue) {
		snprintf(shost->work_q_name, KOBJ_NAME_LEN, "scsi_wq_%d",
			shost->host_no);
		shost->work_q = create_singlethread_workqueue(
					shost->work_q_name);
		if (!shost->work_q)
			goto out_free_shost_data;
	}

	error = scsi_sysfs_add_host(shost);
	if (error)
		goto out_destroy_host;

	scsi_proc_host_add(shost);
	return error;

 out_destroy_host:
	if (shost->work_q)
		destroy_workqueue(shost->work_q);
 out_free_shost_data:
	kfree(shost->shost_data);
 out_del_classdev:
	class_device_del(&shost->shost_classdev);
 out_del_gendev:
	device_del(&shost->shost_gendev);
 out:
	return error;
}
EXPORT_SYMBOL(scsi_add_host);

static void scsi_host_dev_release(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct device *parent = dev->parent;

	if (shost->ehandler) {
		DECLARE_COMPLETION(sem);
		shost->eh_notify = &sem;
		shost->eh_kill = 1;
		up(shost->eh_wait);
		wait_for_completion(&sem);
		shost->eh_notify = NULL;
	}

	if (shost->work_q)
		destroy_workqueue(shost->work_q);

	scsi_proc_hostdir_rm(shost->hostt);
	scsi_destroy_command_freelist(shost);
	kfree(shost->shost_data);

	/*
	 * Some drivers (eg aha1542) do scsi_register()/scsi_unregister()
	 * during probing without performing a scsi_set_device() in between.
	 * In this case dev->parent is NULL.
	 */
	if (parent)
		put_device(parent);
	kfree(shost);
}

/**
 * scsi_host_alloc - register a scsi host adapter instance.
 * @sht:	pointer to scsi host template
 * @privsize:	extra bytes to allocate for driver
 *
 * Note:
 * 	Allocate a new Scsi_Host and perform basic initialization.
 * 	The host is not published to the scsi midlayer until scsi_add_host
 * 	is called.
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 **/
struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *sht, int privsize)
{
	struct Scsi_Host *shost;
	int gfp_mask = GFP_KERNEL, rval;
	DECLARE_COMPLETION(complete);

	if (sht->unchecked_isa_dma && privsize)
		gfp_mask |= __GFP_DMA;

        /* Check to see if this host has any error handling facilities */
        if (!sht->eh_strategy_handler && !sht->eh_abort_handler &&
	    !sht->eh_device_reset_handler && !sht->eh_bus_reset_handler &&
            !sht->eh_host_reset_handler) {
		printk(KERN_ERR "ERROR: SCSI host `%s' has no error handling\n"
				"ERROR: This is not a safe way to run your "
				        "SCSI host\n"
				"ERROR: The error handling must be added to "
				"this driver\n", sht->proc_name);
		dump_stack();
        }

	shost = kmalloc(sizeof(struct Scsi_Host) + privsize, gfp_mask);
	if (!shost)
		return NULL;
	memset(shost, 0, sizeof(struct Scsi_Host) + privsize);

	spin_lock_init(&shost->default_lock);
	scsi_assign_lock(shost, &shost->default_lock);
	INIT_LIST_HEAD(&shost->__devices);
	INIT_LIST_HEAD(&shost->__targets);
	INIT_LIST_HEAD(&shost->eh_cmd_q);
	INIT_LIST_HEAD(&shost->starved_list);
	init_waitqueue_head(&shost->host_wait);

	init_MUTEX(&shost->scan_mutex);

	shost->host_no = scsi_host_next_hn++; /* XXX(hch): still racy */
	shost->dma_channel = 0xff;

	/* These three are default values which can be overridden */
	shost->max_channel = 0;
	shost->max_id = 8;
	shost->max_lun = 8;

	/* Give each shost a default transportt */
	shost->transportt = &blank_transport_template;

	/*
	 * All drivers right now should be able to handle 12 byte
	 * commands.  Every so often there are requests for 16 byte
	 * commands, but individual low-level drivers need to certify that
	 * they actually do something sensible with such commands.
	 */
	shost->max_cmd_len = 12;
	shost->hostt = sht;
	shost->this_id = sht->this_id;
	shost->can_queue = sht->can_queue;
	shost->sg_tablesize = sht->sg_tablesize;
	shost->cmd_per_lun = sht->cmd_per_lun;
	shost->unchecked_isa_dma = sht->unchecked_isa_dma;
	shost->use_clustering = sht->use_clustering;
	shost->ordered_flush = sht->ordered_flush;
	shost->ordered_tag = sht->ordered_tag;

	/*
	 * hosts/devices that do queueing must support ordered tags
	 */
	if (shost->can_queue > 1 && shost->ordered_flush) {
		printk(KERN_ERR "scsi: ordered flushes don't support queueing\n");
		shost->ordered_flush = 0;
	}

	if (sht->max_host_blocked)
		shost->max_host_blocked = sht->max_host_blocked;
	else
		shost->max_host_blocked = SCSI_DEFAULT_HOST_BLOCKED;

	/*
	 * If the driver imposes no hard sector transfer limit, start at
	 * machine infinity initially.
	 */
	if (sht->max_sectors)
		shost->max_sectors = sht->max_sectors;
	else
		shost->max_sectors = SCSI_DEFAULT_MAX_SECTORS;

	/*
	 * assume a 4GB boundary, if not set
	 */
	if (sht->dma_boundary)
		shost->dma_boundary = sht->dma_boundary;
	else
		shost->dma_boundary = 0xffffffff;

	rval = scsi_setup_command_freelist(shost);
	if (rval)
		goto fail_kfree;

	device_initialize(&shost->shost_gendev);
	snprintf(shost->shost_gendev.bus_id, BUS_ID_SIZE, "host%d",
		shost->host_no);
	shost->shost_gendev.release = scsi_host_dev_release;

	class_device_initialize(&shost->shost_classdev);
	shost->shost_classdev.dev = &shost->shost_gendev;
	shost->shost_classdev.class = &shost_class;
	snprintf(shost->shost_classdev.class_id, BUS_ID_SIZE, "host%d",
		  shost->host_no);

	shost->eh_notify = &complete;
	rval = kernel_thread(scsi_error_handler, shost, 0);
	if (rval < 0)
		goto fail_destroy_freelist;
	wait_for_completion(&complete);
	shost->eh_notify = NULL;

	scsi_proc_hostdir_add(shost->hostt);
	return shost;

 fail_destroy_freelist:
	scsi_destroy_command_freelist(shost);
 fail_kfree:
	kfree(shost);
	return NULL;
}
EXPORT_SYMBOL(scsi_host_alloc);

struct Scsi_Host *scsi_register(struct scsi_host_template *sht, int privsize)
{
	struct Scsi_Host *shost = scsi_host_alloc(sht, privsize);

	if (!sht->detect) {
		printk(KERN_WARNING "scsi_register() called on new-style "
				    "template for driver %s\n", sht->name);
		dump_stack();
	}

	if (shost)
		list_add_tail(&shost->sht_legacy_list, &sht->legacy_hosts);
	return shost;
}
EXPORT_SYMBOL(scsi_register);

void scsi_unregister(struct Scsi_Host *shost)
{
	list_del(&shost->sht_legacy_list);
	scsi_host_put(shost);
}
EXPORT_SYMBOL(scsi_unregister);

/**
 * scsi_host_lookup - get a reference to a Scsi_Host by host no
 *
 * @hostnum:	host number to locate
 *
 * Return value:
 *	A pointer to located Scsi_Host or NULL.
 **/
struct Scsi_Host *scsi_host_lookup(unsigned short hostnum)
{
	struct class *class = &shost_class;
	struct class_device *cdev;
	struct Scsi_Host *shost = ERR_PTR(-ENXIO), *p;

	down_read(&class->subsys.rwsem);
	list_for_each_entry(cdev, &class->children, node) {
		p = class_to_shost(cdev);
		if (p->host_no == hostnum) {
			shost = scsi_host_get(p);
			break;
		}
	}
	up_read(&class->subsys.rwsem);

	return shost;
}
EXPORT_SYMBOL(scsi_host_lookup);

/**
 * scsi_host_get - inc a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to inc.
 **/
struct Scsi_Host *scsi_host_get(struct Scsi_Host *shost)
{
	if (test_bit(SHOST_DEL, &shost->shost_state) ||
		!get_device(&shost->shost_gendev))
		return NULL;
	return shost;
}
EXPORT_SYMBOL(scsi_host_get);

/**
 * scsi_host_put - dec a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to dec.
 **/
void scsi_host_put(struct Scsi_Host *shost)
{
	put_device(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_host_put);

int scsi_init_hosts(void)
{
	return class_register(&shost_class);
}

void scsi_exit_hosts(void)
{
	class_unregister(&shost_class);
}

int scsi_is_host_device(const struct device *dev)
{
	return dev->release == scsi_host_dev_release;
}
EXPORT_SYMBOL(scsi_is_host_device);

/**
 * scsi_queue_work - Queue work to the Scsi_Host workqueue.
 * @shost:	Pointer to Scsi_Host.
 * @work:	Work to queue for execution.
 *
 * Return value:
 * 	0 on success / != 0 for error
 **/
int scsi_queue_work(struct Scsi_Host *shost, struct work_struct *work)
{
	if (unlikely(!shost->work_q)) {
		printk(KERN_ERR
			"ERROR: Scsi host '%s' attempted to queue scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();

		return -EINVAL;
	}

	return queue_work(shost->work_q, work);
}
EXPORT_SYMBOL_GPL(scsi_queue_work);

/**
 * scsi_flush_work - Flush a Scsi_Host's workqueue.
 * @shost:	Pointer to Scsi_Host.
 **/
void scsi_flush_work(struct Scsi_Host *shost)
{
	if (!shost->work_q) {
		printk(KERN_ERR
			"ERROR: Scsi host '%s' attempted to flush scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();
		return;
	}

	flush_workqueue(shost->work_q);
}
EXPORT_SYMBOL_GPL(scsi_flush_work);
