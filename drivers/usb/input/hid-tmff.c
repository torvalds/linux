/*
 * Force feedback support for various HID compliant devices by ThrustMaster:
 *    ThrustMaster FireStorm Dual Power 2
 * and possibly others whose device ids haven't been added.
 *
 *  Modified to support ThrustMaster devices by Zinx Verituse
 *  on 2003-01-25 from the Logitech force feedback driver,
 *  which is by Johann Deneux.
 *
 *  Copyright (c) 2003 Zinx Verituse <zinx@epicsol.org>
 *  Copyright (c) 2002 Johann Deneux
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

#include <linux/input.h>
#include <linux/sched.h>

#undef DEBUG
#include <linux/usb.h>

#include <linux/circ_buf.h>

#include "hid.h"
#include "fixp-arith.h"

/* Usages for thrustmaster devices I know about */
#define THRUSTMASTER_USAGE_RUMBLE_LR	(HID_UP_GENDESK | 0xbb)
#define DELAY_CALC(t,delay)		((t) + (delay)*HZ/1000)

/* Effect status */
#define EFFECT_STARTED 0	/* Effect is going to play after some time */
#define EFFECT_PLAYING 1	/* Effect is playing */
#define EFFECT_USED    2

/* For tmff_device::flags */
#define DEVICE_CLOSING 0	/* The driver is being unitialised */

/* Check that the current process can access an effect */
#define CHECK_OWNERSHIP(effect) (current->pid == 0 \
        || effect.owner == current->pid)

#define TMFF_CHECK_ID(id)	((id) >= 0 && (id) < TMFF_EFFECTS)

#define TMFF_CHECK_OWNERSHIP(i, l) \
        (test_bit(EFFECT_USED, l->effects[i].flags) \
        && CHECK_OWNERSHIP(l->effects[i]))

#define TMFF_EFFECTS 8

struct tmff_effect {
	pid_t owner;

	struct ff_effect effect;

	unsigned long flags[1];
	unsigned int count;             /* Number of times left to play */

	unsigned long play_at;          /* When the effect starts to play */
	unsigned long stop_at;		/* When the effect ends */
};

struct tmff_device {
	struct hid_device *hid;

	struct hid_report *report;

	struct hid_field *rumble;

	unsigned int effects_playing;
	struct tmff_effect effects[TMFF_EFFECTS];
	spinlock_t lock;             /* device-level lock. Having locks on
					a per-effect basis could be nice, but
					isn't really necessary */

	unsigned long flags[1];      /* Contains various information about the
					state of the driver for this device */

	struct timer_list timer;
};

/* Callbacks */
static void hid_tmff_exit(struct hid_device *hid);
static int hid_tmff_event(struct hid_device *hid, struct input_dev *input,
			  unsigned int type, unsigned int code, int value);
static int hid_tmff_flush(struct input_dev *input, struct file *file);
static int hid_tmff_upload_effect(struct input_dev *input,
				  struct ff_effect *effect);
static int hid_tmff_erase(struct input_dev *input, int id);

/* Local functions */
static void hid_tmff_recalculate_timer(struct tmff_device *tmff);
static void hid_tmff_timer(unsigned long timer_data);

int hid_tmff_init(struct hid_device *hid)
{
	struct tmff_device *private;
	struct list_head *pos;
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;

	private = kmalloc(sizeof(struct tmff_device), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	memset(private, 0, sizeof(struct tmff_device));
	hid->ff_private = private;

	/* Find the report to use */
	__list_for_each(pos, &hid->report_enum[HID_OUTPUT_REPORT].report_list) {
		struct hid_report *report = (struct hid_report *)pos;
		int fieldnum;

		for (fieldnum = 0; fieldnum < report->maxfield; ++fieldnum) {
			struct hid_field *field = report->field[fieldnum];

			if (field->maxusage <= 0)
				continue;

			switch (field->usage[0].hid) {
				case THRUSTMASTER_USAGE_RUMBLE_LR:
					if (field->report_count < 2) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR with report_count < 2");
						continue;
					}

					if (field->logical_maximum == field->logical_minimum) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR with logical_maximum == logical_minimum");
						continue;
					}

					if (private->report && private->report != report) {
						warn("ignoring THRUSTMASTER_USAGE_RUMBLE_LR in other report");
						continue;
					}

					if (private->rumble && private->rumble != field) {
						warn("ignoring duplicate THRUSTMASTER_USAGE_RUMBLE_LR");
						continue;
					}

					private->report = report;
					private->rumble = field;

					set_bit(FF_RUMBLE, input_dev->ffbit);
					break;

				default:
					warn("ignoring unknown output usage %08x", field->usage[0].hid);
					continue;
			}

			/* Fallthrough to here only when a valid usage is found */
			input_dev->upload_effect = hid_tmff_upload_effect;
			input_dev->flush = hid_tmff_flush;

			set_bit(EV_FF, input_dev->evbit);
			input_dev->ff_effects_max = TMFF_EFFECTS;
		}
	}

	private->hid = hid;

	spin_lock_init(&private->lock);
	init_timer(&private->timer);
	private->timer.data = (unsigned long)private;
	private->timer.function = hid_tmff_timer;

	/* Event and exit callbacks */
	hid->ff_exit = hid_tmff_exit;
	hid->ff_event = hid_tmff_event;

	info("Force feedback for ThrustMaster rumble pad devices by Zinx Verituse <zinx@epicsol.org>");

	return 0;
}

