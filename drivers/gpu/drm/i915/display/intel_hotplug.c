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
#include "i915_utils.h"
#include "intel_connector.h"
#include "intel_display_power.h"
#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dp.h"
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
 * @display: display device
 * @pin: the pin to gather stats on
 * @long_hpd: whether the HPD IRQ was long or short
 *
 * Gather stats about HPD IRQs from the specified @pin, and detect IRQ
 * storms. Only the pin specific stats and state are changed, the caller is
 * responsible for further action.
 *
 * The number of IRQs that are allowed within @HPD_STORM_DETECT_PERIOD is
 * stored in @display->hotplug.hpd_storm_threshold which defaults to
 * @HPD_STORM_DEFAULT_THRESHOLD. Long IRQs count as +10 to this threshold, and
 * short IRQs count as +1. If this threshold is exceeded, it's considered an
 * IRQ storm and the IRQ state is set to @HPD_MARK_DISABLED.
 *
 * By default, most systems will only count long IRQs towards
 * &display->hotplug.hpd_storm_threshold. However, some older systems also
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
static bool intel_hpd_irq_storm_detect(struct intel_display *display,
				       enum hpd_pin pin, bool long_hpd)
{
	struct intel_hotplug *hpd = &display->hotplug;
	unsigned long start = hpd->stats[pin].last_jiffies;
	unsigned long end = start + msecs_to_jiffies(HPD_STORM_DETECT_PERIOD);
	const int increment = long_hpd ? 10 : 1;
	const int threshold = hpd->hpd_storm_threshold;
	bool storm = false;

	if (!threshold ||
	    (!long_hpd && !display->hotplug.hpd_short_storm_enabled))
		return false;

	if (!time_in_range(jiffies, start, end)) {
		hpd->stats[pin].last_jiffies = jiffies;
		hpd->stats[pin].count = 0;
	}

	hpd->stats[pin].count += increment;
	if (hpd->stats[pin].count > threshold) {
		hpd->stats[pin].state = HPD_MARK_DISABLED;
		drm_dbg_kms(display->drm,
			    "HPD interrupt storm detected on PIN %d\n", pin);
		storm = true;
	} else {
		drm_dbg_kms(display->drm,
			    "Received HPD interrupt on PIN %d - cnt: %d\n",
			      pin,
			      hpd->stats[pin].count);
	}

	return storm;
}

static bool detection_work_enabled(struct intel_display *display)
{
	lockdep_assert_held(&display->irq.lock);

	return display->hotplug.detection_work_enabled;
}

static bool
mod_delayed_detection_work(struct intel_display *display, struct delayed_work *work, int delay)
{
	lockdep_assert_held(&display->irq.lock);

	if (!detection_work_enabled(display))
		return false;

	return mod_delayed_work(display->wq.unordered, work, delay);
}

static bool
queue_delayed_detection_work(struct intel_display *display, struct delayed_work *work, int delay)
{
	lockdep_assert_held(&display->irq.lock);

	if (!detection_work_enabled(display))
		return false;

	return queue_delayed_work(display->wq.unordered, work, delay);
}

static bool
queue_detection_work(struct intel_display *display, struct work_struct *work)
{
	lockdep_assert_held(&display->irq.lock);

	if (!detection_work_enabled(display))
		return false;

	return queue_work(display->wq.unordered, work);
}

static void
intel_hpd_irq_storm_switch_to_polling(struct intel_display *display)
{
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	bool hpd_disabled = false;

	lockdep_assert_held(&display->irq.lock);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		enum hpd_pin pin;

		if (connector->base.polled != DRM_CONNECTOR_POLL_HPD)
			continue;

		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE ||
		    display->hotplug.stats[pin].state != HPD_MARK_DISABLED)
			continue;

		drm_info(display->drm,
			 "HPD interrupt storm detected on connector %s: "
			 "switching from hotplug detection to polling\n",
			 connector->base.name);

		display->hotplug.stats[pin].state = HPD_DISABLED;
		connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;
		hpd_disabled = true;
	}
	drm_connector_list_iter_end(&conn_iter);

	/* Enable polling and queue hotplug re-enabling. */
	if (hpd_disabled) {
		drm_kms_helper_poll_reschedule(display->drm);
		mod_delayed_detection_work(display,
					   &display->hotplug.reenable_work,
					   msecs_to_jiffies(HPD_STORM_REENABLE_DELAY));
	}
}

