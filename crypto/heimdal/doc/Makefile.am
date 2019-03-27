# $Id$

include $(top_srcdir)/Makefile.am.common

AUTOMAKE_OPTIONS = no-texinfo.tex

MAKEINFOFLAGS = --css-include=$(srcdir)/heimdal.css

TEXI2DVI = true # ARGH, make distcheck can't be disabled to not build dvifiles

info_TEXINFOS = heimdal.texi hx509.texi

dxy_subst = sed -e 's,[@]srcdir[@],$(srcdir),g' \
	-e 's,[@]objdir[@],.,g' \
	-e 's,[@]PACKAGE_VERSION[@],$(PACKAGE_VERSION),g'

hcrypto.dxy: hcrypto.din Makefile
	$(dxy_subst) < $(srcdir)/hcrypto.din > hcrypto.dxy.tmp
	chmod +x hcrypto.dxy.tmp
	mv hcrypto.dxy.tmp hcrypto.dxy

hdb.dxy: hdb.din Makefile
	$(dxy_subst) < $(srcdir)/hdb.din > hdb.dxy.tmp
	chmod +x hdb.dxy.tmp
	mv hdb.dxy.tmp hdb.dxy

hx509.dxy: hx509.din Makefile
	$(dxy_subst) < $(srcdir)/hx509.din > hx509.dxy.tmp
	chmod +x hx509.dxy.tmp
	mv hx509.dxy.tmp hx509.dxy

gssapi.dxy: gssapi.din Makefile
	$(dxy_subst) < $(srcdir)/gssapi.din > gssapi.dxy.tmp
	chmod +x gssapi.dxy.tmp
	mv gssapi.dxy.tmp gssapi.dxy

krb5.dxy: krb5.din Makefile
	$(dxy_subst) < $(srcdir)/krb5.din > krb5.dxy.tmp
	chmod +x krb5.dxy.tmp
	mv krb5.dxy.tmp krb5.dxy

ntlm.dxy: ntlm.din Makefile
	$(dxy_subst) < $(srcdir)/ntlm.din > ntlm.dxy.tmp
	chmod +x ntlm.dxy.tmp
	mv ntlm.dxy.tmp ntlm.dxy

wind.dxy: wind.din Makefile
	$(dxy_subst) < $(srcdir)/wind.din > wind.dxy.tmp
	chmod +x wind.dxy.tmp
	mv wind.dxy.tmp wind.dxy

texi_subst = sed -e 's,[@]dbdir[@],$(localstatedir),g' \
	-e 's,[@]PACKAGE_VERSION[@],$(PACKAGE_VERSION),g'

vars.texi: vars.tin Makefile
	$(texi_subst) < $(srcdir)/vars.tin > vars.texi.tmp
	chmod +x vars.texi.tmp
	mv vars.texi.tmp vars.texi

PROJECTS = hcrypto hdb hx509 gssapi krb5 ntlm wind 

doxyout doxygen: hdb.dxy hx509.dxy hcrypto.dxy gssapi.dxy krb5.dxy ntlm.dxy wind.dxy
	@find $(srcdir)/doxyout -type d ! -perm -200 -exec chmod u+w {} ';' ; \
	rm -rf $(srcdir)/doxyout ; \
	mkdir $(srcdir)/doxyout ; \
	for a in $(PROJECTS) ; do \
		echo $$a ; \
		doxygen $$a.dxy; \
		(cd $(srcdir)/doxyout && find $$a/man -type f > $$a/manpages ) ; \
	done

install-data-hook: install-doxygen-manpage
uninstall-hook: uninstall-doxygen-manpage
dist-hook: doxygen

install-doxygen-manpage:
	for a in $(PROJECTS) ; do \
		f="$(srcdir)/doxyout/$$a/manpages" ; \
		test -f $$f || continue ; \
		echo "install $$a manual pages $$(wc -l < $$f)" ; \
		while read x ; do \
			section=`echo "$$x" | sed 's/.*\.\([0-9]\)/\1/'` ; \
			$(mkinstalldirs) "$(DESTDIR)$(mandir)/man$$section" ; \
			$(INSTALL_DATA) $(srcdir)/doxyout/$$x "$(DESTDIR)$(mandir)/man$$section" ; \
		done < $$f ; \
	done ; exit 0

uninstall-doxygen-manpage:
	@for a in $(PROJECTS) ; do \
		f="$(srcdir)/doxyout/$$a/manpages" ; \
		test -f $$f || continue ; \
		echo "removing $$a manual pages" ; \
		while read x ; do \
			section=`echo "$$x" | sed 's/.*\.\([0-9]\)/\1/'` ; \
			base=`basename $$x` ; \
			rm "$(DESTDIR)$(mandir)/man$$section/$$base" ; \
		done < $$f ; \
	done


heimdal_TEXINFOS = \
	ack.texi \
	apps.texi \
	copyright.texi \
	heimdal.texi \
	install.texi \
	intro.texi \
	kerberos4.texi \
	migration.texi \
	misc.texi \
	programming.texi \
	setup.texi \
	vars.texi \
	whatis.texi \
	win2k.texi

EXTRA_DIST = \
	NTMakefile \
	doxyout \
	footer.html \
	gssapi.din \
	hdb.din \
	hcrypto.din \
	header.html \
	heimdal.css \
	hx509.din \
	krb5.din \
	ntlm.din \
	init-creds \
	latin1.tex \
	layman.asc \
	doxytmpl.dxy \
	wind.din \
	vars.tin

CLEANFILES = \
	hcrypto.dxy* \
	hx509.dxy* \
	hdb.dxy* \
	gssapi.dxy* \
	krb5.dxy* \
	ntlm.dxy* \
	wind.dxy* \
	vars.texi*
