/*
 * Copyright © 2010-2011 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Jim Liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 */

#include "mdfld_dsi_dbi_dpu.h"
#include "mdfld_dsi_dbi.h"

/*
 * NOTE: all mdlfd_x_damage funcs should be called by holding dpu_update_lock
 */

static int mdfld_cursor_damage(struct mdfld_dbi_dpu_info *dpu_info,
			   mdfld_plane_t plane,
			   struct psb_drm_dpu_rect *damaged_rect)
{
	int x, y;
	int new_x, new_y;
	struct psb_drm_dpu_rect *rect;
	struct psb_drm_dpu_rect *pipe_rect;
	int cursor_size;
	struct mdfld_cursor_info *cursor;
	mdfld_plane_t fb_plane;

	if (plane == MDFLD_CURSORA) {
		cursor = &dpu_info->cursors[0];
		x = dpu_info->cursors[0].x;
		y = dpu_info->cursors[0].y;
		cursor_size = dpu_info->cursors[0].size;
		pipe_rect = &dpu_info->damage_pipea;
		fb_plane = MDFLD_PLANEA;
	} else {
		cursor = &dpu_info->cursors[1];
		x = dpu_info->cursors[1].x;
		y = dpu_info->cursors[1].y;
		cursor_size = dpu_info->cursors[1].size;
		pipe_rect = &dpu_info->damage_pipec;
		fb_plane = MDFLD_PLANEC;
	}
	new_x = damaged_rect->x;
	new_y = damaged_rect->y;

	if (x == new_x && y == new_y)
		return 0;

	rect = &dpu_info->damaged_rects[plane];
	/* Move to right */
	if (new_x >= x) {
		if (new_y > y) {
			rect->x = x;
			rect->y = y;
			rect->width = (new_x + cursor_size) - x;
			rect->height = (new_y + cursor_size) - y;
			goto cursor_out;
		} else {
			rect->x = x;
			rect->y = new_y;
			rect->width = (new_x + cursor_size) - x;
			rect->height = (y - new_y);
			goto cursor_out;
		}
	} else {
		if (new_y > y) {
			rect->x = new_x;
			rect->y = y;
			rect->width = (x + cursor_size) - new_x;
			rect->height = new_y - y;
			goto cursor_out;
		} else {
			rect->x = new_x;
			rect->y = new_y;
			rect->width = (x + cursor_size) - new_x;
			rect->height = (y + cursor_size) - new_y;
		}
	}
cursor_out:
	if (new_x < 0)
		cursor->x = 0;
	else if (new_x > 864)
		cursor->x = 864;
	else
		cursor->x = new_x;

	if (new_y < 0)
		cursor->y = 0;
	else if (new_y > 480)
		cursor->y = 480;
	else
		cursor->y = new_y;

	/*
	 * FIXME: this is a workaround for cursor plane update,
	 * remove it later!
	 */
	rect->x = 0;
	rect->y = 0;
	rect->width = 864;
	rect->height = 480;

	mdfld_check_boundary(dpu_info, rect);
	mdfld_dpu_region_extent(pipe_rect, rect);

	/* Update pending status of dpu_info */
	dpu_info->pending |= (1 << plane);
	/* Update fb panel as well */
	dpu_info->pending |= (1 << fb_plane);
	return 0;
}

static int mdfld_fb_damage(struct mdfld_dbi_dpu_info *dpu_info,
				   mdfld_plane_t plane,
				   struct psb_drm_dpu_rect *damaged_rect)
{
	struct psb_drm_dpu_rect *rect;

	if (plane == MDFLD_PLANEA)
		rect = &dpu_info->damage_pipea;
	else
		rect = &dpu_info->damage_pipec;

	mdfld_check_boundary(dpu_info, damaged_rect);

	/* Add fb damage area to this pipe */
	mdfld_dpu_region_extent(rect, damaged_rect);

	/* Update pending status of dpu_info */
	dpu_info->pending |= (1 << plane);
	return 0;
}

/* Do nothing here, right now */
static int mdfld_overlay_damage(struct mdfld_dbi_dpu_info *dpu_info,
				mdfld_plane_t plane,
				struct psb_drm_dpu_rect *damaged_rect)
{
	return 0;
}

