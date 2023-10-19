/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 */

#ifndef _CCU_SUN9I_A80_DE_H_
#define _CCU_SUN9I_A80_DE_H_

#include <dt-bindings/clock/sun9i-a80-de.h>
#include <dt-bindings/reset/sun9i-a80-de.h>

/* Intermediary clock dividers are not exported */
#define CLK_FE0_DIV	31
#define CLK_FE1_DIV	32
#define CLK_FE2_DIV	33
#define CLK_BE0_DIV	34
#define CLK_BE1_DIV	35
#define CLK_BE2_DIV	36

#define CLK_NUMBER	(CLK_BE2_DIV + 1)

#endif /* _CCU_SUN9I_A80_DE_H_ */
