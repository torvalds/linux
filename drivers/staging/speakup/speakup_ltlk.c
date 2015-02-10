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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * specificly written as a driver for the speakup screenreview
 * s not a general device driver.
 */
#include "speakup.h"
#include "spk_priv.h"
#include "serialio.h"
#include "speakup_dtlk.h" /* local header file for LiteTalk values */

#define DRV_VERSION "2.11"
#define PROCSPEECH 0x0d

static int synth_probe(struct spk_synth *synth);

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"\x01+35p" } },
	{ CAPS_STOP, .u.s = {"\x01-35p" } },
	{ RATE, .u.n = {"\x01%ds", 8, 0, 9, 0, 0, NULL } },
	{ PITCH, .u.n = {"\x01%dp", 50, 0, 99, 0, 0, NULL } },
	{ VOL, .u.n = {"\x01%dv", 5, 0, 9, 0, 0, NULL } },
	{ TONE, .u.n = {"\x01%dx", 1, 0, 2, 0, 0, NULL } },
	{ PUNCT, .u.n = {"\x01%db", 7, 0, 15, 0, 0, NULL } },
	{ VOICE, .u.n = {"\x01%do", 0, 0, 7, 0, 0, NULL } },
	{ FREQUENCY, .u.n = {"\x01%df", 5, 0, 9, 0, 0, NULL } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/ltlk.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute freq_attribute =
	__ATTR(freq, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute punct_attribute =
	__ATTR(punct, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute rate_attribute =
	__ATTR(rate, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute tone_attribute =
	__ATTR(tone, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute voice_attribute =
	__ATTR(voice, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute vol_attribute =
	__ATTR(vol, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);

static struct kobj_attribute delay_time_attribute =
	__ATTR(delay_time, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute direct_attribute =
	__ATTR(direct, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute full_time_attribute =
	__ATTR(full_time, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute jiffy_delta_attribute =
	__ATTR(jiffy_delta, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute trigger_time_attribute =
	__ATTR(trigger_time, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *synth_attrs[] = {
	&caps_start_attribute.attr,
	&caps_stop_attribute.attr,
	&freq_attribute.attr,
	&pitch_attribute.attr,
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

static struct spk_synth synth_ltlk = {
	.name = "ltlk",
	.version = DRV_VERSION,
	.long_name = "LiteTalk",
	.init = "\01@\x01\x31y\n\0",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 40000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.probe = synth_probe,
	.release = spk_serial_release,
	.synth_immediate = spk_synth_immediate,
	.catch_up = spk_do_catch_up,
	.flush = spk_synth_flush,
	.is_alive = spk_synth_is_alive_restart,
	.synth_adjust = NULL,
	.read_buff_add = NULL,
	.get_index = spk_serial_in_nowait,
	.indexing = {
		.command = "\x01%di",
		.lowindex = 1,
		.highindex = 5,
		.currindex = 1,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "ltlk",
	},
};

/* interrogate the LiteTalk and print its settings */
static void synth_interrogate(struct spk_synth *synth)
{
	unsigned char *t, i;
	unsigned char buf[50], rom_v[20];

	spk_synth_immediate(synth, "\x18\x01?");
	for (i = 0; i < 50; i++) {
		buf[i] = spk_serial_in();
		if (i > 2 && buf[i] == 0x7f)
			break;
	}
	t = buf+2;
	for (i = 0; *t != '\r'; t++) {
		rom_v[i] = *t;
		if (++i >= 19)
			break;
	}
	rom_v[i] = 0;
	pr_info("%s: ROM version: %s\n", synth->long_name, rom_v);
}

static int synth_probe(struct spk_synth *synth)
{
	int failed = 0;

	failed = spk_serial_synth_probe(synth);
	if (failed == 0)
		synth_interrogate(synth);
	synth->alive = !failed;
	return failed;
}

module_param_named(ser, synth_ltlk.ser, int, S_IRUGO);
module_param_named(start, synth_ltlk.startup, short, S_IRUGO);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

static int __init ltlk_init(void)
{
	return synth_add(&synth_ltlk);
}

static void __exit ltlk_exit(void)
{
	synth_remove(&synth_ltlk);
}

module_init(ltlk_init);
module_exit(ltlk_exit);
MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for DoubleTalk LT/LiteTalk synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

