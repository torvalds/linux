/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_target.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/delay.h>

static int qla24xx_vport_disable(struct fc_vport *, bool);

/* SYSFS attributes --------------------------------------------------------- */

static ssize_t
qla2x00_sysfs_read_fw_dump(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;

	if (!(ha->fw_dump_reading || ha->mctp_dump_reading))
		return 0;

	if (IS_P3P_TYPE(ha)) {
		if (off < ha->md_template_size) {
			rval = memory_read_from_buffer(buf, count,
			    &off, ha->md_tmplt_hdr, ha->md_template_size);
			return rval;
		}
		off -= ha->md_template_size;
		rval = memory_read_from_buffer(buf, count,
		    &off, ha->md_dump, ha->md_dump_size);
		return rval;
	} else if (ha->mctp_dumped && ha->mctp_dump_reading)
		return memory_read_from_buffer(buf, count, &off, ha->mctp_dump,
		    MCTP_DUMP_SIZE);
	else if (ha->fw_dump_reading)
		return memory_read_from_buffer(buf, count, &off, ha->fw_dump,
					ha->fw_dump_len);
	else
		return 0;
}

static ssize_t
qla2x00_sysfs_write_fw_dump(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr,
			    char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int reading;

	if (off != 0)
		return (0);

	reading = simple_strtol(buf, NULL, 10);
	switch (reading) {
	case 0:
		if (!ha->fw_dump_reading)
			break;

		ql_log(ql_log_info, vha, 0x705d,
		    "Firmware dump cleared on (%ld).\n", vha->host_no);

		if (IS_P3P_TYPE(ha)) {
			qla82xx_md_free(vha);
			qla82xx_md_prep(vha);
		}
		ha->fw_dump_reading = 0;
		ha->fw_dumped = 0;
		break;
	case 1:
		if (ha->fw_dumped && !ha->fw_dump_reading) {
			ha->fw_dump_reading = 1;

			ql_log(ql_log_info, vha, 0x705e,
			    "Raw firmware dump ready for read on (%ld).\n",
			    vha->host_no);
		}
		break;
	case 2:
		qla2x00_alloc_fw_dump(vha);
		break;
	case 3:
		if (IS_QLA82XX(ha)) {
			qla82xx_idc_lock(ha);
			qla82xx_set_reset_owner(vha);
			qla82xx_idc_unlock(ha);
		} else if (IS_QLA8044(ha)) {
			qla8044_idc_lock(ha);
			qla82xx_set_reset_owner(vha);
			qla8044_idc_unlock(ha);
		} else
			qla2x00_system_error(vha);
		break;
	case 4:
		if (IS_P3P_TYPE(ha)) {
			if (ha->md_tmplt_hdr)
				ql_dbg(ql_dbg_user, vha, 0x705b,
				    "MiniDump supported with this firmware.\n");
			else
				ql_dbg(ql_dbg_user, vha, 0x709d,
				    "MiniDump not supported with this firmware.\n");
		}
		break;
	case 5:
		if (IS_P3P_TYPE(ha))
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;
	case 6:
		if (!ha->mctp_dump_reading)
			break;
		ql_log(ql_log_info, vha, 0x70c1,
		    "MCTP dump cleared on (%ld).\n", vha->host_no);
		ha->mctp_dump_reading = 0;
		ha->mctp_dumped = 0;
		break;
	case 7:
		if (ha->mctp_dumped && !ha->mctp_dump_reading) {
			ha->mctp_dump_reading = 1;
			ql_log(ql_log_info, vha, 0x70c2,
			    "Raw mctp dump ready for read on (%ld).\n",
			    vha->host_no);
		}
		break;
	}
	return count;
}

static struct bin_attribute sysfs_fw_dump_attr = {
	.attr = {
		.name = "fw_dump",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_fw_dump,
	.write = qla2x00_sysfs_write_fw_dump,
};

static ssize_t
qla2x00_sysfs_read_nvram(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr,
			 char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (IS_NOCACHE_VPD_TYPE(ha))
		ha->isp_ops->read_optrom(vha, ha->nvram, ha->flt_region_nvram << 2,
		    ha->nvram_size);
	return memory_read_from_buffer(buf, count, &off, ha->nvram,
					ha->nvram_size);
}

static ssize_t
qla2x00_sysfs_write_nvram(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint16_t	cnt;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->nvram_size ||
	    !ha->isp_ops->write_nvram)
		return -EINVAL;

	/* Checksum NVRAM. */
	if (IS_FWI2_CAPABLE(ha)) {
		uint32_t *iter;
		uint32_t chksum;

		iter = (uint32_t *)buf;
		chksum = 0;
		for (cnt = 0; cnt < ((count >> 2) - 1); cnt++, iter++)
			chksum += le32_to_cpu(*iter);
		chksum = ~chksum + 1;
		*iter = cpu_to_le32(chksum);
	} else {
		uint8_t *iter;
		uint8_t chksum;

		iter = (uint8_t *)buf;
		chksum = 0;
		for (cnt = 0; cnt < count - 1; cnt++)
			chksum += *iter++;
		chksum = ~chksum + 1;
		*iter = chksum;
	}

	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x705f,
		    "HBA not online, failing NVRAM update.\n");
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, (uint8_t *)buf, ha->nvram_base, count);
	ha->isp_ops->read_nvram(vha, (uint8_t *)ha->nvram, ha->nvram_base,
	    count);

	ql_dbg(ql_dbg_user, vha, 0x7060,
	    "Setting ISP_ABORT_NEEDED\n");
	/* NVRAM settings take effect immediately. */
	set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	qla2xxx_wake_dpc(vha);
	qla2x00_wait_for_chip_reset(vha);

	return count;
}

static struct bin_attribute sysfs_nvram_attr = {
	.attr = {
		.name = "nvram",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 512,
	.read = qla2x00_sysfs_read_nvram,
	.write = qla2x00_sysfs_write_nvram,
};

static ssize_t
qla2x00_sysfs_read_optrom(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	ssize_t rval = 0;

	mutex_lock(&ha->optrom_mutex);

	if (ha->optrom_state != QLA_SREADING)
		goto out;

	rval = memory_read_from_buffer(buf, count, &off, ha->optrom_buffer,
	    ha->optrom_region_size);

out:
	mutex_unlock(&ha->optrom_mutex);

	return rval;
}

static ssize_t
qla2x00_sysfs_write_optrom(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	mutex_lock(&ha->optrom_mutex);

	if (ha->optrom_state != QLA_SWRITING) {
		mutex_unlock(&ha->optrom_mutex);
		return -EINVAL;
	}
	if (off > ha->optrom_region_size) {
		mutex_unlock(&ha->optrom_mutex);
		return -ERANGE;
	}
	if (off + count > ha->optrom_region_size)
		count = ha->optrom_region_size - off;

	memcpy(&ha->optrom_buffer[off], buf, count);
	mutex_unlock(&ha->optrom_mutex);

	return count;
}

static struct bin_attribute sysfs_optrom_attr = {
	.attr = {
		.name = "optrom",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_optrom,
	.write = qla2x00_sysfs_write_optrom,
};

static ssize_t
qla2x00_sysfs_write_optrom_ctl(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint32_t start = 0;
	uint32_t size = ha->optrom_size;
	int val, valid;
	ssize_t rval = count;

	if (off)
		return -EINVAL;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -EAGAIN;

	if (sscanf(buf, "%d:%x:%x", &val, &start, &size) < 1)
		return -EINVAL;
	if (start > ha->optrom_size)
		return -EINVAL;

	mutex_lock(&ha->optrom_mutex);
	switch (val) {
	case 0:
		if (ha->optrom_state != QLA_SREADING &&
		    ha->optrom_state != QLA_SWRITING) {
			rval =  -EINVAL;
			goto out;
		}
		ha->optrom_state = QLA_SWAITING;

		ql_dbg(ql_dbg_user, vha, 0x7061,
		    "Freeing flash region allocation -- 0x%x bytes.\n",
		    ha->optrom_region_size);

		vfree(ha->optrom_buffer);
		ha->optrom_buffer = NULL;
		break;
	case 1:
		if (ha->optrom_state != QLA_SWAITING) {
			rval = -EINVAL;
			goto out;
		}

		ha->optrom_region_start = start;
		ha->optrom_region_size = start + size > ha->optrom_size ?
		    ha->optrom_size - start : size;

		ha->optrom_state = QLA_SREADING;
		ha->optrom_buffer = vmalloc(ha->optrom_region_size);
		if (ha->optrom_buffer == NULL) {
			ql_log(ql_log_warn, vha, 0x7062,
			    "Unable to allocate memory for optrom retrieval "
			    "(%x).\n", ha->optrom_region_size);

			ha->optrom_state = QLA_SWAITING;
			rval = -ENOMEM;
			goto out;
		}

		if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x7063,
			    "HBA not online, failing NVRAM update.\n");
			rval = -EAGAIN;
			goto out;
		}

