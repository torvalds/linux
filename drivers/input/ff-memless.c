/*
 *  Force feedback support for memoryless devices
 *
 *  Copyright (c) 2006 Anssi Hannula <anssi.hannula@gmail.com>
 *  Copyright (c) 2006 Dmitry Torokhov <dtor@mail.ru>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define DEBUG */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/fixp-arith.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@gmail.com>");
MODULE_DESCRIPTION("Force feedback support for memoryless devices");

/* Number of effects handled with memoryless devices */
#define FF_MEMLESS_EFFECTS	16

/* Envelope update interval in ms */
#define FF_ENVELOPE_INTERVAL	50

#define FF_EFFECT_STARTED	0
#define FF_EFFECT_PLAYING	1
#define FF_EFFECT_ABORTING	2

struct ml_effect_state {
	struct ff_effect *effect;
	unsigned long flags;	/* effect state (STARTED, PLAYING, etc) */
	int count;		/* loop count of the effect */
	unsigned long play_at;	/* start time */
	unsigned long stop_at;	/* stop time */
	unsigned long adj_at;	/* last time the effect was sent */
};

struct ml_device {
	void *private;
	struct ml_effect_state states[FF_MEMLESS_EFFECTS];
	int gain;
	struct timer_list timer;
	struct input_dev *dev;

	int (*play_effect)(struct input_dev *dev, void *data,
			   struct ff_effect *effect);
};

static const struct ff_envelope *get_envelope(const struct ff_effect *effect)
{
	static const struct ff_envelope empty_envelope;

	switch (effect->type) {
	case FF_PERIODIC:
		return &effect->u.periodic.envelope;

	case FF_CONSTANT:
		return &effect->u.constant.envelope;

	default:
		return &empty_envelope;
	}
}

/*
 * Check for the next time envelope requires an update on memoryless devices
 */
static unsigned long calculate_next_time(struct ml_effect_state *state)
{
	const struct ff_envelope *envelope = get_envelope(state->effect);
	unsigned long attack_stop, fade_start, next_fade;

	if (envelope->attack_length) {
		attack_stop = state->play_at +
			msecs_to_jiffies(envelope->attack_length);
		if (time_before(state->adj_at, attack_stop))
			return state->adj_at +
					msecs_to_jiffies(FF_ENVELOPE_INTERVAL);
	}

	if (state->effect->replay.length) {
		if (envelope->fade_length) {
			/* check when fading should start */
			fade_start = state->stop_at -
					msecs_to_jiffies(envelope->fade_length);

			if (time_before(state->adj_at, fade_start))
				return fade_start;

			/* already fading, advance to next checkpoint */
			next_fade = state->adj_at +
					msecs_to_jiffies(FF_ENVELOPE_INTERVAL);
			if (time_before(next_fade, state->stop_at))
				return next_fade;
		}

		return state->stop_at;
	}

	return state->play_at;
}

static void ml_schedule_timer(struct ml_device *ml)
{
	struct ml_effect_state *state;
	unsigned long now = jiffies;
	unsigned long earliest = 0;
	unsigned long next_at;
	int events = 0;
	int i;

	pr_debug("calculating next timer\n");

	for (i = 0; i < FF_MEMLESS_EFFECTS; i++) {

		state = &ml->states[i];

		if (!test_bit(FF_EFFECT_STARTED, &state->flags))
			continue;

		if (test_bit(FF_EFFECT_PLAYING, &state->flags))
			next_at = calculate_next_time(state);
		else
			next_at = state->play_at;

		if (time_before_eq(now, next_at) &&
		    (++events == 1 || time_before(next_at, earliest)))
			earliest = next_at;
	}

	if (!events) {
		pr_debug("no actions\n");
		del_timer(&ml->timer);
	} else {
		pr_debug("timer set\n");
		mod_timer(&ml->timer, earliest);
	}
}

/*
 * Apply an envelope to a value
 */
static int apply_envelope(struct ml_effect_state *state, int value,
			  struct ff_envelope *envelope)
{
	struct ff_effect *effect = state->effect;
	unsigned long now = jiffies;
	int time_from_level;
	int time_of_envelope;
	int envelope_level;
	int difference;

