/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_panel.h>

/**
 * struct panel_delay - Describes delays for a simple panel.
 */
struct panel_delay {
	/**
	 * @hpd_reliable: Time for HPD to be reliable
	 *
	 * The time (in milliseconds) that it takes after powering the panel
	 * before the HPD signal is reliable. Ideally this is 0 but some panels,
	 * board designs, or bad pulldown configs can cause a glitch here.
	 *
	 * NOTE: on some old panel data this number appears to be much too big.
	 * Presumably some old panels simply didn't have HPD hooked up and put
	 * the hpd_absent here because this field predates the
	 * hpd_absent. While that works, it's non-ideal.
	 */
	unsigned int hpd_reliable;

	/**
	 * @hpd_absent: Time to wait if HPD isn't hooked up.
	 *
	 * Add this to the prepare delay if we know Hot Plug Detect isn't used.
	 *
	 * This is T3-max on eDP timing diagrams or the delay from power on
	 * until HPD is guaranteed to be asserted.
	 */
	unsigned int hpd_absent;

	/**
	 * @powered_on_to_enable: Time between panel powered on and enable.
	 *
	 * The minimum time, in milliseconds, that needs to have passed
	 * between when panel powered on and enable may begin.
	 *
	 * This is (T3+T4+T5+T6+T8)-min on eDP timing diagrams or after the
	 * power supply enabled until we can turn the backlight on and see
	 * valid data.
	 *
	 * This doesn't normally need to be set if timings are already met by
	 * prepare_to_enable or enable.
	 */
	unsigned int powered_on_to_enable;

	/**
	 * @prepare_to_enable: Time between prepare and enable.
	 *
	 * The minimum time, in milliseconds, that needs to have passed
	 * between when prepare finished and enable may begin. If at
	 * enable time less time has passed since prepare finished,
	 * the driver waits for the remaining time.
	 *
	 * If a fixed enable delay is also specified, we'll start
	 * counting before delaying for the fixed delay.
	 *
	 * If a fixed prepare delay is also specified, we won't start
	 * counting until after the fixed delay. We can't overlap this
	 * fixed delay with the min time because the fixed delay
	 * doesn't happen at the end of the function if a HPD GPIO was
	 * specified.
	 *
	 * In other words:
	 *   prepare()
	 *     ...
	 *     // do fixed prepare delay
	 *     // wait for HPD GPIO if applicable
	 *     // start counting for prepare_to_enable
	 *
	 *   enable()
	 *     // do fixed enable delay
	 *     // enforce prepare_to_enable min time
	 *
	 * This is not specified in a standard way on eDP timing diagrams.
	 * It is effectively the time from HPD going high till you can
	 * turn on the backlight.
	 */
	unsigned int prepare_to_enable;

	/**
	 * @enable: Time for the panel to display a valid frame.
	 *
	 * The time (in milliseconds) that it takes for the panel to
	 * display the first valid frame after starting to receive
	 * video data.
	 *
	 * This is (T6-min + max(T7-max, T8-min)) on eDP timing diagrams or
	 * the delay after link training finishes until we can turn the
	 * backlight on and see valid data.
	 */
	unsigned int enable;

	/**
	 * @disable: Time for the panel to turn the display off.
	 *
	 * The time (in milliseconds) that it takes for the panel to
	 * turn the display off (no content is visible).
	 *
	 * This is T9-min (delay from backlight off to end of valid video
	 * data) on eDP timing diagrams. It is not common to set.
	 */
	unsigned int disable;

	/**
	 * @unprepare: Time to power down completely.
	 *
	 * The time (in milliseconds) that it takes for the panel
	 * to power itself down completely.
	 *
	 * This time is used to prevent a future "prepare" from
	 * starting until at least this many milliseconds has passed.
	 * If at prepare time less time has passed since unprepare
	 * finished, the driver waits for the remaining time.
	 *
	 * This is T12-min on eDP timing diagrams.
	 */
	unsigned int unprepare;
};

/**
 * struct panel_desc - Describes a simple panel.
 */
struct panel_desc {
	/**
	 * @modes: Pointer to array of fixed modes appropriate for this panel.
	 *
	 * If only one mode then this can just be the address of the mode.
	 * NOTE: cannot be used with "timings" and also if this is specified
	 * then you cannot override the mode in the device tree.
	 */
	const struct drm_display_mode *modes;

	/** @num_modes: Number of elements in modes array. */
	unsigned int num_modes;

	/**
	 * @timings: Pointer to array of display timings
	 *
	 * NOTE: cannot be used with "modes" and also these will be used to
	 * validate a device tree override if one is present.
	 */
	const struct display_timing *timings;

	/** @num_timings: Number of elements in timings array. */
	unsigned int num_timings;

	/** @bpc: Bits per color. */
	unsigned int bpc;

	/** @size: Structure containing the physical size of this panel. */
	struct {
		/**
		 * @size.width: Width (in mm) of the active display area.
		 */
		unsigned int width;

		/**
		 * @size.height: Height (in mm) of the active display area.
		 */
		unsigned int height;
	} size;

	/** @delay: Structure containing various delay values for this panel. */
	struct panel_delay delay;
};

/**
 * struct edp_panel_entry - Maps panel ID to delay / panel name.
 */
struct edp_panel_entry {
	/** @ident: edid identity used for panel matching. */
	const struct drm_edid_ident ident;

	/** @delay: The power sequencing delays needed for this panel. */
	const struct panel_delay *delay;

	/** @override_edid_mode: Override the mode obtained by edid. */
	const struct drm_display_mode *override_edid_mode;
};

struct panel_edp {
	struct drm_panel base;
	bool no_hpd;

	ktime_t prepared_time;
	ktime_t powered_on_time;
	ktime_t unprepared_time;

	const struct panel_desc *desc;

	struct regulator *supply;
	struct i2c_adapter *ddc;
	struct drm_dp_aux *aux;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *hpd_gpio;

	const struct edp_panel_entry *detected_panel;

	const struct drm_edid *drm_edid;

	struct drm_display_mode override_mode;

	enum drm_panel_orientation orientation;
};

static inline struct panel_edp *to_panel_edp(struct drm_panel *panel)
{
	return container_of(panel, struct panel_edp, base);
}

static unsigned int panel_edp_get_timings_modes(struct panel_edp *panel,
						struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];
		struct videomode vm;

		videomode_from_timing(dt, &vm);
		mode = drm_mode_create(connector->dev);
		if (!mode) {
			dev_err(panel->base.dev, "failed to add mode %ux%u\n",
				dt->hactive.typ, dt->vactive.typ);
			continue;
		}

		drm_display_mode_from_videomode(&vm, mode);

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (panel->desc->num_timings == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
		num++;
	}

	return num;
}

static unsigned int panel_edp_get_display_modes(struct panel_edp *panel,
						struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	for (i = 0; i < panel->desc->num_modes; i++) {
		const struct drm_display_mode *m = &panel->desc->modes[i];

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->base.dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay,
				drm_mode_vrefresh(m));
			continue;
		}

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (panel->desc->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	return num;
}

static int panel_edp_override_edid_mode(struct panel_edp *panel,
					struct drm_connector *connector,
					const struct drm_display_mode *override_mode)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, override_mode);
	if (!mode) {
		dev_err(panel->base.dev, "failed to add additional mode\n");
		return 0;
	}

	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	return 1;
}

