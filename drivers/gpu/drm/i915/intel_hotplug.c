/*
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/kernel.h>

#include <drm/drmP.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "intel_drv.h"

/**
 * DOC: Hotplug
 *
 * Simply put, hotplug occurs when a display is connected to or disconnected
 * from the system. However, there may be adapters and docking stations and
 * Display Port short pulses and MST devices involved, complicating matters.
 *
 * Hotplug in i915 is handled in many different levels of abstraction.
 *
 * The platform dependent interrupt handling code in i915_irq.c enables,
 * disables, and does preliminary handling of the interrupts. The interrupt
 * handlers gather the hotplug detect (HPD) information from relevant registers
 * into a platform independent mask of hotplug pins that have fired.
 *
 * The platform independent interrupt handler intel_hpd_irq_handler() in
 * intel_hotplug.c does hotplug irq storm detection and mitigation, and passes
 * further processing to appropriate bottom halves (Display Port specific and
 * regular hotplug).
 *
 * The Display Port work function i915_digport_work_func() calls into
 * intel_dp_hpd_pulse() via hooks, which handles DP short pulses and DP MST long
 * pulses, with failures and non-MST long pulses triggering regular hotplug
 * processing on the connector.
 *
 * The regular hotplug work function i915_hotplug_work_func() calls connector
 * detect hooks, and, if connector status changes, triggers sending of hotplug
 * uevent to userspace via drm_kms_helper_hotplug_event().
 *
 * Finally, the userspace is responsible for triggering a modeset upon receiving
 * the hotplug uevent, disabling or enabling the crtc as needed.
 *
 * The hotplug interrupt storm detection and mitigation code keeps track of the
 * number of interrupts per hotplug pin per a period of time, and if the number
 * of interrupts exceeds a certain threshold, the interrupt is disabled for a
 * while before being re-enabled. The intention is to mitigate issues raising
 * from broken hardware triggering massive amounts of interrupts and grinding
 * the system to a halt.
 *
 * Current implementation expects that hotplug interrupt storm will not be
 * seen when display port sink is connected, hence on platforms whose DP
 * callback is handled by i915_digport_work_func reenabling of hpd is not
 * performed (it was never expected to be disabled in the first place ;) )
 * this is specific to DP sinks handled by this routine and any other display
 * such as HDMI or DVI enabled on the same port will have proper logic since
 * it will use i915_hotplug_work_func where this logic is handled.
 */

bool intel_hpd_pin_to_port(enum hpd_pin pin, enum port *port)
{
	switch (pin) {
	case HPD_PORT_A:
		*port = PORT_A;
		return true;
	case HPD_PORT_B:
		*port = PORT_B;
		return true;
	case HPD_PORT_C:
		*port = PORT_C;
		return true;
	case HPD_PORT_D:
		*port = PORT_D;
		return true;
	case HPD_PORT_E:
		*port = PORT_E;
		return true;
	default:
		return false;	/* no hpd */
	}
}

#define HPD_STORM_DETECT_PERIOD		1000
#define HPD_STORM_REENABLE_DELAY	(2 * 60 * 1000)

/**
 * intel_hpd_irq_storm_detect - gather stats and detect HPD irq storm on a pin
 * @dev_priv: private driver data pointer
 * @pin: the pin to gather stats on
 *
 * Gather stats about HPD irqs from the specified @pin, and detect irq
 * storms. Only the pin specific stats and state are changed, the caller is
 * responsible for further action.
 *
 * The number of irqs that are allowed within @HPD_STORM_DETECT_PERIOD is
 * stored in @dev_priv->hotplug.hpd_storm_threshold which defaults to
 * @HPD_STORM_DEFAULT_THRESHOLD. If this threshold is exceeded, it's
 * considered an irq storm and the irq state is set to @HPD_MARK_DISABLED.
 *
 * The HPD threshold can be controlled through i915_hpd_storm_ctl in debugfs,
 * and should only be adjusted for automated hotplug testing.
 *
 * Return true if an irq storm was detected on @pin.
 */
