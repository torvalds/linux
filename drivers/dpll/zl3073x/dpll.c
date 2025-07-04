// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/dpll.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sprintf.h>

#include "core.h"
#include "dpll.h"
#include "prop.h"
#include "regs.h"

#define ZL3073X_DPLL_REF_NONE		ZL3073X_NUM_REFS
#define ZL3073X_DPLL_REF_IS_VALID(_ref)	((_ref) != ZL3073X_DPLL_REF_NONE)

/**
 * struct zl3073x_dpll_pin - DPLL pin
 * @list: this DPLL pin list entry
 * @dpll: DPLL the pin is registered to
 * @dpll_pin: pointer to registered dpll_pin
 * @label: package label
 * @dir: pin direction
 * @id: pin id
 * @prio: pin priority <0, 14>
 * @selectable: pin is selectable in automatic mode
 * @pin_state: last saved pin state
 */
struct zl3073x_dpll_pin {
	struct list_head	list;
	struct zl3073x_dpll	*dpll;
	struct dpll_pin		*dpll_pin;
	char			label[8];
	enum dpll_pin_direction	dir;
	u8			id;
	u8			prio;
	bool			selectable;
	enum dpll_pin_state	pin_state;
};

/**
 * zl3073x_dpll_is_input_pin - check if the pin is input one
 * @pin: pin to check
 *
 * Return: true if pin is input, false if pin is output.
 */
static bool
zl3073x_dpll_is_input_pin(struct zl3073x_dpll_pin *pin)
{
	return pin->dir == DPLL_PIN_DIRECTION_INPUT;
}

/**
 * zl3073x_dpll_is_p_pin - check if the pin is P-pin
 * @pin: pin to check
 *
 * Return: true if the pin is P-pin, false if it is N-pin
 */
static bool
zl3073x_dpll_is_p_pin(struct zl3073x_dpll_pin *pin)
{
	return zl3073x_is_p_pin(pin->id);
}

static int
zl3073x_dpll_pin_direction_get(const struct dpll_pin *dpll_pin, void *pin_priv,
			       const struct dpll_device *dpll, void *dpll_priv,
			       enum dpll_pin_direction *direction,
			       struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	*direction = pin->dir;

	return 0;
}

/**
 * zl3073x_dpll_selected_ref_get - get currently selected reference
 * @zldpll: pointer to zl3073x_dpll
 * @ref: place to store selected reference
 *
 * Check for currently selected reference the DPLL should be locked to
 * and stores its index to given @ref.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_selected_ref_get(struct zl3073x_dpll *zldpll, u8 *ref)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 state, value;
	int rc;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_AUTO:
		/* For automatic mode read refsel_status register */
		rc = zl3073x_read_u8(zldev,
				     ZL_REG_DPLL_REFSEL_STATUS(zldpll->id),
				     &value);
		if (rc)
			return rc;

		/* Extract reference state */
		state = FIELD_GET(ZL_DPLL_REFSEL_STATUS_STATE, value);

		/* Return the reference only if the DPLL is locked to it */
		if (state == ZL_DPLL_REFSEL_STATUS_STATE_LOCK)
			*ref = FIELD_GET(ZL_DPLL_REFSEL_STATUS_REFSEL, value);
		else
			*ref = ZL3073X_DPLL_REF_NONE;
		break;
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
		/* For manual mode return stored value */
		*ref = zldpll->forced_ref;
		break;
	default:
		/* For other modes like NCO, freerun... there is no input ref */
		*ref = ZL3073X_DPLL_REF_NONE;
		break;
	}

	return 0;
}

/**
 * zl3073x_dpll_connected_ref_get - get currently connected reference
 * @zldpll: pointer to zl3073x_dpll
 * @ref: place to store selected reference
 *
 * Looks for currently connected the DPLL is locked to and stores its index
 * to given @ref.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_connected_ref_get(struct zl3073x_dpll *zldpll, u8 *ref)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	int rc;

	/* Get currently selected input reference */
	rc = zl3073x_dpll_selected_ref_get(zldpll, ref);
	if (rc)
		return rc;

	if (ZL3073X_DPLL_REF_IS_VALID(*ref)) {
		u8 ref_status;

		/* Read the reference monitor status */
		rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(*ref),
				     &ref_status);
		if (rc)
			return rc;

		/* If the monitor indicates an error nothing is connected */
		if (ref_status != ZL_REF_MON_STATUS_OK)
			*ref = ZL3073X_DPLL_REF_NONE;
	}

	return 0;
}

