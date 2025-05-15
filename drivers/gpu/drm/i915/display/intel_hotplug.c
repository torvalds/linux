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

#include <linux/debugfs.h>
#include <linux/kernel.h>

#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_connector.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_hotplug_irq.h"

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

/**
 * intel_hpd_pin_default - return default pin associated with certain port.
 * @port: the hpd port to get associated pin
 *
 * It is only valid and used by digital port encoder.
 *
 * Return pin that is associatade with @port.
 */
enum hpd_pin intel_hpd_pin_default(enum port port)
{
	return HPD_PORT_A + port - PORT_A;
}

/* Threshold == 5 for long IRQs, 50 for short */
#define HPD_STORM_DEFAULT_THRESHOLD	50

#define HPD_STORM_DETECT_PERIOD		1000
#define HPD_STORM_REENABLE_DELAY	(2 * 60 * 1000)
#define HPD_RETRY_DELAY			1000

static enum hpd_pin
intel_connector_hpd_pin(struct intel_connector *connector)
{
	struct intel_encoder *encoder = intel_attached_encoder(connector);

	/*
	 * MST connectors get their encoder attached dynamically
	 * so need to make sure we have an encoder here. But since
	 * MST encoders have their hpd_pin set to HPD_NONE we don't
	 * have to special case them beyond that.
	 */
	return encoder ? encoder->hpd_pin : HPD_NONE;
}

/**
 * intel_hpd_irq_storm_detect - gather stats and detect HPD IRQ storm on a pin
 * @dev_priv: private driver data pointer
 * @pin: the pin to gather stats on
 * @long_hpd: whether the HPD IRQ was long or short
 *
 * Gather stats about HPD IRQs from the specified @pin, and detect IRQ
 * storms. Only the pin specific stats and state are changed, the caller is
 * responsible for further action.
 *
 * The number of IRQs that are allowed within @HPD_STORM_DETECT_PERIOD is
 * stored in @dev_priv->display.hotplug.hpd_storm_threshold which defaults to
 * @HPD_STORM_DEFAULT_THRESHOLD. Long IRQs count as +10 to this threshold, and
 * short IRQs count as +1. If this threshold is exceeded, it's considered an
 * IRQ storm and the IRQ state is set to @HPD_MARK_DISABLED.
 *
 * By default, most systems will only count long IRQs towards
 * &dev_priv->display.hotplug.hpd_storm_threshold. However, some older systems also
 * suffer from short IRQ storms and must also track these. Because short IRQ
 * storms are naturally caused by sideband interactions with DP MST devices,
 * short IRQ detection is only enabled for systems without DP MST support.
 * Systems which are new enough to support DP MST are far less likely to
 * suffer from IRQ storms at all, so this is fine.
 *
 * The HPD threshold can be controlled through i915_hpd_storm_ctl in debugfs,
 * and should only be adjusted for automated hotplug testing.
 *
 * Return true if an IRQ storm was detected on @pin.
 */
static bool intel_hpd_irq_storm_detect(struct drm_i915_private *dev_priv,
				       enum hpd_pin pin, bool long_hpd)
{
	struct intel_hotplug *hpd = &dev_priv->display.hotplug;
	unsigned long start = hpd->stats[pin].last_jiffies;
	unsigned long end = start + msecs_to_jiffies(HPD_STORM_DETECT_PERIOD);
	const int increment = long_hpd ? 10 : 1;
	const int threshold = hpd->hpd_storm_threshold;
	bool storm = false;

	if (!threshold ||
	    (!long_hpd && !dev_priv->display.hotplug.hpd_short_storm_enabled))
		return false;

	if (!time_in_range(jiffies, start, end)) {
		hpd->stats[pin].last_jiffies = jiffies;
		hpd->stats[pin].count = 0;
	}

	hpd->stats[pin].count += increment;
	if (hpd->stats[pin].count > threshold) {
		hpd->stats[pin].state = HPD_MARK_DISABLED;
		drm_dbg_kms(&dev_priv->drm,
			    "HPD interrupt storm detected on PIN %d\n", pin);
		storm = true;
	} else {
		drm_dbg_kms(&dev_priv->drm,
			    "Received HPD interrupt on PIN %d - cnt: %d\n",
			      pin,
			      hpd->stats[pin].count);
	}

