/*
 * Copyright © 2006-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _INTEL_DISPLAY_H_
#define _INTEL_DISPLAY_H_

#include <drm/drm_util.h>

enum i915_gpio {
	GPIOA,
	GPIOB,
	GPIOC,
	GPIOD,
	GPIOE,
	GPIOF,
	GPIOG,
	GPIOH,
	__GPIOI_UNUSED,
	GPIOJ,
	GPIOK,
	GPIOL,
	GPIOM,
};

enum pipe {
	INVALID_PIPE = -1,

	PIPE_A = 0,
	PIPE_B,
	PIPE_C,
	_PIPE_EDP,

	I915_MAX_PIPES = _PIPE_EDP
};

#define pipe_name(p) ((p) + 'A')

enum transcoder {
	TRANSCODER_A = 0,
	TRANSCODER_B,
	TRANSCODER_C,
	TRANSCODER_EDP,
	TRANSCODER_DSI_A,
	TRANSCODER_DSI_C,

	I915_MAX_TRANSCODERS
};

static inline const char *transcoder_name(enum transcoder transcoder)
{
	switch (transcoder) {
	case TRANSCODER_A:
		return "A";
	case TRANSCODER_B:
		return "B";
	case TRANSCODER_C:
		return "C";
	case TRANSCODER_EDP:
		return "EDP";
	case TRANSCODER_DSI_A:
		return "DSI A";
	case TRANSCODER_DSI_C:
		return "DSI C";
	default:
		return "<invalid>";
	}
}

static inline bool transcoder_is_dsi(enum transcoder transcoder)
{
	return transcoder == TRANSCODER_DSI_A || transcoder == TRANSCODER_DSI_C;
}

/*
 * Global legacy plane identifier. Valid only for primary/sprite
 * planes on pre-g4x, and only for primary planes on g4x-bdw.
 */
enum i9xx_plane_id {
	PLANE_A,
	PLANE_B,
	PLANE_C,
};

#define plane_name(p) ((p) + 'A')
#define sprite_name(p, s) ((p) * INTEL_INFO(dev_priv)->num_sprites[(p)] + (s) + 'A')

/*
 * Per-pipe plane identifier.
 * I915_MAX_PLANES in the enum below is the maximum (across all platforms)
 * number of planes per CRTC.  Not all platforms really have this many planes,
 * which means some arrays of size I915_MAX_PLANES may have unused entries
 * between the topmost sprite plane and the cursor plane.
 *
 * This is expected to be passed to various register macros
 * (eg. PLANE_CTL(), PS_PLANE_SEL(), etc.) so adjust with care.
 */
enum plane_id {
	PLANE_PRIMARY,
	PLANE_SPRITE0,
	PLANE_SPRITE1,
	PLANE_SPRITE2,
	PLANE_CURSOR,

	I915_MAX_PLANES,
};

#define for_each_plane_id_on_crtc(__crtc, __p) \
	for ((__p) = PLANE_PRIMARY; (__p) < I915_MAX_PLANES; (__p)++) \
		for_each_if((__crtc)->plane_ids_mask & BIT(__p))

enum port {
	PORT_NONE = -1,

	PORT_A = 0,
	PORT_B,
	PORT_C,
	PORT_D,
	PORT_E,
	PORT_F,

	I915_MAX_PORTS
};

#define port_name(p) ((p) + 'A')

/*
 * Ports identifier referenced from other drivers.
 * Expected to remain stable over time
 */
static inline const char *port_identifier(enum port port)
{
	switch (port) {
	case PORT_A:
		return "Port A";
	case PORT_B:
		return "Port B";
	case PORT_C:
		return "Port C";
	case PORT_D:
		return "Port D";
	case PORT_E:
		return "Port E";
	case PORT_F:
		return "Port F";
	default:
		return "<invalid>";
	}
}

enum tc_port {
	PORT_TC_NONE = -1,

	PORT_TC1 = 0,
	PORT_TC2,
	PORT_TC3,
	PORT_TC4,

	I915_MAX_TC_PORTS
};

enum tc_port_type {
	TC_PORT_UNKNOWN = 0,
	TC_PORT_TYPEC,
	TC_PORT_TBT,
	TC_PORT_LEGACY,
};

