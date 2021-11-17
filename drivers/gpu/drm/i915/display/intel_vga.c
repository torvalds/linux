// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/vgaarb.h>

#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "intel_de.h"
#include "intel_vga.h"

static i915_reg_t intel_vga_cntrl_reg(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		return VLV_VGACNTRL;
	else if (DISPLAY_VER(i915) >= 5)
		return CPU_VGACNTRL;
	else
		return VGACNTRL;
}

/* Disable the VGA plane that we never use */
void intel_vga_disable(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	i915_reg_t vga_reg = intel_vga_cntrl_reg(dev_priv);
	u8 sr1;

	if (intel_de_read(dev_priv, vga_reg) & VGA_DISP_DISABLE)
		return;

	/* WaEnableVGAAccessThroughIOPort:ctg,elk,ilk,snb,ivb,vlv,hsw */
	vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
	outb(SR01, VGA_SR_INDEX);
	sr1 = inb(VGA_SR_DATA);
	outb(sr1 | 1 << 5, VGA_SR_DATA);
	vga_put(pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	intel_de_write(dev_priv, vga_reg, VGA_DISP_DISABLE);
	intel_de_posting_read(dev_priv, vga_reg);
}

void intel_vga_redisable_power_on(struct drm_i915_private *dev_priv)
{
	i915_reg_t vga_reg = intel_vga_cntrl_reg(dev_priv);

	if (!(intel_de_read(dev_priv, vga_reg) & VGA_DISP_DISABLE)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Something enabled VGA plane, disabling it\n");
		intel_vga_disable(dev_priv);
	}
}

void intel_vga_redisable(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	/*
	 * This function can be called both from intel_modeset_setup_hw_state or
	 * at a very early point in our resume sequence, where the power well
	 * structures are not yet restored. Since this function is at a very
	 * paranoid "someone might have enabled VGA while we were not looking"
	 * level, just check if the power well is enabled instead of trying to
	 * follow the "don't touch the power well if we don't need it" policy
	 * the rest of the driver uses.
	 */
	wakeref = intel_display_power_get_if_enabled(i915, POWER_DOMAIN_VGA);
	if (!wakeref)
		return;

	intel_vga_redisable_power_on(i915);

	intel_display_power_put(i915, POWER_DOMAIN_VGA, wakeref);
}

void intel_vga_reset_io_mem(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	/*
	 * After we re-enable the power well, if we touch VGA register 0x3d5
	 * we'll get unclaimed register interrupts. This stops after we write
	 * anything to the VGA MSR register. The vgacon module uses this
	 * register all the time, so if we unbind our driver and, as a
	 * consequence, bind vgacon, we'll get stuck in an infinite loop at
	 * console_unlock(). So make here we touch the VGA MSR register, making
	 * sure vgacon can keep working normally without triggering interrupts
	 * and error messages.
	 */
	vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
	outb(inb(VGA_MSR_READ), VGA_MSR_WRITE);
	vga_put(pdev, VGA_RSRC_LEGACY_IO);
}

static int
intel_vga_set_state(struct drm_i915_private *i915, bool enable_decode)
{
	unsigned int reg = DISPLAY_VER(i915) >= 6 ? SNB_GMCH_CTRL : INTEL_GMCH_CTRL;
	u16 gmch_ctrl;

	if (pci_read_config_word(i915->bridge_dev, reg, &gmch_ctrl)) {
		drm_err(&i915->drm, "failed to read control word\n");
		return -EIO;
	}

	if (!!(gmch_ctrl & INTEL_GMCH_VGA_DISABLE) == !enable_decode)
		return 0;

	if (enable_decode)
		gmch_ctrl &= ~INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= INTEL_GMCH_VGA_DISABLE;

	if (pci_write_config_word(i915->bridge_dev, reg, gmch_ctrl)) {
		drm_err(&i915->drm, "failed to write control word\n");
		return -EIO;
	}

	return 0;
}

static unsigned int
intel_vga_set_decode(struct pci_dev *pdev, bool enable_decode)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);

	intel_vga_set_state(i915, enable_decode);

	if (enable_decode)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

int intel_vga_register(struct drm_i915_private *i915)
{

	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	int ret;

	/*
	 * If we have > 1 VGA cards, then we need to arbitrate access to the
	 * common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(pdev, intel_vga_set_decode);
	if (ret && ret != -ENODEV)
		return ret;

	return 0;
}

void intel_vga_unregister(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	vga_client_unregister(pdev);
}
