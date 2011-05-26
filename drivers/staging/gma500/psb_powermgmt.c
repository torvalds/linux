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
 *
 */
#include "psb_powermgmt.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#undef OSPM_GFX_DPK

extern u32 gui32SGXDeviceID;
extern u32 gui32MRSTDisplayDeviceID;
extern u32 gui32MRSTMSVDXDeviceID;
extern u32 gui32MRSTTOPAZDeviceID;

struct drm_device *gpDrmDevice = NULL;
static struct mutex power_mutex;
static bool gbSuspendInProgress = false;
static bool gbResumeInProgress = false;
static int g_hw_power_status_mask;
static atomic_t g_display_access_count;
static atomic_t g_graphics_access_count;
static atomic_t g_videoenc_access_count;
static atomic_t g_videodec_access_count;
int allow_runtime_pm = 0;

void ospm_power_island_up(int hw_islands);
void ospm_power_island_down(int hw_islands);
static bool gbSuspended = false;
bool gbgfxsuspended = false;

/*
 * ospm_power_init
 *
 * Description: Initialize this ospm power management module
 */
void ospm_power_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = (struct drm_psb_private *)dev->dev_private;

	gpDrmDevice = dev;

	dev_priv->apm_base = dev_priv->apm_reg & 0xffff;
	dev_priv->ospm_base &= 0xffff;

	mutex_init(&power_mutex);
	g_hw_power_status_mask = OSPM_ALL_ISLANDS;
	atomic_set(&g_display_access_count, 0);
	atomic_set(&g_graphics_access_count, 0);
	atomic_set(&g_videoenc_access_count, 0);
	atomic_set(&g_videodec_access_count, 0);
}

/*
 * ospm_power_uninit
 *
 * Description: Uninitialize this ospm power management module
 */
void ospm_power_uninit(void)
{
	mutex_destroy(&power_mutex);
    	pm_runtime_disable(&gpDrmDevice->pdev->dev);
	pm_runtime_set_suspended(&gpDrmDevice->pdev->dev);
}


/*
 * save_display_registers
 *
 * Description: We are going to suspend so save current display
 * register state.
 */
static int save_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc * crtc;
	struct drm_connector * connector;

	/* Display arbitration control + watermarks */
	dev_priv->saveDSPARB = PSB_RVDC32(DSPARB);
	dev_priv->saveDSPFW1 = PSB_RVDC32(DSPFW1);
	dev_priv->saveDSPFW2 = PSB_RVDC32(DSPFW2);
	dev_priv->saveDSPFW3 = PSB_RVDC32(DSPFW3);
	dev_priv->saveDSPFW4 = PSB_RVDC32(DSPFW4);
	dev_priv->saveDSPFW5 = PSB_RVDC32(DSPFW5);
	dev_priv->saveDSPFW6 = PSB_RVDC32(DSPFW6);
	dev_priv->saveCHICKENBIT = PSB_RVDC32(DSPCHICKENBIT);

	/*save crtc and output state*/
	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if(drm_helper_crtc_in_use(crtc)) {
			crtc->funcs->save(crtc);
		}
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		connector->funcs->save(connector);
	}
	mutex_unlock(&dev->mode_config.mutex);

	/* Interrupt state */
	/*
	 * Handled in psb_irq.c
	 */

	return 0;
}

/*
 * restore_display_registers
 *
 * Description: We are going to resume so restore display register state.
 */
static int restore_display_registers(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_crtc * crtc;
	struct drm_connector * connector;

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
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if(drm_helper_crtc_in_use(crtc))
			crtc->funcs->restore(crtc);
	}
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		connector->funcs->restore(connector);
	}
	mutex_unlock(&dev->mode_config.mutex);

	/*Interrupt state*/
	/*
	 * Handled in psb_irq.c
	 */

	return 0;
}
/*
 * powermgmt_suspend_display
 *
 * Description: Suspend the display hardware saving state and disabling
 * as necessary.
 */
void ospm_suspend_display(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int pp_stat, ret=0;

	printk(KERN_ALERT "%s \n", __func__);

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "%s \n", __func__);
#endif
	if (!(g_hw_power_status_mask & OSPM_DISPLAY_ISLAND))
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
		ret = 0;
	}
	ospm_power_island_down(OSPM_DISPLAY_ISLAND);
}

