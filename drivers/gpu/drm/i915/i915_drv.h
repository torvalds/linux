/* i915_drv.h -- Private header for the I915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _I915_DRV_H_
#define _I915_DRV_H_

#include <uapi/drm/i915_drm.h>
#include <uapi/drm/drm_fourcc.h>

#include <linux/io-mapping.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/backlight.h>
#include <linux/hashtable.h>
#include <linux/intel-iommu.h>
#include <linux/kref.h>
#include <linux/pm_qos.h>
#include <linux/shmem_fs.h>

#include <drm/drmP.h>
#include <drm/intel-gtt.h>
#include <drm/drm_legacy.h> /* for struct drm_dma_handle */
#include <drm/drm_gem.h>
#include <drm/drm_auth.h>

#include "i915_params.h"
#include "i915_reg.h"

#include "intel_bios.h"
#include "intel_dpll_mgr.h"
#include "intel_guc.h"
#include "intel_lrc.h"
#include "intel_ringbuffer.h"

#include "i915_gem.h"
#include "i915_gem_gtt.h"
#include "i915_gem_render_state.h"
#include "i915_gem_request.h"

#include "intel_gvt.h"

/* General customization:
 */

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20161024"
#define DRIVER_TIMESTAMP	1477290335

#undef WARN_ON
/* Many gcc seem to no see through this and fall over :( */
#if 0
#define WARN_ON(x) ({ \
	bool __i915_warn_cond = (x); \
	if (__builtin_constant_p(__i915_warn_cond)) \
		BUILD_BUG_ON(__i915_warn_cond); \
	WARN(__i915_warn_cond, "WARN_ON(" #x ")"); })
#else
#define WARN_ON(x) WARN((x), "%s", "WARN_ON(" __stringify(x) ")")
#endif

#undef WARN_ON_ONCE
#define WARN_ON_ONCE(x) WARN_ONCE((x), "%s", "WARN_ON_ONCE(" __stringify(x) ")")

#define MISSING_CASE(x) WARN(1, "Missing switch case (%lu) in %s\n", \
			     (long) (x), __func__);

/* Use I915_STATE_WARN(x) and I915_STATE_WARN_ON() (rather than WARN() and
 * WARN_ON()) for hw state sanity checks to check for unexpected conditions
 * which may not necessarily be a user visible problem.  This will either
 * WARN() or DRM_ERROR() depending on the verbose_checks moduleparam, to
 * enable distros and users to tailor their preferred amount of i915 abrt
 * spam.
 */
#define I915_STATE_WARN(condition, format...) ({			\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		if (!WARN(i915.verbose_state_checks, format))		\
			DRM_ERROR(format);				\
	unlikely(__ret_warn_on);					\
})

#define I915_STATE_WARN_ON(x)						\
	I915_STATE_WARN((x), "%s", "WARN_ON(" __stringify(x) ")")

bool __i915_inject_load_failure(const char *func, int line);
#define i915_inject_load_failure() \
	__i915_inject_load_failure(__func__, __LINE__)

static inline const char *yesno(bool v)
{
	return v ? "yes" : "no";
}

static inline const char *onoff(bool v)
{
	return v ? "on" : "off";
}

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
 * I915_MAX_PLANES in the enum below is the maximum (across all platforms)
 * number of planes per CRTC.  Not all platforms really have this many planes,
 * which means some arrays of size I915_MAX_PLANES may have unused entries
 * between the topmost sprite plane and the cursor plane.
 */
enum plane {
	PLANE_A = 0,
	PLANE_B,
	PLANE_C,
	PLANE_CURSOR,
	I915_MAX_PLANES,
};
#define plane_name(p) ((p) + 'A')

#define sprite_name(p, s) ((p) * INTEL_INFO(dev)->num_sprites[(p)] + (s) + 'A')

enum port {
	PORT_NONE = -1,
	PORT_A = 0,
	PORT_B,
	PORT_C,
	PORT_D,
	PORT_E,
	I915_MAX_PORTS
};
#define port_name(p) ((p) + 'A')

#define I915_NUM_PHYS_VLV 2

enum dpio_channel {
	DPIO_CH0,
	DPIO_CH1
};

enum dpio_phy {
	DPIO_PHY0,
	DPIO_PHY1
};

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
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_INIT,

	POWER_DOMAIN_NUM,
};

#define POWER_DOMAIN_PIPE(pipe) ((pipe) + POWER_DOMAIN_PIPE_A)
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) \
		((pipe) + POWER_DOMAIN_PIPE_A_PANEL_FITTER)
#define POWER_DOMAIN_TRANSCODER(tran) \
	((tran) == TRANSCODER_EDP ? POWER_DOMAIN_TRANSCODER_EDP : \
	 (tran) + POWER_DOMAIN_TRANSCODER_A)

enum hpd_pin {
	HPD_NONE = 0,
	HPD_TV = HPD_NONE,     /* TV is known to be unreliable */
	HPD_CRT,
	HPD_SDVO_B,
	HPD_SDVO_C,
	HPD_PORT_A,
	HPD_PORT_B,
	HPD_PORT_C,
	HPD_PORT_D,
	HPD_PORT_E,
	HPD_NUM_PINS
};

#define for_each_hpd_pin(__pin) \
	for ((__pin) = (HPD_NONE + 1); (__pin) < HPD_NUM_PINS; (__pin)++)

struct i915_hotplug {
	struct work_struct hotplug_work;

	struct {
		unsigned long last_jiffies;
		int count;
		enum {
			HPD_ENABLED = 0,
			HPD_DISABLED = 1,
			HPD_MARK_DISABLED = 2
		} state;
	} stats[HPD_NUM_PINS];
	u32 event_bits;
	struct delayed_work reenable_work;

	struct intel_digital_port *irq_port[I915_MAX_PORTS];
	u32 long_port_mask;
	u32 short_port_mask;
	struct work_struct dig_port_work;

	struct work_struct poll_init_work;
	bool poll_enabled;

	/*
	 * if we get a HPD irq from DP and a HPD irq from non-DP
	 * the non-DP HPD could block the workqueue on a mode config
	 * mutex getting, that userspace may have taken. However
	 * userspace is waiting on the DP workqueue to run which is
	 * blocked behind the non-DP one.
	 */
	struct workqueue_struct *dp_wq;
};

#define I915_GEM_GPU_DOMAINS \
	(I915_GEM_DOMAIN_RENDER | \
	 I915_GEM_DOMAIN_SAMPLER | \
	 I915_GEM_DOMAIN_COMMAND | \
	 I915_GEM_DOMAIN_INSTRUCTION | \
	 I915_GEM_DOMAIN_VERTEX)

#define for_each_pipe(__dev_priv, __p) \
	for ((__p) = 0; (__p) < INTEL_INFO(__dev_priv)->num_pipes; (__p)++)
#define for_each_pipe_masked(__dev_priv, __p, __mask) \
	for ((__p) = 0; (__p) < INTEL_INFO(__dev_priv)->num_pipes; (__p)++) \
		for_each_if ((__mask) & (1 << (__p)))
#define for_each_plane(__dev_priv, __pipe, __p)				\
	for ((__p) = 0;							\
	     (__p) < INTEL_INFO(__dev_priv)->num_sprites[(__pipe)] + 1;	\
	     (__p)++)
#define for_each_sprite(__dev_priv, __p, __s)				\
	for ((__s) = 0;							\
	     (__s) < INTEL_INFO(__dev_priv)->num_sprites[(__p)];	\
	     (__s)++)

#define for_each_port_masked(__port, __ports_mask) \
	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++)	\
		for_each_if ((__ports_mask) & (1 << (__port)))

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
		for_each_if ((plane_mask) &				\
			     (1 << drm_plane_index(&intel_plane->base)))

#define for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane)	\
	list_for_each_entry(intel_plane,				\
			    &(dev)->mode_config.plane_list,		\
			    base.head)					\
		for_each_if ((intel_plane)->pipe == (intel_crtc)->pipe)

#define for_each_intel_crtc(dev, intel_crtc)				\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)

#define for_each_intel_crtc_mask(dev, intel_crtc, crtc_mask)		\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)					\
		for_each_if ((crtc_mask) & (1 << drm_crtc_index(&intel_crtc->base)))

#define for_each_intel_encoder(dev, intel_encoder)		\
	list_for_each_entry(intel_encoder,			\
			    &(dev)->mode_config.encoder_list,	\
			    base.head)

#define for_each_intel_connector(dev, intel_connector)		\
	list_for_each_entry(intel_connector,			\
			    &(dev)->mode_config.connector_list,	\
			    base.head)

#define for_each_encoder_on_crtc(dev, __crtc, intel_encoder) \
	list_for_each_entry((intel_encoder), &(dev)->mode_config.encoder_list, base.head) \
		for_each_if ((intel_encoder)->base.crtc == (__crtc))

#define for_each_connector_on_encoder(dev, __encoder, intel_connector) \
	list_for_each_entry((intel_connector), &(dev)->mode_config.connector_list, base.head) \
		for_each_if ((intel_connector)->base.encoder == (__encoder))

#define for_each_power_domain(domain, mask)				\
	for ((domain) = 0; (domain) < POWER_DOMAIN_NUM; (domain)++)	\
		for_each_if ((1 << (domain)) & (mask))

struct drm_i915_private;
struct i915_mm_struct;
struct i915_mmu_object;

struct drm_i915_file_private {
	struct drm_i915_private *dev_priv;
	struct drm_file *file;

	struct {
		spinlock_t lock;
		struct list_head request_list;
/* 20ms is a fairly arbitrary limit (greater than the average frame time)
 * chosen to prevent the CPU getting more than a frame ahead of the GPU
 * (when using lax throttling for the frontbuffer). We also use it to
 * offer free GPU waitboosts for severely congested workloads.
 */
#define DRM_I915_THROTTLE_JIFFIES msecs_to_jiffies(20)
	} mm;
	struct idr context_idr;

	struct intel_rps_client {
		struct list_head link;
		unsigned boosts;
	} rps;

	unsigned int bsd_engine;
};

/* Used by dp and fdi links */
struct intel_link_m_n {
	uint32_t	tu;
	uint32_t	gmch_m;
	uint32_t	gmch_n;
	uint32_t	link_m;
	uint32_t	link_n;
};

void intel_link_compute_m_n(int bpp, int nlanes,
			    int pixel_clock, int link_clock,
			    struct intel_link_m_n *m_n);

/* Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	0

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;

struct intel_opregion {
	struct opregion_header *header;
	struct opregion_acpi *acpi;
	struct opregion_swsci *swsci;
	u32 swsci_gbda_sub_functions;
	u32 swsci_sbcb_sub_functions;
	struct opregion_asle *asle;
	void *rvda;
	const void *vbt;
	u32 vbt_size;
	u32 *lid_state;
	struct work_struct asle_work;
};
#define OPREGION_SIZE            (8*1024)

struct intel_overlay;
struct intel_overlay_error_state;

struct drm_i915_fence_reg {
	struct list_head link;
	struct drm_i915_private *i915;
	struct i915_vma *vma;
	int pin_count;
	int id;
	/**
	 * Whether the tiling parameters for the currently
	 * associated fence register have changed. Note that
	 * for the purposes of tracking tiling changes we also
	 * treat the unfenced register, the register slot that
	 * the object occupies whilst it executes a fenced
	 * command (such as BLT on gen2/3), as a "fence".
	 */
	bool dirty;
};

struct sdvo_device_mapping {
	u8 initialized;
	u8 dvo_port;
	u8 slave_addr;
	u8 dvo_wiring;
	u8 i2c_pin;
	u8 ddc_pin;
};

struct intel_connector;
struct intel_encoder;
struct intel_crtc_state;
struct intel_initial_plane_config;
struct intel_crtc;
struct intel_limit;
struct dpll;

struct drm_i915_display_funcs {
	int (*get_display_clock_speed)(struct drm_device *dev);
	int (*get_fifo_size)(struct drm_device *dev, int plane);
	int (*compute_pipe_wm)(struct intel_crtc_state *cstate);
	int (*compute_intermediate_wm)(struct drm_device *dev,
				       struct intel_crtc *intel_crtc,
				       struct intel_crtc_state *newstate);
	void (*initial_watermarks)(struct intel_crtc_state *cstate);
	void (*optimize_watermarks)(struct intel_crtc_state *cstate);
	int (*compute_global_watermarks)(struct drm_atomic_state *state);
	void (*update_wm)(struct drm_crtc *crtc);
	int (*modeset_calc_cdclk)(struct drm_atomic_state *state);
	void (*modeset_commit_cdclk)(struct drm_atomic_state *state);
	/* Returns the active state of the crtc, and if the crtc is active,
	 * fills out the pipe-config with the hw state. */
	bool (*get_pipe_config)(struct intel_crtc *,
				struct intel_crtc_state *);
	void (*get_initial_plane_config)(struct intel_crtc *,
					 struct intel_initial_plane_config *);
	int (*crtc_compute_clock)(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state);
	void (*crtc_enable)(struct intel_crtc_state *pipe_config,
			    struct drm_atomic_state *old_state);
	void (*crtc_disable)(struct intel_crtc_state *old_crtc_state,
			     struct drm_atomic_state *old_state);
	void (*update_crtcs)(struct drm_atomic_state *state,
			     unsigned int *crtc_vblank_mask);
	void (*audio_codec_enable)(struct drm_connector *connector,
				   struct intel_encoder *encoder,
				   const struct drm_display_mode *adjusted_mode);
	void (*audio_codec_disable)(struct intel_encoder *encoder);
	void (*fdi_link_train)(struct drm_crtc *crtc);
	void (*init_clock_gating)(struct drm_device *dev);
	int (*queue_flip)(struct drm_device *dev, struct drm_crtc *crtc,
			  struct drm_framebuffer *fb,
			  struct drm_i915_gem_object *obj,
			  struct drm_i915_gem_request *req,
			  uint32_t flags);
	void (*hpd_irq_setup)(struct drm_i915_private *dev_priv);
	/* clock updates for mode set */
	/* cursor updates */
	/* render clock increase/decrease */
	/* display clock increase/decrease */
	/* pll clock increase/decrease */

	void (*load_csc_matrix)(struct drm_crtc_state *crtc_state);
	void (*load_luts)(struct drm_crtc_state *crtc_state);
};

enum forcewake_domain_id {
	FW_DOMAIN_ID_RENDER = 0,
	FW_DOMAIN_ID_BLITTER,
	FW_DOMAIN_ID_MEDIA,

	FW_DOMAIN_ID_COUNT
};

enum forcewake_domains {
	FORCEWAKE_RENDER = (1 << FW_DOMAIN_ID_RENDER),
	FORCEWAKE_BLITTER = (1 << FW_DOMAIN_ID_BLITTER),
	FORCEWAKE_MEDIA	= (1 << FW_DOMAIN_ID_MEDIA),
	FORCEWAKE_ALL = (FORCEWAKE_RENDER |
			 FORCEWAKE_BLITTER |
			 FORCEWAKE_MEDIA)
};

#define FW_REG_READ  (1)
#define FW_REG_WRITE (2)

enum forcewake_domains
intel_uncore_forcewake_for_reg(struct drm_i915_private *dev_priv,
			       i915_reg_t reg, unsigned int op);

struct intel_uncore_funcs {
	void (*force_wake_get)(struct drm_i915_private *dev_priv,
							enum forcewake_domains domains);
	void (*force_wake_put)(struct drm_i915_private *dev_priv,
							enum forcewake_domains domains);

	uint8_t  (*mmio_readb)(struct drm_i915_private *dev_priv, i915_reg_t r, bool trace);
	uint16_t (*mmio_readw)(struct drm_i915_private *dev_priv, i915_reg_t r, bool trace);
	uint32_t (*mmio_readl)(struct drm_i915_private *dev_priv, i915_reg_t r, bool trace);
	uint64_t (*mmio_readq)(struct drm_i915_private *dev_priv, i915_reg_t r, bool trace);

	void (*mmio_writeb)(struct drm_i915_private *dev_priv, i915_reg_t r,
				uint8_t val, bool trace);
	void (*mmio_writew)(struct drm_i915_private *dev_priv, i915_reg_t r,
				uint16_t val, bool trace);
	void (*mmio_writel)(struct drm_i915_private *dev_priv, i915_reg_t r,
				uint32_t val, bool trace);
};

struct intel_forcewake_range {
	u32 start;
	u32 end;

	enum forcewake_domains domains;
};

struct intel_uncore {
	spinlock_t lock; /** lock is also taken in irq contexts. */

	const struct intel_forcewake_range *fw_domains_table;
	unsigned int fw_domains_table_entries;

	struct intel_uncore_funcs funcs;

	unsigned fifo_count;

	enum forcewake_domains fw_domains;
	enum forcewake_domains fw_domains_active;

	struct intel_uncore_forcewake_domain {
		struct drm_i915_private *i915;
		enum forcewake_domain_id id;
		enum forcewake_domains mask;
		unsigned wake_count;
		struct hrtimer timer;
		i915_reg_t reg_set;
		u32 val_set;
		u32 val_clear;
		i915_reg_t reg_ack;
		i915_reg_t reg_post;
		u32 val_reset;
	} fw_domain[FW_DOMAIN_ID_COUNT];

	int unclaimed_mmio_check;
};

