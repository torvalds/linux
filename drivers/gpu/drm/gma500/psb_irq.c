// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 **************************************************************************/

#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>

#include "power.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_irq.h"
#include "psb_reg.h"

/*
 * inline functions
 */

static inline u32 gma_pipestat(int pipe)
{
	if (pipe == 0)
		return PIPEASTAT;
	if (pipe == 1)
		return PIPEBSTAT;
	if (pipe == 2)
		return PIPECSTAT;
	BUG();
}

static inline u32 gma_pipeconf(int pipe)
{
	if (pipe == 0)
		return PIPEACONF;
	if (pipe == 1)
		return PIPEBCONF;
	if (pipe == 2)
		return PIPECCONF;
	BUG();
}

void gma_enable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != mask) {
		u32 reg = gma_pipestat(pipe);
		dev_priv->pipestat[pipe] |= mask;
		/* Enable the interrupt, clear any pending status */
		if (gma_power_begin(&dev_priv->dev, false)) {
			u32 writeVal = PSB_RVDC32(reg);
			writeVal |= (mask | (mask >> 16));
			PSB_WVDC32(writeVal, reg);
			(void) PSB_RVDC32(reg);
			gma_power_end(&dev_priv->dev);
		}
	}
}

void gma_disable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != 0) {
		u32 reg = gma_pipestat(pipe);
		dev_priv->pipestat[pipe] &= ~mask;
		if (gma_power_begin(&dev_priv->dev, false)) {
			u32 writeVal = PSB_RVDC32(reg);
			writeVal &= ~mask;
			PSB_WVDC32(writeVal, reg);
			(void) PSB_RVDC32(reg);
			gma_power_end(&dev_priv->dev);
		}
	}
}

/*
 * Display controller interrupt handler for pipe event.
 */
static void gma_pipe_event_handler(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	uint32_t pipe_stat_val = 0;
	uint32_t pipe_stat_reg = gma_pipestat(pipe);
	uint32_t pipe_enable = dev_priv->pipestat[pipe];
	uint32_t pipe_status = dev_priv->pipestat[pipe] >> 16;
	uint32_t pipe_clear;
	uint32_t i = 0;

	spin_lock(&dev_priv->irqmask_lock);

	pipe_stat_val = PSB_RVDC32(pipe_stat_reg);
	pipe_stat_val &= pipe_enable | pipe_status;
	pipe_stat_val &= pipe_stat_val >> 16;

	spin_unlock(&dev_priv->irqmask_lock);

	/* Clear the 2nd level interrupt status bits
	 * Sometimes the bits are very sticky so we repeat until they unstick */
	for (i = 0; i < 0xffff; i++) {
		PSB_WVDC32(PSB_RVDC32(pipe_stat_reg), pipe_stat_reg);
		pipe_clear = PSB_RVDC32(pipe_stat_reg) & pipe_status;

		if (pipe_clear == 0)
			break;
	}

	if (pipe_clear)
		dev_err(dev->dev,
		"%s, can't clear status bits for pipe %d, its value = 0x%x.\n",
		__func__, pipe, PSB_RVDC32(pipe_stat_reg));

	if (pipe_stat_val & PIPE_VBLANK_STATUS) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);
		struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
		unsigned long flags;

		drm_handle_vblank(dev, pipe);

		spin_lock_irqsave(&dev->event_lock, flags);
		if (gma_crtc->page_flip_event) {
			drm_crtc_send_vblank_event(crtc,
						   gma_crtc->page_flip_event);
			gma_crtc->page_flip_event = NULL;
			drm_crtc_vblank_put(crtc);
		}
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

/*
 * Display controller interrupt handler.
 */
static void gma_vdc_interrupt(struct drm_device *dev, uint32_t vdc_stat)
{
	if (vdc_stat & _PSB_IRQ_ASLE)
		psb_intel_opregion_asle_intr(dev);

	if (vdc_stat & _PSB_VSYNC_PIPEA_FLAG)
		gma_pipe_event_handler(dev, 0);

	if (vdc_stat & _PSB_VSYNC_PIPEB_FLAG)
		gma_pipe_event_handler(dev, 1);
}

/*
 * SGX interrupt handler
 */
