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

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/dp/drm_dp_aux_bus.h>
#include <drm/dp/drm_dp_helper.h>
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
	 * NOTE: on some old panel data this number appers to be much too big.
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
	/** @panel_id: 32-bit ID for panel, encoded with drm_edid_encode_panel_id(). */
	u32 panel_id;

	/** @delay: The power sequencing delays needed for this panel. */
	const struct panel_delay *delay;

	/** @name: Name of this panel (for printing to logs). */
	const char *name;
};

struct panel_edp {
	struct drm_panel base;
	bool enabled;
	bool no_hpd;

	bool prepared;

	ktime_t prepared_time;
	ktime_t unprepared_time;

	const struct panel_desc *desc;

	struct regulator *supply;
	struct i2c_adapter *ddc;
	struct drm_dp_aux *aux;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *hpd_gpio;

	const struct edp_panel_entry *detected_panel;

	struct edid *edid;

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
	now_ktime = ktime_get();

	if (ktime_before(now_ktime, min_ktime))
		msleep(ktime_to_ms(ktime_sub(min_ktime, now_ktime)) + 1);
}

static int panel_edp_disable(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);

	if (!p->enabled)
		return 0;

	if (p->desc->delay.disable)
		msleep(p->desc->delay.disable);

	p->enabled = false;

	return 0;
}

static int panel_edp_suspend(struct device *dev)
{
	struct panel_edp *p = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(p->enable_gpio, 0);
	regulator_disable(p->supply);
	p->unprepared_time = ktime_get();

	return 0;
}

static int panel_edp_unprepare(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);
	int ret;

	/* Unpreparing when already unprepared is a no-op */
	if (!p->prepared)
		return 0;

	pm_runtime_mark_last_busy(panel->dev);
	ret = pm_runtime_put_autosuspend(panel->dev);
	if (ret < 0)
		return ret;
	p->prepared = false;

	return 0;
}

static int panel_edp_get_hpd_gpio(struct device *dev, struct panel_edp *p)
{
	int err;

	p->hpd_gpio = devm_gpiod_get_optional(dev, "hpd", GPIOD_IN);
	if (IS_ERR(p->hpd_gpio)) {
		err = PTR_ERR(p->hpd_gpio);

		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to get 'hpd' GPIO: %d\n", err);

		return err;
	}

	return 0;
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

	delay = p->desc->delay.hpd_reliable;
	if (p->no_hpd)
		delay = max(delay, p->desc->delay.hpd_absent);
	if (delay)
		msleep(delay);

	if (p->hpd_gpio) {
		if (p->desc->delay.hpd_absent)
			hpd_wait_us = p->desc->delay.hpd_absent * 1000UL;
		else
			hpd_wait_us = 2000000;

		err = readx_poll_timeout(gpiod_get_value_cansleep, p->hpd_gpio,
					 hpd_asserted, hpd_asserted,
					 1000, hpd_wait_us);
		if (hpd_asserted < 0)
			err = hpd_asserted;

		if (err) {
			if (err != -ETIMEDOUT)
				dev_err(dev,
					"error waiting for hpd GPIO: %d\n", err);
			goto error;
		}
	}

	p->prepared_time = ktime_get();

	return 0;

error:
	gpiod_set_value_cansleep(p->enable_gpio, 0);
	regulator_disable(p->supply);
	p->unprepared_time = ktime_get();

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
	struct panel_edp *p = to_panel_edp(panel);
	int ret;

	/* Preparing when already prepared is a no-op */
	if (p->prepared)
		return 0;

	ret = pm_runtime_get_sync(panel->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(panel->dev);
		return ret;
	}

	p->prepared = true;

	return 0;
}

