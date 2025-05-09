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
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc_frame.h>

#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "fnic_io.h"
#include "fnic.h"
#include "fnic_fdls.h"
#include "fdls_fc.h"

#define PCI_DEVICE_ID_CISCO_FNIC	0x0045

/* Timer to poll notification area for events. Used for MSI interrupts */
#define FNIC_NOTIFY_TIMER_PERIOD	(2 * HZ)

static struct kmem_cache *fnic_sgl_cache[FNIC_SGL_NUM_CACHES];
static struct kmem_cache *fnic_io_req_cache;
static struct kmem_cache *fdls_frame_cache;
static struct kmem_cache *fdls_frame_elem_cache;
static LIST_HEAD(fnic_list);
static DEFINE_SPINLOCK(fnic_list_lock);
static DEFINE_IDA(fnic_ida);

struct work_struct reset_fnic_work;
LIST_HEAD(reset_fnic_list);
DEFINE_SPINLOCK(reset_fnic_list_lock);

/* Supported devices by fnic module */
static const struct pci_device_id fnic_id_table[] = {
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

unsigned int fnic_fdmi_support = 1;
module_param(fnic_fdmi_support, int, 0644);
MODULE_PARM_DESC(fnic_fdmi_support, "FDMI support");

static unsigned int fnic_tgt_id_binding = 1;
module_param(fnic_tgt_id_binding, uint, 0644);
MODULE_PARM_DESC(fnic_tgt_id_binding,
		 "Target ID binding (0 for none. 1 for binding by WWPN (default))");

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

unsigned int pc_rscn_handling_feature_flag = PC_RSCN_HANDLING_FEATURE_ON;
module_param(pc_rscn_handling_feature_flag, uint, 0644);
MODULE_PARM_DESC(pc_rscn_handling_feature_flag,
		 "PCRSCN handling (0 for none. 1 to handle PCRSCN (default))");

struct workqueue_struct *reset_fnic_work_queue;
struct workqueue_struct *fnic_fip_queue;

static int fnic_sdev_init(struct scsi_device *sdev)
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
	.eh_host_reset_handler = fnic_eh_host_reset_handler,
	.sdev_init = fnic_sdev_init,
	.change_queue_depth = scsi_change_queue_depth,
	.this_id = -1,
	.cmd_per_lun = 3,
	.can_queue = FNIC_DFLT_IO_REQ,
	.sg_tablesize = FNIC_MAX_SG_DESC_CNT,
	.max_sectors = 0xffff,
	.shost_groups = fnic_host_groups,
	.track_queue_depth = 1,
	.cmd_size = sizeof(struct fnic_cmd_priv),
	.map_queues = fnic_mq_map_queues_cpus,
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
	.get_host_port_state = fnic_get_host_port_state,
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
	.issue_fc_host_lip = fnic_issue_fc_host_lip,
	.get_fc_host_stats = fnic_get_stats,
	.reset_fc_host_stats = fnic_reset_host_stats,
	.dd_fcrport_size = sizeof(struct rport_dd_data_s),
	.terminate_rport_io = fnic_terminate_rport_io,
	.bsg_request = NULL,
};

static void fnic_get_host_speed(struct Scsi_Host *shost)
{
	struct fnic *fnic = *((struct fnic **) shost_priv(shost));
	u32 port_speed = vnic_dev_port_speed(fnic->vdev);
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;

	FNIC_MAIN_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				  "port_speed: %d Mbps", port_speed);
	atomic64_set(&fnic_stats->misc_stats.port_speed_in_mbps, port_speed);

	/* Add in other values as they get defined in fw */
	switch (port_speed) {
	case DCEM_PORTSPEED_1G:
		fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
		break;
	case DCEM_PORTSPEED_2G:
		fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
		break;
	case DCEM_PORTSPEED_4G:
		fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
		break;
	case DCEM_PORTSPEED_8G:
		fc_host_speed(shost) = FC_PORTSPEED_8GBIT;
		break;
	case DCEM_PORTSPEED_10G:
		fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
		break;
	case DCEM_PORTSPEED_16G:
		fc_host_speed(shost) = FC_PORTSPEED_16GBIT;
		break;
	case DCEM_PORTSPEED_20G:
		fc_host_speed(shost) = FC_PORTSPEED_20GBIT;
		break;
	case DCEM_PORTSPEED_25G:
		fc_host_speed(shost) = FC_PORTSPEED_25GBIT;
		break;
	case DCEM_PORTSPEED_32G:
		fc_host_speed(shost) = FC_PORTSPEED_32GBIT;
		break;
	case DCEM_PORTSPEED_40G:
	case DCEM_PORTSPEED_4x10G:
		fc_host_speed(shost) = FC_PORTSPEED_40GBIT;
		break;
	case DCEM_PORTSPEED_50G:
		fc_host_speed(shost) = FC_PORTSPEED_50GBIT;
		break;
	case DCEM_PORTSPEED_64G:
		fc_host_speed(shost) = FC_PORTSPEED_64GBIT;
		break;
	case DCEM_PORTSPEED_100G:
		fc_host_speed(shost) = FC_PORTSPEED_100GBIT;
		break;
	case DCEM_PORTSPEED_128G:
		fc_host_speed(shost) = FC_PORTSPEED_128GBIT;
		break;
	default:
		FNIC_MAIN_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					  "Unknown FC speed: %d Mbps", port_speed);
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		break;
	}
}

