/*
 * Copyright © 2013 Intel Corporation
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
 */

#include "i915_drv.h"
#include "intel_drv.h"

#define FORCEWAKE_ACK_TIMEOUT_MS 2

#define __raw_i915_read8(dev_priv__, reg__) readb((dev_priv__)->regs + (reg__))
#define __raw_i915_write8(dev_priv__, reg__, val__) writeb(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read16(dev_priv__, reg__) readw((dev_priv__)->regs + (reg__))
#define __raw_i915_write16(dev_priv__, reg__, val__) writew(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read32(dev_priv__, reg__) readl((dev_priv__)->regs + (reg__))
#define __raw_i915_write32(dev_priv__, reg__, val__) writel(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read64(dev_priv__, reg__) readq((dev_priv__)->regs + (reg__))
#define __raw_i915_write64(dev_priv__, reg__, val__) writeq(val__, (dev_priv__)->regs + (reg__))

#define __raw_posting_read(dev_priv__, reg__) (void)__raw_i915_read32(dev_priv__, reg__)

static void
assert_device_not_suspended(struct drm_i915_private *dev_priv)
{
	WARN_ONCE(HAS_RUNTIME_PM(dev_priv->dev) && dev_priv->pm.suspended,
		  "Device suspended\n");
}

static void __gen6_gt_wait_for_thread_c0(struct drm_i915_private *dev_priv)
{
	/* w/a for a sporadic read returning 0 by waiting for the GT
	 * thread to wake up.
	 */
	if (wait_for_atomic_us((__raw_i915_read32(dev_priv, GEN6_GT_THREAD_STATUS_REG) &
				GEN6_GT_THREAD_STATUS_CORE_MASK) == 0, 500))
		DRM_ERROR("GT thread status wait timed out\n");
}

static void __gen6_gt_force_wake_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE, 0);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);
}

static void __gen6_gt_force_wake_get(struct drm_i915_private *dev_priv,
							int fw_engine)
{
	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK) & 1) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake old ack to clear.\n");

	__raw_i915_write32(dev_priv, FORCEWAKE, 1);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);

	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK) & 1),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake to ack request.\n");

	/* WaRsForcewakeWaitTC0:snb */
	__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void __gen7_gt_force_wake_mt_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_MT, _MASKED_BIT_DISABLE(0xffff));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);
}

static void __gen7_gt_force_wake_mt_get(struct drm_i915_private *dev_priv,
							int fw_engine)
{
	u32 forcewake_ack;

	if (IS_HASWELL(dev_priv->dev) || IS_BROADWELL(dev_priv->dev))
		forcewake_ack = FORCEWAKE_ACK_HSW;
	else
		forcewake_ack = FORCEWAKE_MT_ACK;

	if (wait_for_atomic((__raw_i915_read32(dev_priv, forcewake_ack) & FORCEWAKE_KERNEL) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake old ack to clear.\n");

	__raw_i915_write32(dev_priv, FORCEWAKE_MT,
			   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);

	if (wait_for_atomic((__raw_i915_read32(dev_priv, forcewake_ack) & FORCEWAKE_KERNEL),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake to ack request.\n");

	/* WaRsForcewakeWaitTC0:ivb,hsw */
	__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void gen6_gt_check_fifodbg(struct drm_i915_private *dev_priv)
{
	u32 gtfifodbg;

	gtfifodbg = __raw_i915_read32(dev_priv, GTFIFODBG);
	if (WARN(gtfifodbg, "GT wake FIFO error 0x%x\n", gtfifodbg))
		__raw_i915_write32(dev_priv, GTFIFODBG, gtfifodbg);
}

static void __gen6_gt_force_wake_put(struct drm_i915_private *dev_priv,
							int fw_engine)
{
	__raw_i915_write32(dev_priv, FORCEWAKE, 0);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);
	gen6_gt_check_fifodbg(dev_priv);
}

static void __gen7_gt_force_wake_mt_put(struct drm_i915_private *dev_priv,
							int fw_engine)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_MT,
			   _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);

	if (IS_GEN7(dev_priv->dev))
		gen6_gt_check_fifodbg(dev_priv);
}

static int __gen6_gt_wait_for_fifo(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	/* On VLV, FIFO will be shared by both SW and HW.
	 * So, we need to read the FREE_ENTRIES everytime */
	if (IS_VALLEYVIEW(dev_priv->dev))
		dev_priv->uncore.fifo_count =
			__raw_i915_read32(dev_priv, GTFIFOCTL) &
						GT_FIFO_FREE_ENTRIES_MASK;

	if (dev_priv->uncore.fifo_count < GT_FIFO_NUM_RESERVED_ENTRIES) {
		int loop = 500;
		u32 fifo = __raw_i915_read32(dev_priv, GTFIFOCTL) & GT_FIFO_FREE_ENTRIES_MASK;
		while (fifo <= GT_FIFO_NUM_RESERVED_ENTRIES && loop--) {
			udelay(10);
			fifo = __raw_i915_read32(dev_priv, GTFIFOCTL) & GT_FIFO_FREE_ENTRIES_MASK;
		}
		if (WARN_ON(loop < 0 && fifo <= GT_FIFO_NUM_RESERVED_ENTRIES))
			++ret;
		dev_priv->uncore.fifo_count = fifo;
	}
	dev_priv->uncore.fifo_count--;

	return ret;
}

static void vlv_force_wake_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
			   _MASKED_BIT_DISABLE(0xffff));
	__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_VLV,
			   _MASKED_BIT_DISABLE(0xffff));
	/* something from same cacheline, but !FORCEWAKE_VLV */
	__raw_posting_read(dev_priv, FORCEWAKE_ACK_VLV);
}

