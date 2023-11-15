// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
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
#include <linux/if_ether.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libfc.h>
#include <scsi/fc_frame.h>

#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "fnic_io.h"
#include "fnic_fip.h"
#include "fnic.h"

#define PCI_DEVICE_ID_CISCO_FNIC	0x0045

/* Timer to poll notification area for events. Used for MSI interrupts */
#define FNIC_NOTIFY_TIMER_PERIOD	(2 * HZ)

static struct kmem_cache *fnic_sgl_cache[FNIC_SGL_NUM_CACHES];
static struct kmem_cache *fnic_io_req_cache;
static LIST_HEAD(fnic_list);
static DEFINE_SPINLOCK(fnic_list_lock);

/* Supported devices by fnic module */
static struct pci_device_id fnic_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CISCO, PCI_DEVICE_ID_CISCO_FNIC) },
	{ 0, }
};

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Abhijeet Joglekar <abjoglek@cisco.com>, "
	      "Joseph R. Eykholt <jeykholt@cisco.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, fnic_id_table);

unsigned int fnic_log_level;
module_param(fnic_log_level, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fnic_log_level, "bit mask of fnic logging levels");


unsigned int io_completions = FNIC_DFLT_IO_COMPLETIONS;
module_param(io_completions, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(io_completions, "Max CQ entries to process at a time");

unsigned int fnic_trace_max_pages = 16;
module_param(fnic_trace_max_pages, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fnic_trace_max_pages, "Total allocated memory pages "
					"for fnic trace buffer");

unsigned int fnic_fc_trace_max_pages = 64;
module_param(fnic_fc_trace_max_pages, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fnic_fc_trace_max_pages,
		 "Total allocated memory pages for fc trace buffer");

static unsigned int fnic_max_qdepth = FNIC_DFLT_QUEUE_DEPTH;
module_param(fnic_max_qdepth, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fnic_max_qdepth, "Queue depth to report for each LUN");

static struct libfc_function_template fnic_transport_template = {
	.frame_send = fnic_send,
	.lport_set_port_id = fnic_set_port_id,
	.fcp_abort_io = fnic_empty_scsi_cleanup,
	.fcp_cleanup = fnic_empty_scsi_cleanup,
	.exch_mgr_reset = fnic_exch_mgr_reset
};

static int fnic_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	scsi_change_queue_depth(sdev, fnic_max_qdepth);
	return 0;
}

static const struct scsi_host_template fnic_host_template = {
	.module = THIS_MODULE,
	.name = DRV_NAME,
	.queuecommand = fnic_queuecommand,
	.eh_timed_out = fc_eh_timed_out,
	.eh_abort_handler = fnic_abort_cmd,
	.eh_device_reset_handler = fnic_device_reset,
	.eh_host_reset_handler = fnic_host_reset,
	.slave_alloc = fnic_slave_alloc,
	.change_queue_depth = scsi_change_queue_depth,
	.this_id = -1,
	.cmd_per_lun = 3,
	.can_queue = FNIC_DFLT_IO_REQ,
	.sg_tablesize = FNIC_MAX_SG_DESC_CNT,
	.max_sectors = 0xffff,
	.shost_groups = fnic_host_groups,
	.track_queue_depth = 1,
	.cmd_size = sizeof(struct fnic_cmd_priv),
};

static void
fnic_set_rport_dev_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;
}

static void fnic_get_host_speed(struct Scsi_Host *shost);
static struct scsi_transport_template *fnic_fc_transport;
static struct fc_host_statistics *fnic_get_stats(struct Scsi_Host *);
static void fnic_reset_host_stats(struct Scsi_Host *);

static struct fc_function_template fnic_fc_functions = {

	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_active_fc4s = 1,
	.show_host_maxframe_size = 1,
	.show_host_port_id = 1,
	.show_host_supported_speeds = 1,
	.get_host_speed = fnic_get_host_speed,
	.show_host_speed = 1,
	.show_host_port_type = 1,
	.get_host_port_state = fc_get_host_port_state,
	.show_host_port_state = 1,
	.show_host_symbolic_name = 1,
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_host_fabric_name = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,
	.show_rport_dev_loss_tmo = 1,
	.set_rport_dev_loss_tmo = fnic_set_rport_dev_loss_tmo,
	.issue_fc_host_lip = fnic_reset,
	.get_fc_host_stats = fnic_get_stats,
	.reset_fc_host_stats = fnic_reset_host_stats,
	.dd_fcrport_size = sizeof(struct fc_rport_libfc_priv),
	.terminate_rport_io = fnic_terminate_rport_io,
	.bsg_request = fc_lport_bsg_request,
};

