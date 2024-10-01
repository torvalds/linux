// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
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

	if (!(ha->fw_dump_reading || ha->mctp_dump_reading ||
	      ha->mpi_fw_dump_reading))
		return 0;

	mutex_lock(&ha->optrom_mutex);
	if (IS_P3P_TYPE(ha)) {
		if (off < ha->md_template_size) {
			rval = memory_read_from_buffer(buf, count,
			    &off, ha->md_tmplt_hdr, ha->md_template_size);
		} else {
			off -= ha->md_template_size;
			rval = memory_read_from_buffer(buf, count,
			    &off, ha->md_dump, ha->md_dump_size);
		}
	} else if (ha->mctp_dumped && ha->mctp_dump_reading) {
		rval = memory_read_from_buffer(buf, count, &off, ha->mctp_dump,
		    MCTP_DUMP_SIZE);
	} else if (ha->mpi_fw_dumped && ha->mpi_fw_dump_reading) {
		rval = memory_read_from_buffer(buf, count, &off,
					       ha->mpi_fw_dump,
					       ha->mpi_fw_dump_len);
	} else if (ha->fw_dump_reading) {
		rval = memory_read_from_buffer(buf, count, &off, ha->fw_dump,
					ha->fw_dump_len);
	} else {
		rval = 0;
	}
	mutex_unlock(&ha->optrom_mutex);
	return rval;
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
		ha->fw_dumped = false;
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
		} else {
			qla2x00_system_error(vha);
		}
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
	case 8:
		if (!ha->mpi_fw_dump_reading)
			break;
		ql_log(ql_log_info, vha, 0x70e7,
		       "MPI firmware dump cleared on (%ld).\n", vha->host_no);
		ha->mpi_fw_dump_reading = 0;
		ha->mpi_fw_dumped = 0;
		break;
	case 9:
		if (ha->mpi_fw_dumped && !ha->mpi_fw_dump_reading) {
			ha->mpi_fw_dump_reading = 1;
			ql_log(ql_log_info, vha, 0x70e8,
			       "Raw MPI firmware dump ready for read on (%ld).\n",
			       vha->host_no);
		}
		break;
	case 10:
		if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			ql_log(ql_log_info, vha, 0x70e9,
			       "Issuing MPI firmware dump on host#%ld.\n",
			       vha->host_no);
			ha->isp_ops->mpi_fw_dump(vha, 0);
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
	uint32_t faddr;
	struct active_regions active_regions = { };

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	mutex_lock(&ha->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EAGAIN;
	}

	if (!IS_NOCACHE_VPD_TYPE(ha)) {
		mutex_unlock(&ha->optrom_mutex);
		goto skip;
	}

	faddr = ha->flt_region_nvram;
	if (IS_QLA28XX(ha)) {
		qla28xx_get_aux_images(vha, &active_regions);
		if (active_regions.aux.vpd_nvram == QLA27XX_SECONDARY_IMAGE)
			faddr = ha->flt_region_nvram_sec;
	}
	ha->isp_ops->read_optrom(vha, ha->nvram, faddr << 2, ha->nvram_size);

	mutex_unlock(&ha->optrom_mutex);

skip:
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
		__le32 *iter = (__force __le32 *)buf;
		uint32_t chksum;

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

	mutex_lock(&ha->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, buf, ha->nvram_base, count);
	ha->isp_ops->read_nvram(vha, ha->nvram, ha->nvram_base,
	    count);
	mutex_unlock(&ha->optrom_mutex);

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
	if (size > ha->optrom_size - start)
		size = ha->optrom_size - start;

	mutex_lock(&ha->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EAGAIN;
	}
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
		ha->optrom_region_size = size;

		ha->optrom_state = QLA_SREADING;
		ha->optrom_buffer = vzalloc(ha->optrom_region_size);
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
		 *
		 * > ISP25xx type boards:
		 *
		 *      None -- should go through BSG.
		 */
		valid = 0;
		if (ha->optrom_size == OPTROM_SIZE_2300 && start == 0)
			valid = 1;
		else if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha))
			valid = 1;
		if (!valid) {
			ql_log(ql_log_warn, vha, 0x7065,
			    "Invalid start region 0x%x/0x%x.\n", start, size);
			rval = -EINVAL;
			goto out;
		}

		ha->optrom_region_start = start;
		ha->optrom_region_size = size;

		ha->optrom_state = QLA_SWRITING;
		ha->optrom_buffer = vzalloc(ha->optrom_region_size);
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

		rval = ha->isp_ops->write_optrom(vha, ha->optrom_buffer,
		    ha->optrom_region_start, ha->optrom_region_size);
		if (rval)
			rval = -EIO;
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
	struct active_regions active_regions = { };

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -EAGAIN;

	if (!capable(CAP_SYS_ADMIN))
		return -EINVAL;

	if (!IS_NOCACHE_VPD_TYPE(ha))
		goto skip;

	faddr = ha->flt_region_vpd << 2;

	if (IS_QLA28XX(ha)) {
		qla28xx_get_aux_images(vha, &active_regions);
		if (active_regions.aux.vpd_nvram == QLA27XX_SECONDARY_IMAGE)
			faddr = ha->flt_region_vpd_sec << 2;

		ql_dbg(ql_dbg_init, vha, 0x7070,
		    "Loading %s nvram image.\n",
		    active_regions.aux.vpd_nvram == QLA27XX_PRIMARY_IMAGE ?
		    "primary" : "secondary");
	}

	mutex_lock(&ha->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EAGAIN;
	}

	ha->isp_ops->read_optrom(vha, ha->vpd, faddr, ha->vpd_size);
	mutex_unlock(&ha->optrom_mutex);

	ha->isp_ops->read_optrom(vha, ha->vpd, faddr, ha->vpd_size);
skip:
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

	if (qla2x00_chip_is_down(vha))
		return 0;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->vpd_size ||
	    !ha->isp_ops->write_nvram)
		return 0;

	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x706a,
		    "HBA not online, failing VPD update.\n");
		return -EAGAIN;
	}

	mutex_lock(&ha->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, buf, ha->vpd_base, count);
	ha->isp_ops->read_nvram(vha, ha->vpd, ha->vpd_base, count);

	/* Update flash version information for 4Gb & above. */
	if (!IS_FWI2_CAPABLE(ha)) {
		mutex_unlock(&ha->optrom_mutex);
		return -EINVAL;
	}

	tmp_data = vmalloc(256);
	if (!tmp_data) {
		mutex_unlock(&ha->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x706b,
		    "Unable to allocate memory for VPD information update.\n");
		return -ENOMEM;
	}
	ha->isp_ops->get_flash_version(vha, tmp_data);
	vfree(tmp_data);

	mutex_unlock(&ha->optrom_mutex);

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
	int rval;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count < SFP_DEV_SIZE)
		return 0;

	mutex_lock(&vha->hw->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		return 0;
	}

	rval = qla2x00_read_sfp_dev(vha, buf, count);
	mutex_unlock(&vha->hw->optrom_mutex);

	if (rval)
		return -EIO;

	return count;
}

