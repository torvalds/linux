/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#include "qedf.h"

static ssize_t
qedf_fcoe_mac_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct fc_lport *lport = shost_priv(class_to_shost(dev));
	u32 port_id;
	u8 lport_src_id[3];
	u8 fcoe_mac[6];

	port_id = fc_host_port_id(lport->host);
	lport_src_id[2] = (port_id & 0x000000FF);
	lport_src_id[1] = (port_id & 0x0000FF00) >> 8;
	lport_src_id[0] = (port_id & 0x00FF0000) >> 16;
	fc_fcoe_set_mac(fcoe_mac, lport_src_id);

	return scnprintf(buf, PAGE_SIZE, "%pM\n", fcoe_mac);
}

static DEVICE_ATTR(fcoe_mac, S_IRUGO, qedf_fcoe_mac_show, NULL);

struct device_attribute *qedf_host_attrs[] = {
	&dev_attr_fcoe_mac,
	NULL,
};

extern const struct qed_fcoe_ops *qed_ops;

inline bool qedf_is_vport(struct qedf_ctx *qedf)
{
	return (!(qedf->lport->vport == NULL));
}

/* Get base qedf for physical port from vport */
static struct qedf_ctx *qedf_get_base_qedf(struct qedf_ctx *qedf)
{
	struct fc_lport *lport;
	struct fc_lport *base_lport;

	if (!(qedf_is_vport(qedf)))
		return NULL;

	lport = qedf->lport;
	base_lport = shost_priv(vport_to_shost(lport->vport));
	return (struct qedf_ctx *)(lport_priv(base_lport));
}

void qedf_capture_grc_dump(struct qedf_ctx *qedf)
{
	struct qedf_ctx *base_qedf;

	/* Make sure we use the base qedf to take the GRC dump */
	if (qedf_is_vport(qedf))
		base_qedf = qedf_get_base_qedf(qedf);
	else
		base_qedf = qedf;

	if (test_bit(QEDF_GRCDUMP_CAPTURE, &base_qedf->flags)) {
		QEDF_INFO(&(base_qedf->dbg_ctx), QEDF_LOG_INFO,
		    "GRC Dump already captured.\n");
		return;
	}


	qedf_get_grc_dump(base_qedf->cdev, qed_ops->common,
	    &base_qedf->grcdump, &base_qedf->grcdump_size);
	QEDF_ERR(&(base_qedf->dbg_ctx), "GRC Dump captured.\n");
	set_bit(QEDF_GRCDUMP_CAPTURE, &base_qedf->flags);
	qedf_uevent_emit(base_qedf->lport->host, QEDF_UEVENT_CODE_GRCDUMP,
	    NULL);
}

static ssize_t
qedf_sysfs_read_grcdump(struct file *filep, struct kobject *kobj,
			struct bin_attribute *ba, char *buf, loff_t off,
			size_t count)
{
	ssize_t ret = 0;
	struct fc_lport *lport = shost_priv(dev_to_shost(container_of(kobj,
							struct device, kobj)));
	struct qedf_ctx *qedf = lport_priv(lport);

	if (test_bit(QEDF_GRCDUMP_CAPTURE, &qedf->flags)) {
		ret = memory_read_from_buffer(buf, count, &off,
		    qedf->grcdump, qedf->grcdump_size);
	} else {
		QEDF_ERR(&(qedf->dbg_ctx), "GRC Dump not captured!\n");
	}

	return ret;
}

static ssize_t
qedf_sysfs_write_grcdump(struct file *filep, struct kobject *kobj,
			struct bin_attribute *ba, char *buf, loff_t off,
			size_t count)
{
	struct fc_lport *lport = NULL;
	struct qedf_ctx *qedf = NULL;
	long reading;
	int ret = 0;
	char msg[40];

	if (off != 0)
		return ret;


	lport = shost_priv(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	qedf = lport_priv(lport);

	buf[1] = 0;
	ret = kstrtol(buf, 10, &reading);
	if (ret) {
		QEDF_ERR(&(qedf->dbg_ctx), "Invalid input, err(%d)\n", ret);
		return ret;
	}

	memset(msg, 0, sizeof(msg));
	switch (reading) {
	case 0:
		memset(qedf->grcdump, 0, qedf->grcdump_size);
		clear_bit(QEDF_GRCDUMP_CAPTURE, &qedf->flags);
		break;
	case 1:
		qedf_capture_grc_dump(qedf);
		break;
	}

	return count;
}

static struct bin_attribute sysfs_grcdump_attr = {
	.attr = {
		.name = "grcdump",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.read = qedf_sysfs_read_grcdump,
	.write = qedf_sysfs_write_grcdump,
};

static struct sysfs_bin_attrs bin_file_entries[] = {
	{"grcdump", &sysfs_grcdump_attr},
	{NULL},
};

void qedf_create_sysfs_ctx_attr(struct qedf_ctx *qedf)
{
	qedf_create_sysfs_attr(qedf->lport->host, bin_file_entries);
}

void qedf_remove_sysfs_ctx_attr(struct qedf_ctx *qedf)
{
	qedf_remove_sysfs_attr(qedf->lport->host, bin_file_entries);
}
