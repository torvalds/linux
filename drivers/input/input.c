/*
 * The input core
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/smp_lock.h>
#include "input-compat.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

#define INPUT_DEVICES	256

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

/*
 * input_mutex protects access to both input_dev_list and input_handler_list.
 * This also causes input_[un]register_device and input_[un]register_handler
 * be mutually exclusive which simplifies locking in drivers implementing
 * input handlers.
 */
static DEFINE_MUTEX(input_mutex);

static struct input_handler *input_table[8];

static inline int is_event_supported(unsigned int code,
				     unsigned long *bm, unsigned int max)
{
	return code <= max && test_bit(code, bm);
}

static int input_defuzz_abs_event(int value, int old_val, int fuzz)
{
	if (fuzz) {
		if (value > old_val - fuzz / 2 && value < old_val + fuzz / 2)
			return old_val;

		if (value > old_val - fuzz && value < old_val + fuzz)
			return (old_val * 3 + value) / 4;

		if (value > old_val - fuzz * 2 && value < old_val + fuzz * 2)
			return (old_val + value) / 2;
	}

	return value;
}

/*
 * Pass event first through all filters and then, if event has not been
 * filtered out, through all open handles. This function is called with
 * dev->event_lock held and interrupts disabled.
 */
static void input_pass_event(struct input_dev *dev,
			     unsigned int type, unsigned int code, int value)
{
	struct input_handler *handler;
	struct input_handle *handle;

	rcu_read_lock();

	handle = rcu_dereference(dev->grab);
	if (handle)
		handle->handler->event(handle, type, code, value);
	else {
		bool filtered = false;

		list_for_each_entry_rcu(handle, &dev->h_list, d_node) {
			if (!handle->open)
				continue;

			handler = handle->handler;
			if (!handler->filter) {
				if (filtered)
					break;

				handler->event(handle, type, code, value);

			} else if (handler->filter(handle, type, code, value))
				filtered = true;
		}
	}

	rcu_read_unlock();
}

/*
 * Generate software autorepeat event. Note that we take
 * dev->event_lock here to avoid racing with input_event
 * which may cause keys get "stuck".
 */