/* Iterate over initialised fw domains */
#define for_each_fw_domain_masked(domain__, mask__, dev_priv__) \
	for ((domain__) = &(dev_priv__)->uncore.fw_domain[0]; \
	     (domain__) < &(dev_priv__)->uncore.fw_domain[FW_DOMAIN_ID_COUNT]; \
	     (domain__)++) \
		for_each_if ((mask__) & (domain__)->mask)

#define for_each_fw_domain(domain__, dev_priv__) \
	for_each_fw_domain_masked(domain__, FORCEWAKE_ALL, dev_priv__)

#define CSR_VERSION(major, minor)	((major) << 16 | (minor))
#define CSR_VERSION_MAJOR(version)	((version) >> 16)
#define CSR_VERSION_MINOR(version)	((version) & 0xffff)

struct intel_csr {
	struct work_struct work;
	const char *fw_path;
	uint32_t *dmc_payload;
	uint32_t dmc_fw_size;
	uint32_t version;
	uint32_t mmio_count;
	i915_reg_t mmioaddr[8];
	uint32_t mmiodata[8];
	uint32_t dc_state;
	uint32_t allowed_dc_mask;
};

#define DEV_INFO_FOR_EACH_FLAG(func) \
	/* Keep is_* in chronological order */ \
	func(is_mobile); \
	func(is_i85x); \
	func(is_i915g); \
	func(is_i945gm); \
	func(is_g33); \
	func(is_g4x); \
	func(is_pineview); \
	func(is_broadwater); \
	func(is_crestline); \
	func(is_ivybridge); \
	func(is_valleyview); \
	func(is_cherryview); \
	func(is_haswell); \
	func(is_broadwell); \
	func(is_skylake); \
	func(is_broxton); \
	func(is_kabylake); \
	func(is_preliminary); \
	/* Keep has_* in alphabetical order */ \
	func(has_csr); \
	func(has_ddi); \
	func(has_dp_mst); \
	func(has_fbc); \
	func(has_fpga_dbg); \
	func(has_gmbus_irq); \
	func(has_gmch_display); \
	func(has_guc); \
	func(has_hotplug); \
	func(has_hw_contexts); \
	func(has_l3_dpf); \
	func(has_llc); \
	func(has_logical_ring_contexts); \
	func(has_overlay); \
	func(has_pipe_cxsr); \
	func(has_pooled_eu); \
	func(has_psr); \
	func(has_rc6); \
	func(has_rc6p); \
	func(has_resource_streamer); \
	func(has_runtime_pm); \
	func(has_snoop); \
	func(cursor_needs_physical); \
	func(hws_needs_physical); \
	func(overlay_needs_physical); \
	func(supports_tv)

struct sseu_dev_info {
	u8 slice_mask;
	u8 subslice_mask;
	u8 eu_total;
	u8 eu_per_subslice;
	u8 min_eu_in_pool;
	/* For each slice, which subslice(s) has(have) 7 EUs (bitfield)? */
	u8 subslice_7eu[3];
	u8 has_slice_pg:1;
	u8 has_subslice_pg:1;
	u8 has_eu_pg:1;
};

static inline unsigned int sseu_subslice_total(const struct sseu_dev_info *sseu)
{
	return hweight8(sseu->slice_mask) * hweight8(sseu->subslice_mask);
}

struct intel_device_info {
	u32 display_mmio_offset;
	u16 device_id;
	u8 num_pipes;
	u8 num_sprites[I915_MAX_PIPES];
	u8 gen;
	u16 gen_mask;
	u8 ring_mask; /* Rings supported by the HW */
	u8 num_rings;
#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG
	u16 ddb_size; /* in blocks */
	/* Register offsets for the various display pipes and transcoders */
	int pipe_offsets[I915_MAX_TRANSCODERS];
	int trans_offsets[I915_MAX_TRANSCODERS];
	int palette_offsets[I915_MAX_PIPES];
	int cursor_offsets[I915_MAX_PIPES];

	/* Slice/subslice/EU info */
	struct sseu_dev_info sseu;

	struct color_luts {
		u16 degamma_lut_size;
		u16 gamma_lut_size;
	} color;
};

struct intel_display_error_state;

struct drm_i915_error_state {
	struct kref ref;
	struct timeval time;

	struct drm_i915_private *i915;

	char error_msg[128];
	bool simulated;
	int iommu;
	u32 reset_count;
	u32 suspend_count;
	struct intel_device_info device_info;

	/* Generic register state */
	u32 eir;
	u32 pgtbl_er;
	u32 ier;
	u32 gtier[4];
	u32 ccid;
	u32 derrmr;
	u32 forcewake;
	u32 error; /* gen6+ */
	u32 err_int; /* gen7 */
	u32 fault_data0; /* gen8, gen9 */
	u32 fault_data1; /* gen8, gen9 */
	u32 done_reg;
	u32 gac_eco;
	u32 gam_ecochk;
	u32 gab_ctl;
	u32 gfx_mode;

	u64 fence[I915_MAX_NUM_FENCES];
	struct intel_overlay_error_state *overlay;
	struct intel_display_error_state *display;
	struct drm_i915_error_object *semaphore;

	struct drm_i915_error_engine {
		int engine_id;
		/* Software tracked state */
		bool waiting;
		int num_waiters;
		int hangcheck_score;
		enum intel_engine_hangcheck_action hangcheck_action;
		struct i915_address_space *vm;
		int num_requests;

		/* position of active request inside the ring */
		u32 rq_head, rq_post, rq_tail;

		/* our own tracking of ring head and tail */
		u32 cpu_ring_head;
		u32 cpu_ring_tail;

		u32 last_seqno;
		u32 semaphore_seqno[I915_NUM_ENGINES - 1];

		/* Register state */
		u32 start;
		u32 tail;
		u32 head;
		u32 ctl;
		u32 mode;
		u32 hws;
		u32 ipeir;
		u32 ipehr;
		u32 bbstate;
		u32 instpm;
		u32 instps;
		u32 seqno;
		u64 bbaddr;
		u64 acthd;
		u32 fault_reg;
		u64 faddr;
		u32 rc_psmi; /* sleep state */
		u32 semaphore_mboxes[I915_NUM_ENGINES - 1];
		struct intel_instdone instdone;

		struct drm_i915_error_object {
			u64 gtt_offset;
			u64 gtt_size;
			int page_count;
			int unused;
			u32 *pages[0];
		} *ringbuffer, *batchbuffer, *wa_batchbuffer, *ctx, *hws_page;

		struct drm_i915_error_object *wa_ctx;

		struct drm_i915_error_request {
			long jiffies;
			pid_t pid;
			u32 context;
			u32 seqno;
			u32 head;
			u32 tail;
		} *requests, execlist[2];

		struct drm_i915_error_waiter {
			char comm[TASK_COMM_LEN];
			pid_t pid;
			u32 seqno;
		} *waiters;

		struct {
			u32 gfx_mode;
			union {
				u64 pdp[4];
				u32 pp_dir_base;
			};
		} vm_info;

		pid_t pid;
		char comm[TASK_COMM_LEN];
	} engine[I915_NUM_ENGINES];

	struct drm_i915_error_buffer {
		u32 size;
		u32 name;
		u32 rseqno[I915_NUM_ENGINES], wseqno;
		u64 gtt_offset;
		u32 read_domains;
		u32 write_domain;
		s32 fence_reg:I915_MAX_NUM_FENCE_BITS;
		u32 tiling:2;
		u32 dirty:1;
		u32 purgeable:1;
		u32 userptr:1;
		s32 engine:4;
		u32 cache_level:3;
	} *active_bo[I915_NUM_ENGINES], *pinned_bo;
	u32 active_bo_count[I915_NUM_ENGINES], pinned_bo_count;
	struct i915_address_space *active_vm[I915_NUM_ENGINES];
};

enum i915_cache_level {
	I915_CACHE_NONE = 0,
	I915_CACHE_LLC, /* also used for snoopable memory on non-LLC */
	I915_CACHE_L3_LLC, /* gen7+, L3 sits between the domain specifc
			      caches, eg sampler/render caches, and the
			      large Last-Level-Cache. LLC is coherent with
			      the CPU, but L3 is only visible to the GPU. */
	I915_CACHE_WT, /* hsw:gt3e WriteThrough for scanouts */
};

struct i915_ctx_hang_stats {
	/* This context had batch pending when hang was declared */
	unsigned batch_pending;

	/* This context had batch active when hang was declared */
	unsigned batch_active;

	/* Time when this context was last blamed for a GPU reset */
	unsigned long guilty_ts;

	/* If the contexts causes a second GPU hang within this time,
	 * it is permanently banned from submitting any more work.
	 */
	unsigned long ban_period_seconds;

	/* This context is banned to submit more work */
	bool banned;
};

/* This must match up with the value previously used for execbuf2.rsvd1. */
#define DEFAULT_CONTEXT_HANDLE 0

/**
 * struct i915_gem_context - as the name implies, represents a context.
 * @ref: reference count.
 * @user_handle: userspace tracking identity for this context.
 * @remap_slice: l3 row remapping information.
 * @flags: context specific flags:
 *         CONTEXT_NO_ZEROMAP: do not allow mapping things to page 0.
 * @file_priv: filp associated with this context (NULL for global default
 *	       context).
 * @hang_stats: information about the role of this context in possible GPU
 *		hangs.
 * @ppgtt: virtual memory space used by this context.
 * @legacy_hw_ctx: render context backing object and whether it is correctly
 *                initialized (legacy ring submission mechanism only).
 * @link: link in the global list of contexts.
 *
 * Contexts are memory images used by the hardware to store copies of their
 * internal state.
 */
struct i915_gem_context {
	struct kref ref;
	struct drm_i915_private *i915;
	struct drm_i915_file_private *file_priv;
	struct i915_hw_ppgtt *ppgtt;
	struct pid *pid;

	struct i915_ctx_hang_stats hang_stats;

	unsigned long flags;
#define CONTEXT_NO_ZEROMAP		BIT(0)
#define CONTEXT_NO_ERROR_CAPTURE	BIT(1)

	/* Unique identifier for this context, used by the hw for tracking */
	unsigned int hw_id;
	u32 user_handle;

	u32 ggtt_alignment;

	struct intel_context {
		struct i915_vma *state;
		struct intel_ring *ring;
		uint32_t *lrc_reg_state;
		u64 lrc_desc;
		int pin_count;
		bool initialised;
	} engine[I915_NUM_ENGINES];
	u32 ring_size;
	u32 desc_template;
	struct atomic_notifier_head status_notifier;
	bool execlists_force_single_submission;

	struct list_head link;

	u8 remap_slice;
	bool closed:1;
};

enum fb_op_origin {
	ORIGIN_GTT,
	ORIGIN_CPU,
	ORIGIN_CS,
	ORIGIN_FLIP,
	ORIGIN_DIRTYFB,
};

struct intel_fbc {
	/* This is always the inner lock when overlapping with struct_mutex and
	 * it's the outer lock when overlapping with stolen_lock. */
	struct mutex lock;
	unsigned threshold;
	unsigned int possible_framebuffer_bits;
	unsigned int busy_bits;
	unsigned int visible_pipes_mask;
	struct intel_crtc *crtc;

	struct drm_mm_node compressed_fb;
	struct drm_mm_node *compressed_llb;

	bool false_color;

	bool enabled;
	bool active;

	bool underrun_detected;
	struct work_struct underrun_work;

	struct intel_fbc_state_cache {
		struct {
			unsigned int mode_flags;
			uint32_t hsw_bdw_pixel_rate;
		} crtc;

		struct {
			unsigned int rotation;
			int src_w;
			int src_h;
			bool visible;
		} plane;

		struct {
			u64 ilk_ggtt_offset;
			uint32_t pixel_format;
			unsigned int stride;
			int fence_reg;
			unsigned int tiling_mode;
		} fb;
	} state_cache;

	struct intel_fbc_reg_params {
		struct {
			enum pipe pipe;
			enum plane plane;
			unsigned int fence_y_offset;
		} crtc;

		struct {
			u64 ggtt_offset;
			uint32_t pixel_format;
			unsigned int stride;
			int fence_reg;
		} fb;

		int cfb_size;
	} params;

	struct intel_fbc_work {
		bool scheduled;
		u32 scheduled_vblank;
		struct work_struct work;
	} work;

	const char *no_fbc_reason;
};

/**
 * HIGH_RR is the highest eDP panel refresh rate read from EDID
 * LOW_RR is the lowest eDP panel refresh rate found from EDID
 * parsing for same resolution.
 */
enum drrs_refresh_rate_type {
	DRRS_HIGH_RR,
	DRRS_LOW_RR,
	DRRS_MAX_RR, /* RR count */
};

enum drrs_support_type {
	DRRS_NOT_SUPPORTED = 0,
	STATIC_DRRS_SUPPORT = 1,
	SEAMLESS_DRRS_SUPPORT = 2
};

struct intel_dp;
struct i915_drrs {
	struct mutex mutex;
	struct delayed_work work;
	struct intel_dp *dp;
	unsigned busy_frontbuffer_bits;
	enum drrs_refresh_rate_type refresh_rate_type;
	enum drrs_support_type type;
};

struct i915_psr {
	struct mutex lock;
	bool sink_support;
	bool source_ok;
	struct intel_dp *enabled;
	bool active;
	struct delayed_work work;
	unsigned busy_frontbuffer_bits;
	bool psr2_support;
	bool aux_frame_sync;
	bool link_standby;
};

enum intel_pch {
	PCH_NONE = 0,	/* No PCH present */
	PCH_IBX,	/* Ibexpeak PCH */
	PCH_CPT,	/* Cougarpoint PCH */
	PCH_LPT,	/* Lynxpoint PCH */
	PCH_SPT,        /* Sunrisepoint PCH */
	PCH_KBP,        /* Kabypoint PCH */
	PCH_NOP,
};

enum intel_sbi_destination {
	SBI_ICLK,
	SBI_MPHY,
};

#define QUIRK_PIPEA_FORCE (1<<0)
#define QUIRK_LVDS_SSC_DISABLE (1<<1)
#define QUIRK_INVERT_BRIGHTNESS (1<<2)
#define QUIRK_BACKLIGHT_PRESENT (1<<3)
#define QUIRK_PIPEB_FORCE (1<<4)
#define QUIRK_PIN_SWIZZLED_PAGES (1<<5)

struct intel_fbdev;
struct intel_fbc_work;

struct intel_gmbus {
	struct i2c_adapter adapter;
#define GMBUS_FORCE_BIT_RETRY (1U << 31)
	u32 force_bit;
	u32 reg0;
	i915_reg_t gpio_reg;
	struct i2c_algo_bit_data bit_algo;
	struct drm_i915_private *dev_priv;
};

struct i915_suspend_saved_registers {
	u32 saveDSPARB;
	u32 saveFBC_CONTROL;
	u32 saveCACHE_MODE_0;
	u32 saveMI_ARB_STATE;
	u32 saveSWF0[16];
	u32 saveSWF1[16];
	u32 saveSWF3[3];
	uint64_t saveFENCE[I915_MAX_NUM_FENCES];
	u32 savePCH_PORT_HOTPLUG;
	u16 saveGCDGMBUS;
};

struct vlv_s0ix_state {
	/* GAM */
	u32 wr_watermark;
	u32 gfx_prio_ctrl;
	u32 arb_mode;
	u32 gfx_pend_tlb0;
	u32 gfx_pend_tlb1;
	u32 lra_limits[GEN7_LRA_LIMITS_REG_NUM];
	u32 media_max_req_count;
	u32 gfx_max_req_count;
	u32 render_hwsp;
	u32 ecochk;
	u32 bsd_hwsp;
	u32 blt_hwsp;
	u32 tlb_rd_addr;

	/* MBC */
	u32 g3dctl;
	u32 gsckgctl;
	u32 mbctl;

	/* GCP */
	u32 ucgctl1;
	u32 ucgctl3;
	u32 rcgctl1;
	u32 rcgctl2;
	u32 rstctl;
	u32 misccpctl;

	/* GPM */
	u32 gfxpause;
	u32 rpdeuhwtc;
	u32 rpdeuc;
	u32 ecobus;
	u32 pwrdwnupctl;
	u32 rp_down_timeout;
	u32 rp_deucsw;
	u32 rcubmabdtmr;
	u32 rcedata;
	u32 spare2gh;

	/* Display 1 CZ domain */
	u32 gt_imr;
	u32 gt_ier;
	u32 pm_imr;
	u32 pm_ier;
	u32 gt_scratch[GEN7_GT_SCRATCH_REG_NUM];

	/* GT SA CZ domain */
	u32 tilectl;
	u32 gt_fifoctl;
	u32 gtlc_wake_ctrl;
	u32 gtlc_survive;
	u32 pmwgicz;

	/* Display 2 CZ domain */
	u32 gu_ctl0;
	u32 gu_ctl1;
	u32 pcbr;
	u32 clock_gate_dis2;
};

struct intel_rps_ei {
	u32 cz_clock;
	u32 render_c0;
	u32 media_c0;
};

struct intel_gen6_power_mgmt {
	/*
	 * work, interrupts_enabled and pm_iir are protected by
	 * dev_priv->irq_lock
	 */
	struct work_struct work;
	bool interrupts_enabled;
	u32 pm_iir;

	/* PM interrupt bits that should never be masked */
	u32 pm_intr_keep;

