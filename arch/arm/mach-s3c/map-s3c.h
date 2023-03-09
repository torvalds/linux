/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - Memory map definitions
 */

#ifndef __ASM_PLAT_MAP_S3C_H
#define __ASM_PLAT_MAP_S3C_H __FILE__

#include "map.h"

/*
 * GPIO ports
 *
 * the calculation for the VA of this must ensure that
 * it is the same distance apart from the UART in the
 * phsyical address space, as the initial mapping for the IO
 * is done as a 1:1 mapping. This puts it (currently) at
 * 0xFA800000, which is not in the way of any current mapping
 * by the base system.
*/
#define S3C64XX_VA_GPIO		S3C_ADDR_CPU(0x00000000)

#define S3C64XX_VA_MODEM	S3C_ADDR_CPU(0x00100000)
#define S3C64XX_VA_USB_HSPHY	S3C_ADDR_CPU(0x00200000)

#define S3C_VA_USB_HSPHY	S3C64XX_VA_USB_HSPHY

#include "map-s5p.h"

#endif /* __ASM_PLAT_MAP_S3C_H */
