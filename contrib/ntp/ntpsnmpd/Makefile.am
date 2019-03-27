NULL=

bin_PROGRAMS=	$(NTPSNMPD_DB)
libexec_PROGRAMS=	$(NTPSNMPD_DL)
sbin_PROGRAMS=	$(NTPSNMPD_DS)

EXTRA_PROGRAMS=	ntpsnmpd
ntpsnmpd_SOURCES= netsnmp_daemonize.c ntpsnmpd.c ntpSnmpSubagentObject.c \
		ntpsnmpd-opts.c ntpsnmpd-opts.h ntpSnmpSubagentObject.h \
		ntp_snmp.h
# HMS: we probably want a version.o file here, too.
LDADD  = ../ntpq/libntpq.a ../libntp/libntp.a
LDADD += $(SNMP_LIBS) $(LDADD_LIBNTP) $(LIBM)
LDADD += $(PTHREAD_LIBS) $(LDADD_NTP) $(LIBOPTS_LDADD)

AM_CFLAGS  = $(SNMP_CFLAGS) $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = -I$(top_srcdir)/ntpq
AM_CPPFLAGS += $(NTP_INCS)
AM_CPPFLAGS += $(LIBOPTS_CFLAGS)
AM_CPPFLAGS += $(SNMP_CPPFLAGS)
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS  = $(LDFLAGS_NTP)
AM_LDFLAGS += $(NTP_HARD_LDFLAGS)

EXTRA_DIST=			\
	invoke-ntpsnmpd.menu	\
	invoke-ntpsnmpd.texi	\
	ntpsnmpd-opts.def	\
	ntpsnmpd.1ntpsnmpdman	\
	ntpsnmpd.1ntpsnmpdmdoc	\
	ntpsnmpd.man.in		\
	ntpsnmpd.mdoc.in	\
	ntpsnmpd.html		\
	ntpsnmpd.texi		\
	ntpv4-mib.mib		\
	$(NULL)

BUILT_SOURCES=	ntpsnmpd-opts.c ntpsnmpd-opts.h
CLEANFILES=
DISTCLEANFILES=	config.log $(man_MANS)

html_DATA=				\
	$(srcdir)/ntpsnmpd.html		\
	$(NULL)

noinst_DATA=				\
	$(srcdir)/invoke-ntpsnmpd.menu	\
	$(srcdir)/invoke-ntpsnmpd.texi	\
	$(srcdir)/ntpsnmpd.man.in	\
	$(srcdir)/ntpsnmpd.mdoc.in	\
	$(NULL)

install-data-local: install-html

man1_MANS=
man8_MANS=
man_MANS=	ntpsnmpd.$(NTPSNMPD_MS)

run_ag=		cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" AUTOGEN_DNE_DATE=-D	\
		autogen -L ../sntp/include -L ../sntp/ag-tpl --writable
std_def_list =							\
	$(top_srcdir)/sntp/include/debug-opt.def		\
	$(top_srcdir)/sntp/include/autogen-version.def 		\
	$(top_srcdir)/sntp/include/copyright.def 		\
	$(top_srcdir)/sntp/include/homerc.def 			\
	$(top_srcdir)/sntp/include/ntp.lic			\
	$(top_srcdir)/sntp/include/version.def			\
	$(NULL)

$(srcdir)/ntpsnmpd-opts.h: $(srcdir)/ntpsnmpd-opts.c
	@: do-nothing action to avoid default SCCS get, .h built with .c
	
$(srcdir)/ntpsnmpd-opts.c: $(srcdir)/ntpsnmpd-opts.def $(std_def_list)
	$(run_ag) ntpsnmpd-opts.def

###

$(srcdir)/ntpsnmpd.1ntpsnmpdman: $(srcdir)/ntpsnmpd-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpsnmpdman -Tagman-cmd.tpl ntpsnmpd-opts.def

$(srcdir)/ntpsnmpd.man.in: $(srcdir)/ntpsnmpd.1ntpsnmpdman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpsnmpd.1ntpsnmpdman > $(srcdir)/ntpsnmpd.man.in+
	mv $(srcdir)/ntpsnmpd.man.in+ $(srcdir)/ntpsnmpd.man.in

###

$(srcdir)/ntpsnmpd.1ntpsnmpdmdoc: $(srcdir)/ntpsnmpd-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpsnmpdmdoc -Tagmdoc-cmd.tpl ntpsnmpd-opts.def

$(srcdir)/ntpsnmpd.mdoc.in: $(srcdir)/ntpsnmpd.1ntpsnmpdmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpsnmpd.1ntpsnmpdmdoc > $(srcdir)/ntpsnmpd.mdoc.in+
	mv $(srcdir)/ntpsnmpd.mdoc.in+ $(srcdir)/ntpsnmpd.mdoc.in

###

ntpsnmpd.$(NTPSNMPD_MS): $(srcdir)/ntpsnmpd.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntpsnmpd.$(NTPSNMPD_MS)+:$(srcdir)/ntpsnmpd.$(MANTAGFMT).in
	mv ntpsnmpd.$(NTPSNMPD_MS)+ ntpsnmpd.$(NTPSNMPD_MS)

###

$(srcdir)/invoke-ntpsnmpd.menu: $(srcdir)/invoke-ntpsnmpd.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi
	
$(srcdir)/invoke-ntpsnmpd.texi: $(srcdir)/ntpsnmpd-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section ntpsnmpd-opts.def
	$(top_srcdir)/scripts/build/check--help $@

$(srcdir)/ntpsnmpd.html: $(srcdir)/ntpsnmpd.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -o ntpsnmpd.html ntpsnmpd.texi || true )

include $(top_srcdir)/bincheck.mf
include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/check-libopts.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
