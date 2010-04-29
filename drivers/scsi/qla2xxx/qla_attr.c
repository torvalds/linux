/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/delay.h>

static int qla24xx_vport_disable(struct fc_vport *, bool);
static int qla84xx_reset(scsi_qla_host_t *, struct msg_echo_lb *, struct fc_bsg_job *);
int qla84xx_reset_chip(scsi_qla_host_t *, uint16_t, uint16_t *);
static int qla84xx_mgmt_cmd(scsi_qla_host_t *, struct msg_echo_lb *, struct fc_bsg_job *);
/* SYSFS attributes --------------------------------------------------------- */

static ssize_t
qla2x00_sysfs_read_fw_dump(struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (ha->fw_dump_reading == 0)
		return 0;

	return memory_read_from_buffer(buf, count, &off, ha->fw_dump,
					ha->fw_dump_len);
}

static ssize_t
qla2x00_sysfs_write_fw_dump(struct kobject *kobj,
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

		qla_printk(KERN_INFO, ha,
		    "Firmware dump cleared on (%ld).\n", vha->host_no);

		ha->fw_dump_reading = 0;
		ha->fw_dumped = 0;
		break;
	case 1:
		if (ha->fw_dumped && !ha->fw_dump_reading) {
			ha->fw_dump_reading = 1;

			qla_printk(KERN_INFO, ha,
			    "Raw firmware dump ready for read on (%ld).\n",
			    vha->host_no);
		}
		break;
	case 2:
		qla2x00_alloc_fw_dump(vha);
		break;
	case 3:
		qla2x00_system_error(vha);
		break;
	}
	return (count);
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
qla2x00_sysfs_read_nvram(struct kobject *kobj,
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
qla2x00_sysfs_write_nvram(struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint16_t	cnt;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->nvram_size ||
	    !ha->isp_ops->write_nvram)
		return 0;

	/* Checksum NVRAM. */
	if (IS_FWI2_CAPABLE(ha)) {
		uint32_t *iter;
		uint32_t chksum;

		iter = (uint32_t *)buf;
		chksum = 0;
		for (cnt = 0; cnt < ((count >> 2) - 1); cnt++)
			chksum += le32_to_cpu(*iter++);
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
		qla_printk(KERN_WARNING, ha,
		    "HBA not online, failing NVRAM update.\n");
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, (uint8_t *)buf, ha->nvram_base, count);
	ha->isp_ops->read_nvram(vha, (uint8_t *)ha->nvram, ha->nvram_base,
	    count);

	/* NVRAM settings take effect immediately. */
	set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	qla2xxx_wake_dpc(vha);
	qla2x00_wait_for_chip_reset(vha);

	return (count);
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
qla2x00_sysfs_read_optrom(struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (ha->optrom_state != QLA_SREADING)
		return 0;

	return memory_read_from_buffer(buf, count, &off, ha->optrom_buffer,
					ha->optrom_region_size);
}

static ssize_t
qla2x00_sysfs_write_optrom(struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (ha->optrom_state != QLA_SWRITING)
		return -EINVAL;
	if (off > ha->optrom_region_size)
		return -ERANGE;
	if (off + count > ha->optrom_region_size)
		count = ha->optrom_region_size - off;

	memcpy(&ha->optrom_buffer[off], buf, count);

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
qla2x00_sysfs_write_optrom_ctl(struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	uint32_t start = 0;
	uint32_t size = ha->optrom_size;
	int val, valid;

	if (off)
		return 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return 0;

	if (sscanf(buf, "%d:%x:%x", &val, &start, &size) < 1)
		return -EINVAL;
	if (start > ha->optrom_size)
		return -EINVAL;

	switch (val) {
	case 0:
		if (ha->optrom_state != QLA_SREADING &&
		    ha->optrom_state != QLA_SWRITING)
			break;

		ha->optrom_state = QLA_SWAITING;

		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Freeing flash region allocation -- 0x%x bytes.\n",
		    ha->optrom_region_size));

		vfree(ha->optrom_buffer);
		ha->optrom_buffer = NULL;
		break;
	case 1:
		if (ha->optrom_state != QLA_SWAITING)
			break;

		ha->optrom_region_start = start;
		ha->optrom_region_size = start + size > ha->optrom_size ?
		    ha->optrom_size - start : size;

		ha->optrom_state = QLA_SREADING;
		ha->optrom_buffer = vmalloc(ha->optrom_region_size);
		if (ha->optrom_buffer == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to allocate memory for optrom retrieval "
			    "(%x).\n", ha->optrom_region_size);

			ha->optrom_state = QLA_SWAITING;
			return count;
		}

		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Reading flash region -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size));

		memset(ha->optrom_buffer, 0, ha->optrom_region_size);
		ha->isp_ops->read_optrom(vha, ha->optrom_buffer,
		    ha->optrom_region_start, ha->optrom_region_size);
		break;
	case 2:
		if (ha->optrom_state != QLA_SWAITING)
			break;

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
		else if (IS_QLA25XX(ha) || IS_QLA81XX(ha))
		    valid = 1;
		if (!valid) {
			qla_printk(KERN_WARNING, ha,
			    "Invalid start region 0x%x/0x%x.\n", start, size);
			return -EINVAL;
		}

		ha->optrom_region_start = start;
		ha->optrom_region_size = start + size > ha->optrom_size ?
		    ha->optrom_size - start : size;

		ha->optrom_state = QLA_SWRITING;
		ha->optrom_buffer = vmalloc(ha->optrom_region_size);
		if (ha->optrom_buffer == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to allocate memory for optrom update "
			    "(%x).\n", ha->optrom_region_size);

			ha->optrom_state = QLA_SWAITING;
			return count;
		}

		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Staging flash region write -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size));

		memset(ha->optrom_buffer, 0, ha->optrom_region_size);
		break;
	case 3:
		if (ha->optrom_state != QLA_SWRITING)
			break;

		if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
			qla_printk(KERN_WARNING, ha,
			    "HBA not online, failing flash update.\n");
			return -EAGAIN;
		}

		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Writing flash region -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size));

		ha->isp_ops->write_optrom(vha, ha->optrom_buffer,
		    ha->optrom_region_start, ha->optrom_region_size);
		break;
	default:
		count = -EINVAL;
	}
	return count;
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
qla2x00_sysfs_read_vpd(struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return 0;

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (IS_NOCACHE_VPD_TYPE(ha))
		ha->isp_ops->read_optrom(vha, ha->vpd, ha->flt_region_vpd << 2,
		    ha->vpd_size);
	return memory_read_from_buffer(buf, count, &off, ha->vpd, ha->vpd_size);
}

