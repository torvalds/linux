/*
 * Copyright © 2010 Intel Corporation
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
 * jim liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 */

#include "mdfld_dsi_output.h"
#include "mdfld_dsi_dbi.h"
#include "mdfld_dsi_dpi.h"
#include "mdfld_output.h"
#include <asm/intel_scu_ipc.h>
#include "mdfld_dsi_pkg_sender.h"
#include <linux/pm_runtime.h>

#define MDFLD_DSI_BRIGHTNESS_MAX_LEVEL 100

static int CABC_control = 1;
static int LABC_control = 1;

module_param (CABC_control, int, 0644);
module_param (LABC_control, int, 0644);

/**
 * make these MCS command global 
 * we don't need 'movl' everytime we send them.
 * FIXME: these datas were provided by OEM, we should get them from GCT.
 **/
static u32 mdfld_dbi_mcs_hysteresis[] = {
	0x42000f57, 0x8c006400, 0xff00bf00, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0x38000aff, 0x82005000, 0xff00ab00, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0x000000ff,
};

static u32 mdfld_dbi_mcs_display_profile[] = {
	0x50281450, 0x0000c882, 0x00000000, 0x00000000,
	0x00000000,
};

static u32 mdfld_dbi_mcs_kbbc_profile[] = {
	0x00ffcc60, 0x00000000, 0x00000000, 0x00000000,
}; 
	
static u32 mdfld_dbi_mcs_gamma_profile[] = {
	0x81111158, 0x88888888, 0x88888888,
}; 

/*
 * write hysteresis values.
 */
static void mdfld_dsi_write_hysteresis (struct mdfld_dsi_config *dsi_config,
                                                                int pipe)
{
	struct mdfld_dsi_pkg_sender *sender = mdfld_dsi_get_pkg_sender(dsi_config);

	if(!sender) {
	        WARN_ON(1);
		return;
	}
	mdfld_dsi_send_mcs_long_hs(sender,
				   mdfld_dbi_mcs_hysteresis,
				   17,
				   MDFLD_DSI_SEND_PACKAGE);
}

/*
 * write display profile values.
 */
static void mdfld_dsi_write_display_profile(struct mdfld_dsi_config *dsi_config, int pipe)
{
	struct mdfld_dsi_pkg_sender *sender = mdfld_dsi_get_pkg_sender(dsi_config);

	if(!sender) {
	        WARN_ON(1);
		return;
        }
	mdfld_dsi_send_mcs_long_hs(sender,
				   mdfld_dbi_mcs_display_profile,
				   5,
				   MDFLD_DSI_SEND_PACKAGE);
}

/*
 * write KBBC profile values.
 */
static void mdfld_dsi_write_kbbc_profile (struct mdfld_dsi_config * dsi_config, int pipe)
{
	struct mdfld_dsi_pkg_sender *sender = mdfld_dsi_get_pkg_sender(dsi_config);

	if(!sender) {
	        WARN_ON(1);
		return;
        }
	mdfld_dsi_send_mcs_long_hs(sender,
				   mdfld_dbi_mcs_kbbc_profile,
				   4,
				   MDFLD_DSI_SEND_PACKAGE);
}

/*
 * write gamma setting.
 */
static void mdfld_dsi_write_gamma_setting (struct mdfld_dsi_config *dsi_config, int pipe)
{
	struct mdfld_dsi_pkg_sender *sender = mdfld_dsi_get_pkg_sender(dsi_config);

	if(!sender) {
	        WARN_ON(1);
		return;
	}
	mdfld_dsi_send_mcs_long_hs(sender,
				   mdfld_dbi_mcs_gamma_profile,
				   3,
				   MDFLD_DSI_SEND_PACKAGE);
}

/*
 * Check and see if the generic control or data buffer is empty and ready.
 */
void mdfld_dsi_gen_fifo_ready (struct drm_device *dev, u32 gen_fifo_stat_reg, u32 fifo_stat)
{
	u32 GEN_BF_time_out_count = 0;
	
	/* Check MIPI Adatper command registers */
	for (GEN_BF_time_out_count = 0; GEN_BF_time_out_count < GEN_FB_TIME_OUT; GEN_BF_time_out_count++)
	{
		if ((REG_READ(gen_fifo_stat_reg) & fifo_stat) == fifo_stat)
			break;
		udelay (100);
	}

	if (GEN_BF_time_out_count == GEN_FB_TIME_OUT)
		dev_err(dev->dev,
        "mdfld_dsi_gen_fifo_ready, Timeout. gen_fifo_stat_reg = 0x%x. \n",
                                                gen_fifo_stat_reg);
}

