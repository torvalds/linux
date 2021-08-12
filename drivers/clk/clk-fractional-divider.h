/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CLK_FRACTIONAL_DIV_H
#define _CLK_FRACTIONAL_DIV_H

struct clk_hw;

void clk_fractional_divider_general_approximation(struct clk_hw *hw,
						  unsigned long rate,
						  unsigned long *parent_rate,
						  unsigned long *m,
						  unsigned long *n);

#endif
