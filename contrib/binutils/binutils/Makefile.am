## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = cygnus dejagnu
ACLOCAL_AMFLAGS = -I .. -I ../config -I ../bfd

SUBDIRS = doc po

tooldir = $(exec_prefix)/$(target_alias)

## These aren't set by automake, because they appear in
## bfd/acinclude.m4, which is included by binutils/acinclude.m4, and
## thus is not seen by automake.
CC_FOR_BUILD = @CC_FOR_BUILD@
EXEEXT_FOR_BUILD = @EXEEXT_FOR_BUILD@

YACC = `if [ -f ../bison/bison ]; then echo ../bison/bison -y -L$(srcdir)/../bison/; else echo @YACC@; fi`
YFLAGS = -d
LEX = `if [ -f ../flex/flex ]; then echo ../flex/flex; else echo @LEX@; fi`

WARN_CFLAGS = @WARN_CFLAGS@
NO_WERROR = @NO_WERROR@
AM_CFLAGS = $(WARN_CFLAGS)
LIBICONV = @LIBICONV@

# these two are almost the same program
AR_PROG=ar
RANLIB_PROG=ranlib

# objcopy and strip should be the same program
OBJCOPY_PROG=objcopy
STRIP_PROG=strip-new

STRINGS_PROG=strings

READELF_PROG=readelf

# These should all be the same program too.
SIZE_PROG=size
NM_PROG=nm-new
OBJDUMP_PROG=objdump

# This is the demangler, as a standalone program.
# Note: This one is used as the installed name too, unlike the above.
DEMANGLER_PROG=cxxfilt

ADDR2LINE_PROG=addr2line

NLMCONV_PROG=nlmconv
DLLTOOL_PROG=dlltool
WINDRES_PROG=windres
WINDMC_PROG=windmc
DLLWRAP_PROG=dllwrap

SRCONV_PROG=srconv$(EXEEXT) sysdump$(EXEEXT) coffdump$(EXEEXT)

bin_PROGRAMS = $(SIZE_PROG) $(OBJDUMP_PROG) $(AR_PROG) $(STRINGS_PROG) $(RANLIB_PROG) $(OBJCOPY_PROG) @BUILD_NLMCONV@ @BUILD_SRCONV@ @BUILD_DLLTOOL@ @BUILD_WINDRES@ @BUILD_WINDMC@ $(ADDR2LINE_PROG) $(READELF_PROG) @BUILD_DLLWRAP@ @BUILD_INSTALL_MISC@

## We need a special rule to install the programs which are built with
## -new, and to rename cxxfilt to c++filt.
RENAMED_PROGS = $(NM_PROG) $(STRIP_PROG) $(DEMANGLER_PROG)
noinst_PROGRAMS = $(RENAMED_PROGS) @BUILD_MISC@

EXTRA_PROGRAMS = $(NLMCONV_PROG) srconv sysdump coffdump $(DLLTOOL_PROG) $(WINDRES_PROG) $(WINDMC_PROG) $(DLLWRAP_PROG)

# Stuff that goes in tooldir/ if appropriate.
TOOL_PROGS = nm-new strip-new ar ranlib dlltool objdump objcopy

BASEDIR = $(srcdir)/..
BFDDIR = $(BASEDIR)/bfd
INCDIR	= $(BASEDIR)/include

MKDEP = gcc -MM

INCLUDES = -D_GNU_SOURCE \
	 -I. -I$(srcdir) -I../bfd -I$(BFDDIR) -I$(INCDIR) \
	 @HDEFINES@ \
	 @INCINTL@ \
	 -DLOCALEDIR="\"$(datadir)/locale\"" \
	 -Dbin_dummy_emulation=$(EMULATION_VECTOR)

HFILES = \
	arsup.h binemul.h bucomm.h budbg.h \
	coffgrok.h debug.h dlltool.h nlmconv.h \
	windres.h winduni.h windint.h \
	windmc.h

GENERATED_HFILES = arparse.h sysroff.h sysinfo.h defparse.h rcparse.h mcparse.h

