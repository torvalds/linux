// SPDX-License-Identifier: GPL-2.0-only
/*
 * PolarFire SoC MSS/core complex clock control
 *
 * Copyright (C) 2020-2022 Microchip Technology Inc. All rights reserved.
 */
#include <linux/auxiliary_bus.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <dt-bindings/clock/microchip,mpfs-clock.h>
#include <soc/microchip/mpfs.h>

/* address offset of control registers */
#define REG_MSSPLL_REF_CR	0x08u
#define REG_MSSPLL_POSTDIV_CR	0x10u
#define REG_MSSPLL_SSCG_2_CR	0x2Cu
#define REG_CLOCK_CONFIG_CR	0x08u
#define REG_RTC_CLOCK_CR	0x0Cu
#define REG_SUBBLK_CLOCK_CR	0x84u
#define REG_SUBBLK_RESET_CR	0x88u

#define MSSPLL_FBDIV_SHIFT	0x00u
#define MSSPLL_FBDIV_WIDTH	0x0Cu
#define MSSPLL_REFDIV_SHIFT	0x08u
#define MSSPLL_REFDIV_WIDTH	0x06u
#define MSSPLL_POSTDIV_SHIFT	0x08u
#define MSSPLL_POSTDIV_WIDTH	0x07u
#define MSSPLL_FIXED_DIV	4u

struct mpfs_clock_data {
	struct device *dev;
	void __iomem *base;
	void __iomem *msspll_base;
	struct clk_hw_onecell_data hw_data;
};

struct mpfs_msspll_hw_clock {
	void __iomem *base;
	unsigned int id;
	u32 reg_offset;
	u32 shift;
	u32 width;
	u32 flags;
	struct clk_hw hw;
	struct clk_init_data init;
};

#define to_mpfs_msspll_clk(_hw) container_of(_hw, struct mpfs_msspll_hw_clock, hw)

struct mpfs_cfg_hw_clock {
	struct clk_divider cfg;
	struct clk_init_data init;
	unsigned int id;
	u32 reg_offset;
};

struct mpfs_periph_hw_clock {
	struct clk_gate periph;
	unsigned int id;
};

/*
 * mpfs_clk_lock prevents anything else from writing to the
 * mpfs clk block while a software locked register is being written.
 */
static DEFINE_SPINLOCK(mpfs_clk_lock);

static const struct clk_parent_data mpfs_ext_ref[] = {
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

/*
 * The only two supported reference clock frequencies for the PolarFire SoC are
 * 100 and 125 MHz, as the rtc reference is required to be 1 MHz.
 * It therefore only needs to have divider table entries corresponding to
 * divide by 100 and 125.
 */
static const struct clk_div_table mpfs_div_rtcref_table[] = {
	{ 100, 100 }, { 125, 125 },
	{ 0, 0 }
};

static unsigned long mpfs_clk_msspll_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct mpfs_msspll_hw_clock *msspll_hw = to_mpfs_msspll_clk(hw);
	void __iomem *mult_addr = msspll_hw->base + msspll_hw->reg_offset;
	void __iomem *ref_div_addr = msspll_hw->base + REG_MSSPLL_REF_CR;
	void __iomem *postdiv_addr = msspll_hw->base + REG_MSSPLL_POSTDIV_CR;
	u32 mult, ref_div, postdiv;

	mult = readl_relaxed(mult_addr) >> MSSPLL_FBDIV_SHIFT;
	mult &= clk_div_mask(MSSPLL_FBDIV_WIDTH);
	ref_div = readl_relaxed(ref_div_addr) >> MSSPLL_REFDIV_SHIFT;
	ref_div &= clk_div_mask(MSSPLL_REFDIV_WIDTH);
	postdiv = readl_relaxed(postdiv_addr) >> MSSPLL_POSTDIV_SHIFT;
	postdiv &= clk_div_mask(MSSPLL_POSTDIV_WIDTH);

	return prate * mult / (ref_div * MSSPLL_FIXED_DIV * postdiv);
}

static long mpfs_clk_msspll_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *prate)
{
	struct mpfs_msspll_hw_clock *msspll_hw = to_mpfs_msspll_clk(hw);
	void __iomem *mult_addr = msspll_hw->base + msspll_hw->reg_offset;
	void __iomem *ref_div_addr = msspll_hw->base + REG_MSSPLL_REF_CR;
	u32 mult, ref_div;
	unsigned long rate_before_ctrl;

	mult = readl_relaxed(mult_addr) >> MSSPLL_FBDIV_SHIFT;
	mult &= clk_div_mask(MSSPLL_FBDIV_WIDTH);
	ref_div = readl_relaxed(ref_div_addr) >> MSSPLL_REFDIV_SHIFT;
	ref_div &= clk_div_mask(MSSPLL_REFDIV_WIDTH);

	rate_before_ctrl = rate * (ref_div * MSSPLL_FIXED_DIV) / mult;

	return divider_round_rate(hw, rate_before_ctrl, prate, NULL, MSSPLL_POSTDIV_WIDTH,
				  msspll_hw->flags);
}

