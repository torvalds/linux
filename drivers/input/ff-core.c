// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Force feedback support for Linux input subsystem
 *
 *  Copyright (c) 2006 Anssi Hannula <anssi.hannula@gmail.com>
 *  Copyright (c) 2006 Dmitry Torokhov <dtor@mail.ru>
 */

/* #define DEBUG */

#include <linux/export.h>
#include <linux/input.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/slab.h>

/*
 * Check that the effect_id is a valid effect and whether the user
 * is the owner
 */
static int check_effect_access(struct ff_device *ff, int effect_id,
				struct file *file)
{
	if (effect_id < 0 || effect_id >= ff->max_effects ||
	    !ff->effect_owners[effect_id])
		return -EINVAL;

	if (file && ff->effect_owners[effect_id] != file)
		return -EACCES;

	return 0;
}

/*
 * Checks whether 2 effects can be combined together
 */
static inline int check_effects_compatible(struct ff_effect *e1,
					   struct ff_effect *e2)
{
	return e1->type == e2->type &&
	       (e1->type != FF_PERIODIC ||
		e1->u.periodic.waveform == e2->u.periodic.waveform);
}

/*
 * Convert an effect into compatible one
 */
static int compat_effect(struct ff_device *ff, struct ff_effect *effect)
{
	int magnitude;

	switch (effect->type) {
	case FF_RUMBLE:
		if (!test_bit(FF_PERIODIC, ff->ffbit))
			return -EINVAL;

		/*
		 * calculate magnitude of sine wave as average of rumble's
		 * 2/3 of strong magnitude and 1/3 of weak magnitude
		 */
		magnitude = effect->u.rumble.strong_magnitude / 3 +
			    effect->u.rumble.weak_magnitude / 6;

		effect->type = FF_PERIODIC;
		effect->u.periodic.waveform = FF_SINE;
		effect->u.periodic.period = 50;
		effect->u.periodic.magnitude = magnitude;
		effect->u.periodic.offset = 0;
		effect->u.periodic.phase = 0;
		effect->u.periodic.envelope.attack_length = 0;
		effect->u.periodic.envelope.attack_level = 0;
		effect->u.periodic.envelope.fade_length = 0;
		effect->u.periodic.envelope.fade_level = 0;

		return 0;

	default:
		/* Let driver handle conversion */
		return 0;
	}
}

/**
 * input_ff_upload() - upload effect into force-feedback device
 * @dev: input device
 * @effect: effect to be uploaded
 * @file: owner of the effect
 */
int input_ff_upload(struct input_dev *dev, struct ff_effect *effect,
		    struct file *file)
{
	struct ff_device *ff = dev->ff;
	struct ff_effect *old;
	int error;
	int id;

	if (!test_bit(EV_FF, dev->evbit))
		return -ENOSYS;

	if (effect->type < FF_EFFECT_MIN || effect->type > FF_EFFECT_MAX ||
	    !test_bit(effect->type, dev->ffbit)) {
		dev_dbg(&dev->dev, "invalid or not supported effect type in upload\n");
		return -EINVAL;
	}

	if (effect->type == FF_PERIODIC &&
	    (effect->u.periodic.waveform < FF_WAVEFORM_MIN ||
	     effect->u.periodic.waveform > FF_WAVEFORM_MAX ||
	     !test_bit(effect->u.periodic.waveform, dev->ffbit))) {
		dev_dbg(&dev->dev, "invalid or not supported wave form in upload\n");
		return -EINVAL;
	}

	if (!test_bit(effect->type, ff->ffbit)) {
		error = compat_effect(ff, effect);
		if (error)
			return error;
	}

	guard(mutex)(&ff->mutex);

	if (effect->id == -1) {
		for (id = 0; id < ff->max_effects; id++)
			if (!ff->effect_owners[id])
				break;

		if (id >= ff->max_effects)
			return -ENOSPC;

		effect->id = id;
		old = NULL;

	} else {
		id = effect->id;

		error = check_effect_access(ff, id, file);
		if (error)
			return error;

		old = &ff->effects[id];

		if (!check_effects_compatible(effect, old))
			return -EINVAL;
	}

	error = ff->upload(dev, effect, old);
	if (error)
		return error;