/*
 * Manage the DSI MIPI keyboard and display brightness.
 * FIXME: this is exported to OSPM code. should work out an specific 
 * display interface to OSPM. 
 */
void mdfld_dsi_brightness_init(struct mdfld_dsi_config *dsi_config, int pipe)
{
	struct mdfld_dsi_pkg_sender *sender = mdfld_dsi_get_pkg_sender(dsi_config);
	struct drm_device *dev = sender->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 gen_ctrl_val;
	
	if(!sender) {
	        WARN_ON(1);
	        return;
	}
	/* Set default display backlight value to 85% (0xd8)*/
	mdfld_dsi_send_mcs_short_hs(sender,
				    write_display_brightness,
				    0xd8,
				    1,
				    MDFLD_DSI_SEND_PACKAGE);

	/* Set minimum brightness setting of CABC function to 20% (0x33)*/
	mdfld_dsi_send_mcs_short_hs(sender,
				    write_cabc_min_bright,
				    0x33,
				    1,
				    MDFLD_DSI_SEND_PACKAGE);

	mdfld_dsi_write_hysteresis(dsi_config, pipe);
	mdfld_dsi_write_display_profile (dsi_config, pipe);
	mdfld_dsi_write_kbbc_profile (dsi_config, pipe);
	mdfld_dsi_write_gamma_setting (dsi_config, pipe);

	/* Enable backlight or/and LABC */
	gen_ctrl_val = BRIGHT_CNTL_BLOCK_ON | DISPLAY_DIMMING_ON| BACKLIGHT_ON;
	if (LABC_control == 1 || CABC_control == 1)
		gen_ctrl_val |= DISPLAY_DIMMING_ON| DISPLAY_BRIGHTNESS_AUTO | GAMMA_AUTO;

	if (LABC_control == 1)
		gen_ctrl_val |= AMBIENT_LIGHT_SENSE_ON;

	dev_priv->mipi_ctrl_display = gen_ctrl_val;

	mdfld_dsi_send_mcs_short_hs(sender,
				    write_ctrl_display,
				    (u8)gen_ctrl_val,
				    1,
				    MDFLD_DSI_SEND_PACKAGE);

	if (CABC_control == 0)
		return;
	mdfld_dsi_send_mcs_short_hs(sender,
				    write_ctrl_cabc,
				    UI_IMAGE,
				    1,
				    MDFLD_DSI_SEND_PACKAGE);
}

/*
 * Manage the mipi display brightness.
 * TODO: refine this interface later
 */
void mdfld_dsi_brightness_control(struct drm_device *dev, int pipe, int level)
{
	struct mdfld_dsi_pkg_sender *sender;
	struct drm_psb_private *dev_priv;
	struct mdfld_dsi_config *dsi_config;
	u32 gen_ctrl_val;
	int p_type;	
	
	if (!dev || (pipe != 0 && pipe != 2)) {
		dev_err(dev->dev, "Invalid parameter\n");
		return;
	}

	p_type = mdfld_get_panel_type(dev, 0);

	dev_priv = dev->dev_private;

	if(pipe)
		dsi_config = dev_priv->dsi_configs[1];
	else
		dsi_config = dev_priv->dsi_configs[0];

	sender = mdfld_dsi_get_pkg_sender(dsi_config);

	if(!sender) {
	        WARN_ON(1);
		return;
	}

	gen_ctrl_val = ((level * 0xff) / MDFLD_DSI_BRIGHTNESS_MAX_LEVEL) & 0xff;

	dev_dbg(dev->dev,
                "pipe = %d, gen_ctrl_val = %d.  \n", pipe, gen_ctrl_val);
	
	if(p_type == TMD_VID || p_type == TMD_CMD){
		/* Set display backlight value */
		mdfld_dsi_send_mcs_short_hs(sender, 
					tmd_write_display_brightness, 
					(u8)gen_ctrl_val, 
	                                 1, 
	                        	MDFLD_DSI_SEND_PACKAGE);		
	} else {			
		/* Set display backlight value */
		mdfld_dsi_send_mcs_short_hs(sender,
				    write_display_brightness,
				    (u8)gen_ctrl_val,
                                    1,
                                    MDFLD_DSI_SEND_PACKAGE);


		/* Enable backlight control */
		if (level == 0)
			gen_ctrl_val = 0;
		else 
			gen_ctrl_val = dev_priv->mipi_ctrl_display;

		mdfld_dsi_send_mcs_short_hs(sender,
                                    write_ctrl_display,
                                   (u8)gen_ctrl_val,
                                   1,
                                   MDFLD_DSI_SEND_PACKAGE);
	}
}