	/* Frequencies are stored in potentially platform dependent multiples.
	 * In other words, *_freq needs to be multiplied by X to be interesting.
	 * Soft limits are those which are used for the dynamic reclocking done
	 * by the driver (raise frequencies under heavy loads, and lower for
	 * lighter loads). Hard limits are those imposed by the hardware.
	 *
	 * A distinction is made for overclocking, which is never enabled by
	 * default, and is considered to be above the hard limit if it's
	 * possible at all.
	 */
	u8 cur_freq;		/* Current frequency (cached, may not == HW) */
	u8 min_freq_softlimit;	/* Minimum frequency permitted by the driver */
	u8 max_freq_softlimit;	/* Max frequency permitted by the driver */
	u8 max_freq;		/* Maximum frequency, RP0 if not overclocking */
	u8 min_freq;		/* AKA RPn. Minimum frequency */
	u8 boost_freq;		/* Frequency to request when wait boosting */
	u8 idle_freq;		/* Frequency to request when we are idle */
	u8 efficient_freq;	/* AKA RPe. Pre-determined balanced frequency */
	u8 rp1_freq;		/* "less than" RP0 power/freqency */
	u8 rp0_freq;		/* Non-overclocked max frequency. */
	u16 gpll_ref_freq;	/* vlv/chv GPLL reference frequency */

	u8 up_threshold; /* Current %busy required to uplock */
	u8 down_threshold; /* Current %busy required to downclock */

	int last_adj;
	enum { LOW_POWER, BETWEEN, HIGH_POWER } power;

	spinlock_t client_lock;
	struct list_head clients;
	bool client_boost;

	bool enabled;
	struct delayed_work autoenable_work;
	unsigned boosts;

	/* manual wa residency calculations */
	struct intel_rps_ei up_ei, down_ei;

	/*
	 * Protects RPS/RC6 register access and PCU communication.
	 * Must be taken after struct_mutex if nested. Note that
	 * this lock may be held for long periods of time when
	 * talking to hw - so only take it when talking to hw!
	 */
	struct mutex hw_lock;
};

/* defined intel_pm.c */
extern spinlock_t mchdev_lock;

struct intel_ilk_power_mgmt {
	u8 cur_delay;
	u8 min_delay;
	u8 max_delay;
	u8 fmax;
	u8 fstart;

	u64 last_count1;
	unsigned long last_time1;
	unsigned long chipset_power;
	u64 last_count2;
	u64 last_time2;
	unsigned long gfx_power;
	u8 corr;

	int c_m;
	int r_t;
};

struct drm_i915_private;
struct i915_power_well;

struct i915_power_well_ops {
	/*
	 * Synchronize the well's hw state to match the current sw state, for
	 * example enable/disable it based on the current refcount. Called
	 * during driver init and resume time, possibly after first calling
	 * the enable/disable handlers.
	 */
	void (*sync_hw)(struct drm_i915_private *dev_priv,
			struct i915_power_well *power_well);
	/*
	 * Enable the well and resources that depend on it (for example
	 * interrupts located on the well). Called after the 0->1 refcount
	 * transition.
	 */
	void (*enable)(struct drm_i915_private *dev_priv,
		       struct i915_power_well *power_well);
	/*
	 * Disable the well and resources that depend on it. Called after
	 * the 1->0 refcount transition.
	 */
	void (*disable)(struct drm_i915_private *dev_priv,
			struct i915_power_well *power_well);
	/* Returns the hw enabled state. */
	bool (*is_enabled)(struct drm_i915_private *dev_priv,
			   struct i915_power_well *power_well);
};

/* Power well structure for haswell */
struct i915_power_well {
	const char *name;
	bool always_on;
	/* power well enable/disable usage count */
	int count;
	/* cached hw enabled state */
	bool hw_enabled;
	unsigned long domains;
	unsigned long data;
	const struct i915_power_well_ops *ops;
};

struct i915_power_domains {
	/*
	 * Power wells needed for initialization at driver init and suspend
	 * time are on. They are kept on until after the first modeset.
	 */
	bool init_power_on;
	bool initializing;
	int power_well_count;

	struct mutex lock;
	int domain_use_count[POWER_DOMAIN_NUM];
	struct i915_power_well *power_wells;
};

#define MAX_L3_SLICES 2
struct intel_l3_parity {
	u32 *remap_info[MAX_L3_SLICES];
	struct work_struct error_work;
	int which_slice;
};

struct i915_gem_mm {
	/** Memory allocator for GTT stolen memory */
	struct drm_mm stolen;
	/** Protects the usage of the GTT stolen memory allocator. This is
	 * always the inner lock when overlapping with struct_mutex. */
	struct mutex stolen_lock;

	/** List of all objects in gtt_space. Used to restore gtt
	 * mappings on resume */
	struct list_head bound_list;
	/**
	 * List of objects which are not bound to the GTT (thus
	 * are idle and not used by the GPU) but still have
	 * (presumably uncached) pages still attached.
	 */
	struct list_head unbound_list;

	/** List of all objects in gtt_space, currently mmaped by userspace.
	 * All objects within this list must also be on bound_list.
	 */
	struct list_head userfault_list;

	/** Usable portion of the GTT for GEM */
	unsigned long stolen_base; /* limited to low memory (32-bit) */

	/** PPGTT used for aliasing the PPGTT with the GTT */
	struct i915_hw_ppgtt *aliasing_ppgtt;

	struct notifier_block oom_notifier;
	struct notifier_block vmap_notifier;
	struct shrinker shrinker;

	/** LRU list of objects with fence regs on them. */
	struct list_head fence_list;

	/**
	 * Are we in a non-interruptible section of code like
	 * modesetting?
	 */
	bool interruptible;

	/* the indicator for dispatch video commands on two BSD rings */
	atomic_t bsd_engine_dispatch_index;

	/** Bit 6 swizzling required for X tiling */
	uint32_t bit_6_swizzle_x;
	/** Bit 6 swizzling required for Y tiling */
	uint32_t bit_6_swizzle_y;

	/* accounting, useful for userland debugging */
	spinlock_t object_stat_lock;
	u64 object_memory;
	u32 object_count;
};

struct drm_i915_error_state_buf {
	struct drm_i915_private *i915;
	unsigned bytes;
	unsigned size;
	int err;
	u8 *buf;
	loff_t start;
	loff_t pos;
};

struct i915_error_state_file_priv {
	struct drm_device *dev;
	struct drm_i915_error_state *error;
};

struct i915_gpu_error {
	/* For hangcheck timer */
#define DRM_I915_HANGCHECK_PERIOD 1500 /* in ms */
#define DRM_I915_HANGCHECK_JIFFIES msecs_to_jiffies(DRM_I915_HANGCHECK_PERIOD)
	/* Hang gpu twice in this window and your context gets banned */
#define DRM_I915_CTX_BAN_PERIOD DIV_ROUND_UP(8*DRM_I915_HANGCHECK_PERIOD, 1000)

	struct delayed_work hangcheck_work;

	/* For reset and error_state handling. */
	spinlock_t lock;
	/* Protected by the above dev->gpu_error.lock. */
	struct drm_i915_error_state *first_error;

	unsigned long missed_irq_rings;

	/**
	 * State variable controlling the reset flow and count
	 *
	 * This is a counter which gets incremented when reset is triggered,
	 *
	 * Before the reset commences, the I915_RESET_IN_PROGRESS bit is set
	 * meaning that any waiters holding onto the struct_mutex should
	 * relinquish the lock immediately in order for the reset to start.
	 *
	 * If reset is not completed succesfully, the I915_WEDGE bit is
	 * set meaning that hardware is terminally sour and there is no
	 * recovery. All waiters on the reset_queue will be woken when
	 * that happens.
	 *
	 * This counter is used by the wait_seqno code to notice that reset
	 * event happened and it needs to restart the entire ioctl (since most
	 * likely the seqno it waited for won't ever signal anytime soon).
	 *
	 * This is important for lock-free wait paths, where no contended lock
	 * naturally enforces the correct ordering between the bail-out of the
	 * waiter and the gpu reset work code.
	 */
	unsigned long reset_count;

	unsigned long flags;
#define I915_RESET_IN_PROGRESS	0
#define I915_WEDGED		(BITS_PER_LONG - 1)

	/**
	 * Waitqueue to signal when a hang is detected. Used to for waiters
	 * to release the struct_mutex for the reset to procede.
	 */
	wait_queue_head_t wait_queue;

	/**
	 * Waitqueue to signal when the reset has completed. Used by clients
	 * that wait for dev_priv->mm.wedged to settle.
	 */
	wait_queue_head_t reset_queue;

	/* For missed irq/seqno simulation. */
	unsigned long test_irq_rings;
};

enum modeset_restore {
	MODESET_ON_LID_OPEN,
	MODESET_DONE,
	MODESET_SUSPENDED,
};

#define DP_AUX_A 0x40
#define DP_AUX_B 0x10
#define DP_AUX_C 0x20
#define DP_AUX_D 0x30

#define DDC_PIN_B  0x05
#define DDC_PIN_C  0x04
#define DDC_PIN_D  0x06

struct ddi_vbt_port_info {
	/*
	 * This is an index in the HDMI/DVI DDI buffer translation table.
	 * The special value HDMI_LEVEL_SHIFT_UNKNOWN means the VBT didn't
	 * populate this field.
	 */
#define HDMI_LEVEL_SHIFT_UNKNOWN	0xff
	uint8_t hdmi_level_shift;

	uint8_t supports_dvi:1;
	uint8_t supports_hdmi:1;
	uint8_t supports_dp:1;

	uint8_t alternate_aux_channel;
	uint8_t alternate_ddc_pin;

	uint8_t dp_boost_level;
	uint8_t hdmi_boost_level;
};

enum psr_lines_to_wait {
	PSR_0_LINES_TO_WAIT = 0,
	PSR_1_LINE_TO_WAIT,
	PSR_4_LINES_TO_WAIT,
	PSR_8_LINES_TO_WAIT
};

struct intel_vbt_data {
	struct drm_display_mode *lfp_lvds_vbt_mode; /* if any */
	struct drm_display_mode *sdvo_lvds_vbt_mode; /* if any */

	/* Feature bits */
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int lvds_vbt:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	unsigned int display_clock_mode:1;
	unsigned int fdi_rx_polarity_inverted:1;
	unsigned int panel_type:4;
	int lvds_ssc_freq;
	unsigned int bios_lvds_val; /* initial [PCH_]LVDS reg val in VBIOS */

	enum drrs_support_type drrs_type;

	struct {
		int rate;
		int lanes;
		int preemphasis;
		int vswing;
		bool low_vswing;
		bool initialized;
		bool support;
		int bpp;
		struct edp_power_seq pps;
	} edp;

	struct {
		bool full_link;
		bool require_aux_wakeup;
		int idle_frames;
		enum psr_lines_to_wait lines_to_wait;
		int tp1_wakeup_time;
		int tp2_tp3_wakeup_time;
	} psr;

	struct {
		u16 pwm_freq_hz;
		bool present;
		bool active_low_pwm;
		u8 min_brightness;	/* min_brightness/255 of max */
		enum intel_backlight_type type;
	} backlight;

	/* MIPI DSI */
	struct {
		u16 panel_id;
		struct mipi_config *config;
		struct mipi_pps_data *pps;
		u8 seq_version;
		u32 size;
		u8 *data;
		const u8 *sequence[MIPI_SEQ_MAX];
	} dsi;

	int crt_ddc_pin;

	int child_dev_num;
	union child_device_config *child_dev;

	struct ddi_vbt_port_info ddi_port_info[I915_MAX_PORTS];
	struct sdvo_device_mapping sdvo_mappings[2];
};

enum intel_ddb_partitioning {
	INTEL_DDB_PART_1_2,
	INTEL_DDB_PART_5_6, /* IVB+ */
};

struct intel_wm_level {
	bool enable;
	uint32_t pri_val;
	uint32_t spr_val;
	uint32_t cur_val;
	uint32_t fbc_val;
};

struct ilk_wm_values {
	uint32_t wm_pipe[3];
	uint32_t wm_lp[3];
	uint32_t wm_lp_spr[3];
	uint32_t wm_linetime[3];
	bool enable_fbc_wm;
	enum intel_ddb_partitioning partitioning;
};

struct vlv_pipe_wm {
	uint16_t primary;
	uint16_t sprite[2];
	uint8_t cursor;
};

struct vlv_sr_wm {
	uint16_t plane;
	uint8_t cursor;
};

struct vlv_wm_values {
	struct vlv_pipe_wm pipe[3];
	struct vlv_sr_wm sr;
	struct {
		uint8_t cursor;
		uint8_t sprite[2];
		uint8_t primary;
	} ddl[3];
	uint8_t level;
	bool cxsr;
};

struct skl_ddb_entry {
	uint16_t start, end;	/* in number of blocks, 'end' is exclusive */
};

static inline uint16_t skl_ddb_entry_size(const struct skl_ddb_entry *entry)
{
	return entry->end - entry->start;
}

static inline bool skl_ddb_entry_equal(const struct skl_ddb_entry *e1,
				       const struct skl_ddb_entry *e2)
{
	if (e1->start == e2->start && e1->end == e2->end)
		return true;

	return false;
}

struct skl_ddb_allocation {
	struct skl_ddb_entry plane[I915_MAX_PIPES][I915_MAX_PLANES]; /* packed/uv */
	struct skl_ddb_entry y_plane[I915_MAX_PIPES][I915_MAX_PLANES];
};

struct skl_wm_values {
	unsigned dirty_pipes;
	struct skl_ddb_allocation ddb;
};

struct skl_wm_level {
	bool plane_en;
	uint16_t plane_res_b;
	uint8_t plane_res_l;
};

/*
 * This struct helps tracking the state needed for runtime PM, which puts the
 * device in PCI D3 state. Notice that when this happens, nothing on the
 * graphics device works, even register access, so we don't get interrupts nor
 * anything else.
 *
 * Every piece of our code that needs to actually touch the hardware needs to
 * either call intel_runtime_pm_get or call intel_display_power_get with the
 * appropriate power domain.
 *
 * Our driver uses the autosuspend delay feature, which means we'll only really
 * suspend if we stay with zero refcount for a certain amount of time. The
 * default value is currently very conservative (see intel_runtime_pm_enable), but
 * it can be changed with the standard runtime PM files from sysfs.
 *
 * The irqs_disabled variable becomes true exactly after we disable the IRQs and
 * goes back to false exactly before we reenable the IRQs. We use this variable
 * to check if someone is trying to enable/disable IRQs while they're supposed
 * to be disabled. This shouldn't happen and we'll print some error messages in
 * case it happens.
 *
 * For more, read the Documentation/power/runtime_pm.txt.
 */
struct i915_runtime_pm {
	atomic_t wakeref_count;
	bool suspended;
	bool irqs_enabled;
};

enum intel_pipe_crc_source {
	INTEL_PIPE_CRC_SOURCE_NONE,
	INTEL_PIPE_CRC_SOURCE_PLANE1,
	INTEL_PIPE_CRC_SOURCE_PLANE2,
	INTEL_PIPE_CRC_SOURCE_PF,
	INTEL_PIPE_CRC_SOURCE_PIPE,
	/* TV/DP on pre-gen5/vlv can't use the pipe source. */
	INTEL_PIPE_CRC_SOURCE_TV,
	INTEL_PIPE_CRC_SOURCE_DP_B,
	INTEL_PIPE_CRC_SOURCE_DP_C,
	INTEL_PIPE_CRC_SOURCE_DP_D,
	INTEL_PIPE_CRC_SOURCE_AUTO,
	INTEL_PIPE_CRC_SOURCE_MAX,
};

struct intel_pipe_crc_entry {
	uint32_t frame;
	uint32_t crc[5];
};

#define INTEL_PIPE_CRC_ENTRIES_NR	128
struct intel_pipe_crc {
	spinlock_t lock;
	bool opened;		/* exclusive access to the result file */
	struct intel_pipe_crc_entry *entries;
	enum intel_pipe_crc_source source;
	int head, tail;
	wait_queue_head_t wq;
};

struct i915_frontbuffer_tracking {
	spinlock_t lock;

	/*
	 * Tracking bits for delayed frontbuffer flushing du to gpu activity or
	 * scheduled flips.
	 */
	unsigned busy_bits;
	unsigned flip_bits;
};

struct i915_wa_reg {
	i915_reg_t addr;
	u32 value;
	/* bitmask representing WA bits */
	u32 mask;
};

/*
 * RING_MAX_NONPRIV_SLOTS is per-engine but at this point we are only
 * allowing it for RCS as we don't foresee any requirement of having
 * a whitelist for other engines. When it is really required for
 * other engines then the limit need to be increased.
 */
#define I915_MAX_WA_REGS (16 + RING_MAX_NONPRIV_SLOTS)

struct i915_workarounds {
	struct i915_wa_reg reg[I915_MAX_WA_REGS];
	u32 count;
	u32 hw_whitelist_count[I915_NUM_ENGINES];
};

struct i915_virtual_gpu {
	bool active;
};

