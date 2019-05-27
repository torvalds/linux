/* SPDX-License-Identifier: MIT */

#ifndef _INTEL_SIDEBAND_H_
#define _INTEL_SIDEBAND_H_

#include <linux/bitops.h>
#include <linux/types.h>

struct drm_i915_private;
enum pipe;

enum intel_sbi_destination {
	SBI_ICLK,
	SBI_MPHY,
};

enum {
	VLV_IOSF_SB_BUNIT,
	VLV_IOSF_SB_CCK,
	VLV_IOSF_SB_CCU,
	VLV_IOSF_SB_DPIO,
	VLV_IOSF_SB_FLISDSI,
	VLV_IOSF_SB_GPIO,
	VLV_IOSF_SB_NC,
	VLV_IOSF_SB_PUNIT,
};

void vlv_iosf_sb_get(struct drm_i915_private *i915, unsigned long ports);
u32 vlv_iosf_sb_read(struct drm_i915_private *i915, u8 port, u32 reg);
void vlv_iosf_sb_write(struct drm_i915_private *i915,
		       u8 port, u32 reg, u32 val);
void vlv_iosf_sb_put(struct drm_i915_private *i915, unsigned long ports);

static inline void vlv_bunit_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_BUNIT));
}

u32 vlv_bunit_read(struct drm_i915_private *i915, u32 reg);
void vlv_bunit_write(struct drm_i915_private *i915, u32 reg, u32 val);

static inline void vlv_bunit_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_BUNIT));
}

static inline void vlv_cck_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_CCK));
}

u32 vlv_cck_read(struct drm_i915_private *i915, u32 reg);
void vlv_cck_write(struct drm_i915_private *i915, u32 reg, u32 val);

static inline void vlv_cck_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_CCK));
}

static inline void vlv_ccu_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_CCU));
}

u32 vlv_ccu_read(struct drm_i915_private *i915, u32 reg);
void vlv_ccu_write(struct drm_i915_private *i915, u32 reg, u32 val);

static inline void vlv_ccu_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_CCU));
}

static inline void vlv_dpio_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_DPIO));
}

u32 vlv_dpio_read(struct drm_i915_private *i915, enum pipe pipe, int reg);
void vlv_dpio_write(struct drm_i915_private *i915,
		    enum pipe pipe, int reg, u32 val);

static inline void vlv_dpio_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_DPIO));
}

static inline void vlv_flisdsi_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_FLISDSI));
}

u32 vlv_flisdsi_read(struct drm_i915_private *i915, u32 reg);
void vlv_flisdsi_write(struct drm_i915_private *i915, u32 reg, u32 val);

static inline void vlv_flisdsi_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_FLISDSI));
}

static inline void vlv_nc_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_NC));
}

u32 vlv_nc_read(struct drm_i915_private *i915, u8 addr);

static inline void vlv_nc_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_NC));
}

static inline void vlv_punit_get(struct drm_i915_private *i915)
{
	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_PUNIT));
}

u32 vlv_punit_read(struct drm_i915_private *i915, u32 addr);
int vlv_punit_write(struct drm_i915_private *i915, u32 addr, u32 val);

static inline void vlv_punit_put(struct drm_i915_private *i915)
{
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_PUNIT));
}

u32 intel_sbi_read(struct drm_i915_private *i915, u16 reg,
		   enum intel_sbi_destination destination);
void intel_sbi_write(struct drm_i915_private *i915, u16 reg, u32 value,
		     enum intel_sbi_destination destination);

int sandybridge_pcode_read(struct drm_i915_private *i915, u32 mbox, u32 *val);
int sandybridge_pcode_write_timeout(struct drm_i915_private *i915, u32 mbox,
				    u32 val, int fast_timeout_us,
				    int slow_timeout_ms);
#define sandybridge_pcode_write(i915, mbox, val)	\
	sandybridge_pcode_write_timeout(i915, mbox, val, 500, 0)

int skl_pcode_request(struct drm_i915_private *i915, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms);

#endif /* _INTEL_SIDEBAND_H */