/*
 * shut down DSI controller
 */ 
void mdfld_dsi_controller_shutdown(struct mdfld_dsi_config * dsi_config, int pipe)
{
	struct drm_device * dev;
	u32 reg_offset = pipe ? MIPIC_REG_OFFSET : 0;
	int retry = 100;
	
	if (!dsi_config) {
	        WARN_ON(1);
		return;
	}
	
	dev = dsi_config->dev;
	
	if (!gma_power_begin(dev, true)) {
		dev_err(dev->dev, "hw begin failed\n");
		return;
	}
		
	if(!(REG_READ(MIPIA_DEVICE_READY_REG + reg_offset) &  DSI_DEVICE_READY)) 
		goto shutdown_out;
	
	/* Send shut down package, clean packet send bit first */
	if(REG_READ(MIPIA_INTR_STAT_REG + reg_offset) & DSI_INTR_STATE_SPL_PKG_SENT) {
		REG_WRITE((MIPIA_INTR_STAT_REG + reg_offset), 
				(REG_READ(MIPIA_INTR_STAT_REG + reg_offset) | DSI_INTR_STATE_SPL_PKG_SENT));
	}
	
	/*send shut down package in HS*/
	REG_WRITE((MIPIA_DPI_CONTROL_REG + reg_offset), DSI_DPI_CTRL_HS_SHUTDOWN);
	
	
	/*
	 * make sure shut down is sent.
	 * FIXME: add max retry counter
	 */
	while(!(REG_READ(MIPIA_INTR_STAT_REG + reg_offset) & DSI_INTR_STATE_SPL_PKG_SENT)) {
		retry--;
		
		if(!retry) {
			dev_err(dev->dev, "timeout\n");
			break;
		}
	}
	
	/*sleep 1 ms to ensure shutdown finished*/
	msleep(100);
	
	/*un-ready device*/
	REG_WRITE((MIPIA_DEVICE_READY_REG + reg_offset),
			   (REG_READ(MIPIA_DEVICE_READY_REG + reg_offset) & ~DSI_DEVICE_READY));

shutdown_out:			   
	gma_power_end(dev);
}

void mdfld_dsi_controller_startup(struct mdfld_dsi_config * dsi_config, int pipe)
{
	struct drm_device * dev;
	u32 reg_offset = pipe ? MIPIC_REG_OFFSET : 0;
	int retry = 100;
	
	
	if (!dsi_config) {
		WARN_ON(1);
		return;
	}
	
	dev = dsi_config->dev;
	dev_dbg(dev->dev, "starting up DSI controller on pipe %d...\n", pipe);
	
	if (!gma_power_begin(dev, true)) {
		dev_err(dev->dev, "hw begin failed\n");
		return;
	}
	
	if((REG_READ(MIPIA_DEVICE_READY_REG + reg_offset) & DSI_DEVICE_READY)) 
		goto startup_out;
	
	/*if config DPI, turn on DPI interface*/
	if(dsi_config->type == MDFLD_DSI_ENCODER_DPI) {
		if(REG_READ(MIPIA_INTR_STAT_REG + reg_offset) & DSI_INTR_STATE_SPL_PKG_SENT) {
			REG_WRITE((MIPIA_INTR_STAT_REG + reg_offset), DSI_INTR_STATE_SPL_PKG_SENT);
		}
		
		REG_WRITE((MIPIA_DPI_CONTROL_REG + reg_offset), DSI_DPI_CTRL_HS_TURN_ON);
		
		/*
		 * make sure shut down is sent.
		 * FIXME: add max retry counter
		 */
		while(!(REG_READ(MIPIA_INTR_STAT_REG + reg_offset) & DSI_INTR_STATE_SPL_PKG_SENT)) {
			retry--;
			if(!retry) {
				dev_err(dev->dev, "timeout\n");
				break;
			}
		}
		
		msleep(100);
	}
	
	/*set device ready*/
	REG_WRITE((MIPIA_DEVICE_READY_REG + reg_offset),
			   (REG_READ(MIPIA_DEVICE_READY_REG + reg_offset) | DSI_DEVICE_READY));

startup_out:	
	gma_power_end(dev);
}