static void intel_hpd_irq_storm_reenable_work(struct work_struct *work)
{
	struct intel_display *display =
		container_of(work, typeof(*display), hotplug.reenable_work.work);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	struct ref_tracker *wakeref;
	enum hpd_pin pin;

	wakeref = intel_display_rpm_get(display);

	spin_lock_irq(&display->irq.lock);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE ||
		    display->hotplug.stats[pin].state != HPD_DISABLED)
			continue;

		if (connector->base.polled != connector->polled)
			drm_dbg(display->drm,
				"Reenabling HPD on connector %s\n",
				connector->base.name);
		connector->base.polled = connector->polled;
	}
	drm_connector_list_iter_end(&conn_iter);

	for_each_hpd_pin(pin) {
		if (display->hotplug.stats[pin].state == HPD_DISABLED)
			display->hotplug.stats[pin].state = HPD_ENABLED;
	}

	intel_hpd_irq_setup(display);

	spin_unlock_irq(&display->irq.lock);

	intel_display_rpm_put(display, wakeref);
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

static bool hpd_pin_has_pulse(struct intel_display *display, enum hpd_pin pin)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(display->drm, encoder) {
		if (encoder->hpd_pin != pin)
			continue;

		if (intel_encoder_has_hpd_pulse(encoder))
			return true;
	}

	return false;
}

static bool hpd_pin_is_blocked(struct intel_display *display, enum hpd_pin pin)
{
	lockdep_assert_held(&display->irq.lock);

	return display->hotplug.stats[pin].blocked_count;
}

static u32 get_blocked_hpd_pin_mask(struct intel_display *display)
{
	enum hpd_pin pin;
	u32 hpd_pin_mask = 0;

	for_each_hpd_pin(pin) {
		if (hpd_pin_is_blocked(display, pin))
			hpd_pin_mask |= BIT(pin);
	}

	return hpd_pin_mask;
}