static ssize_t
qla2x00_sysfs_write_vpd(struct kobject *kobj,
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
		qla_printk(KERN_WARNING, ha,
		    "HBA not online, failing VPD update.\n");
		return -EAGAIN;
	}

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(vha, (uint8_t *)buf, ha->vpd_base, count);
	ha->isp_ops->read_nvram(vha, (uint8_t *)ha->vpd, ha->vpd_base, count);

	/* Update flash version information for 4Gb & above. */
	if (!IS_FWI2_CAPABLE(ha))
		goto done;

	tmp_data = vmalloc(256);
	if (!tmp_data) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocate memory for VPD information update.\n");
		goto done;
	}
	ha->isp_ops->get_flash_version(vha, tmp_data);
	vfree(tmp_data);
done:
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
qla2x00_sysfs_read_sfp(struct kobject *kobj,
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
		qla_printk(KERN_WARNING, ha,
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

		rval = qla2x00_read_sfp(vha, ha->sfp_data_dma, addr, offset,
		    SFP_BLOCK_SIZE);
		if (rval != QLA_SUCCESS) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to read SFP data (%x/%x/%x).\n", rval,
			    addr, offset);
			count = 0;
			break;
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
qla2x00_sysfs_write_reset(struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int type;

	if (off != 0)
		return 0;

	type = simple_strtol(buf, NULL, 10);
	switch (type) {
	case 0x2025c:
		qla_printk(KERN_INFO, ha,
		    "Issuing ISP reset on (%ld).\n", vha->host_no);

		scsi_block_requests(vha->host);
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
		qla2x00_wait_for_chip_reset(vha);
		scsi_unblock_requests(vha->host);
		break;
	case 0x2025d:
		if (!IS_QLA81XX(ha))
			break;

		qla_printk(KERN_INFO, ha,
		    "Issuing MPI reset on (%ld).\n", vha->host_no);

		/* Make sure FC side is not in reset */
		qla2x00_wait_for_hba_online(vha);

		/* Issue MPI reset */
		scsi_block_requests(vha->host);
		if (qla81xx_restart_mpi_firmware(vha) != QLA_SUCCESS)
			qla_printk(KERN_WARNING, ha,
			    "MPI reset failed on (%ld).\n", vha->host_no);
		scsi_unblock_requests(vha->host);
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
qla2x00_sysfs_write_edc(struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint16_t dev, adr, opt, len;
	int rval;

	ha->edc_data_len = 0;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count < 8)
		return 0;

	if (!ha->edc_data) {
		ha->edc_data = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->edc_data_dma);
		if (!ha->edc_data) {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Unable to allocate memory for EDC write.\n"));
			return 0;
		}
	}

	dev = le16_to_cpup((void *)&buf[0]);
	adr = le16_to_cpup((void *)&buf[2]);
	opt = le16_to_cpup((void *)&buf[4]);
	len = le16_to_cpup((void *)&buf[6]);

	if (!(opt & BIT_0))
		if (len == 0 || len > DMA_POOL_SIZE || len > count - 8)
			return -EINVAL;

	memcpy(ha->edc_data, &buf[8], len);

	rval = qla2x00_write_edc(vha, dev, adr, ha->edc_data_dma,
	    ha->edc_data, len, opt);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Unable to write EDC (%x) %02x:%02x:%04x:%02x:%02x.\n",
		    rval, dev, adr, opt, len, *buf));
		return 0;
	}

	return count;
}

static struct bin_attribute sysfs_edc_attr = {
	.attr = {
		.name = "edc",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = qla2x00_sysfs_write_edc,
};

static ssize_t
qla2x00_sysfs_write_edc_status(struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	uint16_t dev, adr, opt, len;
	int rval;

	ha->edc_data_len = 0;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count < 8)
		return 0;

	if (!ha->edc_data) {
		ha->edc_data = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->edc_data_dma);
		if (!ha->edc_data) {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Unable to allocate memory for EDC status.\n"));
			return 0;
		}
	}

	dev = le16_to_cpup((void *)&buf[0]);
	adr = le16_to_cpup((void *)&buf[2]);
	opt = le16_to_cpup((void *)&buf[4]);
	len = le16_to_cpup((void *)&buf[6]);

	if (!(opt & BIT_0))
		if (len == 0 || len > DMA_POOL_SIZE)
			return -EINVAL;

	memset(ha->edc_data, 0, len);
	rval = qla2x00_read_edc(vha, dev, adr, ha->edc_data_dma,
	    ha->edc_data, len, opt);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Unable to write EDC status (%x) %02x:%02x:%04x:%02x.\n",
		    rval, dev, adr, opt, len));
		return 0;
	}

	ha->edc_data_len = len;

	return count;
}

static ssize_t
qla2x00_sysfs_read_edc_status(struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count == 0)
		return 0;

	if (!ha->edc_data || ha->edc_data_len == 0 || ha->edc_data_len > count)
		return -EINVAL;

	memcpy(buf, ha->edc_data, ha->edc_data_len);

	return ha->edc_data_len;
}

static struct bin_attribute sysfs_edc_status_attr = {
	.attr = {
		.name = "edc_status",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.write = qla2x00_sysfs_write_edc_status,
	.read = qla2x00_sysfs_read_edc_status,
};

static ssize_t
qla2x00_sysfs_read_xgmac_stats(struct kobject *kobj,
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
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocate memory for XGMAC read-data.\n");
		return 0;
	}

do_read:
	actual_size = 0;
	memset(ha->xgmac_data, 0, XGMAC_DATA_SIZE);

	rval = qla2x00_get_xgmac_stats(vha, ha->xgmac_data_dma,
	    XGMAC_DATA_SIZE, &actual_size);
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
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
qla2x00_sysfs_read_dcbx_tlv(struct kobject *kobj,
		       struct bin_attribute *bin_attr,
		       char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *vha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	struct qla_hw_data *ha = vha->hw;
	int rval;
	uint16_t actual_size;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count > DCBX_TLV_DATA_SIZE)
		return 0;

	if (ha->dcbx_tlv)
		goto do_read;

	ha->dcbx_tlv = dma_alloc_coherent(&ha->pdev->dev, DCBX_TLV_DATA_SIZE,
	    &ha->dcbx_tlv_dma, GFP_KERNEL);
	if (!ha->dcbx_tlv) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocate memory for DCBX TLV read-data.\n");
		return 0;
	}

