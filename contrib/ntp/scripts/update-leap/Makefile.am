NULL=
run_ag=		cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" PERL5LIB="$(abs_top_srcdir)/scripts/lib" AUTOGEN_DNE_DATE=-D	\
		autogen -L ../../sntp/include -L ../../sntp/ag-tpl \
		--writable
std_def_list =							\
	$(top_srcdir)/sntp/include/debug-opt.def		\
	$(top_srcdir)/sntp/include/autogen-version.def 		\
	$(top_srcdir)/sntp/include/copyright.def 		\
	$(top_srcdir)/sntp/include/homerc.def 			\
	$(top_srcdir)/sntp/include/ntp.lic 			\
	$(top_srcdir)/sntp/include/version.def			\
	$(NULL)

bin_SCRIPTS=		$(UPDATE_LEAP_DB)
sbin_SCRIPTS=		$(UPDATE_LEAP_DS)
libexec_SCRIPTS=	$(UPDATE_LEAP_DL)
noinst_SCRIPTS=		$(UPDATE_LEAP_NI)
EXTRA_SCRIPTS=		update-leap

man1_MANS=
man8_MANS=
if INSTALL_UPDATE_LEAP
man_MANS=	update-leap.$(UPDATE_LEAP_MS)
else
noinst_MANS=	update-leap.$(UPDATE_LEAP_MS)
endif

EXTRA_DIST =				\
	invoke-update-leap.menu		\
	invoke-update-leap.texi		\
	update-leap-opts.def		\
	update-leap-opts		\
	update-leap.1update-leapman	\
	update-leap.1update-leapmdoc	\
	update-leap.in			\
	update-leap.man.in		\
	update-leap.mdoc.in		\
	update-leap.sh			\
	update-leap.texi		\
	update-leap.html		\
	$(NULL)

html_DATA = update-leap.html

#CLEANFILES = update-leap.1
DISTCLEANFILES = config.log $(man_MANS) $(noinst_MANS)

noinst_DATA =				\
	invoke-update-leap.menu		\
	invoke-update-leap.texi		\
	update-leap.man.in		\
	update-leap.mdoc.in		\
	update-leap-opts		\
	$(NULL)

$(srcdir)/update-leap: $(srcdir)/update-leap-opts
	@: do-nothing action to avoid default SCCS get

$(srcdir)/update-leap-opts: update-leap-opts.def $(std_def_list)
	$(run_ag) update-leap-opts.def

### Man

$(srcdir)/update-leap.1update-leapman: $(srcdir)/update-leap-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1update-leapman -Tagman-cmd.tpl update-leap-opts.def

$(srcdir)/update-leap.man.in: $(srcdir)/update-leap.1update-leapman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/update-leap.1update-leapman > $(srcdir)/update-leap.man.in+
	mv $(srcdir)/update-leap.man.in+ $(srcdir)/update-leap.man.in

### Mdoc

$(srcdir)/update-leap.1update-leapmdoc: $(srcdir)/update-leap-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1update-leapmdoc -Tagmdoc-cmd.tpl update-leap-opts.def

$(srcdir)/update-leap.mdoc.in: $(srcdir)/update-leap.1update-leapmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/update-leap.1update-leapmdoc > $(srcdir)/update-leap.mdoc.in+
	mv $(srcdir)/update-leap.mdoc.in+ $(srcdir)/update-leap.mdoc.in

### Manpage

update-leap.$(UPDATE_LEAP_MS): $(srcdir)/update-leap.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=update-leap.$(UPDATE_LEAP_MS)+:$(srcdir)/update-leap.$(MANTAGFMT).in
	mv update-leap.$(UPDATE_LEAP_MS)+ update-leap.$(UPDATE_LEAP_MS)

### Texinfo

$(srcdir)/invoke-update-leap.menu: $(srcdir)/invoke-update-leap.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-update-leap.texi: $(srcdir)/update-leap-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section update-leap-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/update-leap.html: $(srcdir)/update-leap.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o update-leap.html update-leap.texi || true )
