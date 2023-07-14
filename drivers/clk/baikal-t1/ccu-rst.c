// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 * Baikal-T1 CCU Resets interface driver
 */

#define pr_fmt(fmt) "bt1-ccu-rst: " fmt

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include <dt-bindings/reset/bt1-ccu.h>

#include "ccu-rst.h"

#define CCU_AXI_MAIN_BASE		0x030
#define CCU_AXI_DDR_BASE		0x034
#define CCU_AXI_SATA_BASE		0x038
#define CCU_AXI_GMAC0_BASE		0x03C
#define CCU_AXI_GMAC1_BASE		0x040
#define CCU_AXI_XGMAC_BASE		0x044
#define CCU_AXI_PCIE_M_BASE		0x048
#define CCU_AXI_PCIE_S_BASE		0x04C
#define CCU_AXI_USB_BASE		0x050
#define CCU_AXI_HWA_BASE		0x054
#define CCU_AXI_SRAM_BASE		0x058

#define CCU_SYS_DDR_BASE		0x02c
#define CCU_SYS_SATA_REF_BASE		0x060
#define CCU_SYS_APB_BASE		0x064
#define CCU_SYS_PCIE_BASE		0x144

#define CCU_RST_DELAY_US		1

#define CCU_RST_TRIG(_base, _ofs)		\
	{					\
		.type = CCU_RST_TRIG,		\
		.base = _base,			\
		.mask = BIT(_ofs),		\
	}

#define CCU_RST_DIR(_base, _ofs)		\
	{					\
		.type = CCU_RST_DIR,		\
		.base = _base,			\
		.mask = BIT(_ofs),		\
	}

struct ccu_rst_info {
	enum ccu_rst_type type;
	unsigned int base;
	unsigned int mask;
};

/*
 * Each AXI-bus clock divider is equipped with the corresponding clock-consumer
 * domain reset (it's self-deasserted reset control).
 */
static const struct ccu_rst_info axi_rst_info[] = {
	[CCU_AXI_MAIN_RST] = CCU_RST_TRIG(CCU_AXI_MAIN_BASE, 1),
	[CCU_AXI_DDR_RST] = CCU_RST_TRIG(CCU_AXI_DDR_BASE, 1),
	[CCU_AXI_SATA_RST] = CCU_RST_TRIG(CCU_AXI_SATA_BASE, 1),
	[CCU_AXI_GMAC0_RST] = CCU_RST_TRIG(CCU_AXI_GMAC0_BASE, 1),
	[CCU_AXI_GMAC1_RST] = CCU_RST_TRIG(CCU_AXI_GMAC1_BASE, 1),
	[CCU_AXI_XGMAC_RST] = CCU_RST_TRIG(CCU_AXI_XGMAC_BASE, 1),
	[CCU_AXI_PCIE_M_RST] = CCU_RST_TRIG(CCU_AXI_PCIE_M_BASE, 1),
	[CCU_AXI_PCIE_S_RST] = CCU_RST_TRIG(CCU_AXI_PCIE_S_BASE, 1),
	[CCU_AXI_USB_RST] = CCU_RST_TRIG(CCU_AXI_USB_BASE, 1),
	[CCU_AXI_HWA_RST] = CCU_RST_TRIG(CCU_AXI_HWA_BASE, 1),
	[CCU_AXI_SRAM_RST] = CCU_RST_TRIG(CCU_AXI_SRAM_BASE, 1),
};

/*
 * SATA reference clock domain and APB-bus domain are connected with the
 * sefl-deasserted reset control, which can be activated via the corresponding
 * clock divider register. DDR and PCIe sub-domains can be reset with directly
 * controlled reset signals. Resetting the DDR controller though won't end up
 * well while the Linux kernel is working.
 */