/* Placeholder function */
static struct fc_host_statistics *fnic_get_stats(struct Scsi_Host *host)
{
	int ret;
	struct fnic *fnic = *((struct fnic **) shost_priv(host));
	struct fc_host_statistics *stats = &fnic->fnic_stats.host_stats;
	struct vnic_stats *vs;
	unsigned long flags;

	if (time_before
		(jiffies, fnic->stats_time + HZ / FNIC_STATS_RATE_LIMIT))
		return stats;
	fnic->stats_time = jiffies;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	ret = vnic_dev_stats_dump(fnic->vdev, &fnic->stats);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (ret) {
		FNIC_MAIN_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
					  "fnic: Get vnic stats failed: 0x%x", ret);
		return stats;
	}
	vs = fnic->stats;
	stats->tx_frames = vs->tx.tx_unicast_frames_ok;
	stats->tx_words = vs->tx.tx_unicast_bytes_ok / 4;
	stats->rx_frames = vs->rx.rx_unicast_frames_ok;
	stats->rx_words = vs->rx.rx_unicast_bytes_ok / 4;
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
	struct fnic *fnic = *((struct fnic **) shost_priv(host));
	struct fc_host_statistics *stats;
	unsigned long flags;

	/* dump current stats, before clearing them */
	stats = fnic_get_stats(host);
	fnic_dump_fchost_stats(host, stats);

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	ret = vnic_dev_stats_clear(fnic->vdev);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (ret) {
		FNIC_MAIN_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
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
			dev_err(&fnic->pdev->dev, "WQ[%d] error_status %d\n", i, error_status);
	}

	for (i = 0; i < fnic->rq_count; i++) {
		error_status = ioread32(&fnic->rq[i].ctrl->error_status);
		if (error_status)
			dev_err(&fnic->pdev->dev, "RQ[%d] error_status %d\n", i, error_status);
	}

	for (i = 0; i < fnic->wq_copy_count; i++) {
		error_status = ioread32(&fnic->hw_copy_wq[i].ctrl->error_status);
		if (error_status)
			dev_err(&fnic->pdev->dev, "CWQ[%d] error_status %d\n", i, error_status);
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
		err = vnic_dev_notify_set(fnic->vdev, fnic->wq_copy_count + fnic->copy_wq_base);
		break;
	default:
		dev_err(&fnic->pdev->dev, "Interrupt mode should be set up"
			     " before devcmd notify set %d\n",
			     vnic_dev_get_intr_mode(fnic->vdev));
		err = -1;
		break;
	}

	return err;
}

static void fnic_notify_timer(struct timer_list *t)
{
	struct fnic *fnic = timer_container_of(fnic, t, notify_timer);

	fnic_handle_link_event(fnic);
	mod_timer(&fnic->notify_timer,
		  round_jiffies(jiffies + FNIC_NOTIFY_TIMER_PERIOD));
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
	int raw_wq_rq_counts;

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
		err = vnic_wq_copy_disable(&fnic->hw_copy_wq[i]);
		if (err)
			return err;
		raw_wq_rq_counts = fnic->raw_wq_count + fnic->rq_count;
		fnic_wq_copy_cmpl_handler(fnic, -1, i + raw_wq_rq_counts);
	}

	/* Clean up completed IOs and FCS frames */
	fnic_wq_cmpl_handler(fnic, -1);
	fnic_rq_cmpl_handler(fnic, -1);

	/* Clean up the IOs and FCS frames that have not completed */
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_clean(&fnic->wq[i], fnic_free_wq_buf);
	for (i = 0; i < fnic->rq_count; i++)
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);
	for (i = 0; i < fnic->wq_copy_count; i++)
		vnic_wq_copy_clean(&fnic->hw_copy_wq[i],
				   fnic_wq_copy_cleanup_handler);

	for (i = 0; i < fnic->cq_count; i++)
		vnic_cq_clean(&fnic->cq[i]);
	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_clean(&fnic->intr[i]);

	mempool_destroy(fnic->io_req_pool);
	mempool_destroy(fnic->frame_pool);
	mempool_destroy(fnic->frame_elem_pool);
	for (i = 0; i < FNIC_SGL_NUM_CACHES; i++)
		mempool_destroy(fnic->io_sgl_pool[i]);

	return 0;
}

