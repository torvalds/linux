// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

#include "leapraid_func.h"

/* IOCTL device file name. */
#define LEAPRAID_DEV_NAME       "leapraid_ctl"

/* IOCTL version. */
#define LEAPRAID_IOCTL_VERSION  0x07

/* IOCTL commands. */
#define LEAPRAID_ADAPTER_INFO   17
#define LEAPRAID_COMMAND        20
#define LEAPRAID_EVENTQUERY     21
#define LEAPRAID_EVENTREPORT    23

/**
 * struct leapraid_ioctl_header - IOCTL command header
 *
 * @adapter_id : Adapter identifier.
 * @port_number: Port identifier.
 * @max_data_size: Maximum data size for transfer.
 */
struct leapraid_ioctl_header {
	u32 adapter_id;
	u32 port_number;
	u32 max_data_size;
};

/**
 * struct leapraid_ioctl_diag_reset - Diagnostic reset request
 *
 * @hdr: Common IOCTL header.
 */
struct leapraid_ioctl_diag_reset {
	struct leapraid_ioctl_header hdr;
};

/**
 * struct leapraid_ioctl_pci_info - PCI device information
 *
 * @u: Union holding PCI bus/device/function information.
 * @u.bits.dev: PCI device number.
 * @u.bits.func: PCI function number.
 * @u.bits.bus: PCI bus number.
 * @u.word: Combined representation of PCI BDF.
 * @seg_id: PCI segment identifier.
 */
struct leapraid_ioctl_pci_info {
	union {
		struct {
			u32 dev:5;
			u32 func:3;
			u32 bus:24;
		} bits;
		u32 word;
	} u;
	u32 seg_id;
};

/**
 * struct leapraid_ioctl_adapter_info - Adapter information for IOCTL
 *
 * @hdr: IOCTL header.
 * @adapter_type: Adapter type identifier.
 * @port_number: Port number.
 * @pci_id: PCI device ID.
 * @revision: Revision number.
 * @sub_dev: Subsystem device ID.
 * @sub_vendor: Subsystem vendor ID.
 * @r0: Reserved.
 * @fw_ver: Firmware version.
 * @bios_ver: BIOS version.
 * @driver_ver: Driver version.
 * @r1: Reserved.
 * @scsi_id: SCSI ID.
 * @r2: Reserved.
 * @pci_info: PCI information structure.
 */
struct leapraid_ioctl_adapter_info {
	struct leapraid_ioctl_header hdr;
	u32 adapter_type;
	u32 port_number;
	u32 pci_id;
	u32 revision;
	u32 sub_dev;
	u32 sub_vendor;
	u32 r0;
	u32 fw_ver;
	u32 bios_ver;
	u8 driver_ver[32];
	u8 r1;
	u8 scsi_id;
	u16 r2;
	struct leapraid_ioctl_pci_info pci_info;
};

/**
 * struct leapraid_ioctl_command - IOCTL command structure
 *
 * @hdr: IOCTL header.
 * @timeout: Command timeout.
 * @rep_msg_buf_ptr: User pointer to reply message buffer.
 * @c2h_buf_ptr: User pointer to card-to-host data buffer.
 * @h2c_buf_ptr: User pointer to host-to-card data buffer.
 * @sense_data_ptr: User pointer to sense data buffer.
 * @max_rep_bytes: Maximum reply bytes.
 * @c2h_size: Card-to-host data size.
 * @h2c_size: Host-to-card data size.
 * @max_sense_bytes: Maximum sense data bytes.
 * @data_sge_offset: Data SGE offset.
 * @mf: Message frame data (flexible array).
 */
struct leapraid_ioctl_command {
	struct leapraid_ioctl_header hdr;
	u32 timeout;
	void __user *rep_msg_buf_ptr;
	void __user *c2h_buf_ptr;
	void __user *h2c_buf_ptr;
	void __user *sense_data_ptr;
	u32 max_rep_bytes;
	u32 c2h_size;
	u32 h2c_size;
	u32 max_sense_bytes;
	u32 data_sge_offset;
	u8 mf[];
};