static struct bin_attribute sysfs_sfp_attr = {
	.attr = {
		.name = "sfp",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = SFP_DEV_SIZE,
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

		if (vha->hw->flags.port_isolated) {
			ql_log(ql_log_info, vha, 0x706e,
			       "Port is isolated, returning.\n");
			return -EINVAL;
		}

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
		if (!IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
		    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
			return -EPERM;

		ql_log(ql_log_info, vha, 0x706f,
		    "Issuing MPI reset.\n");

		if (IS_QLA83XX(ha)) {
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
			WARN_ON_ONCE(qla2x00_wait_for_hba_online(vha) !=
				     QLA_SUCCESS);

			/* Issue MPI reset */
			scsi_block_requests(vha->host);
			if (qla81xx_restart_mpi_firmware(vha) != QLA_SUCCESS)
				ql_log(ql_log_warn, vha, 0x7070,
				    "MPI reset failed.\n");
			scsi_unblock_requests(vha->host);
			break;
		}
		break;
	case 0x2025e:
		if (!IS_P3P_TYPE(ha) || vha != base_vha) {
			ql_log(ql_log_info, vha, 0x7071,
			    "FCoE ctx reset not supported.\n");
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

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (unlikely(pci_channel_offline(vha->hw->pdev)))
		return 0;

	if (qla2x00_chip_is_down(vha))
		return 0;

	type = simple_strtol(buf, NULL, 10);

	did.b.domain = (type & 0x00ff0000) >> 16;
	did.b.area = (type & 0x0000ff00) >> 8;
	did.b.al_pa = (type & 0x000000ff);

	ql_log(ql_log_info, vha, 0xd04d, "portid=%02x%02x%02x done\n",
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

	if (unlikely(pci_channel_offline(ha->pdev)))
		return 0;
	mutex_lock(&vha->hw->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		return 0;
	}

	if (ha->xgmac_data)
		goto do_read;

	ha->xgmac_data = dma_alloc_coherent(&ha->pdev->dev, XGMAC_DATA_SIZE,
	    &ha->xgmac_data_dma, GFP_KERNEL);
	if (!ha->xgmac_data) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x7076,
		    "Unable to allocate memory for XGMAC read-data.\n");
		return 0;
	}

do_read:
	actual_size = 0;
	memset(ha->xgmac_data, 0, XGMAC_DATA_SIZE);

	rval = qla2x00_get_xgmac_stats(vha, ha->xgmac_data_dma,
	    XGMAC_DATA_SIZE, &actual_size);

	mutex_unlock(&vha->hw->optrom_mutex);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x7077,
		    "Unable to read XGMAC data (%x).\n", rval);
		count = 0;
	}

	count = actual_size > count ? count : actual_size;
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

	mutex_lock(&vha->hw->optrom_mutex);
	if (ha->dcbx_tlv)
		goto do_read;
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		return 0;
	}

	ha->dcbx_tlv = dma_alloc_coherent(&ha->pdev->dev, DCBX_TLV_DATA_SIZE,
	    &ha->dcbx_tlv_dma, GFP_KERNEL);
	if (!ha->dcbx_tlv) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x7078,
		    "Unable to allocate memory for DCBX TLV read-data.\n");
		return -ENOMEM;
	}

do_read:
	memset(ha->dcbx_tlv, 0, DCBX_TLV_DATA_SIZE);

	rval = qla2x00_get_dcbx_params(vha, ha->dcbx_tlv_dma,
	    DCBX_TLV_DATA_SIZE);

	mutex_unlock(&vha->hw->optrom_mutex);

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
	int type;
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
		if (iter->type && !IS_FWI2_CAPABLE(vha->hw))
			continue;
		if (iter->type == 2 && !IS_QLA25XX(vha->hw))
			continue;
		if (iter->type == 3 && !(IS_CNA_CAPABLE(vha->hw)))
			continue;

		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
		if (ret)
			ql_log(ql_log_warn, vha, 0x00f3,
			    "Unable to create sysfs %s binary attribute (%d).\n",
			    iter->name, ret);
		else
			ql_dbg(ql_dbg_init, vha, 0x00f4,
			    "Successfully created sysfs %s binary attribute.\n",
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
		if (iter->type && !IS_FWI2_CAPABLE(ha))
			continue;
		if (iter->type == 2 && !IS_QLA25XX(ha))
			continue;
		if (iter->type == 3 && !(IS_CNA_CAPABLE(ha)))
			continue;

		sysfs_remove_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
	}

	if (stop_beacon && ha->beacon_blink_led == 1)
		ha->isp_ops->beacon_off(vha);
}

/* Scsi_Host attributes. */

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
			 vha->hw->isp_ops->pci_info_str(vha, pci_info,
							sizeof(pci_info)));
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
	    qla2x00_chip_is_down(vha))
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
qla_zio_threshold_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return scnprintf(buf, PAGE_SIZE, "%d exchanges\n",
	    vha->hw->last_zio_threshold);
}

static ssize_t
qla_zio_threshold_store(struct device *dev, struct device_attribute *attr,
    const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int val = 0;

	if (vha->hw->zio_mode != QLA_ZIO_MODE_6)
		return -EINVAL;
	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;
	if (val < 0 || val > 256)
		return -ERANGE;

	atomic_set(&vha->hw->zio_threshold, val);
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

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	mutex_lock(&vha->hw->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x707a,
		    "Abort ISP active -- ignoring beacon request.\n");
		return -EBUSY;
	}

	if (val)
		rval = ha->isp_ops->beacon_on(vha);
	else
		rval = ha->isp_ops->beacon_off(vha);

	if (rval != QLA_SUCCESS)
		count = 0;

	mutex_unlock(&vha->hw->optrom_mutex);

	return count;
}

static ssize_t
qla2x00_beacon_config_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	uint16_t led[3] = { 0 };

	if (!IS_QLA2031(ha) && !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return -EPERM;

	if (ql26xx_led_config(vha, 0, led))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%#04hx %#04hx %#04hx\n",
	    led[0], led[1], led[2]);
}