static int mpfs_clk_msspll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long prate)
{
	struct mpfs_msspll_hw_clock *msspll_hw = to_mpfs_msspll_clk(hw);
	void __iomem *mult_addr = msspll_hw->base + msspll_hw->reg_offset;
	void __iomem *ref_div_addr = msspll_hw->base + REG_MSSPLL_REF_CR;
	void __iomem *postdiv_addr = msspll_hw->base + REG_MSSPLL_POSTDIV_CR;
	u32 mult, ref_div, postdiv;
	int divider_setting;
	unsigned long rate_before_ctrl, flags;

	mult = readl_relaxed(mult_addr) >> MSSPLL_FBDIV_SHIFT;
	mult &= clk_div_mask(MSSPLL_FBDIV_WIDTH);
	ref_div = readl_relaxed(ref_div_addr) >> MSSPLL_REFDIV_SHIFT;
	ref_div &= clk_div_mask(MSSPLL_REFDIV_WIDTH);

	rate_before_ctrl = rate * (ref_div * MSSPLL_FIXED_DIV) / mult;
	divider_setting = divider_get_val(rate_before_ctrl, prate, NULL, MSSPLL_POSTDIV_WIDTH,
					  msspll_hw->flags);

	if (divider_setting < 0)
		return divider_setting;

	spin_lock_irqsave(&mpfs_clk_lock, flags);

	postdiv = readl_relaxed(postdiv_addr);
	postdiv &= ~(clk_div_mask(MSSPLL_POSTDIV_WIDTH) << MSSPLL_POSTDIV_SHIFT);
	writel_relaxed(postdiv, postdiv_addr);

	spin_unlock_irqrestore(&mpfs_clk_lock, flags);

	return 0;
}

static const struct clk_ops mpfs_clk_msspll_ops = {
	.recalc_rate = mpfs_clk_msspll_recalc_rate,
	.round_rate = mpfs_clk_msspll_round_rate,
	.set_rate = mpfs_clk_msspll_set_rate,
};

#define CLK_PLL(_id, _name, _parent, _shift, _width, _flags, _offset) {			\
	.id = _id,									\
	.shift = _shift,								\
	.width = _width,								\
	.reg_offset = _offset,								\
	.flags = _flags,								\
	.hw.init = CLK_HW_INIT_PARENTS_DATA(_name, _parent, &mpfs_clk_msspll_ops, 0),	\
}

static struct mpfs_msspll_hw_clock mpfs_msspll_clks[] = {
	CLK_PLL(CLK_MSSPLL, "clk_msspll", mpfs_ext_ref, MSSPLL_FBDIV_SHIFT,
		MSSPLL_FBDIV_WIDTH, 0, REG_MSSPLL_SSCG_2_CR),
};

static int mpfs_clk_register_mssplls(struct device *dev, struct mpfs_msspll_hw_clock *msspll_hws,
				     unsigned int num_clks, struct mpfs_clock_data *data)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_clks; i++) {
		struct mpfs_msspll_hw_clock *msspll_hw = &msspll_hws[i];

		msspll_hw->base = data->msspll_base;
		ret = devm_clk_hw_register(dev, &msspll_hw->hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register msspll id: %d\n",
					     CLK_MSSPLL);

		data->hw_data.hws[msspll_hw->id] = &msspll_hw->hw;
	}

	return 0;
}

/*
 * "CFG" clocks
 */

#define CLK_CFG(_id, _name, _parent, _shift, _width, _table, _flags, _offset) {		\
	.id = _id,									\
	.cfg.shift = _shift,								\
	.cfg.width = _width,								\
	.cfg.table = _table,								\
	.reg_offset = _offset,								\
	.cfg.flags = _flags,								\
	.cfg.hw.init = CLK_HW_INIT(_name, _parent, &clk_divider_ops, 0),		\
	.cfg.lock = &mpfs_clk_lock,							\
}

#define CLK_CPU_OFFSET		0u
#define CLK_AXI_OFFSET		1u
#define CLK_AHB_OFFSET		2u
#define CLK_RTCREF_OFFSET	3u

