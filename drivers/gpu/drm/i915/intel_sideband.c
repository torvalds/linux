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
 *
 */

#include "i915_drv.h"
#include "intel_drv.h"

/*
 * IOSF sideband, see VLV2_SidebandMsg_HAS.docx and
 * VLV_VLV2_PUNIT_HAS_0.8.docx
 */

/* Standard MMIO read, non-posted */
#define SB_MRD_NP	0x00
/* Standard MMIO write, non-posted */
#define SB_MWR_NP	0x01
/* Private register read, double-word addressing, non-posted */
#define SB_CRRDDA_NP	0x06
/* Private register write, double-word addressing, non-posted */
#define SB_CRWRDA_NP	0x07

static int vlv_sideband_rw(struct drm_i915_private *dev_priv, u32 devfn,
			   u32 port, u32 opcode, u32 addr, u32 *val)
{
	u32 cmd, be = 0xf, bar = 0;
	bool is_read = (opcode == SB_MRD_NP || opcode == SB_CRRDDA_NP);

	cmd = (devfn << IOSF_DEVFN_SHIFT) | (opcode << IOSF_OPCODE_SHIFT) |
		(port << IOSF_PORT_SHIFT) | (be << IOSF_BYTE_ENABLES_SHIFT) |
		(bar << IOSF_BAR_SHIFT);

	WARN_ON(!mutex_is_locked(&dev_priv->sb_lock));

	if (intel_wait_for_register(dev_priv,
				    VLV_IOSF_DOORBELL_REQ, IOSF_SB_BUSY, 0,
				    5)) {
		DRM_DEBUG_DRIVER("IOSF sideband idle wait (%s) timed out\n",
				 is_read ? "read" : "write");
		return -EAGAIN;
	}

	I915_WRITE(VLV_IOSF_ADDR, addr);
	I915_WRITE(VLV_IOSF_DATA, is_read ? 0 : *val);
	I915_WRITE(VLV_IOSF_DOORBELL_REQ, cmd);

	if (intel_wait_for_register(dev_priv,
				    VLV_IOSF_DOORBELL_REQ, IOSF_SB_BUSY, 0,
				    5)) {
		DRM_DEBUG_DRIVER("IOSF sideband finish wait (%s) timed out\n",
				 is_read ? "read" : "write");
		return -ETIMEDOUT;
	}

	if (is_read)
		*val = I915_READ(VLV_IOSF_DATA);

	return 0;
}

u32 vlv_punit_read(struct drm_i915_private *dev_priv, u32 addr)
{
	u32 val = 0;

	WARN_ON(!mutex_is_locked(&dev_priv->pcu_lock));

	mutex_lock(&dev_priv->sb_lock);
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_PUNIT,
			SB_CRRDDA_NP, addr, &val);
	mutex_unlock(&dev_priv->sb_lock);

	return val;
}

int vlv_punit_write(struct drm_i915_private *dev_priv, u32 addr, u32 val)
{
	int err;

	WARN_ON(!mutex_is_locked(&dev_priv->pcu_lock));

	mutex_lock(&dev_priv->sb_lock);
	err = vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_PUNIT,
			      SB_CRWRDA_NP, addr, &val);
	mutex_unlock(&dev_priv->sb_lock);

	return err;
}

u32 vlv_bunit_read(struct drm_i915_private *dev_priv, u32 reg)
{
	u32 val = 0;

	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_BUNIT,
			SB_CRRDDA_NP, reg, &val);

	return val;
}

void vlv_bunit_write(struct drm_i915_private *dev_priv, u32 reg, u32 val)
{
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_BUNIT,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_nc_read(struct drm_i915_private *dev_priv, u8 addr)
{
	u32 val = 0;

	WARN_ON(!mutex_is_locked(&dev_priv->pcu_lock));

	mutex_lock(&dev_priv->sb_lock);
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_NC,
			SB_CRRDDA_NP, addr, &val);
	mutex_unlock(&dev_priv->sb_lock);

	return val;
}

u32 vlv_iosf_sb_read(struct drm_i915_private *dev_priv, u8 port, u32 reg)
{
	u32 val = 0;
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), port,
			SB_CRRDDA_NP, reg, &val);
	return val;
}

void vlv_iosf_sb_write(struct drm_i915_private *dev_priv,
		       u8 port, u32 reg, u32 val)
{
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), port,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_cck_read(struct drm_i915_private *dev_priv, u32 reg)
{
	u32 val = 0;
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_CCK,
			SB_CRRDDA_NP, reg, &val);
	return val;
}

void vlv_cck_write(struct drm_i915_private *dev_priv, u32 reg, u32 val)
{
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_CCK,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_ccu_read(struct drm_i915_private *dev_priv, u32 reg)
{
	u32 val = 0;
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_CCU,
			SB_CRRDDA_NP, reg, &val);
	return val;
}

