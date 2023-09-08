// SPDX-License-Identifier: GPL-2.0+
/* speakup_soft.c - speakup driver to register and make available
 * a user space device for software synthesizers.  written by: Kirk
 * Reiser <kirk@braille.uwo.ca>
 *
 * Copyright (C) 2003  Kirk Reiser.
 *
 * this code is specifically written as a driver for the speakup screenreview
 * package and is not a general device driver.
 */

#include <linux/unistd.h>
#include <linux/miscdevice.h>	/* for misc_register, and MISC_DYNAMIC_MINOR */
#include <linux/poll.h>		/* for poll_wait() */

/* schedule(), signal_pending(), TASK_INTERRUPTIBLE */
#include <linux/sched/signal.h>

#include "spk_priv.h"
#include "speakup.h"

#define DRV_VERSION "2.6"
#define PROCSPEECH 0x0d
#define CLEAR_SYNTH 0x18

static int softsynth_probe(struct spk_synth *synth);
static void softsynth_release(struct spk_synth *synth);
static int softsynth_is_alive(struct spk_synth *synth);
static int softsynth_adjust(struct spk_synth *synth, struct st_var_header *var);
static unsigned char get_index(struct spk_synth *synth);

static struct miscdevice synth_device, synthu_device;
static int init_pos;
static int misc_registered;


enum default_vars_id {
	DIRECT_ID = 0, CAPS_START_ID, CAPS_STOP_ID,
	PAUSE_ID, RATE_ID, PITCH_ID, INFLECTION_ID,
	VOL_ID, TONE_ID, PUNCT_ID, VOICE_ID,
	FREQUENCY_ID, V_LAST_VAR_ID,
	 NB_ID
};


static struct var_t vars[NB_ID] = {

