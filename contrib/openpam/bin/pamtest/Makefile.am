# $OpenPAM: Makefile.am 938 2017-04-30 21:34:42Z des $

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = pamtest
pamtest_SOURCES = pamtest.c
if WITH_SYSTEM_LIBPAM
pamtest_LDADD = $(SYSTEM_LIBPAM)
else
pamtest_LDADD = $(top_builddir)/lib/libpam/libpam.la
endif

dist_man1_MANS = pamtest.1
