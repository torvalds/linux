/*
 * strfmt.c - printf implementation
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/ctype.h>
#include <linux/kallsyms.h>
#include "../include/ktap.h"

/* macro to `unsign' a character */
#define uchar(c)	((unsigned char)(c))

#define L_ESC		'%'

/* valid flags in a format specification */
#define FLAGS	"-+ #0"

#define INTFRMLEN	"ll"
#define INTFRM_T	long long

/*
 * maximum size of each format specification (such as '%-099.99d')
 * (+10 accounts for %99.99x plus margin of error)
 */
#define MAX_FORMAT	(sizeof(FLAGS) + sizeof(INTFRMLEN) + 10)

static const char *scanformat(ktap_state *ks, const char *strfrmt, char *form)
{
	const char *p = strfrmt;
	while (*p != '\0' && strchr(FLAGS, *p) != NULL)
		p++;  /* skip flags */

	if ((size_t)(p - strfrmt) >= sizeof(FLAGS)/sizeof(char)) {
		kp_error(ks, "invalid format (repeated flags)\n");
		return NULL;
	}

	if (isdigit(uchar(*p)))
		p++;  /* skip width */

	if (isdigit(uchar(*p)))
		p++;  /* (2 digits at most) */

	if (*p == '.') {
		p++;
		if (isdigit(uchar(*p)))
			p++;  /* skip precision */
		if (isdigit(uchar(*p)))
			p++;  /* (2 digits at most) */
	}

	if (isdigit(uchar(*p))) {
		kp_error(ks, "invalid format (width or precision too long)\n");
		return NULL;
	}

	*(form++) = '%';
	memcpy(form, strfrmt, (p - strfrmt + 1) * sizeof(char));
	form += p - strfrmt + 1;
	*form = '\0';
	return p;
}


/*
 * add length modifier into formats
 */
static void addlenmod(char *form, const char *lenmod)
{
	size_t l = strlen(form);
	size_t lm = strlen(lenmod);
	char spec = form[l - 1];

	strcpy(form + l - 1, lenmod);
	form[l + lm - 1] = spec;
	form[l + lm] = '\0';
}


static void ktap_argerror(ktap_state *ks, int narg, const char *extramsg)
{
	kp_error(ks, "bad argument #%d: (%s)\n", narg, extramsg);
}

int kp_strfmt(ktap_state *ks, struct trace_seq *seq)
{
	int arg = 1;
	size_t sfl;
	ktap_value *arg_fmt = kp_arg(ks, 1);
	int argnum = kp_arg_nr(ks);
	const char *strfrmt, *strfrmt_end;

	strfrmt = svalue(arg_fmt);
	sfl = rawtsvalue(arg_fmt)->tsv.len;
	strfrmt_end = strfrmt + sfl;

	while (strfrmt < strfrmt_end) {
		if (*strfrmt != L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else if (*++strfrmt == L_ESC)
			trace_seq_putc(seq, *strfrmt++);
		else { /* format item */
			char form[MAX_FORMAT];

			if (++arg > argnum) {
				ktap_argerror(ks, arg, "no value");
				return -1;
			}

			strfrmt = scanformat(ks, strfrmt, form);
			switch (*strfrmt++) {
			case 'c':
				trace_seq_printf(seq, form,
						 nvalue(kp_arg(ks, arg)));
				break;
			case 'd':  case 'i': {
				ktap_number n = nvalue(kp_arg(ks, arg));
				INTFRM_T ni = (INTFRM_T)n;
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 'p': {
				char str[KSYM_SYMBOL_LEN];
				SPRINT_SYMBOL(str, nvalue(kp_arg(ks, arg)));
				trace_seq_puts(seq, str);
				break;
			}
			case 'o':  case 'u':  case 'x':  case 'X': {
				ktap_number n = nvalue(kp_arg(ks, arg));
				unsigned INTFRM_T ni = (unsigned INTFRM_T)n;
				addlenmod(form, INTFRMLEN);
				trace_seq_printf(seq, form, ni);
				break;
			}
			case 's': {
				ktap_value *v = kp_arg(ks, arg);
				const char *s;
				size_t l;

				if (isnil(v)) {
					trace_seq_puts(seq, "nil");
					return 0;
				}

				if (ttisevent(v)) {
					kp_event_tostring(ks, seq);
					return 0;
				}

				s = svalue(v);
				l = rawtsvalue(v)->tsv.len;
				if (!strchr(form, '.') && l >= 100) {
					/*
					 * no precision and string is too long
					 * to be formatted;
					 * keep original string
					 */
					trace_seq_puts(seq, s);
					break;
				} else {
					trace_seq_printf(seq, form, s);
					break;
				}
			}
			default: /* also treat cases `pnLlh' */
				kp_error(ks, "invalid option " KTAP_QL("%%%c")
					     " to " KTAP_QL("format"),
					     *(strfrmt - 1));
			}
		}
	}

	return 0;
}