static ssize_t
qla2x00_beacon_config_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	uint16_t options = BIT_0;
	uint16_t led[3] = { 0 };
	uint16_t word[4];
	int n;

	if (!IS_QLA2031(ha) && !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return -EPERM;

	n = sscanf(buf, "%hx %hx %hx %hx", word+0, word+1, word+2, word+3);
	if (n == 4) {
		if (word[0] == 3) {
			options |= BIT_3|BIT_2|BIT_1;
			led[0] = word[1];
			led[1] = word[2];
			led[2] = word[3];
			goto write;
		}
		return -EINVAL;
	}

	if (n == 2) {
		/* check led index */
		if (word[0] == 0) {
			options |= BIT_2;
			led[0] = word[1];
			goto write;
		}
		if (word[0] == 1) {
			options |= BIT_3;
			led[1] = word[1];
			goto write;
		}
		if (word[0] == 2) {
			options |= BIT_1;
			led[2] = word[1];
			goto write;
		}
		return -EINVAL;
	}

	return -EINVAL;

write:
	if (ql26xx_led_config(vha, options, led))
		return -EFAULT;

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

	if (!IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
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
	uint16_t status[2] = { 0 };
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA84XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	if (!ha->cs84xx->op_fw_version) {
		rval = qla84xx_verify_chip(vha, status);

		if (!rval && !status[0])
			return scnprintf(buf, PAGE_SIZE, "%u\n",
			    (uint32_t)ha->cs84xx->op_fw_version);
	}

	return scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t
qla2x00_serdes_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d\n",
	    ha->serdes_version[0], ha->serdes_version[1],
	    ha->serdes_version[2]);
}

static ssize_t
qla2x00_mpi_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha) && !IS_QLA8044(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
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
	int rc;

	mutex_lock(&vha->hw->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x70dc, "ISP reset active.\n");
		goto done;
	}

	if (vha->hw->flags.eeh_busy) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x70dd, "PCI EEH busy.\n");
		goto done;
	}

	rc = qla2x00_get_thermal_temp(vha, &temp);
	mutex_unlock(&vha->hw->optrom_mutex);
	if (rc == QLA_SUCCESS)
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

	mutex_lock(&vha->hw->optrom_mutex);
	if (qla2x00_chip_is_down(vha)) {
		mutex_unlock(&vha->hw->optrom_mutex);
		ql_log(ql_log_warn, vha, 0x707c,
		    "ISP reset active.\n");
		goto out;
	} else if (vha->hw->flags.eeh_busy) {
		mutex_unlock(&vha->hw->optrom_mutex);
		goto out;
	}

	rval = qla2x00_get_firmware_state(vha, state);
	mutex_unlock(&vha->hw->optrom_mutex);
out:
	if (rval != QLA_SUCCESS) {
		memset(state, -1, sizeof(state));
		rval = qla2x00_get_firmware_state(vha, state);
	}

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

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%d.%02d.%02d\n",
	    ha->pep_version[0], ha->pep_version[1], ha->pep_version[2]);
}

static ssize_t
qla2x00_min_supported_speed_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%s\n",
	    ha->min_supported_speed == 6 ? "64Gps" :
	    ha->min_supported_speed == 5 ? "32Gps" :
	    ha->min_supported_speed == 4 ? "16Gps" :
	    ha->min_supported_speed == 3 ? "8Gps" :
	    ha->min_supported_speed == 2 ? "4Gps" :
	    ha->min_supported_speed != 0 ? "unknown" : "");
}

static ssize_t
qla2x00_max_supported_speed_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%s\n",
	    ha->max_supported_speed  == 2 ? "64Gps" :
	    ha->max_supported_speed  == 1 ? "32Gps" :
	    ha->max_supported_speed  == 0 ? "16Gps" : "unknown");
}

static ssize_t
qla2x00_port_speed_store(struct device *dev, struct device_attribute *attr,
    const char *buf, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(dev));
	ulong type, speed;
	int oldspeed, rval;
	int mode = QLA_SET_DATA_RATE_LR;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha)) {
		ql_log(ql_log_warn, vha, 0x70d8,
		    "Speed setting not supported \n");
		return -EINVAL;
	}

	rval = kstrtol(buf, 10, &type);
	if (rval)
		return rval;
	speed = type;
	if (type == 40 || type == 80 || type == 160 ||
	    type == 320) {
		ql_dbg(ql_dbg_user, vha, 0x70d9,
		    "Setting will be affected after a loss of sync\n");
		type = type/10;
		mode = QLA_SET_DATA_RATE_NOLR;
	}

	oldspeed = ha->set_data_rate;

	switch (type) {
	case 0:
		ha->set_data_rate = PORT_SPEED_AUTO;
		break;
	case 4:
		ha->set_data_rate = PORT_SPEED_4GB;
		break;
	case 8:
		ha->set_data_rate = PORT_SPEED_8GB;
		break;
	case 16:
		ha->set_data_rate = PORT_SPEED_16GB;
		break;
	case 32:
		ha->set_data_rate = PORT_SPEED_32GB;
		break;
	default:
		ql_log(ql_log_warn, vha, 0x1199,
		    "Unrecognized speed setting:%lx. Setting Autoneg\n",
		    speed);
		ha->set_data_rate = PORT_SPEED_AUTO;
	}

	if (qla2x00_chip_is_down(vha) || (oldspeed == ha->set_data_rate))
		return -EINVAL;

	ql_log(ql_log_info, vha, 0x70da,
	    "Setting speed to %lx Gbps \n", type);

	rval = qla2x00_set_data_rate(vha, mode);
	if (rval != QLA_SUCCESS)
		return -EIO;

	return strlen(buf);
}

static const struct {
	u16 rate;
	char *str;
} port_speed_str[] = {
	{ PORT_SPEED_4GB, "4" },
	{ PORT_SPEED_8GB, "8" },
	{ PORT_SPEED_16GB, "16" },
	{ PORT_SPEED_32GB, "32" },
	{ PORT_SPEED_64GB, "64" },
	{ PORT_SPEED_10GB, "10" },
};

static ssize_t
qla2x00_port_speed_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	ssize_t rval;
	u16 i;
	char *speed = "Unknown";

	rval = qla2x00_get_data_rate(vha);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x70db,
		    "Unable to get port speed rval:%zd\n", rval);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(port_speed_str); i++) {
		if (port_speed_str[i].rate != ha->link_data_rate)
			continue;
		speed = port_speed_str[i].str;
		break;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", speed);
}

