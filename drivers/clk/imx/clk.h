/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MACH_IMX_CLK_H
#define __MACH_IMX_CLK_H

#include <linux/spinlock.h>
#include <linux/clk-provider.h>

extern spinlock_t imx_ccm_lock;

void imx_check_clocks(struct clk *clks[], unsigned int count);
void imx_check_clk_hws(struct clk_hw *clks[], unsigned int count);
void imx_register_uart_clocks(struct clk ** const clks[]);
void imx_mmdc_mask_handshake(void __iomem *ccm_base, unsigned int chn);
void imx_unregister_clocks(struct clk *clks[], unsigned int count);

extern void imx_cscmr1_fixup(u32 *val);

enum imx_pllv1_type {
	IMX_PLLV1_IMX1,
	IMX_PLLV1_IMX21,
	IMX_PLLV1_IMX25,
	IMX_PLLV1_IMX27,
	IMX_PLLV1_IMX31,
	IMX_PLLV1_IMX35,
};

enum imx_sccg_pll_type {
	SCCG_PLL1,
	SCCG_PLL2,
};

enum imx_pll14xx_type {
	PLL_1416X,
	PLL_1443X,
};

/* NOTE: Rate table should be kept sorted in descending order. */
struct imx_pll14xx_rate_table {
	unsigned int rate;
	unsigned int pdiv;
	unsigned int mdiv;
	unsigned int sdiv;
	unsigned int kdiv;
};

struct imx_pll14xx_clk {
	enum imx_pll14xx_type type;
	const struct imx_pll14xx_rate_table *rate_table;
	int rate_count;
	int flags;
};

extern struct imx_pll14xx_clk imx_1416x_pll;
extern struct imx_pll14xx_clk imx_1443x_pll;
extern struct imx_pll14xx_clk imx_1443x_dram_pll;

#define imx_clk_cpu(name, parent_name, div, mux, pll, step) \
	imx_clk_hw_cpu(name, parent_name, div, mux, pll, step)->clk

#define clk_register_gate2(dev, name, parent_name, flags, reg, bit_idx, \
				cgr_val, clk_gate_flags, lock, share_count) \
	clk_hw_register_gate2(dev, name, parent_name, flags, reg, bit_idx, \
				cgr_val, clk_gate_flags, lock, share_count)->clk

#define imx_clk_pllv3(type, name, parent_name, base, div_mask) \
	imx_clk_hw_pllv3(type, name, parent_name, base, div_mask)->clk

#define imx_clk_pfd(name, parent_name, reg, idx) \
	imx_clk_hw_pfd(name, parent_name, reg, idx)->clk

#define imx_clk_gate_exclusive(name, parent, reg, shift, exclusive_mask) \
	imx_clk_hw_gate_exclusive(name, parent, reg, shift, exclusive_mask)->clk

#define imx_clk_fixed_factor(name, parent, mult, div) \
	imx_clk_hw_fixed_factor(name, parent, mult, div)->clk

#define imx_clk_divider2(name, parent, reg, shift, width) \
	imx_clk_hw_divider2(name, parent, reg, shift, width)->clk

#define imx_clk_gate_dis(name, parent, reg, shift) \
	imx_clk_hw_gate_dis(name, parent, reg, shift)->clk

#define imx_clk_gate2(name, parent, reg, shift) \
	imx_clk_hw_gate2(name, parent, reg, shift)->clk

#define imx_clk_gate2_flags(name, parent, reg, shift, flags) \
	imx_clk_hw_gate2_flags(name, parent, reg, shift, flags)->clk

#define imx_clk_gate2_shared2(name, parent, reg, shift, share_count) \
	imx_clk_hw_gate2_shared2(name, parent, reg, shift, share_count)->clk

#define imx_clk_gate3(name, parent, reg, shift) \
	imx_clk_hw_gate3(name, parent, reg, shift)->clk

#define imx_clk_gate4(name, parent, reg, shift) \
	imx_clk_hw_gate4(name, parent, reg, shift)->clk

#define imx_clk_mux(name, reg, shift, width, parents, num_parents) \
	imx_clk_hw_mux(name, reg, shift, width, parents, num_parents)->clk

struct clk *imx_clk_pll14xx(const char *name, const char *parent_name,
		 void __iomem *base, const struct imx_pll14xx_clk *pll_clk);

struct clk *imx_clk_pllv1(enum imx_pllv1_type type, const char *name,
		const char *parent, void __iomem *base);

struct clk *imx_clk_pllv2(const char *name, const char *parent,
		void __iomem *base);

struct clk *imx_clk_frac_pll(const char *name, const char *parent_name,
			     void __iomem *base);

struct clk *imx_clk_sccg_pll(const char *name,
				const char * const *parent_names,
				u8 num_parents,
				u8 parent, u8 bypass1, u8 bypass2,
				void __iomem *base,
				unsigned long flags);

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
	IMX_PLLV3_AV_IMX7,
};