static void fnic_iounmap(struct fnic *fnic)
{
	if (fnic->bar0.vaddr)
		iounmap(fnic->bar0.vaddr);
}

static void fnic_set_vlan(struct fnic *fnic, u16 vlan_id)
{
	vnic_dev_set_default_vlan(fnic->vdev, vlan_id);
}

static void fnic_scsi_init(struct fnic *fnic)
{
	struct Scsi_Host *host = fnic->host;

	snprintf(fnic->name, sizeof(fnic->name) - 1, "%s%d", DRV_NAME,
			 host->host_no);

	host->transportt = fnic_fc_transport;
}

static void fnic_free_ioreq_tables_mq(struct fnic *fnic)
{
	int hwq;

	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++)
		kfree(fnic->sw_copy_wq[hwq].io_req_table);
}

static int fnic_scsi_drv_init(struct fnic *fnic)
{
	struct Scsi_Host *host = fnic->host;
	int err;
	struct pci_dev *pdev = fnic->pdev;
	struct fnic_iport_s *iport = &fnic->iport;
	int hwq;

	/* Configure maximum outstanding IO reqs*/
	if (fnic->config.io_throttle_count != FNIC_UCSM_DFLT_THROTTLE_CNT_BLD)
		host->can_queue = min_t(u32, FNIC_MAX_IO_REQ,
					max_t(u32, FNIC_MIN_IO_REQ,
					fnic->config.io_throttle_count));

	fnic->fnic_max_tag_id = host->can_queue;
	host->max_lun = fnic->config.luns_per_tgt;
	host->max_id = FNIC_MAX_FCP_TARGET;
	host->max_cmd_len = FNIC_FCOE_MAX_CMD_LEN;

	host->nr_hw_queues = fnic->wq_copy_count;

	dev_info(&fnic->pdev->dev, "fnic: can_queue: %d max_lun: %llu",
			host->can_queue, host->max_lun);

	dev_info(&fnic->pdev->dev, "fnic: max_id: %d max_cmd_len: %d nr_hw_queues: %d",
			host->max_id, host->max_cmd_len, host->nr_hw_queues);

	for (hwq = 0; hwq < fnic->wq_copy_count; hwq++) {
		fnic->sw_copy_wq[hwq].ioreq_table_size = fnic->fnic_max_tag_id;
		fnic->sw_copy_wq[hwq].io_req_table =
			kzalloc((fnic->sw_copy_wq[hwq].ioreq_table_size + 1) *
					sizeof(struct fnic_io_req *), GFP_KERNEL);

		if (!fnic->sw_copy_wq[hwq].io_req_table) {
			fnic_free_ioreq_tables_mq(fnic);
			return -ENOMEM;
		}
	}

	dev_info(&fnic->pdev->dev, "fnic copy wqs: %d, Q0 ioreq table size: %d\n",
			fnic->wq_copy_count, fnic->sw_copy_wq[0].ioreq_table_size);

	fnic_scsi_init(fnic);

	err = scsi_add_host(fnic->host, &pdev->dev);
	if (err) {
		dev_err(&fnic->pdev->dev, "fnic: scsi add host failed: aborting\n");
		return err;
	}
	fc_host_maxframe_size(fnic->host) = iport->max_payload_size;
	fc_host_dev_loss_tmo(fnic->host) =
		fnic->config.port_down_timeout / 1000;
	sprintf(fc_host_symbolic_name(fnic->host),
			DRV_NAME " v" DRV_VERSION " over %s", fnic->name);
	fc_host_port_type(fnic->host) = FC_PORTTYPE_NPORT;
	fc_host_node_name(fnic->host) = iport->wwnn;
	fc_host_port_name(fnic->host) = iport->wwpn;
	fc_host_supported_classes(fnic->host) = FC_COS_CLASS3;
	memset(fc_host_supported_fc4s(fnic->host), 0,
		   sizeof(fc_host_supported_fc4s(fnic->host)));
	fc_host_supported_fc4s(fnic->host)[2] = 1;
	fc_host_supported_fc4s(fnic->host)[7] = 1;
	fc_host_supported_speeds(fnic->host) = 0;
	fc_host_supported_speeds(fnic->host) |= FC_PORTSPEED_8GBIT;

	dev_info(&fnic->pdev->dev, "shost_data: 0x%p\n", fnic->host->shost_data);
	if (fnic->host->shost_data != NULL) {
		if (fnic_tgt_id_binding == 0) {
			dev_info(&fnic->pdev->dev, "Setting target binding to NONE\n");
			fc_host_tgtid_bind_type(fnic->host) = FC_TGTID_BIND_NONE;
		} else {
			dev_info(&fnic->pdev->dev, "Setting target binding to WWPN\n");
			fc_host_tgtid_bind_type(fnic->host) = FC_TGTID_BIND_BY_WWPN;
		}
	}

	fnic->io_req_pool = mempool_create_slab_pool(2, fnic_io_req_cache);
	if (!fnic->io_req_pool) {
		scsi_remove_host(fnic->host);
		return -ENOMEM;
	}

	return 0;
}

