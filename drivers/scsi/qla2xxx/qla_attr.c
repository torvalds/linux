/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>

static int qla24xx_vport_disable(struct fc_vport *, bool);

/* SYSFS attributes --------------------------------------------------------- */

static ssize_t
qla2x00_sysfs_read_fw_dump(struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	char *rbuf = (char *)ha->fw_dump;

	if (ha->fw_dump_reading == 0)
		return 0;
	if (off > ha->fw_dump_len)
                return 0;
	if (off + count > ha->fw_dump_len)
		count = ha->fw_dump_len - off;

	memcpy(buf, &rbuf[off], count);

	return (count);
}

static ssize_t
qla2x00_sysfs_write_fw_dump(struct kobject *kobj,
			    struct bin_attribute *bin_attr,
			    char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	int reading;

	if (off != 0)
		return (0);

	reading = simple_strtol(buf, NULL, 10);
	switch (reading) {
	case 0:
		if (!ha->fw_dump_reading)
			break;

		qla_printk(KERN_INFO, ha,
		    "Firmware dump cleared on (%ld).\n", ha->host_no);

		ha->fw_dump_reading = 0;
		ha->fw_dumped = 0;
		break;
	case 1:
		if (ha->fw_dumped && !ha->fw_dump_reading) {
			ha->fw_dump_reading = 1;

			qla_printk(KERN_INFO, ha,
			    "Raw firmware dump ready for read on (%ld).\n",
			    ha->host_no);
		}
		break;
	case 2:
		qla2x00_alloc_fw_dump(ha);
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
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	int		size = ha->nvram_size;
	char		*nvram_cache = ha->nvram;

	if (!capable(CAP_SYS_ADMIN) || off > size || count == 0)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	}

	/* Read NVRAM data from cache. */
	memcpy(buf, &nvram_cache[off], count);

	return count;
}

static ssize_t
qla2x00_sysfs_write_nvram(struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint16_t	cnt;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->nvram_size)
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

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(ha, (uint8_t *)buf, ha->nvram_base, count);
	ha->isp_ops->read_nvram(ha, (uint8_t *)ha->nvram, ha->nvram_base,
	    count);

	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);

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
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));

	if (ha->optrom_state != QLA_SREADING)
		return 0;
	if (off > ha->optrom_region_size)
		return 0;
	if (off + count > ha->optrom_region_size)
		count = ha->optrom_region_size - off;

	memcpy(buf, &ha->optrom_buffer[off], count);

	return count;
}

static ssize_t
qla2x00_sysfs_write_optrom(struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));

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
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint32_t start = 0;
	uint32_t size = ha->optrom_size;
	int val, valid;

	if (off)
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

		if (start & 0xfff) {
			qla_printk(KERN_WARNING, ha,
			    "Invalid start region 0x%x/0x%x.\n", start, size);
			return -EINVAL;
		}

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
		ha->isp_ops->read_optrom(ha, ha->optrom_buffer,
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
		else if (start == (FA_BOOT_CODE_ADDR*4) ||
		    start == (FA_RISC_CODE_ADDR*4))
			valid = 1;
		else if (IS_QLA25XX(ha) && start == (FA_VPD_NVRAM_ADDR*4))
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

		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Writing flash region -- 0x%x/0x%x.\n",
		    ha->optrom_region_start, ha->optrom_region_size));

		ha->isp_ops->write_optrom(ha, ha->optrom_buffer,
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
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	int           size = ha->vpd_size;
	char          *vpd_cache = ha->vpd;

	if (!capable(CAP_SYS_ADMIN) || off > size || count == 0)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	}

	/* Read NVRAM data from cache. */
	memcpy(buf, &vpd_cache[off], count);

	return count;
}