struct clk_hw *imx_clk_hw_pllv3(enum imx_pllv3_type type, const char *name,
		const char *parent_name, void __iomem *base, u32 div_mask);

#define PLL_1416X_RATE(_rate, _m, _p, _s)		\
	{						\
		.rate	=	(_rate),		\
		.mdiv	=	(_m),			\
		.pdiv	=	(_p),			\
		.sdiv	=	(_s),			\
	}

#define PLL_1443X_RATE(_rate, _m, _p, _s, _k)		\
	{						\
		.rate	=	(_rate),		\
		.mdiv	=	(_m),			\
		.pdiv	=	(_p),			\
		.sdiv	=	(_s),			\
		.kdiv	=	(_k),			\
	}

struct clk_hw *imx_clk_pllv4(const char *name, const char *parent_name,
			     void __iomem *base);

struct clk_hw *clk_hw_register_gate2(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, u8 cgr_val,
		u8 clk_gate_flags, spinlock_t *lock,
		unsigned int *share_count);

struct clk * imx_obtain_fixed_clock(
			const char *name, unsigned long rate);

struct clk_hw *imx_obtain_fixed_clock_hw(
			const char *name, unsigned long rate);

struct clk_hw *imx_obtain_fixed_clk_hw(struct device_node *np,
				       const char *name);

struct clk_hw *imx_clk_hw_gate_exclusive(const char *name, const char *parent,
	 void __iomem *reg, u8 shift, u32 exclusive_mask);

struct clk_hw *imx_clk_hw_pfd(const char *name, const char *parent_name,
		void __iomem *reg, u8 idx);

struct clk_hw *imx_clk_pfdv2(const char *name, const char *parent_name,
			     void __iomem *reg, u8 idx);

struct clk_hw *imx_clk_hw_busy_divider(const char *name, const char *parent_name,
				 void __iomem *reg, u8 shift, u8 width,
				 void __iomem *busy_reg, u8 busy_shift);

struct clk_hw *imx_clk_hw_busy_mux(const char *name, void __iomem *reg, u8 shift,
			     u8 width, void __iomem *busy_reg, u8 busy_shift,
			     const char * const *parent_names, int num_parents);

struct clk_hw *imx7ulp_clk_composite(const char *name,
				     const char * const *parent_names,
				     int num_parents, bool mux_present,
				     bool rate_present, bool gate_present,
				     void __iomem *reg);

struct clk_hw *imx_clk_hw_fixup_divider(const char *name, const char *parent,
				  void __iomem *reg, u8 shift, u8 width,
				  void (*fixup)(u32 *val));

struct clk_hw *imx_clk_hw_fixup_mux(const char *name, void __iomem *reg,
			      u8 shift, u8 width, const char * const *parents,
			      int num_parents, void (*fixup)(u32 *val));