void fnic_mq_map_queues_cpus(struct Scsi_Host *host)
{
	struct fnic *fnic = *((struct fnic **) shost_priv(host));
	struct pci_dev *l_pdev = fnic->pdev;
	int intr_mode = fnic->config.intr_mode;
	struct blk_mq_queue_map *qmap = &host->tag_set.map[HCTX_TYPE_DEFAULT];

	if (intr_mode == VNIC_DEV_INTR_MODE_MSI || intr_mode == VNIC_DEV_INTR_MODE_INTX) {
		FNIC_MAIN_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
			"intr_mode is not msix\n");
		return;
	}

	FNIC_MAIN_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			"qmap->nr_queues: %d\n", qmap->nr_queues);

	if (l_pdev == NULL) {
		FNIC_MAIN_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
						"l_pdev is null\n");
		return;
	}

	blk_mq_map_hw_queues(qmap, &l_pdev->dev, FNIC_PCI_OFFSET);
}

static int fnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct Scsi_Host *host = NULL;
	struct fnic *fnic;
	mempool_t *pool;
	struct fnic_iport_s *iport;
	int err = 0;
	int fnic_id = 0;
	int i;
	unsigned long flags;
	char *desc, *subsys_desc;
	int len;

	/*
	 * Allocate fnic
	 */
	fnic = kzalloc(sizeof(struct fnic), GFP_KERNEL);
	if (!fnic) {
		err = -ENOMEM;
		goto err_out_fnic_alloc;
	}

	iport = &fnic->iport;

	fnic_id = ida_alloc(&fnic_ida, GFP_KERNEL);
	if (fnic_id < 0) {
		dev_err(&pdev->dev, "Unable to alloc fnic ID\n");
		err = fnic_id;
		goto err_out_ida_alloc;
	}

	fnic->pdev = pdev;
	fnic->fnic_num = fnic_id;

	/* Find model name from PCIe subsys ID */
	if (fnic_get_desc_by_devid(pdev, &desc, &subsys_desc) == 0) {
		dev_info(&fnic->pdev->dev, "Model: %s\n", subsys_desc);

		/* Update FDMI model */
		fnic->subsys_desc_len = strlen(subsys_desc);
		len = ARRAY_SIZE(fnic->subsys_desc);
		if (fnic->subsys_desc_len > len)
			fnic->subsys_desc_len = len;
		memcpy(fnic->subsys_desc, subsys_desc, fnic->subsys_desc_len);
		dev_info(&fnic->pdev->dev, "FDMI Model: %s\n", fnic->subsys_desc);
	} else {
		fnic->subsys_desc_len = 0;
		dev_info(&fnic->pdev->dev, "Model: %s subsys_id: 0x%04x\n", "Unknown",
				pdev->subsystem_device);
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&fnic->pdev->dev, "Cannot enable PCI device, aborting.\n");
		goto err_out_pci_enable_device;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&fnic->pdev->dev, "Cannot enable PCI resources, aborting\n");
		goto err_out_pci_request_regions;
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
			dev_err(&fnic->pdev->dev, "No usable DMA configuration "
				     "aborting\n");
			goto err_out_set_dma_mask;
		}
	}

	/* Map vNIC resources from BAR0 */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&fnic->pdev->dev, "BAR0 not memory-map'able, aborting.\n");
		err = -ENODEV;
		goto err_out_map_bar;
	}

	fnic->bar0.vaddr = pci_iomap(pdev, 0, 0);
	fnic->bar0.bus_addr = pci_resource_start(pdev, 0);
	fnic->bar0.len = pci_resource_len(pdev, 0);

	if (!fnic->bar0.vaddr) {
		dev_err(&fnic->pdev->dev, "Cannot memory-map BAR0 res hdr, "
			     "aborting.\n");
		err = -ENODEV;
		goto err_out_fnic_map_bar;
	}

	fnic->vdev = vnic_dev_register(NULL, fnic, pdev, &fnic->bar0);
	if (!fnic->vdev) {
		dev_err(&fnic->pdev->dev, "vNIC registration failed, "
			     "aborting.\n");
		err = -ENODEV;
		goto err_out_dev_register;
	}

	err = vnic_dev_cmd_init(fnic->vdev);
	if (err) {
		dev_err(&fnic->pdev->dev, "vnic_dev_cmd_init() returns %d, aborting\n",
				err);
		goto err_out_dev_cmd_init;
	}

	err = fnic_dev_wait(fnic->vdev, vnic_dev_open,
			    vnic_dev_open_done, CMD_OPENF_RQ_ENABLE_THEN_POST);
	if (err) {
		dev_err(&fnic->pdev->dev, "vNIC dev open failed, aborting.\n");
		goto err_out_dev_open;
	}

	err = vnic_dev_init(fnic->vdev, 0);
	if (err) {
		dev_err(&fnic->pdev->dev, "vNIC dev init failed, aborting.\n");
		goto err_out_dev_init;
	}

	err = vnic_dev_mac_addr(fnic->vdev, iport->hwmac);
	if (err) {
		dev_err(&fnic->pdev->dev, "vNIC get MAC addr failed\n");
		goto err_out_dev_mac_addr;
	}
	/* set data_src for point-to-point mode and to keep it non-zero */
	memcpy(fnic->data_src_addr, iport->hwmac, ETH_ALEN);

	/* Get vNIC configuration */
	err = fnic_get_vnic_config(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Get vNIC configuration failed, "
			     "aborting.\n");
		goto err_out_fnic_get_config;
	}

	switch (fnic->config.flags & 0xff0) {
	case VFCF_FC_INITIATOR:
		{
			host =
				scsi_host_alloc(&fnic_host_template,
								sizeof(struct fnic *));
			if (!host) {
				dev_err(&fnic->pdev->dev, "Unable to allocate scsi host\n");
				err = -ENOMEM;
				goto err_out_scsi_host_alloc;
			}
			*((struct fnic **) shost_priv(host)) = fnic;

			fnic->host = host;
			fnic->role = FNIC_ROLE_FCP_INITIATOR;
			dev_info(&fnic->pdev->dev, "fnic: %d is scsi initiator\n",
					fnic->fnic_num);
		}
		break;
	default:
		dev_info(&fnic->pdev->dev, "fnic: %d has no role defined\n", fnic->fnic_num);
		err = -EINVAL;
		goto err_out_fnic_role;
	}

	/* Setup PCI resources */
	pci_set_drvdata(pdev, fnic);

	fnic_get_res_counts(fnic);

	err = fnic_set_intr_mode(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Failed to set intr mode, "
			     "aborting.\n");
		goto err_out_fnic_set_intr_mode;
	}

	err = fnic_alloc_vnic_resources(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Failed to alloc vNIC resources, "
			     "aborting.\n");
		goto err_out_fnic_alloc_vnic_res;
	}
	dev_info(&fnic->pdev->dev, "fnic copy wqs: %d, Q0 ioreq table size: %d\n",
			fnic->wq_copy_count, fnic->sw_copy_wq[0].ioreq_table_size);

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

	pool = mempool_create_slab_pool(2, fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	if (!pool) {
		err = -ENOMEM;
		goto err_out_free_resources;
	}
	fnic->io_sgl_pool[FNIC_SGL_CACHE_DFLT] = pool;

	pool = mempool_create_slab_pool(2, fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	if (!pool) {
		err = -ENOMEM;
		goto err_out_free_dflt_pool;
	}
	fnic->io_sgl_pool[FNIC_SGL_CACHE_MAX] = pool;

	pool = mempool_create_slab_pool(FDLS_MIN_FRAMES, fdls_frame_cache);
	if (!pool) {
		err = -ENOMEM;
		goto err_out_fdls_frame_pool;
	}
	fnic->frame_pool = pool;

	pool = mempool_create_slab_pool(FDLS_MIN_FRAME_ELEM,
						fdls_frame_elem_cache);
	if (!pool) {
		err = -ENOMEM;
		goto err_out_fdls_frame_elem_pool;
	}
	fnic->frame_elem_pool = pool;

	/* setup vlan config, hw inserts vlan header */
	fnic->vlan_hw_insert = 1;
	fnic->vlan_id = 0;

	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		dev_info(&fnic->pdev->dev, "firmware supports FIP\n");
		/* enable directed and multicast */
		vnic_dev_packet_filter(fnic->vdev, 1, 1, 0, 0, 0);
		vnic_dev_add_addr(fnic->vdev, FIP_ALL_ENODE_MACS);
		vnic_dev_add_addr(fnic->vdev, iport->hwmac);
		spin_lock_init(&fnic->vlans_lock);
		INIT_WORK(&fnic->fip_frame_work, fnic_handle_fip_frame);
		INIT_LIST_HEAD(&fnic->fip_frame_queue);
		INIT_LIST_HEAD(&fnic->vlan_list);
		timer_setup(&fnic->retry_fip_timer, fnic_handle_fip_timer, 0);
		timer_setup(&fnic->fcs_ka_timer, fnic_handle_fcs_ka_timer, 0);
		timer_setup(&fnic->enode_ka_timer, fnic_handle_enode_ka_timer, 0);
		timer_setup(&fnic->vn_ka_timer, fnic_handle_vn_ka_timer, 0);
		fnic->set_vlan = fnic_set_vlan;
	} else {
		dev_info(&fnic->pdev->dev, "firmware uses non-FIP mode\n");
	}
	fnic->state = FNIC_IN_FC_MODE;

	atomic_set(&fnic->in_flight, 0);
	fnic->state_flags = FNIC_FLAGS_NONE;

	/* Enable hardware stripping of vlan header on ingress */
	fnic_set_nic_config(fnic, 0, 0, 0, 0, 0, 0, 1);

	/* Setup notification buffer area */
	err = fnic_notify_set(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Failed to alloc notify buffer, aborting.\n");
		goto err_out_fnic_notify_set;
	}

	/* Setup notify timer when using MSI interrupts */
	if (vnic_dev_get_intr_mode(fnic->vdev) == VNIC_DEV_INTR_MODE_MSI)
		timer_setup(&fnic->notify_timer, fnic_notify_timer, 0);

	/* allocate RQ buffers and post them to RQ*/
	for (i = 0; i < fnic->rq_count; i++) {
		err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
		if (err) {
			dev_err(&fnic->pdev->dev, "fnic_alloc_rq_frame can't alloc "
				     "frame\n");
			goto err_out_alloc_rq_buf;
		}
	}

	init_completion(&fnic->reset_completion_wait);

	/* Start local port initialization */
	iport->max_flogi_retries = fnic->config.flogi_retries;
	iport->max_plogi_retries = fnic->config.plogi_retries;
	iport->plogi_timeout = fnic->config.plogi_timeout;
	iport->service_params =
		(FNIC_FCP_SP_INITIATOR | FNIC_FCP_SP_RD_XRDY_DIS |
		 FNIC_FCP_SP_CONF_CMPL);
	if (fnic->config.flags & VFCF_FCP_SEQ_LVL_ERR)
		iport->service_params |= FNIC_FCP_SP_RETRY;

	iport->boot_time = jiffies;
	iport->e_d_tov = fnic->config.ed_tov;
	iport->r_a_tov = fnic->config.ra_tov;
	iport->link_supported_speeds = FNIC_PORTSPEED_10GBIT;
	iport->wwpn = fnic->config.port_wwn;
	iport->wwnn = fnic->config.node_wwn;

	iport->max_payload_size = fnic->config.maxdatafieldsize;

	if ((iport->max_payload_size < FNIC_MIN_DATA_FIELD_SIZE) ||
		(iport->max_payload_size > FNIC_FC_MAX_PAYLOAD_LEN) ||
		((iport->max_payload_size % 4) != 0)) {
		iport->max_payload_size = FNIC_FC_MAX_PAYLOAD_LEN;
	}

	iport->flags |= FNIC_FIRST_LINK_UP;

	timer_setup(&(iport->fabric.retry_timer), fdls_fabric_timer_callback,
				0);

	fnic->stats_reset_time = jiffies;

	INIT_WORK(&fnic->link_work, fnic_handle_link);
	INIT_WORK(&fnic->frame_work, fnic_handle_frame);
	INIT_WORK(&fnic->tport_work, fnic_tport_event_handler);
	INIT_WORK(&fnic->flush_work, fnic_flush_tx);

	INIT_LIST_HEAD(&fnic->frame_queue);
	INIT_LIST_HEAD(&fnic->tx_queue);
	INIT_LIST_HEAD(&fnic->tport_event_list);

	INIT_DELAYED_WORK(&iport->oxid_pool.schedule_oxid_free_retry,
	fdls_schedule_oxid_free_retry_work);

	/* Initialize the oxid reclaim list and work struct */
	INIT_LIST_HEAD(&iport->oxid_pool.oxid_reclaim_list);
	INIT_DELAYED_WORK(&iport->oxid_pool.oxid_reclaim_work, fdls_reclaim_oxid_handler);

	/* Enable all queues */
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_enable(&fnic->wq[i]);
	for (i = 0; i < fnic->rq_count; i++) {
		if (!ioread32(&fnic->rq[i].ctrl->enable))
			vnic_rq_enable(&fnic->rq[i]);
	}
	for (i = 0; i < fnic->wq_copy_count; i++)
		vnic_wq_copy_enable(&fnic->hw_copy_wq[i]);

	vnic_dev_enable(fnic->vdev);

	err = fnic_request_intr(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Unable to request irq.\n");
		goto err_out_fnic_request_intr;
	}

	fnic_notify_timer_start(fnic);

	fnic_fdls_init(fnic, (fnic->config.flags & VFCF_FIP_CAPABLE));

	err = fnic_scsi_drv_init(fnic);
	if (err)
		goto err_out_scsi_drv_init;

	err = fnic_stats_debugfs_init(fnic);
	if (err) {
		dev_err(&fnic->pdev->dev, "Failed to initialize debugfs for stats\n");
		goto err_out_free_stats_debugfs;
	}

	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_unmask(&fnic->intr[i]);

	spin_lock_irqsave(&fnic_list_lock, flags);
	list_add_tail(&fnic->list, &fnic_list);
	spin_unlock_irqrestore(&fnic_list_lock, flags);

	return 0;

err_out_free_stats_debugfs:
	fnic_stats_debugfs_remove(fnic);
	fnic_free_ioreq_tables_mq(fnic);
	scsi_remove_host(fnic->host);
err_out_scsi_drv_init:
	fnic_free_intr(fnic);
err_out_fnic_request_intr:
err_out_alloc_rq_buf:
	for (i = 0; i < fnic->rq_count; i++) {
		if (ioread32(&fnic->rq[i].ctrl->enable))
			vnic_rq_disable(&fnic->rq[i]);
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);
	}
	vnic_dev_notify_unset(fnic->vdev);
