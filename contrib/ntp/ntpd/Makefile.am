NULL=

bin_PROGRAMS=	$(NTPD_DB) $(NTPDSIM_DB)
libexec_PROGRAMS=	$(NTPD_DL) $(NTPDSIM_DL)
sbin_PROGRAMS=	$(NTPD_DS) $(NTPDSIM_DS)

noinst_LIBRARIES=	libntpd.a

AM_CFLAGS = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += $(LIBOPTS_CFLAGS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS = $(LDFLAGS_NTP)
AM_LDFLAGS += $(NTP_HARD_LDFLAGS)

# LDADD might need RESLIB and ADJLIB.
LDADD = version.o libntpd.a $(LIBPARSE)
AM_YFLAGS = -d -t -r all

if SAVECONFIG_ENABLED
if NTP_CROSSCOMPILE
CHECK_SAVECONFIG=
else
CHECK_SAVECONFIG= check-saveconfig
endif
else !SAVECONFIG_ENABLED
CHECK_SAVECONFIG=
endif

#
# VPHACK and VPHACK_AFTER are enabled on non-GNU makes (such as
# BSD make) to work around issues specific to compiling
# ntp_parser.y into ntp_parser.h and ntp_parser.c in a VPATH
# configuration where we would like (for a change) the output
# files ntp_parser.[ch] to be placed in the source directory,
# as opposed to the build directory.  This allows a single
# host of a flock configured with Bison to update ntp_parser.[ch]
# used by the rest.
#

if VPATH_HACK
VPHACK=	vphack
VPHACK_AFTER= vphack_after
else
VPHACK=
VPHACK_AFTER=
endif

vphack:
	test -e ntp_parser.c || ln -s $(srcdir)/ntp_parser.c .
	test -e ntp_parser.h || ln -s $(srcdir)/ntp_parser.h .

#
# ylwrap script which invokes Bison replaces ntp_parser.h
# symlink with the updated file, when ntp_parser.h changes.
# vphack_after detects this and copies the updated file to srcdir
# and re-creates the ntp_parser.h symlink in its place.
#

vphack_after:
	test -L ntp_parser.h || ( \
		mv ntp_parser.h $(srcdir)/ntp_parser.h && \
		ln -s $(srcdir)/ntp_parser.h . \
		)

# BUILT_SOURCES which should also be in EXTRA_DIST
B_S_DIST=			\
	$(srcdir)/ntpd-opts.c	\
	$(srcdir)/ntpd-opts.h	\
	$(NULL)

BUILT_SOURCES=		\
	$(VPHACK)	\
	$(LIBPARSE)	\
	ntp_parser.c	\
	ntp_parser.h	\
	$(VPHACK_AFTER)	\
	$(B_S_DIST)	\
	$(NULL)

man1_MANS=
man5_MANS=	ntp.conf.5 ntp.keys.5
man8_MANS=
man_MANS=	ntpd.$(NTPD_MS)

# ntpdsim.1 is a remnant along with all the ntpdsim-opts.* files, the
# simulator currently uses ntpd-opts.[ch].  This also means there is no
# longer a reason to have ntpdbase-opts.def split off of ntpd-opts.def.

LDADD_NTPD_COMMON  = $(LDADD_LIBNTP) $(LIBOPTS_LDADD) $(PTHREAD_LIBS)
LDADD_NTPD_COMMON += $(LIBM) $(LDADD_NTP) $(LSCF)
ntpd_LDADD = $(LDADD) ../libntp/libntp.a $(LDADD_NTPD_COMMON) $(LDADD_LIBUTIL)
ntpdsim_LDADD = $(LDADD) ../libntp/libntpsim.a $(LDADD_NTPD_COMMON)
ntpdsim_CPPFLAGS = $(AM_CPPFLAGS) -DSIM
check_y2k_LDADD = $(LDADD) ../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM) $(LDADD_LIBNTP) $(PTHREAD_LIBS)
## we don't want $(LDADD) in keyword_gen_LDADD
keyword_gen_LDADD = ../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM) $(PTHREAD_LIBS)

DISTCLEANFILES =		\
	keyword-gen		\
	.version		\
	version.c		\
	config.log		\
	$(man5_MANS)		\
	$(man_MANS)		\
	$(NULL)

CLEANFILES =			\
	check-saveconfig	\
	compsave.conf		\
	k-g-u-submake		\
	$(EXTRA_PROGRAMS)	\
	$(NULL)

