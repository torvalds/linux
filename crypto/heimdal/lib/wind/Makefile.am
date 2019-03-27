# $Id: Makefile.am,v 1.1 2004/12/20 08:31:45 assar Exp $

include $(top_srcdir)/Makefile.am.common

lib_LTLIBRARIES = libwind.la

built = \
	bidi_table.c				\
	bidi_table.h				\
	combining_table.c			\
	combining_table.h			\
	errorlist_table.c			\
	errorlist_table.h			\
	map_table.c				\
	map_table.h				\
	normalize_table.c			\
	normalize_table.h

built_tests = \
	punycode_examples.h			\
	punycode_examples.c


MAINTAINERCLEANFILES = $(built) $(built_tests)

code = \
	bidi.c					\
	combining.c				\
	doxygen.c				\
	errorlist.c				\
	map.c					\
	ldap.c					\
	normalize.c				\
	punycode.c				\
	stringprep.c				\
	wind.h					\
	windlocl.h				\
	utf8.c

$(code:.c=.lo): $(built)

dist_libwind_la_SOURCES = $(code) $(built)
nodist_libwind_la_SOURCES = wind_err.c wind_err.h

libwind_la_DEPENDENCIES = version-script.map

dist_include_HEADERS = wind.h
nodist_include_HEADERS = wind_err.h

$(libwind_la_OBJECTS): wind_err.h

libwind_la_LDFLAGS = -version-info 0:0:0

libwind_la_LIBADD = \
	$(LIB_roken) \
	$(LIB_com_err)

if versionscript
libwind_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

BUILT_SOURCES =					\
	wind_err.c				\
	wind_err.h

TESTS =						\
	$(check_PROGRAMS)

check_PROGRAMS =				\
	test-bidi				\
	test-map				\
	test-rw					\
	test-normalize				\
	test-prohibited				\
	test-punycode				\
	test-ldap				\
	test-utf8

test_punycode_SOURCES =				\
	test-punycode.c				\
	punycode_examples.c			\
	punycode_examples.h

$(test_punycode_OBJECTS): $(built_tests)

bin_PROGRAMS = idn-lookup

idn_lookup_SOURCES = idn-lookup.c

LDADD = libwind.la $(LIB_roken)

PYTHON = python

if !MAINTAINER_MODE
skip_python = test -f $@ ||
endif

map_table.h map_table.c: rfc3454.txt gen-map.py stringprep.py
	$(skip_python) $(PYTHON) $(srcdir)/gen-map.py $(srcdir)/rfc3454.txt $(builddir)

errorlist_table.h errorlist_table.c: rfc3454.txt gen-errorlist.py stringprep.py
	$(skip_python) $(PYTHON) $(srcdir)/gen-errorlist.py $(srcdir)/rfc3454.txt $(builddir)

normalize_table.h normalize_table.c: UnicodeData.txt CompositionExclusions-3.2.0.txt gen-normalize.py
	$(skip_python) $(PYTHON) $(srcdir)/gen-normalize.py $(srcdir)/UnicodeData.txt $(srcdir)/CompositionExclusions-3.2.0.txt $(builddir)

combining_table.h combining_table.c: UnicodeData.txt gen-combining.py
	$(skip_python) $(PYTHON) $(srcdir)/gen-combining.py $(srcdir)/UnicodeData.txt $(builddir)

bidi_table.h bidi_table.c: rfc3454.txt gen-bidi.py
	$(skip_python) $(PYTHON) $(srcdir)/gen-bidi.py $(srcdir)/rfc3454.txt $(builddir)

punycode_examples.h punycode_examples.c: gen-punycode-examples.py rfc3492.txt
	$(PYTHON) $(srcdir)/gen-punycode-examples.py $(srcdir)/rfc3492.txt $(builddir)

EXTRA_DIST = \
	NTMakefile \
	libwind-exports.def \
	CompositionExclusions-3.2.0.txt \
	DerivedNormalizationProps.txt \
	NormalizationCorrections.txt \
	NormalizationTest.txt \
	UnicodeData.py \
	UnicodeData.txt \
	gen-bidi.py \
	gen-combining.py \
	gen-errorlist.py \
	gen-map.py \
	gen-normalize.py \
	gen-punycode-examples.py \
	generate.py \
	rfc3454.py \
	rfc3454.txt \
	rfc3490.txt \
	rfc3491.txt \
	rfc3492.txt \
	rfc4013.txt \
	rfc4518.py \
	rfc4518.txt \
	stringprep.py \
	util.py \
	version-script.map \
	wind_err.et

CLEANFILES = \
	wind_err.c  \
	wind_err.h

wind_err.h: wind_err.et