CFILES = \
	addr2line.c ar.c arsup.c bin2c.c binemul.c bucomm.c \
	coffdump.c coffgrok.c cxxfilt.c \
	dwarf.c debug.c dlltool.c dllwrap.c \
	emul_aix.c emul_vanilla.c filemode.c \
	ieee.c is-ranlib.c is-strip.c maybe-ranlib.c maybe-strip.c \
	nlmconv.c nm.c not-ranlib.c not-strip.c \
	objcopy.c objdump.c prdbg.c \
	rclex.c rdcoff.c rddbg.c readelf.c rename.c \
	resbin.c rescoff.c resrc.c resres.c \
	size.c srconv.c stabs.c strings.c sysdump.c version.c \
	windres.c winduni.c wrstabs.c \
	windmc.c mclex.c

GENERATED_CFILES = \
	arparse.c arlex.c sysroff.c sysinfo.c syslex.c \
	defparse.c deflex.c nlmheader.c rcparse.c mcparse.c

DEBUG_SRCS = rddbg.c debug.c stabs.c ieee.c rdcoff.c
WRITE_DEBUG_SRCS = $(DEBUG_SRCS) wrstabs.c

# Code shared by all the binutils.
BULIBS = bucomm.c version.c filemode.c

BFDLIB = ../bfd/libbfd.la

OPCODES = ../opcodes/libopcodes.la

LIBIBERTY = ../libiberty/libiberty.a

.PHONY: install-html install-html-am install-html-recursive

install-html:	install-html-recursive

install-html-recursive:
	@failcom='exit 1'; \
	for f in x $$MAKEFLAGS; do \
	  case $$f in \
	    *=* | --[!k]*);; \
	    *k*) failcom='fail=yes';; \
	  esac; \
	done; \
	dot_seen=no; \
	target=`echo $@ | sed s/-recursive//`; \
	list='$(SUBDIRS)'; for subdir in $$list; do \
	  echo "Making $$target in $$subdir"; \
	  if test "$$subdir" = "."; then \
	    dot_seen=yes; \
	    local_target="$$target-am"; \
	  else \
	    local_target="$$target"; \
	  fi; \
	  (cd $$subdir && $(MAKE) $(AM_MAKEFLAGS) $$local_target) \
	  || eval $$failcom; \
	done; \
	if test "$$dot_seen" = "no"; then \
	  $(MAKE) $(AM_MAKEFLAGS) "$$target-am" || exit 1; \
	fi; test -z "$$fail"

POTFILES = $(CFILES) $(DEBUG_SRCS) $(HFILES)
po/POTFILES.in: @MAINT@ Makefile
	for f in $(POTFILES); do echo $$f; done | LC_COLLATE= sort > tmp \
	  && mv tmp $(srcdir)/po/POTFILES.in

EXPECT = expect
RUNTEST = runtest

CC_FOR_TARGET = ` \
  if [ -f $$r/../gcc/xgcc ] ; then \
    if [ -f $$r/../newlib/Makefile ] ; then \
      echo $$r/../gcc/xgcc -B$$r/../gcc/ -idirafter $$r/../newlib/targ-include -idirafter $${srcroot}/../newlib/libc/include -nostdinc; \
    else \
      echo $$r/../gcc/xgcc -B$$r/../gcc/; \
    fi; \
  else \
    if [ "@host@" = "@target@" ] ; then \
      echo $(CC); \
    else \
      echo gcc | sed '$(transform)'; \
    fi; \
  fi`

check-DEJAGNU: site.exp
	srcdir=`cd $(srcdir) && pwd`; export srcdir; \
	r=`pwd`; export r; \
	EXPECT=$(EXPECT); export EXPECT; \
	runtest=$(RUNTEST); \
	if $(SHELL) -c "$$runtest --version" > /dev/null 2>&1; then \
	  CC_FOR_TARGET="$(CC_FOR_TARGET)" CFLAGS_FOR_TARGET="$(CFLAGS)" \
		$$runtest --tool $(DEJATOOL) --srcdir $${srcdir}/testsuite \
			$(RUNTESTFLAGS); \
	else echo "WARNING: could not find \`runtest'" 1>&2; :;\
	fi

installcheck:
	/bin/sh $(srcdir)/sanity.sh $(bindir)

