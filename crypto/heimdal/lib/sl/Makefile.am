# $Id$

include $(top_srcdir)/Makefile.am.common

if do_roken_rename
ES = strtok_r.c snprintf.c strdup.c strupr.c getprogname.c
endif

AM_CPPFLAGS += $(ROKEN_RENAME)

YFLAGS = -d

include_HEADERS = sl.h

lib_LTLIBRARIES = libsl.la
libsl_la_LDFLAGS = -version-info 2:1:2

libsl_la_LIBADD = @LIB_readline@

dist_libsl_la_SOURCES = sl_locl.h sl.c roken_rename.h
nodist_libsl_la_SOURCES = $(ES)

TESTS = test_sl
check_PROGRAMS = $(TESTS)	

# install these?

libexec_heimdal_PROGRAMS = slc

slc_SOURCES = slc-gram.y slc-lex.l slc.h

CLEANFILES = snprintf.c strtok_r.c strdup.c strupr.c getprogname.c slc-lex.c

LDADD =	libsl.la $(LIB_roken)

slc_LDADD = $(LEXLIB) $(LDADD)

strtok_r.c:
	$(LN_S) $(srcdir)/../roken/strtok_r.c .
snprintf.c:
	$(LN_S) $(srcdir)/../roken/snprintf.c .
strdup.c:
	$(LN_S) $(srcdir)/../roken/strdup.c .
strupr.c:
	$(LN_S) $(srcdir)/../roken/strupr.c .
getprogname.c:
	$(LN_S) $(srcdir)/../roken/getprogname.c .

slc-lex.c: slc-gram.h

EXTRA_DIST = NTMakefile