static int leapraid_ctl_validate_sge_offset(struct leapraid_adapter *adapter,
					    const struct leapraid_req *req,
					    u32 offset_bytes,
					    size_t h2c_size,
					    size_t c2h_size)
{
	size_t sge_bytes;

	switch (req->func) {
	case LEAPRAID_FUNC_SCSIIO:
	case LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH:
	case LEAPRAID_FUNC_SMP_PASSTHROUGH:
	case LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH:
	case LEAPRAID_FUNC_FW_DOWNLOAD:
	case LEAPRAID_FUNC_FW_UPLOAD:
		sge_bytes = LEAPRAID_IEEE_SGE64_ENTRY_SIZE;
		break;
	default:
		sge_bytes = adapter->adapter_attr.use_32_dma_mask ?
			sizeof(struct leapraid_sge_simple32) :
			sizeof(struct leapraid_sge_simple64);
		break;
	}

	if (h2c_size && c2h_size)
		sge_bytes *= 2;

	if (offset_bytes > LEAPRAID_REQUEST_SIZE - sge_bytes) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid offset_bytes=%u for func=0x%x\n",
			__func__, offset_bytes, req->func);
		return -EINVAL;
	}

	return 0;
}

static struct leapraid_adapter *leapraid_ctl_lookup_adapter(int adapter_id,
							    bool track_mmap)
{
	struct leapraid_adapter *adapter;
	struct Scsi_Host *shost;

	spin_lock(&leapraid_adapter_lock);
	list_for_each_entry(adapter, &leapraid_adapter_list, list) {
		if (adapter->adapter_attr.id == adapter_id) {
			if (READ_ONCE(adapter->access_ctrl.host_removing))
				break;
			shost = adapter->shost;
			if (!shost || !scsi_host_get(shost))
				break;
			if (track_mmap)
				atomic_inc(&adapter->fw_log_desc.mmap_refcnt);
			spin_unlock(&leapraid_adapter_lock);
			return adapter;
		}
	}
	spin_unlock(&leapraid_adapter_lock);

	return NULL;
}

static void leapraid_ctl_put_adapter(struct leapraid_adapter *adapter)
{
	if (adapter && adapter->shost)
		scsi_host_put(adapter->shost);
}

static void leapraid_ctl_scsiio_cmd(struct leapraid_adapter *adapter,
				    void *ctl_sp_mpi_req,
				    u16 taskid,
				    dma_addr_t h2c_dma_addr, size_t h2c_size,
				    dma_addr_t c2h_dma_addr, size_t c2h_size,
				    u16 dev_hdl, void *psge)
{
	struct leapraid_mpi_scsiio_req *scsiio_request = ctl_sp_mpi_req;

	scsiio_request->sense_buffer_len = SCSI_SENSE_BUFFERSIZE;
	scsiio_request->sense_buffer_low_add =
		leapraid_get_sense_buffer_dma(adapter, taskid);
	memset(&adapter->driver_cmds.ctl_cmd.sense,
	       0, SCSI_SENSE_BUFFERSIZE);
	leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr,
			       h2c_size, c2h_dma_addr, c2h_size);
	if (scsiio_request->func == LEAPRAID_FUNC_SCSIIO)
		leapraid_fire_scsi_io(adapter, taskid, dev_hdl);
	else
		leapraid_fire_task(adapter, taskid);
}

static int leapraid_ctl_smp_passthrough_cmd(struct leapraid_adapter *adapter,
					    void *ctl_sp_req,
					    u16 taskid,
					    dma_addr_t h2c_dma_addr,
					    size_t h2c_size,
					    dma_addr_t c2h_dma_addr,
					    size_t c2h_size,
					    void *psge, void *h2c)
{
	struct leapraid_smp_passthrough_req *smp_pt_req = ctl_sp_req;
	u8 *data;

	if (!adapter->adapter_attr.enable_mp)
		smp_pt_req->physical_port = LEAPRAID_DISABLE_MP_PORT_ID;
	if (smp_pt_req->passthrough_flg & LEAPRAID_SMP_PT_FLAG_SGL_PTR)
		data = (u8 *)&smp_pt_req->sgl;
	else
		data = h2c;

	if (!(smp_pt_req->passthrough_flg & LEAPRAID_SMP_PT_FLAG_SGL_PTR) &&
	    h2c_size <= 10) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid request size=%zu\n",
			__func__, h2c_size);
		return -EINVAL;
	}

	if (data[1] == SMP_PHY_CONTROL &&
	    (data[10] == SMP_PHY_CONTROL_LINK_RESET ||
	     data[10] == SMP_PHY_CONTROL_HARD_RESET))
		adapter->reset_desc.adapter_link_resetting = 1;

	leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr,
			       h2c_size, c2h_dma_addr, c2h_size);
	leapraid_fire_task(adapter, taskid);
	return 0;
}

