// SPDX-License-Identifier: GPL-2.0-only
/*
 * Daire McNamara,<daire.mcnamara@microchip.com>
 * Copyright (C) 2020 Microchip Technology Inc.  All rights reserved.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <dt-bindings/clock/microchip,mpfs-clock.h>

/* address offset of control registers */
#define REG_CLOCK_CONFIG_CR	0x08u
#define REG_SUBBLK_CLOCK_CR	0x84u
#define REG_SUBBLK_RESET_CR	0x88u

struct mpfs_clock_data {
	void __iomem *base;
	struct clk_hw_onecell_data hw_data;
};

struct mpfs_cfg_clock {
	const struct clk_div_table *table;
	unsigned int id;
	u8 shift;
	u8 width;
};

struct mpfs_cfg_hw_clock {
	struct mpfs_cfg_clock cfg;
	void __iomem *sys_base;
	struct clk_hw hw;
	struct clk_init_data init;
};

#define to_mpfs_cfg_clk(_hw) container_of(_hw, struct mpfs_cfg_hw_clock, hw)

struct mpfs_periph_clock {
	unsigned int id;
	u8 shift;
};

struct mpfs_periph_hw_clock {
	struct mpfs_periph_clock periph;
	void __iomem *sys_base;
	struct clk_hw hw;
};

#define to_mpfs_periph_clk(_hw) container_of(_hw, struct mpfs_periph_hw_clock, hw)

/*
 * mpfs_clk_lock prevents anything else from writing to the
 * mpfs clk block while a software locked register is being written.
 */
static DEFINE_SPINLOCK(mpfs_clk_lock);

static const struct clk_parent_data mpfs_cfg_parent[] = {
	{ .index = 0 },
};

static const struct clk_div_table mpfs_div_cpu_axi_table[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 4 }, { 3, 8 },
	{ 0, 0 }
};

static const struct clk_div_table mpfs_div_ahb_table[] = {
	{ 1, 2 }, { 2, 4}, { 3, 8 },
	{ 0, 0 }
};

static unsigned long mpfs_cfg_clk_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct mpfs_cfg_hw_clock *cfg_hw = to_mpfs_cfg_clk(hw);
	struct mpfs_cfg_clock *cfg = &cfg_hw->cfg;
	void __iomem *base_addr = cfg_hw->sys_base;
	u32 val;

	val = readl_relaxed(base_addr + REG_CLOCK_CONFIG_CR) >> cfg->shift;
	val &= clk_div_mask(cfg->width);

	return prate / (1u << val);
}

static long mpfs_cfg_clk_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *prate)
{
	struct mpfs_cfg_hw_clock *cfg_hw = to_mpfs_cfg_clk(hw);
	struct mpfs_cfg_clock *cfg = &cfg_hw->cfg;

	return divider_round_rate(hw, rate, prate, cfg->table, cfg->width, 0);
}

static int mpfs_cfg_clk_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long prate)
{
	struct mpfs_cfg_hw_clock *cfg_hw = to_mpfs_cfg_clk(hw);
	struct mpfs_cfg_clock *cfg = &cfg_hw->cfg;
	void __iomem *base_addr = cfg_hw->sys_base;
	unsigned long flags;
	u32 val;
	int divider_setting;

	divider_setting = divider_get_val(rate, prate, cfg->table, cfg->width, 0);

	if (divider_setting < 0)
		return divider_setting;

	spin_lock_irqsave(&mpfs_clk_lock, flags);

	val = readl_relaxed(base_addr + REG_CLOCK_CONFIG_CR);
	val &= ~(clk_div_mask(cfg->width) << cfg_hw->cfg.shift);
	val |= divider_setting << cfg->shift;
	writel_relaxed(val, base_addr + REG_CLOCK_CONFIG_CR);

	spin_unlock_irqrestore(&mpfs_clk_lock, flags);

	return 0;
}

static const struct clk_ops mpfs_clk_cfg_ops = {
	.recalc_rate = mpfs_cfg_clk_recalc_rate,
	.round_rate = mpfs_cfg_clk_round_rate,
	.set_rate = mpfs_cfg_clk_set_rate,
};

