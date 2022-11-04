// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *   Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * Baikal-T1 CCU Dividers clock driver
 */

#define pr_fmt(fmt) "bt1-ccu-div: " fmt

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/ioport.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/bt1-ccu.h>

#include "ccu-div.h"
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

#define CCU_SYS_SATA_REF_BASE		0x060
#define CCU_SYS_APB_BASE		0x064
#define CCU_SYS_GMAC0_BASE		0x068
#define CCU_SYS_GMAC1_BASE		0x06C
#define CCU_SYS_XGMAC_BASE		0x070
#define CCU_SYS_USB_BASE		0x074
#define CCU_SYS_PVT_BASE		0x078
#define CCU_SYS_HWA_BASE		0x07C
#define CCU_SYS_UART_BASE		0x084
#define CCU_SYS_TIMER0_BASE		0x088
#define CCU_SYS_TIMER1_BASE		0x08C
#define CCU_SYS_TIMER2_BASE		0x090
#define CCU_SYS_WDT_BASE		0x150

#define CCU_DIV_VAR_INFO(_id, _name, _pname, _base, _width, _flags, _features) \
	{								\
		.id = _id,						\
		.name = _name,						\
		.parent_name = _pname,					\
		.base = _base,						\
		.type = CCU_DIV_VAR,					\
		.width = _width,					\
		.flags = _flags,					\
		.features = _features					\
	}

#define CCU_DIV_GATE_INFO(_id, _name, _pname, _base, _divider)	\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _pname,				\
		.base = _base,					\
		.type = CCU_DIV_GATE,				\
		.divider = _divider				\
	}

#define CCU_DIV_BUF_INFO(_id, _name, _pname, _base, _flags)	\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _pname,				\
		.base = _base,					\
		.type = CCU_DIV_BUF,				\
		.flags = _flags					\
	}

#define CCU_DIV_FIXED_INFO(_id, _name, _pname, _divider)	\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _pname,				\
		.type = CCU_DIV_FIXED,				\
		.divider = _divider				\
	}

struct ccu_div_info {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned int base;
	enum ccu_div_type type;
	union {
		unsigned int width;
		unsigned int divider;
	};
	unsigned long flags;
	unsigned long features;
};

struct ccu_div_data {
	struct device_node *np;
	struct regmap *sys_regs;

	unsigned int divs_num;
	const struct ccu_div_info *divs_info;
	struct ccu_div **divs;

	struct ccu_rst *rsts;
};

/*
 * AXI Main Interconnect (axi_main_clk) and DDR AXI-bus (axi_ddr_clk) clocks
 * must be left enabled in any case, since former one is responsible for
 * clocking a bus between CPU cores and the rest of the SoC components, while
 * the later is clocking the AXI-bus between DDR controller and the Main
 * Interconnect. So should any of these clocks get to be disabled, the system
 * will literally stop working. That's why we marked them as critical.
 */
static const struct ccu_div_info axi_info[] = {
	CCU_DIV_VAR_INFO(CCU_AXI_MAIN_CLK, "axi_main_clk", "pcie_clk",
			 CCU_AXI_MAIN_BASE, 4,
			 CLK_IS_CRITICAL, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_DDR_CLK, "axi_ddr_clk", "sata_clk",
			 CCU_AXI_DDR_BASE, 4,
			 CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
			 CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_SATA_CLK, "axi_sata_clk", "sata_clk",
			 CCU_AXI_SATA_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_GMAC0_CLK, "axi_gmac0_clk", "eth_clk",
			 CCU_AXI_GMAC0_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_GMAC1_CLK, "axi_gmac1_clk", "eth_clk",
			 CCU_AXI_GMAC1_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_XGMAC_CLK, "axi_xgmac_clk", "eth_clk",
			 CCU_AXI_XGMAC_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_PCIE_M_CLK, "axi_pcie_m_clk", "pcie_clk",
			 CCU_AXI_PCIE_M_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_PCIE_S_CLK, "axi_pcie_s_clk", "pcie_clk",
			 CCU_AXI_PCIE_S_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_USB_CLK, "axi_usb_clk", "sata_clk",
			 CCU_AXI_USB_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_HWA_CLK, "axi_hwa_clk", "sata_clk",
			 CCU_AXI_HWA_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN),
	CCU_DIV_VAR_INFO(CCU_AXI_SRAM_CLK, "axi_sram_clk", "eth_clk",
			 CCU_AXI_SRAM_BASE, 4,
			 CLK_SET_RATE_GATE, CCU_DIV_RESET_DOMAIN)
};

