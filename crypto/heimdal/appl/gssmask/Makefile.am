# $Id$

include $(top_srcdir)/Makefile.am.common

noinst_PROGRAMS = gssmask gssmaestro

gssmask_SOURCES = gssmask.c common.c common.h protocol.h

gssmaestro_SOURCES = gssmaestro.c common.c common.h protocol.h

LDADD = $(top_builddir)/lib/gssapi/libgssapi.la $(LIB_roken) $(top_builddir)/lib/krb5/libkrb5.la

EXTRA_DIST = NTMakefile