#define CLK_CFG(_id, _name, _parent, _shift, _width, _table, _flags) {		\
	.cfg.id = _id,								\
	.cfg.shift = _shift,							\
	.cfg.width = _width,							\
	.cfg.table = _table,							\
	.hw.init = CLK_HW_INIT_PARENTS_DATA(_name, _parent, &mpfs_clk_cfg_ops,	\
					    _flags),				\
}

static struct mpfs_cfg_hw_clock mpfs_cfg_clks[] = {
	CLK_CFG(CLK_CPU, "clk_cpu", mpfs_cfg_parent, 0, 2, mpfs_div_cpu_axi_table, 0),
	CLK_CFG(CLK_AXI, "clk_axi", mpfs_cfg_parent, 2, 2, mpfs_div_cpu_axi_table, 0),
	CLK_CFG(CLK_AHB, "clk_ahb", mpfs_cfg_parent, 4, 2, mpfs_div_ahb_table, 0),
};

static int mpfs_clk_register_cfg(struct device *dev, struct mpfs_cfg_hw_clock *cfg_hw,
				 void __iomem *sys_base)
{
	cfg_hw->sys_base = sys_base;

	return devm_clk_hw_register(dev, &cfg_hw->hw);
}

static int mpfs_clk_register_cfgs(struct device *dev, struct mpfs_cfg_hw_clock *cfg_hws,
				  unsigned int num_clks, struct mpfs_clock_data *data)
{
	void __iomem *sys_base = data->base;
	unsigned int i, id;
	int ret;

	for (i = 0; i < num_clks; i++) {
		struct mpfs_cfg_hw_clock *cfg_hw = &cfg_hws[i];

		ret = mpfs_clk_register_cfg(dev, cfg_hw, sys_base);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register clock id: %d\n",
					     cfg_hw->cfg.id);

		id = cfg_hws[i].cfg.id;
		data->hw_data.hws[id] = &cfg_hw->hw;
	}

	return 0;
}

static int mpfs_periph_clk_enable(struct clk_hw *hw)
{
	struct mpfs_periph_hw_clock *periph_hw = to_mpfs_periph_clk(hw);
	struct mpfs_periph_clock *periph = &periph_hw->periph;
	void __iomem *base_addr = periph_hw->sys_base;
	u32 reg, val;
	unsigned long flags;

	spin_lock_irqsave(&mpfs_clk_lock, flags);

	reg = readl_relaxed(base_addr + REG_SUBBLK_RESET_CR);
	val = reg & ~(1u << periph->shift);
	writel_relaxed(val, base_addr + REG_SUBBLK_RESET_CR);

	reg = readl_relaxed(base_addr + REG_SUBBLK_CLOCK_CR);
	val = reg | (1u << periph->shift);
	writel_relaxed(val, base_addr + REG_SUBBLK_CLOCK_CR);

	spin_unlock_irqrestore(&mpfs_clk_lock, flags);

	return 0;
}

static void mpfs_periph_clk_disable(struct clk_hw *hw)
{
	struct mpfs_periph_hw_clock *periph_hw = to_mpfs_periph_clk(hw);
	struct mpfs_periph_clock *periph = &periph_hw->periph;
	void __iomem *base_addr = periph_hw->sys_base;
	u32 reg, val;
	unsigned long flags;

	spin_lock_irqsave(&mpfs_clk_lock, flags);

	reg = readl_relaxed(base_addr + REG_SUBBLK_RESET_CR);
	val = reg | (1u << periph->shift);
	writel_relaxed(val, base_addr + REG_SUBBLK_RESET_CR);

	reg = readl_relaxed(base_addr + REG_SUBBLK_CLOCK_CR);
	val = reg & ~(1u << periph->shift);
	writel_relaxed(val, base_addr + REG_SUBBLK_CLOCK_CR);

	spin_unlock_irqrestore(&mpfs_clk_lock, flags);
}

