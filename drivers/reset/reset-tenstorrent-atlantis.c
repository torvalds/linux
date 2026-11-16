// SPDX-License-Identifier: GPL-2.0
/*
 * Tenstorrent Atlantis PRCM Reset Driver
 *
 * Copyright (c) 2026 Tenstorrent
 */

#include <dt-bindings/clock/tenstorrent,atlantis-prcm-rcpu.h>
#include <linux/auxiliary_bus.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>

/* RCPU Reset Register Offsets */
#define RCPU_BLK_RST_REG 0x001c
#define LSIO_BLK_RST_REG 0x0020
#define HSIO_BLK_RST_REG 0x000c
#define PCIE_SUBS_RST_REG 0x0000
#define MM_RSTN_REG 0x0014

struct atlantis_reset_data {
	u8 bit;
	u16 reg;
	bool active_low;
};

struct atlantis_reset_controller_data {
	const struct atlantis_reset_data *reset_data;
	size_t count;
};

struct atlantis_reset_controller {
	struct reset_controller_dev rcdev;
	const struct atlantis_reset_controller_data *data;
	struct regmap *regmap;
};

static inline struct atlantis_reset_controller *
to_atlantis_reset_controller(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct atlantis_reset_controller, rcdev);
}

#define RESET_DATA(_reg, _bit, _active_low)                          \
	{                                                            \
		.bit = _bit, .reg = _reg, .active_low = _active_low, \
	}

static const struct atlantis_reset_data atlantis_rcpu_resets[] = {
	[RST_SMNDMA0]	= RESET_DATA(RCPU_BLK_RST_REG, 0, true),
	[RST_SMNDMA1]	= RESET_DATA(RCPU_BLK_RST_REG, 1, true),
	[RST_WDT0]	= RESET_DATA(RCPU_BLK_RST_REG, 2, true),
	[RST_WDT1]	= RESET_DATA(RCPU_BLK_RST_REG, 3, true),
	[RST_TMR]	= RESET_DATA(RCPU_BLK_RST_REG, 4, true),
	[RST_PVTC]	= RESET_DATA(RCPU_BLK_RST_REG, 12, true),
	[RST_PMU]	= RESET_DATA(RCPU_BLK_RST_REG, 13, true),
	[RST_MAILBOX]	= RESET_DATA(RCPU_BLK_RST_REG, 14, true),
	[RST_SPACC]	= RESET_DATA(RCPU_BLK_RST_REG, 26, true),
	[RST_OTP]	= RESET_DATA(RCPU_BLK_RST_REG, 28, true),
	[RST_TRNG]	= RESET_DATA(RCPU_BLK_RST_REG, 29, true),
	[RST_CRC]	= RESET_DATA(RCPU_BLK_RST_REG, 30, true),
	[RST_QSPI]	= RESET_DATA(LSIO_BLK_RST_REG, 0, true),
	[RST_I2C0]	= RESET_DATA(LSIO_BLK_RST_REG, 1, true),
	[RST_I2C1]	= RESET_DATA(LSIO_BLK_RST_REG, 2, true),
	[RST_I2C2]	= RESET_DATA(LSIO_BLK_RST_REG, 3, true),
	[RST_I2C3]	= RESET_DATA(LSIO_BLK_RST_REG, 4, true),
	[RST_I2C4]	= RESET_DATA(LSIO_BLK_RST_REG, 5, true),
	[RST_UART0]	= RESET_DATA(LSIO_BLK_RST_REG, 6, true),
	[RST_UART1]	= RESET_DATA(LSIO_BLK_RST_REG, 7, true),
	[RST_UART2]	= RESET_DATA(LSIO_BLK_RST_REG, 8, true),
	[RST_UART3]	= RESET_DATA(LSIO_BLK_RST_REG, 9, true),
	[RST_UART4]	= RESET_DATA(LSIO_BLK_RST_REG, 10, true),
	[RST_SPI0]	= RESET_DATA(LSIO_BLK_RST_REG, 11, true),
	[RST_SPI1]	= RESET_DATA(LSIO_BLK_RST_REG, 12, true),
	[RST_SPI2]	= RESET_DATA(LSIO_BLK_RST_REG, 13, true),
	[RST_SPI3]	= RESET_DATA(LSIO_BLK_RST_REG, 14, true),
	[RST_GPIO]	= RESET_DATA(LSIO_BLK_RST_REG, 15, true),
	[RST_CAN0]	= RESET_DATA(LSIO_BLK_RST_REG, 17, true),
	[RST_CAN1]	= RESET_DATA(LSIO_BLK_RST_REG, 18, true),
	[RST_I2S0]	= RESET_DATA(LSIO_BLK_RST_REG, 19, true),
	[RST_I2S1]	= RESET_DATA(LSIO_BLK_RST_REG, 20, true),

};

static const struct atlantis_reset_controller_data atlantis_rcpu_reset_data = {
	.reset_data = atlantis_rcpu_resets,
	.count = ARRAY_SIZE(atlantis_rcpu_resets),
};

static int atlantis_reset_update(struct reset_controller_dev *rcdev,
				 unsigned long id, bool assert)
{
	unsigned int val;
	struct atlantis_reset_controller *rst =
		to_atlantis_reset_controller(rcdev);
	const struct atlantis_reset_data *data = &rst->data->reset_data[id];
	unsigned int mask = BIT(data->bit);
	struct regmap *regmap = rst->regmap;

	if (data->active_low ^ assert)
		val = mask;
	else
		val = 0;

	return regmap_update_bits(regmap, data->reg, mask, val);
}

static int atlantis_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return atlantis_reset_update(rcdev, id, true);
}

static int atlantis_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	return atlantis_reset_update(rcdev, id, false);
}

static const struct reset_control_ops atlantis_reset_control_ops = {
	.assert = atlantis_reset_assert,
	.deassert = atlantis_reset_deassert,
};

static int
atlantis_reset_controller_register(struct device *dev,
				   struct atlantis_reset_controller *controller)
{
	struct reset_controller_dev *rcdev = &controller->rcdev;

	rcdev->ops = &atlantis_reset_control_ops;
	rcdev->owner = THIS_MODULE;
	rcdev->of_node = dev->of_node;
	rcdev->nr_resets = controller->data->count;

	return devm_reset_controller_register(dev, &controller->rcdev);
}
static int atlantis_reset_probe(struct auxiliary_device *adev,
				const struct auxiliary_device_id *id)
{
	struct atlantis_reset_controller *controller;
	struct device *dev = &adev->dev;
	struct regmap *regmap;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	controller = devm_kzalloc_obj(dev, *controller);
	if (!controller)
		return -ENOMEM;
	controller->data =
		(const struct atlantis_reset_controller_data *)id->driver_data;
	controller->regmap = regmap;

	return atlantis_reset_controller_register(dev, controller);
}

static const struct auxiliary_device_id atlantis_reset_ids[] = {
	{ .name = "atlantis_prcm.rcpu-reset",
	  .driver_data = (kernel_ulong_t)&atlantis_rcpu_reset_data },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, atlantis_reset_ids);

static struct auxiliary_driver atlantis_reset_driver = {
	.probe = atlantis_reset_probe,
	.id_table = atlantis_reset_ids,
};
module_auxiliary_driver(atlantis_reset_driver);

MODULE_AUTHOR("Anirudh Srinivasan <asrinivasan@oss.tenstorrent.com>");
MODULE_DESCRIPTION("Atlantis PRCM reset controller driver");
MODULE_LICENSE("GPL");
