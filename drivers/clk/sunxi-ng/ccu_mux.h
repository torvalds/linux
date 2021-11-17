/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CCU_MUX_H_
#define _CCU_MUX_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_mux_fixed_prediv {
	u8	index;
	u16	div;
};

struct ccu_mux_var_prediv {
	u8	index;
	u8	shift;
	u8	width;
};

struct ccu_mux_internal {
	u8		shift;
	u8		width;
	const u8	*table;

	const struct ccu_mux_fixed_prediv	*fixed_predivs;
	u8		n_predivs;

	const struct ccu_mux_var_prediv		*var_predivs;
	u8		n_var_predivs;
};

#define _SUNXI_CCU_MUX_TABLE(_shift, _width, _table)	\
	{						\
		.shift	= _shift,			\
		.width	= _width,			\
		.table	= _table,			\
	}

#define _SUNXI_CCU_MUX(_shift, _width) \
	_SUNXI_CCU_MUX_TABLE(_shift, _width, NULL)

struct ccu_mux {
	u16			reg;
	u32			enable;

	struct ccu_mux_internal	mux;
	struct ccu_common	common;
};

#define SUNXI_CCU_MUX_TABLE_WITH_GATE(_struct, _name, _parents, _table,	\
				     _reg, _shift, _width, _gate,	\
				     _flags)				\
	struct ccu_mux _struct = {					\
		.enable	= _gate,					\
		.mux	= _SUNXI_CCU_MUX_TABLE(_shift, _width, _table),	\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_mux_ops, \
							      _flags),	\
		}							\
	}

#define SUNXI_CCU_MUX_WITH_GATE(_struct, _name, _parents, _reg,		\
				_shift, _width, _gate, _flags)		\
	SUNXI_CCU_MUX_TABLE_WITH_GATE(_struct, _name, _parents, NULL,	\
				      _reg, _shift, _width, _gate,	\
				      _flags)

#define SUNXI_CCU_MUX(_struct, _name, _parents, _reg, _shift, _width,	\
		      _flags)						\
	SUNXI_CCU_MUX_TABLE_WITH_GATE(_struct, _name, _parents, NULL,	\
				      _reg, _shift, _width, 0, _flags)

static inline struct ccu_mux *hw_to_ccu_mux(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_mux, common);
}

extern const struct clk_ops ccu_mux_ops;

unsigned long ccu_mux_helper_apply_prediv(struct ccu_common *common,
					  struct ccu_mux_internal *cm,
					  int parent_index,
					  unsigned long parent_rate);
int ccu_mux_helper_determine_rate(struct ccu_common *common,
				  struct ccu_mux_internal *cm,
				  struct clk_rate_request *req,
				  unsigned long (*round)(struct ccu_mux_internal *,
							 struct clk_hw *,
							 unsigned long *,
							 unsigned long,
							 void *),
				  void *data);
u8 ccu_mux_helper_get_parent(struct ccu_common *common,
			     struct ccu_mux_internal *cm);
int ccu_mux_helper_set_parent(struct ccu_common *common,
			      struct ccu_mux_internal *cm,
			      u8 index);

struct ccu_mux_nb {
	struct notifier_block	clk_nb;
	struct ccu_common	*common;
	struct ccu_mux_internal	*cm;

	u32	delay_us;	/* How many us to wait after reparenting */
	u8	bypass_index;	/* Which parent to temporarily use */
	u8	original_index;	/* This is set by the notifier callback */
};

#define to_ccu_mux_nb(_nb) container_of(_nb, struct ccu_mux_nb, clk_nb)

int ccu_mux_notifier_register(struct clk *clk, struct ccu_mux_nb *mux_nb);

#endif /* _CCU_MUX_H_ */