static ssize_t
qla2x00_mpi_pause_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int rval = 0;

	if (sscanf(buf, "%d", &rval) != 1)
		return -EINVAL;

	ql_log(ql_log_warn, vha, 0x7089, "Pausing MPI...\n");

	rval = qla83xx_wr_reg(vha, 0x002012d4, 0x30000001);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x708a, "Unable to pause MPI.\n");
		count = 0;
	}

	return count;
}

static DEVICE_ATTR(mpi_pause, S_IWUSR, NULL, qla2x00_mpi_pause_store);

/* ----- */

static ssize_t
qlini_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int len = 0;

	len += scnprintf(buf + len, PAGE_SIZE-len,
	    "Supported options: enabled | disabled | dual | exclusive\n");

	/* --- */
	len += scnprintf(buf + len, PAGE_SIZE-len, "Current selection: ");

	switch (vha->qlini_mode) {
	case QLA2XXX_INI_MODE_EXCLUSIVE:
		len += scnprintf(buf + len, PAGE_SIZE-len,
		    QLA2XXX_INI_MODE_STR_EXCLUSIVE);
		break;
	case QLA2XXX_INI_MODE_DISABLED:
		len += scnprintf(buf + len, PAGE_SIZE-len,
		    QLA2XXX_INI_MODE_STR_DISABLED);
		break;
	case QLA2XXX_INI_MODE_ENABLED:
		len += scnprintf(buf + len, PAGE_SIZE-len,
		    QLA2XXX_INI_MODE_STR_ENABLED);
		break;
	case QLA2XXX_INI_MODE_DUAL:
		len += scnprintf(buf + len, PAGE_SIZE-len,
		    QLA2XXX_INI_MODE_STR_DUAL);
		break;
	}
	len += scnprintf(buf + len, PAGE_SIZE-len, "\n");

	return len;
}

static char *mode_to_str[] = {
	"exclusive",
	"disabled",
	"enabled",
	"dual",
};

