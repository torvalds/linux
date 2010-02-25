/*
 * Beat hypervisor console driver
 *
 * (C) Copyright 2006 TOSHIBA CORPORATION
 *
 * This code is based on drivers/char/hvc_rtas.c:
 * (C) Copyright IBM Corporation 2001-2005
 * (C) Copyright Red Hat, Inc. 2005
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/console.h>
#include <asm/prom.h>
#include <asm/hvconsole.h>
#include <asm/firmware.h>

#include "hvc_console.h"

extern int64_t beat_get_term_char(uint64_t, uint64_t *, uint64_t *, uint64_t *);
extern int64_t beat_put_term_char(uint64_t, uint64_t, uint64_t, uint64_t);

struct hvc_struct *hvc_beat_dev = NULL;

/* bug: only one queue is available regardless of vtermno */
static int hvc_beat_get_chars(uint32_t vtermno, char *buf, int cnt)
{
	static unsigned char q[sizeof(unsigned long) * 2]
		__attribute__((aligned(sizeof(unsigned long))));
	static int qlen = 0;
	u64 got;

again:
	if (qlen) {
		if (qlen > cnt) {
			memcpy(buf, q, cnt);
			qlen -= cnt;
			memmove(q + cnt, q, qlen);
			return cnt;
		} else {	/* qlen <= cnt */
			int	r;

			memcpy(buf, q, qlen);
			r = qlen;
			qlen = 0;
			return r;
		}
	}
	if (beat_get_term_char(vtermno, &got,
		((u64 *)q), ((u64 *)q) + 1) == 0) {
		qlen = got;
		goto again;
	}
	return 0;
}

static int hvc_beat_put_chars(uint32_t vtermno, const char *buf, int cnt)
{
	unsigned long kb[2];
	int rest, nlen;

	for (rest = cnt; rest > 0; rest -= nlen) {
		nlen = (rest > 16) ? 16 : rest;
		memcpy(kb, buf, nlen);
		beat_put_term_char(vtermno, nlen, kb[0], kb[1]);
		buf += nlen;
	}
	return cnt;
}

static const struct hv_ops hvc_beat_get_put_ops = {
	.get_chars = hvc_beat_get_chars,
	.put_chars = hvc_beat_put_chars,
};

static int hvc_beat_useit = 1;

static int hvc_beat_config(char *p)
{
	hvc_beat_useit = simple_strtoul(p, NULL, 0);
	return 0;
}

static int __init hvc_beat_console_init(void)
{
	if (hvc_beat_useit && machine_is_compatible("Beat")) {
		hvc_instantiate(0, 0, &hvc_beat_get_put_ops);
	}
	return 0;
}

/* temp */
static int __init hvc_beat_init(void)
{
	struct hvc_struct *hp;

	if (!firmware_has_feature(FW_FEATURE_BEAT))
		return -ENODEV;

	hp = hvc_alloc(0, NO_IRQ, &hvc_beat_get_put_ops, 16);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	hvc_beat_dev = hp;
	return 0;
}

static void __exit hvc_beat_exit(void)
{
	if (hvc_beat_dev)
		hvc_remove(hvc_beat_dev);
}

module_init(hvc_beat_init);
module_exit(hvc_beat_exit);

__setup("hvc_beat=", hvc_beat_config);

console_initcall(hvc_beat_console_init);