do_read:
	actual_size = 0;
	memset(ha->dcbx_tlv, 0, DCBX_TLV_DATA_SIZE);

	rval = qla2x00_get_dcbx_params(vha, ha->dcbx_tlv_dma,
	    DCBX_TLV_DATA_SIZE);
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to read DCBX TLV data (%x).\n", rval);
		count = 0;
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
	{ "edc", &sysfs_edc_attr, 2 },
	{ "edc_status", &sysfs_edc_status_attr, 2 },
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
		if (iter->is4GBp_only == 3 && !IS_QLA81XX(vha->hw))
			continue;

		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
		if (ret)
			qla_printk(KERN_INFO, vha->hw,
			    "Unable to create sysfs %s binary attribute "
			    "(%d).\n", iter->name, ret);
	}
}

void
qla2x00_free_sysfs_attr(scsi_qla_host_t *vha)
{
	struct Scsi_Host *host = vha->host;
	struct sysfs_entry *iter;
	struct qla_hw_data *ha = vha->hw;

	for (iter = bin_file_entries; iter->name; iter++) {
		if (iter->is4GBp_only && !IS_FWI2_CAPABLE(ha))
			continue;
		if (iter->is4GBp_only == 2 && !IS_QLA25XX(ha))
			continue;
		if (iter->is4GBp_only == 3 && !IS_QLA81XX(ha))
			continue;

		sysfs_remove_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
	}

	if (ha->beacon_blink_led == 1)
		ha->isp_ops->beacon_off(vha);
}

/* Scsi_Host attributes. */

static ssize_t
qla2x00_drvr_version_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", qla2x00_version_str);
}

static ssize_t
qla2x00_fw_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	char fw_str[128];

	return snprintf(buf, PAGE_SIZE, "%s\n",
	    ha->isp_ops->fw_version_str(vha, fw_str));
}

static ssize_t
qla2x00_serial_num_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	uint32_t sn;

	if (IS_FWI2_CAPABLE(ha)) {
		qla2xxx_get_vpd_field(vha, "SN", buf, PAGE_SIZE);
		return snprintf(buf, PAGE_SIZE, "%s\n", buf);
	}

	sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | ha->serial1;
	return snprintf(buf, PAGE_SIZE, "%c%05d\n", 'A' + sn / 100000,
	    sn % 100000);
}

static ssize_t
qla2x00_isp_name_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "ISP%04X\n", vha->hw->pdev->device);
}

static ssize_t
qla2x00_isp_id_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return snprintf(buf, PAGE_SIZE, "%04x %04x %04x %04x\n",
	    ha->product_id[0], ha->product_id[1], ha->product_id[2],
	    ha->product_id[3]);
}

static ssize_t
qla2x00_model_name_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%s\n", vha->hw->model_number);
}

static ssize_t
qla2x00_model_desc_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%s\n",
	    vha->hw->model_desc ? vha->hw->model_desc : "");
}

static ssize_t
qla2x00_pci_info_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	char pci_info[30];

	return snprintf(buf, PAGE_SIZE, "%s\n",
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
	    atomic_read(&vha->loop_state) == LOOP_DEAD)
		len = snprintf(buf, PAGE_SIZE, "Link Down\n");
	else if (atomic_read(&vha->loop_state) != LOOP_READY ||
	    test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
		len = snprintf(buf, PAGE_SIZE, "Unknown Link State\n");
	else {
		len = snprintf(buf, PAGE_SIZE, "Link Up - ");

		switch (ha->current_topology) {
		case ISP_CFG_NL:
			len += snprintf(buf + len, PAGE_SIZE-len, "Loop\n");
			break;
		case ISP_CFG_FL:
			len += snprintf(buf + len, PAGE_SIZE-len, "FL_Port\n");
			break;
		case ISP_CFG_N:
			len += snprintf(buf + len, PAGE_SIZE-len,
			    "N_Port to N_Port\n");
			break;
		case ISP_CFG_F:
			len += snprintf(buf + len, PAGE_SIZE-len, "F_Port\n");
			break;
		default:
			len += snprintf(buf + len, PAGE_SIZE-len, "Loop\n");
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
		len += snprintf(buf + len, PAGE_SIZE-len, "Mode 6\n");
		break;
	case QLA_ZIO_DISABLED:
		len += snprintf(buf + len, PAGE_SIZE-len, "Disabled\n");
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

	return snprintf(buf, PAGE_SIZE, "%d us\n", vha->hw->zio_timer * 100);
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
		len += snprintf(buf + len, PAGE_SIZE-len, "Enabled\n");
	else
		len += snprintf(buf + len, PAGE_SIZE-len, "Disabled\n");
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
		qla_printk(KERN_WARNING, ha,
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
	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->bios_revision[1],
	    ha->bios_revision[0]);
}

static ssize_t
qla2x00_optrom_efi_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->efi_revision[1],
	    ha->efi_revision[0]);
}

static ssize_t
qla2x00_optrom_fcode_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->fcode_revision[1],
	    ha->fcode_revision[0]);
}

static ssize_t
qla2x00_optrom_fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d %d\n",
	    ha->fw_revision[0], ha->fw_revision[1], ha->fw_revision[2],
	    ha->fw_revision[3]);
}

static ssize_t
qla2x00_total_isp_aborts_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;
	return snprintf(buf, PAGE_SIZE, "%d\n",
	    ha->qla_stats.total_isp_aborts);
}

static ssize_t
qla24xx_84xx_fw_version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rval = QLA_SUCCESS;
	uint16_t status[2] = {0, 0};
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLA84XX(ha) && ha->cs84xx) {
		if (ha->cs84xx->op_fw_version == 0) {
			rval = qla84xx_verify_chip(vha, status);
	}

	if ((rval == QLA_SUCCESS) && (status[0] == 0))
		return snprintf(buf, PAGE_SIZE, "%u\n",
			(uint32_t)ha->cs84xx->op_fw_version);
	}

	return snprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t
qla2x00_mpi_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha))
		return snprintf(buf, PAGE_SIZE, "\n");

	return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d (%x)\n",
	    ha->mpi_version[0], ha->mpi_version[1], ha->mpi_version[2],
	    ha->mpi_capabilities);
}

static ssize_t
qla2x00_phy_version_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha))
		return snprintf(buf, PAGE_SIZE, "\n");

	return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d\n",
	    ha->phy_version[0], ha->phy_version[1], ha->phy_version[2]);
}

static ssize_t
qla2x00_flash_block_size_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	struct qla_hw_data *ha = vha->hw;

	return snprintf(buf, PAGE_SIZE, "0x%x\n", ha->fdt_block_size);
}

static ssize_t
qla2x00_vlan_id_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_QLA81XX(vha->hw))
		return snprintf(buf, PAGE_SIZE, "\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", vha->fcoe_vlan_id);
}

