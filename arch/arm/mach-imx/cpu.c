// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include "hardware.h"
#include "common.h"

#define OCOTP_UID_H	0x420
#define OCOTP_UID_L	0x410

unsigned int __mxc_cpu_type;
static unsigned int imx_soc_revision;

void mxc_set_cpu_type(unsigned int type)
{
	__mxc_cpu_type = type;
}

void imx_set_soc_revision(unsigned int rev)
{
	imx_soc_revision = rev;
}

unsigned int imx_get_soc_revision(void)
{
	return imx_soc_revision;
}

void imx_print_silicon_rev(const char *cpu, int srev)
{
	if (srev == IMX_CHIP_REVISION_UNKNOWN)
		pr_info("CPU identified as %s, unknown revision\n", cpu);
	else
		pr_info("CPU identified as %s, silicon rev %d.%d\n",
				cpu, (srev >> 4) & 0xf, srev & 0xf);
}

void __init imx_set_aips(void __iomem *base)
{
	unsigned int reg;
/*
 * Set all MPROTx to be non-bufferable, trusted for R/W,
 * not forced to user-mode.
 */
	imx_writel(0x77777777, base + 0x0);
	imx_writel(0x77777777, base + 0x4);

/*
 * Set all OPACRx to be non-bufferable, to not require
 * supervisor privilege level for access, allow for
 * write access and untrusted master access.
 */
	imx_writel(0x0, base + 0x40);
	imx_writel(0x0, base + 0x44);
	imx_writel(0x0, base + 0x48);
	imx_writel(0x0, base + 0x4C);
	reg = imx_readl(base + 0x50) & 0x00FFFFFF;
	imx_writel(reg, base + 0x50);
}

void __init imx_aips_allow_unprivileged_access(
		const char *compat)
{
	void __iomem *aips_base_addr;
	struct device_node *np;

	for_each_compatible_node(np, NULL, compat) {
		aips_base_addr = of_iomap(np, 0);
		WARN_ON(!aips_base_addr);
		imx_set_aips(aips_base_addr);
	}
}

struct device * __init imx_soc_device_init(void)
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

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return NULL;

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
		soc_id = "i.MX7ULP";
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
		regmap_read(ocotp, OCOTP_UID_H, &val);
		soc_uid = val;
		regmap_read(ocotp, OCOTP_UID_L, &val);
		soc_uid <<= 32;
		soc_uid |= val;
	}

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d",
					   (imx_soc_revision >> 4) & 0xf,
					   imx_soc_revision & 0xf);
	if (!soc_dev_attr->revision)
		goto free_soc;

	soc_dev_attr->serial_number = kasprintf(GFP_KERNEL, "%016llX", soc_uid);
	if (!soc_dev_attr->serial_number)
		goto free_rev;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		goto free_serial_number;

	return soc_device_to_device(soc_dev);

free_serial_number:
	kfree(soc_dev_attr->serial_number);
free_rev:
	kfree(soc_dev_attr->revision);
free_soc:
	kfree(soc_dev_attr);
	return NULL;
}
