/*
 * Core functions for Allwinner SoCs
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_SUNXI_COMMON_H_
#define __ARCH_SUNXI_COMMON_H_

void sun6i_secondary_startup(void);
extern struct smp_operations sun6i_smp_ops;

#endif /* __ARCH_SUNXI_COMMON_H_ */
