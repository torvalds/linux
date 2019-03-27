## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = cygnus

# What version of the manual you want; "all" includes everything
CONFIG=all

# Options to extract the man page from as.texinfo
MANCONF = -Dman

TEXI2POD = perl $(top_srcdir)/../etc/texi2pod.pl $(AM_MAKEINFOFLAGS)

POD2MAN = pod2man --center="GNU Development Tools" \
	 --release="binutils-$(VERSION)" --section=1

# List of man pages generated from binutils.texi
man_MANS = \
	addr2line.1 \
	ar.1 \
	dlltool.1 \
	nlmconv.1 \
	nm.1 \
	objcopy.1 \
	objdump.1 \
	ranlib.1 \
	readelf.1 \
	size.1 \
	strings.1 \
	strip.1 \
	windres.1 \
	windmc.1 \
	$(DEMANGLER_NAME).1

info_TEXINFOS = binutils.texi
binutils_TEXI = $(srcdir)/binutils.texi

AM_MAKEINFOFLAGS = -I "$(srcdir)" -I "$(top_srcdir)/../libiberty" \
		   -I ../../bfd/doc
TEXI2DVI = texi2dvi -I "$(srcdir)" -I "$(top_srcdir)/../libiberty" \
		    -I ../../bfd/doc

# Man page generation from texinfo
addr2line.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Daddr2line < $(binutils_TEXI) > addr2line.pod
	-($(POD2MAN) addr2line.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f addr2line.pod

ar.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dar < $(binutils_TEXI) > ar.pod
	-($(POD2MAN) ar.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f ar.pod

dlltool.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Ddlltool < $(binutils_TEXI) > dlltool.pod
	-($(POD2MAN) dlltool.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f dlltool.pod

nlmconv.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dnlmconv < $(binutils_TEXI) > nlmconv.pod
	-($(POD2MAN) nlmconv.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f nlmconv.pod

nm.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dnm < $(binutils_TEXI) > nm.pod
	-($(POD2MAN) nm.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f nm.pod

objcopy.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dobjcopy < $(binutils_TEXI) > objcopy.pod
	-($(POD2MAN) objcopy.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f objcopy.pod

objdump.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dobjdump < $(binutils_TEXI) > objdump.pod
	-($(POD2MAN) objdump.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f objdump.pod

ranlib.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dranlib < $(binutils_TEXI) > ranlib.pod
	-($(POD2MAN) ranlib.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f ranlib.pod

readelf.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dreadelf < $(binutils_TEXI) > readelf.pod
	-($(POD2MAN) readelf.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f readelf.pod

size.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dsize < $(binutils_TEXI) > size.pod
	-($(POD2MAN) size.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f size.pod

strings.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dstrings < $(binutils_TEXI) > strings.pod
	-($(POD2MAN) strings.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f strings.pod

strip.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dstrip < $(binutils_TEXI) > strip.pod
	-($(POD2MAN) strip.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f strip.pod

windres.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dwindres < $(binutils_TEXI) > windres.pod
	-($(POD2MAN) windres.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f windres.pod

windmc.1:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dwindmc < $(binutils_TEXI) > windmc.pod
	-($(POD2MAN) windmc.pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f windmc.pod

cxxfilt.man:	$(binutils_TEXI) $(binutils_TEXINFOS)
	touch $@
	-$(TEXI2POD) $(MANCONF) -Dcxxfilt < $(binutils_TEXI) > $(DEMANGLER_NAME).pod
	-($(POD2MAN) $(DEMANGLER_NAME).pod | sed -e '/^.if n .na/d' > $@.T$$$$ && \
		mv -f $@.T$$$$ $@) || (rm -f $@.T$$$$ && exit 1)
	rm -f $(DEMANGLER_NAME).pod

MOSTLYCLEANFILES     = $(DEMANGLER_NAME).1

$(DEMANGLER_NAME).1: cxxfilt.man Makefile
	if test -f cxxfilt.man; then \
	  man=cxxfilt.man; \
	else \
	  man=$(srcdir)/cxxfilt.man; \
	fi; \
	sed -e 's/@PROGRAM@/$(DEMANGLER_NAME)/' \
	    -e 's/cxxfilt/$(DEMANGLER_NAME)/' < $$man \
		> $(DEMANGLER_NAME).1

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

MAINTAINERCLEANFILES = binutils.info

# Automake 1.9 will only build info files in the objdir if they are
# mentioned in DISTCLEANFILES.  It doesn't have to be unconditional,
# though, so we use a bogus condition.
if GENINSRC_NEVER
DISTCLEANFILES = binutils.info
endif

# Maintenance

# We need it for the taz target in ../../Makefile.in.
info-local: $(MANS)
