## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = 1.9 cygnus

DOCFILES = aoutx.texi  archive.texi archures.texi \
	bfdt.texi  cache.texi coffcode.texi \
	core.texi elf.texi elfcode.texi  format.texi \
	libbfd.texi bfdwin.texi bfdio.texi \
	opncls.texi  reloc.texi  section.texi  \
	syms.texi  targets.texi init.texi hash.texi linker.texi \
	mmo.texi \
	bfdver.texi

PROTOS = archive.p archures.p bfd.p \
	 core.p format.p \
	bfdio.p bfdwin.p \
	libbfd.p opncls.p reloc.p \
	section.p syms.p targets.p  \
	format.p  core.p init.p

IPROTOS = cache.ip libbfd.ip reloc.ip init.ip archures.ip coffcode.ip

# SRCDOC, SRCPROT, SRCIPROT only used to sidestep Sun Make bug in interaction 
# between VPATH and suffix rules.  If you use GNU Make, perhaps other Makes,
# you don't need these three:
SRCDOC = $(srcdir)/../aoutx.h  $(srcdir)/../archive.c \
	$(srcdir)/../archures.c $(srcdir)/../bfd.c \
	$(srcdir)/../bfdio.c $(srcdir)/../bfdwin.c \
	$(srcdir)/../cache.c $(srcdir)/../coffcode.h \
	$(srcdir)/../corefile.c $(srcdir)/../elf.c \
	$(srcdir)/../elfcode.h  $(srcdir)/../format.c \
	$(srcdir)/../libbfd.c $(srcdir)/../opncls.c \
	$(srcdir)/../reloc.c  $(srcdir)/../section.c \
	$(srcdir)/../syms.c  $(srcdir)/../targets.c \
	$(srcdir)/../hash.c $(srcdir)/../linker.c \
	$(srcdir)/../mmo.c

SRCPROT = $(srcdir)/../archive.c $(srcdir)/../archures.c \
	$(srcdir)/../bfd.c $(srcdir)/../coffcode.h $(srcdir)/../corefile.c \
	$(srcdir)/../format.c $(srcdir)/../libbfd.c \
	$(srcdir)/../bfdio.c $(srcdir)/../bfdwin.c \
	$(srcdir)/../opncls.c $(srcdir)/../reloc.c \
	$(srcdir)/../section.c $(srcdir)/../syms.c \
	$(srcdir)/../targets.c $(srcdir)/../init.c

SRCIPROT = $(srcdir)/../cache.c $(srcdir)/../libbfd.c \
	$(srcdir)/../bfdio.c $(srcdir)/../bfdwin.c \
	$(srcdir)/../reloc.c $(srcdir)/../cpu-h8300.c \
	$(srcdir)/../cpu-i960.c $(srcdir)/../archures.c \
	$(srcdir)/../init.c

TEXIDIR = $(srcdir)/../../texinfo/fsf

info_TEXINFOS = bfd.texinfo
bfd_TEXINFOS = $(DOCFILES) bfdsumm.texi

MKDOC = chew$(EXEEXT_FOR_BUILD)

INCLUDES = -I.. -I$(srcdir)/.. -I$(srcdir)/../../include \
	-I$(srcdir)/../../intl -I../../intl

$(MKDOC): $(srcdir)/chew.c
	$(CC_FOR_BUILD) -o chew.$$$$ $(srcdir)/chew.c \
	  $(CFLAGS_FOR_BUILD) $(LDFLAGS_FOR_BUILD) $(H_CFLAGS) \
	  $(INCLUDES); \
	$(SHELL) $(srcdir)/../../move-if-change chew.$$$$ $(MKDOC)

chew.o: chew.c
	$(CC_FOR_BUILD) -c $(INCLUDES) $(H_CFLAGS) $(CFLAGS_FOR_BUILD) $(srcdir)/chew.c

protos: libbfd.h libcoff.h bfd.h

# We can't replace these rules with an implicit rule, because
# makes without VPATH support couldn't find the .h files in `..'.

# We do not depend on chew directly so that we can distribute the info
# files, and permit people to rebuild them, without requiring the makeinfo
# program.  If somebody tries to rebuild info, but none of the .texi files
# have changed, then nothing will be rebuilt.

aoutx.texi: chew.c $(srcdir)/../aoutx.h $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../aoutx.h >aoutx.tmp
	$(SHELL) $(srcdir)/../../move-if-change aoutx.tmp aoutx.texi

archive.texi: chew.c $(srcdir)/../archive.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../archive.c >archive.tmp
	$(SHELL) $(srcdir)/../../move-if-change archive.tmp archive.texi