enum dpio_channel {
	DPIO_CH0,
	DPIO_CH1
};

enum dpio_phy {
	DPIO_PHY0,
	DPIO_PHY1,
	DPIO_PHY2,
};

#define I915_NUM_PHYS_VLV 2

enum aux_ch {
	AUX_CH_A,
	AUX_CH_B,
	AUX_CH_C,
	AUX_CH_D,
	AUX_CH_E, /* ICL+ */
	AUX_CH_F,
};

#define aux_ch_name(a) ((a) + 'A')

enum intel_display_power_domain {
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_A_PANEL_FITTER,
	POWER_DOMAIN_PIPE_B_PANEL_FITTER,
	POWER_DOMAIN_PIPE_C_PANEL_FITTER,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_TRANSCODER_EDP,
	POWER_DOMAIN_TRANSCODER_DSI_A,
	POWER_DOMAIN_TRANSCODER_DSI_C,
	POWER_DOMAIN_PORT_DDI_A_LANES,
	POWER_DOMAIN_PORT_DDI_B_LANES,
	POWER_DOMAIN_PORT_DDI_C_LANES,
	POWER_DOMAIN_PORT_DDI_D_LANES,
	POWER_DOMAIN_PORT_DDI_E_LANES,
	POWER_DOMAIN_PORT_DDI_F_LANES,
	POWER_DOMAIN_PORT_DDI_A_IO,
	POWER_DOMAIN_PORT_DDI_B_IO,
	POWER_DOMAIN_PORT_DDI_C_IO,
	POWER_DOMAIN_PORT_DDI_D_IO,
	POWER_DOMAIN_PORT_DDI_E_IO,
	POWER_DOMAIN_PORT_DDI_F_IO,
	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_PORT_OTHER,
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO,
	POWER_DOMAIN_PLLS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_AUX_E,
	POWER_DOMAIN_AUX_F,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_TBT1,
	POWER_DOMAIN_AUX_TBT2,
	POWER_DOMAIN_AUX_TBT3,
	POWER_DOMAIN_AUX_TBT4,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_INIT,

	POWER_DOMAIN_NUM,
};

#define POWER_DOMAIN_PIPE(pipe) ((pipe) + POWER_DOMAIN_PIPE_A)
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) \
		((pipe) + POWER_DOMAIN_PIPE_A_PANEL_FITTER)
#define POWER_DOMAIN_TRANSCODER(tran) \
	((tran) == TRANSCODER_EDP ? POWER_DOMAIN_TRANSCODER_EDP : \
	 (tran) + POWER_DOMAIN_TRANSCODER_A)

/* Used by dp and fdi links */
struct intel_link_m_n {
	u32 tu;
	u32 gmch_m;
	u32 gmch_n;
	u32 link_m;
	u32 link_n;
};

#define for_each_pipe(__dev_priv, __p) \
	for ((__p) = 0; (__p) < INTEL_INFO(__dev_priv)->num_pipes; (__p)++)

#define for_each_pipe_masked(__dev_priv, __p, __mask) \
	for ((__p) = 0; (__p) < INTEL_INFO(__dev_priv)->num_pipes; (__p)++) \
		for_each_if((__mask) & BIT(__p))

#define for_each_cpu_transcoder_masked(__dev_priv, __t, __mask) \
	for ((__t) = 0; (__t) < I915_MAX_TRANSCODERS; (__t)++)	\
		for_each_if ((__mask) & (1 << (__t)))

#define for_each_universal_plane(__dev_priv, __pipe, __p)		\
	for ((__p) = 0;							\
	     (__p) < INTEL_INFO(__dev_priv)->num_sprites[(__pipe)] + 1;	\
	     (__p)++)

#define for_each_sprite(__dev_priv, __p, __s)				\
	for ((__s) = 0;							\
	     (__s) < INTEL_INFO(__dev_priv)->num_sprites[(__p)];	\
	     (__s)++)

#define for_each_port_masked(__port, __ports_mask) \
	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++)	\
		for_each_if((__ports_mask) & BIT(__port))

#define for_each_crtc(dev, crtc) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#define for_each_intel_plane(dev, intel_plane) \
	list_for_each_entry(intel_plane,			\
			    &(dev)->mode_config.plane_list,	\
			    base.head)