static int mdfld_dsi_get_panel_status(struct mdfld_dsi_config *dsi_config,
					u8 dcs,
					u32 *data,
					u8 transmission)
{
	struct mdfld_dsi_pkg_sender *sender
		= mdfld_dsi_get_pkg_sender(dsi_config);

	if (!sender || !data) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	if (transmission == MDFLD_DSI_HS_TRANSMISSION)
		return mdfld_dsi_read_mcs_hs(sender, dcs, data, 1);
	else if (transmission == MDFLD_DSI_LP_TRANSMISSION)
		return mdfld_dsi_read_mcs_lp(sender, dcs, data, 1);
	else
		return -EINVAL;
}

int mdfld_dsi_get_power_mode(struct mdfld_dsi_config *dsi_config,
				u32 *mode,
				u8 transmission)
{
	if (!dsi_config || !mode) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	return mdfld_dsi_get_panel_status(dsi_config, 0x0a, mode, transmission);
}

int mdfld_dsi_get_diagnostic_result(struct mdfld_dsi_config *dsi_config,
					u32 *result,
					u8 transmission)
{
	if (!dsi_config || !result) {
		DRM_ERROR("Invalid parameter\n");
		return -EINVAL;
	}

	return mdfld_dsi_get_panel_status(dsi_config, 0x0f, result,
					  transmission);
}

/*
 * NOTE: this function was used by OSPM.
 * TODO: will be removed later, should work out display interfaces for OSPM
 */
void mdfld_dsi_controller_init(struct mdfld_dsi_config * dsi_config, int pipe)
{
	if(!dsi_config || ((pipe != 0) && (pipe != 2))) {
	        WARN_ON(1);
		return;
	}

	if(dsi_config->type)
		mdfld_dsi_dpi_controller_init(dsi_config, pipe);
	else
		mdfld_dsi_controller_dbi_init(dsi_config, pipe);
}

static void mdfld_dsi_connector_save(struct drm_connector * connector)
{
}

static void mdfld_dsi_connector_restore(struct drm_connector * connector)
{
}

static enum drm_connector_status mdfld_dsi_connector_detect(struct drm_connector * connector, bool force)
{
	struct psb_intel_output *psb_output
					= to_psb_intel_output(connector);
	struct mdfld_dsi_connector *dsi_connector
	                                = MDFLD_DSI_CONNECTOR(psb_output);
	return dsi_connector->status;
}

static int mdfld_dsi_connector_set_property(struct drm_connector *connector,
					struct drm_property *property,
					uint64_t value)
{
	struct drm_encoder *encoder = connector->encoder;

	if (!strcmp(property->name, "scaling mode") && encoder) {
		struct psb_intel_crtc * psb_crtc = to_psb_intel_crtc(encoder->crtc);
		bool bTransitionFromToCentered;
		uint64_t curValue;

		if (!psb_crtc)
			goto set_prop_error;

		switch (value) {
		case DRM_MODE_SCALE_FULLSCREEN:
			break;
		case DRM_MODE_SCALE_NO_SCALE:
			break;
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			goto set_prop_error;
		}

		if (drm_connector_property_get_value(connector, property, &curValue))
			goto set_prop_error;

		if (curValue == value)
			goto set_prop_done;

		if (drm_connector_property_set_value(connector, property, value))
			goto set_prop_error;

		bTransitionFromToCentered = (curValue == DRM_MODE_SCALE_NO_SCALE) ||
			(value == DRM_MODE_SCALE_NO_SCALE);

		if (psb_crtc->saved_mode.hdisplay != 0 &&
		    psb_crtc->saved_mode.vdisplay != 0) {
			if (bTransitionFromToCentered) {
				if (!drm_crtc_helper_set_mode(encoder->crtc, &psb_crtc->saved_mode,
					    encoder->crtc->x, encoder->crtc->y, encoder->crtc->fb))
					goto set_prop_error;
			} else {
				struct drm_encoder_helper_funcs *pEncHFuncs  = encoder->helper_private;
				pEncHFuncs->mode_set(encoder, &psb_crtc->saved_mode,
						     &psb_crtc->saved_adjusted_mode);
			}
		}
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	} else if (!strcmp(property->name, "backlight") && encoder) {
		struct drm_psb_private *dev_priv = encoder->dev->dev_private;
		struct backlight_device *psb_bd = dev_priv->backlight_device;
		dev_dbg(encoder->dev->dev, "backlight level = %d\n", (int)value);
		if (drm_connector_property_set_value(connector, property, value))
			goto set_prop_error;
		else {
			dev_dbg(encoder->dev->dev,
			                "set brightness to %d", (int)value);
			if (psb_bd) {
				psb_bd->props.brightness = value;
				backlight_update_status(psb_bd);
			}
		}
#endif
	}
set_prop_done:
    return 0;
set_prop_error:
    return -1;
}

