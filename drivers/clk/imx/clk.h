/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MACH_IMX_CLK_H
#define __MACH_IMX_CLK_H

#include <linux/spinlock.h>
#include <linux/clk-provider.h>

extern spinlock_t imx_ccm_lock;

void imx_check_clocks(struct clk *clks[], unsigned int count);
void imx_register_uart_clocks(struct clk ** const clks[]);

extern void imx_cscmr1_fixup(u32 *val);

enum imx_pllv1_type {
	IMX_PLLV1_IMX1,
	IMX_PLLV1_IMX21,
	IMX_PLLV1_IMX25,
	IMX_PLLV1_IMX27,
	IMX_PLLV1_IMX31,
	IMX_PLLV1_IMX35,
};

struct clk *imx_clk_pllv1(enum imx_pllv1_type type, const char *name,
		const char *parent, void __iomem *base);

struct clk *imx_clk_pllv2(const char *name, const char *parent,
		void __iomem *base);

enum imx_pllv3_type {
	IMX_PLLV3_GENERIC,
	IMX_PLLV3_SYS,
	IMX_PLLV3_USB,
	IMX_PLLV3_USB_VF610,
	IMX_PLLV3_AV,
	IMX_PLLV3_ENET,
	IMX_PLLV3_ENET_IMX7,
	IMX_PLLV3_SYS_VF610,
	IMX_PLLV3_DDR_IMX7,
};

struct clk *imx_clk_pllv3(enum imx_pllv3_type type, const char *name,
		const char *parent_name, void __iomem *base, u32 div_mask);

struct clk *clk_register_gate2(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, u8 cgr_val,
		u8 clk_gate_flags, spinlock_t *lock,
		unsigned int *share_count);

struct clk * imx_obtain_fixed_clock(
			const char *name, unsigned long rate);

struct clk *imx_clk_gate_exclusive(const char *name, const char *parent,
	 void __iomem *reg, u8 shift, u32 exclusive_mask);

struct clk *imx_clk_pfd(const char *name, const char *parent_name,
		void __iomem *reg, u8 idx);

struct clk *imx_clk_busy_divider(const char *name, const char *parent_name,
				 void __iomem *reg, u8 shift, u8 width,
				 void __iomem *busy_reg, u8 busy_shift);

struct clk *imx_clk_busy_mux(const char *name, void __iomem *reg, u8 shift,
			     u8 width, void __iomem *busy_reg, u8 busy_shift,
			     const char **parent_names, int num_parents);

struct clk *imx_clk_fixup_divider(const char *name, const char *parent,
				  void __iomem *reg, u8 shift, u8 width,
				  void (*fixup)(u32 *val));

struct clk *imx_clk_fixup_mux(const char *name, void __iomem *reg,
			      u8 shift, u8 width, const char **parents,
			      int num_parents, void (*fixup)(u32 *val));

static inline struct clk *imx_clk_fixed(const char *name, int rate)
{
	return clk_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static inline struct clk *imx_clk_mux_ldb(const char *name, void __iomem *reg,
		u8 shift, u8 width, const char **parents, int num_parents)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT, reg,
			shift, width, CLK_MUX_READ_ONLY, &imx_ccm_lock);
}

static inline struct clk *imx_clk_fixed_factor(const char *name,
		const char *parent, unsigned int mult, unsigned int div)
{
	return clk_register_fixed_factor(NULL, name, parent,
			CLK_SET_RATE_PARENT, mult, div);
}

static inline struct clk *imx_clk_divider(const char *name, const char *parent,
		void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent, CLK_SET_RATE_PARENT,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_divider_flags(const char *name,
		const char *parent, void __iomem *reg, u8 shift, u8 width,
		unsigned long flags)
{
	return clk_register_divider(NULL, name, parent, flags,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_divider2(const char *name, const char *parent,
		void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate_flags(const char *name, const char *parent,
		void __iomem *reg, u8 shift, unsigned long flags)
{
	return clk_register_gate(NULL, name, parent, flags | CLK_SET_RATE_PARENT, reg,
			shift, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate_dis(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, CLK_GATE_SET_TO_DISABLE, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate2(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk *imx_clk_gate2_flags(const char *name, const char *parent,
		void __iomem *reg, u8 shift, unsigned long flags)
{
	return clk_register_gate2(NULL, name, parent, flags | CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk *imx_clk_gate2_shared(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned int *share_count)
{
	return clk_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, share_count);
}

static inline struct clk *imx_clk_gate2_shared2(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned int *share_count)
{
	return clk_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT |
				  CLK_OPS_PARENT_ENABLE, reg, shift, 0x3, 0,
				  &imx_ccm_lock, share_count);
}

static inline struct clk *imx_clk_gate2_cgr(const char *name,
		const char *parent, void __iomem *reg, u8 shift, u8 cgr_val)
{
	return clk_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, cgr_val, 0, &imx_ccm_lock, NULL);
}

static inline struct clk *imx_clk_gate3(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate4(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate2(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk *imx_clk_mux(const char *name, void __iomem *reg,
		u8 shift, u8 width, const char **parents, int num_parents)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT, reg, shift,
			width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_mux2(const char *name, void __iomem *reg,
		u8 shift, u8 width, const char **parents, int num_parents)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_mux_flags(const char *name,
		void __iomem *reg, u8 shift, u8 width, const char **parents,
		int num_parents, unsigned long flags)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			flags | CLK_SET_RATE_NO_REPARENT, reg, shift, width, 0,
			&imx_ccm_lock);
}

struct clk *imx_clk_cpu(const char *name, const char *parent_name,
		struct clk *div, struct clk *mux, struct clk *pll,
		struct clk *step);

#endif