static void __vlv_force_wake_get(struct drm_i915_private *dev_priv,
						int fw_engine)
{
	/* Check for Render Engine */
	if (FORCEWAKE_RENDER & fw_engine) {
		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_VLV) &
						FORCEWAKE_KERNEL) == 0,
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: Render forcewake old ack to clear.\n");

		__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_VLV) &
						FORCEWAKE_KERNEL),
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: waiting for Render to ack.\n");
	}

	/* Check for Media Engine */
	if (FORCEWAKE_MEDIA & fw_engine) {
		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_MEDIA_VLV) &
						FORCEWAKE_KERNEL) == 0,
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: Media forcewake old ack to clear.\n");

		__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_VLV,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_MEDIA_VLV) &
						FORCEWAKE_KERNEL),
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: waiting for media to ack.\n");
	}
}

static void __vlv_force_wake_put(struct drm_i915_private *dev_priv,
					int fw_engine)
{

	/* Check for Render Engine */
	if (FORCEWAKE_RENDER & fw_engine)
		__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
					_MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));


	/* Check for Media Engine */
	if (FORCEWAKE_MEDIA & fw_engine)
		__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_VLV,
				_MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));

	/* something from same cacheline, but !FORCEWAKE_VLV */
	__raw_posting_read(dev_priv, FORCEWAKE_ACK_VLV);
	if (!IS_CHERRYVIEW(dev_priv->dev))
		gen6_gt_check_fifodbg(dev_priv);
}

static void vlv_force_wake_get(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (fw_engine & FORCEWAKE_RENDER &&
	    dev_priv->uncore.fw_rendercount++ != 0)
		fw_engine &= ~FORCEWAKE_RENDER;
	if (fw_engine & FORCEWAKE_MEDIA &&
	    dev_priv->uncore.fw_mediacount++ != 0)
		fw_engine &= ~FORCEWAKE_MEDIA;

	if (fw_engine)
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fw_engine);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void vlv_force_wake_put(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (fw_engine & FORCEWAKE_RENDER) {
		WARN_ON(!dev_priv->uncore.fw_rendercount);
		if (--dev_priv->uncore.fw_rendercount != 0)
			fw_engine &= ~FORCEWAKE_RENDER;
	}

	if (fw_engine & FORCEWAKE_MEDIA) {
		WARN_ON(!dev_priv->uncore.fw_mediacount);
		if (--dev_priv->uncore.fw_mediacount != 0)
			fw_engine &= ~FORCEWAKE_MEDIA;
	}

	if (fw_engine)
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fw_engine);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void __gen9_gt_force_wake_mt_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_RENDER_GEN9,
			_MASKED_BIT_DISABLE(0xffff));

	__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_GEN9,
			_MASKED_BIT_DISABLE(0xffff));

	__raw_i915_write32(dev_priv, FORCEWAKE_BLITTER_GEN9,
			_MASKED_BIT_DISABLE(0xffff));
}

static void
__gen9_force_wake_get(struct drm_i915_private *dev_priv, int fw_engine)
{
	/* Check for Render Engine */
	if (FORCEWAKE_RENDER & fw_engine) {
		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_RENDER_GEN9) &
						FORCEWAKE_KERNEL) == 0,
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: Render forcewake old ack to clear.\n");

		__raw_i915_write32(dev_priv, FORCEWAKE_RENDER_GEN9,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_RENDER_GEN9) &
						FORCEWAKE_KERNEL),
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: waiting for Render to ack.\n");
	}

	/* Check for Media Engine */
	if (FORCEWAKE_MEDIA & fw_engine) {
		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_MEDIA_GEN9) &
						FORCEWAKE_KERNEL) == 0,
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: Media forcewake old ack to clear.\n");

		__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_GEN9,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_MEDIA_GEN9) &
						FORCEWAKE_KERNEL),
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: waiting for Media to ack.\n");
	}

	/* Check for Blitter Engine */
	if (FORCEWAKE_BLITTER & fw_engine) {
		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_BLITTER_GEN9) &
						FORCEWAKE_KERNEL) == 0,
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: Blitter forcewake old ack to clear.\n");

		__raw_i915_write32(dev_priv, FORCEWAKE_BLITTER_GEN9,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

		if (wait_for_atomic((__raw_i915_read32(dev_priv,
						FORCEWAKE_ACK_BLITTER_GEN9) &
						FORCEWAKE_KERNEL),
					FORCEWAKE_ACK_TIMEOUT_MS))
			DRM_ERROR("Timed out: waiting for Blitter to ack.\n");
	}
}

static void
__gen9_force_wake_put(struct drm_i915_private *dev_priv, int fw_engine)
{
	/* Check for Render Engine */
	if (FORCEWAKE_RENDER & fw_engine)
		__raw_i915_write32(dev_priv, FORCEWAKE_RENDER_GEN9,
				_MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));

	/* Check for Media Engine */
	if (FORCEWAKE_MEDIA & fw_engine)
		__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_GEN9,
				_MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));

	/* Check for Blitter Engine */
	if (FORCEWAKE_BLITTER & fw_engine)
		__raw_i915_write32(dev_priv, FORCEWAKE_BLITTER_GEN9,
				_MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));
}