static int panel_edp_get_non_edid_modes(struct panel_edp *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	bool has_override = panel->override_mode.type;
	unsigned int num = 0;

	if (!panel->desc)
		return 0;

	if (has_override) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel->override_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num = 1;
		} else {
			dev_err(panel->base.dev, "failed to add override mode\n");
		}
	}

	/* Only add timings if override was not there or failed to validate */
	if (num == 0 && panel->desc->num_timings)
		num = panel_edp_get_timings_modes(panel, connector);

	/*
	 * Only add fixed modes if timings/override added no mode.
	 *
	 * We should only ever have either the display timings specified
	 * or a fixed mode. Anything else is rather bogus.
	 */
	WARN_ON(panel->desc->num_timings && panel->desc->num_modes);
	if (num == 0)
		num = panel_edp_get_display_modes(panel, connector);

	connector->display_info.bpc = panel->desc->bpc;
	connector->display_info.width_mm = panel->desc->size.width;
	connector->display_info.height_mm = panel->desc->size.height;

	return num;
}

static void panel_edp_wait(ktime_t start_ktime, unsigned int min_ms)
{
	ktime_t now_ktime, min_ktime;

	if (!min_ms)
		return;

	min_ktime = ktime_add(start_ktime, ms_to_ktime(min_ms));
	now_ktime = ktime_get_boottime();

	if (ktime_before(now_ktime, min_ktime))
		msleep(ktime_to_ms(ktime_sub(min_ktime, now_ktime)) + 1);
}

static int panel_edp_disable(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);

	if (p->desc->delay.disable)
		msleep(p->desc->delay.disable);

	return 0;
}

static int panel_edp_suspend(struct device *dev)
{
	struct panel_edp *p = dev_get_drvdata(dev);

	drm_dp_dpcd_set_powered(p->aux, false);
	gpiod_set_value_cansleep(p->enable_gpio, 0);
	regulator_disable(p->supply);
	p->unprepared_time = ktime_get_boottime();

	return 0;
}

static int panel_edp_unprepare(struct drm_panel *panel)
{
	int ret;

	ret = pm_runtime_put_sync_suspend(panel->dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int panel_edp_get_hpd_gpio(struct device *dev, struct panel_edp *p)
{
	p->hpd_gpio = devm_gpiod_get_optional(dev, "hpd", GPIOD_IN);
	if (IS_ERR(p->hpd_gpio))
		return dev_err_probe(dev, PTR_ERR(p->hpd_gpio),
				     "failed to get 'hpd' GPIO\n");

	return 0;
}

static bool panel_edp_can_read_hpd(struct panel_edp *p)
{
	return !p->no_hpd && (p->hpd_gpio || (p->aux && p->aux->wait_hpd_asserted));
}

static int panel_edp_prepare_once(struct panel_edp *p)
{
	struct device *dev = p->base.dev;
	unsigned int delay;
	int err;
	int hpd_asserted;
	unsigned long hpd_wait_us;

	panel_edp_wait(p->unprepared_time, p->desc->delay.unprepare);

	err = regulator_enable(p->supply);
	if (err < 0) {
		dev_err(dev, "failed to enable supply: %d\n", err);
		return err;
	}

	gpiod_set_value_cansleep(p->enable_gpio, 1);
	drm_dp_dpcd_set_powered(p->aux, true);

	p->powered_on_time = ktime_get_boottime();

	delay = p->desc->delay.hpd_reliable;
	if (p->no_hpd)
		delay = max(delay, p->desc->delay.hpd_absent);
	if (delay)
		msleep(delay);

	if (panel_edp_can_read_hpd(p)) {
		if (p->desc->delay.hpd_absent)
			hpd_wait_us = p->desc->delay.hpd_absent * 1000UL;
		else
			hpd_wait_us = 2000000;

		if (p->hpd_gpio) {
			err = readx_poll_timeout(gpiod_get_value_cansleep,
						 p->hpd_gpio, hpd_asserted,
						 hpd_asserted, 1000, hpd_wait_us);
			if (hpd_asserted < 0)
				err = hpd_asserted;
		} else {
			err = p->aux->wait_hpd_asserted(p->aux, hpd_wait_us);
		}

		if (err) {
			if (err != -ETIMEDOUT)
				dev_err(dev,
					"error waiting for hpd GPIO: %d\n", err);
			goto error;
		}
	}

	p->prepared_time = ktime_get_boottime();

	return 0;

error:
	drm_dp_dpcd_set_powered(p->aux, false);
	gpiod_set_value_cansleep(p->enable_gpio, 0);
	regulator_disable(p->supply);
	p->unprepared_time = ktime_get_boottime();

	return err;
}

/*
 * Some panels simply don't always come up and need to be power cycled to
 * work properly.  We'll allow for a handful of retries.
 */
#define MAX_PANEL_PREPARE_TRIES		5

static int panel_edp_resume(struct device *dev)
{
	struct panel_edp *p = dev_get_drvdata(dev);
	int ret;
	int try;

	for (try = 0; try < MAX_PANEL_PREPARE_TRIES; try++) {
		ret = panel_edp_prepare_once(p);
		if (ret != -ETIMEDOUT)
			break;
	}

	if (ret == -ETIMEDOUT)
		dev_err(dev, "Prepare timeout after %d tries\n", try);
	else if (try)
		dev_warn(dev, "Prepare needed %d retries\n", try);

	return ret;
}

static int panel_edp_prepare(struct drm_panel *panel)
{
	int ret;

	ret = pm_runtime_get_sync(panel->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(panel->dev);
		return ret;
	}

	return 0;
}

static int panel_edp_enable(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);
	unsigned int delay;

	delay = p->desc->delay.enable;

	/*
	 * If there is a "prepare_to_enable" delay then that's supposed to be
	 * the delay from HPD going high until we can turn the backlight on.
	 * However, we can only count this if HPD is readable by the panel
	 * driver.
	 *
	 * If we aren't handling the HPD pin ourselves then the best we
	 * can do is assume that HPD went high immediately before we were
	 * called (and link training took zero time). Note that "no-hpd"
	 * actually counts as handling HPD ourselves since we're doing the
	 * worst case delay (in prepare) ourselves.
	 *
	 * NOTE: if we ever end up in this "if" statement then we're
	 * guaranteed that the panel_edp_wait() call below will do no delay.
	 * It already handles that case, though, so we don't need any special
	 * code for it.
	 */
	if (p->desc->delay.prepare_to_enable &&
	    !panel_edp_can_read_hpd(p) && !p->no_hpd)
		delay = max(delay, p->desc->delay.prepare_to_enable);

	if (delay)
		msleep(delay);

	panel_edp_wait(p->prepared_time, p->desc->delay.prepare_to_enable);

	panel_edp_wait(p->powered_on_time, p->desc->delay.powered_on_to_enable);

	return 0;
}

static int panel_edp_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_edp *p = to_panel_edp(panel);
	int num = 0;
	bool has_hard_coded_modes = p->desc->num_timings || p->desc->num_modes;
	bool has_override_edid_mode = p->detected_panel &&
				      p->detected_panel != ERR_PTR(-EINVAL) &&
				      p->detected_panel->override_edid_mode;

	/* probe EDID if a DDC bus is available */
	if (p->ddc) {
		pm_runtime_get_sync(panel->dev);

		if (!p->drm_edid)
			p->drm_edid = drm_edid_read_ddc(connector, p->ddc);

		drm_edid_connector_update(connector, p->drm_edid);

		/*
		 * If both edid and hard-coded modes exists, skip edid modes to
		 * avoid multiple preferred modes.
		 */
		if (p->drm_edid && !has_hard_coded_modes) {
			if (has_override_edid_mode) {
				/*
				 * override_edid_mode is specified. Use
				 * override_edid_mode instead of from edid.
				 */
				num += panel_edp_override_edid_mode(p, connector,
						p->detected_panel->override_edid_mode);
			} else {
				num += drm_edid_connector_add_modes(connector);
			}
		}

		pm_runtime_mark_last_busy(panel->dev);
		pm_runtime_put_autosuspend(panel->dev);
	}

	if (has_hard_coded_modes)
		num += panel_edp_get_non_edid_modes(p, connector);
	else if (!num)
		dev_warn(p->base.dev, "No display modes\n");

	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, p->orientation);

	return num;
}

