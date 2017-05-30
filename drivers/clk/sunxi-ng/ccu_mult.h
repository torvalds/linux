#ifndef _CCU_MULT_H_
#define _CCU_MULT_H_

#include "ccu_common.h"
#include "ccu_frac.h"
#include "ccu_mux.h"

struct ccu_mult_internal {
	u8	offset;
	u8	shift;
	u8	width;
	u8	min;
	u8	max;
};

#define _SUNXI_CCU_MULT_OFFSET_MIN_MAX(_shift, _width, _offset, _min, _max) \
	{								\
		.min	= _min,						\
		.max	= _max,						\
		.offset	= _offset,					\
		.shift	= _shift,					\
		.width	= _width,					\
	}

#define _SUNXI_CCU_MULT_MIN(_shift, _width, _min)	\
	_SUNXI_CCU_MULT_OFFSET_MIN_MAX(_shift, _width, 1, _min, 0)

#define _SUNXI_CCU_MULT_OFFSET(_shift, _width, _offset)	\
	_SUNXI_CCU_MULT_OFFSET_MIN_MAX(_shift, _width, _offset, 1, 0)

#define _SUNXI_CCU_MULT(_shift, _width)		\
	_SUNXI_CCU_MULT_OFFSET_MIN_MAX(_shift, _width, 1, 1, 0)

struct ccu_mult {
	u32			enable;
	u32			lock;

	struct ccu_frac_internal	frac;
	struct ccu_mult_internal	mult;
	struct ccu_mux_internal	mux;
	struct ccu_common	common;
};

#define SUNXI_CCU_N_WITH_GATE_LOCK(_struct, _name, _parent, _reg,	\
				   _mshift, _mwidth, _gate, _lock,	\
				   _flags)				\
	struct ccu_mult _struct = {					\
		.enable	= _gate,					\
		.lock	= _lock,					\
		.mult	= _SUNXI_CCU_MULT(_mshift, _mwidth),		\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_mult_ops,	\
						      _flags),		\
		},							\
	}

static inline struct ccu_mult *hw_to_ccu_mult(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_mult, common);
}

extern const struct clk_ops ccu_mult_ops;

#endif /* _CCU_MULT_H_ */