static const struct ccu_rst_info sys_rst_info[] = {
	[CCU_SYS_SATA_REF_RST] = CCU_RST_TRIG(CCU_SYS_SATA_REF_BASE, 1),
	[CCU_SYS_APB_RST] = CCU_RST_TRIG(CCU_SYS_APB_BASE, 1),
	[CCU_SYS_DDR_FULL_RST] = CCU_RST_DIR(CCU_SYS_DDR_BASE, 1),
	[CCU_SYS_DDR_INIT_RST] = CCU_RST_DIR(CCU_SYS_DDR_BASE, 2),
	[CCU_SYS_PCIE_PCS_PHY_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 0),
	[CCU_SYS_PCIE_PIPE0_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 4),
	[CCU_SYS_PCIE_CORE_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 8),
	[CCU_SYS_PCIE_PWR_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 9),
	[CCU_SYS_PCIE_STICKY_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 10),
	[CCU_SYS_PCIE_NSTICKY_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 11),
	[CCU_SYS_PCIE_HOT_RST] = CCU_RST_DIR(CCU_SYS_PCIE_BASE, 12),
};

static int ccu_rst_reset(struct reset_controller_dev *rcdev, unsigned long idx)
{
	struct ccu_rst *rst = to_ccu_rst(rcdev);
	const struct ccu_rst_info *info = &rst->rsts_info[idx];

	if (info->type != CCU_RST_TRIG)
		return -EOPNOTSUPP;

	regmap_update_bits(rst->sys_regs, info->base, info->mask, info->mask);

	/* The next delay must be enough to cover all the resets. */
	udelay(CCU_RST_DELAY_US);

	return 0;
}

static int ccu_rst_set(struct reset_controller_dev *rcdev,
		       unsigned long idx, bool high)
{
	struct ccu_rst *rst = to_ccu_rst(rcdev);
	const struct ccu_rst_info *info = &rst->rsts_info[idx];

	if (info->type != CCU_RST_DIR)
		return high ? -EOPNOTSUPP : 0;

	return regmap_update_bits(rst->sys_regs, info->base,
				  info->mask, high ? info->mask : 0);
}

static int ccu_rst_assert(struct reset_controller_dev *rcdev,
			  unsigned long idx)
{
	return ccu_rst_set(rcdev, idx, true);
}

static int ccu_rst_deassert(struct reset_controller_dev *rcdev,
			    unsigned long idx)
{
	return ccu_rst_set(rcdev, idx, false);
}

static int ccu_rst_status(struct reset_controller_dev *rcdev,
			  unsigned long idx)
{
	struct ccu_rst *rst = to_ccu_rst(rcdev);
	const struct ccu_rst_info *info = &rst->rsts_info[idx];
	u32 val;

	if (info->type != CCU_RST_DIR)
		return -EOPNOTSUPP;

	regmap_read(rst->sys_regs, info->base, &val);

	return !!(val & info->mask);
}

static const struct reset_control_ops ccu_rst_ops = {
	.reset = ccu_rst_reset,
	.assert = ccu_rst_assert,
	.deassert = ccu_rst_deassert,
	.status = ccu_rst_status,
};

struct ccu_rst *ccu_rst_hw_register(const struct ccu_rst_init_data *rst_init)
{
	struct ccu_rst *rst;
	int ret;

	if (!rst_init)
		return ERR_PTR(-EINVAL);

	rst = kzalloc(sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return ERR_PTR(-ENOMEM);

	rst->sys_regs = rst_init->sys_regs;
	if (of_device_is_compatible(rst_init->np, "baikal,bt1-ccu-axi")) {
		rst->rcdev.nr_resets = ARRAY_SIZE(axi_rst_info);
		rst->rsts_info = axi_rst_info;
	} else if (of_device_is_compatible(rst_init->np, "baikal,bt1-ccu-sys")) {
		rst->rcdev.nr_resets = ARRAY_SIZE(sys_rst_info);
		rst->rsts_info = sys_rst_info;
	} else {
		pr_err("Incompatible DT node '%s' specified\n",
		       of_node_full_name(rst_init->np));
		ret = -EINVAL;
		goto err_kfree_rst;
	}

	rst->rcdev.owner = THIS_MODULE;
	rst->rcdev.ops = &ccu_rst_ops;
	rst->rcdev.of_node = rst_init->np;

	ret = reset_controller_register(&rst->rcdev);
	if (ret) {
		pr_err("Couldn't register '%s' reset controller\n",
		       of_node_full_name(rst_init->np));
		goto err_kfree_rst;
	}

	return rst;

err_kfree_rst:
	kfree(rst);

	return ERR_PTR(ret);
}

void ccu_rst_hw_unregister(struct ccu_rst *rst)
{
	reset_controller_unregister(&rst->rcdev);

	kfree(rst);
}
