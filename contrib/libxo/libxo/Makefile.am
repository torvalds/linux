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
if HAVE_GCC
GCC_WARNINGS = yes
endif
include ${top_srcdir}/warnings.mk

libxoincdir = ${includedir}/libxo

AM_CFLAGS = \
    -I${top_srcdir} \
    ${WARNINGS} \
    ${GETTEXT_CFLAGS}

AM_CFLAGS += \
    -DXO_ENCODERDIR=\"${XO_ENCODERDIR}\"

lib_LTLIBRARIES = libxo.la

LIBS = \
    ${GETTEXT_LIBS}

libxoinc_HEADERS = \
     xo.h \
     xo_encoder.h

noinst_HEADERS = \
     xo_buf.h \
     xo_humanize.h \
     xo_wcwidth.h

libxo_la_SOURCES = \
    libxo.c \
    xo_encoder.c \
    xo_syslog.c

man3_files = \
    libxo.3 \
    xo_attr.3 \
    xo_create.3 \
    xo_emit.3 \
    xo_emit_err.3 \
    xo_err.3 \
    xo_error.3 \
    xo_finish.3 \
    xo_flush.3 \
    xo_message.3 \
    xo_no_setlocale.3 \
    xo_open_container.3 \
    xo_open_list.3 \
    xo_open_marker.3 \
    xo_parse_args.3 \
    xo_set_allocator.3 \
    xo_set_flags.3 \
    xo_set_info.3 \
    xo_set_options.3 \
    xo_set_style.3 \
    xo_set_syslog_enterprise_id.3 \
    xo_set_version.3 \
    xo_set_writer.3 \
    xo_syslog.3

man5_files = \
    xo_format.5

man7_files = \
    xo_options.7

man_MANS = ${man3_files} ${man5_files} ${man7_files}

EXTRA_DIST = \
    ${man_MANS}

call-graph:
	${RM} libxo.o
	${MAKE} CC="clang -Xclang -analyze -Xclang \
            -analyzer-checker=debug.ViewCallGraph" libxo.o

install-data-hook:
	for file in ${man3_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man3dir}/$$file ; done
	for file in ${man5_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man5dir}/$$file ; done
	for file in ${man7_files}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man7dir}/$$file ; done
