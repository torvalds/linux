/*
 * Copyright © 2014 Intel Corporation
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
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include "i915_drv.h"
#include "intel_drv.h"

/**
 * DOC: fifo underrun handling
 *
 * The i915 driver checks for display fifo underruns using the interrupt signals
 * provided by the hardware. This is enabled by default and fairly useful to
 * debug display issues, especially watermark settings.
 *
 * If an underrun is detected this is logged into dmesg. To avoid flooding logs
 * and occupying the cpu underrun interrupts are disabled after the first
 * occurrence until the next modeset on a given pipe.
 *
 * Note that underrun detection on gmch platforms is a bit more ugly since there
 * is no interrupt (despite that the signalling bit is in the PIPESTAT pipe
 * interrupt register). Also on some other platforms underrun interrupts are
 * shared, which means that if we detect an underrun we need to disable underrun
 * reporting on all pipes.
 *
 * The code also supports underrun detection on the PCH transcoder.
 */

static bool ivb_can_enable_err_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc;
	enum pipe pipe;

	assert_spin_locked(&dev_priv->irq_lock);

	for_each_pipe(dev_priv, pipe) {
		crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);

		if (crtc->cpu_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static bool cpt_can_enable_serr_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;
	struct intel_crtc *crtc;

	assert_spin_locked(&dev_priv->irq_lock);

	for_each_pipe(dev_priv, pipe) {
		crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);

		if (crtc->pch_fifo_underrun_disabled)
			return false;
	}

	return true;
}

/**
 * i9xx_check_fifo_underruns - check for fifo underruns
 * @dev_priv: i915 device instance
 *
 * This function checks for fifo underruns on GMCH platforms. This needs to be
 * done manually on modeset to make sure that we catch all underruns since they
 * do not generate an interrupt by themselves on these platforms.
 */
void i9xx_check_fifo_underruns(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	spin_lock_irq(&dev_priv->irq_lock);

	for_each_intel_crtc(dev_priv->dev, crtc) {
		u32 reg = PIPESTAT(crtc->pipe);
		u32 pipestat;

		if (crtc->cpu_fifo_underrun_disabled)
			continue;

		pipestat = I915_READ(reg) & 0xffff0000;
		if ((pipestat & PIPE_FIFO_UNDERRUN_STATUS) == 0)
			continue;

		I915_WRITE(reg, pipestat | PIPE_FIFO_UNDERRUN_STATUS);
		POSTING_READ(reg);

		DRM_ERROR("pipe %c underrun\n", pipe_name(crtc->pipe));
	}

	spin_unlock_irq(&dev_priv->irq_lock);
}

static void i9xx_set_fifo_underrun_reporting(struct drm_device *dev,
					     enum pipe pipe,
					     bool enable, bool old)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg = PIPESTAT(pipe);
	u32 pipestat = I915_READ(reg) & 0xffff0000;

	assert_spin_locked(&dev_priv->irq_lock);

	if (enable) {
		I915_WRITE(reg, pipestat | PIPE_FIFO_UNDERRUN_STATUS);
		POSTING_READ(reg);
	} else {
		if (old && pipestat & PIPE_FIFO_UNDERRUN_STATUS)
			DRM_ERROR("pipe %c underrun\n", pipe_name(pipe));
	}
}

static void ironlake_set_fifo_underrun_reporting(struct drm_device *dev,
						 enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t bit = (pipe == PIPE_A) ? DE_PIPEA_FIFO_UNDERRUN :
					  DE_PIPEB_FIFO_UNDERRUN;

	if (enable)
		ironlake_enable_display_irq(dev_priv, bit);
	else
		ironlake_disable_display_irq(dev_priv, bit);
}

static void ivybridge_set_fifo_underrun_reporting(struct drm_device *dev,
						  enum pipe pipe,
						  bool enable, bool old)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (enable) {
		I915_WRITE(GEN7_ERR_INT, ERR_INT_FIFO_UNDERRUN(pipe));

		if (!ivb_can_enable_err_int(dev))
			return;

		ironlake_enable_display_irq(dev_priv, DE_ERR_INT_IVB);
	} else {
		ironlake_disable_display_irq(dev_priv, DE_ERR_INT_IVB);

		if (old &&
		    I915_READ(GEN7_ERR_INT) & ERR_INT_FIFO_UNDERRUN(pipe)) {
			DRM_ERROR("uncleared fifo underrun on pipe %c\n",
				  pipe_name(pipe));
		}
	}
}

static void broadwell_set_fifo_underrun_reporting(struct drm_device *dev,
						  enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	assert_spin_locked(&dev_priv->irq_lock);

	if (enable)
		dev_priv->de_irq_mask[pipe] &= ~GEN8_PIPE_FIFO_UNDERRUN;
	else
		dev_priv->de_irq_mask[pipe] |= GEN8_PIPE_FIFO_UNDERRUN;
	I915_WRITE(GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
	POSTING_READ(GEN8_DE_PIPE_IMR(pipe));
}

static void ibx_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum transcoder pch_transcoder,
					    bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t bit = (pch_transcoder == TRANSCODER_A) ?
		       SDE_TRANSA_FIFO_UNDER : SDE_TRANSB_FIFO_UNDER;

	if (enable)
		ibx_enable_display_interrupt(dev_priv, bit);
	else
		ibx_disable_display_interrupt(dev_priv, bit);
}

