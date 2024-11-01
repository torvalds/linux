// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "hardware.h"
#include "common.h"

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