static void i915_digport_work_func(struct work_struct *work)
{
	struct intel_display *display =
		container_of(work, struct intel_display, hotplug.dig_port_work);
	struct intel_hotplug *hotplug = &display->hotplug;
	u32 long_hpd_pin_mask, short_hpd_pin_mask;
	struct intel_encoder *encoder;
	u32 blocked_hpd_pin_mask;
	u32 old_bits = 0;

	spin_lock_irq(&display->irq.lock);

	blocked_hpd_pin_mask = get_blocked_hpd_pin_mask(display);
	long_hpd_pin_mask = hotplug->long_hpd_pin_mask & ~blocked_hpd_pin_mask;
	hotplug->long_hpd_pin_mask &= ~long_hpd_pin_mask;
	short_hpd_pin_mask = hotplug->short_hpd_pin_mask & ~blocked_hpd_pin_mask;
	hotplug->short_hpd_pin_mask &= ~short_hpd_pin_mask;

	spin_unlock_irq(&display->irq.lock);

	for_each_intel_encoder(display->drm, encoder) {
		struct intel_digital_port *dig_port;
		enum hpd_pin pin = encoder->hpd_pin;
		bool long_hpd, short_hpd;
		enum irqreturn ret;

		if (!intel_encoder_has_hpd_pulse(encoder))
			continue;

		long_hpd = long_hpd_pin_mask & BIT(pin);
		short_hpd = short_hpd_pin_mask & BIT(pin);

		if (!long_hpd && !short_hpd)
			continue;

		dig_port = enc_to_dig_port(encoder);

		ret = dig_port->hpd_pulse(dig_port, long_hpd);
		if (ret == IRQ_NONE) {
			/* fall back to old school hpd */
			old_bits |= BIT(pin);
		}
	}

	if (old_bits) {
		spin_lock_irq(&display->irq.lock);
		display->hotplug.event_bits |= old_bits;
		queue_delayed_detection_work(display,
					     &display->hotplug.hotplug_work, 0);
		spin_unlock_irq(&display->irq.lock);
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
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_hotplug *hotplug = &display->hotplug;
	struct intel_encoder *encoder = &dig_port->base;

	spin_lock_irq(&display->irq.lock);

	hotplug->short_hpd_pin_mask |= BIT(encoder->hpd_pin);
	if (!hpd_pin_is_blocked(display, encoder->hpd_pin))
		queue_work(hotplug->dp_wq, &hotplug->dig_port_work);

	spin_unlock_irq(&display->irq.lock);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void i915_hotplug_work_func(struct work_struct *work)
{
	struct intel_display *display =
		container_of(work, struct intel_display, hotplug.hotplug_work.work);
	struct intel_hotplug *hotplug = &display->hotplug;
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	u32 changed = 0, retry = 0;
	u32 hpd_event_bits;
	u32 hpd_retry_bits;
	struct drm_connector *first_changed_connector = NULL;
	int changed_connectors = 0;
	u32 blocked_hpd_pin_mask;

	mutex_lock(&display->drm->mode_config.mutex);
	drm_dbg_kms(display->drm, "running encoder hotplug functions\n");

	spin_lock_irq(&display->irq.lock);

	blocked_hpd_pin_mask = get_blocked_hpd_pin_mask(display);
	hpd_event_bits = hotplug->event_bits & ~blocked_hpd_pin_mask;
	hotplug->event_bits &= ~hpd_event_bits;
	hpd_retry_bits = hotplug->retry_bits & ~blocked_hpd_pin_mask;
	hotplug->retry_bits &= ~hpd_retry_bits;

	/* Enable polling for connectors which had HPD IRQ storms */
	intel_hpd_irq_storm_switch_to_polling(display);

	spin_unlock_irq(&display->irq.lock);

	/* Skip calling encode hotplug handlers if ignore long HPD set*/
	if (display->hotplug.ignore_long_hpd) {
		drm_dbg_kms(display->drm, "Ignore HPD flag on - skip encoder hotplug handlers\n");
		mutex_unlock(&display->drm->mode_config.mutex);
		return;
	}

	drm_connector_list_iter_begin(display->drm, &conn_iter);
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

			drm_dbg_kms(display->drm,
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
	mutex_unlock(&display->drm->mode_config.mutex);

	if (changed_connectors == 1)
		drm_kms_helper_connector_hotplug_event(first_changed_connector);
	else if (changed_connectors > 0)
		drm_kms_helper_hotplug_event(display->drm);

	if (first_changed_connector)
		drm_connector_put(first_changed_connector);

	/* Remove shared HPD pins that have changed */
	retry &= ~changed;
	if (retry) {
		spin_lock_irq(&display->irq.lock);
		display->hotplug.retry_bits |= retry;

		mod_delayed_detection_work(display,
					   &display->hotplug.hotplug_work,
					   msecs_to_jiffies(HPD_RETRY_DELAY));
		spin_unlock_irq(&display->irq.lock);
	}
}


/**
 * intel_hpd_irq_handler - main hotplug irq handler
 * @display: display device
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
void intel_hpd_irq_handler(struct intel_display *display,
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

	spin_lock(&display->irq.lock);

	/*
	 * Determine whether ->hpd_pulse() exists for each pin, and
	 * whether we have a short or a long pulse. This is needed
	 * as each pin may have up to two encoders (HDMI and DP) and
	 * only the one of them (DP) will have ->hpd_pulse().
	 */
	for_each_intel_encoder(display->drm, encoder) {
		bool long_hpd;

		pin = encoder->hpd_pin;
		if (!(BIT(pin) & pin_mask))
			continue;

		if (!intel_encoder_has_hpd_pulse(encoder))
			continue;

		long_hpd = long_mask & BIT(pin);

		drm_dbg(display->drm,
			"digital hpd on [ENCODER:%d:%s] - %s\n",
			encoder->base.base.id, encoder->base.name,
			long_hpd ? "long" : "short");

		if (!hpd_pin_is_blocked(display, pin))
			queue_dig = true;

		if (long_hpd) {
			long_hpd_pulse_mask |= BIT(pin);
			display->hotplug.long_hpd_pin_mask |= BIT(pin);
		} else {
			short_hpd_pulse_mask |= BIT(pin);
			display->hotplug.short_hpd_pin_mask |= BIT(pin);
		}
	}

	/* Now process each pin just once */
	for_each_hpd_pin(pin) {
		bool long_hpd;

		if (!(BIT(pin) & pin_mask))
			continue;

		if (display->hotplug.stats[pin].state == HPD_DISABLED) {
			/*
			 * On GMCH platforms the interrupt mask bits only
			 * prevent irq generation, not the setting of the
			 * hotplug bits itself. So only WARN about unexpected
			 * interrupts on saner platforms.
			 */
			drm_WARN_ONCE(display->drm, !HAS_GMCH(display),
				      "Received HPD interrupt on pin %d although disabled\n",
				      pin);
			continue;
		}

		if (display->hotplug.stats[pin].state != HPD_ENABLED)
			continue;

		/*
		 * Delegate to ->hpd_pulse() if one of the encoders for this
		 * pin has it, otherwise let the hotplug_work deal with this
		 * pin directly.
		 */
		if (((short_hpd_pulse_mask | long_hpd_pulse_mask) & BIT(pin))) {
			long_hpd = long_hpd_pulse_mask & BIT(pin);
		} else {
			display->hotplug.event_bits |= BIT(pin);
			long_hpd = true;

			if (!hpd_pin_is_blocked(display, pin))
				queue_hp = true;
		}

		if (intel_hpd_irq_storm_detect(display, pin, long_hpd)) {
			display->hotplug.event_bits &= ~BIT(pin);
			storm_detected = true;
			queue_hp = true;
		}
	}

	/*
	 * Disable any IRQs that storms were detected on. Polling enablement
	 * happens later in our hotplug work.
	 */
	if (storm_detected)
		intel_hpd_irq_setup(display);

	/*
	 * Our hotplug handler can grab modeset locks (by calling down into the
	 * fb helpers). Hence it must not be run on our own dev-priv->wq work
	 * queue for otherwise the flush_work in the pageflip code will
	 * deadlock.
	 */
	if (queue_dig)
		queue_work(display->hotplug.dp_wq, &display->hotplug.dig_port_work);
	if (queue_hp)
		queue_delayed_detection_work(display,
					     &display->hotplug.hotplug_work, 0);

	spin_unlock(&display->irq.lock);
}

/**
 * intel_hpd_init - initializes and enables hpd support
 * @display: display device instance
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
void intel_hpd_init(struct intel_display *display)
{
	int i;

	if (!HAS_DISPLAY(display))
		return;

	for_each_hpd_pin(i) {
		display->hotplug.stats[i].count = 0;
		display->hotplug.stats[i].state = HPD_ENABLED;
	}

	/*
	 * Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked checks happy.
	 */
	spin_lock_irq(&display->irq.lock);
	intel_hpd_irq_setup(display);
	spin_unlock_irq(&display->irq.lock);
}

static void i915_hpd_poll_detect_connectors(struct intel_display *display)
{
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	struct intel_connector *first_changed_connector = NULL;
	int changed = 0;

	mutex_lock(&display->drm->mode_config.mutex);

	if (!display->drm->mode_config.poll_enabled)
		goto out;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
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
	mutex_unlock(&display->drm->mode_config.mutex);

	if (!changed)
		return;

	if (changed == 1)
		drm_kms_helper_connector_hotplug_event(&first_changed_connector->base);
	else
		drm_kms_helper_hotplug_event(display->drm);

	drm_connector_put(&first_changed_connector->base);
}

static void i915_hpd_poll_init_work(struct work_struct *work)
{
	struct intel_display *display =
		container_of(work, typeof(*display), hotplug.poll_init_work);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	intel_wakeref_t wakeref;
	bool enabled;

	mutex_lock(&display->drm->mode_config.mutex);

	enabled = READ_ONCE(display->hotplug.poll_enabled);
	/*
	 * Prevent taking a power reference from this sequence of
	 * i915_hpd_poll_init_work() -> drm_helper_hpd_irq_event() ->
	 * connector detect which would requeue i915_hpd_poll_init_work()
	 * and so risk an endless loop of this same sequence.
	 */
	if (!enabled) {
		wakeref = intel_display_power_get(display,
						  POWER_DOMAIN_DISPLAY_CORE);
		drm_WARN_ON(display->drm,
			    READ_ONCE(display->hotplug.poll_enabled));
		cancel_work(&display->hotplug.poll_init_work);
	}

	spin_lock_irq(&display->irq.lock);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		enum hpd_pin pin;

		pin = intel_connector_hpd_pin(connector);
		if (pin == HPD_NONE)
			continue;

		if (display->hotplug.stats[pin].state == HPD_DISABLED)
			continue;

		connector->base.polled = connector->polled;

		if (enabled && connector->base.polled == DRM_CONNECTOR_POLL_HPD)
			connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;
	}
	drm_connector_list_iter_end(&conn_iter);

	spin_unlock_irq(&display->irq.lock);

	if (enabled)
		drm_kms_helper_poll_reschedule(display->drm);

	mutex_unlock(&display->drm->mode_config.mutex);

	/*
	 * We might have missed any hotplugs that happened while we were
	 * in the middle of disabling polling
	 */
	if (!enabled) {
		i915_hpd_poll_detect_connectors(display);

		intel_display_power_put(display,
					POWER_DOMAIN_DISPLAY_CORE,
					wakeref);
	}
}

