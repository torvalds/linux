/*
 * Input Multitouch Library
 *
 * Copyright (c) 2008-2010 Henrik Rydberg
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input/mt.h>
#include <linux/export.h>
#include <linux/slab.h>

#define TRKID_SGN	((TRKID_MAX + 1) >> 1)

static void copy_abs(struct input_dev *dev, unsigned int dst, unsigned int src)
{
	if (dev->absinfo && test_bit(src, dev->absbit)) {
		dev->absinfo[dst] = dev->absinfo[src];
		dev->absinfo[dst].fuzz = 0;
		dev->absbit[BIT_WORD(dst)] |= BIT_MASK(dst);
	}
}

/**
 * input_mt_init_slots() - initialize MT input slots
 * @dev: input device supporting MT events and finger tracking
 * @num_slots: number of slots used by the device
 * @flags: mt tasks to handle in core
 *
 * This function allocates all necessary memory for MT slot handling
 * in the input device, prepares the ABS_MT_SLOT and
 * ABS_MT_TRACKING_ID events for use and sets up appropriate buffers.
 * Depending on the flags set, it also performs pointer emulation and
 * frame synchronization.
 *
 * May be called repeatedly. Returns -EINVAL if attempting to
 * reinitialize with a different number of slots.
 */
int input_mt_init_slots(struct input_dev *dev, unsigned int num_slots,
			unsigned int flags)
{
	struct input_mt *mt = dev->mt;
	int i;

	if (!num_slots)
		return 0;
	if (mt)
		return mt->num_slots != num_slots ? -EINVAL : 0;

	mt = kzalloc(struct_size(mt, slots, num_slots), GFP_KERNEL);
	if (!mt)
		goto err_mem;

	mt->num_slots = num_slots;
	mt->flags = flags;
	input_set_abs_params(dev, ABS_MT_SLOT, 0, num_slots - 1, 0, 0);
	input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0, TRKID_MAX, 0, 0);

	if (flags & (INPUT_MT_POINTER | INPUT_MT_DIRECT)) {
		__set_bit(EV_KEY, dev->evbit);
		__set_bit(BTN_TOUCH, dev->keybit);

		copy_abs(dev, ABS_X, ABS_MT_POSITION_X);
		copy_abs(dev, ABS_Y, ABS_MT_POSITION_Y);
		copy_abs(dev, ABS_PRESSURE, ABS_MT_PRESSURE);
	}
	if (flags & INPUT_MT_POINTER) {
		__set_bit(BTN_TOOL_FINGER, dev->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
		if (num_slots >= 3)
			__set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);
		if (num_slots >= 4)
			__set_bit(BTN_TOOL_QUADTAP, dev->keybit);
		if (num_slots >= 5)
			__set_bit(BTN_TOOL_QUINTTAP, dev->keybit);
		__set_bit(INPUT_PROP_POINTER, dev->propbit);
	}
	if (flags & INPUT_MT_DIRECT)
		__set_bit(INPUT_PROP_DIRECT, dev->propbit);
	if (flags & INPUT_MT_SEMI_MT)
		__set_bit(INPUT_PROP_SEMI_MT, dev->propbit);
	if (flags & INPUT_MT_TRACK) {
		unsigned int n2 = num_slots * num_slots;
		mt->red = kcalloc(n2, sizeof(*mt->red), GFP_KERNEL);
		if (!mt->red)
			goto err_mem;
	}

	/* Mark slots as 'inactive' */
	for (i = 0; i < num_slots; i++)
		input_mt_set_value(&mt->slots[i], ABS_MT_TRACKING_ID, -1);

	/* Mark slots as 'unused' */
	mt->frame = 1;

	dev->mt = mt;
	return 0;
err_mem:
	kfree(mt);
	return -ENOMEM;
}
EXPORT_SYMBOL(input_mt_init_slots);

/**
 * input_mt_destroy_slots() - frees the MT slots of the input device
 * @dev: input device with allocated MT slots
 *
 * This function is only needed in error path as the input core will
 * automatically free the MT slots when the device is destroyed.
 */
void input_mt_destroy_slots(struct input_dev *dev)
{
	if (dev->mt) {
		kfree(dev->mt->red);
		kfree(dev->mt);
	}
	dev->mt = NULL;
}
EXPORT_SYMBOL(input_mt_destroy_slots);

/**
 * input_mt_report_slot_state() - report contact state
 * @dev: input device with allocated MT slots
 * @tool_type: the tool type to use in this slot
 * @active: true if contact is active, false otherwise
 *
 * Reports a contact via ABS_MT_TRACKING_ID, and optionally
 * ABS_MT_TOOL_TYPE. If active is true and the slot is currently
 * inactive, or if the tool type is changed, a new tracking id is
 * assigned to the slot. The tool type is only reported if the
 * corresponding absbit field is set.
 *
 * Returns true if contact is active.
 */
