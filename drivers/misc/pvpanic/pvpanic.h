// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2021 Oracle.
 */

#ifndef PVPANIC_H_
#define PVPANIC_H_

int devm_pvpanic_probe(struct device *dev, void __iomem *base);
extern const struct attribute_group *pvpanic_dev_groups[];

#endif /* PVPANIC_H_ */