/**
 * intel_hpd_poll_enable - enable polling for connectors with hpd
 * @display: display device instance
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
void intel_hpd_poll_enable(struct intel_display *display)
{
	if (!HAS_DISPLAY(display) || !intel_display_device_enabled(display))
		return;

	WRITE_ONCE(display->hotplug.poll_enabled, true);

	/*
	 * We might already be holding dev->mode_config.mutex, so do this in a
	 * separate worker
	 * As well, there's no issue if we race here since we always reschedule
	 * this worker anyway
	 */
	spin_lock_irq(&display->irq.lock);
	queue_detection_work(display,
			     &display->hotplug.poll_init_work);
	spin_unlock_irq(&display->irq.lock);
}

/**
 * intel_hpd_poll_disable - disable polling for connectors with hpd
 * @display: display device instance
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
void intel_hpd_poll_disable(struct intel_display *display)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(display))
		return;

	for_each_intel_dp(display->drm, encoder)
		intel_dp_dpcd_set_probe(enc_to_intel_dp(encoder), true);

	WRITE_ONCE(display->hotplug.poll_enabled, false);

	spin_lock_irq(&display->irq.lock);
	queue_detection_work(display,
			     &display->hotplug.poll_init_work);
	spin_unlock_irq(&display->irq.lock);
}

void intel_hpd_poll_fini(struct intel_display *display)
{
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	/* Kill all the work that may have been queued by hpd. */
	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		intel_connector_cancel_modeset_retry_work(connector);
		intel_hdcp_cancel_works(connector);
	}
	drm_connector_list_iter_end(&conn_iter);
}

