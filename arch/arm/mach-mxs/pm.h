/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_MXS_PM_H
#define __ARCH_MXS_PM_H

#ifdef CONFIG_PM
void mxs_pm_init(void);
#else
#define mxs_pm_init NULL
#endif

#endif