# There's no global DEPENDENCIES.  So, we must explicitly list everything
# which depends on libintl, since we don't know whether LIBINTL_DEP will be
# non-empty until configure time.  Ugh!
size_DEPENDENCIES =      $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
objdump_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB) $(OPCODES)
nm_new_DEPENDENCIES =    $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
ar_DEPENDENCIES =        $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
strings_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
strip_new_DEPENDENCIES = $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
ranlib_DEPENDENCIES =    $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
cxxfilt_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
objcopy_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
nlmconv_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
srconv_DEPENDENCIES =    $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
sysdump_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
coffdump_DEPENDENCIES =  $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
dlltool_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
windres_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
windmc_DEPENDENCIES =    $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
addr2line_DEPENDENCIES = $(LIBINTL_DEP) $(LIBIBERTY) $(BFDLIB)
readelf_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY)
dllwrap_DEPENDENCIES =   $(LIBINTL_DEP) $(LIBIBERTY)

LDADD = $(BFDLIB) $(LIBIBERTY) $(LIBINTL)

size_SOURCES = size.c $(BULIBS)

objcopy_SOURCES = objcopy.c not-strip.c rename.c $(WRITE_DEBUG_SRCS) $(BULIBS)

strings_SOURCES = strings.c $(BULIBS)

readelf_SOURCES = readelf.c version.c unwind-ia64.c dwarf.c
readelf_LDADD   = $(LIBINTL) $(LIBIBERTY)

strip_new_SOURCES = objcopy.c is-strip.c rename.c $(WRITE_DEBUG_SRCS) $(BULIBS)

nm_new_SOURCES = nm.c $(BULIBS)

objdump_SOURCES = objdump.c dwarf.c prdbg.c $(DEBUG_SRCS) $(BULIBS)
objdump_LDADD = $(OPCODES) $(BFDLIB) $(LIBIBERTY) $(LIBINTL)

objdump.o:objdump.c
	$(COMPILE) -c $(OBJDUMP_DEFS) $(srcdir)/objdump.c

cxxfilt_SOURCES = cxxfilt.c $(BULIBS)

ar_SOURCES = arparse.y arlex.l ar.c not-ranlib.c arsup.c rename.c binemul.c \
	emul_$(EMULATION).c $(BULIBS)
ar_LDADD = $(BFDLIB) $(LIBIBERTY) @LEXLIB@ $(LIBINTL)

ranlib_SOURCES = ar.c is-ranlib.c arparse.y arlex.l arsup.c rename.c \
	binemul.c emul_$(EMULATION).c $(BULIBS)
ranlib_LDADD = $(BFDLIB) $(LIBIBERTY) @LEXLIB@ $(LIBINTL)

addr2line_SOURCES = addr2line.c $(BULIBS)

# The following is commented out for the conversion to automake.
# This rule creates a single binary that switches between ar and ranlib
# by looking at argv[0].  Use this kludge to save some disk space.
# However, you have to install things by hand.
# (That is after 'make install', replace the installed ranlib by a link to ar.)
# Alternatively, you can install ranlib.sh as ranlib.
# ar_with_ranlib: $(ADDL_DEPS) ar.o maybe-ranlib.o
#	$(HLDENV) $(CC) $(HLDFLAGS) $(CFLAGS) $(LDFLAGS) -o $(AR_PROG) ar.o maybe-ranlib.o $(ADDL_LIBS) $(EXTRALIBS)
#	-rm -f $(RANLIB_PROG)
#	-ln $(AR_PROG) $(RANLIB_PROG)
#
# objcopy and strip in one binary that uses argv[0] to decide its action.
#
#objcopy_with_strip: $(ADDL_DEPS) objcopy.o maybe-strip.o
#	$(HLDENV) $(CC) $(HLDFLAGS) $(CFLAGS) $(LDFLAGS) -o $(OBJCOPY_PROG) objcopy.o maybe-strip.o $(ADDL_LIBS) $(EXTRALIBS)
#	-rm -f $(STRIP_PROG)
#	-ln $(OBJCOPY_PROG) $(STRIP_PROG)

sysroff.c: sysinfo$(EXEEXT_FOR_BUILD) sysroff.info
	./sysinfo$(EXEEXT_FOR_BUILD) -c <$(srcdir)/sysroff.info >sysroff.c
	./sysinfo$(EXEEXT_FOR_BUILD) -i <$(srcdir)/sysroff.info >>sysroff.c
	./sysinfo$(EXEEXT_FOR_BUILD) -g <$(srcdir)/sysroff.info >>sysroff.c

sysroff.h: sysinfo$(EXEEXT_FOR_BUILD) sysroff.info
	./sysinfo$(EXEEXT_FOR_BUILD) -d <$(srcdir)/sysroff.info >sysroff.h