archures.texi: chew.c $(srcdir)/../archures.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../archures.c >archures.tmp
	$(SHELL) $(srcdir)/../../move-if-change archures.tmp archures.texi

# We use bfdt.texi, rather than bfd.texi, to avoid conflicting with
# bfd.texinfo on an 8.3 filesystem.
bfdt.texi: chew.c $(srcdir)/../bfd.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../bfd.c >bfd.tmp
	$(SHELL) $(srcdir)/../../move-if-change bfd.tmp bfdt.texi

cache.texi: chew.c $(srcdir)/../cache.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../cache.c >cache.tmp
	$(SHELL) $(srcdir)/../../move-if-change cache.tmp cache.texi

coffcode.texi: chew.c $(srcdir)/../coffcode.h $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../coffcode.h >coffcode.tmp
	$(SHELL) $(srcdir)/../../move-if-change coffcode.tmp coffcode.texi

core.texi: chew.c $(srcdir)/../corefile.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../corefile.c >core.tmp
	$(SHELL) $(srcdir)/../../move-if-change core.tmp core.texi

elf.texi: chew.c $(srcdir)/../elf.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../elf.c >elf.tmp
	$(SHELL) $(srcdir)/../../move-if-change elf.tmp elf.texi

elfcode.texi: chew.c $(srcdir)/../elfcode.h $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../elfcode.h >elfcode.tmp
	$(SHELL) $(srcdir)/../../move-if-change elfcode.tmp elfcode.texi

mmo.texi: chew.c $(srcdir)/../mmo.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../mmo.c >mmo.tmp
	$(SHELL) $(srcdir)/../../move-if-change mmo.tmp mmo.texi

format.texi: chew.c $(srcdir)/../format.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../format.c >format.tmp
	$(SHELL) $(srcdir)/../../move-if-change format.tmp format.texi

libbfd.texi: chew.c $(srcdir)/../libbfd.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../libbfd.c >libbfd.tmp
	$(SHELL) $(srcdir)/../../move-if-change libbfd.tmp libbfd.texi

bfdio.texi: chew.c $(srcdir)/../bfdio.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../bfdio.c >bfdio.tmp
	$(SHELL) $(srcdir)/../../move-if-change bfdio.tmp bfdio.texi

bfdwin.texi: chew.c $(srcdir)/../bfdwin.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str < $(srcdir)/../bfdwin.c >bfdwin.tmp
	$(SHELL) $(srcdir)/../../move-if-change bfdwin.tmp bfdwin.texi

opncls.texi: chew.c $(srcdir)/../opncls.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str  <$(srcdir)/../opncls.c >opncls.tmp
	$(SHELL) $(srcdir)/../../move-if-change opncls.tmp opncls.texi

reloc.texi: chew.c $(srcdir)/../reloc.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../reloc.c >reloc.tmp
	$(SHELL) $(srcdir)/../../move-if-change reloc.tmp reloc.texi

section.texi: chew.c $(srcdir)/../section.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../section.c >section.tmp
	$(SHELL) $(srcdir)/../../move-if-change section.tmp section.texi

syms.texi: chew.c $(srcdir)/../syms.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../syms.c >syms.tmp
	$(SHELL) $(srcdir)/../../move-if-change syms.tmp syms.texi

targets.texi: chew.c $(srcdir)/../targets.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../targets.c >targets.tmp
	$(SHELL) $(srcdir)/../../move-if-change targets.tmp targets.texi

init.texi: chew.c $(srcdir)/../init.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../init.c >init.tmp
	$(SHELL) $(srcdir)/../../move-if-change init.tmp init.texi

hash.texi: chew.c $(srcdir)/../hash.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../hash.c >hash.tmp
	$(SHELL) $(srcdir)/../../move-if-change hash.tmp hash.texi

linker.texi: chew.c $(srcdir)/../linker.c $(srcdir)/doc.str
	$(MAKE) $(MKDOC)
	./$(MKDOC) -f $(srcdir)/doc.str <$(srcdir)/../linker.c >linker.tmp
	$(SHELL) $(srcdir)/../../move-if-change linker.tmp linker.texi

LIBBFD_H_DEP = \
	$(srcdir)/../libbfd-in.h	\
	$(srcdir)/../init.c		\
	$(srcdir)/../libbfd.c		\
	$(srcdir)/../bfdio.c		\
	$(srcdir)/../bfdwin.c		\
	$(srcdir)/../cache.c		\
	$(srcdir)/../reloc.c		\
	$(srcdir)/../archures.c		\
	$(srcdir)/../elf.c		\
	$(srcdir)/header.sed		\
	$(srcdir)/proto.str		\
	$(MKDOC)

