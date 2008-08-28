/*
 * arch/arm/mach-imx/include/mach/mx1ads.h
 *
 * Copyright (C) 2004 Robert Schwebel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __ASM_ARCH_MX1ADS_H
#define __ASM_ARCH_MX1ADS_H

/* ------------------------------------------------------------------------ */
/* Memory Map for the M9328MX1ADS (MX1ADS) Board                            */
/* ------------------------------------------------------------------------ */

#define MX1ADS_FLASH_PHYS		0x10000000
#define MX1ADS_FLASH_SIZE		(16*1024*1024)

#define IMX_FB_PHYS			(0x0C000000 - 0x40000)

#define CLK32 32000

#endif /* __ASM_ARCH_MX1ADS_H */
