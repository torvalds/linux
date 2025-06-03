// SPDX-License-Identifier: GPL-2.0
/*
 * System control and Management Interface (SCMI) NXP LMM Protocol
 *
 * Copyright 2025 NXP
 */

#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>

#include "../../protocols.h"
#include "../../notify.h"

#define SCMI_PROTOCOL_SUPPORTED_VERSION		0x10000

enum scmi_imx_lmm_protocol_cmd {
	SCMI_IMX_LMM_ATTRIBUTES	= 0x3,
	SCMI_IMX_LMM_BOOT = 0x4,
	SCMI_IMX_LMM_RESET = 0x5,
	SCMI_IMX_LMM_SHUTDOWN = 0x6,
	SCMI_IMX_LMM_WAKE = 0x7,
	SCMI_IMX_LMM_SUSPEND = 0x8,
	SCMI_IMX_LMM_NOTIFY = 0x9,
	SCMI_IMX_LMM_RESET_REASON = 0xA,
	SCMI_IMX_LMM_POWER_ON = 0xB,
	SCMI_IMX_LMM_RESET_VECTOR_SET = 0xC,
};

struct scmi_imx_lmm_priv {
	u32 nr_lmm;
};

#define SCMI_IMX_LMM_NR_LM_MASK	GENMASK(5, 0)
#define SCMI_IMX_LMM_NR_MAX	16
struct scmi_msg_imx_lmm_protocol_attributes {
	__le32 attributes;
};

struct scmi_msg_imx_lmm_attributes_out {
	__le32 lmid;
	__le32 attributes;
	__le32 state;
	__le32 errstatus;
	u8 name[LMM_MAX_NAME];
};

struct scmi_imx_lmm_reset_vector_set_in {
	__le32 lmid;
	__le32 cpuid;
	__le32 flags; /* reserved for future extension */
	__le32 resetvectorlow;
	__le32 resetvectorhigh;
};

struct scmi_imx_lmm_shutdown_in {
	__le32 lmid;
#define SCMI_IMX_LMM_SHUTDOWN_GRACEFUL	BIT(0)
	__le32 flags;
};

static int scmi_imx_lmm_validate_lmid(const struct scmi_protocol_handle *ph, u32 lmid)
{
	struct scmi_imx_lmm_priv *priv = ph->get_priv(ph);

	if (lmid >= priv->nr_lmm)
		return -EINVAL;

	return 0;
}

