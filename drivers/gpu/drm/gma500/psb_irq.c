/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 **************************************************************************/
/*
 */

#include <drm/drmP.h>
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "power.h"
#include "psb_irq.h"
#include "mdfld_output.h"

/*
 * inline functions
 */

static inline u32
psb_pipestat(int pipe)
{
	if (pipe == 0)
		return PIPEASTAT;
	if (pipe == 1)
		return PIPEBSTAT;
	if (pipe == 2)
		return PIPECSTAT;
	BUG();
}

static inline u32
mid_pipe_event(int pipe)
{
	if (pipe == 0)
		return _PSB_PIPEA_EVENT_FLAG;
	if (pipe == 1)
		return _MDFLD_PIPEB_EVENT_FLAG;
	if (pipe == 2)
		return _MDFLD_PIPEC_EVENT_FLAG;
	BUG();
}

static inline u32
mid_pipe_vsync(int pipe)
{
	if (pipe == 0)
		return _PSB_VSYNC_PIPEA_FLAG;
	if (pipe == 1)
		return _PSB_VSYNC_PIPEB_FLAG;
	if (pipe == 2)
		return _MDFLD_PIPEC_VBLANK_FLAG;
	BUG();
}

static inline u32
mid_pipeconf(int pipe)
{
	if (pipe == 0)
		return PIPEACONF;
	if (pipe == 1)
		return PIPEBCONF;
	if (pipe == 2)
		return PIPECCONF;
	BUG();
}

void
psb_enable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != mask) {
		u32 reg = psb_pipestat(pipe);
		dev_priv->pipestat[pipe] |= mask;
		/* Enable the interrupt, clear any pending status */
		if (gma_power_begin(dev_priv->dev, false)) {
			u32 writeVal = PSB_RVDC32(reg);
			writeVal |= (mask | (mask >> 16));
			PSB_WVDC32(writeVal, reg);
			(void) PSB_RVDC32(reg);
			gma_power_end(dev_priv->dev);
		}
	}
}

void
psb_disable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != 0) {
		u32 reg = psb_pipestat(pipe);
		dev_priv->pipestat[pipe] &= ~mask;
		if (gma_power_begin(dev_priv->dev, false)) {
			u32 writeVal = PSB_RVDC32(reg);
			writeVal &= ~mask;
			PSB_WVDC32(writeVal, reg);
			(void) PSB_RVDC32(reg);
			gma_power_end(dev_priv->dev);
		}
	}
}

static void mid_enable_pipe_event(struct drm_psb_private *dev_priv, int pipe)
{
	if (gma_power_begin(dev_priv->dev, false)) {
		u32 pipe_event = mid_pipe_event(pipe);
		dev_priv->vdc_irq_mask |= pipe_event;
		PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
		PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
		gma_power_end(dev_priv->dev);
	}
}

static void mid_disable_pipe_event(struct drm_psb_private *dev_priv, int pipe)
{
	if (dev_priv->pipestat[pipe] == 0) {
		if (gma_power_begin(dev_priv->dev, false)) {
			u32 pipe_event = mid_pipe_event(pipe);
			dev_priv->vdc_irq_mask &= ~pipe_event;
			PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
			PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
			gma_power_end(dev_priv->dev);
		}
	}
}

/**
 * Display controller interrupt handler for pipe event.
 *
 */
static void mid_pipe_event_handler(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;

	uint32_t pipe_stat_val = 0;
	uint32_t pipe_stat_reg = psb_pipestat(pipe);
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

	if (pipe_stat_val & PIPE_VBLANK_STATUS)
		drm_handle_vblank(dev, pipe);

	if (pipe_stat_val & PIPE_TE_STATUS)
		drm_handle_vblank(dev, pipe);
}

/*
 * Display controller interrupt handler.
 */
static void psb_vdc_interrupt(struct drm_device *dev, uint32_t vdc_stat)
{
	if (vdc_stat & _PSB_IRQ_ASLE)
		psb_intel_opregion_asle_intr(dev);

	if (vdc_stat & _PSB_VSYNC_PIPEA_FLAG)
		mid_pipe_event_handler(dev, 0);

	if (vdc_stat & _PSB_VSYNC_PIPEB_FLAG)
		mid_pipe_event_handler(dev, 1);
}