/*
 * APB-bus clock is marked as critical since it's a main communication bus
 * for the SoC devices registers IO-operations.
 */
static const struct ccu_div_info sys_info[] = {
	CCU_DIV_VAR_INFO(CCU_SYS_SATA_CLK, "sys_sata_clk",
			 "sata_clk", CCU_SYS_SATA_REF_BASE, 4,
			 CLK_SET_RATE_GATE,
			 CCU_DIV_SKIP_ONE | CCU_DIV_LOCK_SHIFTED |
			 CCU_DIV_RESET_DOMAIN),
	CCU_DIV_BUF_INFO(CCU_SYS_SATA_REF_CLK, "sys_sata_ref_clk",
			 "sys_sata_clk", CCU_SYS_SATA_REF_BASE,
			 CLK_SET_RATE_PARENT),
	CCU_DIV_VAR_INFO(CCU_SYS_APB_CLK, "sys_apb_clk",
			 "pcie_clk", CCU_SYS_APB_BASE, 5,
			 CLK_IS_CRITICAL, CCU_DIV_BASIC | CCU_DIV_RESET_DOMAIN),
	CCU_DIV_GATE_INFO(CCU_SYS_GMAC0_TX_CLK, "sys_gmac0_tx_clk",
			  "eth_clk", CCU_SYS_GMAC0_BASE, 5),
	CCU_DIV_FIXED_INFO(CCU_SYS_GMAC0_PTP_CLK, "sys_gmac0_ptp_clk",
			   "eth_clk", 10),
	CCU_DIV_GATE_INFO(CCU_SYS_GMAC1_TX_CLK, "sys_gmac1_tx_clk",
			  "eth_clk", CCU_SYS_GMAC1_BASE, 5),
	CCU_DIV_FIXED_INFO(CCU_SYS_GMAC1_PTP_CLK, "sys_gmac1_ptp_clk",
			   "eth_clk", 10),
	CCU_DIV_GATE_INFO(CCU_SYS_XGMAC_CLK, "sys_xgmac_clk",
			  "eth_clk", CCU_SYS_XGMAC_BASE, 1),
	CCU_DIV_FIXED_INFO(CCU_SYS_XGMAC_REF_CLK, "sys_xgmac_ref_clk",
			   "sys_xgmac_clk", 8),
	CCU_DIV_FIXED_INFO(CCU_SYS_XGMAC_PTP_CLK, "sys_xgmac_ptp_clk",
			   "sys_xgmac_clk", 8),
	CCU_DIV_GATE_INFO(CCU_SYS_USB_CLK, "sys_usb_clk",
			  "eth_clk", CCU_SYS_USB_BASE, 10),
	CCU_DIV_VAR_INFO(CCU_SYS_PVT_CLK, "sys_pvt_clk",
			 "ref_clk", CCU_SYS_PVT_BASE, 5,
			 CLK_SET_RATE_GATE, 0),
	CCU_DIV_VAR_INFO(CCU_SYS_HWA_CLK, "sys_hwa_clk",
			 "sata_clk", CCU_SYS_HWA_BASE, 4,
			 CLK_SET_RATE_GATE, 0),
	CCU_DIV_VAR_INFO(CCU_SYS_UART_CLK, "sys_uart_clk",
			 "eth_clk", CCU_SYS_UART_BASE, 17,
			 CLK_SET_RATE_GATE, 0),
	CCU_DIV_FIXED_INFO(CCU_SYS_I2C1_CLK, "sys_i2c1_clk",
			   "eth_clk", 10),
	CCU_DIV_FIXED_INFO(CCU_SYS_I2C2_CLK, "sys_i2c2_clk",
			   "eth_clk", 10),
	CCU_DIV_FIXED_INFO(CCU_SYS_GPIO_CLK, "sys_gpio_clk",
			   "ref_clk", 25),
	CCU_DIV_VAR_INFO(CCU_SYS_TIMER0_CLK, "sys_timer0_clk",
			 "ref_clk", CCU_SYS_TIMER0_BASE, 17,
			 CLK_SET_RATE_GATE, CCU_DIV_BASIC),
	CCU_DIV_VAR_INFO(CCU_SYS_TIMER1_CLK, "sys_timer1_clk",
			 "ref_clk", CCU_SYS_TIMER1_BASE, 17,
			 CLK_SET_RATE_GATE, CCU_DIV_BASIC),
	CCU_DIV_VAR_INFO(CCU_SYS_TIMER2_CLK, "sys_timer2_clk",
			 "ref_clk", CCU_SYS_TIMER2_BASE, 17,
			 CLK_SET_RATE_GATE, CCU_DIV_BASIC),
	CCU_DIV_VAR_INFO(CCU_SYS_WDT_CLK, "sys_wdt_clk",
			 "eth_clk", CCU_SYS_WDT_BASE, 17,
			 CLK_SET_RATE_GATE, CCU_DIV_SKIP_ONE_TO_THREE)
};

