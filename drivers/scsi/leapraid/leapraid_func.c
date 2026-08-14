// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#include <linux/module.h>

#include "leapraid_func.h"

static int poll_queues;
module_param(poll_queues, int, 0444);
MODULE_PARM_DESC(poll_queues,
		 "specifies the number of queues for io_uring poll mode.");

static int max_msix_vectors = LEAPRAID_INVALID_MSIX_VECTORS;
module_param(max_msix_vectors, int, 0444);
MODULE_PARM_DESC(max_msix_vectors, "max msix vectors");

static void leapraid_remove_device(struct leapraid_adapter *adapter,
				   struct leapraid_sas_dev *sas_dev);
static void leapraid_set_led(struct leapraid_adapter *adapter,
			     struct leapraid_sas_dev *sas_dev, bool on);
static void leapraid_ublk_io_dev(struct leapraid_adapter *adapter,
				 u64 sas_address,
				 struct leapraid_card_port *port);
static void leapraid_clear_cached_boot_dev(struct leapraid_adapter *adapter,
					   void *dev, u32 chnl);
static int leapraid_make_adapter_available(struct leapraid_adapter *adapter);
static void leapraid_sync_irqs_for_cleanup(struct leapraid_adapter *adapter);
static int leapraid_fw_log_init(struct leapraid_adapter *adapter);
static bool leapraid_should_skip_poll_work(struct leapraid_adapter *adapter);
static int leapraid_make_adapter_ready(struct leapraid_adapter *adapter,
				       enum reset_type type);

static noinline bool leapraid_shost_in_recovery(struct Scsi_Host *shost)
{
	enum scsi_host_state state;

	state = scsi_get_host_state(shost);
	return state == SHOST_RECOVERY ||
	       state == SHOST_CANCEL_RECOVERY ||
	       state == SHOST_DEL_RECOVERY ||
	       shost->tmf_in_progress;
}

static inline bool leapraid_is_end_dev(u32 dev_type)
{
	return (dev_type & LEAPRAID_DEVTYP_END_DEV) &&
		((dev_type & LEAPRAID_DEVTYP_SSP_TGT) ||
		 (dev_type & LEAPRAID_DEVTYP_STP_TGT) ||
		 (dev_type & LEAPRAID_DEVTYP_SATA_DEV));
}

bool leapraid_pci_removed(struct leapraid_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	u32 vendor_id;

	if (pci_bus_read_config_dword(pdev->bus,
				      pdev->devfn,
				      PCI_VENDOR_ID,
				      &vendor_id))
		return true;

	return (vendor_id & LEAPRAID_PCI_VENDOR_ID_MASK) !=
		LEAPRAID_VENDOR_ID;
}

static bool leapraid_pci_active(struct leapraid_adapter *adapter)
{
	return !(adapter->access_ctrl.pcie_recovering ||
		leapraid_pci_removed(adapter));
}

void *leapraid_get_reply_vaddr(struct leapraid_adapter *adapter, u32 rep_paddr)
{
	if (!rep_paddr)
		return NULL;

	return adapter->mem_desc.rep_msg +
		(rep_paddr - (u32)adapter->mem_desc.rep_msg_dma);
}

void *leapraid_get_task_desc(struct leapraid_adapter *adapter, u16 taskid)
{
	return adapter->mem_desc.task_desc +
			taskid * LEAPRAID_REQUEST_SIZE;
}

void *leapraid_get_sense_buffer(struct leapraid_adapter *adapter, u16 taskid)
{
	return adapter->mem_desc.sense_data +
			(taskid - 1) * SCSI_SENSE_BUFFERSIZE;
}

__le32 leapraid_get_sense_buffer_dma(struct leapraid_adapter *adapter,
				     u16 taskid)
{
	return cpu_to_le32(adapter->mem_desc.sense_data_dma +
			   ((taskid - 1) * SCSI_SENSE_BUFFERSIZE));
}

void leapraid_mask_int(struct leapraid_adapter *adapter)
{
	u32 reg;

	adapter->mask_int = 1;
	reg = leapraid_readl(&adapter->iomem_base->host_int_mask);
	reg |= LEAPRAID_TO_SYS_DB_MASK + LEAPRAID_REPLY_INT_MASK +
		LEAPRAID_RESET_IRQ_MASK;
	writel(reg, &adapter->iomem_base->host_int_mask);
	leapraid_readl(&adapter->iomem_base->host_int_mask);
}

void leapraid_unmask_int(struct leapraid_adapter *adapter)
{
	u32 reg;

	reg = leapraid_readl(&adapter->iomem_base->host_int_mask);
	reg &= ~LEAPRAID_REPLY_INT_MASK;
	writel(reg, &adapter->iomem_base->host_int_mask);
	adapter->mask_int = 0;
}

static void leapraid_overheat_suspend(struct leapraid_adapter *adapter)
{
	struct workqueue_struct *wq;
	struct Scsi_Host *shost;
	struct pci_dev *pdev;

	if (!adapter)
		return;

	pdev = adapter->pdev;
	shost = pci_get_drvdata(pdev);
	if (!shost) {
		dev_warn(&pdev->dev,
			 "Overheat suspend failed, invalid host or adapter\n");
		return;
	}

	wq = adapter->overheat_desc.fault_overheat_wq;
	if (!wq)
		return;

	if (atomic_cmpxchg(&adapter->overheat_desc.thermal_alert, 0, 1))
		return;

	queue_work(wq, &adapter->overheat_desc.fault_overheat_work);
}

static void leapraid_stop_adapter_on_fault(struct leapraid_adapter *adapter,
					   u32 db)
{
	u32 adapter_state = db & LEAPRAID_DB_MASK;
	bool fault_1 = false;
	bool fault_2 = false;

	fault_1 = adapter_state == LEAPRAID_DB_MASK;
	fault_2 = adapter_state == LEAPRAID_DB_FAULT &&
		  (db & LEAPRAID_DB_DATA_MASK) == LEAPRAID_DB_OVER_TEMPERATURE;

	if (!fault_1 && !fault_2)
		return;

	if (fault_1)
		dev_err(&adapter->pdev->dev,
			"%s: Doorbell status 0xFFFF!\n", __func__);
	else
		dev_err(&adapter->pdev->dev,
			"%s: Adapter overheating detected!\n", __func__);

	leapraid_overheat_suspend(adapter);
}

u32 leapraid_get_adapter_state(struct leapraid_adapter *adapter)
{
	u32 db;
	u32 adapter_state;

	db = leapraid_readl(&adapter->iomem_base->db);
	adapter_state = db & LEAPRAID_DB_MASK;
	leapraid_stop_adapter_on_fault(adapter, db);
	return adapter_state;
}

static bool leapraid_wait_adapter_ready(struct leapraid_adapter *adapter)
{
	u32 cur_state;
	u32 cnt;

	for (cnt = LEAPRAID_ADAPTER_READY_MAX_RETRY; cnt > 0; cnt--) {
		cur_state = leapraid_get_adapter_state(adapter);

		if (cur_state == LEAPRAID_DB_READY)
			return true;
		if (cur_state == LEAPRAID_DB_FAULT)
			break;
		usleep_range(LEAPRAID_ADAPTER_READY_SLEEP_MIN_US,
			     LEAPRAID_ADAPTER_READY_SLEEP_MAX_US);
	}

	return false;
}

static int leapraid_db_wait_int_host(struct leapraid_adapter *adapter)
{
	u32 cnt;

	for (cnt = LEAPRAID_DB_WAIT_MAX_RETRY; cnt > 0; cnt--) {
		if (leapraid_readl(&adapter->iomem_base->host_int_status) &
		    LEAPRAID_ADAPTER2HOST_DB_STATUS)
			return 0;

		udelay(LEAPRAID_DB_WAIT_DELAY_US);
	}

	return -EFAULT;
}

static int leapraid_db_wait_ack_and_clear_int(struct leapraid_adapter *adapter)
{
	u32 adapter_state;
	u32 int_status;
	u32 cnt;

	for (cnt = LEAPRAID_ADAPTER_READY_MAX_RETRY; cnt > 0; cnt--) {
		int_status =
			leapraid_readl(&adapter->iomem_base->host_int_status);

		if (int_status == 0xFFFFFFFF)
			return -EFAULT;

		if (!(int_status & LEAPRAID_HOST2ADAPTER_DB_STATUS))
			return 0;

		if (int_status & LEAPRAID_ADAPTER2HOST_DB_STATUS) {
			adapter_state = leapraid_get_adapter_state(adapter);
			if (adapter_state == LEAPRAID_DB_FAULT)
				return -EFAULT;
		}

		usleep_range(LEAPRAID_ADAPTER_READY_SLEEP_MIN_US,
			     LEAPRAID_ADAPTER_READY_SLEEP_MAX_US);
	}

	return -EFAULT;
}

static int leapraid_handshake_func(struct leapraid_adapter *adapter,
				   int req_bytes, u32 *req,
				   int rep_bytes, u16 *rep)
{
	int failed, i;

	if (leapraid_readl(&adapter->iomem_base->db) & LEAPRAID_DB_USED) {
		dev_err(&adapter->pdev->dev, "doorbell used\n");
		return -EFAULT;
	}

	if (leapraid_readl(&adapter->iomem_base->host_int_status) &
	    LEAPRAID_ADAPTER2HOST_DB_STATUS)
		writel(0, &adapter->iomem_base->host_int_status);

	writel(((LEAPRAID_FUNC_HANDSHAKE << LEAPRAID_DB_FUNC_SHIFT) |
		((req_bytes / LEAPRAID_DWORDS_BYTE_SIZE) <<
		LEAPRAID_DB_ADD_DWORDS_SHIFT)),
	       &adapter->iomem_base->db);

	if (leapraid_db_wait_int_host(adapter)) {
		dev_err(&adapter->pdev->dev, "%d: Wait db interrupt timeout\n",
			__LINE__);
		return -EFAULT;
	}

	writel(0, &adapter->iomem_base->host_int_status);

	if (leapraid_db_wait_ack_and_clear_int(adapter)) {
		dev_err(&adapter->pdev->dev, "%d: Wait ack failure\n",
			__LINE__);
		return -EFAULT;
	}

	for (i = 0, failed = 0;
	     i < req_bytes / LEAPRAID_DWORDS_BYTE_SIZE && !failed;
	     i++) {
		writel((u32)req[i], &adapter->iomem_base->db);
		if (leapraid_db_wait_ack_and_clear_int(adapter))
			failed = 1;
	}
	if (failed) {
		dev_err(&adapter->pdev->dev, "%d: Wait ack failure\n",
			__LINE__);
		return -EFAULT;
	}

	for (i = 0; i < rep_bytes / LEAPRAID_WORD_BYTE_SIZE; i++) {
		if (leapraid_db_wait_int_host(adapter)) {
			dev_err(&adapter->pdev->dev,
				"%d: Wait db interrupt timeout\n", __LINE__);
			return -EFAULT;
		}
		rep[i] = (u16)(leapraid_readl(&adapter->iomem_base->db)
			       & LEAPRAID_DB_DATA_MASK);
		writel(0, &adapter->iomem_base->host_int_status);
	}

	if (leapraid_db_wait_int_host(adapter)) {
		dev_err(&adapter->pdev->dev, "%d: Wait db interrupt timeout\n",
			__LINE__);
		return -EFAULT;
	}

	writel(0, &adapter->iomem_base->host_int_status);

	return 0;
}

int leapraid_check_adapter_is_op(struct leapraid_adapter *adapter, int wait,
				 const char *caller)
{
	int wait_count;

	for (wait_count = wait; wait_count > 0; wait_count--) {
		if (READ_ONCE(adapter->access_ctrl.host_removing)) {
			dev_warn(&adapter->pdev->dev,
				 "%s: Host is removing\n", caller);
			return -EFAULT;
		}

		if (leapraid_pci_removed(adapter)) {
			dev_warn(&adapter->pdev->dev,
				 "%s: PCI device removed\n", __func__);
			return -EFAULT;
		}

		if (leapraid_get_adapter_state(adapter) ==
		    LEAPRAID_DB_OPERATIONAL)
			return 0;

		dev_dbg(&adapter->pdev->dev,
			"%s: Wait for adapter to become op status(cnt=%d)\n",
			caller, wait - wait_count);

		ssleep(1);
	}

	dev_err(&adapter->pdev->dev,
		"%s: Adapter failed to become op state, last state=%d\n",
		caller, leapraid_get_adapter_state(adapter));

	return -EFAULT;
}

struct leapraid_io_req_tracker *leapraid_get_io_tracker_from_taskid(
		struct leapraid_adapter *adapter, u16 taskid)
{
	struct scsi_cmnd *scmd;

	if (WARN_ON(!taskid))
		return NULL;

	if (WARN_ON(taskid > adapter->shost->can_queue))
		return NULL;

	scmd = leapraid_get_scmd_from_taskid(adapter, taskid);
	if (scmd)
		return scsi_cmd_priv(scmd);

	return NULL;
}

static u8 leapraid_get_cb_idx(struct leapraid_adapter *adapter, u16 taskid)
{
	struct leapraid_driver_cmd *sp_cmd;
	u8 cb_idx = 0xFF;

	if (WARN_ON(!taskid))
		return cb_idx;

	list_for_each_entry(sp_cmd, &adapter->driver_cmds.special_cmd_list,
			    list)
		if (taskid == sp_cmd->taskid ||
		    taskid == sp_cmd->hp_taskid ||
		    taskid == sp_cmd->inter_taskid)
			return sp_cmd->cb_idx;

	WARN_ON(cb_idx == 0xFF);
	return cb_idx;
}

struct scsi_cmnd *leapraid_get_scmd_from_taskid(
					struct leapraid_adapter *adapter,
					u16 taskid)
{
	struct leapraid_scsiio_req *leap_mpi_req;
	struct leapraid_io_req_tracker *st;
	struct scsi_cmnd *scmd;
	u32 uniq_tag;

	if (taskid <= 0 || taskid > adapter->shost->can_queue)
		return NULL;

	uniq_tag = adapter->mem_desc.taskid_to_uniq_tag[taskid - 1] <<
		 BLK_MQ_UNIQUE_TAG_BITS | (taskid - 1);
	leap_mpi_req = leapraid_get_task_desc(adapter, taskid);
	if (!leap_mpi_req->dev_hdl)
		return NULL;

	scmd = scsi_host_find_tag(adapter->shost, uniq_tag);
	if (scmd) {
		st = scsi_cmd_priv(scmd);
		if (st && st->taskid == taskid)
			return scmd;
	}

	return NULL;
}

u16 leapraid_alloc_scsiio_taskid(struct leapraid_adapter *adapter,
				 struct scsi_cmnd *scmd)
{
	struct leapraid_io_req_tracker *request;
	u16 taskid;
	u32 tag = scsi_cmd_to_rq(scmd)->tag;
	u32 unique_tag;

	unique_tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmd));
	tag = blk_mq_unique_tag_to_tag(unique_tag);
	adapter->mem_desc.taskid_to_uniq_tag[tag] =
		blk_mq_unique_tag_to_hwq(unique_tag);

	request = scsi_cmd_priv(scmd);
	taskid = tag + 1;
	request->taskid = taskid;
	request->scmd = scmd;
	return taskid;
}

static void leapraid_check_pending_io(struct leapraid_adapter *adapter)
{
	if (adapter->access_ctrl.shost_recovering &&
	    adapter->reset_desc.pending_io_cnt) {
		if (adapter->reset_desc.pending_io_cnt == 1)
			wake_up(&adapter->reset_desc.reset_wait_queue);
		adapter->reset_desc.pending_io_cnt--;
	}
}

static void leapraid_clear_io_tracker(
		struct leapraid_adapter *adapter,
		struct leapraid_io_req_tracker *io_tracker)
{
	if (!io_tracker)
		return;

	if (WARN_ON(io_tracker->taskid == 0))
		return;

	io_tracker->scmd = NULL;
}

static bool leapraid_is_fixed_taskid(struct leapraid_adapter *adapter,
				     u16 taskid)
{
	struct leapraid_driver_cmds *driver_cmds = &adapter->driver_cmds;

	return (taskid == driver_cmds->ctl_cmd.taskid ||
		taskid == driver_cmds->tm_cmd.hp_taskid ||
		taskid == driver_cmds->ctl_cmd.hp_taskid ||
		taskid == driver_cmds->scan_dev_cmd.inter_taskid ||
		taskid == driver_cmds->transport_cmd.inter_taskid ||
		taskid == driver_cmds->cfg_op_cmd.inter_taskid ||
		taskid == driver_cmds->enc_cmd.inter_taskid ||
		taskid == driver_cmds->notify_event_cmd.inter_taskid);
}

void leapraid_free_taskid(struct leapraid_adapter *adapter, u16 taskid)
{
	struct leapraid_io_req_tracker *io_tracker;
	void *task_desc;

	if (leapraid_is_fixed_taskid(adapter, taskid))
		return;

	if (taskid <= adapter->shost->can_queue) {
		io_tracker = leapraid_get_io_tracker_from_taskid(adapter,
								 taskid);
		if (!io_tracker) {
			leapraid_check_pending_io(adapter);
			return;
		}

		task_desc = leapraid_get_task_desc(adapter, taskid);
		memset(task_desc, 0, LEAPRAID_REQUEST_SIZE);
		leapraid_clear_io_tracker(adapter, io_tracker);
		leapraid_check_pending_io(adapter);
		adapter->mem_desc.taskid_to_uniq_tag[taskid - 1] = 0xFFFF;
	}
}

static u8 leapraid_get_msix_idx(struct leapraid_adapter *adapter,
				struct scsi_cmnd *scmd)
{
	if (scmd && adapter->shost->nr_hw_queues > 1) {
		u32 tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmd));

		return blk_mq_unique_tag_to_hwq(tag);
	}
	return adapter->notification_desc.msix_cpu_map[raw_smp_processor_id()];
}

static u8 leapraid_get_and_set_msix_idx_from_taskid(
		struct leapraid_adapter *adapter, u16 taskid)
{
	struct leapraid_io_req_tracker *io_tracker = NULL;

	if (taskid <= adapter->shost->can_queue)
		io_tracker = leapraid_get_io_tracker_from_taskid(adapter,
								 taskid);

	if (!io_tracker)
		return leapraid_get_msix_idx(adapter, NULL);

	io_tracker->msix_io = leapraid_get_msix_idx(adapter, io_tracker->scmd);

	return io_tracker->msix_io;
}

void leapraid_fire_scsi_io(struct leapraid_adapter *adapter, u16 taskid,
			   u16 handle)
{
	struct leapraid_atomic_req_desc desc;

	desc.flg = LEAPRAID_REQ_DESC_FLG_SCSI_IO;
	desc.msix_idx = leapraid_get_and_set_msix_idx_from_taskid(adapter,
								  taskid);
	desc.taskid = cpu_to_le16(taskid);
	writel((__force u32)cpu_to_le32(*((u32 *)&desc)),
	       &adapter->iomem_base->atomic_req_desc_post);
}

void leapraid_fire_hpr_task(struct leapraid_adapter *adapter, u16 taskid,
			    u16 msix_task)
{
	struct leapraid_atomic_req_desc desc;

	desc.flg = LEAPRAID_REQ_DESC_FLG_HPR;
	desc.msix_idx = msix_task;
	desc.taskid = cpu_to_le16(taskid);
	writel((__force u32)cpu_to_le32(*((u32 *)&desc)),
	       &adapter->iomem_base->atomic_req_desc_post);
}

void leapraid_fire_task(struct leapraid_adapter *adapter, u16 taskid)
{
	struct leapraid_atomic_req_desc desc;

	desc.flg = LEAPRAID_REQ_DESC_FLG_DFLT_TYPE;
	desc.msix_idx = leapraid_get_and_set_msix_idx_from_taskid(adapter,
								  taskid);
	desc.taskid = cpu_to_le16(taskid);
	writel((__force u32)cpu_to_le32(*((u32 *)&desc)),
	       &adapter->iomem_base->atomic_req_desc_post);
}

void leapraid_clean_active_scsi_cmds(struct leapraid_adapter *adapter)
{
	struct leapraid_io_req_tracker *io_tracker;
	struct scsi_cmnd *scmd;
	void *task_desc;
	u16 taskid;

	leapraid_sync_irqs_for_cleanup(adapter);

	for (taskid = 1; taskid <= adapter->shost->can_queue; taskid++) {
		scmd = leapraid_get_scmd_from_taskid(adapter, taskid);
		if (!scmd)
			continue;

		io_tracker = scsi_cmd_priv(scmd);
		if (io_tracker && io_tracker->taskid == 0)
			continue;

		scsi_dma_unmap(scmd);
		task_desc = leapraid_get_task_desc(adapter, taskid);
		memset(task_desc, 0, LEAPRAID_REQUEST_SIZE);
		leapraid_clear_io_tracker(adapter, io_tracker);
		if (!leapraid_pci_active(adapter) ||
		    adapter->reset_desc.adapter_reset_results != 0 ||
		    atomic_read(&adapter->overheat_desc.thermal_alert) ||
		    adapter->access_ctrl.host_removing)
			scmd->result = DID_NO_CONNECT << 16;
		else
			scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
		scsi_done(scmd);
	}
}

static void leapraid_clean_active_driver_cmd(struct leapraid_driver_cmd *cmd)
{
	if (cmd->status & LEAPRAID_CMD_PENDING) {
		cmd->status |= LEAPRAID_CMD_RESET;
		complete(&cmd->done);
	}
}

static void leapraid_clean_active_driver_cmds(struct leapraid_adapter *adapter)
{
	struct leapraid_driver_cmds *driver_cmds;

	driver_cmds = &adapter->driver_cmds;
	leapraid_clean_active_driver_cmd(&driver_cmds->tm_cmd);
	leapraid_clean_active_driver_cmd(&driver_cmds->transport_cmd);
	leapraid_clean_active_driver_cmd(&driver_cmds->enc_cmd);
	leapraid_clean_active_driver_cmd(&driver_cmds->notify_event_cmd);
	leapraid_clean_active_driver_cmd(&driver_cmds->cfg_op_cmd);
	leapraid_clean_active_driver_cmd(&driver_cmds->ctl_cmd);

	if (driver_cmds->scan_dev_cmd.status & LEAPRAID_CMD_PENDING) {
		adapter->scan_dev_desc.scan_dev_failed = 1;
		driver_cmds->scan_dev_cmd.status |= LEAPRAID_CMD_RESET;
		if (adapter->scan_dev_desc.driver_loading) {
			adapter->scan_dev_desc.scan_start_failed =
				LEAPRAID_ADAPTER_STATUS_INTERNAL_ERROR;
			adapter->scan_dev_desc.scan_start = 0;
		} else {
			complete(&driver_cmds->scan_dev_cmd.done);
		}
	}
}

static void leapraid_clean_active_cmds(struct leapraid_adapter *adapter)
{
	leapraid_clean_active_driver_cmds(adapter);
	leapraid_clean_active_fw_evt(adapter);
	leapraid_clean_active_scsi_cmds(adapter);
}

static void leapraid_tgt_not_responding(struct leapraid_adapter *adapter,
					u16 hdl)
{
	struct leapraid_starget_priv *starget_priv = NULL;
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags = 0;
	u32 adapter_state;

	if (adapter->access_ctrl.pcie_recovering)
		return;

	adapter_state = leapraid_get_adapter_state(adapter);
	if (adapter_state != LEAPRAID_DB_OPERATIONAL)
		return;

	if (!hdl || hdl > adapter->adapter_attr.features.max_dev_handle ||
	    test_bit(hdl, adapter->dev_topo.pd_hdls))
		return;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (sas_dev && sas_dev->starget && sas_dev->starget->hostdata) {
		starget_priv = sas_dev->starget->hostdata;
		starget_priv->deleted = 1;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (starget_priv)
		starget_priv->hdl = LEAPRAID_INVALID_DEV_HANDLE;

	if (sas_dev)
		leapraid_sdev_put(sas_dev);
}

static void leapraid_tgt_rst_send(struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_starget_priv *starget_priv = NULL;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_card_port *port;
	u64 sas_address;
	unsigned long flags;
	u32 adapter_state;

	if (adapter->access_ctrl.pcie_recovering)
		return;

	adapter_state = leapraid_get_adapter_state(adapter);
	if (adapter_state != LEAPRAID_DB_OPERATIONAL)
		return;

	if (!hdl || hdl > adapter->adapter_attr.features.max_dev_handle ||
	    test_bit(hdl, adapter->dev_topo.pd_hdls))
		return;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (sas_dev && sas_dev->starget && sas_dev->starget->hostdata) {
		starget_priv = sas_dev->starget->hostdata;
		starget_priv->deleted = 1;
		sas_address = sas_dev->sas_addr;
		port = sas_dev->card_port;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (starget_priv) {
		leapraid_ublk_io_dev(adapter, sas_address, port);
		starget_priv->hdl = LEAPRAID_INVALID_DEV_HANDLE;
	}
	if (sas_dev)
		leapraid_sdev_put(sas_dev);
}

static inline void leapraid_single_mpi_sg_append(
				struct leapraid_adapter *adapter,
				void *sge,
				u32 flag_and_len,
				dma_addr_t dma_addr)
{
	if (adapter->adapter_attr.use_32_dma_mask) {
		struct leapraid_sge_simple32 *sge32 = sge;

		sge32->flg_and_len =
			cpu_to_le32(flag_and_len |
				    (LEAPRAID_SGE_FLG_32 |
				     LEAPRAID_SGE_FLG_SYSTEM_ADDR) <<
				    LEAPRAID_SGE_FLG_SHIFT);
		sge32->addr = cpu_to_le32(dma_addr);
	} else {
		struct leapraid_sge_simple64 *sge64 = sge;

		sge64->flg_and_len =
			cpu_to_le32(flag_and_len |
				    (LEAPRAID_SGE_FLG_64 |
				     LEAPRAID_SGE_FLG_SYSTEM_ADDR) <<
				    LEAPRAID_SGE_FLG_SHIFT);
		sge64->addr = cpu_to_le64(dma_addr);
	}
}

static inline void leapraid_single_ieee_sg_append(void *sge, u8 flag,
						  u8 next_chain_offset,
						  u32 len,
						  dma_addr_t dma_addr)
{
	struct leapraid_chain64_ieee_sg *ieee_sg = sge;

	ieee_sg->flg = flag;
	ieee_sg->next_chain_offset = next_chain_offset;
	ieee_sg->len = cpu_to_le32(len);
	ieee_sg->addr = cpu_to_le64(dma_addr);
}

static void leapraid_build_nodata_mpi_sg(struct leapraid_adapter *adapter,
					 void *sge)
{
	leapraid_single_mpi_sg_append(adapter,
				      sge,
				      (LEAPRAID_SGE_FLG_LAST_ONE |
					LEAPRAID_SGE_FLG_EOB |
					LEAPRAID_SGE_FLG_EOL |
					LEAPRAID_SGE_FLG_SIMPLE_ONE) <<
					LEAPRAID_SGE_FLG_SHIFT,
					LEAPRAID_SGE_NO_DATA_ADDR);
}

void leapraid_build_mpi_sg(struct leapraid_adapter *adapter, void *sge,
			   dma_addr_t h2c_dma_addr, size_t h2c_size,
			   dma_addr_t c2h_dma_addr, size_t c2h_size)
{
	if (h2c_size && !c2h_size) {
		leapraid_single_mpi_sg_append(adapter,
					      sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_LAST_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_EOL |
						LEAPRAID_SGE_FLG_H2C) <<
					       LEAPRAID_SGE_FLG_SHIFT) |
					       h2c_size,
					      h2c_dma_addr);
	} else if (!h2c_size && c2h_size) {
		leapraid_single_mpi_sg_append(adapter,
					      sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_LAST_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_EOL) <<
						LEAPRAID_SGE_FLG_SHIFT) |
						c2h_size,
					      c2h_dma_addr);
	} else if (h2c_size && c2h_size) {
		leapraid_single_mpi_sg_append(adapter,
					      sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_H2C) <<
					       LEAPRAID_SGE_FLG_SHIFT) |
					      h2c_size,
					      h2c_dma_addr);
		if (adapter->adapter_attr.use_32_dma_mask)
			sge += sizeof(struct leapraid_sge_simple32);
		else
			sge += sizeof(struct leapraid_sge_simple64);
		leapraid_single_mpi_sg_append(adapter,
					      sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_LAST_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_EOL) <<
						LEAPRAID_SGE_FLG_SHIFT) |
						c2h_size,
					      c2h_dma_addr);
	} else {
		leapraid_build_nodata_mpi_sg(adapter, sge);
	}
}

void leapraid_build_ieee_nodata_sg(struct leapraid_adapter *adapter, void *sge)
{
	leapraid_single_ieee_sg_append(sge,
				       (LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE |
					LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR |
					LEAPRAID_IEEE_SGE_FLG_EOL),
					0,
					0,
					LEAPRAID_SGE_NO_DATA_ADDR);
}

int leapraid_build_scmd_ieee_sg(struct leapraid_adapter *adapter,
				struct scsi_cmnd *scmd, u16 taskid)
{
	struct leapraid_scsiio_req *scsiio_req;
	struct leapraid_io_req_tracker *io_tracker;
	struct scatterlist *scmd_sg_cur;
	int sg_entries_left;
	void *sg_entry_cur;
	void *host_chain;
	dma_addr_t host_chain_dma;
	u8 host_chain_cursor;
	u32 sg_entries_in_cur_seg;
	u32 chain_offset_in_cur_seg;
	u32 chain_len_in_cur_seg;

	io_tracker = scsi_cmd_priv(scmd);
	scsiio_req = leapraid_get_task_desc(adapter, taskid);
	scmd_sg_cur = scsi_sglist(scmd);
	sg_entries_left = scsi_dma_map(scmd);
	if (sg_entries_left < 0)
		return -ENOMEM;
	sg_entry_cur = &scsiio_req->sgl;
	if (sg_entries_left <= LEAPRAID_SGL_INLINE_THRESHOLD)
		goto fill_last_seg;

	scsiio_req->chain_offset = LEAPRAID_CHAIN_OFFSET_DWORDS;
	leapraid_single_ieee_sg_append(sg_entry_cur,
				       LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE |
				       LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR,
				       0, sg_dma_len(scmd_sg_cur),
				       sg_dma_address(scmd_sg_cur));
	scmd_sg_cur = sg_next(scmd_sg_cur);
	sg_entry_cur += LEAPRAID_IEEE_SGE64_ENTRY_SIZE;
	sg_entries_left--;

	host_chain_cursor = 0;
	host_chain = io_tracker->chain +
		host_chain_cursor * LEAPRAID_CHAIN_SEG_SIZE;
	host_chain_dma = io_tracker->chain_dma +
		host_chain_cursor * LEAPRAID_CHAIN_SEG_SIZE;
	host_chain_cursor += 1;
	for (;;) {
		sg_entries_in_cur_seg =
			(sg_entries_left <= LEAPRAID_MAX_SGES_IN_CHAIN) ?
				sg_entries_left : LEAPRAID_MAX_SGES_IN_CHAIN;
		chain_offset_in_cur_seg =
			(sg_entries_left == (int)sg_entries_in_cur_seg) ?
				0 : sg_entries_in_cur_seg;
		chain_len_in_cur_seg = sg_entries_in_cur_seg *
			LEAPRAID_IEEE_SGE64_ENTRY_SIZE;
		if (chain_offset_in_cur_seg)
			chain_len_in_cur_seg += LEAPRAID_IEEE_SGE64_ENTRY_SIZE;

		leapraid_single_ieee_sg_append(
			sg_entry_cur,
			LEAPRAID_IEEE_SGE_FLG_CHAIN_ONE |
			LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR,
			chain_offset_in_cur_seg, chain_len_in_cur_seg,
			host_chain_dma);
		sg_entry_cur = host_chain;
		if (!chain_offset_in_cur_seg)
			goto fill_last_seg;

		while (sg_entries_in_cur_seg) {
			leapraid_single_ieee_sg_append(
				sg_entry_cur,
				LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE |
				LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR,
				0, sg_dma_len(scmd_sg_cur),
				sg_dma_address(scmd_sg_cur));
			scmd_sg_cur = sg_next(scmd_sg_cur);
			sg_entry_cur += LEAPRAID_IEEE_SGE64_ENTRY_SIZE;
			sg_entries_left--;
			sg_entries_in_cur_seg--;
		}
		host_chain = io_tracker->chain +
			host_chain_cursor * LEAPRAID_CHAIN_SEG_SIZE;
		host_chain_dma = io_tracker->chain_dma +
			host_chain_cursor * LEAPRAID_CHAIN_SEG_SIZE;
		host_chain_cursor += 1;
	}

fill_last_seg:
	while (sg_entries_left > 0) {
		u32 flags = LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE |
			LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR;
		if (sg_entries_left == 1)
			flags |= LEAPRAID_IEEE_SGE_FLG_EOL;
		leapraid_single_ieee_sg_append(sg_entry_cur, flags,
					       0, sg_dma_len(scmd_sg_cur),
					       sg_dma_address(scmd_sg_cur));
		scmd_sg_cur = sg_next(scmd_sg_cur);
		sg_entry_cur += LEAPRAID_IEEE_SGE64_ENTRY_SIZE;
		sg_entries_left--;
	}
	return 0;
}

void leapraid_build_ieee_sg(struct leapraid_adapter *adapter, void *sge,
			    dma_addr_t h2c_dma_addr, size_t h2c_size,
			    dma_addr_t c2h_dma_addr, size_t c2h_size)
{
	u32 base_flag;
	u32 flag;

	base_flag = LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE |
			LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR;

	if (h2c_size && !c2h_size) {
		flag = base_flag | LEAPRAID_IEEE_SGE_FLG_EOL;

		leapraid_single_ieee_sg_append(sge,
					       flag,
					       0,
					       h2c_size,
					       h2c_dma_addr);
	} else if (!h2c_size && c2h_size) {
		flag = base_flag | LEAPRAID_IEEE_SGE_FLG_EOL;

		leapraid_single_ieee_sg_append(sge,
					       flag,
					       0,
					       c2h_size,
					       c2h_dma_addr);
	} else if (h2c_size && c2h_size) {
		leapraid_single_ieee_sg_append(sge,
					       base_flag,
					       0,
					       h2c_size,
					       h2c_dma_addr);
		sge += LEAPRAID_IEEE_SGE64_ENTRY_SIZE;

		flag = base_flag | LEAPRAID_IEEE_SGE_FLG_EOL;
		leapraid_single_ieee_sg_append(sge,
					       flag,
					       0,
					       c2h_size,
					       c2h_dma_addr);
	} else {
		leapraid_build_ieee_nodata_sg(adapter, sge);
	}
}

struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_from_tgt(
		struct leapraid_adapter *adapter,
		struct leapraid_starget_priv *tgt_priv)
{
	assert_spin_locked(&adapter->dev_topo.sas_dev_lock);
	if (tgt_priv->sas_dev)
		leapraid_sdev_get(tgt_priv->sas_dev);

	return tgt_priv->sas_dev;
}

struct leapraid_sas_dev *leapraid_get_sas_dev_from_tgt(
		struct leapraid_adapter *adapter,
		struct leapraid_starget_priv *tgt_priv)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_from_tgt(adapter, tgt_priv);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	return sas_dev;
}

static struct leapraid_card_port *leapraid_get_port_by_id(
		struct leapraid_adapter *adapter,
		u8 port_id, bool skip_dirty)
{
	struct leapraid_card_port *port;
	struct leapraid_card_port *dirty_port = NULL;

	if (!adapter->adapter_attr.enable_mp)
		port_id = LEAPRAID_DISABLE_MP_PORT_ID;

	list_for_each_entry(port, &adapter->dev_topo.card_port_list, list) {
		if (port->port_id != port_id)
			continue;

		if (!(port->flg & LEAPRAID_CARD_PORT_FLG_DIRTY))
			return port;

		if (skip_dirty && !dirty_port)
			dirty_port = port;
	}

	if (dirty_port)
		return dirty_port;

	if (unlikely(!adapter->adapter_attr.enable_mp)) {
		port = kzalloc_obj(*port, GFP_ATOMIC);
		if (!port) {
			dev_warn(&adapter->pdev->dev,
				 "%s: Failed to alloc port\n", __func__);
			return NULL;
		}

		port->port_id = LEAPRAID_DISABLE_MP_PORT_ID;
		list_add_tail(&port->list, &adapter->dev_topo.card_port_list);
		return port;
	}

	return NULL;
}

struct leapraid_vphy *leapraid_get_vphy_by_phy(struct leapraid_card_port *port,
					       u32 phy_seq_num)
{
	struct leapraid_vphy *vphy;

	if (!port || !port->vphys_mask)
		return NULL;

	list_for_each_entry(vphy, &port->vphys_list, list) {
		if (vphy->phy_mask & BIT(phy_seq_num))
			return vphy;
	}

	return NULL;
}

struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_addr_and_rphy(
		struct leapraid_adapter *adapter,
		u64 sas_address,
		struct sas_rphy *rphy)
{
	struct leapraid_sas_dev *sas_dev;

	assert_spin_locked(&adapter->dev_topo.sas_dev_lock);
	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_list, list)
		if (sas_dev->sas_addr == sas_address &&
		    sas_dev->rphy == rphy) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_init_list,
			    list)
		if (sas_dev->sas_addr == sas_address &&
		    sas_dev->rphy == rphy) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	return NULL;
}

struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_addr(
		struct leapraid_adapter *adapter,
		u64 sas_address, struct leapraid_card_port *port)
{
	struct leapraid_sas_dev *sas_dev;

	if (!port) {
		dev_warn(&adapter->pdev->dev, "%s: Invalid port\n", __func__);
		return NULL;
	}

	assert_spin_locked(&adapter->dev_topo.sas_dev_lock);
	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_list, list)
		if (sas_dev->sas_addr == sas_address &&
		    sas_dev->card_port == port) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_init_list,
			    list)
		if (sas_dev->sas_addr == sas_address &&
		    sas_dev->card_port == port) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	return NULL;
}