err_out_fnic_notify_set:
	mempool_destroy(fnic->frame_elem_pool);
err_out_fdls_frame_elem_pool:
	mempool_destroy(fnic->frame_pool);
err_out_fdls_frame_pool:
	mempool_destroy(fnic->io_sgl_pool[FNIC_SGL_CACHE_MAX]);
err_out_free_dflt_pool:
	mempool_destroy(fnic->io_sgl_pool[FNIC_SGL_CACHE_DFLT]);
err_out_free_resources:
	fnic_free_vnic_resources(fnic);
err_out_fnic_alloc_vnic_res:
	fnic_clear_intr_mode(fnic);
err_out_fnic_set_intr_mode:
	scsi_host_put(fnic->host);
err_out_fnic_role:
err_out_scsi_host_alloc:
err_out_fnic_get_config:
err_out_dev_mac_addr:
err_out_dev_init:
	vnic_dev_close(fnic->vdev);
err_out_dev_open:
err_out_dev_cmd_init:
	vnic_dev_unregister(fnic->vdev);
err_out_dev_register:
	fnic_iounmap(fnic);
err_out_fnic_map_bar:
err_out_map_bar:
err_out_set_dma_mask:
	pci_release_regions(pdev);
err_out_pci_request_regions:
	pci_disable_device(pdev);
