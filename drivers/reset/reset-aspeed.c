// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2024 ASPEED Technology Inc.
 */

#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include <dt-bindings/reset/aspeed,ast2700-scu.h>

#define SCU0_RESET_CTRL1 0x200
#define SCU0_RESET_CTRL2 0x220
#define SCU1_RESET_CTRL1 0x200
#define SCU1_RESET_CTRL2 0x220
#define SCU1_PCIE3_CTRL 0x908

struct ast2700_reset_signal {
	bool dedicated_clr; /* dedicated reset clr offset */
	u32 offset, bit;
};

struct aspeed_reset_info {
	unsigned int nr_resets;
	const struct ast2700_reset_signal *signal;
};

struct aspeed_reset {
	struct reset_controller_dev rcdev;
	struct aspeed_reset_info *info;
	spinlock_t lock; /* Protect read-modify-write cycle */
	void __iomem *base;
};

static const struct ast2700_reset_signal ast2700_reset0_signals[] = {
	[SCU0_RESET_SDRAM] = { true, SCU0_RESET_CTRL1, BIT(0) },
	[SCU0_RESET_DDRPHY] = { true, SCU0_RESET_CTRL1, BIT(1) },
	[SCU0_RESET_RSA] = { true, SCU0_RESET_CTRL1, BIT(2) },
	[SCU0_RESET_SHA3] = { true, SCU0_RESET_CTRL1, BIT(3) },
	[SCU0_RESET_HACE] = { true, SCU0_RESET_CTRL1, BIT(4) },
	[SCU0_RESET_SOC] = { true, SCU0_RESET_CTRL1, BIT(5) },
	[SCU0_RESET_VIDEO] = { true, SCU0_RESET_CTRL1, BIT(6) },
	[SCU0_RESET_2D] = { true, SCU0_RESET_CTRL1, BIT(7) },
	[SCU0_RESET_PCIS] = { true, SCU0_RESET_CTRL1, BIT(8) },
	[SCU0_RESET_RVAS0] = { true, SCU0_RESET_CTRL1, BIT(9) },
	[SCU0_RESET_RVAS1] = { true, SCU0_RESET_CTRL1, BIT(10) },
	[SCU0_RESET_SM3] = { true, SCU0_RESET_CTRL1, BIT(11) },
	[SCU0_RESET_SM4] = { true, SCU0_RESET_CTRL1, BIT(12) },
	[SCU0_RESET_CRT0] = { true, SCU0_RESET_CTRL1, BIT(13) },
	[SCU0_RESET_ECC] = { true, SCU0_RESET_CTRL1, BIT(14) },
	[SCU0_RESET_DP_PCI] = { true, SCU0_RESET_CTRL1, BIT(15) },
	[SCU0_RESET_UFS] = { true, SCU0_RESET_CTRL1, BIT(16) },
	[SCU0_RESET_EMMC] = { true, SCU0_RESET_CTRL1, BIT(17) },
	[SCU0_RESET_PCIE1RST] = { true, SCU0_RESET_CTRL1, BIT(18) },
	[SCU0_RESET_PCIE1RSTOE] = { true, SCU0_RESET_CTRL1, BIT(19) },
	[SCU0_RESET_PCIE0RST] = { true, SCU0_RESET_CTRL1, BIT(20) },
	[SCU0_RESET_PCIE0RSTOE] = { true, SCU0_RESET_CTRL1, BIT(21) },
	[SCU0_RESET_JTAG] = { true, SCU0_RESET_CTRL1, BIT(22) },
	[SCU0_RESET_MCTP0] = { true, SCU0_RESET_CTRL1, BIT(23) },
	[SCU0_RESET_MCTP1] = { true, SCU0_RESET_CTRL1, BIT(24) },
	[SCU0_RESET_XDMA0] = { true, SCU0_RESET_CTRL1, BIT(25) },
	[SCU0_RESET_XDMA1] = { true, SCU0_RESET_CTRL1, BIT(26) },
	[SCU0_RESET_H2X1] = { true, SCU0_RESET_CTRL1, BIT(27) },
	[SCU0_RESET_DP] = { true, SCU0_RESET_CTRL1, BIT(28) },
	[SCU0_RESET_DP_MCU] = { true, SCU0_RESET_CTRL1, BIT(29) },
	[SCU0_RESET_SSP] = { true, SCU0_RESET_CTRL1, BIT(30) },
	[SCU0_RESET_H2X0] = { true, SCU0_RESET_CTRL1, BIT(31) },
	[SCU0_RESET_PORTA_VHUB] = { true, SCU0_RESET_CTRL2, BIT(0) },
	[SCU0_RESET_PORTA_PHY3] = { true, SCU0_RESET_CTRL2, BIT(1) },
	[SCU0_RESET_PORTA_XHCI] = { true, SCU0_RESET_CTRL2, BIT(2) },
	[SCU0_RESET_PORTB_VHUB] = { true, SCU0_RESET_CTRL2, BIT(3) },
	[SCU0_RESET_PORTB_PHY3] = { true, SCU0_RESET_CTRL2, BIT(4) },
	[SCU0_RESET_PORTB_XHCI] = { true, SCU0_RESET_CTRL2, BIT(5) },
	[SCU0_RESET_PORTA_VHUB_EHCI] = { true, SCU0_RESET_CTRL2, BIT(6) },
	[SCU0_RESET_PORTB_VHUB_EHCI] = { true, SCU0_RESET_CTRL2, BIT(7) },
	[SCU0_RESET_UHCI] = { true, SCU0_RESET_CTRL2, BIT(8) },
	[SCU0_RESET_TSP] = { true, SCU0_RESET_CTRL2, BIT(9) },
	[SCU0_RESET_E2M0] = { true, SCU0_RESET_CTRL2, BIT(10) },
	[SCU0_RESET_E2M1] = { true, SCU0_RESET_CTRL2, BIT(11) },
	[SCU0_RESET_VLINK] = { true, SCU0_RESET_CTRL2, BIT(12) },
};

