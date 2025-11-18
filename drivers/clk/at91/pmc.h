/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drivers/clk/at91/pmc.h
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 */

#ifndef __PMC_H_
#define __PMC_H_

#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/at91.h>

extern spinlock_t pmc_pcr_lock;

struct pmc_data {
	unsigned int ncore;
	struct clk_hw **chws;
	unsigned int nsystem;
	struct clk_hw **shws;
	unsigned int nperiph;
	struct clk_hw **phws;
	unsigned int ngck;
	struct clk_hw **ghws;
	unsigned int npck;
	struct clk_hw **pchws;

	struct clk_hw *hwtable[];
};

struct clk_range {
	unsigned long min;
	unsigned long max;
};

#define CLK_RANGE(MIN, MAX) {.min = MIN, .max = MAX,}

struct clk_master_layout {
	u32 offset;
	u32 mask;
	u8 pres_shift;
};

extern const struct clk_master_layout at91rm9200_master_layout;
extern const struct clk_master_layout at91sam9x5_master_layout;

struct clk_master_characteristics {
	struct clk_range output;
	u32 divisors[5];
	u8 have_div3_pres;
};

struct clk_pll_layout {
	u32 pllr_mask;
	u32 mul_mask;
	u32 frac_mask;
	u32 div_mask;
	u32 endiv_mask;
	u8 mul_shift;
	u8 frac_shift;
	u8 div_shift;
	u8 endiv_shift;
	u8 div2;
};

extern const struct clk_pll_layout at91rm9200_pll_layout;
extern const struct clk_pll_layout at91sam9g45_pll_layout;
extern const struct clk_pll_layout at91sam9g20_pllb_layout;
extern const struct clk_pll_layout sama5d3_pll_layout;

struct clk_pll_characteristics {
	struct clk_range input;
	int num_output;
	const struct clk_range *output;
	const struct clk_range *core_output;
	u16 *icpll;
	u8 *out;
	u8 upll : 1;
	u32 acr;
};

struct clk_programmable_layout {
	u8 pres_mask;
	u8 pres_shift;
	u8 css_mask;
	u8 have_slck_mck;
	u8 is_pres_direct;
};

extern const struct clk_programmable_layout at91rm9200_programmable_layout;
extern const struct clk_programmable_layout at91sam9g45_programmable_layout;
extern const struct clk_programmable_layout at91sam9x5_programmable_layout;

struct clk_pcr_layout {
	u32 offset;
	u32 cmd;
	u32 div_mask;
	u32 gckcss_mask;
	u32 pid_mask;
};

/**
 * struct at91_clk_pms - Power management state for AT91 clock
 * @rate: clock rate
 * @parent_rate: clock parent rate
 * @status: clock status (enabled or disabled)
 * @parent: clock parent index
 */
struct at91_clk_pms {
	unsigned long rate;
	unsigned long parent_rate;
	unsigned int status;
	unsigned int parent;
};

#define field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define field_prep(_mask, _val) (((_val) << (ffs(_mask) - 1)) & (_mask))

#define ndck(a, s) (a[s - 1].id + 1)
#define nck(a) (a[ARRAY_SIZE(a) - 1].id + 1)

#define PMC_INIT_TABLE(_table, _count)			\
	do {						\
		u8 _i;					\
		for (_i = 0; _i < (_count); _i++)	\
			(_table)[_i] = _i;		\
	} while (0)

#define PMC_FILL_TABLE(_to, _from, _count)		\
	do {						\
		u8 _i;					\
		for (_i = 0; _i < (_count); _i++) {	\
			(_to)[_i] = (_from)[_i];	\
		}					\
	} while (0)

struct pmc_data *pmc_data_allocate(unsigned int ncore, unsigned int nsystem,
				   unsigned int nperiph, unsigned int ngck,
				   unsigned int npck);

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range);

struct clk_hw *of_clk_hw_pmc_get(struct of_phandle_args *clkspec, void *data);

struct clk_hw * __init
at91_clk_register_audio_pll_frac(struct regmap *regmap, const char *name,
				 const char *parent_name);

struct clk_hw * __init
at91_clk_register_audio_pll_pad(struct regmap *regmap, const char *name,
				const char *parent_name);

struct clk_hw * __init
at91_clk_register_audio_pll_pmc(struct regmap *regmap, const char *name,
				const char *parent_name);

struct clk_hw * __init
at91_clk_register_generated(struct regmap *regmap, spinlock_t *lock,
			    const struct clk_pcr_layout *layout,
			    const char *name, const char **parent_names,
			    struct clk_hw **parent_hws, u32 *mux_table,
			    u8 num_parents, u8 id,
			    const struct clk_range *range, int chg_pid);

struct clk_hw * __init
at91_clk_register_h32mx(struct regmap *regmap, const char *name,
			const char *parent_name);

struct clk_hw * __init
at91_clk_i2s_mux_register(struct regmap *regmap, const char *name,
			  const char * const *parent_names,
			  unsigned int num_parents, u8 bus_id);