		ql_dbg(ql_dbg_user, vha, 0x7064,
		    "Reading flash region -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size);

		memset(ha->optrom_buffer, 0, ha->optrom_region_size);
		ha->isp_ops->read_optrom(vha, ha->optrom_buffer,
		    ha->optrom_region_start, ha->optrom_region_size);
		break;
	case 2:
		if (ha->optrom_state != QLA_SWAITING) {
			rval = -EINVAL;
			goto out;
		}

		/*
		 * We need to be more restrictive on which FLASH regions are
		 * allowed to be updated via user-space.  Regions accessible
		 * via this method include:
		 *
		 * ISP21xx/ISP22xx/ISP23xx type boards:
		 *
		 * 	0x000000 -> 0x020000 -- Boot code.
		 *
		 * ISP2322/ISP24xx type boards:
		 *
		 * 	0x000000 -> 0x07ffff -- Boot code.
		 * 	0x080000 -> 0x0fffff -- Firmware.
		 *
		 * ISP25xx type boards:
		 *
		 * 	0x000000 -> 0x07ffff -- Boot code.
		 * 	0x080000 -> 0x0fffff -- Firmware.
		 * 	0x120000 -> 0x12ffff -- VPD and HBA parameters.
		 */
		valid = 0;
		if (ha->optrom_size == OPTROM_SIZE_2300 && start == 0)
			valid = 1;
		else if (start == (ha->flt_region_boot * 4) ||
		    start == (ha->flt_region_fw * 4))
			valid = 1;
		else if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha)
			|| IS_CNA_CAPABLE(ha) || IS_QLA2031(ha)
			|| IS_QLA27XX(ha))
			valid = 1;
		if (!valid) {
			ql_log(ql_log_warn, vha, 0x7065,
			    "Invalid start region 0x%x/0x%x.\n", start, size);
			rval = -EINVAL;
			goto out;
		}

		ha->optrom_region_start = start;
		ha->optrom_region_size = start + size > ha->optrom_size ?
		    ha->optrom_size - start : size;

		ha->optrom_state = QLA_SWRITING;
		ha->optrom_buffer = vmalloc(ha->optrom_region_size);
		if (ha->optrom_buffer == NULL) {
			ql_log(ql_log_warn, vha, 0x7066,
			    "Unable to allocate memory for optrom update "
			    "(%x)\n", ha->optrom_region_size);

			ha->optrom_state = QLA_SWAITING;
			rval = -ENOMEM;
			goto out;
		}

		ql_dbg(ql_dbg_user, vha, 0x7067,
		    "Staging flash region write -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size);

		memset(ha->optrom_buffer, 0, ha->optrom_region_size);
		break;
	case 3:
		if (ha->optrom_state != QLA_SWRITING) {
			rval = -EINVAL;
			goto out;
		}

		if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x7068,
			    "HBA not online, failing flash update.\n");
			rval = -EAGAIN;
			goto out;
		}

		ql_dbg(ql_dbg_user, vha, 0x7069,
		    "Writing flash region -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size);

		ha->isp_ops->write_optrom(vha, ha->optrom_buffer,
		    ha->optrom_region_start, ha->optrom_region_size);
		break;
	default:
		rval = -EINVAL;
	}

out:
	mutex_unlock(&ha->optrom_mutex);
	return rval;
}

static struct bin_attribute sysfs_optrom_ctl_attr = {
	.attr = {
		.name = "optrom_ctl",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = qla2x00_sysfs_write_optrom_ctl,
};

static ssize_t
qla2x00_sysfs_read_vpd(struct file *filp, struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint32_t faddr;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -EAGAIN;

	if (!capable(CAP_SYS_ADMIN))
		return -EINVAL;

	if (IS_NOCACHE_VPD_TYPE(ha)) {
		faddr = ha->flt_region_vpd << 2;

		if (IS_QLA27XX(ha) &&
		    qla27xx_find_valid_image(vha) == QLA27XX_SECONDARY_IMAGE)
			faddr = ha->flt_region_vpd_sec << 2;

		ha->isp_ops->read_optrom(vha, ha->vpd, faddr,
		    ha->vpd_size);
	}
	return memory_read_from_buffer(buf, count, &off, ha->vpd, ha->vpd_size);
}

static ssize_t
qla2x00_sysfs_write_vpd(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint8_t *tmp_data;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return 0;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->vpd_size ||
	    !ha->isp_ops->write_nvram)
		return 0;

	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x706a,
		    "HBA not online, failing VPD update.\n");
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, (uint8_t *)buf, ha->vpd_base, count);
	ha->isp_ops->read_nvram(vha, (uint8_t *)ha->vpd, ha->vpd_base, count);

	/* Update flash version information for 4Gb & above. */
	if (!IS_FWI2_CAPABLE(ha))
		return -EINVAL;

	tmp_data = vmalloc(256);
	if (!tmp_data) {
		ql_log(ql_log_warn, vha, 0x706b,
		    "Unable to allocate memory for VPD information update.\n");
		return -ENOMEM;
	}
	ha->isp_ops->get_flash_version(vha, tmp_data);
	vfree(tmp_data);

	return count;
}

static struct bin_attribute sysfs_vpd_attr = {
	.attr = {
		.name = "vpd",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_vpd,
	.write = qla2x00_sysfs_write_vpd,
};

static ssize_t
qla2x00_sysfs_read_sfp(struct file *filp, struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint16_t iter, addr, offset;
	int rval;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != SFP_DEV_SIZE * 2)
		return 0;

	if (ha->sfp_data)
		goto do_read;

	ha->sfp_data = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
	    &ha->sfp_data_dma);
	if (!ha->sfp_data) {
		ql_log(ql_log_warn, vha, 0x706c,
		    "Unable to allocate memory for SFP read-data.\n");
		return 0;
	}

do_read:
	memset(ha->sfp_data, 0, SFP_BLOCK_SIZE);
	addr = 0xa0;
	for (iter = 0, offset = 0; iter < (SFP_DEV_SIZE * 2) / SFP_BLOCK_SIZE;
	    iter++, offset += SFP_BLOCK_SIZE) {
		if (iter == 4) {
			/* Skip to next device address. */
			addr = 0xa2;
			offset = 0;
		}

		rval = qla2x00_read_sfp(vha, ha->sfp_data_dma, ha->sfp_data,
		    addr, offset, SFP_BLOCK_SIZE, BIT_1);
		if (rval != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x706d,
			    "Unable to read SFP data (%x/%x/%x).\n", rval,
			    addr, offset);

			return -EIO;
		}
		memcpy(buf, ha->sfp_data, SFP_BLOCK_SIZE);
		buf += SFP_BLOCK_SIZE;
	}

	return count;
}