	return storm;
}

static bool detection_work_enabled(struct drm_i915_private *i915)
{
	lockdep_assert_held(&i915->irq_lock);

	return i915->display.hotplug.detection_work_enabled;
}

static bool
mod_delayed_detection_work(struct drm_i915_private *i915, struct delayed_work *work, int delay)
{
	lockdep_assert_held(&i915->irq_lock);

	if (!detection_work_enabled(i915))
		return false;

	return mod_delayed_work(i915->unordered_wq, work, delay);
}

static bool
queue_delayed_detection_work(struct drm_i915_private *i915, struct delayed_work *work, int delay)
{
	lockdep_assert_held(&i915->irq_lock);

	if (!detection_work_enabled(i915))
		return false;

	return queue_delayed_work(i915->unordered_wq, work, delay);
}

static bool
queue_detection_work(struct drm_i915_private *i915, struct work_struct *work)
{
	lockdep_assert_held(&i915->irq_lock);

	if (!detection_work_enabled(i915))
		return false;

	return queue_work(i915->unordered_wq, work);
}

static void
intel_hpd_irq_storm_switch_to_polling(struct drm_i915_private *dev_priv)
{
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	bool hpd_disabled = false;

	lockdep_assert_held(&dev_priv->irq_lock);

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		enum hpd_pin pin;

		if (connector->base.polled != DRM_CONNECTOR_POLL_HPD)
			continue;

		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE ||
		    dev_priv->display.hotplug.stats[pin].state != HPD_MARK_DISABLED)
			continue;

		drm_info(&dev_priv->drm,
			 "HPD interrupt storm detected on connector %s: "
			 "switching from hotplug detection to polling\n",
			 connector->base.name);

		dev_priv->display.hotplug.stats[pin].state = HPD_DISABLED;
		connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;
		hpd_disabled = true;
	}
	drm_connector_list_iter_end(&conn_iter);

	/* Enable polling and queue hotplug re-enabling. */
	if (hpd_disabled) {
		drm_kms_helper_poll_reschedule(&dev_priv->drm);
		mod_delayed_detection_work(dev_priv,
					   &dev_priv->display.hotplug.reenable_work,
					   msecs_to_jiffies(HPD_STORM_REENABLE_DELAY));
	}
}

static void intel_hpd_irq_storm_reenable_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv),
			     display.hotplug.reenable_work.work);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	intel_wakeref_t wakeref;
	enum hpd_pin pin;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	spin_lock_irq(&dev_priv->irq_lock);

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE ||
		    dev_priv->display.hotplug.stats[pin].state != HPD_DISABLED)
			continue;

		if (connector->base.polled != connector->polled)
			drm_dbg(&dev_priv->drm,
				"Reenabling HPD on connector %s\n",
				connector->base.name);
		connector->base.polled = connector->polled;
	}
	drm_connector_list_iter_end(&conn_iter);

	for_each_hpd_pin(pin) {
		if (dev_priv->display.hotplug.stats[pin].state == HPD_DISABLED)
			dev_priv->display.hotplug.stats[pin].state = HPD_ENABLED;
	}

	intel_hpd_irq_setup(dev_priv);

	spin_unlock_irq(&dev_priv->irq_lock);

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
}

static enum intel_hotplug_state
intel_hotplug_detect_connector(struct intel_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	enum drm_connector_status old_status;
	u64 old_epoch_counter;
	int status;
	bool ret = false;

	drm_WARN_ON(dev, !mutex_is_locked(&dev->mode_config.mutex));
	old_status = connector->base.status;
	old_epoch_counter = connector->base.epoch_counter;

	status = drm_helper_probe_detect(&connector->base, NULL, false);
	if (!connector->base.force)
		connector->base.status = status;

	if (old_epoch_counter != connector->base.epoch_counter)
		ret = true;

	if (ret) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] status updated from %s to %s (epoch counter %llu->%llu)\n",
			    connector->base.base.id,
			    connector->base.name,
			    drm_get_connector_status_name(old_status),
			    drm_get_connector_status_name(connector->base.status),
			    old_epoch_counter,
			    connector->base.epoch_counter);
		return INTEL_HOTPLUG_CHANGED;
	}
	return INTEL_HOTPLUG_UNCHANGED;
}