libbfd.h: $(LIBBFD_H_DEP)
	echo "$(LIBBFD_H_DEP)" | sed -f $(srcdir)/header.sed > $@
	for file in $(LIBBFD_H_DEP); do \
	  case $$file in \
	    *-in.h) cat $$file >> $@ ;; \
	    */header.sed) break ;; \
	    *)	echo $$file | sed -e 's,.*/,,' -e 's,^,/* Extracted from ,' \
				-e 's,$$,.  */,' >> $@ ; \
		./$(MKDOC) -i -f $(srcdir)/proto.str < $$file >> $@ ;; \
	  esac; \
	done

LIBCOFF_H_DEP = \
	$(srcdir)/../libcoff-in.h	\
	$(srcdir)/../coffcode.h		\
	$(srcdir)/header.sed		\
	$(srcdir)/proto.str		\
	$(MKDOC)

libcoff.h: $(LIBCOFF_H_DEP)
	echo "$(LIBCOFF_H_DEP)" | sed -f $(srcdir)/header.sed > $@
	for file in $(LIBCOFF_H_DEP); do \
	  case $$file in \
	    *-in.h) cat $$file >> $@ ;; \
	    */header.sed) break ;; \
	    *)	echo $$file | sed -e 's,.*/,,' -e 's,^,/* Extracted from ,' \
				-e 's,$$,.  */,' >> $@ ; \
		./$(MKDOC) -i -f $(srcdir)/proto.str < $$file >> $@ ;; \
	  esac; \
	done

BFD_H_DEP = \
	$(srcdir)/../bfd-in.h 		\
	$(srcdir)/../init.c 		\
	$(srcdir)/../opncls.c 		\
	$(srcdir)/../libbfd.c 		\
	$(srcdir)/../bfdio.c		\
	$(srcdir)/../bfdwin.c		\
	$(srcdir)/../section.c 		\
	$(srcdir)/../archures.c		\
	$(srcdir)/../reloc.c		\
	$(srcdir)/../syms.c		\
	$(srcdir)/../bfd.c		\
	$(srcdir)/../archive.c		\
	$(srcdir)/../corefile.c		\
	$(srcdir)/../targets.c		\
	$(srcdir)/../format.c		\
	$(srcdir)/../linker.c		\
	$(srcdir)/../simple.c		\
	$(srcdir)/header.sed		\
	$(srcdir)/proto.str		\
	$(srcdir)/../version.h		\
	$(MKDOC)

bfd.h: $(BFD_H_DEP)
	echo "$(BFD_H_DEP)" | sed -f $(srcdir)/header.sed > $@
	for file in $(BFD_H_DEP); do \
	  case $$file in \
	    *-in.h) cat $$file >> $@ ;; \
	    */header.sed) break ;; \
	    *)	echo $$file | sed -e 's,.*/,,' -e 's,^,/* Extracted from ,' \
				-e 's,$$,.  */,' >> $@ ; \
		./$(MKDOC) -f $(srcdir)/proto.str < $$file >> $@ ;; \
	  esac; \
	done
	echo "#ifdef __cplusplus" >> $@
	echo "}" >> $@
	echo "#endif" >> $@
	echo "#endif" >> $@

bfdver.texi: $(srcdir)/Makefile.in
	@echo "creating $@"; \
	echo "@set VERSION $(VERSION)" > bfdver.texi; \
	if [ -n "$(PKGVERSION)" ]; then \
	  echo "@set VERSION_PACKAGE $(PKGVERSION)" >> bfdver.texi; \
	fi; \
	echo "@set UPDATED `date '+%B %Y'`" >> bfdver.texi; \
	if [ -n "$(REPORT_BUGS_TEXI)" ]; then \
	  echo "@set BUGURL $(REPORT_BUGS_TEXI)" >> bfdver.texi; \
	fi

noinst_TEXINFOS = bfdint.texi

MOSTLYCLEANFILES = $(MKDOC) *.o

CLEANFILES = *.p *.ip

DISTCLEANFILES = bfd.?? bfd.??? bfd.h libbfd.h libcoff.h texput.log

MAINTAINERCLEANFILES = $(DOCFILES)

# We want install to imply install-info as per GNU standards, despite the
# cygnus option.
install: install-info

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

MAINTAINERCLEANFILES += bfd.info

# Automake 1.9 will only build info files in the objdir if they are
# mentioned in DISTCLEANFILES.  It doesn't have to be unconditional,
# though, so we use a bogus condition.
if GENINSRC_NEVER
DISTCLEANFILES += bfd.info
endif
