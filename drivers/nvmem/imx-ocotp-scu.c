// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX8 OCOTP fusebox driver
 *
 * Copyright 2019 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum ocotp_devtype {
	IMX8QXP,
	IMX8QM,
};

struct ocotp_devtype_data {
	int devtype;
	int nregs;
};

struct ocotp_priv {
	struct device *dev;
	const struct ocotp_devtype_data *data;
	struct imx_sc_ipc *nvmem_ipc;
};

struct imx_sc_msg_misc_fuse_read {
	struct imx_sc_rpc_msg hdr;
	u32 word;
} __packed;

static struct ocotp_devtype_data imx8qxp_data = {
	.devtype = IMX8QXP,
	.nregs = 800,
};

static struct ocotp_devtype_data imx8qm_data = {
	.devtype = IMX8QM,
	.nregs = 800,
};

static int imx_sc_misc_otp_fuse_read(struct imx_sc_ipc *ipc, u32 word,
				     u32 *val)
{
	struct imx_sc_msg_misc_fuse_read msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_OTP_FUSE_READ;
	hdr->size = 2;

	msg.word = word;

	ret = imx_scu_call_rpc(ipc, &msg, true);
	if (ret)
		return ret;

	*val = msg.word;

	return 0;
}

static int imx_scu_ocotp_read(void *context, unsigned int offset,
			      void *val, size_t bytes)
{
	struct ocotp_priv *priv = context;
	u32 count, index, num_bytes;
	u32 *buf;
	void *p;
	int i, ret;

	index = offset >> 2;
	num_bytes = round_up((offset % 4) + bytes, 4);
	count = num_bytes >> 2;

	if (count > (priv->data->nregs - index))
		count = priv->data->nregs - index;

	p = kzalloc(num_bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	buf = p;

	for (i = index; i < (index + count); i++) {
		if (priv->data->devtype == IMX8QXP) {
			if ((i > 271) && (i < 544)) {
				*buf++ = 0;
				continue;
			}
		}

		ret = imx_sc_misc_otp_fuse_read(priv->nvmem_ipc, i, buf);
		if (ret) {
			kfree(p);
			return ret;
		}
		buf++;
	}

	memcpy(val, (u8 *)p + offset % 4, bytes);

	kfree(p);

	return 0;
}

static struct nvmem_config imx_scu_ocotp_nvmem_config = {
	.name = "imx-scu-ocotp",
	.read_only = true,
	.word_size = 4,
	.stride = 1,
	.owner = THIS_MODULE,
	.reg_read = imx_scu_ocotp_read,
};

static const struct of_device_id imx_scu_ocotp_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-scu-ocotp", (void *)&imx8qxp_data },
	{ .compatible = "fsl,imx8qm-scu-ocotp", (void *)&imx8qm_data },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_scu_ocotp_dt_ids);

static int imx_scu_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocotp_priv *priv;
	struct nvmem_device *nvmem;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = imx_scu_get_handle(&priv->nvmem_ipc);
	if (ret)
		return ret;

	priv->data = of_device_get_match_data(dev);
	priv->dev = dev;
	imx_scu_ocotp_nvmem_config.size = 4 * priv->data->nregs;
	imx_scu_ocotp_nvmem_config.dev = dev;
	imx_scu_ocotp_nvmem_config.priv = priv;
	nvmem = devm_nvmem_register(dev, &imx_scu_ocotp_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver imx_scu_ocotp_driver = {
	.probe	= imx_scu_ocotp_probe,
	.driver = {
		.name	= "imx_scu_ocotp",
		.of_match_table = imx_scu_ocotp_dt_ids,
	},
};
module_platform_driver(imx_scu_ocotp_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("i.MX8 SCU OCOTP fuse box driver");
MODULE_LICENSE("GPL v2");