static void hid_tmff_exit(struct hid_device *hid)
{
	struct tmff_device *tmff = hid->ff_private;
	unsigned long flags;

	spin_lock_irqsave(&tmff->lock, flags);

	set_bit(DEVICE_CLOSING, tmff->flags);
	del_timer_sync(&tmff->timer);

	spin_unlock_irqrestore(&tmff->lock, flags);

	kfree(tmff);
}

static int hid_tmff_event(struct hid_device *hid, struct input_dev *input,
			  unsigned int type, unsigned int code, int value)
{
	struct tmff_device *tmff = hid->ff_private;
	struct tmff_effect *effect = &tmff->effects[code];
	unsigned long flags;

	if (type != EV_FF)
		return -EINVAL;
	if (!TMFF_CHECK_ID(code))
		return -EINVAL;
	if (!TMFF_CHECK_OWNERSHIP(code, tmff))
		return -EACCES;
	if (value < 0)
		return -EINVAL;

	spin_lock_irqsave(&tmff->lock, flags);

	if (value > 0) {
		set_bit(EFFECT_STARTED, effect->flags);
		clear_bit(EFFECT_PLAYING, effect->flags);
		effect->count = value;
		effect->play_at = DELAY_CALC(jiffies, effect->effect.replay.delay);
	} else {
		clear_bit(EFFECT_STARTED, effect->flags);
		clear_bit(EFFECT_PLAYING, effect->flags);
	}

	hid_tmff_recalculate_timer(tmff);

	spin_unlock_irqrestore(&tmff->lock, flags);

	return 0;

}

/* Erase all effects this process owns */

static int hid_tmff_flush(struct input_dev *dev, struct file *file)
{
	struct hid_device *hid = dev->private;
	struct tmff_device *tmff = hid->ff_private;
	int i;

	for (i=0; i<dev->ff_effects_max; ++i)

	     /* NOTE: no need to lock here. The only times EFFECT_USED is
		modified is when effects are uploaded or when an effect is
		erased. But a process cannot close its dev/input/eventX fd
		and perform ioctls on the same fd all at the same time */

		if (current->pid == tmff->effects[i].owner
		     && test_bit(EFFECT_USED, tmff->effects[i].flags))
			if (hid_tmff_erase(dev, i))
				warn("erase effect %d failed", i);


	return 0;
}

static int hid_tmff_erase(struct input_dev *dev, int id)
{
	struct hid_device *hid = dev->private;
	struct tmff_device *tmff = hid->ff_private;
	unsigned long flags;

	if (!TMFF_CHECK_ID(id))
		return -EINVAL;
	if (!TMFF_CHECK_OWNERSHIP(id, tmff))
		return -EACCES;

	spin_lock_irqsave(&tmff->lock, flags);

	tmff->effects[id].flags[0] = 0;
	hid_tmff_recalculate_timer(tmff);

	spin_unlock_irqrestore(&tmff->lock, flags);

	return 0;
}

static int hid_tmff_upload_effect(struct input_dev *input,
				  struct ff_effect *effect)
{
	struct hid_device *hid = input->private;
	struct tmff_device *tmff = hid->ff_private;
	int id;
	unsigned long flags;

	if (!test_bit(effect->type, input->ffbit))
		return -EINVAL;
	if (effect->id != -1 && !TMFF_CHECK_ID(effect->id))
		return -EINVAL;

	spin_lock_irqsave(&tmff->lock, flags);