static int panel_edp_get_timings(struct drm_panel *panel,
				 unsigned int num_timings,
				 struct display_timing *timings)
{
	struct panel_edp *p = to_panel_edp(panel);
	unsigned int i;

	if (p->desc->num_timings < num_timings)
		num_timings = p->desc->num_timings;

	if (timings)
		for (i = 0; i < num_timings; i++)
			timings[i] = p->desc->timings[i];

	return p->desc->num_timings;
}

static enum drm_panel_orientation panel_edp_get_orientation(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);

	return p->orientation;
}

static int detected_panel_show(struct seq_file *s, void *data)
{
	struct drm_panel *panel = s->private;
	struct panel_edp *p = to_panel_edp(panel);

	if (IS_ERR(p->detected_panel))
		seq_puts(s, "UNKNOWN\n");
	else if (!p->detected_panel)
		seq_puts(s, "HARDCODED\n");
	else
		seq_printf(s, "%s\n", p->detected_panel->ident.name);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(detected_panel);

static void panel_edp_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	debugfs_create_file("detected_panel", 0600, root, panel, &detected_panel_fops);
}

static const struct drm_panel_funcs panel_edp_funcs = {
	.disable = panel_edp_disable,
	.unprepare = panel_edp_unprepare,
	.prepare = panel_edp_prepare,
	.enable = panel_edp_enable,
	.get_modes = panel_edp_get_modes,
	.get_orientation = panel_edp_get_orientation,
	.get_timings = panel_edp_get_timings,
	.debugfs_init = panel_edp_debugfs_init,
};

#define PANEL_EDP_BOUNDS_CHECK(to_check, bounds, field) \
	(to_check->field.typ >= bounds->field.min && \
	 to_check->field.typ <= bounds->field.max)
static void panel_edp_parse_panel_timing_node(struct device *dev,
					      struct panel_edp *panel,
					      const struct display_timing *ot)
{
	const struct panel_desc *desc = panel->desc;
	struct videomode vm;
	unsigned int i;

	if (WARN_ON(desc->num_modes)) {
		dev_err(dev, "Reject override mode: panel has a fixed mode\n");
		return;
	}
	if (WARN_ON(!desc->num_timings)) {
		dev_err(dev, "Reject override mode: no timings specified\n");
		return;
	}

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];

		if (!PANEL_EDP_BOUNDS_CHECK(ot, dt, hactive) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, hfront_porch) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, hback_porch) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, hsync_len) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, vactive) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, vfront_porch) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, vback_porch) ||
		    !PANEL_EDP_BOUNDS_CHECK(ot, dt, vsync_len))
			continue;

		if (ot->flags != dt->flags)
			continue;

		videomode_from_timing(ot, &vm);
		drm_display_mode_from_videomode(&vm, &panel->override_mode);
		panel->override_mode.type |= DRM_MODE_TYPE_DRIVER |
					     DRM_MODE_TYPE_PREFERRED;
		break;
	}

	if (WARN_ON(!panel->override_mode.type))
		dev_err(dev, "Reject override mode: No display_timing found\n");
}

static const struct edp_panel_entry *find_edp_panel(u32 panel_id, const struct drm_edid *edid);

static void panel_edp_set_conservative_timings(struct panel_edp *panel, struct panel_desc *desc)
{
	/*
	 * It's highly likely that the panel will work if we use very
	 * conservative timings, so let's do that.
	 *
	 * Nearly all panels have a "unprepare" delay of 500 ms though
	 * there are a few with 1000. Let's stick 2000 in just to be
	 * super conservative.
	 *
	 * An "enable" delay of 80 ms seems the most common, but we'll
	 * throw in 200 ms to be safe.
	 */
	desc->delay.unprepare = 2000;
	desc->delay.enable = 200;

	panel->detected_panel = ERR_PTR(-EINVAL);
}

static int generic_edp_panel_probe(struct device *dev, struct panel_edp *panel)
{
	struct panel_desc *desc;
	const struct drm_edid *base_block;
	u32 panel_id;
	char vend[4];
	u16 product_id;
	u32 reliable_ms = 0;
	u32 absent_ms = 0;
	int ret;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	panel->desc = desc;

	/*
	 * Read the dts properties for the initial probe. These are used by
	 * the runtime resume code which will get called by the
	 * pm_runtime_get_sync() call below.
	 */
	of_property_read_u32(dev->of_node, "hpd-reliable-delay-ms", &reliable_ms);
	desc->delay.hpd_reliable = reliable_ms;
	of_property_read_u32(dev->of_node, "hpd-absent-delay-ms", &absent_ms);
	desc->delay.hpd_absent = absent_ms;

	/* Power the panel on so we can read the EDID */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev,
			"Couldn't power on panel to ID it; using conservative timings: %d\n",
			ret);
		panel_edp_set_conservative_timings(panel, desc);
		goto exit;
	}

	base_block = drm_edid_read_base_block(panel->ddc);
	if (base_block) {
		panel_id = drm_edid_get_panel_id(base_block);
	} else {
		dev_err(dev, "Couldn't read EDID for ID; using conservative timings\n");
		panel_edp_set_conservative_timings(panel, desc);
		goto exit;
	}
	drm_edid_decode_panel_id(panel_id, vend, &product_id);

	panel->detected_panel = find_edp_panel(panel_id, base_block);

	drm_edid_free(base_block);

	/*
	 * We're using non-optimized timings and want it really obvious that
	 * someone needs to add an entry to the table, so we'll do a WARN_ON
	 * splat.
	 */
	if (WARN_ON(!panel->detected_panel)) {
		dev_warn(dev,
			 "Unknown panel %s %#06x, using conservative timings\n",
			 vend, product_id);
		panel_edp_set_conservative_timings(panel, desc);
	} else {
		dev_info(dev, "Detected %s %s (%#06x)\n",
			 vend, panel->detected_panel->ident.name, product_id);

		/* Update the delay; everything else comes from EDID */
		desc->delay = *panel->detected_panel->delay;
	}

exit:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int panel_edp_probe(struct device *dev, const struct panel_desc *desc,
			   struct drm_dp_aux *aux)
{
	struct panel_edp *panel;
	struct display_timing dt;
	struct device_node *ddc;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->prepared_time = 0;
	panel->desc = desc;
	panel->aux = aux;

	panel->no_hpd = of_property_read_bool(dev->of_node, "no-hpd");
	if (!panel->no_hpd) {
		err = panel_edp_get_hpd_gpio(dev, panel);
		if (err)
			return err;
	}

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply))
		return PTR_ERR(panel->supply);

	panel->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(panel->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(panel->enable_gpio),
				     "failed to request GPIO\n");

	err = of_drm_get_panel_orientation(dev->of_node, &panel->orientation);
	if (err) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	ddc = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc) {
		panel->ddc = of_find_i2c_adapter_by_node(ddc);
		of_node_put(ddc);

		if (!panel->ddc)
			return -EPROBE_DEFER;
	} else if (aux) {
		panel->ddc = &aux->ddc;
	}

	if (!of_get_display_timing(dev->of_node, "panel-timing", &dt))
		panel_edp_parse_panel_timing_node(dev, panel, &dt);

	dev_set_drvdata(dev, panel);

	drm_panel_init(&panel->base, dev, &panel_edp_funcs, DRM_MODE_CONNECTOR_eDP);

	err = drm_panel_of_backlight(&panel->base);
	if (err)
		goto err_finished_ddc_init;

	/*
	 * We use runtime PM for prepare / unprepare since those power the panel
	 * on and off and those can be very slow operations. This is important
	 * to optimize powering the panel on briefly to read the EDID before
	 * fully enabling the panel.
	 */
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	if (of_device_is_compatible(dev->of_node, "edp-panel")) {
		err = generic_edp_panel_probe(dev, panel);
		if (err) {
			dev_err_probe(dev, err,
				      "Couldn't detect panel nor find a fallback\n");
			goto err_finished_pm_runtime;
		}
		/* generic_edp_panel_probe() replaces desc in the panel */
		desc = panel->desc;
	} else if (desc->bpc != 6 && desc->bpc != 8 && desc->bpc != 10) {
		dev_warn(dev, "Expected bpc in {6,8,10} but got: %u\n", desc->bpc);
	}

	if (!panel->base.backlight && panel->aux) {
		pm_runtime_get_sync(dev);
		err = drm_panel_dp_aux_backlight(&panel->base, panel->aux);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);

		/*
		 * Warn if we get an error, but don't consider it fatal. Having
		 * a panel where we can't control the backlight is better than
		 * no panel.
		 */
		if (err)
			dev_warn(dev, "failed to register dp aux backlight: %d\n", err);
	}

	drm_panel_add(&panel->base);

	return 0;