struct leapraid_sas_dev *leapraid_get_sas_dev_by_addr(
		struct leapraid_adapter *adapter,
		u64 sas_address, struct leapraid_card_port *port)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;

	if (!port) {
		dev_warn(&adapter->pdev->dev, "%s: Invalid port\n", __func__);
		return NULL;
	}

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(adapter, sas_address,
							 port);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	return sas_dev;
}

struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_hdl(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_sas_dev *sas_dev;

	assert_spin_locked(&adapter->dev_topo.sas_dev_lock);
	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_list, list)
		if (sas_dev->hdl == hdl) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_init_list,
			    list)
		if (sas_dev->hdl == hdl) {
			leapraid_sdev_get(sas_dev);
			return sas_dev;
		}

	return NULL;
}

struct leapraid_sas_dev *leapraid_get_sas_dev_by_hdl(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	return sas_dev;
}

void leapraid_sas_dev_remove(struct leapraid_adapter *adapter,
			     struct leapraid_sas_dev *sas_dev)
{
	unsigned long flags;
	bool del_from_list;

	if (!sas_dev) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid SAS device\n", __func__);
		return;
	}

	del_from_list = false;
	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	if (!list_empty(&sas_dev->list)) {
		list_del_init(&sas_dev->list);
		del_from_list = true;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (del_from_list) {
		leapraid_clear_cached_boot_dev(adapter, sas_dev, 0);
		leapraid_sdev_put(sas_dev);
	}
}

static void leapraid_sas_dev_remove_by_hdl(struct leapraid_adapter *adapter,
					   u16 hdl)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	bool del_from_list;

	if (adapter->access_ctrl.shost_recovering)
		return;

	del_from_list = false;
	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (sas_dev && (!list_empty(&sas_dev->list))) {
		list_del_init(&sas_dev->list);
		del_from_list = true;
		leapraid_sdev_put(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (del_from_list) {
		leapraid_remove_device(adapter, sas_dev);
		leapraid_sdev_put(sas_dev);
	}
}

void leapraid_sas_dev_remove_by_sas_address(struct leapraid_adapter *adapter,
					    u64 sas_address,
					    struct leapraid_card_port *port)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	bool del_from_list;

	if (adapter->access_ctrl.shost_recovering)
		return;

	del_from_list = false;
	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(adapter, sas_address,
							 port);
	if (sas_dev && (!list_empty(&sas_dev->list))) {
		list_del_init(&sas_dev->list);
		del_from_list = true;
		leapraid_sdev_put(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (del_from_list) {
		leapraid_remove_device(adapter, sas_dev);
		leapraid_sdev_put(sas_dev);
	}
}

struct leapraid_raid_volume *leapraid_raid_volume_find_by_id(
		struct leapraid_adapter *adapter, uint id, uint channel)
{
	struct leapraid_raid_volume *raid_volume;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	list_for_each_entry(raid_volume, &adapter->dev_topo.raid_volume_list,
			    list) {
		if (raid_volume->id == id && raid_volume->channel == channel) {
			leapraid_raid_volume_get(raid_volume);
			spin_unlock_irqrestore(
				&adapter->dev_topo.raid_volume_lock, flags);
			return raid_volume;
		}
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	return NULL;
}

struct leapraid_raid_volume *leapraid_raid_volume_find_by_hdl(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_raid_volume *raid_volume;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	list_for_each_entry(raid_volume, &adapter->dev_topo.raid_volume_list,
			    list) {
		if (raid_volume->hdl == hdl) {
			leapraid_raid_volume_get(raid_volume);
			spin_unlock_irqrestore(
				&adapter->dev_topo.raid_volume_lock, flags);
			return raid_volume;
		}
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	return NULL;
}

static struct leapraid_raid_volume *leapraid_raid_volume_find_by_wwid(
		struct leapraid_adapter *adapter, u64 wwid)
{
	struct leapraid_raid_volume *raid_volume;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	list_for_each_entry(raid_volume, &adapter->dev_topo.raid_volume_list,
			    list) {
		if (raid_volume->wwid == wwid) {
			leapraid_raid_volume_get(raid_volume);
			spin_unlock_irqrestore(
				&adapter->dev_topo.raid_volume_lock, flags);
			return raid_volume;
		}
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	return NULL;
}

static void leapraid_raid_volume_add(struct leapraid_adapter *adapter,
				     struct leapraid_raid_volume *raid_volume)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	leapraid_raid_volume_get(raid_volume);
	list_add_tail(&raid_volume->list, &adapter->dev_topo.raid_volume_list);
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);
}

void leapraid_raid_volume_remove(struct leapraid_adapter *adapter,
				 struct leapraid_raid_volume *raid_volume)
{
	unsigned long flags;
	bool del_from_list = false;

	if (!raid_volume) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid RAID volume\n", __func__);
		return;
	}

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	if (!list_empty(&raid_volume->list)) {
		list_del_init(&raid_volume->list);
		del_from_list = true;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	leapraid_clear_cached_boot_dev(adapter, raid_volume, RAID_CHANNEL);
	if (del_from_list)
		leapraid_raid_volume_put(raid_volume);
}

static struct leapraid_enc_node *leapraid_enc_find_by_hdl(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_enc_node *enc_dev;

	list_for_each_entry(enc_dev, &adapter->dev_topo.enc_list, list)
		if (le16_to_cpu(enc_dev->pg0.enc_hdl) == hdl)
			return enc_dev;

	return NULL;
}

struct leapraid_topo_node *leapraid_exp_find_by_sas_address(
		struct leapraid_adapter *adapter,
		u64 sas_address, struct leapraid_card_port *port)
{
	struct leapraid_topo_node *sas_exp;

	if (!port) {
		dev_warn(&adapter->pdev->dev, "%s: Invalid port\n", __func__);
		return NULL;
	}

	list_for_each_entry(sas_exp, &adapter->dev_topo.exp_list, list)
		if (sas_exp->sas_address == sas_address &&
		    sas_exp->card_port == port)
			return sas_exp;

	dev_warn(&adapter->pdev->dev,
		 "%s: No expander found for SAS addr=0x%016llx port=%p\n",
		 __func__, (unsigned long long)sas_address, port);
	return NULL;
}

bool leapraid_scmd_find_by_tgt(struct leapraid_adapter *adapter, uint id,
			       uint channel)
{
	struct scsi_cmnd *scmd;
	int taskid;

	for (taskid = 1; taskid <= adapter->shost->can_queue; taskid++) {
		scmd = leapraid_get_scmd_from_taskid(adapter, taskid);
		if (!scmd)
			continue;

		if (scmd->device->id == id && scmd->device->channel == channel)
			return true;
	}

	return false;
}

bool leapraid_scmd_find_by_lun(struct leapraid_adapter *adapter, uint id,
			       unsigned int lun, uint channel)
{
	struct scsi_cmnd *scmd;
	int taskid;

	for (taskid = 1; taskid <= adapter->shost->can_queue; taskid++) {
		scmd = leapraid_get_scmd_from_taskid(adapter, taskid);
		if (!scmd)
			continue;

		if (scmd->device->id == id &&
		    scmd->device->channel == channel &&
		    scmd->device->lun == lun)
			return true;
	}

	return false;
}

static struct leapraid_topo_node *leapraid_exp_find_by_hdl(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_topo_node *sas_exp;

	list_for_each_entry(sas_exp, &adapter->dev_topo.exp_list, list)
		if (sas_exp->hdl == hdl)
			return sas_exp;

	return NULL;
}

static enum leapraid_card_port_checking_flg leapraid_get_card_port_feature(
		struct leapraid_card_port *old_card_port,
		struct leapraid_card_port *card_port,
		struct leapraid_card_port_feature *feature)
{
	feature->dirty_flg =
		old_card_port->flg & LEAPRAID_CARD_PORT_FLG_DIRTY;
	feature->same_addr =
		old_card_port->sas_address == card_port->sas_address;
	feature->exact_phy =
		old_card_port->phy_mask == card_port->phy_mask;
	feature->phy_overlap =
		old_card_port->phy_mask & card_port->phy_mask;
	feature->same_port =
		old_card_port->port_id == card_port->port_id;
	feature->cur_chking_old_port = old_card_port;

	if (!feature->dirty_flg || !feature->same_addr)
		return CARD_PORT_SKIP_CHECKING;

	return CARD_PORT_FURTHER_CHECKING_NEEDED;
}

static bool leapraid_process_card_port_feature(
		struct leapraid_card_port_feature *feature)
{
	struct leapraid_card_port *old_card_port;

	old_card_port = feature->cur_chking_old_port;
	if (feature->exact_phy) {
		feature->checking_state = SAME_PORT_WITH_NOTHING_CHANGED;
		feature->expected_old_port = old_card_port;
		return true;
	}

	if (feature->phy_overlap) {
		if (feature->same_port) {
			feature->checking_state =
				SAME_PORT_WITH_PARTIALLY_CHANGED_PHYS;
			feature->expected_old_port = old_card_port;
		} else if (feature->checking_state !=
			   SAME_PORT_WITH_PARTIALLY_CHANGED_PHYS) {
			feature->checking_state =
				SAME_ADDR_WITH_PARTIALLY_CHANGED_PHYS;
			feature->expected_old_port = old_card_port;
		}
	} else if (feature->checking_state !=
		   SAME_PORT_WITH_PARTIALLY_CHANGED_PHYS &&
		   feature->checking_state !=
		   SAME_ADDR_WITH_PARTIALLY_CHANGED_PHYS) {
		feature->checking_state = SAME_ADDR_ONLY;
		feature->expected_old_port = old_card_port;
		feature->same_addr_port_count++;
	}

	return false;
}

static int leapraid_check_card_port(
		struct leapraid_adapter *adapter,
		struct leapraid_card_port *card_port,
		struct leapraid_card_port **expected_card_port,
		int *count)
{
	struct leapraid_card_port *old_card_port;
	struct leapraid_card_port_feature feature;

	*expected_card_port = NULL;
	memset(&feature, 0, sizeof(struct leapraid_card_port_feature));
	feature.expected_old_port = NULL;
	feature.same_addr_port_count = 0;
	feature.checking_state = NEW_CARD_PORT;

	list_for_each_entry(old_card_port, &adapter->dev_topo.card_port_list,
			    list) {
		if (leapraid_get_card_port_feature(old_card_port, card_port,
						   &feature))
			continue;

		if (leapraid_process_card_port_feature(&feature))
			break;
	}

	if (feature.checking_state == SAME_ADDR_ONLY)
		*count = feature.same_addr_port_count;

	*expected_card_port = feature.expected_old_port;
	return feature.checking_state;
}

static void leapraid_del_phy_part_of_anther_port(
		struct leapraid_adapter *adapter,
		struct leapraid_card_port *card_port_table, int index,
		u8 port_count, int offset)
{
	struct leapraid_topo_node *card_topo_node;
	bool found = false;
	int i;

	card_topo_node = &adapter->dev_topo.card;
	for (i = 0; i < port_count; i++) {
		if (i == index)
			continue;

		if (card_port_table[i].phy_mask & BIT(offset)) {
			leapraid_transport_detach_phy_to_port(
				adapter,
				card_topo_node,
				&card_topo_node->card_phy[offset]);
			found = true;
			break;
		}
	}

	if (!found)
		card_port_table[index].phy_mask |= BIT(offset);
}

static void leapraid_add_or_del_phys_from_existing_port(
		struct leapraid_adapter *adapter,
		struct leapraid_card_port *card_port,
		struct leapraid_card_port *card_port_table,
		int index, u8 port_count)
{
	struct leapraid_topo_node *card_topo_node;
	u32 phy_mask_diff;
	u32 offset;

	card_topo_node = &adapter->dev_topo.card;
	phy_mask_diff = card_port->phy_mask ^
		card_port_table[index].phy_mask;
	for (offset = 0; offset < adapter->dev_topo.card.phys_num; offset++) {
		if (!(phy_mask_diff & BIT(offset)))
			continue;

		if (!(card_port_table[index].phy_mask & BIT(offset))) {
			leapraid_del_phy_part_of_anther_port(adapter,
							     card_port_table,
							     index, port_count,
							     offset);
			continue;
		}

		if (card_topo_node->card_phy[offset].phy_is_assigned)
			leapraid_transport_detach_phy_to_port(
				adapter,
				card_topo_node,
				&card_topo_node->card_phy[offset]);

		leapraid_transport_attach_phy_to_port(
			adapter,
			card_topo_node,
			&card_topo_node->card_phy[offset],
			card_port->sas_address,
			card_port);
	}
}

struct leapraid_sas_dev *leapraid_get_next_sas_dev_from_init_list(
		struct leapraid_adapter *adapter)
{
	struct leapraid_sas_dev *sas_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	if (!list_empty(&adapter->dev_topo.sas_dev_init_list)) {
		sas_dev = list_first_entry(&adapter->dev_topo.sas_dev_init_list,
					   struct leapraid_sas_dev,
					   list);
		leapraid_sdev_get(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	return sas_dev;
}

static bool leapraid_check_boot_dev_internal(u64 sas_address, u64 dev_name,
					     u64 enc_lid, u16 slot,
					     struct leapraid_boot_dev *boot_dev,
					     u8 form)
{
	struct leapraid_boot_dev_format_sas_wwid *wwid;
	struct leapraid_boot_dev_format_enc_slot *es;
	struct leapraid_boot_dev_format_dev_name *dn;
	void *pg_dev;

	if (!boot_dev)
		return false;

	pg_dev = boot_dev->pg_dev;
	switch (form & LEAPRAID_BOOTDEV_FORM_MASK) {
	case LEAPRAID_BOOTDEV_FORM_SAS_WWID:
		wwid = pg_dev;

		if (!sas_address)
			return false;

		return sas_address == le64_to_cpu(wwid->sas_addr);
	case LEAPRAID_BOOTDEV_FORM_ENC_SLOT:
		es = pg_dev;

		if (!enc_lid)
			return false;

		return (enc_lid == le64_to_cpu(es->enc_lid) &&
			slot == le16_to_cpu(es->slot_num));
	case LEAPRAID_BOOTDEV_FORM_DEV_NAME:
		dn = pg_dev;

		if (!dev_name)
			return false;

		return dev_name == le64_to_cpu(dn->dev_name);
	case LEAPRAID_BOOTDEV_FORM_NONE:
	default:
		return false;
	}
}

void leapraid_boot_dev_get(void *dev, u32 chnl)
{
	if (!dev)
		return;

	if (chnl == RAID_CHANNEL)
		leapraid_raid_volume_get((struct leapraid_raid_volume *)dev);
	else
		leapraid_sdev_get((struct leapraid_sas_dev *)dev);
}

void leapraid_boot_dev_put(void *dev, u32 chnl)
{
	if (!dev)
		return;

	if (chnl == RAID_CHANNEL)
		leapraid_raid_volume_put((struct leapraid_raid_volume *)dev);
	else
		leapraid_sdev_put((struct leapraid_sas_dev *)dev);
}

static void leapraid_try_set_boot_dev(struct leapraid_boot_dev *boot_dev,
				      u64 sas_addr, u64 dev_name,
				      u64 enc_lid, u16 slot,
				      void *dev, u32 chnl)
{
	bool matched = false;

	if (boot_dev->dev)
		return;

	matched = leapraid_check_boot_dev_internal(sas_addr, dev_name, enc_lid,
						   slot, boot_dev,
						   boot_dev->form);
	if (matched) {
		leapraid_boot_dev_get(dev, chnl);
		boot_dev->dev = dev;
		boot_dev->chnl = chnl;
	}
}

static void leapraid_clear_boot_dev(struct leapraid_adapter *adapter,
				    struct leapraid_boot_dev *boot_dev,
				    void *dev, u32 chnl)
{
	void *cached_dev;
	u32 cached_chnl;
	unsigned long flags;

	spin_lock_irqsave(&adapter->boot_devs.lock, flags);
	if (boot_dev->dev != dev || boot_dev->chnl != chnl)
		goto out_unlock;

	cached_dev = boot_dev->dev;
	cached_chnl = boot_dev->chnl;
	boot_dev->dev = NULL;
	boot_dev->chnl = 0;
	spin_unlock_irqrestore(&adapter->boot_devs.lock, flags);
	leapraid_boot_dev_put(cached_dev, cached_chnl);
	return;

out_unlock:
	spin_unlock_irqrestore(&adapter->boot_devs.lock, flags);
}

static void leapraid_clear_cached_boot_dev(struct leapraid_adapter *adapter,
					   void *dev, u32 chnl)
{
	leapraid_clear_boot_dev(adapter,
				&adapter->boot_devs.requested_boot_dev,
				dev, chnl);
	leapraid_clear_boot_dev(adapter,
				&adapter->boot_devs.requested_alt_boot_dev,
				dev, chnl);
	leapraid_clear_boot_dev(adapter,
				&adapter->boot_devs.current_boot_dev,
				dev, chnl);
}

static void leapraid_check_boot_dev(struct leapraid_adapter *adapter,
				    void *dev, u32 chnl)
{
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_sas_dev *sas_dev;
	u64 sas_addr;
	u64 dev_name = 0;
	u64 enc_lid = 0;
	u16 slot = 0;

	if (!adapter->scan_dev_desc.driver_loading)
		return;

	switch (chnl) {
	case RAID_CHANNEL:
		raid_volume = dev;
		sas_addr = raid_volume->wwid;
		break;
	default:
		sas_dev = dev;
		sas_addr = sas_dev->sas_addr;
		dev_name = sas_dev->dev_name;
		enc_lid = sas_dev->enc_lid;
		slot = sas_dev->slot;
		break;
	}

	leapraid_try_set_boot_dev(&adapter->boot_devs.requested_boot_dev,
				  sas_addr, dev_name, enc_lid,
				  slot, dev, chnl);
	leapraid_try_set_boot_dev(&adapter->boot_devs.requested_alt_boot_dev,
				  sas_addr, dev_name, enc_lid,
				  slot, dev, chnl);
	leapraid_try_set_boot_dev(&adapter->boot_devs.current_boot_dev,
				  sas_addr, dev_name, enc_lid,
				  slot, dev, chnl);
}

static const char *leapraid_func_name(u8 func)
{
	switch (func) {
	case LEAPRAID_FUNC_SCSIIO:
		return "SCSIIO";
	case LEAPRAID_FUNC_SCSI_TMF:
		return "SCSI_TMF";
	case LEAPRAID_FUNC_ADAPTER_INIT:
		return "ADAPTER_INIT";
	case LEAPRAID_FUNC_GET_ADAPTER_FEATURES:
		return "GET_ADAPTER_FEATURES";
	case LEAPRAID_FUNC_CONFIG_OP:
		return "CONFIG_OP";
	case LEAPRAID_FUNC_SCAN_DEV:
		return "SCAN_DEV";
	case LEAPRAID_FUNC_EVENT_NOTIFY:
		return "EVENT_NOTIFY";
	case LEAPRAID_FUNC_FW_DOWNLOAD:
		return "FW_DOWNLOAD";
	case LEAPRAID_FUNC_FW_UPLOAD:
		return "FW_UPLOAD";
	case LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH:
		return "SCSIIO_RAID_PASSTHROUGH";
	case LEAPRAID_FUNC_SCSI_ENC_PROCESSOR:
		return "SCSI_ENC_PROCESSOR";
	case LEAPRAID_FUNC_SMP_PASSTHROUGH:
		return "SMP_PASSTHROUGH";
	case LEAPRAID_FUNC_SAS_IO_UNIT_CTRL:
		return "SAS_IO_UNIT_CTRL";
	case LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH:
		return "SCSIIO_SATA_PASSTHROUGH";
	case LEAPRAID_FUNC_ADAPTER_UNIT_RESET:
		return "ADAPTER_UNIT_RESET";
	case LEAPRAID_FUNC_HANDSHAKE:
		return "HANDSHAKE";
	case LEAPRAID_FUNC_LOGBUF_INIT:
		return "LOGBUF_INIT";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_cfg_action_name(u8 action)
{
	switch (action) {
	case LEAPRAID_CFG_ACT_PAGE_HEADER:
		return "PAGE_HEADER";
	case LEAPRAID_CFG_ACT_PAGE_READ_CUR:
		return "PAGE_READ_CUR";
	case LEAPRAID_CFG_ACT_PAGE_WRITE_CUR:
		return "PAGE_WRITE_CUR";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_cfg_page_type_name(u8 page_type)
{
	switch (page_type) {
	case LEAPRAID_CFG_PT_IO_UNIT:
		return "IO_UNIT";
	case LEAPRAID_CFG_PT_ADAPTER:
		return "ADAPTER";
	case LEAPRAID_CFG_PT_BIOS:
		return "BIOS";
	case LEAPRAID_CFG_PT_RAID_VOLUME:
		return "RAID_VOLUME";
	case LEAPRAID_CFG_PT_MANUFACTURING:
		return "MANUFACTURING";
	case LEAPRAID_CFG_PT_RAID_PHYSDISK:
		return "RAID_PHYSDISK";
	case LEAPRAID_CFG_PT_EXTENDED:
		return "EXTENDED";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_cfg_ext_page_type_name(u8 ext_page_type)
{
	switch (ext_page_type) {
	case LEAPRAID_CFG_EXTPT_SAS_IO_UNIT:
		return "SAS_IO_UNIT";
	case LEAPRAID_CFG_EXTPT_SAS_EXP:
		return "SAS_EXPANDER";
	case LEAPRAID_CFG_EXTPT_SAS_DEV:
		return "SAS_DEVICE";
	case LEAPRAID_CFG_EXTPT_SAS_PHY:
		return "SAS_PHY";
	case LEAPRAID_CFG_EXTPT_ENC:
		return "ENCLOSURE";
	case LEAPRAID_CFG_EXTPT_RAID_CONFIG:
		return "RAID_CONFIG";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_sep_action_name(u8 action)
{
	switch (action) {
	case LEAPRAID_SEP_REQ_ACT_WRITE_STATUS:
		return "WRITE_STATUS";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_sas_op_name(u8 op)
{
	switch (op) {
	case LEAPRAID_SAS_OP_PHY_LINK_RESET:
		return "PHY_LINK_RESET";
	case LEAPRAID_SAS_OP_PHY_HARD_RESET:
		return "PHY_HARD_RESET";
	case LEAPRAID_SAS_OP_SET_PARAMETER:
		return "SET_PARAMETER";
	default:
		return "UNKNOWN";
	}
}

static const char *leapraid_tm_type_name(u8 task_type)
{
	switch (task_type) {
	case LEAPRAID_TM_TASKTYPE_ABORT_TASK:
		return "ABORT_TASK";
	case LEAPRAID_TM_TASKTYPE_ABRT_TASK_SET:
		return "ABORT_TASK_SET";
	case LEAPRAID_TM_TASKTYPE_TARGET_RESET:
		return "TARGET_RESET";
	case LEAPRAID_TM_TASKTYPE_LOGICAL_UNIT_RESET:
		return "LOGICAL_UNIT_RESET";
	case LEAPRAID_TM_TASKTYPE_CLEAR_TASK_SET:
		return "CLEAR_TASK_SET";
	case LEAPRAID_TM_TASKTYPE_QUERY_TASK:
		return "QUERY_TASK";
	case LEAPRAID_TM_TASKTYPE_CLEAR_ACA:
		return "CLEAR_ACA";
	case LEAPRAID_TM_TASKTYPE_QUERY_TASK_SET:
		return "QUERY_TASK_SET";
	case LEAPRAID_TM_TASKTYPE_QUERY_ASYNC_EVENT:
		return "QUERY_ASYNC_EVENT";
	default:
		return "UNKNOWN";
	}
}

void leapraid_log_req_context(struct leapraid_adapter *adapter, u16 smid,
			      const void *req_data)
{
	const struct leapraid_req *req = req_data;

	if (!adapter || !adapter->pdev || !req_data)
		return;

	switch (req->func) {
	case LEAPRAID_FUNC_CONFIG_OP: {
		const struct leapraid_cfg_req *cfg_req = req_data;

		dev_err(&adapter->pdev->dev,
			"cfg-req: smid=%u func=0x%02x(%s) action=0x%02x(%s)\n",
			smid, req->func, leapraid_func_name(req->func),
			cfg_req->action,
			leapraid_cfg_action_name(cfg_req->action));
		dev_err(&adapter->pdev->dev,
			"cfg-req: page_type=0x%02x(%s) page_num=%u\n",
			cfg_req->header.page_type,
			leapraid_cfg_page_type_name(cfg_req->header.page_type),
			cfg_req->header.page_num);
		if (cfg_req->header.page_type == LEAPRAID_CFG_PT_EXTENDED)
			dev_err(&adapter->pdev->dev,
				"cfg-req: ext_page_type=0x%02x(%s)\n",
				cfg_req->ext_page_type,
				leapraid_cfg_ext_page_type_name(
					cfg_req->ext_page_type));
		dev_err(&adapter->pdev->dev, "cfg-req: page_addr=0x%08x\n",
			le32_to_cpu(cfg_req->page_addr));
		break;
	}
	case LEAPRAID_FUNC_SCSI_TMF: {
		const struct leapraid_scsi_tm_req *tm_req = req_data;

		dev_err(&adapter->pdev->dev,
			"scsi_tm:: smid=%u func=0x%02x(%s) task=0x%02x(%s)\n",
			smid, req->func, leapraid_func_name(req->func),
			tm_req->task_type,
			leapraid_tm_type_name(tm_req->task_type));
		dev_err(&adapter->pdev->dev,
			"scsi_tm:: dev_hdl=0x%04x task_mid=%u\n",
			le16_to_cpu(tm_req->dev_hdl),
			le16_to_cpu(tm_req->task_mid));
		break;
	}
	case LEAPRAID_FUNC_SCSI_ENC_PROCESSOR: {
		const struct leapraid_sep_req *sep_req = req_data;

		dev_err(&adapter->pdev->dev,
			"sep: smid=%u func=0x%02x(%s) action=0x%02x(%s)\n",
			smid, req->func, leapraid_func_name(req->func),
			sep_req->act,
			leapraid_sep_action_name(sep_req->act));
		dev_err(&adapter->pdev->dev,
			"sep: dev_hdl=0x%04x slot=%u enc_hdl=0x%04x\n",
			le16_to_cpu(sep_req->dev_hdl),
			le16_to_cpu(sep_req->slot),
			le16_to_cpu(sep_req->enc_hdl));
		break;
	}
	case LEAPRAID_FUNC_SAS_IO_UNIT_CTRL: {
		const struct leapraid_io_unit_ctrl_req *io_req = req_data;

		dev_err(&adapter->pdev->dev,
			"ctl_cmd: smid=%u func=0x%02x(%s) action=0x%02x(%s)\n",
			smid, req->func, leapraid_func_name(req->func),
			io_req->op, leapraid_sas_op_name(io_req->op));
		dev_err(&adapter->pdev->dev,
			"ctl_cmd: dev_hdl=0x%04x param=0x%02x\n",
			le16_to_cpu(io_req->dev_hdl),
			io_req->adapter_para);
		break;
	}
	case LEAPRAID_FUNC_SMP_PASSTHROUGH: {
		const struct leapraid_smp_passthrough_req *smp_req = req_data;

		dev_err(&adapter->pdev->dev,
			"smp_cmd: smid=%u func=0x%02x(%s) action=0x%02x\n",
			smid, req->func, leapraid_func_name(req->func),
			smp_req->passthrough_flg);
		dev_err(&adapter->pdev->dev,
			"smp_cmd: port=%u req_len=%u\n",
			smp_req->physical_port,
			le16_to_cpu(smp_req->req_data_len));
		dev_err(&adapter->pdev->dev, "smp_cmd: sas_addr=0x%016llx\n",
			(unsigned long long)le64_to_cpu(smp_req->sas_address));
		break;
	}
	default:
		dev_err(&adapter->pdev->dev,
			"cmd: smid=%u func=0x%02x(%s)\n",
			smid, req->func, leapraid_func_name(req->func));
		break;
	}
}

static void leapraid_build_and_fire_cfg_req(
		struct leapraid_adapter *adapter,
		struct leapraid_cfg_req *leap_mpi_cfgp_req,
		struct leapraid_cfg_rep *leap_mpi_cfgp_rep)
{
	struct leapraid_cfg_req *local_leap_cfg_req;
	u16 smid;

	memset(leap_mpi_cfgp_rep, 0, sizeof(struct leapraid_cfg_rep));
	memset(&adapter->driver_cmds.cfg_op_cmd.reply, 0,
	       sizeof(struct leapraid_cfg_rep));
	adapter->driver_cmds.cfg_op_cmd.status = LEAPRAID_CMD_PENDING;
	smid = adapter->driver_cmds.cfg_op_cmd.inter_taskid;
	local_leap_cfg_req = leapraid_get_task_desc(adapter, smid);
	memcpy(local_leap_cfg_req, leap_mpi_cfgp_req,
	       sizeof(struct leapraid_cfg_req));
	init_completion(&adapter->driver_cmds.cfg_op_cmd.done);
	leapraid_fire_task(adapter, smid);
	wait_for_completion_timeout(&adapter->driver_cmds.cfg_op_cmd.done,
				    LEAPRAID_CFG_OP_TIMEOUT * HZ);
}

static int leapraid_req_cfg_func(struct leapraid_adapter *adapter,
				 struct leapraid_cfg_req *leap_mpi_cfgp_req,
				 struct leapraid_cfg_rep *leap_mpi_cfgp_rep,
				 void *target_cfg_pg, void *real_cfg_pg_addr,
				 u16 target_real_cfg_pg_sz)
{
	u32 adapter_status = UINT_MAX;
	bool issue_reset = false;
	u16 smid;
	u8 retry_cnt;
	int rc;

	retry_cnt = 0;
	mutex_lock(&adapter->driver_cmds.cfg_op_cmd.mutex);
	smid = adapter->driver_cmds.cfg_op_cmd.inter_taskid;
retry:
	if (retry_cnt) {
		if (retry_cnt > LEAPRAID_CFG_REQ_RETRY_TIMES) {
			rc = -EFAULT;
			goto out_cleanup;
		}
		dev_warn(&adapter->pdev->dev,
			 "cfg-req: Retry request, cnt=%u\n", retry_cnt);
	}

	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_SHORT,
					  __func__);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"cfg-req: Adapter not operational\n");
		goto out_cleanup;
	}

	leapraid_build_and_fire_cfg_req(adapter, leap_mpi_cfgp_req,
					leap_mpi_cfgp_rep);
	if (!(adapter->driver_cmds.cfg_op_cmd.status & LEAPRAID_CMD_DONE)) {
		retry_cnt++;
		if (adapter->driver_cmds.cfg_op_cmd.status &
		    LEAPRAID_CMD_RESET) {
			dev_warn(&adapter->pdev->dev,
				 "cfg-req: CMD fail due to hard reset\n");
			goto retry;
		}

		if (adapter->access_ctrl.shost_recovering ||
		    adapter->access_ctrl.pcie_recovering) {
			dev_err(&adapter->pdev->dev,
				"cfg-req: pending in %s, status=0x%x\n",
				adapter->access_ctrl.shost_recovering ?
				"shost recovery" : "pcie recovery",
				adapter->driver_cmds.cfg_op_cmd.status);
			leapraid_log_req_context(adapter, smid,
						 leap_mpi_cfgp_req);
			issue_reset = false;
			rc = -EFAULT;
		} else {
			dev_err(&adapter->pdev->dev,
				"cfg-req: timeout, status=0x%x, reset\n",
				adapter->driver_cmds.cfg_op_cmd.status);
			leapraid_log_req_context(adapter, smid,
						 leap_mpi_cfgp_req);
			issue_reset = true;
		}

		goto out_cleanup;
	}

	if (adapter->driver_cmds.cfg_op_cmd.status &
	    LEAPRAID_CMD_REPLY_VALID) {
		memcpy(leap_mpi_cfgp_rep,
		       &adapter->driver_cmds.cfg_op_cmd.reply,
		       sizeof(struct leapraid_cfg_rep));
		adapter_status = le16_to_cpu(
			leap_mpi_cfgp_rep->adapter_status) &
			LEAPRAID_ADAPTER_STATUS_MASK;
		if (adapter_status == LEAPRAID_ADAPTER_STATUS_SUCCESS &&
		    target_cfg_pg && real_cfg_pg_addr &&
		    target_real_cfg_pg_sz &&
		    leap_mpi_cfgp_req->action ==
		    LEAPRAID_CFG_ACT_PAGE_READ_CUR)
			memcpy(target_cfg_pg, real_cfg_pg_addr,
			       target_real_cfg_pg_sz);
		if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS) {
			if (adapter_status !=
			    LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_PAGE)
				dev_err(&adapter->pdev->dev,
					"cfg-rep: adapter_status=0x%x\n",
					adapter_status);
			rc = -EFAULT;
		}
	} else {
		dev_err(&adapter->pdev->dev, "cfg-rep: Reply invalid\n");
		rc = -EFAULT;
	}

out_cleanup:
	adapter->driver_cmds.cfg_op_cmd.status = LEAPRAID_CMD_NOT_USED;
	mutex_unlock(&adapter->driver_cmds.cfg_op_cmd.mutex);
	if (issue_reset) {
		if (adapter->scan_dev_desc.first_scan_dev_fired) {
			dev_warn(&adapter->pdev->dev,
				 "%s:%d cfg-req: Failure, issuing reset\n",
				 __func__, __LINE__);
			leapraid_hard_reset_handler(adapter, FULL_RESET);
		} else {
			dev_warn(&adapter->pdev->dev,
				 "cfg-req: CMD fail in init, skip reset\n");
		}
		rc = -EFAULT;
	}
	return rc;
}

static int leapraid_request_cfg_pg_header(
		struct leapraid_adapter *adapter,
		struct leapraid_cfg_req *leap_mpi_cfgp_req,
		struct leapraid_cfg_rep *leap_mpi_cfgp_rep)
{
	return leapraid_req_cfg_func(adapter, leap_mpi_cfgp_req,
				     leap_mpi_cfgp_rep, NULL, NULL, 0);
}

static int leapraid_request_cfg_pg(struct leapraid_adapter *adapter,
				   struct leapraid_cfg_req *leap_mpi_cfgp_req,
				   struct leapraid_cfg_rep *leap_mpi_cfgp_rep,
				   void *target_cfg_pg, void *real_cfg_pg_addr,
				   u16 target_real_cfg_pg_sz)
{
	return leapraid_req_cfg_func(adapter, leap_mpi_cfgp_req,
				     leap_mpi_cfgp_rep, target_cfg_pg,
				     real_cfg_pg_addr, target_real_cfg_pg_sz);
}

int leapraid_op_config_page(struct leapraid_adapter *adapter,
			    void *target_cfg_pg, union cfg_param_1 cfgp1,
			    union cfg_param_2 cfgp2,
			    enum config_page_action cfg_op)
{
	struct leapraid_cfg_req leap_mpi_cfgp_req;
	struct leapraid_cfg_rep leap_mpi_cfgp_rep;
	u16 real_cfg_pg_sz;
	void *real_cfg_pg_addr;
	dma_addr_t real_cfg_pg_dma = 0;
	u32 __page_size;
	int rc;

	memset(&leap_mpi_cfgp_req, 0, sizeof(struct leapraid_cfg_req));
	leap_mpi_cfgp_req.func = LEAPRAID_FUNC_CONFIG_OP;
	leap_mpi_cfgp_req.action = LEAPRAID_CFG_ACT_PAGE_HEADER;

	switch (cfg_op) {
	case GET_BIOS_PG3:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_BIOS;
		leap_mpi_cfgp_req.header.page_num =
			LEAPRAID_CFG_PAGE_NUM_BIOS3;
		__page_size = sizeof(struct leapraid_bios_page3);
		break;
	case GET_BIOS_PG2:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_BIOS;
		leap_mpi_cfgp_req.header.page_num =
			LEAPRAID_CFG_PAGE_NUM_BIOS2;
		__page_size = sizeof(struct leapraid_bios_page2);
		break;
	case GET_MANUFACTURING_PG0:
		leap_mpi_cfgp_req.header.page_type =
			LEAPRAID_CFG_PT_MANUFACTURING;
		leap_mpi_cfgp_req.header.page_num =
			LEAPRAID_CFG_PAGE_NUM_MANU0;
		__page_size = sizeof(struct leapraid_manufacturing_p0);
		break;
	case GET_SAS_DEVICE_PG0:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type = LEAPRAID_CFG_EXTPT_SAS_DEV;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_DEV0;
		__page_size = sizeof(struct leapraid_sas_dev_p0);
		break;
	case GET_SAS_IOUNIT_PG0:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type =
			LEAPRAID_CFG_EXTPT_SAS_IO_UNIT;
		leap_mpi_cfgp_req.header.page_num =
			LEAPRAID_CFG_PAGE_NUM_IOUNIT0;
		__page_size = cfgp1.size;
		break;
	case GET_SAS_IOUNIT_PG1:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type =
			LEAPRAID_CFG_EXTPT_SAS_IO_UNIT;
		leap_mpi_cfgp_req.header.page_num =
			LEAPRAID_CFG_PAGE_NUM_IOUNIT1;
		__page_size = cfgp1.size;
		break;
	case GET_SAS_EXPANDER_PG0:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type = LEAPRAID_CFG_EXTPT_SAS_EXP;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_EXP0;
		__page_size = sizeof(struct leapraid_exp_p0);
		break;
	case GET_SAS_EXPANDER_PG1:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type = LEAPRAID_CFG_EXTPT_SAS_EXP;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_EXP1;
		__page_size = sizeof(struct leapraid_exp_p1);
		break;
	case GET_SAS_ENCLOSURE_PG0:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type = LEAPRAID_CFG_EXTPT_ENC;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_ENC0;
		__page_size = sizeof(struct leapraid_enc_p0);
		break;
	case GET_PHY_PG0:
		leap_mpi_cfgp_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
		leap_mpi_cfgp_req.ext_page_type = LEAPRAID_CFG_EXTPT_SAS_PHY;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_PHY0;
		__page_size = sizeof(struct leapraid_sas_phy_p0);
		break;
	case GET_RAID_VOLUME_PG0:
		leap_mpi_cfgp_req.header.page_type =
			LEAPRAID_CFG_PT_RAID_VOLUME;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_VOL0;
		__page_size = cfgp1.size;
		break;
	case GET_RAID_VOLUME_PG1:
		leap_mpi_cfgp_req.header.page_type =
			LEAPRAID_CFG_PT_RAID_VOLUME;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_VOL1;
		__page_size = sizeof(struct leapraid_raidvol_p1);
		break;
	case GET_PHY_DISK_PG0:
		leap_mpi_cfgp_req.header.page_type =
			LEAPRAID_CFG_PT_RAID_PHYSDISK;
		leap_mpi_cfgp_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_PD0;
		__page_size = sizeof(struct leapraid_raidpd_p0);
		break;
	default:
		dev_err(&adapter->pdev->dev,
			"Unsupported config page action=%d!\n", cfg_op);
		return -EINVAL;
	}

	leapraid_build_nodata_mpi_sg(adapter,
				     &leap_mpi_cfgp_req.page_buf_sge);
	rc = leapraid_request_cfg_pg_header(adapter,
					    &leap_mpi_cfgp_req,
					    &leap_mpi_cfgp_rep);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"cfg-req: Header failed rc=%dn", rc);
		return rc;
	}

	if (cfg_op == GET_SAS_DEVICE_PG0 ||
	    cfg_op == GET_SAS_EXPANDER_PG0 ||
	    cfg_op == GET_SAS_ENCLOSURE_PG0 ||
	    cfg_op == GET_RAID_VOLUME_PG1)
		leap_mpi_cfgp_req.page_addr = cpu_to_le32(cfgp1.form |
							  cfgp2.handle);
	else if (cfg_op == GET_PHY_DISK_PG0)
		leap_mpi_cfgp_req.page_addr = cpu_to_le32(cfgp1.form |
							  cfgp2.form_specific);
	else if (cfg_op == GET_RAID_VOLUME_PG0)
		leap_mpi_cfgp_req.page_addr =
			cpu_to_le32(cfgp2.handle |
				    LEAPRAID_RAID_VOL_CFG_PGAD_HDL);
	else if (cfg_op == GET_SAS_EXPANDER_PG1)
		leap_mpi_cfgp_req.page_addr =
			cpu_to_le32(cfgp2.handle |
				    (cfgp1.phy_number <<
				     LEAPRAID_SAS_EXP_CFG_PGAD_PHYNUM_SHIFT) |
				    LEAPRAID_SAS_EXP_CFG_PGAD_HDL_PHY_NUM);
	else if (cfg_op == GET_PHY_PG0)
		leap_mpi_cfgp_req.page_addr = cpu_to_le32(cfgp1.phy_number |
			LEAPRAID_SAS_PHY_CFG_PGAD_PHY_NUMBER);

	leap_mpi_cfgp_req.action = LEAPRAID_CFG_ACT_PAGE_READ_CUR;

	leap_mpi_cfgp_req.header.page_num = leap_mpi_cfgp_rep.header.page_num;
	leap_mpi_cfgp_req.header.page_type =
		leap_mpi_cfgp_rep.header.page_type;
	leap_mpi_cfgp_req.header.page_len = leap_mpi_cfgp_rep.header.page_len;
	leap_mpi_cfgp_req.ext_page_len = leap_mpi_cfgp_rep.ext_page_len;
	leap_mpi_cfgp_req.ext_page_type = leap_mpi_cfgp_rep.ext_page_type;

	real_cfg_pg_sz = (leap_mpi_cfgp_req.header.page_len) ?
		leap_mpi_cfgp_req.header.page_len * sizeof(u32) :
		le16_to_cpu(leap_mpi_cfgp_rep.ext_page_len) * sizeof(u32);
	real_cfg_pg_addr = dma_alloc_coherent(&adapter->pdev->dev,
					      real_cfg_pg_sz,
					      &real_cfg_pg_dma,
					      GFP_KERNEL);
	if (!real_cfg_pg_addr)
		return -ENOMEM;

	if (leap_mpi_cfgp_req.action == LEAPRAID_CFG_ACT_PAGE_WRITE_CUR) {
		leapraid_single_mpi_sg_append(adapter,
					      &leap_mpi_cfgp_req.page_buf_sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_LAST_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_EOL |
						LEAPRAID_SGE_FLG_H2C) <<
						LEAPRAID_SGE_FLG_SHIFT) |
					       real_cfg_pg_sz,
					      real_cfg_pg_dma);
		memcpy(real_cfg_pg_addr, target_cfg_pg,
		       min_t(u16, real_cfg_pg_sz, __page_size));
	} else {
		memset(target_cfg_pg, 0, __page_size);
		leapraid_single_mpi_sg_append(adapter,
					      &leap_mpi_cfgp_req.page_buf_sge,
					      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
						LEAPRAID_SGE_FLG_LAST_ONE |
						LEAPRAID_SGE_FLG_EOB |
						LEAPRAID_SGE_FLG_EOL) <<
						LEAPRAID_SGE_FLG_SHIFT) |
					       real_cfg_pg_sz,
					      real_cfg_pg_dma);
		memset(real_cfg_pg_addr, 0,
		       min_t(u16, real_cfg_pg_sz, __page_size));
	}

	rc = leapraid_request_cfg_pg(adapter,
				     &leap_mpi_cfgp_req,
				     &leap_mpi_cfgp_rep,
				     target_cfg_pg,
				     real_cfg_pg_addr,
				     min_t(u16, real_cfg_pg_sz, __page_size));
	if (rc) {
		u32 status;

		status = le16_to_cpu(leap_mpi_cfgp_rep.adapter_status) &
				     LEAPRAID_ADAPTER_STATUS_MASK;
		if (status != LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_PAGE)
			dev_err(&adapter->pdev->dev,
				"cfg-req: rc=%d, pg_info: 0x%x, 0x%x, %d\n",
				rc, leap_mpi_cfgp_req.header.page_type,
				leap_mpi_cfgp_req.ext_page_type,
				leap_mpi_cfgp_req.header.page_num);
	}

	if (real_cfg_pg_addr)
		dma_free_coherent(&adapter->pdev->dev,
				  real_cfg_pg_sz,
				  real_cfg_pg_addr,
				  real_cfg_pg_dma);

	return rc;
}

static int leapraid_cfg_find_vol_in_page(
		struct leapraid_raid_cfg_p0 *raid_cfg_p0,
		u16 pd_hdl, u16 *vol_hdl)
{
	u16 elements = raid_cfg_p0->elements_num;
	int i;

	for (i = 0; i < elements; i++) {
		struct leapraid_raid_cfg_p0_element *elem;
		u16 type;

		elem = &raid_cfg_p0->cfg_element[i];

		type = le16_to_cpu(elem->element_flg) &
		       LEAPRAID_RAIDCFG_P0_EFLG_MASK_ELEMENT_TYPE;

		switch (type) {
		case LEAPRAID_RAIDCFG_P0_EFLG_VOL_PHYS_DISK_ELEMENT:
		case LEAPRAID_RAIDCFG_P0_EFLG_OCE_ELEMENT: {
			u16 phys_hdl;

			phys_hdl = le16_to_cpu(elem->phys_disk_dev_hdl);
			if (phys_hdl == pd_hdl) {
				*vol_hdl = le16_to_cpu(elem->vol_dev_hdl);
				return 0;
			}
			break;
		}
		case LEAPRAID_RAIDCFG_P0_EFLG_HOT_SPARE_ELEMENT:
			*vol_hdl = 0;
			return 0;

		default:
			break;
		}
	}

	return -ENOENT;
}

static int leapraid_cfg_get_volume_hdl_dispatch(
		struct leapraid_adapter *adapter,
		struct leapraid_cfg_req *cfg_req,
		struct leapraid_cfg_rep *cfg_rep,
		struct leapraid_raid_cfg_p0 *raid_cfg_p0,
		void *real_cfg_pg_addr,
		u16 real_cfg_pg_sz,
		u16 raid_cfg_p0_sz,
		u16 pd_hdl, u16 *vol_hdl)
{
	u16 adapter_status;
	int config_num;
	int rc;

	config_num = 0xFF;
	while (true) {
		cfg_req->page_addr =
			cpu_to_le32(config_num +
				    LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP);
		rc = leapraid_request_cfg_pg(
			adapter, cfg_req, cfg_rep,
			raid_cfg_p0, real_cfg_pg_addr,
			min_t(u16, real_cfg_pg_sz, raid_cfg_p0_sz));
		adapter_status = le16_to_cpu(cfg_rep->adapter_status) &
			LEAPRAID_ADAPTER_STATUS_MASK;
		if (rc) {
			if (adapter_status ==
			    LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_PAGE) {
				*vol_hdl = 0;
				return 0;
			}
			return rc;
		}

		if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS)
			return LEAPRAID_OPERATION_FAILED;

		rc = leapraid_cfg_find_vol_in_page(raid_cfg_p0,
						   pd_hdl,
						   vol_hdl);
		if (rc != -ENOENT)
			return rc;

		config_num = raid_cfg_p0->cfg_num;
	}
	return 0;
}

int leapraid_cfg_get_volume_hdl(struct leapraid_adapter *adapter,
				u16 pd_hdl, u16 *vol_hdl)
{
	struct leapraid_raid_cfg_p0 *raid_cfg_p0;
	struct leapraid_cfg_req cfg_req;
	struct leapraid_cfg_rep cfg_rep;
	dma_addr_t real_cfg_pg_dma = 0;
	void *real_cfg_pg_addr;
	u16 real_cfg_pg_sz;
	int rc, raid_cfg_p0_sz;

	*vol_hdl = 0;
	memset(&cfg_req, 0, sizeof(struct leapraid_cfg_req));
	cfg_req.func = LEAPRAID_FUNC_CONFIG_OP;
	cfg_req.action = LEAPRAID_CFG_ACT_PAGE_HEADER;
	cfg_req.header.page_type = LEAPRAID_CFG_PT_EXTENDED;
	cfg_req.ext_page_type = LEAPRAID_CFG_EXTPT_RAID_CONFIG;
	cfg_req.header.page_num = LEAPRAID_CFG_PAGE_NUM_VOL0;

	leapraid_build_nodata_mpi_sg(adapter, &cfg_req.page_buf_sge);
	rc = leapraid_request_cfg_pg_header(adapter, &cfg_req, &cfg_rep);
	if (rc)
		return rc;

	cfg_req.action = LEAPRAID_CFG_ACT_PAGE_READ_CUR;
	raid_cfg_p0_sz = le16_to_cpu(cfg_rep.ext_page_len) *
		LEAPRAID_CFG_UNIT_SIZE;
	raid_cfg_p0 = kmalloc(raid_cfg_p0_sz, GFP_KERNEL);
	if (!raid_cfg_p0)
		return -ENOMEM;

	real_cfg_pg_sz = (cfg_req.header.page_len) ?
		cfg_req.header.page_len * LEAPRAID_CFG_UNIT_SIZE :
		le16_to_cpu(cfg_rep.ext_page_len) * LEAPRAID_CFG_UNIT_SIZE;

	real_cfg_pg_addr = dma_alloc_coherent(&adapter->pdev->dev,
					      real_cfg_pg_sz, &real_cfg_pg_dma,
					      GFP_KERNEL);
	if (!real_cfg_pg_addr) {
		rc = -ENOMEM;
		goto out_free;
	}

	memset(raid_cfg_p0, 0, raid_cfg_p0_sz);
	leapraid_single_mpi_sg_append(adapter,
				      &cfg_req.page_buf_sge,
				      ((LEAPRAID_SGE_FLG_SIMPLE_ONE |
					LEAPRAID_SGE_FLG_LAST_ONE |
					LEAPRAID_SGE_FLG_EOB |
					LEAPRAID_SGE_FLG_EOL) <<
				       LEAPRAID_SGE_FLG_SHIFT) |
				      real_cfg_pg_sz,
				      real_cfg_pg_dma);
	memset(real_cfg_pg_addr, 0,
	       min_t(u16, real_cfg_pg_sz, raid_cfg_p0_sz));

	rc = leapraid_cfg_get_volume_hdl_dispatch(adapter,
						  &cfg_req, &cfg_rep,
						  raid_cfg_p0,
						  real_cfg_pg_addr,
						  real_cfg_pg_sz,
						  raid_cfg_p0_sz,
						  pd_hdl, vol_hdl);

out_free:
	if (real_cfg_pg_addr)
		dma_free_coherent(&adapter->pdev->dev,
				  real_cfg_pg_sz, real_cfg_pg_addr,
				  real_cfg_pg_dma);
	kfree(raid_cfg_p0);
	return rc;
}

static int leapraid_get_adapter_phys(struct leapraid_adapter *adapter,
				     u8 *nr_phys)
{
	struct leapraid_sas_io_unit_p0 sas_io_unit_page0;
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	int rc;

	*nr_phys = 0;
	cfgp1.size = sizeof(struct leapraid_sas_io_unit_p0);
	rc = leapraid_op_config_page(adapter, &sas_io_unit_page0, cfgp1,
				     cfgp2, GET_SAS_IOUNIT_PG0);
	if (rc)
		return rc;

	*nr_phys = sas_io_unit_page0.phy_num;

	return 0;
}

static int leapraid_cfg_get_number_pds(struct leapraid_adapter *adapter,
				       u16 hdl, u8 *num_pds)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_raidvol_p0 raidvol_p0;
	int rc;

	*num_pds = 0;
	cfgp1.size = sizeof(struct leapraid_raidvol_p0);
	cfgp2.handle = hdl;
	rc = leapraid_op_config_page(adapter, &raidvol_p0, cfgp1,
				     cfgp2, GET_RAID_VOLUME_PG0);
	if (!rc)
		*num_pds = raidvol_p0.num_phys_disks;

	return rc;
}

int leapraid_cfg_get_volume_wwid(struct leapraid_adapter *adapter,
				 u16 vol_hdl, u64 *wwid)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_raidvol_p1 raidvol_p1;
	int rc;

	*wwid = 0;
	cfgp1.form = LEAPRAID_RAID_VOL_CFG_PGAD_HDL;
	cfgp2.handle = vol_hdl;
	rc = leapraid_op_config_page(adapter, &raidvol_p1, cfgp1,
				     cfgp2, GET_RAID_VOLUME_PG1);
	if (!rc)
		*wwid = le64_to_cpu(raidvol_p1.wwid);

	return rc;
}

static int leapraid_get_sas_io_unit_page0(
		struct leapraid_adapter *adapter,
		struct leapraid_sas_io_unit_p0 *sas_io_unit_p0,
		u16 sas_iou_pg0_sz)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};

	cfgp1.size = sas_iou_pg0_sz;
	return leapraid_op_config_page(adapter, sas_io_unit_p0, cfgp1,
				       cfgp2, GET_SAS_IOUNIT_PG0);
}

