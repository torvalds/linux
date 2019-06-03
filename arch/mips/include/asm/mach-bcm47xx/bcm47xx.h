/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#ifndef __ASM_BCM47XX_H
#define __ASM_BCM47XX_H

#include <linux/ssb/ssb.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_soc.h>
#include <linux/bcm47xx_nvram.h>

enum bcm47xx_bus_type {
#ifdef CONFIG_BCM47XX_SSB
	BCM47XX_BUS_TYPE_SSB,
#endif
#ifdef CONFIG_BCM47XX_BCMA
	BCM47XX_BUS_TYPE_BCMA,
#endif
};

union bcm47xx_bus {
#ifdef CONFIG_BCM47XX_SSB
	struct ssb_bus ssb;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	struct bcma_soc bcma;
#endif
};

extern union bcm47xx_bus bcm47xx_bus;
extern enum bcm47xx_bus_type bcm47xx_bus_type;

void bcm47xx_fill_sprom(struct ssb_sprom *sprom, const char *prefix,
			bool fallback);

void bcm47xx_set_system_type(u16 chip_id);

#endif /* __ASM_BCM47XX_H */