static struct bin_attribute sysfs_sfp_attr = {
	.attr = {
		.name = "sfp",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = SFP_DEV_SIZE * 2,
	.read = qla2x00_sysfs_read_sfp,
};

static ssize_t
qla2x00_sysfs_write_reset(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	int type;
	uint32_t idc_control;
	uint8_t *tmp_data = NULL;
	if (off != 0)
		return -EINVAL;

	type = simple_strtol(buf, NULL, 10);
	switch (type) {
	case 0x2025c:
		ql_log(ql_log_info, vha, 0x706e,
		    "Issuing ISP reset.\n");

		scsi_block_requests(vha->host);
		if (IS_QLA82XX(ha)) {
			ha->flags.isp82xx_no_md_cap = 1;
			qla82xx_idc_lock(ha);
			qla82xx_set_reset_owner(vha);
			qla82xx_idc_unlock(ha);
		} else if (IS_QLA8044(ha)) {
			qla8044_idc_lock(ha);
			idc_control = qla8044_rd_reg(ha,
			    QLA8044_IDC_DRV_CTRL);
			qla8044_wr_reg(ha, QLA8044_IDC_DRV_CTRL,
			    (idc_control | GRACEFUL_RESET_BIT1));
			qla82xx_set_reset_owner(vha);
			qla8044_idc_unlock(ha);
		} else {
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		qla2x00_wait_for_chip_reset(vha);
		scsi_unblock_requests(vha->host);
		break;
	case 0x2025d:
		if (!IS_QLA81XX(ha) && !IS_QLA83XX(ha))
			return -EPERM;

		ql_log(ql_log_info, vha, 0x706f,
		    "Issuing MPI reset.\n");

		if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
			uint32_t idc_control;

			qla83xx_idc_lock(vha, 0);
			__qla83xx_get_idc_control(vha, &idc_control);
			idc_control |= QLA83XX_IDC_GRACEFUL_RESET;
			__qla83xx_set_idc_control(vha, idc_control);
			qla83xx_wr_reg(vha, QLA83XX_IDC_DEV_STATE,
			    QLA8XXX_DEV_NEED_RESET);
			qla83xx_idc_audit(vha, IDC_AUDIT_TIMESTAMP);
			qla83xx_idc_unlock(vha, 0);
			break;
		} else {
			/* Make sure FC side is not in reset */
			qla2x00_wait_for_hba_online(vha);

			/* Issue MPI reset */
			scsi_block_requests(vha->host);
			if (qla81xx_restart_mpi_firmware(vha) != QLA_SUCCESS)
				ql_log(ql_log_warn, vha, 0x7070,
				    "MPI reset failed.\n");
			scsi_unblock_requests(vha->host);
			break;
		}
	case 0x2025e:
		if (!IS_P3P_TYPE(ha) || vha != base_vha) {
			ql_log(ql_log_info, vha, 0x7071,
			    "FCoE ctx reset no supported.\n");
			return -EPERM;
		}

		ql_log(ql_log_info, vha, 0x7072,
		    "Issuing FCoE ctx reset.\n");
		set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
		qla2x00_wait_for_fcoe_ctx_reset(vha);
		break;
	case 0x2025f:
		if (!IS_QLA8031(ha))
			return -EPERM;
		ql_log(ql_log_info, vha, 0x70bc,
		    "Disabling Reset by IDC control\n");
		qla83xx_idc_lock(vha, 0);
		__qla83xx_get_idc_control(vha, &idc_control);
		idc_control |= QLA83XX_IDC_RESET_DISABLED;
		__qla83xx_set_idc_control(vha, idc_control);
		qla83xx_idc_unlock(vha, 0);
		break;
	case 0x20260:
		if (!IS_QLA8031(ha))
			return -EPERM;
		ql_log(ql_log_info, vha, 0x70bd,
		    "Enabling Reset by IDC control\n");
		qla83xx_idc_lock(vha, 0);
		__qla83xx_get_idc_control(vha, &idc_control);
		idc_control &= ~QLA83XX_IDC_RESET_DISABLED;
		__qla83xx_set_idc_control(vha, idc_control);
		qla83xx_idc_unlock(vha, 0);
		break;
	case 0x20261:
		ql_dbg(ql_dbg_user, vha, 0x70e0,
		    "Updating cache versions without reset ");

		tmp_data = vmalloc(256);
		if (!tmp_data) {
			ql_log(ql_log_warn, vha, 0x70e1,
			    "Unable to allocate memory for VPD information update.\n");
			return -ENOMEM;
		}
		ha->isp_ops->get_flash_version(vha, tmp_data);
		vfree(tmp_data);
		break;
	}
	return count;
}

static struct bin_attribute sysfs_reset_attr = {
	.attr = {
		.name = "reset",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = qla2x00_sysfs_write_reset,
};

static ssize_t
qla2x00_issue_logo(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	int type;
	port_id_t did;

	type = simple_strtol(buf, NULL, 10);

	did.b.domain = (type & 0x00ff0000) >> 16;
	did.b.area = (type & 0x0000ff00) >> 8;
	did.b.al_pa = (type & 0x000000ff);

	ql_log(ql_log_info, vha, 0x70e3, "portid=%02x%02x%02x done\n",
	    did.b.domain, did.b.area, did.b.al_pa);

	ql_log(ql_log_info, vha, 0x70e4, "%s: %d\n", __func__, type);

	qla24xx_els_dcmd_iocb(vha, ELS_DCMD_LOGO, did);
	return count;
}

static struct bin_attribute sysfs_issue_logo_attr = {
	.attr = {
		.name = "issue_logo",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = qla2x00_issue_logo,
};

static ssize_t
qla2x00_sysfs_read_xgmac_stats(struct file *filp, struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int rval;
	uint16_t actual_size;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count > XGMAC_DATA_SIZE)
		return 0;

	if (ha->xgmac_data)
		goto do_read;

	ha->xgmac_data = dma_alloc_coherent(&ha->pdev->dev, XGMAC_DATA_SIZE,
	    &ha->xgmac_data_dma, GFP_KERNEL);
	if (!ha->xgmac_data) {
		ql_log(ql_log_warn, vha, 0x7076,
		    "Unable to allocate memory for XGMAC read-data.\n");
		return 0;
	}

do_read:
	actual_size = 0;
	memset(ha->xgmac_data, 0, XGMAC_DATA_SIZE);

	rval = qla2x00_get_xgmac_stats(vha, ha->xgmac_data_dma,
	    XGMAC_DATA_SIZE, &actual_size);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x7077,
		    "Unable to read XGMAC data (%x).\n", rval);
		count = 0;
	}

	count = actual_size > count ? count: actual_size;
	memcpy(buf, ha->xgmac_data, count);

	return count;
}

static struct bin_attribute sysfs_xgmac_stats_attr = {
	.attr = {
		.name = "xgmac_stats",
		.mode = S_IRUSR,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_xgmac_stats,
};

static ssize_t
qla2x00_sysfs_read_dcbx_tlv(struct file *filp, struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int rval;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count > DCBX_TLV_DATA_SIZE)
		return 0;

	if (ha->dcbx_tlv)
		goto do_read;

	ha->dcbx_tlv = dma_alloc_coherent(&ha->pdev->dev, DCBX_TLV_DATA_SIZE,
	    &ha->dcbx_tlv_dma, GFP_KERNEL);
	if (!ha->dcbx_tlv) {
		ql_log(ql_log_warn, vha, 0x7078,
		    "Unable to allocate memory for DCBX TLV read-data.\n");
		return -ENOMEM;
	}

do_read:
	memset(ha->dcbx_tlv, 0, DCBX_TLV_DATA_SIZE);

	rval = qla2x00_get_dcbx_params(vha, ha->dcbx_tlv_dma,
	    DCBX_TLV_DATA_SIZE);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x7079,
		    "Unable to read DCBX TLV (%x).\n", rval);
		return -EIO;
	}

	memcpy(buf, ha->dcbx_tlv, count);

	return count;
}