static ssize_t
qla2x00_sysfs_write_vpd(struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != ha->vpd_size)
		return 0;

	/* Write NVRAM. */
	ha->isp_ops->write_nvram(ha, (uint8_t *)buf, ha->vpd_base, count);
	ha->isp_ops->read_nvram(ha, (uint8_t *)ha->vpd, ha->vpd_base, count);

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
	struct scsi_qla_host *ha = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
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

		rval = qla2x00_read_sfp(ha, ha->sfp_data_dma, addr, offset,
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
	{ NULL },
};

void
qla2x00_alloc_sysfs_attr(scsi_qla_host_t *ha)
{
	struct Scsi_Host *host = ha->host;
	struct sysfs_entry *iter;
	int ret;

	for (iter = bin_file_entries; iter->name; iter++) {
		if (iter->is4GBp_only && !IS_FWI2_CAPABLE(ha))
			continue;

		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
		if (ret)
			qla_printk(KERN_INFO, ha,
			    "Unable to create sysfs %s binary attribute "
			    "(%d).\n", iter->name, ret);
	}
}

void
qla2x00_free_sysfs_attr(scsi_qla_host_t *ha)
{
	struct Scsi_Host *host = ha->host;
	struct sysfs_entry *iter;

	for (iter = bin_file_entries; iter->name; iter++) {
		if (iter->is4GBp_only && !IS_FWI2_CAPABLE(ha))
			continue;

		sysfs_remove_bin_file(&host->shost_gendev.kobj,
		    iter->attr);
	}

	if (ha->beacon_blink_led == 1)
		ha->isp_ops->beacon_off(ha);
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
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	char fw_str[30];

	return snprintf(buf, PAGE_SIZE, "%s\n",
	    ha->isp_ops->fw_version_str(ha, fw_str));
}

static ssize_t
qla2x00_serial_num_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	uint32_t sn;

	if (IS_FWI2_CAPABLE(ha))
		return snprintf(buf, PAGE_SIZE, "\n");

	sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | ha->serial1;
	return snprintf(buf, PAGE_SIZE, "%c%05d\n", 'A' + sn / 100000,
	    sn % 100000);
}

static ssize_t
qla2x00_isp_name_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "ISP%04X\n", ha->pdev->device);
}

static ssize_t
qla2x00_isp_id_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%04x %04x %04x %04x\n",
	    ha->product_id[0], ha->product_id[1], ha->product_id[2],
	    ha->product_id[3]);
}

static ssize_t
qla2x00_model_name_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%s\n", ha->model_number);
}

static ssize_t
qla2x00_model_desc_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%s\n",
	    ha->model_desc ? ha->model_desc: "");
}

static ssize_t
qla2x00_pci_info_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	char pci_info[30];

	return snprintf(buf, PAGE_SIZE, "%s\n",
	    ha->isp_ops->pci_info_str(ha, pci_info));
}

static ssize_t
qla2x00_state_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	int len = 0;

	if (atomic_read(&ha->loop_state) == LOOP_DOWN ||
	    atomic_read(&ha->loop_state) == LOOP_DEAD)
		len = snprintf(buf, PAGE_SIZE, "Link Down\n");
	else if (atomic_read(&ha->loop_state) != LOOP_READY ||
	    test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags))
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
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	int len = 0;

	switch (ha->zio_mode) {
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
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
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
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	}
	return strlen(buf);
}

static ssize_t
qla2x00_zio_timer_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d us\n", ha->zio_timer * 100);
}

static ssize_t
qla2x00_zio_timer_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	int val = 0;
	uint16_t zio_timer;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;
	if (val > 25500 || val < 100)
		return -ERANGE;

	zio_timer = (uint16_t)(val / 100);
	ha->zio_timer = zio_timer;

	return strlen(buf);
}

static ssize_t
qla2x00_beacon_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	int len = 0;

	if (ha->beacon_blink_led)
		len += snprintf(buf + len, PAGE_SIZE-len, "Enabled\n");
	else
		len += snprintf(buf + len, PAGE_SIZE-len, "Disabled\n");
	return len;
}

