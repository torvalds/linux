/*
 * Bluetooth Broadcom GPIO and Low Power Mode control
 *
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

#ifndef __BOARD_BLUETOOTH_BCM_H__
#define __BOARD_BLUETOOTH_BCM_H__

#include <linux/serial_core.h>

#if defined(CONFIG_MACH_MINI2451)
#define GPIO_BT_EN			S3C2410_GPB(4)
#undef  GPIO_BT_WAKE
#undef  GPIO_BT_HOST_WAKE

#define GPIO_BT_RXD			S3C2410_GPH(3)
#define GPIO_BT_TXD			S3C2410_GPH(2)
#define GPIO_BT_CTS			S3C2410_GPH(10)
#define GPIO_BT_RTS			S3C2410_GPH(11)
#endif /* !CONFIG_MACH_MINI2451 */

extern void bcm_bt_lpm_exit_lpm_locked(struct uart_port *uport);

#endif /*  __BOARD_BLUETOOTH_BCM_H__  */
