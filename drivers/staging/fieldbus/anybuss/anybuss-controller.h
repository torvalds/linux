/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Anybus-S controller definitions
 *
 * Copyright 2018 Arcx Inc
 */

#ifndef __LINUX_ANYBUSS_CONTROLLER_H__
#define __LINUX_ANYBUSS_CONTROLLER_H__

#include <linux/device.h>
#include <linux/regmap.h>

/*
 * To instantiate an Anybus-S host, a controller should provide the following:
 * - a reset function which resets the attached card;
 * - a regmap which provides access to the attached card's dpram;
 * - the irq of the attached card
 */
/**
 * struct anybuss_ops - Controller resources to instantiate an Anybus-S host
 *
 * @reset:	asserts/deasserts the anybus card's reset line.
 * @regmap:	provides access to the card's dual-port RAM area.
 * @irq:	number of the interrupt connected to the card's interrupt line.
 * @host_idx:	for multi-host controllers, the host index:
 *		0 for the first host on the controller, 1 for the second, etc.
 */
struct anybuss_ops {
	void (*reset)(struct device *dev, bool assert);
	struct regmap *regmap;
	int irq;
	int host_idx;
};

struct anybuss_host;

struct anybuss_host * __must_check
anybuss_host_common_probe(struct device *dev,
			  const struct anybuss_ops *ops);
void anybuss_host_common_remove(struct anybuss_host *host);

struct anybuss_host * __must_check
devm_anybuss_host_common_probe(struct device *dev,
			       const struct anybuss_ops *ops);

#endif /* __LINUX_ANYBUSS_CONTROLLER_H__ */