void intel_hpd_init_early(struct intel_display *display)
{
	INIT_DELAYED_WORK(&display->hotplug.hotplug_work,
			  i915_hotplug_work_func);
	INIT_WORK(&display->hotplug.dig_port_work, i915_digport_work_func);
	INIT_WORK(&display->hotplug.poll_init_work, i915_hpd_poll_init_work);
	INIT_DELAYED_WORK(&display->hotplug.reenable_work,
			  intel_hpd_irq_storm_reenable_work);

	display->hotplug.hpd_storm_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	/* If we have MST support, we want to avoid doing short HPD IRQ storm
	 * detection, as short HPD storms will occur as a natural part of
	 * sideband messaging with MST.
	 * On older platforms however, IRQ storms can occur with both long and
	 * short pulses, as seen on some G4x systems.
	 */
	display->hotplug.hpd_short_storm_enabled = !HAS_DP_MST(display);
}

static bool cancel_all_detection_work(struct intel_display *display)
{
	bool was_pending = false;

	if (cancel_delayed_work_sync(&display->hotplug.hotplug_work))
		was_pending = true;
	if (cancel_work_sync(&display->hotplug.poll_init_work))
		was_pending = true;
	if (cancel_delayed_work_sync(&display->hotplug.reenable_work))
		was_pending = true;

	return was_pending;
}

void intel_hpd_cancel_work(struct intel_display *display)
{
	if (!HAS_DISPLAY(display))
		return;

	spin_lock_irq(&display->irq.lock);

	display->hotplug.long_hpd_pin_mask = 0;
	display->hotplug.short_hpd_pin_mask = 0;
	display->hotplug.event_bits = 0;
	display->hotplug.retry_bits = 0;

	spin_unlock_irq(&display->irq.lock);

	cancel_work_sync(&display->hotplug.dig_port_work);

	/*
	 * All other work triggered by hotplug events should be canceled by
	 * now.
	 */
	if (cancel_all_detection_work(display))
		drm_dbg_kms(display->drm, "Hotplug detection work still active\n");
}