#define NEED_EXCH_OFFLOAD(_exchg) ((_exchg) > FW_DEF_EXCHANGES_CNT)
static void qla_set_ini_mode(scsi_qla_host_t *vha, int op)
{
	enum {
		NO_ACTION,
		MODE_CHANGE_ACCEPT,
		MODE_CHANGE_NO_ACTION,
		TARGET_STILL_ACTIVE,
	};
	int action = NO_ACTION;
	int set_mode = 0;
	u8  eo_toggle = 0;	/* exchange offload flipped */

	switch (vha->qlini_mode) {
	case QLA2XXX_INI_MODE_DISABLED:
		switch (op) {
		case QLA2XXX_INI_MODE_DISABLED:
			if (qla_tgt_mode_enabled(vha)) {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;
				if (((vha->ql2xexchoffld !=
				    vha->u_ql2xexchoffld) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld)) ||
				    eo_toggle) {
					/*
					 * The number of exchange to be offload
					 * was tweaked or offload option was
					 * flipped
					 */
					action = MODE_CHANGE_ACCEPT;
				} else {
					action = MODE_CHANGE_NO_ACTION;
				}
			} else {
				action = MODE_CHANGE_NO_ACTION;
			}
			break;
		case QLA2XXX_INI_MODE_EXCLUSIVE:
			if (qla_tgt_mode_enabled(vha)) {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;
				if (((vha->ql2xexchoffld !=
				    vha->u_ql2xexchoffld) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld)) ||
				    eo_toggle) {
					/*
					 * The number of exchange to be offload
					 * was tweaked or offload option was
					 * flipped
					 */
					action = MODE_CHANGE_ACCEPT;
				} else {
					action = MODE_CHANGE_NO_ACTION;
				}
			} else {
				action = MODE_CHANGE_ACCEPT;
			}
			break;
		case QLA2XXX_INI_MODE_DUAL:
			action = MODE_CHANGE_ACCEPT;
			/* active_mode is target only, reset it to dual */
			if (qla_tgt_mode_enabled(vha)) {
				set_mode = 1;
				action = MODE_CHANGE_ACCEPT;
			} else {
				action = MODE_CHANGE_NO_ACTION;
			}
			break;

		case QLA2XXX_INI_MODE_ENABLED:
			if (qla_tgt_mode_enabled(vha))
				action = TARGET_STILL_ACTIVE;
			else {
				action = MODE_CHANGE_ACCEPT;
				set_mode = 1;
			}
			break;
		}
		break;

	case QLA2XXX_INI_MODE_EXCLUSIVE:
		switch (op) {
		case QLA2XXX_INI_MODE_EXCLUSIVE:
			if (qla_tgt_mode_enabled(vha)) {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;
				if (((vha->ql2xexchoffld !=
				    vha->u_ql2xexchoffld) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld)) ||
				    eo_toggle)
					/*
					 * The number of exchange to be offload
					 * was tweaked or offload option was
					 * flipped
					 */
					action = MODE_CHANGE_ACCEPT;
				else
					action = NO_ACTION;
			} else
				action = NO_ACTION;

			break;

		case QLA2XXX_INI_MODE_DISABLED:
			if (qla_tgt_mode_enabled(vha)) {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;
				if (((vha->ql2xexchoffld !=
				      vha->u_ql2xexchoffld) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld)) ||
				    eo_toggle)
					action = MODE_CHANGE_ACCEPT;
				else
					action = MODE_CHANGE_NO_ACTION;
			} else
				action = MODE_CHANGE_NO_ACTION;
			break;

		case QLA2XXX_INI_MODE_DUAL: /* exclusive -> dual */
			if (qla_tgt_mode_enabled(vha)) {
				action = MODE_CHANGE_ACCEPT;
				set_mode = 1;
			} else
				action = MODE_CHANGE_ACCEPT;
			break;

		case QLA2XXX_INI_MODE_ENABLED:
			if (qla_tgt_mode_enabled(vha))
				action = TARGET_STILL_ACTIVE;
			else {
				if (vha->hw->flags.fw_started)
					action = MODE_CHANGE_NO_ACTION;
				else
					action = MODE_CHANGE_ACCEPT;
			}
			break;
		}
		break;

	case QLA2XXX_INI_MODE_ENABLED:
		switch (op) {
		case QLA2XXX_INI_MODE_ENABLED:
			if (NEED_EXCH_OFFLOAD(vha->u_ql2xiniexchg) !=
			    vha->hw->flags.exchoffld_enabled)
				eo_toggle = 1;
			if (((vha->ql2xiniexchg != vha->u_ql2xiniexchg) &&
				NEED_EXCH_OFFLOAD(vha->u_ql2xiniexchg)) ||
			    eo_toggle)
				action = MODE_CHANGE_ACCEPT;
			else
				action = NO_ACTION;
			break;
		case QLA2XXX_INI_MODE_DUAL:
		case QLA2XXX_INI_MODE_DISABLED:
			action = MODE_CHANGE_ACCEPT;
			break;
		default:
			action = MODE_CHANGE_NO_ACTION;
			break;
		}
		break;

	case QLA2XXX_INI_MODE_DUAL:
		switch (op) {
		case QLA2XXX_INI_MODE_DUAL:
			if (qla_tgt_mode_enabled(vha) ||
			    qla_dual_mode_enabled(vha)) {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld +
					vha->u_ql2xiniexchg) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;

				if ((((vha->ql2xexchoffld +
				       vha->ql2xiniexchg) !=
				    (vha->u_ql2xiniexchg +
				     vha->u_ql2xexchoffld)) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xiniexchg +
					vha->u_ql2xexchoffld)) || eo_toggle)
					action = MODE_CHANGE_ACCEPT;
				else
					action = NO_ACTION;
			} else {
				if (NEED_EXCH_OFFLOAD(vha->u_ql2xexchoffld +
					vha->u_ql2xiniexchg) !=
				    vha->hw->flags.exchoffld_enabled)
					eo_toggle = 1;

				if ((((vha->ql2xexchoffld + vha->ql2xiniexchg)
				    != (vha->u_ql2xiniexchg +
					vha->u_ql2xexchoffld)) &&
				    NEED_EXCH_OFFLOAD(vha->u_ql2xiniexchg +
					vha->u_ql2xexchoffld)) || eo_toggle)
					action = MODE_CHANGE_NO_ACTION;
				else
					action = NO_ACTION;
			}
			break;

		case QLA2XXX_INI_MODE_DISABLED:
			if (qla_tgt_mode_enabled(vha) ||
			    qla_dual_mode_enabled(vha)) {
				/* turning off initiator mode */
				set_mode = 1;
				action = MODE_CHANGE_ACCEPT;
			} else {
				action = MODE_CHANGE_NO_ACTION;
			}
			break;

		case QLA2XXX_INI_MODE_EXCLUSIVE:
			if (qla_tgt_mode_enabled(vha) ||
			    qla_dual_mode_enabled(vha)) {
				set_mode = 1;
				action = MODE_CHANGE_ACCEPT;
			} else {
				action = MODE_CHANGE_ACCEPT;
			}
			break;

		case QLA2XXX_INI_MODE_ENABLED:
			if (qla_tgt_mode_enabled(vha) ||
			    qla_dual_mode_enabled(vha)) {
				action = TARGET_STILL_ACTIVE;
			} else {
				action = MODE_CHANGE_ACCEPT;
			}
		}
		break;
	}

	switch (action) {
	case MODE_CHANGE_ACCEPT:
		ql_log(ql_log_warn, vha, 0xffff,
		    "Mode change accepted. From %s to %s, Tgt exchg %d|%d. ini exchg %d|%d\n",
		    mode_to_str[vha->qlini_mode], mode_to_str[op],
		    vha->ql2xexchoffld, vha->u_ql2xexchoffld,
		    vha->ql2xiniexchg, vha->u_ql2xiniexchg);

		vha->qlini_mode = op;
		vha->ql2xexchoffld = vha->u_ql2xexchoffld;
		vha->ql2xiniexchg = vha->u_ql2xiniexchg;
		if (set_mode)
			qlt_set_mode(vha);
		vha->flags.online = 1;
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MODE_CHANGE_NO_ACTION:
		ql_log(ql_log_warn, vha, 0xffff,
		    "Mode is set. No action taken. From %s to %s, Tgt exchg %d|%d. ini exchg %d|%d\n",
		    mode_to_str[vha->qlini_mode], mode_to_str[op],
		    vha->ql2xexchoffld, vha->u_ql2xexchoffld,
		    vha->ql2xiniexchg, vha->u_ql2xiniexchg);
		vha->qlini_mode = op;
		vha->ql2xexchoffld = vha->u_ql2xexchoffld;
		vha->ql2xiniexchg = vha->u_ql2xiniexchg;
		break;

	case TARGET_STILL_ACTIVE:
		ql_log(ql_log_warn, vha, 0xffff,
		    "Target Mode is active. Unable to change Mode.\n");
		break;

	case NO_ACTION:
	default:
		ql_log(ql_log_warn, vha, 0xffff,
		    "Mode unchange. No action taken. %d|%d pct %d|%d.\n",
		    vha->qlini_mode, op,
		    vha->ql2xexchoffld, vha->u_ql2xexchoffld);
		break;
	}
}

static ssize_t
qlini_mode_store(struct device *dev, struct device_attribute *attr,
    const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int ini;

	if (!buf)
		return -EINVAL;

	if (strncasecmp(QLA2XXX_INI_MODE_STR_EXCLUSIVE, buf,
		strlen(QLA2XXX_INI_MODE_STR_EXCLUSIVE)) == 0)
		ini = QLA2XXX_INI_MODE_EXCLUSIVE;
	else if (strncasecmp(QLA2XXX_INI_MODE_STR_DISABLED, buf,
		strlen(QLA2XXX_INI_MODE_STR_DISABLED)) == 0)
		ini = QLA2XXX_INI_MODE_DISABLED;
	else if (strncasecmp(QLA2XXX_INI_MODE_STR_ENABLED, buf,
		  strlen(QLA2XXX_INI_MODE_STR_ENABLED)) == 0)
		ini = QLA2XXX_INI_MODE_ENABLED;
	else if (strncasecmp(QLA2XXX_INI_MODE_STR_DUAL, buf,
		strlen(QLA2XXX_INI_MODE_STR_DUAL)) == 0)
		ini = QLA2XXX_INI_MODE_DUAL;
	else
		return -EINVAL;

	qla_set_ini_mode(vha, ini);
	return strlen(buf);
}

static ssize_t
ql2xexchoffld_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int len = 0;

	len += scnprintf(buf + len, PAGE_SIZE-len,
		"target exchange: new %d : current: %d\n\n",
		vha->u_ql2xexchoffld, vha->ql2xexchoffld);

	len += scnprintf(buf + len, PAGE_SIZE-len,
	    "Please (re)set operating mode via \"/sys/class/scsi_host/host%ld/qlini_mode\" to load new setting.\n",
	    vha->host_no);

	return len;
}