static int panel_edp_enable(struct drm_panel *panel)
{
	struct panel_edp *p = to_panel_edp(panel);
	unsigned int delay;

	if (p->enabled)
		return 0;

	delay = p->desc->delay.enable;

	/*
	 * If there is a "prepare_to_enable" delay then that's supposed to be
	 * the delay from HPD going high until we can turn the backlight on.
	 * However, we can only count this if HPD is handled by the panel
	 * driver, not if it goes to a dedicated pin on the controller.
	 * If we aren't handling the HPD pin ourselves then the best we
	 * can do is assume that HPD went high immediately before we were
	 * called (and link training took zero time).
	 *
	 * NOTE: if we ever end up in this "if" statement then we're
	 * guaranteed that the panel_edp_wait() call below will do no delay.
	 * It already handles that case, though, so we don't need any special
	 * code for it.
	 */
	if (p->desc->delay.prepare_to_enable && !p->hpd_gpio && !p->no_hpd)
		delay = max(delay, p->desc->delay.prepare_to_enable);

	if (delay)
		msleep(delay);

	panel_edp_wait(p->prepared_time, p->desc->delay.prepare_to_enable);

	p->enabled = true;

	return 0;
}

static int panel_edp_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_edp *p = to_panel_edp(panel);
	int num = 0;

	/* probe EDID if a DDC bus is available */
	if (p->ddc) {
		pm_runtime_get_sync(panel->dev);

		if (!p->edid)
			p->edid = drm_get_edid(connector, p->ddc);

		if (p->edid)
			num += drm_add_edid_modes(connector, p->edid);

		pm_runtime_mark_last_busy(panel->dev);
		pm_runtime_put_autosuspend(panel->dev);
	}

	/*
	 * Add hard-coded panel modes. Don't call this if there are no timings
	 * and no modes (the generic edp-panel case) because it will clobber
	 * the display_info that was already set by drm_add_edid_modes().
	 */
	if (p->desc->num_timings || p->desc->num_modes)
		num += panel_edp_get_non_edid_modes(p, connector);
	else if (!num)
		dev_warn(p->base.dev, "No display modes\n");

	/* set up connector's "panel orientation" property */
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

static int detected_panel_show(struct seq_file *s, void *data)
{
	struct drm_panel *panel = s->private;
	struct panel_edp *p = to_panel_edp(panel);

	if (IS_ERR(p->detected_panel))
		seq_puts(s, "UNKNOWN\n");
	else if (!p->detected_panel)
		seq_puts(s, "HARDCODED\n");
	else
		seq_printf(s, "%s\n", p->detected_panel->name);

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

static const struct edp_panel_entry *find_edp_panel(u32 panel_id);

static int generic_edp_panel_probe(struct device *dev, struct panel_edp *panel)
{
	struct panel_desc *desc;
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
	desc->delay.hpd_reliable = absent_ms;

	/* Power the panel on so we can read the EDID */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Couldn't power on panel to read EDID: %d\n", ret);
		goto exit;
	}

	panel_id = drm_edid_get_panel_id(panel->ddc);
	if (!panel_id) {
		dev_err(dev, "Couldn't identify panel via EDID\n");
		ret = -EIO;
		goto exit;
	}
	drm_edid_decode_panel_id(panel_id, vend, &product_id);

	panel->detected_panel = find_edp_panel(panel_id);

	/*
	 * We're using non-optimized timings and want it really obvious that
	 * someone needs to add an entry to the table, so we'll do a WARN_ON
	 * splat.
	 */
	if (WARN_ON(!panel->detected_panel)) {
		dev_warn(dev,
			 "Unknown panel %s %#06x, using conservative timings\n",
			 vend, product_id);

		/*
		 * It's highly likely that the panel will work if we use very
		 * conservative timings, so let's do that. We already know that
		 * the HPD-related delays must have worked since we got this
		 * far, so we really just need the "unprepare" / "enable"
		 * delays. We don't need "prepare_to_enable" since that
		 * overlaps the "enable" delay anyway.
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
	} else {
		dev_info(dev, "Detected %s %s (%#06x)\n",
			 vend, panel->detected_panel->name, product_id);

		/* Update the delay; everything else comes from EDID */
		desc->delay = *panel->detected_panel->delay;
	}

	ret = 0;
exit:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
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

	panel->enabled = false;
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
	if (IS_ERR(panel->enable_gpio)) {
		err = PTR_ERR(panel->enable_gpio);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to request GPIO: %d\n", err);
		return err;
	}

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
		if (err)
			goto err_finished_pm_runtime;
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

static int panel_edp_remove(struct device *dev)
{
	struct panel_edp *panel = dev_get_drvdata(dev);

	drm_panel_remove(&panel->base);
	drm_panel_disable(&panel->base);
	drm_panel_unprepare(&panel->base);

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	if (panel->ddc && (!panel->aux || panel->ddc != &panel->aux->ddc))
		put_device(&panel->ddc->dev);

	kfree(panel->edid);
	panel->edid = NULL;

	return 0;
}

static void panel_edp_shutdown(struct device *dev)
{
	struct panel_edp *panel = dev_get_drvdata(dev);

	drm_panel_disable(&panel->base);
	drm_panel_unprepare(&panel->base);
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
	},
};