enum intel_hotplug_state
intel_encoder_hotplug(struct intel_encoder *encoder,
		      struct intel_connector *connector)
{
	return intel_hotplug_detect_connector(connector);
}

static bool intel_encoder_has_hpd_pulse(struct intel_encoder *encoder)
{
	return intel_encoder_is_dig_port(encoder) &&
		enc_to_dig_port(encoder)->hpd_pulse != NULL;
}

static void i915_digport_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, display.hotplug.dig_port_work);
	u32 long_port_mask, short_port_mask;
	struct intel_encoder *encoder;
	u32 old_bits = 0;

	spin_lock_irq(&dev_priv->irq_lock);
	long_port_mask = dev_priv->display.hotplug.long_port_mask;
	dev_priv->display.hotplug.long_port_mask = 0;
	short_port_mask = dev_priv->display.hotplug.short_port_mask;
	dev_priv->display.hotplug.short_port_mask = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_digital_port *dig_port;
		enum port port = encoder->port;
		bool long_hpd, short_hpd;
		enum irqreturn ret;

		if (!intel_encoder_has_hpd_pulse(encoder))
			continue;

		long_hpd = long_port_mask & BIT(port);
		short_hpd = short_port_mask & BIT(port);

		if (!long_hpd && !short_hpd)
			continue;

		dig_port = enc_to_dig_port(encoder);

		ret = dig_port->hpd_pulse(dig_port, long_hpd);
		if (ret == IRQ_NONE) {
			/* fall back to old school hpd */
			old_bits |= BIT(encoder->hpd_pin);
		}
	}

	if (old_bits) {
		spin_lock_irq(&dev_priv->irq_lock);
		dev_priv->display.hotplug.event_bits |= old_bits;
		queue_delayed_detection_work(dev_priv,
					     &dev_priv->display.hotplug.hotplug_work, 0);
		spin_unlock_irq(&dev_priv->irq_lock);
	}
}

/**
 * intel_hpd_trigger_irq - trigger an hpd irq event for a port
 * @dig_port: digital port
 *
 * Trigger an HPD interrupt event for the given port, emulating a short pulse
 * generated by the sink, and schedule the dig port work to handle it.
 */
