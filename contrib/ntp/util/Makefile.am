NULL=

bin_PROGRAMS=	$(NTP_KEYGEN_DB) $(NTPTIME_DB) $(TICKADJ_DB) $(TIMETRIM_DB)
libexec_PROGRAMS=	$(NTP_KEYGEN_DL) $(NTPTIME_DL) $(TICKADJ_DL) $(TIMETRIM_DL)
sbin_PROGRAMS=	$(NTP_KEYGEN_DS) $(NTPTIME_DS) $(TICKADJ_DS) $(TIMETRIM_DS)

EXTRA_PROGRAMS=	audio-pcm byteorder hist jitter kern longsize ntp-keygen \
	ntptime pps-api precision sht testrs6000 tg tg2 tickadj timetrim

AM_CFLAGS  = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(LIBOPTS_CFLAGS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS  = $(LDFLAGS_NTP)
AM_LDFLAGS += $(NTP_HARD_LDFLAGS)

# LDADD might need RESLIB and ADJLIB
LDADD=		../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM) $(PTHREAD_LIBS)
tg2_LDADD=	../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM)
ntp_keygen_LDADD  = version.o $(LIBOPTS_LDADD) ../libntp/libntp.a
ntp_keygen_LDADD += $(LDADD_LIBEVENT)
ntp_keygen_LDADD += $(LDADD_LIBNTP) $(PTHREAD_LIBS) $(LDADD_NTP) $(LIBM)
ntp_keygen_SOURCES = ntp-keygen.c ntp-keygen-opts.c ntp-keygen-opts.h

tickadj_LDADD=	../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM) $(PTHREAD_LIBS) $(LDADD_NLIST)

EXTRA_DIST=				\
	invoke-ntp-keygen.menu		\
	invoke-ntp-keygen.texi		\
	ntp-keygen-opts.def		\
	ntp-keygen.1ntp-keygenman	\
	ntp-keygen.1ntp-keygenmdoc	\
	ntp-keygen.man.in		\
	ntp-keygen.mdoc.in		\
	ntp-keygen.mdoc.in		\
	ntp-keygen.html			\
	ntp-keygen.texi			\
	$(NULL)

BUILT_SOURCES=	ntp-keygen-opts.c ntp-keygen-opts.h
CLEANFILES=
DISTCLEANFILES=	.version version.c config.log $(man_MANS)

html_DATA=				\
	$(srcdir)/ntp-keygen.html	\
	$(NULL)

noinst_DATA=				\
	$(srcdir)/invoke-ntp-keygen.menu	\
	$(srcdir)/invoke-ntp-keygen.texi	\
	$(srcdir)/ntp-keygen.man.in	\
	$(srcdir)/ntp-keygen.mdoc.in	\
	$(man_MANS)			\
	$(NULL)

man1_MANS=
man8_MANS=
man_MANS=	ntp-keygen.$(NTP_KEYGEN_MS)

run_ag=		cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" AUTOGEN_DNE_DATE=-D	\
		autogen -L ../sntp/include -L ../sntp/ag-tpl --writable
std_def_list =							\
	$(top_srcdir)/sntp/include/debug-opt.def		\
	$(top_srcdir)/sntp/include/autogen-version.def 		\
	$(top_srcdir)/sntp/include/copyright.def 		\
	$(top_srcdir)/sntp/include/homerc.def 			\
	$(top_srcdir)/sntp/include/ntp.lic 			\
	$(top_srcdir)/sntp/include/version.def			\
	$(NULL)

$(srcdir)/ntp-keygen-opts.h: $(srcdir)/ntp-keygen-opts.c
	@: do-nothing action to avoid default SCCS get, .h built with .c

$(srcdir)/ntp-keygen-opts.c: $(srcdir)/ntp-keygen-opts.def $(std_def_list)
	$(run_ag) ntp-keygen-opts.def

###

$(srcdir)/ntp-keygen.1ntp-keygenman: $(srcdir)/ntp-keygen-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntp-keygenman -Tagman-cmd.tpl ntp-keygen-opts.def

$(srcdir)/ntp-keygen.man.in: $(srcdir)/ntp-keygen.1ntp-keygenman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp-keygen.1ntp-keygenman > $(srcdir)/ntp-keygen.man.in+
	mv $(srcdir)/ntp-keygen.man.in+ $(srcdir)/ntp-keygen.man.in

###

$(srcdir)/ntp-keygen.1ntp-keygenmdoc: $(srcdir)/ntp-keygen-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntp-keygenmdoc -Tagmdoc-cmd.tpl ntp-keygen-opts.def

$(srcdir)/ntp-keygen.mdoc.in: $(srcdir)/ntp-keygen.1ntp-keygenmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp-keygen.1ntp-keygenmdoc > $(srcdir)/ntp-keygen.mdoc.in+
	mv $(srcdir)/ntp-keygen.mdoc.in+ $(srcdir)/ntp-keygen.mdoc.in

###

ntp-keygen.$(NTP_KEYGEN_MS): $(srcdir)/ntp-keygen.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntp-keygen.$(NTP_KEYGEN_MS)+:$(srcdir)/ntp-keygen.$(MANTAGFMT).in
	mv ntp-keygen.$(NTP_KEYGEN_MS)+ ntp-keygen.$(NTP_KEYGEN_MS)

###

$(srcdir)/invoke-ntp-keygen.menu: $(srcdir)/invoke-ntp-keygen.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntp-keygen.texi: $(srcdir)/ntp-keygen-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section ntp-keygen-opts.def
	$(top_srcdir)/scripts/build/check--help $@

$(srcdir)/ntp-keygen.html: $(srcdir)/ntp-keygen.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o ntp-keygen.html ntp-keygen.texi || true )


jitter_SOURCES=	jitter.c
jitter_LDADD=

kern.o: kern.c
	$(COMPILE) -DHAVE_TIMEX_H -c kern.c

$(PROGRAMS): version.o

$(top_srcdir)/sntp/scm-rev:
	cd ../sntp && $(MAKE) $(AM_MAKEFLAGS) check-scm-rev

version.c: $(ntp_keygen_OBJECTS) ../libntp/libntp.a Makefile $(top_srcdir)/sntp/scm-rev
	env CSET=`cat $(top_srcdir)/sntp/scm-rev` $(top_builddir)/scripts/build/mkver ntp-keygen

version.o: version.c
	env CCACHE_DISABLE=1 $(COMPILE) -c version.c -o version.o

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/check-libopts.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
