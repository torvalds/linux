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
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include "speakup.h"
#include "spk_priv.h"

#define DRV_VERSION "2.20"
#define SYNTH_CLEAR 0x03
#define PROCSPEECH 0x0b
static int xoff;

static inline int synth_full(void)
{
	return xoff;
}

static void do_catch_up(struct spk_synth *synth);
static void synth_flush(struct spk_synth *synth);
static void read_buff_add(u_char c);
static unsigned char get_index(struct spk_synth *synth);

static int in_escape;
static int is_flushing;

static spinlock_t flush_lock;
static DECLARE_WAIT_QUEUE_HEAD(flush);

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"[:dv ap 160] " } },
	{ CAPS_STOP, .u.s = {"[:dv ap 100 ] " } },
	{ RATE, .u.n = {"[:ra %d] ", 180, 75, 650, 0, 0, NULL } },
	{ INFLECTION, .u.n = {"[:dv pr %d] ", 100, 0, 10000, 0, 0, NULL } },
	{ VOL, .u.n = {"[:dv g5 %d] ", 86, 60, 86, 0, 0, NULL } },
	{ PUNCT, .u.n = {"[:pu %c] ", 0, 0, 2, 0, 0, "nsa" } },
	{ VOICE, .u.n = {"[:n%c] ", 0, 0, 9, 0, 0, "phfdburwkv" } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/dectlk.
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

static int ap_defaults[] = {122, 89, 155, 110, 208, 240, 200, 106, 306};
static int g5_defaults[] = {86, 81, 86, 84, 81, 80, 83, 83, 73};

static struct spk_synth synth_dectlk = {
	.name = "dectlk",
	.version = DRV_VERSION,
	.long_name = "Dectalk Express",
	.init = "[:error sp :name paul :rate 180 :tsr off] ",
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
	.default_pitch = ap_defaults,
	.default_vol = g5_defaults,
	.io_ops = &spk_ttyio_ops,
	.probe = spk_ttyio_synth_probe,
	.release = spk_ttyio_release,
	.synth_immediate = spk_ttyio_synth_immediate,
	.catch_up = do_catch_up,
	.flush = synth_flush,
	.is_alive = spk_synth_is_alive_restart,
	.synth_adjust = NULL,
	.read_buff_add = read_buff_add,
	.get_index = get_index,
	.indexing = {
		.command = "[:in re %d ] ",
		.lowindex = 1,
		.highindex = 8,
		.currindex = 1,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "dectlk",
	},
};

static int is_indnum(u_char *ch)
{
	if ((*ch >= '0') && (*ch <= '9')) {
		*ch = *ch - '0';
		return 1;
	}
	return 0;
}

static u_char lastind;

static unsigned char get_index(struct spk_synth *synth)
{
	u_char rv;

	rv = lastind;
	lastind = 0;
	return rv;
}

static void read_buff_add(u_char c)
{
	static int ind = -1;

	if (c == 0x01) {
		unsigned long flags;

		spin_lock_irqsave(&flush_lock, flags);
		is_flushing = 0;
		wake_up_interruptible(&flush);
		spin_unlock_irqrestore(&flush_lock, flags);
	} else if (c == 0x13) {
		xoff = 1;
	} else if (c == 0x11) {
		xoff = 0;
	} else if (is_indnum(&c)) {
		if (ind == -1)
			ind = c;
		else
			ind = ind * 10 + c;
	} else if ((c > 31) && (c < 127)) {
		if (ind != -1)
			lastind = (u_char)ind;
		ind = -1;
	}
}

static void do_catch_up(struct spk_synth *synth)
{
	int synth_full_val = 0;
	static u_char ch;
	static u_char last = '\0';
	unsigned long flags;
	unsigned long jiff_max;
	unsigned long timeout = msecs_to_jiffies(4000);
	DEFINE_WAIT(wait);
	struct var_t *jiffy_delta;
	struct var_t *delay_time;
	int jiffy_delta_val;
	int delay_time_val;

	jiffy_delta = spk_get_var(JIFFY);
	delay_time = spk_get_var(DELAY);
	spin_lock_irqsave(&speakup_info.spinlock, flags);
	jiffy_delta_val = jiffy_delta->u.n.value;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	jiff_max = jiffies + jiffy_delta_val;

	while (!kthread_should_stop()) {
		/* if no ctl-a in 4, send data anyway */
		spin_lock_irqsave(&flush_lock, flags);
		while (is_flushing && timeout) {
			prepare_to_wait(&flush, &wait, TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&flush_lock, flags);
			timeout = schedule_timeout(timeout);
			spin_lock_irqsave(&flush_lock, flags);
		}
		finish_wait(&flush, &wait);
		is_flushing = 0;
		spin_unlock_irqrestore(&flush_lock, flags);

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
		synth_full_val = synth_full();
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (ch == '\n')
			ch = 0x0D;
		if (synth_full_val || !synth->io_ops->synth_out(synth, ch)) {
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
	if (in_escape)
		/* if in command output ']' so we don't get an error */
		synth->io_ops->synth_out(synth, ']');
	in_escape = 0;
	is_flushing = 1;
	synth->io_ops->flush_buffer();
	synth->io_ops->synth_out(synth, SYNTH_CLEAR);
}

module_param_named(ser, synth_dectlk.ser, int, 0444);
module_param_named(dev, synth_dectlk.dev_name, charp, 0444);
module_param_named(start, synth_dectlk.startup, short, 0444);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(dev, "Set the device e.g. ttyUSB0, for the synthesizer.");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

module_spk_synth(synth_dectlk);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for DECtalk Express synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

