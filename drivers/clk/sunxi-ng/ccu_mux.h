#ifndef _CCU_MUX_H_
#define _CCU_MUX_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_mux_internal {
	u8	shift;
	u8	width;

	struct {
		u8	index;
		u16	div;
	} fixed_prediv;

	struct {
		u8	index;
		u8	shift;
		u8	width;
	} variable_prediv;
};

#define _SUNXI_CCU_MUX(_shift, _width)		\
	{					\
		.shift	= _shift,		\
		.width	= _width,		\
	}

struct ccu_mux {
	u16			reg;
	u32			enable;

	struct ccu_mux_internal	mux;
	struct ccu_common	common;
};

#define SUNXI_CCU_MUX(_struct, _name, _parents, _reg, _shift, _width, _flags) \
	struct ccu_mux _struct = {					\
		.mux	= _SUNXI_CCU_MUX(_shift, _width),		\
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
	struct ccu_mux _struct = {					\
		.enable	= _gate,					\
		.mux	= _SUNXI_CCU_MUX(_shift, _width),		\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_mux_ops, \
							      _flags),	\
		}							\
	}

static inline struct ccu_mux *hw_to_ccu_mux(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_mux, common);
}

extern const struct clk_ops ccu_mux_ops;

void ccu_mux_helper_adjust_parent_for_prediv(struct ccu_common *common,
					     struct ccu_mux_internal *cm,
					     int parent_index,
					     unsigned long *parent_rate);
int ccu_mux_helper_determine_rate(struct ccu_common *common,
				  struct ccu_mux_internal *cm,
				  struct clk_rate_request *req,
				  unsigned long (*round)(struct ccu_mux_internal *,
							 unsigned long,
							 unsigned long,
							 void *),
				  void *data);
u8 ccu_mux_helper_get_parent(struct ccu_common *common,
			     struct ccu_mux_internal *cm);
int ccu_mux_helper_set_parent(struct ccu_common *common,
			      struct ccu_mux_internal *cm,
			      u8 index);

#endif /* _CCU_MUX_H_ */