static const struct ast2700_reset_signal ast2700_reset1_signals[] = {
	[SCU1_RESET_LPC0] = { true, SCU1_RESET_CTRL1, BIT(0) },
	[SCU1_RESET_LPC1] = { true, SCU1_RESET_CTRL1, BIT(1) },
	[SCU1_RESET_MII] = { true, SCU1_RESET_CTRL1, BIT(2) },
	[SCU1_RESET_PECI] = { true, SCU1_RESET_CTRL1, BIT(3) },
	[SCU1_RESET_PWM] = { true, SCU1_RESET_CTRL1, BIT(4) },
	[SCU1_RESET_MAC0] = { true, SCU1_RESET_CTRL1, BIT(5) },
	[SCU1_RESET_MAC1] = { true, SCU1_RESET_CTRL1, BIT(6) },
	[SCU1_RESET_MAC2] = { true, SCU1_RESET_CTRL1, BIT(7) },
	[SCU1_RESET_ADC] = { true, SCU1_RESET_CTRL1, BIT(8) },
	[SCU1_RESET_SD] = { true, SCU1_RESET_CTRL1, BIT(9) },
	[SCU1_RESET_ESPI0] = { true, SCU1_RESET_CTRL1, BIT(10) },
	[SCU1_RESET_ESPI1] = { true, SCU1_RESET_CTRL1, BIT(11) },
	[SCU1_RESET_JTAG1] = { true, SCU1_RESET_CTRL1, BIT(12) },
	[SCU1_RESET_SPI0] = { true, SCU1_RESET_CTRL1, BIT(13) },
	[SCU1_RESET_SPI1] = { true, SCU1_RESET_CTRL1, BIT(14) },
	[SCU1_RESET_SPI2] = { true, SCU1_RESET_CTRL1, BIT(15) },
	[SCU1_RESET_I3C0] = { true, SCU1_RESET_CTRL1, BIT(16) },
	[SCU1_RESET_I3C1] = { true, SCU1_RESET_CTRL1, BIT(17) },
	[SCU1_RESET_I3C2] = { true, SCU1_RESET_CTRL1, BIT(18) },
	[SCU1_RESET_I3C3] = { true, SCU1_RESET_CTRL1, BIT(19) },
	[SCU1_RESET_I3C4] = { true, SCU1_RESET_CTRL1, BIT(20) },
	[SCU1_RESET_I3C5] = { true, SCU1_RESET_CTRL1, BIT(21) },
	[SCU1_RESET_I3C6] = { true, SCU1_RESET_CTRL1, BIT(22) },
	[SCU1_RESET_I3C7] = { true, SCU1_RESET_CTRL1, BIT(23) },
	[SCU1_RESET_I3C8] = { true, SCU1_RESET_CTRL1, BIT(24) },
	[SCU1_RESET_I3C9] = { true, SCU1_RESET_CTRL1, BIT(25) },
	[SCU1_RESET_I3C10] = { true, SCU1_RESET_CTRL1, BIT(26) },
	[SCU1_RESET_I3C11] = { true, SCU1_RESET_CTRL1, BIT(27) },
	[SCU1_RESET_I3C12] = { true, SCU1_RESET_CTRL1, BIT(28) },
	[SCU1_RESET_I3C13] = { true, SCU1_RESET_CTRL1, BIT(29) },
	[SCU1_RESET_I3C14] = { true, SCU1_RESET_CTRL1, BIT(30) },
	[SCU1_RESET_I3C15] = { true, SCU1_RESET_CTRL1, BIT(31) },
	[SCU1_RESET_MCU0] = { true, SCU1_RESET_CTRL2, BIT(0) },
	[SCU1_RESET_MCU1] = { true, SCU1_RESET_CTRL2, BIT(1) },
	[SCU1_RESET_H2A_SPI1] = { true, SCU1_RESET_CTRL2, BIT(2) },
	[SCU1_RESET_H2A_SPI2] = { true, SCU1_RESET_CTRL2, BIT(3) },
	[SCU1_RESET_UART0] = { true, SCU1_RESET_CTRL2, BIT(4) },
	[SCU1_RESET_UART1] = { true, SCU1_RESET_CTRL2, BIT(5) },
	[SCU1_RESET_UART2] = { true, SCU1_RESET_CTRL2, BIT(6) },
	[SCU1_RESET_UART3] = { true, SCU1_RESET_CTRL2, BIT(7) },
	[SCU1_RESET_I2C_FILTER] = { true, SCU1_RESET_CTRL2, BIT(8) },
	[SCU1_RESET_CALIPTRA] = { true, SCU1_RESET_CTRL2, BIT(9) },
	[SCU1_RESET_XDMA] = { true, SCU1_RESET_CTRL2, BIT(10) },
	[SCU1_RESET_FSI] = { true, SCU1_RESET_CTRL2, BIT(12) },
	[SCU1_RESET_CAN] = { true, SCU1_RESET_CTRL2, BIT(13) },
	[SCU1_RESET_MCTP] = { true, SCU1_RESET_CTRL2, BIT(14) },
	[SCU1_RESET_I2C] = { true, SCU1_RESET_CTRL2, BIT(15) },
	[SCU1_RESET_UART6] = { true, SCU1_RESET_CTRL2, BIT(16) },
	[SCU1_RESET_UART7] = { true, SCU1_RESET_CTRL2, BIT(17) },
	[SCU1_RESET_UART8] = { true, SCU1_RESET_CTRL2, BIT(18) },
	[SCU1_RESET_UART9] = { true, SCU1_RESET_CTRL2, BIT(19) },
	[SCU1_RESET_LTPI0] = { true, SCU1_RESET_CTRL2, BIT(20) },
	[SCU1_RESET_VGAL] = { true, SCU1_RESET_CTRL2, BIT(21) },
	[SCU1_RESET_LTPI1] = { true, SCU1_RESET_CTRL2, BIT(22) },
	[SCU1_RESET_ACE] = { true, SCU1_RESET_CTRL2, BIT(23) },
	[SCU1_RESET_E2M] = { true, SCU1_RESET_CTRL2, BIT(24) },
	[SCU1_RESET_UHCI] = { true, SCU1_RESET_CTRL2, BIT(25) },
	[SCU1_RESET_PORTC_USB2UART] = { true, SCU1_RESET_CTRL2, BIT(26) },
	[SCU1_RESET_PORTC_VHUB_EHCI] = { true, SCU1_RESET_CTRL2, BIT(27) },
	[SCU1_RESET_PORTD_USB2UART] = { true, SCU1_RESET_CTRL2, BIT(28) },
	[SCU1_RESET_PORTD_VHUB_EHCI] = { true, SCU1_RESET_CTRL2, BIT(29) },
	[SCU1_RESET_H2X] = { true, SCU1_RESET_CTRL2, BIT(30) },
	[SCU1_RESET_I3CDMA] = { true, SCU1_RESET_CTRL2, BIT(31) },
	[SCU1_RESET_PCIE2RST] = { false, SCU1_PCIE3_CTRL, BIT(0) },
};

