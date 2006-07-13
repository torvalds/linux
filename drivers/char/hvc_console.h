/*
 * hvc_console.h
 * Copyright (C) 2005 IBM Corporation
 *
 * Author(s):
 * 	Ryan S. Arnold <rsa@us.ibm.com>
 *
 * hvc_console header information:
 *      moved here from include/asm-powerpc/hvconsole.h
 *      and drivers/char/hvc_console.c
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef HVC_CONSOLE_H
#define HVC_CONSOLE_H

/*
 * This is the max number of console adapters that can/will be found as
 * console devices on first stage console init.  Any number beyond this range
 * can't be used as a console device but is still a valid tty device.
 */
#define MAX_NR_HVC_CONSOLES	16

/*
 * The Linux TTY code does not support dynamic addition of tty derived devices
 * so we need to know how many tty devices we might need when space is allocated
 * for the tty device.  Since this driver supports hotplug of vty adapters we
 * need to make sure we have enough allocated.
 */
#define HVC_ALLOC_TTY_ADAPTERS	8


/* implemented by a low level driver */
struct hv_ops {
	int (*get_chars)(uint32_t vtermno, char *buf, int count);
	int (*put_chars)(uint32_t vtermno, const char *buf, int count);
};

struct hvc_struct;

/* Register a vterm and a slot index for use as a console (console_init) */
extern int hvc_instantiate(uint32_t vtermno, int index, struct hv_ops *ops);

/* register a vterm for hvc tty operation (module_init or hotplug add) */
extern struct hvc_struct * __devinit hvc_alloc(uint32_t vtermno, int irq,
				struct hv_ops *ops, int outbuf_size);
/* remove a vterm from hvc tty operation (modele_exit or hotplug remove) */
extern int __devexit hvc_remove(struct hvc_struct *hp);

#endif // HVC_CONSOLE_H