err_finished_pm_runtime:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
err_finished_ddc_init:
	if (panel->ddc && (!panel->aux || panel->ddc != &panel->aux->ddc))
		put_device(&panel->ddc->dev);

	return err;
}

static void panel_edp_shutdown(struct device *dev)
{
	struct panel_edp *panel = dev_get_drvdata(dev);

	/*
	 * NOTE: the following two calls don't really belong here. It is the
	 * responsibility of a correctly written DRM modeset driver to call
	 * drm_atomic_helper_shutdown() at shutdown time and that should
	 * cause the panel to be disabled / unprepared if needed. For now,
	 * however, we'll keep these calls due to the sheer number of
	 * different DRM modeset drivers used with panel-edp. Once we've
	 * confirmed that all DRM modeset drivers using this panel properly
	 * call drm_atomic_helper_shutdown() we can simply delete the two
	 * calls below.
	 *
	 * TO BE EXPLICIT: THE CALLS BELOW SHOULDN'T BE COPIED TO ANY NEW
	 * PANEL DRIVERS.
	 *
	 * FIXME: If we're still haven't figured out if all DRM modeset
	 * drivers properly call drm_atomic_helper_shutdown() but we _have_
	 * managed to make sure that DRM modeset drivers get their shutdown()
	 * callback before the panel's shutdown() callback (perhaps using
	 * device link), we could add a WARN_ON here to help move forward.
	 */
	if (panel->base.enabled)
		drm_panel_disable(&panel->base);
	if (panel->base.prepared)
		drm_panel_unprepare(&panel->base);
}

static void panel_edp_remove(struct device *dev)
{
	struct panel_edp *panel = dev_get_drvdata(dev);

	drm_panel_remove(&panel->base);
	panel_edp_shutdown(dev);

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	if (panel->ddc && (!panel->aux || panel->ddc != &panel->aux->ddc))
		put_device(&panel->ddc->dev);

	drm_edid_free(panel->drm_edid);
	panel->drm_edid = NULL;
}

static const struct display_timing auo_b101ean01_timing = {
	.pixelclock = { 65300000, 72500000, 75000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 18, 119, 119 },
	.hback_porch = { 21, 21, 21 },
	.hsync_len = { 32, 32, 32 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 4, 4, 4 },
	.vback_porch = { 8, 8, 8 },
	.vsync_len = { 18, 20, 20 },
};

static const struct panel_desc auo_b101ean01 = {
	.timings = &auo_b101ean01_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 217,
		.height = 136,
	},
};