static inline struct aspeed_reset *to_aspeed_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct aspeed_reset, rcdev);
}

static int aspeed_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_reset *rc = to_aspeed_reset(rcdev);
	void __iomem *reg_offset = rc->base + rc->info->signal[id].offset;

	if (rc->info->signal[id].dedicated_clr) {
		writel(rc->info->signal[id].bit, reg_offset);
	} else {
		guard(spinlock_irqsave)(&rc->lock);
		writel(readl(reg_offset) & ~rc->info->signal[id].bit, reg_offset);
	}

	return 0;
}

static int aspeed_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_reset *rc = to_aspeed_reset(rcdev);
	void __iomem *reg_offset = rc->base + rc->info->signal[id].offset;

	if (rc->info->signal[id].dedicated_clr) {
		writel(rc->info->signal[id].bit, reg_offset + 0x04);
	} else {
		guard(spinlock_irqsave)(&rc->lock);
		writel(readl(reg_offset) | rc->info->signal[id].bit, reg_offset);
	}

	return 0;
}

static int aspeed_reset_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_reset *rc = to_aspeed_reset(rcdev);
	void __iomem *reg_offset = rc->base + rc->info->signal[id].offset;

	return (readl(reg_offset) & rc->info->signal[id].bit) ? 1 : 0;
}

