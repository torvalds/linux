## Makefile.am for JMK's SNTP, by Harlan Stenn

ACLOCAL_AMFLAGS = -I m4 -I libevent/m4 -I libopts/m4

NULL =

AM_CFLAGS  = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(SNTP_INCS)
AM_CPPFLAGS += $(LIBOPTS_CFLAGS)
AM_CPPFLAGS += $(CPPFLAGS_LIBEVENT)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS  = $(LDFLAGS_NTP)
AM_LDFLAGS += $(NTP_HARD_LDFLAGS)

LDADD  = version.o
LDADD += libsntp.a
LDADD += $(LIBOPTS_LDADD)
LDADD += $(LDADD_LIBEVENT)
LDADD += ../libntp/libntp.a $(LDADD_LIBNTP) $(LIBM)
LDADD += $(PTHREAD_LIBS)
LDADD += $(LDADD_NTP)

run_ag =	cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" AUTOGEN_DNE_DATE=-D	\
		autogen -L include -L ag-tpl --writable
std_def_list =						\
	$(srcdir)/include/debug-opt.def			\
	$(srcdir)/include/autogen-version.def 		\
	$(srcdir)/include/copyright.def 		\
	$(srcdir)/include/homerc.def 			\
	$(srcdir)/include/ntp.lic 			\
	$(srcdir)/include/version.def			\
	$(NULL)

EXTRA_PROGRAMS = sntp

bin_PROGRAMS =		@SNTP_DB@
libexec_PROGRAMS =	@SNTP_DL@
sbin_PROGRAMS =		@SNTP_DS@

SUBDIRS = include scripts unity
DIST_FAIL =

if BUILD_LIBEVENT
SUBDIRS += libevent
else
DIST_FAIL += "--enable-local-libevent"
endif

if NEED_LIBOPTS
SUBDIRS += libopts
endif

if BUILD_SNTP
noinst_LIBRARIES = libsntp.a
SUBDIRS += tests
endif

libsntp_a_SOURCES =		\
	crypto.c		\
	kod_management.c	\
	log.c			\
	main.c			\
	networking.c		\
	sntp-opts.c		\
	utilities.c		\
	$(NULL)

sntp_SOURCES =		\
	sntp.c		\
	$(NULL)

noinst_HEADERS =		\
	crypto.h		\
	data_formats.h		\
	kod_management.h	\
	log.h			\
	main.h			\
	networking.h		\
	sntp-opts.h		\
	utilities.h		\
	$(NULL)

DISTCLEANFILES =			\
	.version			\
	version.c			\
	config.log			\
	$(man_MANS)			\
	$(NULL)

EXTRA_DIST =				\
	$(srcdir)/COPYRIGHT		\
	ag-tpl				\
	deps-ver			\
	harden				\
	invoke-sntp.menu		\
	invoke-sntp.texi		\
	@NTP_FORCE_LIBEVENT_DIST@	\
	libpkgver 			\
	loc				\
	sntp-opts.def			\
	sntp.1sntpman			\
	sntp.1sntpmdoc			\
	sntp.man.in			\
	sntp.mdoc.in			\
	sntp.html			\
	sntp.texi			\
	unity/auto			\
	$(srcdir)/scm-rev		\
	$(srcdir)/m4/version.m4		\
	$(NULL)

BUILT_SOURCES =				\
	$(srcdir)/COPYRIGHT		\
	libtool				\
	$(srcdir)/sntp-opts.c		\
	$(srcdir)/sntp-opts.h		\
	check-scm-rev			\
	$(srcdir)/include/version.def	\
	$(srcdir)/m4/version.m4		\
	$(srcdir)/include/version.texi	\
	$(NULL)

CLEANFILES =				\
	built-sources-only		\
	check-COPYRIGHT-submake		\
	$(NULL)

man1_MANS=
man8_MANS=
man_MANS=	sntp.$(SNTP_MS)

## HMS: Real Soon Now...
##info_TEXINFOS=	sntp.texi
##sntp_TEXINFOS=	invoke-sntp.texi

html_DATA=				\
	$(srcdir)/sntp.html		\
	$(NULL)

noinst_DATA=				\
	$(srcdir)/invoke-sntp.menu	\
	$(srcdir)/invoke-sntp.texi	\
	$(srcdir)/sntp.man.in		\
	$(srcdir)/sntp.mdoc.in		\
	$(NULL)

install-data-local: install-html

FRC:
	@: do-nothing action to prevent default SCCS get
	@: FRC "force" depends on nothing and is not a file, so is
	@: always out-of-date causing targets which depend on it to
	@: similarly always be outdated causing their rules to fire
	@: each time they or a dependent is built.

$(PROGRAMS): version.o

## We probably need something about libevent, too
## That is probably not possible since LDADD_LIBEVENT may be
## non-file "-levent_core".

version.c: $(sntp_OBJECTS) ../libntp/libntp.a Makefile $(srcdir)/scm-rev
	env CSET=`cat $(srcdir)/scm-rev` $(top_builddir)/../scripts/build/mkver sntp

version.o: version.c
	env CCACHE_DISABLE=1 $(COMPILE) -c version.c -o version.o

check-autogen-version.def: FRC
	@cd $(srcdir)							\
	&& test -r ../include/autogen-version.def			\
	&& ( if cmp -s ../include/autogen-version.def autogen-version.def; \
	   then : ;							\
	   else cp ../include/autogen-version.def autogen-version.def;	 \
		echo "Installing new sntp/autogen-version.def file";	\
	   fi )