static struct mpfs_cfg_hw_clock mpfs_cfg_clks[] = {
	CLK_CFG(CLK_CPU, "clk_cpu", "clk_msspll", 0, 2, mpfs_div_cpu_axi_table, 0,
		REG_CLOCK_CONFIG_CR),
	CLK_CFG(CLK_AXI, "clk_axi", "clk_msspll", 2, 2, mpfs_div_cpu_axi_table, 0,
		REG_CLOCK_CONFIG_CR),
	CLK_CFG(CLK_AHB, "clk_ahb", "clk_msspll", 4, 2, mpfs_div_ahb_table, 0,
		REG_CLOCK_CONFIG_CR),
	{
		.id = CLK_RTCREF,
		.cfg.shift = 0,
		.cfg.width = 12,
		.cfg.table = mpfs_div_rtcref_table,
		.reg_offset = REG_RTC_CLOCK_CR,
		.cfg.flags = CLK_DIVIDER_ONE_BASED,
		.cfg.hw.init =
			CLK_HW_INIT_PARENTS_DATA("clk_rtcref", mpfs_ext_ref, &clk_divider_ops, 0),
	}
};

static int mpfs_clk_register_cfgs(struct device *dev, struct mpfs_cfg_hw_clock *cfg_hws,
				  unsigned int num_clks, struct mpfs_clock_data *data)
{
	unsigned int i, id;
	int ret;

	for (i = 0; i < num_clks; i++) {
		struct mpfs_cfg_hw_clock *cfg_hw = &cfg_hws[i];

		cfg_hw->cfg.reg = data->base + cfg_hw->reg_offset;
		ret = devm_clk_hw_register(dev, &cfg_hw->cfg.hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register clock id: %d\n",
					     cfg_hw->id);

		id = cfg_hw->id;
		data->hw_data.hws[id] = &cfg_hw->cfg.hw;
	}

	return 0;
}

/*
 * peripheral clocks - devices connected to axi or ahb buses.
 */

#define CLK_PERIPH(_id, _name, _parent, _shift, _flags) {			\
	.id = _id,								\
	.periph.bit_idx = _shift,						\
	.periph.hw.init = CLK_HW_INIT_HW(_name, _parent, &clk_gate_ops,		\
				  _flags),					\
	.periph.lock = &mpfs_clk_lock,						\
}

#define PARENT_CLK(PARENT) (&mpfs_cfg_clks[CLK_##PARENT##_OFFSET].cfg.hw)

/*
 * Critical clocks:
 * - CLK_ENVM: reserved by hart software services (hss) superloop monitor/m mode interrupt
 *   trap handler
 * - CLK_MMUART0: reserved by the hss
 * - CLK_DDRC: provides clock to the ddr subsystem
 * - CLK_RTC: the onboard RTC's AHB bus clock must be kept running as the rtc will stop
 *   if the AHB interface clock is disabled
 * - CLK_FICx: these provide the processor side clocks to the "FIC" (Fabric InterConnect)
 *   clock domain crossers which provide the interface to the FPGA fabric. Disabling them
 *   causes the FPGA fabric to go into reset.
 * - CLK_ATHENA: The athena clock is FIC4, which is reserved for the Athena TeraFire.
 */

static struct mpfs_periph_hw_clock mpfs_periph_clks[] = {
	CLK_PERIPH(CLK_ENVM, "clk_periph_envm", PARENT_CLK(AHB), 0, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_MAC0, "clk_periph_mac0", PARENT_CLK(AHB), 1, 0),
	CLK_PERIPH(CLK_MAC1, "clk_periph_mac1", PARENT_CLK(AHB), 2, 0),
	CLK_PERIPH(CLK_MMC, "clk_periph_mmc", PARENT_CLK(AHB), 3, 0),
	CLK_PERIPH(CLK_TIMER, "clk_periph_timer", PARENT_CLK(RTCREF), 4, 0),
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
	CLK_PERIPH(CLK_RTC, "clk_periph_rtc", PARENT_CLK(AHB), 18, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_QSPI, "clk_periph_qspi", PARENT_CLK(AHB), 19, 0),
	CLK_PERIPH(CLK_GPIO0, "clk_periph_gpio0", PARENT_CLK(AHB), 20, 0),
	CLK_PERIPH(CLK_GPIO1, "clk_periph_gpio1", PARENT_CLK(AHB), 21, 0),
	CLK_PERIPH(CLK_GPIO2, "clk_periph_gpio2", PARENT_CLK(AHB), 22, 0),
	CLK_PERIPH(CLK_DDRC, "clk_periph_ddrc", PARENT_CLK(AHB), 23, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC0, "clk_periph_fic0", PARENT_CLK(AXI), 24, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC1, "clk_periph_fic1", PARENT_CLK(AXI), 25, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC2, "clk_periph_fic2", PARENT_CLK(AXI), 26, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_FIC3, "clk_periph_fic3", PARENT_CLK(AXI), 27, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_ATHENA, "clk_periph_athena", PARENT_CLK(AXI), 28, CLK_IS_CRITICAL),
	CLK_PERIPH(CLK_CFM, "clk_periph_cfm", PARENT_CLK(AHB), 29, 0),
};

