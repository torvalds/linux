/*
 * hdmi_panel.c
 *
 * HDMI library support functions for TI OMAP4 processors.
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 * Authors:	Mythri P k <mythripk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <video/omapdss.h>
#include <linux/slab.h>

#include "dss.h"

static struct {
	/* This protects the panel ops, mainly when accessing the HDMI IP. */
	struct mutex lock;
#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
	/* This protects the audio ops, specifically. */
	spinlock_t audio_lock;
#endif
} hdmi;


static int hdmi_panel_probe(struct omap_dss_device *dssdev)
{
	DSSDBG("ENTER hdmi_panel_probe\n");

	dssdev->panel.config = OMAP_DSS_LCD_TFT |
			OMAP_DSS_LCD_IVS | OMAP_DSS_LCD_IHS;

	dssdev->panel.timings = (struct omap_video_timings){640, 480, 25175, 96, 16, 48, 2 , 11, 31};

	DSSDBG("hdmi_panel_probe x_res= %d y_res = %d\n",
		dssdev->panel.timings.x_res,
		dssdev->panel.timings.y_res);
	return 0;
}

static void hdmi_panel_remove(struct omap_dss_device *dssdev)
{

}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
static int hdmi_panel_audio_enable(struct omap_dss_device *dssdev)
{
	unsigned long flags;
	int r;

	mutex_lock(&hdmi.lock);
	spin_lock_irqsave(&hdmi.audio_lock, flags);

	/* enable audio only if the display is active and supports audio */
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE ||
	    !hdmi_mode_has_audio()) {
		DSSERR("audio not supported or display is off\n");
		r = -EPERM;
		goto err;
	}

	r = hdmi_audio_enable();

	if (!r)
		dssdev->audio_state = OMAP_DSS_AUDIO_ENABLED;

err:
	spin_unlock_irqrestore(&hdmi.audio_lock, flags);
	mutex_unlock(&hdmi.lock);
	return r;
}

static void hdmi_panel_audio_disable(struct omap_dss_device *dssdev)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi.audio_lock, flags);

	hdmi_audio_disable();

	dssdev->audio_state = OMAP_DSS_AUDIO_DISABLED;

	spin_unlock_irqrestore(&hdmi.audio_lock, flags);
}

static int hdmi_panel_audio_start(struct omap_dss_device *dssdev)
{
	unsigned long flags;
	int r;

	spin_lock_irqsave(&hdmi.audio_lock, flags);
	/*
	 * No need to check the panel state. It was checked when trasitioning
	 * to AUDIO_ENABLED.
	 */
	if (dssdev->audio_state != OMAP_DSS_AUDIO_ENABLED) {
		DSSERR("audio start from invalid state\n");
		r = -EPERM;
		goto err;
	}

	r = hdmi_audio_start();

	if (!r)
		dssdev->audio_state = OMAP_DSS_AUDIO_PLAYING;

err:
	spin_unlock_irqrestore(&hdmi.audio_lock, flags);
	return r;
}

static void hdmi_panel_audio_stop(struct omap_dss_device *dssdev)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi.audio_lock, flags);

	hdmi_audio_stop();
	dssdev->audio_state = OMAP_DSS_AUDIO_ENABLED;

	spin_unlock_irqrestore(&hdmi.audio_lock, flags);
}

static bool hdmi_panel_audio_supported(struct omap_dss_device *dssdev)
{
	bool r = false;

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		goto err;

	if (!hdmi_mode_has_audio())
		goto err;

	r = true;
err:
	mutex_unlock(&hdmi.lock);
	return r;
}

static int hdmi_panel_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	unsigned long flags;
	int r;

	mutex_lock(&hdmi.lock);
	spin_lock_irqsave(&hdmi.audio_lock, flags);

	/* config audio only if the display is active and supports audio */
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE ||
	    !hdmi_mode_has_audio()) {
		DSSERR("audio not supported or display is off\n");
		r = -EPERM;
		goto err;
	}

	r = hdmi_audio_config(audio);

	if (!r)
		dssdev->audio_state = OMAP_DSS_AUDIO_CONFIGURED;

err:
	spin_unlock_irqrestore(&hdmi.audio_lock, flags);
	mutex_unlock(&hdmi.lock);
	return r;
}

#else
static int hdmi_panel_audio_enable(struct omap_dss_device *dssdev)
{
	return -EPERM;
}

static void hdmi_panel_audio_disable(struct omap_dss_device *dssdev)
{
}

static int hdmi_panel_audio_start(struct omap_dss_device *dssdev)
{
	return -EPERM;
}

static void hdmi_panel_audio_stop(struct omap_dss_device *dssdev)
{
}

static bool hdmi_panel_audio_supported(struct omap_dss_device *dssdev)
{
	return false;
}

static int hdmi_panel_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	return -EPERM;
}
#endif

