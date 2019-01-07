/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

struct clk_hw;

#if defined(CONFIG_OF) && defined(CONFIG_COMMON_CLK)
struct clk *__of_clk_get_from_provider(struct of_phandle_args *clkspec,
				       const char *dev_id, const char *con_id);
#endif

#ifdef CONFIG_COMMON_CLK
struct clk *__clk_create_clk(struct clk_hw *hw, const char *dev_id,
			     const char *con_id);
void __clk_free_clk(struct clk *clk);
int __clk_get(struct clk *clk);
void __clk_put(struct clk *clk);
#else
/* All these casts to avoid ifdefs in clkdev... */
static inline struct clk *
__clk_create_clk(struct clk_hw *hw, const char *dev_id, const char *con_id)
{
	return (struct clk *)hw;
}
static inline void __clk_free_clk(struct clk *clk) { }
static struct clk_hw *__clk_get_hw(struct clk *clk)
{
	return (struct clk_hw *)clk;
}
static inline int __clk_get(struct clk *clk) { return 1; }
static inline void __clk_put(struct clk *clk) { }

#endif