static void fnic_get_host_speed(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);
	struct fnic *fnic = lport_priv(lp);
	u32 port_speed = vnic_dev_port_speed(fnic->vdev);

	/* Add in other values as they get defined in fw */
	switch (port_speed) {
	case DCEM_PORTSPEED_10G:
		fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
		break;
	case DCEM_PORTSPEED_20G:
		fc_host_speed(shost) = FC_PORTSPEED_20GBIT;
		break;
	case DCEM_PORTSPEED_25G:
		fc_host_speed(shost) = FC_PORTSPEED_25GBIT;
		break;
	case DCEM_PORTSPEED_40G:
	case DCEM_PORTSPEED_4x10G:
		fc_host_speed(shost) = FC_PORTSPEED_40GBIT;
		break;
	case DCEM_PORTSPEED_100G:
		fc_host_speed(shost) = FC_PORTSPEED_100GBIT;
		break;
	default:
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		break;
	}
}

static struct fc_host_statistics *fnic_get_stats(struct Scsi_Host *host)
{
	int ret;
	struct fc_lport *lp = shost_priv(host);
	struct fnic *fnic = lport_priv(lp);
	struct fc_host_statistics *stats = &lp->host_stats;
	struct vnic_stats *vs;
	unsigned long flags;

	if (time_before(jiffies, fnic->stats_time + HZ / FNIC_STATS_RATE_LIMIT))
		return stats;
	fnic->stats_time = jiffies;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	ret = vnic_dev_stats_dump(fnic->vdev, &fnic->stats);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (ret) {
		FNIC_MAIN_DBG(KERN_DEBUG, fnic->lport->host,
			      "fnic: Get vnic stats failed"
			      " 0x%x", ret);
		return stats;
	}
	vs = fnic->stats;
	stats->tx_frames = vs->tx.tx_unicast_frames_ok;
	stats->tx_words  = vs->tx.tx_unicast_bytes_ok / 4;
	stats->rx_frames = vs->rx.rx_unicast_frames_ok;
	stats->rx_words  = vs->rx.rx_unicast_bytes_ok / 4;
	stats->error_frames = vs->tx.tx_errors + vs->rx.rx_errors;
	stats->dumped_frames = vs->tx.tx_drops + vs->rx.rx_drop;
	stats->invalid_crc_count = vs->rx.rx_crc_errors;
	stats->seconds_since_last_reset =
			(jiffies - fnic->stats_reset_time) / HZ;
	stats->fcp_input_megabytes = div_u64(fnic->fcp_input_bytes, 1000000);
	stats->fcp_output_megabytes = div_u64(fnic->fcp_output_bytes, 1000000);

	return stats;
}

/*
 * fnic_dump_fchost_stats
 * note : dumps fc_statistics into system logs
 */
void fnic_dump_fchost_stats(struct Scsi_Host *host,
				struct fc_host_statistics *stats)
{
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: seconds since last reset = %llu\n",
			stats->seconds_since_last_reset);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: tx frames		= %llu\n",
			stats->tx_frames);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: tx words		= %llu\n",
			stats->tx_words);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: rx frames		= %llu\n",
			stats->rx_frames);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: rx words		= %llu\n",
			stats->rx_words);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: lip count		= %llu\n",
			stats->lip_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: nos count		= %llu\n",
			stats->nos_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: error frames		= %llu\n",
			stats->error_frames);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: dumped frames	= %llu\n",
			stats->dumped_frames);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: link failure count	= %llu\n",
			stats->link_failure_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: loss of sync count	= %llu\n",
			stats->loss_of_sync_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: loss of signal count	= %llu\n",
			stats->loss_of_signal_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: prim seq protocol err count = %llu\n",
			stats->prim_seq_protocol_err_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: invalid tx word count= %llu\n",
			stats->invalid_tx_word_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: invalid crc count	= %llu\n",
			stats->invalid_crc_count);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: fcp input requests	= %llu\n",
			stats->fcp_input_requests);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: fcp output requests	= %llu\n",
			stats->fcp_output_requests);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: fcp control requests	= %llu\n",
			stats->fcp_control_requests);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: fcp input megabytes	= %llu\n",
			stats->fcp_input_megabytes);
	FNIC_MAIN_NOTE(KERN_NOTICE, host,
			"fnic: fcp output megabytes	= %llu\n",
			stats->fcp_output_megabytes);
	return;
}

/*
 * fnic_reset_host_stats : clears host stats
 * note : called when reset_statistics set under sysfs dir
 */
