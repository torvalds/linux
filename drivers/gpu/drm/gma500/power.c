/**************************************************************************
 * Copyright (c) 2009-2011, Intel Corporation.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Benjamin Defnet <benjamin.r.defnet@intel.com>
 *    Rajesh Poornachandran <rajesh.poornachandran@intel.com>
 * Massively reworked
 *    Alan Cox <alan@linux.intel.com>
 */

#include "gem.h"
#include "power.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "psb_irq.h"
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

/**
 *	gma_power_init		-	initialise power manager
 *	@dev: our device
 *
 *	Set up for power management tracking of our hardware.
 */
void gma_power_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	/* FIXME: Move APM/OSPM base into relevant device code */
	dev_priv->apm_base = dev_priv->apm_reg & 0xffff;
	dev_priv->ospm_base &= 0xffff;

	if (dev_priv->ops->init_pm)
		dev_priv->ops->init_pm(dev);

	/*
	 * Runtime pm support is broken atm. So for now unconditionally
	 * call pm_runtime_get() here and put it again in psb_driver_unload()
	 *
	 * To fix this we need to call pm_runtime_get() once for each active
	 * pipe at boot and then put() / get() for each pipe disable / enable
	 * so that the device gets runtime suspended when no pipes are active.
	 * Once this is in place the pm_runtime_get() below should be replaced
	 * by a pm_runtime_allow() call to undo the pm_runtime_forbid() from
	 * pci_pm_init().
	 */
	pm_runtime_get(dev->dev);

	dev_priv->pm_initialized = true;
}

/**
 *	gma_power_uninit	-	end power manager
 *	@dev: device to end for
 *
 *	Undo the effects of gma_power_init
 */
void gma_power_uninit(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	if (!dev_priv->pm_initialized)
		return;

	pm_runtime_put_noidle(dev->dev);
}

/**
 *	gma_suspend_display	-	suspend the display logic
 *	@dev: our DRM device
 *
 *	Suspend the display logic of the graphics interface
 */
static void gma_suspend_display(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	dev_priv->ops->save_regs(dev);
	dev_priv->ops->power_down(dev);
}

/**
 *	gma_resume_display	-	resume display side logic
 *	@pdev: PCI device
 *
 *	Resume the display hardware restoring state and enabling
 *	as necessary.
 */
static void gma_resume_display(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	/* turn on the display power island */
	dev_priv->ops->power_up(dev);

	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);

	/* Rebuild our GTT mappings */
	psb_gtt_resume(dev);
	psb_gem_mm_resume(dev);
	dev_priv->ops->restore_regs(dev);
}

/**
 *	gma_suspend_pci		-	suspend PCI side
 *	@pdev: PCI device
 *
 *	Perform the suspend processing on our PCI device state
 */
static void gma_suspend_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	int bsm, vbt;

	pci_save_state(pdev);
	pci_read_config_dword(pdev, 0x5C, &bsm);
	dev_priv->regs.saveBSM = bsm;
	pci_read_config_dword(pdev, 0xFC, &vbt);
	dev_priv->regs.saveVBT = vbt;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
}

/**
 *	gma_resume_pci		-	resume helper
 *	@pdev: our PCI device
 *
 *	Perform the resume processing on our PCI device state - rewrite
 *	register state and re-enable the PCI device
 */
static int gma_resume_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_write_config_dword(pdev, 0x5c, dev_priv->regs.saveBSM);
	pci_write_config_dword(pdev, 0xFC, dev_priv->regs.saveVBT);

	return pci_enable_device(pdev);
}

/**
 *	gma_power_suspend		-	bus callback for suspend
 *	@_dev: our device
 *
 *	Called back by the PCI layer during a suspend of the system. We
 *	perform the necessary shut down steps and save enough state that
 *	we can undo this when resume is called.
 */
int gma_power_suspend(struct device *_dev)
{
	struct pci_dev *pdev = to_pci_dev(_dev);
	struct drm_device *dev = pci_get_drvdata(pdev);

	gma_irq_uninstall(dev);
	gma_suspend_display(dev);
	gma_suspend_pci(pdev);
	return 0;
}

/**
 *	gma_power_resume		-	resume power
 *	@_dev: our device
 *
 *	Resume the PCI side of the graphics and then the displays
 */
int gma_power_resume(struct device *_dev)
{
	struct pci_dev *pdev = to_pci_dev(_dev);
	struct drm_device *dev = pci_get_drvdata(pdev);

	gma_resume_pci(pdev);
	gma_resume_display(pdev);
	gma_irq_install(dev);
	return 0;
}

/**
 *	gma_power_begin		-	begin requiring power
 *	@dev: our DRM device
 *	@force_on: true to force power on
 *
 *	Begin an action that requires the display power island is enabled.
 *	We refcount the islands.
 */
bool gma_power_begin(struct drm_device *dev, bool force_on)
{
	if (force_on)
		return pm_runtime_resume_and_get(dev->dev) == 0;
	else
		return pm_runtime_get_if_in_use(dev->dev) == 1;
}

/**
 *	gma_power_end		-	end use of power
 *	@dev: Our DRM device
 *
 *	Indicate that one of our gma_power_begin() requested periods when
 *	the diplay island power is needed has completed.
 */
void gma_power_end(struct drm_device *dev)
{
	pm_runtime_put(dev->dev);
}