EXTRA_DIST =			\
	complete.conf.in	\
	invoke-ntp.conf.menu	\
	invoke-ntp.conf.texi	\
	invoke-ntp.keys.menu	\
	invoke-ntp.keys.texi	\
	invoke-ntpd.menu	\
	invoke-ntpd.texi	\
	keyword-gen-utd		\
	ntp.conf.5man		\
	ntp.conf.5mdoc		\
	ntp.conf.def		\
	ntp.conf.man.in		\
	ntp.conf.mdoc.in	\
	ntp.conf.html		\
	ntp.conf.texi		\
	ntp.keys.5man		\
	ntp.keys.5mdoc		\
	ntp.keys.def		\
	ntp.keys.man.in		\
	ntp.keys.mdoc.in	\
	ntp.keys.html		\
	ntp.keys.texi		\
	ntpd-opts.def		\
	ntpd.1ntpdman		\
	ntpd.1ntpdmdoc		\
	ntpd.man.in		\
	ntpd.mdoc.in		\
	ntpd.html		\
	ntpd.texi		\
	ntpdbase-opts.def	\
	refclock_msfees.c	\
	$(B_S_DIST)		\
	$(NULL)

###							Y2Kfixes
check_PROGRAMS = @MAKE_CHECK_Y2K@
EXTRA_PROGRAMS = check_y2k keyword-gen ntpd ntpdsim

html_DATA=				\
	$(srcdir)/ntp.conf.html		\
	$(srcdir)/ntp.keys.html		\
	$(srcdir)/ntpd.html		\
	$(NULL)

noinst_DATA =				\
	$(srcdir)/invoke-ntp.conf.menu	\
	$(srcdir)/invoke-ntp.conf.texi	\
	$(srcdir)/invoke-ntp.keys.menu	\
	$(srcdir)/invoke-ntp.keys.texi	\
	$(srcdir)/invoke-ntpd.menu	\
	$(srcdir)/invoke-ntpd.texi	\
	$(srcdir)/ntp.conf.man.in	\
	$(srcdir)/ntp.conf.mdoc.in	\
	$(srcdir)/ntp.keys.man.in	\
	$(srcdir)/ntp.keys.mdoc.in	\
	$(srcdir)/ntpd.man.in		\
	$(srcdir)/ntpd.mdoc.in		\
	$(NULL)

noinst_HEADERS =			\
	declcond.h			\
	ntp_leapsec.h			\
	$(NULL)

install-data-local: install-html

run_ag=		cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" AUTOGEN_DNE_DATE=-D	\
		autogen -L ../sntp/include -L ../sntp/ag-tpl --writable
std_def_list =							\
	$(top_srcdir)/sntp/include/debug-opt.def		\
	$(top_srcdir)/sntp/include/autogen-version.def		\
	$(top_srcdir)/sntp/include/copyright.def		\
	$(top_srcdir)/sntp/include/homerc.def			\
	$(top_srcdir)/sntp/include/ntp.lic			\
	$(top_srcdir)/sntp/include/version.def			\
	$(NULL)

check-local: $(MAKE_CHECK_Y2K) $(CHECK_SAVECONFIG)
	test -z "$(MAKE_CHECK_Y2K)" || ./$(MAKE_CHECK_Y2K)

ntpd_SOURCES =			\
	ntp_config.c		\
	ntp_keyword.h		\
	ntp_io.c		\
	ntp_parser.y		\
	ntp_scanner.c		\
	ntp_scanner.h		\
	ntpd.c			\
	ntpd-opts.c		\
	ntpd-opts.h		\
	$(NULL)

ntpdsim_SOURCES =		\
	$(ntpd_SOURCES)		\
	ntp_prio_q.c		\
	ntpsim.c		\
	$(NULL)

# libntpd_a_SOURCES do not use #ifdef SIM

