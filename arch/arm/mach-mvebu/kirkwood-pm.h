/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Power Management driver for Marvell Kirkwood SoCs
 *
 * Copyright (C) 2013 Ezequiel Garcia <ezequiel@free-electrons.com>
 * Copyright (C) 2010 Simon Guinot <sguinot@lacie.com>
 */

#ifndef __ARCH_KIRKWOOD_PM_H
#define __ARCH_KIRKWOOD_PM_H

#ifdef CONFIG_PM
void kirkwood_pm_init(void);
#else
static inline void kirkwood_pm_init(void) {};
#endif

#endif