/* used in computing the new watermarks state */
struct intel_wm_config {
	unsigned int num_pipes_active;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct drm_i915_private {
	struct drm_device drm;

	struct kmem_cache *objects;
	struct kmem_cache *vmas;
	struct kmem_cache *requests;

	const struct intel_device_info info;

	int relative_constants_mode;

	void __iomem *regs;

	struct intel_uncore uncore;

	struct i915_virtual_gpu vgpu;

	struct intel_gvt *gvt;

	struct intel_guc guc;

	struct intel_csr csr;

	struct intel_gmbus gmbus[GMBUS_NUM_PINS];

	/** gmbus_mutex protects against concurrent usage of the single hw gmbus
	 * controller on different i2c buses. */
	struct mutex gmbus_mutex;

	/**
	 * Base address of the gmbus and gpio block.
	 */
	uint32_t gpio_mmio_base;

	/* MMIO base address for MIPI regs */
	uint32_t mipi_mmio_base;

	uint32_t psr_mmio_base;

	uint32_t pps_mmio_base;

	wait_queue_head_t gmbus_wait_queue;

	struct pci_dev *bridge_dev;
	struct i915_gem_context *kernel_context;
	struct intel_engine_cs *engine[I915_NUM_ENGINES];
	struct i915_vma *semaphore;
	u32 next_seqno;

	struct drm_dma_handle *status_page_dmah;
	struct resource mch_res;

	/* protects the irq masks */
	spinlock_t irq_lock;

	/* protects the mmio flip data */
	spinlock_t mmio_flip_lock;

	bool display_irqs_enabled;

	/* To control wakeup latency, e.g. for irq-driven dp aux transfers. */
	struct pm_qos_request pm_qos;

	/* Sideband mailbox protection */
	struct mutex sb_lock;

	/** Cached value of IMR to avoid reads in updating the bitfield */
	union {
		u32 irq_mask;
		u32 de_irq_mask[I915_MAX_PIPES];
	};
	u32 gt_irq_mask;
	u32 pm_imr;
	u32 pm_ier;
	u32 pm_rps_events;
	u32 pm_guc_events;
	u32 pipestat_irq_mask[I915_MAX_PIPES];

	struct i915_hotplug hotplug;
	struct intel_fbc fbc;
	struct i915_drrs drrs;
	struct intel_opregion opregion;
	struct intel_vbt_data vbt;

	bool preserve_bios_swizzle;

	/* overlay */
	struct intel_overlay *overlay;

	/* backlight registers and fields in struct intel_panel */
	struct mutex backlight_lock;

	/* LVDS info */
	bool no_aux_handshake;

	/* protects panel power sequencer state */
	struct mutex pps_mutex;

	struct drm_i915_fence_reg fence_regs[I915_MAX_NUM_FENCES]; /* assume 965 */
	int num_fence_regs; /* 8 on pre-965, 16 otherwise */

	unsigned int fsb_freq, mem_freq, is_ddr3;
	unsigned int skl_preferred_vco_freq;
	unsigned int cdclk_freq, max_cdclk_freq, atomic_cdclk_freq;
	unsigned int max_dotclk_freq;
	unsigned int rawclk_freq;
	unsigned int hpll_freq;
	unsigned int czclk_freq;

	struct {
		unsigned int vco, ref;
	} cdclk_pll;

	/**
	 * wq - Driver workqueue for GEM.
	 *
	 * NOTE: Work items scheduled here are not allowed to grab any modeset
	 * locks, for otherwise the flushing done in the pageflip code will
	 * result in deadlocks.
	 */
	struct workqueue_struct *wq;

	/* Display functions */
	struct drm_i915_display_funcs display;

	/* PCH chipset type */
	enum intel_pch pch_type;
	unsigned short pch_id;

	unsigned long quirks;

	enum modeset_restore modeset_restore;
	struct mutex modeset_restore_lock;
	struct drm_atomic_state *modeset_restore_state;
	struct drm_modeset_acquire_ctx reset_ctx;

	struct list_head vm_list; /* Global list of all address spaces */
	struct i915_ggtt ggtt; /* VM representing the global address space */

	struct i915_gem_mm mm;
	DECLARE_HASHTABLE(mm_structs, 7);
	struct mutex mm_lock;

	/* The hw wants to have a stable context identifier for the lifetime
	 * of the context (for OA, PASID, faults, etc). This is limited
	 * in execlists to 21 bits.
	 */
	struct ida context_hw_ida;
#define MAX_CONTEXT_HW_ID (1<<21) /* exclusive */

	/* Kernel Modesetting */

	struct drm_crtc *plane_to_crtc_mapping[I915_MAX_PIPES];
	struct drm_crtc *pipe_to_crtc_mapping[I915_MAX_PIPES];
	wait_queue_head_t pending_flip_queue;

#ifdef CONFIG_DEBUG_FS
	struct intel_pipe_crc pipe_crc[I915_MAX_PIPES];
#endif

	/* dpll and cdclk state is protected by connection_mutex */
	int num_shared_dpll;
	struct intel_shared_dpll shared_dplls[I915_NUM_PLLS];
	const struct intel_dpll_mgr *dpll_mgr;

	/*
	 * dpll_lock serializes intel_{prepare,enable,disable}_shared_dpll.
	 * Must be global rather than per dpll, because on some platforms
	 * plls share registers.
	 */
	struct mutex dpll_lock;

	unsigned int active_crtcs;
	unsigned int min_pixclk[I915_MAX_PIPES];

	int dpio_phy_iosf_port[I915_NUM_PHYS_VLV];

	struct i915_workarounds workarounds;

	struct i915_frontbuffer_tracking fb_tracking;

	u16 orig_clock;

	bool mchbar_need_disable;

	struct intel_l3_parity l3_parity;

	/* Cannot be determined by PCIID. You must always read a register. */
	u32 edram_cap;

	/* gen6+ rps state */
	struct intel_gen6_power_mgmt rps;

	/* ilk-only ips/rps state. Everything in here is protected by the global
	 * mchdev_lock in intel_pm.c */
	struct intel_ilk_power_mgmt ips;

	struct i915_power_domains power_domains;

	struct i915_psr psr;

	struct i915_gpu_error gpu_error;

	struct drm_i915_gem_object *vlv_pctx;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	/* list of fbdev register on this device */
	struct intel_fbdev *fbdev;
	struct work_struct fbdev_suspend_work;
#endif

	struct drm_property *broadcast_rgb_property;
	struct drm_property *force_audio_property;

	/* hda/i915 audio component */
	struct i915_audio_component *audio_component;
	bool audio_component_registered;
	/**
	 * av_mutex - mutex for audio/video sync
	 *
	 */
	struct mutex av_mutex;

	uint32_t hw_context_size;
	struct list_head context_list;

	u32 fdi_rx_config;

	/* Shadow for DISPLAY_PHY_CONTROL which can't be safely read */
	u32 chv_phy_control;
	/*
	 * Shadows for CHV DPLL_MD regs to keep the state
	 * checker somewhat working in the presence hardware
	 * crappiness (can't read out DPLL_MD for pipes B & C).
	 */
	u32 chv_dpll_md[I915_MAX_PIPES];
	u32 bxt_phy_grc;

	u32 suspend_count;
	bool suspended_to_idle;
	struct i915_suspend_saved_registers regfile;
	struct vlv_s0ix_state vlv_s0ix_state;

	enum {
		I915_SAGV_UNKNOWN = 0,
		I915_SAGV_DISABLED,
		I915_SAGV_ENABLED,
		I915_SAGV_NOT_CONTROLLED
	} sagv_status;

	struct {
		/*
		 * Raw watermark latency values:
		 * in 0.1us units for WM0,
		 * in 0.5us units for WM1+.
		 */
		/* primary */
		uint16_t pri_latency[5];
		/* sprite */
		uint16_t spr_latency[5];
		/* cursor */
		uint16_t cur_latency[5];
		/*
		 * Raw watermark memory latency values
		 * for SKL for all 8 levels
		 * in 1us units.
		 */
		uint16_t skl_latency[8];

		/*
		 * The skl_wm_values structure is a bit too big for stack
		 * allocation, so we keep the staging struct where we store
		 * intermediate results here instead.
		 */
		struct skl_wm_values skl_results;

		/* current hardware state */
		union {
			struct ilk_wm_values hw;
			struct skl_wm_values skl_hw;
			struct vlv_wm_values vlv;
		};

		uint8_t max_level;

		/*
		 * Should be held around atomic WM register writing; also
		 * protects * intel_crtc->wm.active and
		 * cstate->wm.need_postvbl_update.
		 */
		struct mutex wm_mutex;

		/*
		 * Set during HW readout of watermarks/DDB.  Some platforms
		 * need to know when we're still using BIOS-provided values
		 * (which we don't fully trust).
		 */
		bool distrust_bios_wm;
	} wm;

	struct i915_runtime_pm pm;

	/* Abstract the submission mechanism (legacy ringbuffer or execlists) away */
	struct {
		void (*resume)(struct drm_i915_private *);
		void (*cleanup_engine)(struct intel_engine_cs *engine);

		/**
		 * Is the GPU currently considered idle, or busy executing
		 * userspace requests? Whilst idle, we allow runtime power
		 * management to power down the hardware and display clocks.
		 * In order to reduce the effect on performance, there
		 * is a slight delay before we do so.
		 */
		unsigned int active_engines;
		bool awake;

		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;

		/**
		 * When we detect an idle GPU, we want to turn on
		 * powersaving features. So once we see that there
		 * are no more requests outstanding and no more
		 * arrive within a small period of time, we fire
		 * off the idle_work.
		 */
		struct delayed_work idle_work;
	} gt;

	/* perform PHY state sanity checks? */
	bool chv_phy_assert[2];

	/* Used to save the pipe-to-encoder mapping for audio */
	struct intel_encoder *av_enc_map[I915_MAX_PIPES];

	/*
	 * NOTE: This is the dri1/ums dungeon, don't add stuff here. Your patch
	 * will be rejected. Instead look for a better place.
	 */
};

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

static inline struct drm_i915_private *kdev_to_i915(struct device *kdev)
{
	return to_i915(dev_get_drvdata(kdev));
}

static inline struct drm_i915_private *guc_to_i915(struct intel_guc *guc)
{
	return container_of(guc, struct drm_i915_private, guc);
}

/* Simple iterator over all initialised engines */
#define for_each_engine(engine__, dev_priv__, id__) \
	for ((id__) = 0; \
	     (id__) < I915_NUM_ENGINES; \
	     (id__)++) \
		for_each_if ((engine__) = (dev_priv__)->engine[(id__)])

#define __mask_next_bit(mask) ({					\
	int __idx = ffs(mask) - 1;					\
	mask &= ~BIT(__idx);						\
	__idx;								\
})

/* Iterator over subset of engines selected by mask */
#define for_each_engine_masked(engine__, dev_priv__, mask__, tmp__) \
	for (tmp__ = mask__ & INTEL_INFO(dev_priv__)->ring_mask;	\
	     tmp__ ? (engine__ = (dev_priv__)->engine[__mask_next_bit(tmp__)]), 1 : 0; )

enum hdmi_force_audio {
	HDMI_AUDIO_OFF_DVI = -2,	/* no aux data for HDMI-DVI converter */
	HDMI_AUDIO_OFF,			/* force turn off HDMI audio */
	HDMI_AUDIO_AUTO,		/* trust EDID */
	HDMI_AUDIO_ON,			/* force turn on HDMI audio */
};

#define I915_GTT_OFFSET_NONE ((u32)-1)

struct drm_i915_gem_object_ops {
	unsigned int flags;
#define I915_GEM_OBJECT_HAS_STRUCT_PAGE 0x1

	/* Interface between the GEM object and its backing storage.
	 * get_pages() is called once prior to the use of the associated set
	 * of pages before to binding them into the GTT, and put_pages() is
	 * called after we no longer need them. As we expect there to be
	 * associated cost with migrating pages between the backing storage
	 * and making them available for the GPU (e.g. clflush), we may hold
	 * onto the pages after they are no longer referenced by the GPU
	 * in case they may be used again shortly (for example migrating the
	 * pages to a different memory domain within the GTT). put_pages()
	 * will therefore most likely be called when the object itself is
	 * being released or under memory pressure (where we attempt to
	 * reap pages for the shrinker).
	 */
	int (*get_pages)(struct drm_i915_gem_object *);
	void (*put_pages)(struct drm_i915_gem_object *);

	int (*dmabuf_export)(struct drm_i915_gem_object *);
	void (*release)(struct drm_i915_gem_object *);
};

/*
 * Frontbuffer tracking bits. Set in obj->frontbuffer_bits while a gem bo is
 * considered to be the frontbuffer for the given plane interface-wise. This
 * doesn't mean that the hw necessarily already scans it out, but that any
 * rendering (by the cpu or gpu) will land in the frontbuffer eventually.
 *
 * We have one bit per pipe and per scanout plane type.
 */
#define INTEL_MAX_SPRITE_BITS_PER_PIPE 5
#define INTEL_FRONTBUFFER_BITS_PER_PIPE 8
#define INTEL_FRONTBUFFER_PRIMARY(pipe) \
	(1 << (INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe)))
#define INTEL_FRONTBUFFER_CURSOR(pipe) \
	(1 << (1 + (INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))))
#define INTEL_FRONTBUFFER_SPRITE(pipe, plane) \
	(1 << (2 + plane + (INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))))
#define INTEL_FRONTBUFFER_OVERLAY(pipe) \
	(1 << (2 + INTEL_MAX_SPRITE_BITS_PER_PIPE + (INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))))
#define INTEL_FRONTBUFFER_ALL_MASK(pipe) \
	(0xff << (INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe)))

struct drm_i915_gem_object {
	struct drm_gem_object base;

	const struct drm_i915_gem_object_ops *ops;

	/** List of VMAs backed by this object */
	struct list_head vma_list;

	/** Stolen memory for this object, instead of being backed by shmem. */
	struct drm_mm_node *stolen;
	struct list_head global_list;

	/**
	 * Whether the object is currently in the GGTT mmap.
	 */
	struct list_head userfault_link;

	/** Used in execbuf to temporarily hold a ref */
	struct list_head obj_exec_link;

	struct list_head batch_pool_link;

	unsigned long flags;
	/**
	 * This is set if the object is on the active lists (has pending
	 * rendering and so a non-zero seqno), and is not set if it i s on
	 * inactive (ready to be unbound) list.
	 */
#define I915_BO_ACTIVE_SHIFT 0
#define I915_BO_ACTIVE_MASK ((1 << I915_NUM_ENGINES) - 1)
#define __I915_BO_ACTIVE(bo) \
	((READ_ONCE((bo)->flags) >> I915_BO_ACTIVE_SHIFT) & I915_BO_ACTIVE_MASK)

	/**
	 * This is set if the object has been written to since last bound
	 * to the GTT
	 */
	unsigned int dirty:1;

	/**
	 * Advice: are the backing pages purgeable?
	 */
	unsigned int madv:2;

	/*
	 * Is the object to be mapped as read-only to the GPU
	 * Only honoured if hardware has relevant pte bit
	 */
	unsigned long gt_ro:1;
	unsigned int cache_level:3;
	unsigned int cache_dirty:1;

	atomic_t frontbuffer_bits;
	unsigned int frontbuffer_ggtt_origin; /* write once */

	/** Current tiling stride for the object, if it's tiled. */
	unsigned int tiling_and_stride;
#define FENCE_MINIMUM_STRIDE 128 /* See i915_tiling_ok() */
#define TILING_MASK (FENCE_MINIMUM_STRIDE-1)
#define STRIDE_MASK (~TILING_MASK)

	/** Count of VMA actually bound by this object */
	unsigned int bind_count;
	unsigned int pin_display;

	struct sg_table *pages;
	int pages_pin_count;
	struct get_page {
		struct scatterlist *sg;
		int last;
	} get_page;
	void *mapping;

	/** Breadcrumb of last rendering to the buffer.
	 * There can only be one writer, but we allow for multiple readers.
	 * If there is a writer that necessarily implies that all other
	 * read requests are complete - but we may only be lazily clearing
	 * the read requests. A read request is naturally the most recent
	 * request on a ring, so we may have two different write and read
	 * requests on one ring where the write request is older than the
	 * read request. This allows for the CPU to read from an active
	 * buffer by only waiting for the write to complete.
	 */
	struct i915_gem_active last_read[I915_NUM_ENGINES];
	struct i915_gem_active last_write;

	/** References from framebuffers, locks out tiling changes. */
	unsigned long framebuffer_references;

	/** Record of address bit 17 of each page at last unbind. */
	unsigned long *bit_17;

	struct i915_gem_userptr {
		uintptr_t ptr;
		unsigned read_only :1;
		unsigned workers :4;
#define I915_GEM_USERPTR_MAX_WORKERS 15

		struct i915_mm_struct *mm;
		struct i915_mmu_object *mmu_object;
		struct work_struct *work;
	} userptr;

	/** for phys allocated objects */
	struct drm_dma_handle *phys_handle;
};

static inline struct drm_i915_gem_object *
to_intel_bo(struct drm_gem_object *gem)
{
	/* Assert that to_intel_bo(NULL) == NULL */
	BUILD_BUG_ON(offsetof(struct drm_i915_gem_object, base));

	return container_of(gem, struct drm_i915_gem_object, base);
}

static inline struct drm_i915_gem_object *
i915_gem_object_lookup(struct drm_file *file, u32 handle)
{
	return to_intel_bo(drm_gem_object_lookup(file, handle));
}

__deprecated
extern struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, u32 handle);

__attribute__((nonnull))
static inline struct drm_i915_gem_object *
i915_gem_object_get(struct drm_i915_gem_object *obj)
{
	drm_gem_object_reference(&obj->base);
	return obj;
}

__deprecated
extern void drm_gem_object_reference(struct drm_gem_object *);