static ssize_t
ql2xexchoffld_store(struct device *dev, struct device_attribute *attr,
    const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val > FW_MAX_EXCHANGES_CNT)
		val = FW_MAX_EXCHANGES_CNT;
	else if (val < 0)
		val = 0;

	vha->u_ql2xexchoffld = val;
	return strlen(buf);
}

static ssize_t
ql2xiniexchg_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int len = 0;

	len += scnprintf(buf + len, PAGE_SIZE-len,
		"target exchange: new %d : current: %d\n\n",
		vha->u_ql2xiniexchg, vha->ql2xiniexchg);

	len += scnprintf(buf + len, PAGE_SIZE-len,
	    "Please (re)set operating mode via \"/sys/class/scsi_host/host%ld/qlini_mode\" to load new setting.\n",
	    vha->host_no);

	return len;
}

static ssize_t
ql2xiniexchg_store(struct device *dev, struct device_attribute *attr,
    const char *buf, size_t count)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val > FW_MAX_EXCHANGES_CNT)
		val = FW_MAX_EXCHANGES_CNT;
	else if (val < 0)
		val = 0;

	vha->u_ql2xiniexchg = val;
	return strlen(buf);
}

static ssize_t
qla2x00_dif_bundle_statistics_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	return scnprintf(buf, PAGE_SIZE,
	    "cross=%llu read=%llu write=%llu kalloc=%llu dma_alloc=%llu unusable=%u\n",
	    ha->dif_bundle_crossed_pages, ha->dif_bundle_reads,
	    ha->dif_bundle_writes, ha->dif_bundle_kallocs,
	    ha->dif_bundle_dma_allocs, ha->pool.unusable.count);
}

static ssize_t
qla2x00_fw_attr_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%llx\n",
	    (uint64_t)ha->fw_attributes_ext[1] << 48 |
	    (uint64_t)ha->fw_attributes_ext[0] << 32 |
	    (uint64_t)ha->fw_attributes_h << 16 |
	    (uint64_t)ha->fw_attributes);
}

static ssize_t
qla2x00_port_no_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return scnprintf(buf, PAGE_SIZE, "%u\n", vha->hw->port_no);
}

static ssize_t
qla2x00_dport_diagnostics_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_QLA83XX(vha->hw) && !IS_QLA27XX(vha->hw) &&
	    !IS_QLA28XX(vha->hw))
		return scnprintf(buf, PAGE_SIZE, "\n");

	if (!*vha->dport_data)
		return scnprintf(buf, PAGE_SIZE, "\n");

	return scnprintf(buf, PAGE_SIZE, "%04x %04x %04x %04x\n",
	    vha->dport_data[0], vha->dport_data[1],
	    vha->dport_data[2], vha->dport_data[3]);
}
static DEVICE_ATTR(dport_diagnostics, 0444,
	   qla2x00_dport_diagnostics_show, NULL);

static DEVICE_STRING_ATTR_RO(driver_version, S_IRUGO, qla2x00_version_str);
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
static DEVICE_ATTR(beacon_config, 0644, qla2x00_beacon_config_show,
		   qla2x00_beacon_config_store);
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
static DEVICE_ATTR(serdes_version, 0444, qla2x00_serdes_version_show, NULL);
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
static DEVICE_ATTR(min_supported_speed, 0444,
		   qla2x00_min_supported_speed_show, NULL);
static DEVICE_ATTR(max_supported_speed, 0444,
		   qla2x00_max_supported_speed_show, NULL);
static DEVICE_ATTR(zio_threshold, 0644,
    qla_zio_threshold_show,
    qla_zio_threshold_store);
static DEVICE_ATTR_RW(qlini_mode);
static DEVICE_ATTR_RW(ql2xexchoffld);
static DEVICE_ATTR_RW(ql2xiniexchg);
static DEVICE_ATTR(dif_bundle_statistics, 0444,
    qla2x00_dif_bundle_statistics_show, NULL);
static DEVICE_ATTR(port_speed, 0644, qla2x00_port_speed_show,
    qla2x00_port_speed_store);
static DEVICE_ATTR(port_no, 0444, qla2x00_port_no_show, NULL);
static DEVICE_ATTR(fw_attr, 0444, qla2x00_fw_attr_show, NULL);

static struct attribute *qla2x00_host_attrs[] = {
	&dev_attr_driver_version.attr.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_serial_num.attr,
	&dev_attr_isp_name.attr,
	&dev_attr_isp_id.attr,
	&dev_attr_model_name.attr,
	&dev_attr_model_desc.attr,
	&dev_attr_pci_info.attr,
	&dev_attr_link_state.attr,
	&dev_attr_zio.attr,
	&dev_attr_zio_timer.attr,
	&dev_attr_beacon.attr,
	&dev_attr_beacon_config.attr,
	&dev_attr_optrom_bios_version.attr,
	&dev_attr_optrom_efi_version.attr,
	&dev_attr_optrom_fcode_version.attr,
	&dev_attr_optrom_fw_version.attr,
	&dev_attr_84xx_fw_version.attr,
	&dev_attr_total_isp_aborts.attr,
	&dev_attr_serdes_version.attr,
	&dev_attr_mpi_version.attr,
	&dev_attr_phy_version.attr,
	&dev_attr_flash_block_size.attr,
	&dev_attr_vlan_id.attr,
	&dev_attr_vn_port_mac_address.attr,
	&dev_attr_fabric_param.attr,
	&dev_attr_fw_state.attr,
	&dev_attr_optrom_gold_fw_version.attr,
	&dev_attr_thermal_temp.attr,
	&dev_attr_diag_requests.attr,
	&dev_attr_diag_megabytes.attr,
	&dev_attr_fw_dump_size.attr,
	&dev_attr_allow_cna_fw_dump.attr,
	&dev_attr_pep_version.attr,
	&dev_attr_min_supported_speed.attr,
	&dev_attr_max_supported_speed.attr,
	&dev_attr_zio_threshold.attr,
	&dev_attr_dif_bundle_statistics.attr,
	&dev_attr_port_speed.attr,
	&dev_attr_port_no.attr,
	&dev_attr_fw_attr.attr,
	&dev_attr_dport_diagnostics.attr,
	&dev_attr_mpi_pause.attr,
	&dev_attr_qlini_mode.attr,
	&dev_attr_ql2xiniexchg.attr,
	&dev_attr_ql2xexchoffld.attr,
	NULL,
};

