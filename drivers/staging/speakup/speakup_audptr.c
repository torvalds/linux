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
#include "spk_priv.h"
#include "speakup.h"
#include "serialio.h"

#define DRV_VERSION "2.11"
#define SYNTH_CLEAR 0x18 /* flush synth buffer */
#define PROCSPEECH '\r' /* start synth processing speech char */

static int synth_probe(struct spk_synth *synth);
static void synth_flush(struct spk_synth *synth);

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"\x05[f99]" } },
	{ CAPS_STOP, .u.s = {"\x05[f80]" } },
	{ RATE, .u.n = {"\x05[r%d]", 10, 0, 20, 100, -10, NULL } },
	{ PITCH, .u.n = {"\x05[f%d]", 80, 39, 4500, 0, 0, NULL } },
	{ VOL, .u.n = {"\x05[g%d]", 21, 0, 40, 0, 0, NULL } },
	{ TONE, .u.n = {"\x05[s%d]", 9, 0, 63, 0, 0, NULL } },
	{ PUNCT, .u.n = {"\x05[A%c]", 0, 0, 3, 0, 0, "nmsa" } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/audptr.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute punct_attribute =
	__ATTR(punct, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute rate_attribute =
	__ATTR(rate, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute tone_attribute =
	__ATTR(tone, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute vol_attribute =
	__ATTR(vol, USER_RW, spk_var_show, spk_var_store);

static struct kobj_attribute delay_time_attribute =
	__ATTR(delay_time, ROOT_W, spk_var_show, spk_var_store);
static struct kobj_attribute direct_attribute =
	__ATTR(direct, USER_RW, spk_var_show, spk_var_store);
static struct kobj_attribute full_time_attribute =
	__ATTR(full_time, ROOT_W, spk_var_show, spk_var_store);
static struct kobj_attribute jiffy_delta_attribute =
	__ATTR(jiffy_delta, ROOT_W, spk_var_show, spk_var_store);
static struct kobj_attribute trigger_time_attribute =
	__ATTR(trigger_time, ROOT_W, spk_var_show, spk_var_store);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *synth_attrs[] = {
	&caps_start_attribute.attr,
	&caps_stop_attribute.attr,
	&pitch_attribute.attr,
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

static struct spk_synth synth_audptr = {
	.name = "audptr",
	.version = DRV_VERSION,
	.long_name = "Audapter",
	.init = "\x05[D1]\x05[Ol]",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 400,
	.trigger = 50,
	.jiffies = 30,
	.full = 18000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.probe = synth_probe,
	.release = spk_serial_release,
	.synth_immediate = spk_synth_immediate,
	.catch_up = spk_do_catch_up,
	.flush = synth_flush,
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
		.name = "audptr",
	},
};

static void synth_flush(struct spk_synth *synth)
{
	int timeout = SPK_XMITR_TIMEOUT;
	while (spk_serial_tx_busy()) {
		if (!--timeout)
			break;
		udelay(1);
	}
	outb(SYNTH_CLEAR, speakup_info.port_tts);
	spk_serial_out(PROCSPEECH);
}

static void synth_version(struct spk_synth *synth)
{
	unsigned char test = 0;
	char synth_id[40] = "";
	spk_synth_immediate(synth, "\x05[Q]");
	synth_id[test] = spk_serial_in();
	if (synth_id[test] == 'A') {
		do {
			/* read version string from synth */
			synth_id[++test] = spk_serial_in();
		} while (synth_id[test] != '\n' && test < 32);
		synth_id[++test] = 0x00;
	}
	if (synth_id[0] == 'A')
		pr_info("%s version: %s", synth->long_name, synth_id);
}

static int synth_probe(struct spk_synth *synth)
{
	int failed = 0;

	failed = spk_serial_synth_probe(synth);
	if (failed == 0)
		synth_version(synth);
	synth->alive = !failed;
	return 0;
}

module_param_named(ser, synth_audptr.ser, int, S_IRUGO);
module_param_named(start, synth_audptr.startup, short, S_IRUGO);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

static int __init audptr_init(void)
{
	return synth_add(&synth_audptr);
}

static void __exit audptr_exit(void)
{
	synth_remove(&synth_audptr);
}

module_init(audptr_init);
module_exit(audptr_exit);
MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for Audapter synthesizer");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

