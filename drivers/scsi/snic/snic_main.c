/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "snic.h"
#include "snic_fwint.h"

#define PCI_DEVICE_ID_CISCO_SNIC	0x0046

/* Supported devices by snic module */
static struct pci_device_id snic_id_table[] = {
	{PCI_DEVICE(0x1137, PCI_DEVICE_ID_CISCO_SNIC) },
	{ 0, }	/* end of table */
};

unsigned int snic_log_level = 0x0;
module_param(snic_log_level, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(snic_log_level, "bitmask for snic logging levels");

#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
unsigned int snic_trace_max_pages = 16;
module_param(snic_trace_max_pages, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(snic_trace_max_pages,
		"Total allocated memory pages for snic trace buffer");

#endif
unsigned int snic_max_qdepth = SNIC_DFLT_QUEUE_DEPTH;
module_param(snic_max_qdepth, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(snic_max_qdepth, "Queue depth to report for each LUN");

/*
 * snic_slave_alloc : callback function to SCSI Mid Layer, called on
 * scsi device initialization.
 */
static int
snic_slave_alloc(struct scsi_device *sdev)
{
	struct snic_tgt *tgt = starget_to_tgt(scsi_target(sdev));

	if (!tgt || snic_tgt_chkready(tgt))
		return -ENXIO;

	return 0;
}

/*
 * snic_slave_configure : callback function to SCSI Mid Layer, called on
 * scsi device initialization.
 */
static int
snic_slave_configure(struct scsi_device *sdev)
{
	struct snic *snic = shost_priv(sdev->host);
	u32 qdepth = 0, max_ios = 0;
	int tmo = SNIC_DFLT_CMD_TIMEOUT * HZ;

	/* Set Queue Depth */
	max_ios = snic_max_qdepth;
	qdepth = min_t(u32, max_ios, SNIC_MAX_QUEUE_DEPTH);
	scsi_change_queue_depth(sdev, qdepth);

	if (snic->fwinfo.io_tmo > 1)
		tmo = snic->fwinfo.io_tmo * HZ;

	/* FW requires extended timeouts */
	blk_queue_rq_timeout(sdev->request_queue, tmo);

	return 0;
}

static int
snic_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct snic *snic = shost_priv(sdev->host);
	int qsz = 0;

	qsz = min_t(u32, qdepth, SNIC_MAX_QUEUE_DEPTH);
	if (qsz < sdev->queue_depth)
		atomic64_inc(&snic->s_stats.misc.qsz_rampdown);
	else if (qsz > sdev->queue_depth)
		atomic64_inc(&snic->s_stats.misc.qsz_rampup);

	atomic64_set(&snic->s_stats.misc.last_qsz, sdev->queue_depth);

	scsi_change_queue_depth(sdev, qsz);

	return sdev->queue_depth;
}

static struct scsi_host_template snic_host_template = {
	.module = THIS_MODULE,
	.name = SNIC_DRV_NAME,
	.queuecommand = snic_queuecommand,
	.eh_abort_handler = snic_abort_cmd,
	.eh_device_reset_handler = snic_device_reset,
	.eh_host_reset_handler = snic_host_reset,
	.slave_alloc = snic_slave_alloc,
	.slave_configure = snic_slave_configure,
	.change_queue_depth = snic_change_queue_depth,
	.this_id = -1,
	.cmd_per_lun = SNIC_DFLT_QUEUE_DEPTH,
	.can_queue = SNIC_MAX_IO_REQ,
	.use_clustering = ENABLE_CLUSTERING,
	.sg_tablesize = SNIC_MAX_SG_DESC_CNT,
	.max_sectors = 0x800,
	.shost_attrs = snic_attrs,
	.track_queue_depth = 1,
	.cmd_size = sizeof(struct snic_internal_io_state),
	.proc_name = "snic_scsi",
};

/*
 * snic_handle_link_event : Handles link events such as link up/down/error
 */
void
snic_handle_link_event(struct snic *snic)
{
	unsigned long flags;

	spin_lock_irqsave(&snic->snic_lock, flags);
	if (snic->stop_link_events) {
		spin_unlock_irqrestore(&snic->snic_lock, flags);

		return;
	}
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	queue_work(snic_glob->event_q, &snic->link_work);
} /* end of snic_handle_link_event */

/*
 * snic_notify_set : sets notification area
 * This notification area is to receive events from fw
 * Note: snic supports only MSIX interrupts, in which we can just call
 *  svnic_dev_notify_set directly
 */
static int
snic_notify_set(struct snic *snic)
{
	int ret = 0;
	enum vnic_dev_intr_mode intr_mode;

	intr_mode = svnic_dev_get_intr_mode(snic->vdev);

	if (intr_mode == VNIC_DEV_INTR_MODE_MSIX) {
		ret = svnic_dev_notify_set(snic->vdev, SNIC_MSIX_ERR_NOTIFY);
	} else {
		SNIC_HOST_ERR(snic->shost,
			      "Interrupt mode should be setup before devcmd notify set %d\n",
			      intr_mode);
		ret = -1;
	}

	return ret;
} /* end of snic_notify_set */

/*
 * snic_dev_wait : polls vnic open status.
 */
static int
snic_dev_wait(struct vnic_dev *vdev,
		int (*start)(struct vnic_dev *, int),
		int (*finished)(struct vnic_dev *, int *),
		int arg)
{
	unsigned long time;
	int ret, done;
	int retry_cnt = 0;

	ret = start(vdev, arg);
	if (ret)
		return ret;

	/*
	 * Wait for func to complete...2 seconds max.
	 *
	 * Sometimes schedule_timeout_uninterruptible take long	time
	 * to wakeup, which results skipping retry. The retry counter
	 * ensures to retry at least two times.
	 */
	time = jiffies + (HZ * 2);
	do {
		ret = finished(vdev, &done);
		if (ret)
			return ret;

		if (done)
			return 0;
		schedule_timeout_uninterruptible(HZ/10);
		++retry_cnt;
	} while (time_after(time, jiffies) || (retry_cnt < 3));

	return -ETIMEDOUT;
} /* end of snic_dev_wait */

/*
 * snic_cleanup: called by snic_remove
 * Stops the snic device, masks all interrupts, Completed CQ entries are
 * drained. Posted WQ/RQ/Copy-WQ entries are cleanup
 */
static int
snic_cleanup(struct snic *snic)
{
	unsigned int i;
	int ret;

	svnic_dev_disable(snic->vdev);
	for (i = 0; i < snic->intr_count; i++)
		svnic_intr_mask(&snic->intr[i]);

	for (i = 0; i < snic->wq_count; i++) {
		ret = svnic_wq_disable(&snic->wq[i]);
		if (ret)
			return ret;
	}

	/* Clean up completed IOs */
	snic_fwcq_cmpl_handler(snic, -1);

	snic_wq_cmpl_handler(snic, -1);

	/* Clean up the IOs that have not completed */
	for (i = 0; i < snic->wq_count; i++)
		svnic_wq_clean(&snic->wq[i], snic_free_wq_buf);

	for (i = 0; i < snic->cq_count; i++)
		svnic_cq_clean(&snic->cq[i]);

	for (i = 0; i < snic->intr_count; i++)
		svnic_intr_clean(&snic->intr[i]);

	/* Cleanup snic specific requests */
	snic_free_all_untagged_reqs(snic);

	/* Cleanup Pending SCSI commands */
	snic_shutdown_scsi_cleanup(snic);

	for (i = 0; i < SNIC_REQ_MAX_CACHES; i++)
		mempool_destroy(snic->req_pool[i]);

	return 0;
} /* end of snic_cleanup */


static void
snic_iounmap(struct snic *snic)
{
	if (snic->bar0.vaddr)
		iounmap(snic->bar0.vaddr);
}

/*
 * snic_vdev_open_done : polls for svnic_dev_open cmd completion.
 */
static int
snic_vdev_open_done(struct vnic_dev *vdev, int *done)
{
	struct snic *snic = svnic_dev_priv(vdev);
	int ret;
	int nretries = 5;

	do {
		ret = svnic_dev_open_done(vdev, done);
		if (ret == 0)
			break;

		SNIC_HOST_INFO(snic->shost, "VNIC_DEV_OPEN Timedout.\n");
	} while (nretries--);

	return ret;
} /* end of snic_vdev_open_done */

/*
 * snic_add_host : registers scsi host with ML
 */
static int
snic_add_host(struct Scsi_Host *shost, struct pci_dev *pdev)
{
	int ret = 0;

	ret = scsi_add_host(shost, &pdev->dev);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "snic: scsi_add_host failed. %d\n",
			      ret);

		return ret;
	}

	SNIC_BUG_ON(shost->work_q != NULL);
	snprintf(shost->work_q_name, sizeof(shost->work_q_name), "scsi_wq_%d",
		 shost->host_no);
	shost->work_q = create_singlethread_workqueue(shost->work_q_name);
	if (!shost->work_q) {
		SNIC_HOST_ERR(shost, "Failed to Create ScsiHost wq.\n");

		ret = -ENOMEM;
	}

	return ret;
} /* end of snic_add_host */

