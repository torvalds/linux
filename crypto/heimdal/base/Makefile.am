
include $(top_srcdir)/Makefile.am.common

lib_LTLIBRARIES = libheimbase.la
check_PROGRAMS = test_base

libheimbase_la_LDFLAGS = -version-info 1:0:0

if versionscript
libheimbase_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

include_HEADERS	= heimbase.h

dist_libheimbase_la_SOURCES =	\
	array.c			\
	baselocl.h		\
	bool.c			\
	dict.c			\
	heimbase.c		\
	heimbasepriv.h		\
	heimqueue.h		\
	null.c			\
	number.c		\
	string.c

libheimbase_la_DEPENDENCIES = version-script.map

test_base_LDADD = $(LIB_heimbase)

EXTRA_DIST = NTMakefile version-script.map
