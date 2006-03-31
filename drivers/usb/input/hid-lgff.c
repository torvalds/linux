/*
 * $$
 *
 * Force feedback support for hid-compliant for some of the devices from
 * Logitech, namely:
 * - WingMan Cordless RumblePad
 * - WingMan Force 3D
 *
 *  Copyright (c) 2002-2004 Johann Deneux
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
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <johann.deneux@it.uu.se>
 */

#include <linux/input.h>
#include <linux/sched.h>

//#define DEBUG
#include <linux/usb.h>

#include <linux/circ_buf.h>

#include "hid.h"
#include "fixp-arith.h"


/* Periodicity of the update */
#define PERIOD (HZ/10)

#define RUN_AT(t) (jiffies + (t))

/* Effect status */
#define EFFECT_STARTED 0     /* Effect is going to play after some time
				(ff_replay.delay) */
#define EFFECT_PLAYING 1     /* Effect is being played */
#define EFFECT_USED    2

// For lgff_device::flags
#define DEVICE_CLOSING 0     /* The driver is being unitialised */

/* Check that the current process can access an effect */
#define CHECK_OWNERSHIP(effect) (current->pid == 0 \
        || effect.owner == current->pid)

#define LGFF_CHECK_OWNERSHIP(i, l) \
        (i>=0 && i<LGFF_EFFECTS \
        && test_bit(EFFECT_USED, l->effects[i].flags) \
        && CHECK_OWNERSHIP(l->effects[i]))

#define LGFF_EFFECTS 8

struct device_type {
	u16 idVendor;
	u16 idProduct;
	signed short *ff;
};

struct lgff_effect {
	pid_t owner;

	struct ff_effect effect;

	unsigned long flags[1];
	unsigned int count;          /* Number of times left to play */
	unsigned long started_at;    /* When the effect started to play */
};

struct lgff_device {
	struct hid_device* hid;

	struct hid_report* constant;
	struct hid_report* rumble;
	struct hid_report* condition;

	struct lgff_effect effects[LGFF_EFFECTS];
	spinlock_t lock;             /* device-level lock. Having locks on
					a per-effect basis could be nice, but
					isn't really necessary */

	unsigned long flags[1];      /* Contains various information about the
				        state of the driver for this device */

	struct timer_list timer;
};

/* Callbacks */
static void hid_lgff_exit(struct hid_device* hid);
static int hid_lgff_event(struct hid_device *hid, struct input_dev *input,
			  unsigned int type, unsigned int code, int value);
static int hid_lgff_flush(struct input_dev *input, struct file *file);
static int hid_lgff_upload_effect(struct input_dev *input,
				  struct ff_effect *effect);
static int hid_lgff_erase(struct input_dev *input, int id);

/* Local functions */
static void hid_lgff_input_init(struct hid_device* hid);
static void hid_lgff_timer(unsigned long timer_data);
static struct hid_report* hid_lgff_duplicate_report(struct hid_report*);
static void hid_lgff_delete_report(struct hid_report*);

static signed short ff_rumble[] = {
	FF_RUMBLE,
	-1
};

static signed short ff_joystick[] = {
	FF_CONSTANT,
	-1
};

static struct device_type devices[] = {
	{0x046d, 0xc211, ff_rumble},
	{0x046d, 0xc219, ff_rumble},
	{0x046d, 0xc283, ff_joystick},
	{0x0000, 0x0000, ff_joystick}
};