static void
snic_del_host(struct Scsi_Host *shost)
{
	if (!shost->work_q)
		return;

	destroy_workqueue(shost->work_q);
	shost->work_q = NULL;
	scsi_remove_host(shost);
}

int
snic_get_state(struct snic *snic)
{
	return atomic_read(&snic->state);
}

void
snic_set_state(struct snic *snic, enum snic_state state)
{
	SNIC_HOST_INFO(snic->shost, "snic state change from %s to %s\n",
		       snic_state_to_str(snic_get_state(snic)),
		       snic_state_to_str(state));

	atomic_set(&snic->state, state);
}

/*
 * snic_probe : Initialize the snic interface.
 */
static int
snic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct Scsi_Host *shost;
	struct snic *snic;
	mempool_t *pool;
	unsigned long flags;
	u32 max_ios = 0;
	int ret, i;

	/* Device Information */
	SNIC_INFO("snic device %4x:%4x:%4x:%4x: ",
		  pdev->vendor, pdev->device, pdev->subsystem_vendor,
		  pdev->subsystem_device);

	SNIC_INFO("snic device bus %x: slot %x: fn %x\n",
		  pdev->bus->number, PCI_SLOT(pdev->devfn),
		  PCI_FUNC(pdev->devfn));

	/*
	 * Allocate SCSI Host and setup association between host, and snic
	 */
	shost = scsi_host_alloc(&snic_host_template, sizeof(struct snic));
	if (!shost) {
		SNIC_ERR("Unable to alloc scsi_host\n");
		ret = -ENOMEM;

		goto prob_end;
	}
	snic = shost_priv(shost);
	snic->shost = shost;

	snprintf(snic->name, sizeof(snic->name) - 1, "%s%d", SNIC_DRV_NAME,
		 shost->host_no);

	SNIC_HOST_INFO(shost,
		       "snic%d = %p shost = %p device bus %x: slot %x: fn %x\n",
		       shost->host_no, snic, shost, pdev->bus->number,
		       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	/* Per snic debugfs init */
	ret = snic_stats_debugfs_init(snic);
	if (ret) {
		SNIC_HOST_ERR(snic->shost,
			      "Failed to initialize debugfs stats\n");
		snic_stats_debugfs_remove(snic);
	}
#endif

	/* Setup PCI Resources */
	pci_set_drvdata(pdev, snic);
	snic->pdev = pdev;

	ret = pci_enable_device(pdev);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Cannot enable PCI Resources, aborting : %d\n",
			      ret);

		goto err_free_snic;
	}

	ret = pci_request_regions(pdev, SNIC_DRV_NAME);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Cannot obtain PCI Resources, aborting : %d\n",
			      ret);

		goto err_pci_disable;
	}

	pci_set_master(pdev);

	/*
	 * Query PCI Controller on system for DMA addressing
	 * limitation for the device. Try 43-bit first, and
	 * fail to 32-bit.
	 */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(43));
	if (ret) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			SNIC_HOST_ERR(shost,
				      "No Usable DMA Configuration, aborting %d\n",
				      ret);

			goto err_rel_regions;
		}

		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			SNIC_HOST_ERR(shost,
				      "Unable to obtain 32-bit DMA for consistent allocations, aborting: %d\n",
				      ret);

			goto err_rel_regions;
		}
	} else {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(43));
		if (ret) {
			SNIC_HOST_ERR(shost,
				      "Unable to obtain 43-bit DMA for consistent allocations. aborting: %d\n",
				      ret);

			goto err_rel_regions;
		}
	}


	/* Map vNIC resources from BAR0 */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		SNIC_HOST_ERR(shost, "BAR0 not memory mappable aborting.\n");

		ret = -ENODEV;
		goto err_rel_regions;
	}

	snic->bar0.vaddr = pci_iomap(pdev, 0, 0);
	if (!snic->bar0.vaddr) {
		SNIC_HOST_ERR(shost,
			      "Cannot memory map BAR0 res hdr aborting.\n");

		ret = -ENODEV;
		goto err_rel_regions;
	}

	snic->bar0.bus_addr = pci_resource_start(pdev, 0);
	snic->bar0.len = pci_resource_len(pdev, 0);
	SNIC_BUG_ON(snic->bar0.bus_addr == 0);

	/* Devcmd2 Resource Allocation and Initialization */
	snic->vdev = svnic_dev_alloc_discover(NULL, snic, pdev, &snic->bar0, 1);
	if (!snic->vdev) {
		SNIC_HOST_ERR(shost, "vNIC Resource Discovery Failed.\n");

		ret = -ENODEV;
		goto err_iounmap;
	}

	ret = svnic_dev_cmd_init(snic->vdev, 0);
	if (ret) {
		SNIC_HOST_INFO(shost, "Devcmd2 Init Failed. err = %d\n", ret);

		goto err_vnic_unreg;
	}

	ret = snic_dev_wait(snic->vdev, svnic_dev_open, snic_vdev_open_done, 0);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "vNIC dev open failed, aborting. %d\n",
			      ret);

		goto err_vnic_unreg;
	}

	ret = svnic_dev_init(snic->vdev, 0);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "vNIC dev init failed. aborting. %d\n",
			      ret);

		goto err_dev_close;
	}

	/* Get vNIC information */
	ret = snic_get_vnic_config(snic);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Get vNIC configuration failed, aborting. %d\n",
			      ret);

		goto err_dev_close;
	}

	/* Configure Maximum Outstanding IO reqs */
	max_ios = snic->config.io_throttle_count;
	if (max_ios != SNIC_UCSM_DFLT_THROTTLE_CNT_BLD)
		shost->can_queue = min_t(u32, SNIC_MAX_IO_REQ,
					 max_t(u32, SNIC_MIN_IO_REQ, max_ios));

	snic->max_tag_id = shost->can_queue;

	shost->max_lun = snic->config.luns_per_tgt;
	shost->max_id = SNIC_MAX_TARGET;

	shost->max_cmd_len = MAX_COMMAND_SIZE; /*defined in scsi_cmnd.h*/

	snic_get_res_counts(snic);

	/*
	 * Assumption: Only MSIx is supported
	 */
	ret = snic_set_intr_mode(snic);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Failed to set intr mode aborting. %d\n",
			      ret);

		goto err_dev_close;
	}

	ret = snic_alloc_vnic_res(snic);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Failed to alloc vNIC resources aborting. %d\n",
			      ret);

		goto err_clear_intr;
	}

	/* Initialize specific lists */
	INIT_LIST_HEAD(&snic->list);

	/*
	 * spl_cmd_list for maintaining snic specific cmds
	 * such as EXCH_VER_REQ, REPORT_TARGETS etc
	 */
	INIT_LIST_HEAD(&snic->spl_cmd_list);
	spin_lock_init(&snic->spl_cmd_lock);

	/* initialize all snic locks */
	spin_lock_init(&snic->snic_lock);

	for (i = 0; i < SNIC_WQ_MAX; i++)
		spin_lock_init(&snic->wq_lock[i]);

	for (i = 0; i < SNIC_IO_LOCKS; i++)
		spin_lock_init(&snic->io_req_lock[i]);

	pool = mempool_create_slab_pool(2,
				snic_glob->req_cache[SNIC_REQ_CACHE_DFLT_SGL]);
	if (!pool) {
		SNIC_HOST_ERR(shost, "dflt sgl pool creation failed\n");

		goto err_free_res;
	}

	snic->req_pool[SNIC_REQ_CACHE_DFLT_SGL] = pool;

	pool = mempool_create_slab_pool(2,
				snic_glob->req_cache[SNIC_REQ_CACHE_MAX_SGL]);
	if (!pool) {
		SNIC_HOST_ERR(shost, "max sgl pool creation failed\n");

		goto err_free_dflt_sgl_pool;
	}

	snic->req_pool[SNIC_REQ_CACHE_MAX_SGL] = pool;

	pool = mempool_create_slab_pool(2,
				snic_glob->req_cache[SNIC_REQ_TM_CACHE]);
	if (!pool) {
		SNIC_HOST_ERR(shost, "snic tmreq info pool creation failed.\n");

		goto err_free_max_sgl_pool;
	}

	snic->req_pool[SNIC_REQ_TM_CACHE] = pool;

	/* Initialize snic state */
	atomic_set(&snic->state, SNIC_INIT);

	atomic_set(&snic->ios_inflight, 0);

	/* Setup notification buffer area */
	ret = snic_notify_set(snic);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Failed to alloc notify buffer aborting. %d\n",
			      ret);

		goto err_free_tmreq_pool;
	}

	spin_lock_irqsave(&snic_glob->snic_list_lock, flags);
	list_add_tail(&snic->list, &snic_glob->snic_list);
	spin_unlock_irqrestore(&snic_glob->snic_list_lock, flags);

	snic_disc_init(&snic->disc);
	INIT_WORK(&snic->tgt_work, snic_handle_tgt_disc);
	INIT_WORK(&snic->disc_work, snic_handle_disc);
	INIT_WORK(&snic->link_work, snic_handle_link);

	/* Enable all queues */
	for (i = 0; i < snic->wq_count; i++)
		svnic_wq_enable(&snic->wq[i]);

	ret = svnic_dev_enable_wait(snic->vdev);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "vNIC dev enable failed w/ error %d\n",
			      ret);

		goto err_vdev_enable;
	}

	ret = snic_request_intr(snic);
	if (ret) {
		SNIC_HOST_ERR(shost, "Unable to request irq. %d\n", ret);

		goto err_req_intr;
	}

	for (i = 0; i < snic->intr_count; i++)
		svnic_intr_unmask(&snic->intr[i]);

	/* Get snic params */
	ret = snic_get_conf(snic);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Failed to get snic io config from FW w err %d\n",
			      ret);

		goto err_get_conf;
	}

	/*
	 * Initialization done with PCI system, hardware, firmware.
	 * Add shost to SCSI
	 */
	ret = snic_add_host(shost, pdev);
	if (ret) {
		SNIC_HOST_ERR(shost,
			      "Adding scsi host Failed ... exiting. %d\n",
			      ret);

		goto err_get_conf;
	}

	snic_set_state(snic, SNIC_ONLINE);

	ret = snic_disc_start(snic);
	if (ret) {
		SNIC_HOST_ERR(shost, "snic_probe:Discovery Failed w err = %d\n",
			      ret);

		goto err_get_conf;
	}

	SNIC_HOST_INFO(shost, "SNIC Device Probe Successful.\n");

	return 0;

