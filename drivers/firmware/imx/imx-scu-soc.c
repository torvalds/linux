// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static struct imx_sc_ipc *imx_sc_soc_ipc_handle;

struct imx_sc_msg_misc_get_soc_id {
	struct imx_sc_rpc_msg hdr;
	union {
		struct {
			u32 control;
			u16 resource;
		} __packed req;
		struct {
			u32 id;
		} resp;
	} data;
} __packed __aligned(4);

struct imx_sc_msg_misc_get_soc_uid {
	struct imx_sc_rpc_msg hdr;
	u32 uid_low;
	u32 uid_high;
} __packed;

static int imx_scu_soc_uid(u64 *soc_uid)
{
	struct imx_sc_msg_misc_get_soc_uid msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_UNIQUE_ID;
	hdr->size = 1;

	ret = imx_scu_call_rpc(imx_sc_soc_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: get soc uid failed, ret %d\n", __func__, ret);
		return ret;
	}

	*soc_uid = msg.uid_high;
	*soc_uid <<= 32;
	*soc_uid |= msg.uid_low;

	return 0;
}

static int imx_scu_soc_id(void)
{
	struct imx_sc_msg_misc_get_soc_id msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_GET_CONTROL;
	hdr->size = 3;

	msg.data.req.control = IMX_SC_C_ID;
	msg.data.req.resource = IMX_SC_R_SYSTEM;

	ret = imx_scu_call_rpc(imx_sc_soc_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: get soc info failed, ret %d\n", __func__, ret);
		return ret;
	}

	return msg.data.resp.id;
}

int imx_scu_soc_init(struct device *dev)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	int id, ret;
	u64 uid = 0;
	u32 val;

	ret = imx_scu_get_handle(&imx_sc_soc_ipc_handle);
	if (ret)
		return ret;

	soc_dev_attr = devm_kzalloc(dev, sizeof(*soc_dev_attr),
				    GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Freescale i.MX";

	ret = of_property_read_string(of_root,
				      "model",
				      &soc_dev_attr->machine);
	if (ret)
		return ret;

	id = imx_scu_soc_id();
	if (id < 0)
		return -EINVAL;

	ret = imx_scu_soc_uid(&uid);
	if (ret < 0)
		return -EINVAL;

	/* format soc_id value passed from SCU firmware */
	val = id & 0x1f;
	soc_dev_attr->soc_id = devm_kasprintf(dev, GFP_KERNEL, "0x%x", val);
	if (!soc_dev_attr->soc_id)
		return -ENOMEM;

	/* format revision value passed from SCU firmware */
	val = (id >> 5) & 0xf;
	val = (((val >> 2) + 1) << 4) | (val & 0x3);
	soc_dev_attr->revision = devm_kasprintf(dev, GFP_KERNEL, "%d.%d",
						(val >> 4) & 0xf, val & 0xf);
	if (!soc_dev_attr->revision)
		return -ENOMEM;

	soc_dev_attr->serial_number = devm_kasprintf(dev, GFP_KERNEL,
						     "%016llX", uid);
	if (!soc_dev_attr->serial_number)
		return -ENOMEM;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	return 0;
}