static const struct drm_display_mode auo_b116xa3_mode = {
	.clock = 70589,
	.hdisplay = 1366,
	.hsync_start = 1366 + 40,
	.hsync_end = 1366 + 40 + 40,
	.htotal = 1366 + 40 + 40 + 32,
	.vdisplay = 768,
	.vsync_start = 768 + 10,
	.vsync_end = 768 + 10 + 12,
	.vtotal = 768 + 10 + 12 + 6,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct drm_display_mode auo_b116xak01_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 10,
	.vdisplay = 768,
	.vsync_start = 768 + 4,
	.vsync_end = 768 + 4 + 6,
	.vtotal = 768 + 4 + 6 + 15,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc auo_b116xak01 = {
	.modes = &auo_b116xak01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
	.delay = {
		.hpd_absent = 200,
		.unprepare = 500,
		.enable = 50,
	},
};

static const struct drm_display_mode auo_b133htn01_mode = {
	.clock = 150660,
	.hdisplay = 1920,
	.hsync_start = 1920 + 172,
	.hsync_end = 1920 + 172 + 80,
	.htotal = 1920 + 172 + 80 + 60,
	.vdisplay = 1080,
	.vsync_start = 1080 + 25,
	.vsync_end = 1080 + 25 + 10,
	.vtotal = 1080 + 25 + 10 + 10,
};

static const struct panel_desc auo_b133htn01 = {
	.modes = &auo_b133htn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
	.delay = {
		.hpd_reliable = 105,
		.enable = 20,
		.unprepare = 50,
	},
};

static const struct drm_display_mode auo_b133xtn01_mode = {
	.clock = 69500,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 3,
	.vsync_end = 768 + 3 + 6,
	.vtotal = 768 + 3 + 6 + 13,
};

static const struct panel_desc auo_b133xtn01 = {
	.modes = &auo_b133xtn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
};

static const struct drm_display_mode boe_nv101wxmn51_modes[] = {
	{
		.clock = 71900,
		.hdisplay = 1280,
		.hsync_start = 1280 + 48,
		.hsync_end = 1280 + 48 + 32,
		.htotal = 1280 + 48 + 32 + 80,
		.vdisplay = 800,
		.vsync_start = 800 + 3,
		.vsync_end = 800 + 3 + 5,
		.vtotal = 800 + 3 + 5 + 24,
	},
	{
		.clock = 57500,
		.hdisplay = 1280,
		.hsync_start = 1280 + 48,
		.hsync_end = 1280 + 48 + 32,
		.htotal = 1280 + 48 + 32 + 80,
		.vdisplay = 800,
		.vsync_start = 800 + 3,
		.vsync_end = 800 + 3 + 5,
		.vtotal = 800 + 3 + 5 + 24,
	},
};

static const struct panel_desc boe_nv101wxmn51 = {
	.modes = boe_nv101wxmn51_modes,
	.num_modes = ARRAY_SIZE(boe_nv101wxmn51_modes),
	.bpc = 8,
	.size = {
		.width = 217,
		.height = 136,
	},
	.delay = {
		/* TODO: should be hpd-absent and no-hpd should be set? */
		.hpd_reliable = 210,
		.enable = 50,
		.unprepare = 160,
	},
};

static const struct drm_display_mode boe_nv110wtm_n61_modes[] = {
	{
		.clock = 207800,
		.hdisplay = 2160,
		.hsync_start = 2160 + 48,
		.hsync_end = 2160 + 48 + 32,
		.htotal = 2160 + 48 + 32 + 100,
		.vdisplay = 1440,
		.vsync_start = 1440 + 3,
		.vsync_end = 1440 + 3 + 6,
		.vtotal = 1440 + 3 + 6 + 31,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{
		.clock = 138500,
		.hdisplay = 2160,
		.hsync_start = 2160 + 48,
		.hsync_end = 2160 + 48 + 32,
		.htotal = 2160 + 48 + 32 + 100,
		.vdisplay = 1440,
		.vsync_start = 1440 + 3,
		.vsync_end = 1440 + 3 + 6,
		.vtotal = 1440 + 3 + 6 + 31,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct panel_desc boe_nv110wtm_n61 = {
	.modes = boe_nv110wtm_n61_modes,
	.num_modes = ARRAY_SIZE(boe_nv110wtm_n61_modes),
	.bpc = 8,
	.size = {
		.width = 233,
		.height = 155,
	},
	.delay = {
		.hpd_absent = 200,
		.prepare_to_enable = 80,
		.enable = 50,
		.unprepare = 500,
	},
};

/* Also used for boe_nv133fhm_n62 */
static const struct drm_display_mode boe_nv133fhm_n61_modes = {
	.clock = 147840,
	.hdisplay = 1920,
	.hsync_start = 1920 + 48,
	.hsync_end = 1920 + 48 + 32,
	.htotal = 1920 + 48 + 32 + 200,
	.vdisplay = 1080,
	.vsync_start = 1080 + 3,
	.vsync_end = 1080 + 3 + 6,
	.vtotal = 1080 + 3 + 6 + 31,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
};

/* Also used for boe_nv133fhm_n62 */
static const struct panel_desc boe_nv133fhm_n61 = {
	.modes = &boe_nv133fhm_n61_modes,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 294,
		.height = 165,
	},
	.delay = {
		/*
		 * When power is first given to the panel there's a short
		 * spike on the HPD line.  It was explained that this spike
		 * was until the TCON data download was complete.  On
		 * one system this was measured at 8 ms.  We'll put 15 ms
		 * in the prepare delay just to be safe.  That means:
		 * - If HPD isn't hooked up you still have 200 ms delay.
		 * - If HPD is hooked up we won't try to look at it for the
		 *   first 15 ms.
		 */
		.hpd_reliable = 15,
		.hpd_absent = 200,

		.unprepare = 500,
	},
};

static const struct drm_display_mode boe_nv140fhmn49_modes[] = {
	{
		.clock = 148500,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 2200,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1125,
	},
};

static const struct panel_desc boe_nv140fhmn49 = {
	.modes = boe_nv140fhmn49_modes,
	.num_modes = ARRAY_SIZE(boe_nv140fhmn49_modes),
	.bpc = 6,
	.size = {
		.width = 309,
		.height = 174,
	},
	.delay = {
		/* TODO: should be hpd-absent and no-hpd should be set? */
		.hpd_reliable = 210,
		.enable = 50,
		.unprepare = 160,
	},
};

static const struct drm_display_mode innolux_n116bca_ea1_mode = {
	.clock = 76420,
	.hdisplay = 1366,
	.hsync_start = 1366 + 136,
	.hsync_end = 1366 + 136 + 30,
	.htotal = 1366 + 136 + 30 + 60,
	.vdisplay = 768,
	.vsync_start = 768 + 8,
	.vsync_end = 768 + 8 + 12,
	.vtotal = 768 + 8 + 12 + 12,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_n116bca_ea1 = {
	.modes = &innolux_n116bca_ea1_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
	.delay = {
		.hpd_absent = 200,
		.enable = 80,
		.disable = 50,
		.unprepare = 500,
	},
};

/*
 * Datasheet specifies that at 60 Hz refresh rate:
 * - total horizontal time: { 1506, 1592, 1716 }
 * - total vertical time: { 788, 800, 868 }
 *
 * ...but doesn't go into exactly how that should be split into a front
 * porch, back porch, or sync length.  For now we'll leave a single setting
 * here which allows a bit of tweaking of the pixel clock at the expense of
 * refresh rate.
 */
static const struct display_timing innolux_n116bge_timing = {
	.pixelclock = { 72600000, 76420000, 80240000 },
	.hactive = { 1366, 1366, 1366 },
	.hfront_porch = { 136, 136, 136 },
	.hback_porch = { 60, 60, 60 },
	.hsync_len = { 30, 30, 30 },
	.vactive = { 768, 768, 768 },
	.vfront_porch = { 8, 8, 8 },
	.vback_porch = { 12, 12, 12 },
	.vsync_len = { 12, 12, 12 },
	.flags = DISPLAY_FLAGS_VSYNC_LOW | DISPLAY_FLAGS_HSYNC_LOW,
};

static const struct panel_desc innolux_n116bge = {
	.timings = &innolux_n116bge_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
};

static const struct drm_display_mode innolux_n125hce_gn1_mode = {
	.clock = 162000,
	.hdisplay = 1920,
	.hsync_start = 1920 + 40,
	.hsync_end = 1920 + 40 + 40,
	.htotal = 1920 + 40 + 40 + 80,
	.vdisplay = 1080,
	.vsync_start = 1080 + 4,
	.vsync_end = 1080 + 4 + 4,
	.vtotal = 1080 + 4 + 4 + 24,
};

static const struct panel_desc innolux_n125hce_gn1 = {
	.modes = &innolux_n125hce_gn1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 276,
		.height = 155,
	},
};

static const struct drm_display_mode innolux_p120zdg_bf1_mode = {
	.clock = 206016,
	.hdisplay = 2160,
	.hsync_start = 2160 + 48,
	.hsync_end = 2160 + 48 + 32,
	.htotal = 2160 + 48 + 32 + 80,
	.vdisplay = 1440,
	.vsync_start = 1440 + 3,
	.vsync_end = 1440 + 3 + 10,
	.vtotal = 1440 + 3 + 10 + 27,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc innolux_p120zdg_bf1 = {
	.modes = &innolux_p120zdg_bf1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 254,
		.height = 169,
	},
	.delay = {
		.hpd_absent = 200,
		.unprepare = 500,
	},
};

static const struct drm_display_mode kingdisplay_kd116n21_30nv_a010_mode = {
	.clock = 81000,
	.hdisplay = 1366,
	.hsync_start = 1366 + 40,
	.hsync_end = 1366 + 40 + 32,
	.htotal = 1366 + 40 + 32 + 62,
	.vdisplay = 768,
	.vsync_start = 768 + 5,
	.vsync_end = 768 + 5 + 5,
	.vtotal = 768 + 5 + 5 + 122,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc kingdisplay_kd116n21_30nv_a010 = {
	.modes = &kingdisplay_kd116n21_30nv_a010_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
	.delay = {
		.hpd_absent = 200,
	},
};

static const struct drm_display_mode lg_lp079qx1_sp0v_mode = {
	.clock = 200000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 12,
	.hsync_end = 1536 + 12 + 16,
	.htotal = 1536 + 12 + 16 + 48,
	.vdisplay = 2048,
	.vsync_start = 2048 + 8,
	.vsync_end = 2048 + 8 + 4,
	.vtotal = 2048 + 8 + 4 + 8,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc lg_lp079qx1_sp0v = {
	.modes = &lg_lp079qx1_sp0v_mode,
	.num_modes = 1,
	.size = {
		.width = 129,
		.height = 171,
	},
};

static const struct drm_display_mode lg_lp097qx1_spa1_mode = {
	.clock = 205210,
	.hdisplay = 2048,
	.hsync_start = 2048 + 150,
	.hsync_end = 2048 + 150 + 5,
	.htotal = 2048 + 150 + 5 + 5,
	.vdisplay = 1536,
	.vsync_start = 1536 + 3,
	.vsync_end = 1536 + 3 + 1,
	.vtotal = 1536 + 3 + 1 + 9,
};

static const struct panel_desc lg_lp097qx1_spa1 = {
	.modes = &lg_lp097qx1_spa1_mode,
	.num_modes = 1,
	.size = {
		.width = 208,
		.height = 147,
	},
};

static const struct drm_display_mode lg_lp120up1_mode = {
	.clock = 162300,
	.hdisplay = 1920,
	.hsync_start = 1920 + 40,
	.hsync_end = 1920 + 40 + 40,
	.htotal = 1920 + 40 + 40 + 80,
	.vdisplay = 1280,
	.vsync_start = 1280 + 4,
	.vsync_end = 1280 + 4 + 4,
	.vtotal = 1280 + 4 + 4 + 12,
};

static const struct panel_desc lg_lp120up1 = {
	.modes = &lg_lp120up1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 267,
		.height = 183,
	},
};

static const struct drm_display_mode lg_lp129qe_mode = {
	.clock = 285250,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1700,
	.vsync_start = 1700 + 3,
	.vsync_end = 1700 + 3 + 10,
	.vtotal = 1700 + 3 + 10 + 36,
};

static const struct panel_desc lg_lp129qe = {
	.modes = &lg_lp129qe_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 272,
		.height = 181,
	},
};

static const struct drm_display_mode neweast_wjfh116008a_modes[] = {
	{
		.clock = 138500,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 1920 + 48 + 32 + 80,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1080 + 3 + 5 + 23,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	}, {
		.clock = 110920,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 1920 + 48 + 32 + 80,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1080 + 3 + 5 + 23,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	}
};

static const struct panel_desc neweast_wjfh116008a = {
	.modes = neweast_wjfh116008a_modes,
	.num_modes = 2,
	.bpc = 6,
	.size = {
		.width = 260,
		.height = 150,
	},
	.delay = {
		.hpd_reliable = 110,
		.enable = 20,
		.unprepare = 500,
	},
};

static const struct drm_display_mode samsung_lsn122dl01_c01_mode = {
	.clock = 271560,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1600,
	.vsync_start = 1600 + 2,
	.vsync_end = 1600 + 2 + 5,
	.vtotal = 1600 + 2 + 5 + 57,
};

static const struct panel_desc samsung_lsn122dl01_c01 = {
	.modes = &samsung_lsn122dl01_c01_mode,
	.num_modes = 1,
	.size = {
		.width = 263,
		.height = 164,
	},
};

static const struct drm_display_mode samsung_ltn140at29_301_mode = {
	.clock = 76300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 64,
	.hsync_end = 1366 + 64 + 48,
	.htotal = 1366 + 64 + 48 + 128,
	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 2 + 5,
	.vtotal = 768 + 2 + 5 + 17,
};

static const struct panel_desc samsung_ltn140at29_301 = {
	.modes = &samsung_ltn140at29_301_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 320,
		.height = 187,
	},
};

static const struct drm_display_mode sharp_ld_d5116z01b_mode = {
	.clock = 168480,
	.hdisplay = 1920,
	.hsync_start = 1920 + 48,
	.hsync_end = 1920 + 48 + 32,
	.htotal = 1920 + 48 + 32 + 80,
	.vdisplay = 1280,
	.vsync_start = 1280 + 3,
	.vsync_end = 1280 + 3 + 10,
	.vtotal = 1280 + 3 + 10 + 57,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc sharp_ld_d5116z01b = {
	.modes = &sharp_ld_d5116z01b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 260,
		.height = 120,
	},
};

static const struct display_timing sharp_lq123p1jx31_timing = {
	.pixelclock = { 252750000, 252750000, 266604720 },
	.hactive = { 2400, 2400, 2400 },
	.hfront_porch = { 48, 48, 48 },
	.hback_porch = { 80, 80, 84 },
	.hsync_len = { 32, 32, 32 },
	.vactive = { 1600, 1600, 1600 },
	.vfront_porch = { 3, 3, 3 },
	.vback_porch = { 33, 33, 120 },
	.vsync_len = { 10, 10, 10 },
	.flags = DISPLAY_FLAGS_VSYNC_LOW | DISPLAY_FLAGS_HSYNC_LOW,
};

static const struct panel_desc sharp_lq123p1jx31 = {
	.timings = &sharp_lq123p1jx31_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 259,
		.height = 173,
	},
	.delay = {
		.hpd_reliable = 110,
		.enable = 50,
		.unprepare = 550,
	},
};

static const struct of_device_id platform_of_match[] = {
	{
		/* Must be first */
		.compatible = "edp-panel",
	},
	/*
	 * Do not add panels to the list below unless they cannot be handled by
	 * the generic edp-panel compatible.
	 *
	 * The only two valid reasons are:
	 * - Because of the panel issues (e.g. broken EDID or broken
	 *   identification).
	 * - Because the eDP drivers didn't wire up the AUX bus properly.
	 *   NOTE that, though this is a marginally valid reason,
	 *   some justification needs to be made for why the platform can't
	 *   wire up the AUX bus properly.
	 *
	 * In all other cases the platform should use the aux-bus and declare
	 * the panel using the 'edp-panel' compatible as a device on the AUX
	 * bus.
	 */
	{
		.compatible = "auo,b101ean01",
		.data = &auo_b101ean01,
	}, {
		.compatible = "auo,b116xa01",
		.data = &auo_b116xak01,
	}, {
		.compatible = "auo,b133htn01",
		.data = &auo_b133htn01,
	}, {
		.compatible = "auo,b133xtn01",
		.data = &auo_b133xtn01,
	}, {
		.compatible = "boe,nv101wxmn51",
		.data = &boe_nv101wxmn51,
	}, {
		.compatible = "boe,nv110wtm-n61",
		.data = &boe_nv110wtm_n61,
	}, {
		.compatible = "boe,nv133fhm-n61",
		.data = &boe_nv133fhm_n61,
	}, {
		.compatible = "boe,nv133fhm-n62",
		.data = &boe_nv133fhm_n61,
	}, {
		.compatible = "boe,nv140fhmn49",
		.data = &boe_nv140fhmn49,
	}, {
		.compatible = "innolux,n116bca-ea1",
		.data = &innolux_n116bca_ea1,
	}, {
		.compatible = "innolux,n116bge",
		.data = &innolux_n116bge,
	}, {
		.compatible = "innolux,n125hce-gn1",
		.data = &innolux_n125hce_gn1,
	}, {
		.compatible = "innolux,p120zdg-bf1",
		.data = &innolux_p120zdg_bf1,
	}, {
		.compatible = "kingdisplay,kd116n21-30nv-a010",
		.data = &kingdisplay_kd116n21_30nv_a010,
	}, {
		.compatible = "lg,lp079qx1-sp0v",
		.data = &lg_lp079qx1_sp0v,
	}, {
		.compatible = "lg,lp097qx1-spa1",
		.data = &lg_lp097qx1_spa1,
	}, {
		.compatible = "lg,lp120up1",
		.data = &lg_lp120up1,
	}, {
		.compatible = "lg,lp129qe",
		.data = &lg_lp129qe,
	}, {
		.compatible = "neweast,wjfh116008a",
		.data = &neweast_wjfh116008a,
	}, {
		.compatible = "samsung,lsn122dl01-c01",
		.data = &samsung_lsn122dl01_c01,
	}, {
		.compatible = "samsung,ltn140at29-301",
		.data = &samsung_ltn140at29_301,
	}, {
		.compatible = "sharp,ld-d5116z01b",
		.data = &sharp_ld_d5116z01b,
	}, {
		.compatible = "sharp,lq123p1jx31",
		.data = &sharp_lq123p1jx31,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, platform_of_match);

static const struct panel_delay delay_200_500_p2e80 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.prepare_to_enable = 80,
};

static const struct panel_delay delay_200_500_e50_p2e80 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 50,
	.prepare_to_enable = 80,
};