static ssize_t
qla2x00_vn_port_mac_address_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	if (!IS_QLA81XX(vha->hw))
		return snprintf(buf, PAGE_SIZE, "\n");

	return snprintf(buf, PAGE_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x\n",
	    vha->fcoe_vn_port_mac[5], vha->fcoe_vn_port_mac[4],
	    vha->fcoe_vn_port_mac[3], vha->fcoe_vn_port_mac[2],
	    vha->fcoe_vn_port_mac[1], vha->fcoe_vn_port_mac[0]);
}

static ssize_t
qla2x00_fabric_param_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d\n", vha->hw->switch_cap);
}

static ssize_t
qla2x00_fw_state_show(struct device *dev, struct device_attribute *attr,
    char *buf)
{
	scsi_qla_host_t *vha = shost_priv(class_to_shost(dev));
	int rval = QLA_FUNCTION_FAILED;
	uint16_t state[5];

	if (test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
		DEBUG2_3_11(printk("%s(%ld): isp reset in progress.\n",
			__func__, vha->host_no));
	else if (!vha->hw->flags.eeh_busy)
		rval = qla2x00_get_firmware_state(vha, state);
	if (rval != QLA_SUCCESS)
		memset(state, -1, sizeof(state));

	return snprintf(buf, PAGE_SIZE, "0x%x 0x%x 0x%x 0x%x 0x%x\n", state[0],
	    state[1], state[2], state[3], state[4]);
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

	if (!fcport)
		return;

	if (test_bit(ABORT_ISP_ACTIVE, &fcport->vha->dpc_flags))
		return;

	if (unlikely(pci_channel_offline(fcport->vha->hw->pdev))) {
		qla2x00_abort_all_cmds(fcport->vha, DID_NO_CONNECT << 16);
		return;
	}

	/*
	 * Transport has effectively 'deleted' the rport, clear
	 * all local references.
	 */
	spin_lock_irq(host->host_lock);
	fcport->rport = NULL;
	*((fc_port_t **)rport->dd_data) = NULL;
	spin_unlock_irq(host->host_lock);
}

static void
qla2x00_terminate_rport_io(struct fc_rport *rport)
{
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;

	if (!fcport)
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
	if (fcport->loop_id != FC_NO_LOOP_ID &&
	    !test_bit(UNLOADING, &fcport->vha->dpc_flags))
		fcport->vha->hw->isp_ops->fabric_logout(fcport->vha,
			fcport->loop_id, fcport->d_id.b.domain,
			fcport->d_id.b.area, fcport->d_id.b.al_pa);
}

static int
qla2x00_issue_lip(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

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
	struct fc_host_statistics *pfc_host_stat;

	pfc_host_stat = &ha->fc_host_stat;
	memset(pfc_host_stat, -1, sizeof(struct fc_host_statistics));

	if (test_bit(UNLOADING, &vha->dpc_flags))
		goto done;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto done;

	stats = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &stats_dma);
	if (stats == NULL) {
		DEBUG2_3_11(printk("%s(%ld): Failed to allocate memory.\n",
		    __func__, base_vha->host_no));
		goto done;
	}
	memset(stats, 0, DMA_POOL_SIZE);

	rval = QLA_FUNCTION_FAILED;
	if (IS_FWI2_CAPABLE(ha)) {
		rval = qla24xx_get_isp_stats(base_vha, stats, stats_dma);
	} else if (atomic_read(&base_vha->loop_state) == LOOP_READY &&
		    !test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags) &&
		    !test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags) &&
		    !ha->dpc_active) {
		/* Must be in a 'READY' state for statistics retrieval. */
		rval = qla2x00_get_link_status(base_vha, base_vha->loop_id,
						stats, stats_dma);
	}

	if (rval != QLA_SUCCESS)
		goto done_free;

	pfc_host_stat->link_failure_count = stats->link_fail_cnt;
	pfc_host_stat->loss_of_sync_count = stats->loss_sync_cnt;
	pfc_host_stat->loss_of_signal_count = stats->loss_sig_cnt;
	pfc_host_stat->prim_seq_protocol_err_count = stats->prim_seq_err_cnt;
	pfc_host_stat->invalid_tx_word_count = stats->inval_xmit_word_cnt;
	pfc_host_stat->invalid_crc_count = stats->inval_crc_cnt;
	if (IS_FWI2_CAPABLE(ha)) {
		pfc_host_stat->lip_count = stats->lip_cnt;
		pfc_host_stat->tx_frames = stats->tx_frames;
		pfc_host_stat->rx_frames = stats->rx_frames;
		pfc_host_stat->dumped_frames = stats->dumped_frames;
		pfc_host_stat->nos_count = stats->nos_rcvd;
	}
	pfc_host_stat->fcp_input_megabytes = ha->qla_stats.input_bytes >> 20;
	pfc_host_stat->fcp_output_megabytes = ha->qla_stats.output_bytes >> 20;

done_free:
        dma_pool_free(ha->s_dma_pool, stats, stats_dma);
done:
	return pfc_host_stat;
}

static void
qla2x00_get_host_symbolic_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	qla2x00_get_sym_node_name(vha, fc_host_symbolic_name(shost));
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
	u64 node_name;

	if (vha->device_flags & SWITCH_FOUND)
		node_name = wwn_to_u64(vha->fabric_node_name);
	else
		node_name = wwn_to_u64(vha->node_name);

	fc_host_fabric_name(shost) = node_name;
}

static void
qla2x00_get_host_port_state(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);
	struct scsi_qla_host *base_vha = pci_get_drvdata(vha->hw->pdev);

	if (!base_vha->flags.online)
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	else if (atomic_read(&base_vha->loop_state) == LOOP_TIMEOUT)
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
}