/**
 * zl3073x_dpll_ref_prio_get - get priority for given input pin
 * @pin: pointer to pin
 * @prio: place to store priority
 *
 * Reads current priority for the given input pin and stores the value
 * to @prio.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_ref_prio_get(struct zl3073x_dpll_pin *pin, u8 *prio)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, ref_prio;
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(zldpll->id));
	if (rc)
		return rc;

	/* Read reference priority - one value for P&N pins (4 bits/pin) */
	ref = zl3073x_input_pin_ref_get(pin->id);
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REF_PRIO(ref / 2),
			     &ref_prio);
	if (rc)
		return rc;

	/* Select nibble according pin type */
	if (zl3073x_dpll_is_p_pin(pin))
		*prio = FIELD_GET(ZL_DPLL_REF_PRIO_REF_P, ref_prio);
	else
		*prio = FIELD_GET(ZL_DPLL_REF_PRIO_REF_N, ref_prio);

	return rc;
}

/**
 * zl3073x_dpll_ref_state_get - get status for given input pin
 * @pin: pointer to pin
 * @state: place to store status
 *
 * Checks current status for the given input pin and stores the value
 * to @state.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_ref_state_get(struct zl3073x_dpll_pin *pin,
			   enum dpll_pin_state *state)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 ref, ref_conn, status;
	int rc;

	ref = zl3073x_input_pin_ref_get(pin->id);

	/* Get currently connected reference */
	rc = zl3073x_dpll_connected_ref_get(zldpll, &ref_conn);
	if (rc)
		return rc;

	if (ref == ref_conn) {
		*state = DPLL_PIN_STATE_CONNECTED;
		return 0;
	}

	/* If the DPLL is running in automatic mode and the reference is
	 * selectable and its monitor does not report any error then report
	 * pin as selectable.
	 */
	if (zldpll->refsel_mode == ZL_DPLL_MODE_REFSEL_MODE_AUTO &&
	    pin->selectable) {
		/* Read reference monitor status */
		rc = zl3073x_read_u8(zldev, ZL_REG_REF_MON_STATUS(ref),
				     &status);
		if (rc)
			return rc;

		/* If the monitor indicates errors report the reference
		 * as disconnected
		 */
		if (status == ZL_REF_MON_STATUS_OK) {
			*state = DPLL_PIN_STATE_SELECTABLE;
			return 0;
		}
	}

	/* Otherwise report the pin as disconnected */
	*state = DPLL_PIN_STATE_DISCONNECTED;

	return 0;
}

static int
zl3073x_dpll_input_pin_state_on_dpll_get(const struct dpll_pin *dpll_pin,
					 void *pin_priv,
					 const struct dpll_device *dpll,
					 void *dpll_priv,
					 enum dpll_pin_state *state,
					 struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll_pin *pin = pin_priv;

	return zl3073x_dpll_ref_state_get(pin, state);
}

static int
zl3073x_dpll_output_pin_state_on_dpll_get(const struct dpll_pin *dpll_pin,
					  void *pin_priv,
					  const struct dpll_device *dpll,
					  void *dpll_priv,
					  enum dpll_pin_state *state,
					  struct netlink_ext_ack *extack)
{
	/* If the output pin is registered then it is always connected */
	*state = DPLL_PIN_STATE_CONNECTED;

	return 0;
}

static int
zl3073x_dpll_lock_status_get(const struct dpll_device *dpll, void *dpll_priv,
			     enum dpll_lock_status *status,
			     enum dpll_lock_status_error *status_error,
			     struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 mon_status, state;
	int rc;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_NCO:
		/* In FREERUN and NCO modes the DPLL is always unlocked */
		*status = DPLL_LOCK_STATUS_UNLOCKED;

		return 0;
	default:
		break;
	}

	/* Read DPLL monitor status */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MON_STATUS(zldpll->id),
			     &mon_status);
	if (rc)
		return rc;
	state = FIELD_GET(ZL_DPLL_MON_STATUS_STATE, mon_status);

	switch (state) {
	case ZL_DPLL_MON_STATUS_STATE_LOCK:
		if (FIELD_GET(ZL_DPLL_MON_STATUS_HO_READY, mon_status))
			*status = DPLL_LOCK_STATUS_LOCKED_HO_ACQ;
		else
			*status = DPLL_LOCK_STATUS_LOCKED;
		break;
	case ZL_DPLL_MON_STATUS_STATE_HOLDOVER:
	case ZL_DPLL_MON_STATUS_STATE_ACQUIRING:
		*status = DPLL_LOCK_STATUS_HOLDOVER;
		break;
	default:
		dev_warn(zldev->dev, "Unknown DPLL monitor status: 0x%02x\n",
			 mon_status);
		*status = DPLL_LOCK_STATUS_UNLOCKED;
		break;
	}

	return 0;
}