/*
 * ospm_resume_display
 *
 * Description: Resume the display hardware restoring state and enabling
 * as necessary.
 */
void ospm_resume_display(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;

	printk(KERN_ALERT "%s \n", __func__);

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "%s \n", __func__);
#endif
	if (g_hw_power_status_mask & OSPM_DISPLAY_ISLAND)
		return;

	/* turn on the display power island */
	ospm_power_island_up(OSPM_DISPLAY_ISLAND);

	PSB_WVDC32(pg->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			pg->gmch_ctrl | _PSB_GMCH_ENABLED);

	/* Don't reinitialize the GTT as it is unnecessary.  The gtt is
	 * stored in memory so it will automatically be restored.  All
	 * we need to do is restore the PGETBL_CTL which we already do
	 * above.
	 */
	/*psb_gtt_init(dev_priv->pg, 1);*/

	restore_display_registers(dev);
}

#if 1
/*
 * ospm_suspend_pci
 *
 * Description: Suspend the pci device saving state and disabling
 * as necessary.
 */
static void ospm_suspend_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int bsm, vbt;

	if (gbSuspended)
		return;

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "ospm_suspend_pci\n");
#endif

	pci_save_state(pdev);
	pci_read_config_dword(pdev, 0x5C, &bsm);
	dev_priv->saveBSM = bsm;
	pci_read_config_dword(pdev, 0xFC, &vbt);
	dev_priv->saveVBT = vbt;
	pci_read_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, &dev_priv->msi_addr);
	pci_read_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, &dev_priv->msi_data);

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	gbSuspended = true;
	gbgfxsuspended = true;
}

/*
 * ospm_resume_pci
 *
 * Description: Resume the pci device restoring state and enabling
 * as necessary.
 */
static bool ospm_resume_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret = 0;

	if (!gbSuspended)
		return true;

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "ospm_resume_pci\n");
#endif

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_write_config_dword(pdev, 0x5c, dev_priv->saveBSM);
	pci_write_config_dword(pdev, 0xFC, dev_priv->saveVBT);
	/* retoring MSI address and data in PCIx space */
	pci_write_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, dev_priv->msi_addr);
	pci_write_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, dev_priv->msi_data);
	ret = pci_enable_device(pdev);

	if (ret != 0)
		printk(KERN_ALERT "ospm_resume_pci: pci_enable_device failed: %d\n", ret);
	else
		gbSuspended = false;

	return !gbSuspended;
}
#endif
/*
 * ospm_power_suspend
 *
 * Description: OSPM is telling our driver to suspend so save state
 * and power down all hardware.
 */
int ospm_power_suspend(struct pci_dev *pdev, pm_message_t state)
{
        int ret = 0;
        int graphics_access_count;
        int videoenc_access_count;
        int videodec_access_count;
        int display_access_count;
    	bool suspend_pci = true;

	if(gbSuspendInProgress || gbResumeInProgress)
        {
#ifdef OSPM_GFX_DPK
                printk(KERN_ALERT "OSPM_GFX_DPK: %s system BUSY \n", __func__);
#endif
                return  -EBUSY;
        }

        mutex_lock(&power_mutex);

        if (!gbSuspended) {
                graphics_access_count = atomic_read(&g_graphics_access_count);
                videoenc_access_count = atomic_read(&g_videoenc_access_count);
                videodec_access_count = atomic_read(&g_videodec_access_count);
                display_access_count = atomic_read(&g_display_access_count);

                if (graphics_access_count ||
			videoenc_access_count ||
			videodec_access_count ||
			display_access_count)
                        ret = -EBUSY;

                if (!ret) {
                        gbSuspendInProgress = true;

                        psb_irq_uninstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
                        ospm_suspend_display(gpDrmDevice);
                        if (suspend_pci == true) {
				ospm_suspend_pci(pdev);
                        }
                        gbSuspendInProgress = false;
                } else {
                        printk(KERN_ALERT "ospm_power_suspend: device busy: graphics %d videoenc %d videodec %d display %d\n", graphics_access_count, videoenc_access_count, videodec_access_count, display_access_count);
                }
        }


        mutex_unlock(&power_mutex);
        return ret;
}

/*
 * ospm_power_island_up
 *
 * Description: Restore power to the specified island(s) (powergating)
 */
