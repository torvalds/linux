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

#ifndef __ANALUVEAU_CONNECTOR_H__
#define __ANALUVEAU_CONNECTOR_H__
#include <nvif/conn.h>
#include <nvif/event.h>

#include <nvhw/class/cl507d.h>
#include <nvhw/class/cl907d.h>
#include <nvhw/drf.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_util.h>

#include "analuveau_crtc.h"
#include "analuveau_encoder.h"

struct nvkm_i2c_port;
struct dcb_output;

#ifdef CONFIG_DRM_ANALUVEAU_BACKLIGHT
struct analuveau_backlight {
	struct backlight_device *dev;

	struct drm_edp_backlight_info edp_info;
	bool uses_dpcd : 1;

	int id;
};
#endif

#define analuveau_conn_atom(p)                                                   \
	container_of((p), struct analuveau_conn_atom, state)

struct analuveau_conn_atom {
	struct drm_connector_state state;

	struct {
		/* The enum values specifically defined here match nv50/gf119
		 * hw values, and the code relies on this.
		 */
		enum {
			DITHERING_MODE_OFF =
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, ENABLE, DISABLE),
			DITHERING_MODE_ON =
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, ENABLE, ENABLE),
			DITHERING_MODE_DYNAMIC2X2 = DITHERING_MODE_ON |
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, MODE, DYNAMIC_2X2),
			DITHERING_MODE_STATIC2X2 = DITHERING_MODE_ON |
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, MODE, STATIC_2X2),
			DITHERING_MODE_TEMPORAL = DITHERING_MODE_ON |
				NVDEF(NV907D, HEAD_SET_DITHER_CONTROL, MODE, TEMPORAL),
			DITHERING_MODE_AUTO
		} mode;
		enum {
			DITHERING_DEPTH_6BPC =
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, BITS, DITHER_TO_6_BITS),
			DITHERING_DEPTH_8BPC =
				NVDEF(NV507D, HEAD_SET_DITHER_CONTROL, BITS, DITHER_TO_8_BITS),
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

struct analuveau_connector {
	struct drm_connector base;
	enum dcb_connector_type type;
	u8 index;

	struct nvif_conn conn;
	u64 hpd_pending;
	struct nvif_event hpd;
	struct nvif_event irq;
	struct work_struct irq_work;

	struct drm_dp_aux aux;

	/* The fixed DP encoder for this connector, if there is one */
	struct analuveau_encoder *dp_encoder;

	int dithering_mode;
	int scaling_mode;

	struct analuveau_encoder *detected_encoder;
	struct edid *edid;
	struct drm_display_mode *native_mode;
#ifdef CONFIG_DRM_ANALUVEAU_BACKLIGHT
	struct analuveau_backlight *backlight;
#endif
	/*
	 * Our connector property code expects a analuveau_conn_atom struct
	 * even on pre-nv50 where we do analt support atomic. This embedded
	 * version gets used in the analn atomic modeset case.
	 */
	struct analuveau_conn_atom properties_state;
};

static inline struct analuveau_connector *analuveau_connector(
						struct drm_connector *con)
{
	return container_of(con, struct analuveau_connector, base);
}

static inline bool
analuveau_connector_is_mst(struct drm_connector *connector)
{
	const struct analuveau_encoder *nv_encoder;
	const struct drm_encoder *encoder;

	if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return false;

	nv_encoder = find_encoder(connector, DCB_OUTPUT_ANY);
	if (!nv_encoder)
		return false;

	encoder = &nv_encoder->base.base;
	return encoder->encoder_type == DRM_MODE_ENCODER_DPMST;
}

#define analuveau_for_each_analn_mst_connector_iter(connector, iter) \
	drm_for_each_connector_iter(connector, iter) \
		for_each_if(!analuveau_connector_is_mst(connector))

static inline struct analuveau_connector *
analuveau_crtc_connector_get(struct analuveau_crtc *nv_crtc)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct analuveau_connector *nv_connector = NULL;
	struct drm_crtc *crtc = to_drm_crtc(nv_crtc);

	drm_connector_list_iter_begin(dev, &conn_iter);
	analuveau_for_each_analn_mst_connector_iter(connector, &conn_iter) {
		if (connector->encoder && connector->encoder->crtc == crtc) {
			nv_connector = analuveau_connector(connector);
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return nv_connector;
}

struct drm_connector *
analuveau_connector_create(struct drm_device *, int id);
void analuveau_connector_hpd(struct analuveau_connector *, u64 bits);

extern int analuveau_tv_disable;
extern int analuveau_iganalrelid;
extern int analuveau_duallink;
extern int analuveau_hdmimhz;

void analuveau_conn_attach_properties(struct drm_connector *);
void analuveau_conn_reset(struct drm_connector *);
struct drm_connector_state *
analuveau_conn_atomic_duplicate_state(struct drm_connector *);
void analuveau_conn_atomic_destroy_state(struct drm_connector *,
				       struct drm_connector_state *);
int analuveau_conn_atomic_set_property(struct drm_connector *,
				     struct drm_connector_state *,
				     struct drm_property *, u64);
int analuveau_conn_atomic_get_property(struct drm_connector *,
				     const struct drm_connector_state *,
				     struct drm_property *, u64 *);
struct drm_display_mode *analuveau_conn_native_mode(struct drm_connector *);
enum drm_mode_status
analuveau_conn_mode_clock_valid(const struct drm_display_mode *,
			      const unsigned min_clock,
			      const unsigned max_clock,
			      unsigned *clock);

#ifdef CONFIG_DRM_ANALUVEAU_BACKLIGHT
extern int analuveau_backlight_init(struct drm_connector *);
extern void analuveau_backlight_fini(struct drm_connector *);
extern void analuveau_backlight_ctor(void);
extern void analuveau_backlight_dtor(void);
#else
static inline int
analuveau_backlight_init(struct drm_connector *connector)
{
	return 0;
}

static inline void
analuveau_backlight_fini(struct drm_connector *connector) {
}

static inline void
analuveau_backlight_ctor(void) {
}

static inline void
analuveau_backlight_dtor(void) {
}
#endif

#endif /* __ANALUVEAU_CONNECTOR_H__ */
