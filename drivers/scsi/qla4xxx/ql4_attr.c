/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"

static ssize_t
qla4_8xxx_sysfs_read_fw_dump(struct file *filep, struct kobject *kobj,
			     struct bin_attribute *ba, char *buf, loff_t off,
			     size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
					       struct device, kobj)));

	if (is_qla40XX(ha))
		return -EINVAL;

	if (!test_bit(AF_82XX_DUMP_READING, &ha->flags))
		return 0;

	return memory_read_from_buffer(buf, count, &off, ha->fw_dump,
				       ha->fw_dump_size);
}

static ssize_t
qla4_8xxx_sysfs_write_fw_dump(struct file *filep, struct kobject *kobj,
			      struct bin_attribute *ba, char *buf, loff_t off,
			      size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
					       struct device, kobj)));
	uint32_t dev_state;
	long reading;
	int ret = 0;

	if (is_qla40XX(ha))
		return -EINVAL;

	if (off != 0)
		return ret;

	buf[1] = 0;
	ret = kstrtol(buf, 10, &reading);
	if (ret) {
		ql4_printk(KERN_ERR, ha, "%s: Invalid input. Return err %d\n",
			   __func__, ret);
		return ret;
	}

	switch (reading) {
	case 0:
		/* clear dump collection flags */
		if (test_and_clear_bit(AF_82XX_DUMP_READING, &ha->flags)) {
			clear_bit(AF_82XX_FW_DUMPED, &ha->flags);
			/* Reload minidump template */
			qla4xxx_alloc_fw_dump(ha);
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "Firmware template reloaded\n"));
		}
		break;
	case 1:
		/* Set flag to read dump */
		if (test_bit(AF_82XX_FW_DUMPED, &ha->flags) &&
		    !test_bit(AF_82XX_DUMP_READING, &ha->flags)) {
			set_bit(AF_82XX_DUMP_READING, &ha->flags);
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "Raw firmware dump ready for read on (%ld).\n",
					  ha->host_no));
		}
		break;
	case 2:
		/* Reset HBA */
		ha->isp_ops->idc_lock(ha);
		dev_state = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DEV_STATE);
		if (dev_state == QLA8XXX_DEV_READY) {
			ql4_printk(KERN_INFO, ha,
				   "%s: Setting Need reset, reset_owner is 0x%x.\n",
				   __func__, ha->func_num);
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
					    QLA8XXX_DEV_NEED_RESET);
			set_bit(AF_8XXX_RST_OWNER, &ha->flags);
		} else
			ql4_printk(KERN_INFO, ha,
				   "%s: Reset not performed as device state is 0x%x\n",
				   __func__, dev_state);

		ha->isp_ops->idc_unlock(ha);
		break;
	default:
		/* do nothing */
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
	.read = qla4_8xxx_sysfs_read_fw_dump,
	.write = qla4_8xxx_sysfs_write_fw_dump,
};

static struct sysfs_entry {
	char *name;
	struct bin_attribute *attr;
} bin_file_entries[] = {
	{ "fw_dump", &sysfs_fw_dump_attr },
	{ NULL },
};

void qla4_8xxx_alloc_sysfs_attr(struct scsi_qla_host *ha)
{
	struct Scsi_Host *host = ha->host;
	struct sysfs_entry *iter;
	int ret;

	for (iter = bin_file_entries; iter->name; iter++) {
		ret = sysfs_create_bin_file(&host->shost_gendev.kobj,
					    iter->attr);
		if (ret)
			ql4_printk(KERN_ERR, ha,
				   "Unable to create sysfs %s binary attribute (%d).\n",
				   iter->name, ret);
	}
}

void qla4_8xxx_free_sysfs_attr(struct scsi_qla_host *ha)
{
	struct Scsi_Host *host = ha->host;
	struct sysfs_entry *iter;

	for (iter = bin_file_entries; iter->name; iter++)
		sysfs_remove_bin_file(&host->shost_gendev.kobj,
				      iter->attr);
}

/* Scsi_Host attributes. */
static ssize_t
qla4xxx_fw_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	if (is_qla80XX(ha))
		return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d (%x)\n",
				ha->firmware_version[0],
				ha->firmware_version[1],
				ha->patch_number, ha->build_number);
	else
		return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d.%02d\n",
				ha->firmware_version[0],
				ha->firmware_version[1],
				ha->patch_number, ha->build_number);
}

static ssize_t
qla4xxx_serial_num_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%s\n", ha->serial_number);
}

static ssize_t
qla4xxx_iscsi_version_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%d.%02d\n", ha->iscsi_major,
			ha->iscsi_minor);
}

static ssize_t
qla4xxx_optrom_version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "%d.%02d.%02d.%02d\n",
			ha->bootload_major, ha->bootload_minor,
			ha->bootload_patch, ha->bootload_build);
}

static ssize_t
qla4xxx_board_id_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));
	return snprintf(buf, PAGE_SIZE, "0x%08X\n", ha->board_id);
}

static ssize_t
qla4xxx_fw_state_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	qla4xxx_get_firmware_state(ha);
	return snprintf(buf, PAGE_SIZE, "0x%08X%8X\n", ha->firmware_state,
			ha->addl_fw_state);
}

static ssize_t
qla4xxx_phy_port_cnt_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	if (is_qla40XX(ha))
		return -ENOSYS;

	return snprintf(buf, PAGE_SIZE, "0x%04X\n", ha->phy_port_cnt);
}

static ssize_t
qla4xxx_phy_port_num_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	if (is_qla40XX(ha))
		return -ENOSYS;

	return snprintf(buf, PAGE_SIZE, "0x%04X\n", ha->phy_port_num);
}

static ssize_t
qla4xxx_iscsi_func_cnt_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	if (is_qla40XX(ha))
		return -ENOSYS;

	return snprintf(buf, PAGE_SIZE, "0x%04X\n", ha->iscsi_pci_func_cnt);
}

static ssize_t
qla4xxx_hba_model_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%s\n", ha->model_name);
}

static DEVICE_ATTR(fw_version, S_IRUGO, qla4xxx_fw_version_show, NULL);
static DEVICE_ATTR(serial_num, S_IRUGO, qla4xxx_serial_num_show, NULL);
static DEVICE_ATTR(iscsi_version, S_IRUGO, qla4xxx_iscsi_version_show, NULL);
static DEVICE_ATTR(optrom_version, S_IRUGO, qla4xxx_optrom_version_show, NULL);
static DEVICE_ATTR(board_id, S_IRUGO, qla4xxx_board_id_show, NULL);
static DEVICE_ATTR(fw_state, S_IRUGO, qla4xxx_fw_state_show, NULL);
static DEVICE_ATTR(phy_port_cnt, S_IRUGO, qla4xxx_phy_port_cnt_show, NULL);
static DEVICE_ATTR(phy_port_num, S_IRUGO, qla4xxx_phy_port_num_show, NULL);
static DEVICE_ATTR(iscsi_func_cnt, S_IRUGO, qla4xxx_iscsi_func_cnt_show, NULL);
static DEVICE_ATTR(hba_model, S_IRUGO, qla4xxx_hba_model_show, NULL);

struct device_attribute *qla4xxx_host_attrs[] = {
	&dev_attr_fw_version,
	&dev_attr_serial_num,
	&dev_attr_iscsi_version,
	&dev_attr_optrom_version,
	&dev_attr_board_id,
	&dev_attr_fw_state,
	&dev_attr_phy_port_cnt,
	&dev_attr_phy_port_num,
	&dev_attr_iscsi_func_cnt,
	&dev_attr_hba_model,
	NULL,
};