__attribute__((nonnull))
static inline void
i915_gem_object_put(struct drm_i915_gem_object *obj)
{
	drm_gem_object_unreference(&obj->base);
}

__deprecated
extern void drm_gem_object_unreference(struct drm_gem_object *);

__attribute__((nonnull))
static inline void
i915_gem_object_put_unlocked(struct drm_i915_gem_object *obj)
{
	drm_gem_object_unreference_unlocked(&obj->base);
}

__deprecated
extern void drm_gem_object_unreference_unlocked(struct drm_gem_object *);

static inline bool
i915_gem_object_has_struct_page(const struct drm_i915_gem_object *obj)
{
	return obj->ops->flags & I915_GEM_OBJECT_HAS_STRUCT_PAGE;
}

static inline unsigned long
i915_gem_object_get_active(const struct drm_i915_gem_object *obj)
{
	return (obj->flags >> I915_BO_ACTIVE_SHIFT) & I915_BO_ACTIVE_MASK;
}

static inline bool
i915_gem_object_is_active(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_get_active(obj);
}

static inline void
i915_gem_object_set_active(struct drm_i915_gem_object *obj, int engine)
{
	obj->flags |= BIT(engine + I915_BO_ACTIVE_SHIFT);
}

static inline void
i915_gem_object_clear_active(struct drm_i915_gem_object *obj, int engine)
{
	obj->flags &= ~BIT(engine + I915_BO_ACTIVE_SHIFT);
}

static inline bool
i915_gem_object_has_active_engine(const struct drm_i915_gem_object *obj,
				  int engine)
{
	return obj->flags & BIT(engine + I915_BO_ACTIVE_SHIFT);
}

static inline unsigned int
i915_gem_object_get_tiling(struct drm_i915_gem_object *obj)
{
	return obj->tiling_and_stride & TILING_MASK;
}

static inline bool
i915_gem_object_is_tiled(struct drm_i915_gem_object *obj)
{
	return i915_gem_object_get_tiling(obj) != I915_TILING_NONE;
}

static inline unsigned int
i915_gem_object_get_stride(struct drm_i915_gem_object *obj)
{
	return obj->tiling_and_stride & STRIDE_MASK;
}

static inline struct i915_vma *i915_vma_get(struct i915_vma *vma)
{
	i915_gem_object_get(vma->obj);
	return vma;
}

static inline void i915_vma_put(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->dev->struct_mutex);
	i915_gem_object_put(vma->obj);
}

/*
 * Optimised SGL iterator for GEM objects
 */
static __always_inline struct sgt_iter {
	struct scatterlist *sgp;
	union {
		unsigned long pfn;
		dma_addr_t dma;
	};
	unsigned int curr;
	unsigned int max;
} __sgt_iter(struct scatterlist *sgl, bool dma) {
	struct sgt_iter s = { .sgp = sgl };

	if (s.sgp) {
		s.max = s.curr = s.sgp->offset;
		s.max += s.sgp->length;
		if (dma)
			s.dma = sg_dma_address(s.sgp);
		else
			s.pfn = page_to_pfn(sg_page(s.sgp));
	}

	return s;
}

/**
 * __sg_next - return the next scatterlist entry in a list
 * @sg:		The current sg entry
 *
 * Description:
 *   If the entry is the last, return NULL; otherwise, step to the next
 *   element in the array (@sg@+1). If that's a chain pointer, follow it;
 *   otherwise just return the pointer to the current element.
 **/
static inline struct scatterlist *__sg_next(struct scatterlist *sg)
{
#ifdef CONFIG_DEBUG_SG
	BUG_ON(sg->sg_magic != SG_MAGIC);
#endif
	return sg_is_last(sg) ? NULL :
		likely(!sg_is_chain(++sg)) ? sg :
		sg_chain_ptr(sg);
}

/**
 * for_each_sgt_dma - iterate over the DMA addresses of the given sg_table
 * @__dmap:	DMA address (output)
 * @__iter:	'struct sgt_iter' (iterator state, internal)
 * @__sgt:	sg_table to iterate over (input)
 */
#define for_each_sgt_dma(__dmap, __iter, __sgt)				\
	for ((__iter) = __sgt_iter((__sgt)->sgl, true);			\
	     ((__dmap) = (__iter).dma + (__iter).curr);			\
	     (((__iter).curr += PAGE_SIZE) < (__iter).max) ||		\
	     ((__iter) = __sgt_iter(__sg_next((__iter).sgp), true), 0))

/**
 * for_each_sgt_page - iterate over the pages of the given sg_table
 * @__pp:	page pointer (output)
 * @__iter:	'struct sgt_iter' (iterator state, internal)
 * @__sgt:	sg_table to iterate over (input)
 */
#define for_each_sgt_page(__pp, __iter, __sgt)				\
	for ((__iter) = __sgt_iter((__sgt)->sgl, false);		\
	     ((__pp) = (__iter).pfn == 0 ? NULL :			\
	      pfn_to_page((__iter).pfn + ((__iter).curr >> PAGE_SHIFT))); \
	     (((__iter).curr += PAGE_SIZE) < (__iter).max) ||		\
	     ((__iter) = __sgt_iter(__sg_next((__iter).sgp), false), 0))

/*
 * A command that requires special handling by the command parser.
 */
struct drm_i915_cmd_descriptor {
	/*
	 * Flags describing how the command parser processes the command.
	 *
	 * CMD_DESC_FIXED: The command has a fixed length if this is set,
	 *                 a length mask if not set
	 * CMD_DESC_SKIP: The command is allowed but does not follow the
	 *                standard length encoding for the opcode range in
	 *                which it falls
	 * CMD_DESC_REJECT: The command is never allowed
	 * CMD_DESC_REGISTER: The command should be checked against the
	 *                    register whitelist for the appropriate ring
	 * CMD_DESC_MASTER: The command is allowed if the submitting process
	 *                  is the DRM master
	 */
	u32 flags;
#define CMD_DESC_FIXED    (1<<0)
#define CMD_DESC_SKIP     (1<<1)
#define CMD_DESC_REJECT   (1<<2)
#define CMD_DESC_REGISTER (1<<3)
#define CMD_DESC_BITMASK  (1<<4)
#define CMD_DESC_MASTER   (1<<5)

	/*
	 * The command's unique identification bits and the bitmask to get them.
	 * This isn't strictly the opcode field as defined in the spec and may
	 * also include type, subtype, and/or subop fields.
	 */
	struct {
		u32 value;
		u32 mask;
	} cmd;

	/*
	 * The command's length. The command is either fixed length (i.e. does
	 * not include a length field) or has a length field mask. The flag
	 * CMD_DESC_FIXED indicates a fixed length. Otherwise, the command has
	 * a length mask. All command entries in a command table must include
	 * length information.
	 */
	union {
		u32 fixed;
		u32 mask;
	} length;

	/*
	 * Describes where to find a register address in the command to check
	 * against the ring's register whitelist. Only valid if flags has the
	 * CMD_DESC_REGISTER bit set.
	 *
	 * A non-zero step value implies that the command may access multiple
	 * registers in sequence (e.g. LRI), in that case step gives the
	 * distance in dwords between individual offset fields.
	 */
	struct {
		u32 offset;
		u32 mask;
		u32 step;
	} reg;

#define MAX_CMD_DESC_BITMASKS 3
	/*
	 * Describes command checks where a particular dword is masked and
	 * compared against an expected value. If the command does not match
	 * the expected value, the parser rejects it. Only valid if flags has
	 * the CMD_DESC_BITMASK bit set. Only entries where mask is non-zero
	 * are valid.
	 *
	 * If the check specifies a non-zero condition_mask then the parser
	 * only performs the check when the bits specified by condition_mask
	 * are non-zero.
	 */
	struct {
		u32 offset;
		u32 mask;
		u32 expected;
		u32 condition_offset;
		u32 condition_mask;
	} bits[MAX_CMD_DESC_BITMASKS];
};

/*
 * A table of commands requiring special handling by the command parser.
 *
 * Each engine has an array of tables. Each table consists of an array of
 * command descriptors, which must be sorted with command opcodes in
 * ascending order.
 */
struct drm_i915_cmd_table {
	const struct drm_i915_cmd_descriptor *table;
	int count;
};

/* Note that the (struct drm_i915_private *) cast is just to shut up gcc. */
#define __I915__(p) ({ \
	struct drm_i915_private *__p; \
	if (__builtin_types_compatible_p(typeof(*p), struct drm_i915_private)) \
		__p = (struct drm_i915_private *)p; \
	else if (__builtin_types_compatible_p(typeof(*p), struct drm_device)) \
		__p = to_i915((struct drm_device *)p); \
	else \
		BUILD_BUG(); \
	__p; \
})
#define INTEL_INFO(p)	(&__I915__(p)->info)

#define INTEL_GEN(dev_priv)	((dev_priv)->info.gen)
#define INTEL_DEVID(dev_priv)	((dev_priv)->info.device_id)

#define REVID_FOREVER		0xff
#define INTEL_REVID(p)	(__I915__(p)->drm.pdev->revision)

#define GEN_FOREVER (0)
/*
 * Returns true if Gen is in inclusive range [Start, End].
 *
 * Use GEN_FOREVER for unbound start and or end.
 */
#define IS_GEN(dev_priv, s, e) ({ \
	unsigned int __s = (s), __e = (e); \
	BUILD_BUG_ON(!__builtin_constant_p(s)); \
	BUILD_BUG_ON(!__builtin_constant_p(e)); \
	if ((__s) != GEN_FOREVER) \
		__s = (s) - 1; \
	if ((__e) == GEN_FOREVER) \
		__e = BITS_PER_LONG - 1; \
	else \
		__e = (e) - 1; \
	!!((dev_priv)->info.gen_mask & GENMASK((__e), (__s))); \
})

/*
 * Return true if revision is in range [since,until] inclusive.
 *
 * Use 0 for open-ended since, and REVID_FOREVER for open-ended until.
 */
#define IS_REVID(p, since, until) \
	(INTEL_REVID(p) >= (since) && INTEL_REVID(p) <= (until))

#define IS_I830(dev_priv)	(INTEL_DEVID(dev_priv) == 0x3577)
#define IS_845G(dev_priv)	(INTEL_DEVID(dev_priv) == 0x2562)
#define IS_I85X(dev)		(INTEL_INFO(dev)->is_i85x)
#define IS_I865G(dev_priv)	(INTEL_DEVID(dev_priv) == 0x2572)
#define IS_I915G(dev)		(INTEL_INFO(dev)->is_i915g)
#define IS_I915GM(dev_priv)	(INTEL_DEVID(dev_priv) == 0x2592)
#define IS_I945G(dev_priv)	(INTEL_DEVID(dev_priv) == 0x2772)
#define IS_I945GM(dev)		(INTEL_INFO(dev)->is_i945gm)
#define IS_BROADWATER(dev)	(INTEL_INFO(dev)->is_broadwater)
#define IS_CRESTLINE(dev)	(INTEL_INFO(dev)->is_crestline)
#define IS_GM45(dev_priv)	(INTEL_DEVID(dev_priv) == 0x2A42)
#define IS_G4X(dev_priv)	((dev_priv)->info.is_g4x)
#define IS_PINEVIEW_G(dev_priv)	(INTEL_DEVID(dev_priv) == 0xa001)
#define IS_PINEVIEW_M(dev_priv)	(INTEL_DEVID(dev_priv) == 0xa011)
#define IS_PINEVIEW(dev)	(INTEL_INFO(dev)->is_pineview)
#define IS_G33(dev)		(INTEL_INFO(dev)->is_g33)
#define IS_IRONLAKE_M(dev_priv)	(INTEL_DEVID(dev_priv) == 0x0046)
#define IS_IVYBRIDGE(dev_priv)	((dev_priv)->info.is_ivybridge)
#define IS_IVB_GT1(dev_priv)	(INTEL_DEVID(dev_priv) == 0x0156 || \
				 INTEL_DEVID(dev_priv) == 0x0152 || \
				 INTEL_DEVID(dev_priv) == 0x015a)
#define IS_VALLEYVIEW(dev_priv)	((dev_priv)->info.is_valleyview)
#define IS_CHERRYVIEW(dev_priv)	((dev_priv)->info.is_cherryview)
#define IS_HASWELL(dev_priv)	((dev_priv)->info.is_haswell)
#define IS_BROADWELL(dev_priv)	((dev_priv)->info.is_broadwell)
#define IS_SKYLAKE(dev_priv)	((dev_priv)->info.is_skylake)
#define IS_BROXTON(dev_priv)	((dev_priv)->info.is_broxton)
#define IS_KABYLAKE(dev_priv)	((dev_priv)->info.is_kabylake)
#define IS_MOBILE(dev)		(INTEL_INFO(dev)->is_mobile)
#define IS_HSW_EARLY_SDV(dev_priv) (IS_HASWELL(dev_priv) && \
				    (INTEL_DEVID(dev_priv) & 0xFF00) == 0x0C00)
#define IS_BDW_ULT(dev_priv)	(IS_BROADWELL(dev_priv) && \
				 ((INTEL_DEVID(dev_priv) & 0xf) == 0x6 ||	\
				 (INTEL_DEVID(dev_priv) & 0xf) == 0xb ||	\
				 (INTEL_DEVID(dev_priv) & 0xf) == 0xe))
/* ULX machines are also considered ULT. */
#define IS_BDW_ULX(dev_priv)	(IS_BROADWELL(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0xf) == 0xe)
#define IS_BDW_GT3(dev_priv)	(IS_BROADWELL(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0x00F0) == 0x0020)
#define IS_HSW_ULT(dev_priv)	(IS_HASWELL(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0xFF00) == 0x0A00)
#define IS_HSW_GT3(dev_priv)	(IS_HASWELL(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0x00F0) == 0x0020)
/* ULX machines are also considered ULT. */
#define IS_HSW_ULX(dev_priv)	(INTEL_DEVID(dev_priv) == 0x0A0E || \
				 INTEL_DEVID(dev_priv) == 0x0A1E)
#define IS_SKL_ULT(dev_priv)	(INTEL_DEVID(dev_priv) == 0x1906 || \
				 INTEL_DEVID(dev_priv) == 0x1913 || \
				 INTEL_DEVID(dev_priv) == 0x1916 || \
				 INTEL_DEVID(dev_priv) == 0x1921 || \
				 INTEL_DEVID(dev_priv) == 0x1926)
#define IS_SKL_ULX(dev_priv)	(INTEL_DEVID(dev_priv) == 0x190E || \
				 INTEL_DEVID(dev_priv) == 0x1915 || \
				 INTEL_DEVID(dev_priv) == 0x191E)
#define IS_KBL_ULT(dev_priv)	(INTEL_DEVID(dev_priv) == 0x5906 || \
				 INTEL_DEVID(dev_priv) == 0x5913 || \
				 INTEL_DEVID(dev_priv) == 0x5916 || \
				 INTEL_DEVID(dev_priv) == 0x5921 || \
				 INTEL_DEVID(dev_priv) == 0x5926)
#define IS_KBL_ULX(dev_priv)	(INTEL_DEVID(dev_priv) == 0x590E || \
				 INTEL_DEVID(dev_priv) == 0x5915 || \
				 INTEL_DEVID(dev_priv) == 0x591E)
#define IS_SKL_GT3(dev_priv)	(IS_SKYLAKE(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0x00F0) == 0x0020)
#define IS_SKL_GT4(dev_priv)	(IS_SKYLAKE(dev_priv) && \
				 (INTEL_DEVID(dev_priv) & 0x00F0) == 0x0030)

#define IS_PRELIMINARY_HW(intel_info) ((intel_info)->is_preliminary)

#define SKL_REVID_A0		0x0
#define SKL_REVID_B0		0x1
#define SKL_REVID_C0		0x2
#define SKL_REVID_D0		0x3
#define SKL_REVID_E0		0x4
#define SKL_REVID_F0		0x5
#define SKL_REVID_G0		0x6
#define SKL_REVID_H0		0x7

#define IS_SKL_REVID(p, since, until) (IS_SKYLAKE(p) && IS_REVID(p, since, until))

#define BXT_REVID_A0		0x0
#define BXT_REVID_A1		0x1
#define BXT_REVID_B0		0x3
#define BXT_REVID_C0		0x9

#define IS_BXT_REVID(dev_priv, since, until) \
	(IS_BROXTON(dev_priv) && IS_REVID(dev_priv, since, until))

#define KBL_REVID_A0		0x0
#define KBL_REVID_B0		0x1
#define KBL_REVID_C0		0x2
#define KBL_REVID_D0		0x3
#define KBL_REVID_E0		0x4

#define IS_KBL_REVID(dev_priv, since, until) \
	(IS_KABYLAKE(dev_priv) && IS_REVID(dev_priv, since, until))

/*
 * The genX designation typically refers to the render engine, so render
 * capability related checks should use IS_GEN, while display and other checks
 * have their own (e.g. HAS_PCH_SPLIT for ILK+ display, IS_foo for particular
 * chips, etc.).
 */
#define IS_GEN2(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(1)))
#define IS_GEN3(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(2)))
#define IS_GEN4(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(3)))
#define IS_GEN5(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(4)))
#define IS_GEN6(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(5)))
#define IS_GEN7(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(6)))
#define IS_GEN8(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(7)))
#define IS_GEN9(dev_priv)	(!!((dev_priv)->info.gen_mask & BIT(8)))