sysinfo$(EXEEXT_FOR_BUILD): sysinfo.o syslex.o
	$(CC_FOR_BUILD) $(CFLAGS) $(LDFLAGS) -o $@ sysinfo.o syslex.o

syslex.o:
	if [ -r syslex.c ]; then \
	  $(CC_FOR_BUILD) -c -I. $(AM_CFLAGS) $(CFLAGS) syslex.c -Wno-error ; \
	else \
	  $(CC_FOR_BUILD) -c -I. -I$(srcdir) $(AM_CFLAGS) $(CFLAGS) $(srcdir)/syslex.c -Wno-error ;\
	fi

sysinfo.o:
	if [ -r sysinfo.c ]; then \
	  $(CC_FOR_BUILD) -c -I. $(AM_CFLAGS) $(CFLAGS) sysinfo.c -Wno-error ; \
	else \
	  $(CC_FOR_BUILD) -c -I. $(AM_CFLAGS) $(CFLAGS) $(srcdir)/sysinfo.c -Wno-error ; \
	fi

bin2c$(EXEEXT_FOR_BUILD):
	$(CC_FOR_BUILD) -o $@ $(INCLUDES) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) $(srcdir)/bin2c.c $(srcdir)/version.c

embedspu: embedspu.sh
	sed "s@^program_transform_name=@program_transform_name=$(program_transform_name)@" < $< > $@
	chmod a+x $@

# We need these for parallel make.
arparse.h: arparse.c
defparse.h: defparse.c
nlmheader.h: nlmheader.c
rcparse.h: rcparse.c
mcparse.h: mcparse.c
sysinfo.h: sysinfo.c

# Disable -Werror, if it has been enabled, since old versions of bison/
# yacc will produce working code which contain compile time warnings.
arparse.o:
	$(COMPILE) -c $< $(NO_WERROR)
arlex.o:
	$(COMPILE) -c $< $(NO_WERROR)
sysroff.o:
	$(COMPILE) -c $< $(NO_WERROR)
defparse.o:
	$(COMPILE) -c $< $(NO_WERROR)
deflex.o:
	$(COMPILE) -c $< $(NO_WERROR)
nlmheader.o:
	$(COMPILE) -c $< $(NO_WERROR)
rcparse.o:
	$(COMPILE) -c $< $(NO_WERROR)
mcparse.o:
	$(COMPILE) -c $< $(NO_WERROR)
rclex.o:
	$(COMPILE) -c $< $(NO_WERROR)
mclex.o:
	$(COMPILE) -c $< $(NO_WERROR)

srconv_SOURCES = srconv.c coffgrok.c $(BULIBS)

dlltool_SOURCES = dlltool.c defparse.y deflex.l $(BULIBS)
dlltool_LDADD = $(BFDLIB) $(LIBIBERTY) @LEXLIB@ $(LIBINTL)

dlltool.o:
	$(COMPILE) -c $(DLLTOOL_DEFS) $(srcdir)/dlltool.c

rescoff.o:
	$(COMPILE) -c $(DLLTOOL_DEFS) $(srcdir)/rescoff.c

coffdump_SOURCES = coffdump.c coffgrok.c $(BULIBS)

sysdump_SOURCES = sysdump.c $(BULIBS)

# coff/sym.h and coff/ecoff.h won't be found by the automatic dependency
# scripts, since they are only included conditionally.
nlmconv.o: nlmconv.c $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h
	ldname=`echo ld | sed '$(transform)'`; \
	$(COMPILE) -c -DLD_NAME="\"$${ldname}\"" @NLMCONV_DEFS@ $(srcdir)/nlmconv.c

nlmconv_SOURCES = nlmconv.c nlmheader.y $(BULIBS)

windres_SOURCES = windres.c resrc.c rescoff.c resbin.c rcparse.y rclex.c \
	winduni.c resres.c $(BULIBS)
windres_LDADD = $(BFDLIB) $(LIBIBERTY) @LEXLIB@ $(LIBINTL) $(LIBICONV)

windmc_SOURCES = windmc.c mcparse.y mclex.c \
	winduni.c $(BULIBS)
windmc_LDADD = $(BFDLIB) $(LIBIBERTY) @LEXLIB@ $(LIBINTL) $(LIBICONV)

dllwrap_SOURCES = dllwrap.c version.c
dllwrap_LDADD = $(LIBIBERTY) $(LIBINTL)