	if (effect->id == -1) {
		/* Find a free effect */
		for (id = 0; id < TMFF_EFFECTS && test_bit(EFFECT_USED, tmff->effects[id].flags); ++id);

		if (id >= TMFF_EFFECTS) {
			spin_unlock_irqrestore(&tmff->lock, flags);
			return -ENOSPC;
		}

		effect->id = id;
		tmff->effects[id].owner = current->pid;
		tmff->effects[id].flags[0] = 0;
		set_bit(EFFECT_USED, tmff->effects[id].flags);

	} else {
		/* Re-uploading an owned effect, to change parameters */
		id = effect->id;
		clear_bit(EFFECT_PLAYING, tmff->effects[id].flags);
	}

	tmff->effects[id].effect = *effect;

	hid_tmff_recalculate_timer(tmff);

	spin_unlock_irqrestore(&tmff->lock, flags);
	return 0;
}

/* Start the timer for the next start/stop/delay */
/* Always call this while tmff->lock is locked */

static void hid_tmff_recalculate_timer(struct tmff_device *tmff)
{
	int i;
	int events = 0;
	unsigned long next_time;

	next_time = 0;	/* Shut up compiler's incorrect warning */

	/* Find the next change in an effect's status */
	for (i = 0; i < TMFF_EFFECTS; ++i) {
		struct tmff_effect *effect = &tmff->effects[i];
		unsigned long play_time;

		if (!test_bit(EFFECT_STARTED, effect->flags))
			continue;

		effect->stop_at = DELAY_CALC(effect->play_at, effect->effect.replay.length);

		if (!test_bit(EFFECT_PLAYING, effect->flags))
			play_time = effect->play_at;
		else
			play_time = effect->stop_at;

		events++;

		if (time_after(jiffies, play_time))
			play_time = jiffies;

		if (events == 1)
			next_time = play_time;
		else {
			if (time_after(next_time, play_time))
				next_time = play_time;
		}
	}

	if (!events && tmff->effects_playing) {
		/* Treat all effects turning off as an event */
		events = 1;
		next_time = jiffies;
	}

	if (!events) {
		/* No events, no time, no need for a timer. */
		del_timer_sync(&tmff->timer);
		return;
	}

	mod_timer(&tmff->timer, next_time);
}

/* Changes values from 0 to 0xffff into values from minimum to maximum */
static inline int hid_tmff_scale(unsigned int in, int minimum, int maximum)
{
	int ret;

	ret = (in * (maximum - minimum) / 0xffff) + minimum;
	if (ret < minimum)
		return minimum;
	if (ret > maximum)
		return maximum;
	return ret;
}

static void hid_tmff_timer(unsigned long timer_data)
{
	struct tmff_device *tmff = (struct tmff_device *) timer_data;
	struct hid_device *hid = tmff->hid;
	unsigned long flags;
	int left = 0, right = 0;	/* Rumbling */
	int i;

	spin_lock_irqsave(&tmff->lock, flags);

	tmff->effects_playing = 0;

	for (i = 0; i < TMFF_EFFECTS; ++i) {
		struct tmff_effect *effect = &tmff->effects[i];

		if (!test_bit(EFFECT_STARTED, effect->flags))
			continue;

		if (!time_after(jiffies, effect->play_at))
			continue;

		if (time_after(jiffies, effect->stop_at)) {

			dbg("Finished playing once %d", i);
			clear_bit(EFFECT_PLAYING, effect->flags);

			if (--effect->count <= 0) {
				dbg("Stopped %d", i);
				clear_bit(EFFECT_STARTED, effect->flags);
				continue;
			} else {
				dbg("Start again %d", i);
				effect->play_at = DELAY_CALC(jiffies, effect->effect.replay.delay);
				continue;
			}
		}

		++tmff->effects_playing;

		set_bit(EFFECT_PLAYING, effect->flags);

		switch (effect->effect.type) {
			case FF_RUMBLE:
				right += effect->effect.u.rumble.strong_magnitude;
				left += effect->effect.u.rumble.weak_magnitude;
				break;
			default:
				BUG();
				break;
		}
	}

	left = hid_tmff_scale(left, tmff->rumble->logical_minimum, tmff->rumble->logical_maximum);
	right = hid_tmff_scale(right, tmff->rumble->logical_minimum, tmff->rumble->logical_maximum);

	if (left != tmff->rumble->value[0] || right != tmff->rumble->value[1]) {
		tmff->rumble->value[0] = left;
		tmff->rumble->value[1] = right;
		dbg("(left,right)=(%08x, %08x)", left, right);
		hid_submit_report(hid, tmff->report, USB_DIR_OUT);
	}

	if (!test_bit(DEVICE_CLOSING, tmff->flags))
		hid_tmff_recalculate_timer(tmff);

	spin_unlock_irqrestore(&tmff->lock, flags);
}
