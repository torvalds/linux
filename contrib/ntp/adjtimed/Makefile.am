## adjtimed Makefile.am

bin_PROGRAMS = $(ADJTIMED_DB)
libexec_PROGRAMS = $(ADJTIMED_DL)
sbin_PROGRAMS =	$(ADJTIMED_DS)

BUILT_SOURCES =
CLEANFILES =
EXTRA_PROGRAMS = adjtimed

AM_CFLAGS  = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

LDADD = ../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM) $(PTHREAD_LIBS)
AM_LDFLAGS = $(NTP_HARD_LDFLAGS)

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