static bool intel_hpd_irq_storm_detect(struct drm_i915_private *dev_priv,
				       enum hpd_pin pin)
{
	unsigned long start = dev_priv->hotplug.stats[pin].last_jiffies;
	unsigned long end = start + msecs_to_jiffies(HPD_STORM_DETECT_PERIOD);
	const int threshold = dev_priv->hotplug.hpd_storm_threshold;
	bool storm = false;

	if (!time_in_range(jiffies, start, end)) {
		dev_priv->hotplug.stats[pin].last_jiffies = jiffies;
		dev_priv->hotplug.stats[pin].count = 0;
		DRM_DEBUG_KMS("Received HPD interrupt on PIN %d - cnt: 0\n", pin);
	} else if (dev_priv->hotplug.stats[pin].count > threshold &&
		   threshold) {
		dev_priv->hotplug.stats[pin].state = HPD_MARK_DISABLED;
		DRM_DEBUG_KMS("HPD interrupt storm detected on PIN %d\n", pin);
		storm = true;
	} else {
		dev_priv->hotplug.stats[pin].count++;
		DRM_DEBUG_KMS("Received HPD interrupt on PIN %d - cnt: %d\n", pin,
			      dev_priv->hotplug.stats[pin].count);
	}

	return storm;
}

static void intel_hpd_irq_storm_disable(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_connector *intel_connector;
	struct intel_encoder *intel_encoder;
	struct drm_connector *connector;
	enum hpd_pin pin;
	bool hpd_disabled = false;

	lockdep_assert_held(&dev_priv->irq_lock);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		if (connector->polled != DRM_CONNECTOR_POLL_HPD)
			continue;

		intel_connector = to_intel_connector(connector);
		intel_encoder = intel_connector->encoder;
		if (!intel_encoder)
			continue;

		pin = intel_encoder->hpd_pin;
		if (pin == HPD_NONE ||
		    dev_priv->hotplug.stats[pin].state != HPD_MARK_DISABLED)
			continue;

		DRM_INFO("HPD interrupt storm detected on connector %s: "
			 "switching from hotplug detection to polling\n",
			 connector->name);

		dev_priv->hotplug.stats[pin].state = HPD_DISABLED;
		connector->polled = DRM_CONNECTOR_POLL_CONNECT
			| DRM_CONNECTOR_POLL_DISCONNECT;
		hpd_disabled = true;
	}

	/* Enable polling and queue hotplug re-enabling. */
	if (hpd_disabled) {
		drm_kms_helper_poll_enable(dev);
		mod_delayed_work(system_wq, &dev_priv->hotplug.reenable_work,
				 msecs_to_jiffies(HPD_STORM_REENABLE_DELAY));
	}
}

static void intel_hpd_irq_storm_reenable_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv),
			     hotplug.reenable_work.work);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int i;

	intel_runtime_pm_get(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	for_each_hpd_pin(i) {
		struct drm_connector *connector;

		if (dev_priv->hotplug.stats[i].state != HPD_DISABLED)
			continue;

		dev_priv->hotplug.stats[i].state = HPD_ENABLED;

		list_for_each_entry(connector, &mode_config->connector_list, head) {
			struct intel_connector *intel_connector = to_intel_connector(connector);

			if (intel_connector->encoder->hpd_pin == i) {
				if (connector->polled != intel_connector->polled)
					DRM_DEBUG_DRIVER("Reenabling HPD on connector %s\n",
							 connector->name);
				connector->polled = intel_connector->polled;
				if (!connector->polled)
					connector->polled = DRM_CONNECTOR_POLL_HPD;
			}
		}
	}
	if (dev_priv->display_irqs_enabled && dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_runtime_pm_put(dev_priv);
}

static bool intel_hpd_irq_event(struct drm_device *dev,
				struct drm_connector *connector)
{
	enum drm_connector_status old_status;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));
	old_status = connector->status;

	connector->status = connector->funcs->detect(connector, false);
	if (old_status == connector->status)
		return false;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %s to %s\n",
		      connector->base.id,
		      connector->name,
		      drm_get_connector_status_name(old_status),
		      drm_get_connector_status_name(connector->status));

	return true;
}