EXTRA_DIST = arparse.c arparse.h arlex.c nlmheader.c sysinfo.c sysinfo.h \
	syslex.c deflex.c defparse.h defparse.c rcparse.h rcparse.c \
	mcparse.h mcparse.c

diststuff: $(EXTRA_DIST) info
all: info

DISTCLEANFILES = sysroff.c sysroff.h site.exp site.bak

# Targets to rebuild dependencies in this Makefile.
# Have to get rid of DEP1 here so that "$?" later includes all of $(CFILES).
DEP: dep.sed $(CFILES) $(HFILES) $(GENERATED_CFILES) $(GENERATED_HFILES) config.h
	rm -f DEP1
	$(MAKE) MKDEP="$(MKDEP)" DEP1
	sed -f dep.sed < DEP1 > DEPA
	echo '# IF YOU PUT ANYTHING HERE IT WILL GO AWAY' >> DEPA
	if grep ' /' DEPA > /dev/null 2> /dev/null; then \
	  echo 'make DEP failed!'; exit 1; \
	else \
	  mv -f DEPA $@; \
	fi

DEP1: $(CFILES) $(GENERATED_CFILES)
	echo '# DO NOT DELETE THIS LINE -- mkdep uses it.' > DEP2
	echo '# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.' >> DEP2
	$(MKDEP) $(INCLUDES) $(CFLAGS) $? >> DEP2
	mv -f DEP2 $@

dep.sed: dep-in.sed config.status
	objdir=`pwd`; \
	sed <$(srcdir)/dep-in.sed >dep.sed	\
		-e 's!@INCDIR@!$(INCDIR)!'	\
		-e 's!@BFDDIR@!$(BFDDIR)!'	\
		-e 's!@SRCDIR@!$(srcdir)!'	\
		-e "s!@OBJDIR@!$${objdir}!"	\
		-e 's!@TOPDIR@!'`echo $(srcdir) | sed -e s,/binutils$$,,`'!'

dep: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < Makefile > tmp-Makefile
	cat DEP >> tmp-Makefile
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile Makefile

dep-in: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < $(srcdir)/Makefile.in > tmp-Makefile.in
	cat DEP >> tmp-Makefile.in
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile.in $(srcdir)/Makefile.in

dep-am: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < $(srcdir)/Makefile.am > tmp-Makefile.am
	cat DEP >> tmp-Makefile.am
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile.am $(srcdir)/Makefile.am

.PHONY: dep dep-in dep-am

###

MOSTLYCLEANFILES = sysinfo$(EXEEXT_FOR_BUILD) bin2c$(EXEEXT_FOR_BUILD) \
  binutils.log binutils.sum abcdefgh*
mostlyclean-local:
	-rm -rf tmpdir

CLEANFILES = dep.sed DEP DEPA DEP1 DEP2

.PHONY: install-exec-local

install-exec-local: install-binPROGRAMS $(bin_PROGRAMS) $(noinst_PROGRAMS)
	@list='$(RENAMED_PROGS)'; for p in $$list; do \
	  if test -f $$p$(EXEEXT); then \
	    echo " $(LIBTOOL)  --mode=install $(INSTALL_PROGRAM) $$p$(EXEEXT) $(bindir)/`echo $$p|sed -e 's/-new//' -e 's/cxxfilt/$(DEMANGLER_NAME)/'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	    $(LIBTOOL)  --mode=install $(INSTALL_PROGRAM) $$p$(EXEEXT) $(DESTDIR)$(bindir)/`echo $$p|sed -e 's/-new//' -e 's/cxxfilt/$(DEMANGLER_NAME)/'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	  else :; fi; \
	done
	$(mkinstalldirs) $(DESTDIR)$(tooldir)/bin
	for i in $(TOOL_PROGS); do \
	  if [ -f $$i$(EXEEXT) ]; then \
	    j=`echo $$i | sed -e 's/-new//'`; \
	    k=`echo $$j | sed '$(transform)'`; \
	    if [ "$(bindir)/$$k" != "$(tooldir)/bin/$$j" ]; then \
	      rm -f $(DESTDIR)$(tooldir)/bin/$$j$(EXEEXT); \
	      ln $(DESTDIR)$(bindir)/$$k$(EXEEXT) $(DESTDIR)$(tooldir)/bin/$$j$(EXEEXT) >/dev/null 2>/dev/null \
		|| $(LIBTOOL) --mode=install $(INSTALL_PROGRAM) $$i$(EXEEXT) $(DESTDIR)$(tooldir)/bin/$$j$(EXEEXT); \
	    fi; \
	  else true; \
	  fi; \
	done

