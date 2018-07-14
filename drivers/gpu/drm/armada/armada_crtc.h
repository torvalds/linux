/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_CRTC_H
#define ARMADA_CRTC_H

#include <drm/drm_crtc.h>

struct armada_gem_object;

struct armada_regs {
	uint32_t offset;
	uint32_t mask;
	uint32_t val;
};

#define armada_reg_queue_mod(_r, _i, _v, _m, _o)	\
	do {					\
		struct armada_regs *__reg = _r;	\
		__reg[_i].offset = _o;		\
		__reg[_i].mask = ~(_m);		\
		__reg[_i].val = _v;		\
		_i++;				\
	} while (0)

#define armada_reg_queue_set(_r, _i, _v, _o)	\
	armada_reg_queue_mod(_r, _i, _v, ~0, _o)

#define armada_reg_queue_end(_r, _i)		\
	armada_reg_queue_mod(_r, _i, 0, 0, ~0)

struct armada_crtc;
struct armada_variant;

struct armada_crtc {
	struct drm_crtc		crtc;
	const struct armada_variant *variant;
	void			*variant_data;
	unsigned		num;
	void __iomem		*base;
	struct clk		*clk;
	struct {
		uint32_t	spu_v_h_total;
		uint32_t	spu_v_porch;
		uint32_t	spu_adv_reg;
	} v[2];
	bool			interlaced;
	bool			cursor_update;

	struct armada_gem_object	*cursor_obj;
	int			cursor_x;
	int			cursor_y;
	uint32_t		cursor_hw_pos;
	uint32_t		cursor_hw_sz;
	uint32_t		cursor_w;
	uint32_t		cursor_h;

	uint32_t		cfg_dumb_ctrl;
	uint32_t		spu_iopad_ctrl;

	spinlock_t		irq_lock;
	uint32_t		irq_ena;

	bool			update_pending;
	struct drm_pending_vblank_event *event;
	struct armada_regs	atomic_regs[32];
	struct armada_regs	*regs;
	unsigned int		regs_idx;
};
#define drm_to_armada_crtc(c) container_of(c, struct armada_crtc, crtc)

void armada_drm_crtc_update_regs(struct armada_crtc *, struct armada_regs *);

struct armada_clocking_params {
	unsigned long permillage_min;
	unsigned long permillage_max;
	u32 settable;
	u32 div_max;
};

struct armada_clk_result {
	unsigned long desired_clk_hz;
	struct clk *clk;
	u32 div;
};

int armada_crtc_select_clock(struct armada_crtc *dcrtc,
			     struct armada_clk_result *res,
			     const struct armada_clocking_params *params,
			     struct clk *clks[], size_t num_clks,
			     unsigned long desired_khz);

extern struct platform_driver armada_lcd_platform_driver;

#endif
