/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _CLK_SOPHGO_CV18XX_IP_H_
#define _CLK_SOPHGO_CV18XX_IP_H_

#include <linux/compiler.h>
#include <linux/clk-provider.h>
#include <linux/bitfield.h>

struct cv1800_clk_common {
	void __iomem	*base;
	spinlock_t	*lock;
	struct clk_hw	hw;
	unsigned long	features;
};

#define CV1800_CLK_COMMON(_name, _parents, _op, _flags)			\
	{								\
		.hw.init = CLK_HW_INIT_PARENTS_DATA(_name, _parents,	\
						    _op, _flags),	\
	}

static inline struct cv1800_clk_common *
hw_to_cv1800_clk_common(struct clk_hw *hw)
{
	return container_of(hw, struct cv1800_clk_common, hw);
}

struct cv1800_clk_regbit {
	u16		reg;
	s8		shift;
};

struct cv1800_clk_regfield {
	u16		reg;
	u8		shift;
	u8		width;
	s16		initval;
	unsigned long	flags;
};

#define CV1800_CLK_BIT(_reg, _shift)	\
	{				\
		.reg = _reg,		\
		.shift = _shift,	\
	}

#define CV1800_CLK_REG(_reg, _shift, _width, _initval, _flags)	\
	{							\
		.reg = _reg,					\
		.shift = _shift,				\
		.width = _width,				\
		.initval = _initval,				\
		.flags = _flags,				\
	}

#define cv1800_clk_regfield_genmask(_reg) \
	GENMASK((_reg)->shift + (_reg)->width - 1, (_reg)->shift)
#define cv1800_clk_regfield_get(_val, _reg) \
	(((_val) >> (_reg)->shift) & GENMASK((_reg)->width - 1, 0))
#define cv1800_clk_regfield_set(_val, _new, _reg)    \
	(((_val) & ~cv1800_clk_regfield_genmask((_reg))) | \
	 (((_new) & GENMASK((_reg)->width - 1, 0)) << (_reg)->shift))

#define _CV1800_SET_FIELD(_reg, _val, _field) \
	(((_reg) & ~(_field)) | FIELD_PREP((_field), (_val)))

int cv1800_clk_setbit(struct cv1800_clk_common *common,
		      struct cv1800_clk_regbit *field);
int cv1800_clk_clearbit(struct cv1800_clk_common *common,
			struct cv1800_clk_regbit *field);
int cv1800_clk_checkbit(struct cv1800_clk_common *common,
			struct cv1800_clk_regbit *field);

void cv1800_clk_wait_for_lock(struct cv1800_clk_common *common,
			      u32 reg, u32 lock);

#endif // _CLK_SOPHGO_CV18XX_IP_H_
