#ifndef __MACH_CLOCK_H__
#define __MACH_CLOCK_H__

#ifndef CONFIG_ARCH_RK30
#define RK30_CLK_OFFBOARD_TEST
#endif


/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define IS_PD			(1 << 2)	/* Power Domain */

enum _clk_i2s_rate_support {
	i2s_8192khz = 8192000,
	i2s_11289_6khz = 11289600,
	i2s_12288khz = 12288000,
	i2s_22579_2khz = 22579200,
	i2s_24576khz = 24576000,//HDMI
	i2s_49152khz = 24576000,//HDMI
};

struct _pll_data{
	u8 id;
	void *table;
};
//struct clk_node;
struct clk {
	struct list_head	node;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	
	int			(*mode)(struct clk *clk, int on);
	unsigned long		(*recalc)(struct clk *);	/* if null, follow parent */
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	struct clk*		(*get_parent)(struct clk *);	/* get clk's parent from the hardware. default is clksel_get_parent if parents present */
	int			(*set_parent)(struct clk *, struct clk *);	/* default is clksel_set_parent if parents present */

	unsigned long		rate;
	u32			flags;
	s16			usecount;
	u16			notifier_count;
	u8			gate_idx;
	struct _pll_data *pll;
	u32			clksel_con;
	u32			div_mask;
	u32			div_shift;
	u32			div_max;
	u32			src_mask;
	u32			src_shift;
	
	struct clk	**parents;
	u8	parents_num;
	struct clk_node	*dvfs_info;
	
};

int __init clk_disable_unused(void);
void clk_recalculate_root_clocks_nolock(void);
void clk_recalculate_root_clocks(void);
int clk_register(struct clk *clk);
void clk_register_default_ops_clk(struct clk *clk);

int clk_enable_nolock(struct clk *clk);
void clk_disable_nolock(struct clk *clk);
long clk_round_rate_nolock(struct clk *clk, unsigned long rate);
int clk_set_rate_nolock(struct clk *clk, unsigned long rate);
int clk_set_parent_nolock(struct clk *clk, struct clk *parent);
int clk_set_rate_locked(struct clk * clk,unsigned long rate);
void clk_register_dvfs(struct clk_node *dvfs_clk, struct clk *clk);
struct clk_node *clk_get_dvfs_info(struct clk *clk);
int is_suport_round_rate(struct clk *clk);

#ifdef RK30_CLK_OFFBOARD_TEST
#include <linux/device.h>
struct clk *rk30_clk_get(struct device *dev, const char *con_id);
#endif

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

struct clk_dump_ops {
	void (*dump_clk)(struct seq_file *s, struct clk *clk, int deep,const struct list_head *root_clocks);
	void (*dump_regs)(struct seq_file *s);
};

void clk_register_dump_ops(struct clk_dump_ops *ops);
#else
static void clk_register_dump_ops(struct clk_dump_ops *ops){
}

#endif

#endif