err_get_conf:
	snic_free_all_untagged_reqs(snic);

	for (i = 0; i < snic->intr_count; i++)
		svnic_intr_mask(&snic->intr[i]);

	snic_free_intr(snic);

err_req_intr:
	svnic_dev_disable(snic->vdev);

err_vdev_enable:
	svnic_dev_notify_unset(snic->vdev);

	for (i = 0; i < snic->wq_count; i++) {
		int rc = 0;

		rc = svnic_wq_disable(&snic->wq[i]);
		if (rc) {
			SNIC_HOST_ERR(shost,
				      "WQ Disable Failed w/ err = %d\n", rc);

			 break;
		}
	}
	snic_del_host(snic->shost);

err_free_tmreq_pool:
	mempool_destroy(snic->req_pool[SNIC_REQ_TM_CACHE]);

err_free_max_sgl_pool:
	mempool_destroy(snic->req_pool[SNIC_REQ_CACHE_MAX_SGL]);

err_free_dflt_sgl_pool:
	mempool_destroy(snic->req_pool[SNIC_REQ_CACHE_DFLT_SGL]);

err_free_res:
	snic_free_vnic_res(snic);

err_clear_intr:
	snic_clear_intr_mode(snic);

err_dev_close:
	svnic_dev_close(snic->vdev);