static void i915_digport_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, hotplug.dig_port_work);
	u32 long_port_mask, short_port_mask;
	struct intel_digital_port *intel_dig_port;
	int i;
	u32 old_bits = 0;

	spin_lock_irq(&dev_priv->irq_lock);
	long_port_mask = dev_priv->hotplug.long_port_mask;
	dev_priv->hotplug.long_port_mask = 0;
	short_port_mask = dev_priv->hotplug.short_port_mask;
	dev_priv->hotplug.short_port_mask = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	for (i = 0; i < I915_MAX_PORTS; i++) {
		bool valid = false;
		bool long_hpd = false;
		intel_dig_port = dev_priv->hotplug.irq_port[i];
		if (!intel_dig_port || !intel_dig_port->hpd_pulse)
			continue;

		if (long_port_mask & (1 << i))  {
			valid = true;
			long_hpd = true;
		} else if (short_port_mask & (1 << i))
			valid = true;

		if (valid) {
			enum irqreturn ret;

			ret = intel_dig_port->hpd_pulse(intel_dig_port, long_hpd);
			if (ret == IRQ_NONE) {
				/* fall back to old school hpd */
				old_bits |= (1 << intel_dig_port->base.hpd_pin);
			}
		}
	}

	if (old_bits) {
		spin_lock_irq(&dev_priv->irq_lock);
		dev_priv->hotplug.event_bits |= old_bits;
		spin_unlock_irq(&dev_priv->irq_lock);
		schedule_work(&dev_priv->hotplug.hotplug_work);
	}
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void i915_hotplug_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, hotplug.hotplug_work);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_connector *intel_connector;
	struct intel_encoder *intel_encoder;
	struct drm_connector *connector;
	bool changed = false;
	u32 hpd_event_bits;

	mutex_lock(&mode_config->mutex);
	DRM_DEBUG_KMS("running encoder hotplug functions\n");

	spin_lock_irq(&dev_priv->irq_lock);

	hpd_event_bits = dev_priv->hotplug.event_bits;
	dev_priv->hotplug.event_bits = 0;

	/* Disable hotplug on connectors that hit an irq storm. */
	intel_hpd_irq_storm_disable(dev_priv);

	spin_unlock_irq(&dev_priv->irq_lock);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		intel_connector = to_intel_connector(connector);
		if (!intel_connector->encoder)
			continue;
		intel_encoder = intel_connector->encoder;
		if (hpd_event_bits & (1 << intel_encoder->hpd_pin)) {
			DRM_DEBUG_KMS("Connector %s (pin %i) received hotplug event.\n",
				      connector->name, intel_encoder->hpd_pin);
			if (intel_encoder->hot_plug)
				intel_encoder->hot_plug(intel_encoder);
			if (intel_hpd_irq_event(dev, connector))
				changed = true;
		}
	}
	mutex_unlock(&mode_config->mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);
}


/**
 * intel_hpd_irq_handler - main hotplug irq handler
 * @dev_priv: drm_i915_private
 * @pin_mask: a mask of hpd pins that have triggered the irq
 * @long_mask: a mask of hpd pins that may be long hpd pulses
 *
 * This is the main hotplug irq handler for all platforms. The platform specific
 * irq handlers call the platform specific hotplug irq handlers, which read and
 * decode the appropriate registers into bitmasks about hpd pins that have
 * triggered (@pin_mask), and which of those pins may be long pulses
 * (@long_mask). The @long_mask is ignored if the port corresponding to the pin
 * is not a digital port.
 *
 * Here, we do hotplug irq storm detection and mitigation, and pass further
 * processing to appropriate bottom halves.
 */
