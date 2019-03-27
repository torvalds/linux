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

bin_SCRIPTS=		$(NTPSWEEP_DB)
sbin_SCRIPTS=		$(NTPSWEEP_DS)
libexec_SCRIPTS=	$(NTPSWEEP_DL)
noinst_SCRIPTS=		$(NTPSWEEP_NI)
EXTRA_SCRIPTS=		ntpsweep

man1_MANS=
man8_MANS=
if INSTALL_NTPSWEEP
man_MANS=	ntpsweep.$(NTPSWEEP_MS)
else
noinst_MANS=	ntpsweep.$(NTPSWEEP_MS)
endif

EXTRA_DIST =			\
	ntpsweep.in		\
	ntpsweep.1ntpsweepman	\
	ntpsweep.1ntpsweepmdoc	\
	ntpsweep.man.in		\
	ntpsweep.mdoc.in	\
	ntpsweep.texi		\
	ntpsweep.html		\
	ntpsweep-opts.def	\
	ntpsweep-opts		\
	invoke-ntpsweep.texi	\
	invoke-ntpsweep.menu	\
	$(NULL)

html_DATA = ntpsweep.html

CLEANFILES = ntpsweep.1
DISTCLEANFILES = config.log $(man_MANS) $(noinst_MANS)

noinst_DATA =			\
	ntpsweep.1		\
	ntpsweep.1ntpsweepman	\
	ntpsweep.1ntpsweepmdoc	\
	ntpsweep.man.in		\
	ntpsweep.mdoc.in	\
	ntpsweep.texi		\
	ntpsweep.html		\
	ntpsweep-opts.def	\
	ntpsweep-opts		\
	invoke-ntpsweep.texi	\
	invoke-ntpsweep.menu	\
	$(NULL)

ntpsweep: $(srcdir)/ntpsweep-opts

$(srcdir)/ntpsweep-opts: ntpsweep-opts.def $(std_def_list)
	$(run_ag) ntpsweep-opts.def

### Nroff

$(srcdir)/ntpsweep.1ntpsweepman: ntpsweep-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpsweepman -Tagman-cmd.tpl ntpsweep-opts.def

$(srcdir)/ntpsweep.man.in: ntpsweep.1ntpsweepman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpsweep.1ntpsweepman > $(srcdir)/ntpsweep.man.in+
	mv $(srcdir)/ntpsweep.man.in+ $(srcdir)/ntpsweep.man.in

### Mdoc

$(srcdir)/ntpsweep.1ntpsweepmdoc: ntpsweep-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1ntpsweepmdoc -Tagmdoc-cmd.tpl ntpsweep-opts.def

$(srcdir)/ntpsweep.mdoc.in: ntpsweep.1ntpsweepmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/ntpsweep.1ntpsweepmdoc > $(srcdir)/ntpsweep.mdoc.in+
	mv $(srcdir)/ntpsweep.mdoc.in+ $(srcdir)/ntpsweep.mdoc.in

### Manpage

ntpsweep.1: ntpsweep.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=ntpsweep.1+:$(srcdir)/ntpsweep.$(MANTAGFMT).in
	mv ntpsweep.1+ ntpsweep.1

### Texinfo

$(srcdir)/invoke-ntpsweep.menu: invoke-ntpsweep.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-ntpsweep.texi: ntpsweep-opts ntpsweep-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section ntpsweep-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/ntpsweep.html: invoke-ntpsweep.menu invoke-ntpsweep.texi ntpsweep.texi $(top_srcdir)/sntp/include/version.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o ntpsweep.html ntpsweep.texi || true )
