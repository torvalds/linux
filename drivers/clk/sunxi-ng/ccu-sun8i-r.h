/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Icenowy <icenowy@aosc.xyz>
 */

#ifndef _CCU_SUN8I_R_H
#define _CCU_SUN8I_R_H

#include <dt-bindings/clock/sun8i-r-ccu.h>
#include <dt-bindings/reset/sun8i-r-ccu.h>

/* AHB/APB bus clocks are not exported */
#define CLK_AHB0	1
#define CLK_APB0	2

#define CLK_NUMBER	(CLK_IR + 1)

#endif /* _CCU_SUN8I_R_H */