#define ENGINE_MASK(id)	BIT(id)
#define RENDER_RING	ENGINE_MASK(RCS)
#define BSD_RING	ENGINE_MASK(VCS)
#define BLT_RING	ENGINE_MASK(BCS)
#define VEBOX_RING	ENGINE_MASK(VECS)
#define BSD2_RING	ENGINE_MASK(VCS2)
#define ALL_ENGINES	(~0)

#define HAS_ENGINE(dev_priv, id) \
	(!!(INTEL_INFO(dev_priv)->ring_mask & ENGINE_MASK(id)))

#define HAS_BSD(dev_priv)	HAS_ENGINE(dev_priv, VCS)
#define HAS_BSD2(dev_priv)	HAS_ENGINE(dev_priv, VCS2)
#define HAS_BLT(dev_priv)	HAS_ENGINE(dev_priv, BCS)
#define HAS_VEBOX(dev_priv)	HAS_ENGINE(dev_priv, VECS)

#define HAS_LLC(dev)		(INTEL_INFO(dev)->has_llc)
#define HAS_SNOOP(dev)		(INTEL_INFO(dev)->has_snoop)
#define HAS_EDRAM(dev)		(!!(__I915__(dev)->edram_cap & EDRAM_ENABLED))
#define HAS_WT(dev_priv)	((IS_HASWELL(dev_priv) || \
				 IS_BROADWELL(dev_priv)) && HAS_EDRAM(dev_priv))
#define HWS_NEEDS_PHYSICAL(dev)	(INTEL_INFO(dev)->hws_needs_physical)

#define HAS_HW_CONTEXTS(dev)	(INTEL_INFO(dev)->has_hw_contexts)
#define HAS_LOGICAL_RING_CONTEXTS(dev)	(INTEL_INFO(dev)->has_logical_ring_contexts)
#define USES_PPGTT(dev)		(i915.enable_ppgtt)
#define USES_FULL_PPGTT(dev)	(i915.enable_ppgtt >= 2)
#define USES_FULL_48BIT_PPGTT(dev)	(i915.enable_ppgtt == 3)

#define HAS_OVERLAY(dev)		(INTEL_INFO(dev)->has_overlay)
#define OVERLAY_NEEDS_PHYSICAL(dev)	(INTEL_INFO(dev)->overlay_needs_physical)

/* Early gen2 have a totally busted CS tlb and require pinned batches. */
#define HAS_BROKEN_CS_TLB(dev_priv)	(IS_I830(dev_priv) || IS_845G(dev_priv))

/* WaRsDisableCoarsePowerGating:skl,bxt */
#define NEEDS_WaRsDisableCoarsePowerGating(dev_priv) \
	(IS_BXT_REVID(dev_priv, 0, BXT_REVID_A1) || \
	 IS_SKL_GT3(dev_priv) || \
	 IS_SKL_GT4(dev_priv))

/*
 * dp aux and gmbus irq on gen4 seems to be able to generate legacy interrupts
 * even when in MSI mode. This results in spurious interrupt warnings if the
 * legacy irq no. is shared with another device. The kernel then disables that
 * interrupt source and so prevents the other device from working properly.
 */
#define HAS_AUX_IRQ(dev) (INTEL_INFO(dev)->gen >= 5)
#define HAS_GMBUS_IRQ(dev) (INTEL_INFO(dev)->has_gmbus_irq)

/* With the 945 and later, Y tiling got adjusted so that it was 32 128-byte
 * rows, which changed the alignment requirements and fence programming.
 */
#define HAS_128_BYTE_Y_TILING(dev_priv) (!IS_GEN2(dev_priv) && \
					 !(IS_I915G(dev_priv) || \
					 IS_I915GM(dev_priv)))
#define SUPPORTS_TV(dev)		(INTEL_INFO(dev)->supports_tv)
#define I915_HAS_HOTPLUG(dev)		 (INTEL_INFO(dev)->has_hotplug)

#define HAS_FW_BLC(dev) (INTEL_INFO(dev)->gen > 2)
#define HAS_PIPE_CXSR(dev) (INTEL_INFO(dev)->has_pipe_cxsr)
#define HAS_FBC(dev) (INTEL_INFO(dev)->has_fbc)

#define HAS_IPS(dev_priv)	(IS_HSW_ULT(dev_priv) || IS_BROADWELL(dev_priv))

#define HAS_DP_MST(dev)	(INTEL_INFO(dev)->has_dp_mst)

#define HAS_DDI(dev_priv)	((dev_priv)->info.has_ddi)
#define HAS_FPGA_DBG_UNCLAIMED(dev)	(INTEL_INFO(dev)->has_fpga_dbg)
#define HAS_PSR(dev)		(INTEL_INFO(dev)->has_psr)
#define HAS_RC6(dev)		(INTEL_INFO(dev)->has_rc6)
#define HAS_RC6p(dev)		(INTEL_INFO(dev)->has_rc6p)

#define HAS_CSR(dev)	(INTEL_INFO(dev)->has_csr)

#define HAS_RUNTIME_PM(dev_priv) ((dev_priv)->info.has_runtime_pm)
/*
 * For now, anything with a GuC requires uCode loading, and then supports
 * command submission once loaded. But these are logically independent
 * properties, so we have separate macros to test them.
 */
#define HAS_GUC(dev)		(INTEL_INFO(dev)->has_guc)
#define HAS_GUC_UCODE(dev)	(HAS_GUC(dev))
#define HAS_GUC_SCHED(dev)	(HAS_GUC(dev))

#define HAS_RESOURCE_STREAMER(dev) (INTEL_INFO(dev)->has_resource_streamer)

#define HAS_POOLED_EU(dev)	(INTEL_INFO(dev)->has_pooled_eu)

#define INTEL_PCH_DEVICE_ID_MASK		0xff00
#define INTEL_PCH_IBX_DEVICE_ID_TYPE		0x3b00
#define INTEL_PCH_CPT_DEVICE_ID_TYPE		0x1c00
#define INTEL_PCH_PPT_DEVICE_ID_TYPE		0x1e00
#define INTEL_PCH_LPT_DEVICE_ID_TYPE		0x8c00
#define INTEL_PCH_LPT_LP_DEVICE_ID_TYPE		0x9c00
#define INTEL_PCH_SPT_DEVICE_ID_TYPE		0xA100
#define INTEL_PCH_SPT_LP_DEVICE_ID_TYPE		0x9D00
#define INTEL_PCH_KBP_DEVICE_ID_TYPE		0xA200
#define INTEL_PCH_P2X_DEVICE_ID_TYPE		0x7100
#define INTEL_PCH_P3X_DEVICE_ID_TYPE		0x7000
#define INTEL_PCH_QEMU_DEVICE_ID_TYPE		0x2900 /* qemu q35 has 2918 */

#define INTEL_PCH_TYPE(dev_priv) ((dev_priv)->pch_type)
#define HAS_PCH_KBP(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_KBP)
#define HAS_PCH_SPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_SPT)
#define HAS_PCH_LPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_LPT)
#define HAS_PCH_LPT_LP(dev_priv) \
	((dev_priv)->pch_id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE)
#define HAS_PCH_LPT_H(dev_priv) \
	((dev_priv)->pch_id == INTEL_PCH_LPT_DEVICE_ID_TYPE)
#define HAS_PCH_CPT(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_CPT)
#define HAS_PCH_IBX(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_IBX)
#define HAS_PCH_NOP(dev_priv) (INTEL_PCH_TYPE(dev_priv) == PCH_NOP)
#define HAS_PCH_SPLIT(dev_priv) (INTEL_PCH_TYPE(dev_priv) != PCH_NONE)

#define HAS_GMCH_DISPLAY(dev_priv) ((dev_priv)->info.has_gmch_display)

#define HAS_LSPCON(dev_priv) (IS_GEN9(dev_priv))

/* DPF == dynamic parity feature */
#define HAS_L3_DPF(dev_priv) ((dev_priv)->info.has_l3_dpf)
#define NUM_L3_SLICES(dev_priv) (IS_HSW_GT3(dev_priv) ? \
				 2 : HAS_L3_DPF(dev_priv))

#define GT_FREQUENCY_MULTIPLIER 50
#define GEN9_FREQ_SCALER 3

#include "i915_trace.h"

static inline bool intel_scanout_needs_vtd_wa(struct drm_i915_private *dev_priv)
{
#ifdef CONFIG_INTEL_IOMMU
	if (INTEL_GEN(dev_priv) >= 6 && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

extern int i915_suspend_switcheroo(struct drm_device *dev, pm_message_t state);
extern int i915_resume_switcheroo(struct drm_device *dev);

int intel_sanitize_enable_ppgtt(struct drm_i915_private *dev_priv,
				int enable_ppgtt);

bool intel_sanitize_semaphores(struct drm_i915_private *dev_priv, int value);

/* i915_drv.c */
void __printf(3, 4)
__i915_printk(struct drm_i915_private *dev_priv, const char *level,
	      const char *fmt, ...);

#define i915_report_error(dev_priv, fmt, ...)				   \
	__i915_printk(dev_priv, KERN_ERR, fmt, ##__VA_ARGS__)

#ifdef CONFIG_COMPAT
extern long i915_compat_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);
#endif
extern const struct dev_pm_ops i915_pm_ops;

extern int i915_driver_load(struct pci_dev *pdev,
			    const struct pci_device_id *ent);
extern void i915_driver_unload(struct drm_device *dev);
extern int intel_gpu_reset(struct drm_i915_private *dev_priv, u32 engine_mask);
extern bool intel_has_gpu_reset(struct drm_i915_private *dev_priv);
extern void i915_reset(struct drm_i915_private *dev_priv);
extern int intel_guc_reset(struct drm_i915_private *dev_priv);
extern void intel_engine_init_hangcheck(struct intel_engine_cs *engine);
extern unsigned long i915_chipset_val(struct drm_i915_private *dev_priv);
extern unsigned long i915_mch_val(struct drm_i915_private *dev_priv);
extern unsigned long i915_gfx_val(struct drm_i915_private *dev_priv);
extern void i915_update_gfx_val(struct drm_i915_private *dev_priv);
int vlv_force_gfx_clock(struct drm_i915_private *dev_priv, bool on);

/* intel_hotplug.c */
void intel_hpd_irq_handler(struct drm_i915_private *dev_priv,
			   u32 pin_mask, u32 long_mask);
void intel_hpd_init(struct drm_i915_private *dev_priv);
void intel_hpd_init_work(struct drm_i915_private *dev_priv);
void intel_hpd_cancel_work(struct drm_i915_private *dev_priv);
bool intel_hpd_pin_to_port(enum hpd_pin pin, enum port *port);
bool intel_hpd_disable(struct drm_i915_private *dev_priv, enum hpd_pin pin);
void intel_hpd_enable(struct drm_i915_private *dev_priv, enum hpd_pin pin);

/* i915_irq.c */
static inline void i915_queue_hangcheck(struct drm_i915_private *dev_priv)
{
	unsigned long delay;

	if (unlikely(!i915.enable_hangcheck))
		return;

	/* Don't continually defer the hangcheck so that it is always run at
	 * least once after work has been scheduled on any ring. Otherwise,
	 * we will ignore a hung ring if a second ring is kept busy.
	 */

	delay = round_jiffies_up_relative(DRM_I915_HANGCHECK_JIFFIES);
	queue_delayed_work(system_long_wq,
			   &dev_priv->gpu_error.hangcheck_work, delay);
}

__printf(3, 4)
void i915_handle_error(struct drm_i915_private *dev_priv,
		       u32 engine_mask,
		       const char *fmt, ...);

extern void intel_irq_init(struct drm_i915_private *dev_priv);
int intel_irq_install(struct drm_i915_private *dev_priv);
void intel_irq_uninstall(struct drm_i915_private *dev_priv);

extern void intel_uncore_sanitize(struct drm_i915_private *dev_priv);
extern void intel_uncore_early_sanitize(struct drm_i915_private *dev_priv,
					bool restore_forcewake);
extern void intel_uncore_init(struct drm_i915_private *dev_priv);
extern bool intel_uncore_unclaimed_mmio(struct drm_i915_private *dev_priv);
extern bool intel_uncore_arm_unclaimed_mmio_detection(struct drm_i915_private *dev_priv);
extern void intel_uncore_fini(struct drm_i915_private *dev_priv);
extern void intel_uncore_forcewake_reset(struct drm_i915_private *dev_priv,
					 bool restore);
const char *intel_uncore_forcewake_domain_to_str(const enum forcewake_domain_id id);
void intel_uncore_forcewake_get(struct drm_i915_private *dev_priv,
				enum forcewake_domains domains);
void intel_uncore_forcewake_put(struct drm_i915_private *dev_priv,
				enum forcewake_domains domains);
/* Like above but the caller must manage the uncore.lock itself.
 * Must be used with I915_READ_FW and friends.
 */
void intel_uncore_forcewake_get__locked(struct drm_i915_private *dev_priv,
					enum forcewake_domains domains);
void intel_uncore_forcewake_put__locked(struct drm_i915_private *dev_priv,
					enum forcewake_domains domains);
u64 intel_uncore_edram_size(struct drm_i915_private *dev_priv);

void assert_forcewakes_inactive(struct drm_i915_private *dev_priv);

int intel_wait_for_register(struct drm_i915_private *dev_priv,
			    i915_reg_t reg,
			    const u32 mask,
			    const u32 value,
			    const unsigned long timeout_ms);
int intel_wait_for_register_fw(struct drm_i915_private *dev_priv,
			       i915_reg_t reg,
			       const u32 mask,
			       const u32 value,
			       const unsigned long timeout_ms);

static inline bool intel_gvt_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->gvt;
}

static inline bool intel_vgpu_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->vgpu.active;
}

void
i915_enable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		     u32 status_mask);

void
i915_disable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		      u32 status_mask);

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv);
void valleyview_disable_display_irqs(struct drm_i915_private *dev_priv);
void i915_hotplug_interrupt_update(struct drm_i915_private *dev_priv,
				   uint32_t mask,
				   uint32_t bits);
void ilk_update_display_irq(struct drm_i915_private *dev_priv,
			    uint32_t interrupt_mask,
			    uint32_t enabled_irq_mask);
static inline void
ilk_enable_display_irq(struct drm_i915_private *dev_priv, uint32_t bits)
{
	ilk_update_display_irq(dev_priv, bits, bits);
}
static inline void
ilk_disable_display_irq(struct drm_i915_private *dev_priv, uint32_t bits)
{
	ilk_update_display_irq(dev_priv, bits, 0);
}
void bdw_update_pipe_irq(struct drm_i915_private *dev_priv,
			 enum pipe pipe,
			 uint32_t interrupt_mask,
			 uint32_t enabled_irq_mask);
static inline void bdw_enable_pipe_irq(struct drm_i915_private *dev_priv,
				       enum pipe pipe, uint32_t bits)
{
	bdw_update_pipe_irq(dev_priv, pipe, bits, bits);
}
static inline void bdw_disable_pipe_irq(struct drm_i915_private *dev_priv,
					enum pipe pipe, uint32_t bits)
{
	bdw_update_pipe_irq(dev_priv, pipe, bits, 0);
}
void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
				  uint32_t interrupt_mask,
				  uint32_t enabled_irq_mask);
static inline void
ibx_enable_display_interrupt(struct drm_i915_private *dev_priv, uint32_t bits)
{
	ibx_display_interrupt_update(dev_priv, bits, bits);
}
static inline void
ibx_disable_display_interrupt(struct drm_i915_private *dev_priv, uint32_t bits)
{
	ibx_display_interrupt_update(dev_priv, bits, 0);
}