static int leapraid_get_sas_address(struct leapraid_adapter *adapter,
				    u16 hdl, u64 *sas_address)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_p0;

	*sas_address = 0;
	cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
	cfgp2.handle = hdl;
	if (leapraid_op_config_page(adapter, &sas_dev_p0, cfgp1,
				    cfgp2, GET_SAS_DEVICE_PG0))
		return -ENXIO;

	if (hdl <= adapter->dev_topo.card.phys_num &&
	    (!(le32_to_cpu(sas_dev_p0.dev_info) & LEAPRAID_DEVTYP_SEP)))
		*sas_address = adapter->dev_topo.card.sas_address;
	else
		*sas_address = le64_to_cpu(sas_dev_p0.sas_address);

	return 0;
}

int leapraid_get_volume_cap(struct leapraid_adapter *adapter,
			    struct leapraid_raid_volume *raid_volume)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_raidvol_p0 *raidvol_p0;
	struct leapraid_sas_dev_p0 sas_dev_p0;
	struct leapraid_raidpd_p0 raidpd_p0;
	u8 num_pds;
	u16 sz;
	int rc = 0;

	if (leapraid_cfg_get_number_pds(adapter, raid_volume->hdl,
					&num_pds) || !num_pds)
		return -EFAULT;

	raid_volume->pd_num = num_pds;
	sz = offsetof(struct leapraid_raidvol_p0, phys_disk) +
		(num_pds * sizeof(struct leapraid_raidvol0_phys_disk));
	raidvol_p0 = kzalloc(sz, GFP_KERNEL);
	if (!raidvol_p0)
		return -ENOMEM;

	cfgp1.size = sz;
	cfgp2.handle = raid_volume->hdl;
	if (leapraid_op_config_page(adapter, raidvol_p0, cfgp1, cfgp2,
				    GET_RAID_VOLUME_PG0)) {
		rc = -EFAULT;
		goto out_cleanup;
	}

	raid_volume->vol_type = raidvol_p0->volume_type;
	cfgp1.form = LEAPRAID_PHYSDISK_CFG_PGAD_PHYSDISKNUM;
	cfgp2.form_specific = raidvol_p0->phys_disk[0].phys_disk_num;
	if (!(leapraid_op_config_page(adapter, &raidpd_p0, cfgp1, cfgp2,
				      GET_PHY_DISK_PG0))) {
		cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
		cfgp2.handle = le16_to_cpu(raidpd_p0.dev_hdl);
		if (!(leapraid_op_config_page(adapter, &sas_dev_p0, cfgp1,
					      cfgp2, GET_SAS_DEVICE_PG0)))
			raid_volume->dev_info =
				le32_to_cpu(sas_dev_p0.dev_info);
	}

out_cleanup:
	kfree(raidvol_p0);
	return rc;
}

static bool leapraid_should_skip_poll_work(struct leapraid_adapter *adapter)
{
	unsigned long flags;
	bool skip;

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	skip = adapter->access_ctrl.shost_recovering ||
		adapter->access_ctrl.pcie_recovering ||
		adapter->access_ctrl.host_removing;
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);

	return skip;
}

static void leapraid_fw_log_work(struct work_struct *work)
{
	struct leapraid_adapter *adapter = container_of(work,
		struct leapraid_adapter, fw_log_desc.fw_log_work.work);
	struct leapraid_fw_log_info *infom;
	struct leapraid_reg_base __iomem *iomem_base;
	unsigned long flags;

	if (leapraid_should_skip_poll_work(adapter))
		goto scheduled_timer;

	infom = (struct leapraid_fw_log_info *)
		(adapter->fw_log_desc.fw_log_buffer +
		 LEAPRAID_SYS_LOG_BUF_SIZE);

	iomem_base = adapter->iomem_base;
	if (adapter->fw_log_desc.fw_log_init_flag == 0) {
		infom->user_position =
			leapraid_readl(&iomem_base->host_log_buf_pos);
		infom->adapter_position =
			leapraid_readl(&iomem_base->adapter_log_buf_pos);
		adapter->fw_log_desc.fw_log_init_flag++;
	}

	writel(infom->user_position, &iomem_base->host_log_buf_pos);
	infom->adapter_position =
		leapraid_readl(&iomem_base->adapter_log_buf_pos);

scheduled_timer:
	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	if (adapter->fw_log_desc.fw_log_wq)
		queue_delayed_work(
			adapter->fw_log_desc.fw_log_wq,
			&adapter->fw_log_desc.fw_log_work,
			msecs_to_jiffies(LEAPRAID_PCIE_LOG_POLLING_INTERVAL));
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
}

void leapraid_fw_log_stop(struct leapraid_adapter *adapter)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	if (!adapter->fw_log_desc.open_pcie_trace)
		return;

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	wq = adapter->fw_log_desc.fw_log_wq;
	adapter->fw_log_desc.fw_log_wq = NULL;
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
	if (wq) {
		if (!cancel_delayed_work_sync(&adapter->fw_log_desc
					      .fw_log_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

void leapraid_fw_log_start(struct leapraid_adapter *adapter)
{
	unsigned long flags;

	if (!adapter->fw_log_desc.open_pcie_trace)
		return;

	if (adapter->fw_log_desc.fw_log_wq)
		return;

	INIT_DELAYED_WORK(&adapter->fw_log_desc.fw_log_work,
			  leapraid_fw_log_work);
	snprintf(adapter->fw_log_desc.fw_log_wq_name,
		 sizeof(adapter->fw_log_desc.fw_log_wq_name),
		 "poll_%s%u_fw_log",
		 LEAPRAID_DRIVER_NAME, adapter->adapter_attr.id);
	adapter->fw_log_desc.fw_log_wq =
		create_singlethread_workqueue(
			adapter->fw_log_desc.fw_log_wq_name);
	if (!adapter->fw_log_desc.fw_log_wq)
		return;

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	if (adapter->fw_log_desc.fw_log_wq)
		queue_delayed_work(
			adapter->fw_log_desc.fw_log_wq,
			&adapter->fw_log_desc.fw_log_work,
			msecs_to_jiffies(LEAPRAID_PCIE_LOG_POLLING_INTERVAL));
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
}

static void leapraid_check_scheduled_fault_work(struct work_struct *work)
{
	struct leapraid_adapter *adapter;
	unsigned long flags;
	u32 adapter_state;
	int rc;

	adapter = container_of(work, struct leapraid_adapter,
			       reset_desc.fault_reset_work.work);

	if (leapraid_should_skip_poll_work(adapter))
		goto scheduled_timer;

	adapter_state = leapraid_get_adapter_state(adapter);
	if (adapter_state != LEAPRAID_DB_OPERATIONAL) {
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset 0x%x\n",
			 __func__, __LINE__, adapter_state);
		rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
		adapter_state = leapraid_get_adapter_state(adapter);
		if (rc && adapter_state != LEAPRAID_DB_OPERATIONAL) {
			dev_err(&adapter->pdev->dev,
				"%s: Hard reset failed, state=0x%x rc=%d\n",
				__func__, adapter_state, rc);
			return;
		}
	}

scheduled_timer:
	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	if (adapter->reset_desc.fault_reset_wq)
		queue_delayed_work(
			adapter->reset_desc.fault_reset_wq,
			&adapter->reset_desc.fault_reset_work,
			msecs_to_jiffies(LEAPRAID_FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
}

void leapraid_check_scheduled_fault_start(struct leapraid_adapter *adapter)
{
	unsigned long flags;

	if (adapter->reset_desc.fault_reset_wq)
		return;

	INIT_DELAYED_WORK(&adapter->reset_desc.fault_reset_work,
			  leapraid_check_scheduled_fault_work);
	snprintf(adapter->reset_desc.fault_reset_wq_name,
		 sizeof(adapter->reset_desc.fault_reset_wq_name),
		 "poll_%s%u_status",
		 LEAPRAID_DRIVER_NAME, adapter->adapter_attr.id);
	adapter->reset_desc.fault_reset_wq =
		create_singlethread_workqueue(
			adapter->reset_desc.fault_reset_wq_name);
	if (!adapter->reset_desc.fault_reset_wq) {
		dev_err(&adapter->pdev->dev,
			"Create single thread workqueue failed!\n");
		return;
	}

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	if (adapter->reset_desc.fault_reset_wq)
		queue_delayed_work(
			adapter->reset_desc.fault_reset_wq,
			&adapter->reset_desc.fault_reset_work,
			msecs_to_jiffies(LEAPRAID_FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
}

void leapraid_check_scheduled_fault_stop(struct leapraid_adapter *adapter)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	wq = adapter->reset_desc.fault_reset_wq;
	adapter->reset_desc.fault_reset_wq = NULL;
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);

	if (!wq)
		return;

	if (!cancel_delayed_work_sync(&adapter->reset_desc.fault_reset_work))
		flush_workqueue(wq);
	destroy_workqueue(wq);
}

static void leapraid_overheat_work(struct work_struct *work)
{
	struct leapraid_overheat_desc *desc;
	struct leapraid_adapter *adapter;
	struct workqueue_struct *wq;
	struct Scsi_Host *shost;
	struct pci_dev *pdev;
	unsigned long flags;

	desc = container_of(work, struct leapraid_overheat_desc,
			    fault_overheat_work);
	adapter = container_of(desc, struct leapraid_adapter, overheat_desc);
	pdev = adapter->pdev;
	shost = pci_get_drvdata(pdev);

	if (!shost) {
		dev_err(&pdev->dev,
			"Overheat processing failed: invalid host/adapter\n");
		atomic_set(&adapter->overheat_desc.thermal_alert, 0);
		wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		return;
	}

	if (adapter->access_ctrl.host_removing) {
		atomic_set(&adapter->overheat_desc.thermal_alert, 0);
		wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		return;
	}

	adapter->access_ctrl.host_removing = 1;
	adapter->access_ctrl.shost_recover_async = 0;
	adapter->scan_dev_desc.scan_start = 0;
	adapter->scan_dev_desc.wait_scan_dev_done = 0;
	adapter->scan_dev_desc.driver_loading = 0;
	wake_up(&adapter->access_ctrl.shost_recover_wq);
	wake_up(&adapter->scan_dev_desc.wait_driver_loading);

	leapraid_mask_int(adapter);

	leapraid_check_scheduled_fault_stop(adapter);
	leapraid_fw_log_stop(adapter);
	leapraid_mq_polling_pause(adapter);

	leapraid_clean_active_cmds(adapter);
	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	wq = adapter->fw_evt_s.fw_evt_thread;
	adapter->fw_evt_s.fw_evt_thread = NULL;
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	sas_remove_host(shost);
	leapraid_cleanup_lists(adapter);
	atomic_set(&adapter->overheat_desc.thermal_alert, 0);
	wake_up(&adapter->scan_dev_desc.wait_driver_loading);
	dev_err(&pdev->dev, "%s: Suspend adapter due to overheat\n", __func__);
}

static void leapraid_overheat_init(struct leapraid_adapter *adapter)
{
	if (adapter->overheat_desc.fault_overheat_wq)
		return;

	atomic_set(&adapter->overheat_desc.thermal_alert, 0);
	snprintf(adapter->overheat_desc.fault_overheat_wq_name,
		 sizeof(adapter->overheat_desc.fault_overheat_wq_name),
		 "driver_%s%u_overheat",
		 LEAPRAID_DRIVER_NAME,
		 adapter->adapter_attr.id);
	adapter->overheat_desc.fault_overheat_wq =
		create_singlethread_workqueue(
			adapter->overheat_desc.fault_overheat_wq_name);
	if (!adapter->overheat_desc.fault_overheat_wq) {
		dev_err(&adapter->pdev->dev,
			"Failed to create overheat workqueue\n");
		return;
	}

	INIT_WORK(&adapter->overheat_desc.fault_overheat_work,
		  leapraid_overheat_work);
}

void leapraid_overheat_cleanup(struct leapraid_adapter *adapter)
{
	struct workqueue_struct *wq;

	wq = xchg(&adapter->overheat_desc.fault_overheat_wq, NULL);
	if (!wq)
		return;

	cancel_work_sync(&adapter->overheat_desc.fault_overheat_work);
	destroy_workqueue(wq);
}

static void leapraid_fw_work(struct leapraid_adapter *adapter,
			     struct leapraid_fw_evt_work *fw_evt);

static void leapraid_fw_evt_free(struct kref *r)
{
	struct leapraid_fw_evt_work *fw_evt;

	fw_evt = container_of(r, struct leapraid_fw_evt_work, refcnt);

	kfree(fw_evt->evt_data);
	kfree(fw_evt);
}

static void leapraid_fw_evt_get(struct leapraid_fw_evt_work *fw_evt)
{
	kref_get(&fw_evt->refcnt);
}

static void leapraid_fw_evt_put(struct leapraid_fw_evt_work *fw_work)
{
	kref_put(&fw_work->refcnt, leapraid_fw_evt_free);
}

static struct leapraid_fw_evt_work *leapraid_alloc_fw_evt_work(void)
{
	struct leapraid_fw_evt_work *fw_evt =
		kzalloc(sizeof(*fw_evt), GFP_ATOMIC);

	if (fw_evt)
		kref_init(&fw_evt->refcnt);

	return fw_evt;
}

static void leapraid_run_fw_evt_work(struct work_struct *work)
{
	struct leapraid_fw_evt_work *fw_evt =
		container_of(work, struct leapraid_fw_evt_work, work);

	leapraid_fw_work(fw_evt->adapter, fw_evt);
}

static void leapraid_fw_evt_add(struct leapraid_adapter *adapter,
				struct leapraid_fw_evt_work *fw_evt)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering ||
	    !adapter->fw_evt_s.fw_evt_thread) {
		spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
		return;
	}

	leapraid_fw_evt_get(fw_evt);
	INIT_LIST_HEAD(&fw_evt->list);
	list_add_tail(&fw_evt->list, &adapter->fw_evt_s.fw_evt_list);
	INIT_WORK(&fw_evt->work, leapraid_run_fw_evt_work);
	leapraid_fw_evt_get(fw_evt);
	queue_work(adapter->fw_evt_s.fw_evt_thread, &fw_evt->work);
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
}

static void leapraid_del_fw_evt_from_list(struct leapraid_adapter *adapter,
					  struct leapraid_fw_evt_work *fw_evt)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (!list_empty(&fw_evt->list)) {
		list_del_init(&fw_evt->list);
		leapraid_fw_evt_put(fw_evt);
	}
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
}

static struct leapraid_fw_evt_work *leapraid_next_fw_evt(
		struct leapraid_adapter *adapter)
{
	struct leapraid_fw_evt_work *fw_evt = NULL;
	unsigned long flags;

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (!list_empty(&adapter->fw_evt_s.fw_evt_list)) {
		fw_evt = list_first_entry(&adapter->fw_evt_s.fw_evt_list,
					  struct leapraid_fw_evt_work, list);
		list_del_init(&fw_evt->list);
	}
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	return fw_evt;
}

void leapraid_clean_active_fw_evt(struct leapraid_adapter *adapter)
{
	struct leapraid_fw_evt_work *fw_evt;
	unsigned long flags;
	bool in_fw_evt_context;
	bool rc;

	if ((list_empty(&adapter->fw_evt_s.fw_evt_list) &&
	     !adapter->fw_evt_s.cur_evt) || !adapter->fw_evt_s.fw_evt_thread)
		return;

	adapter->fw_evt_s.fw_evt_cleanup = 1;
	if (adapter->access_ctrl.shost_recovering &&
	    adapter->fw_evt_s.cur_evt)
		adapter->fw_evt_s.cur_evt->ignore = 1;

	while ((fw_evt = leapraid_next_fw_evt(adapter))) {
		rc = cancel_work_sync(&fw_evt->work);
		if (rc)
			leapraid_fw_evt_put(fw_evt);
		leapraid_fw_evt_put(fw_evt);
	}

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	fw_evt = adapter->fw_evt_s.cur_evt;
	if (fw_evt) {
		in_fw_evt_context = adapter->fw_evt_s.cur_evt_task == current;
		leapraid_fw_evt_get(fw_evt);
	}
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (fw_evt) {
		if (!in_fw_evt_context)
			cancel_work_sync(&fw_evt->work);
		leapraid_fw_evt_put(fw_evt);
	}

	adapter->fw_evt_s.fw_evt_cleanup = 0;
}

static void leapraid_internal_dev_ublk(struct scsi_device *sdev,
				       struct leapraid_sdev_priv *sdev_priv)
{
	int rc;

	sdev_printk(KERN_WARNING, sdev,
		    "hdl 0x%04x: Now internal unblkg dev\n",
		    sdev_priv->starget_priv->hdl);
	sdev_priv->block = 0;
	rc = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
	if (rc == -EINVAL) {
		sdev_printk(KERN_WARNING, sdev,
			    "hdl 0x%04x: unblkg failed, rc=%d\n",
			    sdev_priv->starget_priv->hdl, rc);
		sdev_priv->block = 1;
		rc = scsi_internal_device_block_nowait(sdev);
		if (rc)
			sdev_printk(KERN_WARNING, sdev,
				    "hdl 0x%04x: Earlier ublkg err, rc=%d\n",
				    sdev_priv->starget_priv->hdl, rc);

		sdev_priv->block = 0;
		rc = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
		if (rc)
			sdev_printk(KERN_WARNING, sdev,
				    "hdl 0x%04x: ublkg failed again, rc=%d\n",
				    sdev_priv->starget_priv->hdl, rc);
	}
}

static void leapraid_ublk_io_dev(struct leapraid_adapter *adapter,
				 u64 sas_addr,
				 struct leapraid_card_port *card_port)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, adapter->shost) {
		sdev_priv = sdev->hostdata;
		if (!sdev_priv || !sdev_priv->starget_priv)
			continue;

		if (sdev_priv->starget_priv->sas_address != sas_addr)
			continue;

		if (sdev_priv->starget_priv->card_port != card_port)
			continue;

		if (sdev_priv->block)
			leapraid_internal_dev_ublk(sdev, sdev_priv);

		scsi_device_set_state(sdev, SDEV_OFFLINE);
	}
}

static void leapraid_ublk_io_all_dev(struct leapraid_adapter *adapter)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_starget_priv *stgt_priv;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, adapter->shost) {
		sdev_priv = sdev->hostdata;

		if (!sdev_priv)
			continue;

		stgt_priv = sdev_priv->starget_priv;
		if (!stgt_priv || stgt_priv->deleted)
			continue;

		if (!sdev_priv->block)
			continue;

		sdev_printk(KERN_WARNING, sdev, "hdl 0x%04x: blkg...\n",
			    sdev_priv->starget_priv->hdl);
		leapraid_internal_dev_ublk(sdev, sdev_priv);
		continue;
	}
}

static void leapraid_internal_dev_blk(
		struct scsi_device *sdev,
		struct leapraid_sdev_priv *sdev_priv)
{
	int rc;

	sdev_printk(KERN_INFO, sdev, "Internal blkg hdl 0x%04x\n",
		    sdev_priv->starget_priv->hdl);
	sdev_priv->block = 1;
	rc = scsi_internal_device_block_nowait(sdev);
	if (rc == -EINVAL)
		sdev_printk(KERN_WARNING, sdev,
			    "hdl 0x%04x: blkg failed, rc=%d\n",
			    rc, sdev_priv->starget_priv->hdl);
}

static void leapraid_imm_blkio_to_end_dev(struct leapraid_adapter *adapter,
					  struct leapraid_sas_port *sas_port)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_sas_dev *sas_dev;
	struct scsi_device *sdev;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(
		adapter,
		sas_port->remote_identify.sas_address,
		sas_port->card_port);

	if (sas_dev) {
		shost_for_each_device(sdev, adapter->shost) {
			sdev_priv = sdev->hostdata;
			if (!sdev_priv)
				continue;

			if (sdev_priv->starget_priv->hdl != sas_dev->hdl)
				continue;

			if (sdev_priv->block)
				continue;

			if (sas_dev && sas_dev->pend_sas_rphy_add)
				continue;

			if (sdev_priv->sep) {
				sdev_printk(KERN_INFO, sdev,
					    "skip dev blk: sep hdl 0x%04x\n",
					    sdev_priv->starget_priv->hdl);
				continue;
			}

			leapraid_internal_dev_blk(sdev, sdev_priv);
		}

		leapraid_sdev_put(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
}

static void leapraid_imm_blkio_set_end_dev_blk_hdls(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node_exp)
{
	struct leapraid_sas_port *sas_port;

	list_for_each_entry(sas_port,
			    &topo_node_exp->sas_port_list, port_list) {
		if (sas_port->remote_identify.device_type == SAS_END_DEVICE)
			leapraid_imm_blkio_to_end_dev(adapter, sas_port);
	}
}

static void leapraid_imm_blkio_to_kids_attchd_to_ex(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node_exp);

static void leapraid_imm_blkio_to_sib_exp(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node_exp)
{
	struct leapraid_topo_node *topo_node_exp_sib;
	struct leapraid_sas_port *sas_port;

	list_for_each_entry(sas_port,
			    &topo_node_exp->sas_port_list, port_list) {
		if (sas_port->remote_identify.device_type ==
			 SAS_EDGE_EXPANDER_DEVICE ||
		    sas_port->remote_identify.device_type ==
			 SAS_FANOUT_EXPANDER_DEVICE) {
			topo_node_exp_sib =
				leapraid_exp_find_by_sas_address(
					adapter,
					sas_port->remote_identify.sas_address,
					sas_port->card_port);
			leapraid_imm_blkio_to_kids_attchd_to_ex(
				adapter,
				topo_node_exp_sib);
		}
	}
}

static void leapraid_imm_blkio_to_kids_attchd_to_ex(
				struct leapraid_adapter *adapter,
				struct leapraid_topo_node *topo_node_exp)
{
	if (!topo_node_exp)
		return;

	leapraid_imm_blkio_set_end_dev_blk_hdls(adapter, topo_node_exp);

	leapraid_imm_blkio_to_sib_exp(adapter, topo_node_exp);
}

static void leapraid_report_sdev_directly(struct leapraid_adapter *adapter,
					  struct leapraid_sas_dev *sas_dev)
{
	struct leapraid_sas_port *sas_port;

	sas_port = leapraid_transport_port_add(adapter,
					       sas_dev->hdl,
					       sas_dev->parent_sas_addr,
					       sas_dev->card_port);
	if (!sas_port) {
		leapraid_sas_dev_remove(adapter, sas_dev);
		return;
	}

	if (!sas_dev->starget) {
		if (!adapter->scan_dev_desc.driver_loading) {
			leapraid_transport_port_remove(
				adapter,
				sas_dev->sas_addr,
				sas_dev->parent_sas_addr,
				sas_dev->card_port);
			leapraid_sas_dev_remove(adapter, sas_dev);
		}
		return;
	}
}

static struct leapraid_sas_dev *leapraid_init_sas_dev(
		struct leapraid_adapter *adapter,
		struct leapraid_sas_dev_p0 *sas_dev_pg0,
		struct leapraid_card_port *card_port, u16 hdl,
		u64 parent_sas_addr, u64 sas_addr, u32 dev_info)
{
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_enc_node *enc_dev;
	unsigned long flags;

	sas_dev = kzalloc_obj(*sas_dev);
	if (!sas_dev)
		return NULL;

	kref_init(&sas_dev->refcnt);
	sas_dev->hdl = hdl;
	sas_dev->dev_info = dev_info;
	sas_dev->sas_addr = sas_addr;
	sas_dev->card_port = card_port;
	sas_dev->parent_sas_addr = parent_sas_addr;
	sas_dev->phy = sas_dev_pg0->phy_num;
	sas_dev->enc_hdl = le16_to_cpu(sas_dev_pg0->enc_hdl);
	sas_dev->dev_name = le64_to_cpu(sas_dev_pg0->dev_name);
	sas_dev->port_connection = sas_dev_pg0->max_port_connections;
	sas_dev->slot = sas_dev->enc_hdl ? le16_to_cpu(sas_dev_pg0->slot) : 0;
	if (le16_to_cpu(sas_dev_pg0->flg) &
	    LEAPRAID_SAS_DEV_P0_FLG_ENC_LEVEL_VALID) {
		sas_dev->enc_level = sas_dev_pg0->enc_level;
		memcpy(sas_dev->connector_name,
		       sas_dev_pg0->connector_name,
		       LEAPRAID_SAS_DEV_P0_CON_NAME_LEN);
		sas_dev->connector_name[LEAPRAID_SAS_DEV_P0_CON_NAME_LEN] =
			'\0';
	} else {
		sas_dev->enc_level = 0;
		sas_dev->connector_name[0] = '\0';
	}
	if (sas_dev->enc_hdl) {
		spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
		enc_dev = leapraid_enc_find_by_hdl(adapter, sas_dev->enc_hdl);
		if (enc_dev)
			sas_dev->enc_lid = le64_to_cpu(enc_dev->pg0.enc_lid);
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);
	}
	dev_info(&adapter->pdev->dev,
		 "add dev: hdl=0x%x, SAS addr=0x%016llx, port connect=0x%x\n",
		 hdl, sas_dev->sas_addr, sas_dev->port_connection);

	return sas_dev;
}