bool input_mt_report_slot_state(struct input_dev *dev,
				unsigned int tool_type, bool active)
{
	struct input_mt *mt = dev->mt;
	struct input_mt_slot *slot;
	int id;

	if (!mt)
		return false;

	slot = &mt->slots[mt->slot];
	slot->frame = mt->frame;

	if (!active) {
		input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
		return false;
	}

	id = input_mt_get_value(slot, ABS_MT_TRACKING_ID);
	if (id < 0)
		id = input_mt_new_trkid(mt);

	input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, id);
	input_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, tool_type);

	return true;
}
EXPORT_SYMBOL(input_mt_report_slot_state);

/**
 * input_mt_report_finger_count() - report contact count
 * @dev: input device with allocated MT slots
 * @count: the number of contacts
 *
 * Reports the contact count via BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP,
 * BTN_TOOL_TRIPLETAP and BTN_TOOL_QUADTAP.
 *
 * The input core ensures only the KEY events already setup for
 * this device will produce output.
 */
void input_mt_report_finger_count(struct input_dev *dev, int count)
{
	input_event(dev, EV_KEY, BTN_TOOL_FINGER, count == 1);
	input_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, count == 2);
	input_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, count == 3);
	input_event(dev, EV_KEY, BTN_TOOL_QUADTAP, count == 4);
	input_event(dev, EV_KEY, BTN_TOOL_QUINTTAP, count == 5);
}
EXPORT_SYMBOL(input_mt_report_finger_count);

/**
 * input_mt_report_pointer_emulation() - common pointer emulation
 * @dev: input device with allocated MT slots
 * @use_count: report number of active contacts as finger count
 *
 * Performs legacy pointer emulation via BTN_TOUCH, ABS_X, ABS_Y and
 * ABS_PRESSURE. Touchpad finger count is emulated if use_count is true.
 *
 * The input core ensures only the KEY and ABS axes already setup for
 * this device will produce output.
 */
void input_mt_report_pointer_emulation(struct input_dev *dev, bool use_count)
{
	struct input_mt *mt = dev->mt;
	struct input_mt_slot *oldest;
	int oldid, count, i;

	if (!mt)
		return;

	oldest = NULL;
	oldid = mt->trkid;
	count = 0;

	for (i = 0; i < mt->num_slots; ++i) {
		struct input_mt_slot *ps = &mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
		count++;
	}

	input_event(dev, EV_KEY, BTN_TOUCH, count > 0);

	if (use_count) {
		if (count == 0 &&
		    !test_bit(ABS_MT_DISTANCE, dev->absbit) &&
		    test_bit(ABS_DISTANCE, dev->absbit) &&
		    input_abs_get_val(dev, ABS_DISTANCE) != 0) {
			/*
			 * Force reporting BTN_TOOL_FINGER for devices that
			 * only report general hover (and not per-contact
			 * distance) when contact is in proximity but not
			 * on the surface.
			 */
			count = 1;
		}

		input_mt_report_finger_count(dev, count);
	}

	if (oldest) {
		int x = input_mt_get_value(oldest, ABS_MT_POSITION_X);
		int y = input_mt_get_value(oldest, ABS_MT_POSITION_Y);

		input_event(dev, EV_ABS, ABS_X, x);
		input_event(dev, EV_ABS, ABS_Y, y);

		if (test_bit(ABS_MT_PRESSURE, dev->absbit)) {
			int p = input_mt_get_value(oldest, ABS_MT_PRESSURE);
			input_event(dev, EV_ABS, ABS_PRESSURE, p);
		}
	} else {
		if (test_bit(ABS_MT_PRESSURE, dev->absbit))
			input_event(dev, EV_ABS, ABS_PRESSURE, 0);
	}
}
EXPORT_SYMBOL(input_mt_report_pointer_emulation);

static void __input_mt_drop_unused(struct input_dev *dev, struct input_mt *mt)
{
	int i;

	for (i = 0; i < mt->num_slots; i++) {
		if (!input_mt_is_used(mt, &mt->slots[i])) {
			input_mt_slot(dev, i);
			input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
		}
	}
}

/**
 * input_mt_drop_unused() - Inactivate slots not seen in this frame
 * @dev: input device with allocated MT slots
 *
 * Lift all slots not seen since the last call to this function.
 */
void input_mt_drop_unused(struct input_dev *dev)
{
	struct input_mt *mt = dev->mt;

	if (mt) {
		__input_mt_drop_unused(dev, mt);
		mt->frame++;
	}
}
EXPORT_SYMBOL(input_mt_drop_unused);

/**
 * input_mt_sync_frame() - synchronize mt frame
 * @dev: input device with allocated MT slots
 *
 * Close the frame and prepare the internal state for a new one.
 * Depending on the flags, marks unused slots as inactive and performs
 * pointer emulation.
 */
void input_mt_sync_frame(struct input_dev *dev)
{
	struct input_mt *mt = dev->mt;
	bool use_count = false;

	if (!mt)
		return;

	if (mt->flags & INPUT_MT_DROP_UNUSED)
		__input_mt_drop_unused(dev, mt);

	if ((mt->flags & INPUT_MT_POINTER) && !(mt->flags & INPUT_MT_SEMI_MT))
		use_count = true;

	input_mt_report_pointer_emulation(dev, use_count);

	mt->frame++;
}
EXPORT_SYMBOL(input_mt_sync_frame);