static struct bin_attribute sysfs_dcbx_tlv_attr = {
	.attr = {
		.name = "dcbx_tlv",
		.mode = S_IRUSR,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_dcbx_tlv,
};

static struct sysfs_entry {
	char *name;
	struct bin_attribute *attr;
	int is4GBp_only;
} bin_file_entries[] = {
	{ "fw_dump", &sysfs_fw_dump_attr, },
	{ "nvram", &sysfs_nvram_attr, },
	{ "optrom", &sysfs_optrom_attr, },
	{ "optrom_ctl", &sysfs_optrom_ctl_attr, },
	{ "vpd", &sysfs_vpd_attr, 1 },
	{ "sfp", &sysfs_sfp_attr, 1 },
	{ "reset", &sysfs_reset_attr, },
	{ "issue_logo", &sysfs_issue_logo_attr, },
	{ "xgmac_stats", &sysfs_xgmac_stats_attr, 3 },
	{ "dcbx_tlv", &sysfs_dcbx_tlv_attr, 3 },
	{ NULL },
};

void
qla2x00_alloc_sysfs_attr(scsi_qla_host_t *vha)
{
	struct Scsi_Host *host = vha->host;
	struct sysfs_entry *iter;
	int ret;

	for (iter = bin_file_entries; iter->name; iter++) {
		if (iter->is4GBp_only && !IS_FWI2_CAPABLE(vha->hw))
			continue;
		if (iter->is4GBp_only == 2 && !IS_QLA25XX(vha->hw))
			continue;
		if (iter->is4GBp_only == 3 && !(IS_CNA_CAPABLE(vha->hw)))
			continue;

		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
		if (ret)
			ql_log(ql_log_warn, vha, 0x00f3,
			    "Unable to create sysfs %s binary attribute (%d).\n",
			    iter->name, ret);
		else
			ql_dbg(ql_dbg_init, vha, 0x00f4,
			    "Successfully created sysfs %s binary attribure.\n",
			    iter->name);
	}
}

void
qla2x00_free_sysfs_attr(scsi_qla_host_t *vha, bool stop_beacon)
{
	struct Scsi_Host *host = vha->host;
	struct sysfs_entry *iter;
	struct qla_hw_data *ha = vha->hw;

	for (iter = bin_file_entries; iter->name; iter++) {
		if (iter->is4GBp_only && !IS_FWI2_CAPABLE(ha))
			continue;
		if (iter->is4GBp_only == 2 && !IS_QLA25XX(ha))
			continue;
		if (iter->is4GBp_only == 3 && !(IS_CNA_CAPABLE(vha->hw)))
			continue;
		if (iter->is4GBp_only == 0x27 && !IS_QLA27XX(vha->hw))
			continue;

		sysfs_remove_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
	}

	if (stop_beacon && ha->beacon_blink_led == 1)
		ha->isp_ops->beacon_off(vha);
}

/* Scsi_Host attributes. */

static ssize_t
qla2x00_drvr_version_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", qla2x00_version_str);
}

static ssize_t
qla2x00_fw_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	char fw_str[128];

	return scnprintf(buf, PAGE_SIZE, "%s\n",
	    ha->isp_ops->fw_version_str(vha, fw_str, sizeof(fw_str)));
}

static ssize_t
qla2x00_serial_num_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	uint32_t sn;

	if (IS_QLAFX00(vha->hw)) {
		return scnprintf(buf, PAGE_SIZE, "%s\n",
		    vha->hw->mr.serial_num);
	} else if (IS_FWI2_CAPABLE(ha)) {
		qla2xxx_get_vpd_field(vha, "SN", buf, PAGE_SIZE - 1);
		return strlen(strcat(buf, "\n"));
	}

	sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | ha->serial1;
	return scnprintf(buf, PAGE_SIZE, "%c%05d\n", 'A' + sn / 100000,
	    sn % 100000);
}

static ssize_t
qla2x00_isp_name_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return scnprintf(buf, PAGE_SIZE, "ISP%04X\n", vha->hw->pdev->device);
}

static ssize_t
qla2x00_isp_id_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLAFX00(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "%s\n",
		    vha->hw->mr.hw_version);

	return scnprintf(buf, PAGE_SIZE, "%04x %04x %04x %04x\n",
	    ha->product_id[0], ha->product_id[1], ha->product_id[2],
	    ha->product_id[3]);
}

static ssize_t
qla2x00_model_name_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return scnprintf(buf, PAGE_SIZE, "%s\n", vha->hw->model_number);
}

static ssize_t
qla2x00_model_desc_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return scnprintf(buf, PAGE_SIZE, "%s\n", vha->hw->model_desc);
}

static ssize_t
qla2x00_pci_info_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	char pci_info[30];

	return scnprintf(buf, PAGE_SIZE, "%s\n",
	    vha->hw->isp_ops->pci_info_str(vha, pci_info));
}

static ssize_t
qla2x00_link_state_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	int len = 0;

	if (atomic_read(&vha->loop_state) == LOOP_DOWN ||
	    atomic_read(&vha->loop_state) == LOOP_DEAD ||
	    vha->device_flags & DFLG_NO_CABLE)
		len = scnprintf(buf, PAGE_SIZE, "Link Down\n");
	else if (atomic_read(&vha->loop_state) != LOOP_READY ||
	    qla2x00_reset_active(vha))
		len = scnprintf(buf, PAGE_SIZE, "Unknown Link State\n");
	else {
		len = scnprintf(buf, PAGE_SIZE, "Link Up - ");

		switch (ha->current_topology) {
		case ISP_CFG_NL:
			len += scnprintf(buf + len, PAGE_SIZE-len, "Loop\n");
			break;
		case ISP_CFG_FL:
			len += scnprintf(buf + len, PAGE_SIZE-len, "FL_Port\n");
			break;
		case ISP_CFG_N:
			len += scnprintf(buf + len, PAGE_SIZE-len,
			    "N_Port to N_Port\n");
			break;
		case ISP_CFG_F:
			len += scnprintf(buf + len, PAGE_SIZE-len, "F_Port\n");
			break;
		default:
			len += scnprintf(buf + len, PAGE_SIZE-len, "Loop\n");
			break;
		}
	}
	return len;
}

static ssize_t
qla2x00_zio_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int len = 0;

	switch (vha->hw->zio_mode) {
	case QLA_ZIO_MODE_6:
		len += scnprintf(buf + len, PAGE_SIZE-len, "Mode 6\n");
		break;
	case QLA_ZIO_DISABLED:
		len += scnprintf(buf + len, PAGE_SIZE-len, "Disabled\n");
		break;
	}
	return len;
}

static ssize_t
qla2x00_zio_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	int val = 0;
	uint16_t zio_mode;

	if (!IS_ZIO_SUPPORTED(ha))
		return -ENOTSUPP;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val)
		zio_mode = QLA_ZIO_MODE_6;
	else
		zio_mode = QLA_ZIO_DISABLED;

	/* Update per-hba values and queue a reset. */
	if (zio_mode != QLA_ZIO_DISABLED || ha->zio_mode != QLA_ZIO_DISABLED) {
		ha->zio_mode = zio_mode;
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	}
	return strlen(buf);
}

static ssize_t
qla2x00_zio_timer_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return scnprintf(buf, PAGE_SIZE, "%d us\n", vha->hw->zio_timer * 100);
}

static ssize_t
qla2x00_zio_timer_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int val = 0;
	uint16_t zio_timer;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;
	if (val > 25500 || val < 100)
		return -ERANGE;

	zio_timer = (uint16_t)(val / 100);
	vha->hw->zio_timer = zio_timer;

	return strlen(buf);
}

static ssize_t
qla2x00_beacon_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int len = 0;

	if (vha->hw->beacon_blink_led)
		len += scnprintf(buf + len, PAGE_SIZE-len, "Enabled\n");
	else
		len += scnprintf(buf + len, PAGE_SIZE-len, "Disabled\n");
	return len;
}

static ssize_t
qla2x00_beacon_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	int val = 0;
	int rval;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return -EPERM;

	if (test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)) {
		ql_log(ql_log_warn, vha, 0x707a,
		    "Abort ISP active -- ignoring beacon request.\n");
		return -EBUSY;
	}

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val)
		rval = ha->isp_ops->beacon_on(vha);
	else
		rval = ha->isp_ops->beacon_off(vha);

	if (rval != QLA_SUCCESS)
		count = 0;

	return count;
}

static ssize_t
qla2x00_optrom_bios_version_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return scnprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->bios_revision[1],
	    ha->bios_revision[0]);
}

static ssize_t
qla2x00_optrom_efi_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return scnprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->efi_revision[1],
	    ha->efi_revision[0]);
}

static ssize_t
qla2x00_optrom_fcode_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return scnprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->fcode_revision[1],
	    ha->fcode_revision[0]);
}

static ssize_t
qla2x00_optrom_fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d %d\n",
	    ha->fw_revision[0], ha->fw_revision[1], ha->fw_revision[2],
	    ha->fw_revision[3]);
}

static ssize_t
qla2x00_optrom_gold_fw_version_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA83XX(ha) && !IS_QLA27XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d (%d)\n",
	    ha->gold_fw_version[0], ha->gold_fw_version[1],
	    ha->gold_fw_version[2], ha->gold_fw_version[3]);
}