static int scmi_imx_lmm_attributes(const struct scmi_protocol_handle *ph,
				   u32 lmid, struct scmi_imx_lmm_info *info)
{
	struct scmi_msg_imx_lmm_attributes_out *out;
	struct scmi_xfer *t;
	int ret;

	ret = ph->xops->xfer_get_init(ph, SCMI_IMX_LMM_ATTRIBUTES, sizeof(u32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(lmid, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		out = t->rx.buf;
		info->lmid = le32_to_cpu(out->lmid);
		info->state = le32_to_cpu(out->state);
		info->errstatus = le32_to_cpu(out->errstatus);
		strscpy(info->name, out->name);
		dev_dbg(ph->dev, "i.MX LMM: Logical Machine(%d), name: %s\n",
			info->lmid, info->name);
	} else {
		dev_err(ph->dev, "i.MX LMM: Failed to get info of Logical Machine(%u)\n", lmid);
	}

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int
scmi_imx_lmm_power_boot(const struct scmi_protocol_handle *ph, u32 lmid, bool boot)
{
	struct scmi_xfer *t;
	u8 msg_id;
	int ret;

	ret = scmi_imx_lmm_validate_lmid(ph, lmid);
	if (ret)
		return ret;

	if (boot)
		msg_id = SCMI_IMX_LMM_BOOT;
	else
		msg_id = SCMI_IMX_LMM_POWER_ON;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(u32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(lmid, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_lmm_reset_vector_set(const struct scmi_protocol_handle *ph,
					 u32 lmid, u32 cpuid, u32 flags, u64 vector)
{
	struct scmi_imx_lmm_reset_vector_set_in *in;
	struct scmi_xfer *t;
	int ret;

	ret = ph->xops->xfer_get_init(ph, SCMI_IMX_LMM_RESET_VECTOR_SET, sizeof(*in),
				      0, &t);
	if (ret)
		return ret;

	in = t->tx.buf;
	in->lmid = cpu_to_le32(lmid);
	in->cpuid = cpu_to_le32(cpuid);
	in->flags = cpu_to_le32(0);
	in->resetvectorlow = cpu_to_le32(lower_32_bits(vector));
	in->resetvectorhigh = cpu_to_le32(upper_32_bits(vector));
	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_lmm_shutdown(const struct scmi_protocol_handle *ph, u32 lmid,
				 u32 flags)
{
	struct scmi_imx_lmm_shutdown_in *in;
	struct scmi_xfer *t;
	int ret;

	ret = scmi_imx_lmm_validate_lmid(ph, lmid);
	if (ret)
		return ret;

	ret = ph->xops->xfer_get_init(ph, SCMI_IMX_LMM_SHUTDOWN, sizeof(*in),
				      0, &t);
	if (ret)
		return ret;

	in = t->tx.buf;
	in->lmid = cpu_to_le32(lmid);
	if (flags & SCMI_IMX_LMM_SHUTDOWN_GRACEFUL)
		in->flags = cpu_to_le32(SCMI_IMX_LMM_SHUTDOWN_GRACEFUL);
	else
		in->flags = cpu_to_le32(0);
	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static const struct scmi_imx_lmm_proto_ops scmi_imx_lmm_proto_ops = {
	.lmm_power_boot = scmi_imx_lmm_power_boot,
	.lmm_info = scmi_imx_lmm_attributes,
	.lmm_reset_vector_set = scmi_imx_lmm_reset_vector_set,
	.lmm_shutdown = scmi_imx_lmm_shutdown,
};

static int scmi_imx_lmm_protocol_attributes_get(const struct scmi_protocol_handle *ph,
						struct scmi_imx_lmm_priv *priv)
{
	struct scmi_msg_imx_lmm_protocol_attributes *attr;
	struct scmi_xfer *t;
	int ret;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0,
				      sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		priv->nr_lmm = le32_get_bits(attr->attributes, SCMI_IMX_LMM_NR_LM_MASK);
		if (priv->nr_lmm > SCMI_IMX_LMM_NR_MAX) {
			dev_err(ph->dev, "i.MX LMM: %d:Exceed max supported Logical Machines\n",
				priv->nr_lmm);
			ret = -EINVAL;
		} else {
			dev_info(ph->dev, "i.MX LMM: %d Logical Machines\n", priv->nr_lmm);
		}
	}

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_imx_lmm_protocol_init(const struct scmi_protocol_handle *ph)
{
	struct scmi_imx_lmm_priv *info;
	u32 version;
	int ret;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_info(ph->dev, "NXP SM LMM Version %d.%d\n",
		 PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	info = devm_kzalloc(ph->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = scmi_imx_lmm_protocol_attributes_get(ph, info);
	if (ret)
		return ret;

	return ph->set_priv(ph, info, version);
}

static const struct scmi_protocol scmi_imx_lmm = {
	.id = SCMI_PROTOCOL_IMX_LMM,
	.owner = THIS_MODULE,
	.instance_init = &scmi_imx_lmm_protocol_init,
	.ops = &scmi_imx_lmm_proto_ops,
	.supported_version = SCMI_PROTOCOL_SUPPORTED_VERSION,
	.vendor_id = SCMI_IMX_VENDOR,
	.sub_vendor_id = SCMI_IMX_SUBVENDOR,
};
module_scmi_protocol(scmi_imx_lmm);

MODULE_ALIAS("scmi-protocol-" __stringify(SCMI_PROTOCOL_IMX_LMM) "-" SCMI_IMX_VENDOR);
MODULE_DESCRIPTION("i.MX SCMI LMM driver");
MODULE_LICENSE("GPL");
