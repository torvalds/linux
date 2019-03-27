NULL=

BUILT_SOURCES =
CLEANFILES =
noinst_PROGRAMS = @TESTDCF@ @DCFD@
EXTRA_PROGRAMS = testdcf dcfd

AM_CFLAGS  = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS = $(NTP_HARD_LDFLAGS)

LDADD = ../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM)

ETAGS_ARGS = Makefile.am
DISTCLEANFILES = $(EXTRA_PROGRAMS)

check-local: @DCFD@
	case "$(noinst_PROGRAMS)" in	\
	 *dcfd*) ./dcfd -Y ;;		\
	esac

include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