static void leapraid_add_dev(struct leapraid_adapter *adapter, u16 hdl)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_pg0;
	struct leapraid_card_port *card_port;
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	u64 parent_sas_addr;
	u32 dev_info;
	u64 sas_addr;
	u8 port_id;

	cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
	cfgp2.handle = hdl;
	if (leapraid_op_config_page(adapter, &sas_dev_pg0,
				    cfgp1, cfgp2, GET_SAS_DEVICE_PG0))
		return;

	dev_info = le32_to_cpu(sas_dev_pg0.dev_info);
	if (!(leapraid_is_end_dev(dev_info)))
		return;

	sas_addr = le64_to_cpu(sas_dev_pg0.sas_address);
	if (!(le16_to_cpu(sas_dev_pg0.flg) &
	      LEAPRAID_SAS_DEV_P0_FLG_DEV_PRESENT))
		return;

	port_id = sas_dev_pg0.physical_port;
	card_port = leapraid_get_port_by_id(adapter, port_id, false);
	if (!card_port)
		return;

	sas_dev = leapraid_get_sas_dev_by_addr(adapter, sas_addr, card_port);
	if (sas_dev) {
		leapraid_sdev_put(sas_dev);
		return;
	}

	if (leapraid_get_sas_address(adapter,
				     le16_to_cpu(sas_dev_pg0.parent_dev_hdl),
				     &parent_sas_addr))
		return;

	sas_dev = leapraid_init_sas_dev(adapter, &sas_dev_pg0, card_port,
					hdl, parent_sas_addr, sas_addr,
					dev_info);
	if (!sas_dev)
		return;

	if (adapter->scan_dev_desc.wait_scan_dev_done) {
		spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
		leapraid_sdev_get(sas_dev);
		list_add_tail(&sas_dev->list,
			      &adapter->dev_topo.sas_dev_init_list);
		leapraid_check_boot_dev(adapter, sas_dev, 0);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	} else {
		spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
		leapraid_sdev_get(sas_dev);
		list_add_tail(&sas_dev->list, &adapter->dev_topo.sas_dev_list);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		leapraid_report_sdev_directly(adapter, sas_dev);
	}
	leapraid_sdev_put(sas_dev);
}

static void leapraid_remove_device(struct leapraid_adapter *adapter,
				   struct leapraid_sas_dev *sas_dev)
{
	struct leapraid_starget_priv *starget_priv;

	leapraid_clear_cached_boot_dev(adapter, sas_dev, 0);
	if (sas_dev->led_on) {
		leapraid_set_led(adapter, sas_dev, false);
		sas_dev->led_on = 0;
	}

	if (sas_dev->starget && sas_dev->starget->hostdata) {
		starget_priv = sas_dev->starget->hostdata;
		starget_priv->deleted = 1;
		leapraid_ublk_io_dev(adapter,
				     sas_dev->sas_addr, sas_dev->card_port);
		starget_priv->hdl = LEAPRAID_INVALID_DEV_HANDLE;
	}

	leapraid_transport_port_remove(adapter,
				       sas_dev->sas_addr,
				       sas_dev->parent_sas_addr,
				       sas_dev->card_port);

	dev_info(&adapter->pdev->dev,
		 "remove dev: hdl=0x%04x, SAS addr=0x%016llx\n",
		 sas_dev->hdl, (unsigned long long)sas_dev->sas_addr);
}

static struct leapraid_vphy *leapraid_alloc_vphy(
		struct leapraid_adapter *adapter,
		u8 port_id, u8 phy_num)
{
	struct leapraid_card_port *port;
	struct leapraid_vphy *vphy;

	port = leapraid_get_port_by_id(adapter, port_id, false);
	if (!port)
		return NULL;

	vphy = leapraid_get_vphy_by_phy(port, phy_num);
	if (vphy)
		return vphy;
	vphy = kzalloc_obj(*vphy);
	if (!vphy)
		return NULL;

	if (!port->vphys_mask)
		INIT_LIST_HEAD(&port->vphys_list);

	port->vphys_mask |= BIT(phy_num);
	vphy->phy_mask |= BIT(phy_num);
	list_add_tail(&vphy->list, &port->vphys_list);
	return vphy;
}

static int leapraid_add_port_to_card_port_list(
					struct leapraid_adapter *adapter,
					u8 port_id, bool refresh)
{
	struct leapraid_card_port *card_port;

	card_port = leapraid_get_port_by_id(adapter, port_id, false);
	if (card_port)
		return 0;
	card_port = kzalloc_obj(*card_port);
	if (!card_port)
		return -ENOMEM;

	card_port->port_id = port_id;
	dev_dbg(&adapter->pdev->dev,
		"port: %d is added to card_port list\n",
		card_port->port_id);

	if (refresh && adapter->access_ctrl.shost_recovering)
		card_port->flg = LEAPRAID_CARD_PORT_FLG_NEW;
	list_add_tail(&card_port->list, &adapter->dev_topo.card_port_list);
	return 0;
}

static int leapraid_add_card_phy(struct leapraid_adapter *adapter,
				 struct leapraid_sas_io_unit_p0 *iou,
				 int i)
{
	struct leapraid_sas_phy_p0 phy_pg0;
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_topo_node *card = &adapter->dev_topo.card;
	u8 port_id;

	cfgp1.phy_number = i;

	if (leapraid_op_config_page(adapter, &phy_pg0,
				    cfgp1, cfgp2, GET_PHY_PG0))
		return -EINVAL;

	port_id = iou->phy_info[i].port;

	if (leapraid_add_port_to_card_port_list(adapter, port_id, false))
		return -EINVAL;

	if ((le32_to_cpu(phy_pg0.phy_info) &
	     LEAPRAID_SAS_PHYINFO_VPHY) &&
	    ((phy_pg0.neg_link_rate >>
	      LEAPRAID_SAS_NEG_LINK_RATE_SHIFT) >=
	     LEAPRAID_SAS_NEG_LINK_RATE_1_5)) {
		if (!leapraid_alloc_vphy(adapter, port_id, i))
			return -ENOMEM;

		card->card_phy[i].vphy = 1;
	}

	card->card_phy[i].hdl = card->hdl;
	card->card_phy[i].phy_id = i;
	card->card_phy[i].card_port =
		leapraid_get_port_by_id(adapter, port_id, false);

	leapraid_transport_add_card_phy(adapter,
					&card->card_phy[i],
					&phy_pg0,
					card->parent_dev);

	return 0;
}

static int leapraid_refresh_card_phy(struct leapraid_adapter *adapter,
				     struct leapraid_sas_io_unit_p0 *iou,
				     int i)
{
	struct leapraid_topo_node *card = &adapter->dev_topo.card;
	struct leapraid_sas_phy_p0 phy_pg0;
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	u16 attached_hdl;
	u32 dev_info;
	u8 link_rate;
	u8 port_id;

	link_rate = iou->phy_info[i].neg_link_rate >>
		LEAPRAID_SAS_NEG_LINK_RATE_SHIFT;
	port_id = iou->phy_info[i].port;
	if (leapraid_add_port_to_card_port_list(adapter, port_id, true))
		return -EINVAL;

	dev_info = le32_to_cpu(iou->phy_info[i].controller_phy_dev_info);
	if (dev_info & LEAPRAID_DEVTYP_SEP &&
	    link_rate >= LEAPRAID_SAS_NEG_LINK_RATE_1_5) {
		cfgp1.phy_number = i;

		if (leapraid_op_config_page(adapter, &phy_pg0,
					    cfgp1, cfgp2, GET_PHY_PG0))
			return 0;

		if (le32_to_cpu(phy_pg0.phy_info) &
		    LEAPRAID_SAS_PHYINFO_VPHY) {
			if (!leapraid_alloc_vphy(adapter, port_id, i))
				return -ENOMEM;
			card->card_phy[i].vphy = 1;
		}
	}

	card->card_phy[i].hdl = card->hdl;

	attached_hdl = le16_to_cpu(iou->phy_info[i].attached_dev_hdl);
	if (attached_hdl && link_rate < LEAPRAID_SAS_NEG_LINK_RATE_1_5)
		link_rate = LEAPRAID_SAS_NEG_LINK_RATE_1_5;

	card->card_phy[i].card_port =
		leapraid_get_port_by_id(adapter, port_id, false);

	if (!card->card_phy[i].phy) {
		cfgp1.phy_number = i;
		if (leapraid_op_config_page(adapter, &phy_pg0,
					    cfgp1, cfgp2, GET_PHY_PG0))
			return 0;

		card->card_phy[i].phy_id = i;

		leapraid_transport_add_card_phy(adapter,
						&card->card_phy[i],
						&phy_pg0,
						card->parent_dev);
		return 0;
	}

	leapraid_transport_update_links(adapter,
					card->sas_address,
					attached_hdl,
					i,
					link_rate,
					card->card_phy[i].card_port);

	return 0;
}

static void leapraid_sas_host_add(struct leapraid_adapter *adapter,
				  bool refresh)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_pg0;
	struct leapraid_enc_p0 enc_pg0;
	struct leapraid_sas_io_unit_p0 *sas_iou_pg0;
	u16 sas_iou_pg0_sz;
	u8 phys_num;
	int i;
	int rc;

	if (!refresh) {
		if (leapraid_get_adapter_phys(adapter, &phys_num) || !phys_num)
			return;

		adapter->dev_topo.card.card_phy =
			kcalloc(phys_num,
				sizeof(struct leapraid_card_phy), GFP_KERNEL);
		if (!adapter->dev_topo.card.card_phy)
			return;

		adapter->dev_topo.card.phys_num = phys_num;
	}

	sas_iou_pg0_sz =
		offsetof(struct leapraid_sas_io_unit_p0, phy_info) +
			 (adapter->dev_topo.card.phys_num *
			  sizeof(struct leapraid_sas_io_unit0_phy_info));
	sas_iou_pg0 = kzalloc(sas_iou_pg0_sz, GFP_KERNEL);
	if (!sas_iou_pg0)
		return;

	if (leapraid_get_sas_io_unit_page0(adapter,
					   sas_iou_pg0,
					   sas_iou_pg0_sz))
		goto out_free;

	adapter->dev_topo.card.parent_dev = &adapter->shost->shost_gendev;
	adapter->dev_topo.card.hdl =
		le16_to_cpu(sas_iou_pg0->phy_info[0].controller_dev_hdl);
	for (i = 0; i < adapter->dev_topo.card.phys_num; i++) {
		if (!refresh) /* add */
			rc = leapraid_add_card_phy(adapter, sas_iou_pg0, i);
		else /* refresh */
			rc = leapraid_refresh_card_phy(adapter,
						       sas_iou_pg0,
						       i);
		if (rc)
			goto out_free;
	}

	if (!refresh) {
		cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
		cfgp2.handle = adapter->dev_topo.card.hdl;
		if (leapraid_op_config_page(adapter, &sas_dev_pg0, cfgp1,
					    cfgp2, GET_SAS_DEVICE_PG0))
			goto out_free;

		adapter->dev_topo.card.enc_hdl =
			le16_to_cpu(sas_dev_pg0.enc_hdl);
		adapter->dev_topo.card.sas_address =
			le64_to_cpu(sas_dev_pg0.sas_address);
		dev_info(&adapter->pdev->dev,
			 "add host: hdl=0x%04x, SAS addr=0x%016llx, phy=%d\n",
			 adapter->dev_topo.card.hdl,
			 (unsigned long long)adapter->dev_topo.card.sas_address,
			 adapter->dev_topo.card.phys_num);

		if (adapter->dev_topo.card.enc_hdl) {
			cfgp1.form = LEAPRAID_SAS_ENC_CFG_PGAD_HDL;
			cfgp2.handle = adapter->dev_topo.card.enc_hdl;
			if (!(leapraid_op_config_page(adapter, &enc_pg0,
						      cfgp1, cfgp2,
						      GET_SAS_ENCLOSURE_PG0)))
				adapter->dev_topo.card.enc_lid =
					le64_to_cpu(enc_pg0.enc_lid);
		}
	}
out_free:
	kfree(sas_iou_pg0);
}

static int leapraid_internal_exp_add(struct leapraid_adapter *adapter,
				     struct leapraid_exp_p0 *exp_pg0,
				     union cfg_param_1 *cfgp1,
				     union cfg_param_2 *cfgp2,
				     u16 hdl)
{
	struct leapraid_topo_node *topo_node_exp;
	struct leapraid_sas_port *sas_port = NULL;
	struct leapraid_enc_node *enc_dev;
	struct leapraid_exp_p1 exp_pg1;
	int ret;
	int rc;
	unsigned long flags;
	u8 port_id;
	u16 parent_handle;
	u64 sas_addr_parent;
	int i;

	port_id = exp_pg0->physical_port;
	parent_handle = le16_to_cpu(exp_pg0->parent_dev_hdl);

	rc = leapraid_get_sas_address(adapter,
				      parent_handle, &sas_addr_parent);
	if (rc)
		return rc;
	topo_node_exp = kzalloc_obj(*topo_node_exp);
	if (!topo_node_exp)
		return -ENOMEM;

	topo_node_exp->hdl = hdl;
	topo_node_exp->phys_num = exp_pg0->phy_num;
	topo_node_exp->sas_address_parent = sas_addr_parent;
	topo_node_exp->sas_address = le64_to_cpu(exp_pg0->sas_address);
	topo_node_exp->card_port =
		leapraid_get_port_by_id(adapter, port_id, false);
	if (!topo_node_exp->card_port) {
		rc = -EPERM;
		goto out_fail;
	}

	dev_info(&adapter->pdev->dev,
		 "add exp: saddr=0x%016llx, hdl=0x%04x, phdl=0x%04x, phy=%d\n",
		 (unsigned long long)topo_node_exp->sas_address,
		 hdl, parent_handle,
		 topo_node_exp->phys_num);
	if (!topo_node_exp->phys_num) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid PHY num from exp_pg0\n", __func__);
		rc = -EPERM;
		goto out_fail;
	}

	topo_node_exp->card_phy =
		kcalloc(topo_node_exp->phys_num,
			sizeof(struct leapraid_card_phy), GFP_KERNEL);
	if (!topo_node_exp->card_phy) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to alloc expander phy array, count=%u\n",
			__func__, topo_node_exp->phys_num);
		rc = -EPERM;
		goto out_fail;
	}

	INIT_LIST_HEAD(&topo_node_exp->sas_port_list);
	sas_port = leapraid_transport_port_add(adapter, hdl, sas_addr_parent,
					       topo_node_exp->card_port);
	if (!sas_port) {
		rc = -EPERM;
		goto out_fail;
	}

	topo_node_exp->parent_dev = &sas_port->rphy->dev;
	topo_node_exp->rphy = sas_port->rphy;
	for (i = 0; i < topo_node_exp->phys_num; i++) {
		cfgp1->phy_number = i;
		cfgp2->handle = hdl;
		if (leapraid_op_config_page(adapter, &exp_pg1, *cfgp1, *cfgp2,
					    GET_SAS_EXPANDER_PG1)) {
			dev_err(&adapter->pdev->dev,
				"%s: Failed to get exp_pg1, phy=%d\n",
				__func__, i);
			rc = -EPERM;
			goto out_fail;
		}

		topo_node_exp->card_phy[i].hdl = hdl;
		topo_node_exp->card_phy[i].phy_id = i;
		topo_node_exp->card_phy[i].card_port =
			leapraid_get_port_by_id(adapter, port_id, false);
		ret = leapraid_transport_add_exp_phy(
				adapter,
				&topo_node_exp->card_phy[i],
				&exp_pg1,
				topo_node_exp->parent_dev);
		if (ret) {
			rc = -EPERM;
			goto out_fail;
		}
	}

	if (topo_node_exp->enc_hdl) {
		spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
		enc_dev = leapraid_enc_find_by_hdl(adapter,
						   topo_node_exp->enc_hdl);
		if (enc_dev)
			topo_node_exp->enc_lid =
				le64_to_cpu(enc_dev->pg0.enc_lid);
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);
	}

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_add_tail(&topo_node_exp->list, &adapter->dev_topo.exp_list);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);
	return 0;

out_fail:
	if (sas_port)
		leapraid_transport_port_remove(adapter,
					       topo_node_exp->sas_address,
					       sas_addr_parent,
					       topo_node_exp->card_port);
	kfree(topo_node_exp->card_phy);
	kfree(topo_node_exp);
	return rc;
}

static int leapraid_exp_add(struct leapraid_adapter *adapter, u16 hdl)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_topo_node *topo_node_exp;
	struct leapraid_exp_p0 exp_pg0;
	u16 parent_handle;
	u64 sas_addr, sas_addr_parent;
	unsigned long flags;
	u8 port_id;
	int rc;

	if (!hdl) {
		dev_warn(&adapter->pdev->dev, "%s: Invalid hdl\n", __func__);
		return -EPERM;
	}

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.pcie_recovering) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed, shost_recovering=%d pcie_recovering=%d\n",
			 __func__, adapter->access_ctrl.shost_recovering,
			adapter->access_ctrl.pcie_recovering);
		return -EPERM;
	}

	cfgp1.form = LEAPRAID_SAS_EXP_CFD_PGAD_HDL;
	cfgp2.handle = hdl;
	if (leapraid_op_config_page(adapter, &exp_pg0, cfgp1, cfgp2,
				    GET_SAS_EXPANDER_PG0))
		return -EPERM;

	parent_handle = le16_to_cpu(exp_pg0.parent_dev_hdl);
	if (leapraid_get_sas_address(adapter, parent_handle, &sas_addr_parent))
		return -EPERM;

	port_id = exp_pg0.physical_port;
	if (sas_addr_parent != adapter->dev_topo.card.sas_address) {
		spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
		topo_node_exp =
			leapraid_exp_find_by_sas_address(
				adapter,
				sas_addr_parent,
				leapraid_get_port_by_id(adapter,
							port_id,
							false));
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);
		if (!topo_node_exp) {
			rc = leapraid_exp_add(adapter, parent_handle);
			if (rc != 0)
				return rc;
		}
	}

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	sas_addr = le64_to_cpu(exp_pg0.sas_address);
	topo_node_exp =
		leapraid_exp_find_by_sas_address(
			adapter,
			sas_addr,
			leapraid_get_port_by_id(adapter, port_id, false));
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	if (topo_node_exp)
		return 0;

	return leapraid_internal_exp_add(adapter, &exp_pg0, &cfgp1,
					 &cfgp2, hdl);
}

static void leapraid_exp_node_rm(struct leapraid_adapter *adapter,
				 struct leapraid_topo_node *topo_node_exp)
{
	struct leapraid_sas_port *sas_port, *sas_port_next;
	unsigned long flags;
	int port_id;

	list_for_each_entry_safe(sas_port, sas_port_next,
				 &topo_node_exp->sas_port_list,
				 port_list) {
		if (adapter->access_ctrl.shost_recovering)
			return;

		switch (sas_port->remote_identify.device_type) {
		case SAS_END_DEVICE:
			leapraid_sas_dev_remove_by_sas_address(
				adapter,
				sas_port->remote_identify.sas_address,
				sas_port->card_port);
			break;
		case SAS_EDGE_EXPANDER_DEVICE:
		case SAS_FANOUT_EXPANDER_DEVICE:
			leapraid_exp_rm(
				adapter,
				sas_port->remote_identify.sas_address,
				sas_port->card_port);
			break;
		default:
			break;
		}
	}

	port_id = topo_node_exp->card_port->port_id;
	leapraid_transport_port_remove(adapter, topo_node_exp->sas_address,
				       topo_node_exp->sas_address_parent,
				       topo_node_exp->card_port);
	dev_info(&adapter->pdev->dev,
		 "removing exp: port=%d, SAS addr=0x%016llx, hdl=0x%04x\n",
		 port_id, (unsigned long long)topo_node_exp->sas_address,
		 topo_node_exp->hdl);
	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_del(&topo_node_exp->list);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);
	kfree(topo_node_exp->card_phy);
	kfree(topo_node_exp);
}

void leapraid_exp_rm(struct leapraid_adapter *adapter, u64 sas_addr,
		     struct leapraid_card_port *port)
{
	struct leapraid_topo_node *topo_node_exp;
	unsigned long flags;

	if (adapter->access_ctrl.shost_recovering)
		return;

	if (!port)
		return;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	topo_node_exp = leapraid_exp_find_by_sas_address(adapter,
							 sas_addr,
							 port);
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	if (topo_node_exp)
		leapraid_exp_node_rm(adapter, topo_node_exp);
}

static void leapraid_internal_sas_topo_chg_evt(
		struct leapraid_adapter *adapter,
		struct leapraid_card_port *card_port,
		struct leapraid_topo_node *topo_node_exp,
		struct leapraid_fw_evt_work *fw_evt,
		u64 sas_addr, u8 max_phys)
{
	struct leapraid_evt_data_sas_topo_change_list *evt_data;
	u8 phy_number;
	u8 link_rate;
	u16 reason_code;
	u16 hdl;
	int i;

	evt_data = fw_evt->evt_data;
	for (i = 0; i < evt_data->entry_num; i++) {
		if (fw_evt->ignore)
			return;

		if (adapter->access_ctrl.host_removing ||
		    adapter->access_ctrl.pcie_recovering)
			return;

		phy_number = evt_data->start_phy_num + i;
		if (phy_number >= max_phys)
			continue;

		reason_code = evt_data->phy[i].phy_status &
			LEAPRAID_EVT_SAS_TOPO_RC_MASK;

		hdl = le16_to_cpu(evt_data->phy[i].attached_dev_hdl);
		if (!hdl ||
		    hdl > adapter->adapter_attr.features.max_dev_handle) {
			dev_warn(&adapter->pdev->dev,
				 "%s: Invalid device handle\n", __func__);
			continue;
		}
		link_rate = evt_data->phy[i].link_rate >>
			LEAPRAID_SAS_NEG_LINK_RATE_SHIFT;
		switch (reason_code) {
		case LEAPRAID_EVT_SAS_TOPO_RC_TARG_ADDED:
			if (adapter->access_ctrl.shost_recovering)
				break;
			leapraid_transport_update_links(adapter, sas_addr,
							hdl, phy_number,
							link_rate, card_port);
			if (link_rate < LEAPRAID_SAS_NEG_LINK_RATE_1_5)
				break;
			leapraid_add_dev(adapter, hdl);
			break;
		case LEAPRAID_EVT_SAS_TOPO_RC_TARG_NOT_RESPONDING:
			leapraid_sas_dev_remove_by_hdl(adapter, hdl);
			break;
		}
	}

	if (evt_data->exp_status == LEAPRAID_EVT_SAS_TOPO_ES_NOT_RESPONDING &&
	    topo_node_exp)
		leapraid_exp_rm(adapter, sas_addr, card_port);
}

static void leapraid_sas_topo_chg_evt(struct leapraid_adapter *adapter,
				      struct leapraid_fw_evt_work *fw_evt)
{
	struct leapraid_topo_node *topo_node_exp;
	struct leapraid_card_port *card_port;
	struct leapraid_evt_data_sas_topo_change_list *evt_data;
	u16 phdl;
	u8 max_phys;
	u64 sas_addr;
	unsigned long flags;

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering)
		return;

	evt_data = fw_evt->evt_data;
	leapraid_sas_host_add(adapter, adapter->dev_topo.card.phys_num > 0);

	if (fw_evt->ignore)
		return;

	phdl = le16_to_cpu(evt_data->exp_dev_hdl);
	card_port = leapraid_get_port_by_id(adapter,
					    evt_data->physical_port,
					    false);
	if (evt_data->exp_status == LEAPRAID_EVT_SAS_TOPO_ES_ADDED &&
	    leapraid_exp_add(adapter, phdl) != 0)
		return;

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	topo_node_exp = leapraid_exp_find_by_hdl(adapter, phdl);
	if (topo_node_exp) {
		sas_addr = topo_node_exp->sas_address;
		max_phys = topo_node_exp->phys_num;
		card_port = topo_node_exp->card_port;
	} else if (phdl < adapter->dev_topo.card.phys_num) {
		sas_addr = adapter->dev_topo.card.sas_address;
		max_phys = adapter->dev_topo.card.phys_num;
	} else {
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);
		return;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	leapraid_internal_sas_topo_chg_evt(adapter, card_port,
					   topo_node_exp, fw_evt,
					   sas_addr, max_phys);
}

static void leapraid_reprobe_lun(struct scsi_device *sdev, void *no_uld_attach)
{
	sdev->no_uld_attach = no_uld_attach ? 1 : 0;
	sdev_printk(KERN_INFO, sdev,
		    "%s RAID component to upper layer\n",
		    sdev->no_uld_attach ? "hide" : "expose");
	WARN_ON(scsi_device_reprobe(sdev));
}

static void leapraid_sas_pd_add(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_p0;
	struct leapraid_sas_dev *sas_dev;
	u64 sas_address;
	u16 parent_hdl;
	u16 hdl;

	hdl = le16_to_cpu(evt_data->phys_disk_dev_hdl);
	if (!hdl || hdl > adapter->adapter_attr.features.max_dev_handle) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid device handle\n", __func__);
		return;
	}

	set_bit(hdl, adapter->dev_topo.pd_hdls);
	sas_dev = leapraid_get_sas_dev_by_hdl(adapter, hdl);
	if (sas_dev) {
		leapraid_sdev_put(sas_dev);
		dev_warn(&adapter->pdev->dev,
			 "Dev handle 0x%x already exists\n", hdl);
		return;
	}

	cfgp1.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
	cfgp2.handle = hdl;
	if (leapraid_op_config_page(adapter, &sas_dev_p0, cfgp1, cfgp2,
				    GET_SAS_DEVICE_PG0)) {
		dev_warn(&adapter->pdev->dev, "Failed to read dev page0\n");
		return;
	}

	parent_hdl = le16_to_cpu(sas_dev_p0.parent_dev_hdl);
	if (!leapraid_get_sas_address(adapter, parent_hdl, &sas_address))
		leapraid_transport_update_links(adapter, sas_address, hdl,
						sas_dev_p0.phy_num,
						LEAPRAID_SAS_NEG_LINK_RATE_1_5,
			 leapraid_get_port_by_id(adapter,
						 sas_dev_p0.physical_port,
						 false));
	leapraid_add_dev(adapter, hdl);
}

static void leapraid_sas_pd_delete(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	u16 hdl;

	hdl = le16_to_cpu(evt_data->phys_disk_dev_hdl);
	leapraid_sas_dev_remove_by_hdl(adapter, hdl);
}

static void leapraid_sas_pd_hide(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	struct leapraid_starget_priv *starget_priv;
	struct scsi_target *starget = NULL;
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	u64 volume_wwid = 0;
	u16 volume_hdl;
	u16 hdl;

	hdl = le16_to_cpu(evt_data->phys_disk_dev_hdl);
	if (!hdl || hdl > adapter->adapter_attr.features.max_dev_handle) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid device handle\n", __func__);
		return;
	}
	leapraid_cfg_get_volume_hdl(adapter, hdl, &volume_hdl);
	if (volume_hdl)
		leapraid_cfg_get_volume_wwid(adapter,
					     volume_hdl,
					     &volume_wwid);

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (!sas_dev) {
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		return;
	}

	set_bit(hdl, adapter->dev_topo.pd_hdls);
	if (sas_dev->starget && sas_dev->starget->hostdata) {
		starget = sas_dev->starget;
		starget_priv = starget->hostdata;
		starget_priv->flg |= LEAPRAID_TGT_FLG_RAID_MEMBER;
		sas_dev->volume_hdl = volume_hdl;
		sas_dev->volume_wwid = volume_wwid;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	if (starget) {
		starget_for_each_device(starget,
					(void *)LEAPRAID_NO_ULD_ATTACH_FLAG,
					leapraid_reprobe_lun);
	}

	leapraid_sdev_put(sas_dev);
}

static void leapraid_sas_pd_expose(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	struct leapraid_starget_priv *starget_priv;
	struct scsi_target *starget = NULL;
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;
	u16 hdl;

	hdl = le16_to_cpu(evt_data->phys_disk_dev_hdl);
	if (!hdl || hdl > adapter->adapter_attr.features.max_dev_handle) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid device handle\n", __func__);
		return;
	}

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (!sas_dev) {
		dev_warn(&adapter->pdev->dev,
			 "%s:%d: sas_dev not found, hdl=0x%x\n",
			 __func__, __LINE__, hdl);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		return;
	}

	sas_dev->volume_hdl = 0;
	sas_dev->volume_wwid = 0;
	clear_bit(hdl, adapter->dev_topo.pd_hdls);
	if (sas_dev->starget && sas_dev->starget->hostdata) {
		starget = sas_dev->starget;
		starget_priv = starget->hostdata;
		starget_priv->flg &= ~LEAPRAID_TGT_FLG_RAID_MEMBER;
		sas_dev->led_on = 0;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	if (starget) {
		starget_for_each_device(starget,
					NULL,
					leapraid_reprobe_lun);
	}

	leapraid_sdev_put(sas_dev);
}

static void leapraid_sas_vol_visibility(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	struct leapraid_raid_volume *raid_volume;
	struct scsi_device *sdev;
	bool reprobe_flg = false;
	bool sdev_held = false;
	unsigned long flags;
	u16 hdl;
	u8 rc;

	hdl = le16_to_cpu(evt_data->vol_dev_hdl);
	rc = evt_data->reason_code;
	raid_volume = leapraid_raid_volume_find_by_hdl(adapter, hdl);
	if (!raid_volume) {
		dev_warn(&adapter->pdev->dev,
			 "%s:%d: Volume handle 0x%x not found\n",
			 __func__, __LINE__, hdl);
		return;
	}

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	sdev = raid_volume->sdev;
	if (!sdev) {
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
		leapraid_raid_volume_put(raid_volume);
		dev_warn(&adapter->pdev->dev,
			 "%s:%d: Volume handle 0x%x has no sdev\n",
			 __func__, __LINE__, hdl);
		return;
	}

	if (sdev->no_uld_attach &&
	    rc == LEAPRAID_EVT_IR_RC_VOLUME_UNHIDE) {
		sdev->no_uld_attach = 0;
		reprobe_flg = true;
	} else if (!sdev->no_uld_attach &&
		   rc == LEAPRAID_EVT_IR_RC_VOLUME_HIDE) {
		sdev->no_uld_attach = 1;
		reprobe_flg = true;
	}

	if (reprobe_flg && !scsi_device_get(sdev))
		sdev_held = true;
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);
	leapraid_raid_volume_put(raid_volume);

	if (!reprobe_flg) {
		dev_warn(&adapter->pdev->dev,
			 "%s:rc(0x%x): Request matches, skipping\n",
			 __func__, rc);
		return;
	}

	if (!sdev_held) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed to hold sdev for reprobe, hdl=0x%x\n",
			 __func__, hdl);
		return;
	}

	if (sdev->no_uld_attach)
		sdev_printk(KERN_INFO, sdev, "hide vol\n");
	else
		sdev_printk(KERN_INFO, sdev, "unhide vol\n");

	WARN_ON(scsi_device_reprobe(sdev));
	scsi_device_put(sdev);
}

static void leapraid_sas_volume_add(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	struct leapraid_raid_volume *raid_volume;
	unsigned long flags;
	u64 wwid;
	u16 hdl;

	hdl = le16_to_cpu(evt_data->vol_dev_hdl);

	if (leapraid_cfg_get_volume_wwid(adapter, hdl, &wwid)) {
		dev_warn(&adapter->pdev->dev, "Failed to read volume page1\n");
		return;
	}

	if (!wwid) {
		dev_warn(&adapter->pdev->dev, "Invalid WWID(handle=0x%x)\n",
			 hdl);
		return;
	}

	raid_volume = leapraid_raid_volume_find_by_wwid(adapter, wwid);
	if (raid_volume) {
		dev_warn(&adapter->pdev->dev,
			 "Volume handle 0x%x already exists\n", hdl);
		leapraid_raid_volume_put(raid_volume);
		return;
	}

	raid_volume = kzalloc(sizeof(*raid_volume), GFP_KERNEL);
	if (!raid_volume)
		return;

	INIT_LIST_HEAD(&raid_volume->list);
	kref_init(&raid_volume->refcnt);
	raid_volume->id = adapter->dev_topo.sas_id++;
	raid_volume->channel = RAID_CHANNEL;
	raid_volume->hdl = hdl;
	raid_volume->wwid = wwid;
	leapraid_raid_volume_add(adapter, raid_volume);
	if (!adapter->scan_dev_desc.wait_scan_dev_done) {
		if (scsi_add_device(adapter->shost, RAID_CHANNEL,
				    raid_volume->id, 0))
			leapraid_raid_volume_remove(adapter, raid_volume);
		dev_info(&adapter->pdev->dev,
			 "add RAID volume: hdl=0x%x, wwid=0x%llx\n", hdl, wwid);
	} else {
		spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
		leapraid_check_boot_dev(adapter, raid_volume, RAID_CHANNEL);
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
	}
	leapraid_raid_volume_put(raid_volume);
}

static void leapraid_sas_volume_delete_by_ptr(
		struct leapraid_adapter *adapter,
		struct leapraid_raid_volume *raid_volume)
{
	struct leapraid_starget_priv *starget_priv;
	struct scsi_target *starget = NULL;
	unsigned long flags;
	bool in_list;

	if (!raid_volume)
		return;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	in_list = !list_empty(&raid_volume->list);
	if (in_list && raid_volume->starget) {
		starget = raid_volume->starget;
		starget_priv = starget->hostdata;
		if (starget_priv)
			starget_priv->deleted = 1;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	if (!in_list)
		return;

	dev_info(&adapter->pdev->dev,
		 "delete RAID volume: hdl=0x%x, wwid=0x%llx\n",
		 raid_volume->hdl, raid_volume->wwid);
	leapraid_raid_volume_remove(adapter, raid_volume);

	if (starget)
		scsi_remove_target(&starget->dev);
}

static void leapraid_sas_volume_delete(struct leapraid_adapter *adapter,
				       u16 hdl)
{
	struct leapraid_raid_volume *raid_volume;

	raid_volume = leapraid_raid_volume_find_by_hdl(adapter, hdl);
	if (!raid_volume) {
		dev_warn(&adapter->pdev->dev,
			 "%s:%d: Volume handle 0x%x not found\n",
			 __func__, __LINE__, hdl);
		return;
	}

	leapraid_sas_volume_delete_by_ptr(adapter, raid_volume);
	leapraid_raid_volume_put(raid_volume);
}

static void leapraid_sas_ir_chg_evt(struct leapraid_adapter *adapter,
				    struct leapraid_fw_evt_work *fw_evt)
{
	struct leapraid_evt_data_ir_change *evt_data;

	evt_data = fw_evt->evt_data;

	switch (evt_data->reason_code) {
	case LEAPRAID_EVT_IR_RC_VOLUME_ADD:
		leapraid_sas_volume_add(adapter, evt_data);
		break;
	case LEAPRAID_EVT_IR_RC_VOLUME_DELETE:
		leapraid_sas_volume_delete(adapter,
					   le16_to_cpu(evt_data->vol_dev_hdl));
		break;
	case LEAPRAID_EVT_IR_RC_PD_HIDDEN_TO_ADD:
		leapraid_sas_pd_add(adapter, evt_data);
		break;
	case LEAPRAID_EVT_IR_RC_PD_UNHIDDEN_TO_DELETE:
		leapraid_sas_pd_delete(adapter, evt_data);
		break;
	case LEAPRAID_EVT_IR_RC_PD_CREATED_TO_HIDE:
		leapraid_sas_pd_hide(adapter, evt_data);
		break;
	case LEAPRAID_EVT_IR_RC_PD_DELETED_TO_EXPOSE:
		leapraid_sas_pd_expose(adapter, evt_data);
		break;
	case LEAPRAID_EVT_IR_RC_VOLUME_HIDE:
	case LEAPRAID_EVT_IR_RC_VOLUME_UNHIDE:
		leapraid_sas_vol_visibility(adapter, evt_data);
		break;
	default:
		break;
	}
}

static void leapraid_sas_enc_dev_stat_add_node(
		struct leapraid_adapter *adapter, u16 hdl)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_enc_node *enc_node;
	struct leapraid_enc_node *enc_exist;
	unsigned long flags;
	int rc;

	enc_node = kzalloc_obj(*enc_node);
	if (!enc_node)
		return;

	cfgp1.form = LEAPRAID_SAS_ENC_CFG_PGAD_HDL;
	cfgp2.handle = hdl;
	rc = leapraid_op_config_page(adapter, &enc_node->pg0, cfgp1, cfgp2,
				     GET_SAS_ENCLOSURE_PG0);
	if (rc) {
		kfree(enc_node);
		return;
	}

	spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
	enc_exist = leapraid_enc_find_by_hdl(adapter, hdl);
	if (enc_exist) {
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);
		kfree(enc_node);
		return;
	}
	list_add_tail(&enc_node->list, &adapter->dev_topo.enc_list);
	spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);
}