static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (test_bit(dev->repeat_key, dev->key) &&
	    is_event_supported(dev->repeat_key, dev->keybit, KEY_MAX)) {

		input_pass_event(dev, EV_KEY, dev->repeat_key, 2);

		if (dev->sync) {
			/*
			 * Only send SYN_REPORT if we are not in a middle
			 * of driver parsing a new hardware packet.
			 * Otherwise assume that the driver will send
			 * SYN_REPORT once it's done.
			 */
			input_pass_event(dev, EV_SYN, SYN_REPORT, 1);
		}

		if (dev->rep[REP_PERIOD])
			mod_timer(&dev->timer, jiffies +
					msecs_to_jiffies(dev->rep[REP_PERIOD]));
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void input_start_autorepeat(struct input_dev *dev, int code)
{
	if (test_bit(EV_REP, dev->evbit) &&
	    dev->rep[REP_PERIOD] && dev->rep[REP_DELAY] &&
	    dev->timer.data) {
		dev->repeat_key = code;
		mod_timer(&dev->timer,
			  jiffies + msecs_to_jiffies(dev->rep[REP_DELAY]));
	}
}

static void input_stop_autorepeat(struct input_dev *dev)
{
	del_timer(&dev->timer);
}

#define INPUT_IGNORE_EVENT	0
#define INPUT_PASS_TO_HANDLERS	1
#define INPUT_PASS_TO_DEVICE	2
#define INPUT_PASS_TO_ALL	(INPUT_PASS_TO_HANDLERS | INPUT_PASS_TO_DEVICE)

static int input_handle_abs_event(struct input_dev *dev,
				  unsigned int code, int *pval)
{
	bool is_mt_event;
	int *pold;

	if (code == ABS_MT_SLOT) {
		/*
		 * "Stage" the event; we'll flush it later, when we
		 * get actual touch data.
		 */
		if (*pval >= 0 && *pval < dev->mtsize)
			dev->slot = *pval;

		return INPUT_IGNORE_EVENT;
	}

	is_mt_event = code >= ABS_MT_FIRST && code <= ABS_MT_LAST;

	if (!is_mt_event) {
		pold = &dev->absinfo[code].value;
	} else if (dev->mt) {
		struct input_mt_slot *mtslot = &dev->mt[dev->slot];
		pold = &mtslot->abs[code - ABS_MT_FIRST];
	} else {
		/*
		 * Bypass filtering for multi-touch events when
		 * not employing slots.
		 */
		pold = NULL;
	}

	if (pold) {
		*pval = input_defuzz_abs_event(*pval, *pold,
						dev->absinfo[code].fuzz);
		if (*pold == *pval)
			return INPUT_IGNORE_EVENT;

		*pold = *pval;
	}

	/* Flush pending "slot" event */
	if (is_mt_event && dev->slot != input_abs_get_val(dev, ABS_MT_SLOT)) {
		input_abs_set_val(dev, ABS_MT_SLOT, dev->slot);
		input_pass_event(dev, EV_ABS, ABS_MT_SLOT, dev->slot);
	}

	return INPUT_PASS_TO_HANDLERS;
}

static void input_handle_event(struct input_dev *dev,
			       unsigned int type, unsigned int code, int value)
{
	int disposition = INPUT_IGNORE_EVENT;

	switch (type) {

	case EV_SYN:
		switch (code) {
		case SYN_CONFIG:
			disposition = INPUT_PASS_TO_ALL;
			break;

		case SYN_REPORT:
			if (!dev->sync) {
				dev->sync = true;
				disposition = INPUT_PASS_TO_HANDLERS;
			}
			break;
		case SYN_MT_REPORT:
			dev->sync = false;
			disposition = INPUT_PASS_TO_HANDLERS;
			break;
		}
		break;

	case EV_KEY:
		if (is_event_supported(code, dev->keybit, KEY_MAX) &&
		    !!test_bit(code, dev->key) != value) {

			if (value != 2) {
				__change_bit(code, dev->key);
				if (value)
					input_start_autorepeat(dev, code);
				else
					input_stop_autorepeat(dev);
			}

			disposition = INPUT_PASS_TO_HANDLERS;
		}
		break;

	case EV_SW:
		if (is_event_supported(code, dev->swbit, SW_MAX) &&
		    !!test_bit(code, dev->sw) != value) {

			__change_bit(code, dev->sw);
			disposition = INPUT_PASS_TO_HANDLERS;
		}
		break;

	case EV_ABS:
		if (is_event_supported(code, dev->absbit, ABS_MAX))
			disposition = input_handle_abs_event(dev, code, &value);

		break;

	case EV_REL:
		if (is_event_supported(code, dev->relbit, REL_MAX) && value)
			disposition = INPUT_PASS_TO_HANDLERS;

		break;

	case EV_MSC:
		if (is_event_supported(code, dev->mscbit, MSC_MAX))
			disposition = INPUT_PASS_TO_ALL;

		break;

	case EV_LED:
		if (is_event_supported(code, dev->ledbit, LED_MAX) &&
		    !!test_bit(code, dev->led) != value) {

			__change_bit(code, dev->led);
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_SND:
		if (is_event_supported(code, dev->sndbit, SND_MAX)) {

			if (!!test_bit(code, dev->snd) != !!value)
				__change_bit(code, dev->snd);
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_REP:
		if (code <= REP_MAX && value >= 0 && dev->rep[code] != value) {
			dev->rep[code] = value;
			disposition = INPUT_PASS_TO_ALL;
		}
		break;

	case EV_FF:
		if (value >= 0)
			disposition = INPUT_PASS_TO_ALL;
		break;

	case EV_PWR:
		disposition = INPUT_PASS_TO_ALL;
		break;
	}

	if (disposition != INPUT_IGNORE_EVENT && type != EV_SYN)
		dev->sync = false;

	if ((disposition & INPUT_PASS_TO_DEVICE) && dev->event)
		dev->event(dev, type, code, value);

	if (disposition & INPUT_PASS_TO_HANDLERS)
		input_pass_event(dev, type, code, value);
}

/**
 * input_event() - report new input event
 * @dev: device that generated the event
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * This function should be used by drivers implementing various input
 * devices to report input events. See also input_inject_event().
 *
 * NOTE: input_event() may be safely used right after input device was
 * allocated with input_allocate_device(), even before it is registered
 * with input_register_device(), but the event will not reach any of the
 * input handlers. Such early invocation of input_event() may be used
 * to 'seed' initial state of a switch or initial position of absolute
 * axis, etc.
 */
void input_event(struct input_dev *dev,
		 unsigned int type, unsigned int code, int value)
{
	unsigned long flags;

	if (is_event_supported(type, dev->evbit, EV_MAX)) {

		spin_lock_irqsave(&dev->event_lock, flags);
		add_input_randomness(type, code, value);
		input_handle_event(dev, type, code, value);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}
EXPORT_SYMBOL(input_event);

/**
 * input_inject_event() - send input event from input handler
 * @handle: input handle to send event through
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * Similar to input_event() but will ignore event if device is
 * "grabbed" and handle injecting event is not the one that owns
 * the device.
 */
void input_inject_event(struct input_handle *handle,
			unsigned int type, unsigned int code, int value)
{
	struct input_dev *dev = handle->dev;
	struct input_handle *grab;
	unsigned long flags;

	if (is_event_supported(type, dev->evbit, EV_MAX)) {
		spin_lock_irqsave(&dev->event_lock, flags);

		rcu_read_lock();
		grab = rcu_dereference(dev->grab);
		if (!grab || grab == handle)
			input_handle_event(dev, type, code, value);
		rcu_read_unlock();

		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}
EXPORT_SYMBOL(input_inject_event);

/**
 * input_alloc_absinfo - allocates array of input_absinfo structs
 * @dev: the input device emitting absolute events
 *
 * If the absinfo struct the caller asked for is already allocated, this
 * functions will not do anything.
 */
void input_alloc_absinfo(struct input_dev *dev)
{
	if (!dev->absinfo)
		dev->absinfo = kcalloc(ABS_CNT, sizeof(struct input_absinfo),
					GFP_KERNEL);

	WARN(!dev->absinfo, "%s(): kcalloc() failed?\n", __func__);
}
EXPORT_SYMBOL(input_alloc_absinfo);

void input_set_abs_params(struct input_dev *dev, unsigned int axis,
			  int min, int max, int fuzz, int flat)
{
	struct input_absinfo *absinfo;

	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	absinfo = &dev->absinfo[axis];
	absinfo->minimum = min;
	absinfo->maximum = max;
	absinfo->fuzz = fuzz;
	absinfo->flat = flat;

	dev->absbit[BIT_WORD(axis)] |= BIT_MASK(axis);
}
EXPORT_SYMBOL(input_set_abs_params);


/**
 * input_grab_device - grabs device for exclusive use
 * @handle: input handle that wants to own the device
 *
 * When a device is grabbed by an input handle all events generated by
 * the device are delivered only to this handle. Also events injected
 * by other input handles are ignored while device is grabbed.
 */
int input_grab_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int retval;

	retval = mutex_lock_interruptible(&dev->mutex);
	if (retval)
		return retval;

	if (dev->grab) {
		retval = -EBUSY;
		goto out;
	}

	rcu_assign_pointer(dev->grab, handle);
	synchronize_rcu();

 out:
	mutex_unlock(&dev->mutex);
	return retval;
}
EXPORT_SYMBOL(input_grab_device);

static void __input_release_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	if (dev->grab == handle) {
		rcu_assign_pointer(dev->grab, NULL);
		/* Make sure input_pass_event() notices that grab is gone */
		synchronize_rcu();

		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->open && handle->handler->start)
				handle->handler->start(handle);
	}
}

/**
 * input_release_device - release previously grabbed device
 * @handle: input handle that owns the device
 *
 * Releases previously grabbed device so that other input handles can
 * start receiving input events. Upon release all handlers attached
 * to the device have their start() method called so they have a change
 * to synchronize device state with the rest of the system.
 */
void input_release_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	mutex_lock(&dev->mutex);
	__input_release_device(handle);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL(input_release_device);

/**
 * input_open_device - open input device
 * @handle: handle through which device is being accessed
 *
 * This function should be called by input handlers when they
 * want to start receive events from given input device.
 */
int input_open_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int retval;

	retval = mutex_lock_interruptible(&dev->mutex);
	if (retval)
		return retval;

	if (dev->going_away) {
		retval = -ENODEV;
		goto out;
	}

	handle->open++;

	if (!dev->users++ && dev->open)
		retval = dev->open(dev);

	if (retval) {
		dev->users--;
		if (!--handle->open) {
			/*
			 * Make sure we are not delivering any more events
			 * through this handle
			 */
			synchronize_rcu();
		}
	}

 out:
	mutex_unlock(&dev->mutex);
	return retval;
}
EXPORT_SYMBOL(input_open_device);

int input_flush_device(struct input_handle *handle, struct file *file)
{
	struct input_dev *dev = handle->dev;
	int retval;

	retval = mutex_lock_interruptible(&dev->mutex);
	if (retval)
		return retval;

	if (dev->flush)
		retval = dev->flush(dev, file);

	mutex_unlock(&dev->mutex);
	return retval;
}
EXPORT_SYMBOL(input_flush_device);

/**
 * input_close_device - close input device
 * @handle: handle through which device is being accessed
 *
 * This function should be called by input handlers when they
 * want to stop receive events from given input device.
 */
void input_close_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	mutex_lock(&dev->mutex);

	__input_release_device(handle);

	if (!--dev->users && dev->close)
		dev->close(dev);

	if (!--handle->open) {
		/*
		 * synchronize_rcu() makes sure that input_pass_event()
		 * completed and that no more input events are delivered
		 * through this handle
		 */
		synchronize_rcu();
	}

	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL(input_close_device);

/*
 * Simulate keyup events for all keys that are marked as pressed.
 * The function must be called with dev->event_lock held.
 */
static void input_dev_release_keys(struct input_dev *dev)
{
	int code;

	if (is_event_supported(EV_KEY, dev->evbit, EV_MAX)) {
		for (code = 0; code <= KEY_MAX; code++) {
			if (is_event_supported(code, dev->keybit, KEY_MAX) &&
			    __test_and_clear_bit(code, dev->key)) {
				input_pass_event(dev, EV_KEY, code, 0);
			}
		}
		input_pass_event(dev, EV_SYN, SYN_REPORT, 1);
	}
}

/*
 * Prepare device for unregistering
 */
static void input_disconnect_device(struct input_dev *dev)
{
	struct input_handle *handle;

	/*
	 * Mark device as going away. Note that we take dev->mutex here
	 * not to protect access to dev->going_away but rather to ensure
	 * that there are no threads in the middle of input_open_device()
	 */
	mutex_lock(&dev->mutex);
	dev->going_away = true;
	mutex_unlock(&dev->mutex);

	spin_lock_irq(&dev->event_lock);

	/*
	 * Simulate keyup events for all pressed keys so that handlers
	 * are not left with "stuck" keys. The driver may continue
	 * generate events even after we done here but they will not
	 * reach any handlers.
	 */
	input_dev_release_keys(dev);

	list_for_each_entry(handle, &dev->h_list, d_node)
		handle->open = 0;

	spin_unlock_irq(&dev->event_lock);
}

/**
 * input_scancode_to_scalar() - converts scancode in &struct input_keymap_entry
 * @ke: keymap entry containing scancode to be converted.
 * @scancode: pointer to the location where converted scancode should
 *	be stored.
 *
 * This function is used to convert scancode stored in &struct keymap_entry
 * into scalar form understood by legacy keymap handling methods. These
 * methods expect scancodes to be represented as 'unsigned int'.
 */
int input_scancode_to_scalar(const struct input_keymap_entry *ke,
			     unsigned int *scancode)
{
	switch (ke->len) {
	case 1:
		*scancode = *((u8 *)ke->scancode);
		break;

	case 2:
		*scancode = *((u16 *)ke->scancode);
		break;

	case 4:
		*scancode = *((u32 *)ke->scancode);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(input_scancode_to_scalar);

/*
 * Those routines handle the default case where no [gs]etkeycode() is
 * defined. In this case, an array indexed by the scancode is used.
 */

static unsigned int input_fetch_keycode(struct input_dev *dev,
					unsigned int index)
{
	switch (dev->keycodesize) {
	case 1:
		return ((u8 *)dev->keycode)[index];

	case 2:
		return ((u16 *)dev->keycode)[index];

	default:
		return ((u32 *)dev->keycode)[index];
	}
}

static int input_default_getkeycode(struct input_dev *dev,
				    struct input_keymap_entry *ke)
{
	unsigned int index;
	int error;

	if (!dev->keycodesize)
		return -EINVAL;

	if (ke->flags & INPUT_KEYMAP_BY_INDEX)
		index = ke->index;
	else {
		error = input_scancode_to_scalar(ke, &index);
		if (error)
			return error;
	}

	if (index >= dev->keycodemax)
		return -EINVAL;

	ke->keycode = input_fetch_keycode(dev, index);
	ke->index = index;
	ke->len = sizeof(index);
	memcpy(ke->scancode, &index, sizeof(index));

	return 0;
}

static int input_default_setkeycode(struct input_dev *dev,
				    const struct input_keymap_entry *ke,
				    unsigned int *old_keycode)
{
	unsigned int index;
	int error;
	int i;

	if (!dev->keycodesize)
		return -EINVAL;

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
	} else {
		error = input_scancode_to_scalar(ke, &index);
		if (error)
			return error;
	}

	if (index >= dev->keycodemax)
		return -EINVAL;

	if (dev->keycodesize < sizeof(dev->keycode) &&
			(ke->keycode >> (dev->keycodesize * 8)))
		return -EINVAL;

	switch (dev->keycodesize) {
		case 1: {
			u8 *k = (u8 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
		case 2: {
			u16 *k = (u16 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
		default: {
			u32 *k = (u32 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
	}

	__clear_bit(*old_keycode, dev->keybit);
	__set_bit(ke->keycode, dev->keybit);

	for (i = 0; i < dev->keycodemax; i++) {
		if (input_fetch_keycode(dev, i) == *old_keycode) {
			__set_bit(*old_keycode, dev->keybit);
			break; /* Setting the bit twice is useless, so break */
		}
	}

	return 0;
}

/**
 * input_get_keycode - retrieve keycode currently mapped to a given scancode
 * @dev: input device which keymap is being queried
 * @ke: keymap entry
 *
 * This function should be called by anyone interested in retrieving current
 * keymap. Presently evdev handlers use it.
 */
int input_get_keycode(struct input_dev *dev, struct input_keymap_entry *ke)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (dev->getkeycode) {
		/*
		 * Support for legacy drivers, that don't implement the new
		 * ioctls
		 */
		u32 scancode = ke->index;

		memcpy(ke->scancode, &scancode, sizeof(scancode));
		ke->len = sizeof(scancode);
		retval = dev->getkeycode(dev, scancode, &ke->keycode);
	} else {
		retval = dev->getkeycode_new(dev, ke);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
	return retval;
}
EXPORT_SYMBOL(input_get_keycode);

/**
 * input_set_keycode - attribute a keycode to a given scancode
 * @dev: input device which keymap is being updated
 * @ke: new keymap entry
 *
 * This function should be called by anyone needing to update current
 * keymap. Presently keyboard and evdev handlers use it.
 */
int input_set_keycode(struct input_dev *dev,
		      const struct input_keymap_entry *ke)
{
	unsigned long flags;
	unsigned int old_keycode;
	int retval;

	if (ke->keycode > KEY_MAX)
		return -EINVAL;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (dev->setkeycode) {
		/*
		 * Support for legacy drivers, that don't implement the new
		 * ioctls
		 */
		unsigned int scancode;

		retval = input_scancode_to_scalar(ke, &scancode);
		if (retval)
			goto out;

		/*
		 * We need to know the old scancode, in order to generate a
		 * keyup effect, if the set operation happens successfully
		 */
		if (!dev->getkeycode) {
			retval = -EINVAL;
			goto out;
		}

		retval = dev->getkeycode(dev, scancode, &old_keycode);
		if (retval)
			goto out;

		retval = dev->setkeycode(dev, scancode, ke->keycode);
	} else {
		retval = dev->setkeycode_new(dev, ke, &old_keycode);
	}

	if (retval)
		goto out;

	/* Make sure KEY_RESERVED did not get enabled. */
	__clear_bit(KEY_RESERVED, dev->keybit);

	/*
	 * Simulate keyup event if keycode is not present
	 * in the keymap anymore
	 */
	if (test_bit(EV_KEY, dev->evbit) &&
	    !is_event_supported(old_keycode, dev->keybit, KEY_MAX) &&
	    __test_and_clear_bit(old_keycode, dev->key)) {

		input_pass_event(dev, EV_KEY, old_keycode, 0);
		if (dev->sync)
			input_pass_event(dev, EV_SYN, SYN_REPORT, 1);
	}

 out:
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return retval;
}
EXPORT_SYMBOL(input_set_keycode);

#define MATCH_BIT(bit, max) \
		for (i = 0; i < BITS_TO_LONGS(max); i++) \
			if ((id->bit[i] & dev->bit[i]) != id->bit[i]) \
				break; \
		if (i != BITS_TO_LONGS(max)) \
			continue;

static const struct input_device_id *input_match_device(struct input_handler *handler,
							struct input_dev *dev)
{
	const struct input_device_id *id;
	int i;

	for (id = handler->id_table; id->flags || id->driver_info; id++) {

		if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
			if (id->bustype != dev->id.bustype)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VENDOR)
			if (id->vendor != dev->id.vendor)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_PRODUCT)
			if (id->product != dev->id.product)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VERSION)
			if (id->version != dev->id.version)
				continue;

		MATCH_BIT(evbit,  EV_MAX);
		MATCH_BIT(keybit, KEY_MAX);
		MATCH_BIT(relbit, REL_MAX);
		MATCH_BIT(absbit, ABS_MAX);
		MATCH_BIT(mscbit, MSC_MAX);
		MATCH_BIT(ledbit, LED_MAX);
		MATCH_BIT(sndbit, SND_MAX);
		MATCH_BIT(ffbit,  FF_MAX);
		MATCH_BIT(swbit,  SW_MAX);

		if (!handler->match || handler->match(handler, dev))
			return id;
	}

	return NULL;
}

static int input_attach_handler(struct input_dev *dev, struct input_handler *handler)
{
	const struct input_device_id *id;
	int error;

	id = input_match_device(handler, dev);
	if (!id)
		return -ENODEV;

	error = handler->connect(handler, dev, id);
	if (error && error != -ENODEV)
		printk(KERN_ERR
			"input: failed to attach handler %s to device %s, "
			"error: %d\n",
			handler->name, kobject_name(&dev->dev.kobj), error);

	return error;
}

#ifdef CONFIG_COMPAT

static int input_bits_to_string(char *buf, int buf_size,
				unsigned long bits, bool skip_empty)
{
	int len = 0;

	if (INPUT_COMPAT_TEST) {
		u32 dword = bits >> 32;
		if (dword || !skip_empty)
			len += snprintf(buf, buf_size, "%x ", dword);

		dword = bits & 0xffffffffUL;
		if (dword || !skip_empty || len)
			len += snprintf(buf + len, max(buf_size - len, 0),
					"%x", dword);
	} else {
		if (bits || !skip_empty)
			len += snprintf(buf, buf_size, "%lx", bits);
	}

	return len;
}

#else /* !CONFIG_COMPAT */

static int input_bits_to_string(char *buf, int buf_size,
				unsigned long bits, bool skip_empty)
{
	return bits || !skip_empty ?
		snprintf(buf, buf_size, "%lx", bits) : 0;
}

#endif

#ifdef CONFIG_PROC_FS

static struct proc_dir_entry *proc_bus_input_dir;
static DECLARE_WAIT_QUEUE_HEAD(input_devices_poll_wait);
static int input_devices_state;

static inline void input_wakeup_procfs_readers(void)
{
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
}

static unsigned int input_proc_devices_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &input_devices_poll_wait, wait);
	if (file->f_version != input_devices_state) {
		file->f_version = input_devices_state;
		return POLLIN | POLLRDNORM;
	}

	return 0;
}

union input_seq_state {
	struct {
		unsigned short pos;
		bool mutex_acquired;
	};
	void *p;
};

static void *input_devices_seq_start(struct seq_file *seq, loff_t *pos)
{
	union input_seq_state *state = (union input_seq_state *)&seq->private;
	int error;

	/* We need to fit into seq->private pointer */
	BUILD_BUG_ON(sizeof(union input_seq_state) != sizeof(seq->private));

	error = mutex_lock_interruptible(&input_mutex);
	if (error) {
		state->mutex_acquired = false;
		return ERR_PTR(error);
	}

	state->mutex_acquired = true;

	return seq_list_start(&input_dev_list, *pos);
}

static void *input_devices_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &input_dev_list, pos);
}

static void input_seq_stop(struct seq_file *seq, void *v)
{
	union input_seq_state *state = (union input_seq_state *)&seq->private;

	if (state->mutex_acquired)
		mutex_unlock(&input_mutex);
}

static void input_seq_print_bitmap(struct seq_file *seq, const char *name,
				   unsigned long *bitmap, int max)
{
	int i;
	bool skip_empty = true;
	char buf[18];

	seq_printf(seq, "B: %s=", name);

	for (i = BITS_TO_LONGS(max) - 1; i >= 0; i--) {
		if (input_bits_to_string(buf, sizeof(buf),
					 bitmap[i], skip_empty)) {
			skip_empty = false;
			seq_printf(seq, "%s%s", buf, i > 0 ? " " : "");
		}
	}

	/*
	 * If no output was produced print a single 0.
	 */
	if (skip_empty)
		seq_puts(seq, "0");

	seq_putc(seq, '\n');
}

static int input_devices_seq_show(struct seq_file *seq, void *v)
{
	struct input_dev *dev = container_of(v, struct input_dev, node);
	const char *path = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
	struct input_handle *handle;

	seq_printf(seq, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
		   dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

	seq_printf(seq, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
	seq_printf(seq, "P: Phys=%s\n", dev->phys ? dev->phys : "");
	seq_printf(seq, "S: Sysfs=%s\n", path ? path : "");
	seq_printf(seq, "U: Uniq=%s\n", dev->uniq ? dev->uniq : "");
	seq_printf(seq, "H: Handlers=");

	list_for_each_entry(handle, &dev->h_list, d_node)
		seq_printf(seq, "%s ", handle->name);
	seq_putc(seq, '\n');

	input_seq_print_bitmap(seq, "EV", dev->evbit, EV_MAX);
	if (test_bit(EV_KEY, dev->evbit))
		input_seq_print_bitmap(seq, "KEY", dev->keybit, KEY_MAX);
	if (test_bit(EV_REL, dev->evbit))
		input_seq_print_bitmap(seq, "REL", dev->relbit, REL_MAX);
	if (test_bit(EV_ABS, dev->evbit))
		input_seq_print_bitmap(seq, "ABS", dev->absbit, ABS_MAX);
	if (test_bit(EV_MSC, dev->evbit))
		input_seq_print_bitmap(seq, "MSC", dev->mscbit, MSC_MAX);
	if (test_bit(EV_LED, dev->evbit))
		input_seq_print_bitmap(seq, "LED", dev->ledbit, LED_MAX);
	if (test_bit(EV_SND, dev->evbit))
		input_seq_print_bitmap(seq, "SND", dev->sndbit, SND_MAX);
	if (test_bit(EV_FF, dev->evbit))
		input_seq_print_bitmap(seq, "FF", dev->ffbit, FF_MAX);
	if (test_bit(EV_SW, dev->evbit))
		input_seq_print_bitmap(seq, "SW", dev->swbit, SW_MAX);

	seq_putc(seq, '\n');

	kfree(path);
	return 0;
}

static const struct seq_operations input_devices_seq_ops = {
	.start	= input_devices_seq_start,
	.next	= input_devices_seq_next,
	.stop	= input_seq_stop,
	.show	= input_devices_seq_show,
};

static int input_proc_devices_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &input_devices_seq_ops);
}

static const struct file_operations input_devices_fileops = {
	.owner		= THIS_MODULE,
	.open		= input_proc_devices_open,
	.poll		= input_proc_devices_poll,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void *input_handlers_seq_start(struct seq_file *seq, loff_t *pos)
{
	union input_seq_state *state = (union input_seq_state *)&seq->private;
	int error;

	/* We need to fit into seq->private pointer */
	BUILD_BUG_ON(sizeof(union input_seq_state) != sizeof(seq->private));

	error = mutex_lock_interruptible(&input_mutex);
	if (error) {
		state->mutex_acquired = false;
		return ERR_PTR(error);
	}

	state->mutex_acquired = true;
	state->pos = *pos;

	return seq_list_start(&input_handler_list, *pos);
}

static void *input_handlers_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	union input_seq_state *state = (union input_seq_state *)&seq->private;

	state->pos = *pos + 1;
	return seq_list_next(v, &input_handler_list, pos);
}

static int input_handlers_seq_show(struct seq_file *seq, void *v)
{
	struct input_handler *handler = container_of(v, struct input_handler, node);
	union input_seq_state *state = (union input_seq_state *)&seq->private;

	seq_printf(seq, "N: Number=%u Name=%s", state->pos, handler->name);
	if (handler->filter)
		seq_puts(seq, " (filter)");
	if (handler->fops)
		seq_printf(seq, " Minor=%d", handler->minor);
	seq_putc(seq, '\n');

	return 0;
}

static const struct seq_operations input_handlers_seq_ops = {
	.start	= input_handlers_seq_start,
	.next	= input_handlers_seq_next,
	.stop	= input_seq_stop,
	.show	= input_handlers_seq_show,
};

static int input_proc_handlers_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &input_handlers_seq_ops);
}