void ospm_power_island_up(int hw_islands)
{
	u32 pwr_cnt = 0;
	u32 pwr_sts = 0;
	u32 pwr_mask = 0;

	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) gpDrmDevice->dev_private;


	if (hw_islands & OSPM_DISPLAY_ISLAND) {
		pwr_mask = PSB_PWRGT_DISPLAY_MASK;

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

	g_hw_power_status_mask |= hw_islands;
}

/*
 * ospm_power_resume
 */
int ospm_power_resume(struct pci_dev *pdev)
{
	if(gbSuspendInProgress || gbResumeInProgress)
        {
#ifdef OSPM_GFX_DPK
                printk(KERN_ALERT "OSPM_GFX_DPK: %s hw_island: Suspend || gbResumeInProgress!!!! \n", __func__);
#endif
                return 0;
        }

        mutex_lock(&power_mutex);

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "OSPM_GFX_DPK: ospm_power_resume \n");
#endif

  	gbResumeInProgress = true;

        ospm_resume_pci(pdev);

	ospm_resume_display(gpDrmDevice->pdev);
        psb_irq_preinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
        psb_irq_postinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);

	gbResumeInProgress = false;

        mutex_unlock(&power_mutex);

	return 0;
}


/*
 * ospm_power_island_down
 *
 * Description: Cut power to the specified island(s) (powergating)
 */
void ospm_power_island_down(int islands)
{
#if 0
	u32 pwr_cnt = 0;
	u32 pwr_mask = 0;
	u32 pwr_sts = 0;

	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) gpDrmDevice->dev_private;

	g_hw_power_status_mask &= ~islands;

	if (islands & OSPM_GRAPHICS_ISLAND) {
		pwr_cnt |= PSB_PWRGT_GFX_MASK;
		pwr_mask |= PSB_PWRGT_GFX_MASK;
		if (dev_priv->graphics_state == PSB_PWR_STATE_ON) {
			dev_priv->gfx_on_time += (jiffies - dev_priv->gfx_last_mode_change) * 1000 / HZ;
			dev_priv->gfx_last_mode_change = jiffies;
			dev_priv->graphics_state = PSB_PWR_STATE_OFF;
			dev_priv->gfx_off_cnt++;
		}
	}
	if (islands & OSPM_VIDEO_ENC_ISLAND) {
		pwr_cnt |= PSB_PWRGT_VID_ENC_MASK;
		pwr_mask |= PSB_PWRGT_VID_ENC_MASK;
	}
	if (islands & OSPM_VIDEO_DEC_ISLAND) {
		pwr_cnt |= PSB_PWRGT_VID_DEC_MASK;
		pwr_mask |= PSB_PWRGT_VID_DEC_MASK;
	}
	if (pwr_cnt) {
		pwr_cnt |= inl(dev_priv->apm_base);
		outl(pwr_cnt, dev_priv->apm_base  + PSB_APM_CMD);
		while (true) {
			pwr_sts = inl(dev_priv->apm_base + PSB_APM_STS);

			if ((pwr_sts & pwr_mask) == pwr_mask)
				break;
			else
				udelay(10);
		}
	}

	if (islands & OSPM_DISPLAY_ISLAND) {
		pwr_mask = PSB_PWRGT_DISPLAY_MASK;

		outl(pwr_mask, (dev_priv->ospm_base + PSB_PM_SSC));

		while (true) {
			pwr_sts = inl(dev_priv->ospm_base + PSB_PM_SSS);
			if ((pwr_sts & pwr_mask) == pwr_mask)
				break;
			else
				udelay(10);
		}
	}
#endif
}


/*
 * ospm_power_is_hw_on
 *
 * Description: do an instantaneous check for if the specified islands
 * are on.  Only use this in cases where you know the g_state_change_mutex
 * is already held such as in irq install/uninstall.  Otherwise, use
 * ospm_power_using_hw_begin().
 */
bool ospm_power_is_hw_on(int hw_islands)
{
	return ((g_hw_power_status_mask & hw_islands) == hw_islands) ? true:false;
}

/*
 * ospm_power_using_hw_begin
 *
 * Description: Notify PowerMgmt module that you will be accessing the
 * specified island's hw so don't power it off.  If force_on is true,
 * this will power on the specified island if it is off.
 * Otherwise, this will return false and the caller is expected to not
 * access the hw.
 *
 * NOTE *** If this is called from and interrupt handler or other atomic
 * context, then it will return false if we are in the middle of a
 * power state transition and the caller will be expected to handle that
 * even if force_on is set to true.
 */
