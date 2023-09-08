// SPDX-License-Identifier: GPL-2.0+
/*
 * originally written by: Kirk Reiser <kirk@braille.uwo.ca>
 * this version considerably modified by David Borowski, david575@rogers.com
 * eventually modified by Samuel Thibault <samuel.thibault@ens-lyon.org>
 *
 * Copyright (C) 1998-99  Kirk Reiser.
 * Copyright (C) 2003 David Borowski.
 * Copyright (C) 2007 Samuel Thibault.
 *
 * specifically written as a driver for the speakup screenreview
 * s not a general device driver.
 */
#include "spk_priv.h"
#include "speakup.h"

#define PROCSPEECH '\n'
#define DRV_VERSION "2.11"
#define SYNTH_CLEAR '!'


enum default_vars_id {
	CAPS_START_ID = 0, CAPS_STOP_ID,
	PAUSE_ID,
	RATE_ID, PITCH_ID, INFLECTION_ID,
	VOL_ID, TONE_ID, PUNCT_ID,
	DIRECT_ID, V_LAST_VAR_ID,
	NB_ID
};




static struct var_t vars[NB_ID] = {
	[CAPS_START_ID] = { CAPS_START, .u.s = {"CAPS_START\n" } },
	[CAPS_STOP_ID] = { CAPS_STOP, .u.s = {"CAPS_STOP\n" } },
	[PAUSE_ID] = { PAUSE, .u.s = {"PAUSE\n"} },
	[RATE_ID] = { RATE, .u.n = {"RATE %d\n", 8, 1, 16, 0, 0, NULL } },
	[PITCH_ID] = { PITCH, .u.n = {"PITCH %d\n", 8, 0, 16, 0, 0, NULL } },
	[INFLECTION_ID] = { INFLECTION, .u.n = {"INFLECTION %d\n", 8, 0, 16, 0, 0, NULL } },
	[VOL_ID] = { VOL, .u.n = {"VOL %d\n", 8, 0, 16, 0, 0, NULL } },
	[TONE_ID] = { TONE, .u.n = {"TONE %d\n", 8, 0, 16, 0, 0, NULL } },
	[PUNCT_ID] = { PUNCT, .u.n = {"PUNCT %d\n", 0, 0, 3, 0, 0, NULL } },
	[DIRECT_ID] = { DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/dummy.
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
static struct kobj_attribute tone_attribute =
	__ATTR(tone, 0644, spk_var_show, spk_var_store);
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
	&tone_attribute.attr,
	&vol_attribute.attr,
	&delay_time_attribute.attr,
	&direct_attribute.attr,
	&full_time_attribute.attr,
	&jiffy_delta_attribute.attr,
	&trigger_time_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static void read_buff_add(u_char c)
{
	pr_info("speakup_dummy: got character %02x\n", c);
}

static struct spk_synth synth_dummy = {
	.name = "dummy",
	.version = DRV_VERSION,
	.long_name = "Dummy",
	.init = "Speakup\n",
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
	.catch_up = spk_do_catch_up_unicode,
	.flush = spk_synth_flush,
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
		.name = "dummy",
	},
};

module_param_named(ser, synth_dummy.ser, int, 0444);
module_param_named(dev, synth_dummy.dev_name, charp, 0444);
module_param_named(start, synth_dummy.startup, short, 0444);
module_param_named(rate, vars[RATE_ID].u.n.default_val, int, 0444);
module_param_named(pitch, vars[PITCH_ID].u.n.default_val, int, 0444);
module_param_named(inflection, vars[INFLECTION_ID].u.n.default_val, int, 0444);
module_param_named(vol, vars[VOL_ID].u.n.default_val, int, 0444);
module_param_named(tone, vars[TONE_ID].u.n.default_val, int, 0444);
module_param_named(punct, vars[PUNCT_ID].u.n.default_val, int, 0444);
module_param_named(direct, vars[DIRECT_ID].u.n.default_val, int, 0444);




MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(dev, "Set the device e.g. ttyUSB0, for the synthesizer.");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");
MODULE_PARM_DESC(rate, "Set the rate variable on load.");
MODULE_PARM_DESC(pitch, "Set the pitch variable on load.");
MODULE_PARM_DESC(inflection, "Set the inflection variable on load.");
MODULE_PARM_DESC(vol, "Set the vol variable on load.");
MODULE_PARM_DESC(tone, "Set the tone variable on load.");
MODULE_PARM_DESC(punct, "Set the punct variable on load.");
MODULE_PARM_DESC(direct, "Set the direct variable on load.");


module_spk_synth(synth_dummy);

MODULE_AUTHOR("Samuel Thibault <samuel.thibault@ens-lyon.org>");
MODULE_DESCRIPTION("Speakup support for text console");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