static void leapraid_ctl_sas_io_unit_ctrl_cmd(struct leapraid_adapter *adapter,
					      void *ctl_sp_req,
					      dma_addr_t h2c_dma_addr,
					      size_t h2c_size,
					      dma_addr_t c2h_dma_addr,
					      size_t c2h_size,
					      void *psge, u16 taskid)
{
	struct leapraid_sas_io_unit_ctrl_req *sas_io_unit_ctl_req = ctl_sp_req;

	if (sas_io_unit_ctl_req->op == LEAPRAID_SAS_OP_PHY_HARD_RESET ||
	    sas_io_unit_ctl_req->op == LEAPRAID_SAS_OP_PHY_LINK_RESET)
		adapter->reset_desc.adapter_link_resetting = 1;
	leapraid_build_mpi_sg(adapter, psge, h2c_dma_addr,
			      h2c_size, c2h_dma_addr, c2h_size);
	leapraid_fire_task(adapter, taskid);
}

static int leapraid_ctl_do_command(struct leapraid_adapter *adapter,
				   struct leapraid_ioctl_command *karg,
				   void __user *mf)
{
	struct leapraid_req *leap_mpi_req;
	struct leapraid_req *ctl_sp_mpi_req;
	u16 taskid;
	void *h2c = NULL;
	size_t h2c_size = 0;
	dma_addr_t h2c_dma_addr = 0;
	void *c2h = NULL;
	size_t c2h_size = 0;
	dma_addr_t c2h_dma_addr = 0;
	void *psge;
	unsigned long timeout;
	u16 dev_hdl = LEAPRAID_INVALID_DEV_HANDLE;
	bool issue_reset = false;
	u32 data_sge_offset_bytes;
	u32 sz;
	int rc;

	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_SHORT,
					  __func__);
	if (rc)
		return rc;

	leap_mpi_req = kzalloc(LEAPRAID_REQUEST_SIZE, GFP_KERNEL);
	if (!leap_mpi_req)
		return -ENOMEM;

	if (karg->data_sge_offset > (UINT_MAX / LEAPRAID_SGE_OFFSET_SIZE)) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid data_sge_offset=%u\n",
			__func__, karg->data_sge_offset);
		rc = -EINVAL;
		goto out_cleanup;
	}

	data_sge_offset_bytes = karg->data_sge_offset *
				LEAPRAID_SGE_OFFSET_SIZE;
	if (data_sge_offset_bytes > LEAPRAID_REQUEST_SIZE) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid data_sge_offset=%u\n",
			__func__, karg->data_sge_offset);
		rc = -EINVAL;
		goto out_cleanup;
	}

	if (copy_from_user(leap_mpi_req, mf, data_sge_offset_bytes)) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to copy request message from user\n",
			__func__);
		rc = -EFAULT;
		goto out_cleanup;
	}

	h2c_size = karg->h2c_size;
	c2h_size = karg->c2h_size;
	rc = leapraid_ctl_validate_sge_offset(adapter, leap_mpi_req,
					      data_sge_offset_bytes,
					      h2c_size, c2h_size);
	if (rc)
		goto out_cleanup;

	taskid = adapter->driver_cmds.ctl_cmd.taskid;

	adapter->driver_cmds.ctl_cmd.status = LEAPRAID_CMD_PENDING;
	memset(&adapter->driver_cmds.ctl_cmd.reply, 0, LEAPRAID_REPLY_SIZE);
	ctl_sp_mpi_req = leapraid_get_task_desc(adapter, taskid);
	memset(ctl_sp_mpi_req, 0, LEAPRAID_REQUEST_SIZE);
	memcpy(ctl_sp_mpi_req, leap_mpi_req, data_sge_offset_bytes);

	if (ctl_sp_mpi_req->func == LEAPRAID_FUNC_SCSIIO ||
	    ctl_sp_mpi_req->func == LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH ||
	    ctl_sp_mpi_req->func == LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH) {
		dev_hdl = le16_to_cpu(ctl_sp_mpi_req->func_dep1);
		if (!dev_hdl ||
		    dev_hdl > adapter->adapter_attr.features.max_dev_handle) {
			dev_err(&adapter->pdev->dev,
				"%s: Invalid device handle\n", __func__);
			rc = -EINVAL;
			goto out_cleanup;
		}
	}

	if (WARN_ON(ctl_sp_mpi_req->func == LEAPRAID_FUNC_SCSI_TMF)) {
		rc = -EINVAL;
		goto out_cleanup;
	}

	if (h2c_size) {
		h2c = dma_alloc_coherent(&adapter->pdev->dev, h2c_size,
					 &h2c_dma_addr, GFP_KERNEL);
		if (!h2c) {
			rc = -ENOMEM;
			goto out_cleanup;
		}
		if (copy_from_user(h2c, karg->h2c_buf_ptr, h2c_size)) {
			dev_err(&adapter->pdev->dev,
				"%s: Failed to copy h2c from user\n",
				__func__);
			rc = -EFAULT;
			goto out_cleanup;
		}
	}
	if (c2h_size) {
		c2h = dma_alloc_coherent(&adapter->pdev->dev, c2h_size,
					 &c2h_dma_addr, GFP_KERNEL);
		if (!c2h) {
			rc = -ENOMEM;
			goto out_cleanup;
		}
	}

	psge = (void *)ctl_sp_mpi_req + data_sge_offset_bytes;
	init_completion(&adapter->driver_cmds.ctl_cmd.done);

	switch (ctl_sp_mpi_req->func) {
	case LEAPRAID_FUNC_SCSIIO:
	case LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH:
		leapraid_ctl_scsiio_cmd(adapter, ctl_sp_mpi_req, taskid,
					h2c_dma_addr, h2c_size,
					c2h_dma_addr, c2h_size,
					dev_hdl, psge);
		break;
	case LEAPRAID_FUNC_SMP_PASSTHROUGH:
		if (!h2c) {
			rc = -EINVAL;
			goto out_cleanup;
		}
		rc = leapraid_ctl_smp_passthrough_cmd(adapter,
						      ctl_sp_mpi_req, taskid,
						      h2c_dma_addr, h2c_size,
						      c2h_dma_addr, c2h_size,
						      psge, h2c);
		if (rc)
			goto out_cleanup;
		break;
	case LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH:
		leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr, h2c_size,
				       c2h_dma_addr, c2h_size);
		leapraid_fire_task(adapter, taskid);
		break;
	case LEAPRAID_FUNC_FW_DOWNLOAD:
	case LEAPRAID_FUNC_FW_UPLOAD:
		leapraid_build_ieee_sg(adapter, psge, h2c_dma_addr, h2c_size,
				       c2h_dma_addr, c2h_size);
		leapraid_fire_task(adapter, taskid);
		break;
	case LEAPRAID_FUNC_SAS_IO_UNIT_CTRL:
		leapraid_ctl_sas_io_unit_ctrl_cmd(adapter, ctl_sp_mpi_req,
						  h2c_dma_addr, h2c_size,
						  c2h_dma_addr, c2h_size,
						  psge, taskid);
		break;
	default:
		leapraid_build_mpi_sg(adapter, psge, h2c_dma_addr,
				      h2c_size, c2h_dma_addr, c2h_size);
		leapraid_fire_task(adapter, taskid);
		break;
	}

	timeout = karg->timeout;
	if (timeout < LEAPRAID_CTL_CMD_TIMEOUT)
		timeout = LEAPRAID_CTL_CMD_TIMEOUT;
	if (!wait_for_completion_timeout(&adapter->driver_cmds.ctl_cmd.done,
					 timeout * HZ)) {
		dev_err(&adapter->pdev->dev,
			"%s: ctl_cmd timeout, status=0x%x\n",
			__func__, adapter->driver_cmds.ctl_cmd.status);
		leapraid_log_req_context(adapter, taskid, ctl_sp_mpi_req);
	}

	if ((leap_mpi_req->func == LEAPRAID_FUNC_SMP_PASSTHROUGH ||
	     leap_mpi_req->func == LEAPRAID_FUNC_SAS_IO_UNIT_CTRL) &&
	    adapter->reset_desc.adapter_link_resetting)
		adapter->reset_desc.adapter_link_resetting = 0;

	if (!(adapter->driver_cmds.ctl_cmd.status & LEAPRAID_CMD_DONE)) {
		issue_reset =
			leapraid_check_reset(
				adapter->driver_cmds.ctl_cmd.status);
		goto reset;
	}

	if (c2h_size && copy_to_user(karg->c2h_buf_ptr, c2h, c2h_size)) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to copy c2h to user\n", __func__);
		rc = -ENODATA;
		goto out_cleanup;
	}
	if (karg->max_rep_bytes) {
		sz = min_t(u32, karg->max_rep_bytes, LEAPRAID_REPLY_SIZE);
		if (copy_to_user(karg->rep_msg_buf_ptr,
				 &adapter->driver_cmds.ctl_cmd.reply,
				 sz)) {
			dev_err(&adapter->pdev->dev,
				"%s: Failed to copy reply to user\n",
				__func__);
			rc = -ENODATA;
			goto out_cleanup;
		}
	}

	if (karg->max_sense_bytes &&
	    (leap_mpi_req->func == LEAPRAID_FUNC_SCSIIO ||
	     leap_mpi_req->func == LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH)) {
		if (!karg->sense_data_ptr)
			goto out_cleanup;

		sz = min_t(u32, karg->max_sense_bytes, SCSI_SENSE_BUFFERSIZE);
		if (copy_to_user(karg->sense_data_ptr,
				 &adapter->driver_cmds.ctl_cmd.sense,
				 sz)) {
			dev_err(&adapter->pdev->dev,
				"%s: Failed to copy sense data to user\n",
				__func__);
			rc = -ENODATA;
			goto out_cleanup;
		}
	}