static int
zl3073x_dpll_mode_get(const struct dpll_device *dpll, void *dpll_priv,
		      enum dpll_mode *mode, struct netlink_ext_ack *extack)
{
	struct zl3073x_dpll *zldpll = dpll_priv;

	switch (zldpll->refsel_mode) {
	case ZL_DPLL_MODE_REFSEL_MODE_FREERUN:
	case ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER:
	case ZL_DPLL_MODE_REFSEL_MODE_NCO:
	case ZL_DPLL_MODE_REFSEL_MODE_REFLOCK:
		/* Use MANUAL for device FREERUN, HOLDOVER, NCO and
		 * REFLOCK modes
		 */
		*mode = DPLL_MODE_MANUAL;
		break;
	case ZL_DPLL_MODE_REFSEL_MODE_AUTO:
		/* Use AUTO for device AUTO mode */
		*mode = DPLL_MODE_AUTOMATIC;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct dpll_pin_ops zl3073x_dpll_input_pin_ops = {
	.direction_get = zl3073x_dpll_pin_direction_get,
	.state_on_dpll_get = zl3073x_dpll_input_pin_state_on_dpll_get,
};

static const struct dpll_pin_ops zl3073x_dpll_output_pin_ops = {
	.direction_get = zl3073x_dpll_pin_direction_get,
	.state_on_dpll_get = zl3073x_dpll_output_pin_state_on_dpll_get,
};

static const struct dpll_device_ops zl3073x_dpll_device_ops = {
	.lock_status_get = zl3073x_dpll_lock_status_get,
	.mode_get = zl3073x_dpll_mode_get,
};

/**
 * zl3073x_dpll_pin_alloc - allocate DPLL pin
 * @zldpll: pointer to zl3073x_dpll
 * @dir: pin direction
 * @id: pin id
 *
 * Allocates and initializes zl3073x_dpll_pin structure for given
 * pin id and direction.
 *
 * Return: pointer to allocated structure on success, error pointer on error
 */
static struct zl3073x_dpll_pin *
zl3073x_dpll_pin_alloc(struct zl3073x_dpll *zldpll, enum dpll_pin_direction dir,
		       u8 id)
{
	struct zl3073x_dpll_pin *pin;

	pin = kzalloc(sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return ERR_PTR(-ENOMEM);

	pin->dpll = zldpll;
	pin->dir = dir;
	pin->id = id;

	return pin;
}

/**
 * zl3073x_dpll_pin_free - deallocate DPLL pin
 * @pin: pin to free
 *
 * Deallocates DPLL pin previously allocated by @zl3073x_dpll_pin_alloc.
 */
static void
zl3073x_dpll_pin_free(struct zl3073x_dpll_pin *pin)
{
	WARN(pin->dpll_pin, "DPLL pin is still registered\n");

	kfree(pin);
}

/**
 * zl3073x_dpll_pin_register - register DPLL pin
 * @pin: pointer to DPLL pin
 * @index: absolute pin index for registration
 *
 * Registers given DPLL pin into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_pin_register(struct zl3073x_dpll_pin *pin, u32 index)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	struct zl3073x_pin_props *props;
	const struct dpll_pin_ops *ops;
	int rc;

	/* Get pin properties */
	props = zl3073x_pin_props_get(zldpll->dev, pin->dir, pin->id);
	if (IS_ERR(props))
		return PTR_ERR(props);

	/* Save package label */
	strscpy(pin->label, props->package_label);

	if (zl3073x_dpll_is_input_pin(pin)) {
		rc = zl3073x_dpll_ref_prio_get(pin, &pin->prio);
		if (rc)
			goto err_prio_get;

		if (pin->prio == ZL_DPLL_REF_PRIO_NONE) {
			/* Clamp prio to max value & mark pin non-selectable */
			pin->prio = ZL_DPLL_REF_PRIO_MAX;
			pin->selectable = false;
		} else {
			/* Mark pin as selectable */
			pin->selectable = true;
		}
	}

	/* Create or get existing DPLL pin */
	pin->dpll_pin = dpll_pin_get(zldpll->dev->clock_id, index, THIS_MODULE,
				     &props->dpll_props);
	if (IS_ERR(pin->dpll_pin)) {
		rc = PTR_ERR(pin->dpll_pin);
		goto err_pin_get;
	}

	if (zl3073x_dpll_is_input_pin(pin))
		ops = &zl3073x_dpll_input_pin_ops;
	else
		ops = &zl3073x_dpll_output_pin_ops;

	/* Register the pin */
	rc = dpll_pin_register(zldpll->dpll_dev, pin->dpll_pin, ops, pin);
	if (rc)
		goto err_register;

	/* Free pin properties */
	zl3073x_pin_props_put(props);

	return 0;

err_register:
	dpll_pin_put(pin->dpll_pin);
err_prio_get:
	pin->dpll_pin = NULL;
err_pin_get:
	zl3073x_pin_props_put(props);

	return rc;
}

/**
 * zl3073x_dpll_pin_unregister - unregister DPLL pin
 * @pin: pointer to DPLL pin
 *
 * Unregisters pin previously registered by @zl3073x_dpll_pin_register.
 */
static void
zl3073x_dpll_pin_unregister(struct zl3073x_dpll_pin *pin)
{
	struct zl3073x_dpll *zldpll = pin->dpll;
	const struct dpll_pin_ops *ops;

	WARN(!pin->dpll_pin, "DPLL pin is not registered\n");

	if (zl3073x_dpll_is_input_pin(pin))
		ops = &zl3073x_dpll_input_pin_ops;
	else
		ops = &zl3073x_dpll_output_pin_ops;

	/* Unregister the pin */
	dpll_pin_unregister(zldpll->dpll_dev, pin->dpll_pin, ops, pin);

	dpll_pin_put(pin->dpll_pin);
	pin->dpll_pin = NULL;
}

/**
 * zl3073x_dpll_pins_unregister - unregister all registered DPLL pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Enumerates all DPLL pins registered to given DPLL device and
 * unregisters them.
 */
static void
zl3073x_dpll_pins_unregister(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dpll_pin *pin, *next;

	list_for_each_entry_safe(pin, next, &zldpll->pins, list) {
		zl3073x_dpll_pin_unregister(pin);
		list_del(&pin->list);
		zl3073x_dpll_pin_free(pin);
	}
}

/**
 * zl3073x_dpll_pin_is_registrable - check if the pin is registrable
 * @zldpll: pointer to zl3073x_dpll structure
 * @dir: pin direction
 * @index: pin index
 *
 * Checks if the given pin can be registered to given DPLL. For both
 * directions the pin can be registered if it is enabled. In case of
 * differential signal type only P-pin is reported as registrable.
 * And additionally for the output pin, the pin can be registered only
 * if it is connected to synthesizer that is driven by given DPLL.
 *
 * Return: true if the pin is registrable, false if not
 */
static bool
zl3073x_dpll_pin_is_registrable(struct zl3073x_dpll *zldpll,
				enum dpll_pin_direction dir, u8 index)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	bool is_diff, is_enabled;
	const char *name;

	if (dir == DPLL_PIN_DIRECTION_INPUT) {
		u8 ref = zl3073x_input_pin_ref_get(index);

		name = "REF";

		/* Skip the pin if the DPLL is running in NCO mode */
		if (zldpll->refsel_mode == ZL_DPLL_MODE_REFSEL_MODE_NCO)
			return false;

		is_diff = zl3073x_ref_is_diff(zldev, ref);
		is_enabled = zl3073x_ref_is_enabled(zldev, ref);
	} else {
		/* Output P&N pair shares single HW output */
		u8 out = zl3073x_output_pin_out_get(index);

		name = "OUT";

		/* Skip the pin if it is connected to different DPLL channel */
		if (zl3073x_out_dpll_get(zldev, out) != zldpll->id) {
			dev_dbg(zldev->dev,
				"%s%u is driven by different DPLL\n", name,
				out);

			return false;
		}

		is_diff = zl3073x_out_is_diff(zldev, out);
		is_enabled = zl3073x_out_is_enabled(zldev, out);
	}

	/* Skip N-pin if the corresponding input/output is differential */
	if (is_diff && zl3073x_is_n_pin(index)) {
		dev_dbg(zldev->dev, "%s%u is differential, skipping N-pin\n",
			name, index / 2);

		return false;
	}

	/* Skip the pin if it is disabled */
	if (!is_enabled) {
		dev_dbg(zldev->dev, "%s%u%c is disabled\n", name, index / 2,
			zl3073x_is_p_pin(index) ? 'P' : 'N');

		return false;
	}

	return true;
}

/**
 * zl3073x_dpll_pins_register - register all registerable DPLL pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Enumerates all possible input/output pins and registers all of them
 * that are registrable.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_pins_register(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dpll_pin *pin;
	enum dpll_pin_direction dir;
	u8 id, index;
	int rc;

	/* Process input pins */
	for (index = 0; index < ZL3073X_NUM_PINS; index++) {
		/* First input pins and then output pins */
		if (index < ZL3073X_NUM_INPUT_PINS) {
			id = index;
			dir = DPLL_PIN_DIRECTION_INPUT;
		} else {
			id = index - ZL3073X_NUM_INPUT_PINS;
			dir = DPLL_PIN_DIRECTION_OUTPUT;
		}

		/* Check if the pin registrable to this DPLL */
		if (!zl3073x_dpll_pin_is_registrable(zldpll, dir, id))
			continue;

		pin = zl3073x_dpll_pin_alloc(zldpll, dir, id);
		if (IS_ERR(pin)) {
			rc = PTR_ERR(pin);
			goto error;
		}

		rc = zl3073x_dpll_pin_register(pin, index);
		if (rc)
			goto error;

		list_add(&pin->list, &zldpll->pins);
	}