err_vnic_unreg:
	svnic_dev_unregister(snic->vdev);

err_iounmap:
	snic_iounmap(snic);

err_rel_regions:
	pci_release_regions(pdev);

err_pci_disable:
	pci_disable_device(pdev);

err_free_snic:
#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	snic_stats_debugfs_remove(snic);
#endif
	scsi_host_put(shost);
	pci_set_drvdata(pdev, NULL);

prob_end:
	SNIC_INFO("sNIC device : bus %d: slot %d: fn %d Registration Failed.\n",
		  pdev->bus->number, PCI_SLOT(pdev->devfn),
		  PCI_FUNC(pdev->devfn));

	return ret;
} /* end of snic_probe */


/*
 * snic_remove : invoked on unbinding the interface to cleanup the
 * resources allocated in snic_probe on initialization.
 */
static void
snic_remove(struct pci_dev *pdev)
{
	struct snic *snic = pci_get_drvdata(pdev);
	unsigned long flags;

	if (!snic) {
		SNIC_INFO("sNIC dev: bus %d slot %d fn %d snic inst is null.\n",
			  pdev->bus->number, PCI_SLOT(pdev->devfn),
			  PCI_FUNC(pdev->devfn));

		return;
	}

	/*
	 * Mark state so that the workqueue thread stops forwarding
	 * received frames and link events. ISR and other threads
	 * that can queue work items will also stop creating work
	 * items on the snic workqueue
	 */
	snic_set_state(snic, SNIC_OFFLINE);
	spin_lock_irqsave(&snic->snic_lock, flags);
	snic->stop_link_events = 1;
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	flush_workqueue(snic_glob->event_q);
	snic_disc_term(snic);

	spin_lock_irqsave(&snic->snic_lock, flags);
	snic->in_remove = 1;
	spin_unlock_irqrestore(&snic->snic_lock, flags);

	/*
	 * This stops the snic device, masks all interrupts, Completed
	 * CQ entries are drained. Posted WQ/RQ/Copy-WQ entries are
	 * cleanup
	 */
	snic_cleanup(snic);

	spin_lock_irqsave(&snic_glob->snic_list_lock, flags);
	list_del(&snic->list);
	spin_unlock_irqrestore(&snic_glob->snic_list_lock, flags);

	snic_tgt_del_all(snic);
#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	snic_stats_debugfs_remove(snic);
#endif
	snic_del_host(snic->shost);

	svnic_dev_notify_unset(snic->vdev);
	snic_free_intr(snic);
	snic_free_vnic_res(snic);
	snic_clear_intr_mode(snic);
	svnic_dev_close(snic->vdev);
	svnic_dev_unregister(snic->vdev);
	snic_iounmap(snic);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	/* this frees Scsi_Host and snic memory (continuous chunk) */
	scsi_host_put(snic->shost);
} /* end of snic_remove */