void intel_hpd_trigger_irq(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	spin_lock_irq(&i915->irq_lock);
	i915->display.hotplug.short_port_mask |= BIT(dig_port->base.port);
	spin_unlock_irq(&i915->irq_lock);

	queue_work(i915->display.hotplug.dp_wq, &i915->display.hotplug.dig_port_work);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void i915_hotplug_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     display.hotplug.hotplug_work.work);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	u32 changed = 0, retry = 0;
	u32 hpd_event_bits;
	u32 hpd_retry_bits;
	struct drm_connector *first_changed_connector = NULL;
	int changed_connectors = 0;

	mutex_lock(&dev_priv->drm.mode_config.mutex);
	drm_dbg_kms(&dev_priv->drm, "running encoder hotplug functions\n");

	spin_lock_irq(&dev_priv->irq_lock);

	hpd_event_bits = dev_priv->display.hotplug.event_bits;
	dev_priv->display.hotplug.event_bits = 0;
	hpd_retry_bits = dev_priv->display.hotplug.retry_bits;
	dev_priv->display.hotplug.retry_bits = 0;

	/* Enable polling for connectors which had HPD IRQ storms */
	intel_hpd_irq_storm_switch_to_polling(dev_priv);

	spin_unlock_irq(&dev_priv->irq_lock);

	/* Skip calling encode hotplug handlers if ignore long HPD set*/
	if (dev_priv->display.hotplug.ignore_long_hpd) {
		drm_dbg_kms(&dev_priv->drm, "Ignore HPD flag on - skip encoder hotplug handlers\n");
		mutex_unlock(&dev_priv->drm.mode_config.mutex);
		return;
	}

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		enum hpd_pin pin;
		u32 hpd_bit;

		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE)
			continue;

		hpd_bit = BIT(pin);
		if ((hpd_event_bits | hpd_retry_bits) & hpd_bit) {
			struct intel_encoder *encoder =
				intel_attached_encoder(connector);

			if (hpd_event_bits & hpd_bit)
				connector->hotplug_retries = 0;
			else
				connector->hotplug_retries++;

			drm_dbg_kms(&dev_priv->drm,
				    "Connector %s (pin %i) received hotplug event. (retry %d)\n",
				    connector->base.name, pin,
				    connector->hotplug_retries);

			switch (encoder->hotplug(encoder, connector)) {
			case INTEL_HOTPLUG_UNCHANGED:
				break;
			case INTEL_HOTPLUG_CHANGED:
				changed |= hpd_bit;
				changed_connectors++;
				if (!first_changed_connector) {
					drm_connector_get(&connector->base);
					first_changed_connector = &connector->base;
				}
				break;
			case INTEL_HOTPLUG_RETRY:
				retry |= hpd_bit;
				break;
			}
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev_priv->drm.mode_config.mutex);

	if (changed_connectors == 1)
		drm_kms_helper_connector_hotplug_event(first_changed_connector);
	else if (changed_connectors > 0)
		drm_kms_helper_hotplug_event(&dev_priv->drm);

	if (first_changed_connector)
		drm_connector_put(first_changed_connector);

	/* Remove shared HPD pins that have changed */
	retry &= ~changed;
	if (retry) {
		spin_lock_irq(&dev_priv->irq_lock);
		dev_priv->display.hotplug.retry_bits |= retry;

		mod_delayed_detection_work(dev_priv,
					   &dev_priv->display.hotplug.hotplug_work,
					   msecs_to_jiffies(HPD_RETRY_DELAY));
		spin_unlock_irq(&dev_priv->irq_lock);
	}
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
	struct intel_encoder *encoder;
	bool storm_detected = false;
	bool queue_dig = false, queue_hp = false;
	u32 long_hpd_pulse_mask = 0;
	u32 short_hpd_pulse_mask = 0;
	enum hpd_pin pin;

	if (!pin_mask)
		return;

	spin_lock(&dev_priv->irq_lock);

	/*
	 * Determine whether ->hpd_pulse() exists for each pin, and
	 * whether we have a short or a long pulse. This is needed
	 * as each pin may have up to two encoders (HDMI and DP) and
	 * only the one of them (DP) will have ->hpd_pulse().
	 */
	for_each_intel_encoder(&dev_priv->drm, encoder) {
		enum port port = encoder->port;
		bool long_hpd;

		pin = encoder->hpd_pin;
		if (!(BIT(pin) & pin_mask))
			continue;

		if (!intel_encoder_has_hpd_pulse(encoder))
			continue;

		long_hpd = long_mask & BIT(pin);

		drm_dbg(&dev_priv->drm,
			"digital hpd on [ENCODER:%d:%s] - %s\n",
			encoder->base.base.id, encoder->base.name,
			long_hpd ? "long" : "short");
		queue_dig = true;

		if (long_hpd) {
			long_hpd_pulse_mask |= BIT(pin);
			dev_priv->display.hotplug.long_port_mask |= BIT(port);
		} else {
			short_hpd_pulse_mask |= BIT(pin);
			dev_priv->display.hotplug.short_port_mask |= BIT(port);
		}
	}

	/* Now process each pin just once */
	for_each_hpd_pin(pin) {
		bool long_hpd;

		if (!(BIT(pin) & pin_mask))
			continue;

		if (dev_priv->display.hotplug.stats[pin].state == HPD_DISABLED) {
			/*
			 * On GMCH platforms the interrupt mask bits only
			 * prevent irq generation, not the setting of the
			 * hotplug bits itself. So only WARN about unexpected
			 * interrupts on saner platforms.
			 */
			drm_WARN_ONCE(&dev_priv->drm, !HAS_GMCH(dev_priv),
				      "Received HPD interrupt on pin %d although disabled\n",
				      pin);
			continue;
		}

		if (dev_priv->display.hotplug.stats[pin].state != HPD_ENABLED)
			continue;

		/*
		 * Delegate to ->hpd_pulse() if one of the encoders for this
		 * pin has it, otherwise let the hotplug_work deal with this
		 * pin directly.
		 */
		if (((short_hpd_pulse_mask | long_hpd_pulse_mask) & BIT(pin))) {
			long_hpd = long_hpd_pulse_mask & BIT(pin);
		} else {
			dev_priv->display.hotplug.event_bits |= BIT(pin);
			long_hpd = true;
			queue_hp = true;
		}

		if (intel_hpd_irq_storm_detect(dev_priv, pin, long_hpd)) {
			dev_priv->display.hotplug.event_bits &= ~BIT(pin);
			storm_detected = true;
			queue_hp = true;
		}
	}

	/*
	 * Disable any IRQs that storms were detected on. Polling enablement
	 * happens later in our hotplug work.
	 */
	if (storm_detected)
		intel_hpd_irq_setup(dev_priv);

	/*
	 * Our hotplug handler can grab modeset locks (by calling down into the
	 * fb helpers). Hence it must not be run on our own dev-priv->wq work
	 * queue for otherwise the flush_work in the pageflip code will
	 * deadlock.
	 */
	if (queue_dig)
		queue_work(dev_priv->display.hotplug.dp_wq, &dev_priv->display.hotplug.dig_port_work);
	if (queue_hp)
		queue_delayed_detection_work(dev_priv,
					     &dev_priv->display.hotplug.hotplug_work, 0);

	spin_unlock(&dev_priv->irq_lock);
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
 * Also see: intel_hpd_poll_enable() and intel_hpd_poll_disable().
 */