void intel_hpd_irq_handler(struct drm_i915_private *dev_priv,
			   u32 pin_mask, u32 long_mask)
{
	int i;
	enum port port;
	bool storm_detected = false;
	bool queue_dig = false, queue_hp = false;
	bool is_dig_port;

	if (!pin_mask)
		return;

	spin_lock(&dev_priv->irq_lock);
	for_each_hpd_pin(i) {
		if (!(BIT(i) & pin_mask))
			continue;

		is_dig_port = intel_hpd_pin_to_port(i, &port) &&
			      dev_priv->hotplug.irq_port[port];

		if (is_dig_port) {
			bool long_hpd = long_mask & BIT(i);

			DRM_DEBUG_DRIVER("digital hpd port %c - %s\n", port_name(port),
					 long_hpd ? "long" : "short");
			/*
			 * For long HPD pulses we want to have the digital queue happen,
			 * but we still want HPD storm detection to function.
			 */
			queue_dig = true;
			if (long_hpd) {
				dev_priv->hotplug.long_port_mask |= (1 << port);
			} else {
				/* for short HPD just trigger the digital queue */
				dev_priv->hotplug.short_port_mask |= (1 << port);
				continue;
			}
		}

		if (dev_priv->hotplug.stats[i].state == HPD_DISABLED) {
			/*
			 * On GMCH platforms the interrupt mask bits only
			 * prevent irq generation, not the setting of the
			 * hotplug bits itself. So only WARN about unexpected
			 * interrupts on saner platforms.
			 */
			WARN_ONCE(!HAS_GMCH_DISPLAY(dev_priv),
				  "Received HPD interrupt on pin %d although disabled\n", i);
			continue;
		}

		if (dev_priv->hotplug.stats[i].state != HPD_ENABLED)
			continue;

		if (!is_dig_port) {
			dev_priv->hotplug.event_bits |= BIT(i);
			queue_hp = true;
		}

		if (intel_hpd_irq_storm_detect(dev_priv, i)) {
			dev_priv->hotplug.event_bits &= ~BIT(i);
			storm_detected = true;
		}
	}

	if (storm_detected && dev_priv->display_irqs_enabled)
		dev_priv->display.hpd_irq_setup(dev_priv);
	spin_unlock(&dev_priv->irq_lock);

	/*
	 * Our hotplug handler can grab modeset locks (by calling down into the
	 * fb helpers). Hence it must not be run on our own dev-priv->wq work
	 * queue for otherwise the flush_work in the pageflip code will
	 * deadlock.
	 */
	if (queue_dig)
		queue_work(dev_priv->hotplug.dp_wq, &dev_priv->hotplug.dig_port_work);
	if (queue_hp)
		schedule_work(&dev_priv->hotplug.hotplug_work);
}

/**
 * intel_hpd_init - initializes and enables hpd support
 * @dev_priv: i915 device instance
 *
 * This function enables the hotplug support. It requires that interrupts have
 * already been enabled with intel_irq_init_hw(). From this point on hotplug and
 * poll request can run concurrently to other code, so locking rules must be
 * obeyed.
 *
 * This is a separate step from interrupt enabling to simplify the locking rules
 * in the driver load and resume code.
 *
 * Also see: intel_hpd_poll_init(), which enables connector polling
 */
void intel_hpd_init(struct drm_i915_private *dev_priv)
{
	int i;

	for_each_hpd_pin(i) {
		dev_priv->hotplug.stats[i].count = 0;
		dev_priv->hotplug.stats[i].state = HPD_ENABLED;
	}

	WRITE_ONCE(dev_priv->hotplug.poll_enabled, false);
	schedule_work(&dev_priv->hotplug.poll_init_work);

	/*
	 * Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked checks happy.
	 */
	if (dev_priv->display_irqs_enabled && dev_priv->display.hpd_irq_setup) {
		spin_lock_irq(&dev_priv->irq_lock);
		if (dev_priv->display_irqs_enabled)
			dev_priv->display.hpd_irq_setup(dev_priv);
		spin_unlock_irq(&dev_priv->irq_lock);
	}
}