static void leapraid_sas_enc_dev_stat_del_node(
		struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_enc_node *enc_node;
	unsigned long flags;

	if (!hdl)
		return;

	spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
	enc_node = leapraid_enc_find_by_hdl(adapter, hdl);
	if (enc_node)
		list_del(&enc_node->list);
	spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);

	kfree(enc_node);
}

static void leapraid_sas_enc_dev_stat_chg_evt(
		struct leapraid_adapter *adapter,
		struct leapraid_fw_evt_work *fw_evt)
{
	struct leapraid_evt_data_sas_enc_dev_status_change *evt_data;
	u16 enc_hdl;

	if (adapter->access_ctrl.shost_recovering)
		return;

	evt_data = fw_evt->evt_data;
	enc_hdl = le16_to_cpu(evt_data->enc_hdl);
	switch (evt_data->reason_code) {
	case LEAPRAID_EVT_SAS_ENCL_RC_ADDED:
		if (enc_hdl)
			leapraid_sas_enc_dev_stat_add_node(adapter, enc_hdl);
		break;
	case LEAPRAID_EVT_SAS_ENCL_RC_NOT_RESPONDING:
		leapraid_sas_enc_dev_stat_del_node(adapter, enc_hdl);
		break;
	default:
		break;
	}
}

static void leapraid_remove_unresp_sas_end_dev(
		struct leapraid_adapter *adapter)
{
	struct leapraid_sas_dev *sas_dev, *sas_dev_next;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	list_for_each_entry_safe(sas_dev, sas_dev_next,
				 &adapter->dev_topo.sas_dev_init_list, list) {
		if (sas_dev->rphy || sas_dev->pend_sas_rphy_add)
			continue;

		list_del_init(&sas_dev->list);
		leapraid_clear_cached_boot_dev(adapter, sas_dev, 0);
		leapraid_sdev_put(sas_dev);
	}
	list_for_each_entry_safe(sas_dev, sas_dev_next,
				 &adapter->dev_topo.sas_dev_list, list) {
		if (!sas_dev->resp)
			list_move_tail(&sas_dev->list, &head);
		else
			sas_dev->resp = 0;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	list_for_each_entry_safe(sas_dev, sas_dev_next, &head, list) {
		leapraid_remove_device(adapter, sas_dev);
		list_del_init(&sas_dev->list);
		leapraid_sdev_put(sas_dev);
	}

	dev_warn(&adapter->pdev->dev,
		 "Unresponsive SAS end devices removed\n");
}

static void leapraid_remove_unresp_raid_volumes(
		struct leapraid_adapter *adapter)
{
	unsigned long flags;
	struct leapraid_raid_volume *raid_volume, *raid_volume_next;
	LIST_HEAD(head);

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	list_for_each_entry_safe(raid_volume, raid_volume_next,
				 &adapter->dev_topo.raid_volume_list, list) {
		if (!raid_volume->resp)
			list_move_tail(&raid_volume->list, &head);
		else
			raid_volume->resp = 0;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	list_for_each_entry_safe(raid_volume, raid_volume_next, &head, list) {
		leapraid_sas_volume_delete_by_ptr(adapter, raid_volume);
	}

	dev_warn(&adapter->pdev->dev,
		 "Unresponsive RAID volumes removed\n");
}

static void leapraid_remove_unresp_sas_exp(struct leapraid_adapter *adapter)
{
	struct leapraid_topo_node *topo_node_exp, *topo_node_exp_next;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_for_each_entry_safe(topo_node_exp, topo_node_exp_next,
				 &adapter->dev_topo.exp_list, list) {
		if (!topo_node_exp->resp)
			list_move_tail(&topo_node_exp->list, &head);
		else
			topo_node_exp->resp = 0;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);

	list_for_each_entry_safe(topo_node_exp, topo_node_exp_next,
				 &head, list)
		leapraid_exp_node_rm(adapter, topo_node_exp);

	dev_warn(&adapter->pdev->dev,
		 "Unresponsive SAS expanders removed\n");
}

static void leapraid_remove_unresp_dev(struct leapraid_adapter *adapter)
{
	leapraid_remove_unresp_sas_end_dev(adapter);
	if (adapter->adapter_attr.raid_support)
		leapraid_remove_unresp_raid_volumes(adapter);
	leapraid_remove_unresp_sas_exp(adapter);
	leapraid_ublk_io_all_dev(adapter);
}

static void leapraid_del_dirty_vphy(struct leapraid_adapter *adapter)
{
	struct leapraid_card_port *card_port, *card_port_next;
	struct leapraid_vphy *vphy, *vphy_next;

	list_for_each_entry_safe(card_port, card_port_next,
				 &adapter->dev_topo.card_port_list, list) {
		if (!card_port->vphys_mask)
			continue;

		list_for_each_entry_safe(vphy, vphy_next,
					 &card_port->vphys_list, list) {
			if (!(vphy->flg & LEAPRAID_VPHY_FLG_DIRTY))
				continue;

			card_port->vphys_mask &= ~vphy->phy_mask;
			list_del(&vphy->list);
			kfree(vphy);
		}

		if (!card_port->vphys_mask && !card_port->sas_address)
			card_port->flg |= LEAPRAID_CARD_PORT_FLG_DIRTY;
	}
}

static void leapraid_del_dirty_card_port(struct leapraid_adapter *adapter)
{
	struct leapraid_card_port *card_port, *card_port_next;

	list_for_each_entry_safe(card_port, card_port_next,
				 &adapter->dev_topo.card_port_list, list) {
		if (!(card_port->flg & LEAPRAID_CARD_PORT_FLG_DIRTY) ||
		    card_port->flg & LEAPRAID_CARD_PORT_FLG_NEW)
			continue;

		list_del(&card_port->list);
		kfree(card_port);
	}
}

static void leapraid_update_dev_qdepth(struct leapraid_adapter *adapter)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_adapter_attr *attr;
	struct scsi_device *sdev;
	u16 qdepth;

	attr = &adapter->adapter_attr;
	shost_for_each_device(sdev, adapter->shost) {
		sdev_priv = sdev->hostdata;
		if (!sdev_priv || !sdev_priv->starget_priv)
			continue;
		sas_dev = sdev_priv->starget_priv->sas_dev;
		if (sas_dev && sas_dev->dev_info & LEAPRAID_DEVTYP_SSP_TGT)
			qdepth = (sas_dev->port_connection > 1) ?
				attr->wideport_max_queue_depth :
				attr->narrowport_max_queue_depth;
		else if (sas_dev && sas_dev->dev_info &
			 LEAPRAID_DEVTYP_SATA_DEV)
			qdepth = attr->sata_max_queue_depth;
		else
			continue;

		leapraid_change_queue_depth(sdev, qdepth);
	}
}

static void leapraid_update_exp_links(struct leapraid_adapter *adapter,
				      struct leapraid_topo_node *topo_node_exp,
				      u16 hdl)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_exp_p1 exp_p1;
	int i;

	cfgp2.handle = hdl;
	for (i = 0; i < topo_node_exp->phys_num; i++) {
		cfgp1.phy_number = i;
		if (leapraid_op_config_page(adapter, &exp_p1, cfgp1, cfgp2,
					    GET_SAS_EXPANDER_PG1))
			return;

		leapraid_transport_update_links(
			adapter,
			topo_node_exp->sas_address,
			le16_to_cpu(exp_p1.attached_dev_hdl),
			i,
			exp_p1.neg_link_rate >>
				LEAPRAID_SAS_NEG_LINK_RATE_SHIFT,
			topo_node_exp->card_port);
	}
}

static void leapraid_scan_exp_after_reset(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_topo_node *topo_node_exp;
	struct leapraid_exp_p0 exp_p0;
	unsigned long flags;
	u16 hdl;
	u8 port_id;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (hdl = 0xFFFF, cfgp2.handle = hdl;
	     !leapraid_op_config_page(adapter, &exp_p0, cfgp1, cfgp2,
				      GET_SAS_EXPANDER_PG0);
	     cfgp2.handle = hdl) {
		hdl = le16_to_cpu(exp_p0.dev_hdl);
		port_id = exp_p0.physical_port;
		spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
		topo_node_exp =
			leapraid_exp_find_by_sas_address(
				adapter,
				le64_to_cpu(exp_p0.sas_address),
				leapraid_get_port_by_id(adapter,
							port_id,
							false));
		spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock,
				       flags);

		if (topo_node_exp) {
			leapraid_update_exp_links(adapter, topo_node_exp, hdl);
		} else {
			leapraid_exp_add(adapter, hdl);

			dev_info(&adapter->pdev->dev,
				 "add exp: hdl=0x%04x, SAS addr=0x%016llx\n",
				 hdl,
				 (unsigned long long)le64_to_cpu(
					exp_p0.sas_address));
		}
	}
}

static void leapraid_scan_phy_disks_after_reset(
		struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	union cfg_param_1 cfgp1_extra = {0};
	union cfg_param_2 cfgp2_extra = {0};
	struct leapraid_sas_dev_p0 sas_dev_p0;
	struct leapraid_raidpd_p0 raidpd_p0;
	struct leapraid_sas_dev *sas_dev;
	u8 phys_disk_num, port_id;
	u16 hdl, parent_hdl;
	u64 sas_addr;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (phys_disk_num = 0xFF, cfgp2.form_specific = phys_disk_num;
	     !leapraid_op_config_page(adapter, &raidpd_p0,
				      cfgp1, cfgp2, GET_PHY_DISK_PG0);
	     cfgp2.form_specific = phys_disk_num) {
		phys_disk_num = raidpd_p0.phys_disk_num;
		hdl = le16_to_cpu(raidpd_p0.dev_hdl);
		sas_dev = leapraid_get_sas_dev_by_hdl(adapter, hdl);
		if (sas_dev) {
			leapraid_sdev_put(sas_dev);
			continue;
		}

		cfgp1_extra.form = LEAPRAID_SAS_DEV_CFG_PGAD_HDL;
		cfgp2_extra.handle = hdl;
		if (leapraid_op_config_page(adapter, &sas_dev_p0, cfgp1_extra,
					    cfgp2_extra, GET_SAS_DEVICE_PG0) !=
					    0)
			continue;

		parent_hdl = le16_to_cpu(sas_dev_p0.parent_dev_hdl);
		if (!leapraid_get_sas_address(adapter,
					      parent_hdl,
					      &sas_addr)) {
			port_id = sas_dev_p0.physical_port;
			leapraid_transport_update_links(
				adapter, sas_addr, hdl,
				sas_dev_p0.phy_num,
				LEAPRAID_SAS_NEG_LINK_RATE_1_5,
				leapraid_get_port_by_id(
					adapter, port_id, false));
			if (!hdl || hdl >
			    adapter->adapter_attr.features.max_dev_handle) {
				dev_warn(&adapter->pdev->dev,
					 "%s: Invalid device handle\n",
					 __func__);
			} else {
				set_bit(hdl, adapter->dev_topo.pd_hdls);
				leapraid_add_dev(adapter, hdl);

				dev_info(&adapter->pdev->dev,
					 "add pd hdl=0x%04x saddr=0x%016llx\n",
					 hdl,
					 (unsigned long long)le64_to_cpu(
						sas_dev_p0.sas_address));
			}
		}
	}
}

static void leapraid_scan_vol_after_reset(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	union cfg_param_1 cfgp1_extra = {0};
	union cfg_param_2 cfgp2_extra = {0};
	struct leapraid_evt_data_ir_change evt_data;
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_raidvol_p1 *vol_p1;
	struct leapraid_raidvol_p0 *vol_p0;
	u16 hdl;

	vol_p0 = kzalloc_obj(*vol_p0);
	if (!vol_p0)
		return;

	vol_p1 = kzalloc_obj(*vol_p1);
	if (!vol_p1) {
		kfree(vol_p0);
		return;
	}

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (hdl = 0xFFFF, cfgp2.handle = hdl;
	     !leapraid_op_config_page(adapter, vol_p1, cfgp1,
				      cfgp2, GET_RAID_VOLUME_PG1);
	     cfgp2.handle = hdl) {
		hdl = le16_to_cpu(vol_p1->dev_hdl);
		raid_volume = leapraid_raid_volume_find_by_wwid(
				adapter,
				le64_to_cpu(vol_p1->wwid));
		if (raid_volume) {
			leapraid_raid_volume_put(raid_volume);
			continue;
		}

		cfgp1_extra.size = sizeof(struct leapraid_raidvol_p0);
		cfgp2_extra.handle = hdl;
		if (leapraid_op_config_page(adapter, vol_p0, cfgp1_extra,
					    cfgp2_extra, GET_RAID_VOLUME_PG0))
			continue;

		if (vol_p0->volume_state == LEAPRAID_VOL_STATE_OPTIMAL ||
		    vol_p0->volume_state == LEAPRAID_VOL_STATE_ONLINE ||
		    vol_p0->volume_state == LEAPRAID_VOL_STATE_DEGRADED) {
			memset(&evt_data, 0,
			       sizeof(struct leapraid_evt_data_ir_change));
			evt_data.reason_code = LEAPRAID_EVT_IR_RC_VOLUME_ADD;
			evt_data.vol_dev_hdl = vol_p1->dev_hdl;
			leapraid_sas_volume_add(adapter, &evt_data);
			dev_info(&adapter->pdev->dev,
				 "add volume: hdl=0x%04x\n",
				 vol_p1->dev_hdl);
		}
	}

	kfree(vol_p0);
	kfree(vol_p1);
}

static void leapraid_scan_sas_dev_after_reset(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_p0;
	struct leapraid_sas_dev *sas_dev;
	u16 hdl, parent_hdl;
	u64 sas_address;
	u8 port_id;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (hdl = 0xFFFF, cfgp2.handle = hdl;
	     !leapraid_op_config_page(adapter, &sas_dev_p0, cfgp1, cfgp2,
				      GET_SAS_DEVICE_PG0);
	     cfgp2.handle = hdl) {
		hdl = le16_to_cpu(sas_dev_p0.dev_hdl);
		if (!hdl ||
		    hdl > adapter->adapter_attr.features.max_dev_handle) {
			dev_warn(&adapter->pdev->dev,
				 "%s: Invalid device handle\n", __func__);
			continue;
		}

		if (!(leapraid_is_end_dev(le32_to_cpu(sas_dev_p0.dev_info))))
			continue;

		port_id = sas_dev_p0.physical_port;
		sas_dev = leapraid_get_sas_dev_by_addr(
				adapter,
				le64_to_cpu(sas_dev_p0.sas_address),
				leapraid_get_port_by_id(
					adapter,
					port_id,
					false));
		if (sas_dev) {
			leapraid_sdev_put(sas_dev);
			continue;
		}

		parent_hdl = le16_to_cpu(sas_dev_p0.parent_dev_hdl);
		if (!leapraid_get_sas_address(adapter, parent_hdl,
					      &sas_address)) {
			leapraid_transport_update_links(
				adapter,
				sas_address,
				hdl,
				sas_dev_p0.phy_num,
				LEAPRAID_SAS_NEG_LINK_RATE_1_5,
				leapraid_get_port_by_id(adapter,
							port_id,
							false));
			leapraid_add_dev(adapter, hdl);
			dev_info(&adapter->pdev->dev,
				 "Add SAS dev: hdl=0x%04x, saddr=0x%016llx\n",
				 hdl,
				 (unsigned long long)le64_to_cpu(
					sas_dev_p0.sas_address));
		}
	}
}

static void leapraid_scan_all_dev_after_reset(struct leapraid_adapter *adapter)
{
	leapraid_sas_host_add(adapter, adapter->dev_topo.card.phys_num > 0);
	leapraid_scan_exp_after_reset(adapter);
	if (adapter->adapter_attr.raid_support) {
		leapraid_scan_phy_disks_after_reset(adapter);
		leapraid_scan_vol_after_reset(adapter);
	}
	leapraid_scan_sas_dev_after_reset(adapter);
}

static void leapraid_hardreset_async_logic(struct leapraid_adapter *adapter)
{
	unsigned long flags;

	leapraid_remove_unresp_dev(adapter);
	leapraid_del_dirty_vphy(adapter);
	leapraid_del_dirty_card_port(adapter);
	leapraid_update_dev_qdepth(adapter);
	leapraid_scan_all_dev_after_reset(adapter);

	if (adapter->scan_dev_desc.driver_loading)
		leapraid_scan_dev_done(adapter);
	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	adapter->access_ctrl.shost_recover_async = 0;
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
	wake_up(&adapter->access_ctrl.shost_recover_wq);
}

static int leapraid_send_enc_cmd(struct leapraid_adapter *adapter,
				 struct leapraid_sep_rep *sep_rep,
				 struct leapraid_sep_req *sep_req)
{
	void *req;
	bool reset_flg = false;
	int rc;
	u16 smid;

	mutex_lock(&adapter->driver_cmds.enc_cmd.mutex);
	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_SHORT,
					  __func__);
	if (rc)
		goto unlock;

	adapter->driver_cmds.enc_cmd.status = LEAPRAID_CMD_PENDING;
	smid = adapter->driver_cmds.enc_cmd.inter_taskid;
	req = leapraid_get_task_desc(adapter, smid);
	memset(req, 0, LEAPRAID_REQUEST_SIZE);
	memcpy(req, sep_req, sizeof(struct leapraid_sep_req));
	init_completion(&adapter->driver_cmds.enc_cmd.done);
	leapraid_fire_task(adapter, smid);
	wait_for_completion_timeout(&adapter->driver_cmds.enc_cmd.done,
				    LEAPRAID_ENC_CMD_TIMEOUT * HZ);
	if (!(adapter->driver_cmds.enc_cmd.status & LEAPRAID_CMD_DONE)) {
		dev_err(&adapter->pdev->dev,
			"%s: SEP command timeout, status=0x%x\n",
			__func__, adapter->driver_cmds.enc_cmd.status);
		leapraid_log_req_context(adapter, smid, sep_req);
		reset_flg =
			leapraid_check_reset(
				adapter->driver_cmds.enc_cmd.status);
		rc = -EFAULT;
		goto do_hard_reset;
	}

	if (adapter->driver_cmds.enc_cmd.status & LEAPRAID_CMD_REPLY_VALID)
		memcpy(sep_rep, &adapter->driver_cmds.enc_cmd.reply,
		       sizeof(struct leapraid_sep_rep));
do_hard_reset:
	if (reset_flg) {
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
			 __func__, __LINE__);
		leapraid_hard_reset_handler(adapter, FULL_RESET);
	}

	adapter->driver_cmds.enc_cmd.status = LEAPRAID_CMD_NOT_USED;
unlock:
	mutex_unlock(&adapter->driver_cmds.enc_cmd.mutex);
	return rc;
}

static void leapraid_set_led(struct leapraid_adapter *adapter,
			     struct leapraid_sas_dev *sas_dev, bool on)
{
	struct leapraid_sep_rep sep_rep;
	struct leapraid_sep_req sep_req;

	if (!sas_dev)
		return;

	memset(&sep_req, 0, sizeof(struct leapraid_sep_req));
	memset(&sep_rep, 0, sizeof(struct leapraid_sep_rep));
	sep_req.func = LEAPRAID_FUNC_SCSI_ENC_PROCESSOR;
	sep_req.act = LEAPRAID_SEP_REQ_ACT_WRITE_STATUS;
	if (on) {
		u32 status = LEAPRAID_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT;

		sep_req.slot_status = cpu_to_le32(status);
		sep_req.dev_hdl = cpu_to_le16(sas_dev->hdl);
		sep_req.flg = LEAPRAID_SEP_REQ_FLG_DEVHDL_ADDRESS;
		if (leapraid_send_enc_cmd(adapter, &sep_rep, &sep_req)) {
			leapraid_sdev_put(sas_dev);
			return;
		}

		sas_dev->led_on = 1;
		leapraid_sdev_put(sas_dev);
	} else {
		sep_req.slot_status = 0;
		sep_req.slot = cpu_to_le16(sas_dev->slot);
		sep_req.dev_hdl = 0;
		sep_req.enc_hdl = cpu_to_le16(sas_dev->enc_hdl);
		sep_req.flg = LEAPRAID_SEP_REQ_FLG_ENCLOSURE_SLOT_ADDRESS;
		leapraid_send_enc_cmd(adapter, &sep_rep, &sep_req);
	}
}

static int leapraid_wait_adapter_recovery(struct leapraid_adapter *adapter)
{
	unsigned long flags;

	while (leapraid_shost_in_recovery(adapter->shost) ||
	       READ_ONCE(adapter->access_ctrl.shost_recovering)) {
		if (READ_ONCE(adapter->access_ctrl.host_removing) ||
		    READ_ONCE(adapter->fw_evt_s.fw_evt_cleanup)) {
			spin_lock_irqsave(
				&adapter->reset_desc.adapter_reset_lock,
				flags);
			adapter->access_ctrl.shost_recover_async = 0;
			spin_unlock_irqrestore(
				&adapter->reset_desc.adapter_reset_lock,
				flags);

			wake_up(&adapter->access_ctrl.shost_recover_wq);

			dev_warn(&adapter->pdev->dev,
				 "%s: Failed, shost %d, host %d\n",
				 __func__,
				 adapter->access_ctrl.shost_recovering,
				 adapter->access_ctrl.host_removing);

			return -EFAULT;
		}

		wait_event_timeout(
			adapter->access_ctrl.recovery_waitq,
			(!leapraid_shost_in_recovery(adapter->shost) &&
			 !READ_ONCE(adapter->access_ctrl.shost_recovering)),
			msecs_to_jiffies(1000));
	}

	return 0;
}

static void leapraid_fw_work(struct leapraid_adapter *adapter,
			     struct leapraid_fw_evt_work *fw_evt)
{
	struct leapraid_sas_dev *sas_dev;
	unsigned long flags;

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	adapter->fw_evt_s.cur_evt = fw_evt;
	adapter->fw_evt_s.cur_evt_task = current;
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	leapraid_del_fw_evt_from_list(adapter, fw_evt);
	if (adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering) {
		spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
		leapraid_fw_evt_put(fw_evt);
		adapter->fw_evt_s.cur_evt = NULL;
		adapter->fw_evt_s.cur_evt_task = NULL;
		spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
		return;
	}
	switch (fw_evt->evt_type) {
	case LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST:
		leapraid_sas_topo_chg_evt(adapter, fw_evt);
		break;
	case LEAPRAID_EVT_IR_CHANGE:
		leapraid_sas_ir_chg_evt(adapter, fw_evt);
		break;
	case LEAPRAID_EVT_SAS_ENCL_DEV_STATUS_CHANGE:
		leapraid_sas_enc_dev_stat_chg_evt(adapter, fw_evt);
		break;
	case LEAPRAID_EVT_REMOVE_DEAD_DEV:
		if (leapraid_wait_adapter_recovery(adapter))
			goto out_cleanup;

		leapraid_hardreset_async_logic(adapter);
		break;
	case LEAPRAID_EVT_TURN_ON_PFA_LED:
		sas_dev = leapraid_get_sas_dev_by_hdl(adapter,
						      fw_evt->dev_handle);
		leapraid_set_led(adapter, sas_dev, true);
		break;
	case LEAPRAID_EVT_SCAN_DEV_DONE:
		adapter->scan_dev_desc.scan_start = 0;
		break;
	default:
		break;
	}
out_cleanup:
	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	leapraid_fw_evt_put(fw_evt);
	adapter->fw_evt_s.cur_evt = NULL;
	adapter->fw_evt_s.cur_evt_task = NULL;
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
}

static void leapraid_sas_dev_stat_chg_evt(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_sas_dev_status_change *event_data)
{
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_sas_dev *sas_dev;
	u64 sas_address;
	unsigned long flags;

	switch (event_data->reason_code) {
	case LEAPRAID_EVT_SAS_DEV_STAT_RC_INTERNAL_DEV_RESET:
	case LEAPRAID_EVT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		break;
	default:
		return;
	}

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);

	sas_address = le64_to_cpu(event_data->sas_address);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(
			adapter,
			sas_address,
			leapraid_get_port_by_id(adapter,
						event_data->physical_port,
						false));
	if (!sas_dev || !sas_dev->starget)
		goto out_unlock;

	starget_priv = sas_dev->starget->hostdata;
	if (starget_priv) {
		switch (event_data->reason_code) {
		case LEAPRAID_EVT_SAS_DEV_STAT_RC_INTERNAL_DEV_RESET:
			starget_priv->tm_busy = 1;
			break;
		case LEAPRAID_EVT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
			starget_priv->tm_busy = 0;
			break;
		}
	}

out_unlock:
	if (sas_dev)
		leapraid_sdev_put(sas_dev);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
}

static void leapraid_set_volume_delete_flag(struct leapraid_adapter *adapter,
					    u16 handle)
{
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_starget_priv *sas_target_priv_data;
	unsigned long flags;

	raid_volume = leapraid_raid_volume_find_by_hdl(adapter, handle);
	if (raid_volume) {
		spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
		if (raid_volume->starget &&
		    raid_volume->starget->hostdata) {
			sas_target_priv_data = raid_volume->starget->hostdata;
			sas_target_priv_data->deleted = 1;
		}
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
		leapraid_raid_volume_put(raid_volume);
	}
}

static void leapraid_check_ir_change_evt(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_ir_change *evt_data)
{
	u16 phys_disk_dev_hdl;

	switch (evt_data->reason_code) {
	case LEAPRAID_EVT_IR_RC_VOLUME_DELETE:
		leapraid_set_volume_delete_flag(
			adapter,
			le16_to_cpu(evt_data->vol_dev_hdl));
		break;
	case LEAPRAID_EVT_IR_RC_PD_UNHIDDEN_TO_DELETE:
		phys_disk_dev_hdl =
			le16_to_cpu(evt_data->phys_disk_dev_hdl);
		if (!phys_disk_dev_hdl ||
		    phys_disk_dev_hdl >
		    adapter->adapter_attr.features.max_dev_handle) {
			dev_warn(&adapter->pdev->dev,
				 "%s: Invalid device handle\n", __func__);
		} else {
			clear_bit(phys_disk_dev_hdl,
				  adapter->dev_topo.pd_hdls);
			leapraid_tgt_rst_send(adapter, phys_disk_dev_hdl);
		}
		break;
	}
}

static void leapraid_topo_del_evts_process_exp_status(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_sas_topo_change_list *evt_data)
{
	struct leapraid_fw_evt_work *fw_evt = NULL;
	struct leapraid_evt_data_sas_topo_change_list *loc_evt_data;
	unsigned long flags;
	u16 exp_hdl;

	exp_hdl = le16_to_cpu(evt_data->exp_dev_hdl);

	switch (evt_data->exp_status) {
	case LEAPRAID_EVT_SAS_TOPO_ES_NOT_RESPONDING:
		spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
		list_for_each_entry(fw_evt,
				    &adapter->fw_evt_s.fw_evt_list, list) {
			if (fw_evt->evt_type !=
			    LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST ||
			    fw_evt->ignore)
				continue;

			loc_evt_data = fw_evt->evt_data;
			if ((loc_evt_data->exp_status ==
			     LEAPRAID_EVT_SAS_TOPO_ES_ADDED ||
			     loc_evt_data->exp_status ==
			     LEAPRAID_EVT_SAS_TOPO_ES_RESPONDING) &&
			    le16_to_cpu(loc_evt_data->exp_dev_hdl) == exp_hdl)
				fw_evt->ignore = 1;
		}
		spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
		break;
	default:
		break;
	}
}

static void leapraid_check_topo_del_evts(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_data_sas_topo_change_list *evt_data)
{
	int reason_code;
	u16 hdl;
	int i;

	for (i = 0; i < evt_data->entry_num; i++) {
		hdl = le16_to_cpu(evt_data->phy[i].attached_dev_hdl);
		if (!hdl)
			continue;

		reason_code = evt_data->phy[i].phy_status &
				LEAPRAID_EVT_SAS_TOPO_RC_MASK;
		if (reason_code ==
		    LEAPRAID_EVT_SAS_TOPO_RC_TARG_NOT_RESPONDING)
			leapraid_tgt_not_responding(adapter, hdl);
	}
	leapraid_topo_del_evts_process_exp_status(adapter, evt_data);
}

static bool leapraid_async_evt_validate(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_notify_rep *event_notify_rep)
{
	size_t msg_len;
	size_t evt_sz;
	size_t evt_avail;
	u16 evt;

	msg_len = event_notify_rep->msg_len * sizeof(u32);
	if (msg_len > LEAPRAID_REPLY_SIZE ||
	    msg_len < offsetof(struct leapraid_evt_notify_rep, evt_data)) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid async event msg_len=%zu\n",
			 __func__, msg_len);
		return false;
	}

	evt_sz = le16_to_cpu(event_notify_rep->evt_data_len) * sizeof(u32);
	evt_avail = msg_len -
		    offsetof(struct leapraid_evt_notify_rep, evt_data);
	if (evt_sz > evt_avail) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Invalid async event evt_data_len=%zu\n",
			 __func__, evt_sz);
		return false;
	}

	evt = le16_to_cpu(event_notify_rep->evt);
	switch (evt) {
	case LEAPRAID_EVT_SAS_DEV_STATUS_CHANGE:
		if (evt_sz <
		    sizeof(struct leapraid_evt_data_sas_dev_status_change))
			goto invalid_evt_sz;
		break;
	case LEAPRAID_EVT_IR_CHANGE:
		if (evt_sz < sizeof(struct leapraid_evt_data_ir_change))
			goto invalid_evt_sz;
		break;
	case LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST:
	{
		struct leapraid_evt_data_sas_topo_change_list *evt_data =
			(void *)event_notify_rep->evt_data;
		size_t hdr_sz;

		hdr_sz =
			offsetof(struct leapraid_evt_data_sas_topo_change_list,
				 phy);
		if (evt_sz < hdr_sz)
			goto invalid_evt_sz;

		if (evt_data->entry_num >
		    (evt_sz - hdr_sz) / sizeof(evt_data->phy[0]))
			goto invalid_evt_sz;
		break;
	}
	case LEAPRAID_EVT_SAS_ENCL_DEV_STATUS_CHANGE:
		if (evt_sz <
		    sizeof(struct leapraid_evt_data_sas_enc_dev_status_change))
			goto invalid_evt_sz;
		break;
	default:
		break;
	}

	return true;

invalid_evt_sz:
	dev_warn(&adapter->pdev->dev,
		 "%s: Invalid async event size=%zu for evt=0x%x\n",
		 __func__, evt_sz, evt);
	return false;
}

static bool leapraid_async_process_evt(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_notify_rep *event_notify_rep)
{
	u16 evt = le16_to_cpu(event_notify_rep->evt);
	bool exit_flag = false;

	if (adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering)
		return true;

	switch (evt) {
	case LEAPRAID_EVT_SAS_DEV_STATUS_CHANGE:
		leapraid_sas_dev_stat_chg_evt(
			adapter,
			(struct leapraid_evt_data_sas_dev_status_change
				*)event_notify_rep->evt_data);
		break;
	case LEAPRAID_EVT_IR_CHANGE:
		leapraid_check_ir_change_evt(
			adapter,
			(struct leapraid_evt_data_ir_change
				*)event_notify_rep->evt_data);
		break;
	case LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST:
		leapraid_check_topo_del_evts(
			adapter,
			(struct leapraid_evt_data_sas_topo_change_list
				*)event_notify_rep->evt_data);
		if (adapter->access_ctrl.shost_recovering) {
			exit_flag = true;
			return exit_flag;
		}
		break;
	case LEAPRAID_EVT_SAS_ENCL_DEV_STATUS_CHANGE:
		break;
	default:
		exit_flag = true;
		return exit_flag;
	}

	return exit_flag;
}

static void leapraid_async_evt_cb_enqueue(
		struct leapraid_adapter *adapter,
		struct leapraid_evt_notify_rep *evt_notify_rep)
{
	struct leapraid_fw_evt_work *fw_evt;
	u16 evt_sz;

	fw_evt = leapraid_alloc_fw_evt_work();
	if (!fw_evt)
		return;

	evt_sz = le16_to_cpu(evt_notify_rep->evt_data_len) * sizeof(u32);
	fw_evt->evt_data = kmemdup(evt_notify_rep->evt_data,
				   evt_sz, GFP_ATOMIC);
	if (!fw_evt->evt_data) {
		leapraid_fw_evt_put(fw_evt);
		return;
	}
	fw_evt->adapter = adapter;
	fw_evt->evt_type = le16_to_cpu(evt_notify_rep->evt);
	leapraid_fw_evt_add(adapter, fw_evt);
	leapraid_fw_evt_put(fw_evt);
}

static void leapraid_async_evt_cb(struct leapraid_adapter *adapter,
				  u8 msix_index, u32 rep_paddr)
{
	struct leapraid_evt_notify_rep *evt_notify_rep;

	if (adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering)
		return;

	evt_notify_rep = leapraid_get_reply_vaddr(adapter, rep_paddr);
	if (unlikely(!evt_notify_rep))
		return;

	if (!leapraid_async_evt_validate(adapter, evt_notify_rep))
		return;

	if (leapraid_async_process_evt(adapter, evt_notify_rep))
		return;

	leapraid_async_evt_cb_enqueue(adapter, evt_notify_rep);
}

static void leapraid_handle_async_event(struct leapraid_adapter *adapter,
					u8 msix_index, u32 reply)
{
	struct leapraid_evt_notify_rep *leap_mpi_rep =
		leapraid_get_reply_vaddr(adapter, reply);

	if (!leap_mpi_rep)
		return;

	if (leap_mpi_rep->func != LEAPRAID_FUNC_EVENT_NOTIFY)
		return;

	leapraid_async_evt_cb(adapter, msix_index, reply);
}

void leapraid_async_turn_on_led(struct leapraid_adapter *adapter, u16 handle)
{
	struct leapraid_fw_evt_work *fw_event;

	fw_event = leapraid_alloc_fw_evt_work();
	if (!fw_event)
		return;

	fw_event->dev_handle = handle;
	fw_event->adapter = adapter;
	fw_event->evt_type = LEAPRAID_EVT_TURN_ON_PFA_LED;
	leapraid_fw_evt_add(adapter, fw_event);
	leapraid_fw_evt_put(fw_event);
}

static void leapraid_hardreset_barrier(struct leapraid_adapter *adapter)
{
	struct leapraid_fw_evt_work *fw_event;

	fw_event = leapraid_alloc_fw_evt_work();
	if (!fw_event)
		return;

	fw_event->adapter = adapter;
	fw_event->evt_type = LEAPRAID_EVT_REMOVE_DEAD_DEV;
	leapraid_fw_evt_add(adapter, fw_event);
	leapraid_fw_evt_put(fw_event);
}

static void leapraid_scan_dev_complete(struct leapraid_adapter *adapter)
{
	struct leapraid_fw_evt_work *fw_evt;

	fw_evt = leapraid_alloc_fw_evt_work();
	if (!fw_evt)
		return;

	fw_evt->evt_type = LEAPRAID_EVT_SCAN_DEV_DONE;
	fw_evt->adapter = adapter;
	leapraid_fw_evt_add(adapter, fw_evt);
	leapraid_fw_evt_put(fw_evt);
}

static void leapraid_handle_scan_cb(struct leapraid_adapter *adapter,
				    struct leapraid_driver_cmd *cmd,
				    struct leapraid_rep *rep)
{
	u16 status;

	cmd->status &= ~LEAPRAID_CMD_PENDING;

	status = le16_to_cpu(rep->adapter_status) &
		 LEAPRAID_ADAPTER_STATUS_MASK;
	if (status != LEAPRAID_ADAPTER_STATUS_SUCCESS)
		adapter->scan_dev_desc.scan_dev_failed = 1;

	if (!cmd->async_scan_dev) {
		complete(&cmd->done);
		return;
	}

	if (status == LEAPRAID_ADAPTER_STATUS_SUCCESS)
		leapraid_scan_dev_complete(adapter);
	else
		adapter->scan_dev_desc.scan_start_failed = status;
}

static void leapraid_handle_ctl_cb(struct leapraid_adapter *adapter,
				   struct leapraid_rep *rep,
				   u16 taskid)
{
	struct leapraid_scsiio_rep *scsiio_reply;

	if (rep->function != LEAPRAID_FUNC_SCSIIO &&
	    rep->function != LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH)
		return;

	scsiio_reply = (struct leapraid_scsiio_rep *)rep;
	if (!(scsiio_reply->scsi_state &
	      LEAPRAID_SCSI_STATE_AUTOSENSE_VALID))
		return;

	memcpy(&adapter->driver_cmds.ctl_cmd.sense,
	       leapraid_get_sense_buffer(adapter, taskid),
	       min_t(u32, SCSI_SENSE_BUFFERSIZE,
		     le32_to_cpu(scsiio_reply->sense_count)));
}

static bool leapraid_driver_cmds_done(struct leapraid_adapter *adapter,
				      u16 taskid, u8 msix_index,
				      u32 rep_paddr, u8 cb_idx)
{
	struct leapraid_rep *leap_mpi_rep =
		leapraid_get_reply_vaddr(adapter, rep_paddr);
	struct leapraid_driver_cmd *sp_cmd, *_sp_cmd = NULL;
	u8 reply_len;

	list_for_each_entry(sp_cmd, &adapter->driver_cmds.special_cmd_list,
			    list)
		if (cb_idx == sp_cmd->cb_idx) {
			_sp_cmd = sp_cmd;
			break;
		}

	if (WARN_ON(!_sp_cmd))
		return true;
	if (WARN_ON(_sp_cmd->status == LEAPRAID_CMD_NOT_USED))
		return true;
	if (WARN_ON(taskid != _sp_cmd->hp_taskid &&
		    taskid != _sp_cmd->taskid &&
		    taskid != _sp_cmd->inter_taskid))
		return true;
	_sp_cmd->status |= LEAPRAID_CMD_DONE;
	if (leap_mpi_rep) {
		reply_len = leap_mpi_rep->msg_len * sizeof(u32);
		if (reply_len > LEAPRAID_REPLY_SIZE)
			reply_len = LEAPRAID_REPLY_SIZE;
		memcpy(&_sp_cmd->reply, leap_mpi_rep, reply_len);
		_sp_cmd->status |= LEAPRAID_CMD_REPLY_VALID;

		if (_sp_cmd->cb_idx == LEAPRAID_SCAN_DEV_CB_IDX) {
			leapraid_handle_scan_cb(adapter, _sp_cmd,
						leap_mpi_rep);
			return true;
		}

		if (_sp_cmd->cb_idx == LEAPRAID_CTL_CB_IDX)
			leapraid_handle_ctl_cb(adapter, leap_mpi_rep, taskid);
	}

	_sp_cmd->status &= ~LEAPRAID_CMD_PENDING;
	complete(&_sp_cmd->done);

	return true;
}