irqreturn_t psb_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = arg;
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t vdc_stat, dsp_int = 0, sgx_int = 0, hotplug_int = 0;
	int handled = 0;

	spin_lock(&dev_priv->irqmask_lock);

	vdc_stat = PSB_RVDC32(PSB_INT_IDENTITY_R);

	if (vdc_stat & _PSB_PIPE_EVENT_FLAG)
		dsp_int = 1;

	/* FIXME: Handle Medfield
	if (vdc_stat & _MDFLD_DISP_ALL_IRQ_FLAG)
		dsp_int = 1;
	*/

	if (vdc_stat & _PSB_IRQ_SGX_FLAG)
		sgx_int = 1;
	if (vdc_stat & _PSB_IRQ_DISP_HOTSYNC)
		hotplug_int = 1;

	vdc_stat &= dev_priv->vdc_irq_mask;
	spin_unlock(&dev_priv->irqmask_lock);

	if (dsp_int && gma_power_is_on(dev)) {
		psb_vdc_interrupt(dev, vdc_stat);
		handled = 1;
	}

	if (sgx_int) {
		/* Not expected - we have it masked, shut it up */
		u32 s, s2;
		s = PSB_RSGX32(PSB_CR_EVENT_STATUS);
		s2 = PSB_RSGX32(PSB_CR_EVENT_STATUS2);
		PSB_WSGX32(s, PSB_CR_EVENT_HOST_CLEAR);
		PSB_WSGX32(s2, PSB_CR_EVENT_HOST_CLEAR2);
		/* if s & _PSB_CE_TWOD_COMPLETE we have 2D done but
		   we may as well poll even if we add that ! */
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
	DRM_READMEMORYBARRIER();

	if (!handled)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

void psb_irq_preinstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (gma_power_is_on(dev))
		PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);
	if (dev->vblank_enabled[0])
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEA_FLAG;
	if (dev->vblank_enabled[1])
		dev_priv->vdc_irq_mask |= _PSB_VSYNC_PIPEB_FLAG;

	/* FIXME: Handle Medfield irq mask
	if (dev->vblank_enabled[1])
		dev_priv->vdc_irq_mask |= _MDFLD_PIPEB_EVENT_FLAG;
	if (dev->vblank_enabled[2])
		dev_priv->vdc_irq_mask |= _MDFLD_PIPEC_EVENT_FLAG;
	*/

	/* Revisit this area - want per device masks ? */
	if (dev_priv->ops->hotplug)
		dev_priv->vdc_irq_mask |= _PSB_IRQ_DISP_HOTSYNC;
	dev_priv->vdc_irq_mask |= _PSB_IRQ_ASLE;

	/* This register is safe even if display island is off */
	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

int psb_irq_postinstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	/* This register is safe even if display island is off */
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);

	if (dev->vblank_enabled[0])
		psb_enable_pipestat(dev_priv, 0, PIPE_VBLANK_INTERRUPT_ENABLE);
	else
		psb_disable_pipestat(dev_priv, 0, PIPE_VBLANK_INTERRUPT_ENABLE);

	if (dev->vblank_enabled[1])
		psb_enable_pipestat(dev_priv, 1, PIPE_VBLANK_INTERRUPT_ENABLE);
	else
		psb_disable_pipestat(dev_priv, 1, PIPE_VBLANK_INTERRUPT_ENABLE);

	if (dev->vblank_enabled[2])
		psb_enable_pipestat(dev_priv, 2, PIPE_VBLANK_INTERRUPT_ENABLE);
	else
		psb_disable_pipestat(dev_priv, 2, PIPE_VBLANK_INTERRUPT_ENABLE);

	if (dev_priv->ops->hotplug_enable)
		dev_priv->ops->hotplug_enable(dev, true);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
	return 0;
}

void psb_irq_uninstall(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (dev_priv->ops->hotplug_enable)
		dev_priv->ops->hotplug_enable(dev, false);

	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);

	if (dev->vblank_enabled[0])
		psb_disable_pipestat(dev_priv, 0, PIPE_VBLANK_INTERRUPT_ENABLE);

	if (dev->vblank_enabled[1])
		psb_disable_pipestat(dev_priv, 1, PIPE_VBLANK_INTERRUPT_ENABLE);

	if (dev->vblank_enabled[2])
		psb_disable_pipestat(dev_priv, 2, PIPE_VBLANK_INTERRUPT_ENABLE);

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
}

