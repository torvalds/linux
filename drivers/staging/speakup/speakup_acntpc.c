/*
 * written by: Kirk Reiser <kirk@braille.uwo.ca>
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
 * this code is specificly written as a driver for the speakup screenreview
 * package and is not a general device driver.
 * This driver is for the Aicom Acent PC internal synthesizer.
 */

#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kthread.h>

#include "spk_priv.h"
#include "serialio.h"
#include "speakup.h"
#include "speakup_acnt.h" /* local header file for Accent values */

#define DRV_VERSION "2.10"
#define PROCSPEECH '\r'

static int synth_probe(struct spk_synth *synth);
static void accent_release(void);
static const char *synth_immediate(struct spk_synth *synth, const char *buf);
static void do_catch_up(struct spk_synth *synth);
static void synth_flush(struct spk_synth *synth);

static int synth_port_control;
static int port_forced;
static unsigned int synth_portlist[] = { 0x2a8, 0 };

static struct var_t vars[] = {
	{ CAPS_START, .u.s = {"\033P8" } },
	{ CAPS_STOP, .u.s = {"\033P5" } },
	{ RATE, .u.n = {"\033R%c", 9, 0, 17, 0, 0, "0123456789abcdefgh" } },
	{ PITCH, .u.n = {"\033P%d", 5, 0, 9, 0, 0, NULL } },
	{ VOL, .u.n = {"\033A%d", 5, 0, 9, 0, 0, NULL } },
	{ TONE, .u.n = {"\033V%d", 5, 0, 9, 0, 0, NULL } },
	{ DIRECT, .u.n = {NULL, 0, 0, 1, 0, 0, NULL } },
	V_LAST_VAR
};

/*
 * These attributes will appear in /sys/accessibility/speakup/acntpc.
 */
