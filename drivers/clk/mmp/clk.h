/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MACH_MMP_CLK_H
#define __MACH_MMP_CLK_H

#include <linux/clk-provider.h>
#include <linux/math.h>
#include <linux/pm_domain.h>
#include <linux/clkdev.h>

#define APBC_NO_BUS_CTRL	BIT(0)
#define APBC_POWER_CTRL		BIT(1)


/* Clock type "factor" */
struct mmp_clk_factor_masks {
	unsigned int factor;
	unsigned int num_mask;
	unsigned int den_mask;
	unsigned int num_shift;
	unsigned int den_shift;
	unsigned int enable_mask;
};

struct mmp_clk_factor {
	struct clk_hw hw;
	void __iomem *base;
	struct mmp_clk_factor_masks *masks;
	struct u32_fract *ftbl;
	unsigned int ftbl_cnt;
	spinlock_t *lock;
};

extern struct clk *mmp_clk_register_factor(const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *base, struct mmp_clk_factor_masks *masks,
		struct u32_fract *ftbl, unsigned int ftbl_cnt,
		spinlock_t *lock);

/* Clock type "mix" */
#define MMP_CLK_BITS_MASK(width, shift)			\
		(((1 << (width)) - 1) << (shift))
#define MMP_CLK_BITS_GET_VAL(data, width, shift)	\
		((data & MMP_CLK_BITS_MASK(width, shift)) >> (shift))
#define MMP_CLK_BITS_SET_VAL(val, width, shift)		\
		(((val) << (shift)) & MMP_CLK_BITS_MASK(width, shift))

enum {
	MMP_CLK_MIX_TYPE_V1,
	MMP_CLK_MIX_TYPE_V2,
	MMP_CLK_MIX_TYPE_V3,
};

/* The register layout */
struct mmp_clk_mix_reg_info {
	void __iomem *reg_clk_ctrl;
	void __iomem *reg_clk_sel;
	u8 width_div;
	u8 shift_div;
	u8 width_mux;
	u8 shift_mux;
	u8 bit_fc;
};

/* The suggested clock table from user. */
struct mmp_clk_mix_clk_table {
	unsigned long rate;
	u8 parent_index;
	unsigned int divisor;
	unsigned int valid;
};

struct mmp_clk_mix_config {
	struct mmp_clk_mix_reg_info reg_info;
	struct mmp_clk_mix_clk_table *table;
	unsigned int table_size;
	u32 *mux_table;
	struct clk_div_table *div_table;
	u8 div_flags;
	u8 mux_flags;
};

struct mmp_clk_mix {
	struct clk_hw hw;
	struct mmp_clk_mix_reg_info reg_info;
	struct mmp_clk_mix_clk_table *table;
	u32 *mux_table;
	struct clk_div_table *div_table;
	unsigned int table_size;
	u8 div_flags;
	u8 mux_flags;
	unsigned int type;
	spinlock_t *lock;
};

extern const struct clk_ops mmp_clk_mix_ops;
extern struct clk *mmp_clk_register_mix(struct device *dev,
					const char *name,
					const char * const *parent_names,
					u8 num_parents,
					unsigned long flags,
					struct mmp_clk_mix_config *config,
					spinlock_t *lock);


/* Clock type "gate". MMP private gate */
#define MMP_CLK_GATE_NEED_DELAY		BIT(0)

struct mmp_clk_gate {
	struct clk_hw hw;
	void __iomem *reg;
	u32 mask;
	u32 val_enable;
	u32 val_disable;
	unsigned int flags;
	spinlock_t *lock;
};

extern const struct clk_ops mmp_clk_gate_ops;
extern struct clk *mmp_clk_register_gate(struct device *dev, const char *name,
			const char *parent_name, unsigned long flags,
			void __iomem *reg, u32 mask, u32 val_enable,
			u32 val_disable, unsigned int gate_flags,
			spinlock_t *lock);

extern struct clk *mmp_clk_register_apbc(const char *name,
		const char *parent_name, void __iomem *base,
		unsigned int delay, unsigned int apbc_flags, spinlock_t *lock);
extern struct clk *mmp_clk_register_apmu(const char *name,
		const char *parent_name, void __iomem *base, u32 enable_mask,
		spinlock_t *lock);

struct mmp_clk_unit {
	unsigned int nr_clks;
	struct clk **clk_table;
	struct clk_onecell_data clk_data;
};

struct mmp_param_fixed_rate_clk {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long fixed_rate;
};
void mmp_register_fixed_rate_clks(struct mmp_clk_unit *unit,
				struct mmp_param_fixed_rate_clk *clks,
				int size);

struct mmp_param_fixed_factor_clk {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long mult;
	unsigned long div;
	unsigned long flags;
};
void mmp_register_fixed_factor_clks(struct mmp_clk_unit *unit,
				struct mmp_param_fixed_factor_clk *clks,
				int size);

struct mmp_param_general_gate_clk {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 bit_idx;
	u8 gate_flags;
	spinlock_t *lock;
};
void mmp_register_general_gate_clks(struct mmp_clk_unit *unit,
				struct mmp_param_general_gate_clk *clks,
				void __iomem *base, int size);

struct mmp_param_gate_clk {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u32 mask;
	u32 val_enable;
	u32 val_disable;
	unsigned int gate_flags;
	spinlock_t *lock;
};
void mmp_register_gate_clks(struct mmp_clk_unit *unit,
			struct mmp_param_gate_clk *clks,
			void __iomem *base, int size);

struct mmp_param_mux_clk {
	unsigned int id;
	char *name;
	const char * const *parent_name;
	u8 num_parents;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u8 width;
	u8 mux_flags;
	spinlock_t *lock;
};
void mmp_register_mux_clks(struct mmp_clk_unit *unit,
			struct mmp_param_mux_clk *clks,
			void __iomem *base, int size);

struct mmp_param_div_clk {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u8 width;
	u8 div_flags;
	spinlock_t *lock;
};
void mmp_register_div_clks(struct mmp_clk_unit *unit,
			struct mmp_param_div_clk *clks,
			void __iomem *base, int size);

struct mmp_param_pll_clk {
	unsigned int id;
	char *name;
	unsigned long default_rate;
	unsigned long enable_offset;
	u32 enable;
	unsigned long offset;
	u8 shift;
	/* MMP3 specific: */
	unsigned long input_rate;
	unsigned long postdiv_offset;
	unsigned long postdiv_shift;
};
void mmp_register_pll_clks(struct mmp_clk_unit *unit,
			struct mmp_param_pll_clk *clks,
			void __iomem *base, int size);

#define DEFINE_MIX_REG_INFO(w_d, s_d, w_m, s_m, fc)	\
{							\
	.width_div = (w_d),				\
	.shift_div = (s_d),				\
	.width_mux = (w_m),				\
	.shift_mux = (s_m),				\
	.bit_fc = (fc),					\
}

void mmp_clk_init(struct device_node *np, struct mmp_clk_unit *unit,
		int nr_clks);
void mmp_clk_add(struct mmp_clk_unit *unit, unsigned int id,
		struct clk *clk);

/* Power islands */
#define MMP_PM_DOMAIN_NO_DISABLE		BIT(0)

struct generic_pm_domain *mmp_pm_domain_register(const char *name,
		void __iomem *reg,
		u32 power_on, u32 reset, u32 clock_enable,
		unsigned int flags, spinlock_t *lock);

#endif