# What appears below is generated by a hacked mkdep using gcc -MM.

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
addr2line.o: addr2line.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/demangle.h $(INCDIR)/libiberty.h \
  bucomm.h
ar.o: ar.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/progress.h $(INCDIR)/aout/ar.h $(BFDDIR)/libbfd.h \
  $(INCDIR)/hashtab.h bucomm.h arsup.h $(INCDIR)/filenames.h \
  binemul.h
arsup.o: arsup.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/filenames.h bucomm.h arsup.h
bin2c.o: bin2c.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h
binemul.o: binemul.c binemul.h sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h
bucomm.o: bucomm.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/filenames.h $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h \
  bucomm.h
coffdump.o: coffdump.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h bucomm.h coffgrok.h
coffgrok.o: coffgrok.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h bucomm.h coffgrok.h
cxxfilt.o: cxxfilt.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/demangle.h $(INCDIR)/libiberty.h \
  $(INCDIR)/safe-ctype.h bucomm.h
dwarf.o: dwarf.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/elf/dwarf2.h \
  dwarf.h
debug.o: debug.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  debug.h
dlltool.o: dlltool.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/demangle.h $(INCDIR)/libiberty.h \
  $(INCDIR)/dyn-string.h bucomm.h dlltool.h $(INCDIR)/safe-ctype.h
dllwrap.o: dllwrap.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/dyn-string.h bucomm.h
emul_aix.o: emul_aix.c binemul.h sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h $(INCDIR)/bfdlink.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/xcoff.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(BFDDIR)/libxcoff.h
emul_vanilla.o: emul_vanilla.c binemul.h sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h
filemode.o: filemode.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h
ieee.o: ieee.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/ieee.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h debug.h budbg.h $(INCDIR)/filenames.h
is-ranlib.o: is-ranlib.c
is-strip.o: is-strip.c
maybe-ranlib.o: maybe-ranlib.c
maybe-strip.o: maybe-strip.c
nlmconv.o: nlmconv.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h $(BFDDIR)/libnlm.h \
  $(INCDIR)/nlm/common.h $(INCDIR)/nlm/internal.h $(INCDIR)/nlm/external.h \
  nlmconv.h bucomm.h
nm.o: nm.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/progress.h $(INCDIR)/aout/stab_gnu.h \
  $(INCDIR)/aout/stab.def $(INCDIR)/aout/ranlib.h $(INCDIR)/demangle.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h \
  $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h $(INCDIR)/elf/common.h \
  bucomm.h
not-ranlib.o: not-ranlib.c
not-strip.o: not-strip.c
objcopy.o: objcopy.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/progress.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h bucomm.h \
  budbg.h $(INCDIR)/filenames.h $(INCDIR)/fnmatch.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h
objdump.o: objdump.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/progress.h \
  bucomm.h dwarf.h $(INCDIR)/safe-ctype.h $(INCDIR)/dis-asm.h \
  ../bfd/bfd.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/demangle.h $(INCDIR)/libiberty.h debug.h \
  budbg.h $(INCDIR)/aout/aout64.h
prdbg.o: prdbg.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/demangle.h $(INCDIR)/libiberty.h debug.h \
  budbg.h
rclex.o: rclex.c ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/safe-ctype.h windres.h $(INCDIR)/ansidecl.h \
  winduni.h windint.h rcparse.h