	[DIRECT_ID]  = { DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	[CAPS_START_ID] = { CAPS_START, .u.s = {"\x01+3p" } },
	[CAPS_STOP_ID]  = { CAPS_STOP, .u.s = {"\x01-3p" } },
	[PAUSE_ID]  = { PAUSE, .u.n = {"\x01P" } },
	[RATE_ID]  = { RATE, .u.n = {"\x01%ds", 2, 0, 9, 0, 0, NULL } },
	[PITCH_ID]  = { PITCH, .u.n = {"\x01%dp", 5, 0, 9, 0, 0, NULL } },
	[INFLECTION_ID]  = { INFLECTION, .u.n = {"\x01%dr", 5, 0, 9, 0, 0, NULL } },
	[VOL_ID]  = { VOL, .u.n = {"\x01%dv", 5, 0, 9, 0, 0, NULL } },
	[TONE_ID]  = { TONE, .u.n = {"\x01%dx", 1, 0, 2, 0, 0, NULL } },
	[PUNCT_ID]  = { PUNCT, .u.n = {"\x01%db", 0, 0, 3, 0, 0, NULL } },
	[VOICE_ID]  = { VOICE, .u.n = {"\x01%do", 0, 0, 7, 0, 0, NULL } },
	[FREQUENCY_ID]  = { FREQUENCY, .u.n = {"\x01%df", 5, 0, 9, 0, 0, NULL } },
	V_LAST_VAR
};

/* These attributes will appear in /sys/accessibility/speakup/soft. */

static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute freq_attribute =
	__ATTR(freq, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute inflection_attribute =
	__ATTR(inflection, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute punct_attribute =
	__ATTR(punct, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute rate_attribute =
	__ATTR(rate, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute tone_attribute =
	__ATTR(tone, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute voice_attribute =
	__ATTR(voice, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute vol_attribute =
	__ATTR(vol, 0644, spk_var_show, spk_var_store);

/*
 * We should uncomment the following definition, when we agree on a
 * method of passing a language designation to the software synthesizer.
 * static struct kobj_attribute lang_attribute =
 *	__ATTR(lang, 0644, spk_var_show, spk_var_store);
 */

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
	&freq_attribute.attr,
/*	&lang_attribute.attr, */
	&pitch_attribute.attr,
	&inflection_attribute.attr,
	&punct_attribute.attr,
	&rate_attribute.attr,
	&tone_attribute.attr,
	&voice_attribute.attr,
	&vol_attribute.attr,
	&delay_time_attribute.attr,
	&direct_attribute.attr,
	&full_time_attribute.attr,
	&jiffy_delta_attribute.attr,
	&trigger_time_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct spk_synth synth_soft = {
	.name = "soft",
	.version = DRV_VERSION,
	.long_name = "software synth",
	.init = "\01@\x01\x31y\n",
	.procspeech = PROCSPEECH,
	.delay = 0,
	.trigger = 0,
	.jiffies = 0,
	.full = 0,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.io_ops = NULL,
	.probe = softsynth_probe,
	.release = softsynth_release,
	.synth_immediate = NULL,
	.catch_up = NULL,
	.flush = NULL,
	.is_alive = softsynth_is_alive,
	.synth_adjust = softsynth_adjust,
	.read_buff_add = NULL,
	.get_index = get_index,
	.indexing = {
		.command = "\x01%di",
		.lowindex = 1,
		.highindex = 5,
		.currindex = 1,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "soft",
	},
};

static char *get_initstring(void)
{
	static char buf[40];
	char *cp;
	struct var_t *var;
	size_t len;
	size_t n;

	memset(buf, 0, sizeof(buf));
	cp = buf;
	len = sizeof(buf);

	var = synth_soft.vars;
	while (var->var_id != MAXVARS) {
		if (var->var_id != CAPS_START && var->var_id != CAPS_STOP &&
		    var->var_id != PAUSE && var->var_id != DIRECT) {
			n = scnprintf(cp, len, var->u.n.synth_fmt,
				      var->u.n.value);
			cp = cp + n;
			len = len - n;
		}
		var++;
	}
	cp = cp + scnprintf(cp, len, "\n");
	return buf;
}

static int softsynth_open(struct inode *inode, struct file *fp)
{
	unsigned long flags;
	/*if ((fp->f_flags & O_ACCMODE) != O_RDONLY) */
	/*	return -EPERM; */
	spin_lock_irqsave(&speakup_info.spinlock, flags);
	if (synth_soft.alive) {
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		return -EBUSY;
	}
	synth_soft.alive = 1;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	return 0;
}

static int softsynth_close(struct inode *inode, struct file *fp)
{
	unsigned long flags;

	spin_lock_irqsave(&speakup_info.spinlock, flags);
	synth_soft.alive = 0;
	init_pos = 0;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	/* Make sure we let applications go before leaving */
	speakup_start_ttys();
	return 0;
}

static ssize_t softsynthx_read(struct file *fp, char __user *buf, size_t count,
			       loff_t *pos, int unicode)
{
	int chars_sent = 0;
	char __user *cp;
	char *init;
	size_t bytes_per_ch = unicode ? 3 : 1;
	u16 ch;
	int empty;
	unsigned long flags;
	DEFINE_WAIT(wait);

	if (count < bytes_per_ch)
		return -EINVAL;

	spin_lock_irqsave(&speakup_info.spinlock, flags);
	synth_soft.alive = 1;
	while (1) {
		prepare_to_wait(&speakup_event, &wait, TASK_INTERRUPTIBLE);
		if (synth_current() == &synth_soft) {
			if (!unicode)
				synth_buffer_skip_nonlatin1();
			if (!synth_buffer_empty() || speakup_info.flushing)
				break;
		}
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (fp->f_flags & O_NONBLOCK) {
			finish_wait(&speakup_event, &wait);
			return -EAGAIN;
		}
		if (signal_pending(current)) {
			finish_wait(&speakup_event, &wait);
			return -ERESTARTSYS;
		}
		schedule();
		spin_lock_irqsave(&speakup_info.spinlock, flags);
	}
	finish_wait(&speakup_event, &wait);

	cp = buf;
	init = get_initstring();

	/* Keep 3 bytes available for a 16bit UTF-8-encoded character */
	while (chars_sent <= count - bytes_per_ch) {
		if (synth_current() != &synth_soft)
			break;
		if (speakup_info.flushing) {
			speakup_info.flushing = 0;
			ch = '\x18';
		} else if (init[init_pos]) {
			ch = init[init_pos++];
		} else {
			if (!unicode)
				synth_buffer_skip_nonlatin1();
			if (synth_buffer_empty())
				break;
			ch = synth_buffer_getc();
		}
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);

		if ((!unicode && ch < 0x100) || (unicode && ch < 0x80)) {
			u_char c = ch;

			if (copy_to_user(cp, &c, 1))
				return -EFAULT;

			chars_sent++;
			cp++;
		} else if (unicode && ch < 0x800) {
			u_char s[2] = {
				0xc0 | (ch >> 6),
				0x80 | (ch & 0x3f)
			};

			if (copy_to_user(cp, s, sizeof(s)))
				return -EFAULT;

			chars_sent += sizeof(s);
			cp += sizeof(s);
		} else if (unicode) {
			u_char s[3] = {
				0xe0 | (ch >> 12),
				0x80 | ((ch >> 6) & 0x3f),
				0x80 | (ch & 0x3f)
			};

			if (copy_to_user(cp, s, sizeof(s)))
				return -EFAULT;

			chars_sent += sizeof(s);
			cp += sizeof(s);
		}

		spin_lock_irqsave(&speakup_info.spinlock, flags);
	}
	*pos += chars_sent;
	empty = synth_buffer_empty();
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	if (empty) {
		speakup_start_ttys();
		*pos = 0;
	}
	return chars_sent;
}

static ssize_t softsynth_read(struct file *fp, char __user *buf, size_t count,
			      loff_t *pos)
{
	return softsynthx_read(fp, buf, count, pos, 0);
}

static ssize_t softsynthu_read(struct file *fp, char __user *buf, size_t count,
			       loff_t *pos)
{
	return softsynthx_read(fp, buf, count, pos, 1);
}

static int last_index;

static ssize_t softsynth_write(struct file *fp, const char __user *buf,
			       size_t count, loff_t *pos)
{
	unsigned long supplied_index = 0;
	int converted;

	converted = kstrtoul_from_user(buf, count, 0, &supplied_index);

	if (converted < 0)
		return converted;

	last_index = supplied_index;
	return count;
}

static __poll_t softsynth_poll(struct file *fp, struct poll_table_struct *wait)
{
	unsigned long flags;
	__poll_t ret = 0;

	poll_wait(fp, &speakup_event, wait);

	spin_lock_irqsave(&speakup_info.spinlock, flags);
	if (synth_current() == &synth_soft &&
	    (!synth_buffer_empty() || speakup_info.flushing))
		ret = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	return ret;
}

static unsigned char get_index(struct spk_synth *synth)
{
	int rv;

	rv = last_index;
	last_index = 0;
	return rv;
}

static const struct file_operations softsynth_fops = {
	.owner = THIS_MODULE,
	.poll = softsynth_poll,
	.read = softsynth_read,
	.write = softsynth_write,
	.open = softsynth_open,
	.release = softsynth_close,
};

static const struct file_operations softsynthu_fops = {
	.owner = THIS_MODULE,
	.poll = softsynth_poll,
	.read = softsynthu_read,
	.write = softsynth_write,
	.open = softsynth_open,
	.release = softsynth_close,
};

static int softsynth_probe(struct spk_synth *synth)
{
	if (misc_registered != 0)
		return 0;
	memset(&synth_device, 0, sizeof(synth_device));
	synth_device.minor = MISC_DYNAMIC_MINOR;
	synth_device.name = "softsynth";
	synth_device.fops = &softsynth_fops;
	if (misc_register(&synth_device)) {
		pr_warn("Couldn't initialize miscdevice /dev/softsynth.\n");
		return -ENODEV;
	}

	memset(&synthu_device, 0, sizeof(synthu_device));
	synthu_device.minor = MISC_DYNAMIC_MINOR;
	synthu_device.name = "softsynthu";
	synthu_device.fops = &softsynthu_fops;
	if (misc_register(&synthu_device)) {
		misc_deregister(&synth_device);
		pr_warn("Couldn't initialize miscdevice /dev/softsynthu.\n");
		return -ENODEV;
	}

	misc_registered = 1;
	pr_info("initialized device: /dev/softsynth, node (MAJOR 10, MINOR %d)\n",
		synth_device.minor);
	pr_info("initialized device: /dev/softsynthu, node (MAJOR 10, MINOR %d)\n",
		synthu_device.minor);
	return 0;
}

static void softsynth_release(struct spk_synth *synth)
{
	misc_deregister(&synth_device);
	misc_deregister(&synthu_device);
	misc_registered = 0;
	pr_info("unregistered /dev/softsynth\n");
	pr_info("unregistered /dev/softsynthu\n");
}

static int softsynth_is_alive(struct spk_synth *synth)
{
	if (synth_soft.alive)
		return 1;
	return 0;
}

static int softsynth_adjust(struct spk_synth *synth, struct st_var_header *var)
{
	struct st_var_header *punc_level_var;
	struct var_t *var_data;

	if (var->var_id != PUNC_LEVEL)
		return 0;

	/* We want to set the the speech synthesis punctuation level
	 * accordingly, so it properly tunes speaking A_PUNC characters */
	var_data = var->data;
	if (!var_data)
		return 0;
	punc_level_var = spk_get_var_header(PUNCT);
	if (!punc_level_var)
		return 0;
	spk_set_num_var(var_data->u.n.value, punc_level_var, E_SET);

	return 1;
}

module_param_named(start, synth_soft.startup, short, 0444);
module_param_named(direct, vars[DIRECT_ID].u.n.default_val, int, 0444);
module_param_named(rate, vars[RATE_ID].u.n.default_val, int, 0444);
module_param_named(pitch, vars[PITCH_ID].u.n.default_val, int, 0444);
module_param_named(inflection, vars[INFLECTION_ID].u.n.default_val, int, 0444);
module_param_named(vol, vars[VOL_ID].u.n.default_val, int, 0444);
module_param_named(tone, vars[TONE_ID].u.n.default_val, int, 0444);
module_param_named(punct, vars[PUNCT_ID].u.n.default_val, int, 0444);
module_param_named(voice, vars[VOICE_ID].u.n.default_val, int, 0444);
module_param_named(frequency, vars[FREQUENCY_ID].u.n.default_val, int, 0444);



MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");
MODULE_PARM_DESC(direct, "Set the direct variable on load.");
MODULE_PARM_DESC(rate, "Sets the rate of the synthesizer.");
MODULE_PARM_DESC(pitch, "Sets the pitch of the synthesizer.");
MODULE_PARM_DESC(inflection, "Sets the inflection of the synthesizer.");
MODULE_PARM_DESC(vol, "Sets the volume of the speech synthesizer.");
MODULE_PARM_DESC(tone, "Sets the tone of the speech synthesizer.");
MODULE_PARM_DESC(punct, "Sets the amount of punctuation spoken by the synthesizer.");
MODULE_PARM_DESC(voice, "Sets the voice used by the synthesizer.");
MODULE_PARM_DESC(frequency, "Sets the frequency of speech synthesizer.");

module_spk_synth(synth_soft);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_DESCRIPTION("Speakup userspace software synthesizer support");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