static ssize_t
qla2x00_total_isp_aborts_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return scnprintf(buf, PAGE_SIZE, "%d\n",
	    vha->qla_stats.total_isp_aborts);
}

static ssize_t
qla24xx_84xx_fw_version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rval = QLA_SUCCESS;
	uint16_t status[2] = {0, 0};
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA84XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	if (ha->cs84xx->op_fw_version == 0)
		rval = qla84xx_verify_chip(vha, status);

	if ((rval == QLA_SUCCESS) && (status[0] == 0))
		return scnprintf(buf, PAGE_SIZE, "%u\n",
			(uint32_t)ha->cs84xx->op_fw_version);

	return scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t
qla2x00_mpi_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha) && !IS_QLA8044(ha) &&
	    !IS_QLA27XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d (%x)\n",
	    ha->mpi_version[0], ha->mpi_version[1], ha->mpi_version[2],
	    ha->mpi_capabilities);
}

static ssize_t
qla2x00_phy_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d\n",
	    ha->phy_version[0], ha->phy_version[1], ha->phy_version[2]);
}

static ssize_t
qla2x00_flash_block_size_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", ha->fdt_block_size);
}

static ssize_t
qla2x00_vlan_id_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_CNA_CAPABLE(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d\n", vha->fcoe_vlan_id);
}

static ssize_t
qla2x00_vn_port_mac_address_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_CNA_CAPABLE(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%pMR\n", vha->fcoe_vn_port_mac);
}

static ssize_t
qla2x00_fabric_param_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return scnprintf(buf, PAGE_SIZE, "%d\n", vha->hw->switch_cap);
}

static ssize_t
qla2x00_thermal_temp_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	uint16_t temp = 0;

	if (qla2x00_reset_active(vha)) {
		ql_log(ql_log_warn, vha, 0x70dc, "ISP reset active.\n");
		goto done;
	}

	if (vha->hw->flags.eeh_busy) {
		ql_log(ql_log_warn, vha, 0x70dd, "PCI EEH busy.\n");
		goto done;
	}

	if (qla2x00_get_thermal_temp(vha, &temp) == QLA_SUCCESS)
		return scnprintf(buf, PAGE_SIZE, "%d\n", temp);

done:
	return scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t
qla2x00_fw_state_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int rval = QLA_FUNCTION_FAILED;
	uint16_t state[6];
	uint32_t pstate;

	if (IS_QLAFX00(vha->hw)) {
		pstate = qlafx00_fw_state_show(dev, attr, buf);
		return scnprintf(buf, PAGE_SIZE, "0x%x\n", pstate);
	}

	if (qla2x00_reset_active(vha))
		ql_log(ql_log_warn, vha, 0x707c,
		    "ISP reset active.\n");
	else if (!vha->hw->flags.eeh_busy)
		rval = qla2x00_get_firmware_state(vha, state);
	if (rval != QLA_SUCCESS)
		memset(state, -1, sizeof(state));

	return scnprintf(buf, PAGE_SIZE, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
	    state[0], state[1], state[2], state[3], state[4], state[5]);
}

static ssize_t
qla2x00_diag_requests_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_BIDI_CAPABLE(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%llu\n", vha->bidi_stats.io_count);
}

static ssize_t
qla2x00_diag_megabytes_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_BIDI_CAPABLE(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
	    vha->bidi_stats.transfer_bytes >> 20);
}

static ssize_t
qla2x00_fw_dump_size_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	uint32_t size;

	if (!ha->fw_dumped)
		size = 0;
	else if (IS_P3P_TYPE(ha))
		size = ha->md_template_size + ha->md_dump_size;
	else
		size = ha->fw_dump_len;

	return scnprintf(buf, PAGE_SIZE, "%d\n", size);
}

static ssize_t
qla2x00_allow_cna_fw_dump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_P3P_TYPE(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");
	else
		return scnprintf(buf, PAGE_SIZE, "%s\n",
		    vha->hw->allow_cna_fw_dump ? "true" : "false");
}

static ssize_t
qla2x00_allow_cna_fw_dump_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int val = 0;

	if (!IS_P3P_TYPE(vha->hw))
		return -EINVAL;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	vha->hw->allow_cna_fw_dump = val != 0;

	return strlen(buf);
}

static ssize_t
qla2x00_pep_version_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d\n",
	    ha->pep_version[0], ha->pep_version[1], ha->pep_version[2]);
}

static DEVICE_ATTR(driver_version, S_IRUGO, qla2x00_drvr_version_show, NULL);
static DEVICE_ATTR(fw_version, S_IRUGO, qla2x00_fw_version_show, NULL);
static DEVICE_ATTR(serial_num, S_IRUGO, qla2x00_serial_num_show, NULL);
static DEVICE_ATTR(isp_name, S_IRUGO, qla2x00_isp_name_show, NULL);
static DEVICE_ATTR(isp_id, S_IRUGO, qla2x00_isp_id_show, NULL);
static DEVICE_ATTR(model_name, S_IRUGO, qla2x00_model_name_show, NULL);
static DEVICE_ATTR(model_desc, S_IRUGO, qla2x00_model_desc_show, NULL);
static DEVICE_ATTR(pci_info, S_IRUGO, qla2x00_pci_info_show, NULL);
static DEVICE_ATTR(link_state, S_IRUGO, qla2x00_link_state_show, NULL);
static DEVICE_ATTR(zio, S_IRUGO | S_IWUSR, qla2x00_zio_show, qla2x00_zio_store);
static DEVICE_ATTR(zio_timer, S_IRUGO | S_IWUSR, qla2x00_zio_timer_show,
		   qla2x00_zio_timer_store);
static DEVICE_ATTR(beacon, S_IRUGO | S_IWUSR, qla2x00_beacon_show,
		   qla2x00_beacon_store);
static DEVICE_ATTR(optrom_bios_version, S_IRUGO,
		   qla2x00_optrom_bios_version_show, NULL);
static DEVICE_ATTR(optrom_efi_version, S_IRUGO,
		   qla2x00_optrom_efi_version_show, NULL);
static DEVICE_ATTR(optrom_fcode_version, S_IRUGO,
		   qla2x00_optrom_fcode_version_show, NULL);
static DEVICE_ATTR(optrom_fw_version, S_IRUGO, qla2x00_optrom_fw_version_show,
		   NULL);
static DEVICE_ATTR(optrom_gold_fw_version, S_IRUGO,
    qla2x00_optrom_gold_fw_version_show, NULL);
static DEVICE_ATTR(84xx_fw_version, S_IRUGO, qla24xx_84xx_fw_version_show,
		   NULL);
static DEVICE_ATTR(total_isp_aborts, S_IRUGO, qla2x00_total_isp_aborts_show,
		   NULL);
static DEVICE_ATTR(mpi_version, S_IRUGO, qla2x00_mpi_version_show, NULL);
static DEVICE_ATTR(phy_version, S_IRUGO, qla2x00_phy_version_show, NULL);
static DEVICE_ATTR(flash_block_size, S_IRUGO, qla2x00_flash_block_size_show,
		   NULL);
static DEVICE_ATTR(vlan_id, S_IRUGO, qla2x00_vlan_id_show, NULL);
static DEVICE_ATTR(vn_port_mac_address, S_IRUGO,
		   qla2x00_vn_port_mac_address_show, NULL);
static DEVICE_ATTR(fabric_param, S_IRUGO, qla2x00_fabric_param_show, NULL);
static DEVICE_ATTR(fw_state, S_IRUGO, qla2x00_fw_state_show, NULL);
static DEVICE_ATTR(thermal_temp, S_IRUGO, qla2x00_thermal_temp_show, NULL);
static DEVICE_ATTR(diag_requests, S_IRUGO, qla2x00_diag_requests_show, NULL);
static DEVICE_ATTR(diag_megabytes, S_IRUGO, qla2x00_diag_megabytes_show, NULL);
static DEVICE_ATTR(fw_dump_size, S_IRUGO, qla2x00_fw_dump_size_show, NULL);
static DEVICE_ATTR(allow_cna_fw_dump, S_IRUGO | S_IWUSR,
		   qla2x00_allow_cna_fw_dump_show,
		   qla2x00_allow_cna_fw_dump_store);
static DEVICE_ATTR(pep_version, S_IRUGO, qla2x00_pep_version_show, NULL);

