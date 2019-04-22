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

#include <drm/i915_drm.h>
#include "intel_drv.h"
#include "i915_reg.h"

static void i915_save_display(struct drm_i915_private *dev_priv)
{
	/* Display arbitration control */
	if (INTEL_GEN(dev_priv) <= 4)
		dev_priv->regfile.saveDSPARB = I915_READ(DSPARB);

	/* save FBC interval */
	if (HAS_FBC(dev_priv) && INTEL_GEN(dev_priv) <= 4 && !IS_G4X(dev_priv))
		dev_priv->regfile.saveFBC_CONTROL = I915_READ(FBC_CONTROL);
}

static void i915_restore_display(struct drm_i915_private *dev_priv)
{
	/* Display arbitration */
	if (INTEL_GEN(dev_priv) <= 4)
		I915_WRITE(DSPARB, dev_priv->regfile.saveDSPARB);

	/* only restore FBC info on the platform that supports FBC*/
	intel_fbc_global_disable(dev_priv);

	/* restore FBC interval */
	if (HAS_FBC(dev_priv) && INTEL_GEN(dev_priv) <= 4 && !IS_G4X(dev_priv))
		I915_WRITE(FBC_CONTROL, dev_priv->regfile.saveFBC_CONTROL);

	i915_redisable_vga(dev_priv);
}

int i915_save_state(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int i;

	mutex_lock(&dev_priv->drm.struct_mutex);

	i915_save_display(dev_priv);

	if (IS_GEN(dev_priv, 4))
		pci_read_config_word(pdev, GCDGMBUS,
				     &dev_priv->regfile.saveGCDGMBUS);

	/* Cache mode state */
	if (INTEL_GEN(dev_priv) < 7)
		dev_priv->regfile.saveCACHE_MODE_0 = I915_READ(CACHE_MODE_0);

	/* Memory Arbitration state */
	dev_priv->regfile.saveMI_ARB_STATE = I915_READ(MI_ARB_STATE);

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

	mutex_unlock(&dev_priv->drm.struct_mutex);

	return 0;
}

int i915_restore_state(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int i;

	mutex_lock(&dev_priv->drm.struct_mutex);

	if (IS_GEN(dev_priv, 4))
		pci_write_config_word(pdev, GCDGMBUS,
				      dev_priv->regfile.saveGCDGMBUS);
	i915_restore_display(dev_priv);

	/* Cache mode state */
	if (INTEL_GEN(dev_priv) < 7)
		I915_WRITE(CACHE_MODE_0, dev_priv->regfile.saveCACHE_MODE_0 |
			   0xffff0000);

	/* Memory arbitration state */
	I915_WRITE(MI_ARB_STATE, dev_priv->regfile.saveMI_ARB_STATE | 0xffff0000);

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

	mutex_unlock(&dev_priv->drm.struct_mutex);

	intel_i2c_reset(dev_priv);

	return 0;
}