static ssize_t
qla2x00_beacon_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));
	int val = 0;
	int rval;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return -EPERM;

	if (test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) {
		qla_printk(KERN_WARNING, ha,
		    "Abort ISP active -- ignoring beacon request.\n");
		return -EBUSY;
	}

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val)
		rval = ha->isp_ops->beacon_on(ha);
	else
		rval = ha->isp_ops->beacon_off(ha);

	if (rval != QLA_SUCCESS)
		count = 0;

	return count;
}

static ssize_t
qla2x00_optrom_bios_version_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->bios_revision[1],
	    ha->bios_revision[0]);
}

static ssize_t
qla2x00_optrom_efi_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->efi_revision[1],
	    ha->efi_revision[0]);
}

static ssize_t
qla2x00_optrom_fcode_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->fcode_revision[1],
	    ha->fcode_revision[0]);
}

static ssize_t
qla2x00_optrom_fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	scsi_qla_host_t *ha = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d %d\n",
	    ha->fw_revision[0], ha->fw_revision[1], ha->fw_revision[2],
	    ha->fw_revision[3]);
}

static DEVICE_ATTR(driver_version, S_IRUGO, qla2x00_drvr_version_show, NULL);
static DEVICE_ATTR(fw_version, S_IRUGO, qla2x00_fw_version_show, NULL);
static DEVICE_ATTR(serial_num, S_IRUGO, qla2x00_serial_num_show, NULL);
static DEVICE_ATTR(isp_name, S_IRUGO, qla2x00_isp_name_show, NULL);
static DEVICE_ATTR(isp_id, S_IRUGO, qla2x00_isp_id_show, NULL);
static DEVICE_ATTR(model_name, S_IRUGO, qla2x00_model_name_show, NULL);
static DEVICE_ATTR(model_desc, S_IRUGO, qla2x00_model_desc_show, NULL);
static DEVICE_ATTR(pci_info, S_IRUGO, qla2x00_pci_info_show, NULL);
static DEVICE_ATTR(state, S_IRUGO, qla2x00_state_show, NULL);
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

struct device_attribute *qla2x00_host_attrs[] = {
	&dev_attr_driver_version,
	&dev_attr_fw_version,
	&dev_attr_serial_num,
	&dev_attr_isp_name,
	&dev_attr_isp_id,
	&dev_attr_model_name,
	&dev_attr_model_desc,
	&dev_attr_pci_info,
	&dev_attr_state,
	&dev_attr_zio,
	&dev_attr_zio_timer,
	&dev_attr_beacon,
	&dev_attr_optrom_bios_version,
	&dev_attr_optrom_efi_version,
	&dev_attr_optrom_fcode_version,
	&dev_attr_optrom_fw_version,
	NULL,
};

/* Host attributes. */

static void
qla2x00_get_host_port_id(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	fc_host_port_id(shost) = ha->d_id.b.domain << 16 |
	    ha->d_id.b.area << 8 | ha->d_id.b.al_pa;
}

static void
qla2x00_get_host_speed(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_parent(shost_priv(shost));
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
	}
	fc_host_speed(shost) = speed;
}