static const struct drm_display_mode auo_b116xw03_mode = {
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

static const struct panel_desc auo_b116xw03 = {
	.modes = &auo_b116xw03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
	.delay = {
		.enable = 400,
	},
};

static const struct drm_display_mode auo_b133han05_mode = {
	.clock = 142600,
	.hdisplay = 1920,
	.hsync_start = 1920 + 58,
	.hsync_end = 1920 + 58 + 42,
	.htotal = 1920 + 58 + 42 + 60,
	.vdisplay = 1080,
	.vsync_start = 1080 + 3,
	.vsync_end = 1080 + 3 + 5,
	.vtotal = 1080 + 3 + 5 + 54,
};

static const struct panel_desc auo_b133han05 = {
	.modes = &auo_b133han05_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 293,
		.height = 165,
	},
	.delay = {
		.hpd_reliable = 100,
		.enable = 20,
		.unprepare = 50,
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

static const struct drm_display_mode auo_b140han06_mode = {
	.clock = 141000,
	.hdisplay = 1920,
	.hsync_start = 1920 + 16,
	.hsync_end = 1920 + 16 + 16,
	.htotal = 1920 + 16 + 16 + 152,
	.vdisplay = 1080,
	.vsync_start = 1080 + 3,
	.vsync_end = 1080 + 3 + 14,
	.vtotal = 1080 + 3 + 14 + 19,
};

static const struct panel_desc auo_b140han06 = {
	.modes = &auo_b140han06_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 309,
		.height = 174,
	},
	.delay = {
		.hpd_reliable = 100,
		.enable = 20,
		.unprepare = 50,
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
		.prepare_to_enable = 80,
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

static const struct drm_display_mode ivo_m133nwf4_r0_mode = {
	.clock = 138778,
	.hdisplay = 1920,
	.hsync_start = 1920 + 24,
	.hsync_end = 1920 + 24 + 48,
	.htotal = 1920 + 24 + 48 + 88,
	.vdisplay = 1080,
	.vsync_start = 1080 + 3,
	.vsync_end = 1080 + 3 + 12,
	.vtotal = 1080 + 3 + 12 + 17,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc ivo_m133nwf4_r0 = {
	.modes = &ivo_m133nwf4_r0_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 294,
		.height = 165,
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

static const struct drm_display_mode sharp_lq140m1jw46_mode[] = {
	{
		.clock = 346500,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 1920 + 48 + 32 + 80,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1080 + 3 + 5 + 69,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	}, {
		.clock = 144370,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 1920 + 48 + 32 + 80,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1080 + 3 + 5 + 69,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	},
};

static const struct panel_desc sharp_lq140m1jw46 = {
	.modes = sharp_lq140m1jw46_mode,
	.num_modes = ARRAY_SIZE(sharp_lq140m1jw46_mode),
	.bpc = 8,
	.size = {
		.width = 309,
		.height = 174,
	},
	.delay = {
		.hpd_absent = 80,
		.enable = 50,
		.unprepare = 500,
	},
};

static const struct drm_display_mode starry_kr122ea0sra_mode = {
	.clock = 147000,
	.hdisplay = 1920,
	.hsync_start = 1920 + 16,
	.hsync_end = 1920 + 16 + 16,
	.htotal = 1920 + 16 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 15,
	.vsync_end = 1200 + 15 + 2,
	.vtotal = 1200 + 15 + 2 + 18,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc starry_kr122ea0sra = {
	.modes = &starry_kr122ea0sra_mode,
	.num_modes = 1,
	.size = {
		.width = 263,
		.height = 164,
	},
	.delay = {
		/* TODO: should be hpd-absent and no-hpd should be set? */
		.hpd_reliable = 10 + 200,
		.enable = 50,
		.unprepare = 10 + 500,
	},
};

static const struct of_device_id platform_of_match[] = {
	{
		/* Must be first */
		.compatible = "edp-panel",
	}, {
		.compatible = "auo,b101ean01",
		.data = &auo_b101ean01,
	}, {
		.compatible = "auo,b116xa01",
		.data = &auo_b116xak01,
	}, {
		.compatible = "auo,b116xw03",
		.data = &auo_b116xw03,
	}, {
		.compatible = "auo,b133han05",
		.data = &auo_b133han05,
	}, {
		.compatible = "auo,b133htn01",
		.data = &auo_b133htn01,
	}, {
		.compatible = "auo,b133xtn01",
		.data = &auo_b133xtn01,
	}, {
		.compatible = "auo,b140han06",
		.data = &auo_b140han06,
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
		.compatible = "ivo,m133nwf4-r0",
		.data = &ivo_m133nwf4_r0,
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
		.compatible = "sharp,lq140m1jw46",
		.data = &sharp_lq140m1jw46,
	}, {
		.compatible = "starry,kr122ea0sra",
		.data = &starry_kr122ea0sra,
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

static const struct panel_delay delay_200_500_e80_d50 = {
	.hpd_absent = 200,
	.unprepare = 500,
	.enable = 80,
	.disable = 50,
};

static const struct panel_delay delay_100_500_e200 = {
	.hpd_absent = 100,
	.unprepare = 500,
	.enable = 200,
};

#define EDP_PANEL_ENTRY(vend_chr_0, vend_chr_1, vend_chr_2, product_id, _delay, _name) \
{ \
	.name = _name, \
	.panel_id = drm_edid_encode_panel_id(vend_chr_0, vend_chr_1, vend_chr_2, \
					     product_id), \
	.delay = _delay \
}

/*
 * This table is used to figure out power sequencing delays for panels that
 * are detected by EDID. Entries here may point to entries in the
 * platform_of_match table (if a panel is listed in both places).
 *
 * Sort first by vendor, then by product ID.
 */
static const struct edp_panel_entry edp_panels[] = {
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x405c, &auo_b116xak01.delay, "B116XAK01"),
	EDP_PANEL_ENTRY('A', 'U', 'O', 0x615c, &delay_200_500_e50, "B116XAN06.1"),

	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0786, &delay_200_500_p2e80, "NV116WHM-T01"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x07d1, &boe_nv133fhm_n61.delay, "NV133FHM-N61"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x082d, &boe_nv133fhm_n61.delay, "NV133FHM-N62"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x098d, &boe_nv110wtm_n61.delay, "NV110WTM-N61"),
	EDP_PANEL_ENTRY('B', 'O', 'E', 0x0a5d, &delay_200_500_e50, "NV116WHM-N45"),

	EDP_PANEL_ENTRY('C', 'M', 'N', 0x114c, &innolux_n116bca_ea1.delay, "N116BCA-EA1"),

	EDP_PANEL_ENTRY('K', 'D', 'B', 0x0624, &kingdisplay_kd116n21_30nv_a010.delay, "116N21-30NV-A010"),
	EDP_PANEL_ENTRY('K', 'D', 'B', 0x1120, &delay_200_500_e80_d50, "116N29-30NK-C007"),

	EDP_PANEL_ENTRY('S', 'H', 'P', 0x154c, &delay_200_500_p2e100, "LQ116M1JW10"),

	EDP_PANEL_ENTRY('S', 'T', 'A', 0x0100, &delay_100_500_e200, "2081116HHD028001-51D"),

	{ /* sentinal */ }
};

static const struct edp_panel_entry *find_edp_panel(u32 panel_id)
{
	const struct edp_panel_entry *panel;

	if (!panel_id)
		return NULL;

	for (panel = edp_panels; panel->panel_id; panel++)
		if (panel->panel_id == panel_id)
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

static int panel_edp_platform_remove(struct platform_device *pdev)
{
	return panel_edp_remove(&pdev->dev);
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
	.remove = panel_edp_platform_remove,
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