reset:
	if (issue_reset) {
		rc = -ENODATA;
		if (leap_mpi_req->func == LEAPRAID_FUNC_SCSIIO ||
		    leap_mpi_req->func ==
		    LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH ||
		    leap_mpi_req->func ==
		    LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH) {
			dev_err(&adapter->pdev->dev,
				"Fire tgt reset, hdl=0x%04x\n",
				le16_to_cpu(leap_mpi_req->func_dep1));
			leapraid_issue_locked_tm(
				adapter,
				le16_to_cpu(leap_mpi_req->func_dep1), 0, 0, 0,
				LEAPRAID_TM_TASKTYPE_TARGET_RESET, taskid,
				LEAPRAID_TM_MSGFLAGS_LINK_RESET);
		} else {
			dev_info(&adapter->pdev->dev,
				 "%s:%d: call hard_reset\n",
				 __func__, __LINE__);
			leapraid_hard_reset_handler(adapter, FULL_RESET);
		}
	}
out_cleanup:
	if (c2h)
		dma_free_coherent(&adapter->pdev->dev, c2h_size,
				  c2h, c2h_dma_addr);
	if (h2c)
		dma_free_coherent(&adapter->pdev->dev, h2c_size,
				  h2c, h2c_dma_addr);
	kfree(leap_mpi_req);
	adapter->driver_cmds.ctl_cmd.status = LEAPRAID_CMD_NOT_USED;
	return rc;
}