static void gma_sgx_interrupt(struct drm_device *dev, u32 stat_1, u32 stat_2)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	u32 val, addr;

	if (stat_1 & _PSB_CE_TWOD_COMPLETE)
		val = PSB_RSGX32(PSB_CR_2D_BLIT_STATUS);

	if (stat_2 & _PSB_CE2_BIF_REQUESTER_FAULT) {
		val = PSB_RSGX32(PSB_CR_BIF_INT_STAT);
		addr = PSB_RSGX32(PSB_CR_BIF_FAULT);
		if (val) {
			if (val & _PSB_CBI_STAT_PF_N_RW)
				DRM_ERROR("SGX MMU page fault:");
			else
				DRM_ERROR("SGX MMU read / write protection fault:");

			if (val & _PSB_CBI_STAT_FAULT_CACHE)
				DRM_ERROR("\tCache requestor");
			if (val & _PSB_CBI_STAT_FAULT_TA)
				DRM_ERROR("\tTA requestor");
			if (val & _PSB_CBI_STAT_FAULT_VDM)
				DRM_ERROR("\tVDM requestor");
			if (val & _PSB_CBI_STAT_FAULT_2D)
				DRM_ERROR("\t2D requestor");
			if (val & _PSB_CBI_STAT_FAULT_PBE)
				DRM_ERROR("\tPBE requestor");
			if (val & _PSB_CBI_STAT_FAULT_TSP)
				DRM_ERROR("\tTSP requestor");
			if (val & _PSB_CBI_STAT_FAULT_ISP)
				DRM_ERROR("\tISP requestor");
			if (val & _PSB_CBI_STAT_FAULT_USSEPDS)
				DRM_ERROR("\tUSSEPDS requestor");
			if (val & _PSB_CBI_STAT_FAULT_HOST)
				DRM_ERROR("\tHost requestor");

			DRM_ERROR("\tMMU failing address is 0x%08x.\n",
				  (unsigned int)addr);
		}
	}

	/* Clear bits */
	PSB_WSGX32(stat_1, PSB_CR_EVENT_HOST_CLEAR);
	PSB_WSGX32(stat_2, PSB_CR_EVENT_HOST_CLEAR2);
	PSB_RSGX32(PSB_CR_EVENT_HOST_CLEAR2);
}

static irqreturn_t gma_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	uint32_t vdc_stat, dsp_int = 0, sgx_int = 0, hotplug_int = 0;
	u32 sgx_stat_1, sgx_stat_2;
	int handled = 0;

	spin_lock(&dev_priv->irqmask_lock);

	vdc_stat = PSB_RVDC32(PSB_INT_IDENTITY_R);

	if (vdc_stat & (_PSB_PIPE_EVENT_FLAG|_PSB_IRQ_ASLE))
		dsp_int = 1;

	if (vdc_stat & _PSB_IRQ_SGX_FLAG)
		sgx_int = 1;
	if (vdc_stat & _PSB_IRQ_DISP_HOTSYNC)
		hotplug_int = 1;

	vdc_stat &= dev_priv->vdc_irq_mask;
	spin_unlock(&dev_priv->irqmask_lock);

	if (dsp_int) {
		gma_vdc_interrupt(dev, vdc_stat);
		handled = 1;
	}

	if (sgx_int) {
		sgx_stat_1 = PSB_RSGX32(PSB_CR_EVENT_STATUS);
		sgx_stat_2 = PSB_RSGX32(PSB_CR_EVENT_STATUS2);
		gma_sgx_interrupt(dev, sgx_stat_1, sgx_stat_2);
		handled = 1;
	}

	/* Note: this bit has other meanings on some devices, so we will
	   need to address that later if it ever matters */
	if (hotplug_int && dev_priv->ops->hotplug) {
		handled = dev_priv->ops->hotplug(dev);
		REG_WRITE(PORT_HOTPLUG_STAT, REG_READ(PORT_HOTPLUG_STAT));
	}

	PSB_WVDC32(vdc_stat, PSB_INT_IDENTITY_R);
	(void) PSB_RVDC32(PSB_INT_IDENTITY_R);
	rmb();

	if (!handled)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

void gma_irq_preinstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);
	PSB_WVDC32(0x00000000, PSB_INT_MASK_R);
	PSB_WVDC32(0x00000000, PSB_INT_ENABLE_R);
	PSB_WSGX32(0x00000000, PSB_CR_EVENT_HOST_ENABLE);
	PSB_RSGX32(PSB_CR_EVENT_HOST_ENABLE);

	if (dev->vblank[0].enabled)
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEA_FLAG;
	if (dev->vblank[1].enabled)
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEB_FLAG;

	/* Revisit this area - want per device masks ? */
	if (dev_priv->ops->hotplug)
		dev_priv->vdc_irq_mask |= _PSB_IRQ_DISP_HOTSYNC;
	dev_priv->vdc_irq_mask |= _PSB_IRQ_ASLE | _PSB_IRQ_SGX_FLAG;

	/* This register is safe even if display island is off */
	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

