/*
 * Copyright (C) 2009 Francisco Jerez.
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

#ifndef __NV17_TV_H__
#define __NV17_TV_H__

struct nv17_tv_state {
	uint8_t tv_enc[0x40];

	uint32_t hfilter[4][7];
	uint32_t hfilter2[4][7];
	uint32_t vfilter[4][7];

	uint32_t ptv_200;
	uint32_t ptv_204;
	uint32_t ptv_208;
	uint32_t ptv_20c;
	uint32_t ptv_304;
	uint32_t ptv_500;
	uint32_t ptv_504;
	uint32_t ptv_508;
	uint32_t ptv_600;
	uint32_t ptv_604;
	uint32_t ptv_608;
	uint32_t ptv_60c;
	uint32_t ptv_610;
	uint32_t ptv_614;
};

enum nv17_tv_norm{
	TV_NORM_PAL,
	TV_NORM_PAL_M,
	TV_NORM_PAL_N,
	TV_NORM_PAL_NC,
	TV_NORM_NTSC_M,
	TV_NORM_NTSC_J,
	NUM_LD_TV_NORMS,
	TV_NORM_HD480I = NUM_LD_TV_NORMS,
	TV_NORM_HD480P,
	TV_NORM_HD576I,
	TV_NORM_HD576P,
	TV_NORM_HD720P,
	TV_NORM_HD1080I,
	NUM_TV_NORMS
};

struct nv17_tv_encoder {
	struct nouveau_encoder base;

	struct nv17_tv_state state;
	struct nv17_tv_state saved_state;

	int overscan;
	int flicker;
	int saturation;
	int hue;
	enum nv17_tv_norm tv_norm;
	int subconnector;
	int select_subconnector;
	uint32_t pin_mask;
};
#define to_tv_enc(x) container_of(nouveau_encoder(x),		\
				  struct nv17_tv_encoder, base)

extern const char * const nv17_tv_norm_names[NUM_TV_NORMS];

extern struct nv17_tv_norm_params {
	enum {
		TV_ENC_MODE,
		CTV_ENC_MODE,
	} kind;

	union {
		struct {
			int hdisplay;
			int vdisplay;
			int vrefresh; /* mHz */

			uint8_t tv_enc[0x40];
		} tv_enc_mode;

		struct {
			struct drm_display_mode mode;

			uint32_t ctv_regs[38];
		} ctv_enc_mode;
	};

} nv17_tv_norms[NUM_TV_NORMS];
#define get_tv_norm(enc) (&nv17_tv_norms[to_tv_enc(enc)->tv_norm])

extern const struct drm_display_mode nv17_tv_modes[];

static inline int interpolate(int y0, int y1, int y2, int x)
{
	return y1 + (x < 50 ? y1 - y0 : y2 - y1) * (x - 50) / 50;
}

void nv17_tv_state_save(struct drm_device *dev, struct nv17_tv_state *state);
void nv17_tv_state_load(struct drm_device *dev, struct nv17_tv_state *state);
void nv17_tv_update_properties(struct drm_encoder *encoder);
void nv17_tv_update_rescaler(struct drm_encoder *encoder);
void nv17_ctv_update_rescaler(struct drm_encoder *encoder);

/* TV hardware access functions */

static inline void nv_write_ptv(struct drm_device *dev, uint32_t reg,
				uint32_t val)
{
	struct nvif_device *device = &nouveau_drm(dev)->client.device;
	nvif_wr32(&device->object, reg, val);
}

static inline uint32_t nv_read_ptv(struct drm_device *dev, uint32_t reg)
{
	struct nvif_device *device = &nouveau_drm(dev)->client.device;
	return nvif_rd32(&device->object, reg);
}

static inline void nv_write_tv_enc(struct drm_device *dev, uint8_t reg,
				   uint8_t val)
{
	nv_write_ptv(dev, NV_PTV_TV_INDEX, reg);
	nv_write_ptv(dev, NV_PTV_TV_DATA, val);
}

static inline uint8_t nv_read_tv_enc(struct drm_device *dev, uint8_t reg)
{
	nv_write_ptv(dev, NV_PTV_TV_INDEX, reg);
	return nv_read_ptv(dev, NV_PTV_TV_DATA);
}

#define nv_load_ptv(dev, state, reg) \
	nv_write_ptv(dev, NV_PTV_OFFSET + 0x##reg, state->ptv_##reg)
#define nv_save_ptv(dev, state, reg) \
	state->ptv_##reg = nv_read_ptv(dev, NV_PTV_OFFSET + 0x##reg)
#define nv_load_tv_enc(dev, state, reg) \
	nv_write_tv_enc(dev, 0x##reg, state->tv_enc[0x##reg])

#endif