static umode_t qla_host_attr_is_visible(struct kobject *kobj,
					struct attribute *attr, int i)
{
	if (ql2x_ini_mode != QLA2XXX_INI_MODE_DUAL &&
	    (attr == &dev_attr_qlini_mode.attr ||
	     attr == &dev_attr_ql2xiniexchg.attr ||
	     attr == &dev_attr_ql2xexchoffld.attr))
		return 0;
	return attr->mode;
}

static const struct attribute_group qla2x00_host_attr_group = {
	.is_visible = qla_host_attr_is_visible,
	.attrs = qla2x00_host_attrs
};

const struct attribute_group *qla2x00_host_groups[] = {
	&qla2x00_host_attr_group,
	NULL
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
	scsi_qla_host_t *vha = shost_priv(shost);
	u32 speed;

	if (IS_QLAFX00(vha->hw)) {
		qlafx00_get_host_speed(shost);
		return;
	}

	switch (vha->hw->link_data_rate) {
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
	case PORT_SPEED_64GB:
		speed = FC_PORTSPEED_64GBIT;
		break;
	default:
		speed = FC_PORTSPEED_UNKNOWN;
		break;
	}

	fc_host_speed(shost) = speed;
}

static void
qla2x00_get_host_port_type(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	uint32_t port_type;

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
	default:
		port_type = FC_PORTTYPE_UNKNOWN;
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

static inline void
qla2x00_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;

	rport->dev_loss_tmo = timeout ? timeout : 1;

	if (IS_ENABLED(CONFIG_NVME_FC) && fcport && fcport->nvme_remote_port)
		nvme_fc_set_remoteport_devloss(fcport->nvme_remote_port,
					       rport->dev_loss_tmo);
}

static void
qla2x00_dev_loss_tmo_callbk(struct fc_rport *rport)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;
	unsigned long flags;

	if (!fcport)
		return;

	ql_dbg(ql_dbg_async, fcport->vha, 0x5101,
	       DBG_FCPORT_PRFMT(fcport, "dev_loss_tmo expiry, rport_state=%d",
				rport->port_state));

	/*
	 * Now that the rport has been deleted, set the fcport state to
	 * FCS_DEVICE_DEAD, if the fcport is still lost.
	 */
	if (fcport->scan_state != QLA_FCPORT_FOUND)
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_DEAD);

	/*
	 * Transport has effectively 'deleted' the rport, clear
	 * all local references.
	 */
	spin_lock_irqsave(host->host_lock, flags);
	/* Confirm port has not reappeared before clearing pointers. */
	if (rport->port_state != FC_PORTSTATE_ONLINE) {
		fcport->rport = NULL;
		*((fc_port_t **)rport->dd_data) = NULL;
	}
	spin_unlock_irqrestore(host->host_lock, flags);

	if (test_bit(ABORT_ISP_ACTIVE, &fcport->vha->dpc_flags))
		return;

	if (unlikely(pci_channel_offline(fcport->vha->hw->pdev))) {
		/* Will wait for wind down of adapter */
		ql_dbg(ql_dbg_aer, fcport->vha, 0x900c,
		    "%s pci offline detected (id %06x)\n", __func__,
		    fcport->d_id.b24);
		qla_pci_set_eeh_busy(fcport->vha);
		qla2x00_eh_wait_for_pending_commands(fcport->vha, fcport->d_id.b24,
		    0, WAIT_TARGET);
		return;
	}
}

static void
qla2x00_terminate_rport_io(struct fc_rport *rport)
{
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;
	scsi_qla_host_t *vha;

	if (!fcport)
		return;

	if (test_bit(UNLOADING, &fcport->vha->dpc_flags))
		return;

	if (test_bit(ABORT_ISP_ACTIVE, &fcport->vha->dpc_flags))
		return;
	vha = fcport->vha;

	if (unlikely(pci_channel_offline(fcport->vha->hw->pdev))) {
		/* Will wait for wind down of adapter */
		ql_dbg(ql_dbg_aer, fcport->vha, 0x900b,
		    "%s pci offline detected (id %06x)\n", __func__,
		    fcport->d_id.b24);
		qla_pci_set_eeh_busy(vha);
		qla2x00_eh_wait_for_pending_commands(fcport->vha, fcport->d_id.b24,
			0, WAIT_TARGET);
		return;
	}
	/*
	 * At this point all fcport's software-states are cleared.  Perform any
	 * final cleanup of firmware resources (PCBs and XCBs).
	 *
	 * Attempt to cleanup only lost devices.
	 */
	if (fcport->loop_id != FC_NO_LOOP_ID) {
		if (IS_FWI2_CAPABLE(fcport->vha->hw) &&
		    fcport->scan_state != QLA_FCPORT_FOUND) {
			if (fcport->loop_id != FC_NO_LOOP_ID)
				fcport->logout_on_delete = 1;

			if (!EDIF_NEGOTIATION_PENDING(fcport)) {
				ql_dbg(ql_dbg_disc, fcport->vha, 0x911e,
				       "%s %d schedule session deletion\n", __func__,
				       __LINE__);
				qlt_schedule_sess_for_deletion(fcport);
			}
		} else if (!IS_FWI2_CAPABLE(fcport->vha->hw)) {
			qla2x00_port_logout(fcport->vha, fcport);
		}
	}

	/* check for any straggling io left behind */
	if (qla2x00_eh_wait_for_pending_commands(fcport->vha, fcport->d_id.b24, 0, WAIT_TARGET)) {
		ql_log(ql_log_warn, vha, 0x300b,
		       "IO not return.  Resetting. \n");
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
		qla2x00_wait_for_chip_reset(vha);
	}
}