static void leapraid_complete_task(struct leapraid_adapter *adapter,
				   u16 taskid, u8 msix_idx, u32 rep)
{
	bool scsiio_task = taskid <= adapter->shost->can_queue;

	if (scsiio_task) {
		if (leapraid_scsiio_done(adapter, taskid, msix_idx, rep))
			leapraid_free_taskid(adapter, taskid);
		return;
	}

	if (leapraid_driver_cmds_done(adapter, taskid, msix_idx, rep,
				      leapraid_get_cb_idx(adapter, taskid)))
		leapraid_free_taskid(adapter, taskid);
}

static void leapraid_request_descript_handler(
		struct leapraid_adapter *adapter,
		union leapraid_rep_desc_union *rpf,
		u8 req_desc_type, u8 msix_idx)
{
	u32 rep;
	u16 taskid;

	rep = 0;
	taskid = le16_to_cpu(rpf->dflt_rep.taskid);
	switch (req_desc_type) {
	case LEAPRAID_RPY_DESC_FLG_FP_SCSI_IO_SUCCESS:
	case LEAPRAID_RPY_DESC_FLG_SCSI_IO_SUCCESS:
		leapraid_complete_task(adapter, taskid, msix_idx, 0);
		break;
	case LEAPRAID_RPY_DESC_FLG_ADDRESS_REPLY:
		rep = le32_to_cpu(rpf->addr_rep.rep_frame_addr);
		if (rep > (u32)adapter->mem_desc.rep_msg_dma +
			   adapter->adapter_attr.rep_msg_qd *
			   LEAPRAID_REPLY_SIZE ||
		    rep < (u32)adapter->mem_desc.rep_msg_dma)
			rep = 0;

		if (taskid)
			leapraid_complete_task(adapter, taskid, msix_idx, rep);
		else
			leapraid_handle_async_event(adapter, msix_idx, rep);

		if (!rep)
			return;

		adapter->rep_msg_host_idx =
			(adapter->rep_msg_host_idx ==
				(adapter->adapter_attr.rep_msg_qd - 1)) ?
					0 : adapter->rep_msg_host_idx + 1;
		adapter->mem_desc.rep_msg_addr[adapter->rep_msg_host_idx] =
			cpu_to_le32(rep);
		wmb(); /* Make sure that all write ops are in order */
		writel(adapter->rep_msg_host_idx,
		       &adapter->iomem_base->rep_msg_host_idx);

		break;
	default:
		break;
	}
}

int leapraid_rep_queue_handler(struct leapraid_rq *rq)
{
	struct leapraid_adapter *adapter = rq->adapter;
	union leapraid_rep_desc_union *rep_desc;
	u8 req_desc_type;
	u64 finish_cmds;
	u8 msix_idx;

	msix_idx = rq->msix_idx;
	finish_cmds = 0;
	if (!atomic_add_unless(&rq->busy, LEAPRAID_BUSY_LIMIT,
			       LEAPRAID_BUSY_LIMIT))
		return finish_cmds;

	rep_desc = &rq->rep_desc[rq->rep_post_host_idx];
	req_desc_type = rep_desc->dflt_rep.rep_flg &
				LEAPRAID_RPY_DESC_FLG_TYPE_MASK;
	if (req_desc_type == LEAPRAID_RPY_DESC_FLG_UNUSED) {
		atomic_dec(&rq->busy);
		return finish_cmds;
	}

	for (;;) {
		if (rep_desc->u.low == UINT_MAX ||
		    rep_desc->u.high == UINT_MAX)
			break;

		leapraid_request_descript_handler(adapter, rep_desc,
						  req_desc_type, msix_idx);
		dev_dbg(&adapter->pdev->dev,
			"LEAPRAID_SCSIIO: Handled Desc taskid %d, msix %d\n",
			rep_desc->dflt_rep.taskid, msix_idx);
		rep_desc->words = cpu_to_le64(ULLONG_MAX);
		rq->rep_post_host_idx =
			(rq->rep_post_host_idx ==
				(adapter->adapter_attr.rep_desc_qd -
					LEAPRAID_BUSY_LIMIT)) ?
					0 : rq->rep_post_host_idx + 1;
		req_desc_type =
			rq->rep_desc[rq->rep_post_host_idx].dflt_rep.rep_flg &
					LEAPRAID_RPY_DESC_FLG_TYPE_MASK;
		finish_cmds++;
		if (req_desc_type == LEAPRAID_RPY_DESC_FLG_UNUSED)
			break;
		rep_desc = rq->rep_desc + rq->rep_post_host_idx;
	}

	if (!finish_cmds) {
		atomic_dec(&rq->busy);
		return finish_cmds;
	}

	wmb(); /* Make sure that all write ops are in order */
	writel(rq->rep_post_host_idx | ((msix_idx & LEAPRAID_MSIX_GROUP_MASK) <<
					LEAPRAID_RPHI_MSIX_IDX_SHIFT),
	       &adapter->iomem_base->rep_post_reg_idx[
		       msix_idx / LEAPRAID_MSIX_GROUP_SIZE].idx);
	atomic_dec(&rq->busy);
	return finish_cmds;
}

static irqreturn_t leapraid_irq_handler(int irq, void *bus_id)
{
	struct leapraid_rq *rq = bus_id;
	struct leapraid_adapter *adapter = rq->adapter;

	dev_dbg(&adapter->pdev->dev,
		"LEAPRAID_SCSIIO: Receive a interrupt, irq %d msix %d\n",
		irq, rq->msix_idx);

	if (adapter->mask_int)
		return IRQ_NONE;

	return (leapraid_rep_queue_handler(rq) > 0 ?
		 IRQ_HANDLED : IRQ_NONE);
}

void leapraid_sync_irqs(struct leapraid_adapter *adapter, bool poll)
{
	struct leapraid_int_rq *int_rq;
	struct leapraid_blk_mq_poll_rq *blk_mq_poll_rq;
	unsigned int i;

	if (!adapter->notification_desc.msix_enable)
		return;

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering)
		return;

	for (i = 0; i < adapter->notification_desc.iopoll_qdex; i++) {
		int_rq = &adapter->notification_desc.int_rqs[i];
		if (adapter->access_ctrl.shost_recovering ||
		    adapter->access_ctrl.host_removing ||
		    adapter->access_ctrl.pcie_recovering)
			return;

		if (int_rq->rq.msix_idx == 0)
			continue;

		synchronize_irq(pci_irq_vector(adapter->pdev,
					       int_rq->rq.msix_idx));
		if (poll)
			leapraid_rep_queue_handler(&int_rq->rq);
	}

	for (i = 0; i < adapter->notification_desc.iopoll_qcnt; i++) {
		blk_mq_poll_rq =
			&adapter->notification_desc.blk_mq_poll_rqs[i];
		if (adapter->access_ctrl.shost_recovering ||
		    adapter->access_ctrl.host_removing ||
		    adapter->access_ctrl.pcie_recovering)
			return;

		if (blk_mq_poll_rq->rq.msix_idx == 0)
			continue;

		leapraid_rep_queue_handler(&blk_mq_poll_rq->rq);
	}
}

static void leapraid_sync_irqs_for_cleanup(struct leapraid_adapter *adapter)
{
	struct leapraid_notification_desc *desc = &adapter->notification_desc;
	struct leapraid_int_rq *int_rq;
	unsigned int i;

	if (!adapter->mask_int && leapraid_pci_active(adapter))
		leapraid_mask_int(adapter);

	for (i = 0; i < desc->int_rqs_allocated; i++) {
		int_rq = &desc->int_rqs[i];
		synchronize_irq(pci_irq_vector(adapter->pdev,
					       int_rq->rq.msix_idx));
	}
}

void leapraid_mq_polling_pause(struct leapraid_adapter *adapter)
{
	struct leapraid_notification_desc *desc;
	int iopoll_q_count;
	int qid;

	desc = &adapter->notification_desc;
	iopoll_q_count = adapter->adapter_attr.rq_cnt - desc->iopoll_qdex;

	for (qid = 0; qid < iopoll_q_count; qid++)
		atomic_set(&desc->blk_mq_poll_rqs[qid].pause, 1);

	for (qid = 0; qid < iopoll_q_count; qid++) {
		while (atomic_read(&desc->blk_mq_poll_rqs[qid].busy)) {
			cpu_relax();
			udelay(LEAPRAID_IO_POLL_DELAY_US);
		}
	}
}

void leapraid_mq_polling_resume(struct leapraid_adapter *adapter)
{
	struct leapraid_notification_desc *desc;
	int iopoll_q_count;
	int qid;

	desc = &adapter->notification_desc;
	iopoll_q_count = adapter->adapter_attr.rq_cnt - desc->iopoll_qdex;

	for (qid = 0; qid < iopoll_q_count; qid++)
		atomic_set(&desc->blk_mq_poll_rqs[qid].pause, 0);
}

static int leapraid_unlock_host_diag(struct leapraid_adapter *adapter,
				     u32 *host_diag)
{
	const u32 unlock_seq[] = {
		LEAPRAID_WRSEQ_FLUSH_KEY_VALUE,
		LEAPRAID_WRSEQ_1ST_KEY_VALUE,
		LEAPRAID_WRSEQ_2ND_KEY_VALUE,
		LEAPRAID_WRSEQ_3RD_KEY_VALUE,
		LEAPRAID_WRSEQ_4TH_KEY_VALUE,
		LEAPRAID_WRSEQ_5TH_KEY_VALUE,
		LEAPRAID_WRSEQ_6TH_KEY_VALUE
	};

	const int max_retries = LEAPRAID_UNLOCK_RETRY_LIMIT;
	int retry = 0;
	unsigned int i;

	*host_diag = 0;
	while (retry++ <= max_retries) {
		for (i = 0; i < ARRAY_SIZE(unlock_seq); i++)
			writel(unlock_seq[i], &adapter->iomem_base->ws);

		msleep(LEAPRAID_UNLOCK_SLEEP_MS);

		*host_diag = leapraid_readl(&adapter->iomem_base->host_diag);
		if (*host_diag & LEAPRAID_DIAG_WRITE_ENABLE)
			return 0;
	}

	dev_err(&adapter->pdev->dev, "Try host reset timeout!\n");
	return -EFAULT;
}

static int leapraid_host_diag_reset(struct leapraid_adapter *adapter)
{
	u32 host_diag;

	pci_cfg_access_lock(adapter->pdev);

	mutex_lock(&adapter->reset_desc.host_diag_mutex);
	if (leapraid_unlock_host_diag(adapter, &host_diag))
		goto out_cleanup;

	writel(host_diag | LEAPRAID_DIAG_RESET,
	       &adapter->iomem_base->host_diag);

	msleep(LEAPRAID_MSLEEP_EXTRA_LONG_MS);
	msleep(LEAPRAID_MSLEEP_NORMAL_MS);

	host_diag = leapraid_readl(&adapter->iomem_base->host_diag);
	if (host_diag == LEAPRAID_INVALID_HOST_DIAG_VAL ||
	    host_diag & LEAPRAID_DIAG_RESET)
		goto out_cleanup;

	writel(0x0, &adapter->iomem_base->ws);
	mutex_unlock(&adapter->reset_desc.host_diag_mutex);
	if (!leapraid_wait_adapter_ready(adapter))
		goto out_failed;

	pci_cfg_access_unlock(adapter->pdev);
	return 0;

out_cleanup:
	mutex_unlock(&adapter->reset_desc.host_diag_mutex);
out_failed:
	pci_cfg_access_unlock(adapter->pdev);
	dev_err(&adapter->pdev->dev, "Host diag failed\n");
	return -EFAULT;
}

static int leapraid_find_matching_port(
		struct leapraid_card_port *card_port_table,
		u8 count, u8 port_id, u64 sas_addr)
{
	int i;

	for (i = 0; i < count; i++)
		if (card_port_table[i].port_id == port_id &&
		    card_port_table[i].sas_address == sas_addr)
			return i;

	return LEAPRAID_INVALID_INDEX;
}

static u8 leapraid_fill_card_port_table(
		struct leapraid_adapter *adapter,
		struct leapraid_sas_io_unit_p0 *sas_iounit_p0,
		struct leapraid_card_port *new_card_port_table)
{
	u8 port_entry_num = 0, port_id;
	u16 attached_hdl;
	u64 attached_sas_addr;
	int i, idx;

	for (i = 0; i < adapter->dev_topo.card.phys_num; i++) {
		if (sas_iounit_p0->phy_info[i].neg_link_rate >>
		    LEAPRAID_SAS_NEG_LINK_RATE_SHIFT <
		    LEAPRAID_SAS_NEG_LINK_RATE_1_5)
			continue;

		attached_hdl = le16_to_cpu(sas_iounit_p0->phy_info[i]
					   .attached_dev_hdl);
		if (leapraid_get_sas_address(adapter,
					     attached_hdl,
					     &attached_sas_addr) != 0)
			continue;

		port_id = sas_iounit_p0->phy_info[i].port;

		idx = leapraid_find_matching_port(new_card_port_table,
						  port_entry_num,
						  port_id,
						  attached_sas_addr);
		if (idx >= 0) {
			new_card_port_table[idx].phy_mask |= BIT(i);
		} else {
			new_card_port_table[port_entry_num].port_id = port_id;
			new_card_port_table[port_entry_num].phy_mask = BIT(i);
			new_card_port_table[port_entry_num].sas_address =
				attached_sas_addr;
			port_entry_num++;
		}
	}

	return port_entry_num;
}

static u8 leapraid_set_new_card_port_table_after_reset(
		struct leapraid_adapter *adapter,
		struct leapraid_card_port *new_card_port_table)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_io_unit_p0 *sas_iounit_p0;
	u8 port_entry_num = 0;
	u16 sz;

	sz = offsetof(struct leapraid_sas_io_unit_p0, phy_info) +
		(adapter->dev_topo.card.phys_num *
		 sizeof(struct leapraid_sas_io_unit0_phy_info));
	sas_iounit_p0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_p0)
		return port_entry_num;

	cfgp1.size = sz;
	if (leapraid_op_config_page(adapter, sas_iounit_p0, cfgp1, cfgp2,
				    GET_SAS_IOUNIT_PG0) != 0)
		goto out_free;

	port_entry_num = leapraid_fill_card_port_table(adapter,
						       sas_iounit_p0,
						       new_card_port_table);
out_free:
	kfree(sas_iounit_p0);
	return port_entry_num;
}

static void leapraid_update_existing_port(struct leapraid_adapter *adapter,
					  struct leapraid_card_port *new_table,
					  int entry_idx, int port_entry_num)
{
	struct leapraid_card_port *matched_card_port;
	int matched_code;
	int count, lcount = 0;
	u64 sas_addr;
	int i;

	matched_code = leapraid_check_card_port(adapter,
						&new_table[entry_idx],
						&matched_card_port,
						&count);

	if (!matched_card_port)
		return;

	if (matched_code == SAME_PORT_WITH_PARTIALLY_CHANGED_PHYS ||
	    matched_code == SAME_ADDR_WITH_PARTIALLY_CHANGED_PHYS) {
		leapraid_add_or_del_phys_from_existing_port(adapter,
							    matched_card_port,
							    new_table,
							    entry_idx,
							    port_entry_num);
	} else if (matched_code == SAME_ADDR_ONLY) {
		sas_addr = new_table[entry_idx].sas_address;
		for (i = 0; i < port_entry_num; i++)
			if (new_table[i].sas_address == sas_addr)
				lcount++;

		if (count > 1 || lcount > 1)
			return;

		leapraid_add_or_del_phys_from_existing_port(adapter,
							    matched_card_port,
							    new_table,
							    entry_idx,
							    port_entry_num);
	}

	if (matched_card_port->port_id != new_table[entry_idx].port_id)
		matched_card_port->port_id = new_table[entry_idx].port_id;

	matched_card_port->flg &= ~LEAPRAID_CARD_PORT_FLG_DIRTY;
	matched_card_port->phy_mask = new_table[entry_idx].phy_mask;
}

static void leapraid_update_card_port_after_reset(
		struct leapraid_adapter *adapter)
{
	struct leapraid_card_port *new_card_port_table;
	struct leapraid_card_port *matched_card_port;
	u8 port_entry_num;
	u8 nr_phys;
	int i;

	if (leapraid_get_adapter_phys(adapter, &nr_phys) || !nr_phys)
		return;

	if (!adapter->dev_topo.card.card_phy) {
		adapter->dev_topo.card.card_phy =
			kcalloc(nr_phys, sizeof(struct leapraid_card_phy),
				GFP_KERNEL);
		if (!adapter->dev_topo.card.card_phy)
			return;
	}

	adapter->dev_topo.card.phys_num = nr_phys;

	new_card_port_table = kcalloc(adapter->dev_topo.card.phys_num,
				      sizeof(struct leapraid_card_port),
				      GFP_KERNEL);
	if (!new_card_port_table)
		return;

	port_entry_num =
		leapraid_set_new_card_port_table_after_reset(
			adapter,
			new_card_port_table);
	if (!port_entry_num)
		goto out_free_port_table;

	list_for_each_entry(matched_card_port,
			    &adapter->dev_topo.card_port_list, list)
		matched_card_port->flg |= LEAPRAID_CARD_PORT_FLG_DIRTY;

	matched_card_port = NULL;
	for (i = 0; i < port_entry_num; i++)
		leapraid_update_existing_port(adapter,
					      new_card_port_table,
					      i, port_entry_num);
out_free_port_table:
	kfree(new_card_port_table);
}

static bool leapraid_is_valid_vphy(
		struct leapraid_adapter *adapter,
		struct leapraid_sas_io_unit_p0 *sas_io_unit_p0,
		int phy_index)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_phy_p0 phy_p0;
	u32 dev_info;

	if (sas_io_unit_p0->phy_info[phy_index].neg_link_rate >>
	    LEAPRAID_SAS_NEG_LINK_RATE_SHIFT <
	    LEAPRAID_SAS_NEG_LINK_RATE_1_5)
		return false;

	dev_info = le32_to_cpu(sas_io_unit_p0->phy_info[phy_index]
			       .controller_phy_dev_info);
	if (!(dev_info & LEAPRAID_DEVTYP_SEP))
		return false;

	cfgp1.phy_number = phy_index;
	if (leapraid_op_config_page(adapter, &phy_p0, cfgp1, cfgp2,
				    GET_PHY_PG0))
		return false;

	if (!(le32_to_cpu(phy_p0.phy_info) & LEAPRAID_SAS_PHYINFO_VPHY))
		return false;

	return true;
}

static void leapraid_update_vphy_binding(struct leapraid_adapter *adapter,
					 struct leapraid_card_port *card_port,
					 struct leapraid_vphy *vphy,
					 int phy_index, u8 may_new_port_id,
					 u64 attached_sas_addr)
{
	struct leapraid_card_port *may_new_card_port;
	struct leapraid_sas_dev *sas_dev;

	may_new_card_port = leapraid_get_port_by_id(adapter,
						    may_new_port_id,
						    true);
	if (!may_new_card_port) {
		may_new_card_port = kzalloc_obj(*may_new_card_port);
		if (!may_new_card_port)
			return;
		may_new_card_port->port_id = may_new_port_id;
		dev_err(&adapter->pdev->dev,
			"%s: New card port %p added, port=%d\n",
			__func__, may_new_card_port, may_new_port_id);
		list_add_tail(&may_new_card_port->list,
			      &adapter->dev_topo.card_port_list);
	}

	if (card_port != may_new_card_port) {
		if (!may_new_card_port->vphys_mask)
			INIT_LIST_HEAD(&may_new_card_port->vphys_list);
		may_new_card_port->vphys_mask |= BIT(phy_index);
		card_port->vphys_mask &= ~BIT(phy_index);
		list_move(&vphy->list, &may_new_card_port->vphys_list);

		sas_dev = leapraid_get_sas_dev_by_addr(adapter,
						       attached_sas_addr,
						       card_port);
		if (sas_dev) {
			sas_dev->card_port = may_new_card_port;
			leapraid_sdev_put(sas_dev);
		}
	}

	if (may_new_card_port->flg & LEAPRAID_CARD_PORT_FLG_DIRTY) {
		may_new_card_port->sas_address = 0;
		may_new_card_port->phy_mask = 0;
		may_new_card_port->flg &= ~LEAPRAID_CARD_PORT_FLG_DIRTY;
	}
	vphy->flg &= ~LEAPRAID_VPHY_FLG_DIRTY;
}

static void leapraid_update_vphys_after_reset(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_io_unit_p0 *sas_iounit_p0;
	struct leapraid_card_port *card_port, *card_port_next;
	struct leapraid_vphy *vphy, *vphy_next;
	u64 attached_sas_addr;
	u16 sz;
	u16 attached_hdl;
	bool found = false;
	u8 port_id;
	int i;

	list_for_each_entry_safe(card_port, card_port_next,
				 &adapter->dev_topo.card_port_list, list) {
		if (!card_port->vphys_mask)
			continue;

		list_for_each_entry_safe(vphy, vphy_next,
					 &card_port->vphys_list, list)
			vphy->flg |= LEAPRAID_VPHY_FLG_DIRTY;
	}

	sz = offsetof(struct leapraid_sas_io_unit_p0, phy_info) +
		(adapter->dev_topo.card.phys_num *
		 sizeof(struct leapraid_sas_io_unit0_phy_info));
	sas_iounit_p0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_p0)
		return;

	cfgp1.size = sz;
	if (leapraid_op_config_page(adapter, sas_iounit_p0, cfgp1, cfgp2,
				    GET_SAS_IOUNIT_PG0) != 0)
		goto out_free;

	for (i = 0; i < adapter->dev_topo.card.phys_num; i++) {
		if (!leapraid_is_valid_vphy(adapter, sas_iounit_p0, i))
			continue;

		attached_hdl =
			le16_to_cpu(sas_iounit_p0->phy_info[i]
					.attached_dev_hdl);
		if (leapraid_get_sas_address(adapter, attached_hdl,
					     &attached_sas_addr) != 0)
			continue;

		found = false;
		card_port = NULL;
		card_port_next = NULL;
		list_for_each_entry_safe(card_port, card_port_next,
					 &adapter->dev_topo.card_port_list,
					 list) {
			if (!card_port->vphys_mask)
				continue;

			list_for_each_entry_safe(vphy, vphy_next,
						 &card_port->vphys_list,
						 list) {
				if (!(vphy->flg & LEAPRAID_VPHY_FLG_DIRTY))
					continue;

				if (vphy->sas_address != attached_sas_addr)
					continue;

				if (!(vphy->phy_mask & BIT(i)))
					vphy->phy_mask = BIT(i);

				port_id = sas_iounit_p0->phy_info[i].port;

				leapraid_update_vphy_binding(adapter,
							     card_port,
							     vphy,
							     i,
							     port_id,
							     attached_sas_addr);

				found = true;
				break;
			}
			if (found)
				break;
		}
	}
out_free:
	kfree(sas_iounit_p0);
}

static void leapraid_mark_all_dev_deleted(struct leapraid_adapter *adapter)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, adapter->shost) {
		sdev_priv = sdev->hostdata;
		if (sdev_priv && sdev_priv->starget_priv)
			sdev_priv->starget_priv->deleted = 1;
	}
}

static void leapraid_free_enc_list(struct leapraid_adapter *adapter)
{
	struct leapraid_enc_node *enc_dev, *enc_dev_next;
	LIST_HEAD(free_list);
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
	list_splice_init(&adapter->dev_topo.enc_list, &free_list);
	spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);

	list_for_each_entry_safe(enc_dev, enc_dev_next, &free_list, list) {
		list_del(&enc_dev->list);
		kfree(enc_dev);
	}
}

static void leapraid_rebuild_enc_list_after_reset(
		struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_enc_node *enc_node;
	u16 enc_hdl;
	unsigned long flags;
	int rc;

	leapraid_free_enc_list(adapter);

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (enc_hdl = 0xFFFF; ; enc_hdl = le16_to_cpu(enc_node->pg0.enc_hdl)) {
		enc_node = kzalloc_obj(*enc_node);
		if (!enc_node)
			return;

		cfgp2.handle = enc_hdl;
		rc = leapraid_op_config_page(adapter, &enc_node->pg0, cfgp1,
					     cfgp2, GET_SAS_ENCLOSURE_PG0);
		if (rc) {
			kfree(enc_node);
			return;
		}

		spin_lock_irqsave(&adapter->dev_topo.enc_lock, flags);
		list_add_tail(&enc_node->list, &adapter->dev_topo.enc_list);
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, flags);
	}
}

static void leapraid_mark_resp_sas_dev(struct leapraid_adapter *adapter,
				       struct leapraid_sas_dev_p0 *sas_dev_p0)
{
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_enc_node *enc_node = NULL;
	struct leapraid_card_port *card_port;
	struct leapraid_sas_dev *sas_dev;
	struct scsi_target *starget;
	unsigned long flags;
	unsigned long enc_flags = 0;
	u16 enc_hdl;
	u64 enc_lid = 0;

	card_port = leapraid_get_port_by_id(adapter, sas_dev_p0->physical_port,
					    false);
	enc_hdl = le16_to_cpu(sas_dev_p0->enc_hdl);
	if (enc_hdl) {
		spin_lock_irqsave(&adapter->dev_topo.enc_lock, enc_flags);
		enc_node = leapraid_enc_find_by_hdl(adapter, enc_hdl);
		if (enc_node)
			enc_lid = le64_to_cpu(enc_node->pg0.enc_lid);
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, enc_flags);
		if (!enc_node)
			dev_info(&adapter->pdev->dev,
				 "enc hdl 0x%04x has no matched enc dev\n",
				 enc_hdl);
	}

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	list_for_each_entry(sas_dev, &adapter->dev_topo.sas_dev_list, list) {
		if (sas_dev->sas_addr != le64_to_cpu(sas_dev_p0->sas_address) ||
		    sas_dev->slot != le16_to_cpu(sas_dev_p0->slot) ||
		    sas_dev->card_port != card_port)
			continue;

		sas_dev->resp = 1;
		starget = sas_dev->starget;
		if (starget && starget->hostdata) {
			starget_priv = starget->hostdata;
			starget_priv->tm_busy = 0;
			starget_priv->deleted = 0;
		} else {
			starget_priv = NULL;
		}

		if (starget) {
			starget_printk(
				KERN_INFO, starget,
				"dev: hdl=0x%04x saddr=0x%016llx port_id=%d\n",
				sas_dev->hdl,
				(unsigned long long)sas_dev->sas_addr,
				sas_dev->card_port->port_id);
			if (sas_dev->enc_hdl != 0)
				starget_printk(
					KERN_INFO,
					starget,
					"enc info: enc_lid=0x%016llx slot=%d\n",
					(unsigned long long)sas_dev->enc_lid,
					sas_dev->slot);
		}

		if (le16_to_cpu(sas_dev_p0->flg) &
		    LEAPRAID_SAS_DEV_P0_FLG_ENC_LEVEL_VALID) {
			sas_dev->enc_level = sas_dev_p0->enc_level;
			memcpy(sas_dev->connector_name,
			       sas_dev_p0->connector_name,
			       LEAPRAID_SAS_DEV_P0_CON_NAME_LEN);
			sas_dev->connector_name[
				LEAPRAID_SAS_DEV_P0_CON_NAME_LEN] = '\0';
		} else {
			sas_dev->enc_level = 0;
			sas_dev->connector_name[0] = '\0';
		}

		sas_dev->enc_hdl = enc_hdl;
		sas_dev->enc_lid = enc_lid;

		if (sas_dev->hdl == le16_to_cpu(sas_dev_p0->dev_hdl))
			goto unlock;

		dev_info(&adapter->pdev->dev,
			 "hdl changed: 0x%04x -> 0x%04x\n",
			 sas_dev->hdl, sas_dev_p0->dev_hdl);
		sas_dev->hdl = le16_to_cpu(sas_dev_p0->dev_hdl);
		if (starget_priv)
			starget_priv->hdl = le16_to_cpu(sas_dev_p0->dev_hdl);

		goto unlock;
	}
unlock:
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
}

static void leapraid_search_resp_sas_dev(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_sas_dev_p0 sas_dev_p0;
	u32 device_info;

	if (list_empty(&adapter->dev_topo.sas_dev_list))
		return;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (cfgp2.handle = 0xFFFF;
	     !leapraid_op_config_page(adapter, &sas_dev_p0,
				      cfgp1, cfgp2, GET_SAS_DEVICE_PG0);
	     cfgp2.handle = le16_to_cpu(sas_dev_p0.dev_hdl)) {
		device_info = le32_to_cpu(sas_dev_p0.dev_info);
		if (!(leapraid_is_end_dev(device_info)))
			continue;

		leapraid_mark_resp_sas_dev(adapter, &sas_dev_p0);
	}
}

static void leapraid_mark_resp_raid_volume(struct leapraid_adapter *adapter,
					   u64 wwid, u16 hdl)
{
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_starget_priv *starget_priv = NULL;
	struct scsi_target *starget;
	unsigned long flags;
	u64 volume_wwid;
	u16 old_hdl;

	raid_volume = leapraid_raid_volume_find_by_wwid(adapter, wwid);
	if (!raid_volume)
		return;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	if (!raid_volume->starget) {
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
		leapraid_raid_volume_put(raid_volume);
		return;
	}

	starget = raid_volume->starget;
	if (starget->hostdata) {
		starget_priv = starget->hostdata;
		starget_priv->deleted = 0;
	}

	raid_volume->resp = 1;
	volume_wwid = raid_volume->wwid;
	old_hdl = raid_volume->hdl;
	if (old_hdl != hdl) {
		raid_volume->hdl = hdl;
		if (starget_priv)
			starget_priv->hdl = hdl;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	starget_printk(KERN_INFO, starget,
		       "raid volume: hdl=0x%04x, wwid=0x%016llx\n",
		       hdl, (unsigned long long)volume_wwid);
	if (old_hdl != hdl)
		dev_info(&adapter->pdev->dev,
			 "hdl changed: 0x%04x -> 0x%04x\n",
			 old_hdl, hdl);

	leapraid_raid_volume_put(raid_volume);
}

static void leapraid_search_resp_raid_volume(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_1 cfgp1_extra = {0};
	union cfg_param_2 cfgp2 = {0};
	union cfg_param_2 cfgp2_extra = {0};
	struct leapraid_raidvol_p1 raidvol_p1;
	struct leapraid_raidvol_p0 raidvol_p0;
	struct leapraid_raidpd_p0 raidpd_p0;
	unsigned long flags;
	u16 hdl;
	u8 phys_disk_num;
	bool is_empty;

	if (!adapter->adapter_attr.raid_support)
		return;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	is_empty = list_empty(&adapter->dev_topo.raid_volume_list);
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);
	if (is_empty)
		return;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (hdl = 0xFFFF, cfgp2.handle = hdl;
	     !leapraid_op_config_page(adapter, &raidvol_p1, cfgp1, cfgp2,
				      GET_RAID_VOLUME_PG1);
	     cfgp2.handle = hdl) {
		hdl = le16_to_cpu(raidvol_p1.dev_hdl);
		cfgp1_extra.size = sizeof(struct leapraid_raidvol_p0);
		cfgp2_extra.handle = hdl;
		if (leapraid_op_config_page(adapter, &raidvol_p0, cfgp1_extra,
					    cfgp2_extra, GET_RAID_VOLUME_PG0))
			continue;

		if (raidvol_p0.volume_state == LEAPRAID_VOL_STATE_OPTIMAL ||
		    raidvol_p0.volume_state == LEAPRAID_VOL_STATE_ONLINE ||
		    raidvol_p0.volume_state == LEAPRAID_VOL_STATE_DEGRADED)
			leapraid_mark_resp_raid_volume(
				adapter,
				le64_to_cpu(raidvol_p1.wwid),
				hdl);
	}

	memset(adapter->dev_topo.pd_hdls, 0, adapter->dev_topo.pd_hdls_sz);
	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (phys_disk_num = 0xFF, cfgp2.form_specific = phys_disk_num;
	     !leapraid_op_config_page(adapter, &raidpd_p0, cfgp1, cfgp2,
				      GET_PHY_DISK_PG0);
	     cfgp2.form_specific = phys_disk_num) {
		phys_disk_num = raidpd_p0.phys_disk_num;
		hdl = le16_to_cpu(raidpd_p0.dev_hdl);
		if (!hdl ||
		    hdl > adapter->adapter_attr.features.max_dev_handle)
			dev_warn(&adapter->pdev->dev,
				 "%s: Invalid device handle\n", __func__);
		else
			set_bit(hdl, adapter->dev_topo.pd_hdls);
	}
}

static void leapraid_mark_resp_exp(struct leapraid_adapter *adapter,
				   struct leapraid_exp_p0 *exp_pg0)
{
	struct leapraid_enc_node *enc_node = NULL;
	struct leapraid_topo_node *topo_node_exp;
	u16 enc_hdl = le16_to_cpu(exp_pg0->enc_hdl);
	u64 sas_address = le64_to_cpu(exp_pg0->sas_address);
	u16 hdl = le16_to_cpu(exp_pg0->dev_hdl);
	u8 port_id = exp_pg0->physical_port;
	struct leapraid_card_port *card_port = leapraid_get_port_by_id(adapter,
								       port_id,
								       false);
	unsigned long flags;
	unsigned long enc_flags = 0;
	u64 enc_lid = 0;
	int i;

	if (enc_hdl) {
		spin_lock_irqsave(&adapter->dev_topo.enc_lock, enc_flags);
		enc_node = leapraid_enc_find_by_hdl(adapter, enc_hdl);
		if (enc_node)
			enc_lid = le64_to_cpu(enc_node->pg0.enc_lid);
		spin_unlock_irqrestore(&adapter->dev_topo.enc_lock, enc_flags);
	}

	spin_lock_irqsave(&adapter->dev_topo.topo_node_lock, flags);
	list_for_each_entry(topo_node_exp, &adapter->dev_topo.exp_list, list) {
		if (topo_node_exp->sas_address != sas_address ||
		    topo_node_exp->card_port != card_port)
			continue;

		topo_node_exp->resp = 1;
		topo_node_exp->enc_hdl = enc_hdl;
		topo_node_exp->enc_lid = enc_lid;
		if (topo_node_exp->hdl == hdl)
			goto unlock;

		dev_info(&adapter->pdev->dev,
			 "hdl changed: 0x%04x -> 0x%04x\n",
			 topo_node_exp->hdl, hdl);
		topo_node_exp->hdl = hdl;
		for (i = 0; i < topo_node_exp->phys_num; i++)
			topo_node_exp->card_phy[i].hdl = hdl;
		goto unlock;
	}
unlock:
	spin_unlock_irqrestore(&adapter->dev_topo.topo_node_lock, flags);
}

static void leapraid_search_resp_exp(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	struct leapraid_exp_p0 exp_p0;
	u64 sas_address;
	u16 hdl;
	u8 port;

	if (list_empty(&adapter->dev_topo.exp_list))
		return;

	cfgp1.form = LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP;
	for (hdl = 0xFFFF, cfgp2.handle = hdl;
	     !leapraid_op_config_page(adapter, &exp_p0, cfgp1, cfgp2,
				      GET_SAS_EXPANDER_PG0);
	     cfgp2.handle = hdl) {
		hdl = le16_to_cpu(exp_p0.dev_hdl);
		sas_address = le64_to_cpu(exp_p0.sas_address);
		port = exp_p0.physical_port;

		dev_dbg(&adapter->pdev->dev,
			"exp detected: hdl=0x%04x, sas=0x%016llx, port=%u",
			hdl, (unsigned long long)sas_address,
			adapter->adapter_attr.enable_mp ? port :
				LEAPRAID_DISABLE_MP_PORT_ID);
		leapraid_mark_resp_exp(adapter, &exp_p0);
	}
}

void leapraid_wait_cmds_done(struct leapraid_adapter *adapter)
{
	struct leapraid_io_req_tracker *io_req_tracker;
	unsigned long flags;
	u16 i;

	adapter->reset_desc.pending_io_cnt = 0;
	if (!leapraid_pci_active(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s %s: PCI error, device reset or unplugged!\n",
			adapter->adapter_attr.name, __func__);
		return;
	}

	if (leapraid_get_adapter_state(adapter) != LEAPRAID_DB_OPERATIONAL)
		return;

	spin_lock_irqsave(&adapter->dynamic_task_desc.task_lock, flags);
	for (i = 1; i <= adapter->shost->can_queue; i++) {
		io_req_tracker = leapraid_get_io_tracker_from_taskid(adapter,
								     i);
		if (io_req_tracker && io_req_tracker->taskid != 0 &&
		    io_req_tracker->scmd)
			adapter->reset_desc.pending_io_cnt++;
	}
	spin_unlock_irqrestore(&adapter->dynamic_task_desc.task_lock, flags);

	if (!adapter->reset_desc.pending_io_cnt)
		return;

	wait_event_timeout(adapter->reset_desc.reset_wait_queue,
			   adapter->reset_desc.pending_io_cnt == 0,
			   LEAPRAID_IO_CMD_TIMEOUT * HZ);
}

int leapraid_hard_reset_handler(struct leapraid_adapter *adapter,
				enum reset_type type)
{
	unsigned long flags;
	bool wake = false;
	int rc;

	mutex_lock(&adapter->reset_desc.adapter_reset_mutex);

	if (adapter->access_ctrl.shost_recover_async) {
		rc = adapter->reset_desc.adapter_reset_results;
		dev_info(&adapter->pdev->dev,
			 "Skip nested hard reset, async evt running, rc=%d\n",
			 rc);
		mutex_unlock(&adapter->reset_desc.adapter_reset_mutex);
		return rc;
	}