struct device_attribute *qla2x00_host_attrs[] = {
	&dev_attr_driver_version,
	&dev_attr_fw_version,
	&dev_attr_serial_num,
	&dev_attr_isp_name,
	&dev_attr_isp_id,
	&dev_attr_model_name,
	&dev_attr_model_desc,
	&dev_attr_pci_info,
	&dev_attr_link_state,
	&dev_attr_zio,
	&dev_attr_zio_timer,
	&dev_attr_beacon,
	&dev_attr_optrom_bios_version,
	&dev_attr_optrom_efi_version,
	&dev_attr_optrom_fcode_version,
	&dev_attr_optrom_fw_version,
	&dev_attr_84xx_fw_version,
	&dev_attr_total_isp_aborts,
	&dev_attr_mpi_version,
	&dev_attr_phy_version,
	&dev_attr_flash_block_size,
	&dev_attr_vlan_id,
	&dev_attr_vn_port_mac_address,
	&dev_attr_fabric_param,
	&dev_attr_fw_state,
	&dev_attr_optrom_gold_fw_version,
	&dev_attr_thermal_temp,
	&dev_attr_diag_requests,
	&dev_attr_diag_megabytes,
	&dev_attr_fw_dump_size,
	&dev_attr_allow_cna_fw_dump,
	&dev_attr_pep_version,
	NULL,
};

/* Host attributes. */

static void
qla2x00_get_host_port_id(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	fc_host_port_id(shost) = vha->d_id.b.domain << 16 |
	    vha->d_id.b.area << 8 | vha->d_id.b.al_pa;
}

static void
qla2x00_get_host_speed(struct Scsi_Host *shost)
{
	struct qla_hw_data *ha = ((struct scsi_qla_host *)
					(shost_priv(shost)))->hw;
	u32 speed = FC_PORTSPEED_UNKNOWN;

	if (IS_QLAFX00(ha)) {
		qlafx00_get_host_speed(shost);
		return;
	}

	switch (ha->link_data_rate) {
	case PORT_SPEED_1GB:
		speed = FC_PORTSPEED_1GBIT;
		break;
	case PORT_SPEED_2GB:
		speed = FC_PORTSPEED_2GBIT;
		break;
	case PORT_SPEED_4GB:
		speed = FC_PORTSPEED_4GBIT;
		break;
	case PORT_SPEED_8GB:
		speed = FC_PORTSPEED_8GBIT;
		break;
	case PORT_SPEED_10GB:
		speed = FC_PORTSPEED_10GBIT;
		break;
	case PORT_SPEED_16GB:
		speed = FC_PORTSPEED_16GBIT;
		break;
	case PORT_SPEED_32GB:
		speed = FC_PORTSPEED_32GBIT;
		break;
	}
	fc_host_speed(shost) = speed;
}

static void
qla2x00_get_host_port_type(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	uint32_t port_type = FC_PORTTYPE_UNKNOWN;

	if (vha->vp_idx) {
		fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
		return;
	}
	switch (vha->hw->current_topology) {
	case ISP_CFG_NL:
		port_type = FC_PORTTYPE_LPORT;
		break;
	case ISP_CFG_FL:
		port_type = FC_PORTTYPE_NLPORT;
		break;
	case ISP_CFG_N:
		port_type = FC_PORTTYPE_PTP;
		break;
	case ISP_CFG_F:
		port_type = FC_PORTTYPE_NPORT;
		break;
	}
	fc_host_port_type(shost) = port_type;
}

static void
qla2x00_get_starget_node_name(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t *fcport;
	u64 node_name = 0;

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->rport &&
		    starget->id == fcport->rport->scsi_target_id) {
			node_name = wwn_to_u64(fcport->node_name);
			break;
		}
	}

	fc_starget_node_name(starget) = node_name;
}

static void
qla2x00_get_starget_port_name(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t *fcport;
	u64 port_name = 0;

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->rport &&
		    starget->id == fcport->rport->scsi_target_id) {
			port_name = wwn_to_u64(fcport->port_name);
			break;
		}
	}

	fc_starget_port_name(starget) = port_name;
}

static void
qla2x00_get_starget_port_id(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t *fcport;
	uint32_t port_id = ~0U;

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->rport &&
		    starget->id == fcport->rport->scsi_target_id) {
			port_id = fcport->d_id.b.domain << 16 |
			    fcport->d_id.b.area << 8 | fcport->d_id.b.al_pa;
			break;
		}
	}

	fc_starget_port_id(starget) = port_id;
}

static void
qla2x00_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;
}

static void
qla2x00_dev_loss_tmo_callbk(struct fc_rport *rport)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;
	unsigned long flags;

	if (!fcport)
		return;

	/* Now that the rport has been deleted, set the fcport state to
	   FCS_DEVICE_DEAD */
	qla2x00_set_fcport_state(fcport, FCS_DEVICE_DEAD);

	/*
	 * Transport has effectively 'deleted' the rport, clear
	 * all local references.
	 */
	spin_lock_irqsave(host->host_lock, flags);
	fcport->rport = fcport->drport = NULL;
	*((fc_port_t **)rport->dd_data) = NULL;
	spin_unlock_irqrestore(host->host_lock, flags);

	if (test_bit(ABORT_ISP_ACTIVE, &fcport->vha->dpc_flags))
		return;

	if (unlikely(pci_channel_offline(fcport->vha->hw->pdev))) {
		qla2x00_abort_all_cmds(fcport->vha, DID_NO_CONNECT << 16);
		return;
	}
}

static void
qla2x00_terminate_rport_io(struct fc_rport *rport)
{
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;

	if (!fcport)
		return;

	if (test_bit(UNLOADING, &fcport->vha->dpc_flags))
		return;

	if (test_bit(ABORT_ISP_ACTIVE, &fcport->vha->dpc_flags))
		return;

	if (unlikely(pci_channel_offline(fcport->vha->hw->pdev))) {
		qla2x00_abort_all_cmds(fcport->vha, DID_NO_CONNECT << 16);
		return;
	}
	/*
	 * At this point all fcport's software-states are cleared.  Perform any
	 * final cleanup of firmware resources (PCBs and XCBs).
	 */
	if (fcport->loop_id != FC_NO_LOOP_ID) {
		if (IS_FWI2_CAPABLE(fcport->vha->hw))
			fcport->vha->hw->isp_ops->fabric_logout(fcport->vha,
			    fcport->loop_id, fcport->d_id.b.domain,
			    fcport->d_id.b.area, fcport->d_id.b.al_pa);
		else
			qla2x00_port_logout(fcport->vha, fcport);
	}
}

static int
qla2x00_issue_lip(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	if (IS_QLAFX00(vha->hw))
		return 0;

	qla2x00_loop_reset(vha);
	return 0;
}

static struct fc_host_statistics *
qla2x00_get_fc_host_stats(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	int rval;
	struct link_statistics *stats;
	dma_addr_t stats_dma;
	struct fc_host_statistics *p = &vha->fc_host_stat;

	memset(p, -1, sizeof(*p));

	if (IS_QLAFX00(vha->hw))
		goto done;

	if (test_bit(UNLOADING, &vha->dpc_flags))
		goto done;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto done;

	if (qla2x00_reset_active(vha))
		goto done;

	stats = dma_alloc_coherent(&ha->pdev->dev,
	    sizeof(*stats), &stats_dma, GFP_KERNEL);
	if (!stats) {
		ql_log(ql_log_warn, vha, 0x707d,
		    "Failed to allocate memory for stats.\n");
		goto done;
	}
	memset(stats, 0, sizeof(*stats));

	rval = QLA_FUNCTION_FAILED;
	if (IS_FWI2_CAPABLE(ha)) {
		rval = qla24xx_get_isp_stats(base_vha, stats, stats_dma, 0);
	} else if (atomic_read(&base_vha->loop_state) == LOOP_READY &&
	    !ha->dpc_active) {
		/* Must be in a 'READY' state for statistics retrieval. */
		rval = qla2x00_get_link_status(base_vha, base_vha->loop_id,
						stats, stats_dma);
	}

	if (rval != QLA_SUCCESS)
		goto done_free;

	p->link_failure_count = stats->link_fail_cnt;
	p->loss_of_sync_count = stats->loss_sync_cnt;
	p->loss_of_signal_count = stats->loss_sig_cnt;
	p->prim_seq_protocol_err_count = stats->prim_seq_err_cnt;
	p->invalid_tx_word_count = stats->inval_xmit_word_cnt;
	p->invalid_crc_count = stats->inval_crc_cnt;
	if (IS_FWI2_CAPABLE(ha)) {
		p->lip_count = stats->lip_cnt;
		p->tx_frames = stats->tx_frames;
		p->rx_frames = stats->rx_frames;
		p->dumped_frames = stats->discarded_frames;
		p->nos_count = stats->nos_rcvd;
		p->error_frames =
			stats->dropped_frames + stats->discarded_frames;
		p->rx_words = vha->qla_stats.input_bytes;
		p->tx_words = vha->qla_stats.output_bytes;
	}
	p->fcp_control_requests = vha->qla_stats.control_requests;
	p->fcp_input_requests = vha->qla_stats.input_requests;
	p->fcp_output_requests = vha->qla_stats.output_requests;
	p->fcp_input_megabytes = vha->qla_stats.input_bytes >> 20;
	p->fcp_output_megabytes = vha->qla_stats.output_bytes >> 20;
	p->seconds_since_last_reset =
		get_jiffies_64() - vha->qla_stats.jiffies_at_last_reset;
	do_div(p->seconds_since_last_reset, HZ);

done_free:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct link_statistics),
	    stats, stats_dma);