void intel_hpd_init(struct drm_i915_private *dev_priv)
{
	int i;

	if (!HAS_DISPLAY(dev_priv))
		return;

	for_each_hpd_pin(i) {
		dev_priv->display.hotplug.stats[i].count = 0;
		dev_priv->display.hotplug.stats[i].state = HPD_ENABLED;
	}

	/*
	 * Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked checks happy.
	 */
	spin_lock_irq(&dev_priv->irq_lock);
	intel_hpd_irq_setup(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void i915_hpd_poll_detect_connectors(struct drm_i915_private *i915)
{
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	struct intel_connector *first_changed_connector = NULL;
	int changed = 0;

	mutex_lock(&i915->drm.mode_config.mutex);

	if (!i915->drm.mode_config.poll_enabled)
		goto out;

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (!(connector->base.polled & DRM_CONNECTOR_POLL_HPD))
			continue;

		if (intel_hotplug_detect_connector(connector) != INTEL_HOTPLUG_CHANGED)
			continue;

		changed++;

		if (changed == 1) {
			drm_connector_get(&connector->base);
			first_changed_connector = connector;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

out:
	mutex_unlock(&i915->drm.mode_config.mutex);

	if (!changed)
		return;

	if (changed == 1)
		drm_kms_helper_connector_hotplug_event(&first_changed_connector->base);
	else
		drm_kms_helper_hotplug_event(&i915->drm);

	drm_connector_put(&first_changed_connector->base);
}

static void i915_hpd_poll_init_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     display.hotplug.poll_init_work);
	struct intel_display *display = &dev_priv->display;
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	intel_wakeref_t wakeref;
	bool enabled;

	mutex_lock(&dev_priv->drm.mode_config.mutex);

	enabled = READ_ONCE(dev_priv->display.hotplug.poll_enabled);
	/*
	 * Prevent taking a power reference from this sequence of
	 * i915_hpd_poll_init_work() -> drm_helper_hpd_irq_event() ->
	 * connector detect which would requeue i915_hpd_poll_init_work()
	 * and so risk an endless loop of this same sequence.
	 */
	if (!enabled) {
		wakeref = intel_display_power_get(display,
						  POWER_DOMAIN_DISPLAY_CORE);
		drm_WARN_ON(&dev_priv->drm,
			    READ_ONCE(dev_priv->display.hotplug.poll_enabled));
		cancel_work(&dev_priv->display.hotplug.poll_init_work);
	}

	spin_lock_irq(&dev_priv->irq_lock);

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		enum hpd_pin pin;

		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE)
			continue;

		if (dev_priv->display.hotplug.stats[pin].state == HPD_DISABLED)
			continue;

		connector->base.polled = connector->polled;

		if (enabled && connector->base.polled == DRM_CONNECTOR_POLL_HPD)
			connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;
	}
	drm_connector_list_iter_end(&conn_iter);

	spin_unlock_irq(&dev_priv->irq_lock);

	if (enabled)
		drm_kms_helper_poll_reschedule(&dev_priv->drm);

	mutex_unlock(&dev_priv->drm.mode_config.mutex);

	/*
	 * We might have missed any hotplugs that happened while we were
	 * in the middle of disabling polling
	 */
	if (!enabled) {
		i915_hpd_poll_detect_connectors(dev_priv);

		intel_display_power_put(display,
					POWER_DOMAIN_DISPLAY_CORE,
					wakeref);
	}
}

