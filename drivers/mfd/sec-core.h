/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2012 Samsung Electronics Co., Ltd
 *                http://www.samsung.com
 * Copyright 2025 Linaro Ltd.
 *
 * Samsung SxM core driver internal data
 */

#ifndef __SEC_CORE_INT_H
#define __SEC_CORE_INT_H

struct i2c_client;

extern const struct dev_pm_ops sec_pmic_pm_ops;

int sec_pmic_probe(struct device *dev, int device_type, unsigned int irq,
		   struct regmap *regmap, struct i2c_client *client);
void sec_pmic_shutdown(struct device *dev);

int sec_irq_init(struct sec_pmic_dev *sec_pmic);

#endif /* __SEC_CORE_INT_H */