static const struct reset_control_ops aspeed_reset_ops = {
	.assert = aspeed_reset_assert,
	.deassert = aspeed_reset_deassert,
	.status = aspeed_reset_status,
};

static int aspeed_reset_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct aspeed_reset *reset;
	struct device *dev = &adev->dev;

	reset = devm_kzalloc(dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	spin_lock_init(&reset->lock);

	reset->info = (struct aspeed_reset_info *)id->driver_data;
	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.nr_resets = reset->info->nr_resets;
	reset->rcdev.ops = &aspeed_reset_ops;
	reset->rcdev.of_node = dev->parent->of_node;
	reset->rcdev.dev = dev;
	reset->rcdev.of_reset_n_cells = 1;
	reset->base = (void __iomem *)adev->dev.platform_data;

	return devm_reset_controller_register(dev, &reset->rcdev);
}

static const struct aspeed_reset_info ast2700_reset0_info = {
	.nr_resets = ARRAY_SIZE(ast2700_reset0_signals),
	.signal = ast2700_reset0_signals,
};

static const struct aspeed_reset_info ast2700_reset1_info = {
	.nr_resets = ARRAY_SIZE(ast2700_reset1_signals),
	.signal = ast2700_reset1_signals,
};

static const struct auxiliary_device_id aspeed_reset_ids[] = {
	{ .name = "clk_ast2700.reset0", .driver_data = (kernel_ulong_t)&ast2700_reset0_info },
	{ .name = "clk_ast2700.reset1", .driver_data = (kernel_ulong_t)&ast2700_reset1_info },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, aspeed_reset_ids);

static struct auxiliary_driver aspeed_reset_driver = {
	.probe		= aspeed_reset_probe,
	.id_table	= aspeed_reset_ids,
};

module_auxiliary_driver(aspeed_reset_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED SoC Reset Controller Driver");
MODULE_LICENSE("GPL");