static int leapraid_ctl_get_adapter_info(struct leapraid_adapter *adapter,
					 void __user *arg)
{
	struct leapraid_ioctl_adapter_info *karg;
	u8 revision;
	int rc = 0;

	karg = kzalloc_obj(*karg);
	if (!karg)
		return -ENOMEM;

	pci_read_config_byte(adapter->pdev, PCI_CLASS_REVISION, &revision);
	karg->revision = revision;
	karg->pci_id = adapter->pdev->device;
	karg->sub_dev = adapter->pdev->subsystem_device;
	karg->sub_vendor = adapter->pdev->subsystem_vendor;
	karg->pci_info.u.bits.bus = adapter->pdev->bus->number;
	karg->pci_info.u.bits.dev = PCI_SLOT(adapter->pdev->devfn);
	karg->pci_info.u.bits.func = PCI_FUNC(adapter->pdev->devfn);
	karg->pci_info.seg_id = pci_domain_nr(adapter->pdev->bus);
	karg->fw_ver = adapter->adapter_attr.features.fw_version;

	snprintf(karg->driver_ver, sizeof(karg->driver_ver), "%s-%s",
		 LEAPRAID_DRIVER_NAME, LEAPRAID_DRIVER_VERSION);

	karg->adapter_type = LEAPRAID_IOCTL_VERSION;
	karg->bios_ver = adapter->adapter_attr.bios_version;
	if (copy_to_user(arg, karg,
			 sizeof(struct leapraid_ioctl_adapter_info))) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to copy info to user\n", __func__);
		rc = -EFAULT;
		goto free_buf;
	}

