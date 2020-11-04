/*
 *
 * Copyright 2008 (c) Intel Corporation
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
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
 */

#include "display/intel_fbc.h"
#include "display/intel_gmbus.h"
#include "display/intel_vga.h"

#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_suspend.h"

static void intel_save_swf(struct drm_i915_private *dev_priv)
{
	int i;

	/* Scratch space */
	if (IS_GEN(dev_priv, 2) && IS_MOBILE(dev_priv)) {
		for (i = 0; i < 7; i++) {
			dev_priv->regfile.saveSWF0[i] = I915_READ(SWF0(i));
			dev_priv->regfile.saveSWF1[i] = I915_READ(SWF1(i));
		}
		for (i = 0; i < 3; i++)
			dev_priv->regfile.saveSWF3[i] = I915_READ(SWF3(i));
	} else if (IS_GEN(dev_priv, 2)) {
		for (i = 0; i < 7; i++)
			dev_priv->regfile.saveSWF1[i] = I915_READ(SWF1(i));
	} else if (HAS_GMCH(dev_priv)) {
		for (i = 0; i < 16; i++) {
			dev_priv->regfile.saveSWF0[i] = I915_READ(SWF0(i));
			dev_priv->regfile.saveSWF1[i] = I915_READ(SWF1(i));
		}
		for (i = 0; i < 3; i++)
			dev_priv->regfile.saveSWF3[i] = I915_READ(SWF3(i));
	}
}

static void intel_restore_swf(struct drm_i915_private *dev_priv)
{
	int i;

	/* Scratch space */
	if (IS_GEN(dev_priv, 2) && IS_MOBILE(dev_priv)) {
		for (i = 0; i < 7; i++) {
			I915_WRITE(SWF0(i), dev_priv->regfile.saveSWF0[i]);
			I915_WRITE(SWF1(i), dev_priv->regfile.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			I915_WRITE(SWF3(i), dev_priv->regfile.saveSWF3[i]);
	} else if (IS_GEN(dev_priv, 2)) {
		for (i = 0; i < 7; i++)
			I915_WRITE(SWF1(i), dev_priv->regfile.saveSWF1[i]);
	} else if (HAS_GMCH(dev_priv)) {
		for (i = 0; i < 16; i++) {
			I915_WRITE(SWF0(i), dev_priv->regfile.saveSWF0[i]);
			I915_WRITE(SWF1(i), dev_priv->regfile.saveSWF1[i]);
		}
		for (i = 0; i < 3; i++)
			I915_WRITE(SWF3(i), dev_priv->regfile.saveSWF3[i]);
	}
}

void i915_save_display(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	/* Display arbitration control */
	if (INTEL_GEN(dev_priv) <= 4)
		dev_priv->regfile.saveDSPARB = I915_READ(DSPARB);

	if (IS_GEN(dev_priv, 4))
		pci_read_config_word(pdev, GCDGMBUS,
				     &dev_priv->regfile.saveGCDGMBUS);

	intel_save_swf(dev_priv);
}

void i915_restore_display(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	intel_restore_swf(dev_priv);

	if (IS_GEN(dev_priv, 4))
		pci_write_config_word(pdev, GCDGMBUS,
				      dev_priv->regfile.saveGCDGMBUS);

	/* Display arbitration */
	if (INTEL_GEN(dev_priv) <= 4)
		I915_WRITE(DSPARB, dev_priv->regfile.saveDSPARB);

	/* only restore FBC info on the platform that supports FBC*/
	intel_fbc_global_disable(dev_priv);

	intel_vga_redisable(dev_priv);

	intel_gmbus_reset(dev_priv);
}
