NULL=
BUILT_SOURCES =
CLEANFILES =
noinst_LIBRARIES = libntp.a @MAKE_LIBNTPSIM@
EXTRA_LIBRARIES = libntpsim.a

libisc_SRCS =								\
	$(srcdir)/../lib/isc/assertions.c				\
	$(srcdir)/../lib/isc/buffer.c					\
	$(srcdir)/../lib/isc/backtrace-emptytbl.c			\
	$(srcdir)/../lib/isc/backtrace.c				\
	$(srcdir)/../lib/isc/$(LIBISC_PTHREADS_NOTHREADS)/condition.c	\
	$(srcdir)/../lib/isc/unix/dir.c					\
	$(srcdir)/../lib/isc/error.c					\
	$(srcdir)/../lib/isc/unix/errno2result.c			\
	$(srcdir)/../lib/isc/event.c					\
	$(srcdir)/../lib/isc/unix/file.c				\
	$(srcdir)/../lib/isc/inet_ntop.c				\
	$(srcdir)/../lib/isc/inet_pton.c				\
	$(srcdir)/../lib/isc/unix/interfaceiter.c			\
	$(srcdir)/../lib/isc/lib.c					\
	$(srcdir)/../lib/isc/log.c					\
	$(srcdir)/../lib/isc/md5.c					\
	$(srcdir)/../lib/isc/nls/msgcat.c				\
	$(srcdir)/../lib/isc/unix/net.c					\
	$(srcdir)/../lib/isc/netaddr.c					\
	$(srcdir)/../lib/isc/netscope.c					\
	$(srcdir)/../lib/isc/ondestroy.c				\
	$(srcdir)/../lib/isc/random.c					\
	$(srcdir)/../lib/isc/result.c					\
	$(srcdir)/../lib/isc/unix/stdio.c				\
	$(srcdir)/../lib/isc/unix/stdtime.c				\
	$(srcdir)/../lib/isc/unix/strerror.c				\
	$(srcdir)/../lib/isc/task.c					\
	$(srcdir)/../lib/isc/$(LIBISC_PTHREADS_NOTHREADS)/thread.c	\
	$(srcdir)/../lib/isc/unix/time.c				\
	$(srcdir)/../lib/isc/sha1.c					\
	$(srcdir)/../lib/isc/sockaddr.c					\
	$(srcdir)/../lib/isc/tsmemcmp.c					\
	$(NULL)

if PTHREADS
libisc_SRCS += $(srcdir)/../lib/isc/pthreads/mutex.c
endif

libntp_a_SRCS =						\
	a_md5encrypt.c					\
	adjtime.c					\
	atoint.c					\
	atolfp.c					\
	atouint.c					\
	audio.c						\
	authkeys.c					\
	authreadkeys.c					\
	authusekey.c					\
	bsd_strerror.c					\
	buftvtots.c					\
	caljulian.c					\
	caltontp.c					\
	calyearstart.c					\
	clocktime.c					\
	clocktypes.c					\
	decodenetnum.c					\
	dofptoa.c					\
	dolfptoa.c					\
	emalloc.c					\
	findconfig.c					\
	getopt.c					\
	hextoint.c					\
	hextolfp.c					\
	humandate.c					\
	icom.c						\
	iosignal.c					\
	is_ip_address.c					\
	lib_strbuf.c					\
	libssl_compat.c					\
	machines.c					\
	mktime.c					\
	modetoa.c					\
	mstolfp.c					\
	msyslog.c					\
	netof.c						\
	ntp_calendar.c					\
	ntp_crypto_rnd.c				\
	ntp_intres.c					\
	ntp_libopts.c					\
	ntp_lineedit.c					\
	ntp_random.c					\
	ntp_rfc2553.c					\
	ntp_worker.c					\
	numtoa.c					\
	numtohost.c					\
	octtoint.c					\
	prettydate.c					\
	refidsmear.c					\
	recvbuff.c					\
	refnumtoa.c					\
	snprintf.c					\
	socket.c					\
	socktoa.c					\
	socktohost.c					\
	ssl_init.c					\
	statestr.c					\
	strdup.c					\
	strl_obsd.c					\
	syssignal.c					\
	timetoa.c					\
	timevalops.c					\
	uglydate.c					\
	vint64ops.c					\
	work_fork.c					\
	work_thread.c					\
	xsbprintf.c					\
	ymd2yd.c					\
	$(libisc_SRCS)					\
	$(NULL)

libntp_a_SOURCES = systime.c $(libntp_a_SRCS)
libntpsim_a_SOURCES = systime_s.c $(libntp_a_SRCS)
EXTRA_libntp_a_SOURCES = adjtimex.c

AM_CFLAGS = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(LIBOPTS_CFLAGS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS = $(NTP_HARD_LDFLAGS)

EXTRA_DIST = README

include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
