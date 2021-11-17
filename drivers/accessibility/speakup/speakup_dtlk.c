// SPDX-License-Identifier: GPL-2.0+
/*
 * originally written by: Kirk Reiser <kirk@braille.uwo.ca>
 * this version considerably modified by David Borowski, david575@rogers.com
 *
 * Copyright (C) 1998-99  Kirk Reiser.
 * Copyright (C) 2003 David Borowski.
 *
 * specificly written as a driver for the speakup screenreview
 * package it's not a general device driver.
 * This driver is for the RC Systems DoubleTalk PC internal synthesizer.
 */
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kthread.h>

#include "spk_priv.h"
#include "serialio.h"
#include "speakup_dtlk.h" /* local header file for DoubleTalk values */
#include "speakup.h"

#define DRV_VERSION "2.10"
#define PROCSPEECH 0x00

static int synth_probe(struct spk_synth *synth);
static void dtlk_release(struct spk_synth *synth);
static const char *synth_immediate(struct spk_synth *synth, const char *buf);
static void do_catch_up(struct spk_synth *synth);
static void synth_flush(struct spk_synth *synth);

static int synth_lpc;
static int port_forced;
static unsigned int synth_portlist[] = {
		 0x25e, 0x29e, 0x2de, 0x31e, 0x35e, 0x39e, 0
};

static u_char synth_status;

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
 * These attributes will appear in /sys/accessibility/speakup/dtlk.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute freq_attribute =
	__ATTR(freq, 0644, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, 0644, spk_var_show, spk_var_store);
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

static struct spk_synth synth_dtlk = {
	.name = "dtlk",
	.version = DRV_VERSION,
	.long_name = "DoubleTalk PC",
	.init = "\x01@\x01\x31y",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 30,
	.jiffies = 50,
	.full = 1000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.io_ops = &spk_serial_io_ops,
	.probe = synth_probe,
	.release = dtlk_release,
	.synth_immediate = synth_immediate,
	.catch_up = do_catch_up,
	.flush = synth_flush,
	.is_alive = spk_synth_is_alive_nop,
	.synth_adjust = NULL,
	.read_buff_add = NULL,
	.get_index = spk_synth_get_index,
	.indexing = {
		.command = "\x01%di",
		.lowindex = 1,
		.highindex = 5,
		.currindex = 1,
	},
	.attributes = {
		.attrs = synth_attrs,
		.name = "dtlk",
	},
};

static inline bool synth_readable(void)
{
	synth_status = inb_p(speakup_info.port_tts + UART_RX);
	return (synth_status & TTS_READABLE) != 0;
}

static inline bool synth_writable(void)
{
	synth_status = inb_p(speakup_info.port_tts + UART_RX);
	return (synth_status & TTS_WRITABLE) != 0;
}

static inline bool synth_full(void)
{
	synth_status = inb_p(speakup_info.port_tts + UART_RX);
	return (synth_status & TTS_ALMOST_FULL) != 0;
}

static void spk_out(const char ch)
{
	int timeout = SPK_XMITR_TIMEOUT;

	while (!synth_writable()) {
		if (!--timeout)
			break;
		udelay(1);
	}
	outb_p(ch, speakup_info.port_tts);
	timeout = SPK_XMITR_TIMEOUT;
	while (synth_writable()) {
		if (!--timeout)
			break;
		udelay(1);
	}
}

static void do_catch_up(struct spk_synth *synth)
{
	u_char ch;
	unsigned long flags;
	unsigned long jiff_max;
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
		set_current_state(TASK_INTERRUPTIBLE);
		delay_time_val = delay_time->u.n.value;
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (synth_full()) {
			schedule_timeout(msecs_to_jiffies(delay_time_val));
			continue;
		}
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		ch = synth_buffer_getc();
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (ch == '\n')
			ch = PROCSPEECH;
		spk_out(ch);
		if (time_after_eq(jiffies, jiff_max) && (ch == SPACE)) {
			spk_out(PROCSPEECH);
			spin_lock_irqsave(&speakup_info.spinlock, flags);
			delay_time_val = delay_time->u.n.value;
			jiffy_delta_val = jiffy_delta->u.n.value;
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			schedule_timeout(msecs_to_jiffies(delay_time_val));
			jiff_max = jiffies + jiffy_delta_val;
		}
	}
	spk_out(PROCSPEECH);
}

static const char *synth_immediate(struct spk_synth *synth, const char *buf)
{
	u_char ch;

	while ((ch = (u_char)*buf)) {
		if (synth_full())
			return buf;
		if (ch == '\n')
			ch = PROCSPEECH;
		spk_out(ch);
		buf++;
	}
	return NULL;
}

