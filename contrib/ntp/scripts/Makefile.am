NULL=
SUBDIRS=		\
	build		\
	calc_tickadj	\
	ntp-wait	\
	ntpsweep	\
	ntptrace	\
	lib		\
	update-leap	\
	$(NULL)

man1_MANS=
man8_MANS=

DISTCLEANFILES=	config.log $(man_MANS)

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

noinst_SCRIPTS =		\
	ntpver			\
	plot_summary		\
	summary			\
	$(NULL)

install-data-local: install-html

EXTRA_DIST =				\
	deprecated			\
	monitoring			\
	invoke-plot_summary.texi	\
	invoke-plot_summary.menu	\
	plot_summary.1plot_summaryman	\
	plot_summary.1plot_summarymdoc	\
	plot_summary.mdoc.in		\
	plot_summary.man.in		\
	plot_summary.texi		\
	plot_summary.html		\
	plot_summary-opts		\
	plot_summary-opts.def		\
	rc				\
	invoke-summary.texi		\
	invoke-summary.menu		\
	summary.1summaryman		\
	summary.1summarymdoc		\
	summary.mdoc.in			\
	summary.man.in			\
	summary.texi			\
	summary.html			\
	summary-opts			\
	summary-opts.def		\
	stats				\
	$(NULL)

noinst_DATA =				\
	invoke-plot_summary.texi	\
	invoke-plot_summary.menu	\
	plot_summary.1plot_summaryman	\
	plot_summary.1plot_summarymdoc	\
	plot_summary.mdoc.in		\
	plot_summary.man.in		\
	plot_summary.texi		\
	plot_summary.html		\
	invoke-summary.texi		\
	invoke-summary.menu		\
	summary.1summaryman		\
	summary.1summarymdoc		\
	summary.mdoc.in			\
	summary.man.in			\
	summary.texi			\
	summary.html			\
	$(NULL)

#OPTTPL = ../sntp/ag-tpl/perlopt.tpl
OPTTPL = $(std_def_list)

##

plot_summary: $(srcdir)/plot_summary-opts

$(srcdir)/plot_summary-opts: plot_summary-opts.def $(OPTTPL)
	$(run_ag) plot_summary-opts.def 

### Nroff

$(srcdir)/plot_summary.1plot_summaryman: $(srcdir)/plot_summary-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1plot_summaryman -Tagman-cmd.tpl plot_summary-opts.def

$(srcdir)/plot_summary.man.in: $(srcdir)/plot_summary.1plot_summaryman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/plot_summary.1plot_summaryman > $(srcdir)/plot_summary.man.in+
	mv $(srcdir)/plot_summary.man.in+ $(srcdir)/plot_summary.man.in

### Mdoc

$(srcdir)/plot_summary.1plot_summarymdoc: $(srcdir)/plot_summary-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1plot_summarymdoc -Tagmdoc-cmd.tpl plot_summary-opts.def

$(srcdir)/plot_summary.mdoc.in: $(srcdir)/plot_summary.1plot_summarymdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/plot_summary.1plot_summarymdoc > $(srcdir)/plot_summary.mdoc.in+
	mv $(srcdir)/plot_summary.mdoc.in+ $(srcdir)/plot_summary.mdoc.in

### Manpage (local)

plot_summary.1: $(srcdir)/plot_summary.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=plot_summary.1+:$(srcdir)/plot_summary.$(MANTAGFMT).in
	mv plot_summary.1+ plot_summary.1

### Texinfo

$(srcdir)/invoke-plot_summary.menu: $(srcdir)/invoke-plot_summary.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-plot_summary.texi: plot_summary-opts $(srcdir)/plot_summary-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section plot_summary-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/plot_summary.html: $(srcdir)/invoke-plot_summary.menu $(srcdir)/invoke-plot_summary.texi $(srcdir)/plot_summary.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o plot_summary.html plot_summary.texi || true )


##

summary: $(srcdir)/summary-opts

$(srcdir)/summary-opts: summary-opts.def $(OPTTPL)
	$(run_ag) summary-opts.def

### Nroff

$(srcdir)/summary.1summaryman: $(srcdir)/summary-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1summaryman -Tagman-cmd.tpl summary-opts.def

$(srcdir)/summary.man.in: $(srcdir)/summary.1summaryman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/summary.1summaryman > $(srcdir)/summary.man.in+
	mv $(srcdir)/summary.man.in+ $(srcdir)/summary.man.in

### Mdoc

$(srcdir)/summary.1summarymdoc: $(srcdir)/summary-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1summarymdoc -Tagmdoc-cmd.tpl summary-opts.def

$(srcdir)/summary.mdoc.in: $(srcdir)/summary.1summarymdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/summary.1summarymdoc > $(srcdir)/summary.mdoc.in+
	mv $(srcdir)/summary.mdoc.in+ $(srcdir)/summary.mdoc.in

### Manpage (local)

summary.1: $(srcdir)/summary.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=summary.1+:$(srcdir)/summary.$(MANTAGFMT).in
	mv summary.1+ summary.1

### Texinfo

$(srcdir)/invoke-summary.menu: $(srcdir)/invoke-summary.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-summary.texi: summary-opts $(srcdir)/summary-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section summary-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/summary.html: $(srcdir)/invoke-summary.menu $(srcdir)/invoke-summary.texi $(srcdir)/summary.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o summary.html summary.texi || true )