static const struct panel_delay delay_200_500_p2e100 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.prepare_to_enable = 100,
};

static const struct panel_delay delay_200_500_e50 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 50,
};

static const struct panel_delay delay_200_500_e50_p2e200 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 50,
	.prepare_to_enable = 200,
};

static const struct panel_delay delay_200_500_e80 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 80,
};

static const struct panel_delay delay_200_500_e80_d50 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 80,
	.disable = 50,
};

static const struct panel_delay delay_80_500_e50 = {
	.hpd_absent = 80,
	.unprepare = 500,
	.enable = 50,
};

static const struct panel_delay delay_100_500_e200 = {
	.hpd_absent = 100,
	.unprepare = 500,
	.enable = 200,
};

static const struct panel_delay delay_200_500_e200 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 200,
};

static const struct panel_delay delay_200_500_e200_d200 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 200,
	.disable = 200,
};

static const struct panel_delay delay_200_500_e200_d10 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 200,
	.disable = 10,
};

static const struct panel_delay delay_200_150_e200 = {
	.hpd_absent = 200,
	.unprepare = 150,
	.enable = 200,
};

static const struct panel_delay delay_200_500_e50_po2e200 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 50,
	.powered_on_to_enable = 200,
};

#define EDP_PANEL_ENTRY(vend_chr_0, vend_chr_1, vend_chr_2, product_id, _delay, _name) \
{ \
	.ident = { \
		.name = _name, \
		.panel_id = drm_edid_encode_panel_id(vend_chr_0, vend_chr_1, vend_chr_2, \
						     product_id), \
	}, \
	.delay = _delay \
}