static void
gen9_force_wake_get(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (FORCEWAKE_RENDER & fw_engine) {
		if (dev_priv->uncore.fw_rendercount++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							FORCEWAKE_RENDER);
	}

	if (FORCEWAKE_MEDIA & fw_engine) {
		if (dev_priv->uncore.fw_mediacount++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							FORCEWAKE_MEDIA);
	}

	if (FORCEWAKE_BLITTER & fw_engine) {
		if (dev_priv->uncore.fw_blittercount++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							FORCEWAKE_BLITTER);
	}

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
gen9_force_wake_put(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (FORCEWAKE_RENDER & fw_engine) {
		WARN_ON(dev_priv->uncore.fw_rendercount == 0);
		if (--dev_priv->uncore.fw_rendercount == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							FORCEWAKE_RENDER);
	}

	if (FORCEWAKE_MEDIA & fw_engine) {
		WARN_ON(dev_priv->uncore.fw_mediacount == 0);
		if (--dev_priv->uncore.fw_mediacount == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							FORCEWAKE_MEDIA);
	}

	if (FORCEWAKE_BLITTER & fw_engine) {
		WARN_ON(dev_priv->uncore.fw_blittercount == 0);
		if (--dev_priv->uncore.fw_blittercount == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							FORCEWAKE_BLITTER);
	}

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void gen6_force_wake_timer(unsigned long arg)
{
	struct drm_i915_private *dev_priv = (void *)arg;
	unsigned long irqflags;

	assert_device_not_suspended(dev_priv);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	WARN_ON(!dev_priv->uncore.forcewake_count);

	if (--dev_priv->uncore.forcewake_count == 0)
		dev_priv->uncore.funcs.force_wake_put(dev_priv, FORCEWAKE_ALL);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	intel_runtime_pm_put(dev_priv);
}

void intel_uncore_forcewake_reset(struct drm_device *dev, bool restore)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (del_timer_sync(&dev_priv->uncore.force_wake_timer))
		gen6_force_wake_timer((unsigned long)dev_priv);

	/* Hold uncore.lock across reset to prevent any register access
	 * with forcewake not set correctly
	 */
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (IS_VALLEYVIEW(dev))
		vlv_force_wake_reset(dev_priv);
	else if (IS_GEN6(dev) || IS_GEN7(dev))
		__gen6_gt_force_wake_reset(dev_priv);

	if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev) || IS_BROADWELL(dev))
		__gen7_gt_force_wake_mt_reset(dev_priv);

	if (IS_GEN9(dev))
		__gen9_gt_force_wake_mt_reset(dev_priv);

	if (restore) { /* If reset with a user forcewake, try to restore */
		unsigned fw = 0;

		if (IS_VALLEYVIEW(dev)) {
			if (dev_priv->uncore.fw_rendercount)
				fw |= FORCEWAKE_RENDER;

			if (dev_priv->uncore.fw_mediacount)
				fw |= FORCEWAKE_MEDIA;
		} else if (IS_GEN9(dev)) {
			if (dev_priv->uncore.fw_rendercount)
				fw |= FORCEWAKE_RENDER;

			if (dev_priv->uncore.fw_mediacount)
				fw |= FORCEWAKE_MEDIA;

			if (dev_priv->uncore.fw_blittercount)
				fw |= FORCEWAKE_BLITTER;
		} else {
			if (dev_priv->uncore.forcewake_count)
				fw = FORCEWAKE_ALL;
		}

		if (fw)
			dev_priv->uncore.funcs.force_wake_get(dev_priv, fw);

		if (IS_GEN6(dev) || IS_GEN7(dev))
			dev_priv->uncore.fifo_count =
				__raw_i915_read32(dev_priv, GTFIFOCTL) &
				GT_FIFO_FREE_ENTRIES_MASK;
	}

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void __intel_uncore_early_sanitize(struct drm_device *dev,
					  bool restore_forcewake)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_FPGA_DBG_UNCLAIMED(dev))
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);

	if ((IS_HASWELL(dev) || IS_BROADWELL(dev)) &&
	    (__raw_i915_read32(dev_priv, HSW_EDRAM_PRESENT) == 1)) {
		/* The docs do not explain exactly how the calculation can be
		 * made. It is somewhat guessable, but for now, it's always
		 * 128MB.
		 * NB: We can't write IDICR yet because we do not have gt funcs
		 * set up */
		dev_priv->ellc_size = 128;
		DRM_INFO("Found %zuMB of eLLC\n", dev_priv->ellc_size);
	}

	/* clear out old GT FIFO errors */
	if (IS_GEN6(dev) || IS_GEN7(dev))
		__raw_i915_write32(dev_priv, GTFIFODBG,
				   __raw_i915_read32(dev_priv, GTFIFODBG));

	intel_uncore_forcewake_reset(dev, restore_forcewake);
}

void intel_uncore_early_sanitize(struct drm_device *dev, bool restore_forcewake)
{
	__intel_uncore_early_sanitize(dev, restore_forcewake);
	i915_check_and_clear_faults(dev);
}

void intel_uncore_sanitize(struct drm_device *dev)
{
	/* BIOS often leaves RC6 enabled, but disable it for hw init */
	intel_disable_gt_powersave(dev);
}

/*
 * Generally this is called implicitly by the register read function. However,
 * if some sequence requires the GT to not power down then this function should
 * be called at the beginning of the sequence followed by a call to
 * gen6_gt_force_wake_put() at the end of the sequence.
 */