static void fnic_reset_host_stats(struct Scsi_Host *host)
{
	int ret;
	struct fc_lport *lp = shost_priv(host);
	struct fnic *fnic = lport_priv(lp);
	struct fc_host_statistics *stats;
	unsigned long flags;

	/* dump current stats, before clearing them */
	stats = fnic_get_stats(host);
	fnic_dump_fchost_stats(host, stats);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	ret = vnic_dev_stats_clear(fnic->vdev);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (ret) {
		FNIC_MAIN_DBG(KERN_DEBUG, fnic->lport->host,
				"fnic: Reset vnic stats failed"
				" 0x%x", ret);
		return;
	}
	fnic->stats_reset_time = jiffies;
	memset(stats, 0, sizeof(*stats));

	return;
}

void fnic_log_q_error(struct fnic *fnic)
{
	unsigned int i;
	u32 error_status;

	for (i = 0; i < fnic->raw_wq_count; i++) {
		error_status = ioread32(&fnic->wq[i].ctrl->error_status);
		if (error_status)
			shost_printk(KERN_ERR, fnic->lport->host,
				     "WQ[%d] error_status"
				     " %d\n", i, error_status);
	}

	for (i = 0; i < fnic->rq_count; i++) {
		error_status = ioread32(&fnic->rq[i].ctrl->error_status);
		if (error_status)
			shost_printk(KERN_ERR, fnic->lport->host,
				     "RQ[%d] error_status"
				     " %d\n", i, error_status);
	}

	for (i = 0; i < fnic->wq_copy_count; i++) {
		error_status = ioread32(&fnic->wq_copy[i].ctrl->error_status);
		if (error_status)
			shost_printk(KERN_ERR, fnic->lport->host,
				     "CWQ[%d] error_status"
				     " %d\n", i, error_status);
	}
}

void fnic_handle_link_event(struct fnic *fnic)
{
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	queue_work(fnic_event_queue, &fnic->link_work);

}

static int fnic_notify_set(struct fnic *fnic)
{
	int err;

	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		err = vnic_dev_notify_set(fnic->vdev, FNIC_INTX_NOTIFY);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		err = vnic_dev_notify_set(fnic->vdev, -1);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		err = vnic_dev_notify_set(fnic->vdev, FNIC_MSIX_ERR_NOTIFY);
		break;
	default:
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Interrupt mode should be set up"
			     " before devcmd notify set %d\n",
			     vnic_dev_get_intr_mode(fnic->vdev));
		err = -1;
		break;
	}

	return err;
}

static void fnic_notify_timer(struct timer_list *t)
{
	struct fnic *fnic = from_timer(fnic, t, notify_timer);

	fnic_handle_link_event(fnic);
	mod_timer(&fnic->notify_timer,
		  round_jiffies(jiffies + FNIC_NOTIFY_TIMER_PERIOD));
}

static void fnic_fip_notify_timer(struct timer_list *t)
{
	struct fnic *fnic = from_timer(fnic, t, fip_timer);

	fnic_handle_fip_timer(fnic);
}

static void fnic_notify_timer_start(struct fnic *fnic)
{
	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
		/*
		 * Schedule first timeout immediately. The driver is
		 * initiatialized and ready to look for link up notification
		 */
		mod_timer(&fnic->notify_timer, jiffies);
		break;
	default:
		/* Using intr for notification for INTx/MSI-X */
		break;
	}
}

static int fnic_dev_wait(struct vnic_dev *vdev,
			 int (*start)(struct vnic_dev *, int),
			 int (*finished)(struct vnic_dev *, int *),
			 int arg)
{
	unsigned long time;
	int done;
	int err;
	int count;

	count = 0;

	err = start(vdev, arg);
	if (err)
		return err;

	/* Wait for func to complete.
	* Sometime schedule_timeout_uninterruptible take long time
	* to wake up so we do not retry as we are only waiting for
	* 2 seconds in while loop. By adding count, we make sure
	* we try atleast three times before returning -ETIMEDOUT
	*/
	time = jiffies + (HZ * 2);
	do {
		err = finished(vdev, &done);
		count++;
		if (err)
			return err;
		if (done)
			return 0;
		schedule_timeout_uninterruptible(HZ / 10);
	} while (time_after(time, jiffies) || (count < 3));

	return -ETIMEDOUT;
}