/**
 * intel_hpd_poll_enable - enable polling for connectors with hpd
 * @dev_priv: i915 device instance
 *
 * This function enables polling for all connectors which support HPD.
 * Under certain conditions HPD may not be functional. On most Intel GPUs,
 * this happens when we enter runtime suspend.
 * On Valleyview and Cherryview systems, this also happens when we shut off all
 * of the powerwells.
 *
 * Since this function can get called in contexts where we're already holding
 * dev->mode_config.mutex, we do the actual hotplug enabling in a separate
 * worker.
 *
 * Also see: intel_hpd_init() and intel_hpd_poll_disable().
 */
void intel_hpd_poll_enable(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = &dev_priv->display;

	if (!HAS_DISPLAY(dev_priv) ||
	    !intel_display_device_enabled(display))
		return;

	WRITE_ONCE(dev_priv->display.hotplug.poll_enabled, true);

	/*
	 * We might already be holding dev->mode_config.mutex, so do this in a
	 * separate worker
	 * As well, there's no issue if we race here since we always reschedule
	 * this worker anyway
	 */
	spin_lock_irq(&dev_priv->irq_lock);
	queue_detection_work(dev_priv,
			     &dev_priv->display.hotplug.poll_init_work);
	spin_unlock_irq(&dev_priv->irq_lock);
}

/**
 * intel_hpd_poll_disable - disable polling for connectors with hpd
 * @dev_priv: i915 device instance
 *
 * This function disables polling for all connectors which support HPD.
 * Under certain conditions HPD may not be functional. On most Intel GPUs,
 * this happens when we enter runtime suspend.
 * On Valleyview and Cherryview systems, this also happens when we shut off all
 * of the powerwells.
 *
 * Since this function can get called in contexts where we're already holding
 * dev->mode_config.mutex, we do the actual hotplug enabling in a separate
 * worker.
 *
 * Also used during driver init to initialize connector->polled
 * appropriately for all connectors.
 *
 * Also see: intel_hpd_init() and intel_hpd_poll_enable().
 */
void intel_hpd_poll_disable(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	WRITE_ONCE(dev_priv->display.hotplug.poll_enabled, false);

	spin_lock_irq(&dev_priv->irq_lock);
	queue_detection_work(dev_priv,
			     &dev_priv->display.hotplug.poll_init_work);
	spin_unlock_irq(&dev_priv->irq_lock);
}

void intel_hpd_poll_fini(struct drm_i915_private *i915)
{
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	/* Kill all the work that may have been queued by hpd. */
	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		intel_connector_cancel_modeset_retry_work(connector);
		intel_hdcp_cancel_works(connector);
	}
	drm_connector_list_iter_end(&conn_iter);
}

