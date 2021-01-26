// SPDX-License-Identifier: GPL-2.0+
/*
 * originally written by: Kirk Reiser <kirk@braille.uwo.ca>
 * this version considerably modified by David Borowski, david575@rogers.com
 *
 * Copyright (C) 1998-99  Kirk Reiser.
 * Copyright (C) 2003 David Borowski.
 *
 * specificly written as a driver for the speakup screenreview
 * s not a general device driver.
 */
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kthread.h>

#include "spk_priv.h"
#include "speakup.h"

#define DRV_VERSION "2.14"
#define SYNTH_CLEAR 0x03
#define PROCSPEECH 0x0b

static volatile unsigned char last_char;

static void read_buff_add(u_char ch)
{
	last_char = ch;
}

static inline bool synth_full(void)
{
	return last_char == 0x13;
}

static void do_catch_up(struct spk_synth *synth);
static void synth_flush(struct spk_synth *synth);

static int in_escape;

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"[:dv ap 222]" } },
	{ CAPS_STOP, .u.s = {"[:dv ap 100]" } },
	{ RATE, .u.n = {"[:ra %d]", 7, 0, 9, 150, 25, NULL } },
	{ PITCH, .u.n = {"[:dv ap %d]", 100, 0, 100, 0, 0, NULL } },
	{ INFLECTION, .u.n = {"[:dv pr %d] ", 100, 0, 10000, 0, 0, NULL } },
	{ VOL, .u.n = {"[:dv gv %d]", 13, 0, 16, 0, 5, NULL } },
	{ PUNCT, .u.n = {"[:pu %c]", 0, 0, 2, 0, 0, "nsa" } },
	{ VOICE, .u.n = {"[:n%c]", 0, 0, 9, 0, 0, "phfdburwkv" } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/decext.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute inflection_attribute =
	__ATTR(inflection, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute punct_attribute =
	__ATTR(punct, 0644, spk_var_show, spk_var_store);
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
	&pitch_attribute.attr,
	&inflection_attribute.attr,
	&punct_attribute.attr,
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

static struct spk_synth synth_decext = {
	.name = "decext",
	.version = DRV_VERSION,
	.long_name = "Dectalk External",
	.init = "[:pe -380]",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 40000,
	.flags = SF_DEC,
	.dev_name = SYNTH_DEFAULT_DEV,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.io_ops = &spk_ttyio_ops,
	.probe = spk_ttyio_synth_probe,
	.release = spk_ttyio_release,
	.synth_immediate = spk_ttyio_synth_immediate,
	.catch_up = do_catch_up,
	.flush = synth_flush,
	.is_alive = spk_synth_is_alive_restart,
	.synth_adjust = NULL,
	.read_buff_add = read_buff_add,
	.get_index = NULL,
	.indexing = {
		.command = NULL,
		.lowindex = 0,
		.highindex = 0,
		.currindex = 0,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "decext",
	},
};

static void do_catch_up(struct spk_synth *synth)
{
	u_char ch;
	static u_char last = '\0';
	unsigned long flags;
	unsigned long jiff_max;
	struct var_t *jiffy_delta;
	struct var_t *delay_time;
	int jiffy_delta_val = 0;
	int delay_time_val = 0;

	jiffy_delta = spk_get_var(JIFFY);
	delay_time = spk_get_var(DELAY);

	spin_lock_irqsave(&speakup_info.spinlock, flags);
	jiffy_delta_val = jiffy_delta->u.n.value;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	jiff_max = jiffies + jiffy_delta_val;

	while (!kthread_should_stop()) {
		spin_lock_irqsave(&speakup_info.spinlock, flags);
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
		delay_time_val = delay_time->u.n.value;
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (ch == '\n')
			ch = 0x0D;
		if (synth_full() || !synth->io_ops->synth_out(synth, ch)) {
			schedule_timeout(msecs_to_jiffies(delay_time_val));
			continue;
		}
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		synth_buffer_getc();
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (ch == '[') {
			in_escape = 1;
		} else if (ch == ']') {
			in_escape = 0;
		} else if (ch <= SPACE) {
			if (!in_escape && strchr(",.!?;:", last))
				synth->io_ops->synth_out(synth, PROCSPEECH);
			if (time_after_eq(jiffies, jiff_max)) {
				if (!in_escape)
					synth->io_ops->synth_out(synth,
								 PROCSPEECH);
				spin_lock_irqsave(&speakup_info.spinlock,
						  flags);
				jiffy_delta_val = jiffy_delta->u.n.value;
				delay_time_val = delay_time->u.n.value;
				spin_unlock_irqrestore(&speakup_info.spinlock,
						       flags);
				schedule_timeout(msecs_to_jiffies
						 (delay_time_val));
				jiff_max = jiffies + jiffy_delta_val;
			}
		}
		last = ch;
	}
	if (!in_escape)
		synth->io_ops->synth_out(synth, PROCSPEECH);
}

static void synth_flush(struct spk_synth *synth)
{
	in_escape = 0;
	synth->io_ops->flush_buffer();
	synth->synth_immediate(synth, "\033P;10z\033\\");
}

module_param_named(ser, synth_decext.ser, int, 0444);
module_param_named(dev, synth_decext.dev_name, charp, 0444);
module_param_named(start, synth_decext.startup, short, 0444);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(dev, "Set the device e.g. ttyUSB0, for the synthesizer.");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

module_spk_synth(synth_decext);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for DECtalk External synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