static int
qla24xx_vport_create(struct fc_vport *fc_vport, bool disable)
{
	int	ret = 0;
	uint8_t	qos = 0;
	scsi_qla_host_t *base_vha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha = NULL;
	struct qla_hw_data *ha = base_vha->hw;
	uint16_t options = 0;
	int	cnt;
	struct req_que *req = ha->req_q_map[0];

	ret = qla24xx_vport_create_req_sanity_check(fc_vport);
	if (ret) {
		DEBUG15(printk("qla24xx_vport_create_req_sanity_check failed, "
		    "status %x\n", ret));
		return (ret);
	}

	vha = qla24xx_create_vhost(fc_vport);
	if (vha == NULL) {
		DEBUG15(printk ("qla24xx_create_vhost failed, vha = %p\n",
		    vha));
		return FC_VPORT_FAILED;
	}
	if (disable) {
		atomic_set(&vha->vp_state, VP_OFFLINE);
		fc_vport_set_state(fc_vport, FC_VPORT_DISABLED);
	} else
		atomic_set(&vha->vp_state, VP_FAILED);

	/* ready to create vport */
	qla_printk(KERN_INFO, vha->hw, "VP entry id %d assigned.\n",
							vha->vp_idx);

	/* initialized vport states */
	atomic_set(&vha->loop_state, LOOP_DOWN);
	vha->vp_err_state=  VP_ERR_PORTDWN;
	vha->vp_prev_err_state=  VP_ERR_UNKWN;
	/* Check if physical ha port is Up */
	if (atomic_read(&base_vha->loop_state) == LOOP_DOWN ||
	    atomic_read(&base_vha->loop_state) == LOOP_DEAD) {
		/* Don't retry or attempt login of this virtual port */
		DEBUG15(printk ("scsi(%ld): pport loop_state is not UP.\n",
		    base_vha->host_no));
		atomic_set(&vha->loop_state, LOOP_DEAD);
		if (!disable)
			fc_vport_set_state(fc_vport, FC_VPORT_LINKDOWN);
	}

	if (scsi_add_host_with_dma(vha->host, &fc_vport->dev,
				   &ha->pdev->dev)) {
		DEBUG15(printk("scsi(%ld): scsi_add_host failure for VP[%d].\n",
			vha->host_no, vha->vp_idx));
		goto vport_create_failed_2;
	}

	/* initialize attributes */
	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) =
		fc_host_supported_classes(base_vha->host);
	fc_host_supported_speeds(vha->host) =
		fc_host_supported_speeds(base_vha->host);

	qla24xx_vport_disable(fc_vport, disable);

	if (ha->flags.cpu_affinity_enabled) {
		req = ha->req_q_map[1];
		goto vport_queue;
	} else if (ql2xmaxqueues == 1 || !ha->npiv_info)
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
		ret = qla25xx_create_req_que(ha, options, vha->vp_idx, 0, 0,
			qos);
		if (!ret)
			qla_printk(KERN_WARNING, ha,
			"Can't create request queue for vp_idx:%d\n",
			vha->vp_idx);
		else {
			DEBUG2(qla_printk(KERN_INFO, ha,
			"Request Que:%d (QoS: %d) created for vp_idx:%d\n",
			ret, qos, vha->vp_idx));
			req = ha->req_q_map[ret];
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
	fc_port_t *fcport, *tfcport;
	struct qla_hw_data *ha = vha->hw;
	uint16_t id = vha->vp_idx;

	while (test_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags) ||
	    test_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags))
		msleep(1000);

	qla24xx_disable_vp(vha);

	fc_remove_host(vha->host);

	scsi_remove_host(vha->host);

	list_for_each_entry_safe(fcport, tfcport, &vha->vp_fcports, list) {
		list_del(&fcport->list);
		kfree(fcport);
		fcport = NULL;
	}

	qla24xx_deallocate_vp_id(vha);

	mutex_lock(&ha->vport_lock);
	ha->cur_vport_count--;
	clear_bit(vha->vp_idx, ha->vp_idx_map);
	mutex_unlock(&ha->vport_lock);

	if (vha->timer_active) {
		qla2x00_vp_stop_timer(vha);
		DEBUG15(printk ("scsi(%ld): timer for the vport[%d] = %p "
		    "has stopped\n",
		    vha->host_no, vha->vp_idx, vha));
        }

	if (vha->req->id && !ha->flags.cpu_affinity_enabled) {
		if (qla25xx_delete_req_que(vha, vha->req) != QLA_SUCCESS)
			qla_printk(KERN_WARNING, ha,
				"Queue delete failed.\n");
	}

	scsi_host_put(vha->host);
	qla_printk(KERN_INFO, ha, "vport %d deleted\n", id);
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

/* BSG support for ELS/CT pass through */
inline srb_t *
qla2x00_get_ctx_bsg_sp(scsi_qla_host_t *vha, fc_port_t *fcport, size_t size)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;
	struct srb_bsg_ctx *ctx;

	sp = mempool_alloc(ha->srb_mempool, GFP_KERNEL);
	if (!sp)
		goto done;
	ctx = kzalloc(size, GFP_KERNEL);
	if (!ctx) {
		mempool_free(sp, ha->srb_mempool);
		goto done;
	}

	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->ctx = ctx;
done:
	return sp;
}

static int
qla2x00_process_els(struct fc_bsg_job *bsg_job)
{
	struct fc_rport *rport;
	fc_port_t *fcport;
	struct Scsi_Host *host;
	scsi_qla_host_t *vha;
	struct qla_hw_data *ha;
	srb_t *sp;
	const char *type;
	int req_sg_cnt, rsp_sg_cnt;
	int rval =  (DRIVER_ERROR << 16);
	uint16_t nextlid = 0;
	struct srb_bsg *els;

	/*  Multiple SG's are not supported for ELS requests */
        if (bsg_job->request_payload.sg_cnt > 1 ||
		bsg_job->reply_payload.sg_cnt > 1) {
		DEBUG2(printk(KERN_INFO
		    "multiple SG's are not supported for ELS requests"
		    " [request_sg_cnt: %x reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt,
		    bsg_job->reply_payload.sg_cnt));
		rval = -EPERM;
		goto done;
        }

	/* ELS request for rport */
	if (bsg_job->request->msgcode == FC_BSG_RPT_ELS) {
		rport = bsg_job->rport;
		fcport = *(fc_port_t **) rport->dd_data;
		host = rport_to_shost(rport);
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_RPT_ELS";

		/* make sure the rport is logged in,
		 * if not perform fabric login
		 */
		if (qla2x00_fabric_login(vha, fcport, &nextlid)) {
			DEBUG2(qla_printk(KERN_WARNING, ha,
			    "failed to login port %06X for ELS passthru\n",
			    fcport->d_id.b24));
			rval = -EIO;
			goto done;
		}
	} else {
		host = bsg_job->shost;
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_HST_ELS_NOLOGIN";

		/* Allocate a dummy fcport structure, since functions
		 * preparing the IOCB and mailbox command retrieves port
		 * specific information from fcport structure. For Host based
		 * ELS commands there will be no fcport structure allocated
		 */
		fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
		if (!fcport) {
			rval = -ENOMEM;
			goto done;
		}

		/* Initialize all required  fields of fcport */
		fcport->vha = vha;
		fcport->vp_idx = vha->vp_idx;
		fcport->d_id.b.al_pa =
		    bsg_job->request->rqst_data.h_els.port_id[0];
		fcport->d_id.b.area =
		    bsg_job->request->rqst_data.h_els.port_id[1];
		fcport->d_id.b.domain =
		    bsg_job->request->rqst_data.h_els.port_id[2];
		fcport->loop_id =
		    (fcport->d_id.b.al_pa == 0xFD) ?
		    NPH_FABRIC_CONTROLLER : NPH_F_PORT;
	}

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "host not online\n"));
		rval = -EIO;
		goto done;
	}

        req_sg_cnt =
	    dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
        if (!req_sg_cnt) {
		rval = -ENOMEM;
		goto done_free_fcport;
	}
        rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
        if (!rsp_sg_cnt) {
		rval = -ENOMEM;
                goto done_free_fcport;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
	    (rsp_sg_cnt != bsg_job->reply_payload.sg_cnt))
	{
		DEBUG2(printk(KERN_INFO
		    "dma mapping resulted in different sg counts \
		    [request_sg_cnt: %x dma_request_sg_cnt: %x\
		    reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt, req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, rsp_sg_cnt));
		rval = -EAGAIN;
                goto done_unmap_sg;
	}

	/* Alloc SRB structure */
	sp = qla2x00_get_ctx_bsg_sp(vha, fcport, sizeof(struct srb_bsg));
	if (!sp) {
		rval = -ENOMEM;
                goto done_unmap_sg;
	}

	els = sp->ctx;
	els->ctx.type =
	    (bsg_job->request->msgcode == FC_BSG_RPT_ELS ?
	    SRB_ELS_CMD_RPT : SRB_ELS_CMD_HST);
	els->bsg_job = bsg_job;

	DEBUG2(qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%x): bsg rqst type: %s els type: %x - loop-id=%x "
	    "portid=%02x%02x%02x.\n", vha->host_no, sp->handle, type,
	    bsg_job->request->rqst_data.h_els.command_code,
	    fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
	    fcport->d_id.b.al_pa));

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		kfree(sp->ctx);
		mempool_free(sp, ha->srb_mempool);
		rval = -EIO;
		goto done_unmap_sg;
	}
	return rval;