err_out_pci_enable_device:
	ida_free(&fnic_ida, fnic->fnic_num);
err_out_ida_alloc:
	kfree(fnic);
err_out_fnic_alloc:
	return err;
}

static void fnic_remove(struct pci_dev *pdev)
{
	struct fnic *fnic = pci_get_drvdata(pdev);
	unsigned long flags;

	/*
	 * Sometimes when probe() fails and do not exit with an error code,
	 * remove() gets called with 'drvdata' not set. Avoid a crash by
	 * adding a defensive check.
	 */
	if (!fnic)
		return;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->stop_rx_link_events = 1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/*
	 * Flush the fnic event queue. After this call, there should
	 * be no event queued for this fnic device in the workqueue
	 */
	flush_workqueue(fnic_event_queue);

	fnic_scsi_unload(fnic);

	if (vnic_dev_get_intr_mode(fnic->vdev) == VNIC_DEV_INTR_MODE_MSI)
		timer_delete_sync(&fnic->notify_timer);

	if (fnic->config.flags & VFCF_FIP_CAPABLE) {
		timer_delete_sync(&fnic->retry_fip_timer);
		timer_delete_sync(&fnic->fcs_ka_timer);
		timer_delete_sync(&fnic->enode_ka_timer);
		timer_delete_sync(&fnic->vn_ka_timer);

		fnic_free_txq(&fnic->fip_frame_queue);
		fnic_fcoe_reset_vlans(fnic);
	}

	if ((fnic_fdmi_support == 1) && (fnic->iport.fabric.fdmi_pending > 0))
		timer_delete_sync(&fnic->iport.fabric.fdmi_timer);

	fnic_stats_debugfs_remove(fnic);

	/*
	 * This stops the fnic device, masks all interrupts. Completed
	 * CQ entries are drained. Posted WQ/RQ/Copy-WQ entries are
	 * cleaned up
	 */
	fnic_cleanup(fnic);

	spin_lock_irqsave(&fnic_list_lock, flags);
	list_del(&fnic->list);
	spin_unlock_irqrestore(&fnic_list_lock, flags);

	fnic_free_txq(&fnic->frame_queue);
	fnic_free_txq(&fnic->tx_queue);

	vnic_dev_notify_unset(fnic->vdev);
	fnic_free_intr(fnic);
	fnic_free_vnic_resources(fnic);
	fnic_clear_intr_mode(fnic);
	vnic_dev_close(fnic->vdev);
	vnic_dev_unregister(fnic->vdev);
	fnic_iounmap(fnic);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	ida_free(&fnic_ida, fnic->fnic_num);
	fnic_scsi_unload_cleanup(fnic);
	scsi_host_put(fnic->host);
	kfree(fnic);
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

	fdls_frame_cache = kmem_cache_create("fdls_frames",
					FNIC_FCOE_FRAME_MAXSZ,
					0, SLAB_HWCACHE_ALIGN, NULL);
	if (!fdls_frame_cache) {
		pr_err("fnic fdls frame cache create failed\n");
		err = -ENOMEM;
		goto err_create_fdls_frame_cache;
	}

	fdls_frame_elem_cache = kmem_cache_create("fdls_frame_elem",
					sizeof(struct fnic_frame_list),
					0, SLAB_HWCACHE_ALIGN, NULL);
	if (!fdls_frame_elem_cache) {
		pr_err("fnic fdls frame elem cache create failed\n");
		err = -ENOMEM;
		goto err_create_fdls_frame_cache_elem;
	}

	fnic_event_queue =
		alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, "fnic_event_wq");
	if (!fnic_event_queue) {
		printk(KERN_ERR PFX "fnic work queue create failed\n");
		err = -ENOMEM;
		goto err_create_fnic_workq;
	}

	fnic_fip_queue =
		alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, "fnic_fip_q");
	if (!fnic_fip_queue) {
		printk(KERN_ERR PFX "fnic FIP work queue create failed\n");
		err = -ENOMEM;
		goto err_create_fip_workq;
	}

	if (pc_rscn_handling_feature_flag == PC_RSCN_HANDLING_FEATURE_ON) {
		reset_fnic_work_queue =
			create_singlethread_workqueue("reset_fnic_work_queue");
		if (!reset_fnic_work_queue) {
			pr_err("reset fnic work queue create failed\n");
			err = -ENOMEM;
			goto err_create_reset_fnic_workq;
		}
		spin_lock_init(&reset_fnic_list_lock);
		INIT_LIST_HEAD(&reset_fnic_list);
		INIT_WORK(&reset_fnic_work, fnic_reset_work_handler);
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
	if (pc_rscn_handling_feature_flag == PC_RSCN_HANDLING_FEATURE_ON)
		destroy_workqueue(reset_fnic_work_queue);
err_create_reset_fnic_workq:
	destroy_workqueue(fnic_event_queue);
err_create_fnic_workq:
	kmem_cache_destroy(fdls_frame_elem_cache);
err_create_fdls_frame_cache_elem:
	kmem_cache_destroy(fdls_frame_cache);
err_create_fdls_frame_cache:
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

	if (pc_rscn_handling_feature_flag == PC_RSCN_HANDLING_FEATURE_ON)
		destroy_workqueue(reset_fnic_work_queue);

	if (fnic_fip_queue)
		destroy_workqueue(fnic_fip_queue);

	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	kmem_cache_destroy(fnic_io_req_cache);
	kmem_cache_destroy(fdls_frame_cache);
	fc_release_transport(fnic_fc_transport);
	fnic_trace_free();
	fnic_fc_trace_free();
	fnic_debugfs_terminate();
	ida_destroy(&fnic_ida);
}

module_init(fnic_init_module);
module_exit(fnic_cleanup_module);