static int fnic_cleanup(struct fnic *fnic)
{
	unsigned int i;
	int err;

	vnic_dev_disable(fnic->vdev);
	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_mask(&fnic->intr[i]);

	for (i = 0; i < fnic->rq_count; i++) {
		err = vnic_rq_disable(&fnic->rq[i]);
		if (err)
			return err;
	}
	for (i = 0; i < fnic->raw_wq_count; i++) {
		err = vnic_wq_disable(&fnic->wq[i]);
		if (err)
			return err;
	}
	for (i = 0; i < fnic->wq_copy_count; i++) {
		err = vnic_wq_copy_disable(&fnic->wq_copy[i]);
		if (err)
			return err;
	}

	/* Clean up completed IOs and FCS frames */
	fnic_wq_copy_cmpl_handler(fnic, io_completions);
	fnic_wq_cmpl_handler(fnic, -1);
	fnic_rq_cmpl_handler(fnic, -1);

	/* Clean up the IOs and FCS frames that have not completed */
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_clean(&fnic->wq[i], fnic_free_wq_buf);
	for (i = 0; i < fnic->rq_count; i++)
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);
	for (i = 0; i < fnic->wq_copy_count; i++)
		vnic_wq_copy_clean(&fnic->wq_copy[i],
				   fnic_wq_copy_cleanup_handler);

	for (i = 0; i < fnic->cq_count; i++)
		vnic_cq_clean(&fnic->cq[i]);
	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_clean(&fnic->intr[i]);

	mempool_destroy(fnic->io_req_pool);
	for (i = 0; i < FNIC_SGL_NUM_CACHES; i++)
		mempool_destroy(fnic->io_sgl_pool[i]);

	return 0;
}

static void fnic_iounmap(struct fnic *fnic)
{
	if (fnic->bar0.vaddr)
		iounmap(fnic->bar0.vaddr);
}

/**
 * fnic_get_mac() - get assigned data MAC address for FIP code.
 * @lport: 	local port.
 */
static u8 *fnic_get_mac(struct fc_lport *lport)
{
	struct fnic *fnic = lport_priv(lport);

	return fnic->data_src_addr;
}

static void fnic_set_vlan(struct fnic *fnic, u16 vlan_id)
{
	vnic_dev_set_default_vlan(fnic->vdev, vlan_id);
}

static int fnic_scsi_drv_init(struct fnic *fnic)
{
	struct Scsi_Host *host = fnic->lport->host;

	/* Configure maximum outstanding IO reqs*/
	if (fnic->config.io_throttle_count != FNIC_UCSM_DFLT_THROTTLE_CNT_BLD)
		host->can_queue = min_t(u32, FNIC_MAX_IO_REQ,
					max_t(u32, FNIC_MIN_IO_REQ,
					fnic->config.io_throttle_count));

	fnic->fnic_max_tag_id = host->can_queue;
	host->max_lun = fnic->config.luns_per_tgt;
	host->max_id = FNIC_MAX_FCP_TARGET;
	host->max_cmd_len = FCOE_MAX_CMD_LEN;

	host->nr_hw_queues = fnic->wq_copy_count;
	if (host->nr_hw_queues > 1)
		shost_printk(KERN_ERR, host,
				"fnic: blk-mq is not supported");

	host->nr_hw_queues = fnic->wq_copy_count = 1;

	shost_printk(KERN_INFO, host,
			"fnic: can_queue: %d max_lun: %llu",
			host->can_queue, host->max_lun);

	shost_printk(KERN_INFO, host,
			"fnic: max_id: %d max_cmd_len: %d nr_hw_queues: %d",
			host->max_id, host->max_cmd_len, host->nr_hw_queues);

	return 0;
}