static void synth_flush(struct spk_synth *synth)
{
	outb_p(SYNTH_CLEAR, speakup_info.port_tts);
	while (synth_writable())
		cpu_relax();
}

static char synth_read_tts(void)
{
	u_char ch;

	while (!synth_readable())
		cpu_relax();
	ch = synth_status & 0x7f;
	outb_p(ch, speakup_info.port_tts);
	while (synth_readable())
		cpu_relax();
	return (char)ch;
}

/* interrogate the DoubleTalk PC and return its settings */
static struct synth_settings *synth_interrogate(struct spk_synth *synth)
{
	u_char *t;
	static char buf[sizeof(struct synth_settings) + 1];
	int total, i;
	static struct synth_settings status;

	synth_immediate(synth, "\x18\x01?");
	for (total = 0, i = 0; i < 50; i++) {
		buf[total] = synth_read_tts();
		if (total > 2 && buf[total] == 0x7f)
			break;
		if (total < sizeof(struct synth_settings))
			total++;
	}
	t = buf;
	/* serial number is little endian */
	status.serial_number = t[0] + t[1] * 256;
	t += 2;
	for (i = 0; *t != '\r'; t++) {
		status.rom_version[i] = *t;
		if (i < sizeof(status.rom_version) - 1)
			i++;
	}
	status.rom_version[i] = 0;
	t++;
	status.mode = *t++;
	status.punc_level = *t++;
	status.formant_freq = *t++;
	status.pitch = *t++;
	status.speed = *t++;
	status.volume = *t++;
	status.tone = *t++;
	status.expression = *t++;
	status.ext_dict_loaded = *t++;
	status.ext_dict_status = *t++;
	status.free_ram = *t++;
	status.articulation = *t++;
	status.reverb = *t++;
	status.eob = *t++;
	return &status;
}

static int synth_probe(struct spk_synth *synth)
{
	unsigned int port_val = 0;
	int i = 0;
	struct synth_settings *sp;

	pr_info("Probing for DoubleTalk.\n");
	if (port_forced) {
		speakup_info.port_tts = port_forced;
		pr_info("probe forced to %x by kernel command line\n",
			speakup_info.port_tts);
		if ((port_forced & 0xf) != 0xf)
			pr_info("warning: port base should probably end with f\n");
		if (synth_request_region(speakup_info.port_tts - 1,
					 SYNTH_IO_EXTENT)) {
			pr_warn("sorry, port already reserved\n");
			return -EBUSY;
		}
		port_val = inw(speakup_info.port_tts - 1);
		synth_lpc = speakup_info.port_tts - 1;
	} else {
		for (i = 0; synth_portlist[i]; i++) {
			if (synth_request_region(synth_portlist[i],
						 SYNTH_IO_EXTENT))
				continue;
			port_val = inw(synth_portlist[i]) & 0xfbff;
			if (port_val == 0x107f) {
				synth_lpc = synth_portlist[i];
				speakup_info.port_tts = synth_lpc + 1;
				break;
			}
			synth_release_region(synth_portlist[i],
					     SYNTH_IO_EXTENT);
		}
	}
	port_val &= 0xfbff;
	if (port_val != 0x107f) {
		pr_info("DoubleTalk PC: not found\n");
		if (synth_lpc)
			synth_release_region(synth_lpc, SYNTH_IO_EXTENT);
		return -ENODEV;
	}
	while (inw_p(synth_lpc) != 0x147f)
		cpu_relax(); /* wait until it's ready */
	sp = synth_interrogate(synth);
	pr_info("%s: %03x-%03x, ROM ver %s, s/n %u, driver: %s\n",
		synth->long_name, synth_lpc, synth_lpc + SYNTH_IO_EXTENT - 1,
		sp->rom_version, sp->serial_number, synth->version);
	synth->alive = 1;
	return 0;
}

static void dtlk_release(struct spk_synth *synth)
{
	spk_stop_serial_interrupt();
	if (speakup_info.port_tts)
		synth_release_region(speakup_info.port_tts - 1,
				     SYNTH_IO_EXTENT);
	speakup_info.port_tts = 0;
}

module_param_hw_named(port, port_forced, int, ioport, 0444);
module_param_named(start, synth_dtlk.startup, short, 0444);

MODULE_PARM_DESC(port, "Set the port for the synthesizer (override probing).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

module_spk_synth(synth_dtlk);

MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for DoubleTalk PC synthesizers");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