done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	goto done_free_fcport;

done_free_fcport:
	if (bsg_job->request->msgcode == FC_BSG_HST_ELS_NOLOGIN)
		kfree(fcport);
done:
	return rval;
}

static int
qla2x00_process_ct(struct fc_bsg_job *bsg_job)
{
	srb_t *sp;
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = (DRIVER_ERROR << 16);
	int req_sg_cnt, rsp_sg_cnt;
	uint16_t loop_id;
	struct fc_port *fcport;
	char  *type = "FC_BSG_HST_CT";
	struct srb_bsg *ct;

	/* pass through is supported only for ISP 4Gb or higher */
        if (!IS_FWI2_CAPABLE(ha)) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "scsi(%ld):Firmware is not capable to support FC "
		    "CT pass thru\n", vha->host_no));
		rval = -EPERM;
                goto done;
	}

        req_sg_cnt =
	    dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
        if (!req_sg_cnt) {
		rval = -ENOMEM;
		goto done;
	}

        rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
            bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
        if (!rsp_sg_cnt) {
		rval = -ENOMEM;
                goto done;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(rsp_sg_cnt != bsg_job->reply_payload.sg_cnt))
	{
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "dma mapping resulted in different sg counts \
		    [request_sg_cnt: %x dma_request_sg_cnt: %x\
		    reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt, req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, rsp_sg_cnt));
		rval = -EAGAIN;
                goto done_unmap_sg;
	}

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "host not online\n"));
		rval = -EIO;
                goto done_unmap_sg;
	}

	loop_id =
	    (bsg_job->request->rqst_data.h_ct.preamble_word1 & 0xFF000000)
	    >> 24;
	switch (loop_id) {
		case 0xFC:
			loop_id = cpu_to_le16(NPH_SNS);
			break;
		case 0xFA:
			loop_id = vha->mgmt_svr_loop_id;
			break;
		default:
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Unknown loop id: %x\n", loop_id));
			rval = -EINVAL;
			goto done_unmap_sg;
	}

	/* Allocate a dummy fcport structure, since functions preparing the
	 * IOCB and mailbox command retrieves port specific information
	 * from fcport structure. For Host based ELS commands there will be
	 * no fcport structure allocated
	 */
	fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (!fcport)
	{
		rval = -ENOMEM;
		goto  done_unmap_sg;
	}

	/* Initialize all required  fields of fcport */
	fcport->vha = vha;
	fcport->vp_idx = vha->vp_idx;
	fcport->d_id.b.al_pa = bsg_job->request->rqst_data.h_ct.port_id[0];
	fcport->d_id.b.area = bsg_job->request->rqst_data.h_ct.port_id[1];
	fcport->d_id.b.domain = bsg_job->request->rqst_data.h_ct.port_id[2];
	fcport->loop_id = loop_id;

	/* Alloc SRB structure */
	sp = qla2x00_get_ctx_bsg_sp(vha, fcport, sizeof(struct srb_bsg));
	if (!sp) {
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	ct = sp->ctx;
	ct->ctx.type = SRB_CT_CMD;
	ct->bsg_job = bsg_job;

	DEBUG2(qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%x): bsg rqst type: %s els type: %x - loop-id=%x "
	    "portid=%02x%02x%02x.\n", vha->host_no, sp->handle, type,
	    (bsg_job->request->rqst_data.h_ct.preamble_word2 >> 16),
	    fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
	    fcport->d_id.b.al_pa));

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		kfree(sp->ctx);
		mempool_free(sp, ha->srb_mempool);
		rval = -EIO;
		goto done_free_fcport;
	}
	return rval;

done_free_fcport:
	kfree(fcport);
done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done:
	return rval;
}

