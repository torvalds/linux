## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = 1.8 cygnus

# What version of the manual you want; "all" includes everything
CONFIG=all

# Options to extract the man page from as.texinfo
MANCONF = -Dman

TEXI2POD = perl $(BASEDIR)/etc/texi2pod.pl $(AM_MAKEINFOFLAGS)

POD2MAN = pod2man --center="GNU Development Tools" \
	--release="binutils-$(VERSION)" --section=1

man_MANS = as.1

info_TEXINFOS = as.texinfo 
as_TEXINFOS = asconfig.texi $(CPU_DOCS)

AM_MAKEINFOFLAGS = -I "$(srcdir)" -I "$(top_srcdir)/../libiberty" \
		   -I ../../bfd/doc
TEXI2DVI = texi2dvi -I "$(srcdir)" -I "$(top_srcdir)/../libiberty" \
		   -I ../../bfd/doc

asconfig.texi: $(CONFIG).texi
	rm -f asconfig.texi
	cp $(srcdir)/$(CONFIG).texi ./asconfig.texi
	chmod u+w ./asconfig.texi

CPU_DOCS = \
	c-alpha.texi \
	c-arc.texi \
	c-arm.texi \
	c-avr.texi \
	c-bfin.texi \
	c-cr16.texi \
	c-d10v.texi \
	c-cris.texi \
	c-h8300.texi \
	c-hppa.texi \
	c-i370.texi \
	c-i386.texi \
	c-i860.texi \
	c-i960.texi \
	c-ip2k.texi \
	c-m32c.texi \
	c-m32r.texi \
	c-m68hc11.texi \
	c-m68k.texi \
	c-mips.texi \
	c-mmix.texi \
	c-mt.texi \
	c-msp430.texi \
	c-ns32k.texi \
	c-pdp11.texi \
	c-pj.texi \
	c-ppc.texi \
	c-sh.texi \
	c-sh64.texi \
	c-sparc.texi \
        c-tic54x.texi \
	c-vax.texi \
	c-v850.texi \
	c-xtensa.texi \
	c-z80.texi \
	c-z8k.texi

# We want install to imply install-info as per GNU standards, despite the
# cygnus option.
install-data-local: install-info

html__strip_dir = `echo $$p | sed -e 's|^.*/||'`;

install-html: install-html-am

install-html-am: $(HTMLS)
	@$(NORMAL_INSTALL)
	test -z "$(htmldir)" || $(mkdir_p) "$(DESTDIR)$(htmldir)"
	@list='$(HTMLS)'; for p in $$list; do \
	  if test -f "$$p" || test -d "$$p"; then d=""; else d="$(srcdir)/"; fi; \
	  f=$(html__strip_dir) \
	  if test -d "$$d$$p"; then \
	    echo " $(mkdir_p) '$(DESTDIR)$(htmldir)/$$f'"; \
	    $(mkdir_p) "$(DESTDIR)$(htmldir)/$$f" || exit 1; \
	    echo " $(INSTALL_DATA) '$$d$$p'/* '$(DESTDIR)$(htmldir)/$$f'"; \
	    $(INSTALL_DATA) "$$d$$p"/* "$(DESTDIR)$(htmldir)/$$f"; \
	  else \
	    echo " $(INSTALL_DATA) '$$d$$p' '$(DESTDIR)$(htmldir)/$$f'"; \
	    $(INSTALL_DATA) "$$d$$p" "$(DESTDIR)$(htmldir)/$$f"; \
	  fi; \
	done

# This one isn't ready for prime time yet.  Not even a little bit.

noinst_TEXINFOS = internals.texi

MAINTAINERCLEANFILES = asconfig.texi

BASEDIR = $(srcdir)/../..
BFDDIR = $(BASEDIR)/bfd

CONFIG_STATUS_DEPENDENCIES = $(BFDDIR)/configure.in

# Maintenance

# We need it for the taz target in ../../Makefile.in.
info-local: $(MANS)

# Build the man page from the texinfo file
# The sed command removes the no-adjust Nroff command so that
# the man output looks standard.
as.1: $(srcdir)/as.texinfo asconfig.texi $(CPU_DOCS)
	touch $@
	-$(TEXI2POD) $(MANCONF) < $(srcdir)/as.texinfo > as.pod
	-($(POD2MAN) as.pod | \
	        sed -e '/^.if n .na/d' > $@.T$$$$ && \
	        mv -f $@.T$$$$ $@) || \
	        (rm -f $@.T$$$$ && exit 1)
	rm -f as.pod

MAINTAINERCLEANFILES += as.info

# Automake 1.9 will only build info files in the objdir if they are
# mentioned in DISTCLEANFILES.  It doesn't have to be unconditional,
# though, so we use a bogus condition.
if GENINSRC_NEVER
DISTCLEANFILES = as.info
endif