	return 0;

error:
	zl3073x_dpll_pins_unregister(zldpll);

	return rc;
}

/**
 * zl3073x_dpll_device_register - register DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Registers given DPLL device into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_dpll_device_register(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	u8 dpll_mode_refsel;
	int rc;

	/* Read DPLL mode and forcibly selected reference */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(zldpll->id),
			     &dpll_mode_refsel);
	if (rc)
		return rc;

	/* Extract mode and selected input reference */
	zldpll->refsel_mode = FIELD_GET(ZL_DPLL_MODE_REFSEL_MODE,
					dpll_mode_refsel);
	zldpll->forced_ref = FIELD_GET(ZL_DPLL_MODE_REFSEL_REF,
				       dpll_mode_refsel);

	zldpll->dpll_dev = dpll_device_get(zldev->clock_id, zldpll->id,
					   THIS_MODULE);
	if (IS_ERR(zldpll->dpll_dev)) {
		rc = PTR_ERR(zldpll->dpll_dev);
		zldpll->dpll_dev = NULL;

		return rc;
	}

	rc = dpll_device_register(zldpll->dpll_dev,
				  zl3073x_prop_dpll_type_get(zldev, zldpll->id),
				  &zl3073x_dpll_device_ops, zldpll);
	if (rc) {
		dpll_device_put(zldpll->dpll_dev);
		zldpll->dpll_dev = NULL;
	}

	return rc;
}