void gen6_gt_force_wake_get(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	intel_runtime_pm_get(dev_priv);

	/* Redirect to Gen9 specific routine */
	if (IS_GEN9(dev_priv->dev))
		return gen9_force_wake_get(dev_priv, fw_engine);

	/* Redirect to VLV specific routine */
	if (IS_VALLEYVIEW(dev_priv->dev))
		return vlv_force_wake_get(dev_priv, fw_engine);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	if (dev_priv->uncore.forcewake_count++ == 0)
		dev_priv->uncore.funcs.force_wake_get(dev_priv, FORCEWAKE_ALL);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/*
 * see gen6_gt_force_wake_get()
 */
void gen6_gt_force_wake_put(struct drm_i915_private *dev_priv, int fw_engine)
{
	unsigned long irqflags;
	bool delayed = false;

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	/* Redirect to Gen9 specific routine */
	if (IS_GEN9(dev_priv->dev)) {
		gen9_force_wake_put(dev_priv, fw_engine);
		goto out;
	}

	/* Redirect to VLV specific routine */
	if (IS_VALLEYVIEW(dev_priv->dev)) {
		vlv_force_wake_put(dev_priv, fw_engine);
		goto out;
	}


	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	WARN_ON(!dev_priv->uncore.forcewake_count);

	if (--dev_priv->uncore.forcewake_count == 0) {
		dev_priv->uncore.forcewake_count++;
		delayed = true;
		mod_timer_pinned(&dev_priv->uncore.force_wake_timer,
				 jiffies + 1);
	}
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

out:
	if (!delayed)
		intel_runtime_pm_put(dev_priv);
}

void assert_force_wake_inactive(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	WARN_ON(dev_priv->uncore.forcewake_count > 0);
}

/* We give fast paths for the really cool registers */
#define NEEDS_FORCE_WAKE(dev_priv, reg) \
	 ((reg) < 0x40000 && (reg) != FORCEWAKE)

#define REG_RANGE(reg, start, end) ((reg) >= (start) && (reg) < (end))

#define FORCEWAKE_VLV_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x4000) || \
	 REG_RANGE((reg), 0x5000, 0x8000) || \
	 REG_RANGE((reg), 0xB000, 0x12000) || \
	 REG_RANGE((reg), 0x2E000, 0x30000))

#define FORCEWAKE_VLV_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x22000, 0x24000) || \
	 REG_RANGE((reg), 0x30000, 0x40000))

#define FORCEWAKE_CHV_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x4000) || \
	 REG_RANGE((reg), 0x5200, 0x8000) || \
	 REG_RANGE((reg), 0x8300, 0x8500) || \
	 REG_RANGE((reg), 0xB000, 0xB480) || \
	 REG_RANGE((reg), 0xE000, 0xE800))

#define FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x8800, 0x8900) || \
	 REG_RANGE((reg), 0xD000, 0xD800) || \
	 REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x1A000, 0x1C000) || \
	 REG_RANGE((reg), 0x1E800, 0x1EA00) || \
	 REG_RANGE((reg), 0x30000, 0x38000))

#define FORCEWAKE_CHV_COMMON_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x4000, 0x5000) || \
	 REG_RANGE((reg), 0x8000, 0x8300) || \
	 REG_RANGE((reg), 0x8500, 0x8600) || \
	 REG_RANGE((reg), 0x9000, 0xB000) || \
	 REG_RANGE((reg), 0xF000, 0x10000))

#define FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg) \
	REG_RANGE((reg), 0xB00,  0x2000)

#define FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x2700) || \
	 REG_RANGE((reg), 0x3000, 0x4000) || \
	 REG_RANGE((reg), 0x5200, 0x8000) || \
	 REG_RANGE((reg), 0x8140, 0x8160) || \
	 REG_RANGE((reg), 0x8300, 0x8500) || \
	 REG_RANGE((reg), 0x8C00, 0x8D00) || \
	 REG_RANGE((reg), 0xB000, 0xB480) || \
	 REG_RANGE((reg), 0xE000, 0xE900) || \
	 REG_RANGE((reg), 0x24400, 0x24800))

#define FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x8130, 0x8140) || \
	 REG_RANGE((reg), 0x8800, 0x8A00) || \
	 REG_RANGE((reg), 0xD000, 0xD800) || \
	 REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x1A000, 0x1EA00) || \
	 REG_RANGE((reg), 0x30000, 0x40000))

#define FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg) \
	REG_RANGE((reg), 0x9400, 0x9800)

#define FORCEWAKE_GEN9_BLITTER_RANGE_OFFSET(reg) \
	((reg) < 0x40000 &&\
	 !FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg))

static void
ilk_dummy_write(struct drm_i915_private *dev_priv)
{
	/* WaIssueDummyWriteToWakeupFromRC6:ilk Issue a dummy write to wake up
	 * the chip from rc6 before touching it for real. MI_MODE is masked,
	 * hence harmless to write 0 into. */
	__raw_i915_write32(dev_priv, MI_MODE, 0);
}