void intel_hpd_init_early(struct drm_i915_private *i915)
{
	INIT_DELAYED_WORK(&i915->display.hotplug.hotplug_work,
			  i915_hotplug_work_func);
	INIT_WORK(&i915->display.hotplug.dig_port_work, i915_digport_work_func);
	INIT_WORK(&i915->display.hotplug.poll_init_work, i915_hpd_poll_init_work);
	INIT_DELAYED_WORK(&i915->display.hotplug.reenable_work,
			  intel_hpd_irq_storm_reenable_work);

	i915->display.hotplug.hpd_storm_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	/* If we have MST support, we want to avoid doing short HPD IRQ storm
	 * detection, as short HPD storms will occur as a natural part of
	 * sideband messaging with MST.
	 * On older platforms however, IRQ storms can occur with both long and
	 * short pulses, as seen on some G4x systems.
	 */
	i915->display.hotplug.hpd_short_storm_enabled = !HAS_DP_MST(i915);
}

static bool cancel_all_detection_work(struct drm_i915_private *i915)
{
	bool was_pending = false;

	if (cancel_delayed_work_sync(&i915->display.hotplug.hotplug_work))
		was_pending = true;
	if (cancel_work_sync(&i915->display.hotplug.poll_init_work))
		was_pending = true;
	if (cancel_delayed_work_sync(&i915->display.hotplug.reenable_work))
		was_pending = true;

	return was_pending;
}

void intel_hpd_cancel_work(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	spin_lock_irq(&dev_priv->irq_lock);

	dev_priv->display.hotplug.long_port_mask = 0;
	dev_priv->display.hotplug.short_port_mask = 0;
	dev_priv->display.hotplug.event_bits = 0;
	dev_priv->display.hotplug.retry_bits = 0;

	spin_unlock_irq(&dev_priv->irq_lock);

	cancel_work_sync(&dev_priv->display.hotplug.dig_port_work);

	/*
	 * All other work triggered by hotplug events should be canceled by
	 * now.
	 */
	if (cancel_all_detection_work(dev_priv))
		drm_dbg_kms(&dev_priv->drm, "Hotplug detection work still active\n");
}

bool intel_hpd_disable(struct drm_i915_private *dev_priv, enum hpd_pin pin)
{
	bool ret = false;

	if (pin == HPD_NONE)
		return false;

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display.hotplug.stats[pin].state == HPD_ENABLED) {
		dev_priv->display.hotplug.stats[pin].state = HPD_DISABLED;
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
	dev_priv->display.hotplug.stats[pin].state = HPD_ENABLED;
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void queue_work_for_missed_irqs(struct drm_i915_private *i915)
{
	bool queue_work = false;
	enum hpd_pin pin;

	lockdep_assert_held(&i915->irq_lock);

	if (i915->display.hotplug.event_bits ||
	    i915->display.hotplug.retry_bits)
		queue_work = true;

	for_each_hpd_pin(pin) {
		switch (i915->display.hotplug.stats[pin].state) {
		case HPD_MARK_DISABLED:
			queue_work = true;
			break;
		case HPD_ENABLED:
			break;
		default:
			MISSING_CASE(i915->display.hotplug.stats[pin].state);
		}
	}

	if (queue_work)
		queue_delayed_detection_work(i915, &i915->display.hotplug.hotplug_work, 0);
}

void intel_hpd_enable_detection_work(struct drm_i915_private *i915)
{
	spin_lock_irq(&i915->irq_lock);
	i915->display.hotplug.detection_work_enabled = true;
	queue_work_for_missed_irqs(i915);
	spin_unlock_irq(&i915->irq_lock);
}

void intel_hpd_disable_detection_work(struct drm_i915_private *i915)
{
	spin_lock_irq(&i915->irq_lock);
	i915->display.hotplug.detection_work_enabled = false;
	spin_unlock_irq(&i915->irq_lock);

	cancel_all_detection_work(i915);
}

bool intel_hpd_schedule_detection(struct drm_i915_private *i915)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&i915->irq_lock, flags);
	ret = queue_delayed_detection_work(i915, &i915->display.hotplug.hotplug_work, 0);
	spin_unlock_irqrestore(&i915->irq_lock, flags);

	return ret;
}