struct snic_global *snic_glob;

/*
 * snic_global_data_init: Initialize SNIC Global Data
 * Notes: All the global lists, variables should be part of global data
 * this helps in debugging.
 */
static int
snic_global_data_init(void)
{
	int ret = 0;
	struct kmem_cache *cachep;
	ssize_t len = 0;

	snic_glob = kzalloc(sizeof(*snic_glob), GFP_KERNEL);

	if (!snic_glob) {
		SNIC_ERR("Failed to allocate Global Context.\n");

		ret = -ENOMEM;
		goto gdi_end;
	}

#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	/* Debugfs related Initialization */
	/* Create debugfs entries for snic */
	ret = snic_debugfs_init();
	if (ret < 0) {
		SNIC_ERR("Failed to create sysfs dir for tracing and stats.\n");
		snic_debugfs_term();
		/* continue even if it fails */
	}

	/* Trace related Initialization */
	/* Allocate memory for trace buffer */
	ret = snic_trc_init();
	if (ret < 0) {
		SNIC_ERR("Trace buffer init failed, SNIC tracing disabled\n");
		snic_trc_free();
		/* continue even if it fails */
	}

#endif
	INIT_LIST_HEAD(&snic_glob->snic_list);
	spin_lock_init(&snic_glob->snic_list_lock);

	/* Create a cache for allocation of snic_host_req+default size ESGLs */
	len = sizeof(struct snic_req_info);
	len += sizeof(struct snic_host_req) + sizeof(struct snic_dflt_sgl);
	cachep = kmem_cache_create("snic_req_dfltsgl", len, SNIC_SG_DESC_ALIGN,
				   SLAB_HWCACHE_ALIGN, NULL);
	if (!cachep) {
		SNIC_ERR("Failed to create snic default sgl slab\n");
		ret = -ENOMEM;

		goto err_dflt_req_slab;
	}
	snic_glob->req_cache[SNIC_REQ_CACHE_DFLT_SGL] = cachep;

	/* Create a cache for allocation of max size Extended SGLs */
	len = sizeof(struct snic_req_info);
	len += sizeof(struct snic_host_req) + sizeof(struct snic_max_sgl);
	cachep = kmem_cache_create("snic_req_maxsgl", len, SNIC_SG_DESC_ALIGN,
				   SLAB_HWCACHE_ALIGN, NULL);
	if (!cachep) {
		SNIC_ERR("Failed to create snic max sgl slab\n");
		ret = -ENOMEM;

		goto err_max_req_slab;
	}
	snic_glob->req_cache[SNIC_REQ_CACHE_MAX_SGL] = cachep;

	len = sizeof(struct snic_host_req);
	cachep = kmem_cache_create("snic_req_maxsgl", len, SNIC_SG_DESC_ALIGN,
				   SLAB_HWCACHE_ALIGN, NULL);
	if (!cachep) {
		SNIC_ERR("Failed to create snic tm req slab\n");
		ret = -ENOMEM;

		goto err_tmreq_slab;
	}
	snic_glob->req_cache[SNIC_REQ_TM_CACHE] = cachep;

	/* snic_event queue */
	snic_glob->event_q = create_singlethread_workqueue("snic_event_wq");
	if (!snic_glob->event_q) {
		SNIC_ERR("snic event queue create failed\n");
		ret = -ENOMEM;

		goto err_eventq;
	}

	return ret;

err_eventq:
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_TM_CACHE]);