static int hdmi_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;
	DSSDBG("ENTER hdmi_panel_enable\n");

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	r = omapdss_hdmi_display_enable(dssdev);
	if (r) {
		DSSERR("failed to power on\n");
		goto err;
	}

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

err:
	mutex_unlock(&hdmi.lock);

	return r;
}

static void hdmi_panel_disable(struct omap_dss_device *dssdev)
{
	mutex_lock(&hdmi.lock);

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		/*
		 * TODO: notify audio users that the display was disabled. For
		 * now, disable audio locally to not break our audio state
		 * machine.
		 */
		hdmi_panel_audio_disable(dssdev);
		omapdss_hdmi_display_disable(dssdev);
	}

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&hdmi.lock);
}

static int hdmi_panel_suspend(struct omap_dss_device *dssdev)
{
	int r = 0;

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		r = -EINVAL;
		goto err;
	}

	/*
	 * TODO: notify audio users that the display was suspended. For now,
	 * disable audio locally to not break our audio state machine.
	 */
	hdmi_panel_audio_disable(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	omapdss_hdmi_display_disable(dssdev);

err:
	mutex_unlock(&hdmi.lock);

	return r;
}

static int hdmi_panel_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_SUSPENDED) {
		r = -EINVAL;
		goto err;
	}

	r = omapdss_hdmi_display_enable(dssdev);
	if (r) {
		DSSERR("failed to power on\n");
		goto err;
	}
	/* TODO: notify audio users that the panel resumed. */

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

err:
	mutex_unlock(&hdmi.lock);

	return r;
}

static void hdmi_get_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	mutex_lock(&hdmi.lock);

	*timings = dssdev->panel.timings;

	mutex_unlock(&hdmi.lock);
}

static void hdmi_set_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	DSSDBG("hdmi_set_timings\n");

	mutex_lock(&hdmi.lock);

	/*
	 * TODO: notify audio users that there was a timings change. For
	 * now, disable audio locally to not break our audio state machine.
	 */
	hdmi_panel_audio_disable(dssdev);

	dssdev->panel.timings = *timings;
	omapdss_hdmi_display_set_timing(dssdev);

	mutex_unlock(&hdmi.lock);
}

static int hdmi_check_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	int r = 0;

	DSSDBG("hdmi_check_timings\n");

	mutex_lock(&hdmi.lock);

	r = omapdss_hdmi_display_check_timing(dssdev, timings);

	mutex_unlock(&hdmi.lock);
	return r;
}

static int hdmi_read_edid(struct omap_dss_device *dssdev, u8 *buf, int len)
{
	int r;

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		r = omapdss_hdmi_display_enable(dssdev);
		if (r)
			goto err;
	}

	r = omapdss_hdmi_read_edid(buf, len);

	if (dssdev->state == OMAP_DSS_DISPLAY_DISABLED ||
			dssdev->state == OMAP_DSS_DISPLAY_SUSPENDED)
		omapdss_hdmi_display_disable(dssdev);
err:
	mutex_unlock(&hdmi.lock);

	return r;
}

static bool hdmi_detect(struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&hdmi.lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		r = omapdss_hdmi_display_enable(dssdev);
		if (r)
			goto err;
	}

	r = omapdss_hdmi_detect();

	if (dssdev->state == OMAP_DSS_DISPLAY_DISABLED ||
			dssdev->state == OMAP_DSS_DISPLAY_SUSPENDED)
		omapdss_hdmi_display_disable(dssdev);
err:
	mutex_unlock(&hdmi.lock);

	return r;
}

static struct omap_dss_driver hdmi_driver = {
	.probe		= hdmi_panel_probe,
	.remove		= hdmi_panel_remove,
	.enable		= hdmi_panel_enable,
	.disable	= hdmi_panel_disable,
	.suspend	= hdmi_panel_suspend,
	.resume		= hdmi_panel_resume,
	.get_timings	= hdmi_get_timings,
	.set_timings	= hdmi_set_timings,
	.check_timings	= hdmi_check_timings,
	.read_edid	= hdmi_read_edid,
	.detect		= hdmi_detect,
	.audio_enable	= hdmi_panel_audio_enable,
	.audio_disable	= hdmi_panel_audio_disable,
	.audio_start	= hdmi_panel_audio_start,
	.audio_stop	= hdmi_panel_audio_stop,
	.audio_supported	= hdmi_panel_audio_supported,
	.audio_config	= hdmi_panel_audio_config,
	.driver			= {
		.name   = "hdmi_panel",
		.owner  = THIS_MODULE,
	},
};

int hdmi_panel_init(void)
{
	mutex_init(&hdmi.lock);

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
	spin_lock_init(&hdmi.audio_lock);
#endif

	omap_dss_register_driver(&hdmi_driver);

	return 0;
}

void hdmi_panel_exit(void)
{
	omap_dss_unregister_driver(&hdmi_driver);

}