int mdfld_dbi_dpu_report_damage(struct drm_device *dev,
				mdfld_plane_t plane,
				struct psb_drm_dpu_rect *rect)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;
	int ret = 0;

	/* DPU not in use, no damage reporting needed */
	if (dpu_info == NULL)
		return 0;

	spin_lock(&dpu_info->dpu_update_lock);

	switch (plane) {
	case MDFLD_PLANEA:
	case MDFLD_PLANEC:
		mdfld_fb_damage(dpu_info, plane, rect);
		break;
	case MDFLD_CURSORA:
	case MDFLD_CURSORC:
		mdfld_cursor_damage(dpu_info, plane, rect);
		break;
	case MDFLD_OVERLAYA:
	case MDFLD_OVERLAYC:
		mdfld_overlay_damage(dpu_info, plane, rect);
		break;
	default:
		DRM_ERROR("Invalid plane type %d\n", plane);
		ret = -EINVAL;
	}
	spin_unlock(&dpu_info->dpu_update_lock);
	return ret;
}

int mdfld_dbi_dpu_report_fullscreen_damage(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv;
	struct mdfld_dbi_dpu_info *dpu_info;
	struct mdfld_dsi_config  *dsi_config;
	struct psb_drm_dpu_rect rect;
	int i;

	if (!dev) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	dev_priv = dev->dev_private;
	dpu_info = dev_priv->dbi_dpu_info;

	/* This is fine - we may be in non DPU mode */
	if (!dpu_info)
		return -EINVAL;

	for (i = 0; i < dpu_info->dbi_output_num; i++) {
		dsi_config = dev_priv->dsi_configs[i];
		if (dsi_config) {
			rect.x = rect.y = 0;
			rect.width = dsi_config->fixed_mode->hdisplay;
			rect.height = dsi_config->fixed_mode->vdisplay;
			mdfld_dbi_dpu_report_damage(dev,
				    i ? (MDFLD_PLANEC) : (MDFLD_PLANEA),
				    &rect);
		}
	}
	/* Exit DSR state */
	mdfld_dpu_exit_dsr(dev);
	return 0;
}

int mdfld_dsi_dbi_dsr_off(struct drm_device *dev,
					struct psb_drm_dpu_rect *rect)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;

	mdfld_dbi_dpu_report_damage(dev, MDFLD_PLANEA, rect);

	/* If dual display mode */
	if (dpu_info->dbi_output_num == 2)
		mdfld_dbi_dpu_report_damage(dev, MDFLD_PLANEC, rect);

	/* Force dsi to exit DSR mode */
	mdfld_dpu_exit_dsr(dev);
	return 0;
}