static int fnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct Scsi_Host *host;
	struct fc_lport *lp;
	struct fnic *fnic;
	mempool_t *pool;
	int err;
	int i;
	unsigned long flags;

	/*
	 * Allocate SCSI Host and set up association between host,
	 * local port, and fnic
	 */
	lp = libfc_host_alloc(&fnic_host_template, sizeof(struct fnic));
	if (!lp) {
		printk(KERN_ERR PFX "Unable to alloc libfc local port\n");
		err = -ENOMEM;
		goto err_out;
	}
	host = lp->host;
	fnic = lport_priv(lp);
	fnic->lport = lp;
	fnic->ctlr.lp = lp;

	fnic->link_events = 0;

	snprintf(fnic->name, sizeof(fnic->name) - 1, "%s%d", DRV_NAME,
		 host->host_no);

	host->transportt = fnic_fc_transport;

	fnic_stats_debugfs_init(fnic);

	/* Setup PCI resources */
	pci_set_drvdata(pdev, fnic);

	fnic->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Cannot enable PCI device, aborting.\n");
		goto err_out_free_hba;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Cannot enable PCI resources, aborting\n");
		goto err_out_disable_device;
	}

	pci_set_master(pdev);

	/* Query PCI controller on system for DMA addressing
	 * limitation for the device.  Try 47-bit first, and
	 * fail to 32-bit. Cisco VIC supports 47 bits only.
	 */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(47));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			shost_printk(KERN_ERR, fnic->lport->host,
				     "No usable DMA configuration "
				     "aborting\n");
			goto err_out_release_regions;
		}
	}

	/* Map vNIC resources from BAR0 */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "BAR0 not memory-map'able, aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	fnic->bar0.vaddr = pci_iomap(pdev, 0, 0);
	fnic->bar0.bus_addr = pci_resource_start(pdev, 0);
	fnic->bar0.len = pci_resource_len(pdev, 0);

	if (!fnic->bar0.vaddr) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Cannot memory-map BAR0 res hdr, "
			     "aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	fnic->vdev = vnic_dev_register(NULL, fnic, pdev, &fnic->bar0);
	if (!fnic->vdev) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "vNIC registration failed, "
			     "aborting.\n");
		err = -ENODEV;
		goto err_out_iounmap;
	}

	err = vnic_dev_cmd_init(fnic->vdev);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
				"vnic_dev_cmd_init() returns %d, aborting\n",
				err);
		goto err_out_vnic_unregister;
	}

	err = fnic_dev_wait(fnic->vdev, vnic_dev_open,
			    vnic_dev_open_done, CMD_OPENF_RQ_ENABLE_THEN_POST);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "vNIC dev open failed, aborting.\n");
		goto err_out_dev_cmd_deinit;
	}

	err = vnic_dev_init(fnic->vdev, 0);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "vNIC dev init failed, aborting.\n");
		goto err_out_dev_close;
	}

	err = vnic_dev_mac_addr(fnic->vdev, fnic->ctlr.ctl_src_addr);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "vNIC get MAC addr failed \n");
		goto err_out_dev_close;
	}
	/* set data_src for point-to-point mode and to keep it non-zero */
	memcpy(fnic->data_src_addr, fnic->ctlr.ctl_src_addr, ETH_ALEN);

	/* Get vNIC configuration */
	err = fnic_get_vnic_config(fnic);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Get vNIC configuration failed, "
			     "aborting.\n");
		goto err_out_dev_close;
	}

	fnic_scsi_drv_init(fnic);

	fnic_get_res_counts(fnic);

	err = fnic_set_intr_mode(fnic);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Failed to set intr mode, "
			     "aborting.\n");
		goto err_out_dev_close;
	}

	err = fnic_alloc_vnic_resources(fnic);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Failed to alloc vNIC resources, "
			     "aborting.\n");
		goto err_out_clear_intr;
	}


	/* initialize all fnic locks */
	spin_lock_init(&fnic->fnic_lock);

	for (i = 0; i < FNIC_WQ_MAX; i++)
		spin_lock_init(&fnic->wq_lock[i]);

	for (i = 0; i < FNIC_WQ_COPY_MAX; i++) {
		spin_lock_init(&fnic->wq_copy_lock[i]);
		fnic->wq_copy_desc_low[i] = DESC_CLEAN_LOW_WATERMARK;
		fnic->fw_ack_recd[i] = 0;
		fnic->fw_ack_index[i] = -1;
	}

	for (i = 0; i < FNIC_IO_LOCKS; i++)
		spin_lock_init(&fnic->io_req_lock[i]);

	spin_lock_init(&fnic->sgreset_lock);

	err = -ENOMEM;
	fnic->io_req_pool = mempool_create_slab_pool(2, fnic_io_req_cache);
	if (!fnic->io_req_pool)
		goto err_out_free_resources;

	pool = mempool_create_slab_pool(2, fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	if (!pool)
		goto err_out_free_ioreq_pool;
	fnic->io_sgl_pool[FNIC_SGL_CACHE_DFLT] = pool;

	pool = mempool_create_slab_pool(2, fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	if (!pool)
		goto err_out_free_dflt_pool;
	fnic->io_sgl_pool[FNIC_SGL_CACHE_MAX] = pool;

	/* setup vlan config, hw inserts vlan header */
	fnic->vlan_hw_insert = 1;
	fnic->vlan_id = 0;

	/* Initialize the FIP fcoe_ctrl struct */
	fnic->ctlr.send = fnic_eth_send;
	fnic->ctlr.update_mac = fnic_update_mac;
	fnic->ctlr.get_src_addr = fnic_get_mac;
	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		shost_printk(KERN_INFO, fnic->lport->host,
			     "firmware supports FIP\n");
		/* enable directed and multicast */
		vnic_dev_packet_filter(fnic->vdev, 1, 1, 0, 0, 0);
		vnic_dev_add_addr(fnic->vdev, FIP_ALL_ENODE_MACS);
		vnic_dev_add_addr(fnic->vdev, fnic->ctlr.ctl_src_addr);
		fnic->set_vlan = fnic_set_vlan;
		fcoe_ctlr_init(&fnic->ctlr, FIP_MODE_AUTO);
		timer_setup(&fnic->fip_timer, fnic_fip_notify_timer, 0);
		spin_lock_init(&fnic->vlans_lock);
		INIT_WORK(&fnic->fip_frame_work, fnic_handle_fip_frame);
		INIT_WORK(&fnic->event_work, fnic_handle_event);
		skb_queue_head_init(&fnic->fip_frame_queue);
		INIT_LIST_HEAD(&fnic->evlist);
		INIT_LIST_HEAD(&fnic->vlans);
	} else {
		shost_printk(KERN_INFO, fnic->lport->host,
			     "firmware uses non-FIP mode\n");
		fcoe_ctlr_init(&fnic->ctlr, FIP_MODE_NON_FIP);
		fnic->ctlr.state = FIP_ST_NON_FIP;
	}
	fnic->state = FNIC_IN_FC_MODE;

	atomic_set(&fnic->in_flight, 0);
	fnic->state_flags = FNIC_FLAGS_NONE;

	/* Enable hardware stripping of vlan header on ingress */
	fnic_set_nic_config(fnic, 0, 0, 0, 0, 0, 0, 1);

	/* Setup notification buffer area */
	err = fnic_notify_set(fnic);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Failed to alloc notify buffer, aborting.\n");
		goto err_out_free_max_pool;
	}

	/* Setup notify timer when using MSI interrupts */
	if (vnic_dev_get_intr_mode(fnic->vdev) == VNIC_DEV_INTR_MODE_MSI)
		timer_setup(&fnic->notify_timer, fnic_notify_timer, 0);

	/* allocate RQ buffers and post them to RQ*/
	for (i = 0; i < fnic->rq_count; i++) {
		vnic_rq_enable(&fnic->rq[i]);
		err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
		if (err) {
			shost_printk(KERN_ERR, fnic->lport->host,
				     "fnic_alloc_rq_frame can't alloc "
				     "frame\n");
			goto err_out_free_rq_buf;
		}
	}

	/*
	 * Initialization done with PCI system, hardware, firmware.
	 * Add host to SCSI
	 */
	err = scsi_add_host(lp->host, &pdev->dev);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "fnic: scsi_add_host failed...exiting\n");
		goto err_out_free_rq_buf;
	}

	/* Start local port initiatialization */

	lp->link_up = 0;

	lp->max_retry_count = fnic->config.flogi_retries;
	lp->max_rport_retry_count = fnic->config.plogi_retries;
	lp->service_params = (FCP_SPPF_INIT_FCN | FCP_SPPF_RD_XRDY_DIS |
			      FCP_SPPF_CONF_COMPL);
	if (fnic->config.flags & VFCF_FCP_SEQ_LVL_ERR)
		lp->service_params |= FCP_SPPF_RETRY;

	lp->boot_time = jiffies;
	lp->e_d_tov = fnic->config.ed_tov;
	lp->r_a_tov = fnic->config.ra_tov;
	lp->link_supported_speeds = FC_PORTSPEED_10GBIT;
	fc_set_wwnn(lp, fnic->config.node_wwn);
	fc_set_wwpn(lp, fnic->config.port_wwn);

	fcoe_libfc_config(lp, &fnic->ctlr, &fnic_transport_template, 0);

	if (!fc_exch_mgr_alloc(lp, FC_CLASS_3, FCPIO_HOST_EXCH_RANGE_START,
			       FCPIO_HOST_EXCH_RANGE_END, NULL)) {
		err = -ENOMEM;
		goto err_out_remove_scsi_host;
	}

	fc_lport_init_stats(lp);
	fnic->stats_reset_time = jiffies;

	fc_lport_config(lp);

	if (fc_set_mfs(lp, fnic->config.maxdatafieldsize +
		       sizeof(struct fc_frame_header))) {
		err = -EINVAL;
		goto err_out_free_exch_mgr;
	}
	fc_host_maxframe_size(lp->host) = lp->mfs;
	fc_host_dev_loss_tmo(lp->host) = fnic->config.port_down_timeout / 1000;

	sprintf(fc_host_symbolic_name(lp->host),
		DRV_NAME " v" DRV_VERSION " over %s", fnic->name);

	spin_lock_irqsave(&fnic_list_lock, flags);
	list_add_tail(&fnic->list, &fnic_list);
	spin_unlock_irqrestore(&fnic_list_lock, flags);

	INIT_WORK(&fnic->link_work, fnic_handle_link);
	INIT_WORK(&fnic->frame_work, fnic_handle_frame);
	skb_queue_head_init(&fnic->frame_queue);
	skb_queue_head_init(&fnic->tx_queue);

	/* Enable all queues */
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_enable(&fnic->wq[i]);
	for (i = 0; i < fnic->wq_copy_count; i++)
		vnic_wq_copy_enable(&fnic->wq_copy[i]);

	fc_fabric_login(lp);

	err = fnic_request_intr(fnic);
	if (err) {
		shost_printk(KERN_ERR, fnic->lport->host,
			     "Unable to request irq.\n");
		goto err_out_free_exch_mgr;
	}

	vnic_dev_enable(fnic->vdev);

	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_unmask(&fnic->intr[i]);

	fnic_notify_timer_start(fnic);

	return 0;