#define for_each_intel_plane_mask(dev, intel_plane, plane_mask)		\
	list_for_each_entry(intel_plane,				\
			    &(dev)->mode_config.plane_list,		\
			    base.head)					\
		for_each_if((plane_mask) &				\
			    drm_plane_mask(&intel_plane->base)))

#define for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane)	\
	list_for_each_entry(intel_plane,				\
			    &(dev)->mode_config.plane_list,		\
			    base.head)					\
		for_each_if((intel_plane)->pipe == (intel_crtc)->pipe)

#define for_each_intel_crtc(dev, intel_crtc)				\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)

#define for_each_intel_crtc_mask(dev, intel_crtc, crtc_mask)		\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)					\
		for_each_if((crtc_mask) & drm_crtc_mask(&intel_crtc->base))

#define for_each_intel_encoder(dev, intel_encoder)		\
	list_for_each_entry(intel_encoder,			\
			    &(dev)->mode_config.encoder_list,	\
			    base.head)

#define for_each_intel_dp(dev, intel_encoder)			\
	for_each_intel_encoder(dev, intel_encoder)		\
		for_each_if(intel_encoder_is_dp(intel_encoder))

#define for_each_intel_connector_iter(intel_connector, iter) \
	while ((intel_connector = to_intel_connector(drm_connector_list_iter_next(iter))))

#define for_each_encoder_on_crtc(dev, __crtc, intel_encoder) \
	list_for_each_entry((intel_encoder), &(dev)->mode_config.encoder_list, base.head) \
		for_each_if((intel_encoder)->base.crtc == (__crtc))

#define for_each_connector_on_encoder(dev, __encoder, intel_connector) \
	list_for_each_entry((intel_connector), &(dev)->mode_config.connector_list, base.head) \
		for_each_if((intel_connector)->base.encoder == (__encoder))

#define for_each_power_domain(domain, mask)				\
	for ((domain) = 0; (domain) < POWER_DOMAIN_NUM; (domain)++)	\
		for_each_if(BIT_ULL(domain) & (mask))

#define for_each_power_well(__dev_priv, __power_well)				\
	for ((__power_well) = (__dev_priv)->power_domains.power_wells;	\
	     (__power_well) - (__dev_priv)->power_domains.power_wells <	\
		(__dev_priv)->power_domains.power_well_count;		\
	     (__power_well)++)

#define for_each_power_well_rev(__dev_priv, __power_well)			\
	for ((__power_well) = (__dev_priv)->power_domains.power_wells +		\
			      (__dev_priv)->power_domains.power_well_count - 1;	\
	     (__power_well) - (__dev_priv)->power_domains.power_wells >= 0;	\
	     (__power_well)--)

#define for_each_power_domain_well(__dev_priv, __power_well, __domain_mask)	\
	for_each_power_well(__dev_priv, __power_well)				\
		for_each_if((__power_well)->desc->domains & (__domain_mask))

#define for_each_power_domain_well_rev(__dev_priv, __power_well, __domain_mask) \
	for_each_power_well_rev(__dev_priv, __power_well)		        \
		for_each_if((__power_well)->desc->domains & (__domain_mask))

#define for_each_new_intel_plane_in_state(__state, plane, new_plane_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_total_plane && \
		     ((plane) = to_intel_plane((__state)->base.planes[__i].ptr), \
		      (new_plane_state) = to_intel_plane_state((__state)->base.planes[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(plane)

#define for_each_new_intel_crtc_in_state(__state, crtc, new_crtc_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_crtc && \
		     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
		      (new_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(crtc)

#define for_each_oldnew_intel_plane_in_state(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_total_plane && \
		     ((plane) = to_intel_plane((__state)->base.planes[__i].ptr), \
		      (old_plane_state) = to_intel_plane_state((__state)->base.planes[__i].old_state), \
		      (new_plane_state) = to_intel_plane_state((__state)->base.planes[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(plane)

void intel_link_compute_m_n(int bpp, int nlanes,
			    int pixel_clock, int link_clock,
			    struct intel_link_m_n *m_n,
			    bool constant_n);

bool is_ccs_modifier(u64 modifier);
#endif