static void mdfld_dsi_connector_destroy(struct drm_connector *connector)
{
	struct psb_intel_output * psb_output = to_psb_intel_output(connector);
	struct mdfld_dsi_connector * dsi_connector = MDFLD_DSI_CONNECTOR(psb_output);
	struct mdfld_dsi_pkg_sender * sender;
	
	if(!dsi_connector)
	        return;
	
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	
	sender = dsi_connector->pkg_sender;

	mdfld_dsi_pkg_sender_destroy(sender);

	kfree(dsi_connector);
}

static int mdfld_dsi_connector_get_modes(struct drm_connector * connector)
{
	struct psb_intel_output * psb_output = to_psb_intel_output(connector);
	struct mdfld_dsi_connector * dsi_connector = MDFLD_DSI_CONNECTOR(psb_output);
	struct mdfld_dsi_config * dsi_config = mdfld_dsi_get_config(dsi_connector);
	struct drm_display_mode * fixed_mode = dsi_config->fixed_mode;
	struct drm_display_mode * dup_mode = NULL;
	struct drm_device * dev = connector->dev;
	
	connector->display_info.min_vfreq = 0;
	connector->display_info.max_vfreq = 200;
	connector->display_info.min_hfreq = 0;
	connector->display_info.max_hfreq = 200;

	if(fixed_mode) {
		dev_dbg(dev->dev, "fixed_mode %dx%d\n",
		        fixed_mode->hdisplay, fixed_mode->vdisplay);
		
		dup_mode = drm_mode_duplicate(dev, fixed_mode);
		drm_mode_probed_add(connector, dup_mode);
		return 1;
	}
	dev_err(dev->dev, "Didn't get any modes!\n");
	return 0;
}