err_tmreq_slab:
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_CACHE_MAX_SGL]);

err_max_req_slab:
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_CACHE_DFLT_SGL]);

err_dflt_req_slab:
#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	snic_trc_free();
	snic_debugfs_term();
#endif
	kfree(snic_glob);
	snic_glob = NULL;

gdi_end:
	return ret;
} /* end of snic_glob_init */

/*
 * snic_global_data_cleanup : Frees SNIC Global Data
 */
static void
snic_global_data_cleanup(void)
{
	SNIC_BUG_ON(snic_glob == NULL);

	destroy_workqueue(snic_glob->event_q);
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_TM_CACHE]);
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_CACHE_MAX_SGL]);
	kmem_cache_destroy(snic_glob->req_cache[SNIC_REQ_CACHE_DFLT_SGL]);

#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	/* Freeing Trace Resources */
	snic_trc_free();

	/* Freeing Debugfs Resources */
	snic_debugfs_term();
#endif
	kfree(snic_glob);
	snic_glob = NULL;
} /* end of snic_glob_cleanup */

static struct pci_driver snic_driver = {
	.name = SNIC_DRV_NAME,
	.id_table = snic_id_table,
	.probe = snic_probe,
	.remove = snic_remove,
};

static int __init
snic_init_module(void)
{
	int ret = 0;

#ifndef __x86_64__
	SNIC_INFO("SNIC Driver is supported only for x86_64 platforms!\n");
	add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
#endif

	SNIC_INFO("%s, ver %s\n", SNIC_DRV_DESCRIPTION, SNIC_DRV_VERSION);

	ret = snic_global_data_init();
	if (ret) {
		SNIC_ERR("Failed to Initialize Global Data.\n");

		return ret;
	}

	ret = pci_register_driver(&snic_driver);
	if (ret < 0) {
		SNIC_ERR("PCI driver register error\n");

		goto err_pci_reg;
	}

	return ret;

err_pci_reg:
	snic_global_data_cleanup();

	return ret;
}

static void __exit
snic_cleanup_module(void)
{
	pci_unregister_driver(&snic_driver);
	snic_global_data_cleanup();
}

module_init(snic_init_module);
module_exit(snic_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(SNIC_DRV_DESCRIPTION);
MODULE_VERSION(SNIC_DRV_VERSION);
MODULE_DEVICE_TABLE(pci, snic_id_table);
MODULE_AUTHOR("Narsimhulu Musini <nmusini@cisco.com>, "
	      "Sesidhar Baddela <sebaddel@cisco.com>");
