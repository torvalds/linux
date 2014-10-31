#ifndef __MACH_MMP_CLK_H
#define __MACH_MMP_CLK_H

#include <linux/clk-provider.h>
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
};

struct mmp_clk_factor_tbl {
	unsigned int num;
	unsigned int den;
};

struct mmp_clk_factor {
	struct clk_hw hw;
	void __iomem *base;
	struct mmp_clk_factor_masks *masks;
	struct mmp_clk_factor_tbl *ftbl;
	unsigned int ftbl_cnt;
	spinlock_t *lock;
};

extern struct clk *mmp_clk_register_factor(const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *base, struct mmp_clk_factor_masks *masks,
		struct mmp_clk_factor_tbl *ftbl, unsigned int ftbl_cnt,
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
					u8 num_parents,
					const char **parent_names,
					unsigned long flags,
					struct mmp_clk_mix_config *config,
					spinlock_t *lock);


extern struct clk *mmp_clk_register_pll2(const char *name,
		const char *parent_name, unsigned long flags);
extern struct clk *mmp_clk_register_apbc(const char *name,
		const char *parent_name, void __iomem *base,
		unsigned int delay, unsigned int apbc_flags, spinlock_t *lock);
extern struct clk *mmp_clk_register_apmu(const char *name,
		const char *parent_name, void __iomem *base, u32 enable_mask,
		spinlock_t *lock);
#endif
