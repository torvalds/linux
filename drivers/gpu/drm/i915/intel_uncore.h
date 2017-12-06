/*
 * Copyright © 2017 Intel Corporation
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

#ifndef __INTEL_UNCORE_H__
#define __INTEL_UNCORE_H__

#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/hrtimer.h>

#include "i915_reg.h"

struct drm_i915_private;

enum forcewake_domain_id {
	FW_DOMAIN_ID_RENDER = 0,
	FW_DOMAIN_ID_BLITTER,
	FW_DOMAIN_ID_MEDIA,

	FW_DOMAIN_ID_COUNT
};

enum forcewake_domains {
	FORCEWAKE_RENDER = BIT(FW_DOMAIN_ID_RENDER),
	FORCEWAKE_BLITTER = BIT(FW_DOMAIN_ID_BLITTER),
	FORCEWAKE_MEDIA	= BIT(FW_DOMAIN_ID_MEDIA),
	FORCEWAKE_ALL = (FORCEWAKE_RENDER |
			 FORCEWAKE_BLITTER |
			 FORCEWAKE_MEDIA)
};

struct intel_uncore_funcs {
	void (*force_wake_get)(struct drm_i915_private *dev_priv,
			       enum forcewake_domains domains);
	void (*force_wake_put)(struct drm_i915_private *dev_priv,
			       enum forcewake_domains domains);

	uint8_t  (*mmio_readb)(struct drm_i915_private *dev_priv,
			       i915_reg_t r, bool trace);
	uint16_t (*mmio_readw)(struct drm_i915_private *dev_priv,
			       i915_reg_t r, bool trace);
	uint32_t (*mmio_readl)(struct drm_i915_private *dev_priv,
			       i915_reg_t r, bool trace);
	uint64_t (*mmio_readq)(struct drm_i915_private *dev_priv,
			       i915_reg_t r, bool trace);

	void (*mmio_writeb)(struct drm_i915_private *dev_priv,
			    i915_reg_t r, uint8_t val, bool trace);
	void (*mmio_writew)(struct drm_i915_private *dev_priv,
			    i915_reg_t r, uint16_t val, bool trace);
	void (*mmio_writel)(struct drm_i915_private *dev_priv,
			    i915_reg_t r, uint32_t val, bool trace);
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

	struct notifier_block pmic_bus_access_nb;
	struct intel_uncore_funcs funcs;

	unsigned int fifo_count;

	enum forcewake_domains fw_domains;
	enum forcewake_domains fw_domains_active;

	u32 fw_set;
	u32 fw_clear;
	u32 fw_reset;

	struct intel_uncore_forcewake_domain {
		enum forcewake_domain_id id;
		enum forcewake_domains mask;
		unsigned int wake_count;
		bool active;
		struct hrtimer timer;
		i915_reg_t reg_set;
		i915_reg_t reg_ack;
	} fw_domain[FW_DOMAIN_ID_COUNT];

	struct {
		unsigned int count;

		int saved_mmio_check;
		int saved_mmio_debug;
	} user_forcewake;

	int unclaimed_mmio_check;
};

/* Iterate over initialised fw domains */
#define for_each_fw_domain_masked(domain__, mask__, dev_priv__, tmp__) \
	for (tmp__ = (mask__); \
	     tmp__ ? (domain__ = &(dev_priv__)->uncore.fw_domain[__mask_next_bit(tmp__)]), 1 : 0;)

#define for_each_fw_domain(domain__, dev_priv__, tmp__) \
	for_each_fw_domain_masked(domain__, (dev_priv__)->uncore.fw_domains, dev_priv__, tmp__)


void intel_uncore_sanitize(struct drm_i915_private *dev_priv);
void intel_uncore_init(struct drm_i915_private *dev_priv);
bool intel_uncore_unclaimed_mmio(struct drm_i915_private *dev_priv);
bool intel_uncore_arm_unclaimed_mmio_detection(struct drm_i915_private *dev_priv);
void intel_uncore_fini(struct drm_i915_private *dev_priv);
void intel_uncore_suspend(struct drm_i915_private *dev_priv);
void intel_uncore_resume_early(struct drm_i915_private *dev_priv);
void intel_uncore_runtime_resume(struct drm_i915_private *dev_priv);

u64 intel_uncore_edram_size(struct drm_i915_private *dev_priv);
void assert_forcewakes_inactive(struct drm_i915_private *dev_priv);
void assert_forcewakes_active(struct drm_i915_private *dev_priv,
			      enum forcewake_domains fw_domains);
const char *intel_uncore_forcewake_domain_to_str(const enum forcewake_domain_id id);

enum forcewake_domains
intel_uncore_forcewake_for_reg(struct drm_i915_private *dev_priv,
			       i915_reg_t reg, unsigned int op);
#define FW_REG_READ  (1)
#define FW_REG_WRITE (2)

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

void intel_uncore_forcewake_user_get(struct drm_i915_private *dev_priv);
void intel_uncore_forcewake_user_put(struct drm_i915_private *dev_priv);

int intel_wait_for_register(struct drm_i915_private *dev_priv,
			    i915_reg_t reg,
			    u32 mask,
			    u32 value,
			    unsigned int timeout_ms);
int __intel_wait_for_register_fw(struct drm_i915_private *dev_priv,
				 i915_reg_t reg,
				 u32 mask,
				 u32 value,
				 unsigned int fast_timeout_us,
				 unsigned int slow_timeout_ms,
				 u32 *out_value);
static inline
int intel_wait_for_register_fw(struct drm_i915_private *dev_priv,
			       i915_reg_t reg,
			       u32 mask,
			       u32 value,
			       unsigned int timeout_ms)
{
	return __intel_wait_for_register_fw(dev_priv, reg, mask, value,
					    2, timeout_ms, NULL);
}

#endif /* !__INTEL_UNCORE_H__ */
