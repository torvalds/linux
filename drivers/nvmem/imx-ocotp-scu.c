// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX8 OCOTP fusebox driver
 *
 * Copyright 2019 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#include <linux/arm-smccc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define IMX_SIP_OTP			0xC200000A
#define IMX_SIP_OTP_WRITE		0x2

enum ocotp_devtype {
	IMX8QXP,
	IMX8QM,
};

#define ECC_REGION	BIT(0)
#define HOLE_REGION	BIT(1)

struct ocotp_region {
	u32 start;
	u32 end;
	u32 flag;
};

struct ocotp_devtype_data {
	int devtype;
	int nregs;
	u32 num_region;
	struct ocotp_region region[];
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

static DEFINE_MUTEX(scu_ocotp_mutex);

static struct ocotp_devtype_data imx8qxp_data = {
	.devtype = IMX8QXP,
	.nregs = 800,
	.num_region = 3,
	.region = {
		{0x10, 0x10f, ECC_REGION},
		{0x110, 0x21F, HOLE_REGION},
		{0x220, 0x31F, ECC_REGION},
	},
};

static struct ocotp_devtype_data imx8qm_data = {
	.devtype = IMX8QM,
	.nregs = 800,
	.num_region = 2,
	.region = {
		{0x10, 0x10f, ECC_REGION},
		{0x1a0, 0x1ff, ECC_REGION},
	},
};

static bool in_hole(void *context, u32 index)
{
	struct ocotp_priv *priv = context;
	const struct ocotp_devtype_data *data = priv->data;
	int i;

	for (i = 0; i < data->num_region; i++) {
		if (data->region[i].flag & HOLE_REGION) {
			if ((index >= data->region[i].start) &&
			    (index <= data->region[i].end))
				return true;
		}
	}

	return false;
}

static bool in_ecc(void *context, u32 index)
{
	struct ocotp_priv *priv = context;
	const struct ocotp_devtype_data *data = priv->data;
	int i;

	for (i = 0; i < data->num_region; i++) {
		if (data->region[i].flag & ECC_REGION) {
			if ((index >= data->region[i].start) &&
			    (index <= data->region[i].end))
				return true;
		}
	}

	return false;
}

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

	mutex_lock(&scu_ocotp_mutex);

	buf = p;

	for (i = index; i < (index + count); i++) {
		if (in_hole(context, i)) {
			*buf++ = 0;
			continue;
		}

		ret = imx_sc_misc_otp_fuse_read(priv->nvmem_ipc, i, buf);
		if (ret) {
			mutex_unlock(&scu_ocotp_mutex);
			kfree(p);
			return ret;
		}
		buf++;
	}

	memcpy(val, (u8 *)p + offset % 4, bytes);

	mutex_unlock(&scu_ocotp_mutex);

	kfree(p);

	return 0;
}

static int imx_scu_ocotp_write(void *context, unsigned int offset,
			       void *val, size_t bytes)
{
	struct ocotp_priv *priv = context;
	struct arm_smccc_res res;
	u32 *buf = val;
	u32 tmp;
	u32 index;
	int ret;

	/* allow only writing one complete OTP word at a time */
	if ((bytes != 4) || (offset % 4))
		return -EINVAL;

	index = offset >> 2;

	if (in_hole(context, index))
		return -EINVAL;

	if (in_ecc(context, index)) {
		pr_warn("ECC region, only program once\n");
		mutex_lock(&scu_ocotp_mutex);
		ret = imx_sc_misc_otp_fuse_read(priv->nvmem_ipc, index, &tmp);
		mutex_unlock(&scu_ocotp_mutex);
		if (ret)
			return ret;
		if (tmp) {
			pr_warn("ECC region, already has value: %x\n", tmp);
			return -EIO;
		}
	}

	mutex_lock(&scu_ocotp_mutex);

	arm_smccc_smc(IMX_SIP_OTP, IMX_SIP_OTP_WRITE, index, *buf,
		      0, 0, 0, 0, &res);

	mutex_unlock(&scu_ocotp_mutex);

	return res.a0;
}

static struct nvmem_config imx_scu_ocotp_nvmem_config = {
	.name = "imx-scu-ocotp",
	.read_only = false,
	.word_size = 4,
	.stride = 1,
	.owner = THIS_MODULE,
	.reg_read = imx_scu_ocotp_read,
	.reg_write = imx_scu_ocotp_write,
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
