#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if LIBXO_WARNINGS_HIGH
LIBXO_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -DLIBXO_XMLSOFT_NEED_PRIVATE \
    -I${top_builddir} \
    -I${top_srcdir} \
    -I${top_srcdir}/libxo \
    ${WARNINGS}

LIBS = \
    ${LIBXO_LIBS}

bin_PROGRAMS = xo

xo_SOURCES = xo.c
#xo_LDADD = ../libxo/libxo.la
#xo_LDFLAGS = -static

LDADD = \
    ${top_builddir}/libxo/libxo.la

if HAVE_HUMANIZE_NUMBER
LDADD += -lutil
endif

man_MANS = xo.1

EXTRA_DIST = xo.1

install-data-hook:
	for file in ${man_MANS}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man1dir}/$$file ; done