static void
hsw_unclaimed_reg_debug(struct drm_i915_private *dev_priv, u32 reg, bool read,
			bool before)
{
	const char *op = read ? "reading" : "writing to";
	const char *when = before ? "before" : "after";

	if (!i915.mmio_debug)
		return;

	if (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM) {
		WARN(1, "Unclaimed register detected %s %s register 0x%x\n",
		     when, op, reg);
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}

static void
hsw_unclaimed_reg_detect(struct drm_i915_private *dev_priv)
{
	if (i915.mmio_debug)
		return;

	if (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM) {
		DRM_ERROR("Unclaimed register detected. Please use the i915.mmio_debug=1 to debug this problem.");
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}

#define REG_READ_HEADER(x) \
	unsigned long irqflags; \
	u##x val = 0; \
	assert_device_not_suspended(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define REG_READ_FOOTER \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

#define __gen4_read(x) \
static u##x \
gen4_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	val = __raw_i915_read##x(dev_priv, reg); \
	REG_READ_FOOTER; \
}

#define __gen5_read(x) \
static u##x \
gen5_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	ilk_dummy_write(dev_priv); \
	val = __raw_i915_read##x(dev_priv, reg); \
	REG_READ_FOOTER; \
}

#define __gen6_read(x) \
static u##x \
gen6_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	hsw_unclaimed_reg_debug(dev_priv, reg, true, true); \
	if (dev_priv->uncore.forcewake_count == 0 && \
	    NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		dev_priv->uncore.funcs.force_wake_get(dev_priv, \
						      FORCEWAKE_ALL); \
		val = __raw_i915_read##x(dev_priv, reg); \
		dev_priv->uncore.funcs.force_wake_put(dev_priv, \
						      FORCEWAKE_ALL); \
	} else { \
		val = __raw_i915_read##x(dev_priv, reg); \
	} \
	hsw_unclaimed_reg_debug(dev_priv, reg, true, false); \
	REG_READ_FOOTER; \
}

#define __vlv_read(x) \
static u##x \
vlv_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	unsigned fwengine = 0; \
	REG_READ_HEADER(x); \
	if (FORCEWAKE_VLV_RENDER_RANGE_OFFSET(reg)) { \
		if (dev_priv->uncore.fw_rendercount == 0) \
			fwengine = FORCEWAKE_RENDER; \
	} else if (FORCEWAKE_VLV_MEDIA_RANGE_OFFSET(reg)) { \
		if (dev_priv->uncore.fw_mediacount == 0) \
			fwengine = FORCEWAKE_MEDIA; \
	}  \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fwengine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fwengine); \
	REG_READ_FOOTER; \
}

#define __chv_read(x) \
static u##x \
chv_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	unsigned fwengine = 0; \
	REG_READ_HEADER(x); \
	if (FORCEWAKE_CHV_RENDER_RANGE_OFFSET(reg)) { \
		if (dev_priv->uncore.fw_rendercount == 0) \
			fwengine = FORCEWAKE_RENDER; \
	} else if (FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(reg)) { \
		if (dev_priv->uncore.fw_mediacount == 0) \
			fwengine = FORCEWAKE_MEDIA; \
	} else if (FORCEWAKE_CHV_COMMON_RANGE_OFFSET(reg)) { \
		if (dev_priv->uncore.fw_rendercount == 0) \
			fwengine |= FORCEWAKE_RENDER; \
		if (dev_priv->uncore.fw_mediacount == 0) \
			fwengine |= FORCEWAKE_MEDIA; \
	} \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fwengine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fwengine); \
	REG_READ_FOOTER; \
}

#define SKL_NEEDS_FORCE_WAKE(dev_priv, reg)	\
	 ((reg) < 0x40000 && !FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg))

#define __gen9_read(x) \
static u##x \
gen9_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	if (!SKL_NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		val = __raw_i915_read##x(dev_priv, reg); \
	} else { \
		unsigned fwengine = 0; \
		if (FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine = FORCEWAKE_RENDER; \
		} else if (FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine = FORCEWAKE_MEDIA; \
		} else if (FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine |= FORCEWAKE_RENDER; \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine |= FORCEWAKE_MEDIA; \
		} else { \
			if (dev_priv->uncore.fw_blittercount == 0) \
				fwengine = FORCEWAKE_BLITTER; \
		} \
		if (fwengine) \
			dev_priv->uncore.funcs.force_wake_get(dev_priv, fwengine); \
		val = __raw_i915_read##x(dev_priv, reg); \
		if (fwengine) \
			dev_priv->uncore.funcs.force_wake_put(dev_priv, fwengine); \
	} \
	REG_READ_FOOTER; \
}

__gen9_read(8)
__gen9_read(16)
__gen9_read(32)
__gen9_read(64)
__chv_read(8)
__chv_read(16)
__chv_read(32)
__chv_read(64)
__vlv_read(8)
__vlv_read(16)
__vlv_read(32)
__vlv_read(64)
__gen6_read(8)
__gen6_read(16)
__gen6_read(32)
__gen6_read(64)
__gen5_read(8)
__gen5_read(16)
__gen5_read(32)
__gen5_read(64)
__gen4_read(8)
__gen4_read(16)
__gen4_read(32)
__gen4_read(64)

#undef __gen9_read
#undef __chv_read
#undef __vlv_read
#undef __gen6_read
#undef __gen5_read
#undef __gen4_read
#undef REG_READ_FOOTER
#undef REG_READ_HEADER

#define REG_WRITE_HEADER \
	unsigned long irqflags; \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	assert_device_not_suspended(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define REG_WRITE_FOOTER \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags)

#define __gen4_write(x) \
static void \
gen4_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	REG_WRITE_HEADER; \
	__raw_i915_write##x(dev_priv, reg, val); \
	REG_WRITE_FOOTER; \
}

#define __gen5_write(x) \
static void \
gen5_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	REG_WRITE_HEADER; \
	ilk_dummy_write(dev_priv); \
	__raw_i915_write##x(dev_priv, reg, val); \
	REG_WRITE_FOOTER; \
}

