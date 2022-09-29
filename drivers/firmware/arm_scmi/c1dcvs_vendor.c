// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_c1dcvs.h>
#include "common.h"

#define SCMI_MAX_RX_SIZE	128

enum scmi_c1dcvs_protocol_cmd {
	SET_ENABLE_C1DCVS = 11,
	GET_ENABLE_C1DCVS,
	SET_ENABLE_TRACE,
	GET_ENABLE_TRACE,
	SET_IPC_THRESH,
	GET_IPC_THRESH,
	SET_EFREQ_THRESH,
	GET_EFREQ_THRESH,
	SET_HYSTERESIS,
	GET_HYSTERESIS,
};

struct c1dcvs_thresh {
	unsigned int cluster;
	unsigned int thresh;
};

static int scmi_send_tunable_c1dcvs(const struct scmi_protocol_handle *ph,
				    void *buf, u32 msg_id)
{
	int ret;
	struct scmi_xfer *t;
	unsigned int *msg;
	unsigned int *src = buf;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg), sizeof(*msg),
				      &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	*msg = cpu_to_le32(*src);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_get_tunable_c1dcvs(const struct scmi_protocol_handle *ph,
				    void *buf, u32 msg_id)
{
	int ret;
	struct scmi_xfer *t;
	struct c1dcvs_thresh *msg;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg), SCMI_MAX_RX_SIZE,
				      &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	memcpy(buf, t->rx.buf, t->rx.len);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_send_thresh_c1dcvs(const struct scmi_protocol_handle *ph,
				    void *buf, u32 msg_id)
{
	int ret;
	struct scmi_xfer *t;
	struct c1dcvs_thresh *msg;
	unsigned int *src = buf;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg), sizeof(*msg),
				      &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->cluster = cpu_to_le32(src[0]);
	msg->thresh = cpu_to_le32(src[1]);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_set_enable_c1dcvs(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_send_tunable_c1dcvs(ph, buf, SET_ENABLE_C1DCVS);
}
static int scmi_get_enable_c1dcvs(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_get_tunable_c1dcvs(ph, buf, GET_ENABLE_C1DCVS);
}
static int scmi_set_enable_trace(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_send_tunable_c1dcvs(ph, buf, SET_ENABLE_TRACE);
}
static int scmi_get_enable_trace(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_get_tunable_c1dcvs(ph, buf, GET_ENABLE_TRACE);
}
static int scmi_set_ipc_thresh(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_send_thresh_c1dcvs(ph, buf, SET_IPC_THRESH);
}
static int scmi_get_ipc_thresh(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_get_tunable_c1dcvs(ph, buf, GET_IPC_THRESH);
}
static int scmi_set_efreq_thresh(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_send_thresh_c1dcvs(ph, buf, SET_EFREQ_THRESH);
}
static int scmi_get_efreq_thresh(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_get_tunable_c1dcvs(ph, buf, GET_EFREQ_THRESH);
}
static int scmi_set_hysteresis(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_send_tunable_c1dcvs(ph, buf, SET_HYSTERESIS);
}
static int scmi_get_hysteresis(const struct scmi_protocol_handle *ph, void *buf)
{
	return scmi_get_tunable_c1dcvs(ph, buf, GET_HYSTERESIS);
}

static struct scmi_c1dcvs_vendor_ops c1dcvs_config_ops = {
	.set_enable_c1dcvs	= scmi_set_enable_c1dcvs,
	.get_enable_c1dcvs	= scmi_get_enable_c1dcvs,
	.set_enable_trace	= scmi_set_enable_trace,
	.get_enable_trace	= scmi_get_enable_trace,
	.set_ipc_thresh		= scmi_set_ipc_thresh,
	.get_ipc_thresh		= scmi_get_ipc_thresh,
	.set_efreq_thresh	= scmi_set_efreq_thresh,
	.get_efreq_thresh	= scmi_get_efreq_thresh,
	.set_hysteresis		= scmi_set_hysteresis,
	.get_hysteresis		= scmi_get_hysteresis,
};

static int scmi_c1dcvs_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	ph->xops->version_get(ph, &version);

	dev_dbg(ph->dev, "version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	return 0;
}

static const struct scmi_protocol scmi_c1dcvs = {
	.id = SCMI_C1DCVS_PROTOCOL,
	.owner = THIS_MODULE,
	.instance_init = &scmi_c1dcvs_protocol_init,
	.ops = &c1dcvs_config_ops,
};
module_scmi_protocol(scmi_c1dcvs);

MODULE_DESCRIPTION("SCMI C1DCVS vendor Protocol");
MODULE_LICENSE("GPL");
