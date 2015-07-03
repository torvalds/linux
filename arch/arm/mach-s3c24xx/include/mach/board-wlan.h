/*
 *  Copyright (C) 2015 FriendlyARM (www.arm9.net)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __BOARD_WLAN_H__
#define __BOARD_WLAN_H__

#define GPIO_WLAN_EN		S3C2410_GPB(3)
#define GPIO_WLAN_EN_AF    	1

/* EINT for WLAN to wake-up HOST */
#define GPIO_WLAN_HOST_WAKE	S3C2410_GPF(6)
#define GPIO_WLAN_HOST_WAKE_AF	0xF

static inline int brcm_gpio_host_wake(void)
{
	return GPIO_WLAN_HOST_WAKE;
}

#define GPIO_WLAN_SDIO_CLK	S3C2410_GPE(5)
#define GPIO_WLAN_SDIO_CLK_AF	2
#define GPIO_WLAN_SDIO_CMD	S3C2410_GPE(6)
#define GPIO_WLAN_SDIO_CMD_AF	2
#define GPIO_WLAN_SDIO_D0	S3C2410_GPE(7)
#define GPIO_WLAN_SDIO_D0_AF	2
#define GPIO_WLAN_SDIO_D1	S3C2410_GPE(8)
#define GPIO_WLAN_SDIO_D1_AF	2
#define GPIO_WLAN_SDIO_D2	S3C2410_GPE(9)
#define GPIO_WLAN_SDIO_D2_AF	2
#define GPIO_WLAN_SDIO_D3	S3C2410_GPE(10)
#define GPIO_WLAN_SDIO_D3_AF	2

extern int brcm_wlan_init(void);

#endif /*  __BOARD_WLAN_H__  */
