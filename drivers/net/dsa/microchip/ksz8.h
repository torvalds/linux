/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip KSZ8XXX series register access
 *
 * Copyright (C) 2020 Pengutronix, Michael Grzeschik <kernel@pengutronix.de>
 */

#ifndef __KSZ8XXX_H
#define __KSZ8XXX_H

#include <linux/types.h>
#include <net/dsa.h>
#include "ksz_common.h"

extern const struct ksz_dev_ops ksz8463_dev_ops;
extern const struct ksz_dev_ops ksz87xx_dev_ops;
extern const struct ksz_dev_ops ksz88xx_dev_ops;
extern const struct phylink_mac_ops ksz88x3_phylink_mac_ops;
extern const struct phylink_mac_ops ksz8_phylink_mac_ops;
extern const struct dsa_switch_ops ksz8463_switch_ops;
extern const struct dsa_switch_ops ksz87xx_switch_ops;
extern const struct dsa_switch_ops ksz88xx_switch_ops;

#endif