	if (envelope->attack_length &&
	    time_before(now,
			state->play_at + msecs_to_jiffies(envelope->attack_length))) {
		pr_debug("value = 0x%x, attack_level = 0x%x\n",
			 value, envelope->attack_level);
		time_from_level = jiffies_to_msecs(now - state->play_at);
		time_of_envelope = envelope->attack_length;
		envelope_level = min_t(u16, envelope->attack_level, 0x7fff);

	} else if (envelope->fade_length && effect->replay.length &&
		   time_after(now,
			      state->stop_at - msecs_to_jiffies(envelope->fade_length)) &&
		   time_before(now, state->stop_at)) {
		time_from_level = jiffies_to_msecs(state->stop_at - now);
		time_of_envelope = envelope->fade_length;
		envelope_level = min_t(u16, envelope->fade_level, 0x7fff);
	} else
		return value;

	difference = abs(value) - envelope_level;

	pr_debug("difference = %d\n", difference);
	pr_debug("time_from_level = 0x%x\n", time_from_level);
	pr_debug("time_of_envelope = 0x%x\n", time_of_envelope);

	difference = difference * time_from_level / time_of_envelope;

	pr_debug("difference = %d\n", difference);

	return value < 0 ?
		-(difference + envelope_level) : (difference + envelope_level);
}

/*
 * Return the type the effect has to be converted into (memless devices)
 */
static int get_compatible_type(struct ff_device *ff, int effect_type)
{

	if (test_bit(effect_type, ff->ffbit))
		return effect_type;

	if (effect_type == FF_PERIODIC && test_bit(FF_RUMBLE, ff->ffbit))
		return FF_RUMBLE;

	pr_err("invalid type in get_compatible_type()\n");

	return 0;
}

/*
 * Only left/right direction should be used (under/over 0x8000) for
 * forward/reverse motor direction (to keep calculation fast & simple).
 */
static u16 ml_calculate_direction(u16 direction, u16 force,
				  u16 new_direction, u16 new_force)
{
	if (!force)
		return new_direction;
	if (!new_force)
		return direction;
	return (((u32)(direction >> 1) * force +
		 (new_direction >> 1) * new_force) /
		(force + new_force)) << 1;
}

#define FRAC_N 8
static inline s16 fixp_new16(s16 a)
{
	return ((s32)a) >> (16 - FRAC_N);
}

static inline s16 fixp_mult(s16 a, s16 b)
{
	a = ((s32)a * 0x100) / 0x7fff;
	return ((s32)(a * b)) >> FRAC_N;
}

/*
 * Combine two effects and apply gain.
 */
static void ml_combine_effects(struct ff_effect *effect,
			       struct ml_effect_state *state,
			       int gain)
{
	struct ff_effect *new = state->effect;
	unsigned int strong, weak, i;
	int x, y;
	s16 level;

	switch (new->type) {
	case FF_CONSTANT:
		i = new->direction * 360 / 0xffff;
		level = fixp_new16(apply_envelope(state,
					new->u.constant.level,
					&new->u.constant.envelope));
		x = fixp_mult(fixp_sin16(i), level) * gain / 0xffff;
		y = fixp_mult(-fixp_cos16(i), level) * gain / 0xffff;
		/*
		 * here we abuse ff_ramp to hold x and y of constant force
		 * If in future any driver wants something else than x and y
		 * in s8, this should be changed to something more generic
		 */
		effect->u.ramp.start_level =
			clamp_val(effect->u.ramp.start_level + x, -0x80, 0x7f);
		effect->u.ramp.end_level =
			clamp_val(effect->u.ramp.end_level + y, -0x80, 0x7f);
		break;

	case FF_RUMBLE:
		strong = (u32)new->u.rumble.strong_magnitude * gain / 0xffff;
		weak = (u32)new->u.rumble.weak_magnitude * gain / 0xffff;

		if (effect->u.rumble.strong_magnitude + strong)
			effect->direction = ml_calculate_direction(
				effect->direction,
				effect->u.rumble.strong_magnitude,
				new->direction, strong);
		else if (effect->u.rumble.weak_magnitude + weak)
			effect->direction = ml_calculate_direction(
				effect->direction,
				effect->u.rumble.weak_magnitude,
				new->direction, weak);
		else
			effect->direction = 0;
		effect->u.rumble.strong_magnitude =
			min(strong + effect->u.rumble.strong_magnitude,
			    0xffffU);
		effect->u.rumble.weak_magnitude =
			min(weak + effect->u.rumble.weak_magnitude, 0xffffU);
		break;

	case FF_PERIODIC:
		i = apply_envelope(state, abs(new->u.periodic.magnitude),
				   &new->u.periodic.envelope);

		/* here we also scale it 0x7fff => 0xffff */
		i = i * gain / 0x7fff;

		if (effect->u.rumble.strong_magnitude + i)
			effect->direction = ml_calculate_direction(
				effect->direction,
				effect->u.rumble.strong_magnitude,
				new->direction, i);
		else
			effect->direction = 0;
		effect->u.rumble.strong_magnitude =
			min(i + effect->u.rumble.strong_magnitude, 0xffffU);
		effect->u.rumble.weak_magnitude =
			min(i + effect->u.rumble.weak_magnitude, 0xffffU);
		break;

	default:
		pr_err("invalid type in ml_combine_effects()\n");
		break;
	}

}