static int mpfs_clk_register_periphs(struct device *dev, struct mpfs_periph_hw_clock *periph_hws,
				     int num_clks, struct mpfs_clock_data *data)
{
	unsigned int i, id;
	int ret;

	for (i = 0; i < num_clks; i++) {
		struct mpfs_periph_hw_clock *periph_hw = &periph_hws[i];

		periph_hw->periph.reg = data->base + REG_SUBBLK_CLOCK_CR;
		ret = devm_clk_hw_register(dev, &periph_hw->periph.hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register clock id: %d\n",
					     periph_hw->id);

		id = periph_hws[i].id;
		data->hw_data.hws[id] = &periph_hw->periph.hw;
	}

	return 0;
}

/*
 * Peripheral clock resets
 */

#if IS_ENABLED(CONFIG_RESET_CONTROLLER)

u32 mpfs_reset_read(struct device *dev)
{
	struct mpfs_clock_data *clock_data = dev_get_drvdata(dev->parent);

	return readl_relaxed(clock_data->base + REG_SUBBLK_RESET_CR);
}
EXPORT_SYMBOL_NS_GPL(mpfs_reset_read, MCHP_CLK_MPFS);

void mpfs_reset_write(struct device *dev, u32 val)
{
	struct mpfs_clock_data *clock_data = dev_get_drvdata(dev->parent);

	writel_relaxed(val, clock_data->base + REG_SUBBLK_RESET_CR);
}
EXPORT_SYMBOL_NS_GPL(mpfs_reset_write, MCHP_CLK_MPFS);

static void mpfs_reset_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void mpfs_reset_adev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	kfree(adev);
}

static struct auxiliary_device *mpfs_reset_adev_alloc(struct mpfs_clock_data *clk_data)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	adev->name = "reset-mpfs";
	adev->dev.parent = clk_data->dev;
	adev->dev.release = mpfs_reset_adev_release;
	adev->id = 666u;

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(adev);
		return ERR_PTR(ret);
	}

	return adev;
}

static int mpfs_reset_controller_register(struct mpfs_clock_data *clk_data)
{
	struct auxiliary_device *adev;
	int ret;

	adev = mpfs_reset_adev_alloc(clk_data);
	if (IS_ERR(adev))
		return PTR_ERR(adev);

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(clk_data->dev, mpfs_reset_unregister_adev, adev);
}

#else /* !CONFIG_RESET_CONTROLLER */

static int mpfs_reset_controller_register(struct mpfs_clock_data *clk_data)
{
	return 0;
}

#endif /* !CONFIG_RESET_CONTROLLER */

static int mpfs_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_clock_data *clk_data;
	unsigned int num_clks;
	int ret;

	/* CLK_RESERVED is not part of clock arrays, so add 1 */
	num_clks = ARRAY_SIZE(mpfs_msspll_clks) + ARRAY_SIZE(mpfs_cfg_clks)
		   + ARRAY_SIZE(mpfs_periph_clks) + 1;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hw_data.hws, num_clks), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clk_data->base))
		return PTR_ERR(clk_data->base);

	clk_data->msspll_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(clk_data->msspll_base))
		return PTR_ERR(clk_data->msspll_base);

	clk_data->hw_data.num = num_clks;
	clk_data->dev = dev;
	dev_set_drvdata(dev, clk_data);

	ret = mpfs_clk_register_mssplls(dev, mpfs_msspll_clks, ARRAY_SIZE(mpfs_msspll_clks),
					clk_data);
	if (ret)
		return ret;

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

	return mpfs_reset_controller_register(clk_data);
}

static const struct of_device_id mpfs_clk_of_match_table[] = {
	{ .compatible = "microchip,mpfs-clkcfg", },
	{}
};
MODULE_DEVICE_TABLE(of, mpfs_clk_of_match_table);

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
MODULE_AUTHOR("Padmarao Begari <padmarao.begari@microchip.com>");
MODULE_AUTHOR("Daire McNamara <daire.mcnamara@microchip.com>");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_LICENSE("GPL");