static void
qla2x00_get_host_port_type(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_parent(shost_priv(shost));
	uint32_t port_type = FC_PORTTYPE_UNKNOWN;

	switch (ha->current_topology) {
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
	scsi_qla_host_t *ha = shost_priv(host);
	fc_port_t *fcport;
	u64 node_name = 0;

	list_for_each_entry(fcport, &ha->fcports, list) {
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
	scsi_qla_host_t *ha = shost_priv(host);
	fc_port_t *fcport;
	u64 port_name = 0;

	list_for_each_entry(fcport, &ha->fcports, list) {
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
	scsi_qla_host_t *ha = shost_priv(host);
	fc_port_t *fcport;
	uint32_t port_id = ~0U;

	list_for_each_entry(fcport, &ha->fcports, list) {
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
qla2x00_get_rport_loss_tmo(struct fc_rport *rport)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	scsi_qla_host_t *ha = shost_priv(host);

	rport->dev_loss_tmo = ha->port_down_retry_count + 5;
}

static void
qla2x00_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	scsi_qla_host_t *ha = shost_priv(host);

	if (timeout)
		ha->port_down_retry_count = timeout;
	else
		ha->port_down_retry_count = 1;

	rport->dev_loss_tmo = ha->port_down_retry_count + 5;
}

static int
qla2x00_issue_lip(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	qla2x00_loop_reset(ha);
	return 0;
}

static struct fc_host_statistics *
qla2x00_get_fc_host_stats(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_parent(shost_priv(shost));
	int rval;
	struct link_statistics *stats;
	dma_addr_t stats_dma;
	struct fc_host_statistics *pfc_host_stat;

	pfc_host_stat = &ha->fc_host_stat;
	memset(pfc_host_stat, -1, sizeof(struct fc_host_statistics));

	stats = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &stats_dma);
	if (stats == NULL) {
		DEBUG2_3_11(printk("%s(%ld): Failed to allocate memory.\n",
		    __func__, ha->host_no));
		goto done;
	}
	memset(stats, 0, DMA_POOL_SIZE);

	rval = QLA_FUNCTION_FAILED;
	if (IS_FWI2_CAPABLE(ha)) {
		rval = qla24xx_get_isp_stats(ha, stats, stats_dma);
	} else if (atomic_read(&ha->loop_state) == LOOP_READY &&
		    !test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) &&
		    !test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) &&
		    !ha->dpc_active) {
		/* Must be in a 'READY' state for statistics retrieval. */
		rval = qla2x00_get_link_status(ha, ha->loop_id, stats,
		    stats_dma);
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
		pfc_host_stat->tx_frames = stats->tx_frames;
		pfc_host_stat->rx_frames = stats->rx_frames;
		pfc_host_stat->dumped_frames = stats->dumped_frames;
		pfc_host_stat->nos_count = stats->nos_rcvd;
	}

done_free:
        dma_pool_free(ha->s_dma_pool, stats, stats_dma);
done:
	return pfc_host_stat;
}

static void
qla2x00_get_host_symbolic_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	qla2x00_get_sym_node_name(ha, fc_host_symbolic_name(shost));
}

static void
qla2x00_set_host_system_hostname(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	set_bit(REGISTER_FDMI_NEEDED, &ha->dpc_flags);
}

static void
qla2x00_get_host_fabric_name(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);
	u64 node_name;

	if (ha->device_flags & SWITCH_FOUND)
		node_name = wwn_to_u64(ha->fabric_node_name);
	else
		node_name = wwn_to_u64(ha->node_name);

	fc_host_fabric_name(shost) = node_name;
}

static void
qla2x00_get_host_port_state(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_parent(shost_priv(shost));

	if (!ha->flags.online)
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	else if (atomic_read(&ha->loop_state) == LOOP_TIMEOUT)
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
}

