# $Id$

include $(top_srcdir)/Makefile.am.common

YFLAGS = -d

lib_LTLIBRARIES = libcom_err.la
libcom_err_la_LDFLAGS = -version-info 2:3:1

if versionscript
libcom_err_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

libcom_err_la_LIBADD = $(LIB_libintl)

bin_PROGRAMS = compile_et

include_HEADERS = com_err.h com_right.h

compile_et_SOURCES = compile_et.c compile_et.h parse.y lex.l lex.h

libcom_err_la_CPPFLAGS = $(ROKEN_RENAME) $(INCLUDE_libintl)
dist_libcom_err_la_SOURCES = error.c com_err.c roken_rename.h

if do_roken_rename
nodist_libcom_err_la_SOURCES = snprintf.c strlcpy.c
endif

libcom_err_la_DEPENDENCIES = version-script.map

$(compile_et_OBJECTS): parse.h parse.c ## XXX broken automake 1.4s

compile_et_LDADD = \
	libcom_err.la \
	$(LIB_roken) \
	$(LEXLIB)

snprintf.c:
	$(LN_S) $(srcdir)/../roken/snprintf.c .
strlcpy.c:
	$(LN_S) $(srcdir)/../roken/strlcpy.c .

EXTRA_DIST = \
	NTMakefile \
	compile_et-version.rc \
	libcom_err-version.rc \
	libcom_err-exports.def \
	version-script.map
