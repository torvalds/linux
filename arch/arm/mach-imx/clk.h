#ifndef __MACH_IMX_CLK_H
#define __MACH_IMX_CLK_H

#include <linux/spinlock.h>
#include <linux/clk-provider.h>
#include <mach/clock.h>

struct clk *imx_clk_pllv1(const char *name, const char *parent,
		void __iomem *base);

struct clk *imx_clk_pllv2(const char *name, const char *parent,
		void __iomem *base);

enum imx_pllv3_type {
	IMX_PLLV3_GENERIC,
	IMX_PLLV3_SYS,
	IMX_PLLV3_USB,
	IMX_PLLV3_AV,
	IMX_PLLV3_ENET,
	IMX_PLLV3_MLB,
};

struct clk *imx_clk_pllv3(enum imx_pllv3_type type, const char *name,
		const char *parent_name, void __iomem *base, u32 gate_mask,
		u32 div_mask);

static inline struct clk *imx_clk_fixed(const char *name, int rate)
{
	return clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, rate);
}

static inline struct clk *imx_clk_divider(const char *name, const char *parent,
		void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent, CLK_SET_RATE_PARENT,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_mux(const char *name, void __iomem *reg,
		u8 shift, u8 width, const char **parents, int num_parents)
{
	return clk_register_mux(NULL, name, parents, num_parents, 0, reg, shift,
			width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_fixed_factor(const char *name,
		const char *parent, unsigned int mult, unsigned int div)
{
	return clk_register_fixed_factor(NULL, name, parent,
			CLK_SET_RATE_PARENT, mult, div);
}

#endif