/*
 * Because memoryless devices have only one effect per effect type active
 * at one time we have to combine multiple effects into one
 */
static int ml_get_combo_effect(struct ml_device *ml,
			       unsigned long *effect_handled,
			       struct ff_effect *combo_effect)
{
	struct ff_effect *effect;
	struct ml_effect_state *state;
	int effect_type;
	int i;

	memset(combo_effect, 0, sizeof(struct ff_effect));

	for (i = 0; i < FF_MEMLESS_EFFECTS; i++) {
		if (__test_and_set_bit(i, effect_handled))
			continue;

		state = &ml->states[i];
		effect = state->effect;

		if (!test_bit(FF_EFFECT_STARTED, &state->flags))
			continue;

		if (time_before(jiffies, state->play_at))
			continue;

		/*
		 * here we have started effects that are either
		 * currently playing (and may need be aborted)
		 * or need to start playing.
		 */
		effect_type = get_compatible_type(ml->dev->ff, effect->type);
		if (combo_effect->type != effect_type) {
			if (combo_effect->type != 0) {
				__clear_bit(i, effect_handled);
				continue;
			}
			combo_effect->type = effect_type;
		}

		if (__test_and_clear_bit(FF_EFFECT_ABORTING, &state->flags)) {
			__clear_bit(FF_EFFECT_PLAYING, &state->flags);
			__clear_bit(FF_EFFECT_STARTED, &state->flags);
		} else if (effect->replay.length &&
			   time_after_eq(jiffies, state->stop_at)) {

			__clear_bit(FF_EFFECT_PLAYING, &state->flags);

			if (--state->count <= 0) {
				__clear_bit(FF_EFFECT_STARTED, &state->flags);
			} else {
				state->play_at = jiffies +
					msecs_to_jiffies(effect->replay.delay);
				state->stop_at = state->play_at +
					msecs_to_jiffies(effect->replay.length);
			}
		} else {
			__set_bit(FF_EFFECT_PLAYING, &state->flags);
			state->adj_at = jiffies;
			ml_combine_effects(combo_effect, state, ml->gain);
		}
	}

	return combo_effect->type != 0;
}

static void ml_play_effects(struct ml_device *ml)
{
	struct ff_effect effect;
	DECLARE_BITMAP(handled_bm, FF_MEMLESS_EFFECTS);

	memset(handled_bm, 0, sizeof(handled_bm));

	while (ml_get_combo_effect(ml, handled_bm, &effect))
		ml->play_effect(ml->dev, ml->private, &effect);

	ml_schedule_timer(ml);
}