err_out_free_exch_mgr:
	fc_exch_mgr_free(lp);
err_out_remove_scsi_host:
	fc_remove_host(lp->host);
	scsi_remove_host(lp->host);
err_out_free_rq_buf:
	for (i = 0; i < fnic->rq_count; i++)
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);
	vnic_dev_notify_unset(fnic->vdev);
err_out_free_max_pool:
	mempool_destroy(fnic->io_sgl_pool[FNIC_SGL_CACHE_MAX]);
err_out_free_dflt_pool:
	mempool_destroy(fnic->io_sgl_pool[FNIC_SGL_CACHE_DFLT]);
err_out_free_ioreq_pool:
	mempool_destroy(fnic->io_req_pool);
err_out_free_resources:
	fnic_free_vnic_resources(fnic);
err_out_clear_intr:
	fnic_clear_intr_mode(fnic);
err_out_dev_close:
	vnic_dev_close(fnic->vdev);
err_out_dev_cmd_deinit:
err_out_vnic_unregister:
	vnic_dev_unregister(fnic->vdev);
err_out_iounmap:
	fnic_iounmap(fnic);
err_out_release_regions:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
err_out_free_hba:
	fnic_stats_debugfs_remove(fnic);
	scsi_host_put(lp->host);
err_out:
	return err;
}