libntpd_a_SOURCES =		\
	cmd_args.c		\
	jupiter.h		\
	ntp_control.c		\
	ntp_crypto.c		\
	ntp_filegen.c		\
	ntp_leapsec.c		\
	ntp_loopfilter.c	\
	ntp_monitor.c		\
	ntp_peer.c		\
	ntp_proto.c		\
	ntp_refclock.c		\
	ntp_request.c		\
	ntp_restrict.c		\
	ntp_signd.c		\
	ntp_timer.c		\
	ntp_util.c		\
	ppsapi_timepps.h	\
	rc_cmdlength.c		\
	refclock_acts.c		\
	refclock_arbiter.c	\
	refclock_arc.c		\
	refclock_as2201.c	\
	refclock_atom.c		\
	refclock_bancomm.c	\
	refclock_chronolog.c	\
	refclock_chu.c		\
	refclock_conf.c		\
	refclock_datum.c	\
	refclock_dumbclock.c	\
	refclock_fg.c		\
	refclock_gpsdjson.c	\
	refclock_gpsvme.c	\
	refclock_heath.c	\
	refclock_hopfser.c	\
	refclock_hopfpci.c	\
	refclock_hpgps.c	\
	refclock_irig.c		\
	refclock_jjy.c		\
	refclock_jupiter.c	\
	refclock_leitch.c	\
	refclock_local.c	\
	refclock_mx4200.c	\
	refclock_neoclock4x.c	\
	refclock_nmea.c		\
	refclock_oncore.c	\
	refclock_palisade.c	\
	refclock_palisade.h	\
	refclock_parse.c	\
	refclock_pcf.c		\
	refclock_pst.c		\
	refclock_ripencc.c	\
	refclock_shm.c		\
	refclock_tpro.c		\
	refclock_true.c		\
	refclock_tt560.c	\
	refclock_ulink.c	\
	refclock_wwv.c		\
	refclock_wwvb.c		\
	refclock_zyfer.c	\
	refclock_tsyncpci.c	\
	$(NULL)

k-g-u-submake: keyword-gen
	./keyword-gen $(srcdir)/ntp_parser.h > k-g.out
	@grep -v diff_ignore_line < k-g.out > cmp1
	@grep -v diff_ignore_line < $(srcdir)/ntp_keyword.h > cmp2
	@cmp cmp1 cmp2 > /dev/null || \
		{ mv -f k-g.out $(srcdir)/ntp_keyword.h && \
		  echo 'Generated changed ntp_keyword.h.' ;}
	@[ ! -f k-g.out ] || \
		{ rm k-g.out && echo 'ntp_keyword.h is up to date.' ;}
	@rm cmp1 cmp2
	@echo 'keyword-gen and ntp_keyword.h are up to date.' > $@

$(srcdir)/keyword-gen-utd: $(srcdir)/keyword-gen.c $(srcdir)/ntp_parser.h
	$(MAKE) $(AM_MAKEFLAGS) k-g-u-submake	# avoid explicit dependency
	grep diff_ignore_line $(srcdir)/ntp_keyword.h > k-g-u
	mv -f k-g-u $@

$(srcdir)/ntp_keyword.h: $(srcdir)/keyword-gen-utd
	@: do-nothing action to avoid default SCCS get
	@: .h updated if needed by k-g-u-submake rule

$(srcdir)/ntpd-opts.h: $(srcdir)/ntpd-opts.c
	@: do-nothing action to avoid default SCCS get, .h built with .c

$(srcdir)/ntpd-opts.c: $(srcdir)/ntpd-opts.def $(srcdir)/ntpdbase-opts.def $(std_def_list)
	$(run_ag) ntpd-opts.def

###

$(srcdir)/ntpd.1ntpdman: $(srcdir)/ntpd-opts.def $(srcdir)/ntpdbase-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpdman -Tagman-cmd.tpl ntpd-opts.def

$(srcdir)/ntpd.man.in: $(srcdir)/ntpd.1ntpdman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpd.1ntpdman > $(srcdir)/ntpd.man.in+
	mv $(srcdir)/ntpd.man.in+ $(srcdir)/ntpd.man.in

###

$(srcdir)/ntpd.1ntpdmdoc: $(srcdir)/ntpd-opts.def $(srcdir)/ntpdbase-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpdmdoc -Tagmdoc-cmd.tpl ntpd-opts.def

$(srcdir)/ntpd.mdoc.in: $(srcdir)/ntpd.1ntpdmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpd.1ntpdmdoc > $(srcdir)/ntpd.mdoc.in+
	mv $(srcdir)/ntpd.mdoc.in+ $(srcdir)/ntpd.mdoc.in

###

ntpd.$(NTPD_MS): $(srcdir)/ntpd.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntpd.$(NTPD_MS)+:$(srcdir)/ntpd.$(MANTAGFMT).in
	mv ntpd.$(NTPD_MS)+ ntpd.$(NTPD_MS)

###

$(srcdir)/invoke-ntp.conf.menu: $(srcdir)/invoke-ntp.conf.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntp.conf.texi: $(srcdir)/ntp.conf.def $(std_def_list)
	$(run_ag) -Tagtexi-file.tpl -DLEVEL=section ntp.conf.def

