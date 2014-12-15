/*
 *	arch/arm/mach-meson6/include/mach/usb.h
 *
 *  Copyright (C) 2013 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block defintions for core devices like the timer.
 * copy from linux kernel
 */

#ifndef __MACH_MESSON_USB_REGS_H
#define __MACH_MESSON_USB_REGS_H

#include <mach/am_regs.h>

#define MESON_USB_PORT_NUM 3

#define MESON_USB_NAMES "usb0","usb1","usb2"
#define MESON_USB_FIFOS 1024,1024,1024
#define MESON_USB_CTRL_ADDRS      ((void *)IO_USB_A_BASE),((void *)IO_USB_B_BASE),((void *)IO_USB_C_BASE)
#define MESON_USB_CTRL_SIZES		SZ_256K,SZ_256K,SZ_256K
#define MESON_USB_PHY_ADDRS      ((void *)P_USB_ADDR0),((void *)P_USB_ADDR8),((void *)P_USB_ADDR16)
#define MESON_USB_PHY_SIZES			8,8,8
#define MESON_USB_IRQS		INT_USB_A, INT_USB_B, INT_USB_C

#endif