#define __gen6_write(x) \
static void \
gen6_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	REG_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	REG_WRITE_FOOTER; \
}

#define __hsw_write(x) \
static void \
hsw_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	REG_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	hsw_unclaimed_reg_debug(dev_priv, reg, false, true); \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	hsw_unclaimed_reg_debug(dev_priv, reg, false, false); \
	hsw_unclaimed_reg_detect(dev_priv); \
	REG_WRITE_FOOTER; \
}

static const u32 gen8_shadowed_regs[] = {
	FORCEWAKE_MT,
	GEN6_RPNSWREQ,
	GEN6_RC_VIDEO_FREQ,
	RING_TAIL(RENDER_RING_BASE),
	RING_TAIL(GEN6_BSD_RING_BASE),
	RING_TAIL(VEBOX_RING_BASE),
	RING_TAIL(BLT_RING_BASE),
	/* TODO: Other registers are not yet used */
};

static bool is_gen8_shadowed(struct drm_i915_private *dev_priv, u32 reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gen8_shadowed_regs); i++)
		if (reg == gen8_shadowed_regs[i])
			return true;

	return false;
}

#define __gen8_write(x) \
static void \
gen8_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	REG_WRITE_HEADER; \
	hsw_unclaimed_reg_debug(dev_priv, reg, false, true); \
	if (reg < 0x40000 && !is_gen8_shadowed(dev_priv, reg)) { \
		if (dev_priv->uncore.forcewake_count == 0) \
			dev_priv->uncore.funcs.force_wake_get(dev_priv,	\
							      FORCEWAKE_ALL); \
		__raw_i915_write##x(dev_priv, reg, val); \
		if (dev_priv->uncore.forcewake_count == 0) \
			dev_priv->uncore.funcs.force_wake_put(dev_priv, \
							      FORCEWAKE_ALL); \
	} else { \
		__raw_i915_write##x(dev_priv, reg, val); \
	} \
	hsw_unclaimed_reg_debug(dev_priv, reg, false, false); \
	hsw_unclaimed_reg_detect(dev_priv); \
	REG_WRITE_FOOTER; \
}

#define __chv_write(x) \
static void \
chv_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	unsigned fwengine = 0; \
	bool shadowed = is_gen8_shadowed(dev_priv, reg); \
	REG_WRITE_HEADER; \
	if (!shadowed) { \
		if (FORCEWAKE_CHV_RENDER_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine = FORCEWAKE_RENDER; \
		} else if (FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine = FORCEWAKE_MEDIA; \
		} else if (FORCEWAKE_CHV_COMMON_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine |= FORCEWAKE_RENDER; \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine |= FORCEWAKE_MEDIA; \
		} \
	} \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fwengine); \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (fwengine) \
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fwengine); \
	REG_WRITE_FOOTER; \
}

static const u32 gen9_shadowed_regs[] = {
	RING_TAIL(RENDER_RING_BASE),
	RING_TAIL(GEN6_BSD_RING_BASE),
	RING_TAIL(VEBOX_RING_BASE),
	RING_TAIL(BLT_RING_BASE),
	FORCEWAKE_BLITTER_GEN9,
	FORCEWAKE_RENDER_GEN9,
	FORCEWAKE_MEDIA_GEN9,
	GEN6_RPNSWREQ,
	GEN6_RC_VIDEO_FREQ,
	/* TODO: Other registers are not yet used */
};

static bool is_gen9_shadowed(struct drm_i915_private *dev_priv, u32 reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gen9_shadowed_regs); i++)
		if (reg == gen9_shadowed_regs[i])
			return true;

	return false;
}

#define __gen9_write(x) \
static void \
gen9_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, \
		bool trace) { \
	REG_WRITE_HEADER; \
	if (!SKL_NEEDS_FORCE_WAKE((dev_priv), (reg)) || \
			is_gen9_shadowed(dev_priv, reg)) { \
		__raw_i915_write##x(dev_priv, reg, val); \
	} else { \
		unsigned fwengine = 0; \
		if (FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine = FORCEWAKE_RENDER; \
		} else if (FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine = FORCEWAKE_MEDIA; \
		} else if (FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg)) { \
			if (dev_priv->uncore.fw_rendercount == 0) \
				fwengine |= FORCEWAKE_RENDER; \
			if (dev_priv->uncore.fw_mediacount == 0) \
				fwengine |= FORCEWAKE_MEDIA; \
		} else { \
			if (dev_priv->uncore.fw_blittercount == 0) \
				fwengine = FORCEWAKE_BLITTER; \
		} \
		if (fwengine) \
			dev_priv->uncore.funcs.force_wake_get(dev_priv, \
					fwengine); \
		__raw_i915_write##x(dev_priv, reg, val); \
		if (fwengine) \
			dev_priv->uncore.funcs.force_wake_put(dev_priv, \
					fwengine); \
	} \
	REG_WRITE_FOOTER; \
}

__gen9_write(8)
__gen9_write(16)
__gen9_write(32)
__gen9_write(64)
__chv_write(8)
__chv_write(16)
__chv_write(32)
__chv_write(64)
__gen8_write(8)
__gen8_write(16)
__gen8_write(32)
__gen8_write(64)
__hsw_write(8)
__hsw_write(16)
__hsw_write(32)
__hsw_write(64)
__gen6_write(8)
__gen6_write(16)
__gen6_write(32)
__gen6_write(64)
__gen5_write(8)
__gen5_write(16)
__gen5_write(32)
__gen5_write(64)
__gen4_write(8)
__gen4_write(16)
__gen4_write(32)
__gen4_write(64)

