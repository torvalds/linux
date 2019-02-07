/* SPDX-License-Identifier: GPL-2.0+ */
/* spk_priv.h
 * review functions for the speakup screen review package.
 * originally written by: Kirk Reiser and Andy Berdan.
 *
 * extensively modified by David Borowski.
 *
 * Copyright (C) 1998  Kirk Reiser.
 * Copyright (C) 2003  David Borowski.
 */
#ifndef _SPEAKUP_PRIVATE_H
#define _SPEAKUP_PRIVATE_H

#include "spk_types.h"
#include "spk_priv_keyinfo.h"

#ifndef pr_warn
#define pr_warn(fmt, arg...) printk(KERN_WARNING fmt, ##arg)
#endif

#define V_LAST_VAR { MAXVARS }
#define SPACE 0x20
#define SYNTH_CHECK 20030716 /* today's date ought to do for check value */
/* synth flags, for odd synths */
#define SF_DEC 1 /* to fiddle puncs in alpha strings so it doesn't spell */
#ifdef MODULE
#define SYNTH_START 1
#else
#define SYNTH_START 0
#endif

#define KT_SPKUP 15
#define SPK_SYNTH_TIMEOUT 100000 /* in micro-seconds */
#define SYNTH_DEFAULT_DEV "ttyS0"
#define SYNTH_DEFAULT_SER 0

const struct old_serial_port *spk_serial_init(int index);
void spk_stop_serial_interrupt(void);
int spk_wait_for_xmitr(struct spk_synth *in_synth);
void spk_serial_release(void);
void spk_ttyio_release(void);
void spk_ttyio_register_ldisc(void);
void spk_ttyio_unregister_ldisc(void);

void synth_buffer_skip_nonlatin1(void);
u16 synth_buffer_getc(void);
u16 synth_buffer_peek(void);
int synth_buffer_empty(void);
struct var_t *spk_get_var(enum var_id_t var_id);
ssize_t spk_var_show(struct kobject *kobj, struct kobj_attribute *attr,
		     char *buf);
ssize_t spk_var_store(struct kobject *kobj, struct kobj_attribute *attr,
		      const char *buf, size_t count);

int spk_serial_synth_probe(struct spk_synth *synth);
int spk_ttyio_synth_probe(struct spk_synth *synth);
const char *spk_serial_synth_immediate(struct spk_synth *synth,
				       const char *buff);
const char *spk_ttyio_synth_immediate(struct spk_synth *synth,
				      const char *buff);
void spk_do_catch_up(struct spk_synth *synth);
void spk_do_catch_up_unicode(struct spk_synth *synth);
void spk_synth_flush(struct spk_synth *synth);
unsigned char spk_synth_get_index(struct spk_synth *synth);
int spk_synth_is_alive_nop(struct spk_synth *synth);
int spk_synth_is_alive_restart(struct spk_synth *synth);
__printf(1, 2)
void synth_printf(const char *buf, ...);
void synth_putwc(u16 wc);
void synth_putwc_s(u16 wc);
void synth_putws(const u16 *buf);
void synth_putws_s(const u16 *buf);
int synth_request_region(unsigned long start, unsigned long n);
int synth_release_region(unsigned long start, unsigned long n);
int synth_add(struct spk_synth *in_synth);
void synth_remove(struct spk_synth *in_synth);

extern struct speakup_info_t speakup_info;

extern struct var_t synth_time_vars[];

extern struct spk_io_ops spk_serial_io_ops;
extern struct spk_io_ops spk_ttyio_ops;

#endif
