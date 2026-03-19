// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>

#include <drm/drm_device.h>
#include <drm/drm_print.h>
#include <drm/intel/i915_drm.h>
#include <video/vga.h>

#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_vga.h"
#include "intel_vga_regs.h"

static unsigned int intel_gmch_ctrl_reg(struct intel_display *display)
{
	return DISPLAY_VER(display) >= 6 ? SNB_GMCH_CTRL : I830_GMCH_CTRL;
}

static bool intel_vga_decode_is_enabled(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	u16 gmch_ctrl = 0;

	if (pci_bus_read_config_word(pdev->bus, PCI_DEVFN(0, 0),
				     intel_gmch_ctrl_reg(display), &gmch_ctrl))
		return false;

	return !(gmch_ctrl & INTEL_GMCH_VGA_DISABLE);
}

static i915_reg_t intel_vga_cntrl_reg(struct intel_display *display)
{
	if (display->platform.valleyview || display->platform.cherryview)
		return VLV_VGACNTRL;
	else if (DISPLAY_VER(display) >= 5)
		return CPU_VGACNTRL;
	else
		return VGACNTRL;
}

static bool has_vga_pipe_sel(struct intel_display *display)
{
	if (display->platform.i845g ||
	    display->platform.i865g)
		return false;

	if (display->platform.valleyview ||
	    display->platform.cherryview)
		return true;

	return DISPLAY_VER(display) < 7;
}

static bool has_vga_mmio_access(struct intel_display *display)
{
	/* WaEnableVGAAccessThroughIOPort:ctg+ */
	return DISPLAY_VER(display) < 5 && !display->platform.g4x;
}

static bool intel_pci_has_vga_io_decode(struct pci_dev *pdev)
{
	u16 cmd = 0;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if ((cmd & PCI_COMMAND_IO) == 0)
		return false;

	pdev = pdev->bus->self;
	while (pdev) {
		u16 ctl = 0;

		pci_read_config_word(pdev, PCI_BRIDGE_CONTROL, &ctl);
		if ((ctl & PCI_BRIDGE_CTL_VGA) == 0)
			return false;

		pdev = pdev->bus->self;
	}

	return true;
}

static bool intel_pci_set_io_decode(struct pci_dev *pdev, bool enable)
{
	u16 old = 0, cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &old);
	cmd = old & ~PCI_COMMAND_IO;
	if (enable)
		cmd |= PCI_COMMAND_IO;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	return old & PCI_COMMAND_IO;
}

static bool intel_pci_bridge_set_vga(struct pci_dev *pdev, bool enable)
{
	u16 old = 0, ctl;

	pci_read_config_word(pdev->bus->self, PCI_BRIDGE_CONTROL, &old);
	ctl = old & ~PCI_BRIDGE_CTL_VGA;
	if (enable)
		ctl |= PCI_BRIDGE_CTL_VGA;
	pci_write_config_word(pdev->bus->self, PCI_BRIDGE_CONTROL, ctl);

	return old & PCI_BRIDGE_CTL_VGA;
}

static int intel_vga_get(struct intel_display *display, bool mmio,
			 bool *old_io_decode)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	int err;

	if (mmio) {
		*old_io_decode = false;
		return 0;
	}

	/*
	 * Bypass the VGA arbiter on the iGPU and just enable
	 * IO decode by hand. This avoids clobbering the VGA
	 * routing for an external GPU when it's the current
	 * VGA device, and thus prevents the all 0xff/white
	 * readout from VGA memory when taking over from vgacon.
	 *
	 * The iGPU has the highest VGA decode priority so it will
	 * grab any VGA IO access when IO decode is enabled, regardless
	 * of how any other VGA routing bits are configured.
	 */
	if (display->platform.dgfx) {
		err = vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
		if (err)
			return err;
	}

	*old_io_decode = intel_pci_set_io_decode(pdev, true);

	return 0;
}

static void intel_vga_put(struct intel_display *display, bool io_decode, bool mmio)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);

	if (mmio)
		return;

	/* see intel_vga_get() */
	intel_pci_set_io_decode(pdev, io_decode);

	if (display->platform.dgfx)
		vga_put(pdev, VGA_RSRC_LEGACY_IO);
}

u8 intel_vga_read(struct intel_display *display, u16 reg, bool mmio)
{
	if (mmio)
		return intel_de_read8(display, _MMIO(reg));
	else
		return inb(reg);
}

static void intel_vga_write(struct intel_display *display, u16 reg, u8 val, bool mmio)
{
	if (mmio)
		intel_de_write8(display, _MMIO(reg), val);
	else
		outb(val, reg);
}