bool ospm_power_using_hw_begin(int hw_island, UHBUsage usage)
{
        return 1;	/*FIXMEAC */
#if 0
	bool ret = true;
	bool island_is_off = false;
	bool b_atomic = (in_interrupt() || in_atomic());
	bool locked = true;
	struct pci_dev *pdev = gpDrmDevice->pdev;
	u32 deviceID = 0;
	bool force_on = usage ? true: false;
	/*quick path, not 100% race safe, but should be enough comapre to current other code in this file */
	if (!force_on) {
		if (hw_island & (OSPM_ALL_ISLANDS & ~g_hw_power_status_mask))
			return false;
		else {
			locked = false;
#ifdef CONFIG_PM_RUNTIME
			/* increment pm_runtime_refcount */
			pm_runtime_get(&pdev->dev);
#endif
			goto increase_count;
		}
	}


	if (!b_atomic)
		mutex_lock(&power_mutex);

	island_is_off = hw_island & (OSPM_ALL_ISLANDS & ~g_hw_power_status_mask);

	if (b_atomic && (gbSuspendInProgress || gbResumeInProgress || gbSuspended) && force_on && island_is_off)
		ret = false;

	if (ret && island_is_off && !force_on)
		ret = false;

	if (ret && island_is_off && force_on) {
		gbResumeInProgress = true;

		ret = ospm_resume_pci(pdev);

		if (ret) {
			switch(hw_island)
			{
			case OSPM_DISPLAY_ISLAND:
				deviceID = gui32MRSTDisplayDeviceID;
				ospm_resume_display(pdev);
				psb_irq_preinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
				psb_irq_postinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
				break;
			case OSPM_GRAPHICS_ISLAND:
				deviceID = gui32SGXDeviceID;
				ospm_power_island_up(OSPM_GRAPHICS_ISLAND);
				psb_irq_preinstall_islands(gpDrmDevice, OSPM_GRAPHICS_ISLAND);
				psb_irq_postinstall_islands(gpDrmDevice, OSPM_GRAPHICS_ISLAND);
				break;
#if 1
			case OSPM_VIDEO_DEC_ISLAND:
				if(!ospm_power_is_hw_on(OSPM_DISPLAY_ISLAND)) {
					//printk(KERN_ALERT "%s power on display for video decode use\n", __func__);
					deviceID = gui32MRSTDisplayDeviceID;
					ospm_resume_display(pdev);
					psb_irq_preinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
					psb_irq_postinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
				}
				else{
					//printk(KERN_ALERT "%s display is already on for video decode use\n", __func__);
				}

				if(!ospm_power_is_hw_on(OSPM_VIDEO_DEC_ISLAND)) {
					//printk(KERN_ALERT "%s power on video decode\n", __func__);
					deviceID = gui32MRSTMSVDXDeviceID;
					ospm_power_island_up(OSPM_VIDEO_DEC_ISLAND);
					psb_irq_preinstall_islands(gpDrmDevice, OSPM_VIDEO_DEC_ISLAND);
					psb_irq_postinstall_islands(gpDrmDevice, OSPM_VIDEO_DEC_ISLAND);
				}
				else{
					//printk(KERN_ALERT "%s video decode is already on\n", __func__);
				}

				break;
			case OSPM_VIDEO_ENC_ISLAND:
				if(!ospm_power_is_hw_on(OSPM_DISPLAY_ISLAND)) {
					//printk(KERN_ALERT "%s power on display for video encode\n", __func__);
					deviceID = gui32MRSTDisplayDeviceID;
					ospm_resume_display(pdev);
					psb_irq_preinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
					psb_irq_postinstall_islands(gpDrmDevice, OSPM_DISPLAY_ISLAND);
				}
				else{
					//printk(KERN_ALERT "%s display is already on for video encode use\n", __func__);
				}

				if(!ospm_power_is_hw_on(OSPM_VIDEO_ENC_ISLAND)) {
					//printk(KERN_ALERT "%s power on video encode\n", __func__);
					deviceID = gui32MRSTTOPAZDeviceID;
					ospm_power_island_up(OSPM_VIDEO_ENC_ISLAND);
					psb_irq_preinstall_islands(gpDrmDevice, OSPM_VIDEO_ENC_ISLAND);
					psb_irq_postinstall_islands(gpDrmDevice, OSPM_VIDEO_ENC_ISLAND);
				}
				else{
					//printk(KERN_ALERT "%s video decode is already on\n", __func__);
				}
#endif
				break;

			default:
				printk(KERN_ALERT "%s unknown island !!!! \n", __func__);
				break;
			}

		}

		if (!ret)
			printk(KERN_ALERT "ospm_power_using_hw_begin: forcing on %d failed\n", hw_island);

		gbResumeInProgress = false;
	}
increase_count:
	if (ret) {
		switch(hw_island)
		{
		case OSPM_GRAPHICS_ISLAND:
			atomic_inc(&g_graphics_access_count);
			break;
		case OSPM_VIDEO_ENC_ISLAND:
			atomic_inc(&g_videoenc_access_count);
			break;
		case OSPM_VIDEO_DEC_ISLAND:
			atomic_inc(&g_videodec_access_count);
			break;
		case OSPM_DISPLAY_ISLAND:
			atomic_inc(&g_display_access_count);
			break;
		}
	}

	if (!b_atomic && locked)
		mutex_unlock(&power_mutex);

	return ret;
#endif
}


