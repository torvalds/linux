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
 * The above copyright yestice and this permission yestice (including the
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

#ifndef __NOUVEAU_CONNECTOR_H__
#define __NOUVEAU_CONNECTOR_H__

#include <nvif/yestify.h>

#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_util.h>

#include "yesuveau_crtc.h"
#include "yesuveau_encoder.h"

struct nvkm_i2c_port;
struct dcb_output;

#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
struct yesuveau_backlight;
#endif

#define yesuveau_conn_atom(p)                                                   \
	container_of((p), struct yesuveau_conn_atom, state)

struct yesuveau_conn_atom {
	struct drm_connector_state state;

	struct {
		/* The enum values specifically defined here match nv50/gf119
		 * hw values, and the code relies on this.
		 */
		enum {
			DITHERING_MODE_OFF = 0x00,
			DITHERING_MODE_ON = 0x01,
			DITHERING_MODE_DYNAMIC2X2 = 0x10 | DITHERING_MODE_ON,
			DITHERING_MODE_STATIC2X2 = 0x18 | DITHERING_MODE_ON,
			DITHERING_MODE_TEMPORAL = 0x20 | DITHERING_MODE_ON,
			DITHERING_MODE_AUTO
		} mode;
		enum {
			DITHERING_DEPTH_6BPC = 0x00,
			DITHERING_DEPTH_8BPC = 0x02,
			DITHERING_DEPTH_AUTO
		} depth;
	} dither;

	struct {
		int mode;	/* DRM_MODE_SCALE_* */
		struct {
			enum {
				UNDERSCAN_OFF,
				UNDERSCAN_ON,
				UNDERSCAN_AUTO,
			} mode;
			u32 hborder;
			u32 vborder;
		} underscan;
		bool full;
	} scaler;

	struct {
		int color_vibrance;
		int vibrant_hue;
	} procamp;

	union {
		struct {
			bool dither:1;
			bool scaler:1;
			bool procamp:1;
		};
		u8 mask;
	} set;
};

struct yesuveau_connector {
	struct drm_connector base;
	enum dcb_connector_type type;
	u8 index;
	u8 *dcb;

	struct nvif_yestify hpd;

	struct drm_dp_aux aux;

	int dithering_mode;
	int scaling_mode;

	struct yesuveau_encoder *detected_encoder;
	struct edid *edid;
	struct drm_display_mode *native_mode;
#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
	struct yesuveau_backlight *backlight;
#endif
	/*
	 * Our connector property code expects a yesuveau_conn_atom struct
	 * even on pre-nv50 where we do yest support atomic. This embedded
	 * version gets used in the yesn atomic modeset case.
	 */
	struct yesuveau_conn_atom properties_state;
};

static inline struct yesuveau_connector *yesuveau_connector(
						struct drm_connector *con)
{
	return container_of(con, struct yesuveau_connector, base);
}

static inline bool
yesuveau_connector_is_mst(struct drm_connector *connector)
{
	const struct yesuveau_encoder *nv_encoder;
	const struct drm_encoder *encoder;

	if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return false;

	nv_encoder = find_encoder(connector, DCB_OUTPUT_ANY);
	if (!nv_encoder)
		return false;

	encoder = &nv_encoder->base.base;
	return encoder->encoder_type == DRM_MODE_ENCODER_DPMST;
}

#define yesuveau_for_each_yesn_mst_connector_iter(connector, iter) \
	drm_for_each_connector_iter(connector, iter) \
		for_each_if(!yesuveau_connector_is_mst(connector))

static inline struct yesuveau_connector *
yesuveau_crtc_connector_get(struct yesuveau_crtc *nv_crtc)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct yesuveau_connector *nv_connector = NULL;
	struct drm_crtc *crtc = to_drm_crtc(nv_crtc);

	drm_connector_list_iter_begin(dev, &conn_iter);
	yesuveau_for_each_yesn_mst_connector_iter(connector, &conn_iter) {
		if (connector->encoder && connector->encoder->crtc == crtc) {
			nv_connector = yesuveau_connector(connector);
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return nv_connector;
}

struct drm_connector *
yesuveau_connector_create(struct drm_device *, const struct dcb_output *);

extern int yesuveau_tv_disable;
extern int yesuveau_igyesrelid;
extern int yesuveau_duallink;
extern int yesuveau_hdmimhz;

void yesuveau_conn_attach_properties(struct drm_connector *);
void yesuveau_conn_reset(struct drm_connector *);
struct drm_connector_state *
yesuveau_conn_atomic_duplicate_state(struct drm_connector *);
void yesuveau_conn_atomic_destroy_state(struct drm_connector *,
				       struct drm_connector_state *);
int yesuveau_conn_atomic_set_property(struct drm_connector *,
				     struct drm_connector_state *,
				     struct drm_property *, u64);
int yesuveau_conn_atomic_get_property(struct drm_connector *,
				     const struct drm_connector_state *,
				     struct drm_property *, u64 *);
struct drm_display_mode *yesuveau_conn_native_mode(struct drm_connector *);

#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
extern int yesuveau_backlight_init(struct drm_connector *);
extern void yesuveau_backlight_fini(struct drm_connector *);
extern void yesuveau_backlight_ctor(void);
extern void yesuveau_backlight_dtor(void);
#else
static inline int
yesuveau_backlight_init(struct drm_connector *connector)
{
	return 0;
}

static inline void
yesuveau_backlight_fini(struct drm_connector *connector) {
}

static inline void
yesuveau_backlight_ctor(void) {
}

static inline void
yesuveau_backlight_dtor(void) {
}
#endif

#endif /* __NOUVEAU_CONNECTOR_H__ */