static const struct file_operations input_handlers_fileops = {
	.owner		= THIS_MODULE,
	.open		= input_proc_handlers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init input_proc_init(void)
{
	struct proc_dir_entry *entry;

	proc_bus_input_dir = proc_mkdir("bus/input", NULL);
	if (!proc_bus_input_dir)
		return -ENOMEM;

	entry = proc_create("devices", 0, proc_bus_input_dir,
			    &input_devices_fileops);
	if (!entry)
		goto fail1;

	entry = proc_create("handlers", 0, proc_bus_input_dir,
			    &input_handlers_fileops);
	if (!entry)
		goto fail2;

	return 0;

 fail2:	remove_proc_entry("devices", proc_bus_input_dir);
 fail1: remove_proc_entry("bus/input", NULL);
	return -ENOMEM;
}

static void input_proc_exit(void)
{
	remove_proc_entry("devices", proc_bus_input_dir);
	remove_proc_entry("handlers", proc_bus_input_dir);
	remove_proc_entry("bus/input", NULL);
}

#else /* !CONFIG_PROC_FS */
static inline void input_wakeup_procfs_readers(void) { }
static inline int input_proc_init(void) { return 0; }
static inline void input_proc_exit(void) { }
#endif

#define INPUT_DEV_STRING_ATTR_SHOW(name)				\
static ssize_t input_dev_show_##name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	struct input_dev *input_dev = to_input_dev(dev);		\
									\
	return scnprintf(buf, PAGE_SIZE, "%s\n",			\
			 input_dev->name ? input_dev->name : "");	\
}									\
static DEVICE_ATTR(name, S_IRUGO, input_dev_show_##name, NULL)

INPUT_DEV_STRING_ATTR_SHOW(name);
INPUT_DEV_STRING_ATTR_SHOW(phys);
INPUT_DEV_STRING_ATTR_SHOW(uniq);

static int input_print_modalias_bits(char *buf, int size,
				     char name, unsigned long *bm,
				     unsigned int min_bit, unsigned int max_bit)
{
	int len = 0, i;

	len += snprintf(buf, max(size, 0), "%c", name);
	for (i = min_bit; i < max_bit; i++)
		if (bm[BIT_WORD(i)] & BIT_MASK(i))
			len += snprintf(buf + len, max(size - len, 0), "%X,", i);
	return len;
}

static int input_print_modalias(char *buf, int size, struct input_dev *id,
				int add_cr)
{
	int len;

	len = snprintf(buf, max(size, 0),
		       "input:b%04Xv%04Xp%04Xe%04X-",
		       id->id.bustype, id->id.vendor,
		       id->id.product, id->id.version);

	len += input_print_modalias_bits(buf + len, size - len,
				'e', id->evbit, 0, EV_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'k', id->keybit, KEY_MIN_INTERESTING, KEY_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'r', id->relbit, 0, REL_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'a', id->absbit, 0, ABS_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'm', id->mscbit, 0, MSC_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'l', id->ledbit, 0, LED_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				's', id->sndbit, 0, SND_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'f', id->ffbit, 0, FF_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'w', id->swbit, 0, SW_MAX);

	if (add_cr)
		len += snprintf(buf + len, max(size - len, 0), "\n");

	return len;
}

static ssize_t input_dev_show_modalias(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct input_dev *id = to_input_dev(dev);
	ssize_t len;

	len = input_print_modalias(buf, PAGE_SIZE, id, 1);

	return min_t(int, len, PAGE_SIZE);
}
static DEVICE_ATTR(modalias, S_IRUGO, input_dev_show_modalias, NULL);

static struct attribute *input_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_phys.attr,
	&dev_attr_uniq.attr,
	&dev_attr_modalias.attr,
	NULL
};