static int mdfld_dsi_connector_mode_valid(struct drm_connector * connector, struct drm_display_mode * mode)
{
	struct psb_intel_output * psb_output = to_psb_intel_output(connector);
	struct mdfld_dsi_connector * dsi_connector = MDFLD_DSI_CONNECTOR(psb_output);
	struct mdfld_dsi_config * dsi_config = mdfld_dsi_get_config(dsi_connector);
	struct drm_display_mode * fixed_mode = dsi_config->fixed_mode;

	dev_dbg(connector->dev->dev, "mode %p, fixed mode %p\n",
	                                                mode, fixed_mode);

	if(mode->flags & DRM_MODE_FLAG_DBLSCAN) 
		return MODE_NO_DBLESCAN;

	if(mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	/**
	 * FIXME: current DC has no fitting unit, reject any mode setting request
	 * will figure out a way to do up-scaling(pannel fitting) later.  
	 **/
	if(fixed_mode) {
		if(mode->hdisplay != fixed_mode->hdisplay)
			return MODE_PANEL;

		if(mode->vdisplay != fixed_mode->vdisplay)
			return MODE_PANEL;
	}
	dev_dbg(connector->dev->dev, "mode ok\n");

	return MODE_OK;
}

static void mdfld_dsi_connector_dpms(struct drm_connector *connector, int mode)
{
#ifdef CONFIG_PM_RUNTIME
	struct drm_device * dev = connector->dev;
	struct drm_psb_private * dev_priv = dev->dev_private;
	bool panel_on, panel_on2;
#endif
	/* First, execute DPMS */
	drm_helper_connector_dpms(connector, mode);

#ifdef CONFIG_PM_RUNTIME
	if(mdfld_panel_dpi(dev)) {
		/* DPI panel */
		panel_on = dev_priv->dpi_panel_on;
		panel_on2 = dev_priv->dpi_panel_on2;
	} else {
		/* DBI panel */
		panel_on = dev_priv->dbi_panel_on;
		panel_on2 = dev_priv->dbi_panel_on2;
	}

	/* Then check all display panels + monitors status */
	/* Make sure that the Display (B) sub-system status isn't i3 when
	 * R/W the DC register, otherwise "Fabric error" issue would occur
	 * during S0i3 state. */
	if(!panel_on && !panel_on2 && !(REG_READ(HDMIB_CONTROL)
	                                        & HDMIB_PORT_EN)) {
		/* Request rpm idle */
		if(dev_priv->rpm_enabled)
			pm_request_idle(&dev->pdev->dev);
	}
	/*
	 * if rpm wasn't enabled yet, try to allow it
	 * FIXME: won't enable rpm for DPI since DPI
	 * CRTC setting is a little messy now.
	 * Enable it later!
	 */
#if 0
	if(!dev_priv->rpm_enabled && !mdfld_panel_dpi(dev))
		ospm_runtime_pm_allow(dev);
#endif
#endif
}

static struct drm_encoder *mdfld_dsi_connector_best_encoder(
                                        struct drm_connector *connector) 
{
	struct psb_intel_output * psb_output = to_psb_intel_output(connector);
	struct mdfld_dsi_connector * dsi_connector = MDFLD_DSI_CONNECTOR(psb_output);
	struct mdfld_dsi_config * dsi_config = mdfld_dsi_get_config(dsi_connector);
	struct mdfld_dsi_encoder * encoder = NULL;
	
	if(dsi_config->type == MDFLD_DSI_ENCODER_DBI) 
		encoder = dsi_config->encoders[MDFLD_DSI_ENCODER_DBI];
	else if (dsi_config->type == MDFLD_DSI_ENCODER_DPI) 
		encoder = dsi_config->encoders[MDFLD_DSI_ENCODER_DPI];
	
	dev_dbg(connector->dev->dev, "get encoder %p\n", encoder);
	
	if(!encoder) {
		dev_err(connector->dev->dev,
                        "Invalid encoder for type %d\n", dsi_config->type);
		return NULL;
	}
	dsi_config->encoder = encoder;	
	return &encoder->base;	
}

/* DSI connector funcs */
static const struct drm_connector_funcs mdfld_dsi_connector_funcs = {
	.dpms = /*drm_helper_connector_dpms*/mdfld_dsi_connector_dpms,
	.save = mdfld_dsi_connector_save,
	.restore = mdfld_dsi_connector_restore,
	.detect = mdfld_dsi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = mdfld_dsi_connector_set_property,
	.destroy = mdfld_dsi_connector_destroy,
};

/* DSI connector helper funcs */
static const struct drm_connector_helper_funcs mdfld_dsi_connector_helper_funcs = {
	.get_modes = mdfld_dsi_connector_get_modes,
	.mode_valid = mdfld_dsi_connector_mode_valid,
	.best_encoder = mdfld_dsi_connector_best_encoder,
};

static int mdfld_dsi_get_default_config(struct drm_device * dev, 
										struct mdfld_dsi_config * config, int pipe)
{
	if(!dev || !config) {
	        WARN_ON(1);
		return -EINVAL;
	}
	
	config->bpp = 24;
	config->type = mdfld_panel_dpi(dev);
	config->lane_count = 2;
	config->channel_num = 0;
	/*NOTE: video mode is ignored when type is MDFLD_DSI_ENCODER_DBI*/
	if (mdfld_get_panel_type(dev, pipe) == TMD_VID) {
		config->video_mode = MDFLD_DSI_VIDEO_NON_BURST_MODE_SYNC_PULSE;
	} else {
		config->video_mode = MDFLD_DSI_VIDEO_BURST_MODE;
	}
	
	return 0;
}

/*
 * Returns the panel fixed mode from configuration. 
 */
struct drm_display_mode *
mdfld_dsi_get_configuration_mode(struct mdfld_dsi_config * dsi_config, int pipe)
{
	struct drm_device *dev = dsi_config->dev;
	struct drm_display_mode *mode;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mrst_timing_info *ti = &dev_priv->gct_data.DTD;
	bool use_gct = false;

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode) {
	        dev_err(dev->dev, "Out of memory for mode\n");
		return NULL;
        }
	if (use_gct) {
		dev_dbg(dev->dev, "gct find MIPI panel.\n");

		mode->hdisplay = (ti->hactive_hi << 8) | ti->hactive_lo;
		mode->vdisplay = (ti->vactive_hi << 8) | ti->vactive_lo;
		mode->hsync_start = mode->hdisplay + \
				((ti->hsync_offset_hi << 8) | \
				ti->hsync_offset_lo);
		mode->hsync_end = mode->hsync_start + \
				((ti->hsync_pulse_width_hi << 8) | \
				ti->hsync_pulse_width_lo);
		mode->htotal = mode->hdisplay + ((ti->hblank_hi << 8) | \
								ti->hblank_lo);
		mode->vsync_start = \
			mode->vdisplay + ((ti->vsync_offset_hi << 8) | \
						ti->vsync_offset_lo);
		mode->vsync_end = \
			mode->vsync_start + ((ti->vsync_pulse_width_hi << 8) | \
						ti->vsync_pulse_width_lo);
		mode->vtotal = mode->vdisplay + \
				((ti->vblank_hi << 8) | ti->vblank_lo);
		mode->clock = ti->pixel_clock * 10;
	} else {
		if(dsi_config->type == MDFLD_DSI_ENCODER_DPI) { 
			if (mdfld_get_panel_type(dev, pipe) == TMD_VID) {
				mode->hdisplay = 480;
				mode->vdisplay = 854;
				mode->hsync_start = 487;
				mode->hsync_end = 490;
				mode->htotal = 499;
				mode->vsync_start = 861;
				mode->vsync_end = 865;
				mode->vtotal = 873;
				mode->clock = 33264;
			} else {
				mode->hdisplay = 864;
				mode->vdisplay = 480;
				mode->hsync_start = 873;
				mode->hsync_end = 876;
				mode->htotal = 887;
				mode->vsync_start = 487;
				mode->vsync_end = 490;
				mode->vtotal = 499;
				mode->clock = 33264;
			}
		} else if(dsi_config->type == MDFLD_DSI_ENCODER_DBI) {
			mode->hdisplay = 864;
			mode->vdisplay = 480;
			mode->hsync_start = 872;
			mode->hsync_end = 876;
			mode->htotal = 884;
			mode->vsync_start = 482;
			mode->vsync_end = 494;
			mode->vtotal = 486;
			mode->clock = 25777;
			
		}
	}

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);
	
	mode->type |= DRM_MODE_TYPE_PREFERRED;

	return mode;
}