#undef __gen9_write
#undef __chv_write
#undef __gen8_write
#undef __hsw_write
#undef __gen6_write
#undef __gen5_write
#undef __gen4_write
#undef REG_WRITE_FOOTER
#undef REG_WRITE_HEADER

#define ASSIGN_WRITE_MMIO_VFUNCS(x) \
do { \
	dev_priv->uncore.funcs.mmio_writeb = x##_write8; \
	dev_priv->uncore.funcs.mmio_writew = x##_write16; \
	dev_priv->uncore.funcs.mmio_writel = x##_write32; \
	dev_priv->uncore.funcs.mmio_writeq = x##_write64; \
} while (0)

#define ASSIGN_READ_MMIO_VFUNCS(x) \
do { \
	dev_priv->uncore.funcs.mmio_readb = x##_read8; \
	dev_priv->uncore.funcs.mmio_readw = x##_read16; \
	dev_priv->uncore.funcs.mmio_readl = x##_read32; \
	dev_priv->uncore.funcs.mmio_readq = x##_read64; \
} while (0)

void intel_uncore_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	setup_timer(&dev_priv->uncore.force_wake_timer,
		    gen6_force_wake_timer, (unsigned long)dev_priv);

	__intel_uncore_early_sanitize(dev, false);

	if (IS_GEN9(dev)) {
		dev_priv->uncore.funcs.force_wake_get = __gen9_force_wake_get;
		dev_priv->uncore.funcs.force_wake_put = __gen9_force_wake_put;
	} else if (IS_VALLEYVIEW(dev)) {
		dev_priv->uncore.funcs.force_wake_get = __vlv_force_wake_get;
		dev_priv->uncore.funcs.force_wake_put = __vlv_force_wake_put;
	} else if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
		dev_priv->uncore.funcs.force_wake_get = __gen7_gt_force_wake_mt_get;
		dev_priv->uncore.funcs.force_wake_put = __gen7_gt_force_wake_mt_put;
	} else if (IS_IVYBRIDGE(dev)) {
		u32 ecobus;

		/* IVB configs may use multi-threaded forcewake */

		/* A small trick here - if the bios hasn't configured
		 * MT forcewake, and if the device is in RC6, then
		 * force_wake_mt_get will not wake the device and the
		 * ECOBUS read will return zero. Which will be
		 * (correctly) interpreted by the test below as MT
		 * forcewake being disabled.
		 */
		mutex_lock(&dev->struct_mutex);
		__gen7_gt_force_wake_mt_get(dev_priv, FORCEWAKE_ALL);
		ecobus = __raw_i915_read32(dev_priv, ECOBUS);
		__gen7_gt_force_wake_mt_put(dev_priv, FORCEWAKE_ALL);
		mutex_unlock(&dev->struct_mutex);

		if (ecobus & FORCEWAKE_MT_ENABLE) {
			dev_priv->uncore.funcs.force_wake_get =
				__gen7_gt_force_wake_mt_get;
			dev_priv->uncore.funcs.force_wake_put =
				__gen7_gt_force_wake_mt_put;
		} else {
			DRM_INFO("No MT forcewake available on Ivybridge, this can result in issues\n");
			DRM_INFO("when using vblank-synced partial screen updates.\n");
			dev_priv->uncore.funcs.force_wake_get =
				__gen6_gt_force_wake_get;
			dev_priv->uncore.funcs.force_wake_put =
				__gen6_gt_force_wake_put;
		}
	} else if (IS_GEN6(dev)) {
		dev_priv->uncore.funcs.force_wake_get =
			__gen6_gt_force_wake_get;
		dev_priv->uncore.funcs.force_wake_put =
			__gen6_gt_force_wake_put;
	}

	switch (INTEL_INFO(dev)->gen) {
	default:
		MISSING_CASE(INTEL_INFO(dev)->gen);
		return;
	case 9:
		ASSIGN_WRITE_MMIO_VFUNCS(gen9);
		ASSIGN_READ_MMIO_VFUNCS(gen9);
		break;
	case 8:
		if (IS_CHERRYVIEW(dev)) {
			ASSIGN_WRITE_MMIO_VFUNCS(chv);
			ASSIGN_READ_MMIO_VFUNCS(chv);

		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(gen8);
			ASSIGN_READ_MMIO_VFUNCS(gen6);
		}
		break;
	case 7:
	case 6:
		if (IS_HASWELL(dev)) {
			ASSIGN_WRITE_MMIO_VFUNCS(hsw);
		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(gen6);
		}

		if (IS_VALLEYVIEW(dev)) {
			ASSIGN_READ_MMIO_VFUNCS(vlv);
		} else {
			ASSIGN_READ_MMIO_VFUNCS(gen6);
		}
		break;
	case 5:
		ASSIGN_WRITE_MMIO_VFUNCS(gen5);
		ASSIGN_READ_MMIO_VFUNCS(gen5);
		break;
	case 4:
	case 3:
	case 2:
		ASSIGN_WRITE_MMIO_VFUNCS(gen4);
		ASSIGN_READ_MMIO_VFUNCS(gen4);
		break;
	}

	i915_check_and_clear_faults(dev);
}
#undef ASSIGN_WRITE_MMIO_VFUNCS
#undef ASSIGN_READ_MMIO_VFUNCS

