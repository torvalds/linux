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
 * specificly written as a driver for the speakup screenreview
 * s not a general device driver.
 */
#include "spk_priv.h"
#include "speakup.h"

#define DRV_VERSION "2.11"
#define SYNTH_CLEAR 0x18
#define PROCSPEECH '\r' /* process speech char */

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"\x05P8" } },
	{ CAPS_STOP, .u.s = {"\x05P5" } },
	{ RATE, .u.n = {"\x05R%d", 5, 0, 9, 0, 0, NULL } },
	{ PITCH, .u.n = {"\x05P%d", 5, 0, 9, 0, 0, NULL } },
	{ VOL, .u.n = {"\x05V%d", 5, 0, 9, 0, 0, NULL } },
	{ TONE, .u.n = {"\x05T%c", 12, 0, 25, 61, 0, NULL } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
	 };

/* These attributes will appear in /sys/accessibility/speakup/txprt. */

static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, 0644, spk_var_show, spk_var_store);
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

static struct spk_synth synth_txprt = {
	.name = "txprt",
	.version = DRV_VERSION,
	.long_name = "Transport",
	.init = "\x05N1",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 40000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.io_ops = &spk_serial_io_ops,
	.probe = spk_serial_synth_probe,
	.release = spk_serial_release,
	.synth_immediate = spk_serial_synth_immediate,
	.catch_up = spk_do_catch_up,
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
		.name = "txprt",
	},
};

module_param_named(ser, synth_txprt.ser, int, 0444);
module_param_named(start, synth_txprt.startup, short, 0444);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

module_spk_synth(synth_txprt);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for Transport synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

