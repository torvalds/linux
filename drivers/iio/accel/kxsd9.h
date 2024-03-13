/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/device.h>
#include <linux/kernel.h>

#define KXSD9_STATE_RX_SIZE 2
#define KXSD9_STATE_TX_SIZE 2

int kxsd9_common_probe(struct device *dev,
		       struct regmap *map,
		       const char *name);
void kxsd9_common_remove(struct device *dev);

extern const struct dev_pm_ops kxsd9_dev_pm_ops;