#define EDP_PANEL_ENTRY2(vend_chr_0, vend_chr_1, vend_chr_2, product_id, _delay, _name, _mode) \
{ \
	.ident = { \
		.name = _name, \
		.panel_id = drm_edid_encode_panel_id(vend_chr_0, vend_chr_1, vend_chr_2, \
						     product_id), \
	}, \
	.delay = _delay, \
	.override_edid_mode = _mode \
}

/*
 * This table is used to figure out power sequencing delays for panels that
 * are detected by EDID. Entries here may point to entries in the
 * platform_of_match table (if a panel is listed in both places).
 *
 * Sort first by vendor, then by product ID.
 */
static const struct edp_panel_entry edp_panels[] = {
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x105c, &delay_200_500_e50, "B116XTN01.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x1062, &delay_200_500_e50, "B120XAN01.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x125c, &delay_200_500_e50, "Unknown"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x145c, &delay_200_500_e50, "B116XAB01.4"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x1999, &delay_200_500_e50, "Unknown"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x1e9b, &delay_200_500_e50, "B133UAN02.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x1ea5, &delay_200_500_e50, "B116XAK01.6"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x203d, &delay_200_500_e50, "B140HTN02.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x208d, &delay_200_500_e50, "B140HTN02.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x235c, &delay_200_500_e50, "B116XTN02.3"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x239b, &delay_200_500_e50, "B116XAN06.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x255c, &delay_200_500_e50, "B116XTN02.5"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x403d, &delay_200_500_e50, "B140HAN04.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x405c, &auo_b116xak01.delay, "B116XAN04.0"),
	EDP_PANEL_ENTRY2('A', 'U', 'O', 0x405c, &auo_b116xak01.delay, "B116XAK01.0",
			 &auo_b116xa3_mode),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x435c, &delay_200_500_e50, "Unknown"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x582d, &delay_200_500_e50, "B133UAN01.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x615c, &delay_200_500_e50, "B116XAN06.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x635c, &delay_200_500_e50, "B116XAN06.3"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x639c, &delay_200_500_e50, "B140HAK02.7"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x723c, &delay_200_500_e50, "B140XTN07.2"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x73aa, &delay_200_500_e50, "B116XTN02.3"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x8594, &delay_200_500_e50, "B133UAN01.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0xa199, &delay_200_500_e50, "B116XAN06.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0xc4b4, &delay_200_500_e50, "B116XAT04.1"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0xd497, &delay_200_500_e50, "B120XAN01.0"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0xf390, &delay_200_500_e50, "B140XTN07.7"),

	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0607, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0608, &delay_200_500_e50, "NT116WHM-N11"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0609, &delay_200_500_e50_po2e200, "NT116WHM-N21 V4.1"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0623, &delay_200_500_e200, "NT116WHM-N21 V4.0"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0668, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x068f, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x06e5, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0705, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0715, &delay_200_150_e200, "NT116WHM-N21"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0717, &delay_200_500_e50_po2e200, "NV133FHM-N42"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0731, &delay_200_500_e80, "NT116WHM-N42"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0741, &delay_200_500_e200, "NT116WHM-N44"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0744, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x074c, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0751, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0754, &delay_200_500_e50_po2e200, "NV116WHM-N45"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0771, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0786, &delay_200_500_p2e80, "NV116WHM-T01"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0797, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07a8, &delay_200_500_e50_po2e200, "NT116WHM-N21"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07d1, &boe_nv133fhm_n61.delay, "NV133FHM-N61"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07d3, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07f6, &delay_200_500_e200, "NT140FHM-N44"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07f8, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0813, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0827, &delay_200_500_e50_p2e80, "NT140WHM-N44 V8.0"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x082d, &boe_nv133fhm_n61.delay, "NV133FHM-N62"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0843, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x08b2, &delay_200_500_e200, "NT140WHM-N49"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0848, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0849, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x09c3, &delay_200_500_e50, "NT116WHM-N21,836X2"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x094b, &delay_200_500_e50, "NT116WHM-N21"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0951, &delay_200_500_e80, "NV116WHM-N47"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x095f, &delay_200_500_e50, "NE135FBM-N41 v8.1"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x096e, &delay_200_500_e50_po2e200, "NV116WHM-T07 V8.0"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0979, &delay_200_500_e50, "NV116WHM-N49 V8.0"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x098d, &boe_nv110wtm_n61.delay, "NV110WTM-N61"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0993, &delay_200_500_e80, "NV116WHM-T14 V8.0"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x09ad, &delay_200_500_e80, "NV116WHM-N47"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x09ae, &delay_200_500_e200, "NT140FHM-N45"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x09dd, &delay_200_500_e50, "NT116WHM-N21"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0a1b, &delay_200_500_e50, "NV133WUM-N63"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0a36, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0a3e, &delay_200_500_e80, "NV116WHM-N49"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0a5d, &delay_200_500_e50, "NV116WHM-N45"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0ac5, &delay_200_500_e50, "NV116WHM-N4C"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0ae8, &delay_200_500_e50_p2e80, "NV140WUM-N41"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0b34, &delay_200_500_e80, "NV122WUM-N41"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0b43, &delay_200_500_e200, "NV140FHM-T09"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0b56, &delay_200_500_e80, "NT140FHM-N47"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0b66, &delay_200_500_e80, "NE140WUM-N6G"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0c20, &delay_200_500_e80, "NT140FHM-N47"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0cb6, &delay_200_500_e200, "NT116WHM-N44"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0cfa, &delay_200_500_e50, "NV116WHM-A4D"),

	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1130, &delay_200_500_e50, "N116BGE-EB2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1132, &delay_200_500_e80_d50, "N116BGE-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1138, &innolux_n116bca_ea1.delay, "N116BCA-EA1-RC4"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1139, &delay_200_500_e80_d50, "N116BGE-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1141, &delay_200_500_e80_d50, "Unknown"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1145, &delay_200_500_e80_d50, "N116BCN-EB1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x114a, &delay_200_500_e80_d50, "Unknown"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x114c, &innolux_n116bca_ea1.delay, "N116BCA-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1152, &delay_200_500_e80_d50, "N116BCN-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1153, &delay_200_500_e80_d50, "N116BGE-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1154, &delay_200_500_e80_d50, "N116BCA-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1156, &delay_200_500_e80_d50, "Unknown"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1157, &delay_200_500_e80_d50, "N116BGE-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x115b, &delay_200_500_e80_d50, "N116BCN-EB1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x115d, &delay_200_500_e80_d50, "N116BCA-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x115e, &delay_200_500_e80_d50, "N116BCA-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1160, &delay_200_500_e80_d50, "N116BCJ-EAK"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1161, &delay_200_500_e80, "N116BCP-EA2"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1247, &delay_200_500_e80_d50, "N120ACA-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x142b, &delay_200_500_e80_d50, "N140HCA-EAC"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x142e, &delay_200_500_e80_d50, "N140BGA-EA4"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x144f, &delay_200_500_e80_d50, "N140HGA-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x1468, &delay_200_500_e80, "N140HGA-EA1"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x14d4, &delay_200_500_e80_d50, "N140HCA-EAC"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x14d6, &delay_200_500_e80_d50, "N140BGA-EA4"),
	EDP_PANEL_ENTRY('C', 'M', 'N', 0x14e5, &delay_200_500_e80_d50, "N140HGA-EA1"),

	EDP_PANEL_ENTRY('C', 'S', 'O', 0x1200, &delay_200_500_e50_p2e200, "MNC207QS1-1"),

	EDP_PANEL_ENTRY('C', 'S', 'W', 0x1100, &delay_200_500_e80_d50, "MNB601LS1-1"),
	EDP_PANEL_ENTRY('C', 'S', 'W', 0x1104, &delay_200_500_e50, "MNB601LS1-4"),

	EDP_PANEL_ENTRY('H', 'K', 'C', 0x2d51, &delay_200_500_e200, "Unknown"),
	EDP_PANEL_ENTRY('H', 'K', 'C', 0x2d5b, &delay_200_500_e200, "MB116AN01"),
	EDP_PANEL_ENTRY('H', 'K', 'C', 0x2d5c, &delay_200_500_e200, "MB116AN01-2"),

	EDP_PANEL_ENTRY('I', 'V', 'O', 0x048e, &delay_200_500_e200_d10, "M116NWR6 R5"),
	EDP_PANEL_ENTRY('I', 'V', 'O', 0x057d, &delay_200_500_e200, "R140NWF5 RH"),
	EDP_PANEL_ENTRY('I', 'V', 'O', 0x854a, &delay_200_500_p2e100, "M133NW4J"),
	EDP_PANEL_ENTRY('I', 'V', 'O', 0x854b, &delay_200_500_p2e100, "R133NW4K-R0"),
	EDP_PANEL_ENTRY('I', 'V', 'O', 0x8c4d, &delay_200_150_e200, "R140NWFM R1"),

	EDP_PANEL_ENTRY('K', 'D', 'B', 0x044f, &delay_200_500_e80_d50, "Unknown"),
	EDP_PANEL_ENTRY('K', 'D', 'B', 0x0624, &kingdisplay_kd116n21_30nv_a010.delay, "116N21-30NV-A010"),
	EDP_PANEL_ENTRY('K', 'D', 'B', 0x1118, &delay_200_500_e50, "KD116N29-30NK-A005"),
	EDP_PANEL_ENTRY('K', 'D', 'B', 0x1120, &delay_200_500_e80_d50, "116N29-30NK-C007"),
	EDP_PANEL_ENTRY('K', 'D', 'B', 0x1212, &delay_200_500_e50, "KD116N0930A16"),

	EDP_PANEL_ENTRY('K', 'D', 'C', 0x044f, &delay_200_500_e50, "KD116N9-30NH-F3"),
	EDP_PANEL_ENTRY('K', 'D', 'C', 0x05f1, &delay_200_500_e80_d50, "KD116N5-30NV-G7"),
	EDP_PANEL_ENTRY('K', 'D', 'C', 0x0809, &delay_200_500_e50, "KD116N2930A15"),

	EDP_PANEL_ENTRY('L', 'G', 'D', 0x0000, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x048d, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x0497, &delay_200_500_e200_d200, "LP116WH7-SPB1"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x052c, &delay_200_500_e200_d200, "LP133WF2-SPL7"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x0537, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x054a, &delay_200_500_e200_d200, "LP116WH8-SPC1"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x0567, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x05af, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x05f1, &delay_200_500_e200_d200, "Unknown"),
	EDP_PANEL_ENTRY('L', 'G', 'D', 0x0778, &delay_200_500_e200_d200, "134WT1"),

	EDP_PANEL_ENTRY('S', 'H', 'P', 0x1511, &delay_200_500_e50, "LQ140M1JW48"),
	EDP_PANEL_ENTRY('S', 'H', 'P', 0x1523, &delay_80_500_e50, "LQ140M1JW46"),
	EDP_PANEL_ENTRY('S', 'H', 'P', 0x153a, &delay_200_500_e50, "LQ140T1JH01"),
	EDP_PANEL_ENTRY('S', 'H', 'P', 0x154c, &delay_200_500_p2e100, "LQ116M1JW10"),
	EDP_PANEL_ENTRY('S', 'H', 'P', 0x1593, &delay_200_500_p2e100, "LQ134N1"),

	EDP_PANEL_ENTRY('S', 'T', 'A', 0x0100, &delay_100_500_e200, "2081116HHD028001-51D"),

	{ /* sentinal */ }
};