static void i915_hpd_poll_init_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     hotplug.poll_init_work);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	bool enabled;

	mutex_lock(&dev->mode_config.mutex);

	enabled = READ_ONCE(dev_priv->hotplug.poll_enabled);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct intel_connector *intel_connector =
			to_intel_connector(connector);
		connector->polled = intel_connector->polled;

		/* MST has a dynamic intel_connector->encoder and it's reprobing
		 * is all handled by the MST helpers. */
		if (intel_connector->mst_port)
			continue;

		if (!connector->polled && I915_HAS_HOTPLUG(dev_priv) &&
		    intel_connector->encoder->hpd_pin > HPD_NONE) {
			connector->polled = enabled ?
				DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT :
				DRM_CONNECTOR_POLL_HPD;
		}
	}

	if (enabled)
		drm_kms_helper_poll_enable(dev);

	mutex_unlock(&dev->mode_config.mutex);

	/*
	 * We might have missed any hotplugs that happened while we were
	 * in the middle of disabling polling
	 */
	if (!enabled)
		drm_helper_hpd_irq_event(dev);
}

/**
 * intel_hpd_poll_init - enables/disables polling for connectors with hpd
 * @dev_priv: i915 device instance
 *
 * This function enables polling for all connectors, regardless of whether or
 * not they support hotplug detection. Under certain conditions HPD may not be
 * functional. On most Intel GPUs, this happens when we enter runtime suspend.
 * On Valleyview and Cherryview systems, this also happens when we shut off all
 * of the powerwells.
 *
 * Since this function can get called in contexts where we're already holding
 * dev->mode_config.mutex, we do the actual hotplug enabling in a seperate
 * worker.
 *
 * Also see: intel_hpd_init(), which restores hpd handling.
 */
void intel_hpd_poll_init(struct drm_i915_private *dev_priv)
{
	WRITE_ONCE(dev_priv->hotplug.poll_enabled, true);

	/*
	 * We might already be holding dev->mode_config.mutex, so do this in a
	 * seperate worker
	 * As well, there's no issue if we race here since we always reschedule
	 * this worker anyway
	 */
	schedule_work(&dev_priv->hotplug.poll_init_work);
}

void intel_hpd_init_work(struct drm_i915_private *dev_priv)
{
	INIT_WORK(&dev_priv->hotplug.hotplug_work, i915_hotplug_work_func);
	INIT_WORK(&dev_priv->hotplug.dig_port_work, i915_digport_work_func);
	INIT_WORK(&dev_priv->hotplug.poll_init_work, i915_hpd_poll_init_work);
	INIT_DELAYED_WORK(&dev_priv->hotplug.reenable_work,
			  intel_hpd_irq_storm_reenable_work);
}

void intel_hpd_cancel_work(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);

	dev_priv->hotplug.long_port_mask = 0;
	dev_priv->hotplug.short_port_mask = 0;
	dev_priv->hotplug.event_bits = 0;

	spin_unlock_irq(&dev_priv->irq_lock);

	cancel_work_sync(&dev_priv->hotplug.dig_port_work);
	cancel_work_sync(&dev_priv->hotplug.hotplug_work);
	cancel_work_sync(&dev_priv->hotplug.poll_init_work);
	cancel_delayed_work_sync(&dev_priv->hotplug.reenable_work);
}

bool intel_hpd_disable(struct drm_i915_private *dev_priv, enum hpd_pin pin)
{
	bool ret = false;

	if (pin == HPD_NONE)
		return false;

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->hotplug.stats[pin].state == HPD_ENABLED) {
		dev_priv->hotplug.stats[pin].state = HPD_DISABLED;
		ret = true;
	}
	spin_unlock_irq(&dev_priv->irq_lock);

	return ret;
}

void intel_hpd_enable(struct drm_i915_private *dev_priv, enum hpd_pin pin)
{
	if (pin == HPD_NONE)
		return;

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->hotplug.stats[pin].state = HPD_ENABLED;
	spin_unlock_irq(&dev_priv->irq_lock);
}