static void cpt_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum transcoder pch_transcoder,
					    bool enable, bool old)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (enable) {
		I915_WRITE(SERR_INT,
			   SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));

		if (!cpt_can_enable_serr_int(dev))
			return;

		ibx_enable_display_interrupt(dev_priv, SDE_ERROR_CPT);
	} else {
		ibx_disable_display_interrupt(dev_priv, SDE_ERROR_CPT);

		if (old && I915_READ(SERR_INT) &
		    SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder)) {
			DRM_ERROR("uncleared pch fifo underrun on pch transcoder %c\n",
				  transcoder_name(pch_transcoder));
		}
	}
}

static bool __intel_set_cpu_fifo_underrun_reporting(struct drm_device *dev,
						    enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	bool old;

	assert_spin_locked(&dev_priv->irq_lock);

	old = !intel_crtc->cpu_fifo_underrun_disabled;
	intel_crtc->cpu_fifo_underrun_disabled = !enable;

	if (HAS_GMCH_DISPLAY(dev))
		i9xx_set_fifo_underrun_reporting(dev, pipe, enable, old);
	else if (IS_GEN5(dev) || IS_GEN6(dev))
		ironlake_set_fifo_underrun_reporting(dev, pipe, enable);
	else if (IS_GEN7(dev))
		ivybridge_set_fifo_underrun_reporting(dev, pipe, enable, old);
	else if (IS_GEN8(dev) || IS_GEN9(dev))
		broadwell_set_fifo_underrun_reporting(dev, pipe, enable);

	return old;
}

/**
 * intel_set_cpu_fifo_underrun_reporting - set cpu fifo underrrun reporting state
 * @dev_priv: i915 device instance
 * @pipe: (CPU) pipe to set state for
 * @enable: whether underruns should be reported or not
 *
 * This function sets the fifo underrun state for @pipe. It is used in the
 * modeset code to avoid false positives since on many platforms underruns are
 * expected when disabling or enabling the pipe.
 *
 * Notice that on some platforms disabling underrun reports for one pipe
 * disables for all due to shared interrupts. Actual reporting is still per-pipe
 * though.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_cpu_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pipe, bool enable)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	ret = __intel_set_cpu_fifo_underrun_reporting(dev_priv->dev, pipe,
						      enable);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return ret;
}

static bool
__cpu_fifo_underrun_reporting_enabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	return !intel_crtc->cpu_fifo_underrun_disabled;
}

/**
 * intel_set_pch_fifo_underrun_reporting - set PCH fifo underrun reporting state
 * @dev_priv: i915 device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 * @enable: whether underruns should be reported or not
 *
 * This function makes us disable or enable PCH fifo underruns for a specific
 * PCH transcoder. Notice that on some PCHs (e.g. CPT/PPT), disabling FIFO
 * underrun reporting for one transcoder may also disable all the other PCH
 * error interruts for the other transcoders, due to the fact that there's just
 * one interrupt mask/enable bit for all the transcoders.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_pch_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum transcoder pch_transcoder,
					   bool enable)
{
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pch_transcoder];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	unsigned long flags;
	bool old;

	/*
	 * NOTE: Pre-LPT has a fixed cpu pipe -> pch transcoder mapping, but LPT
	 * has only one pch transcoder A that all pipes can use. To avoid racy
	 * pch transcoder -> pipe lookups from interrupt code simply store the
	 * underrun statistics in crtc A. Since we never expose this anywhere
	 * nor use it outside of the fifo underrun code here using the "wrong"
	 * crtc on LPT won't cause issues.
	 */

	spin_lock_irqsave(&dev_priv->irq_lock, flags);

	old = !intel_crtc->pch_fifo_underrun_disabled;
	intel_crtc->pch_fifo_underrun_disabled = !enable;

	if (HAS_PCH_IBX(dev_priv->dev))
		ibx_set_fifo_underrun_reporting(dev_priv->dev, pch_transcoder,
						enable);
	else
		cpt_set_fifo_underrun_reporting(dev_priv->dev, pch_transcoder,
						enable, old);

	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
	return old;
}

/**
 * intel_pch_fifo_underrun_irq_handler - handle PCH fifo underrun interrupt
 * @dev_priv: i915 device instance
 * @pipe: (CPU) pipe to set state for
 *
 * This handles a CPU fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_cpu_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	/* GMCH can't disable fifo underruns, filter them. */
	if (HAS_GMCH_DISPLAY(dev_priv->dev) &&
	    !__cpu_fifo_underrun_reporting_enabled(dev_priv, pipe))
		return;

	if (intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false))
		DRM_ERROR("CPU pipe %c FIFO underrun\n",
			  pipe_name(pipe));
}

/**
 * intel_pch_fifo_underrun_irq_handler - handle PCH fifo underrun interrupt
 * @dev_priv: i915 device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 *
 * This handles a PCH fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_pch_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum transcoder pch_transcoder)
{
	if (intel_set_pch_fifo_underrun_reporting(dev_priv, pch_transcoder,
						  false))
		DRM_ERROR("PCH transcoder %c FIFO underrun\n",
			  transcoder_name(pch_transcoder));
}