void vlv_ccu_write(struct drm_i915_private *dev_priv, u32 reg, u32 val)
{
	vlv_sideband_rw(dev_priv, PCI_DEVFN(0, 0), IOSF_PORT_CCU,
			SB_CRWRDA_NP, reg, &val);
}

u32 vlv_dpio_read(struct drm_i915_private *dev_priv, enum pipe pipe, int reg)
{
	u32 val = 0;

	vlv_sideband_rw(dev_priv, DPIO_DEVFN, DPIO_PHY_IOSF_PORT(DPIO_PHY(pipe)),
			SB_MRD_NP, reg, &val);

	/*
	 * FIXME: There might be some registers where all 1's is a valid value,
	 * so ideally we should check the register offset instead...
	 */
	WARN(val == 0xffffffff, "DPIO read pipe %c reg 0x%x == 0x%x\n",
	     pipe_name(pipe), reg, val);

	return val;
}

void vlv_dpio_write(struct drm_i915_private *dev_priv, enum pipe pipe, int reg, u32 val)
{
	vlv_sideband_rw(dev_priv, DPIO_DEVFN, DPIO_PHY_IOSF_PORT(DPIO_PHY(pipe)),
			SB_MWR_NP, reg, &val);
}

/* SBI access */
u32 intel_sbi_read(struct drm_i915_private *dev_priv, u16 reg,
		   enum intel_sbi_destination destination)
{
	u32 value = 0;
	WARN_ON(!mutex_is_locked(&dev_priv->sb_lock));

	if (intel_wait_for_register(dev_priv,
				    SBI_CTL_STAT, SBI_BUSY, 0,
				    100)) {
		DRM_ERROR("timeout waiting for SBI to become ready\n");
		return 0;
	}

	I915_WRITE(SBI_ADDR, (reg << 16));
	I915_WRITE(SBI_DATA, 0);

	if (destination == SBI_ICLK)
		value = SBI_CTL_DEST_ICLK | SBI_CTL_OP_CRRD;
	else
		value = SBI_CTL_DEST_MPHY | SBI_CTL_OP_IORD;
	I915_WRITE(SBI_CTL_STAT, value | SBI_BUSY);

	if (intel_wait_for_register(dev_priv,
				    SBI_CTL_STAT,
				    SBI_BUSY,
				    0,
				    100)) {
		DRM_ERROR("timeout waiting for SBI to complete read\n");
		return 0;
	}

	if (I915_READ(SBI_CTL_STAT) & SBI_RESPONSE_FAIL) {
		DRM_ERROR("error during SBI read of reg %x\n", reg);
		return 0;
	}

	return I915_READ(SBI_DATA);
}

void intel_sbi_write(struct drm_i915_private *dev_priv, u16 reg, u32 value,
		     enum intel_sbi_destination destination)
{
	u32 tmp;

	WARN_ON(!mutex_is_locked(&dev_priv->sb_lock));

	if (intel_wait_for_register(dev_priv,
				    SBI_CTL_STAT, SBI_BUSY, 0,
				    100)) {
		DRM_ERROR("timeout waiting for SBI to become ready\n");
		return;
	}

	I915_WRITE(SBI_ADDR, (reg << 16));
	I915_WRITE(SBI_DATA, value);

	if (destination == SBI_ICLK)
		tmp = SBI_CTL_DEST_ICLK | SBI_CTL_OP_CRWR;
	else
		tmp = SBI_CTL_DEST_MPHY | SBI_CTL_OP_IOWR;
	I915_WRITE(SBI_CTL_STAT, SBI_BUSY | tmp);

	if (intel_wait_for_register(dev_priv,
				    SBI_CTL_STAT,
				    SBI_BUSY,
				    0,
				    100)) {
		DRM_ERROR("timeout waiting for SBI to complete write\n");
		return;
	}

	if (I915_READ(SBI_CTL_STAT) & SBI_RESPONSE_FAIL) {
		DRM_ERROR("error during SBI write of %x to reg %x\n",
			  value, reg);
		return;
	}
}

u32 vlv_flisdsi_read(struct drm_i915_private *dev_priv, u32 reg)
{
	u32 val = 0;
	vlv_sideband_rw(dev_priv, DPIO_DEVFN, IOSF_PORT_FLISDSI, SB_CRRDDA_NP,
			reg, &val);
	return val;
}

void vlv_flisdsi_write(struct drm_i915_private *dev_priv, u32 reg, u32 val)
{
	vlv_sideband_rw(dev_priv, DPIO_DEVFN, IOSF_PORT_FLISDSI, SB_CRWRDA_NP,
			reg, &val);
}