static int
qla2x00_process_vendor_specific(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval;
	uint8_t command_sent;
	uint32_t vendor_cmd;
	char *type;
	struct msg_echo_lb elreq;
	uint16_t response[MAILBOX_REGISTER_COUNT];
	uint8_t* fw_sts_ptr;
	uint8_t *req_data;
	dma_addr_t req_data_dma;
	uint32_t req_data_len;
	uint8_t *rsp_data;
	dma_addr_t rsp_data_dma;
	uint32_t rsp_data_len;

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
		rval = -EBUSY;
		goto done;
	}

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "host not online\n"));
		rval = -EIO;
                goto done;
	}

        elreq.req_sg_cnt =
	    dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
        if (!elreq.req_sg_cnt) {
		rval = -ENOMEM;
		goto done;
	}
        elreq.rsp_sg_cnt =
	    dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
        if (!elreq.rsp_sg_cnt) {
		rval = -ENOMEM;
                goto done;
	}

	if ((elreq.req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
	    (elreq.rsp_sg_cnt != bsg_job->reply_payload.sg_cnt))
	{
		DEBUG2(printk(KERN_INFO
		    "dma mapping resulted in different sg counts \
		    [request_sg_cnt: %x dma_request_sg_cnt: %x\
		    reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt, elreq.req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, elreq.rsp_sg_cnt));
		rval = -EAGAIN;
                goto done_unmap_sg;
	}
	req_data_len = rsp_data_len = bsg_job->request_payload.payload_len;
	req_data = dma_alloc_coherent(&ha->pdev->dev, req_data_len,
	    &req_data_dma, GFP_KERNEL);

	rsp_data = dma_alloc_coherent(&ha->pdev->dev, rsp_data_len,
	    &rsp_data_dma, GFP_KERNEL);

	/* Copy the request buffer in req_data now */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, req_data,
	    req_data_len);

	elreq.send_dma = req_data_dma;
	elreq.rcv_dma = rsp_data_dma;
	elreq.transfer_size = req_data_len;

	/* Vendor cmd : loopback or ECHO diagnostic
	 * Options:
	 * 	Loopback : Either internal or external loopback
	 * 	ECHO: ECHO ELS or Vendor specific FC4  link data
	 */
	vendor_cmd = bsg_job->request->rqst_data.h_vendor.vendor_cmd[0];
	elreq.options =
	    *(((uint32_t *)bsg_job->request->rqst_data.h_vendor.vendor_cmd)
	    + 1);

	switch (bsg_job->request->rqst_data.h_vendor.vendor_cmd[0]) {
	case QL_VND_LOOPBACK:
		if (ha->current_topology != ISP_CFG_F) {
			type = "FC_BSG_HST_VENDOR_LOOPBACK";

			DEBUG2(qla_printk(KERN_INFO, ha,
				"scsi(%ld) bsg rqst type: %s vendor rqst type: %x options: %x.\n",
				vha->host_no, type, vendor_cmd, elreq.options));

			command_sent = INT_DEF_LB_LOOPBACK_CMD;
			rval = qla2x00_loopback_test(vha, &elreq, response);
			if (IS_QLA81XX(ha)) {
				if (response[0] == MBS_COMMAND_ERROR && response[1] == MBS_LB_RESET) {
					DEBUG2(printk(KERN_ERR "%s(%ld): ABORTing "
						"ISP\n", __func__, vha->host_no));
					set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
					qla2xxx_wake_dpc(vha);
				 }
			}
		} else {
			type = "FC_BSG_HST_VENDOR_ECHO_DIAG";
			DEBUG2(qla_printk(KERN_INFO, ha,
				"scsi(%ld) bsg rqst type: %s vendor rqst type: %x options: %x.\n",
				vha->host_no, type, vendor_cmd, elreq.options));

			command_sent = INT_DEF_LB_ECHO_CMD;
			rval = qla2x00_echo_test(vha, &elreq, response);
		}
		break;
	case QLA84_RESET:
		if (!IS_QLA84XX(vha->hw)) {
			rval = -EINVAL;
			DEBUG16(printk(
				"%s(%ld): 8xxx exiting.\n",
				__func__, vha->host_no));
			return rval;
		}
		rval = qla84xx_reset(vha, &elreq, bsg_job);
		break;
	case QLA84_MGMT_CMD:
		if (!IS_QLA84XX(vha->hw)) {
			rval = -EINVAL;
			DEBUG16(printk(
				"%s(%ld): 8xxx exiting.\n",
				__func__, vha->host_no));
			return rval;
		}
		rval = qla84xx_mgmt_cmd(vha, &elreq, bsg_job);
		break;
	default:
		rval = -ENOSYS;
	}

	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
			"scsi(%ld) Vendor request %s failed\n", vha->host_no, type));
		rval = 0;
		bsg_job->reply->result = (DID_ERROR << 16);
		bsg_job->reply->reply_payload_rcv_len = 0;
		fw_sts_ptr = ((uint8_t*)bsg_job->req->sense) + sizeof(struct fc_bsg_reply);
		memcpy( fw_sts_ptr, response, sizeof(response));
		fw_sts_ptr += sizeof(response);
                *fw_sts_ptr = command_sent;
	} else {
		DEBUG2(qla_printk(KERN_WARNING, ha,
			"scsi(%ld) Vendor request %s completed\n", vha->host_no, type));
		rval = bsg_job->reply->result = 0;
		bsg_job->reply_len = sizeof(struct fc_bsg_reply) + sizeof(response) + sizeof(uint8_t);
		bsg_job->reply->reply_payload_rcv_len = bsg_job->reply_payload.payload_len;
		fw_sts_ptr = ((uint8_t*)bsg_job->req->sense) + sizeof(struct fc_bsg_reply);
		memcpy(fw_sts_ptr, response, sizeof(response));
		fw_sts_ptr += sizeof(response);
		*fw_sts_ptr = command_sent;
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, rsp_data,
		rsp_data_len);
	}
	bsg_job->job_done(bsg_job);

done_unmap_sg:

	if(req_data)
		dma_free_coherent(&ha->pdev->dev, req_data_len,
			req_data, req_data_dma);
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

done:
        return rval;
}

static int
qla24xx_bsg_request(struct fc_bsg_job *bsg_job)
{
        int ret = -EINVAL;

        switch (bsg_job->request->msgcode) {
		case FC_BSG_RPT_ELS:
		case FC_BSG_HST_ELS_NOLOGIN:
			ret = qla2x00_process_els(bsg_job);
			break;
		case FC_BSG_HST_CT:
			ret = qla2x00_process_ct(bsg_job);
			break;
		case FC_BSG_HST_VENDOR:
			ret = qla2x00_process_vendor_specific(bsg_job);
			break;
		case FC_BSG_HST_ADD_RPORT:
		case FC_BSG_HST_DEL_RPORT:
		case FC_BSG_RPT_CT:
		default:
			DEBUG2(printk("qla2xxx: unsupported BSG request\n"));
			break;
        }
	return ret;
}