/* i915_gem.c */
int i915_gem_create_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int i915_gem_pread_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int i915_gem_execbuffer(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_execbuffer2(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int i915_gem_busy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int i915_gem_set_tiling(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_get_tiling(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
void i915_gem_init_userptr(struct drm_i915_private *dev_priv);
int i915_gem_userptr_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file);
int i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int i915_gem_wait_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
void i915_gem_load_init(struct drm_device *dev);
void i915_gem_load_cleanup(struct drm_device *dev);
void i915_gem_load_init_fences(struct drm_i915_private *dev_priv);
int i915_gem_freeze(struct drm_i915_private *dev_priv);
int i915_gem_freeze_late(struct drm_i915_private *dev_priv);

void *i915_gem_object_alloc(struct drm_device *dev);
void i915_gem_object_free(struct drm_i915_gem_object *obj);
void i915_gem_object_init(struct drm_i915_gem_object *obj,
			 const struct drm_i915_gem_object_ops *ops);
struct drm_i915_gem_object *i915_gem_object_create(struct drm_device *dev,
						   u64 size);
struct drm_i915_gem_object *i915_gem_object_create_from_data(
		struct drm_device *dev, const void *data, size_t size);
void i915_gem_close_object(struct drm_gem_object *gem, struct drm_file *file);
void i915_gem_free_object(struct drm_gem_object *obj);

struct i915_vma * __must_check
i915_gem_object_ggtt_pin(struct drm_i915_gem_object *obj,
			 const struct i915_ggtt_view *view,
			 u64 size,
			 u64 alignment,
			 u64 flags);

int i915_vma_bind(struct i915_vma *vma, enum i915_cache_level cache_level,
		  u32 flags);
void __i915_vma_set_map_and_fenceable(struct i915_vma *vma);
int __must_check i915_vma_unbind(struct i915_vma *vma);
void i915_vma_close(struct i915_vma *vma);
void i915_vma_destroy(struct i915_vma *vma);

int i915_gem_object_unbind(struct drm_i915_gem_object *obj);
int i915_gem_object_put_pages(struct drm_i915_gem_object *obj);
void i915_gem_release_mmap(struct drm_i915_gem_object *obj);

void i915_gem_runtime_suspend(struct drm_i915_private *dev_priv);

int __must_check i915_gem_object_get_pages(struct drm_i915_gem_object *obj);

static inline int __sg_page_count(struct scatterlist *sg)
{
	return sg->length >> PAGE_SHIFT;
}

struct page *
i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj, int n);

static inline dma_addr_t
i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj, int n)
{
	if (n < obj->get_page.last) {
		obj->get_page.sg = obj->pages->sgl;
		obj->get_page.last = 0;
	}

	while (obj->get_page.last + __sg_page_count(obj->get_page.sg) <= n) {
		obj->get_page.last += __sg_page_count(obj->get_page.sg++);
		if (unlikely(sg_is_chain(obj->get_page.sg)))
			obj->get_page.sg = sg_chain_ptr(obj->get_page.sg);
	}

	return sg_dma_address(obj->get_page.sg) + ((n - obj->get_page.last) << PAGE_SHIFT);
}

static inline struct page *
i915_gem_object_get_page(struct drm_i915_gem_object *obj, int n)
{
	if (WARN_ON(n >= obj->base.size >> PAGE_SHIFT))
		return NULL;

	if (n < obj->get_page.last) {
		obj->get_page.sg = obj->pages->sgl;
		obj->get_page.last = 0;
	}

	while (obj->get_page.last + __sg_page_count(obj->get_page.sg) <= n) {
		obj->get_page.last += __sg_page_count(obj->get_page.sg++);
		if (unlikely(sg_is_chain(obj->get_page.sg)))
			obj->get_page.sg = sg_chain_ptr(obj->get_page.sg);
	}

	return nth_page(sg_page(obj->get_page.sg), n - obj->get_page.last);
}

static inline void i915_gem_object_pin_pages(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(obj->pages == NULL);
	obj->pages_pin_count++;
}

static inline void i915_gem_object_unpin_pages(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(obj->pages_pin_count == 0);
	obj->pages_pin_count--;
	GEM_BUG_ON(obj->pages_pin_count < obj->bind_count);
}

enum i915_map_type {
	I915_MAP_WB = 0,
	I915_MAP_WC,
};

/**
 * i915_gem_object_pin_map - return a contiguous mapping of the entire object
 * @obj - the object to map into kernel address space
 * @type - the type of mapping, used to select pgprot_t
 *
 * Calls i915_gem_object_pin_pages() to prevent reaping of the object's
 * pages and then returns a contiguous mapping of the backing storage into
 * the kernel address space. Based on the @type of mapping, the PTE will be
 * set to either WriteBack or WriteCombine (via pgprot_t).
 *
 * The caller must hold the struct_mutex, and is responsible for calling
 * i915_gem_object_unpin_map() when the mapping is no longer required.
 *
 * Returns the pointer through which to access the mapped object, or an
 * ERR_PTR() on error.
 */
void *__must_check i915_gem_object_pin_map(struct drm_i915_gem_object *obj,
					   enum i915_map_type type);

/**
 * i915_gem_object_unpin_map - releases an earlier mapping
 * @obj - the object to unmap
 *
 * After pinning the object and mapping its pages, once you are finished
 * with your access, call i915_gem_object_unpin_map() to release the pin
 * upon the mapping. Once the pin count reaches zero, that mapping may be
 * removed.
 *
 * The caller must hold the struct_mutex.
 */
static inline void i915_gem_object_unpin_map(struct drm_i915_gem_object *obj)
{
	lockdep_assert_held(&obj->base.dev->struct_mutex);
	i915_gem_object_unpin_pages(obj);
}

int i915_gem_obj_prepare_shmem_read(struct drm_i915_gem_object *obj,
				    unsigned int *needs_clflush);
int i915_gem_obj_prepare_shmem_write(struct drm_i915_gem_object *obj,
				     unsigned int *needs_clflush);
#define CLFLUSH_BEFORE 0x1
#define CLFLUSH_AFTER 0x2
#define CLFLUSH_FLAGS (CLFLUSH_BEFORE | CLFLUSH_AFTER)

static inline void
i915_gem_obj_finish_shmem_access(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin_pages(obj);
}

int __must_check i915_mutex_lock_interruptible(struct drm_device *dev);
void i915_vma_move_to_active(struct i915_vma *vma,
			     struct drm_i915_gem_request *req,
			     unsigned int flags);
int i915_gem_dumb_create(struct drm_file *file_priv,
			 struct drm_device *dev,
			 struct drm_mode_create_dumb *args);
int i915_gem_mmap_gtt(struct drm_file *file_priv, struct drm_device *dev,
		      uint32_t handle, uint64_t *offset);
int i915_gem_mmap_gtt_version(void);

void i915_gem_track_fb(struct drm_i915_gem_object *old,
		       struct drm_i915_gem_object *new,
		       unsigned frontbuffer_bits);

int __must_check i915_gem_set_seqno(struct drm_device *dev, u32 seqno);

struct drm_i915_gem_request *
i915_gem_find_active_request(struct intel_engine_cs *engine);

void i915_gem_retire_requests(struct drm_i915_private *dev_priv);

static inline bool i915_reset_in_progress(struct i915_gpu_error *error)
{
	return unlikely(test_bit(I915_RESET_IN_PROGRESS, &error->flags));
}

static inline bool i915_terminally_wedged(struct i915_gpu_error *error)
{
	return unlikely(test_bit(I915_WEDGED, &error->flags));
}

static inline bool i915_reset_in_progress_or_wedged(struct i915_gpu_error *error)
{
	return i915_reset_in_progress(error) | i915_terminally_wedged(error);
}

static inline u32 i915_reset_count(struct i915_gpu_error *error)
{
	return READ_ONCE(error->reset_count);
}

void i915_gem_reset(struct drm_i915_private *dev_priv);
void i915_gem_set_wedged(struct drm_i915_private *dev_priv);
bool i915_gem_clflush_object(struct drm_i915_gem_object *obj, bool force);
int __must_check i915_gem_init(struct drm_device *dev);
int __must_check i915_gem_init_hw(struct drm_device *dev);
void i915_gem_init_swizzling(struct drm_device *dev);
void i915_gem_cleanup_engines(struct drm_device *dev);
int __must_check i915_gem_wait_for_idle(struct drm_i915_private *dev_priv,
					unsigned int flags);
int __must_check i915_gem_suspend(struct drm_device *dev);
void i915_gem_resume(struct drm_device *dev);
int i915_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int __must_check
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool readonly);
int __must_check
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj,
				  bool write);
int __must_check
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write);
struct i915_vma * __must_check
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     u32 alignment,
				     const struct i915_ggtt_view *view);
void i915_gem_object_unpin_from_display_plane(struct i915_vma *vma);
int i915_gem_object_attach_phys(struct drm_i915_gem_object *obj,
				int align);
int i915_gem_open(struct drm_device *dev, struct drm_file *file);
void i915_gem_release(struct drm_device *dev, struct drm_file *file);

u64 i915_gem_get_ggtt_size(struct drm_i915_private *dev_priv, u64 size,
			   int tiling_mode);
u64 i915_gem_get_ggtt_alignment(struct drm_i915_private *dev_priv, u64 size,
				int tiling_mode, bool fenced);

int i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level);

struct drm_gem_object *i915_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf);

struct dma_buf *i915_gem_prime_export(struct drm_device *dev,
				struct drm_gem_object *gem_obj, int flags);

struct i915_vma *
i915_gem_obj_to_vma(struct drm_i915_gem_object *obj,
		     struct i915_address_space *vm,
		     const struct i915_ggtt_view *view);

struct i915_vma *
i915_gem_obj_lookup_or_create_vma(struct drm_i915_gem_object *obj,
				  struct i915_address_space *vm,
				  const struct i915_ggtt_view *view);

static inline struct i915_hw_ppgtt *
i915_vm_to_ppgtt(struct i915_address_space *vm)
{
	return container_of(vm, struct i915_hw_ppgtt, base);
}

static inline struct i915_vma *
i915_gem_object_to_ggtt(struct drm_i915_gem_object *obj,
			const struct i915_ggtt_view *view)
{
	return i915_gem_obj_to_vma(obj, &to_i915(obj->base.dev)->ggtt.base, view);
}

static inline unsigned long
i915_gem_object_ggtt_offset(struct drm_i915_gem_object *o,
			    const struct i915_ggtt_view *view)
{
	return i915_ggtt_offset(i915_gem_object_to_ggtt(o, view));
}

/* i915_gem_fence.c */
int __must_check i915_vma_get_fence(struct i915_vma *vma);
int __must_check i915_vma_put_fence(struct i915_vma *vma);

/**
 * i915_vma_pin_fence - pin fencing state
 * @vma: vma to pin fencing for
 *
 * This pins the fencing state (whether tiled or untiled) to make sure the
 * vma (and its object) is ready to be used as a scanout target. Fencing
 * status must be synchronize first by calling i915_vma_get_fence():
 *
 * The resulting fence pin reference must be released again with
 * i915_vma_unpin_fence().
 *
 * Returns:
 *
 * True if the vma has a fence, false otherwise.
 */
static inline bool
i915_vma_pin_fence(struct i915_vma *vma)
{
	if (vma->fence) {
		vma->fence->pin_count++;
		return true;
	} else
		return false;
}

/**
 * i915_vma_unpin_fence - unpin fencing state
 * @vma: vma to unpin fencing for
 *
 * This releases the fence pin reference acquired through
 * i915_vma_pin_fence. It will handle both objects with and without an
 * attached fence correctly, callers do not need to distinguish this.
 */
static inline void
i915_vma_unpin_fence(struct i915_vma *vma)
{
	if (vma->fence) {
		GEM_BUG_ON(vma->fence->pin_count <= 0);
		vma->fence->pin_count--;
	}
}

void i915_gem_restore_fences(struct drm_device *dev);

void i915_gem_detect_bit_6_swizzle(struct drm_device *dev);
void i915_gem_object_do_bit_17_swizzle(struct drm_i915_gem_object *obj);
void i915_gem_object_save_bit_17_swizzle(struct drm_i915_gem_object *obj);

/* i915_gem_context.c */
int __must_check i915_gem_context_init(struct drm_device *dev);
void i915_gem_context_lost(struct drm_i915_private *dev_priv);
void i915_gem_context_fini(struct drm_device *dev);
int i915_gem_context_open(struct drm_device *dev, struct drm_file *file);
void i915_gem_context_close(struct drm_device *dev, struct drm_file *file);
int i915_switch_context(struct drm_i915_gem_request *req);
int i915_gem_switch_to_kernel_context(struct drm_i915_private *dev_priv);
void i915_gem_context_free(struct kref *ctx_ref);
struct drm_i915_gem_object *
i915_gem_alloc_context_obj(struct drm_device *dev, size_t size);
struct i915_gem_context *
i915_gem_context_create_gvt(struct drm_device *dev);

static inline struct i915_gem_context *
i915_gem_context_lookup(struct drm_i915_file_private *file_priv, u32 id)
{
	struct i915_gem_context *ctx;

	lockdep_assert_held(&file_priv->dev_priv->drm.struct_mutex);

	ctx = idr_find(&file_priv->context_idr, id);
	if (!ctx)
		return ERR_PTR(-ENOENT);

	return ctx;
}

static inline struct i915_gem_context *
i915_gem_context_get(struct i915_gem_context *ctx)
{
	kref_get(&ctx->ref);
	return ctx;
}

static inline void i915_gem_context_put(struct i915_gem_context *ctx)
{
	lockdep_assert_held(&ctx->i915->drm.struct_mutex);
	kref_put(&ctx->ref, i915_gem_context_free);
}

static inline bool i915_gem_context_is_default(const struct i915_gem_context *c)
{
	return c->user_handle == DEFAULT_CONTEXT_HANDLE;
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file);
int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file);
int i915_gem_context_getparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_setparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_reset_stats_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file);

/* i915_gem_evict.c */
int __must_check i915_gem_evict_something(struct i915_address_space *vm,
					  u64 min_size, u64 alignment,
					  unsigned cache_level,
					  u64 start, u64 end,
					  unsigned flags);
int __must_check i915_gem_evict_for_vma(struct i915_vma *target);
int i915_gem_evict_vm(struct i915_address_space *vm, bool do_idle);

/* belongs in i915_gem_gtt.h */
static inline void i915_gem_chipset_flush(struct drm_i915_private *dev_priv)
{
	wmb();
	if (INTEL_GEN(dev_priv) < 6)
		intel_gtt_chipset_flush();
}

/* i915_gem_stolen.c */
int i915_gem_stolen_insert_node(struct drm_i915_private *dev_priv,
				struct drm_mm_node *node, u64 size,
				unsigned alignment);
int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *dev_priv,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start,
					 u64 end);
void i915_gem_stolen_remove_node(struct drm_i915_private *dev_priv,
				 struct drm_mm_node *node);
int i915_gem_init_stolen(struct drm_device *dev);
void i915_gem_cleanup_stolen(struct drm_device *dev);
struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_device *dev, u32 size);
struct drm_i915_gem_object *
i915_gem_object_create_stolen_for_preallocated(struct drm_device *dev,
					       u32 stolen_offset,
					       u32 gtt_offset,
					       u32 size);

/* i915_gem_shrinker.c */
unsigned long i915_gem_shrink(struct drm_i915_private *dev_priv,
			      unsigned long target,
			      unsigned flags);
#define I915_SHRINK_PURGEABLE 0x1
#define I915_SHRINK_UNBOUND 0x2
#define I915_SHRINK_BOUND 0x4
#define I915_SHRINK_ACTIVE 0x8
#define I915_SHRINK_VMAPS 0x10
unsigned long i915_gem_shrink_all(struct drm_i915_private *dev_priv);
void i915_gem_shrinker_init(struct drm_i915_private *dev_priv);
void i915_gem_shrinker_cleanup(struct drm_i915_private *dev_priv);


/* i915_gem_tiling.c */
static inline bool i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	return dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_9_10_17 &&
		i915_gem_object_is_tiled(obj);
}

/* i915_debugfs.c */
#ifdef CONFIG_DEBUG_FS
int i915_debugfs_register(struct drm_i915_private *dev_priv);
void i915_debugfs_unregister(struct drm_i915_private *dev_priv);
int i915_debugfs_connector_add(struct drm_connector *connector);
void intel_display_crc_init(struct drm_i915_private *dev_priv);
#else
static inline int i915_debugfs_register(struct drm_i915_private *dev_priv) {return 0;}
static inline void i915_debugfs_unregister(struct drm_i915_private *dev_priv) {}
static inline int i915_debugfs_connector_add(struct drm_connector *connector)
{ return 0; }
static inline void intel_display_crc_init(struct drm_i915_private *dev_priv) {}
#endif

/* i915_gpu_error.c */
#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

__printf(2, 3)
void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...);
int i915_error_state_to_str(struct drm_i915_error_state_buf *estr,
			    const struct i915_error_state_file_priv *error);
int i915_error_state_buf_init(struct drm_i915_error_state_buf *eb,
			      struct drm_i915_private *i915,
			      size_t count, loff_t pos);
static inline void i915_error_state_buf_release(
	struct drm_i915_error_state_buf *eb)
{
	kfree(eb->buf);
}
void i915_capture_error_state(struct drm_i915_private *dev_priv,
			      u32 engine_mask,
			      const char *error_msg);
void i915_error_state_get(struct drm_device *dev,
			  struct i915_error_state_file_priv *error_priv);
void i915_error_state_put(struct i915_error_state_file_priv *error_priv);
void i915_destroy_error_state(struct drm_device *dev);

#else

static inline void i915_capture_error_state(struct drm_i915_private *dev_priv,
					    u32 engine_mask,
					    const char *error_msg)
{
}

static inline void i915_destroy_error_state(struct drm_device *dev)
{
}

#endif

const char *i915_cache_level_str(struct drm_i915_private *i915, int type);

/* i915_cmd_parser.c */
int i915_cmd_parser_get_version(struct drm_i915_private *dev_priv);
void intel_engine_init_cmd_parser(struct intel_engine_cs *engine);
void intel_engine_cleanup_cmd_parser(struct intel_engine_cs *engine);
bool intel_engine_needs_cmd_parser(struct intel_engine_cs *engine);
int intel_engine_cmd_parser(struct intel_engine_cs *engine,
			    struct drm_i915_gem_object *batch_obj,
			    struct drm_i915_gem_object *shadow_batch_obj,
			    u32 batch_start_offset,
			    u32 batch_len,
			    bool is_master);

/* i915_suspend.c */
extern int i915_save_state(struct drm_device *dev);
extern int i915_restore_state(struct drm_device *dev);

/* i915_sysfs.c */
void i915_setup_sysfs(struct drm_i915_private *dev_priv);
void i915_teardown_sysfs(struct drm_i915_private *dev_priv);

/* intel_i2c.c */
extern int intel_setup_gmbus(struct drm_device *dev);
extern void intel_teardown_gmbus(struct drm_device *dev);
extern bool intel_gmbus_is_valid_pin(struct drm_i915_private *dev_priv,
				     unsigned int pin);

