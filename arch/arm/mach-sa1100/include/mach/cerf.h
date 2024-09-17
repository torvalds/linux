/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-sa1100/include/mach/cerf.h
 *
 * Apr-2003 : Removed some old PDA crud [FB]
 */
#ifndef _INCLUDE_CERF_H_
#define _INCLUDE_CERF_H_


#define CERF_ETH_IO			0xf0000000
#define CERF_ETH_IRQ IRQ_GPIO26

#define CERF_GPIO_CF_BVD2		19
#define CERF_GPIO_CF_BVD1		20
#define CERF_GPIO_CF_RESET		21
#define CERF_GPIO_CF_IRQ		22
#define CERF_GPIO_CF_CD			23

#endif // _INCLUDE_CERF_H_
