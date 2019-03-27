# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_krb4)

bin_PROGRAMS = rcp

rcp_SOURCES  = rcp.c util.c rcp_locl.h extern.h

man_MANS = rcp.1

EXTRA_DIST = NTMakefile $(man_MANS)

LDADD = $(LIB_roken)