$(srcdir)/invoke-ntp.keys.menu: $(srcdir)/invoke-ntp.keys.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntp.keys.texi: $(srcdir)/ntp.keys.def $(std_def_list)
	$(run_ag) -Tagtexi-file.tpl -DLEVEL=section ntp.keys.def

$(srcdir)/ntp.conf.html: $(srcdir)/ntp.conf.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o ntp.conf.html ntp.conf.texi || true )

$(srcdir)/ntp.keys.html: $(srcdir)/ntp.keys.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o ntp.keys.html ntp.keys.texi || true )

$(srcdir)/ntpd.html: $(srcdir)/ntpd.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o ntpd.html ntpd.texi || true )

###

$(srcdir)/ntp.conf.5man: $(srcdir)/ntp.conf.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=5man -Tagman-cmd.tpl ntp.conf.def

$(srcdir)/ntp.conf.man.in: $(srcdir)/ntp.conf.5man $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp.conf.5man > $(srcdir)/ntp.conf.man.in+
	mv $(srcdir)/ntp.conf.man.in+ $(srcdir)/ntp.conf.man.in

###

$(srcdir)/ntp.conf.5mdoc: $(srcdir)/ntp.conf.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=5mdoc -Tagmdoc-cmd.tpl ntp.conf.def

$(srcdir)/ntp.conf.mdoc.in: $(srcdir)/ntp.conf.5mdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp.conf.5mdoc > $(srcdir)/ntp.conf.mdoc.in+
	mv $(srcdir)/ntp.conf.mdoc.in+ $(srcdir)/ntp.conf.mdoc.in

###

ntp.conf.5: $(srcdir)/ntp.conf.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntp.conf.5+:$(srcdir)/ntp.conf.$(MANTAGFMT).in
	mv ntp.conf.5+ ntp.conf.5

###

$(srcdir)/ntp.keys.5man: $(srcdir)/ntp.keys.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=5man -Tagman-file.tpl ntp.keys.def

$(srcdir)/ntp.keys.man.in: $(srcdir)/ntp.keys.5man $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp.keys.5man > $(srcdir)/ntp.keys.man.in+
	mv $(srcdir)/ntp.keys.man.in+ $(srcdir)/ntp.keys.man.in

###

$(srcdir)/ntp.keys.5mdoc: $(srcdir)/ntp.keys.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=5mdoc -Tagmdoc-file.tpl ntp.keys.def

$(srcdir)/ntp.keys.mdoc.in: $(srcdir)/ntp.keys.5mdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp.keys.5mdoc > $(srcdir)/ntp.keys.mdoc.in+
	mv $(srcdir)/ntp.keys.mdoc.in+ $(srcdir)/ntp.keys.mdoc.in

###

ntp.keys.5: $(srcdir)/ntp.keys.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntp.keys.5+:$(srcdir)/ntp.keys.$(MANTAGFMT).in
	mv ntp.keys.5+ ntp.keys.5

###

$(srcdir)/invoke-ntpd.menu: $(srcdir)/invoke-ntpd.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntpd.texi: $(srcdir)/ntpd-opts.def $(srcdir)/ntpdbase-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section ntpd-opts.def
	$(top_srcdir)/scripts/build/check--help $@

$(PROGRAMS): $(LDADD)

compsave.conf: ntpd complete.conf
	./ntpd --configfile complete.conf --saveconfigquit $@

check-saveconfig: complete.conf compsave.conf
	-diff -u complete.conf compsave.conf
	cmp complete.conf compsave.conf && echo stamp > $@

../libntp/libntpsim.a:
	cd ../libntp && $(MAKE) $(AM_MAKEFLAGS) libntpsim.a

../libparse/libparse.a:
	cd ../libparse && $(MAKE) $(AM_MAKEFLAGS) check-libparse

$(top_srcdir)/sntp/scm-rev:
	cd ../sntp && $(MAKE) $(AM_MAKEFLAGS) check-scm-rev

version.c: $(ntpd_OBJECTS) ../libntp/libntp.a @LIBPARSE@ Makefile $(top_srcdir)/sntp/scm-rev
	env CSET=`cat $(top_srcdir)/sntp/scm-rev` $(top_builddir)/scripts/build/mkver ntpd

version.o: version.c
	env CCACHE_DISABLE=1 $(COMPILE) -c version.c -o version.o

$(srcdir)/Makefile.am:
	@: do-nothing

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/check-libopts.mf
include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
