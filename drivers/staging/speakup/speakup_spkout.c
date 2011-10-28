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
#define SYNTH_CLEAR 0x18
#define PROCSPEECH '\r'

static void synth_flush(struct spk_synth *synth);

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"\x05P+" } },
	{ CAPS_STOP, .u.s = {"\x05P-" } },
	{ RATE, .u.n = {"\x05R%d", 7, 0, 9, 0, 0, NULL } },
	{ PITCH, .u.n = {"\x05P%d", 3, 0, 9, 0, 0, NULL } },
	{ VOL, .u.n = {"\x05V%d", 9, 0, 9, 0, 0, NULL } },
	{ TONE, .u.n = {"\x05T%c", 8, 0, 25, 65, 0, NULL } },
	{ PUNCT, .u.n = {"\x05M%c", 0, 0, 3, 0, 0, "nsma" } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/spkout.
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

static struct spk_synth synth_spkout = {
	.name = "spkout",
	.version = DRV_VERSION,
	.long_name = "Speakout",
	.init = "\005W1\005I2\005C3",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 40000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.probe = serial_synth_probe,
	.release = spk_serial_release,
	.synth_immediate = spk_synth_immediate,
	.catch_up = spk_do_catch_up,
	.flush = synth_flush,
	.is_alive = spk_synth_is_alive_restart,
	.synth_adjust = NULL,
	.read_buff_add = NULL,
	.get_index = spk_serial_in_nowait,
	.indexing = {
		.command = "\x05[%c",
		.lowindex = 1,
		.highindex = 5,
		.currindex = 1,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "spkout",
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
}

module_param_named(ser, synth_spkout.ser, int, S_IRUGO);
module_param_named(start, synth_spkout.startup, short, S_IRUGO);

MODULE_PARM_DESC(ser, "Set the serial port for the synthesizer (0-based).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

static int __init spkout_init(void)
{
	return synth_add(&synth_spkout);
}

static void __exit spkout_exit(void)
{
	synth_remove(&synth_spkout);
}

module_init(spkout_init);
module_exit(spkout_exit);
MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for Speak Out synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