void intel_uncore_fini(struct drm_device *dev)
{
	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_uncore_sanitize(dev);
	intel_uncore_forcewake_reset(dev, false);
}

#define GEN_RANGE(l, h) GENMASK(h, l)

static const struct register_whitelist {
	uint64_t offset;
	uint32_t size;
	/* supported gens, 0x10 for 4, 0x30 for 4 and 5, etc. */
	uint32_t gen_bitmask;
} whitelist[] = {
	{ RING_TIMESTAMP(RENDER_RING_BASE), 8, GEN_RANGE(4, 9) },
};

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_reg_read *reg = data;
	struct register_whitelist const *entry = whitelist;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(whitelist); i++, entry++) {
		if (entry->offset == reg->offset &&
		    (1 << INTEL_INFO(dev)->gen & entry->gen_bitmask))
			break;
	}

	if (i == ARRAY_SIZE(whitelist))
		return -EINVAL;

	intel_runtime_pm_get(dev_priv);

	switch (entry->size) {
	case 8:
		reg->val = I915_READ64(reg->offset);
		break;
	case 4:
		reg->val = I915_READ(reg->offset);
		break;
	case 2:
		reg->val = I915_READ16(reg->offset);
		break;
	case 1:
		reg->val = I915_READ8(reg->offset);
		break;
	default:
		MISSING_CASE(entry->size);
		ret = -EINVAL;
		goto out;
	}

out:
	intel_runtime_pm_put(dev_priv);
	return ret;
}

int i915_get_reset_stats_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_reset_stats *args = data;
	struct i915_ctx_hang_stats *hs;
	struct intel_context *ctx;
	int ret;

	if (args->flags || args->pad)
		return -EINVAL;

	if (args->ctx_id == DEFAULT_CONTEXT_HANDLE && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	ctx = i915_gem_context_get(file->driver_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}
	hs = &ctx->hang_stats;

	if (capable(CAP_SYS_ADMIN))
		args->reset_count = i915_reset_count(&dev_priv->gpu_error);
	else
		args->reset_count = 0;

	args->batch_active = hs->batch_active;
	args->batch_pending = hs->batch_pending;

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_reset_complete(struct drm_device *dev)
{
	u8 gdrst;
	pci_read_config_byte(dev->pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_STATUS) == 0;
}

static int i915_do_reset(struct drm_device *dev)
{
	/* assert reset for at least 20 usec */
	pci_write_config_byte(dev->pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	udelay(20);
	pci_write_config_byte(dev->pdev, I915_GDRST, 0);

	return wait_for(i915_reset_complete(dev), 500);
}

static int g4x_reset_complete(struct drm_device *dev)
{
	u8 gdrst;
	pci_read_config_byte(dev->pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_ENABLE) == 0;
}

static int g33_do_reset(struct drm_device *dev)
{
	pci_write_config_byte(dev->pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	return wait_for(g4x_reset_complete(dev), 500);
}

static int g4x_do_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	pci_write_config_byte(dev->pdev, I915_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(dev), 500);
	if (ret)
		return ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	I915_WRITE(VDECCLK_GATE_D, I915_READ(VDECCLK_GATE_D) | VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	pci_write_config_byte(dev->pdev, I915_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(dev), 500);
	if (ret)
		return ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	I915_WRITE(VDECCLK_GATE_D, I915_READ(VDECCLK_GATE_D) & ~VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	pci_write_config_byte(dev->pdev, I915_GDRST, 0);

	return 0;
}

static int ironlake_do_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	I915_WRITE(MCHBAR_MIRROR_BASE + ILK_GDSR,
		   ILK_GRDOM_RENDER | ILK_GRDOM_RESET_ENABLE);
	ret = wait_for((I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR) &
			ILK_GRDOM_RESET_ENABLE) == 0, 500);
	if (ret)
		return ret;

	I915_WRITE(MCHBAR_MIRROR_BASE + ILK_GDSR,
		   ILK_GRDOM_MEDIA | ILK_GRDOM_RESET_ENABLE);
	ret = wait_for((I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR) &
			ILK_GRDOM_RESET_ENABLE) == 0, 500);
	if (ret)
		return ret;

	I915_WRITE(MCHBAR_MIRROR_BASE + ILK_GDSR, 0);

	return 0;
}

static int gen6_do_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int	ret;

	/* Reset the chip */

	/* GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	__raw_i915_write32(dev_priv, GEN6_GDRST, GEN6_GRDOM_FULL);

	/* Spin waiting for the device to ack the reset request */
	ret = wait_for((__raw_i915_read32(dev_priv, GEN6_GDRST) & GEN6_GRDOM_FULL) == 0, 500);

	intel_uncore_forcewake_reset(dev, true);

	return ret;
}

int intel_gpu_reset(struct drm_device *dev)
{
	if (INTEL_INFO(dev)->gen >= 6)
		return gen6_do_reset(dev);
	else if (IS_GEN5(dev))
		return ironlake_do_reset(dev);
	else if (IS_G4X(dev))
		return g4x_do_reset(dev);
	else if (IS_G33(dev))
		return g33_do_reset(dev);
	else if (INTEL_INFO(dev)->gen >= 3)
		return i915_do_reset(dev);
	else
		return -ENODEV;
}

void intel_uncore_check_errors(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_FPGA_DBG_UNCLAIMED(dev) &&
	    (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM)) {
		DRM_ERROR("Unclaimed register before interrupt\n");
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}
