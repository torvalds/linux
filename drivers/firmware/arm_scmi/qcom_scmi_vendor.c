// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "common.h"
#include <linux/qcom_scmi_vendor.h>

#define	EXTENDED_MSG_ID			0
#define	SCMI_MAX_TX_RX_SIZE		128
#define	PROTOCOL_PAYLOAD_SIZE		16
#define	SET_PARAM			0x10
#define	GET_PARAM			0x11
#define	START_ACTIVITY			0x12
#define	STOP_ACTIVITY			0x13

static int qcom_scmi_set_param(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
	u32 param_id, size_t size)
{
	int ret = -EINVAL;
	struct scmi_xfer *t;
	uint32_t *msg;

	if (!ph || !ph->xops)
		return ret;
	ret = ph->xops->xfer_get_init(ph, SET_PARAM, size + PROTOCOL_PAYLOAD_SIZE,
			SCMI_MAX_TX_RX_SIZE, &t);
	if (ret)
		return ret;
	msg = t->tx.buf;
	*msg++ = cpu_to_le32(EXTENDED_MSG_ID);
	*msg++ = cpu_to_le32(algo_str & GENMASK(31, 0));
	*msg++ = cpu_to_le32((algo_str & GENMASK(63, 32)) >> 32);
	*msg++ = cpu_to_le32(param_id);
	memcpy(msg, buf, size);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int qcom_scmi_get_param(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
	u32 param_id, size_t tx_size, size_t rx_size)
{

	int ret = -EINVAL;
	struct scmi_xfer *t;
	uint32_t *msg;

	if (!ph || !ph->xops || !buf)
		return ret;
	ret = ph->xops->xfer_get_init(ph, GET_PARAM, tx_size + PROTOCOL_PAYLOAD_SIZE,
			SCMI_MAX_TX_RX_SIZE, &t);
	if (ret)
		return ret;
	msg = t->tx.buf;
	*msg++ = cpu_to_le32(EXTENDED_MSG_ID);
	*msg++ = cpu_to_le32(algo_str & GENMASK(31, 0));
	*msg++ = cpu_to_le32((algo_str & GENMASK(63, 32)) >> 32);
	*msg++ = cpu_to_le32(param_id);
	memcpy(msg, buf, tx_size);
	ret = ph->xops->do_xfer(ph, t);
	if (t->rx.len > rx_size) {
		pr_err("SCMI received buffer size %d is more than expected size %d\n",
			t->rx.len, rx_size);
		return -EMSGSIZE;
	}
	memcpy(buf, t->rx.buf, t->rx.len);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int qcom_scmi_start_activity(const struct scmi_protocol_handle *ph,
	void *buf, u64 algo_str, u32 param_id, size_t size)
{
	int ret = -EINVAL;
	struct scmi_xfer *t;
	uint32_t *msg;

	if (!ph || !ph->xops)
		return ret;
	ret = ph->xops->xfer_get_init(ph, START_ACTIVITY, size + PROTOCOL_PAYLOAD_SIZE,
			SCMI_MAX_TX_RX_SIZE, &t);
	if (ret)
		return ret;
	msg = t->tx.buf;
	*msg++ = cpu_to_le32(EXTENDED_MSG_ID);
	*msg++ = cpu_to_le32(algo_str & GENMASK(31, 0));
	*msg++ = cpu_to_le32((algo_str & GENMASK(63, 32)) >> 32);
	*msg++ = cpu_to_le32(param_id);
	memcpy(msg, buf, size);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int qcom_scmi_stop_activity(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
	u32 param_id, size_t size)
{
	int ret = -EINVAL;
	struct scmi_xfer *t;
	uint32_t *msg;

	if (!ph || !ph->xops)
		return ret;
	ret = ph->xops->xfer_get_init(ph, STOP_ACTIVITY, size + PROTOCOL_PAYLOAD_SIZE,
			SCMI_MAX_TX_RX_SIZE, &t);
	if (ret)
		return ret;
	msg = t->tx.buf;
	*msg++ = cpu_to_le32(EXTENDED_MSG_ID);
	*msg++ = cpu_to_le32(algo_str & GENMASK(31, 0));
	*msg++ = cpu_to_le32((algo_str & GENMASK(63, 32)) >> 32);
	*msg++ = cpu_to_le32(param_id);
	memcpy(msg, buf, size);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);
	return ret;
}
static struct qcom_scmi_vendor_ops qcom_proto_ops = {
	.set_param = qcom_scmi_set_param,
	.get_param = qcom_scmi_get_param,
	.start_activity = qcom_scmi_start_activity,
	.stop_activity = qcom_scmi_stop_activity,
};

static int qcom_scmi_vendor_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	ph->xops->version_get(ph, &version);

	dev_dbg(ph->dev, "qcom scmi version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));
	return 0;
}

static const struct scmi_protocol qcom_scmi_vendor = {
	.id = QCOM_SCMI_VENDOR_PROTOCOL,
	.owner = THIS_MODULE,
	.instance_init = &qcom_scmi_vendor_protocol_init,
	.ops = &qcom_proto_ops,
};
module_scmi_protocol(qcom_scmi_vendor);

MODULE_DESCRIPTION("qcom scmi vendor Protocol");
MODULE_LICENSE("GPL");
