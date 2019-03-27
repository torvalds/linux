#
ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libblacklist.la
include_HEADERS = ../include/blacklist.h

bin_PROGRAMS = blacklistd blacklistctl srvtest cltest

VPATH = ../bin:../lib:../test:../include

AM_CPPFLAGS = -I../include  -DDOT="."
AM_CFLAGS = @WARNINGS@

libblacklist_la_SOURCES = bl.c blacklist.c
libblacklist_la_LDFLAGS = -no-undefined -version-info 0:0:0
libblacklist_la_LIBADD = $(LTLIBOBJS)

SRCS = internal.c support.c run.c conf.c state.c
blacklistd_SOURCES = blacklistd.c ${SRCS}
blacklistd_LDADD = libblacklist.la
blacklistctl_SOURCES = blacklistctl.c ${SRCS}
blacklistctl_LDADD = libblacklist.la
srvtest_SOURCES = srvtest.c ${SRCS}
srvtest_LDADD = libblacklist.la
cltest_SOURCES = cltest.c ${SRCS}
cltest_LDADD = libblacklist.la
