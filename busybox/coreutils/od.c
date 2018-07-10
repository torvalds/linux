/* vi: set sw=4 ts=4: */
/*
 * od implementation for busybox
 * Based on code from util-linux v 2.11l
 *
 * Copyright (c) 1990
 * The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Original copyright notice is retained at the end of this file.
 */
//config:config OD
//config:	bool "od (11 kb)"
//config:	default y
//config:	help
//config:	od is used to dump binary files in octal and other formats.

//applet:IF_OD(APPLET(od, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_OD) += od.o

//usage:#if !ENABLE_DESKTOP
//usage:#define od_trivial_usage
//usage:       "[-aBbcDdeFfHhIiLlOovXx] [FILE]"
//usage:#define od_full_usage "\n\n"
//usage:       "Print FILE (or stdin) unambiguously, as octal bytes by default"
//usage:#endif

#include "libbb.h"
#if ENABLE_DESKTOP
/* This one provides -t (busybox's own build script needs it) */
#include "od_bloaty.c"
#else

#include "dump.h"

static void
odoffset(dumper_t *dumper, int argc, char ***argvp)
{
	char *num, *p;
	int base;
	char *end;

	/*
	 * The offset syntax of od(1) was genuinely bizarre.  First, if
	 * it started with a plus it had to be an offset.  Otherwise, if
	 * there were at least two arguments, a number or lower-case 'x'
	 * followed by a number makes it an offset.  By default it was
	 * octal; if it started with 'x' or '0x' it was hex.  If it ended
	 * in a '.', it was decimal.  If a 'b' or 'B' was appended, it
	 * multiplied the number by 512 or 1024 byte units.  There was
	 * no way to assign a block count to a hex offset.
	 *
	 * We assumes it's a file if the offset is bad.
	 */
	p = **argvp;

	if (!p) {
		/* hey someone is probably piping to us ... */
		return;
	}

	if ((*p != '+')
		&& (argc < 2
			|| (!isdigit(p[0])
				&& ((p[0] != 'x') || !isxdigit(p[1])))))
		return;

	base = 0;
	/*
	 * skip over leading '+', 'x[0-9a-fA-f]' or '0x', and
	 * set base.
	 */
	if (p[0] == '+')
		++p;
	if (p[0] == 'x' && isxdigit(p[1])) {
		++p;
		base = 16;
	} else if (p[0] == '0' && p[1] == 'x') {
		p += 2;
		base = 16;
	}

	/* skip over the number */
	if (base == 16)
		for (num = p; isxdigit(*p); ++p)
			continue;
	else
		for (num = p; isdigit(*p); ++p)
			continue;

	/* check for no number */
	if (num == p)
		return;

	/* if terminates with a '.', base is decimal */
	if (*p == '.') {
		if (base)
			return;
		base = 10;
	}

	dumper->dump_skip = strtol(num, &end, base ? base : 8);

	/* if end isn't the same as p, we got a non-octal digit */
	if (end != p)
		dumper->dump_skip = 0;
	else {
		if (*p) {
			if (*p == 'b') {
				dumper->dump_skip *= 512;
				++p;
			} else if (*p == 'B') {
				dumper->dump_skip *= 1024;
				++p;
			}
		}
		if (*p)
			dumper->dump_skip = 0;
		else {
			++*argvp;
			/*
			 * If the offset uses a non-octal base, the base of
			 * the offset is changed as well.  This isn't pretty,
			 * but it's easy.
			 */
#define TYPE_OFFSET 7
			{
				char x_or_d;
				if (base == 16) {
					x_or_d = 'x';
					goto DO_X_OR_D;
				}
				if (base == 10) {
					x_or_d = 'd';
 DO_X_OR_D:
					dumper->fshead->nextfu->fmt[TYPE_OFFSET]
						= dumper->fshead->nextfs->nextfu->fmt[TYPE_OFFSET]
						= x_or_d;
				}
			}
		}
	}
}

static const char *const add_strings[] = {
	"16/1 \"%3_u \" \"\\n\"",              /* a */
	"8/2 \" %06o \" \"\\n\"",              /* B, o */
	"16/1 \"%03o \" \"\\n\"",              /* b */
	"16/1 \"%3_c \" \"\\n\"",              /* c */
	"8/2 \"  %05u \" \"\\n\"",             /* d */
	"4/4 \"     %010u \" \"\\n\"",         /* D */
	"2/8 \"          %21.14e \" \"\\n\"",  /* e (undocumented in od), F */
	"4/4 \" %14.7e \" \"\\n\"",            /* f */
	"4/4 \"       %08x \" \"\\n\"",        /* H, X */
	"8/2 \"   %04x \" \"\\n\"",            /* h, x */
	"4/4 \"    %11d \" \"\\n\"",           /* I, L, l */
	"8/2 \" %6d \" \"\\n\"",               /* i */
	"4/4 \"    %011o \" \"\\n\"",          /* O */
};

static const char od_opts[] ALIGN1 = "aBbcDdeFfHhIiLlOoXxv";

static const char od_o2si[] ALIGN1 = {
	0, 1, 2, 3, 5,
	4, 6, 6, 7, 8,
	9, 0xa, 0xb, 0xa, 0xa,
	0xb, 1, 8, 9,
};

int od_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int od_main(int argc, char **argv)
{
	int ch;
	int first = 1;
	char *p;
	dumper_t *dumper = alloc_dumper();

	while ((ch = getopt(argc, argv, od_opts)) > 0) {
		if (ch == 'v') {
			dumper->dump_vflag = ALL;
		} else if (((p = strchr(od_opts, ch)) != NULL) && (*p != '\0')) {
			if (first) {
				first = 0;
				bb_dump_add(dumper, "\"%07.7_Ao\n\"");
				bb_dump_add(dumper, "\"%07.7_ao  \"");
			} else {
				bb_dump_add(dumper, "\"         \"");
			}
			bb_dump_add(dumper, add_strings[(int)od_o2si[(p - od_opts)]]);
		} else {  /* P, p, s, w, or other unhandled */
			bb_show_usage();
		}
	}
	if (!dumper->fshead) {
		bb_dump_add(dumper, "\"%07.7_Ao\n\"");
		bb_dump_add(dumper, "\"%07.7_ao  \" 8/2 \"%06o \" \"\\n\"");
	}

	argc -= optind;
	argv += optind;

	odoffset(dumper, argc, &argv);

	return bb_dump_dump(dumper, argv);
}
#endif /* ENABLE_DESKTOP */

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