int hid_lgff_init(struct hid_device* hid)
{
	struct lgff_device *private;
	struct hid_report* report;
	struct hid_field* field;

	/* Find the report to use */
	if (list_empty(&hid->report_enum[HID_OUTPUT_REPORT].report_list)) {
		err("No output report found");
		return -1;
	}
	/* Check that the report looks ok */
	report = (struct hid_report*)hid->report_enum[HID_OUTPUT_REPORT].report_list.next;
	if (!report) {
		err("NULL output report");
		return -1;
	}
	field = report->field[0];
	if (!field) {
		err("NULL field");
		return -1;
	}

	private = kzalloc(sizeof(struct lgff_device), GFP_KERNEL);
	if (!private)
		return -1;
	hid->ff_private = private;

	/* Input init */
	hid_lgff_input_init(hid);


	private->constant = hid_lgff_duplicate_report(report);
	if (!private->constant) {
		kfree(private);
		return -1;
	}
	private->constant->field[0]->value[0] = 0x51;
	private->constant->field[0]->value[1] = 0x08;
	private->constant->field[0]->value[2] = 0x7f;
	private->constant->field[0]->value[3] = 0x7f;

	private->rumble = hid_lgff_duplicate_report(report);
	if (!private->rumble) {
		hid_lgff_delete_report(private->constant);
		kfree(private);
		return -1;
	}
	private->rumble->field[0]->value[0] = 0x42;


	private->condition = hid_lgff_duplicate_report(report);
	if (!private->condition) {
		hid_lgff_delete_report(private->rumble);
		hid_lgff_delete_report(private->constant);
		kfree(private);
		return -1;
	}

	private->hid = hid;

	spin_lock_init(&private->lock);
	init_timer(&private->timer);
	private->timer.data = (unsigned long)private;
	private->timer.function = hid_lgff_timer;

	/* Event and exit callbacks */
	hid->ff_exit = hid_lgff_exit;
	hid->ff_event = hid_lgff_event;

	/* Start the update task */
	private->timer.expires = RUN_AT(PERIOD);
	add_timer(&private->timer);  /*TODO: only run the timer when at least
				       one effect is playing */

	printk(KERN_INFO "Force feedback for Logitech force feedback devices by Johann Deneux <johann.deneux@it.uu.se>\n");

	return 0;
}

static struct hid_report* hid_lgff_duplicate_report(struct hid_report* report)
{
	struct hid_report* ret;

	ret = kmalloc(sizeof(struct lgff_device), GFP_KERNEL);
	if (!ret)
		return NULL;
	*ret = *report;

	ret->field[0] = kmalloc(sizeof(struct hid_field), GFP_KERNEL);
	if (!ret->field[0]) {
		kfree(ret);
		return NULL;
	}
	*ret->field[0] = *report->field[0];

	ret->field[0]->value = kzalloc(sizeof(s32[8]), GFP_KERNEL);
	if (!ret->field[0]->value) {
		kfree(ret->field[0]);
		kfree(ret);
		return NULL;
	}

	return ret;
}

static void hid_lgff_delete_report(struct hid_report* report)
{
	if (report) {
		kfree(report->field[0]->value);
		kfree(report->field[0]);
		kfree(report);
	}
}

static void hid_lgff_input_init(struct hid_device* hid)
{
	struct device_type* dev = devices;
	signed short* ff;
	u16 idVendor = le16_to_cpu(hid->dev->descriptor.idVendor);
	u16 idProduct = le16_to_cpu(hid->dev->descriptor.idProduct);
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;

	while (dev->idVendor && (idVendor != dev->idVendor || idProduct != dev->idProduct))
		dev++;

	for (ff = dev->ff; *ff >= 0; ff++)
		set_bit(*ff, input_dev->ffbit);

	input_dev->upload_effect = hid_lgff_upload_effect;
	input_dev->flush = hid_lgff_flush;

	set_bit(EV_FF, input_dev->evbit);
	input_dev->ff_effects_max = LGFF_EFFECTS;
}

static void hid_lgff_exit(struct hid_device* hid)
{
	struct lgff_device *lgff = hid->ff_private;

	set_bit(DEVICE_CLOSING, lgff->flags);
	del_timer_sync(&lgff->timer);

	hid_lgff_delete_report(lgff->condition);
	hid_lgff_delete_report(lgff->rumble);
	hid_lgff_delete_report(lgff->constant);

	kfree(lgff);
}

