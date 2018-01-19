/*
 * originally written by: Kirk Reiser <kirk@braille.uwo.ca>
 * this version considerably modified by David Borowski, david575@rogers.com
 *
 * Copyright (C) 1998-99  Kirk Reiser.
 * Copyright (C) 2003 David Borowski.
 *
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
 * this code is specificly written as a driver for the speakup screenreview
 * package and is not a general device driver.
 */
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/serial_reg.h>	/* for UART_MCR* constants */

#include "spk_priv.h"
#include "speakup.h"

#define DRV_VERSION "2.21"
#define SYNTH_CLEAR 0x18
#define PROCSPEECH '\r'

static void do_catch_up(struct spk_synth *synth);

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"cap, " } },
	{ CAPS_STOP, .u.s = {"" } },
	{ RATE, .u.n = {"@W%d", 6, 1, 9, 0, 0, NULL } },
	{ PITCH, .u.n = {"@F%x", 10, 0, 15, 0, 0, NULL } },
	{ VOL, .u.n = {"@A%x", 10, 0, 15, 0, 0, NULL } },
	{ VOICE, .u.n = {"@V%d", 1, 1, 6, 0, 0, NULL } },
	{ LANG, .u.n = {"@=%d,", 1, 1, 4, 0, 0, NULL } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/apollo.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute lang_attribute =
	__ATTR(lang, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute rate_attribute =
	__ATTR(rate, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute voice_attribute =
	__ATTR(voice, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute vol_attribute =
	__ATTR(vol, 0644, spk_var_show, spk_var_store);

static struct kobj_attribute delay_time_attribute =
	__ATTR(delay_time, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute direct_attribute =
	__ATTR(direct, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute full_time_attribute =
	__ATTR(full_time, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute jiffy_delta_attribute =
	__ATTR(jiffy_delta, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute trigger_time_attribute =
	__ATTR(trigger_time, 0644, spk_var_show, spk_var_store);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *synth_attrs[] = {
	&caps_start_attribute.attr,
	&caps_stop_attribute.attr,
	&lang_attribute.attr,
	&pitch_attribute.attr,
	&rate_attribute.attr,
	&voice_attribute.attr,
	&vol_attribute.attr,
	&delay_time_attribute.attr,
	&direct_attribute.attr,
	&full_time_attribute.attr,
	&jiffy_delta_attribute.attr,
	&trigger_time_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct spk_synth synth_apollo = {
	.name = "apollo",
	.version = DRV_VERSION,
	.long_name = "Apollo",
	.init = "@R3@D0@K1\r",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 40000,
	.dev_name = SYNTH_DEFAULT_DEV,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.io_ops = &spk_ttyio_ops,
	.probe = spk_ttyio_synth_probe,
	.release = spk_ttyio_release,
	.synth_immediate = spk_ttyio_synth_immediate,
	.catch_up = do_catch_up,
	.flush = spk_synth_flush,
	.is_alive = spk_synth_is_alive_restart,
	.synth_adjust = NULL,
	.read_buff_add = NULL,
	.get_index = NULL,
	.indexing = {
		.command = NULL,
		.lowindex = 0,
		.highindex = 0,
		.currindex = 0,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "apollo",
	},
};

static void do_catch_up(struct spk_synth *synth)
{
	u_char ch;
	unsigned long flags;
	unsigned long jiff_max;
	struct var_t *jiffy_delta;
	struct var_t *delay_time;
	struct var_t *full_time;
	int full_time_val = 0;
	int delay_time_val = 0;
	int jiffy_delta_val = 0;

	jiffy_delta = spk_get_var(JIFFY);
	delay_time = spk_get_var(DELAY);
	full_time = spk_get_var(FULL);
	spin_lock_irqsave(&speakup_info.spinlock, flags);
	jiffy_delta_val = jiffy_delta->u.n.value;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	jiff_max = jiffies + jiffy_delta_val;

	while (!kthread_should_stop()) {
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		jiffy_delta_val = jiffy_delta->u.n.value;
		full_time_val = full_time->u.n.value;
		delay_time_val = delay_time->u.n.value;
		if (speakup_info.flushing) {
			speakup_info.flushing = 0;
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			synth->flush(synth);
			continue;
		}
		synth_buffer_skip_nonlatin1();
		if (synth_buffer_empty()) {
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			break;
		}
		ch = synth_buffer_peek();
		set_current_state(TASK_INTERRUPTIBLE);
		full_time_val = full_time->u.n.value;
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (!synth->io_ops->synth_out(synth, ch)) {
			synth->io_ops->tiocmset(0, UART_MCR_RTS);
			synth->io_ops->tiocmset(UART_MCR_RTS, 0);
			schedule_timeout(msecs_to_jiffies(full_time_val));
			continue;
		}
		if (time_after_eq(jiffies, jiff_max) && (ch == SPACE)) {
			spin_lock_irqsave(&speakup_info.spinlock, flags);
			jiffy_delta_val = jiffy_delta->u.n.value;
			full_time_val = full_time->u.n.value;
			delay_time_val = delay_time->u.n.value;
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			if (synth->io_ops->synth_out(synth, synth->procspeech))
				schedule_timeout(msecs_to_jiffies
						 (delay_time_val));
			else
				schedule_timeout(msecs_to_jiffies
						 (full_time_val));
			jiff_max = jiffies + jiffy_delta_val;
		}
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		synth_buffer_getc();
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	}
	synth->io_ops->synth_out(synth, PROCSPEECH);
}

module_param_named(ser, synth_apollo.ser, int, 0444);
module_param_named(dev, synth_apollo.dev_name, charp, 0444);
module_param_named(start, synth_apollo.startup, short, 0444);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(dev, "Set the device e.g. ttyUSB0, for the synthesizer.");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

module_spk_synth(synth_apollo);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for Apollo II synthesizer");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