done:
	return p;
}

static void
qla2x00_reset_host_stats(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	struct link_statistics *stats;
	dma_addr_t stats_dma;

	memset(&vha->qla_stats, 0, sizeof(vha->qla_stats));
	memset(&vha->fc_host_stat, 0, sizeof(vha->fc_host_stat));

	vha->qla_stats.jiffies_at_last_reset = get_jiffies_64();

	if (IS_FWI2_CAPABLE(ha)) {
		stats = dma_alloc_coherent(&ha->pdev->dev,
		    sizeof(*stats), &stats_dma, GFP_KERNEL);
		if (!stats) {
			ql_log(ql_log_warn, vha, 0x70d7,
			    "Failed to allocate memory for stats.\n");
			return;
		}

		/* reset firmware statistics */
		qla24xx_get_isp_stats(base_vha, stats, stats_dma, BIT_0);

		dma_free_coherent(&ha->pdev->dev, sizeof(*stats),
		    stats, stats_dma);
	}
}

static void
qla2x00_get_host_symbolic_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	qla2x00_get_sym_node_name(vha, fc_host_symbolic_name(shost),
	    sizeof(fc_host_symbolic_name(shost)));
}

static void
qla2x00_set_host_system_hostname(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);
}

static void
qla2x00_get_host_fabric_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	uint8_t node_name[WWN_SIZE] = { 0xFF, 0xFF, 0xFF, 0xFF, \
		0xFF, 0xFF, 0xFF, 0xFF};
	u64 fabric_name = wwn_to_u64(node_name);

	if (vha->device_flags & SWITCH_FOUND)
		fabric_name = wwn_to_u64(vha->fabric_node_name);

	fc_host_fabric_name(shost) = fabric_name;
}

static void
qla2x00_get_host_port_state(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	struct scsi_qla_host *base_vha = pci_get_drvdata(vha->hw->pdev);

	if (!base_vha->flags.online) {
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
		return;
	}

	switch (atomic_read(&base_vha->loop_state)) {
	case LOOP_UPDATE:
		fc_host_port_state(shost) = FC_PORTSTATE_DIAGNOSTICS;
		break;
	case LOOP_DOWN:
		if (test_bit(LOOP_RESYNC_NEEDED, &base_vha->dpc_flags))
			fc_host_port_state(shost) = FC_PORTSTATE_DIAGNOSTICS;
		else
			fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
		break;
	case LOOP_DEAD:
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
		break;
	case LOOP_READY:
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
		break;
	default:
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
		break;
	}
}

static int
qla24xx_vport_create(struct fc_vport *fc_vport, bool disable)
{
	int	ret = 0;
	uint8_t	qos = 0;
	scsi_qla_host_t *base_vha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha = NULL;
	struct qla_hw_data *ha = base_vha->hw;
	int	cnt;
	struct req_que *req = ha->req_q_map[0];
	struct qla_qpair *qpair;

	ret = qla24xx_vport_create_req_sanity_check(fc_vport);
	if (ret) {
		ql_log(ql_log_warn, vha, 0x707e,
		    "Vport sanity check failed, status %x\n", ret);
		return (ret);
	}

	vha = qla24xx_create_vhost(fc_vport);
	if (vha == NULL) {
		ql_log(ql_log_warn, vha, 0x707f, "Vport create host failed.\n");
		return FC_VPORT_FAILED;
	}
	if (disable) {
		atomic_set(&vha->vp_state, VP_OFFLINE);
		fc_vport_set_state(fc_vport, FC_VPORT_DISABLED);
	} else
		atomic_set(&vha->vp_state, VP_FAILED);

	/* ready to create vport */
	ql_log(ql_log_info, vha, 0x7080,
	    "VP entry id %d assigned.\n", vha->vp_idx);

	/* initialized vport states */
	atomic_set(&vha->loop_state, LOOP_DOWN);
	vha->vp_err_state=  VP_ERR_PORTDWN;
	vha->vp_prev_err_state=  VP_ERR_UNKWN;
	/* Check if physical ha port is Up */
	if (atomic_read(&base_vha->loop_state) == LOOP_DOWN ||
	    atomic_read(&base_vha->loop_state) == LOOP_DEAD) {
		/* Don't retry or attempt login of this virtual port */
		ql_dbg(ql_dbg_user, vha, 0x7081,
		    "Vport loop state is not UP.\n");
		atomic_set(&vha->loop_state, LOOP_DEAD);
		if (!disable)
			fc_vport_set_state(fc_vport, FC_VPORT_LINKDOWN);
	}

	if (IS_T10_PI_CAPABLE(ha) && ql2xenabledif) {
		if (ha->fw_attributes & BIT_4) {
			int prot = 0, guard;
			vha->flags.difdix_supported = 1;
			ql_dbg(ql_dbg_user, vha, 0x7082,
			    "Registered for DIF/DIX type 1 and 3 protection.\n");
			if (ql2xenabledif == 1)
				prot = SHOST_DIX_TYPE0_PROTECTION;
			scsi_host_set_prot(vha->host,
			    prot | SHOST_DIF_TYPE1_PROTECTION
			    | SHOST_DIF_TYPE2_PROTECTION
			    | SHOST_DIF_TYPE3_PROTECTION
			    | SHOST_DIX_TYPE1_PROTECTION
			    | SHOST_DIX_TYPE2_PROTECTION
			    | SHOST_DIX_TYPE3_PROTECTION);

			guard = SHOST_DIX_GUARD_CRC;

			if (IS_PI_IPGUARD_CAPABLE(ha) &&
			    (ql2xenabledif > 1 || IS_PI_DIFB_DIX0_CAPABLE(ha)))
				guard |= SHOST_DIX_GUARD_IP;

			scsi_host_set_guard(vha->host, guard);
		} else
			vha->flags.difdix_supported = 0;
	}

	if (scsi_add_host_with_dma(vha->host, &fc_vport->dev,
				   &ha->pdev->dev)) {
		ql_dbg(ql_dbg_user, vha, 0x7083,
		    "scsi_add_host failure for VP[%d].\n", vha->vp_idx);
		goto vport_create_failed_2;
	}

	/* initialize attributes */
	fc_host_dev_loss_tmo(vha->host) = ha->port_down_retry_count;
	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) =
		fc_host_supported_classes(base_vha->host);
	fc_host_supported_speeds(vha->host) =
		fc_host_supported_speeds(base_vha->host);

	qlt_vport_create(vha, ha);
	qla24xx_vport_disable(fc_vport, disable);

	if (!ql2xmqsupport || !ha->npiv_info)
		goto vport_queue;

	/* Create a request queue in QoS mode for the vport */
	for (cnt = 0; cnt < ha->nvram_npiv_size; cnt++) {
		if (memcmp(ha->npiv_info[cnt].port_name, vha->port_name, 8) == 0
			&& memcmp(ha->npiv_info[cnt].node_name, vha->node_name,
					8) == 0) {
			qos = ha->npiv_info[cnt].q_qos;
			break;
		}
	}

	if (qos) {
		qpair = qla2xxx_create_qpair(vha, qos, vha->vp_idx);
		if (!qpair)
			ql_log(ql_log_warn, vha, 0x7084,
			    "Can't create qpair for VP[%d]\n",
			    vha->vp_idx);
		else {
			ql_dbg(ql_dbg_multiq, vha, 0xc001,
			    "Queue pair: %d Qos: %d) created for VP[%d]\n",
			    qpair->id, qos, vha->vp_idx);
			ql_dbg(ql_dbg_user, vha, 0x7085,
			    "Queue Pair: %d Qos: %d) created for VP[%d]\n",
			    qpair->id, qos, vha->vp_idx);
			req = qpair->req;
			vha->qpair = qpair;
		}
	}

