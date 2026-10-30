// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/anlogic,dr1v90-cru.h>

struct dr1v90_reset_map {
	u32 offset;
	u32 bit;
};

struct dr1v90_reset_controller {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	spinlock_t lock; /* protect register read-modify-write */
};

static inline struct dr1v90_reset_controller *
to_dr1v90_reset_controller(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct dr1v90_reset_controller, rcdev);
}

static const struct dr1v90_reset_map dr1v90_resets[] = {
	[RESET_OCM]		= { 0x74, BIT(4)},
	[RESET_QSPI]		= { 0x74, BIT(5)},
	[RESET_SMC]		= { 0x74, BIT(6)},
	[RESET_WDT]		= { 0x74, BIT(7)},
	[RESET_DMAC_AXI]	= { 0x74, BIT(8)},
	[RESET_DMAC_AHB]	= { 0x74, BIT(9)},
	[RESET_NPU]		= { 0x74, BIT(12)},
	[RESET_JPU]		= { 0x74, BIT(13)},
	[RESET_DDRBUS]		= { 0x74, BIT(14)},
	[RESET_NIC_HP0]		= { 0x78, BIT(0)},
	[RESET_NIC_HP1]		= { 0x78, BIT(1)},
	[RESET_NIC_GP0M]	= { 0x78, BIT(4)},
	[RESET_NIC_GP1M]	= { 0x78, BIT(5)},
	[RESET_GPIO]		= { 0x78, BIT(8)},
	[RESET_IPC]		= { 0x78, BIT(12)},
	[RESET_USB0]		= { 0x7C, BIT(0)},
	[RESET_USB1]		= { 0x7C, BIT(1)},
	[RESET_GBE0]		= { 0x7C, BIT(4)},
	[RESET_GBE1]		= { 0x7C, BIT(5)},
	[RESET_SDIO0]		= { 0x7C, BIT(8)},
	[RESET_SDIO1]		= { 0x7C, BIT(9)},
	[RESET_UART0]		= { 0x7C, BIT(12)},
	[RESET_UART1]		= { 0x7C, BIT(13)},
	[RESET_SPI0]		= { 0x7C, BIT(16)},
	[RESET_SPI1]		= { 0x7C, BIT(17)},
	[RESET_CAN0]		= { 0x7C, BIT(20)},
	[RESET_CAN1]		= { 0x7C, BIT(21)},
	[RESET_TTC0]		= { 0x7C, BIT(24)},
	[RESET_TTC1]		= { 0x7C, BIT(25)},
	[RESET_I2C0]		= { 0x7C, BIT(28)},
	[RESET_I2C1]		= { 0x7C, BIT(29)}
};

static int dr1v90_reset_control_update(struct reset_controller_dev *rcdev,
				       unsigned long id, bool assert)
{
	struct dr1v90_reset_controller *rstc = to_dr1v90_reset_controller(rcdev);
	u32 offset = dr1v90_resets[id].offset;
	u32 bit = dr1v90_resets[id].bit;
	u32 reg;

	guard(spinlock_irqsave)(&rstc->lock);

	reg = readl(rstc->base + offset);
	if (assert)
		reg &= ~bit;
	else
		reg |= bit;
	writel(reg, rstc->base + offset);

	return 0;
}

static int dr1v90_reset_control_assert(struct reset_controller_dev *rcdev,
				       unsigned long id)
{
	return dr1v90_reset_control_update(rcdev, id, true);
}

static int dr1v90_reset_control_deassert(struct reset_controller_dev *rcdev,
					 unsigned long id)
{
	return dr1v90_reset_control_update(rcdev, id, false);
}

static const struct reset_control_ops dr1v90_reset_control_ops = {
	.assert = dr1v90_reset_control_assert,
	.deassert = dr1v90_reset_control_deassert,
};

static int dr1v90_reset_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct dr1v90_reset_controller *rstc;
	struct device *dev = &adev->dev;

	rstc = devm_kzalloc(dev, sizeof(*rstc), GFP_KERNEL);
	if (!rstc)
		return -ENOMEM;

	spin_lock_init(&rstc->lock);

	rstc->base = dev->platform_data;
	rstc->rcdev.dev = dev;
	rstc->rcdev.nr_resets = ARRAY_SIZE(dr1v90_resets);
	rstc->rcdev.of_node = dev->parent->of_node;
	rstc->rcdev.ops = &dr1v90_reset_control_ops;
	rstc->rcdev.owner = THIS_MODULE;

	return devm_reset_controller_register(dev, &rstc->rcdev);
}

static const struct auxiliary_device_id dr1v90_reset_ids[] = {
	{
		.name = "anlogic_dr1_cru.reset"
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(auxiliary, dr1v90_reset_ids);

static struct auxiliary_driver dr1v90_reset_driver = {
	.probe = dr1v90_reset_probe,
	.id_table = dr1v90_reset_ids,
};
module_auxiliary_driver(dr1v90_reset_driver);

MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_DESCRIPTION("Anlogic DR1V90 reset controller driver");
MODULE_LICENSE("GPL");