static void queue_work_for_missed_irqs(struct intel_display *display)
{
	struct intel_hotplug *hotplug = &display->hotplug;
	bool queue_hp_work = false;
	u32 blocked_hpd_pin_mask;
	enum hpd_pin pin;

	lockdep_assert_held(&display->irq.lock);

	blocked_hpd_pin_mask = get_blocked_hpd_pin_mask(display);
	if ((hotplug->event_bits | hotplug->retry_bits) & ~blocked_hpd_pin_mask)
		queue_hp_work = true;

	for_each_hpd_pin(pin) {
		switch (display->hotplug.stats[pin].state) {
		case HPD_MARK_DISABLED:
			queue_hp_work = true;
			break;
		case HPD_DISABLED:
		case HPD_ENABLED:
			break;
		default:
			MISSING_CASE(display->hotplug.stats[pin].state);
		}
	}

	if ((hotplug->long_hpd_pin_mask | hotplug->short_hpd_pin_mask) & ~blocked_hpd_pin_mask)
		queue_work(hotplug->dp_wq, &hotplug->dig_port_work);

	if (queue_hp_work)
		queue_delayed_detection_work(display, &display->hotplug.hotplug_work, 0);
}

static bool block_hpd_pin(struct intel_display *display, enum hpd_pin pin)
{
	struct intel_hotplug *hotplug = &display->hotplug;

	lockdep_assert_held(&display->irq.lock);

	hotplug->stats[pin].blocked_count++;

	return hotplug->stats[pin].blocked_count == 1;
}

static bool unblock_hpd_pin(struct intel_display *display, enum hpd_pin pin)
{
	struct intel_hotplug *hotplug = &display->hotplug;

	lockdep_assert_held(&display->irq.lock);

	if (drm_WARN_ON(display->drm, hotplug->stats[pin].blocked_count == 0))
		return true;

	hotplug->stats[pin].blocked_count--;

	return hotplug->stats[pin].blocked_count == 0;
}

/**
 * intel_hpd_block - Block handling of HPD IRQs on an HPD pin
 * @encoder: Encoder to block the HPD handling for
 *
 * Blocks the handling of HPD IRQs on the HPD pin of @encoder.
 *
 * On return:
 *
 * - It's guaranteed that the blocked encoders' HPD pulse handler
 *   (via intel_digital_port::hpd_pulse()) is not running.
 * - The hotplug event handling (via intel_encoder::hotplug()) of an
 *   HPD IRQ pending at the time this function is called may be still
 *   running.
 * - Detection on the encoder's connector (via
 *   drm_connector_helper_funcs::detect_ctx(),
 *   drm_connector_funcs::detect()) remains allowed, for instance as part of
 *   userspace connector probing, or DRM core's connector polling.
 *
 * The call must be followed by calling intel_hpd_unblock(), or
 * intel_hpd_clear_and_unblock().
 *
 * Note that the handling of HPD IRQs for another encoder using the same HPD
 * pin as that of @encoder will be also blocked.
 */
void intel_hpd_block(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_hotplug *hotplug = &display->hotplug;
	bool do_flush = false;

	if (encoder->hpd_pin == HPD_NONE)
		return;

	spin_lock_irq(&display->irq.lock);

	if (block_hpd_pin(display, encoder->hpd_pin))
		do_flush = true;

	spin_unlock_irq(&display->irq.lock);

	if (do_flush && hpd_pin_has_pulse(display, encoder->hpd_pin))
		flush_work(&hotplug->dig_port_work);
}

/**
 * intel_hpd_unblock - Unblock handling of HPD IRQs on an HPD pin
 * @encoder: Encoder to unblock the HPD handling for
 *
 * Unblock the handling of HPD IRQs on the HPD pin of @encoder, which was
 * previously blocked by intel_hpd_block(). Any HPD IRQ raised on the
 * HPD pin while it was blocked will be handled for @encoder and for any
 * other encoder sharing the same HPD pin.
 */