static int hid_lgff_event(struct hid_device *hid, struct input_dev* input,
			  unsigned int type, unsigned int code, int value)
{
	struct lgff_device *lgff = hid->ff_private;
	struct lgff_effect *effect = lgff->effects + code;
	unsigned long flags;

	if (type != EV_FF)                     return -EINVAL;
	if (!LGFF_CHECK_OWNERSHIP(code, lgff)) return -EACCES;
	if (value < 0)                         return -EINVAL;

	spin_lock_irqsave(&lgff->lock, flags);

	if (value > 0) {
		if (test_bit(EFFECT_STARTED, effect->flags)) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -EBUSY;
		}
		if (test_bit(EFFECT_PLAYING, effect->flags)) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -EBUSY;
		}

		effect->count = value;

		if (effect->effect.replay.delay) {
			set_bit(EFFECT_STARTED, effect->flags);
		} else {
			set_bit(EFFECT_PLAYING, effect->flags);
		}
		effect->started_at = jiffies;
	}
	else { /* value == 0 */
		clear_bit(EFFECT_STARTED, effect->flags);
		clear_bit(EFFECT_PLAYING, effect->flags);
	}

	spin_unlock_irqrestore(&lgff->lock, flags);

	return 0;

}

/* Erase all effects this process owns */
static int hid_lgff_flush(struct input_dev *dev, struct file *file)
{
	struct hid_device *hid = dev->private;
	struct lgff_device *lgff = hid->ff_private;
	int i;

	for (i=0; i<dev->ff_effects_max; ++i) {

		/*NOTE: no need to lock here. The only times EFFECT_USED is
		  modified is when effects are uploaded or when an effect is
		  erased. But a process cannot close its dev/input/eventX fd
		  and perform ioctls on the same fd all at the same time */
		if ( current->pid == lgff->effects[i].owner
		     && test_bit(EFFECT_USED, lgff->effects[i].flags)) {

			if (hid_lgff_erase(dev, i))
				warn("erase effect %d failed", i);
		}

	}

	return 0;
}

static int hid_lgff_erase(struct input_dev *dev, int id)
{
	struct hid_device *hid = dev->private;
	struct lgff_device *lgff = hid->ff_private;
	unsigned long flags;

	if (!LGFF_CHECK_OWNERSHIP(id, lgff)) return -EACCES;

	spin_lock_irqsave(&lgff->lock, flags);
	lgff->effects[id].flags[0] = 0;
	spin_unlock_irqrestore(&lgff->lock, flags);

	return 0;
}

static int hid_lgff_upload_effect(struct input_dev* input,
				  struct ff_effect* effect)
{
	struct hid_device *hid = input->private;
	struct lgff_device *lgff = hid->ff_private;
	struct lgff_effect new;
	int id;
	unsigned long flags;

	dbg("ioctl rumble");

	if (!test_bit(effect->type, input->ffbit)) return -EINVAL;

	spin_lock_irqsave(&lgff->lock, flags);

	if (effect->id == -1) {
		int i;

		for (i=0; i<LGFF_EFFECTS && test_bit(EFFECT_USED, lgff->effects[i].flags); ++i);
		if (i >= LGFF_EFFECTS) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -ENOSPC;
		}

		effect->id = i;
		lgff->effects[i].owner = current->pid;
		lgff->effects[i].flags[0] = 0;
		set_bit(EFFECT_USED, lgff->effects[i].flags);
	}
	else if (!LGFF_CHECK_OWNERSHIP(effect->id, lgff)) {
		spin_unlock_irqrestore(&lgff->lock, flags);
		return -EACCES;
	}

	id = effect->id;
	new = lgff->effects[id];

	new.effect = *effect;

	if (test_bit(EFFECT_STARTED, lgff->effects[id].flags)
	    || test_bit(EFFECT_STARTED, lgff->effects[id].flags)) {

		/* Changing replay parameters is not allowed (for the time
		   being) */
		if (new.effect.replay.delay != lgff->effects[id].effect.replay.delay
		    || new.effect.replay.length != lgff->effects[id].effect.replay.length) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -ENOSYS;
		}

		lgff->effects[id] = new;

	} else {
		lgff->effects[id] = new;
	}

	spin_unlock_irqrestore(&lgff->lock, flags);
	return 0;
}