int mdfld_dsi_panel_reset(int pipe)
{
	unsigned gpio;
	int ret = 0;

	switch (pipe) {
	case 0:
		gpio = 128;
		break;
	case 2:
		gpio = 34;
		break;
	default:
		DRM_ERROR("Invalid output\n");
		return -EINVAL;
	}

	ret = gpio_request(gpio, "gfx");
	if (ret) {
		DRM_ERROR("gpio_rqueset failed\n");
		return ret;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret) {
		DRM_ERROR("gpio_direction_output failed\n");
		goto gpio_error;
	}

	gpio_get_value(128);

gpio_error:
	if (gpio_is_valid(gpio))
		gpio_free(gpio);

	return ret;
}

/*
 * MIPI output init
 * @dev drm device
 * @pipe pipe number. 0 or 2
 * @config 
 * 
 * Do the initialization of a MIPI output, including create DRM mode objects
 * initialization of DSI output on @pipe 
 */
void mdfld_dsi_output_init(struct drm_device *dev,
			   int pipe, 
			   struct mdfld_dsi_config *config,
			   struct panel_funcs* p_cmd_funcs,
			   struct panel_funcs* p_vid_funcs)
{
	struct mdfld_dsi_config * dsi_config;
	struct mdfld_dsi_connector * dsi_connector;
	struct psb_intel_output * psb_output;
	struct drm_connector * connector;
	struct mdfld_dsi_encoder * encoder;
	struct drm_psb_private * dev_priv = dev->dev_private;
	struct panel_info dsi_panel_info;
	u32 width_mm, height_mm;

	dev_dbg(dev->dev, "init DSI output on pipe %d\n", pipe);
	
	if(!dev || ((pipe != 0) && (pipe != 2))) {
	        WARN_ON(1);
		return;
	}
	
	/*create a new connetor*/
	dsi_connector = kzalloc(sizeof(struct mdfld_dsi_connector), GFP_KERNEL);
	if(!dsi_connector) {
		DRM_ERROR("No memory");
		return;
	}
	
	dsi_connector->pipe =  pipe;
	