static int
qla24xx_bsg_timeout(struct fc_bsg_job *bsg_job)
{
        scsi_qla_host_t *vha = shost_priv(bsg_job->shost);
        struct qla_hw_data *ha = vha->hw;
        srb_t *sp;
        int cnt, que;
        unsigned long flags;
        struct req_que *req;
	struct srb_bsg *sp_bsg;

	/* find the bsg job from the active list of commands */
        spin_lock_irqsave(&ha->hardware_lock, flags);
	for (que = 0; que < ha->max_req_queues; que++) {
		req = ha->req_q_map[que];
		if (!req)
			continue;

		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++ ) {
			sp = req->outstanding_cmds[cnt];

			if (sp) {
				sp_bsg = (struct srb_bsg*)sp->ctx;

				if (((sp_bsg->ctx.type == SRB_CT_CMD) ||
				    (sp_bsg->ctx.type == SRB_ELS_CMD_RPT)
				    || ( sp_bsg->ctx.type == SRB_ELS_CMD_HST)) &&
				    (sp_bsg->bsg_job == bsg_job)) {
					if (ha->isp_ops->abort_command(sp)) {
						DEBUG2(qla_printk(KERN_INFO, ha,
						"scsi(%ld): mbx abort_command failed\n", vha->host_no));
						bsg_job->req->errors = bsg_job->reply->result = -EIO;
					} else {
						DEBUG2(qla_printk(KERN_INFO, ha,
						"scsi(%ld): mbx abort_command success\n", vha->host_no));
						bsg_job->req->errors = bsg_job->reply->result = 0;
					}
					goto done;
				}
			}
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	DEBUG2(qla_printk(KERN_INFO, ha,
		"scsi(%ld) SRB not found to abort\n", vha->host_no));
	bsg_job->req->errors = bsg_job->reply->result = -ENXIO;
	return 0;

done:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (bsg_job->request->msgcode == FC_BSG_HST_CT)
		kfree(sp->fcport);
	kfree(sp->ctx);
	mempool_free(sp, ha->srb_mempool);
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
	.bsg_request = qla24xx_bsg_request,
	.bsg_timeout = qla24xx_bsg_timeout,
};

void
qla2x00_init_host_attr(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	u32 speed = FC_PORTSPEED_UNKNOWN;

	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) = FC_COS_CLASS3;
	fc_host_max_npiv_vports(vha->host) = ha->max_npiv_vports;
	fc_host_npiv_vports_inuse(vha->host) = ha->cur_vport_count;

	if (IS_QLA81XX(ha))
		speed = FC_PORTSPEED_10GBIT;
	else if (IS_QLA25XX(ha))
		speed = FC_PORTSPEED_8GBIT | FC_PORTSPEED_4GBIT |
		    FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else if (IS_QLA24XX_TYPE(ha))
		speed = FC_PORTSPEED_4GBIT | FC_PORTSPEED_2GBIT |
		    FC_PORTSPEED_1GBIT;
	else if (IS_QLA23XX(ha))
		speed = FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else
		speed = FC_PORTSPEED_1GBIT;
	fc_host_supported_speeds(vha->host) = speed;
}
static int
qla84xx_reset(scsi_qla_host_t *ha, struct msg_echo_lb *mreq, struct fc_bsg_job *bsg_job)
{
	int             ret = 0;
	int             cmd;
	uint16_t        cmd_status;

	DEBUG16(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	cmd = (*((bsg_job->request->rqst_data.h_vendor.vendor_cmd) + 2))
			== A84_RESET_FLAG_ENABLE_DIAG_FW ?
				A84_ISSUE_RESET_DIAG_FW : A84_ISSUE_RESET_OP_FW;
	ret = qla84xx_reset_chip(ha, cmd == A84_ISSUE_RESET_DIAG_FW,
	&cmd_status);
	return ret;
}

static int
qla84xx_mgmt_cmd(scsi_qla_host_t *ha, struct msg_echo_lb *mreq, struct fc_bsg_job *bsg_job)
{
	struct access_chip_84xx *mn;
	dma_addr_t mn_dma, mgmt_dma;
	void *mgmt_b = NULL;
	int ret = 0;
	int rsp_hdr_len, len = 0;
	struct qla84_msg_mgmt *ql84_mgmt;

	ql84_mgmt = (struct qla84_msg_mgmt *) vmalloc(sizeof(struct qla84_msg_mgmt));
	ql84_mgmt->cmd =
		*((uint16_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 2));
	ql84_mgmt->mgmtp.u.mem.start_addr =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 3));
	ql84_mgmt->len =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 4));
	ql84_mgmt->mgmtp.u.config.id =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 5));
	ql84_mgmt->mgmtp.u.config.param0 =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 6));
	ql84_mgmt->mgmtp.u.config.param1 =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 7));
	ql84_mgmt->mgmtp.u.info.type =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 8));
	ql84_mgmt->mgmtp.u.info.context =
		*((uint32_t *)(bsg_job->request->rqst_data.h_vendor.vendor_cmd + 9));

	rsp_hdr_len = bsg_job->request_payload.payload_len;

	mn = dma_pool_alloc(ha->hw->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw buffer "
		"failed%lu\n", __func__, ha->host_no));
		return -ENOMEM;
	}

	memset(mn, 0, sizeof (struct access_chip_84xx));

	mn->entry_type = ACCESS_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	switch (ql84_mgmt->cmd) {
	case QLA84_MGMT_READ_MEM:
		mn->options = cpu_to_le16(ACO_DUMP_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_WRITE_MEM:
		mn->options = cpu_to_le16(ACO_LOAD_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_CHNG_CONFIG:
		mn->options = cpu_to_le16(ACO_CHANGE_CONFIG_PARAM);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.id);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param0);
		mn->parameter3 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param1);
		break;
	case QLA84_MGMT_GET_INFO:
		mn->options = cpu_to_le16(ACO_REQUEST_INFO);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.type);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.context);
		break;
	default:
		ret = -EIO;
		goto exit_mgmt0;
	}

	if ((len == ql84_mgmt->len) &&
		ql84_mgmt->cmd != QLA84_MGMT_CHNG_CONFIG) {
		mgmt_b = dma_alloc_coherent(&ha->hw->pdev->dev, len,
				&mgmt_dma, GFP_KERNEL);
		if (mgmt_b == NULL) {
			DEBUG2(printk(KERN_ERR "%s: dma alloc mgmt_b "
			"failed%lu\n", __func__, ha->host_no));
			ret = -ENOMEM;
			goto exit_mgmt0;
		}
		mn->total_byte_cnt = cpu_to_le32(ql84_mgmt->len);
		mn->dseg_count = cpu_to_le16(1);
		mn->dseg_address[0] = cpu_to_le32(LSD(mgmt_dma));
		mn->dseg_address[1] = cpu_to_le32(MSD(mgmt_dma));
		mn->dseg_length = cpu_to_le32(len);

		if (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM) {
			memcpy(mgmt_b, ql84_mgmt->payload, len);
		}
	}

	ret = qla2x00_issue_iocb(ha, mn, mn_dma, 0);
	if ((ret != QLA_SUCCESS) || (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM)
		|| (ql84_mgmt->cmd == QLA84_MGMT_CHNG_CONFIG)) {
			if (ret != QLA_SUCCESS)
				DEBUG2(printk(KERN_ERR "%s(%lu): failed\n",
					__func__, ha->host_no));
	} else if ((ql84_mgmt->cmd == QLA84_MGMT_READ_MEM) ||
			(ql84_mgmt->cmd == QLA84_MGMT_GET_INFO)) {
	}

	if (mgmt_b)
		dma_free_coherent(&ha->hw->pdev->dev, len, mgmt_b, mgmt_dma);

exit_mgmt0:
	dma_pool_free(ha->hw->s_dma_pool, mn, mn_dma);
	return ret;
}