static void hid_lgff_timer(unsigned long timer_data)
{
	struct lgff_device *lgff = (struct lgff_device*)timer_data;
	struct hid_device *hid = lgff->hid;
	unsigned long flags;
	int x = 0x7f, y = 0x7f;   // Coordinates of constant effects
	unsigned int left = 0, right = 0;   // Rumbling
	int i;

	spin_lock_irqsave(&lgff->lock, flags);

	for (i=0; i<LGFF_EFFECTS; ++i) {
		struct lgff_effect* effect = lgff->effects +i;

		if (test_bit(EFFECT_PLAYING, effect->flags)) {

			switch (effect->effect.type) {
			case FF_CONSTANT: {
				//TODO: handle envelopes
				int degrees = effect->effect.direction * 360 >> 16;
				x += fixp_mult(fixp_sin(degrees),
					       fixp_new16(effect->effect.u.constant.level));
				y += fixp_mult(-fixp_cos(degrees),
					       fixp_new16(effect->effect.u.constant.level));
			}       break;
			case FF_RUMBLE:
				right += effect->effect.u.rumble.strong_magnitude;
				left += effect->effect.u.rumble.weak_magnitude;
				break;
			};

			/* One run of the effect is finished playing */
			if (time_after(jiffies,
					effect->started_at
					+ effect->effect.replay.delay*HZ/1000
					+ effect->effect.replay.length*HZ/1000)) {
				dbg("Finished playing once %d", i);
				if (--effect->count <= 0) {
					dbg("Stopped %d", i);
					clear_bit(EFFECT_PLAYING, effect->flags);
				}
				else {
					dbg("Start again %d", i);
					if (effect->effect.replay.length != 0) {
						clear_bit(EFFECT_PLAYING, effect->flags);
						set_bit(EFFECT_STARTED, effect->flags);
					}
					effect->started_at = jiffies;
				}
			}

		} else if (test_bit(EFFECT_STARTED, lgff->effects[i].flags)) {
			/* Check if we should start playing the effect */
			if (time_after(jiffies,
					lgff->effects[i].started_at
					+ lgff->effects[i].effect.replay.delay*HZ/1000)) {
				dbg("Now playing %d", i);
				clear_bit(EFFECT_STARTED, lgff->effects[i].flags);
				set_bit(EFFECT_PLAYING, lgff->effects[i].flags);
			}
		}
	}

#define CLAMP(x) if (x < 0) x = 0; if (x > 0xff) x = 0xff

	// Clamp values
	CLAMP(x);
	CLAMP(y);
	CLAMP(left);
	CLAMP(right);

#undef CLAMP

	if (x != lgff->constant->field[0]->value[2]
	    || y != lgff->constant->field[0]->value[3]) {
		lgff->constant->field[0]->value[2] = x;
		lgff->constant->field[0]->value[3] = y;
		dbg("(x,y)=(%04x, %04x)", x, y);
		hid_submit_report(hid, lgff->constant, USB_DIR_OUT);
	}

	if (left != lgff->rumble->field[0]->value[2]
	    || right != lgff->rumble->field[0]->value[3]) {
		lgff->rumble->field[0]->value[2] = left;
		lgff->rumble->field[0]->value[3] = right;
		dbg("(left,right)=(%04x, %04x)", left, right);
		hid_submit_report(hid, lgff->rumble, USB_DIR_OUT);
	}

	if (!test_bit(DEVICE_CLOSING, lgff->flags)) {
		lgff->timer.expires = RUN_AT(PERIOD);
		add_timer(&lgff->timer);
	}

	spin_unlock_irqrestore(&lgff->lock, flags);
}
