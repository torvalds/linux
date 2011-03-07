/*
 * arch/arm/mach-spear6xx/include/mach/hardware.h
 *
 * Hardware definitions for SPEAr6xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#include <plat/hardware.h>
#include <mach/spear.h>

/* Vitual to physical translation of statically mapped space */
#define IO_ADDRESS(x)		(x | 0xF0000000)

#endif /* __MACH_HARDWARE_H */