static void fnic_remove(struct pci_dev *pdev)
{
	struct fnic *fnic = pci_get_drvdata(pdev);
	struct fc_lport *lp = fnic->lport;
	unsigned long flags;

	/*
	 * Mark state so that the workqueue thread stops forwarding
	 * received frames and link events to the local port. ISR and
	 * other threads that can queue work items will also stop
	 * creating work items on the fnic workqueue
	 */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->stop_rx_link_events = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (vnic_dev_get_intr_mode(fnic->vdev) == VNIC_DEV_INTR_MODE_MSI)
		del_timer_sync(&fnic->notify_timer);

	/*
	 * Flush the fnic event queue. After this call, there should
	 * be no event queued for this fnic device in the workqueue
	 */
	flush_workqueue(fnic_event_queue);
	skb_queue_purge(&fnic->frame_queue);
	skb_queue_purge(&fnic->tx_queue);

	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		del_timer_sync(&fnic->fip_timer);
		skb_queue_purge(&fnic->fip_frame_queue);
		fnic_fcoe_reset_vlans(fnic);
		fnic_fcoe_evlist_free(fnic);
	}

	/*
	 * Log off the fabric. This stops all remote ports, dns port,
	 * logs off the fabric. This flushes all rport, disc, lport work
	 * before returning
	 */
	fc_fabric_logoff(fnic->lport);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->in_remove = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	fcoe_ctlr_destroy(&fnic->ctlr);
	fc_lport_destroy(lp);
	fnic_stats_debugfs_remove(fnic);

	/*
	 * This stops the fnic device, masks all interrupts. Completed
	 * CQ entries are drained. Posted WQ/RQ/Copy-WQ entries are
	 * cleaned up
	 */
	fnic_cleanup(fnic);

	BUG_ON(!skb_queue_empty(&fnic->frame_queue));
	BUG_ON(!skb_queue_empty(&fnic->tx_queue));

	spin_lock_irqsave(&fnic_list_lock, flags);
	list_del(&fnic->list);
	spin_unlock_irqrestore(&fnic_list_lock, flags);

	fc_remove_host(fnic->lport->host);
	scsi_remove_host(fnic->lport->host);
	fc_exch_mgr_free(fnic->lport);
	vnic_dev_notify_unset(fnic->vdev);
	fnic_free_intr(fnic);
	fnic_free_vnic_resources(fnic);
	fnic_clear_intr_mode(fnic);
	vnic_dev_close(fnic->vdev);
	vnic_dev_unregister(fnic->vdev);
	fnic_iounmap(fnic);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	scsi_host_put(lp->host);
}

static struct pci_driver fnic_driver = {
	.name = DRV_NAME,
	.id_table = fnic_id_table,
	.probe = fnic_probe,
	.remove = fnic_remove,
};