static inline struct clk *imx_clk_fixed(const char *name, int rate)
{
	return clk_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static inline struct clk_hw *imx_clk_hw_fixed(const char *name, int rate)
{
	return clk_hw_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static inline struct clk_hw *imx_clk_hw_mux_ldb(const char *name, void __iomem *reg,
			u8 shift, u8 width, const char * const *parents,
			int num_parents)
{
	return clk_hw_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT, reg,
			shift, width, CLK_MUX_READ_ONLY, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_fixed_factor(const char *name,
		const char *parent, unsigned int mult, unsigned int div)
{
	return clk_hw_register_fixed_factor(NULL, name, parent,
			CLK_SET_RATE_PARENT, mult, div);
}

static inline struct clk *imx_clk_divider(const char *name, const char *parent,
		void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent, CLK_SET_RATE_PARENT,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_divider(const char *name,
						const char *parent,
						void __iomem *reg, u8 shift,
						u8 width)
{
	return clk_hw_register_divider(NULL, name, parent, CLK_SET_RATE_PARENT,
				       reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_divider_flags(const char *name,
		const char *parent, void __iomem *reg, u8 shift, u8 width,
		unsigned long flags)
{
	return clk_register_divider(NULL, name, parent, flags,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_divider_flags(const char *name,
						   const char *parent,
						   void __iomem *reg, u8 shift,
						   u8 width, unsigned long flags)
{
	return clk_hw_register_divider(NULL, name, parent, flags,
				       reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_divider2(const char *name, const char *parent,
		void __iomem *reg, u8 shift, u8 width)
{
	return clk_hw_register_divider(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_divider2_flags(const char *name,
		const char *parent, void __iomem *reg, u8 shift, u8 width,
		unsigned long flags)
{
	return clk_register_divider(NULL, name, parent,
			flags | CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate_flags(const char *name, const char *parent,
		void __iomem *reg, u8 shift, unsigned long flags)
{
	return clk_hw_register_gate(NULL, name, parent, flags | CLK_SET_RATE_PARENT, reg,
			shift, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate(const char *name, const char *parent,
					     void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
				    shift, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate_dis(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, CLK_GATE_SET_TO_DISABLE, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate_dis_flags(const char *name, const char *parent,
		void __iomem *reg, u8 shift, unsigned long flags)
{
	return clk_hw_register_gate(NULL, name, parent, flags | CLK_SET_RATE_PARENT, reg,
			shift, CLK_GATE_SET_TO_DISABLE, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate2(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk_hw *imx_clk_hw_gate2_flags(const char *name, const char *parent,
		void __iomem *reg, u8 shift, unsigned long flags)
{
	return clk_hw_register_gate2(NULL, name, parent, flags | CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk_hw *imx_clk_hw_gate2_shared(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned int *share_count)
{
	return clk_hw_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, 0x3, 0, &imx_ccm_lock, share_count);
}

static inline struct clk_hw *imx_clk_hw_gate2_shared2(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned int *share_count)
{
	return clk_hw_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT |
				  CLK_OPS_PARENT_ENABLE, reg, shift, 0x3, 0,
				  &imx_ccm_lock, share_count);
}

static inline struct clk *imx_clk_gate2_cgr(const char *name,
		const char *parent, void __iomem *reg, u8 shift, u8 cgr_val)
{
	return clk_register_gate2(NULL, name, parent, CLK_SET_RATE_PARENT, reg,
			shift, cgr_val, 0, &imx_ccm_lock, NULL);
}

static inline struct clk_hw *imx_clk_hw_gate3(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_gate3_flags(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned long flags)
{
	return clk_register_gate(NULL, name, parent,
			flags | CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_gate4(const char *name, const char *parent,
		void __iomem *reg, u8 shift)
{
	return clk_hw_register_gate2(NULL, name, parent,
			CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk *imx_clk_gate4_flags(const char *name,
		const char *parent, void __iomem *reg, u8 shift,
		unsigned long flags)
{
	return clk_register_gate2(NULL, name, parent,
			flags | CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, 0x3, 0, &imx_ccm_lock, NULL);
}

static inline struct clk_hw *imx_clk_hw_mux(const char *name, void __iomem *reg,
			u8 shift, u8 width, const char * const *parents,
			int num_parents)
{
	return clk_hw_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT, reg, shift,
			width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_mux2(const char *name, void __iomem *reg,
			u8 shift, u8 width, const char * const *parents,
			int num_parents)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			CLK_SET_RATE_NO_REPARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_mux2(const char *name, void __iomem *reg,
					     u8 shift, u8 width,
					     const char * const *parents,
					     int num_parents)
{
	return clk_hw_register_mux(NULL, name, parents, num_parents,
				   CLK_SET_RATE_NO_REPARENT |
				   CLK_OPS_PARENT_ENABLE,
				   reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk *imx_clk_mux_flags(const char *name,
			void __iomem *reg, u8 shift, u8 width,
			const char * const *parents, int num_parents,
			unsigned long flags)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			flags | CLK_SET_RATE_NO_REPARENT, reg, shift, width, 0,
			&imx_ccm_lock);
}

static inline struct clk *imx_clk_mux2_flags(const char *name,
		void __iomem *reg, u8 shift, u8 width,
		const char * const *parents,
		int num_parents, unsigned long flags)
{
	return clk_register_mux(NULL, name, parents, num_parents,
			flags | CLK_SET_RATE_NO_REPARENT | CLK_OPS_PARENT_ENABLE,
			reg, shift, width, 0, &imx_ccm_lock);
}

static inline struct clk_hw *imx_clk_hw_mux_flags(const char *name,
						  void __iomem *reg, u8 shift,
						  u8 width,
						  const char * const *parents,
						  int num_parents,
						  unsigned long flags)
{
	return clk_hw_register_mux(NULL, name, parents, num_parents,
				   flags | CLK_SET_RATE_NO_REPARENT,
				   reg, shift, width, 0, &imx_ccm_lock);
}

struct clk_hw *imx_clk_hw_cpu(const char *name, const char *parent_name,
		struct clk *div, struct clk *mux, struct clk *pll,
		struct clk *step);

struct clk *imx8m_clk_composite_flags(const char *name,
					const char * const *parent_names,
					int num_parents, void __iomem *reg,
					unsigned long flags);

#define __imx8m_clk_composite(name, parent_names, reg, flags) \
	imx8m_clk_composite_flags(name, parent_names, \
		ARRAY_SIZE(parent_names), reg, \
		flags | CLK_SET_RATE_NO_REPARENT | CLK_OPS_PARENT_ENABLE)

#define imx8m_clk_composite(name, parent_names, reg) \
	__imx8m_clk_composite(name, parent_names, reg, 0)

#define imx8m_clk_composite_critical(name, parent_names, reg) \
	__imx8m_clk_composite(name, parent_names, reg, CLK_IS_CRITICAL)

struct clk_hw *imx_clk_divider_gate(const char *name, const char *parent_name,
		unsigned long flags, void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, const struct clk_div_table *table,
		spinlock_t *lock);
#endif