	scoped_guard(spinlock_irq, &dev->event_lock) {
		ff->effects[id] = *effect;
		ff->effect_owners[id] = file;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(input_ff_upload);

/*
 * Erases the effect if the requester is also the effect owner. The mutex
 * should already be locked before calling this function.
 */
static int erase_effect(struct input_dev *dev, int effect_id,
			struct file *file)
{
	struct ff_device *ff = dev->ff;
	int error;

	error = check_effect_access(ff, effect_id, file);
	if (error)
		return error;

	scoped_guard(spinlock_irq, &dev->event_lock) {
		ff->playback(dev, effect_id, 0);
		ff->effect_owners[effect_id] = NULL;
	}

	if (ff->erase) {
		error = ff->erase(dev, effect_id);
		if (error) {
			scoped_guard(spinlock_irq, &dev->event_lock)
				ff->effect_owners[effect_id] = file;

			return error;
		}
	}

	return 0;
}

/**
 * input_ff_erase - erase a force-feedback effect from device
 * @dev: input device to erase effect from
 * @effect_id: id of the effect to be erased
 * @file: purported owner of the request
 *
 * This function erases a force-feedback effect from specified device.
 * The effect will only be erased if it was uploaded through the same
 * file handle that is requesting erase.
 */
int input_ff_erase(struct input_dev *dev, int effect_id, struct file *file)
{
	struct ff_device *ff = dev->ff;

	if (!test_bit(EV_FF, dev->evbit))
		return -ENOSYS;

	guard(mutex)(&ff->mutex);
	return erase_effect(dev, effect_id, file);
}
EXPORT_SYMBOL_GPL(input_ff_erase);

/*
 * input_ff_flush - erase all effects owned by a file handle
 * @dev: input device to erase effect from
 * @file: purported owner of the effects
 *
 * This function erases all force-feedback effects associated with
 * the given owner from specified device. Note that @file may be %NULL,
 * in which case all effects will be erased.
 */
int input_ff_flush(struct input_dev *dev, struct file *file)
{
	struct ff_device *ff = dev->ff;
	int i;

	dev_dbg(&dev->dev, "flushing now\n");

	guard(mutex)(&ff->mutex);

	for (i = 0; i < ff->max_effects; i++)
		erase_effect(dev, i, file);

	return 0;
}
EXPORT_SYMBOL_GPL(input_ff_flush);

/**
 * input_ff_event() - generic handler for force-feedback events
 * @dev: input device to send the effect to
 * @type: event type (anything but EV_FF is ignored)
 * @code: event code
 * @value: event value
 */
int input_ff_event(struct input_dev *dev, unsigned int type,
		   unsigned int code, int value)
{
	struct ff_device *ff = dev->ff;

	if (type != EV_FF)
		return 0;

	switch (code) {
	case FF_GAIN:
		if (!test_bit(FF_GAIN, dev->ffbit) || value > 0xffffU)
			break;

		ff->set_gain(dev, value);
		break;

	case FF_AUTOCENTER:
		if (!test_bit(FF_AUTOCENTER, dev->ffbit) || value > 0xffffU)
			break;

		ff->set_autocenter(dev, value);
		break;

	default:
		if (check_effect_access(ff, code, NULL) == 0)
			ff->playback(dev, code, value);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(input_ff_event);

/**
 * input_ff_create() - create force-feedback device
 * @dev: input device supporting force-feedback
 * @max_effects: maximum number of effects supported by the device
 *
 * This function allocates all necessary memory for a force feedback
 * portion of an input device and installs all default handlers.
 * @dev->ffbit should be already set up before calling this function.
 * Once ff device is created you need to setup its upload, erase,
 * playback and other handlers before registering input device
 */
int input_ff_create(struct input_dev *dev, unsigned int max_effects)
{
	int i;

	if (!max_effects) {
		dev_err(&dev->dev, "cannot allocate device without any effects\n");
		return -EINVAL;
	}

	if (max_effects > FF_MAX_EFFECTS) {
		dev_err(&dev->dev, "cannot allocate more than FF_MAX_EFFECTS effects\n");
		return -EINVAL;
	}

	struct ff_device *ff __free(kfree) =
		kzalloc(struct_size(ff, effect_owners, max_effects),
			GFP_KERNEL);
	if (!ff)
		return -ENOMEM;

	ff->effects = kcalloc(max_effects, sizeof(*ff->effects), GFP_KERNEL);
	if (!ff->effects)
		return -ENOMEM;

	ff->max_effects = max_effects;
	mutex_init(&ff->mutex);

	dev->flush = input_ff_flush;
	dev->event = input_ff_event;
	__set_bit(EV_FF, dev->evbit);

	/* Copy "true" bits into ff device bitmap */
	for_each_set_bit(i, dev->ffbit, FF_CNT)
		__set_bit(i, ff->ffbit);

	/* we can emulate RUMBLE with periodic effects */
	if (test_bit(FF_PERIODIC, ff->ffbit))
		__set_bit(FF_RUMBLE, dev->ffbit);

	dev->ff = no_free_ptr(ff);

	return 0;
}
EXPORT_SYMBOL_GPL(input_ff_create);

/**
 * input_ff_destroy() - frees force feedback portion of input device
 * @dev: input device supporting force feedback
 *
 * This function is only needed in error path as input core will
 * automatically free force feedback structures when device is
 * destroyed.
 */
void input_ff_destroy(struct input_dev *dev)
{
	struct ff_device *ff = dev->ff;

	__clear_bit(EV_FF, dev->evbit);
	if (ff) {
		if (ff->destroy)
			ff->destroy(ff);
		kfree(ff->private);
		kfree(ff->effects);
		kfree(ff);
		dev->ff = NULL;
	}
}
EXPORT_SYMBOL_GPL(input_ff_destroy);