static int mpfs_periph_clk_is_enabled(struct clk_hw *hw)
{
	struct mpfs_periph_hw_clock *periph_hw = to_mpfs_periph_clk(hw);
	struct mpfs_periph_clock *periph = &periph_hw->periph;
	void __iomem *base_addr = periph_hw->sys_base;
	u32 reg;

	reg = readl_relaxed(base_addr + REG_SUBBLK_RESET_CR);
	if ((reg & (1u << periph->shift)) == 0u) {
		reg = readl_relaxed(base_addr + REG_SUBBLK_CLOCK_CR);
		if (reg & (1u << periph->shift))
			return 1;
	}

	return 0;
}

static const struct clk_ops mpfs_periph_clk_ops = {
	.enable = mpfs_periph_clk_enable,
	.disable = mpfs_periph_clk_disable,
	.is_enabled = mpfs_periph_clk_is_enabled,
};

#define CLK_PERIPH(_id, _name, _parent, _shift, _flags) {			\
	.periph.id = _id,							\
	.periph.shift = _shift,							\
	.hw.init = CLK_HW_INIT_HW(_name, _parent, &mpfs_periph_clk_ops,		\
				  _flags),					\
}

#define PARENT_CLK(PARENT) (&mpfs_cfg_clks[CLK_##PARENT].hw)

/*
 * Critical clocks:
 * - CLK_ENVM: reserved by hart software services (hss) superloop monitor/m mode interrupt
 *   trap handler
 * - CLK_MMUART0: reserved by the hss
 * - CLK_DDRC: provides clock to the ddr subsystem
 * - CLK_FICx: these provide clocks for sections of the fpga fabric, disabling them would
 *   cause the fabric to go into reset
 */

static struct mpfs_periph_hw_clock mpfs_periph_clks[] = {
	CLK_PERIPH(CLK_ENVM, "clk_periph_envm", PARENT_CLK(AHB), 0, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_MAC0, "clk_periph_mac0", PARENT_CLK(AHB), 1, 0),
	CLK_PERIPH(CLK_MAC1, "clk_periph_mac1", PARENT_CLK(AHB), 2, 0),
	CLK_PERIPH(CLK_MMC, "clk_periph_mmc", PARENT_CLK(AHB), 3, 0),
	CLK_PERIPH(CLK_TIMER, "clk_periph_timer", PARENT_CLK(AHB), 4, 0),
	CLK_PERIPH(CLK_MMUART0, "clk_periph_mmuart0", PARENT_CLK(AHB), 5, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_MMUART1, "clk_periph_mmuart1", PARENT_CLK(AHB), 6, 0),
	CLK_PERIPH(CLK_MMUART2, "clk_periph_mmuart2", PARENT_CLK(AHB), 7, 0),
	CLK_PERIPH(CLK_MMUART3, "clk_periph_mmuart3", PARENT_CLK(AHB), 8, 0),
	CLK_PERIPH(CLK_MMUART4, "clk_periph_mmuart4", PARENT_CLK(AHB), 9, 0),
	CLK_PERIPH(CLK_SPI0, "clk_periph_spi0", PARENT_CLK(AHB), 10, 0),
	CLK_PERIPH(CLK_SPI1, "clk_periph_spi1", PARENT_CLK(AHB), 11, 0),
	CLK_PERIPH(CLK_I2C0, "clk_periph_i2c0", PARENT_CLK(AHB), 12, 0),
	CLK_PERIPH(CLK_I2C1, "clk_periph_i2c1", PARENT_CLK(AHB), 13, 0),
	CLK_PERIPH(CLK_CAN0, "clk_periph_can0", PARENT_CLK(AHB), 14, 0),
	CLK_PERIPH(CLK_CAN1, "clk_periph_can1", PARENT_CLK(AHB), 15, 0),
	CLK_PERIPH(CLK_USB, "clk_periph_usb", PARENT_CLK(AHB), 16, 0),
	CLK_PERIPH(CLK_RTC, "clk_periph_rtc", PARENT_CLK(AHB), 18, 0),
	CLK_PERIPH(CLK_QSPI, "clk_periph_qspi", PARENT_CLK(AHB), 19, 0),
	CLK_PERIPH(CLK_GPIO0, "clk_periph_gpio0", PARENT_CLK(AHB), 20, 0),
	CLK_PERIPH(CLK_GPIO1, "clk_periph_gpio1", PARENT_CLK(AHB), 21, 0),
	CLK_PERIPH(CLK_GPIO2, "clk_periph_gpio2", PARENT_CLK(AHB), 22, 0),
	CLK_PERIPH(CLK_DDRC, "clk_periph_ddrc", PARENT_CLK(AHB), 23, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC0, "clk_periph_fic0", PARENT_CLK(AHB), 24, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC1, "clk_periph_fic1", PARENT_CLK(AHB), 25, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC2, "clk_periph_fic2", PARENT_CLK(AHB), 26, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC3, "clk_periph_fic3", PARENT_CLK(AHB), 27, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_ATHENA, "clk_periph_athena", PARENT_CLK(AHB), 28, 0),
	CLK_PERIPH(CLK_CFM, "clk_periph_cfm", PARENT_CLK(AHB), 29, 0),
};