static const struct edp_panel_entry *find_edp_panel(u32 panel_id, const struct drm_edid *edid)
{
	const struct edp_panel_entry *panel;

	if (!panel_id)
		return NULL;

	/*
	 * Match with identity first. This allows handling the case where
	 * vendors incorrectly reused the same panel ID for multiple panels that
	 * need different settings. If there's no match, try again with panel
	 * ID, which should be unique.
	 */
	for (panel = edp_panels; panel->ident.panel_id; panel++)
		if (drm_edid_match(edid, &panel->ident))
			return panel;

	for (panel = edp_panels; panel->ident.panel_id; panel++)
		if (panel->ident.panel_id == panel_id)
			return panel;

	return NULL;
}

static int panel_edp_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	/* Skip one since "edp-panel" is only supported on DP AUX bus */
	id = of_match_node(platform_of_match + 1, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return panel_edp_probe(&pdev->dev, id->data, NULL);
}

static void panel_edp_platform_remove(struct platform_device *pdev)
{
	panel_edp_remove(&pdev->dev);
}

static void panel_edp_platform_shutdown(struct platform_device *pdev)
{
	panel_edp_shutdown(&pdev->dev);
}

static const struct dev_pm_ops panel_edp_pm_ops = {
	SET_RUNTIME_PM_OPS(panel_edp_suspend, panel_edp_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver panel_edp_platform_driver = {
	.driver = {
		.name = "panel-edp",
		.of_match_table = platform_of_match,
		.pm = &panel_edp_pm_ops,
	},
	.probe = panel_edp_platform_probe,
	.remove_new = panel_edp_platform_remove,
	.shutdown = panel_edp_platform_shutdown,
};

static int panel_edp_dp_aux_ep_probe(struct dp_aux_ep_device *aux_ep)
{
	const struct of_device_id *id;

	id = of_match_node(platform_of_match, aux_ep->dev.of_node);
	if (!id)
		return -ENODEV;

	return panel_edp_probe(&aux_ep->dev, id->data, aux_ep->aux);
}

static void panel_edp_dp_aux_ep_remove(struct dp_aux_ep_device *aux_ep)
{
	panel_edp_remove(&aux_ep->dev);
}

static void panel_edp_dp_aux_ep_shutdown(struct dp_aux_ep_device *aux_ep)
{
	panel_edp_shutdown(&aux_ep->dev);
}

static struct dp_aux_ep_driver panel_edp_dp_aux_ep_driver = {
	.driver = {
		.name = "panel-simple-dp-aux",
		.of_match_table = platform_of_match,	/* Same as platform one! */
		.pm = &panel_edp_pm_ops,
	},
	.probe = panel_edp_dp_aux_ep_probe,
	.remove = panel_edp_dp_aux_ep_remove,
	.shutdown = panel_edp_dp_aux_ep_shutdown,
};

static int __init panel_edp_init(void)
{
	int err;

	err = platform_driver_register(&panel_edp_platform_driver);
	if (err < 0)
		return err;

	err = dp_aux_dp_driver_register(&panel_edp_dp_aux_ep_driver);
	if (err < 0)
		goto err_did_platform_register;

	return 0;

err_did_platform_register:
	platform_driver_unregister(&panel_edp_platform_driver);

	return err;
}
module_init(panel_edp_init);

static void __exit panel_edp_exit(void)
{
	dp_aux_dp_driver_unregister(&panel_edp_dp_aux_ep_driver);
	platform_driver_unregister(&panel_edp_platform_driver);
}
module_exit(panel_edp_exit);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM Driver for Simple eDP Panels");
MODULE_LICENSE("GPL and additional rights");