static int adjust_dual(int *begin, int step, int *end, int eq, int mu)
{
	int f, *p, s, c;

	if (begin == end)
		return 0;

	f = *begin;
	p = begin + step;
	s = p == end ? f + 1 : *p;

	for (; p != end; p += step)
		if (*p < f)
			s = f, f = *p;
		else if (*p < s)
			s = *p;

	c = (f + s + 1) / 2;
	if (c == 0 || (c > mu && (!eq || mu > 0)))
		return 0;
	/* Improve convergence for positive matrices by penalizing overcovers */
	if (s < 0 && mu <= 0)
		c *= 2;

	for (p = begin; p != end; p += step)
		*p -= c;

	return (c < s && s <= 0) || (f >= 0 && f < c);
}

static void find_reduced_matrix(int *w, int nr, int nc, int nrc, int mu)
{
	int i, k, sum;

	for (k = 0; k < nrc; k++) {
		for (i = 0; i < nr; i++)
			adjust_dual(w + i, nr, w + i + nrc, nr <= nc, mu);
		sum = 0;
		for (i = 0; i < nrc; i += nr)
			sum += adjust_dual(w + i, 1, w + i + nr, nc <= nr, mu);
		if (!sum)
			break;
	}
}

static int input_mt_set_matrix(struct input_mt *mt,
			       const struct input_mt_pos *pos, int num_pos,
			       int mu)
{
	const struct input_mt_pos *p;
	struct input_mt_slot *s;
	int *w = mt->red;
	int x, y;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (!input_mt_is_active(s))
			continue;
		x = input_mt_get_value(s, ABS_MT_POSITION_X);
		y = input_mt_get_value(s, ABS_MT_POSITION_Y);
		for (p = pos; p != pos + num_pos; p++) {
			int dx = x - p->x, dy = y - p->y;
			*w++ = dx * dx + dy * dy - mu;
		}
	}

	return w - mt->red;
}

static void input_mt_set_slots(struct input_mt *mt,
			       int *slots, int num_pos)
{
	struct input_mt_slot *s;
	int *w = mt->red, j;

	for (j = 0; j != num_pos; j++)
		slots[j] = -1;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (!input_mt_is_active(s))
			continue;

		for (j = 0; j != num_pos; j++) {
			if (w[j] < 0) {
				slots[j] = s - mt->slots;
				break;
			}
		}

		w += num_pos;
	}

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (input_mt_is_active(s))
			continue;

		for (j = 0; j != num_pos; j++) {
			if (slots[j] < 0) {
				slots[j] = s - mt->slots;
				break;
			}
		}
	}
}

/**
 * input_mt_assign_slots() - perform a best-match assignment
 * @dev: input device with allocated MT slots
 * @slots: the slot assignment to be filled
 * @pos: the position array to match
 * @num_pos: number of positions
 * @dmax: maximum ABS_MT_POSITION displacement (zero for infinite)
 *
 * Performs a best match against the current contacts and returns
 * the slot assignment list. New contacts are assigned to unused
 * slots.
 *
 * The assignments are balanced so that all coordinate displacements are
 * below the euclidian distance dmax. If no such assignment can be found,
 * some contacts are assigned to unused slots.
 *
 * Returns zero on success, or negative error in case of failure.
 */
int input_mt_assign_slots(struct input_dev *dev, int *slots,
			  const struct input_mt_pos *pos, int num_pos,
			  int dmax)
{
	struct input_mt *mt = dev->mt;
	int mu = 2 * dmax * dmax;
	int nrc;

	if (!mt || !mt->red)
		return -ENXIO;
	if (num_pos > mt->num_slots)
		return -EINVAL;
	if (num_pos < 1)
		return 0;

	nrc = input_mt_set_matrix(mt, pos, num_pos, mu);
	find_reduced_matrix(mt->red, num_pos, nrc / num_pos, nrc, mu);
	input_mt_set_slots(mt, slots, num_pos);

	return 0;
}
EXPORT_SYMBOL(input_mt_assign_slots);

/**
 * input_mt_get_slot_by_key() - return slot matching key
 * @dev: input device with allocated MT slots
 * @key: the key of the sought slot
 *
 * Returns the slot of the given key, if it exists, otherwise
 * set the key on the first unused slot and return.
 *
 * If no available slot can be found, -1 is returned.
 * Note that for this function to work properly, input_mt_sync_frame() has
 * to be called at each frame.
 */
int input_mt_get_slot_by_key(struct input_dev *dev, int key)
{
	struct input_mt *mt = dev->mt;
	struct input_mt_slot *s;

	if (!mt)
		return -1;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++)
		if (input_mt_is_active(s) && s->key == key)
			return s - mt->slots;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++)
		if (!input_mt_is_active(s) && !input_mt_is_used(mt, s)) {
			s->key = key;
			return s - mt->slots;
		}

	return -1;
}
EXPORT_SYMBOL(input_mt_get_slot_by_key);