static int
qla2x00_issue_lip(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	if (IS_QLAFX00(vha->hw))
		return 0;

	if (vha->hw->flags.port_isolated)
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
	struct qla_qpair *qpair;
	int i;
	u64 ib = 0, ob = 0, ir = 0, or = 0;

	memset(p, -1, sizeof(*p));

	if (IS_QLAFX00(vha->hw))
		goto done;

	if (test_bit(UNLOADING, &vha->dpc_flags))
		goto done;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto done;

	if (qla2x00_chip_is_down(vha))
		goto done;

	stats = dma_alloc_coherent(&ha->pdev->dev, sizeof(*stats), &stats_dma,
				   GFP_KERNEL);
	if (!stats) {
		ql_log(ql_log_warn, vha, 0x707d,
		    "Failed to allocate memory for stats.\n");
		goto done;
	}

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

	/* --- */
	for (i = 0; i < vha->hw->max_qpairs; i++) {
		qpair = vha->hw->queue_pair_map[i];
		if (!qpair)
			continue;
		ir += qpair->counters.input_requests;
		or += qpair->counters.output_requests;
		ib += qpair->counters.input_bytes;
		ob += qpair->counters.output_bytes;
	}
	ir += ha->base_qpair->counters.input_requests;
	or += ha->base_qpair->counters.output_requests;
	ib += ha->base_qpair->counters.input_bytes;
	ob += ha->base_qpair->counters.output_bytes;

	ir += vha->qla_stats.input_requests;
	or += vha->qla_stats.output_requests;
	ib += vha->qla_stats.input_bytes;
	ob += vha->qla_stats.output_bytes;
	/* --- */

	p->link_failure_count = le32_to_cpu(stats->link_fail_cnt);
	p->loss_of_sync_count = le32_to_cpu(stats->loss_sync_cnt);
	p->loss_of_signal_count = le32_to_cpu(stats->loss_sig_cnt);
	p->prim_seq_protocol_err_count = le32_to_cpu(stats->prim_seq_err_cnt);
	p->invalid_tx_word_count = le32_to_cpu(stats->inval_xmit_word_cnt);
	p->invalid_crc_count = le32_to_cpu(stats->inval_crc_cnt);
	if (IS_FWI2_CAPABLE(ha)) {
		p->lip_count = le32_to_cpu(stats->lip_cnt);
		p->tx_frames = le32_to_cpu(stats->tx_frames);
		p->rx_frames = le32_to_cpu(stats->rx_frames);
		p->dumped_frames = le32_to_cpu(stats->discarded_frames);
		p->nos_count = le32_to_cpu(stats->nos_rcvd);
		p->error_frames =
		    le32_to_cpu(stats->dropped_frames) +
		    le32_to_cpu(stats->discarded_frames);
		if (IS_QLA83XX(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			p->rx_words = le64_to_cpu(stats->fpm_recv_word_cnt);
			p->tx_words = le64_to_cpu(stats->fpm_xmit_word_cnt);
		} else {
			p->rx_words = ib >> 2;
			p->tx_words = ob >> 2;
		}
	}

	p->fcp_control_requests = vha->qla_stats.control_requests;
	p->fcp_input_requests = ir;
	p->fcp_output_requests = or;
	p->fcp_input_megabytes  = ib >> 20;
	p->fcp_output_megabytes = ob >> 20;
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
	int i;
	struct qla_qpair *qpair;

	memset(&vha->qla_stats, 0, sizeof(vha->qla_stats));
	memset(&vha->fc_host_stat, 0, sizeof(vha->fc_host_stat));
	for (i = 0; i < vha->hw->max_qpairs; i++) {
		qpair = vha->hw->queue_pair_map[i];
		if (!qpair)
			continue;
		memset(&qpair->counters, 0, sizeof(qpair->counters));
	}
	memset(&ha->base_qpair->counters, 0, sizeof(qpair->counters));

	vha->qla_stats.jiffies_at_last_reset = get_jiffies_64();

	if (IS_FWI2_CAPABLE(ha)) {
		int rval;

		stats = dma_alloc_coherent(&ha->pdev->dev,
		    sizeof(*stats), &stats_dma, GFP_KERNEL);
		if (!stats) {
			ql_log(ql_log_warn, vha, 0x70d7,
			    "Failed to allocate memory for stats.\n");
			return;
		}

		/* reset firmware statistics */
		rval = qla24xx_get_isp_stats(base_vha, stats, stats_dma, BIT_0);
		if (rval != QLA_SUCCESS)
			ql_log(ql_log_warn, vha, 0x70de,
			       "Resetting ISP statistics failed: rval = %d\n",
			       rval);

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
	static const uint8_t node_name[WWN_SIZE] = {
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
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
	vha->vp_err_state = VP_ERR_PORTDWN;
	vha->vp_prev_err_state = VP_ERR_UNKWN;
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
		qpair = qla2xxx_create_qpair(vha, qos, vha->vp_idx, true);
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

	set_bit(VPORT_DELETE, &vha->dpc_flags);

	while (test_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags))
		msleep(1000);


	qla24xx_disable_vp(vha);
	qla2x00_wait_for_sess_deletion(vha);

	qla_nvme_delete(vha);
	qla_enode_stop(vha);
	qla_edb_stop(vha);

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

	vha->gnl.l = NULL;

	vfree(vha->scan.l);

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

static uint
qla2x00_get_host_supported_speeds(scsi_qla_host_t *vha, uint speeds)
{
	uint supported_speeds = FC_PORTSPEED_UNKNOWN;

	if (speeds & FDMI_PORT_SPEED_64GB)
		supported_speeds |= FC_PORTSPEED_64GBIT;
	if (speeds & FDMI_PORT_SPEED_32GB)
		supported_speeds |= FC_PORTSPEED_32GBIT;
	if (speeds & FDMI_PORT_SPEED_16GB)
		supported_speeds |= FC_PORTSPEED_16GBIT;
	if (speeds & FDMI_PORT_SPEED_8GB)
		supported_speeds |= FC_PORTSPEED_8GBIT;
	if (speeds & FDMI_PORT_SPEED_4GB)
		supported_speeds |= FC_PORTSPEED_4GBIT;
	if (speeds & FDMI_PORT_SPEED_2GB)
		supported_speeds |= FC_PORTSPEED_2GBIT;
	if (speeds & FDMI_PORT_SPEED_1GB)
		supported_speeds |= FC_PORTSPEED_1GBIT;

	return supported_speeds;
}

void
qla2x00_init_host_attr(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	u32 speeds = 0, fdmi_speed = 0;

	fc_host_dev_loss_tmo(vha->host) = ha->port_down_retry_count;
	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) = ha->base_qpair->enable_class_2 ?
			(FC_COS_CLASS2|FC_COS_CLASS3) : FC_COS_CLASS3;
	fc_host_max_npiv_vports(vha->host) = ha->max_npiv_vports;
	fc_host_npiv_vports_inuse(vha->host) = ha->cur_vport_count;

	fdmi_speed = qla25xx_fdmi_port_speed_capability(ha);
	speeds = qla2x00_get_host_supported_speeds(vha, fdmi_speed);

	fc_host_supported_speeds(vha->host) = speeds;
}