free_buf:
	kfree(karg);
	return rc;
}

static int leapraid_ctl_ioctl_main(struct file *file, unsigned int cmd,
				   void __user *arg)
{
	struct leapraid_ioctl_header ioctl_header;
	struct leapraid_adapter *adapter = NULL;
	struct leapraid_ioctl_command __user *uarg;
	struct leapraid_ioctl_command karg;
	int rc = -ENOIOCTLCMD;

	if (copy_from_user(&ioctl_header, arg,
			   sizeof(struct leapraid_ioctl_header))) {
		pr_err("%s:%s: Failed to copy header from user\n",
		       LEAPRAID_DRIVER_NAME, __func__);
		return -EFAULT;
	}

	adapter = leapraid_ctl_lookup_adapter(ioctl_header.adapter_id, false);
	if (!adapter)
		return -EFAULT;

	if (atomic_read(&adapter->overheat_desc.thermal_alert)) {
		dev_warn(&adapter->pdev->dev,
			 "%s: Failed, thermal_alert=%d\n",
			 __func__,
			 atomic_read(&adapter->overheat_desc.thermal_alert));
		rc = -EFAULT;
		goto out_put;
	}

	mutex_lock(&adapter->access_ctrl.pci_access_lock);

	rc = leapraid_check_adapter_is_op(adapter, LEAPRAID_DB_WAIT_OP_LONG,
					  __func__);
	if (rc)
		goto unlock;

	if (!wait_event_timeout(adapter->access_ctrl.shost_recover_wq,
				!adapter->access_ctrl.shost_recover_async,
				LEAPRAID_WAIT_SHOST_RECOVERY * HZ)) {
		dev_warn(&adapter->pdev->dev,
			 "Timeout waiting for shost recovery async\n");
		rc = -EAGAIN;
		goto unlock;
	}

	if (adapter->access_ctrl.pcie_recovering ||
	    adapter->scan_dev_desc.driver_loading ||
	    adapter->access_ctrl.host_removing) {
		rc = -EAGAIN;
		goto unlock;
	}

	if (file->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&adapter->driver_cmds.ctl_cmd.mutex)) {
			rc = -EAGAIN;
			goto unlock;
		}
	} else if (mutex_lock_interruptible(&adapter->driver_cmds
					    .ctl_cmd.mutex)) {
		rc = -ERESTARTSYS;
		goto unlock;
	}

	switch (_IOC_NR(cmd)) {
	case LEAPRAID_ADAPTER_INFO:
		if (_IOC_SIZE(cmd) ==
		    sizeof(struct leapraid_ioctl_adapter_info))
			rc = leapraid_ctl_get_adapter_info(adapter, arg);
		break;
	case LEAPRAID_COMMAND:
		if (copy_from_user(&karg, arg, sizeof(karg))) {
			dev_err(&adapter->pdev->dev,
				"%s: Failed to copy data from user\n",
				__func__);
			rc = -EFAULT;
			break;
		}

		if (karg.hdr.adapter_id != ioctl_header.adapter_id) {
			rc = -EINVAL;
			break;
		}

		if (_IOC_SIZE(cmd) == sizeof(struct leapraid_ioctl_command)) {
			uarg = arg;
			rc = leapraid_ctl_do_command(adapter, &karg,
						     &uarg->mf);
			if (rc)
				dev_warn(&adapter->pdev->dev,
					 "%s: IOCTL cmd failed rc=%d\n",
					 __func__, rc);
		}
		break;
	case LEAPRAID_EVENTQUERY:
	case LEAPRAID_EVENTREPORT:
		rc = 0;
		break;
	default:
		dev_err(&adapter->pdev->dev,
			"Unknown IOCTL opcode=0x%08x\n", cmd);
		break;
	}
	mutex_unlock(&adapter->driver_cmds.ctl_cmd.mutex);