struct clk_hw * __init
at91_clk_register_main_rc_osc(struct regmap *regmap, const char *name,
			      u32 frequency, u32 accuracy);
struct clk_hw * __init
at91_clk_register_main_osc(struct regmap *regmap, const char *name,
			   const char *parent_name,
			   struct clk_parent_data *parent_data, bool bypass);
struct clk_hw * __init
at91_clk_register_rm9200_main(struct regmap *regmap,
			      const char *name,
			      const char *parent_name,
			      struct clk_hw *parent_hw);
struct clk_hw * __init
at91_clk_register_sam9x5_main(struct regmap *regmap, const char *name,
			      const char **parent_names,
			      struct clk_hw **parent_hws, int num_parents);

struct clk_hw * __init
at91_clk_register_master_pres(struct regmap *regmap, const char *name,
			      int num_parents, const char **parent_names,
			      struct clk_hw **parent_hws,
			      const struct clk_master_layout *layout,
			      const struct clk_master_characteristics *characteristics,
			      spinlock_t *lock);

struct clk_hw * __init
at91_clk_register_master_div(struct regmap *regmap, const char *name,
			     const char *parent_names, struct clk_hw *parent_hw,
			     const struct clk_master_layout *layout,
			     const struct clk_master_characteristics *characteristics,
			     spinlock_t *lock, u32 flags, u32 safe_div);

struct clk_hw * __init
at91_clk_sama7g5_register_master(struct regmap *regmap,
				 const char *name, int num_parents,
				 const char **parent_names,
				 struct clk_hw **parent_hws, u32 *mux_table,
				 spinlock_t *lock, u8 id, bool critical,
				 int chg_pid);

struct clk_hw * __init
at91_clk_register_peripheral(struct regmap *regmap, const char *name,
			     const char *parent_name, struct clk_hw *parent_hw,
			     u32 id);
struct clk_hw * __init
at91_clk_register_sam9x5_peripheral(struct regmap *regmap, spinlock_t *lock,
				    const struct clk_pcr_layout *layout,
				    const char *name, const char *parent_name,
				    struct clk_hw *parent_hw,
				    u32 id, const struct clk_range *range,
				    int chg_pid, unsigned long flags);

struct clk_hw * __init
at91_clk_register_pll(struct regmap *regmap, const char *name,
		      const char *parent_name, u8 id,
		      const struct clk_pll_layout *layout,
		      const struct clk_pll_characteristics *characteristics);
struct clk_hw * __init
at91_clk_register_plldiv(struct regmap *regmap, const char *name,
			 const char *parent_name);

struct clk_hw * __init
sam9x60_clk_register_div_pll(struct regmap *regmap, spinlock_t *lock,
			     const char *name, const char *parent_name,
			     struct clk_hw *parent_hw, u8 id,
			     const struct clk_pll_characteristics *characteristics,
			     const struct clk_pll_layout *layout, u32 flags,
			     u32 safe_div);

struct clk_hw * __init
sam9x60_clk_register_frac_pll(struct regmap *regmap, spinlock_t *lock,
			      const char *name, const char *parent_name,
			      struct clk_hw *parent_hw, u8 id,
			      const struct clk_pll_characteristics *characteristics,
			      const struct clk_pll_layout *layout, u32 flags);

struct clk_hw * __init
at91_clk_register_programmable(struct regmap *regmap, const char *name,
			       const char **parent_names, struct clk_hw **parent_hws,
			       u8 num_parents, u8 id,
			       const struct clk_programmable_layout *layout,
			       u32 *mux_table);

struct clk_hw * __init
at91_clk_register_sam9260_slow(struct regmap *regmap,
			       const char *name,
			       const char **parent_names,
			       int num_parents);

struct clk_hw * __init
at91sam9x5_clk_register_smd(struct regmap *regmap, const char *name,
			    const char **parent_names, u8 num_parents);

struct clk_hw * __init
at91_clk_register_system(struct regmap *regmap, const char *name,
			 const char *parent_name, struct clk_hw *parent_hw,
			 u8 id, unsigned long flags);

struct clk_hw * __init
at91sam9x5_clk_register_usb(struct regmap *regmap, const char *name,
			    const char **parent_names, u8 num_parents);
struct clk_hw * __init
at91sam9n12_clk_register_usb(struct regmap *regmap, const char *name,
			     const char *parent_name);
struct clk_hw * __init
sam9x60_clk_register_usb(struct regmap *regmap, const char *name,
			 const char **parent_names, u8 num_parents);
struct clk_hw * __init
at91rm9200_clk_register_usb(struct regmap *regmap, const char *name,
			    const char *parent_name, const u32 *divisors);

struct clk_hw * __init
at91_clk_register_utmi(struct regmap *regmap_pmc, struct regmap *regmap_sfr,
		       const char *name, const char *parent_name,
		       struct clk_hw *parent_hw);

struct clk_hw * __init
at91_clk_sama7g5_register_utmi(struct regmap *regmap, const char *name,
			       const char *parent_name, struct clk_hw *parent_hw);

#endif /* __PMC_H_ */