static struct kobj_attribute caps_start_attribute =
	__ATTR(caps_start, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute caps_stop_attribute =
	__ATTR(caps_stop, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute pitch_attribute =
	__ATTR(pitch, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute rate_attribute =
	__ATTR(rate, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
static struct kobj_attribute tone_attribute =
	__ATTR(tone, S_IWUSR|S_IRUGO, spk_var_show, spk_var_store);
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

static struct spk_synth synth_acntpc = {
	.name = "acntpc",
	.version = DRV_VERSION,
	.long_name = "Accent PC",
	.init = "\033=X \033Oi\033T2\033=M\033N1\n",
	.procspeech = PROCSPEECH,
	.clear = SYNTH_CLEAR,
	.delay = 500,
	.trigger = 50,
	.jiffies = 50,
	.full = 1000,
	.startup = SYNTH_START,
	.checkval = SYNTH_CHECK,
	.vars = vars,
	.probe = synth_probe,
	.release = accent_release,
	.synth_immediate = synth_immediate,
	.catch_up = do_catch_up,
	.flush = synth_flush,
	.is_alive = spk_synth_is_alive_nop,
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
		.name = "acntpc",
	},
};

static inline bool synth_writable(void)
{
	return inb_p(synth_port_control) & SYNTH_WRITABLE;
}

static inline bool synth_full(void)
{
	return inb_p(speakup_info.port_tts + UART_RX) == 'F';
}

static const char *synth_immediate(struct spk_synth *synth, const char *buf)
{
	u_char ch;
	while ((ch = *buf)) {
		int timeout = SPK_XMITR_TIMEOUT;
		if (ch == '\n')
			ch = PROCSPEECH;
		if (synth_full())
			return buf;
		while (synth_writable()) {
			if (!--timeout)
				return buf;
			udelay(1);
		}
		outb_p(ch, speakup_info.port_tts);
		buf++;
	}
	return NULL;
}

static void do_catch_up(struct spk_synth *synth)
{
	u_char ch;
	unsigned long flags;
	unsigned long jiff_max;
	int timeout;
	int delay_time_val;
	int jiffy_delta_val;
	int full_time_val;
	struct var_t *delay_time;
	struct var_t *full_time;
	struct var_t *jiffy_delta;

	jiffy_delta = spk_get_var(JIFFY);
	delay_time = spk_get_var(DELAY);
	full_time = spk_get_var(FULL);

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
		if (synth_buffer_empty()) {
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			break;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		full_time_val = full_time->u.n.value;
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (synth_full()) {
			schedule_timeout(msecs_to_jiffies(full_time_val));
			continue;
		}
		set_current_state(TASK_RUNNING);
		timeout = SPK_XMITR_TIMEOUT;
		while (synth_writable()) {
			if (!--timeout)
				break;
			udelay(1);
		}
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		ch = synth_buffer_getc();
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		if (ch == '\n')
			ch = PROCSPEECH;
		outb_p(ch, speakup_info.port_tts);
		if (time_after_eq(jiffies, jiff_max) && ch == SPACE) {
			timeout = SPK_XMITR_TIMEOUT;
			while (synth_writable()) {
				if (!--timeout)
					break;
				udelay(1);
			}
			outb_p(PROCSPEECH, speakup_info.port_tts);
			spin_lock_irqsave(&speakup_info.spinlock, flags);
			jiffy_delta_val = jiffy_delta->u.n.value;
			delay_time_val = delay_time->u.n.value;
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			schedule_timeout(msecs_to_jiffies(delay_time_val));
			jiff_max = jiffies+jiffy_delta_val;
		}
	}
	timeout = SPK_XMITR_TIMEOUT;
	while (synth_writable()) {
		if (!--timeout)
			break;
		udelay(1);
	}
	outb_p(PROCSPEECH, speakup_info.port_tts);
}

static void synth_flush(struct spk_synth *synth)
{
	outb_p(SYNTH_CLEAR, speakup_info.port_tts);
}

static int synth_probe(struct spk_synth *synth)
{
	unsigned int port_val = 0;
	int i = 0;
	pr_info("Probing for %s.\n", synth->long_name);
	if (port_forced) {
		speakup_info.port_tts = port_forced;
		pr_info("probe forced to %x by kernel command line\n",
				speakup_info.port_tts);
		if (synth_request_region(speakup_info.port_tts-1,
					SYNTH_IO_EXTENT)) {
			pr_warn("sorry, port already reserved\n");
			return -EBUSY;
		}
		port_val = inw(speakup_info.port_tts-1);
		synth_port_control = speakup_info.port_tts-1;
	} else {
		for (i = 0; synth_portlist[i]; i++) {
			if (synth_request_region(synth_portlist[i],
						SYNTH_IO_EXTENT)) {
				pr_warn
				    ("request_region: failed with 0x%x, %d\n",
				     synth_portlist[i], SYNTH_IO_EXTENT);
				continue;
			}
			port_val = inw(synth_portlist[i]) & 0xfffc;
			if (port_val == 0x53fc) {
				/* 'S' and out&input bits */
				synth_port_control = synth_portlist[i];
				speakup_info.port_tts = synth_port_control+1;
				break;
			}
		}
	}
	port_val &= 0xfffc;
	if (port_val != 0x53fc) {
		/* 'S' and out&input bits */
		pr_info("%s: not found\n", synth->long_name);
		synth_release_region(synth_port_control, SYNTH_IO_EXTENT);
		synth_port_control = 0;
		return -ENODEV;
	}
	pr_info("%s: %03x-%03x, driver version %s,\n", synth->long_name,
		synth_port_control, synth_port_control+SYNTH_IO_EXTENT-1,
		synth->version);
	synth->alive = 1;
	return 0;
}

static void accent_release(void)
{
	if (speakup_info.port_tts)
		synth_release_region(speakup_info.port_tts-1, SYNTH_IO_EXTENT);
	speakup_info.port_tts = 0;
}

module_param_named(port, port_forced, int, S_IRUGO);
module_param_named(start, synth_acntpc.startup, short, S_IRUGO);

MODULE_PARM_DESC(port, "Set the port for the synthesizer (override probing).");
MODULE_PARM_DESC(start, "Start the synthesizer once it is loaded.");

static int __init acntpc_init(void)
{
	return synth_add(&synth_acntpc);
}

static void __exit acntpc_exit(void)
{
	synth_remove(&synth_acntpc);
}

module_init(acntpc_init);
module_exit(acntpc_exit);
MODULE_AUTHOR("Kirk Reiser <kirk@braille.uwo.ca>");
MODULE_AUTHOR("David Borowski");
MODULE_DESCRIPTION("Speakup support for Accent PC synthesizer");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

