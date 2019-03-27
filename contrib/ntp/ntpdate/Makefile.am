NULL =

BUILT_SOURCES =
CLEANFILES =

bin_PROGRAMS = $(NTPDATE_DB)
libexec_PROGRAMS = $(NTPDATE_DL)
sbin_PROGRAMS = $(NTPDATE_DS)

EXTRA_PROGRAMS = ntpdate

AM_CFLAGS = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS = $(LDFLAGS_NTP)
AM_LDFLAGS += $(NTP_HARD_LDFLAGS)

# LDADD might need RESLIB and ADJLIB
LDADD = version.o ../libntp/libntp.a
ntpdate_LDADD  = $(LDADD) $(LDADD_LIBNTP) $(PTHREAD_LIBS) $(LIBOPTS_LDADD)
ntpdate_LDADD += $(LIBM) $(LDADD_NTP)
DISTCLEANFILES=	.version version.c stamp-v
noinst_HEADERS=	ntpdate.h

$(PROGRAMS): $(LDADD)

$(top_srcdir)/sntp/scm-rev:
	cd ../sntp && $(MAKE) $(AM_MAKEFLAGS) check-scm-rev

version.c: $(ntpdate_OBJECTS) ../libntp/libntp.a Makefile $(top_srcdir)/sntp/scm-rev
	env CSET=`cat $(top_srcdir)/sntp/scm-rev` $(top_builddir)/scripts/build/mkver ntpdate
	
version.o: version.c
	env CCACHE_DISABLE=1 $(COMPILE) -c version.c -o version.o

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
