NULL=

bin_SCRIPTS =	$(NTP_WAIT_DB)
libexec_SCRIPTS =	$(NTP_WAIT_DL)
sbin_SCRIPTS =	$(NTP_WAIT_DS)

man1_MANS=
man8_MANS=
man_MANS=	ntp-wait.$(NTP_WAIT_MS)

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

EXTRA_SCRIPTS = ntp-wait

DISTCLEANFILES =	config.log $(man_MANS)
EXTRA_DIST =			\
	ntp-wait.in		\
	ntp-wait.1ntp-waitman	\
	ntp-wait.1ntp-waitmdoc	\
	ntp-wait.man.in		\
	ntp-wait.mdoc.in	\
	ntp-wait.texi		\
	ntp-wait.html		\
	ntp-wait-opts.def	\
	ntp-wait-opts		\
	invoke-ntp-wait.texi	\
	invoke-ntp-wait.menu	\
	$(NULL)

html_DATA = $(srcdir)/ntp-wait.html

noinst_DATA =			\
	ntp-wait.1ntp-waitman	\
	ntp-wait.1ntp-waitmdoc	\
	ntp-wait.man.in		\
	ntp-wait.mdoc.in	\
	ntp-wait.texi		\
	ntp-wait.html		\
	ntp-wait-opts.def	\
	ntp-wait-opts		\
	invoke-ntp-wait.texi	\
	invoke-ntp-wait.menu	\
	$(NULL)

ntp-wait: $(srcdir)/ntp-wait-opts

$(srcdir)/ntp-wait-opts: $(srcdir)/ntp-wait-opts.def $(std_def_list)
	$(run_ag) ntp-wait-opts.def

### Nroff

$(srcdir)/ntp-wait.1ntp-waitman: $(srcdir)/ntp-wait-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntp-waitman -Tagman-cmd.tpl ntp-wait-opts.def

$(srcdir)/ntp-wait.man.in: $(srcdir)/ntp-wait.1ntp-waitman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp-wait.1ntp-waitman > $(srcdir)/ntp-wait.man.in+
	mv $(srcdir)/ntp-wait.man.in+ $(srcdir)/ntp-wait.man.in

### Mdoc

$(srcdir)/ntp-wait.1ntp-waitmdoc: $(srcdir)/ntp-wait-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntp-waitmdoc -Tagmdoc-cmd.tpl ntp-wait-opts.def

$(srcdir)/ntp-wait.mdoc.in: $(srcdir)/ntp-wait.1ntp-waitmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntp-wait.1ntp-waitmdoc > $(srcdir)/ntp-wait.mdoc.in+
	mv $(srcdir)/ntp-wait.mdoc.in+ $(srcdir)/ntp-wait.mdoc.in

### Manpage

ntp-wait.$(NTP_WAIT_MS): $(srcdir)/ntp-wait.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntp-wait.$(NTP_WAIT_MS)+:$(srcdir)/ntp-wait.$(MANTAGFMT).in
	mv ntp-wait.$(NTP_WAIT_MS)+ ntp-wait.$(NTP_WAIT_MS)

### Texinfo

$(srcdir)/invoke-ntp-wait.menu: $(srcdir)/invoke-ntp-wait.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntp-wait.texi: ntp-wait-opts $(srcdir)/ntp-wait-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section ntp-wait-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/ntp-wait.html: $(srcdir)/invoke-ntp-wait.menu $(srcdir)/invoke-ntp-wait.texi $(srcdir)/ntp-wait.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o ntp-wait.html ntp-wait.texi || true )