static void mdfld_dpu_cursor_plane_flush(struct mdfld_dbi_dpu_info *dpu_info,
						 mdfld_plane_t plane)
{
	struct drm_device *dev = dpu_info->dev;
	u32 curpos_reg = CURAPOS;
	u32 curbase_reg = CURABASE;
	u32 curcntr_reg = CURACNTR;
	struct mdfld_cursor_info *cursor = &dpu_info->cursors[0];

	if (plane == MDFLD_CURSORC) {
		curpos_reg = CURCPOS;
		curbase_reg = CURCBASE;
		curcntr_reg = CURCCNTR;
		cursor = &dpu_info->cursors[1];
	}

	REG_WRITE(curcntr_reg, REG_READ(curcntr_reg));
	REG_WRITE(curpos_reg,
		(((cursor->x & CURSOR_POS_MASK) << CURSOR_X_SHIFT) |
		((cursor->y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT)));
	REG_WRITE(curbase_reg, REG_READ(curbase_reg));
}

static void mdfld_dpu_fb_plane_flush(struct mdfld_dbi_dpu_info *dpu_info,
						 mdfld_plane_t plane)
{
	u32 pipesrc_reg = PIPEASRC;
	u32 dspsize_reg = DSPASIZE;
	u32 dspoff_reg = DSPALINOFF;
	u32 dspsurf_reg = DSPASURF;
	u32 dspstride_reg = DSPASTRIDE;
	u32 stride;
	struct psb_drm_dpu_rect *rect = &dpu_info->damage_pipea;
	struct drm_device *dev = dpu_info->dev;

	if (plane == MDFLD_PLANEC) {
		pipesrc_reg = PIPECSRC;
		dspsize_reg = DSPCSIZE;
		dspoff_reg = DSPCLINOFF;
		dspsurf_reg = DSPCSURF;
		dspstride_reg = DSPCSTRIDE;
		rect = &dpu_info->damage_pipec;
	}

	stride = REG_READ(dspstride_reg);
	/* FIXME: should I do the pipe src update here? */
	REG_WRITE(pipesrc_reg, ((rect->width - 1) << 16) | (rect->height - 1));
	/* Flush plane */
	REG_WRITE(dspsize_reg, ((rect->height - 1) << 16) | (rect->width - 1));
	REG_WRITE(dspoff_reg, ((rect->x * 4) + (rect->y * stride)));
	REG_WRITE(dspsurf_reg, REG_READ(dspsurf_reg));

	/*
	 * TODO: wait for flip finished and restore the pipesrc reg,
	 * or cursor will be show at a wrong position
	 */
}

static void mdfld_dpu_overlay_plane_flush(struct mdfld_dbi_dpu_info *dpu_info,
						  mdfld_plane_t plane)
{
}

/*
 * TODO: we are still in dbi normal mode now, we will try to use partial
 * mode later.
 */
static int mdfld_dbi_prepare_cb(struct mdfld_dsi_dbi_output *dbi_output,
				struct mdfld_dbi_dpu_info *dpu_info, int pipe)
{
	u8 *cb_addr = (u8 *)dbi_output->dbi_cb_addr;
	u32 *index;
	struct psb_drm_dpu_rect *rect = pipe ?
		(&dpu_info->damage_pipec) : (&dpu_info->damage_pipea);

	/* FIXME: lock command buffer, this may lead to a deadlock,
	   as we already hold the dpu_update_lock */
	if (!spin_trylock(&dbi_output->cb_lock)) {
		DRM_ERROR("lock command buffer failed, try again\n");
		return -EAGAIN;
	}

	index = &dbi_output->cb_write;

	if (*index) {
		DRM_ERROR("DBI command buffer unclean\n");
		return -EAGAIN;
	}

	/* Column address */
	*(cb_addr + ((*index)++)) = set_column_address;
	*(cb_addr + ((*index)++)) = rect->x >> 8;
	*(cb_addr + ((*index)++)) = rect->x;
	*(cb_addr + ((*index)++)) = (rect->x + rect->width - 1) >> 8;
	*(cb_addr + ((*index)++)) = (rect->x + rect->width - 1);

	*index = 8;

	/* Page address */
	*(cb_addr + ((*index)++)) = set_page_addr;
	*(cb_addr + ((*index)++)) = rect->y >> 8;
	*(cb_addr + ((*index)++)) = rect->y;
	*(cb_addr + ((*index)++)) = (rect->y + rect->height - 1) >> 8;
	*(cb_addr + ((*index)++)) = (rect->y + rect->height - 1);

	*index = 16;

	/*write memory*/
	*(cb_addr + ((*index)++)) = write_mem_start;

	return 0;
}

static int mdfld_dbi_flush_cb(struct mdfld_dsi_dbi_output *dbi_output, int pipe)
{
	u32 cmd_phy = dbi_output->dbi_cb_phy;
	u32 *index = &dbi_output->cb_write;
	int reg_offset = pipe ? MIPIC_REG_OFFSET : 0;
	struct drm_device *dev = dbi_output->dev;

	if (*index == 0 || !dbi_output)
		return 0;

	REG_WRITE((MIPIA_CMD_LEN_REG + reg_offset), 0x010505);
	REG_WRITE((MIPIA_CMD_ADD_REG + reg_offset), cmd_phy | 3);

	*index = 0;

	/* FIXME: unlock command buffer */
	spin_unlock(&dbi_output->cb_lock);
	return 0;
}

static int mdfld_dpu_update_pipe(struct mdfld_dsi_dbi_output *dbi_output,
				 struct mdfld_dbi_dpu_info *dpu_info, int pipe)
{
	struct drm_device *dev =  dbi_output->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	mdfld_plane_t cursor_plane = MDFLD_CURSORA;
	mdfld_plane_t fb_plane = MDFLD_PLANEA;
	mdfld_plane_t overlay_plane = MDFLD_OVERLAYA;
	int ret = 0;
	u32 plane_mask = MDFLD_PIPEA_PLANE_MASK;

	/* Damaged rects on this pipe */
	if (pipe) {
		cursor_plane = MDFLD_CURSORC;
		fb_plane = MDFLD_PLANEC;
		overlay_plane = MDFLD_OVERLAYC;
		plane_mask = MDFLD_PIPEC_PLANE_MASK;
	}

	/*update cursor which assigned to @pipe*/
	if (dpu_info->pending & (1 << cursor_plane))
		mdfld_dpu_cursor_plane_flush(dpu_info, cursor_plane);

	/*update fb which assigned to @pipe*/
	if (dpu_info->pending & (1 << fb_plane))
		mdfld_dpu_fb_plane_flush(dpu_info, fb_plane);

	/* TODO: update overlay */
	if (dpu_info->pending & (1 << overlay_plane))
		mdfld_dpu_overlay_plane_flush(dpu_info, overlay_plane);

	/* Flush damage area to panel fb */
	if (dpu_info->pending & plane_mask) {
		ret = mdfld_dbi_prepare_cb(dbi_output, dpu_info, pipe);
		/*
		 * TODO: remove b_dsr_enable later,
		 * added it so that text console could boot smoothly
		 */
		/* Clean pending flags on this pipe */
		if (!ret && dev_priv->dsr_enable) {
			dpu_info->pending &= ~plane_mask;
			/* Reset overlay pipe damage rect */
			mdfld_dpu_init_damage(dpu_info, pipe);
		}
	}
	return ret;
}

static int mdfld_dpu_update_fb(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	struct psb_intel_crtc *psb_crtc;
	struct mdfld_dsi_dbi_output **dbi_output;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;
	bool pipe_updated[2];
	unsigned long irq_flags;
	u32 dpll_reg = MRST_DPLL_A;
	u32 dspcntr_reg = DSPACNTR;
	u32 pipeconf_reg = PIPEACONF;
	u32 dsplinoff_reg = DSPALINOFF;
	u32 dspsurf_reg = DSPASURF;
	u32 mipi_state_reg = MIPIA_INTR_STAT_REG;
	u32 reg_offset = 0;
	int pipe;
	int i;
	int ret;

	dbi_output = dpu_info->dbi_outputs;
	pipe_updated[0] = pipe_updated[1] = false;

	if (!gma_power_begin(dev, true))
		return -EAGAIN;

	/* Try to prevent any new damage reports */
	if (!spin_trylock_irqsave(&dpu_info->dpu_update_lock, irq_flags))
		return -EAGAIN;

	for (i = 0; i < dpu_info->dbi_output_num; i++) {
		crtc = dbi_output[i]->base.base.crtc;
		psb_crtc = (crtc) ? to_psb_intel_crtc(crtc) : NULL;

		pipe = dbi_output[i]->channel_num ? 2 : 0;

		if (pipe == 2) {
			dspcntr_reg = DSPCCNTR;
			pipeconf_reg = PIPECCONF;
			dsplinoff_reg = DSPCLINOFF;
			dspsurf_reg = DSPCSURF;
			reg_offset = MIPIC_REG_OFFSET;
		}

		if (!(REG_READ((MIPIA_GEN_FIFO_STAT_REG + reg_offset))
							& (1 << 27)) ||
			!(REG_READ(dpll_reg) & DPLL_VCO_ENABLE) ||
			!(REG_READ(dspcntr_reg) & DISPLAY_PLANE_ENABLE) ||
			!(REG_READ(pipeconf_reg) & DISPLAY_PLANE_ENABLE)) {
			dev_err(dev->dev,
				"DBI FIFO is busy, DSI %d state %x\n",
				pipe,
				REG_READ(mipi_state_reg + reg_offset));
			continue;
		}

		/*
		 *	If DBI output is in a exclusive state then the pipe
		 *	change won't be updated
		 */
		if (dbi_output[i]->dbi_panel_on &&
		   !(dbi_output[i]->mode_flags & MODE_SETTING_ON_GOING) &&
		   !(psb_crtc &&
			psb_crtc->mode_flags & MODE_SETTING_ON_GOING) &&
		   !(dbi_output[i]->mode_flags & MODE_SETTING_IN_DSR)) {
			ret = mdfld_dpu_update_pipe(dbi_output[i],
				dpu_info, dbi_output[i]->channel_num ? 2 : 0);
			if (!ret)
				pipe_updated[i] = true;
		}
	}

	for (i = 0; i < dpu_info->dbi_output_num; i++)
		if (pipe_updated[i])
			mdfld_dbi_flush_cb(dbi_output[i],
				dbi_output[i]->channel_num ? 2 : 0);

	spin_unlock_irqrestore(&dpu_info->dpu_update_lock, irq_flags);
	gma_power_end(dev);
	return 0;
}

static int __mdfld_dbi_exit_dsr(struct mdfld_dsi_dbi_output *dbi_output,
								int pipe)
{
	struct drm_device *dev = dbi_output->dev;
	struct drm_crtc *crtc = dbi_output->base.base.crtc;
	struct psb_intel_crtc *psb_crtc = (crtc) ? to_psb_intel_crtc(crtc)
								: NULL;
	u32 reg_val;
	u32 dpll_reg = MRST_DPLL_A;
	u32 pipeconf_reg = PIPEACONF;
	u32 dspcntr_reg = DSPACNTR;
	u32 dspbase_reg = DSPABASE;
	u32 dspsurf_reg = DSPASURF;
	u32 reg_offset = 0;

	if (!dbi_output)
		return 0;

	/* If mode setting on-going, back off */
	if ((dbi_output->mode_flags & MODE_SETTING_ON_GOING) ||
		(psb_crtc && psb_crtc->mode_flags & MODE_SETTING_ON_GOING))
		return -EAGAIN;

	if (pipe == 2) {
		dpll_reg = MRST_DPLL_A;
		pipeconf_reg = PIPECCONF;
		dspcntr_reg = DSPCCNTR;
		dspbase_reg = MDFLD_DSPCBASE;
		dspsurf_reg = DSPCSURF;

		reg_offset = MIPIC_REG_OFFSET;
	}

	if (!gma_power_begin(dev, true))
		return -EAGAIN;

	/* Enable DPLL */
	reg_val = REG_READ(dpll_reg);
	if (!(reg_val & DPLL_VCO_ENABLE)) {

		if (reg_val & MDFLD_PWR_GATE_EN) {
			reg_val &= ~MDFLD_PWR_GATE_EN;
			REG_WRITE(dpll_reg, reg_val);
			REG_READ(dpll_reg);
			udelay(500);
		}

		reg_val |= DPLL_VCO_ENABLE;
		REG_WRITE(dpll_reg, reg_val);
		REG_READ(dpll_reg);
		udelay(500);

		/* FIXME: add timeout */
		while (!(REG_READ(pipeconf_reg) & PIPECONF_DSIPLL_LOCK))
			cpu_relax();
	}

	/* Enable pipe */
	reg_val = REG_READ(pipeconf_reg);
	if (!(reg_val & PIPEACONF_ENABLE)) {
		reg_val |= PIPEACONF_ENABLE;
		REG_WRITE(pipeconf_reg, reg_val);
		REG_READ(pipeconf_reg);
		udelay(500);
		mdfldWaitForPipeEnable(dev, pipe);
	}

	/* Enable plane */
	reg_val = REG_READ(dspcntr_reg);
	if (!(reg_val & DISPLAY_PLANE_ENABLE)) {
		reg_val |= DISPLAY_PLANE_ENABLE;
		REG_WRITE(dspcntr_reg, reg_val);
		REG_READ(dspcntr_reg);
		udelay(500);
	}

	gma_power_end(dev);

	/* Clean IN_DSR flag */
	dbi_output->mode_flags &= ~MODE_SETTING_IN_DSR;

	return 0;
}

int mdfld_dpu_exit_dsr(struct drm_device *dev)
{
	struct mdfld_dsi_dbi_output **dbi_output;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;
	int i;
	int pipe;

	dbi_output = dpu_info->dbi_outputs;

	for (i = 0; i < dpu_info->dbi_output_num; i++) {
		/* If this output is not in DSR mode, don't call exit dsr */
		if (dbi_output[i]->mode_flags & MODE_SETTING_IN_DSR)
			__mdfld_dbi_exit_dsr(dbi_output[i],
					dbi_output[i]->channel_num ? 2 : 0);
	}

	/* Enable TE interrupt */
	for (i = 0; i < dpu_info->dbi_output_num; i++) {
		/* If this output is not in DSR mode, don't call exit dsr */
		pipe = dbi_output[i]->channel_num ? 2 : 0;
		if (dbi_output[i]->dbi_panel_on && pipe) {
			mdfld_disable_te(dev, 0);
			mdfld_enable_te(dev, 2);
		} else if (dbi_output[i]->dbi_panel_on && !pipe) {
			mdfld_disable_te(dev, 2);
			mdfld_enable_te(dev, 0);
		}
	}
	return 0;
}

static int mdfld_dpu_enter_dsr(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;
	struct mdfld_dsi_dbi_output **dbi_output;
	int i;

	dbi_output = dpu_info->dbi_outputs;

	for (i = 0; i < dpu_info->dbi_output_num; i++) {
		/* If output is off or already in DSR state, don't re-enter */
		if (dbi_output[i]->dbi_panel_on &&
		   !(dbi_output[i]->mode_flags & MODE_SETTING_IN_DSR)) {
			mdfld_dsi_dbi_enter_dsr(dbi_output[i],
				dbi_output[i]->channel_num ? 2 : 0);
		}
	}

	return 0;
}

static void mdfld_dbi_dpu_timer_func(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;
	struct timer_list *dpu_timer = &dpu_info->dpu_timer;
	unsigned long flags;

	if (dpu_info->pending) {
		dpu_info->idle_count = 0;
		/* Update panel fb with damaged area */
		mdfld_dpu_update_fb(dev);
	} else {
		dpu_info->idle_count++;
	}

	if (dpu_info->idle_count >= MDFLD_MAX_IDLE_COUNT) {
		mdfld_dpu_enter_dsr(dev);
		/* Stop timer by return */
		return;
	}

	spin_lock_irqsave(&dpu_info->dpu_timer_lock, flags);
	if (!timer_pending(dpu_timer)) {
		dpu_timer->expires = jiffies + MDFLD_DSR_DELAY;
		add_timer(dpu_timer);
	}
	spin_unlock_irqrestore(&dpu_info->dpu_timer_lock, flags);
}

void mdfld_dpu_update_panel(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;

	if (dpu_info->pending) {
		dpu_info->idle_count = 0;

		/*update panel fb with damaged area*/
		mdfld_dpu_update_fb(dev);
	} else {
		dpu_info->idle_count++;
	}

	if (dpu_info->idle_count >= MDFLD_MAX_IDLE_COUNT) {
		/*enter dsr*/
		mdfld_dpu_enter_dsr(dev);
	}
}

static int mdfld_dbi_dpu_timer_init(struct drm_device *dev,
				struct mdfld_dbi_dpu_info *dpu_info)
{
	struct timer_list *dpu_timer = &dpu_info->dpu_timer;
	unsigned long flags;

	spin_lock_init(&dpu_info->dpu_timer_lock);
	spin_lock_irqsave(&dpu_info->dpu_timer_lock, flags);

	init_timer(dpu_timer);

	dpu_timer->data = (unsigned long)dev;
	dpu_timer->function = mdfld_dbi_dpu_timer_func;
	dpu_timer->expires = jiffies + MDFLD_DSR_DELAY;

	spin_unlock_irqrestore(&dpu_info->dpu_timer_lock, flags);

	return 0;
}

void mdfld_dbi_dpu_timer_start(struct mdfld_dbi_dpu_info *dpu_info)
{
	struct timer_list *dpu_timer = &dpu_info->dpu_timer;
	unsigned long flags;

	spin_lock_irqsave(&dpu_info->dpu_timer_lock, flags);
	if (!timer_pending(dpu_timer)) {
		dpu_timer->expires = jiffies + MDFLD_DSR_DELAY;
		add_timer(dpu_timer);
	}
	spin_unlock_irqrestore(&dpu_info->dpu_timer_lock, flags);
}

int mdfld_dbi_dpu_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;

	if (!dpu_info || IS_ERR(dpu_info)) {
		dpu_info = kzalloc(sizeof(struct mdfld_dbi_dpu_info),
								GFP_KERNEL);
		if (!dpu_info) {
			DRM_ERROR("No memory\n");
			return -ENOMEM;
		}
		dev_priv->dbi_dpu_info = dpu_info;
	}

	dpu_info->dev = dev;

	dpu_info->cursors[0].size = MDFLD_CURSOR_SIZE;
	dpu_info->cursors[1].size = MDFLD_CURSOR_SIZE;

	/*init dpu_update_lock*/
	spin_lock_init(&dpu_info->dpu_update_lock);

	/*init dpu refresh timer*/
	mdfld_dbi_dpu_timer_init(dev, dpu_info);

	/*init pipe damage area*/
	mdfld_dpu_init_damage(dpu_info, 0);
	mdfld_dpu_init_damage(dpu_info, 2);

	return 0;
}

void mdfld_dbi_dpu_exit(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dbi_dpu_info *dpu_info = dev_priv->dbi_dpu_info;

	if (!dpu_info)
		return;

	del_timer_sync(&dpu_info->dpu_timer);
	kfree(dpu_info);
	dev_priv->dbi_dpu_info = NULL;
}