	if (!leapraid_pci_active(adapter)) {
		if (leapraid_pci_removed(adapter)) {
			dev_info(&adapter->pdev->dev,
				 "pci_dev removed, pause poll, clean cmds\n");
			leapraid_mq_polling_pause(adapter);
			leapraid_clean_active_scsi_cmds(adapter);
			leapraid_mq_polling_resume(adapter);
		}
		dev_err(&adapter->pdev->dev, "PCIe unavailable!\n");
		mutex_unlock(&adapter->reset_desc.adapter_reset_mutex);
		return -ENXIO;
	}

	dev_info(&adapter->pdev->dev, "Starting hard reset\n");

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	adapter->access_ctrl.shost_recovering = 1;
	adapter->access_ctrl.shost_recover_async = 1;
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);

	leapraid_wait_cmds_done(adapter);
	leapraid_mask_int(adapter);
	leapraid_mq_polling_pause(adapter);
	rc = leapraid_make_adapter_ready(adapter, type);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"Failed to make adapter ready, rc=%d\n", rc);
		goto out_cleanup;
	}

	rc = leapraid_fw_log_init(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Firmware log init failed\n");
		goto out_cleanup;
	}

	leapraid_clean_active_cmds(adapter);
	if (adapter->scan_dev_desc.driver_loading &&
	    adapter->scan_dev_desc.scan_dev_failed) {
		dev_err(&adapter->pdev->dev,
			"Previous device scan failed or driver loading\n");
		adapter->access_ctrl.host_removing = 1;
		rc = -EFAULT;
		goto out_cleanup;
	}

	rc = leapraid_make_adapter_available(adapter);
	if (!rc) {
		dev_info(&adapter->pdev->dev,
			 "Adapter is now available, rebuilding topology\n");
		if (adapter->adapter_attr.enable_mp) {
			leapraid_update_card_port_after_reset(adapter);
			leapraid_update_vphys_after_reset(adapter);
		}
		leapraid_mark_all_dev_deleted(adapter);
		leapraid_rebuild_enc_list_after_reset(adapter);
		leapraid_search_resp_sas_dev(adapter);
		leapraid_search_resp_raid_volume(adapter);
		leapraid_search_resp_exp(adapter);
		leapraid_hardreset_barrier(adapter);
	}
out_cleanup:
	if (rc)
		dev_err(&adapter->pdev->dev, "Hard reset failed\n");

	spin_lock_irqsave(&adapter->reset_desc.adapter_reset_lock, flags);
	adapter->reset_desc.adapter_reset_results = rc;
	adapter->access_ctrl.shost_recovering = 0;
	wake_up(&adapter->access_ctrl.recovery_waitq);
	if (rc) {
		adapter->access_ctrl.shost_recover_async = 0;
		wake = true;
	}
	spin_unlock_irqrestore(&adapter->reset_desc.adapter_reset_lock, flags);
	if (wake)
		wake_up(&adapter->access_ctrl.shost_recover_wq);

	adapter->reset_desc.reset_cnt++;
	mutex_unlock(&adapter->reset_desc.adapter_reset_mutex);

	if (rc)
		leapraid_clean_active_scsi_cmds(adapter);
	leapraid_mq_polling_resume(adapter);

	return rc;
}

static int leapraid_get_adapter_features(struct leapraid_adapter *adapter)
{
	struct leapraid_adapter_features_req leap_mpi_req;
	struct leapraid_adapter_features_rep leap_mpi_rep;
	u8 fw_major, fw_minor, fw_build, fw_release;
	u32 req_sz;
	u32 rep_sz;
	u32 db;
	int r;

	db = leapraid_readl(&adapter->iomem_base->db);
	if (db & LEAPRAID_DB_USED ||
	    (db & LEAPRAID_DB_MASK) == LEAPRAID_DB_FAULT) {
		dev_err(&adapter->pdev->dev,
			"%s: Doorbell used or fault\n", __func__);
		return -EFAULT;
	}

	if ((db & LEAPRAID_DB_MASK) != LEAPRAID_DB_READY &&
	    (db & LEAPRAID_DB_MASK) != LEAPRAID_DB_OPERATIONAL &&
	    !leapraid_wait_adapter_ready(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s: adapter not ready\n", __func__);
		return -EFAULT;
	}

	req_sz = sizeof(struct leapraid_adapter_features_req);
	rep_sz = sizeof(struct leapraid_adapter_features_rep);
	memset(&leap_mpi_req, 0, req_sz);
	memset(&leap_mpi_rep, 0, rep_sz);
	leap_mpi_req.func = LEAPRAID_FUNC_GET_ADAPTER_FEATURES;
	r = leapraid_handshake_func(adapter,
				    req_sz,
				    (u32 *)&leap_mpi_req,
				    rep_sz,
				    (u16 *)&leap_mpi_rep);
	if (r) {
		dev_err(&adapter->pdev->dev,
			"%s %s: Handshake failed, r=%d\n",
			adapter->adapter_attr.name, __func__, r);
		return r;
	}

	memset(&adapter->adapter_attr.features, 0,
	       sizeof(struct leapraid_adapter_features));
	adapter->adapter_attr.features.req_slot =
		le16_to_cpu(leap_mpi_rep.req_slot);
	adapter->adapter_attr.features.hp_slot =
		le16_to_cpu(leap_mpi_rep.hp_slot);
	adapter->adapter_attr.features.adapter_caps =
		le32_to_cpu(leap_mpi_rep.adapter_caps);
	adapter->adapter_attr.features.max_msix_vectors =
		leap_mpi_rep.max_msix_vectors;
	adapter->adapter_attr.features.max_volumes =
		leap_mpi_rep.max_volumes;
	if (!adapter->adapter_attr.features.max_volumes)
		adapter->adapter_attr.features.max_volumes =
			LEAPRAID_MAX_VOLUMES_DEFAULT;
	adapter->adapter_attr.features.max_dev_handle =
		le16_to_cpu(leap_mpi_rep.max_dev_hdl);
	if (!adapter->adapter_attr.features.max_dev_handle)
		adapter->adapter_attr.features.max_dev_handle =
			LEAPRAID_MAX_DEV_HANDLE_DEFAULT;
	adapter->adapter_attr.features.min_dev_handle =
		le16_to_cpu(leap_mpi_rep.min_dev_hdl);
	if (adapter->adapter_attr.features.adapter_caps &
	    LEAPRAID_ADAPTER_FEATURES_CAP_INTEGRATED_RAID)
		adapter->adapter_attr.raid_support = 1;
	adapter->adapter_attr.wideport_max_queue_depth =
		le16_to_cpu(leap_mpi_rep.sas_wide_max_qdepth) ?
		le16_to_cpu(leap_mpi_rep.sas_wide_max_qdepth) :
		LEAPRAID_SAS_QUEUE_DEPTH;
	adapter->adapter_attr.narrowport_max_queue_depth =
		le16_to_cpu(leap_mpi_rep.sas_narrow_max_qdepth) ?
		le16_to_cpu(leap_mpi_rep.sas_narrow_max_qdepth) :
		LEAPRAID_SAS_QUEUE_DEPTH;
	adapter->adapter_attr.sata_max_queue_depth =
		leap_mpi_rep.sata_max_qdepth ?
		leap_mpi_rep.sata_max_qdepth :
		LEAPRAID_SATA_QUEUE_DEPTH;
	adapter->adapter_attr.raid_volume_max_queue_depth =
		LEAPRAID_RAID_QUEUE_DEPTH;
	dev_info(&adapter->pdev->dev,
		 "max: wp qd=%d, np qd=%d, SATA qd=%d, raid qd=%d\n",
		 adapter->adapter_attr.wideport_max_queue_depth,
		 adapter->adapter_attr.narrowport_max_queue_depth,
		 adapter->adapter_attr.sata_max_queue_depth,
		 adapter->adapter_attr.raid_volume_max_queue_depth);
	if (WARN_ON(!(adapter->adapter_attr.features.adapter_caps &
		      LEAPRAID_ADAPTER_FEATURES_CAP_ATOMIC_REQ)))
		return -EFAULT;
	adapter->adapter_attr.features.fw_version =
		le32_to_cpu(leap_mpi_rep.fw_version);

	fw_major = (adapter->adapter_attr.features.fw_version >>
		    LEAPRAID_VER_MAJOR_SHIFT) & LEAPRAID_VER_MASK;
	fw_minor = (adapter->adapter_attr.features.fw_version >>
		    LEAPRAID_VER_MINOR_SHIFT) & LEAPRAID_VER_MASK;
	fw_build = (adapter->adapter_attr.features.fw_version >>
		    LEAPRAID_VER_BUILD_SHIFT)  & LEAPRAID_VER_MASK;
	fw_release =
		adapter->adapter_attr.features.fw_version & LEAPRAID_VER_MASK;

	dev_info(&adapter->pdev->dev,
		 "Firmware version: %u.%u.%u.%u (0x%08x)\n",
		 fw_major, fw_minor, fw_build, fw_release,
		 adapter->adapter_attr.features.fw_version);

	adapter->adapter_attr.features.msg_ver =
		le16_to_cpu(leap_mpi_rep.msg_ver);
	adapter->adapter_attr.features.product_id =
		le16_to_cpu(leap_mpi_rep.product_id);
	dev_info(&adapter->pdev->dev,
		 "message version: 0x%x, product id 0x%x\n",
		 adapter->adapter_attr.features.msg_ver,
		 adapter->adapter_attr.features.product_id);

	if (adapter->adapter_attr.features.msg_ver < 0x1000) {
		dev_err(&adapter->pdev->dev, "Device not supported\n");
		return -EFAULT;
	}
	adapter->shost->max_id = LEAPRAID_INVALID_INITIAL_VALUE;

	return 0;
}

static inline void leapraid_disable_pcie(struct leapraid_adapter *adapter)
{
	mutex_lock(&adapter->access_ctrl.pci_access_lock);
	if (adapter->iomem_base) {
		iounmap(adapter->iomem_base);
		adapter->iomem_base = NULL;
	}
	if (pci_is_enabled(adapter->pdev)) {
		pci_release_regions(adapter->pdev);
		pci_disable_device(adapter->pdev);
	}
	mutex_unlock(&adapter->access_ctrl.pci_access_lock);
}

static int leapraid_enable_pcie(struct leapraid_adapter *adapter)
{
	u64 dma_mask;
	int rc;

	rc = pci_enable_device(adapter->pdev);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Failed to enable PCI device\n");
		return rc;
	}

	rc = pci_request_regions(adapter->pdev, LEAPRAID_DRIVER_NAME);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"Failed to obtain PCI resources\n");
		return rc;
	}

	if (sizeof(dma_addr_t) > 4) {
		dma_mask = DMA_BIT_MASK(DMA_64_BITS);
		adapter->adapter_attr.use_32_dma_mask = 0;
	} else {
		dma_mask = DMA_BIT_MASK(DMA_32_BITS);
		adapter->adapter_attr.use_32_dma_mask = 1;
	}

	rc = dma_set_mask_and_coherent(&adapter->pdev->dev, dma_mask);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"Failed to set %lld DMA mask\n", dma_mask);
		return rc;
	}
	adapter->iomem_base = ioremap(pci_resource_start(adapter->pdev, 0),
				      sizeof(struct leapraid_reg_base));
	if (!adapter->iomem_base) {
		dev_err(&adapter->pdev->dev,
			"Failed to map memory for controller registers\n");
		return -ENOMEM;
	}

	pci_set_master(adapter->pdev);

	return 0;
}

static void leapraid_cpus_on_irq(struct leapraid_adapter *adapter)
{
	struct leapraid_int_rq *int_rq;
	unsigned int i, base_group, this_group;
	unsigned int cpu, nr_cpus, total_msix, index;

	total_msix = adapter->notification_desc.iopoll_qdex;
	nr_cpus = num_online_cpus();

	if (!nr_cpus || !total_msix)
		return;
	base_group = nr_cpus / total_msix;

	cpu = cpumask_first(cpu_online_mask);
	for (index = 0; index < adapter->notification_desc.iopoll_qdex;
	     index++) {
		int_rq = &adapter->notification_desc.int_rqs[index];

		if (cpu >= adapter->notification_desc.msix_cpu_map_sz)
			break;

		this_group = base_group +
				(index < (nr_cpus % total_msix) ? 1 : 0);

		for (i = 0 ; i < this_group ; i++) {
			if (cpu >= adapter->notification_desc.msix_cpu_map_sz)
				break;

			adapter->notification_desc.msix_cpu_map[cpu] =
				int_rq->rq.msix_idx;
			cpu = cpumask_next(cpu, cpu_online_mask);
		}
	}
}

static void leapraid_map_msix_to_cpu(struct leapraid_adapter *adapter)
{
	struct leapraid_int_rq *int_rq;
	const cpumask_t *affinity_mask;
	int cpu;
	u32 i;

	if (!adapter->adapter_attr.rq_cnt)
		return;

	for (i = 0; i < adapter->notification_desc.iopoll_qdex; i++) {
		int_rq = &adapter->notification_desc.int_rqs[i];
		affinity_mask = pci_irq_get_affinity(adapter->pdev,
						     int_rq->rq.msix_idx);
		if (!affinity_mask) {
			dev_warn(&adapter->pdev->dev,
				 "%s: IRQ affinity NULL, msix_idx=%d\n",
				 __func__, int_rq->rq.msix_idx);
			goto out_apply_irq_affinity;
		}

		for_each_cpu_and(cpu, affinity_mask, cpu_online_mask) {
			if (cpu >= adapter->notification_desc.msix_cpu_map_sz)
				continue;

			adapter->notification_desc.msix_cpu_map[cpu] =
				int_rq->rq.msix_idx;
		}
	}
	return;
out_apply_irq_affinity:
	leapraid_cpus_on_irq(adapter);
}

static int leapraid_alloc_msix_cpu_map(struct leapraid_adapter *adapter)
{
	adapter->notification_desc.msix_cpu_map_sz = nr_cpu_ids;
	adapter->notification_desc.msix_cpu_map =
		kzalloc(adapter->notification_desc.msix_cpu_map_sz,
			GFP_KERNEL);
	if (!adapter->notification_desc.msix_cpu_map)
		return -ENOMEM;

	return 0;
}

static void leapraid_configure_reply_queue_affinity(
		struct leapraid_adapter *adapter)
{
	if (!adapter || !adapter->notification_desc.msix_enable)
		return;

	leapraid_map_msix_to_cpu(adapter);
}

static void leapraid_free_irq(struct leapraid_adapter *adapter)
{
	struct leapraid_int_rq *int_rq;
	unsigned int i;
	int irq;

	for (i = 0; adapter->notification_desc.int_rqs &&
	     i < adapter->notification_desc.int_rqs_allocated; i++) {
		int_rq = &adapter->notification_desc.int_rqs[i];
		if (!int_rq)
			continue;

		irq = pci_irq_vector(adapter->pdev, int_rq->rq.msix_idx);
		irq_set_affinity_hint(irq, NULL);
		free_irq(irq, &int_rq->rq);
	}
	adapter->notification_desc.int_rqs_allocated = 0;

	if (adapter->notification_desc.irq_vectors_allocated) {
		pci_free_irq_vectors(adapter->pdev);
		adapter->notification_desc.irq_vectors_allocated = 0;
	}

	adapter->notification_desc.msix_enable = 0;

	kfree(adapter->notification_desc.blk_mq_poll_rqs);
	adapter->notification_desc.blk_mq_poll_rqs = NULL;

	kfree(adapter->notification_desc.int_rqs);
	adapter->notification_desc.int_rqs = NULL;

	kfree(adapter->notification_desc.msix_cpu_map);
	adapter->notification_desc.msix_cpu_map = NULL;
	adapter->notification_desc.msix_cpu_map_sz = 0;
	adapter->notification_desc.iopoll_qdex = 0;
	adapter->notification_desc.iopoll_qcnt = 0;
}

static int leapraid_setup_irqs(struct leapraid_adapter *adapter)
{
	int irq_mode = adapter->notification_desc.irq_mode;
	unsigned int i;
	int rc = 0;

	if (irq_mode == LEAPRAID_INTERRUPT_MODE_MSIX) {
		rc = pci_alloc_irq_vectors_affinity(
			adapter->pdev,
			adapter->notification_desc.iopoll_qdex,
			adapter->notification_desc.iopoll_qdex,
			PCI_IRQ_MSIX | PCI_IRQ_AFFINITY, NULL);
		if (rc < 0) {
			dev_err(&adapter->pdev->dev,
				"%d MSI/MSIX vectors allocated failed!\n",
				adapter->notification_desc.iopoll_qdex);
			return rc;
		}

		adapter->notification_desc.irq_vectors_allocated = 1;
	}

	for (i = 0; i < adapter->notification_desc.iopoll_qdex; i++) {
		adapter->notification_desc.int_rqs[i].rq.adapter = adapter;
		adapter->notification_desc.int_rqs[i].rq.msix_idx = i;
		atomic_set(&adapter->notification_desc.int_rqs[i].rq.busy, 0);
		if (irq_mode == LEAPRAID_INTERRUPT_MODE_MSIX)
			snprintf(adapter->notification_desc.int_rqs[i].rq.name,
				 LEAPRAID_NAME_LENGTH, "%s%u-MSIx%u",
				 LEAPRAID_DRIVER_NAME,
				 adapter->adapter_attr.id, i);
		else if (irq_mode == LEAPRAID_INTERRUPT_MODE_MSI)
			snprintf(adapter->notification_desc.int_rqs[i].rq.name,
				 LEAPRAID_NAME_LENGTH, "%s%u-MSI%u",
				 LEAPRAID_DRIVER_NAME,
				 adapter->adapter_attr.id, i);

		rc = request_irq(pci_irq_vector(adapter->pdev, i),
				 leapraid_irq_handler,
				 IRQF_SHARED,
				 adapter->notification_desc.int_rqs[i].rq.name,
				 &adapter->notification_desc.int_rqs[i].rq);
		if (rc) {
			dev_err(&adapter->pdev->dev,
				"MSI/MSIx: request_irq %s failed!\n",
				adapter->notification_desc.int_rqs[i].rq.name);
			return rc;
		}
		adapter->notification_desc.int_rqs_allocated++;
	}

	return 0;
}

static int leapraid_setup_legacy_int(struct leapraid_adapter *adapter)
{
	int rc;

	adapter->notification_desc.int_rqs[0].rq.adapter = adapter;
	adapter->notification_desc.int_rqs[0].rq.msix_idx = 0;
	atomic_set(&adapter->notification_desc.int_rqs[0].rq.busy, 0);
	snprintf(adapter->notification_desc.int_rqs[0].rq.name,
		 LEAPRAID_NAME_LENGTH, "%s%d-LegacyInt",
		 LEAPRAID_DRIVER_NAME, adapter->adapter_attr.id);

	rc = pci_alloc_irq_vectors_affinity(
		adapter->pdev,
		adapter->notification_desc.iopoll_qdex,
		adapter->notification_desc.iopoll_qdex,
		PCI_IRQ_INTX | PCI_IRQ_AFFINITY,
		NULL);
	if (rc < 0) {
		dev_err(&adapter->pdev->dev,
			"Legacy irq allocated failed!\n");
		return rc;
	}

	adapter->notification_desc.irq_vectors_allocated = 1;
	adapter->notification_desc.irq_mode = LEAPRAID_INTERRUPT_MODE_LEGACY;
	rc = request_irq(pci_irq_vector(adapter->pdev, 0),
			 leapraid_irq_handler,
			 IRQF_SHARED,
			 adapter->notification_desc.int_rqs[0].rq.name,
			 &adapter->notification_desc.int_rqs[0].rq);
	if (rc) {
		irq_set_affinity_hint(pci_irq_vector(adapter->pdev, 0), NULL);
		pci_free_irq_vectors(adapter->pdev);
		adapter->notification_desc.irq_vectors_allocated = 0;
		dev_err(&adapter->pdev->dev,
			"Legacy Int: request_irq %s failed!\n",
			adapter->notification_desc.int_rqs[0].rq.name);
		return -EBUSY;
	}
	adapter->notification_desc.int_rqs_allocated = 1;
	return rc;
}

static int leapraid_set_legacy_int(struct leapraid_adapter *adapter)
{
	int rc;

	rc = leapraid_alloc_msix_cpu_map(adapter);
	if (rc)
		return rc;

	adapter->adapter_attr.rq_cnt = 1;
	adapter->notification_desc.iopoll_qdex =
		adapter->adapter_attr.rq_cnt;
	adapter->notification_desc.iopoll_qcnt = 0;
	dev_info(&adapter->pdev->dev,
		 "Legacy Intr: req queue cnt=%d intr=%d/poll=%d rep queues!\n",
		 adapter->adapter_attr.rq_cnt,
		 adapter->notification_desc.iopoll_qdex,
		 adapter->notification_desc.iopoll_qcnt);
	adapter->notification_desc.int_rqs =
		kcalloc(adapter->notification_desc.iopoll_qdex,
			sizeof(struct leapraid_int_rq), GFP_KERNEL);
	if (!adapter->notification_desc.int_rqs)
		return -ENOMEM;

	return leapraid_setup_legacy_int(adapter);
}

static int leapraid_set_msix(struct leapraid_adapter *adapter)
{
	int iopoll_qcnt = 0;
	unsigned int i;
	int rc, msix_cnt;

	msix_cnt = pci_msix_vec_count(adapter->pdev);
	if (msix_cnt <= 0 ||
	    adapter->adapter_attr.features.max_msix_vectors == 0) {
		dev_info(&adapter->pdev->dev, "MSIX unsupported!\n");
		return -EOPNOTSUPP;
	}

	msix_cnt = min_t(int, msix_cnt,
			 adapter->adapter_attr.features.max_msix_vectors);

	if (reset_devices)
		adapter->adapter_attr.rq_cnt = 1;
	else
		adapter->adapter_attr.rq_cnt = min_t(int,
						     num_online_cpus(),
						     msix_cnt);

	if (max_msix_vectors > 0)
		adapter->adapter_attr.rq_cnt = min_t(
			int, max_msix_vectors, adapter->adapter_attr.rq_cnt);

	if (adapter->adapter_attr.rq_cnt <= 1)
		adapter->shost->host_tagset = 0;
	if (adapter->shost->host_tagset) {
		iopoll_qcnt = poll_queues;
		if (iopoll_qcnt >= adapter->adapter_attr.rq_cnt)
			iopoll_qcnt = 0;
	}
	if (iopoll_qcnt) {
		adapter->notification_desc.blk_mq_poll_rqs =
			kcalloc(iopoll_qcnt,
				sizeof(struct leapraid_blk_mq_poll_rq),
				GFP_KERNEL);
		if (!adapter->notification_desc.blk_mq_poll_rqs)
			return -ENOMEM;
		adapter->adapter_attr.rq_cnt =
			min(adapter->adapter_attr.rq_cnt + iopoll_qcnt,
			    msix_cnt);
	}

	adapter->notification_desc.iopoll_qdex =
		adapter->adapter_attr.rq_cnt - iopoll_qcnt;

	adapter->notification_desc.iopoll_qcnt = iopoll_qcnt;
	dev_info(&adapter->pdev->dev,
		 "MSIx: req queue cnt=%d, intr=%d/poll=%d rep queues!\n",
		 adapter->adapter_attr.rq_cnt,
		 adapter->notification_desc.iopoll_qdex,
		 adapter->notification_desc.iopoll_qcnt);

	adapter->notification_desc.int_rqs =
		kcalloc(adapter->notification_desc.iopoll_qdex,
			sizeof(struct leapraid_int_rq), GFP_KERNEL);
	if (!adapter->notification_desc.int_rqs)
		return -ENOMEM;

	for (i = 0; i < adapter->notification_desc.iopoll_qcnt; i++) {
		adapter->notification_desc.blk_mq_poll_rqs[i].rq.adapter =
			adapter;
		adapter->notification_desc.blk_mq_poll_rqs[i].rq.msix_idx =
			i + adapter->notification_desc.iopoll_qdex;
		atomic_set(
			&adapter->notification_desc.blk_mq_poll_rqs[i].rq.busy,
			0);
		snprintf(adapter->notification_desc.blk_mq_poll_rqs[i].rq.name,
			 LEAPRAID_NAME_LENGTH,
			 "%s%u-MQ-Poll%u", LEAPRAID_DRIVER_NAME,
			 adapter->adapter_attr.id, i);
		atomic_set(&adapter->notification_desc.blk_mq_poll_rqs[i].busy,
			   0);
		atomic_set(&adapter->notification_desc.blk_mq_poll_rqs[i].pause,
			   0);
	}

	rc = leapraid_alloc_msix_cpu_map(adapter);
	if (rc)
		return rc;

	adapter->notification_desc.irq_mode = LEAPRAID_INTERRUPT_MODE_MSIX;
	adapter->notification_desc.msix_enable = 1;
	rc = leapraid_setup_irqs(adapter);
	if (rc) {
		leapraid_free_irq(adapter);
		adapter->notification_desc.msix_enable = 0;
		return rc;
	}

	return 0;
}

static int leapraid_set_msi(struct leapraid_adapter *adapter)
{
	int iopoll_qcnt = 0;
	unsigned int i;
	int rc, msi_cnt;

	msi_cnt = pci_msi_vec_count(adapter->pdev);
	if (msi_cnt <= 0 ||
	    adapter->adapter_attr.features.max_msix_vectors == 0) {
		dev_info(&adapter->pdev->dev, "MSI unsupported!\n");
		return -EOPNOTSUPP;
	}

	msi_cnt = min_t(int, msi_cnt,
			adapter->adapter_attr.features.max_msix_vectors);

	if (reset_devices)
		adapter->adapter_attr.rq_cnt = 1;
	else
		adapter->adapter_attr.rq_cnt = min_t(int,
						     num_online_cpus(),
						     msi_cnt);

	if (max_msix_vectors > 0)
		adapter->adapter_attr.rq_cnt = min_t(
			int, max_msix_vectors, adapter->adapter_attr.rq_cnt);

	if (adapter->adapter_attr.rq_cnt <= 1)
		adapter->shost->host_tagset = 0;
	if (adapter->shost->host_tagset) {
		iopoll_qcnt = poll_queues;
		if (iopoll_qcnt >= adapter->adapter_attr.rq_cnt)
			iopoll_qcnt = 0;
	}

	if (iopoll_qcnt) {
		adapter->notification_desc.blk_mq_poll_rqs =
			kcalloc(iopoll_qcnt,
				sizeof(struct leapraid_blk_mq_poll_rq),
				GFP_KERNEL);
		if (!adapter->notification_desc.blk_mq_poll_rqs)
			return -ENOMEM;

		adapter->adapter_attr.rq_cnt =
			 min(adapter->adapter_attr.rq_cnt + iopoll_qcnt,
			     msi_cnt);
	}

	adapter->notification_desc.iopoll_qdex =
		adapter->adapter_attr.rq_cnt - iopoll_qcnt;
	rc = pci_alloc_irq_vectors_affinity(
		adapter->pdev,
		1,
		adapter->notification_desc.iopoll_qdex,
		PCI_IRQ_MSI | PCI_IRQ_AFFINITY, NULL);
	if (rc < 0) {
		dev_err(&adapter->pdev->dev,
			"%d MSI vectors allocated failed!\n",
			adapter->notification_desc.iopoll_qdex);
		leapraid_free_irq(adapter);
		return rc;
	}
	adapter->notification_desc.irq_vectors_allocated = 1;
	if (rc != adapter->notification_desc.iopoll_qdex) {
		adapter->notification_desc.iopoll_qdex = rc;
		adapter->adapter_attr.rq_cnt =
			adapter->notification_desc.iopoll_qdex + iopoll_qcnt;
	}
	adapter->notification_desc.iopoll_qcnt = iopoll_qcnt;
	dev_info(&adapter->pdev->dev,
		 "MSI: req queue cnt=%d, intr=%d/poll=%d rep queues!\n",
		 adapter->adapter_attr.rq_cnt,
		 adapter->notification_desc.iopoll_qdex,
		 adapter->notification_desc.iopoll_qcnt);

	adapter->notification_desc.int_rqs =
		kcalloc(adapter->notification_desc.iopoll_qdex,
			sizeof(struct leapraid_int_rq),
			GFP_KERNEL);
	if (!adapter->notification_desc.int_rqs)
		return -ENOMEM;

	for (i = 0; i < adapter->notification_desc.iopoll_qcnt; i++) {
		adapter->notification_desc.blk_mq_poll_rqs[i].rq.adapter =
			adapter;
		adapter->notification_desc.blk_mq_poll_rqs[i].rq.msix_idx =
			i + adapter->notification_desc.iopoll_qdex;
		atomic_set(
			&adapter->notification_desc.blk_mq_poll_rqs[i].rq.busy,
			0);
		snprintf(adapter->notification_desc.blk_mq_poll_rqs[i].rq.name,
			 LEAPRAID_NAME_LENGTH,
			 "%s%u-MQ-Poll%u", LEAPRAID_DRIVER_NAME,
			 adapter->adapter_attr.id, i);
		atomic_set(
			&adapter->notification_desc.blk_mq_poll_rqs[i].busy,
			0);
		atomic_set(
			&adapter->notification_desc.blk_mq_poll_rqs[i].pause,
			0);
	}

	rc = leapraid_alloc_msix_cpu_map(adapter);
	if (rc)
		return rc;

	adapter->notification_desc.irq_mode = LEAPRAID_INTERRUPT_MODE_MSI;
	adapter->notification_desc.msix_enable = 1;
	rc = leapraid_setup_irqs(adapter);
	if (rc) {
		leapraid_free_irq(adapter);
		adapter->notification_desc.msix_enable = 0;
		return rc;
	}

	return 0;
}

static int leapraid_set_notification_auto(struct leapraid_adapter *adapter)
{
	int rc;

	rc = leapraid_set_msix(adapter);
	if (!rc)
		return 0;

	leapraid_free_irq(adapter);
	dev_info(&adapter->pdev->dev,
		 "MSI-X setup failed (%d), trying MSI\n", rc);

	rc = leapraid_set_msi(adapter);
	if (!rc)
		return 0;

	leapraid_free_irq(adapter);
	dev_info(&adapter->pdev->dev,
		 "MSI setup failed (%d), back to legacy INTx\n", rc);

	rc = leapraid_set_legacy_int(adapter);
	if (rc)
		dev_err(&adapter->pdev->dev,
			"%s: Enable legacy irq failed!\n", __func__);

	return rc;
}

int leapraid_set_pcie_and_notification(struct leapraid_adapter *adapter)
{
	int rc;

	rc = leapraid_enable_pcie(adapter);
	if (rc)
		goto out_fail;

	leapraid_mask_int(adapter);

	rc = leapraid_make_adapter_ready(adapter, PART_RESET);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Make adapter ready failure\n");
		goto out_fail;
	}

	rc = leapraid_get_adapter_features(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Get adapter feature failure\n");
		goto out_fail;
	}

	rc = leapraid_set_notification_auto(adapter);
	if (rc)
		goto out_fail;

	pci_save_state(adapter->pdev);

	return 0;

out_fail:
	leapraid_free_irq(adapter);
	leapraid_disable_pcie(adapter);
	return rc;
}

void leapraid_disable_controller(struct leapraid_adapter *adapter)
{
	if (!adapter->iomem_base)
		return;

	leapraid_mask_int(adapter);

	adapter->access_ctrl.shost_recovering = 1;
	leapraid_make_adapter_ready(adapter, PART_RESET);
	adapter->access_ctrl.shost_recovering = 0;
	wake_up(&adapter->access_ctrl.recovery_waitq);

	leapraid_free_irq(adapter);
	leapraid_disable_pcie(adapter);
}

static int leapraid_adapter_unit_reset(struct leapraid_adapter *adapter)
{
	int rc = 0;

	writel(LEAPRAID_FUNC_ADAPTER_UNIT_RESET << LEAPRAID_DB_FUNC_SHIFT,
	       &adapter->iomem_base->db);
	if (leapraid_db_wait_ack_and_clear_int(adapter))
		rc = -EFAULT;

	if (!leapraid_wait_adapter_ready(adapter)) {
		dev_err(&adapter->pdev->dev, "unit reset failed\n");
		return -EFAULT;
	}

	return rc;
}

static int leapraid_make_adapter_ready(struct leapraid_adapter *adapter,
				       enum reset_type type)
{
	u32 db;
	int count;

	if (!leapraid_pci_active(adapter))
		return 0;

	count = 0;
	db = leapraid_readl(&adapter->iomem_base->db);
	if ((db & LEAPRAID_DB_MASK) == LEAPRAID_DB_RESET) {
		while ((db & LEAPRAID_DB_MASK) != LEAPRAID_DB_READY) {
			if (count++ == LEAPRAID_DB_RETRY_COUNT_MAX) {
				dev_err(&adapter->pdev->dev,
					"Wait adapter ready timeout\n");
				return -EFAULT;
			}
			ssleep(1);
			db = leapraid_readl(&adapter->iomem_base->db);
			dev_info(&adapter->pdev->dev,
				 "Wait adapter ready, count=%d, db=0x%x\n",
				 count, db);
		}
	}
	if ((db & LEAPRAID_DB_MASK) == LEAPRAID_DB_READY)
		return 0;

	if (db & LEAPRAID_DB_USED)
		goto full_reset;

	if ((db & LEAPRAID_DB_MASK) == LEAPRAID_DB_FAULT)
		goto full_reset;

	if (type == FULL_RESET)
		goto full_reset;

	if ((db & LEAPRAID_DB_MASK) == LEAPRAID_DB_OPERATIONAL &&
	    !leapraid_adapter_unit_reset(adapter))
		return 0;

full_reset:
	return leapraid_host_diag_reset(adapter);
}

static void leapraid_fw_log_exit(struct leapraid_adapter *adapter)
{
	if (!adapter->fw_log_desc.open_pcie_trace)
		return;

	if (adapter->fw_log_desc.fw_log_buffer) {
		wait_event(adapter->fw_log_desc.mmap_waitq,
			   !atomic_read(&adapter->fw_log_desc.mmap_refcnt));
		dma_free_coherent(&adapter->pdev->dev,
				  (LEAPRAID_SYS_LOG_BUF_SIZE +
				   LEAPRAID_SYS_LOG_BUF_RESERVE),
				  adapter->fw_log_desc.fw_log_buffer,
				  adapter->fw_log_desc.fw_log_buffer_dma);
		adapter->fw_log_desc.fw_log_buffer = NULL;
	}
}

static int leapraid_fw_log_init(struct leapraid_adapter *adapter)
{
	struct leapraid_adapter_log_req adapter_log_req;
	struct leapraid_adapter_log_rep adapter_log_rep;
	u16 adapter_status;
	u64 buf_addr;
	u32 rc;

	if (!adapter->fw_log_desc.open_pcie_trace)
		return 0;

	if (!adapter->fw_log_desc.fw_log_buffer) {
		adapter->fw_log_desc.fw_log_buffer =
			dma_alloc_coherent(
				&adapter->pdev->dev,
				(LEAPRAID_SYS_LOG_BUF_SIZE +
				 LEAPRAID_SYS_LOG_BUF_RESERVE),
				&adapter->fw_log_desc.fw_log_buffer_dma,
				GFP_KERNEL);
		if (!adapter->fw_log_desc.fw_log_buffer)
			return -ENOMEM;
	}

	memset(&adapter_log_req, 0, sizeof(struct leapraid_adapter_log_req));
	adapter_log_req.func = LEAPRAID_FUNC_LOGBUF_INIT;
	buf_addr = adapter->fw_log_desc.fw_log_buffer_dma;

	adapter_log_req.mbox.w[0] =
		cpu_to_le32((u32)(buf_addr & 0xFFFFFFFF));
	adapter_log_req.mbox.w[1] =
		cpu_to_le32((u32)((buf_addr >> 32) & 0xFFFFFFFF));
	adapter_log_req.mbox.w[2] =
		cpu_to_le32(LEAPRAID_SYS_LOG_BUF_SIZE);
	rc = leapraid_handshake_func(adapter,
				     sizeof(struct leapraid_adapter_log_req),
				     (u32 *)&adapter_log_req,
				     sizeof(struct leapraid_adapter_log_rep),
				     (u16 *)&adapter_log_rep);
	if (rc != 0) {
		dev_err(&adapter->pdev->dev, "%s: Handshake failed, rc=%d\n",
			__func__, rc);
		return rc;
	}

	adapter_status = le16_to_cpu(adapter_log_rep.adapter_status) &
			LEAPRAID_ADAPTER_STATUS_MASK;
	if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS) {
		dev_err(&adapter->pdev->dev, "%s: failed!\n", __func__);
		rc = -EIO;
	}

	return rc;
}