/*
 * ospm_power_using_hw_end
 *
 * Description: Notify PowerMgmt module that you are done accessing the
 * specified island's hw so feel free to power it off.  Note that this
 * function doesn't actually power off the islands.
 */
void ospm_power_using_hw_end(int hw_island)
{
#if 0 /* FIXMEAC */
	switch(hw_island)
	{
	case OSPM_GRAPHICS_ISLAND:
		atomic_dec(&g_graphics_access_count);
		break;
	case OSPM_VIDEO_ENC_ISLAND:
		atomic_dec(&g_videoenc_access_count);
		break;
	case OSPM_VIDEO_DEC_ISLAND:
		atomic_dec(&g_videodec_access_count);
		break;
	case OSPM_DISPLAY_ISLAND:
		atomic_dec(&g_display_access_count);
		break;
	}

	//decrement runtime pm ref count
	pm_runtime_put(&gpDrmDevice->pdev->dev);

	WARN_ON(atomic_read(&g_graphics_access_count) < 0);
	WARN_ON(atomic_read(&g_videoenc_access_count) < 0);
	WARN_ON(atomic_read(&g_videodec_access_count) < 0);
	WARN_ON(atomic_read(&g_display_access_count) < 0);
#endif
}

int ospm_runtime_pm_allow(struct drm_device * dev)
{
	return 0;
}

void ospm_runtime_pm_forbid(struct drm_device * dev)
{
	struct drm_psb_private * dev_priv = dev->dev_private;

	DRM_INFO("%s\n", __FUNCTION__);

	pm_runtime_forbid(&dev->pdev->dev);
	dev_priv->rpm_enabled = 0;
}

int psb_runtime_suspend(struct device *dev)
{
	pm_message_t state;
	int ret = 0;
	state.event = 0;

#ifdef OSPM_GFX_DPK
	printk(KERN_ALERT "OSPM_GFX_DPK: %s \n", __func__);
#endif
        if (atomic_read(&g_graphics_access_count) || atomic_read(&g_videoenc_access_count)
		|| atomic_read(&g_videodec_access_count) || atomic_read(&g_display_access_count)){
#ifdef OSPM_GFX_DPK
                printk(KERN_ALERT "OSPM_GFX_DPK: GFX: %d VEC: %d VED: %d DC: %d DSR: %d \n", atomic_read(&g_graphics_access_count),
			atomic_read(&g_videoenc_access_count), atomic_read(&g_videodec_access_count), atomic_read(&g_display_access_count));
#endif
                return -EBUSY;
        }
        else
		ret = ospm_power_suspend(gpDrmDevice->pdev, state);

	return ret;
}

int psb_runtime_resume(struct device *dev)
{
	return 0;
}

int psb_runtime_idle(struct device *dev)
{
	/*printk (KERN_ALERT "lvds:%d,mipi:%d\n", dev_priv->is_lvds_on, dev_priv->is_mipi_on);*/
	if (atomic_read(&g_graphics_access_count) || atomic_read(&g_videoenc_access_count)
		|| atomic_read(&g_videodec_access_count) || atomic_read(&g_display_access_count))
		return 1;
	else
		return 0;
}