static void ml_effect_timer(struct timer_list *t)
{
	struct ml_device *ml = from_timer(ml, t, timer);
	struct input_dev *dev = ml->dev;
	unsigned long flags;

	pr_debug("timer: updating effects\n");

	spin_lock_irqsave(&dev->event_lock, flags);
	ml_play_effects(ml);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

/*
 * Sets requested gain for FF effects. Called with dev->event_lock held.
 */
static void ml_ff_set_gain(struct input_dev *dev, u16 gain)
{
	struct ml_device *ml = dev->ff->private;
	int i;

	ml->gain = gain;

	for (i = 0; i < FF_MEMLESS_EFFECTS; i++)
		__clear_bit(FF_EFFECT_PLAYING, &ml->states[i].flags);

	ml_play_effects(ml);
}

/*
 * Start/stop specified FF effect. Called with dev->event_lock held.
 */
static int ml_ff_playback(struct input_dev *dev, int effect_id, int value)
{
	struct ml_device *ml = dev->ff->private;
	struct ml_effect_state *state = &ml->states[effect_id];

	if (value > 0) {
		pr_debug("initiated play\n");

		__set_bit(FF_EFFECT_STARTED, &state->flags);
		state->count = value;
		state->play_at = jiffies +
				 msecs_to_jiffies(state->effect->replay.delay);
		state->stop_at = state->play_at +
				 msecs_to_jiffies(state->effect->replay.length);
		state->adj_at = state->play_at;

	} else {
		pr_debug("initiated stop\n");

		if (test_bit(FF_EFFECT_PLAYING, &state->flags))
			__set_bit(FF_EFFECT_ABORTING, &state->flags);
		else
			__clear_bit(FF_EFFECT_STARTED, &state->flags);
	}

	ml_play_effects(ml);

	return 0;
}

static int ml_ff_upload(struct input_dev *dev,
			struct ff_effect *effect, struct ff_effect *old)
{
	struct ml_device *ml = dev->ff->private;
	struct ml_effect_state *state = &ml->states[effect->id];

	spin_lock_irq(&dev->event_lock);

	if (test_bit(FF_EFFECT_STARTED, &state->flags)) {
		__clear_bit(FF_EFFECT_PLAYING, &state->flags);
		state->play_at = jiffies +
				 msecs_to_jiffies(state->effect->replay.delay);
		state->stop_at = state->play_at +
				 msecs_to_jiffies(state->effect->replay.length);
		state->adj_at = state->play_at;
		ml_schedule_timer(ml);
	}

	spin_unlock_irq(&dev->event_lock);

	return 0;
}

static void ml_ff_destroy(struct ff_device *ff)
{
	struct ml_device *ml = ff->private;

	/*
	 * Even though we stop all playing effects when tearing down
	 * an input device (via input_device_flush() that calls into
	 * input_ff_flush() that stops and erases all effects), we
	 * do not actually stop the timer, and therefore we should
	 * do it here.
	 */
	del_timer_sync(&ml->timer);

	kfree(ml->private);
}

/**
 * input_ff_create_memless() - create memoryless force-feedback device
 * @dev: input device supporting force-feedback
 * @data: driver-specific data to be passed into @play_effect
 * @play_effect: driver-specific method for playing FF effect
 */
int input_ff_create_memless(struct input_dev *dev, void *data,
		int (*play_effect)(struct input_dev *, void *, struct ff_effect *))
{
	struct ml_device *ml;
	struct ff_device *ff;
	int error;
	int i;

	ml = kzalloc(sizeof(struct ml_device), GFP_KERNEL);
	if (!ml)
		return -ENOMEM;

	ml->dev = dev;
	ml->private = data;
	ml->play_effect = play_effect;
	ml->gain = 0xffff;
	timer_setup(&ml->timer, ml_effect_timer, 0);

	set_bit(FF_GAIN, dev->ffbit);

	error = input_ff_create(dev, FF_MEMLESS_EFFECTS);
	if (error) {
		kfree(ml);
		return error;
	}

	ff = dev->ff;
	ff->private = ml;
	ff->upload = ml_ff_upload;
	ff->playback = ml_ff_playback;
	ff->set_gain = ml_ff_set_gain;
	ff->destroy = ml_ff_destroy;

	/* we can emulate periodic effects with RUMBLE */
	if (test_bit(FF_RUMBLE, ff->ffbit)) {
		set_bit(FF_PERIODIC, dev->ffbit);
		set_bit(FF_SINE, dev->ffbit);
		set_bit(FF_TRIANGLE, dev->ffbit);
		set_bit(FF_SQUARE, dev->ffbit);
	}

	for (i = 0; i < FF_MEMLESS_EFFECTS; i++)
		ml->states[i].effect = &ff->effects[i];

	return 0;
}
EXPORT_SYMBOL_GPL(input_ff_create_memless);