static struct ccu_div_data *axi_data;
static struct ccu_div_data *sys_data;

static void ccu_div_set_data(struct ccu_div_data *data)
{
	struct device_node *np = data->np;

	if (of_device_is_compatible(np, "baikal,bt1-ccu-axi"))
		axi_data = data;
	else if (of_device_is_compatible(np, "baikal,bt1-ccu-sys"))
		sys_data = data;
	else
		pr_err("Invalid DT node '%s' specified\n", of_node_full_name(np));
}

static struct ccu_div_data *ccu_div_get_data(struct device_node *np)
{
	if (of_device_is_compatible(np, "baikal,bt1-ccu-axi"))
		return axi_data;
	else if (of_device_is_compatible(np, "baikal,bt1-ccu-sys"))
		return sys_data;

	pr_err("Invalid DT node '%s' specified\n", of_node_full_name(np));

	return NULL;
}

static struct ccu_div *ccu_div_find_desc(struct ccu_div_data *data,
					 unsigned int clk_id)
{
	int idx;

	for (idx = 0; idx < data->divs_num; ++idx) {
		if (data->divs_info[idx].id == clk_id)
			return data->divs[idx];
	}

	return ERR_PTR(-EINVAL);
}

static struct ccu_div_data *ccu_div_create_data(struct device_node *np)
{
	struct ccu_div_data *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->np = np;
	if (of_device_is_compatible(np, "baikal,bt1-ccu-axi")) {
		data->divs_num = ARRAY_SIZE(axi_info);
		data->divs_info = axi_info;
	} else if (of_device_is_compatible(np, "baikal,bt1-ccu-sys")) {
		data->divs_num = ARRAY_SIZE(sys_info);
		data->divs_info = sys_info;
	} else {
		pr_err("Incompatible DT node '%s' specified\n",
			of_node_full_name(np));
		ret = -EINVAL;
		goto err_kfree_data;
	}

	data->divs = kcalloc(data->divs_num, sizeof(*data->divs), GFP_KERNEL);
	if (!data->divs) {
		ret = -ENOMEM;
		goto err_kfree_data;
	}

	return data;

err_kfree_data:
	kfree(data);

	return ERR_PTR(ret);
}

static void ccu_div_free_data(struct ccu_div_data *data)
{
	kfree(data->divs);

	kfree(data);
}

static int ccu_div_find_sys_regs(struct ccu_div_data *data)
{
	data->sys_regs = syscon_node_to_regmap(data->np->parent);
	if (IS_ERR(data->sys_regs)) {
		pr_err("Failed to find syscon regs for '%s'\n",
			of_node_full_name(data->np));
		return PTR_ERR(data->sys_regs);
	}

	return 0;
}

static struct clk_hw *ccu_div_of_clk_hw_get(struct of_phandle_args *clkspec,
					    void *priv)
{
	struct ccu_div_data *data = priv;
	struct ccu_div *div;
	unsigned int clk_id;

	clk_id = clkspec->args[0];
	div = ccu_div_find_desc(data, clk_id);
	if (IS_ERR(div)) {
		if (div != ERR_PTR(-EPROBE_DEFER))
			pr_info("Invalid clock ID %d specified\n", clk_id);

		return ERR_CAST(div);
	}

	return ccu_div_get_clk_hw(div);
}