static int mpfs_clk_register_periph(struct device *dev, struct mpfs_periph_hw_clock *periph_hw,
				    void __iomem *sys_base)
{
	periph_hw->sys_base = sys_base;

	return devm_clk_hw_register(dev, &periph_hw->hw);
}

static int mpfs_clk_register_periphs(struct device *dev, struct mpfs_periph_hw_clock *periph_hws,
				     int num_clks, struct mpfs_clock_data *data)
{
	void __iomem *sys_base = data->base;
	unsigned int i, id;
	int ret;

	for (i = 0; i < num_clks; i++) {
		struct mpfs_periph_hw_clock *periph_hw = &periph_hws[i];

		ret = mpfs_clk_register_periph(dev, periph_hw, sys_base);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register clock id: %d\n",
					     periph_hw->periph.id);

		id = periph_hws[i].periph.id;
		data->hw_data.hws[id] = &periph_hw->hw;
	}

	return 0;
}

static int mpfs_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_clock_data *clk_data;
	unsigned int num_clks;
	int ret;

	/* CLK_RESERVED is not part of cfg_clks nor periph_clks, so add 1 */
	num_clks = ARRAY_SIZE(mpfs_cfg_clks) + ARRAY_SIZE(mpfs_periph_clks) + 1;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hw_data.hws, num_clks), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clk_data->base))
		return PTR_ERR(clk_data->base);

	clk_data->hw_data.num = num_clks;

	ret = mpfs_clk_register_cfgs(dev, mpfs_cfg_clks, ARRAY_SIZE(mpfs_cfg_clks), clk_data);
	if (ret)
		return ret;

	ret = mpfs_clk_register_periphs(dev, mpfs_periph_clks, ARRAY_SIZE(mpfs_periph_clks),
					clk_data);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, &clk_data->hw_data);
	if (ret)
		return ret;

	return ret;
}

static const struct of_device_id mpfs_clk_of_match_table[] = {
	{ .compatible = "microchip,mpfs-clkcfg", },
	{}
};
MODULE_DEVICE_TABLE(of, mpfs_clk_match_table);

static struct platform_driver mpfs_clk_driver = {
	.probe = mpfs_clk_probe,
	.driver	= {
		.name = "microchip-mpfs-clkcfg",
		.of_match_table = mpfs_clk_of_match_table,
	},
};

static int __init clk_mpfs_init(void)
{
	return platform_driver_register(&mpfs_clk_driver);
}
core_initcall(clk_mpfs_init);

static void __exit clk_mpfs_exit(void)
{
	platform_driver_unregister(&mpfs_clk_driver);
}
module_exit(clk_mpfs_exit);

MODULE_DESCRIPTION("Microchip PolarFire SoC Clock Driver");
MODULE_LICENSE("GPL v2");