static int i915_hpd_storm_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct intel_hotplug *hotplug = &dev_priv->display.hotplug;

	/* Synchronize with everything first in case there's been an HPD
	 * storm, but we haven't finished handling it in the kernel yet
	 */
	intel_synchronize_irq(dev_priv);
	flush_work(&dev_priv->display.hotplug.dig_port_work);
	flush_delayed_work(&dev_priv->display.hotplug.hotplug_work);

	seq_printf(m, "Threshold: %d\n", hotplug->hpd_storm_threshold);
	seq_printf(m, "Detected: %s\n",
		   str_yes_no(delayed_work_pending(&hotplug->reenable_work)));

	return 0;
}

static ssize_t i915_hpd_storm_ctl_write(struct file *file,
					const char __user *ubuf, size_t len,
					loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct intel_hotplug *hotplug = &dev_priv->display.hotplug;
	unsigned int new_threshold;
	int i;
	char *newline;
	char tmp[16];

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	/* Strip newline, if any */
	newline = strchr(tmp, '\n');
	if (newline)
		*newline = '\0';

	if (strcmp(tmp, "reset") == 0)
		new_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	else if (kstrtouint(tmp, 10, &new_threshold) != 0)
		return -EINVAL;

	if (new_threshold > 0)
		drm_dbg_kms(&dev_priv->drm,
			    "Setting HPD storm detection threshold to %d\n",
			    new_threshold);
	else
		drm_dbg_kms(&dev_priv->drm, "Disabling HPD storm detection\n");

	spin_lock_irq(&dev_priv->irq_lock);
	hotplug->hpd_storm_threshold = new_threshold;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&dev_priv->display.hotplug.reenable_work);

	return len;
}

static int i915_hpd_storm_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_hpd_storm_ctl_show, inode->i_private);
}

static const struct file_operations i915_hpd_storm_ctl_fops = {
	.owner = THIS_MODULE,
	.open = i915_hpd_storm_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_hpd_storm_ctl_write
};

static int i915_hpd_short_storm_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;

	seq_printf(m, "Enabled: %s\n",
		   str_yes_no(dev_priv->display.hotplug.hpd_short_storm_enabled));

	return 0;
}

static int
i915_hpd_short_storm_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_hpd_short_storm_ctl_show,
			   inode->i_private);
}

static ssize_t i915_hpd_short_storm_ctl_write(struct file *file,
					      const char __user *ubuf,
					      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct intel_hotplug *hotplug = &dev_priv->display.hotplug;
	char *newline;
	char tmp[16];
	int i;
	bool new_state;

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	/* Strip newline, if any */
	newline = strchr(tmp, '\n');
	if (newline)
		*newline = '\0';

	/* Reset to the "default" state for this system */
	if (strcmp(tmp, "reset") == 0)
		new_state = !HAS_DP_MST(dev_priv);
	else if (kstrtobool(tmp, &new_state) != 0)
		return -EINVAL;

	drm_dbg_kms(&dev_priv->drm, "%sabling HPD short storm detection\n",
		    new_state ? "En" : "Dis");

	spin_lock_irq(&dev_priv->irq_lock);
	hotplug->hpd_short_storm_enabled = new_state;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&dev_priv->display.hotplug.reenable_work);

	return len;
}

static const struct file_operations i915_hpd_short_storm_ctl_fops = {
	.owner = THIS_MODULE,
	.open = i915_hpd_short_storm_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_hpd_short_storm_ctl_write,
};

void intel_hpd_debugfs_register(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;

	debugfs_create_file("i915_hpd_storm_ctl", 0644, minor->debugfs_root,
			    i915, &i915_hpd_storm_ctl_fops);
	debugfs_create_file("i915_hpd_short_storm_ctl", 0644, minor->debugfs_root,
			    i915, &i915_hpd_short_storm_ctl_fops);
	debugfs_create_bool("i915_ignore_long_hpd", 0644, minor->debugfs_root,
			    &i915->display.hotplug.ignore_long_hpd);
}