vport_queue:
	vha->req = req;
	return 0;

vport_create_failed_2:
	qla24xx_disable_vp(vha);
	qla24xx_deallocate_vp_id(vha);
	scsi_host_put(vha->host);
	return FC_VPORT_FAILED;
}

static int
qla24xx_vport_delete(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *vha = fc_vport->dd_data;
	struct qla_hw_data *ha = vha->hw;
	uint16_t id = vha->vp_idx;

	while (test_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags) ||
	    test_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags))
		msleep(1000);

	qla24xx_disable_vp(vha);

	vha->flags.delete_progress = 1;

	qlt_remove_target(ha, vha);

	fc_remove_host(vha->host);

	scsi_remove_host(vha->host);

	/* Allow timer to run to drain queued items, when removing vp */
	qla24xx_deallocate_vp_id(vha);

	if (vha->timer_active) {
		qla2x00_vp_stop_timer(vha);
		ql_dbg(ql_dbg_user, vha, 0x7086,
		    "Timer for the VP[%d] has stopped\n", vha->vp_idx);
	}

	qla2x00_free_fcports(vha);

	mutex_lock(&ha->vport_lock);
	ha->cur_vport_count--;
	clear_bit(vha->vp_idx, ha->vp_idx_map);
	mutex_unlock(&ha->vport_lock);

	dma_free_coherent(&ha->pdev->dev, vha->gnl.size, vha->gnl.l,
	    vha->gnl.ldma);

	if (vha->qpair && vha->qpair->vp_idx == vha->vp_idx) {
		if (qla2xxx_delete_qpair(vha, vha->qpair) != QLA_SUCCESS)
			ql_log(ql_log_warn, vha, 0x7087,
			    "Queue Pair delete failed.\n");
	}

	ql_log(ql_log_info, vha, 0x7088, "VP[%d] deleted.\n", id);
	scsi_host_put(vha->host);
	return 0;
}

static int
qla24xx_vport_disable(struct fc_vport *fc_vport, bool disable)
{
	scsi_qla_host_t *vha = fc_vport->dd_data;

	if (disable)
		qla24xx_disable_vp(vha);
	else
		qla24xx_enable_vp(vha);

	return 0;
}

struct fc_function_template qla2xxx_transport_functions = {

	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_speeds = 1,

	.get_host_port_id = qla2x00_get_host_port_id,
	.show_host_port_id = 1,
	.get_host_speed = qla2x00_get_host_speed,
	.show_host_speed = 1,
	.get_host_port_type = qla2x00_get_host_port_type,
	.show_host_port_type = 1,
	.get_host_symbolic_name = qla2x00_get_host_symbolic_name,
	.show_host_symbolic_name = 1,
	.set_host_system_hostname = qla2x00_set_host_system_hostname,
	.show_host_system_hostname = 1,
	.get_host_fabric_name = qla2x00_get_host_fabric_name,
	.show_host_fabric_name = 1,
	.get_host_port_state = qla2x00_get_host_port_state,
	.show_host_port_state = 1,

	.dd_fcrport_size = sizeof(struct fc_port *),
	.show_rport_supported_classes = 1,

	.get_starget_node_name = qla2x00_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = qla2x00_get_starget_port_name,
	.show_starget_port_name = 1,
	.get_starget_port_id  = qla2x00_get_starget_port_id,
	.show_starget_port_id = 1,

	.set_rport_dev_loss_tmo = qla2x00_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.issue_fc_host_lip = qla2x00_issue_lip,
	.dev_loss_tmo_callbk = qla2x00_dev_loss_tmo_callbk,
	.terminate_rport_io = qla2x00_terminate_rport_io,
	.get_fc_host_stats = qla2x00_get_fc_host_stats,
	.reset_fc_host_stats = qla2x00_reset_host_stats,

	.vport_create = qla24xx_vport_create,
	.vport_disable = qla24xx_vport_disable,
	.vport_delete = qla24xx_vport_delete,
	.bsg_request = qla24xx_bsg_request,
	.bsg_timeout = qla24xx_bsg_timeout,
};

struct fc_function_template qla2xxx_transport_vport_functions = {

	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,

	.get_host_port_id = qla2x00_get_host_port_id,
	.show_host_port_id = 1,
	.get_host_speed = qla2x00_get_host_speed,
	.show_host_speed = 1,
	.get_host_port_type = qla2x00_get_host_port_type,
	.show_host_port_type = 1,
	.get_host_symbolic_name = qla2x00_get_host_symbolic_name,
	.show_host_symbolic_name = 1,
	.set_host_system_hostname = qla2x00_set_host_system_hostname,
	.show_host_system_hostname = 1,
	.get_host_fabric_name = qla2x00_get_host_fabric_name,
	.show_host_fabric_name = 1,
	.get_host_port_state = qla2x00_get_host_port_state,
	.show_host_port_state = 1,

	.dd_fcrport_size = sizeof(struct fc_port *),
	.show_rport_supported_classes = 1,

	.get_starget_node_name = qla2x00_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = qla2x00_get_starget_port_name,
	.show_starget_port_name = 1,
	.get_starget_port_id  = qla2x00_get_starget_port_id,
	.show_starget_port_id = 1,

	.set_rport_dev_loss_tmo = qla2x00_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.issue_fc_host_lip = qla2x00_issue_lip,
	.dev_loss_tmo_callbk = qla2x00_dev_loss_tmo_callbk,
	.terminate_rport_io = qla2x00_terminate_rport_io,
	.get_fc_host_stats = qla2x00_get_fc_host_stats,
	.reset_fc_host_stats = qla2x00_reset_host_stats,

	.bsg_request = qla24xx_bsg_request,
	.bsg_timeout = qla24xx_bsg_timeout,
};

void
qla2x00_init_host_attr(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	u32 speed = FC_PORTSPEED_UNKNOWN;

	fc_host_dev_loss_tmo(vha->host) = ha->port_down_retry_count;
	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) = ha->tgt.enable_class_2 ?
			(FC_COS_CLASS2|FC_COS_CLASS3) : FC_COS_CLASS3;
	fc_host_max_npiv_vports(vha->host) = ha->max_npiv_vports;
	fc_host_npiv_vports_inuse(vha->host) = ha->cur_vport_count;

	if (IS_CNA_CAPABLE(ha))
		speed = FC_PORTSPEED_10GBIT;
	else if (IS_QLA2031(ha))
		speed = FC_PORTSPEED_16GBIT | FC_PORTSPEED_8GBIT |
		    FC_PORTSPEED_4GBIT;
	else if (IS_QLA25XX(ha))
		speed = FC_PORTSPEED_8GBIT | FC_PORTSPEED_4GBIT |
		    FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else if (IS_QLA24XX_TYPE(ha))
		speed = FC_PORTSPEED_4GBIT | FC_PORTSPEED_2GBIT |
		    FC_PORTSPEED_1GBIT;
	else if (IS_QLA23XX(ha))
		speed = FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else if (IS_QLAFX00(ha))
		speed = FC_PORTSPEED_8GBIT | FC_PORTSPEED_4GBIT |
		    FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else if (IS_QLA27XX(ha))
		speed = FC_PORTSPEED_32GBIT | FC_PORTSPEED_16GBIT |
		    FC_PORTSPEED_8GBIT;
	else
		speed = FC_PORTSPEED_1GBIT;
	fc_host_supported_speeds(vha->host) = speed;
}
