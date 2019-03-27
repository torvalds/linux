NULL=
run_ag=		cd $(srcdir) &&	env PATH="$(abs_builddir):$(PATH)" AUTOGEN_DNE_DATE=-D	\
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


bin_SCRIPTS=	$(CALC_TICKADJ_DB)
libexec_SCRIPTS=	$(CALC_TICKADJ_DL)
sbin_SCRIPTS=	$(CALC_TICKADJ_DS)
EXTRA_SCRIPTS=	calc_tickadj

man1_MANS=
man8_MANS=
manpage_HACK=
if WANT_CALC_TICKADJ_MS
man_MANS=	calc_tickadj.$(CALC_TICKADJ_MS)
else
manpage_HACK+=	calc_tickadj.
# Note that if we don't WANT_CALC_TICKADJ then CALC_TICKADJ_MS will
# be empty.  But we still need the .texi and other files to be built, as
# they are general prerequisites.
# The manpage_HACK is listed in noinst_DATA so that takes care of these.
endif

DISTCLEANFILES =	config.log $(man_MANS) calc_tickadj.

EXTRA_DIST =				\
	calc_tickadj.in 		\
	calc_tickadj.1calc_tickadjman	\
	calc_tickadj.1calc_tickadjmdoc	\
	calc_tickadj.man.in		\
	calc_tickadj.mdoc.in		\
	calc_tickadj.texi		\
	calc_tickadj.html		\
	calc_tickadj-opts.def		\
	calc_tickadj-opts		\
	invoke-calc_tickadj.texi	\
	invoke-calc_tickadj.menu	\
	$(NULL)

noinst_DATA = 				\
	calc_tickadj.1calc_tickadjman	\
	calc_tickadj.1calc_tickadjmdoc	\
	calc_tickadj.man.in		\
	calc_tickadj.mdoc.in		\
	calc_tickadj.texi		\
	calc_tickadj.html		\
	calc_tickadj-opts.def		\
	calc_tickadj-opts		\
	invoke-calc_tickadj.texi	\
	invoke-calc_tickadj.menu	\
	$(manpage_HACK)			\
	$(NULL)

BUILT_SOURCES = $(noinst_DATA)

calc_tickadj: $(srcdir)/calc_tickadj-opts

$(srcdir)/calc_tickadj-opts: $(srcdir)/calc_tickadj-opts.def
	$(run_ag) calc_tickadj-opts.def

### Nroff

$(srcdir)/calc_tickadj.1calc_tickadjman: $(srcdir)/calc_tickadj-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1calc_tickadjman -Tagman-cmd.tpl calc_tickadj-opts.def

$(srcdir)/calc_tickadj.man.in: $(srcdir)/calc_tickadj.1calc_tickadjman $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/calc_tickadj.1calc_tickadjman > $(srcdir)/calc_tickadj.man.in+
	mv $(srcdir)/calc_tickadj.man.in+ $(srcdir)/calc_tickadj.man.in

### Mdoc

$(srcdir)/calc_tickadj.1calc_tickadjmdoc: $(srcdir)/calc_tickadj-opts.def $(std_def_list)
	$(run_ag) -DMAN_SECTION=1calc_tickadjmdoc -Tagmdoc-cmd.tpl calc_tickadj-opts.def

$(srcdir)/calc_tickadj.mdoc.in: $(srcdir)/calc_tickadj.1calc_tickadjmdoc $(top_srcdir)/sntp/scripts/mansec2subst.sed
	sed -f $(top_srcdir)/sntp/scripts/mansec2subst.sed $(srcdir)/calc_tickadj.1calc_tickadjmdoc > $(srcdir)/calc_tickadj.mdoc.in+
	mv $(srcdir)/calc_tickadj.mdoc.in+ $(srcdir)/calc_tickadj.mdoc.in

### Manpage

calc_tickadj.$(CALC_TICKADJ_MS): $(srcdir)/calc_tickadj.$(MANTAGFMT).in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=calc_tickadj.$(CALC_TICKADJ_MS)+:$(srcdir)/calc_tickadj.$(MANTAGFMT).in
	mv calc_tickadj.$(CALC_TICKADJ_MS)+ calc_tickadj.$(CALC_TICKADJ_MS)

### Texinfo

$(srcdir)/invoke-calc_tickadj.menu: $(srcdir)/invoke-calc_tickadj.texi
	@: do-nothing action to avoid default SCCS get, .menu built with .texi

$(srcdir)/invoke-calc_tickadj.texi: $(srcdir)/calc_tickadj-opts $(srcdir)/calc_tickadj-opts.def $(std_def_list)
	$(run_ag) -Tagtexi-cmd.tpl -DLEVEL=section calc_tickadj-opts.def
	$(top_srcdir)/scripts/build/check--help $@

### HTML

$(srcdir)/calc_tickadj.html: $(srcdir)/invoke-calc_tickadj.menu $(srcdir)/invoke-calc_tickadj.texi $(srcdir)/calc_tickadj.texi
	cd $(srcdir) && ( makeinfo --force --html --no-split -I ../sntp -o calc_tickadj.html calc_tickadj.texi || true )
