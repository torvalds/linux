/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Icenowy Zheng <icenowy@aosc.io>
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
