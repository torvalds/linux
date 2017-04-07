/*
 * Copyright 2016 Icenowy <icenowy@aosc.xyz>
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
 */

#ifndef _CCU_SUN8I_R_H
#define _CCU_SUN8I_R_H_

#include <dt-bindings/clock/sun8i-r-ccu.h>
#include <dt-bindings/reset/sun8i-r-ccu.h>

/* AHB/APB bus clocks are not exported */
#define CLK_AHB0	1
#define CLK_APB0	2

#define CLK_NUMBER	(CLK_IR + 1)

#endif /* _CCU_SUN8I_R_H */
