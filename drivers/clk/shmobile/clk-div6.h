#ifndef __SHMOBILE_CLK_DIV6_H__
#define __SHMOBILE_CLK_DIV6_H__

struct clk *cpg_div6_register(const char *name, unsigned int num_parents,
			      const char **parent_names, void __iomem *reg);

#endif
