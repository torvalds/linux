/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Murata ZPA2326 pressure and temperature sensor IIO driver
 *
 * Copyright (c) 2016 Parrot S.A.
 *
 * Author: Gregor Boirie <gregor.boirie@parrot.com>
 */

#ifndef _ZPA2326_H
#define _ZPA2326_H

/* Register map. */
#define ZPA2326_REF_P_XL_REG              (0x8)
#define ZPA2326_REF_P_L_REG               (0x9)
#define ZPA2326_REF_P_H_REG               (0xa)
#define ZPA2326_DEVICE_ID_REG             (0xf)
#define ZPA2326_DEVICE_ID                 (0xb9)
#define ZPA2326_RES_CONF_REG              (0x10)
#define ZPA2326_CTRL_REG0_REG             (0x20)
#define ZPA2326_CTRL_REG0_ONE_SHOT        BIT(0)
#define ZPA2326_CTRL_REG0_ENABLE          BIT(1)
#define ZPA2326_CTRL_REG1_REG             (0x21)
#define ZPA2326_CTRL_REG1_MASK_DATA_READY BIT(2)
#define ZPA2326_CTRL_REG2_REG             (0x22)
#define ZPA2326_CTRL_REG2_SWRESET         BIT(2)
#define ZPA2326_CTRL_REG3_REG             (0x23)
#define ZPA2326_CTRL_REG3_ODR_SHIFT       (4)
#define ZPA2326_CTRL_REG3_ENABLE_MEAS     BIT(7)
#define ZPA2326_INT_SOURCE_REG            (0x24)
#define ZPA2326_INT_SOURCE_DATA_READY     BIT(2)
#define ZPA2326_THS_P_LOW_REG             (0x25)
#define ZPA2326_THS_P_HIGH_REG            (0x26)
#define ZPA2326_STATUS_REG                (0x27)
#define ZPA2326_STATUS_P_DA               BIT(1)
#define ZPA2326_STATUS_FIFO_E             BIT(2)
#define ZPA2326_STATUS_P_OR               BIT(5)
#define ZPA2326_PRESS_OUT_XL_REG          (0x28)
#define ZPA2326_PRESS_OUT_L_REG           (0x29)
#define ZPA2326_PRESS_OUT_H_REG           (0x2a)
#define ZPA2326_TEMP_OUT_L_REG            (0x2b)
#define ZPA2326_TEMP_OUT_H_REG            (0x2c)

struct device;
struct regmap;

bool zpa2326_isreg_writeable(struct device *dev, unsigned int reg);
bool zpa2326_isreg_readable(struct device *dev, unsigned int reg);
bool zpa2326_isreg_precious(struct device *dev, unsigned int reg);

/**
 * zpa2326_probe() - Instantiate and register core ZPA2326 IIO device
 * @parent: Hardware sampling device the created IIO device will be a child of.
 * @name:   Arbitrary name to identify the device.
 * @irq:    Interrupt line, negative if none.
 * @hwid:   Expected device hardware id.
 * @regmap: Registers map used to abstract underlying bus accesses.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
int zpa2326_probe(struct device        *parent,
		  const char           *name,
		  int                   irq,
		  unsigned int          hwid,
		  struct regmap        *regmap);

/**
 * zpa2326_remove() - Unregister and destroy core ZPA2326 IIO device.
 * @parent: Hardware sampling device the IIO device to remove is a child of.
 */
void zpa2326_remove(const struct device *parent);

#ifdef CONFIG_PM
#include <linux/pm.h>
extern const struct dev_pm_ops zpa2326_pm_ops;
#define ZPA2326_PM_OPS (&zpa2326_pm_ops)
#else
#define ZPA2326_PM_OPS (NULL)
#endif

#endif
