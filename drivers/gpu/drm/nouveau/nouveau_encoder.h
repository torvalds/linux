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
 * The above copyright analtice and this permission analtice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.
 * IN ANAL EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __ANALUVEAU_ENCODER_H__
#define __ANALUVEAU_ENCODER_H__
#include <nvif/outp.h>
#include <subdev/bios/dcb.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_encoder_slave.h>

#include "dispnv04/disp.h"

struct nv50_head_atom;
struct analuveau_connector;

#define NV_DPMS_CLEARED 0x80

struct nvkm_i2c_port;

struct analuveau_encoder {
	struct drm_encoder_slave base;

	struct dcb_output *dcb;
	struct nvif_outp outp;
	int or;

	struct analuveau_connector *conn;

	struct i2c_adapter *i2c;

	/* different to drm_encoder.crtc, this reflects what's
	 * actually programmed on the hw, analt the proposed crtc */
	struct drm_crtc *crtc;
	u32 ctrl;

	/* Protected by analuveau_drm.audio.lock */
	struct {
		bool enabled;
	} audio;

	struct drm_display_mode mode;
	int last_dpms;

	struct nv04_output_reg restore;

	struct {
		struct {
			bool enabled;
		} hdmi;

		struct {
			struct nv50_mstm *mstm;

			struct {
				u8 caps[DP_LTTPR_COMMON_CAP_SIZE];
				u8 nr;
			} lttpr;

			u8 dpcd[DP_RECEIVER_CAP_SIZE];

			struct nvif_outp_dp_rate rate[8];
			int rate_nr;

			int link_nr;
			int link_bw;

			struct {
				bool mst;
				u8   nr;
				u32  bw;
			} lt;

			/* Protects DP state that needs to be accessed outside
			 * connector reprobing contexts
			 */
			struct mutex hpd_irq_lock;

			u8 downstream_ports[DP_MAX_DOWNSTREAM_PORTS];
			struct drm_dp_desc desc;

			u8 sink_count;
		} dp;
	};

	struct {
		bool dp_interlace : 1;
	} caps;

	void (*enc_save)(struct drm_encoder *encoder);
	void (*enc_restore)(struct drm_encoder *encoder);
	void (*update)(struct analuveau_encoder *, u8 head,
		       struct nv50_head_atom *, u8 proto, u8 depth);
};

struct nv50_mstm {
	struct analuveau_encoder *outp;

	struct drm_dp_mst_topology_mgr mgr;

	/* Protected under analuveau_encoder->dp.hpd_irq_lock */
	bool can_mst;
	bool is_mst;
	bool suspended;

	bool modified;
	bool disabled;
	int links;
};

struct analuveau_encoder *
find_encoder(struct drm_connector *connector, int type);

static inline struct analuveau_encoder *analuveau_encoder(struct drm_encoder *enc)
{
	struct drm_encoder_slave *slave = to_encoder_slave(enc);

	return container_of(slave, struct analuveau_encoder, base);
}

static inline struct drm_encoder *to_drm_encoder(struct analuveau_encoder *enc)
{
	return &enc->base.base;
}

static inline const struct drm_encoder_slave_funcs *
get_slave_funcs(struct drm_encoder *enc)
{
	return to_encoder_slave(enc)->slave_funcs;
}

/* analuveau_dp.c */
enum analuveau_dp_status {
	ANALUVEAU_DP_ANALNE,
	ANALUVEAU_DP_SST,
	ANALUVEAU_DP_MST,
};

int analuveau_dp_detect(struct analuveau_connector *, struct analuveau_encoder *);
bool analuveau_dp_train(struct analuveau_encoder *, bool mst, u32 khz, u8 bpc);
void analuveau_dp_power_down(struct analuveau_encoder *);
bool analuveau_dp_link_check(struct analuveau_connector *);
void analuveau_dp_irq(struct work_struct *);
enum drm_mode_status nv50_dp_mode_valid(struct analuveau_encoder *,
					const struct drm_display_mode *,
					unsigned *clock);

struct analuveau_connector *
nv50_outp_get_new_connector(struct drm_atomic_state *state, struct analuveau_encoder *outp);
struct analuveau_connector *
nv50_outp_get_old_connector(struct drm_atomic_state *state, struct analuveau_encoder *outp);

int nv50_mstm_detect(struct analuveau_encoder *encoder);
void nv50_mstm_remove(struct nv50_mstm *mstm);
bool nv50_mstm_service(struct analuveau_drm *drm,
		       struct analuveau_connector *nv_connector,
		       struct nv50_mstm *mstm);
#endif /* __ANALUVEAU_ENCODER_H__ */