static int
qla24xx_vport_create(struct fc_vport *fc_vport, bool disable)
{
	int	ret = 0;
	scsi_qla_host_t *ha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha;

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
	qla_printk(KERN_INFO, vha, "VP entry id %d assigned.\n", vha->vp_idx);

	/* initialized vport states */
	atomic_set(&vha->loop_state, LOOP_DOWN);
	vha->vp_err_state=  VP_ERR_PORTDWN;
	vha->vp_prev_err_state=  VP_ERR_UNKWN;
	/* Check if physical ha port is Up */
	if (atomic_read(&ha->loop_state) == LOOP_DOWN ||
	    atomic_read(&ha->loop_state) == LOOP_DEAD) {
		/* Don't retry or attempt login of this virtual port */
		DEBUG15(printk ("scsi(%ld): pport loop_state is not UP.\n",
		    vha->host_no));
		atomic_set(&vha->loop_state, LOOP_DEAD);
		if (!disable)
			fc_vport_set_state(fc_vport, FC_VPORT_LINKDOWN);
	}

	if (scsi_add_host(vha->host, &fc_vport->dev)) {
		DEBUG15(printk("scsi(%ld): scsi_add_host failure for VP[%d].\n",
			vha->host_no, vha->vp_idx));
		goto vport_create_failed_2;
	}

	/* initialize attributes */
	fc_host_node_name(vha->host) = wwn_to_u64(vha->node_name);
	fc_host_port_name(vha->host) = wwn_to_u64(vha->port_name);
	fc_host_supported_classes(vha->host) =
		fc_host_supported_classes(ha->host);
	fc_host_supported_speeds(vha->host) =
		fc_host_supported_speeds(ha->host);

	qla24xx_vport_disable(fc_vport, disable);

	return 0;
vport_create_failed_2:
	qla24xx_disable_vp(vha);
	qla24xx_deallocate_vp_id(vha);
	kfree(vha->port_name);
	kfree(vha->node_name);
	scsi_host_put(vha->host);
	return FC_VPORT_FAILED;
}

static int
qla24xx_vport_delete(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *ha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha = fc_vport->dd_data;

	qla24xx_disable_vp(vha);
	qla24xx_deallocate_vp_id(vha);

	down(&ha->vport_sem);
	ha->cur_vport_count--;
	clear_bit(vha->vp_idx, ha->vp_idx_map);
	up(&ha->vport_sem);

	kfree(vha->node_name);
	kfree(vha->port_name);

	if (vha->timer_active) {
		qla2x00_vp_stop_timer(vha);
		DEBUG15(printk ("scsi(%ld): timer for the vport[%d] = %p "
		    "has stopped\n",
		    vha->host_no, vha->vp_idx, vha));
        }

	fc_remove_host(vha->host);

	scsi_remove_host(vha->host);

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

	.get_rport_dev_loss_tmo = qla2x00_get_rport_loss_tmo,
	.set_rport_dev_loss_tmo = qla2x00_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.issue_fc_host_lip = qla2x00_issue_lip,
	.get_fc_host_stats = qla2x00_get_fc_host_stats,

	.vport_create = qla24xx_vport_create,
	.vport_disable = qla24xx_vport_disable,
	.vport_delete = qla24xx_vport_delete,
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

	.get_rport_dev_loss_tmo = qla2x00_get_rport_loss_tmo,
	.set_rport_dev_loss_tmo = qla2x00_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.issue_fc_host_lip = qla2x00_issue_lip,
	.get_fc_host_stats = qla2x00_get_fc_host_stats,
};

void
qla2x00_init_host_attr(scsi_qla_host_t *ha)
{
	u32 speed = FC_PORTSPEED_UNKNOWN;

	fc_host_node_name(ha->host) = wwn_to_u64(ha->node_name);
	fc_host_port_name(ha->host) = wwn_to_u64(ha->port_name);
	fc_host_supported_classes(ha->host) = FC_COS_CLASS3;
	fc_host_max_npiv_vports(ha->host) = ha->max_npiv_vports;;
	fc_host_npiv_vports_inuse(ha->host) = ha->cur_vport_count;

	if (IS_QLA25XX(ha))
		speed = FC_PORTSPEED_8GBIT | FC_PORTSPEED_4GBIT |
		    FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else if (IS_QLA24XX_TYPE(ha))
		speed = FC_PORTSPEED_4GBIT | FC_PORTSPEED_2GBIT |
		    FC_PORTSPEED_1GBIT;
	else if (IS_QLA23XX(ha))
		speed = FC_PORTSPEED_2GBIT | FC_PORTSPEED_1GBIT;
	else
		speed = FC_PORTSPEED_1GBIT;
	fc_host_supported_speeds(ha->host) = speed;
}
