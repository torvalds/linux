/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Atheros AR71XX/AR724X/AR913X common definitions
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 */

#ifndef __ATH79_COMMON_H
#define __ATH79_COMMON_H

#include <linux/types.h>

#define ATH79_MEM_SIZE_MIN	(2 * 1024 * 1024)
#define ATH79_MEM_SIZE_MAX	(256 * 1024 * 1024)

void ath79_ddr_ctrl_init(void);

#endif /* __ATH79_COMMON_H */