unlock:
	mutex_unlock(&adapter->access_ctrl.pci_access_lock);
out_put:
	leapraid_ctl_put_adapter(adapter);
	return rc;
}

static long leapraid_ctl_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	return leapraid_ctl_ioctl_main(file, cmd, (void __user *)arg);
}

static void leapraid_fw_mmap_open(struct vm_area_struct *vma)
{
	struct leapraid_adapter *adapter = vma->vm_private_data;

	if (!adapter)
		return;

	get_device(&adapter->shost->shost_gendev);
	atomic_inc(&adapter->fw_log_desc.mmap_refcnt);
}

static void leapraid_fw_mmap_close(struct vm_area_struct *vma)
{
	struct leapraid_adapter *adapter = vma->vm_private_data;

	if (!adapter)
		return;

	if (atomic_dec_and_test(&adapter->fw_log_desc.mmap_refcnt))
		wake_up(&adapter->fw_log_desc.mmap_waitq);
	leapraid_ctl_put_adapter(adapter);
}

static const struct vm_operations_struct leapraid_fw_mmap_vm_ops = {
	.open = leapraid_fw_mmap_open,
	.close = leapraid_fw_mmap_close,
};

static int leapraid_fw_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct leapraid_adapter *adapter = NULL;
	/* Userspace passes the adapter ID via vma->vm_pgoff. */
	u32 adapter_id = vma->vm_pgoff;
	unsigned long length;
	int rc = -EINVAL;

	length = vma->vm_end - vma->vm_start;

	adapter = leapraid_ctl_lookup_adapter(adapter_id, true);
	if (!adapter) {
		pr_err("%s: No adapter found!\n", __func__);
		return -EINVAL;
	}

	if (READ_ONCE(adapter->access_ctrl.host_removing)) {
		rc = -EAGAIN;
		goto out_put;
	}

	if (length > (LEAPRAID_SYS_LOG_BUF_SIZE +
		      LEAPRAID_SYS_LOG_BUF_RESERVE)) {
		dev_err(&adapter->pdev->dev,
			"Requested mapping size is too large!\n");
		rc = -EINVAL;
		goto out_put;
	}

	if (!adapter->fw_log_desc.fw_log_buffer) {
		dev_err(&adapter->pdev->dev, "No log buffer!\n");
		rc = -EINVAL;
		goto out_put;
	}

	vma->vm_pgoff = 0;

	rc = dma_mmap_coherent(&adapter->pdev->dev, vma,
			       adapter->fw_log_desc.fw_log_buffer,
			       adapter->fw_log_desc.fw_log_buffer_dma,
			       length);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"Failed to map memory to user space!\n");
		goto out_put;
	}

	vma->vm_private_data = adapter;
	vma->vm_ops = &leapraid_fw_mmap_vm_ops;
	leapraid_fw_mmap_open(vma);

	rc = 0;
out_put:
	if (adapter &&
	    atomic_dec_and_test(&adapter->fw_log_desc.mmap_refcnt))
		wake_up(&adapter->fw_log_desc.mmap_waitq);
	leapraid_ctl_put_adapter(adapter);
	return rc;
}

static const struct file_operations leapraid_ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = leapraid_ctl_ioctl,
	.mmap = leapraid_fw_mmap,
};

static struct miscdevice leapraid_ctl_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = LEAPRAID_DEV_NAME,
	.fops = &leapraid_ctl_fops,
};

int leapraid_ctl_init(void)
{
	if (misc_register(&leapraid_ctl_dev) < 0) {
		pr_err("%s Can't register misc device\n",
		       LEAPRAID_DRIVER_NAME);
		return -ENODEV;
	}
	return 0;
}

void leapraid_ctl_exit(void)
{
	misc_deregister(&leapraid_ctl_dev);
}