void psb_irq_turn_on_dpst(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) dev->dev_private;
	u32 hist_reg;
	u32 pwm_reg;

	if (gma_power_begin(dev, false)) {
		PSB_WVDC32(1 << 31, HISTOGRAM_LOGIC_CONTROL);
		hist_reg = PSB_RVDC32(HISTOGRAM_LOGIC_CONTROL);
		PSB_WVDC32(1 << 31, HISTOGRAM_INT_CONTROL);
		hist_reg = PSB_RVDC32(HISTOGRAM_INT_CONTROL);

		PSB_WVDC32(0x80010100, PWM_CONTROL_LOGIC);
		pwm_reg = PSB_RVDC32(PWM_CONTROL_LOGIC);
		PSB_WVDC32(pwm_reg | PWM_PHASEIN_ENABLE
						| PWM_PHASEIN_INT_ENABLE,
							   PWM_CONTROL_LOGIC);
		pwm_reg = PSB_RVDC32(PWM_CONTROL_LOGIC);

		psb_enable_pipestat(dev_priv, 0, PIPE_DPST_EVENT_ENABLE);

		hist_reg = PSB_RVDC32(HISTOGRAM_INT_CONTROL);
		PSB_WVDC32(hist_reg | HISTOGRAM_INT_CTRL_CLEAR,
							HISTOGRAM_INT_CONTROL);
		pwm_reg = PSB_RVDC32(PWM_CONTROL_LOGIC);
		PSB_WVDC32(pwm_reg | 0x80010100 | PWM_PHASEIN_ENABLE,
							PWM_CONTROL_LOGIC);

		gma_power_end(dev);
	}
}

int psb_irq_enable_dpst(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	/* enable DPST */
	mid_enable_pipe_event(dev_priv, 0);
	psb_irq_turn_on_dpst(dev);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
	return 0;
}

void psb_irq_turn_off_dpst(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	u32 hist_reg;
	u32 pwm_reg;

	if (gma_power_begin(dev, false)) {
		PSB_WVDC32(0x00000000, HISTOGRAM_INT_CONTROL);
		hist_reg = PSB_RVDC32(HISTOGRAM_INT_CONTROL);

		psb_disable_pipestat(dev_priv, 0, PIPE_DPST_EVENT_ENABLE);

		pwm_reg = PSB_RVDC32(PWM_CONTROL_LOGIC);
		PSB_WVDC32(pwm_reg & ~PWM_PHASEIN_INT_ENABLE,
							PWM_CONTROL_LOGIC);
		pwm_reg = PSB_RVDC32(PWM_CONTROL_LOGIC);

		gma_power_end(dev);
	}
}

int psb_irq_disable_dpst(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	mid_disable_pipe_event(dev_priv, 0);
	psb_irq_turn_off_dpst(dev);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	return 0;
}

#ifdef PSB_FIXME
static int psb_vblank_do_wait(struct drm_device *dev,
			      unsigned int *sequence, atomic_t *counter)
{
	unsigned int cur_vblank;
	int ret = 0;
	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(counter))
		      - *sequence) <= (1 << 23)));
	*sequence = cur_vblank;

	return ret;
}
#endif

/*
 * It is used to enable VBLANK interrupt
 */
int psb_enable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long irqflags;
	uint32_t reg_val = 0;
	uint32_t pipeconf_reg = mid_pipeconf(pipe);

	/* Medfield is different - we should perhaps extract out vblank
	   and blacklight etc ops */
	if (IS_MFLD(dev))
		return mdfld_enable_te(dev, pipe);

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
	psb_enable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	return 0;
}

/*
 * It is used to disable VBLANK interrupt
 */
void psb_disable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (IS_MFLD(dev))
		mdfld_disable_te(dev, pipe);
	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	if (pipe == 0)
		dev_priv->vdc_irq_mask &= ~_PSB_VSYNC_PIPEA_FLAG;
	else if (pipe == 1)
		dev_priv->vdc_irq_mask &= ~_PSB_VSYNC_PIPEB_FLAG;

	PSB_WVDC32(~dev_priv->vdc_irq_mask, PSB_INT_MASK_R);
	PSB_WVDC32(dev_priv->vdc_irq_mask, PSB_INT_ENABLE_R);
	psb_disable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

/*
 * It is used to enable TE interrupt
 */
int mdfld_enable_te(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;
	uint32_t reg_val = 0;
	uint32_t pipeconf_reg = mid_pipeconf(pipe);

	if (gma_power_begin(dev, false)) {
		reg_val = REG_READ(pipeconf_reg);
		gma_power_end(dev);
	}

	if (!(reg_val & PIPEACONF_ENABLE))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	mid_enable_pipe_event(dev_priv, pipe);
	psb_enable_pipestat(dev_priv, pipe, PIPE_TE_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	return 0;
}

/*
 * It is used to disable TE interrupt
 */
void mdfld_disable_te(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *) dev->dev_private;
	unsigned long irqflags;

	if (!dev_priv->dsr_enable)
		return;

	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);

	mid_disable_pipe_event(dev_priv, pipe);
	psb_disable_pipestat(dev_priv, pipe, PIPE_TE_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
u32 psb_get_vblank_counter(struct drm_device *dev, int pipe)
{
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
		dev_err(dev->dev, "trying to get vblank count for disabled pipe %d\n",
								pipe);
		goto psb_get_vblank_counter_exit;
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

psb_get_vblank_counter_exit:

	gma_power_end(dev);

	return count;
}

