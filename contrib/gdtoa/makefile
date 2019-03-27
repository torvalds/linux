# /****************************************************************
# Copyright (C) 1998 by Lucent Technologies
# All Rights Reserved
#
# Permission to use, copy, modify, and distribute this software and
# its documentation for any purpose and without fee is hereby
# granted, provided that the above copyright notice appear in all
# copies and that both that the copyright notice and this
# permission notice and warranty disclaimer appear in supporting
# documentation, and that the name of Lucent or any of its entities
# not be used in advertising or publicity pertaining to
# distribution of the software without specific, written prior
# permission.
#
# LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
# IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
# SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
# IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
# ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
# THIS SOFTWARE.
#
# ****************************************************************/

.SUFFIXES: .c .o
CC = cc
CFLAGS = -g
AR ?= ar
ARFLAGS ?= ruv

.c.o:
	$(CC) -c $(CFLAGS) $*.c

# invoke "make Printf" to add printf.o to gdtoa.a (if desired)

all: arith.h gd_qnan.h gdtoa.a

arith.h: arithchk.c
	$(CC) $(CFLAGS) arithchk.c || $(CC) -DNO_LONG_LONG $(CFLAGS) arithchk.c
	./a.out >arith.h
	rm -f a.out arithchk.o

gd_qnan.h: arith.h qnan.c
	$(CC) $(CFLAGS) qnan.c
	./a.out >gd_qnan.h
	rm -f a.out qnan.o

gdtoa.a:  dmisc.c dtoa.c g_Qfmt.c g__fmt.c g_ddfmt.c g_dfmt.c\
	 g_ffmt.c g_xLfmt.c g_xfmt.c gdtoa.c gethex.c gmisc.c hd_init.c\
	 hexnan.c misc.c smisc.c strtoIQ.c strtoId.c strtoIdd.c\
	 strtoIf.c strtoIg.c strtoIx.c strtoIxL.c strtod.c strtodI.c\
	 strtodg.c strtof.c strtopQ.c strtopd.c strtopdd.c strtopf.c\
	 strtopx.c strtopxL.c strtorQ.c strtord.c strtordd.c strtorf.c\
	 strtorx.c strtorxL.c sum.c ulp.c
	$(CC) -c $(CFLAGS) $?
	x=`echo $? | sed 's/\.c/.o/g'` && $(AR) $(ARFLAGS) gdtoa.a $$x && rm $$x
	ranlib gdtoa.a || true

Printf: all printf.c
	$(CC) -c $(CFLAGS) printf.c
	$(AR) $(ARFLAGS) gdtoa.a printf.o
	rm printf.o
	touch Printf

# If your system lacks ranlib, you do not need it.

xs0 = README arithchk.c dmisc.c dtoa.c g_Qfmt.c g__fmt.c g_ddfmt.c\
	 g_dfmt.c g_ffmt.c g_xLfmt.c g_xfmt.c gdtoa.c gdtoa.h\
	 gdtoa_fltrnds.h gdtoaimp.h gethex.c gmisc.c hd_init.c hexnan.c\
	 makefile misc.c printf.c printf.c0 qnan.c smisc.c stdio1.h\
	 strtoIQ.c strtoId.c strtoIdd.c strtoIf.c strtoIg.c strtoIx.c\
	 strtoIxL.c strtod.c strtodI.c strtodg.c strtodnrp.c strtof.c\
	 strtopQ.c strtopd.c strtopdd.c strtopf.c strtopx.c strtopxL.c\
	 strtorQ.c strtord.c strtordd.c strtorf.c strtorx.c strtorxL.c\
	 sum.c ulp.c

# "make -r xsum.out" to check for transmission errors; source for xsum is
# netlib's "xsum.c from f2c", e.g.,
# ftp://netlib.bell-labs.com/netlib/f2c/xsum.c.gz

xsum.out: xsum0.out $(xs0)
	xsum $(xs0) >xsum1.out
	cmp xsum0.out xsum1.out && mv xsum1.out xsum.out || diff xsum[01].out

clean:
	rm -f arith.h gd_qnan.h *.[ao] Printf xsum.out xsum1.out