/**
 * zl3073x_dpll_device_unregister - unregister DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Unregisters given DPLL device from DPLL sub-system previously registered
 * by @zl3073x_dpll_device_register.
 */
static void
zl3073x_dpll_device_unregister(struct zl3073x_dpll *zldpll)
{
	WARN(!zldpll->dpll_dev, "DPLL device is not registered\n");

	dpll_device_unregister(zldpll->dpll_dev, &zl3073x_dpll_device_ops,
			       zldpll);
	dpll_device_put(zldpll->dpll_dev);
	zldpll->dpll_dev = NULL;
}

/**
 * zl3073x_dpll_changes_check - check for changes and send notifications
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Checks for changes on given DPLL device and its registered DPLL pins
 * and sends notifications about them.
 *
 * This function is periodically called from @zl3073x_dev_periodic_work.
 */
void
zl3073x_dpll_changes_check(struct zl3073x_dpll *zldpll)
{
	struct zl3073x_dev *zldev = zldpll->dev;
	enum dpll_lock_status lock_status;
	struct device *dev = zldev->dev;
	struct zl3073x_dpll_pin *pin;
	int rc;

	/* Get current lock status for the DPLL */
	rc = zl3073x_dpll_lock_status_get(zldpll->dpll_dev, zldpll,
					  &lock_status, NULL, NULL);
	if (rc) {
		dev_err(dev, "Failed to get DPLL%u lock status: %pe\n",
			zldpll->id, ERR_PTR(rc));
		return;
	}

	/* If lock status was changed then notify DPLL core */
	if (zldpll->lock_status != lock_status) {
		zldpll->lock_status = lock_status;
		dpll_device_change_ntf(zldpll->dpll_dev);
	}

	/* Input pin monitoring does make sense only in automatic
	 * or forced reference modes.
	 */
	if (zldpll->refsel_mode != ZL_DPLL_MODE_REFSEL_MODE_AUTO &&
	    zldpll->refsel_mode != ZL_DPLL_MODE_REFSEL_MODE_REFLOCK)
		return;

	list_for_each_entry(pin, &zldpll->pins, list) {
		enum dpll_pin_state state;

		/* Output pins change checks are not necessary because output
		 * states are constant.
		 */
		if (!zl3073x_dpll_is_input_pin(pin))
			continue;

		rc = zl3073x_dpll_ref_state_get(pin, &state);
		if (rc) {
			dev_err(dev,
				"Failed to get %s on DPLL%u state: %pe\n",
				pin->label, zldpll->id, ERR_PTR(rc));
			return;
		}

		if (state != pin->pin_state) {
			dev_dbg(dev, "%s state changed: %u->%u\n", pin->label,
				pin->pin_state, state);
			pin->pin_state = state;
			dpll_pin_change_ntf(pin->dpll_pin);
		}
	}
}

