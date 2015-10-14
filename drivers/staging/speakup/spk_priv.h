/* spk_priv.h
 * review functions for the speakup screen review package.
 * originally written by: Kirk Reiser and Andy Berdan.
 *
 * extensively modified by David Borowski.
 *
 * Copyright (C) 1998  Kirk Reiser.
 * Copyright (C) 2003  David Borowski.

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

const struct old_serial_port *spk_serial_init(int index);
void spk_stop_serial_interrupt(void);
int spk_wait_for_xmitr(void);
unsigned char spk_serial_in(void);
unsigned char spk_serial_in_nowait(void);
int spk_serial_out(const char ch);
void spk_serial_release(void);

char synth_buffer_getc(void);
char synth_buffer_peek(void);
int synth_buffer_empty(void);
struct var_t *spk_get_var(enum var_id_t var_id);
ssize_t spk_var_show(struct kobject *kobj, struct kobj_attribute *attr,
		     char *buf);
ssize_t spk_var_store(struct kobject *kobj, struct kobj_attribute *attr,
		      const char *buf, size_t count);

int spk_serial_synth_probe(struct spk_synth *synth);
const char *spk_synth_immediate(struct spk_synth *synth, const char *buff);
void spk_do_catch_up(struct spk_synth *synth);
void spk_synth_flush(struct spk_synth *synth);
int spk_synth_is_alive_nop(struct spk_synth *synth);
int spk_synth_is_alive_restart(struct spk_synth *synth);
void synth_printf(const char *buf, ...);
int synth_request_region(u_long, u_long);
int synth_release_region(u_long, u_long);
int synth_add(struct spk_synth *in_synth);
void synth_remove(struct spk_synth *in_synth);

extern struct speakup_info_t speakup_info;

extern struct var_t synth_time_vars[];

#endif
