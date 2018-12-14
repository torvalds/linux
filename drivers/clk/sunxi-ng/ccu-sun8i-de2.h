/*
 * Copyright 2016 Icenowy Zheng <icenowy@aosc.io>
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

#ifndef _CCU_SUN8I_DE2_H_
#define _CCU_SUN8I_DE2_H_

#include <dt-bindings/clock/sun8i-de2.h>
#include <dt-bindings/reset/sun8i-de2.h>

/* Intermediary clock dividers are not exported */
#define CLK_MIXER0_DIV	3
#define CLK_MIXER1_DIV	4
#define CLK_WB_DIV	5
#define CLK_ROT_DIV	11

#define CLK_NUMBER_WITH_ROT	(CLK_ROT_DIV + 1)
#define CLK_NUMBER_WITHOUT_ROT	(CLK_WB + 1)

#endif /* _CCU_SUN8I_DE2_H_ */
