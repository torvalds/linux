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

#define IMX_SCU_SOC_DRIVER_NAME		"imx-scu-soc"

static struct imx_sc_ipc *soc_ipc_handle;

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
} __packed;

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

	ret = imx_scu_call_rpc(soc_ipc_handle, &msg, true);
	if (ret) {
		pr_err("%s: get soc info failed, ret %d\n", __func__, ret);
		return ret;
	}

	return msg.data.resp.id;
}

static int imx_scu_soc_probe(struct platform_device *pdev)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	int id, ret;
	u32 val;

	ret = imx_scu_get_handle(&soc_ipc_handle);
	if (ret)
		return ret;

	soc_dev_attr = devm_kzalloc(&pdev->dev, sizeof(*soc_dev_attr),
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

	/* format soc_id value passed from SCU firmware */
	val = id & 0x1f;
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "0x%x", val);
	if (!soc_dev_attr->soc_id)
		return -ENOMEM;

	/* format revision value passed from SCU firmware */
	val = (id >> 5) & 0xf;
	val = (((val >> 2) + 1) << 4) | (val & 0x3);
	soc_dev_attr->revision = kasprintf(GFP_KERNEL,
					   "%d.%d",
					   (val >> 4) & 0xf,
					   val & 0xf);
	if (!soc_dev_attr->revision) {
		ret = -ENOMEM;
		goto free_soc_id;
	}

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto free_revision;
	}

	return 0;

free_revision:
	kfree(soc_dev_attr->revision);
free_soc_id:
	kfree(soc_dev_attr->soc_id);
	return ret;
}

static struct platform_driver imx_scu_soc_driver = {
	.driver = {
		.name = IMX_SCU_SOC_DRIVER_NAME,
	},
	.probe = imx_scu_soc_probe,
};

static int __init imx_scu_soc_init(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx-scu");
	if (!np)
		return -ENODEV;

	of_node_put(np);

	ret = platform_driver_register(&imx_scu_soc_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple(IMX_SCU_SOC_DRIVER_NAME,
					       -1, NULL, 0);
	if (IS_ERR(pdev))
		platform_driver_unregister(&imx_scu_soc_driver);

	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(imx_scu_soc_init);
