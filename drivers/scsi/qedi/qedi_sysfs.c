/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include "qedi.h"
#include "qedi_gbl.h"
#include "qedi_iscsi.h"
#include "qedi_dbg.h"

static inline struct qedi_ctx *qedi_dev_to_hba(struct device *dev)
{
	struct Scsi_Host *shost = class_to_shost(dev);

	return iscsi_host_priv(shost);
}

static ssize_t qedi_show_port_state(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct qedi_ctx *qedi = qedi_dev_to_hba(dev);

	if (atomic_read(&qedi->link_state) == QEDI_LINK_UP)
		return sprintf(buf, "Online\n");
	else
		return sprintf(buf, "Linkdown\n");
}

static ssize_t qedi_show_speed(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct qedi_ctx *qedi = qedi_dev_to_hba(dev);
	struct qed_link_output if_link;

	qedi_ops->common->get_link(qedi->cdev, &if_link);

	return sprintf(buf, "%d Gbit\n", if_link.speed / 1000);
}

static DEVICE_ATTR(port_state, 0444, qedi_show_port_state, NULL);
static DEVICE_ATTR(speed, 0444, qedi_show_speed, NULL);

struct device_attribute *qedi_shost_attrs[] = {
	&dev_attr_port_state,
	&dev_attr_speed,
	NULL
};