rdcoff.o: rdcoff.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/coff/internal.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h bucomm.h debug.h budbg.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h
rddbg.o: rddbg.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  bucomm.h debug.h budbg.h
readelf.o: readelf.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h dwarf.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/external.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/h8.h $(INCDIR)/elf/reloc-macros.h $(INCDIR)/elf/alpha.h \
  $(INCDIR)/elf/arc.h $(INCDIR)/elf/arm.h $(INCDIR)/elf/avr.h \
  $(INCDIR)/elf/bfin.h $(INCDIR)/elf/cris.h $(INCDIR)/elf/crx.h \
  $(INCDIR)/elf/d10v.h $(INCDIR)/elf/d30v.h $(INCDIR)/elf/dlx.h \
  $(INCDIR)/elf/fr30.h $(INCDIR)/elf/frv.h $(INCDIR)/elf/hppa.h \
  $(INCDIR)/elf/i386.h $(INCDIR)/elf/i370.h $(INCDIR)/elf/i860.h \
  $(INCDIR)/elf/i960.h $(INCDIR)/elf/ia64.h $(INCDIR)/elf/ip2k.h \
  $(INCDIR)/elf/iq2000.h $(INCDIR)/elf/m32c.h $(INCDIR)/elf/m32r.h \
  $(INCDIR)/elf/m68k.h $(INCDIR)/elf/m68hc11.h $(INCDIR)/elf/mcore.h \
  $(INCDIR)/elf/mep.h $(INCDIR)/elf/mips.h $(INCDIR)/elf/mmix.h \
  $(INCDIR)/elf/mn10200.h $(INCDIR)/elf/mn10300.h $(INCDIR)/elf/mt.h \
  $(INCDIR)/elf/msp430.h $(INCDIR)/elf/or32.h $(INCDIR)/elf/pj.h \
  $(INCDIR)/elf/ppc.h $(INCDIR)/elf/ppc64.h $(INCDIR)/elf/s390.h \
  $(INCDIR)/elf/score.h $(INCDIR)/elf/sh.h $(INCDIR)/elf/sparc.h \
  $(INCDIR)/elf/spu.h $(INCDIR)/elf/v850.h $(INCDIR)/elf/vax.h \
  $(INCDIR)/elf/x86-64.h $(INCDIR)/elf/xstormy16.h $(INCDIR)/elf/xtensa.h \
  $(INCDIR)/aout/ar.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  unwind-ia64.h
rename.o: rename.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h
resbin.o: resbin.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  windres.h winduni.h windint.h
rescoff.o: rescoff.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h windres.h winduni.h windint.h \
  $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
resrc.o: resrc.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/safe-ctype.h windres.h winduni.h windint.h
resres.o: resres.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  windres.h winduni.h windint.h
size.o: size.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  bucomm.h
srconv.o: srconv.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h sysroff.h coffgrok.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h sysroff.c
stabs.o: stabs.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/demangle.h $(INCDIR)/libiberty.h \
  debug.h budbg.h $(INCDIR)/filenames.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/aout/stab_gnu.h $(INCDIR)/aout/stab.def
strings.o: strings.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h bucomm.h
sysdump.o: sysdump.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h bucomm.h \
  sysroff.h sysroff.c $(INCDIR)/ansidecl.h
version.o: version.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h
windres.o: windres.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h \
  windres.h winduni.h windint.h
winduni.o: winduni.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h bucomm.h winduni.h $(INCDIR)/safe-ctype.h
wrstabs.o: wrstabs.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h bucomm.h \
  debug.h budbg.h $(INCDIR)/aout/aout64.h $(INCDIR)/aout/stab_gnu.h \
  $(INCDIR)/aout/stab.def
windmc.o: windmc.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h windmc.h \
  winduni.h windint.h
mclex.o: mclex.c sysdep.h $(INCDIR)/ansidecl.h ../bfd/bfdver.h \
  config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/safe-ctype.h windmc.h winduni.h mcparse.h
arparse.o: arparse.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h arsup.h
arlex.o: arlex.c $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h arparse.h
sysroff.o: sysroff.c
sysinfo.o: sysinfo.c
syslex.o: syslex.c config.h sysinfo.h
defparse.o: defparse.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h dlltool.h
deflex.o: deflex.c $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  defparse.h dlltool.h $(INCDIR)/ansidecl.h
nlmheader.o: nlmheader.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h $(INCDIR)/safe-ctype.h \
  ../bfd/bfd.h $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/nlm/common.h $(INCDIR)/nlm/internal.h bucomm.h \
  nlmconv.h
rcparse.o: rcparse.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h windres.h winduni.h windint.h \
  $(INCDIR)/safe-ctype.h
mcparse.o: mcparse.c sysdep.h $(INCDIR)/ansidecl.h \
  ../bfd/bfdver.h config.h $(INCDIR)/fopen-same.h ../bfd/bfd.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h bucomm.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h windmc.h winduni.h $(INCDIR)/safe-ctype.h
# IF YOU PUT ANYTHING HERE IT WILL GO AWAY