static int ccu_div_clk_register(struct ccu_div_data *data, bool defer)
{
	int idx, ret;

	for (idx = 0; idx < data->divs_num; ++idx) {
		const struct ccu_div_info *info = &data->divs_info[idx];
		struct ccu_div_init_data init = {0};

		if (!!(info->features & CCU_DIV_BASIC) ^ defer) {
			if (!data->divs[idx])
				data->divs[idx] = ERR_PTR(-EPROBE_DEFER);

			continue;
		}

		init.id = info->id;
		init.name = info->name;
		init.parent_name = info->parent_name;
		init.np = data->np;
		init.type = info->type;
		init.flags = info->flags;
		init.features = info->features;

		if (init.type == CCU_DIV_VAR) {
			init.base = info->base;
			init.sys_regs = data->sys_regs;
			init.width = info->width;
		} else if (init.type == CCU_DIV_GATE) {
			init.base = info->base;
			init.sys_regs = data->sys_regs;
			init.divider = info->divider;
		} else if (init.type == CCU_DIV_BUF) {
			init.base = info->base;
			init.sys_regs = data->sys_regs;
		} else {
			init.divider = info->divider;
		}

		data->divs[idx] = ccu_div_hw_register(&init);
		if (IS_ERR(data->divs[idx])) {
			ret = PTR_ERR(data->divs[idx]);
			pr_err("Couldn't register divider '%s' hw\n",
				init.name);
			goto err_hw_unregister;
		}
	}

	return 0;

err_hw_unregister:
	for (--idx; idx >= 0; --idx) {
		if (!!(data->divs_info[idx].features & CCU_DIV_BASIC) ^ defer)
			continue;

		ccu_div_hw_unregister(data->divs[idx]);
	}

	return ret;
}

static void ccu_div_clk_unregister(struct ccu_div_data *data, bool defer)
{
	int idx;

	/* Uninstall only the clocks registered on the specfied stage */
	for (idx = 0; idx < data->divs_num; ++idx) {
		if (!!(data->divs_info[idx].features & CCU_DIV_BASIC) ^ defer)
			continue;

		ccu_div_hw_unregister(data->divs[idx]);
	}
}

static int ccu_div_of_register(struct ccu_div_data *data)
{
	int ret;

	ret = of_clk_add_hw_provider(data->np, ccu_div_of_clk_hw_get, data);
	if (ret) {
		pr_err("Couldn't register dividers '%s' clock provider\n",
		       of_node_full_name(data->np));
	}

	return ret;
}

static int ccu_div_rst_register(struct ccu_div_data *data)
{
	struct ccu_rst_init_data init = {0};

	init.sys_regs = data->sys_regs;
	init.np = data->np;

	data->rsts = ccu_rst_hw_register(&init);
	if (IS_ERR(data->rsts)) {
		pr_err("Couldn't register divider '%s' reset controller\n",
			of_node_full_name(data->np));
		return PTR_ERR(data->rsts);
	}

	return 0;
}

static int ccu_div_probe(struct platform_device *pdev)
{
	struct ccu_div_data *data;
	int ret;

	data = ccu_div_get_data(dev_of_node(&pdev->dev));
	if (!data)
		return -EINVAL;

	ret = ccu_div_clk_register(data, false);
	if (ret)
		return ret;

	ret = ccu_div_rst_register(data);
	if (ret)
		goto err_clk_unregister;

	return 0;

err_clk_unregister:
	ccu_div_clk_unregister(data, false);

	return ret;
}

static const struct of_device_id ccu_div_of_match[] = {
	{ .compatible = "baikal,bt1-ccu-axi" },
	{ .compatible = "baikal,bt1-ccu-sys" },
	{ }
};

static struct platform_driver ccu_div_driver = {
	.probe  = ccu_div_probe,
	.driver = {
		.name = "clk-ccu-div",
		.of_match_table = ccu_div_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ccu_div_driver);

static __init void ccu_div_init(struct device_node *np)
{
	struct ccu_div_data *data;
	int ret;

	data = ccu_div_create_data(np);
	if (IS_ERR(data))
		return;

	ret = ccu_div_find_sys_regs(data);
	if (ret)
		goto err_free_data;

	ret = ccu_div_clk_register(data, true);
	if (ret)
		goto err_free_data;

	ret = ccu_div_of_register(data);
	if (ret)
		goto err_clk_unregister;

	ccu_div_set_data(data);

	return;

err_clk_unregister:
	ccu_div_clk_unregister(data, true);

err_free_data:
	ccu_div_free_data(data);
}
CLK_OF_DECLARE_DRIVER(ccu_axi, "baikal,bt1-ccu-axi", ccu_div_init);
CLK_OF_DECLARE_DRIVER(ccu_sys, "baikal,bt1-ccu-sys", ccu_div_init);
