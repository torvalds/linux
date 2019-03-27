# $OpenPAM: Makefile.am 938 2017-04-30 21:34:42Z des $

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = su
su_SOURCES = su.c
if WITH_SYSTEM_LIBPAM
su_LDADD = $(SYSTEM_LIBPAM)
else
su_LDADD = $(top_builddir)/lib/libpam/libpam.la
endif

dist_man1_MANS = su.1
