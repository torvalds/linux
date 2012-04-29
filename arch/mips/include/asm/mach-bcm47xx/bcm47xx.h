/*
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __ASM_BCM47XX_H
#define __ASM_BCM47XX_H

#include <linux/ssb/ssb.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_soc.h>

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

void bcm47xx_fill_sprom(struct ssb_sprom *sprom, const char *prefix);
void bcm47xx_fill_sprom_ethernet(struct ssb_sprom *sprom, const char *prefix);

#ifdef CONFIG_BCM47XX_SSB
void bcm47xx_fill_ssb_boardinfo(struct ssb_boardinfo *boardinfo,
				const char *prefix);
#endif

#endif /* __ASM_BCM47XX_H */