$(srcdir)/autogen-version.def:
	$(MAKE) $(AM_MAKEFLAGS) check-autogen-version.def

built-sources-only: $(BUILT_SOURCES)
	@: do-nothing action to avoid default SCCS get

## HMS: The next bit is still suboptimal.  We'll get an error if this is
## a bk repo and srcdir or scm-rev is unwritable.

check-scm-rev: $(srcdir)/scm-rev
	@: do-nothing

$(srcdir)/scm-rev:
	-bk root $(srcdir) >/dev/null 2>&1 &&			\
	    cd $(srcdir)/.. &&					\
	    x=`bk -R prs -hr+ -nd:I: ChangeSet` &&		\
	    y=`cat sntp/scm-rev 2>/dev/null` || true &&		\
	    case "$$x" in ''|$$y) ;; *) echo $$x > sntp/scm-rev ;; esac

$(srcdir)/m4/version.m4: $(srcdir)/../packageinfo.sh
	TEMPDIR=`pwd` && export TEMPDIR && cd $(srcdir) && \
	../scripts/build/genver m4/version.m4

$(srcdir)/include/version.def: $(srcdir)/../packageinfo.sh
	TEMPDIR=`pwd` && export TEMPDIR && cd $(srcdir) && \
	../scripts/build/genver include/version.def

$(srcdir)/include/version.texi: $(srcdir)/../packageinfo.sh
	TEMPDIR=`pwd` && export TEMPDIR && cd $(srcdir) && \
	../scripts/build/genver include/version.texi

$(srcdir)/../COPYRIGHT:
	cd .. && $(MAKE) $(AM_MAKEFLAGS) COPYRIGHT-please

check-COPYRIGHT-submake: $(srcdir)/../COPYRIGHT
	@cmp -s $(srcdir)/../COPYRIGHT $(srcdir)/COPYRIGHT	\
	 || { cp $(srcdir)/../COPYRIGHT $(srcdir)		\
	      && echo 'updated sntp/COPYRIGHT installed' ;}
	@echo 'submake avoided if this file is newer than parent COPYRIGHT' > $@

check-COPYRIGHT: FRC
	[ ! -r $(srcdir)/../COPYRIGHT ]					\
	|| [ check-COPYRIGHT-submake -nt $(srcdir)/../COPYRIGHT ]	\
	|| $(MAKE) $(AM_MAKEFLAGS) check-COPYRIGHT-submake

$(srcdir)/COPYRIGHT: check-COPYRIGHT
	@: do-nothing action to prevent any default

$(srcdir)/sntp-opts.h: $(srcdir)/sntp-opts.c
	@: do-nothing action to avoid default SCCS get, .h built with .c

$(srcdir)/sntp-opts.c: $(srcdir)/sntp-opts.def $(std_def_list)
	$(run_ag) sntp-opts.def

###

$(srcdir)/sntp.1sntpman: $(srcdir)/sntp-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1sntpman -Tagman-cmd.tpl sntp-opts.def

$(srcdir)/sntp.man.in: $(srcdir)/sntp.1sntpman $(srcdir)/scripts/mansec2subst.sed
	sed -f $(srcdir)/scripts/mansec2subst.sed $(srcdir)/sntp.1sntpman > $(srcdir)/sntp.man.in+
	mv $(srcdir)/sntp.man.in+ $(srcdir)/sntp.man.in

###

$(srcdir)/sntp.1sntpmdoc: $(srcdir)/sntp-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1sntpmdoc -Tagmdoc-cmd.tpl sntp-opts.def

$(srcdir)/sntp.mdoc.in: $(srcdir)/sntp.1sntpmdoc $(srcdir)/scripts/mansec2subst.sed
	sed -f $(srcdir)/scripts/mansec2subst.sed $(srcdir)/sntp.1sntpmdoc > $(srcdir)/sntp.mdoc.in+
	mv $(srcdir)/sntp.mdoc.in+ $(srcdir)/sntp.mdoc.in

###

sntp.$(SNTP_MS): $(srcdir)/sntp.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=sntp.$(SNTP_MS)+:$(srcdir)/sntp.$(MANTAGFMT).in
	mv sntp.$(SNTP_MS)+ sntp.$(SNTP_MS)

###

$(srcdir)/invoke-sntp.menu: $(srcdir)/invoke-sntp.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-sntp.texi: $(srcdir)/sntp-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section sntp-opts.def
	$(top_srcdir)/../scripts/build/check--help $@

$(srcdir)/sntp.html: $(srcdir)/invoke-sntp.menu $(srcdir)/invoke-sntp.texi $(srcdir)/sntp.texi $(srcdir)/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o sntp.html sntp.texi || true )

libtool: $(LIBTOOL_DEPS)
	./config.status --recheck

# HMS: libevent/ is a target if it's in EXTRA_DIST via NTP_FORCE_LIBEVENT_DIST.
# Note that libevent/ is already in DIST_SUBDIRS (implicit in Makefile.am
# but explicit in Makefile.in).  This check doesn't help with distclean.
libevent: distdir-pre-check

# HMS: Stops the build for gmake or pmake
distdir-pre-check:
	case "$(DIST_FAIL)" in	\
	 '')  ;;		\
	 *) $(error re-run configure adding $(DIST_FAIL) if you want to make a distribution.);	\
	    echo "re-run configure adding $(DIST_FAIL) if you want to make a distribution.";	\
	    exit 1 ;;		\
	esac

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/check-libntp.mf
include $(top_srcdir)/check-libopts.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