static void leapraid_free_host_memory(struct leapraid_adapter *adapter)
{
	unsigned int i;

	if (adapter->mem_desc.task_desc) {
		dma_free_coherent(&adapter->pdev->dev,
				  adapter->adapter_attr.task_desc_dma_size,
				  adapter->mem_desc.task_desc,
				  adapter->mem_desc.task_desc_dma);
		adapter->mem_desc.task_desc = NULL;
	}

	if (adapter->mem_desc.sense_data) {
		dma_free_coherent(
			&adapter->pdev->dev,
			adapter->adapter_attr.io_qd * SCSI_SENSE_BUFFERSIZE,
			adapter->mem_desc.sense_data,
			adapter->mem_desc.sense_data_dma);
		adapter->mem_desc.sense_data = NULL;
	}

	if (adapter->mem_desc.rep_msg) {
		dma_free_coherent(
			&adapter->pdev->dev,
			adapter->adapter_attr.rep_msg_qd * LEAPRAID_REPLY_SIZE,
			adapter->mem_desc.rep_msg,
			adapter->mem_desc.rep_msg_dma);
		adapter->mem_desc.rep_msg = NULL;
	}

	if (adapter->mem_desc.rep_msg_addr) {
		dma_free_coherent(&adapter->pdev->dev,
				  adapter->adapter_attr.rep_msg_qd *
				  LEAPRAID_REP_MSG_ADDR_SIZE,
				  adapter->mem_desc.rep_msg_addr,
				  adapter->mem_desc.rep_msg_addr_dma);
		adapter->mem_desc.rep_msg_addr = NULL;
	}

	if (adapter->mem_desc.rep_desc_seg_maint) {
		for (i = 0; i < adapter->adapter_attr.rep_desc_q_seg_cnt;
		     i++) {
			if (adapter->mem_desc.rep_desc_seg_maint[i]
			    .rep_desc_seg) {
				dma_free_coherent(
					&adapter->pdev->dev,
					(adapter->adapter_attr.rep_desc_qd *
					 LEAPRAID_REP_DESC_ENTRY_SIZE) *
					LEAPRAID_REP_DESC_CHUNK_SIZE,
					adapter->mem_desc.rep_desc_seg_maint[i]
						.rep_desc_seg,
					adapter->mem_desc.rep_desc_seg_maint[i]
						.rep_desc_seg_dma);
				adapter->mem_desc.rep_desc_seg_maint[i]
					.rep_desc_seg = NULL;
			}
		}

		if (adapter->mem_desc.rep_desc_q_arr) {
			dma_free_coherent(
				&adapter->pdev->dev,
				adapter->adapter_attr.rq_cnt *
				LEAPRAID_REP_RQ_CNT_SIZE,
				adapter->mem_desc.rep_desc_q_arr,
				adapter->mem_desc.rep_desc_q_arr_dma);
			adapter->mem_desc.rep_desc_q_arr = NULL;
		}

		for (i = 0; i < adapter->adapter_attr.rep_desc_q_seg_cnt; i++) {
			struct leapraid_mem_desc *mem_desc =
				 &adapter->mem_desc;
			kfree(adapter->mem_desc.rep_desc_seg_maint[i]
				.rep_desc_maint);
			mem_desc->rep_desc_seg_maint[i].rep_desc_maint = NULL;
		}
		kfree(adapter->mem_desc.rep_desc_seg_maint);
		adapter->mem_desc.rep_desc_seg_maint = NULL;
	}

	kfree(adapter->mem_desc.taskid_to_uniq_tag);
	adapter->mem_desc.taskid_to_uniq_tag = NULL;

	dma_pool_destroy(adapter->mem_desc.sg_chain_pool);
	adapter->mem_desc.sg_chain_pool = NULL;
}

static inline bool leapraid_is_in_same_4g_seg(dma_addr_t start, u32 size)
{
	return upper_32_bits(start) == upper_32_bits(start + size - 1);
}

int leapraid_internal_init_cmd_priv(struct leapraid_adapter *adapter,
				    struct leapraid_io_req_tracker *io_tracker)
{
	io_tracker->chain =
		dma_pool_alloc(adapter->mem_desc.sg_chain_pool,
			       GFP_KERNEL,
			       &io_tracker->chain_dma);

	if (!io_tracker->chain)
		return -ENOMEM;

	return 0;
}

void leapraid_internal_exit_cmd_priv(struct leapraid_adapter *adapter,
				     struct leapraid_io_req_tracker *io_tracker)
{
	if (io_tracker && io_tracker->chain)
		dma_pool_free(adapter->mem_desc.sg_chain_pool,
			      io_tracker->chain,
			      io_tracker->chain_dma);
}

static int leapraid_request_host_memory(struct leapraid_adapter *adapter)
{
	struct leapraid_adapter_features *facts =
			&adapter->adapter_attr.features;
	u16 rep_desc_q_cnt_allocated;
	unsigned int i, j;
	int rc;

	/* Scatter-gather table size. */
	adapter->shost->sg_tablesize = LEAPRAID_SG_DEPTH;
	if (reset_devices)
		adapter->shost->sg_tablesize =
				LEAPRAID_KDUMP_MIN_PHYS_SEGMENTS;
	/* High-priority commands queue depth. */
	adapter->dynamic_task_desc.hp_cmd_qd = LEAPRAID_FIXED_HP_CMDS;
	/* Internal commands queue depth. */
	adapter->dynamic_task_desc.inter_cmd_qd = LEAPRAID_FIXED_INTER_CMDS;
	/* Adapter commands total queue depth. */
	if (reset_devices)
		adapter->adapter_attr.adapter_total_qd =
			LEAPRAID_DEFAULT_CMD_QD_OFFSET +
			adapter->dynamic_task_desc.inter_cmd_qd +
			adapter->dynamic_task_desc.hp_cmd_qd;
	else
		adapter->adapter_attr.adapter_total_qd = facts->req_slot +
				adapter->dynamic_task_desc.hp_cmd_qd;
	/* Reply message queue depth. */
	adapter->adapter_attr.rep_msg_qd =
		adapter->adapter_attr.adapter_total_qd +
		LEAPRAID_DEFAULT_CMD_QD_OFFSET;
	/* Reply descriptor queue depth. */
	adapter->adapter_attr.rep_desc_qd =
		round_up(adapter->adapter_attr.adapter_total_qd +
			 adapter->adapter_attr.rep_msg_qd +
			 LEAPRAID_TASKID_OFFSET_CTRL_CMD,
			 LEAPRAID_REPLY_QD_ALIGNMENT);
	/* SCSI command I/O depth. */
	adapter->adapter_attr.io_qd =
		adapter->adapter_attr.adapter_total_qd -
		adapter->dynamic_task_desc.hp_cmd_qd -
		adapter->dynamic_task_desc.inter_cmd_qd;
	/* SCSI host can queue. */
	adapter->shost->can_queue = adapter->adapter_attr.io_qd -
		LEAPRAID_TASKID_OFFSET_CTRL_CMD;
	adapter->driver_cmds.ctl_cmd.taskid = adapter->shost->can_queue +
		LEAPRAID_TASKID_OFFSET_CTRL_CMD;

	/* Allocate task descriptor. */
try_again:
	adapter->adapter_attr.task_desc_dma_size =
		(adapter->adapter_attr.adapter_total_qd +
			LEAPRAID_TASKID_OFFSET_CTRL_CMD) *
		LEAPRAID_REQUEST_SIZE;
	adapter->mem_desc.task_desc =
		dma_alloc_coherent(&adapter->pdev->dev,
				   adapter->adapter_attr.task_desc_dma_size,
				   &adapter->mem_desc.task_desc_dma,
				   GFP_KERNEL);
	if (!adapter->mem_desc.task_desc)
		return -ENOMEM;

	/* Allocate chain message pool. */
	adapter->mem_desc.sg_chain_pool_size =
		LEAPRAID_DEFAULT_CHAINS_PER_IO * LEAPRAID_CHAIN_SEG_SIZE;
	adapter->mem_desc.sg_chain_pool =
		dma_pool_create("leapraid chain pool",
				&adapter->pdev->dev,
				adapter->mem_desc.sg_chain_pool_size,
				LEAPRAID_DMA_ALIGN, 0);
	if (!adapter->mem_desc.sg_chain_pool)
		return -ENOMEM;

	/* Allocate I/O tracker to SCSI I/O. */
	adapter->mem_desc.taskid_to_uniq_tag =
		kcalloc(adapter->shost->can_queue, sizeof(u16), GFP_KERNEL);
	if (!adapter->mem_desc.taskid_to_uniq_tag)
		return -ENOMEM;

	adapter->dynamic_task_desc.hp_taskid =
		adapter->adapter_attr.io_qd +
		LEAPRAID_HP_TASKID_OFFSET_CTL_CMD;
	/* Allocate static high-priority task ID. */
	adapter->driver_cmds.ctl_cmd.hp_taskid =
		adapter->dynamic_task_desc.hp_taskid;
	adapter->driver_cmds.tm_cmd.hp_taskid =
		adapter->dynamic_task_desc.hp_taskid +
		LEAPRAID_HP_TASKID_OFFSET_TM_CMD;

	adapter->dynamic_task_desc.inter_taskid =
		adapter->dynamic_task_desc.hp_taskid +
		adapter->dynamic_task_desc.hp_cmd_qd;
	adapter->driver_cmds.scan_dev_cmd.inter_taskid =
		adapter->dynamic_task_desc.inter_taskid;
	adapter->driver_cmds.cfg_op_cmd.inter_taskid =
		adapter->dynamic_task_desc.inter_taskid +
		LEAPRAID_TASKID_OFFSET_CFG_OP_CMD;
	adapter->driver_cmds.transport_cmd.inter_taskid =
		adapter->dynamic_task_desc.inter_taskid +
		LEAPRAID_TASKID_OFFSET_TRANSPORT_CMD;
	adapter->driver_cmds.enc_cmd.inter_taskid =
		adapter->dynamic_task_desc.inter_taskid +
		LEAPRAID_TASKID_OFFSET_ENC_CMD;
	adapter->driver_cmds.notify_event_cmd.inter_taskid =
		adapter->dynamic_task_desc.inter_taskid +
		LEAPRAID_TASKID_OFFSET_NOTIFY_EVENT_CMD;
	dev_info(&adapter->pdev->dev, "queue depth:\n");
	dev_info(&adapter->pdev->dev, "	host->can_queue: %d\n",
		 adapter->shost->can_queue);
	dev_info(&adapter->pdev->dev, "	io_qd: %d\n",
		 adapter->adapter_attr.io_qd);
	dev_info(&adapter->pdev->dev, "	hpr_cmd_qd: %d\n",
		 adapter->dynamic_task_desc.hp_cmd_qd);
	dev_info(&adapter->pdev->dev, "	inter_cmd_qd: %d\n",
		 adapter->dynamic_task_desc.inter_cmd_qd);
	dev_info(&adapter->pdev->dev, "	adapter_total_qd: %d\n",
		 adapter->adapter_attr.adapter_total_qd);

	dev_info(&adapter->pdev->dev, "taskid range:\n");
	dev_info(&adapter->pdev->dev,
		 "	adapter->dynamic_task_desc.hp_taskid: %d\n",
		 adapter->dynamic_task_desc.hp_taskid);
	dev_info(&adapter->pdev->dev,
		 "	adapter->dynamic_task_desc.inter_taskid: %d\n",
		 adapter->dynamic_task_desc.inter_taskid);

	/*
	 * Allocate sense data DMA buffer.
	 * Constraint: Must reside within the same 4GB segment
	 * (driver-maintained).
	 */
	adapter->mem_desc.sense_data =
		dma_alloc_coherent(
			&adapter->pdev->dev,
			adapter->adapter_attr.io_qd * SCSI_SENSE_BUFFERSIZE,
			&adapter->mem_desc.sense_data_dma,
			GFP_KERNEL);
	if (!adapter->mem_desc.sense_data)
		return -ENOMEM;

	if (!leapraid_is_in_same_4g_seg(adapter->mem_desc.sense_data_dma,
					adapter->adapter_attr.io_qd *
					SCSI_SENSE_BUFFERSIZE)) {
		dev_warn(&adapter->pdev->dev,
			 "Try 32bit DMA: Sense not in same 4G\n");
		rc = -EAGAIN;
		goto out_fail;
	}

	/*
	 * Allocate reply frame buffer.
	 * Constraint: Must reside in the same 4GB segment as the sense DMA.
	 */
	adapter->mem_desc.rep_msg =
		dma_alloc_coherent(&adapter->pdev->dev,
				   adapter->adapter_attr.rep_msg_qd *
				   LEAPRAID_REPLY_SIZE,
				   &adapter->mem_desc.rep_msg_dma,
				   GFP_KERNEL);
	if (!adapter->mem_desc.rep_msg) {
		rc = -ENOMEM;
		goto out_fail;
	}

	if (!leapraid_is_in_same_4g_seg(adapter->mem_desc.rep_msg_dma,
					adapter->adapter_attr.rep_msg_qd *
					LEAPRAID_REPLY_SIZE)) {
		dev_warn(&adapter->pdev->dev,
			 "Use 32 bit DMA due to rep msg is not in same 4g!\n");
		rc = -EAGAIN;
		goto out_fail;
	}

	/* Address of reply frame. */
	adapter->mem_desc.rep_msg_addr =
		dma_alloc_coherent(&adapter->pdev->dev,
				   adapter->adapter_attr.rep_msg_qd *
				   LEAPRAID_REP_MSG_ADDR_SIZE,
				   &adapter->mem_desc.rep_msg_addr_dma,
				   GFP_KERNEL);
	if (!adapter->mem_desc.rep_msg_addr)
		return -ENOMEM;
	adapter->adapter_attr.rep_desc_q_seg_cnt =
		DIV_ROUND_UP(adapter->adapter_attr.rq_cnt,
			     LEAPRAID_REP_DESC_CHUNK_SIZE);
	adapter->mem_desc.rep_desc_seg_maint =
		kcalloc(adapter->adapter_attr.rep_desc_q_seg_cnt,
			sizeof(struct leapraid_rep_desc_seg_maint),
			GFP_KERNEL);
	if (!adapter->mem_desc.rep_desc_seg_maint)
		return -ENOMEM;

	rep_desc_q_cnt_allocated = 0;
	for (i = 0; i < adapter->adapter_attr.rep_desc_q_seg_cnt; i++) {
		adapter->mem_desc.rep_desc_seg_maint[i].rep_desc_maint =
			kcalloc(LEAPRAID_REP_DESC_CHUNK_SIZE,
				sizeof(struct leapraid_rep_desc_maint),
				GFP_KERNEL);
		if (!adapter->mem_desc.rep_desc_seg_maint[i].rep_desc_maint)
			return -ENOMEM;

		adapter->mem_desc.rep_desc_seg_maint[i].rep_desc_seg =
			dma_alloc_coherent(
				&adapter->pdev->dev,
				(adapter->adapter_attr.rep_desc_qd *
				LEAPRAID_REP_DESC_ENTRY_SIZE) *
				LEAPRAID_REP_DESC_CHUNK_SIZE,
				&adapter->mem_desc.rep_desc_seg_maint[i]
					.rep_desc_seg_dma,
				GFP_KERNEL);
		if (!adapter->mem_desc.rep_desc_seg_maint[i].rep_desc_seg)
			return -ENOMEM;

		for (j = 0; j < LEAPRAID_REP_DESC_CHUNK_SIZE; j++) {
			if (rep_desc_q_cnt_allocated >=
			    adapter->adapter_attr.rq_cnt)
				break;
			adapter->mem_desc
				.rep_desc_seg_maint[i]
				.rep_desc_maint[j]
				.rep_desc =
					(void *)((u8 *)(
					adapter->mem_desc
						.rep_desc_seg_maint[i]
						.rep_desc_seg) +
					j *
					(adapter->adapter_attr.rep_desc_qd *
						LEAPRAID_REP_DESC_ENTRY_SIZE));
			adapter->mem_desc
				.rep_desc_seg_maint[i]
				.rep_desc_maint[j]
				.rep_desc_dma =
					adapter->mem_desc
						.rep_desc_seg_maint[i]
						.rep_desc_seg_dma +
					j *
					(adapter->adapter_attr.rep_desc_qd *
						LEAPRAID_REP_DESC_ENTRY_SIZE);
			rep_desc_q_cnt_allocated++;
		}
	}

	if (!reset_devices) {
		adapter->mem_desc.rep_desc_q_arr =
			dma_alloc_coherent(
				&adapter->pdev->dev,
				adapter->adapter_attr.rq_cnt *
				LEAPRAID_REP_RQ_CNT_SIZE,
				&adapter->mem_desc.rep_desc_q_arr_dma,
				GFP_KERNEL);
		if (!adapter->mem_desc.rep_desc_q_arr)
			return -ENOMEM;
	}

	return 0;
out_fail:
	if (rc == -EAGAIN) {
		leapraid_free_host_memory(adapter);
		adapter->adapter_attr.use_32_dma_mask = 1;
		rc = dma_set_mask_and_coherent(&adapter->pdev->dev,
					       DMA_BIT_MASK(DMA_32_BITS));
		if (rc) {
			dev_err(&adapter->pdev->dev,
				"Failed to set 32 DMA mask\n");
			return rc;
		}
		goto try_again;
	}
	return rc;
}

static int leapraid_alloc_dev_topo_bitmaps(struct leapraid_adapter *adapter)
{
	u16 pd_hdls_sz;

	pd_hdls_sz =
		BITS_TO_LONGS(
			adapter->adapter_attr.features.max_dev_handle + 1) *
		sizeof(unsigned long);

	adapter->dev_topo.pd_hdls_sz = pd_hdls_sz;
	adapter->dev_topo.pd_hdls =
		kzalloc(adapter->dev_topo.pd_hdls_sz, GFP_KERNEL);
	if (!adapter->dev_topo.pd_hdls)
		return -ENOMEM;

	adapter->dev_topo.blocking_hdls =
		kzalloc(adapter->dev_topo.pd_hdls_sz, GFP_KERNEL);
	if (!adapter->dev_topo.blocking_hdls)
		return -ENOMEM;

	return 0;
}

static void leapraid_free_dev_topo_bitmaps(struct leapraid_adapter *adapter)
{
	kfree(adapter->dev_topo.pd_hdls);
	kfree(adapter->dev_topo.blocking_hdls);
}

static int leapraid_init_driver_cmds(struct leapraid_adapter *adapter)
{
	INIT_LIST_HEAD(&adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.scan_dev_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.scan_dev_cmd.cb_idx = LEAPRAID_SCAN_DEV_CB_IDX;
	list_add_tail(&adapter->driver_cmds.scan_dev_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.cfg_op_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.cfg_op_cmd.cb_idx = LEAPRAID_CONFIG_CB_IDX;
	mutex_init(&adapter->driver_cmds.cfg_op_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.cfg_op_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.transport_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.transport_cmd.cb_idx = LEAPRAID_TRANSPORT_CB_IDX;
	mutex_init(&adapter->driver_cmds.transport_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.transport_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.enc_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.enc_cmd.cb_idx = LEAPRAID_ENC_CB_IDX;
	mutex_init(&adapter->driver_cmds.enc_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.enc_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.notify_event_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.notify_event_cmd.cb_idx =
		LEAPRAID_NOTIFY_EVENT_CB_IDX;
	mutex_init(&adapter->driver_cmds.notify_event_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.notify_event_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.ctl_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.ctl_cmd.cb_idx = LEAPRAID_CTL_CB_IDX;
	mutex_init(&adapter->driver_cmds.ctl_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.ctl_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	adapter->driver_cmds.tm_cmd.status = LEAPRAID_CMD_NOT_USED;
	adapter->driver_cmds.tm_cmd.cb_idx = LEAPRAID_TM_CB_IDX;
	mutex_init(&adapter->driver_cmds.tm_cmd.mutex);
	list_add_tail(&adapter->driver_cmds.tm_cmd.list,
		      &adapter->driver_cmds.special_cmd_list);

	return 0;
}

static void leapraid_unmask_evts(struct leapraid_adapter *adapter, u16 evt)
{
	if (evt >= LEAPRAID_MAX_EVENT_NUM)
		return;

	clear_bit(evt, (unsigned long *)adapter->fw_evt_s.leapraid_evt_masks);
}

static void leapraid_init_event_mask(struct leapraid_adapter *adapter)
{
	int i;

	for (i = 0; i < LEAPRAID_EVT_MASK_COUNT; i++)
		adapter->fw_evt_s.leapraid_evt_masks[i] =
			LEAPRAID_INVALID_INITIAL_VALUE;

	leapraid_unmask_evts(adapter, LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST);
	leapraid_unmask_evts(adapter, LEAPRAID_EVT_SAS_ENCL_DEV_STATUS_CHANGE);
	leapraid_unmask_evts(adapter, LEAPRAID_EVT_SAS_DEV_STATUS_CHANGE);
	leapraid_unmask_evts(adapter, LEAPRAID_EVT_IR_CHANGE);
}

static void leapraid_prepare_adapter_init_req(
		struct leapraid_adapter *adapter,
		struct leapraid_adapter_init_req *init_req)
{
	ktime_t cur_time;
	int i, chunk;
	u32 reply_post_free_ary_sz;

	memset(init_req, 0, sizeof(struct leapraid_adapter_init_req));
	init_req->func = LEAPRAID_FUNC_ADAPTER_INIT;
	init_req->who_init = LEAPRAID_WHOINIT_LINUX_DRIVER;
	init_req->msg_ver = cpu_to_le16(LEAPRAID_MSG_VERSION);
	init_req->header_ver = cpu_to_le16(LEAPRAID_HEADER_VERSION);
	init_req->driver_ver =
		cpu_to_le32((LEAPRAID_MAJOR_VERSION <<
				LEAPRAID_VER_MAJOR_SHIFT) |
			(LEAPRAID_MINOR_VERSION << LEAPRAID_VER_MINOR_SHIFT) |
			(LEAPRAID_BUILD_VERSION << LEAPRAID_VER_BUILD_SHIFT) |
			LEAPRAID_RELEASE_VERSION);
	if (adapter->notification_desc.msix_enable)
		init_req->host_msix_vectors = adapter->adapter_attr.rq_cnt;

	init_req->req_frame_size =
		cpu_to_le16(LEAPRAID_REQUEST_SIZE / LEAPRAID_DWORDS_BYTE_SIZE);
	init_req->rep_desc_qd =
		cpu_to_le16(adapter->adapter_attr.rep_desc_qd);
	init_req->rep_msg_qd =
		cpu_to_le16(adapter->adapter_attr.rep_msg_qd);
	init_req->sense_buffer_add_high =
		cpu_to_le32((u64)adapter->mem_desc.sense_data_dma >> 32);
	init_req->rep_msg_dma_high =
		cpu_to_le32((u64)adapter->mem_desc.rep_msg_dma >> 32);
	init_req->task_desc_base_addr =
		cpu_to_le64((u64)adapter->mem_desc.task_desc_dma);
	init_req->rep_msg_addr_dma =
		cpu_to_le64((u64)adapter->mem_desc.rep_msg_addr_dma);
	if (!reset_devices) {
		reply_post_free_ary_sz =
			adapter->adapter_attr.rq_cnt * LEAPRAID_REP_RQ_CNT_SIZE;
		memset(adapter->mem_desc.rep_desc_q_arr, 0,
		       reply_post_free_ary_sz);

		chunk = LEAPRAID_REP_DESC_CHUNK_SIZE;
		for (i = 0; i < adapter->adapter_attr.rq_cnt; i++)
			adapter->mem_desc.rep_desc_q_arr[i].rep_desc_base_addr =
				cpu_to_le64 ((u64)adapter->mem_desc
					     .rep_desc_seg_maint[i / chunk]
					     .rep_desc_maint[i % chunk]
					     .rep_desc_dma);

		init_req->msg_flg =
			LEAPRAID_ADAPTER_INIT_MSGFLG_RDPQ_ARRAY_MODE;
		init_req->rep_desc_q_arr_addr =
			cpu_to_le64((u64)adapter->mem_desc.rep_desc_q_arr_dma);
	} else {
		init_req->rep_desc_q_arr_addr =
			cpu_to_le64((u64)adapter->mem_desc
					.rep_desc_seg_maint[0]
					.rep_desc_maint[0]
					.rep_desc_dma);
	}
	cur_time = ktime_get_real();
	init_req->time_stamp = cpu_to_le64(ktime_to_ms(cur_time));
}

static int leapraid_send_adapter_init(struct leapraid_adapter *adapter)
{
	struct leapraid_adapter_init_req init_req;
	struct leapraid_adapter_init_rep init_rep;
	u16 adapter_status;
	int rc;

	leapraid_prepare_adapter_init_req(adapter, &init_req);

	rc = leapraid_handshake_func(adapter,
				     sizeof(struct leapraid_adapter_init_req),
				     (u32 *)&init_req,
				     sizeof(struct leapraid_adapter_init_rep),
				     (u16 *)&init_rep);
	if (rc != 0) {
		dev_err(&adapter->pdev->dev, "%s: Handshake failed, rc=%d\n",
			__func__, rc);
		return rc;
	}

	adapter_status =
		le16_to_cpu(init_rep.adapter_status) &
		LEAPRAID_ADAPTER_STATUS_MASK;
	if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS) {
		dev_err(&adapter->pdev->dev, "%s: failed\n", __func__);
		rc = -EIO;
	}

	return rc;
}

static int leapraid_cfg_pages(struct leapraid_adapter *adapter)
{
	union cfg_param_1 cfgp1 = {0};
	union cfg_param_2 cfgp2 = {0};
	union {
		struct leapraid_manufacturing_p0 manufacturing_page0;
		struct leapraid_bios_page3 bios_page3;
		struct leapraid_bios_page2 bios_page2;
	} cfg_page;
	int rc;

	rc = leapraid_op_config_page(adapter, &cfg_page.bios_page3, cfgp1,
				     cfgp2, GET_BIOS_PG3);
	if (rc)
		return rc;

	adapter->adapter_attr.bios_version =
		le32_to_cpu(cfg_page.bios_page3.bios_version);

	rc = leapraid_op_config_page(adapter, &cfg_page.bios_page2, cfgp1,
				     cfgp2, GET_BIOS_PG2);
	if (rc)
		return rc;

	adapter->boot_devs.requested_boot_dev.form =
		cfg_page.bios_page2.requested_boot_dev_form;
	memcpy(adapter->boot_devs.requested_boot_dev.pg_dev,
	       &cfg_page.bios_page2.requested_boot_dev,
	       LEAPRAID_BOOT_DEV_SIZE);
	adapter->boot_devs.requested_alt_boot_dev.form =
		cfg_page.bios_page2.requested_alt_boot_dev_form;
	memcpy(adapter->boot_devs.requested_alt_boot_dev.pg_dev,
	       &cfg_page.bios_page2.requested_alt_boot_dev,
	       LEAPRAID_BOOT_DEV_SIZE);
	adapter->boot_devs.current_boot_dev.form =
		cfg_page.bios_page2.current_boot_dev_form;
	memcpy(adapter->boot_devs.current_boot_dev.pg_dev,
	       &cfg_page.bios_page2.current_boot_dev,
	       LEAPRAID_BOOT_DEV_SIZE);

	rc = leapraid_op_config_page(adapter, &cfg_page.manufacturing_page0,
				     cfgp1, cfgp2, GET_MANUFACTURING_PG0);
	if (rc)
		return rc;

	snprintf(adapter->adapter_attr.board_name,
		 sizeof(adapter->adapter_attr.board_name),
		 "%.*s",
		 (int)sizeof(cfg_page.manufacturing_page0.board_name),
		 cfg_page.manufacturing_page0.board_name);
	return rc;
}

static int leapraid_evt_notify(struct leapraid_adapter *adapter)
{
	struct leapraid_evt_notify_req *evt_notify_req;
	struct leapraid_evt_notify_rep *evt_notify_rep;
	u16 adapter_status;
	int rc = 0;
	int i;

	mutex_lock(&adapter->driver_cmds.notify_event_cmd.mutex);
	adapter->driver_cmds.notify_event_cmd.status = LEAPRAID_CMD_PENDING;
	evt_notify_req =
		leapraid_get_task_desc(
			adapter,
			adapter->driver_cmds.notify_event_cmd.inter_taskid);
	memset(evt_notify_req, 0, sizeof(struct leapraid_evt_notify_req));
	evt_notify_req->func = LEAPRAID_FUNC_EVENT_NOTIFY;
	for (i = 0; i < LEAPRAID_EVT_MASK_COUNT; i++)
		evt_notify_req->evt_masks[i] =
			cpu_to_le32(adapter->fw_evt_s.leapraid_evt_masks[i]);
	init_completion(&adapter->driver_cmds.notify_event_cmd.done);
	leapraid_fire_task(adapter,
			   adapter->driver_cmds.notify_event_cmd.inter_taskid);
	wait_for_completion_timeout(
		&adapter->driver_cmds.notify_event_cmd.done,
		LEAPRAID_NOTIFY_EVENT_CMD_TIMEOUT * HZ);

	if (!(adapter->driver_cmds.notify_event_cmd.status &
	      LEAPRAID_CMD_DONE)) {
		rc = -EFAULT;
		goto out_cleanup;
	}

	if (!(adapter->driver_cmds.notify_event_cmd.status &
	      LEAPRAID_CMD_REPLY_VALID)) {
		rc = -EFAULT;
		goto out_cleanup;
	}

	evt_notify_rep = (void *)&adapter->driver_cmds.notify_event_cmd.reply;
	adapter_status = le16_to_cpu(evt_notify_rep->adapter_status) &
			 LEAPRAID_ADAPTER_STATUS_MASK;
	if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS)
		rc = -EFAULT;

out_cleanup:
	adapter->driver_cmds.notify_event_cmd.status = LEAPRAID_CMD_NOT_USED;
	mutex_unlock(&adapter->driver_cmds.notify_event_cmd.mutex);

	return rc;
}

int leapraid_scan_dev(struct leapraid_adapter *adapter, bool async_scan_dev)
{
	struct leapraid_scan_dev_req *scan_dev_req;
	struct leapraid_scan_dev_rep *scan_dev_rep;
	u16 adapter_status;
	int rc = 0;

	dev_info(&adapter->pdev->dev,
		 "Send device scan, async_scan_dev=%d!\n", async_scan_dev);

	adapter->driver_cmds.scan_dev_cmd.status = LEAPRAID_CMD_PENDING;
	adapter->driver_cmds.scan_dev_cmd.async_scan_dev = async_scan_dev;
	scan_dev_req =
		leapraid_get_task_desc(
			adapter,
			adapter->driver_cmds.scan_dev_cmd.inter_taskid);
	memset(scan_dev_req, 0, sizeof(struct leapraid_scan_dev_req));
	scan_dev_req->func = LEAPRAID_FUNC_SCAN_DEV;

	if (async_scan_dev) {
		adapter->scan_dev_desc.first_scan_dev_fired = 1;
		leapraid_fire_task(
			adapter,
			adapter->driver_cmds.scan_dev_cmd.inter_taskid);
		return 0;
	}

	init_completion(&adapter->driver_cmds.scan_dev_cmd.done);
	leapraid_fire_task(adapter,
			   adapter->driver_cmds.scan_dev_cmd.inter_taskid);
	wait_for_completion_timeout(&adapter->driver_cmds.scan_dev_cmd.done,
				    LEAPRAID_SCAN_DEV_CMD_TIMEOUT * HZ);
	if (!(adapter->driver_cmds.scan_dev_cmd.status & LEAPRAID_CMD_DONE)) {
		dev_err(&adapter->pdev->dev, "Device scan timeout!\n");
		if (adapter->driver_cmds.scan_dev_cmd.status &
		    LEAPRAID_CMD_RESET)
			rc = -EFAULT;
		else
			rc = -ETIME;
		goto out_cleanup;
	}

	scan_dev_rep = (void *)&adapter->driver_cmds.scan_dev_cmd.reply;
	adapter_status =
		le16_to_cpu(scan_dev_rep->adapter_status) &
		LEAPRAID_ADAPTER_STATUS_MASK;
	if (adapter_status != LEAPRAID_ADAPTER_STATUS_SUCCESS) {
		dev_err(&adapter->pdev->dev, "Device scan failure!\n");
		rc = -EFAULT;
		goto out_cleanup;
	}

out_cleanup:
	adapter->driver_cmds.scan_dev_cmd.status = LEAPRAID_CMD_NOT_USED;
	return rc;
}

static void leapraid_init_task_tracker(struct leapraid_adapter *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->dynamic_task_desc.task_lock, flags);

	spin_unlock_irqrestore(&adapter->dynamic_task_desc.task_lock, flags);
}

static void leapraid_init_rep_msg_addr(struct leapraid_adapter *adapter)
{
	u32 reply_address;
	unsigned int i;

	for (i = 0, reply_address = (u32)adapter->mem_desc.rep_msg_dma;
	     i < adapter->adapter_attr.rep_msg_qd;
	     i++, reply_address += LEAPRAID_REPLY_SIZE)
		adapter->mem_desc.rep_msg_addr[i] = cpu_to_le32(reply_address);
}

static void init_rep_desc(
		struct leapraid_rq *rq,
		int index,
		union leapraid_rep_desc_union *reply_post_free_contig)
{
	struct leapraid_adapter *adapter = rq->adapter;
	unsigned int i;

	if (!reset_devices)
		rq->rep_desc =
			adapter->mem_desc
				.rep_desc_seg_maint[index /
					LEAPRAID_REP_DESC_CHUNK_SIZE]
				.rep_desc_maint[index %
					LEAPRAID_REP_DESC_CHUNK_SIZE]
				.rep_desc;
	else
		rq->rep_desc = reply_post_free_contig;

	rq->rep_post_host_idx = 0;
	for (i = 0; i < adapter->adapter_attr.rep_desc_qd; i++)
		rq->rep_desc[i].words = cpu_to_le64(ULLONG_MAX);
}

static void leapraid_init_rep_desc(struct leapraid_adapter *adapter)
{
	union leapraid_rep_desc_union *reply_post_free_contig;
	struct leapraid_int_rq *int_rq;
	struct leapraid_blk_mq_poll_rq *blk_mq_poll_rq;
	unsigned int i;
	int index;

	index = 0;
	reply_post_free_contig = adapter->mem_desc
					.rep_desc_seg_maint[0]
					.rep_desc_maint[0]
					.rep_desc;

	for (i = 0; i < adapter->notification_desc.iopoll_qdex; i++) {
		int_rq = &adapter->notification_desc.int_rqs[i];
		init_rep_desc(&int_rq->rq, index, reply_post_free_contig);
		if (!reset_devices)
			index++;
		else
			reply_post_free_contig +=
				adapter->adapter_attr.rep_desc_qd;
	}

	for (i = 0; i < adapter->notification_desc.iopoll_qcnt; i++) {
		blk_mq_poll_rq = &adapter->notification_desc.blk_mq_poll_rqs[i];
		init_rep_desc(&blk_mq_poll_rq->rq,
			      index, reply_post_free_contig);
		if (!reset_devices)
			index++;
		else
			reply_post_free_contig +=
				adapter->adapter_attr.rep_desc_qd;
	}
}

static void leapraid_init_bar_idx_regs(struct leapraid_adapter *adapter)
{
	struct leapraid_int_rq *int_rq;
	struct leapraid_blk_mq_poll_rq *blk_mq_poll_rq;
	unsigned int i, j;

	adapter->rep_msg_host_idx = adapter->adapter_attr.rep_msg_qd - 1;
	writel(adapter->rep_msg_host_idx,
	       &adapter->iomem_base->rep_msg_host_idx);

	for (i = 0; i < adapter->notification_desc.iopoll_qdex; i++) {
		int_rq = &adapter->notification_desc.int_rqs[i];
		for (j = 0; j < REP_POST_HOST_IDX_REG_CNT; j++)
			writel((int_rq->rq.msix_idx & 7) <<
				LEAPRAID_RPHI_MSIX_IDX_SHIFT,
				&adapter->iomem_base->rep_post_reg_idx[j].idx);
	}

	for (i = 0; i < adapter->notification_desc.iopoll_qcnt; i++) {
		blk_mq_poll_rq =
			&adapter->notification_desc.blk_mq_poll_rqs[i];
		for (j = 0; j < REP_POST_HOST_IDX_REG_CNT; j++)
			writel((blk_mq_poll_rq->rq.msix_idx & 7) <<
				LEAPRAID_RPHI_MSIX_IDX_SHIFT,
				&adapter->iomem_base->rep_post_reg_idx[j].idx);
	}
}

static int leapraid_make_adapter_available(struct leapraid_adapter *adapter)
{
	int rc;

	leapraid_init_task_tracker(adapter);
	leapraid_init_rep_msg_addr(adapter);

	if (adapter->scan_dev_desc.driver_loading)
		leapraid_configure_reply_queue_affinity(adapter);

	leapraid_init_rep_desc(adapter);
	rc = leapraid_send_adapter_init(adapter);
	if (rc)
		return rc;

	leapraid_init_bar_idx_regs(adapter);
	leapraid_unmask_int(adapter);
	rc = leapraid_cfg_pages(adapter);
	if (rc)
		return rc;

	rc = leapraid_evt_notify(adapter);
	if (rc)
		return rc;

	if (!adapter->access_ctrl.shost_recovering) {
		adapter->scan_dev_desc.wait_scan_dev_done = 1;
		return 0;
	}

	return leapraid_scan_dev(adapter, false);
}

int leapraid_ctrl_init(struct leapraid_adapter *adapter)
{
	u32 cap;
	int rc;

	rc = leapraid_init_driver_cmds(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Init driver cmds failure\n");
		goto out_free;
	}

	rc = leapraid_set_pcie_and_notification(adapter);
	if (rc)
		goto out_free;

	pci_set_drvdata(adapter->pdev, adapter->shost);

	pcie_capability_read_dword(adapter->pdev, PCI_EXP_DEVCAP, &cap);

	if (cap & PCI_EXP_DEVCAP_EXT_TAG)
		pcie_capability_set_word(adapter->pdev, PCI_EXP_DEVCTL,
					 PCI_EXP_DEVCTL_EXT_TAG);

	rc = leapraid_fw_log_init(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "FW log init failure\n");
		goto out_free;
	}

	rc = leapraid_request_host_memory(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Request host memory failure\n");
		goto out_free;
	}

	init_waitqueue_head(&adapter->reset_desc.reset_wait_queue);
	init_waitqueue_head(&adapter->access_ctrl.shost_recover_wq);

	rc = leapraid_alloc_dev_topo_bitmaps(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev, "Alloc topo bitmaps failure\n");
		goto out_free;
	}

	leapraid_init_event_mask(adapter);

	rc = leapraid_make_adapter_available(adapter);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"Make adapter available failure\n");
		goto out_free;
	}

	leapraid_overheat_init(adapter);
	if (!adapter->overheat_desc.fault_overheat_wq) {
		rc = -ENOMEM;
		goto out_free;
	}

	return 0;

out_free:
	adapter->access_ctrl.host_removing = 1;
	leapraid_fw_log_exit(adapter);
	leapraid_disable_controller(adapter);
	leapraid_free_host_memory(adapter);
	leapraid_free_dev_topo_bitmaps(adapter);
	pci_set_drvdata(adapter->pdev, NULL);
	return rc;
}

void leapraid_remove_ctrl(struct leapraid_adapter *adapter)
{
	leapraid_overheat_cleanup(adapter);
	leapraid_check_scheduled_fault_stop(adapter);
	leapraid_fw_log_stop(adapter);
	leapraid_fw_log_exit(adapter);
	leapraid_disable_controller(adapter);
	leapraid_free_host_memory(adapter);
	leapraid_free_dev_topo_bitmaps(adapter);
	leapraid_free_enc_list(adapter);
	pci_set_drvdata(adapter->pdev, NULL);
}