extern struct i2c_adapter *
intel_gmbus_get_adapter(struct drm_i915_private *dev_priv, unsigned int pin);
extern void intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed);
extern void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
static inline bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter)
{
	return container_of(adapter, struct intel_gmbus, adapter)->force_bit;
}
extern void intel_i2c_reset(struct drm_device *dev);

/* intel_bios.c */
int intel_bios_init(struct drm_i915_private *dev_priv);
bool intel_bios_is_valid_vbt(const void *buf, size_t size);
bool intel_bios_is_tv_present(struct drm_i915_private *dev_priv);
bool intel_bios_is_lvds_present(struct drm_i915_private *dev_priv, u8 *i2c_pin);
bool intel_bios_is_port_present(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_port_edp(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_port_dp_dual_mode(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_dsi_present(struct drm_i915_private *dev_priv, enum port *port);
bool intel_bios_is_port_hpd_inverted(struct drm_i915_private *dev_priv,
				     enum port port);
bool intel_bios_is_lspcon_present(struct drm_i915_private *dev_priv,
				enum port port);


/* intel_opregion.c */
#ifdef CONFIG_ACPI
extern int intel_opregion_setup(struct drm_i915_private *dev_priv);
extern void intel_opregion_register(struct drm_i915_private *dev_priv);
extern void intel_opregion_unregister(struct drm_i915_private *dev_priv);
extern void intel_opregion_asle_intr(struct drm_i915_private *dev_priv);
extern int intel_opregion_notify_encoder(struct intel_encoder *intel_encoder,
					 bool enable);
extern int intel_opregion_notify_adapter(struct drm_i915_private *dev_priv,
					 pci_power_t state);
extern int intel_opregion_get_panel_type(struct drm_i915_private *dev_priv);
#else
static inline int intel_opregion_setup(struct drm_i915_private *dev) { return 0; }
static inline void intel_opregion_register(struct drm_i915_private *dev_priv) { }
static inline void intel_opregion_unregister(struct drm_i915_private *dev_priv) { }
static inline void intel_opregion_asle_intr(struct drm_i915_private *dev_priv)
{
}
static inline int
intel_opregion_notify_encoder(struct intel_encoder *intel_encoder, bool enable)
{
	return 0;
}
static inline int
intel_opregion_notify_adapter(struct drm_i915_private *dev, pci_power_t state)
{
	return 0;
}
static inline int intel_opregion_get_panel_type(struct drm_i915_private *dev)
{
	return -ENODEV;
}
#endif

/* intel_acpi.c */
#ifdef CONFIG_ACPI
extern void intel_register_dsm_handler(void);
extern void intel_unregister_dsm_handler(void);
#else
static inline void intel_register_dsm_handler(void) { return; }
static inline void intel_unregister_dsm_handler(void) { return; }
#endif /* CONFIG_ACPI */

/* intel_device_info.c */
static inline struct intel_device_info *
mkwrite_device_info(struct drm_i915_private *dev_priv)
{
	return (struct intel_device_info *)&dev_priv->info;
}

void intel_device_info_runtime_init(struct drm_i915_private *dev_priv);
void intel_device_info_dump(struct drm_i915_private *dev_priv);

/* modesetting */
extern void intel_modeset_init_hw(struct drm_device *dev);
extern void intel_modeset_init(struct drm_device *dev);
extern void intel_modeset_gem_init(struct drm_device *dev);
extern void intel_modeset_cleanup(struct drm_device *dev);
extern int intel_connector_register(struct drm_connector *);
extern void intel_connector_unregister(struct drm_connector *);
extern int intel_modeset_vga_set_state(struct drm_device *dev, bool state);
extern void intel_display_resume(struct drm_device *dev);
extern void i915_redisable_vga(struct drm_device *dev);
extern void i915_redisable_vga_power_on(struct drm_device *dev);
extern bool ironlake_set_drps(struct drm_i915_private *dev_priv, u8 val);
extern void intel_init_pch_refclk(struct drm_device *dev);
extern void intel_set_rps(struct drm_i915_private *dev_priv, u8 val);
extern void intel_set_memory_cxsr(struct drm_i915_private *dev_priv,
				  bool enable);

int i915_reg_read_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);

/* overlay */
extern struct intel_overlay_error_state *
intel_overlay_capture_error_state(struct drm_i915_private *dev_priv);
extern void intel_overlay_print_error_state(struct drm_i915_error_state_buf *e,
					    struct intel_overlay_error_state *error);

extern struct intel_display_error_state *
intel_display_capture_error_state(struct drm_i915_private *dev_priv);
extern void intel_display_print_error_state(struct drm_i915_error_state_buf *e,
					    struct drm_device *dev,
					    struct intel_display_error_state *error);

int sandybridge_pcode_read(struct drm_i915_private *dev_priv, u32 mbox, u32 *val);
int sandybridge_pcode_write(struct drm_i915_private *dev_priv, u32 mbox, u32 val);

/* intel_sideband.c */
u32 vlv_punit_read(struct drm_i915_private *dev_priv, u32 addr);
void vlv_punit_write(struct drm_i915_private *dev_priv, u32 addr, u32 val);
u32 vlv_nc_read(struct drm_i915_private *dev_priv, u8 addr);
u32 vlv_iosf_sb_read(struct drm_i915_private *dev_priv, u8 port, u32 reg);
void vlv_iosf_sb_write(struct drm_i915_private *dev_priv, u8 port, u32 reg, u32 val);
u32 vlv_cck_read(struct drm_i915_private *dev_priv, u32 reg);
void vlv_cck_write(struct drm_i915_private *dev_priv, u32 reg, u32 val);
u32 vlv_ccu_read(struct drm_i915_private *dev_priv, u32 reg);
void vlv_ccu_write(struct drm_i915_private *dev_priv, u32 reg, u32 val);
u32 vlv_bunit_read(struct drm_i915_private *dev_priv, u32 reg);
void vlv_bunit_write(struct drm_i915_private *dev_priv, u32 reg, u32 val);
u32 vlv_dpio_read(struct drm_i915_private *dev_priv, enum pipe pipe, int reg);
void vlv_dpio_write(struct drm_i915_private *dev_priv, enum pipe pipe, int reg, u32 val);
u32 intel_sbi_read(struct drm_i915_private *dev_priv, u16 reg,
		   enum intel_sbi_destination destination);
void intel_sbi_write(struct drm_i915_private *dev_priv, u16 reg, u32 value,
		     enum intel_sbi_destination destination);
u32 vlv_flisdsi_read(struct drm_i915_private *dev_priv, u32 reg);
void vlv_flisdsi_write(struct drm_i915_private *dev_priv, u32 reg, u32 val);

/* intel_dpio_phy.c */
void chv_set_phy_signal_level(struct intel_encoder *encoder,
			      u32 deemph_reg_value, u32 margin_reg_value,
			      bool uniq_trans_scale);
void chv_data_lane_soft_reset(struct intel_encoder *encoder,
			      bool reset);
void chv_phy_pre_pll_enable(struct intel_encoder *encoder);
void chv_phy_pre_encoder_enable(struct intel_encoder *encoder);
void chv_phy_release_cl2_override(struct intel_encoder *encoder);
void chv_phy_post_pll_disable(struct intel_encoder *encoder);

void vlv_set_phy_signal_level(struct intel_encoder *encoder,
			      u32 demph_reg_value, u32 preemph_reg_value,
			      u32 uniqtranscale_reg_value, u32 tx3_demph);
void vlv_phy_pre_pll_enable(struct intel_encoder *encoder);
void vlv_phy_pre_encoder_enable(struct intel_encoder *encoder);
void vlv_phy_reset_lanes(struct intel_encoder *encoder);

int intel_gpu_freq(struct drm_i915_private *dev_priv, int val);
int intel_freq_opcode(struct drm_i915_private *dev_priv, int val);

#define I915_READ8(reg)		dev_priv->uncore.funcs.mmio_readb(dev_priv, (reg), true)
#define I915_WRITE8(reg, val)	dev_priv->uncore.funcs.mmio_writeb(dev_priv, (reg), (val), true)

#define I915_READ16(reg)	dev_priv->uncore.funcs.mmio_readw(dev_priv, (reg), true)
#define I915_WRITE16(reg, val)	dev_priv->uncore.funcs.mmio_writew(dev_priv, (reg), (val), true)
#define I915_READ16_NOTRACE(reg)	dev_priv->uncore.funcs.mmio_readw(dev_priv, (reg), false)
#define I915_WRITE16_NOTRACE(reg, val)	dev_priv->uncore.funcs.mmio_writew(dev_priv, (reg), (val), false)

#define I915_READ(reg)		dev_priv->uncore.funcs.mmio_readl(dev_priv, (reg), true)
#define I915_WRITE(reg, val)	dev_priv->uncore.funcs.mmio_writel(dev_priv, (reg), (val), true)
#define I915_READ_NOTRACE(reg)		dev_priv->uncore.funcs.mmio_readl(dev_priv, (reg), false)
#define I915_WRITE_NOTRACE(reg, val)	dev_priv->uncore.funcs.mmio_writel(dev_priv, (reg), (val), false)

/* Be very careful with read/write 64-bit values. On 32-bit machines, they
 * will be implemented using 2 32-bit writes in an arbitrary order with
 * an arbitrary delay between them. This can cause the hardware to
 * act upon the intermediate value, possibly leading to corruption and
 * machine death. For this reason we do not support I915_WRITE64, or
 * dev_priv->uncore.funcs.mmio_writeq.
 *
 * When reading a 64-bit value as two 32-bit values, the delay may cause
 * the two reads to mismatch, e.g. a timestamp overflowing. Also note that
 * occasionally a 64-bit register does not actualy support a full readq
 * and must be read using two 32-bit reads.
 *
 * You have been warned.
 */
#define I915_READ64(reg)	dev_priv->uncore.funcs.mmio_readq(dev_priv, (reg), true)

#define I915_READ64_2x32(lower_reg, upper_reg) ({			\
	u32 upper, lower, old_upper, loop = 0;				\
	upper = I915_READ(upper_reg);					\
	do {								\
		old_upper = upper;					\
		lower = I915_READ(lower_reg);				\
		upper = I915_READ(upper_reg);				\
	} while (upper != old_upper && loop++ < 2);			\
	(u64)upper << 32 | lower; })

#define POSTING_READ(reg)	(void)I915_READ_NOTRACE(reg)
#define POSTING_READ16(reg)	(void)I915_READ16_NOTRACE(reg)

#define __raw_read(x, s) \
static inline uint##x##_t __raw_i915_read##x(struct drm_i915_private *dev_priv, \
					     i915_reg_t reg) \
{ \
	return read##s(dev_priv->regs + i915_mmio_reg_offset(reg)); \
}

#define __raw_write(x, s) \
static inline void __raw_i915_write##x(struct drm_i915_private *dev_priv, \
				       i915_reg_t reg, uint##x##_t val) \
{ \
	write##s(val, dev_priv->regs + i915_mmio_reg_offset(reg)); \
}
__raw_read(8, b)
__raw_read(16, w)
__raw_read(32, l)
__raw_read(64, q)

__raw_write(8, b)
__raw_write(16, w)
__raw_write(32, l)
__raw_write(64, q)

#undef __raw_read
#undef __raw_write

/* These are untraced mmio-accessors that are only valid to be used inside
 * critical sections inside IRQ handlers where forcewake is explicitly
 * controlled.
 * Think twice, and think again, before using these.
 * Note: Should only be used between intel_uncore_forcewake_irqlock() and
 * intel_uncore_forcewake_irqunlock().
 */
#define I915_READ_FW(reg__) __raw_i915_read32(dev_priv, (reg__))
#define I915_WRITE_FW(reg__, val__) __raw_i915_write32(dev_priv, (reg__), (val__))
#define I915_WRITE64_FW(reg__, val__) __raw_i915_write64(dev_priv, (reg__), (val__))
#define POSTING_READ_FW(reg__) (void)I915_READ_FW(reg__)

/* "Broadcast RGB" property */
#define INTEL_BROADCAST_RGB_AUTO 0
#define INTEL_BROADCAST_RGB_FULL 1
#define INTEL_BROADCAST_RGB_LIMITED 2

static inline i915_reg_t i915_vgacntrl_reg(struct drm_i915_private *dev_priv)
{
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return VLV_VGACNTRL;
	else if (INTEL_GEN(dev_priv) >= 5)
		return CPU_VGACNTRL;
	else
		return VGACNTRL;
}

static inline unsigned long msecs_to_jiffies_timeout(const unsigned int m)
{
	unsigned long j = msecs_to_jiffies(m);

	return min_t(unsigned long, MAX_JIFFY_OFFSET, j + 1);
}

static inline unsigned long nsecs_to_jiffies_timeout(const u64 n)
{
        return min_t(u64, MAX_JIFFY_OFFSET, nsecs_to_jiffies64(n) + 1);
}

static inline unsigned long
timespec_to_jiffies_timeout(const struct timespec *value)
{
	unsigned long j = timespec_to_jiffies(value);

	return min_t(unsigned long, MAX_JIFFY_OFFSET, j + 1);
}

/*
 * If you need to wait X milliseconds between events A and B, but event B
 * doesn't happen exactly after event A, you record the timestamp (jiffies) of
 * when event A happened, then just before event B you call this function and
 * pass the timestamp as the first argument, and X as the second argument.
 */
static inline void
wait_remaining_ms_from_jiffies(unsigned long timestamp_jiffies, int to_wait_ms)
{
	unsigned long target_jiffies, tmp_jiffies, remaining_jiffies;

	/*
	 * Don't re-read the value of "jiffies" every time since it may change
	 * behind our back and break the math.
	 */
	tmp_jiffies = jiffies;
	target_jiffies = timestamp_jiffies +
			 msecs_to_jiffies_timeout(to_wait_ms);

	if (time_after(target_jiffies, tmp_jiffies)) {
		remaining_jiffies = target_jiffies - tmp_jiffies;
		while (remaining_jiffies)
			remaining_jiffies =
			    schedule_timeout_uninterruptible(remaining_jiffies);
	}
}

static inline bool
__i915_request_irq_complete(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;

	/* Before we do the heavier coherent read of the seqno,
	 * check the value (hopefully) in the CPU cacheline.
	 */
	if (i915_gem_request_completed(req))
		return true;

	/* Ensure our read of the seqno is coherent so that we
	 * do not "miss an interrupt" (i.e. if this is the last
	 * request and the seqno write from the GPU is not visible
	 * by the time the interrupt fires, we will see that the
	 * request is incomplete and go back to sleep awaiting
	 * another interrupt that will never come.)
	 *
	 * Strictly, we only need to do this once after an interrupt,
	 * but it is easier and safer to do it every time the waiter
	 * is woken.
	 */
	if (engine->irq_seqno_barrier &&
	    rcu_access_pointer(engine->breadcrumbs.irq_seqno_bh) == current &&
	    cmpxchg_relaxed(&engine->breadcrumbs.irq_posted, 1, 0)) {
		struct task_struct *tsk;

		/* The ordering of irq_posted versus applying the barrier
		 * is crucial. The clearing of the current irq_posted must
		 * be visible before we perform the barrier operation,
		 * such that if a subsequent interrupt arrives, irq_posted
		 * is reasserted and our task rewoken (which causes us to
		 * do another __i915_request_irq_complete() immediately
		 * and reapply the barrier). Conversely, if the clear
		 * occurs after the barrier, then an interrupt that arrived
		 * whilst we waited on the barrier would not trigger a
		 * barrier on the next pass, and the read may not see the
		 * seqno update.
		 */
		engine->irq_seqno_barrier(engine);

		/* If we consume the irq, but we are no longer the bottom-half,
		 * the real bottom-half may not have serialised their own
		 * seqno check with the irq-barrier (i.e. may have inspected
		 * the seqno before we believe it coherent since they see
		 * irq_posted == false but we are still running).
		 */
		rcu_read_lock();
		tsk = rcu_dereference(engine->breadcrumbs.irq_seqno_bh);
		if (tsk && tsk != current)
			/* Note that if the bottom-half is changed as we
			 * are sending the wake-up, the new bottom-half will
			 * be woken by whomever made the change. We only have
			 * to worry about when we steal the irq-posted for
			 * ourself.
			 */
			wake_up_process(tsk);
		rcu_read_unlock();

		if (i915_gem_request_completed(req))
			return true;
	}

	return false;
}

void i915_memcpy_init_early(struct drm_i915_private *dev_priv);
bool i915_memcpy_from_wc(void *dst, const void *src, unsigned long len);

/* i915_mm.c */
int remap_io_mapping(struct vm_area_struct *vma,
		     unsigned long addr, unsigned long pfn, unsigned long size,
		     struct io_mapping *iomap);

#define ptr_mask_bits(ptr) ({						\
	unsigned long __v = (unsigned long)(ptr);			\
	(typeof(ptr))(__v & PAGE_MASK);					\
})

#define ptr_unpack_bits(ptr, bits) ({					\
	unsigned long __v = (unsigned long)(ptr);			\
	(bits) = __v & ~PAGE_MASK;					\
	(typeof(ptr))(__v & PAGE_MASK);					\
})

#define ptr_pack_bits(ptr, bits)					\
	((typeof(ptr))((unsigned long)(ptr) | (bits)))

#define fetch_and_zero(ptr) ({						\
	typeof(*ptr) __T = *(ptr);					\
	*(ptr) = (typeof(*ptr))0;					\
	__T;								\
})

#endif
