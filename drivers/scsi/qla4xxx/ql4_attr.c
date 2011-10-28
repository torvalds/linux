/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"

/* Scsi_Host attributes. */
static ssize_t
qla4xxx_fw_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(class_to_shost(dev));

	if (is_qla8022(ha))
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

static DEVICE_ATTR(fw_version, S_IRUGO, qla4xxx_fw_version_show, NULL);
static DEVICE_ATTR(serial_num, S_IRUGO, qla4xxx_serial_num_show, NULL);
static DEVICE_ATTR(iscsi_version, S_IRUGO, qla4xxx_iscsi_version_show, NULL);
static DEVICE_ATTR(optrom_version, S_IRUGO, qla4xxx_optrom_version_show, NULL);

struct device_attribute *qla4xxx_host_attrs[] = {
	&dev_attr_fw_version,
	&dev_attr_serial_num,
	&dev_attr_iscsi_version,
	&dev_attr_optrom_version,
	NULL,
};