static int __init fnic_init_module(void)
{
	size_t len;
	int err = 0;

	printk(KERN_INFO PFX "%s, ver %s\n", DRV_DESCRIPTION, DRV_VERSION);

	/* Create debugfs entries for fnic */
	err = fnic_debugfs_init();
	if (err < 0) {
		printk(KERN_ERR PFX "Failed to create fnic directory "
				"for tracing and stats logging\n");
		fnic_debugfs_terminate();
	}

	/* Allocate memory for trace buffer */
	err = fnic_trace_buf_init();
	if (err < 0) {
		printk(KERN_ERR PFX
		       "Trace buffer initialization Failed. "
		       "Fnic Tracing utility is disabled\n");
		fnic_trace_free();
	}

    /* Allocate memory for fc trace buffer */
	err = fnic_fc_trace_init();
	if (err < 0) {
		printk(KERN_ERR PFX "FC trace buffer initialization Failed "
		       "FC frame tracing utility is disabled\n");
		fnic_fc_trace_free();
	}

	/* Create a cache for allocation of default size sgls */
	len = sizeof(struct fnic_dflt_sgl_list);
	fnic_sgl_cache[FNIC_SGL_CACHE_DFLT] = kmem_cache_create
		("fnic_sgl_dflt", len + FNIC_SG_DESC_ALIGN, FNIC_SG_DESC_ALIGN,
		 SLAB_HWCACHE_ALIGN,
		 NULL);
	if (!fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]) {
		printk(KERN_ERR PFX "failed to create fnic dflt sgl slab\n");
		err = -ENOMEM;
		goto err_create_fnic_sgl_slab_dflt;
	}

	/* Create a cache for allocation of max size sgls*/
	len = sizeof(struct fnic_sgl_list);
	fnic_sgl_cache[FNIC_SGL_CACHE_MAX] = kmem_cache_create
		("fnic_sgl_max", len + FNIC_SG_DESC_ALIGN, FNIC_SG_DESC_ALIGN,
		  SLAB_HWCACHE_ALIGN,
		  NULL);
	if (!fnic_sgl_cache[FNIC_SGL_CACHE_MAX]) {
		printk(KERN_ERR PFX "failed to create fnic max sgl slab\n");
		err = -ENOMEM;
		goto err_create_fnic_sgl_slab_max;
	}

	/* Create a cache of io_req structs for use via mempool */
	fnic_io_req_cache = kmem_cache_create("fnic_io_req",
					      sizeof(struct fnic_io_req),
					      0, SLAB_HWCACHE_ALIGN, NULL);
	if (!fnic_io_req_cache) {
		printk(KERN_ERR PFX "failed to create fnic io_req slab\n");
		err = -ENOMEM;
		goto err_create_fnic_ioreq_slab;
	}

	fnic_event_queue = create_singlethread_workqueue("fnic_event_wq");
	if (!fnic_event_queue) {
		printk(KERN_ERR PFX "fnic work queue create failed\n");
		err = -ENOMEM;
		goto err_create_fnic_workq;
	}

	fnic_fip_queue = create_singlethread_workqueue("fnic_fip_q");
	if (!fnic_fip_queue) {
		printk(KERN_ERR PFX "fnic FIP work queue create failed\n");
		err = -ENOMEM;
		goto err_create_fip_workq;
	}

	fnic_fc_transport = fc_attach_transport(&fnic_fc_functions);
	if (!fnic_fc_transport) {
		printk(KERN_ERR PFX "fc_attach_transport error\n");
		err = -ENOMEM;
		goto err_fc_transport;
	}

	/* register the driver with PCI system */
	err = pci_register_driver(&fnic_driver);
	if (err < 0) {
		printk(KERN_ERR PFX "pci register error\n");
		goto err_pci_register;
	}
	return err;

err_pci_register:
	fc_release_transport(fnic_fc_transport);
err_fc_transport:
	destroy_workqueue(fnic_fip_queue);
err_create_fip_workq:
	destroy_workqueue(fnic_event_queue);
err_create_fnic_workq:
	kmem_cache_destroy(fnic_io_req_cache);
err_create_fnic_ioreq_slab:
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
err_create_fnic_sgl_slab_max:
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
err_create_fnic_sgl_slab_dflt:
	fnic_trace_free();
	fnic_fc_trace_free();
	fnic_debugfs_terminate();
	return err;
}

static void __exit fnic_cleanup_module(void)
{
	pci_unregister_driver(&fnic_driver);
	destroy_workqueue(fnic_event_queue);
	if (fnic_fip_queue)
		destroy_workqueue(fnic_fip_queue);
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	kmem_cache_destroy(fnic_io_req_cache);
	fc_release_transport(fnic_fc_transport);
	fnic_trace_free();
	fnic_fc_trace_free();
	fnic_debugfs_terminate();
}

module_init(fnic_init_module);
module_exit(fnic_cleanup_module);
