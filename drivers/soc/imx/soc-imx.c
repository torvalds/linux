// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 NXP
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <soc/imx/cpu.h>
#include <soc/imx/revision.h>

#define OCOTP_UID_H	0x420
#define OCOTP_UID_L	0x410

#define OCOTP_ULP_UID_1		0x4b0
#define OCOTP_ULP_UID_2		0x4c0
#define OCOTP_ULP_UID_3		0x4d0
#define OCOTP_ULP_UID_4		0x4e0

static int __init imx_soc_device_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	const char *ocotp_compat = NULL;
	struct soc_device *soc_dev;
	struct device_node *root;
	struct regmap *ocotp = NULL;
	const char *soc_id;
	u64 soc_uid = 0;
	u32 val;
	int ret;

	if (of_machine_is_compatible("fsl,ls1021a"))
		return 0;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Freescale i.MX";

	root = of_find_node_by_path("/");
	ret = of_property_read_string(root, "model", &soc_dev_attr->machine);
	of_node_put(root);
	if (ret)
		goto free_soc;

	switch (__mxc_cpu_type) {
	case MXC_CPU_MX1:
		soc_id = "i.MX1";
		break;
	case MXC_CPU_MX21:
		soc_id = "i.MX21";
		break;
	case MXC_CPU_MX25:
		soc_id = "i.MX25";
		break;
	case MXC_CPU_MX27:
		soc_id = "i.MX27";
		break;
	case MXC_CPU_MX31:
		soc_id = "i.MX31";
		break;
	case MXC_CPU_MX35:
		soc_id = "i.MX35";
		break;
	case MXC_CPU_MX51:
		soc_id = "i.MX51";
		break;
	case MXC_CPU_MX53:
		soc_id = "i.MX53";
		break;
	case MXC_CPU_IMX6SL:
		ocotp_compat = "fsl,imx6sl-ocotp";
		soc_id = "i.MX6SL";
		break;
	case MXC_CPU_IMX6DL:
		ocotp_compat = "fsl,imx6q-ocotp";
		soc_id = "i.MX6DL";
		break;
	case MXC_CPU_IMX6SX:
		ocotp_compat = "fsl,imx6sx-ocotp";
		soc_id = "i.MX6SX";
		break;
	case MXC_CPU_IMX6Q:
		ocotp_compat = "fsl,imx6q-ocotp";
		soc_id = "i.MX6Q";
		break;
	case MXC_CPU_IMX6UL:
		ocotp_compat = "fsl,imx6ul-ocotp";
		soc_id = "i.MX6UL";
		break;
	case MXC_CPU_IMX6ULL:
		ocotp_compat = "fsl,imx6ull-ocotp";
		soc_id = "i.MX6ULL";
		break;
	case MXC_CPU_IMX6ULZ:
		ocotp_compat = "fsl,imx6ull-ocotp";
		soc_id = "i.MX6ULZ";
		break;
	case MXC_CPU_IMX6SLL:
		ocotp_compat = "fsl,imx6sll-ocotp";
		soc_id = "i.MX6SLL";
		break;
	case MXC_CPU_IMX7D:
		ocotp_compat = "fsl,imx7d-ocotp";
		soc_id = "i.MX7D";
		break;
	case MXC_CPU_IMX7ULP:
		ocotp_compat = "fsl,imx7ulp-ocotp";
		soc_id = "i.MX7ULP";
		break;
	case MXC_CPU_VF500:
		ocotp_compat = "fsl,vf610-ocotp";
		soc_id = "VF500";
		break;
	case MXC_CPU_VF510:
		ocotp_compat = "fsl,vf610-ocotp";
		soc_id = "VF510";
		break;
	case MXC_CPU_VF600:
		ocotp_compat = "fsl,vf610-ocotp";
		soc_id = "VF600";
		break;
	case MXC_CPU_VF610:
		ocotp_compat = "fsl,vf610-ocotp";
		soc_id = "VF610";
		break;
	default:
		soc_id = "Unknown";
	}
	soc_dev_attr->soc_id = soc_id;

	if (ocotp_compat) {
		ocotp = syscon_regmap_lookup_by_compatible(ocotp_compat);
		if (IS_ERR(ocotp))
			pr_err("%s: failed to find %s regmap!\n", __func__, ocotp_compat);
	}

	if (!IS_ERR_OR_NULL(ocotp)) {
		if (__mxc_cpu_type == MXC_CPU_IMX7ULP) {
			regmap_read(ocotp, OCOTP_ULP_UID_4, &val);
			soc_uid = val & 0xffff;
			regmap_read(ocotp, OCOTP_ULP_UID_3, &val);
			soc_uid <<= 16;
			soc_uid |= val & 0xffff;
			regmap_read(ocotp, OCOTP_ULP_UID_2, &val);
			soc_uid <<= 16;
			soc_uid |= val & 0xffff;
			regmap_read(ocotp, OCOTP_ULP_UID_1, &val);
			soc_uid <<= 16;
			soc_uid |= val & 0xffff;
		} else {
			regmap_read(ocotp, OCOTP_UID_H, &val);
			soc_uid = val;
			regmap_read(ocotp, OCOTP_UID_L, &val);
			soc_uid <<= 32;
			soc_uid |= val;
		}
	}

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d",
					   (imx_get_soc_revision() >> 4) & 0xf,
					   imx_get_soc_revision() & 0xf);
	if (!soc_dev_attr->revision) {
		ret = -ENOMEM;
		goto free_soc;
	}

	soc_dev_attr->serial_number = kasprintf(GFP_KERNEL, "%016llX", soc_uid);
	if (!soc_dev_attr->serial_number) {
		ret = -ENOMEM;
		goto free_rev;
	}

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto free_serial_number;
	}

	return 0;

free_serial_number:
	kfree(soc_dev_attr->serial_number);
free_rev:
	kfree(soc_dev_attr->revision);
free_soc:
	kfree(soc_dev_attr);
	return ret;
}
device_initcall(imx_soc_device_init);