/* Disable the VGA plane that we never use */
void intel_vga_disable(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	i915_reg_t vga_reg = intel_vga_cntrl_reg(display);
	bool mmio = has_vga_mmio_access(display);
	bool io_decode;
	u8 msr, sr1;
	u32 tmp;
	int err;

	if (!intel_vga_decode_is_enabled(display)) {
		drm_dbg_kms(display->drm, "VGA decode is disabled\n");

		/*
		 * On older hardware VGA_DISP_DISABLE defaults to 0, but
		 * it *must* be set or else the pipe will be completely
		 * stuck (at least on g4x).
		 */
		goto reset_vgacntr;
	}

	tmp = intel_de_read(display, vga_reg);

	if ((tmp & VGA_DISP_DISABLE) == 0) {
		enum pipe pipe;

		if (display->platform.cherryview)
			pipe = REG_FIELD_GET(VGA_PIPE_SEL_MASK_CHV, tmp);
		else if (has_vga_pipe_sel(display))
			pipe = REG_FIELD_GET(VGA_PIPE_SEL_MASK, tmp);
		else
			pipe = PIPE_A;

		drm_dbg_kms(display->drm, "Disabling VGA plane on pipe %c\n",
			    pipe_name(pipe));
	} else {
		drm_dbg_kms(display->drm, "VGA plane is disabled\n");

		/*
		 * Unfortunately at least some BIOSes (eg. HSW Lenovo
		 * ThinkCentre E73) set up the VGA registers even when
		 * in UEFI mode with the VGA plane disabled. So we need to
		 * always clean up the mess for iGPUs. For discrete GPUs we
		 * don't really care about the state of the VGA registers
		 * since all VGA accesses can be blocked via the bridge.
		 */
		if (display->platform.dgfx)
			goto reset_vgacntr;
	}

	/*
	 * This should not fail, because the vga_get() family of functions
	 * will only report errors for dGPUs that are unreachable via the
	 * bridge, and cannot be made reachable either. We shouldn't even
	 * get here for this case, but if we do, we assume that the bridge
	 * will also refuse future requests to forward VGA accesses.
	 */
	err = intel_vga_get(display, mmio, &io_decode);
	if (err)
		goto reset_vgacntr;

	drm_WARN_ON(display->drm, !mmio && !intel_pci_has_vga_io_decode(pdev));

	intel_vga_write(display, VGA_SEQ_I, 0x01, mmio);
	sr1 = intel_vga_read(display, VGA_SEQ_D, mmio);
	sr1 |= VGA_SR01_SCREEN_OFF;
	intel_vga_write(display, VGA_SEQ_D, sr1, mmio);

	msr = intel_vga_read(display, VGA_MIS_R, mmio);
	/*
	 * Always disable VGA memory decode for iGPU so that
	 * intel_vga_set_decode() doesn't need to access VGA registers.
	 * VGA_MIS_ENB_MEM_ACCESS=0 is also the reset value.
	 */
	msr &= ~VGA_MIS_ENB_MEM_ACCESS;
	/*
	 * VGA_MIS_COLOR controls both GPU level and display engine level
	 * MDA vs. CGA decode logic. But when the register gets reset
	 * (reset value has VGA_MIS_COLOR=0) by the power well, only the
	 * display engine level decode logic gets notified.
	 *
	 * Switch to MDA mode to make sure the GPU level decode logic will
	 * be in sync with the display engine level decode logic after the
	 * power well has been reset. Otherwise the GPU will claim CGA
	 * register accesses but the display engine will not, causing
	 * RMbus NoClaim errors.
	 */
	msr &= ~VGA_MIS_COLOR;
	intel_vga_write(display, VGA_MIS_W, msr, mmio);

	intel_vga_put(display, io_decode, mmio);

	/*
	 * Inform the arbiter about VGA memory decode being disabled so
	 * that it doesn't disable all memory decode for the iGPU when
	 * targeting another GPU.
	 */
	if (!display->platform.dgfx)
		vga_set_legacy_decoding(pdev, VGA_RSRC_LEGACY_IO);

	udelay(300);

reset_vgacntr:
	intel_de_write(display, vga_reg, VGA_DISP_DISABLE);
	intel_de_posting_read(display, vga_reg);
}

static unsigned int intel_vga_set_decode(struct pci_dev *pdev, bool enable_decode)
{
	struct intel_display *display = to_intel_display(pdev);
	unsigned int decodes = VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;

	drm_dbg_kms(display->drm, "%s VGA decode due to VGA arbitration\n",
		    str_enable_disable(enable_decode));

	/*
	 * Can't use GMCH_CTRL INTEL_GMCH_VGA_DISABLE to disable VGA
	 * decode on ILK+ since the register is locked. Instead
	 * intel_disable_vga() will disable VGA memory decode for the
	 * iGPU, and here we just need to take care of the IO decode.
	 * For discrete GPUs we rely on the bridge VGA control.
	 *
	 * We can't disable IO decode already in intel_vga_disable()
	 * because at least some laptops (eg. CTG Dell Latitude E5400)
	 * will hang during reboot/shutfown with IO decode disabled.
	 */
	if (display->platform.dgfx) {
		if (!enable_decode)
			intel_pci_bridge_set_vga(pdev, false);
		else
			decodes |= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
	} else {
		if (!enable_decode)
			intel_pci_set_io_decode(pdev, false);
		else
			decodes |= VGA_RSRC_LEGACY_IO;
	}

	return decodes;
}

void intel_vga_register(struct intel_display *display)
{

	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
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
	drm_WARN_ON(display->drm, ret && ret != -ENODEV);
}

void intel_vga_unregister(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);

	vga_client_unregister(pdev);
}
