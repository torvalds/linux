/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_CRTC_H
#define ARMADA_CRTC_H

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
struct armada_plane;
struct armada_variant;

struct armada_plane_work {
	void			(*fn)(struct armada_crtc *,
				      struct armada_plane *,
				      struct armada_plane_work *);
};

struct armada_plane_state {
	u32 src_hw;
	u32 dst_hw;
	u32 dst_yx;
	u32 ctrl0;
};

struct armada_plane {
	struct drm_plane	base;
	wait_queue_head_t	frame_wait;
	struct armada_plane_work *work;
	struct armada_plane_state state;
};
#define drm_to_armada_plane(p) container_of(p, struct armada_plane, base)

int armada_drm_plane_init(struct armada_plane *plane);
int armada_drm_plane_work_queue(struct armada_crtc *dcrtc,
	struct armada_plane *plane, struct armada_plane_work *work);
int armada_drm_plane_work_wait(struct armada_plane *plane, long timeout);
struct armada_plane_work *armada_drm_plane_work_cancel(
	struct armada_crtc *dcrtc, struct armada_plane *plane);
void armada_drm_plane_calc_addrs(u32 *addrs, struct drm_framebuffer *fb,
	int x, int y);

struct armada_crtc {
	struct drm_crtc		crtc;
	const struct armada_variant *variant;
	unsigned		num;
	void __iomem		*base;
	struct clk		*clk;
	struct clk		*extclk[2];
	struct {
		uint32_t	spu_v_h_total;
		uint32_t	spu_v_porch;
		uint32_t	spu_adv_reg;
	} v[2];
	bool			interlaced;
	bool			cursor_update;
	uint8_t			csc_yuv_mode;
	uint8_t			csc_rgb_mode;

	struct drm_plane	*plane;

	struct armada_gem_object	*cursor_obj;
	int			cursor_x;
	int			cursor_y;
	uint32_t		cursor_hw_pos;
	uint32_t		cursor_hw_sz;
	uint32_t		cursor_w;
	uint32_t		cursor_h;

	int			dpms;
	uint32_t		cfg_dumb_ctrl;
	uint32_t		dumb_ctrl;
	uint32_t		spu_iopad_ctrl;

	spinlock_t		irq_lock;
	uint32_t		irq_ena;
};
#define drm_to_armada_crtc(c) container_of(c, struct armada_crtc, crtc)

void armada_drm_crtc_gamma_set(struct drm_crtc *, u16, u16, u16, int);
void armada_drm_crtc_gamma_get(struct drm_crtc *, u16 *, u16 *, u16 *, int);
void armada_drm_crtc_update_regs(struct armada_crtc *, struct armada_regs *);

void armada_drm_crtc_plane_disable(struct armada_crtc *dcrtc,
	struct drm_plane *plane);

extern struct platform_driver armada_lcd_platform_driver;

#endif