void intel_hpd_unblock(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	if (encoder->hpd_pin == HPD_NONE)
		return;

	spin_lock_irq(&display->irq.lock);

	if (unblock_hpd_pin(display, encoder->hpd_pin))
		queue_work_for_missed_irqs(display);

	spin_unlock_irq(&display->irq.lock);
}

/**
 * intel_hpd_clear_and_unblock - Unblock handling of new HPD IRQs on an HPD pin
 * @encoder: Encoder to unblock the HPD handling for
 *
 * Unblock the handling of HPD IRQs on the HPD pin of @encoder, which was
 * previously blocked by intel_hpd_block(). Any HPD IRQ raised on the
 * HPD pin while it was blocked will be cleared, handling only new IRQs.
 */
void intel_hpd_clear_and_unblock(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_hotplug *hotplug = &display->hotplug;
	enum hpd_pin pin = encoder->hpd_pin;

	if (pin == HPD_NONE)
		return;

	spin_lock_irq(&display->irq.lock);

	if (unblock_hpd_pin(display, pin)) {
		hotplug->event_bits &= ~BIT(pin);
		hotplug->retry_bits &= ~BIT(pin);
		hotplug->short_hpd_pin_mask &= ~BIT(pin);
		hotplug->long_hpd_pin_mask &= ~BIT(pin);
	}

	spin_unlock_irq(&display->irq.lock);
}

void intel_hpd_enable_detection_work(struct intel_display *display)
{
	spin_lock_irq(&display->irq.lock);
	display->hotplug.detection_work_enabled = true;
	queue_work_for_missed_irqs(display);
	spin_unlock_irq(&display->irq.lock);
}

void intel_hpd_disable_detection_work(struct intel_display *display)
{
	spin_lock_irq(&display->irq.lock);
	display->hotplug.detection_work_enabled = false;
	spin_unlock_irq(&display->irq.lock);

	cancel_all_detection_work(display);
}

bool intel_hpd_schedule_detection(struct intel_display *display)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&display->irq.lock, flags);
	ret = queue_delayed_detection_work(display, &display->hotplug.hotplug_work, 0);
	spin_unlock_irqrestore(&display->irq.lock, flags);

	return ret;
}

static int i915_hpd_storm_ctl_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	struct intel_hotplug *hotplug = &display->hotplug;

	/* Synchronize with everything first in case there's been an HPD
	 * storm, but we haven't finished handling it in the kernel yet
	 */
	intel_synchronize_irq(dev_priv);
	flush_work(&display->hotplug.dig_port_work);
	flush_delayed_work(&display->hotplug.hotplug_work);

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
	struct intel_display *display = m->private;
	struct intel_hotplug *hotplug = &display->hotplug;
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
		drm_dbg_kms(display->drm,
			    "Setting HPD storm detection threshold to %d\n",
			    new_threshold);
	else
		drm_dbg_kms(display->drm, "Disabling HPD storm detection\n");

	spin_lock_irq(&display->irq.lock);
	hotplug->hpd_storm_threshold = new_threshold;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&display->irq.lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&display->hotplug.reenable_work);

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
	struct intel_display *display = m->private;

	seq_printf(m, "Enabled: %s\n",
		   str_yes_no(display->hotplug.hpd_short_storm_enabled));

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
	struct intel_display *display = m->private;
	struct intel_hotplug *hotplug = &display->hotplug;
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
		new_state = !HAS_DP_MST(display);
	else if (kstrtobool(tmp, &new_state) != 0)
		return -EINVAL;

	drm_dbg_kms(display->drm, "%sabling HPD short storm detection\n",
		    new_state ? "En" : "Dis");

	spin_lock_irq(&display->irq.lock);
	hotplug->hpd_short_storm_enabled = new_state;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&display->irq.lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&display->hotplug.reenable_work);

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

void intel_hpd_debugfs_register(struct intel_display *display)
{
	struct dentry *debugfs_root = display->drm->debugfs_root;

	debugfs_create_file("i915_hpd_storm_ctl", 0644, debugfs_root,
			    display, &i915_hpd_storm_ctl_fops);
	debugfs_create_file("i915_hpd_short_storm_ctl", 0644, debugfs_root,
			    display, &i915_hpd_short_storm_ctl_fops);
	debugfs_create_bool("i915_ignore_long_hpd", 0644, debugfs_root,
			    &display->hotplug.ignore_long_hpd);
}