	/*set DSI config*/
	if(config) { 
		dsi_config = config;
	} else {
		dsi_config = kzalloc(sizeof(struct mdfld_dsi_config), GFP_KERNEL);
		if(!dsi_config) {
			dev_err(dev->dev,
			        "cannot allocate memory for DSI config\n");
			goto dsi_init_err0;
		}
		
		mdfld_dsi_get_default_config(dev, dsi_config, pipe);
	}
	
	dsi_connector->private = dsi_config;
	
	dsi_config->changed = 1;
	dsi_config->dev = dev;
	
	/* Init fixed mode basing on DSI config type */
	if(dsi_config->type == MDFLD_DSI_ENCODER_DBI) {
		dsi_config->fixed_mode = p_cmd_funcs->get_config_mode(dev);
		if(p_cmd_funcs->get_panel_info(dev, pipe, &dsi_panel_info))
			goto dsi_init_err0;
	} else if(dsi_config->type == MDFLD_DSI_ENCODER_DPI) {
		dsi_config->fixed_mode = p_vid_funcs->get_config_mode(dev);
		if(p_vid_funcs->get_panel_info(dev, pipe, &dsi_panel_info))
			goto dsi_init_err0;
	}

	width_mm = dsi_panel_info.width_mm;
	height_mm = dsi_panel_info.height_mm;

	dsi_config->mode = dsi_config->fixed_mode;
	dsi_config->connector = dsi_connector;
	
	if(!dsi_config->fixed_mode) {
		dev_err(dev->dev, "No pannel fixed mode was found\n");
		goto dsi_init_err0;
	}
	
	if(pipe && dev_priv->dsi_configs[0]) {
		dsi_config->dvr_ic_inited = 0;
		dev_priv->dsi_configs[1] = dsi_config;
	} else if(pipe == 0) {
		dsi_config->dvr_ic_inited = 1;
		dev_priv->dsi_configs[0] = dsi_config;
	} else {
		dev_err(dev->dev, "Trying to init MIPI1 before MIPI0\n");
		goto dsi_init_err0;
	}

	/*init drm connector object*/
	psb_output = &dsi_connector->base;
	
	psb_output->type = (pipe == 0) ? INTEL_OUTPUT_MIPI : INTEL_OUTPUT_MIPI2;

	connector = &psb_output->base;
	drm_connector_init(dev, connector, &mdfld_dsi_connector_funcs, DRM_MODE_CONNECTOR_MIPI);
	drm_connector_helper_add(connector, &mdfld_dsi_connector_helper_funcs);
	
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->display_info.width_mm = width_mm;
	connector->display_info.height_mm = height_mm;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;
	
	/* Attach properties */
	drm_connector_attach_property(connector, dev->mode_config.scaling_mode_property, DRM_MODE_SCALE_FULLSCREEN);
	drm_connector_attach_property(connector, dev_priv->backlight_property, MDFLD_DSI_BRIGHTNESS_MAX_LEVEL);

	/* Init DSI package sender on this output */
	if (mdfld_dsi_pkg_sender_init(dsi_connector, pipe)) {
		DRM_ERROR("Package Sender initialization failed on pipe %d\n", pipe);
		goto dsi_init_err0;
	}

	/* Init DBI & DPI encoders */
	if (p_cmd_funcs) {
		encoder = mdfld_dsi_dbi_init(dev, dsi_connector, p_cmd_funcs);
		if(!encoder) {
			dev_err(dev->dev, "Create DBI encoder failed\n");
			goto dsi_init_err1;
		}
		encoder->private = dsi_config;
		dsi_config->encoders[MDFLD_DSI_ENCODER_DBI] = encoder;
	}
	
	if(p_vid_funcs) {
		encoder = mdfld_dsi_dpi_init(dev, dsi_connector, p_vid_funcs);
		if(!encoder) {
			dev_err(dev->dev, "Create DPI encoder failed\n");
			goto dsi_init_err1;
		}
		encoder->private = dsi_config;
		dsi_config->encoders[MDFLD_DSI_ENCODER_DPI] = encoder;
	}
	
	drm_sysfs_connector_add(connector);
	return;
	
	/*TODO: add code to destroy outputs on error*/
dsi_init_err1:
	/*destroy sender*/
	mdfld_dsi_pkg_sender_destroy(dsi_connector->pkg_sender);

	drm_connector_cleanup(connector);
	kfree(dsi_config->fixed_mode);
	kfree(dsi_config);
dsi_init_err0:
	kfree(dsi_connector);
}
