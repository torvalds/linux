/**************************************************************************
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.

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
#include "psb_powermgmt.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

static struct mutex power_mutex;

/**
 *	gma_power_init		-	initialise power manager
 *	@dev: our device
 *
 *	Set up for power management tracking of our hardware.
 */
void gma_power_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	dev_priv->apm_base = dev_priv->apm_reg & 0xffff;
	dev_priv->ospm_base &= 0xffff;

	dev_priv->display_power = true;	/* We start active */
	dev_priv->display_count = 0;	/* Currently no users */
	dev_priv->suspended = false;	/* And not suspended */
	mutex_init(&power_mutex);

	if (!IS_MRST(dev)) {
		/* FIXME: wants further review */
		u32 gating = PSB_RSGX32(PSB_CR_CLKGATECTL);
		/* Disable 2D clock gating */
		gating &= ~3;
		gating |= 1;
		PSB_WSGX32(gating, PSB_CR_CLKGATECTL);
		PSB_RSGX32(PSB_CR_CLKGATECTL);
	}
}

/**
 *	gma_power_uninit	-	end power manager
 *	@dev: device to end for
 *
 *	Undo the effects of gma_power_init
 */
void gma_power_uninit(struct drm_device *dev)
{
	mutex_destroy(&power_mutex);
	pm_runtime_disable(&dev->pdev->dev);
	pm_runtime_set_suspended(&dev->pdev->dev);
}


/**
 *	save_display_registers	-	save registers lost on suspend
 *	@dev: our DRM device
 *
 *	Save the state we need in order to be able to restore the interface
 *	upon resume from suspend
 */
static int save_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;

	/* Display arbitration control + watermarks */
	dev_priv->saveDSPARB = PSB_RVDC32(DSPARB);
	dev_priv->saveDSPFW1 = PSB_RVDC32(DSPFW1);
	dev_priv->saveDSPFW2 = PSB_RVDC32(DSPFW2);
	dev_priv->saveDSPFW3 = PSB_RVDC32(DSPFW3);
	dev_priv->saveDSPFW4 = PSB_RVDC32(DSPFW4);
	dev_priv->saveDSPFW5 = PSB_RVDC32(DSPFW5);
	dev_priv->saveDSPFW6 = PSB_RVDC32(DSPFW6);
	dev_priv->saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/* Save crtc and output state */
	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (drm_helper_crtc_in_use(crtc))
			crtc->funcs->save(crtc);
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->save(connector);

	mutex_unlock(&dev->mode_config.mutex);
	return 0;
}

/**
 *	restore_display_registers	-	restore lost register state
 *	@dev: our DRM device
 *
 *	Restore register state that was lost during suspend and resume.
 */
static int restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_connector *connector;

	/* Display arbitration + watermarks */
	PSB_WVDC32(dev_priv->saveDSPARB, DSPARB);
	PSB_WVDC32(dev_priv->saveDSPFW1, DSPFW1);
	PSB_WVDC32(dev_priv->saveDSPFW2, DSPFW2);
	PSB_WVDC32(dev_priv->saveDSPFW3, DSPFW3);
	PSB_WVDC32(dev_priv->saveDSPFW4, DSPFW4);
	PSB_WVDC32(dev_priv->saveDSPFW5, DSPFW5);
	PSB_WVDC32(dev_priv->saveDSPFW6, DSPFW6);
	PSB_WVDC32(dev_priv->saveCHICKENBIT, DSPCHICKENBIT);

	/*make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (drm_helper_crtc_in_use(crtc))
			crtc->funcs->restore(crtc);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		connector->funcs->restore(connector);

	mutex_unlock(&dev->mode_config.mutex);
	return 0;
}

/**
 *	power_down	-	power down the display island
 *	@dev: our DRM device
 *
 *	Power down the display interface of our device
 */
static void power_down(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask ;
	u32 pwr_sts;

	if (IS_MRST(dev)) {
		pwr_mask = PSB_PWRGT_DISPLAY_MASK;
		outl(pwr_mask, dev_priv->ospm_base + PSB_PM_SSC);

		while (true) {
			pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
			if ((pwr_sts & pwr_mask) == pwr_mask)
				break;
			else
				udelay(10);
		}
		dev_priv->display_power = false;
	}
}


/**
 *	gma_suspend_display	-	suspend the display logic
 *	@dev: our DRM device
 *
 *	Suspend the display logic of the graphics interface
 */
static void gma_suspend_display(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int pp_stat;

	if (dev_priv->suspended)
		return;

	save_display_registers(dev);

	if (dev_priv->iLVDS_enable) {
		/*shutdown the panel*/
		PSB_WVDC32(0, PP_CONTROL);

		do {
			pp_stat = PSB_RVDC32(PP_STATUS);
		} while (pp_stat & 0x80000000);

		/*turn off the plane*/
		PSB_WVDC32(0x58000000, DSPACNTR);
		PSB_WVDC32(0, DSPASURF);/*trigger the plane disable*/
		/*wait ~4 ticks*/
		msleep(4);

		/*turn off pipe*/
		PSB_WVDC32(0x0, PIPEACONF);
		/*wait ~8 ticks*/
		msleep(8);

		/*turn off PLLs*/
		PSB_WVDC32(0, MRST_DPLL_A);
	} else {
		PSB_WVDC32(DPI_SHUT_DOWN, DPI_CONTROL_REG);
		PSB_WVDC32(0x0, PIPEACONF);
		PSB_WVDC32(0x2faf0000, BLC_PWM_CTL);
		while (REG_READ(0x70008) & 0x40000000);
		while ((PSB_RVDC32(GEN_FIFO_STAT_REG) & DPI_FIFO_EMPTY)
			!= DPI_FIFO_EMPTY);
		PSB_WVDC32(0, DEVICE_READY_REG);
			/* turn off panel power */
	}
	power_down(dev);
}