/**
 * zl3073x_dpll_init_fine_phase_adjust - do initial fine phase adjustments
 * @zldev: pointer to zl3073x device
 *
 * Performs initial fine phase adjustments needed per datasheet.
 *
 * Return: 0 on success, <0 on error
 */
int
zl3073x_dpll_init_fine_phase_adjust(struct zl3073x_dev *zldev)
{
	int rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_MASK, 0x1f);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_INTVL, 0x01);
	if (rc)
		return rc;

	rc = zl3073x_write_u16(zldev, ZL_REG_SYNTH_PHASE_SHIFT_DATA, 0xffff);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_SYNTH_PHASE_SHIFT_CTRL, 0x01);
	if (rc)
		return rc;

	return rc;
}

/**
 * zl3073x_dpll_alloc - allocate DPLL device
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel number
 *
 * Allocates DPLL device structure for given DPLL channel.
 *
 * Return: pointer to DPLL device on success, error pointer on error
 */
struct zl3073x_dpll *
zl3073x_dpll_alloc(struct zl3073x_dev *zldev, u8 ch)
{
	struct zl3073x_dpll *zldpll;

	zldpll = kzalloc(sizeof(*zldpll), GFP_KERNEL);
	if (!zldpll)
		return ERR_PTR(-ENOMEM);

	zldpll->dev = zldev;
	zldpll->id = ch;
	INIT_LIST_HEAD(&zldpll->pins);

	return zldpll;
}

/**
 * zl3073x_dpll_free - free DPLL device
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Deallocates given DPLL device previously allocated by @zl3073x_dpll_alloc.
 */
void
zl3073x_dpll_free(struct zl3073x_dpll *zldpll)
{
	WARN(zldpll->dpll_dev, "DPLL device is still registered\n");

	kfree(zldpll);
}

/**
 * zl3073x_dpll_register - register DPLL device and all its pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Registers given DPLL device and all its pins into DPLL sub-system.
 *
 * Return: 0 on success, <0 on error
 */
int
zl3073x_dpll_register(struct zl3073x_dpll *zldpll)
{
	int rc;

	rc = zl3073x_dpll_device_register(zldpll);
	if (rc)
		return rc;

	rc = zl3073x_dpll_pins_register(zldpll);
	if (rc) {
		zl3073x_dpll_device_unregister(zldpll);
		return rc;
	}

	return 0;
}

/**
 * zl3073x_dpll_unregister - unregister DPLL device and its pins
 * @zldpll: pointer to zl3073x_dpll structure
 *
 * Unregisters given DPLL device and all its pins from DPLL sub-system
 * previously registered by @zl3073x_dpll_register.
 */
void
zl3073x_dpll_unregister(struct zl3073x_dpll *zldpll)
{
	/* Unregister all pins and dpll */
	zl3073x_dpll_pins_unregister(zldpll);
	zl3073x_dpll_device_unregister(zldpll);
}
