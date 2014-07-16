/*
 * linux/drivers/clk/clk.h
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if defined(CONFIG_OF) && defined(CONFIG_COMMON_CLK)
struct clk *of_clk_get_by_clkspec(struct of_phandle_args *clkspec);
struct clk *__of_clk_get_from_provider(struct of_phandle_args *clkspec);
void of_clk_lock(void);
void of_clk_unlock(void);
#endif