/*
 * power_up
 *
 * Description: Restore power to the specified island(s) (powergating)
 */
static void power_up(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 pwr_mask = PSB_PWRGT_DISPLAY_MASK;
	u32 pwr_sts, pwr_cnt;

	if (IS_MRST(dev)) {
		pwr_cnt = inl(dev_priv->ospm_base + PSB_PM_SSC);
		pwr_cnt &= ~pwr_mask;
		outl(pwr_cnt, (dev_priv->ospm_base + PSB_PM_SSC));

		while (true) {
			pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
			if ((pwr_sts & pwr_mask) == 0)
				break;
			else
				udelay(10);
		}
	}
	dev_priv->suspended = false;
	dev_priv->display_power = true;
}

/**
 *	gma_resume_display	-	resume display side logic
 *
 *	Resume the display hardware restoring state and enabling
 *	as necessary.
 */
static void gma_resume_display(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (dev_priv->suspended == false)
		return;

	/* turn on the display power island */
	power_up(dev);

	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);

	/* Don't reinitialize the GTT as it is unnecessary.  The gtt is
	 * stored in memory so it will automatically be restored.  All
	 * we need to do is restore the PGETBL_CTL which we already do
	 * above.
	 */
	/*psb_gtt_init(dev_priv->pg, 1);*/

	restore_display_registers(dev);
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
	struct drm_psb_private *dev_priv = dev->dev_private;
	int bsm, vbt;

	if (dev_priv->suspended)
		return;

	pci_save_state(pdev);
	pci_read_config_dword(pdev, 0x5C, &bsm);
	dev_priv->saveBSM = bsm;
	pci_read_config_dword(pdev, 0xFC, &vbt);
	dev_priv->saveVBT = vbt;
	pci_read_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, &dev_priv->msi_addr);
	pci_read_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, &dev_priv->msi_data);

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	dev_priv->suspended = true;
}

/**
 *	gma_resume_pci		-	resume helper
 *	@dev: our PCI device
 *
 *	Perform the resume processing on our PCI device state - rewrite
 *	register state and re-enable the PCI device
 */
static bool gma_resume_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->suspended)
		return true;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_write_config_dword(pdev, 0x5c, dev_priv->saveBSM);
	pci_write_config_dword(pdev, 0xFC, dev_priv->saveVBT);
	/* retoring MSI address and data in PCIx space */
	pci_write_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, dev_priv->msi_addr);
	pci_write_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, dev_priv->msi_data);
	ret = pci_enable_device(pdev);

	if (ret != 0)
		dev_err(&pdev->dev, "pci_enable failed: %d\n", ret);
	else
		dev_priv->suspended = false;
	return !dev_priv->suspended;
}

/**
 *	gma_power_suspend		-	bus callback for suspend
 *	@pdev: our PCI device
 *	@state: suspend type
 *
 *	Called back by the PCI layer during a suspend of the system. We
 *	perform the necessary shut down steps and save enough state that
 *	we can undo this when resume is called.
 */
int gma_power_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	mutex_lock(&power_mutex);
	if (!dev_priv->suspended) {
		if (dev_priv->display_count) {
			mutex_unlock(&power_mutex);
			return -EBUSY;
		}
		psb_irq_uninstall(dev);
		gma_suspend_display(dev);
		gma_suspend_pci(pdev);
	}
	mutex_unlock(&power_mutex);
	return 0;
}


/**
 *	gma_power_resume		-	resume power
 *	@pdev: PCI device
 *
 *	Resume the PCI side of the graphics and then the displays
 */
int gma_power_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	mutex_lock(&power_mutex);
	gma_resume_pci(pdev);
	gma_resume_display(pdev);
	psb_irq_preinstall(dev);
	psb_irq_postinstall(dev);
	mutex_unlock(&power_mutex);
	return 0;
}



/**
 *	gma_power_is_on		-	returne true if power is on
 *	@dev: our DRM device
 *
 *	Returns true if the display island power is on at this moment
 */
bool gma_power_is_on(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return dev_priv->display_power;
}


/**
 *	gma_power_begin		-	begin requiring power
 *	@dev: our DRM device
 *	@force_on: true to force power on
 *
 *	Begin an action that requires the display power island is enabled.
 *	We refcount the islands.
 *
 *	FIXME: locking
 */
bool gma_power_begin(struct drm_device *dev, bool force_on)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	/* Power already on ? */
	if (dev_priv->display_power) {
		dev_priv->display_count++;
		pm_runtime_get(&dev->pdev->dev);
		return true;
	}
	if (force_on == false)
		return false;

	/* Ok power up needed */
	ret = gma_resume_pci(dev->pdev);
	if (ret == 0) {
		psb_irq_preinstall(dev);
		psb_irq_postinstall(dev);
		pm_runtime_get(&dev->pdev->dev);
		dev_priv->display_count++;
		return true;
	}
	return false;
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
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->display_count--;
	WARN_ON(dev_priv->display_count < 0);
	pm_runtime_put(&dev->pdev->dev);
}

int psb_runtime_suspend(struct device *dev)
{
	static pm_message_t dummy;
	return gma_power_suspend(to_pci_dev(dev), dummy);
}

int psb_runtime_resume(struct device *dev)
{
	return 0;
}

int psb_runtime_idle(struct device *dev)
{
	struct drm_device *drmdev = pci_get_drvdata(to_pci_dev(dev));
	struct drm_psb_private *dev_priv = drmdev->dev_private;
	if (dev_priv->display_count)
		return 0;
	else
		return 1;
}

