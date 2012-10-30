/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_ENCODER_H__
#define __NOUVEAU_ENCODER_H__

#include <subdev/bios/dcb.h>

#include <drm/drm_encoder_slave.h>
#include "nv04_display.h"

#define NV_DPMS_CLEARED 0x80

struct nouveau_i2c_port;

struct dp_train_func {
	void (*link_set)(struct drm_device *, struct dcb_output *, int crtc,
			 int nr, u32 bw, bool enhframe);
	void (*train_set)(struct drm_device *, struct dcb_output *, u8 pattern);
	void (*train_adj)(struct drm_device *, struct dcb_output *,
			  u8 lane, u8 swing, u8 preem);
};

struct nouveau_encoder {
	struct drm_encoder_slave base;

	struct dcb_output *dcb;
	int or;

	/* different to drm_encoder.crtc, this reflects what's
	 * actually programmed on the hw, not the proposed crtc */
	struct drm_crtc *crtc;

	struct drm_display_mode mode;
	int last_dpms;

	struct nv04_output_reg restore;

	union {
		struct {
			u8  dpcd[8];
			int link_nr;
			int link_bw;
			u32 datarate;
		} dp;
	};
};

struct nouveau_encoder *
find_encoder(struct drm_connector *connector, int type);

static inline struct nouveau_encoder *nouveau_encoder(struct drm_encoder *enc)
{
	struct drm_encoder_slave *slave = to_encoder_slave(enc);

	return container_of(slave, struct nouveau_encoder, base);
}

static inline struct drm_encoder *to_drm_encoder(struct nouveau_encoder *enc)
{
	return &enc->base.base;
}

static inline struct drm_encoder_slave_funcs *
get_slave_funcs(struct drm_encoder *enc)
{
	return to_encoder_slave(enc)->slave_funcs;
}

/* nouveau_dp.c */
bool nouveau_dp_detect(struct drm_encoder *);
void nouveau_dp_dpms(struct drm_encoder *, int mode, u32 datarate,
		     struct dp_train_func *);
u8 *nouveau_dp_bios_data(struct drm_device *, struct dcb_output *, u8 **);

struct nouveau_connector *
nouveau_encoder_connector_get(struct nouveau_encoder *encoder);
int nv50_sor_create(struct drm_connector *, struct dcb_output *);
void nv50_sor_dp_calc_tu(struct drm_device *, int, int, u32, u32);
int nv50_dac_create(struct drm_connector *, struct dcb_output *);


#endif /* __NOUVEAU_ENCODER_H__ */