void gma_irq_postinstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned long irqflags;
	unsigned int i;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	/* Enable 2D and MMU fault interrupts */
	PSB_WSGX32(_PSB_CE2_BIF_REQUESTER_FAULT, PSB_CR_EVENT_HOST_ENABLE2);
	PSB_WSGX32(_PSB_CE_TWOD_COMPLETE, PSB_CR_EVENT_HOST_ENABLE);
	PSB_RSGX32(PSB_CR_EVENT_HOST_ENABLE); /* Post */

	/* This register is safe even if display island is off */
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);

	for (i = 0; i < dev->num_crtcs; ++i) {
		if (dev->vblank[i].enabled)
			gma_enable_pipestat(dev_priv, i, PIPE_VBLANK_INTERRUPT_ENABLE);
		else
			gma_disable_pipestat(dev_priv, i, PIPE_VBLANK_INTERRUPT_ENABLE);
	}

	if (dev_priv->ops->hotplug_enable)
		dev_priv->ops->hotplug_enable(dev, true);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

int gma_irq_install(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;

	if (dev_priv->use_msi && pci_enable_msi(pdev)) {
		dev_warn(dev->dev, "Enabling MSI failed!\n");
		dev_priv->use_msi = false;
	}

	if (pdev->irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	gma_irq_preinstall(dev);

	/* PCI devices require shared interrupts. */
	ret = request_irq(pdev->irq, gma_irq_handler, IRQF_SHARED, dev->driver->name, dev);
	if (ret)
		return ret;

	gma_irq_postinstall(dev);

	return 0;
}

void gma_irq_uninstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned long irqflags;
	unsigned int i;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (dev_priv->ops->hotplug_enable)
		dev_priv->ops->hotplug_enable(dev, false);

	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);

	for (i = 0; i < dev->num_crtcs; ++i) {
		if (dev->vblank[i].enabled)
			gma_disable_pipestat(dev_priv, i, PIPE_VBLANK_INTERRUPT_ENABLE);
	}

	dev_priv->vdc_irq_mask &= _PSB_IRQ_SGX_FLAG |
				  _PSB_IRQ_MSVDX_FLAG |
				  _LNC_IRQ_TOPAZ_FLAG;

	/* These two registers are safe even if display island is off */
	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);

	wmb();

	/* This register is safe even if display island is off */
	PSB_WVDC32(PSB_RVDC32(PSB_INT_IDENTITY_R), PSB_INT_IDENTITY_R);
	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	free_irq(pdev->irq, dev);
	if (dev_priv->use_msi)
		pci_disable_msi(pdev);
}

int gma_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned long irqflags;
	uint32_t reg_val = 0;
	uint32_t pipeconf_reg = gma_pipeconf(pipe);

	if (gma_power_begin(dev, false)) {
		reg_val = REG_READ(pipeconf_reg);
		gma_power_end(dev);
	}

	if (!(reg_val & PIPEACONF_ENABLE))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (pipe == 0)
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEA_FLAG;
	else if (pipe == 1)
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEB_FLAG;

	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
	gma_enable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	return 0;
}

void gma_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (pipe == 0)
		dev_priv->vdc_irq_mask &= ~_PSB_VSYNC_PIPEA_FLAG;
	else if (pipe == 1)
		dev_priv->vdc_irq_mask &= ~_PSB_VSYNC_PIPEB_FLAG;

	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
	gma_disable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
u32 gma_crtc_get_vblank_counter(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	uint32_t high_frame = PIPEAFRAMEHIGH;
	uint32_t low_frame = PIPEAFRAMEPIXEL;
	uint32_t pipeconf_reg = PIPEACONF;
	uint32_t reg_val = 0;
	uint32_t high1 = 0, high2 = 0, low = 0, count = 0;

	switch (pipe) {
	case 0:
		break;
	case 1:
		high_frame = PIPEBFRAMEHIGH;
		low_frame = PIPEBFRAMEPIXEL;
		pipeconf_reg = PIPEBCONF;
		break;
	case 2:
		high_frame = PIPECFRAMEHIGH;
		low_frame = PIPECFRAMEPIXEL;
		pipeconf_reg = PIPECCONF;
		break;
	default:
		dev_err(dev->dev, "%s, invalid pipe.\n", __func__);
		return 0;
	}

	if (!gma_power_begin(dev, false))
		return 0;

	reg_val = REG_READ(pipeconf_reg);

	if (!(reg_val & PIPEACONF_ENABLE)) {
		dev_err(dev->dev, "trying to get vblank count for disabled pipe %u\n",
			pipe);
		goto err_gma_power_end;
	}

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = ((REG_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
		low =  ((REG_READ(low_frame) & PIPE_FRAME_LOW_MASK) >>
			PIPE_FRAME_LOW_SHIFT);
		high2 = ((REG_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
	} while (high1 != high2);

	count = (high1 << 8) | low;

err_gma_power_end:
	gma_power_end(dev);

	return count;
}