static struct attribute_group input_dev_attr_group = {
	.attrs	= input_dev_attrs,
};

#define INPUT_DEV_ID_ATTR(name)						\
static ssize_t input_dev_show_id_##name(struct device *dev,		\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct input_dev *input_dev = to_input_dev(dev);		\
	return scnprintf(buf, PAGE_SIZE, "%04x\n", input_dev->id.name);	\
}									\
static DEVICE_ATTR(name, S_IRUGO, input_dev_show_id_##name, NULL)

INPUT_DEV_ID_ATTR(bustype);
INPUT_DEV_ID_ATTR(vendor);
INPUT_DEV_ID_ATTR(product);
INPUT_DEV_ID_ATTR(version);

static struct attribute *input_dev_id_attrs[] = {
	&dev_attr_bustype.attr,
	&dev_attr_vendor.attr,
	&dev_attr_product.attr,
	&dev_attr_version.attr,
	NULL
};

static struct attribute_group input_dev_id_attr_group = {
	.name	= "id",
	.attrs	= input_dev_id_attrs,
};

static int input_print_bitmap(char *buf, int buf_size, unsigned long *bitmap,
			      int max, int add_cr)
{
	int i;
	int len = 0;
	bool skip_empty = true;

	for (i = BITS_TO_LONGS(max) - 1; i >= 0; i--) {
		len += input_bits_to_string(buf + len, max(buf_size - len, 0),
					    bitmap[i], skip_empty);
		if (len) {
			skip_empty = false;
			if (i > 0)
				len += snprintf(buf + len, max(buf_size - len, 0), " ");
		}
	}

	/*
	 * If no output was produced print a single 0.
	 */
	if (len == 0)
		len = snprintf(buf, buf_size, "%d", 0);

	if (add_cr)
		len += snprintf(buf + len, max(buf_size - len, 0), "\n");

	return len;
}

#define INPUT_DEV_CAP_ATTR(ev, bm)					\
static ssize_t input_dev_show_cap_##bm(struct device *dev,		\
				       struct device_attribute *attr,	\
				       char *buf)			\
{									\
	struct input_dev *input_dev = to_input_dev(dev);		\
	int len = input_print_bitmap(buf, PAGE_SIZE,			\
				     input_dev->bm##bit, ev##_MAX,	\
				     true);				\
	return min_t(int, len, PAGE_SIZE);				\
}									\
static DEVICE_ATTR(bm, S_IRUGO, input_dev_show_cap_##bm, NULL)

INPUT_DEV_CAP_ATTR(EV, ev);
INPUT_DEV_CAP_ATTR(KEY, key);
INPUT_DEV_CAP_ATTR(REL, rel);
INPUT_DEV_CAP_ATTR(ABS, abs);
INPUT_DEV_CAP_ATTR(MSC, msc);
INPUT_DEV_CAP_ATTR(LED, led);
INPUT_DEV_CAP_ATTR(SND, snd);
INPUT_DEV_CAP_ATTR(FF, ff);
INPUT_DEV_CAP_ATTR(SW, sw);

static struct attribute *input_dev_caps_attrs[] = {
	&dev_attr_ev.attr,
	&dev_attr_key.attr,
	&dev_attr_rel.attr,
	&dev_attr_abs.attr,
	&dev_attr_msc.attr,
	&dev_attr_led.attr,
	&dev_attr_snd.attr,
	&dev_attr_ff.attr,
	&dev_attr_sw.attr,
	NULL
};

static struct attribute_group input_dev_caps_attr_group = {
	.name	= "capabilities",
	.attrs	= input_dev_caps_attrs,
};

static const struct attribute_group *input_dev_attr_groups[] = {
	&input_dev_attr_group,
	&input_dev_id_attr_group,
	&input_dev_caps_attr_group,
	NULL
};

static void input_dev_release(struct device *device)
{
	struct input_dev *dev = to_input_dev(device);

	input_ff_destroy(dev);
	input_mt_destroy_slots(dev);
	kfree(dev->absinfo);
	kfree(dev);

	module_put(THIS_MODULE);
}

/*
 * Input uevent interface - loading event handlers based on
 * device bitfields.
 */
static int input_add_uevent_bm_var(struct kobj_uevent_env *env,
				   const char *name, unsigned long *bitmap, int max)
{
	int len;

	if (add_uevent_var(env, "%s=", name))
		return -ENOMEM;

	len = input_print_bitmap(&env->buf[env->buflen - 1],
				 sizeof(env->buf) - env->buflen,
				 bitmap, max, false);
	if (len >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;

	env->buflen += len;
	return 0;
}

static int input_add_uevent_modalias_var(struct kobj_uevent_env *env,
					 struct input_dev *dev)
{
	int len;

	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	len = input_print_modalias(&env->buf[env->buflen - 1],
				   sizeof(env->buf) - env->buflen,
				   dev, 0);
	if (len >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;

	env->buflen += len;
	return 0;
}

#define INPUT_ADD_HOTPLUG_VAR(fmt, val...)				\
	do {								\
		int err = add_uevent_var(env, fmt, val);		\
		if (err)						\
			return err;					\
	} while (0)

#define INPUT_ADD_HOTPLUG_BM_VAR(name, bm, max)				\
	do {								\
		int err = input_add_uevent_bm_var(env, name, bm, max);	\
		if (err)						\
			return err;					\
	} while (0)

#define INPUT_ADD_HOTPLUG_MODALIAS_VAR(dev)				\
	do {								\
		int err = input_add_uevent_modalias_var(env, dev);	\
		if (err)						\
			return err;					\
	} while (0)

static int input_dev_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct input_dev *dev = to_input_dev(device);

	INPUT_ADD_HOTPLUG_VAR("PRODUCT=%x/%x/%x/%x",
				dev->id.bustype, dev->id.vendor,
				dev->id.product, dev->id.version);
	if (dev->name)
		INPUT_ADD_HOTPLUG_VAR("NAME=\"%s\"", dev->name);
	if (dev->phys)
		INPUT_ADD_HOTPLUG_VAR("PHYS=\"%s\"", dev->phys);
	if (dev->uniq)
		INPUT_ADD_HOTPLUG_VAR("UNIQ=\"%s\"", dev->uniq);

	INPUT_ADD_HOTPLUG_BM_VAR("EV=", dev->evbit, EV_MAX);
	if (test_bit(EV_KEY, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("KEY=", dev->keybit, KEY_MAX);
	if (test_bit(EV_REL, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("REL=", dev->relbit, REL_MAX);
	if (test_bit(EV_ABS, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("ABS=", dev->absbit, ABS_MAX);
	if (test_bit(EV_MSC, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("MSC=", dev->mscbit, MSC_MAX);
	if (test_bit(EV_LED, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("LED=", dev->ledbit, LED_MAX);
	if (test_bit(EV_SND, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("SND=", dev->sndbit, SND_MAX);
	if (test_bit(EV_FF, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("FF=", dev->ffbit, FF_MAX);
	if (test_bit(EV_SW, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("SW=", dev->swbit, SW_MAX);

	INPUT_ADD_HOTPLUG_MODALIAS_VAR(dev);

	return 0;
}

#define INPUT_DO_TOGGLE(dev, type, bits, on)				\
	do {								\
		int i;							\
		bool active;						\
									\
		if (!test_bit(EV_##type, dev->evbit))			\
			break;						\
									\
		for (i = 0; i < type##_MAX; i++) {			\
			if (!test_bit(i, dev->bits##bit))		\
				continue;				\
									\
			active = test_bit(i, dev->bits);		\
			if (!active && !on)				\
				continue;				\
									\
			dev->event(dev, EV_##type, i, on ? active : 0);	\
		}							\
	} while (0)

#ifdef CONFIG_PM
static void input_dev_reset(struct input_dev *dev, bool activate)
{
	if (!dev->event)
		return;

	INPUT_DO_TOGGLE(dev, LED, led, activate);
	INPUT_DO_TOGGLE(dev, SND, snd, activate);

	if (activate && test_bit(EV_REP, dev->evbit)) {
		dev->event(dev, EV_REP, REP_PERIOD, dev->rep[REP_PERIOD]);
		dev->event(dev, EV_REP, REP_DELAY, dev->rep[REP_DELAY]);
	}
}

static int input_dev_suspend(struct device *dev)
{
	struct input_dev *input_dev = to_input_dev(dev);

	mutex_lock(&input_dev->mutex);
	input_dev_reset(input_dev, false);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int input_dev_resume(struct device *dev)
{
	struct input_dev *input_dev = to_input_dev(dev);

	mutex_lock(&input_dev->mutex);
	input_dev_reset(input_dev, true);

	/*
	 * Keys that have been pressed at suspend time are unlikely
	 * to be still pressed when we resume.
	 */
	spin_lock_irq(&input_dev->event_lock);
	input_dev_release_keys(input_dev);
	spin_unlock_irq(&input_dev->event_lock);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static const struct dev_pm_ops input_dev_pm_ops = {
	.suspend	= input_dev_suspend,
	.resume		= input_dev_resume,
	.poweroff	= input_dev_suspend,
	.restore	= input_dev_resume,
};
#endif /* CONFIG_PM */

static struct device_type input_dev_type = {
	.groups		= input_dev_attr_groups,
	.release	= input_dev_release,
	.uevent		= input_dev_uevent,
#ifdef CONFIG_PM
	.pm		= &input_dev_pm_ops,
#endif
};

static char *input_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "input/%s", dev_name(dev));
}

struct class input_class = {
	.name		= "input",
	.devnode	= input_devnode,
};
EXPORT_SYMBOL_GPL(input_class);

/**
 * input_allocate_device - allocate memory for new input device
 *
 * Returns prepared struct input_dev or NULL.
 *
 * NOTE: Use input_free_device() to free devices that have not been
 * registered; input_unregister_device() should be used for already
 * registered devices.
 */
struct input_dev *input_allocate_device(void)
{
	struct input_dev *dev;

	dev = kzalloc(sizeof(struct input_dev), GFP_KERNEL);
	if (dev) {
		dev->dev.type = &input_dev_type;
		dev->dev.class = &input_class;
		device_initialize(&dev->dev);
		mutex_init(&dev->mutex);
		spin_lock_init(&dev->event_lock);
		INIT_LIST_HEAD(&dev->h_list);
		INIT_LIST_HEAD(&dev->node);

		__module_get(THIS_MODULE);
	}

	return dev;
}
EXPORT_SYMBOL(input_allocate_device);

/**
 * input_free_device - free memory occupied by input_dev structure
 * @dev: input device to free
 *
 * This function should only be used if input_register_device()
 * was not called yet or if it failed. Once device was registered
 * use input_unregister_device() and memory will be freed once last
 * reference to the device is dropped.
 *
 * Device should be allocated by input_allocate_device().
 *
 * NOTE: If there are references to the input device then memory
 * will not be freed until last reference is dropped.
 */
void input_free_device(struct input_dev *dev)
{
	if (dev)
		input_put_device(dev);
}
EXPORT_SYMBOL(input_free_device);

/**
 * input_mt_create_slots() - create MT input slots
 * @dev: input device supporting MT events and finger tracking
 * @num_slots: number of slots used by the device
 *
 * This function allocates all necessary memory for MT slot handling in the
 * input device, and adds ABS_MT_SLOT to the device capabilities. All slots
 * are initially marked as unused by setting ABS_MT_TRACKING_ID to -1.
 */
int input_mt_create_slots(struct input_dev *dev, unsigned int num_slots)
{
	int i;

	if (!num_slots)
		return 0;

	dev->mt = kcalloc(num_slots, sizeof(struct input_mt_slot), GFP_KERNEL);
	if (!dev->mt)
		return -ENOMEM;

	dev->mtsize = num_slots;
	input_set_abs_params(dev, ABS_MT_SLOT, 0, num_slots - 1, 0, 0);

	/* Mark slots as 'unused' */
	for (i = 0; i < num_slots; i++)
		dev->mt[i].abs[ABS_MT_TRACKING_ID - ABS_MT_FIRST] = -1;

	return 0;
}
EXPORT_SYMBOL(input_mt_create_slots);

/**
 * input_mt_destroy_slots() - frees the MT slots of the input device
 * @dev: input device with allocated MT slots
 *
 * This function is only needed in error path as the input core will
 * automatically free the MT slots when the device is destroyed.
 */
void input_mt_destroy_slots(struct input_dev *dev)
{
	kfree(dev->mt);
	dev->mt = NULL;
	dev->mtsize = 0;
}
EXPORT_SYMBOL(input_mt_destroy_slots);

/**
 * input_set_capability - mark device as capable of a certain event
 * @dev: device that is capable of emitting or accepting event
 * @type: type of the event (EV_KEY, EV_REL, etc...)
 * @code: event code
 *
 * In addition to setting up corresponding bit in appropriate capability
 * bitmap the function also adjusts dev->evbit.
 */
void input_set_capability(struct input_dev *dev, unsigned int type, unsigned int code)
{
	switch (type) {
	case EV_KEY:
		__set_bit(code, dev->keybit);
		break;

	case EV_REL:
		__set_bit(code, dev->relbit);
		break;

	case EV_ABS:
		__set_bit(code, dev->absbit);
		break;

	case EV_MSC:
		__set_bit(code, dev->mscbit);
		break;

	case EV_SW:
		__set_bit(code, dev->swbit);
		break;

	case EV_LED:
		__set_bit(code, dev->ledbit);
		break;

	case EV_SND:
		__set_bit(code, dev->sndbit);
		break;

	case EV_FF:
		__set_bit(code, dev->ffbit);
		break;

	case EV_PWR:
		/* do nothing */
		break;

	default:
		printk(KERN_ERR
			"input_set_capability: unknown type %u (code %u)\n",
			type, code);
		dump_stack();
		return;
	}

	__set_bit(type, dev->evbit);
}
EXPORT_SYMBOL(input_set_capability);

#define INPUT_CLEANSE_BITMASK(dev, type, bits)				\
	do {								\
		if (!test_bit(EV_##type, dev->evbit))			\
			memset(dev->bits##bit, 0,			\
				sizeof(dev->bits##bit));		\
	} while (0)

static void input_cleanse_bitmasks(struct input_dev *dev)
{
	INPUT_CLEANSE_BITMASK(dev, KEY, key);
	INPUT_CLEANSE_BITMASK(dev, REL, rel);
	INPUT_CLEANSE_BITMASK(dev, ABS, abs);
	INPUT_CLEANSE_BITMASK(dev, MSC, msc);
	INPUT_CLEANSE_BITMASK(dev, LED, led);
	INPUT_CLEANSE_BITMASK(dev, SND, snd);
	INPUT_CLEANSE_BITMASK(dev, FF, ff);
	INPUT_CLEANSE_BITMASK(dev, SW, sw);
}

/**
 * input_register_device - register device with input core
 * @dev: device to be registered
 *
 * This function registers device with input core. The device must be
 * allocated with input_allocate_device() and all it's capabilities
 * set up before registering.
 * If function fails the device must be freed with input_free_device().
 * Once device has been successfully registered it can be unregistered
 * with input_unregister_device(); input_free_device() should not be
 * called in this case.
 */
int input_register_device(struct input_dev *dev)
{
	static atomic_t input_no = ATOMIC_INIT(0);
	struct input_handler *handler;
	const char *path;
	int error;

	/* Every input device generates EV_SYN/SYN_REPORT events. */
	__set_bit(EV_SYN, dev->evbit);

	/* KEY_RESERVED is not supposed to be transmitted to userspace. */
	__clear_bit(KEY_RESERVED, dev->keybit);

	/* Make sure that bitmasks not mentioned in dev->evbit are clean. */
	input_cleanse_bitmasks(dev);

	/*
	 * If delay and period are pre-set by the driver, then autorepeating
	 * is handled by the driver itself and we don't do it in input.c.
	 */
	init_timer(&dev->timer);
	if (!dev->rep[REP_DELAY] && !dev->rep[REP_PERIOD]) {
		dev->timer.data = (long) dev;
		dev->timer.function = input_repeat_key;
		dev->rep[REP_DELAY] = 250;
		dev->rep[REP_PERIOD] = 33;
	}

	if (!dev->getkeycode && !dev->getkeycode_new)
		dev->getkeycode_new = input_default_getkeycode;

	if (!dev->setkeycode && !dev->setkeycode_new)
		dev->setkeycode_new = input_default_setkeycode;

	dev_set_name(&dev->dev, "input%ld",
		     (unsigned long) atomic_inc_return(&input_no) - 1);

	error = device_add(&dev->dev);
	if (error)
		return error;

	path = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
	printk(KERN_INFO "input: %s as %s\n",
		dev->name ? dev->name : "Unspecified device", path ? path : "N/A");
	kfree(path);

	error = mutex_lock_interruptible(&input_mutex);
	if (error) {
		device_del(&dev->dev);
		return error;
	}

	list_add_tail(&dev->node, &input_dev_list);

	list_for_each_entry(handler, &input_handler_list, node)
		input_attach_handler(dev, handler);

	input_wakeup_procfs_readers();

	mutex_unlock(&input_mutex);

	return 0;
}
EXPORT_SYMBOL(input_register_device);

/**
 * input_unregister_device - unregister previously registered device
 * @dev: device to be unregistered
 *
 * This function unregisters an input device. Once device is unregistered
 * the caller should not try to access it as it may get freed at any moment.
 */
void input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle, *next;

	input_disconnect_device(dev);

	mutex_lock(&input_mutex);

	list_for_each_entry_safe(handle, next, &dev->h_list, d_node)
		handle->handler->disconnect(handle);
	WARN_ON(!list_empty(&dev->h_list));

	del_timer_sync(&dev->timer);
	list_del_init(&dev->node);

	input_wakeup_procfs_readers();

	mutex_unlock(&input_mutex);

	device_unregister(&dev->dev);
}
EXPORT_SYMBOL(input_unregister_device);

/**
 * input_register_handler - register a new input handler
 * @handler: handler to be registered
 *
 * This function registers a new input handler (interface) for input
 * devices in the system and attaches it to all input devices that
 * are compatible with the handler.
 */
int input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	int retval;

	retval = mutex_lock_interruptible(&input_mutex);
	if (retval)
		return retval;

	INIT_LIST_HEAD(&handler->h_list);

	if (handler->fops != NULL) {
		if (input_table[handler->minor >> 5]) {
			retval = -EBUSY;
			goto out;
		}
		input_table[handler->minor >> 5] = handler;
	}

	list_add_tail(&handler->node, &input_handler_list);

	list_for_each_entry(dev, &input_dev_list, node)
		input_attach_handler(dev, handler);

	input_wakeup_procfs_readers();

 out:
	mutex_unlock(&input_mutex);
	return retval;
}
EXPORT_SYMBOL(input_register_handler);

/**
 * input_unregister_handler - unregisters an input handler
 * @handler: handler to be unregistered
 *
 * This function disconnects a handler from its input devices and
 * removes it from lists of known handlers.
 */
void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle, *next;

	mutex_lock(&input_mutex);

	list_for_each_entry_safe(handle, next, &handler->h_list, h_node)
		handler->disconnect(handle);
	WARN_ON(!list_empty(&handler->h_list));

	list_del_init(&handler->node);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;

	input_wakeup_procfs_readers();

	mutex_unlock(&input_mutex);
}
EXPORT_SYMBOL(input_unregister_handler);

/**
 * input_handler_for_each_handle - handle iterator
 * @handler: input handler to iterate
 * @data: data for the callback
 * @fn: function to be called for each handle
 *
 * Iterate over @bus's list of devices, and call @fn for each, passing
 * it @data and stop when @fn returns a non-zero value. The function is
 * using RCU to traverse the list and therefore may be usind in atonic
 * contexts. The @fn callback is invoked from RCU critical section and
 * thus must not sleep.
 */
int input_handler_for_each_handle(struct input_handler *handler, void *data,
				  int (*fn)(struct input_handle *, void *))
{
	struct input_handle *handle;
	int retval = 0;

	rcu_read_lock();

	list_for_each_entry_rcu(handle, &handler->h_list, h_node) {
		retval = fn(handle, data);
		if (retval)
			break;
	}

	rcu_read_unlock();

	return retval;
}
EXPORT_SYMBOL(input_handler_for_each_handle);

/**
 * input_register_handle - register a new input handle
 * @handle: handle to register
 *
 * This function puts a new input handle onto device's
 * and handler's lists so that events can flow through
 * it once it is opened using input_open_device().
 *
 * This function is supposed to be called from handler's
 * connect() method.
 */
int input_register_handle(struct input_handle *handle)
{
	struct input_handler *handler = handle->handler;
	struct input_dev *dev = handle->dev;
	int error;

	/*
	 * We take dev->mutex here to prevent race with
	 * input_release_device().
	 */
	error = mutex_lock_interruptible(&dev->mutex);
	if (error)
		return error;

	/*
	 * Filters go to the head of the list, normal handlers
	 * to the tail.
	 */
	if (handler->filter)
		list_add_rcu(&handle->d_node, &dev->h_list);
	else
		list_add_tail_rcu(&handle->d_node, &dev->h_list);

	mutex_unlock(&dev->mutex);

	/*
	 * Since we are supposed to be called from ->connect()
	 * which is mutually exclusive with ->disconnect()
	 * we can't be racing with input_unregister_handle()
	 * and so separate lock is not needed here.
	 */
	list_add_tail_rcu(&handle->h_node, &handler->h_list);

	if (handler->start)
		handler->start(handle);

	return 0;
}
EXPORT_SYMBOL(input_register_handle);

/**
 * input_unregister_handle - unregister an input handle
 * @handle: handle to unregister
 *
 * This function removes input handle from device's
 * and handler's lists.
 *
 * This function is supposed to be called from handler's
 * disconnect() method.
 */
void input_unregister_handle(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	list_del_rcu(&handle->h_node);

	/*
	 * Take dev->mutex to prevent race with input_release_device().
	 */
	mutex_lock(&dev->mutex);
	list_del_rcu(&handle->d_node);
	mutex_unlock(&dev->mutex);

	synchronize_rcu();
}
EXPORT_SYMBOL(input_unregister_handle);

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler;
	const struct file_operations *old_fops, *new_fops = NULL;
	int err;

	err = mutex_lock_interruptible(&input_mutex);
	if (err)
		return err;

	/* No load-on-demand here? */
	handler = input_table[iminor(inode) >> 5];
	if (handler)
		new_fops = fops_get(handler->fops);

	mutex_unlock(&input_mutex);

	/*
	 * That's _really_ odd. Usually NULL ->open means "nothing special",
	 * not "no device". Oh, well...
	 */
	if (!new_fops || !new_fops->open) {
		fops_put(new_fops);
		err = -ENODEV;
		goto out;
	}

	old_fops = file->f_op;
	file->f_op = new_fops;

	err = new_fops->open(inode, file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
out:
	return err;
}

static const struct file_operations input_fops = {
	.owner = THIS_MODULE,
	.open = input_open_file,
	.llseek = noop_llseek,
};

static int __init input_init(void)
{
	int err;

	err = class_register(&input_class);
	if (err) {
		printk(KERN_ERR "input: unable to register input_dev class\n");
		return err;
	}

	err = input_proc_init();
	if (err)
		goto fail1;

	err = register_chrdev(INPUT_MAJOR, "input", &input_fops);
	if (err) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		goto fail2;
	}

	return 0;

 fail2:	input_proc_exit();
 fail1:	class_unregister(&input_class);
	return err;
}

static void __exit input_exit(void)
{
	input_proc_exit();
	unregister_chrdev(INPUT_MAJOR, "input");
	class_unregister(&input_class);
}

subsys_initcall(input_init);
module_exit(input_exit);
