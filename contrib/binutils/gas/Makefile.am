## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = 1.8 cygnus dejagnu
ACLOCAL_AMFLAGS = -I .. -I ../config -I ../bfd

SUBDIRS = doc po
# Automake should figure this out on its own.  It doesn't, because
# of the "cygnus" option.  But distclean still wants it.
DIST_SUBDIRS = $(SUBDIRS)

tooldir = $(exec_prefix)/$(target_alias)

YACC = `if [ -f ../bison/bison ] ; then echo ../bison/bison -y -L../bison/bison ; else echo @YACC@ ; fi`
LEX = `if [ -f ../flex/flex ] ; then echo ../flex/flex ; else echo @LEX@ ; fi`

# We have to set this, because autoconf 2.59 does not substitute YFLAGS.
# Autoconf 2.61 does, so this can be removed when we upgrade.
YFLAGS =

WARN_CFLAGS = @WARN_CFLAGS@
NO_WERROR = @NO_WERROR@
AM_CFLAGS = $(WARN_CFLAGS)

MKDEP = gcc -MM

TARG_CPU = @target_cpu_type@
TARG_CPU_C = $(srcdir)/config/tc-@target_cpu_type@.c
TARG_CPU_O = tc-@target_cpu_type@.o
TARG_CPU_H = $(srcdir)/config/tc-@target_cpu_type@.h
OBJ_FORMAT_C = $(srcdir)/config/obj-@obj_format@.c
OBJ_FORMAT_O = obj-@obj_format@.o
OBJ_FORMAT_H = $(srcdir)/config/obj-@obj_format@.h
TARG_ENV_H = $(srcdir)/config/te-@te_file@.h
ATOF_TARG_C = $(srcdir)/config/atof-@atof@.c
ATOF_TARG_O = atof-@atof@.o

# use @target_cpu_type@ for refering to configured target name
IT_HDRS=itbl-parse.h $(srcdir)/itbl-ops.h
IT_SRCS=itbl-parse.c itbl-lex.c $(srcdir)/itbl-ops.c
IT_DEPS=$(srcdir)/itbl-parse.y $(srcdir)/itbl-lex.l $(srcdir)/config/itbl-@target_cpu_type@.h
IT_OBJS=itbl-parse.o itbl-lex.o itbl-ops.o

# CPU types.  This is only used for dependency information.

CPU_TYPES = \
	alpha \
	arc \
	arm \
	avr \
	bfin \
	cr16 \
	cris \
	crx \
	d10v \
	d30v \
	dlx \
	fr30 \
	frv \
	h8300 \
	hppa \
	i370 \
	i386 \
	i860 \
	i960 \
	ia64 \
	ip2k \
	m32c \
	m32r \
	m68hc11 \
	m68k \
	maxq \
	mcore \
	mep \
	mips \
	mmix \
	mn10200 \
	mn10300 \
	msp430 \
	mt \
	ns32k \
	openrisc \
	or32 \
	pdp11 \
	pj \
	ppc \
	s390 \
	score \
	sh \
	sh64 \
	sparc \
	spu \
	tic30 \
	tic4x \
	tic54x \
	v850 \
	vax \
	xc16x \
	xstormy16 \
	xtensa \
	z80 \
	z8k

# Object format types.  This is only used for dependency information.
# We deliberately omit SOM, since it does not work as a cross assembler.

OBJ_FORMATS = \
	aout \
	coff \
	ecoff \
	elf \
	evax

# This is an sh case which sets valid according to whether the CPU
# type in the shell variable c and the OS type in the shell variable o
# are supported.  This helps cuts down on the amount of dependency
# information.

CPU_OBJ_VALID = \
	valid= ; \
	case $$o in \
	aout) \
	  case $$c in \
	  arm | cris | i386 | m68k | ns32k | pdp11 | sparc | tic30 | vax) \
	    valid=yes ;; \
	  esac ;; \
	coff) \
	  case $$c in \
	  arm | h8300 | i386 | i960 | m68k | maxq | mcore | mips | or32 \
		| ppc | sh | sparc | tic* | xscale | z80 | z8k) \
	    valid=yes ;; \
	  esac ;; \
	ecoff) \
	  case $$c in \
	  mips | alpha) valid=yes ;; \
	  esac ;; \
	elf) valid=yes ; \
	  case $$c in \
	  maxq | ns32k | tic* | z80 | z8k) valid= ;; \
	  esac ;; \
	evax) \
	  case $$c in \
	  alpha) valid=yes ;; \
	  esac ;; \
	vms) \
	  case $$c in \
	  vax) valid=yes ;; \
	  esac ;; \
	esac;

# These are like CPU_TYPES and CPU_OBJ_VALID, for the obj=multi case.

MULTI_CPU_TYPES = i386 mips cris

MULTI_CPU_OBJ_VALID = \
	valid= ; \
	case $$o in \
	aout) \
	  case $$c in \
	  i386 | cris) valid=yes ;; \
	  esac ;; \
	coff) \
	  case $$c in \
	  i386 | mips) valid=yes ;; \
	  esac ;; \
	ecoff) \
	  case $$c in \
	  mips) valid=yes ;; \
	  esac ;; \
	elf) valid=yes ;; \
	esac;

# Regular source files.

GAS_CFILES = \
	app.c \
	as.c \
	atof-generic.c \
	cond.c \
	depend.c \
	dwarf2dbg.c \
	dw2gencfi.c \
	ecoff.c \
	ehopt.c \
	expr.c \
	flonum-copy.c \
	flonum-konst.c \
	flonum-mult.c \
	frags.c \
	hash.c \
	input-file.c \
	input-scrub.c \
	listing.c \
	literal.c \
	macro.c \
	messages.c \
	output-file.c \
	read.c \
	sb.c \
	stabs.c \
	subsegs.c \
	symbols.c \
	write.c

CFILES = $(GAS_CFILES) itbl-ops.c

HFILES = \
	as.h \
	asintl.h \
	bignum.h \
	bit_fix.h \
	cgen.h \
	dwarf2dbg.h \
	dw2gencfi.h \
	ecoff.h \
	emul-target.h \
	emul.h \
	expr.h \
	flonum.h \
	frags.h \
	hash.h \
	input-file.h \
	itbl-lex.h \
	itbl-ops.h \
	listing.h \
	macro.h \
	obj.h \
	output-file.h \
	read.h \
	sb.h \
	struc-symbol.h \
	subsegs.h \
	symbols.h \
	tc.h \
	write.h

# CPU files in config.

TARGET_CPU_CFILES = \
	config/tc-alpha.c \
	config/tc-arc.c \
	config/tc-arm.c \
	config/tc-avr.c \
	config/tc-bfin.c \
	config/tc-cr16.c \
	config/tc-cris.c \
	config/tc-crx.c \
	config/tc-d10v.c \
	config/tc-d30v.c \
	config/tc-dlx.c \
	config/tc-fr30.c \
	config/tc-frv.c \
	config/tc-h8300.c \
	config/tc-hppa.c \
	config/tc-ia64.c \
	config/tc-i370.c \
	config/tc-i386.c \
	config/tc-i860.c \
	config/tc-i960.c \
	config/tc-ip2k.c \
	config/tc-m32c.c \
	config/tc-m32r.c \
	config/tc-m68hc11.c \
	config/tc-m68k.c \
	config/tc-mcore.c \
	config/tc-mep.c \
	config/tc-mips.c \
	config/tc-mmix.c \
	config/tc-mn10200.c \
	config/tc-mn10300.c \
	config/tc-msp430.c \
	config/tc-ns32k.c \
	config/tc-openrisc.c \
	config/tc-or32.c \
	config/tc-pdp11.c \
	config/tc-pj.c \
	config/tc-ppc.c \
	config/tc-s390.c \
	config/tc-score.c \
	config/tc-sh.c \
	config/tc-sh64.c \
	config/tc-sparc.c \
	config/tc-spu.c \
	config/tc-tic30.c \
	config/tc-tic54x.c \
	config/tc-vax.c \
	config/tc-v850.c \
	config/tc-xstormy16.c \
	config/tc-xc16x.c \
	config/tc-xtensa.c \
	config/tc-z80.c \
	config/tc-z8k.c

TARGET_CPU_HFILES = \
	config/tc-alpha.h \
	config/tc-arc.h \
	config/tc-arm.h \
	config/tc-avr.h \
	config/tc-bfin.h \
	config/tc-cr16.h \
	config/tc-cris.h \
	config/tc-crx.h \
	config/tc-d10v.h \
	config/tc-d30v.h \
	config/tc-dlx.h \
	config/tc-fr30.h \
	config/tc-frv.h \
	config/tc-h8300.h \
	config/tc-hppa.h \
	config/tc-ia64.h \
	config/tc-i370.h \
	config/tc-i386.h \
	config/tc-i860.h \
	config/tc-i960.h \
	config/tc-ip2k.h \
	config/tc-m32c.h \
	config/tc-m32r.h \
	config/tc-m68hc11.h \
	config/tc-m68k.h \
	config/tc-mcore.h \
	config/tc-mep.h \
	config/tc-mips.h \
	config/tc-mmix.h \
	config/tc-mn10200.h \
	config/tc-mn10300.h \
	config/tc-msp430.h \
	config/tc-ns32k.h \
	config/tc-openrisc.h \
	config/tc-or32.h \
	config/tc-pdp11.h \
	config/tc-pj.h \
	config/tc-ppc.h \
	config/tc-s390.h \
	config/tc-score.h \
	config/tc-sh.h \
	config/tc-sh64.h \
	config/tc-sparc.h \
	config/tc-spu.h \
	config/tc-tic30.h \
	config/tc-tic54x.h \
	config/tc-vax.h \
	config/tc-v850.h \
	config/tc-xstormy16.h \
	config/tc-xc16x.h \
	config/tc-xtensa.h \
	config/tc-z80.h \
	config/tc-z8k.h

# OBJ files in config

OBJ_FORMAT_CFILES = \
	config/obj-aout.c \
	config/obj-coff.c \
	config/obj-ecoff.c \
	config/obj-elf.c \
	config/obj-evax.c \
	config/obj-som.c

OBJ_FORMAT_HFILES = \
	config/obj-aout.h \
	config/obj-coff.h \
	config/obj-ecoff.h \
	config/obj-elf.h \
	config/obj-evax.h \
	config/obj-som.h

# Emulation header files in config

TARG_ENV_HFILES = \
	config/te-386bsd.h \
	config/te-armlinuxeabi.h \
	config/te-aux.h \
	config/te-delta.h \
	config/te-delt88.h \
	config/te-dpx2.h \
	config/te-dynix.h \
	config/te-epoc-pe.h \
	config/te-generic.h \
	config/te-go32.h \
	config/te-hppa.h \
	config/te-hppa64.h \
	config/te-hppalinux64.h \
	config/te-i386aix.h \
	config/te-ia64aix.h \
	config/te-ic960.h \
	config/te-linux.h \
	config/te-lnews.h \
	config/te-lynx.h \
	config/te-mach.h \
	config/te-macos.h \
	config/te-nbsd.h \
	config/te-nbsd532.h \
	config/te-netware.h \
	config/te-pc532mach.h \
	config/te-pe.h \
	config/te-psos.h \
	config/te-riscix.h \
	config/te-sparcaout.h \
	config/te-sun3.h \
	config/te-svr4.h \
	config/te-symbian.h \
	config/te-tmips.h \
	config/te-wince-pe.h

# Multi files in config

MULTI_CFILES = \
	config/e-crisaout.c \
	config/e-criself.c \
	config/e-i386aout.c \
	config/e-i386coff.c \
	config/e-i386elf.c \
	config/e-mipsecoff.c \
	config/e-mipself.c

CONFIG_OBJS = \
	$(TARG_CPU_O) \
	$(OBJ_FORMAT_O) \
	$(ATOF_TARG_O) \
	$(extra_objects)

GENERIC_OBJS = \
	app.o \
	as.o \
	atof-generic.o \
	cond.o \
	depend.o \
	dwarf2dbg.o \
	dw2gencfi.o \
	ehopt.o \
	expr.o \
	flonum-konst.o \
	flonum-copy.o \
	flonum-mult.o \
	frags.o \
	hash.o \
	input-file.o \
	input-scrub.o \
	literal.o \
	messages.o \
	output-file.o \
	read.o \
	subsegs.o \
	symbols.o \
	write.o \
	listing.o \
	ecoff.o \
	stabs.o \
	sb.o \
	macro.o

OBJS = $(CONFIG_OBJS) $(GENERIC_OBJS)

POTFILES = $(MULTI_CFILES) $(TARGET_ENV_HFILES) $(OBJ_FORMAT_HFILES) \
	$(OBJ_FORMAT_CFILES) $(TARGET_CPU_HFILES) $(TARGET_CPU_CFILES) \
	$(HFILES) $(CFILES)
po/POTFILES.in: @MAINT@ Makefile
	for f in $(POTFILES); do echo $$f; done | LC_COLLATE= sort > tmp \
	  && mv tmp $(srcdir)/po/POTFILES.in

# Note: GASP is now deprecated and has been removed.  It is still
# available in the CVS archive or older binutils releases if it is needed.
noinst_PROGRAMS = as-new
noinst_SCRIPTS = $(GDBINIT)
EXTRA_SCRIPTS = .gdbinit

EXTRA_DIST = m68k-parse.c itbl-parse.c itbl-parse.h itbl-lex.c \
	bfin-parse.c bfin-parse.h bfin-lex.c

diststuff: $(EXTRA_DIST) info
all: info

DISTCLEANFILES = targ-cpu.h obj-format.h targ-env.h itbl-cpu.h cgen-desc.h

# Now figure out from those variables how to compile and link.

BASEDIR = $(srcdir)/..
BFDDIR = $(BASEDIR)/bfd
INCDIR = $(BASEDIR)/include

# This is the variable actually used when we compile.
# Specify the directories to be searched for header files.
# Both . and srcdir are used, in that order,
# so that tm.h and config.h will be found in the compilation
# subdirectory rather than in the source directory.
INCLUDES = -D_GNU_SOURCE -I. -I$(srcdir) -I../bfd -I$(srcdir)/config \
	-I$(INCDIR) -I$(srcdir)/.. -I$(BFDDIR) @INCINTL@ \
	-DLOCALEDIR="\"$(datadir)/locale\""

# This should be parallel to INCLUDES, but should replace $(srcdir)
# with $${srcdir}, and should work in a subdirectory.  This is used
# when building dependencies, because the dependency building is done
# in a subdirectory.
DEP_INCLUDES = -D_GNU_SOURCE -I.. -I$${srcdir} -I../../bfd \
	-I$${srcdir}/config -I$${srcdir}/../include -I$${srcdir}/.. \
	-I$${srcdir}/../bfd @INCINTL@ \
	-DLOCALEDIR="\"$(datadir)/locale\""

DEP_FLAGS = -DOBJ_MAYBE_ELF \
	-I. -I.. -I$${srcdir} -I../../bfd $(DEP_INCLUDES)

# How to link with both our special library facilities
# and the system's installed libraries.

GASLIBS = @OPCODES_LIB@ ../bfd/libbfd.la ../libiberty/libiberty.a

# Files to be copied away after each stage in building.
STAGESTUFF = *.o $(noinst_PROGRAMS)

as_new_SOURCES = $(GAS_CFILES)
as_new_LDADD = $(TARG_CPU_O) $(OBJ_FORMAT_O) $(ATOF_TARG_O) \
	$(extra_objects) $(GASLIBS) $(LIBINTL) $(LIBM)
as_new_DEPENDENCIES = $(TARG_CPU_O) $(OBJ_FORMAT_O) $(ATOF_TARG_O) \
	$(extra_objects) $(GASLIBS) $(LIBINTL_DEP)

# Stuff that every object file depends upon.  If anything is removed
# from this list, remove it from dep-in.sed as well.
$(OBJS): ../bfd/bfd.h $(INCDIR)/symcat.h \
	$(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h $(INCDIR)/progress.h \
	$(INCDIR)/fopen-same.h $(INCDIR)/fopen-bin.h $(INCDIR)/fopen-vms.h \
	$(OBJ_FORMAT_H) $(TARG_CPU_H) $(TARG_ENV_H) \
	as.h asintl.h bignum.h bit_fix.h config.h emul.h expr.h flonum.h \
	frags.h hash.h listing.h obj.h read.h symbols.h tc.h write.h

EXPECT = expect
RUNTEST = runtest
RUNTESTFLAGS=

check-DEJAGNU: site.exp
	if [ -d testsuite ]; then \
	  true; \
	else \
	  mkdir testsuite; \
	fi
	rm -f testsuite/site.exp
	cp site.exp testsuite/site.exp
	rootme=`pwd`; export rootme; \
	srcdir=`cd ${srcdir}; pwd` ; export srcdir ; \
	EXPECT=${EXPECT} ; export EXPECT ; \
	runtest=$(RUNTEST); \
	cd testsuite; \
	if $(SHELL) -c "$$runtest --version" > /dev/null 2>&1; then \
	  $$runtest --tool $(DEJATOOL) --srcdir $${srcdir}/testsuite \
		$(RUNTESTFLAGS); \
	else echo "WARNING: could not find \`runtest'" 1>&2; :;\
	fi

# The implicit .c.o rule doesn't work for these, perhaps because of
# the variables, or perhaps because the sources are not on vpath.
$(TARG_CPU_O): $(TARG_CPU_C)
	$(COMPILE) -c $(TARG_CPU_C)
$(ATOF_TARG_O): $(ATOF_TARG_C)
	$(COMPILE) -c $(ATOF_TARG_C)

# ecoff.c only has full dependencies when ECOFF_DEBUGGING is defined,
# so the automatic dependency stuff doesn't work.
ecoff.o : ecoff.c ecoff.h $(INCDIR)/coff/internal.h $(INCDIR)/coff/sym.h \
	$(INCDIR)/coff/ecoff.h $(INCDIR)/coff/symconst.h \
	$(INCDIR)/aout/stab_gnu.h

# We need all these explicit rules for the multi stuff.  Because of
# these rules, we don't need one for OBJ_FORMAT_O.

obj-aout.o : $(srcdir)/config/obj-aout.c $(DEP_@target_cpu_type@_aout)
	$(COMPILE) -c $(srcdir)/config/obj-aout.c
obj-coff.o: $(srcdir)/config/obj-coff.c $(DEP_@target_cpu_type@_coff)
	$(COMPILE) -c $(srcdir)/config/obj-coff.c
obj-ecoff.o : $(srcdir)/config/obj-ecoff.c $(DEP_@target_cpu_type@_ecoff)
	$(COMPILE) -c $(srcdir)/config/obj-ecoff.c
obj-elf.o : $(srcdir)/config/obj-elf.c $(DEP_@target_cpu_type@_elf)
	$(COMPILE) -c $(srcdir)/config/obj-elf.c
obj-evax.o : $(srcdir)/config/obj-evax.c
	$(COMPILE) -c $(srcdir)/config/obj-evax.c
obj-multi.o : $(srcdir)/config/obj-multi.c
	$(COMPILE) -c $(srcdir)/config/obj-multi.c
obj-som.o : $(srcdir)/config/obj-som.c
	$(COMPILE) -c $(srcdir)/config/obj-som.c

e-mipself.o : $(srcdir)/config/e-mipself.c
	$(COMPILE) -c $(srcdir)/config/e-mipself.c
e-mipsecoff.o : $(srcdir)/config/e-mipsecoff.c
	$(COMPILE) -c $(srcdir)/config/e-mipsecoff.c
e-i386aout.o: $(srcdir)/config/e-i386aout.c
	$(COMPILE) -c $(srcdir)/config/e-i386aout.c
e-i386coff.o: $(srcdir)/config/e-i386coff.c
	$(COMPILE) -c $(srcdir)/config/e-i386coff.c
e-i386elf.o: $(srcdir)/config/e-i386elf.c
	$(COMPILE) -c $(srcdir)/config/e-i386elf.c
e-crisaout.o: $(srcdir)/config/e-crisaout.c
	$(COMPILE) -c $(srcdir)/config/e-crisaout.c
e-criself.o: $(srcdir)/config/e-criself.c
	$(COMPILE) -c $(srcdir)/config/e-criself.c

xtensa-relax.o: $(srcdir)/config/xtensa-relax.c
	$(COMPILE) -c $(srcdir)/config/xtensa-relax.c

# The m68k operand parser.

EXTRA_as_new_SOURCES = config/m68k-parse.y config/bfin-parse.y

# If m68k-parse.y is in a different directory, then ylwrap will use an
# absolute path when it invokes yacc, which will cause yacc to put the
# absolute path into the generated file.  That's a pain when it comes
# to generating snapshots, because it introduces spurious diffs.
# Since when we make the snapshots $(srcdir) = ".", we check for that
# case and handle it differently.  This means that anybody who
# configures with $(srcdir) = "." will have to set their path in the
# debugger if they want to debug m68k-parse.y.  This is bad, but on
# the other hand it's good that people who use the prebuilt
# m68k-parse.c don't get a spurious absolute path.
m68k-parse.c: $(srcdir)/config/m68k-parse.y
	f=$(srcdir)/config/m68k-parse.y; \
	if [ $$f = "./config/m68k-parse.y" ]; then \
	  ln -s config/m68k-parse.y . > /dev/null 2>/dev/null || \
	   ln config/m68k-parse.y . > /dev/null 2>/dev/null || \
	   cp config/m68k-parse.y . >/dev/null 2>/dev/null; \
	  f=m68k-parse.y; \
	else true; fi; \
	$(SHELL) $(YLWRAP) $$f y.tab.c m68k-parse.c -- $(YACCCOMPILE); \
	if [ $$f = "m68k-parse.y" ]; then \
	  rm -f m68k-parse.y; \
	else true; fi
# Disable -Werror, if it has been enabled, since old versions of bison/
# yacc will produce working code which contain compile time warnings.
m68k-parse.o: m68k-parse.c $(srcdir)/config/m68k-parse.h
	$(COMPILE) -c $< $(NO_WERROR)

# Don't let the .y.h rule clobber m68k-parse.h.
m68k-parse.h: ; @true
$(srcdir)/config/m68k-parse.h: ; @true

bfin-parse.c: $(srcdir)/config/bfin-parse.y
	$(SHELL) $(YLWRAP) $(srcdir)/config/bfin-parse.y y.tab.c bfin-parse.c y.tab.h bfin-parse.h -- $(YACCCOMPILE) -d ;
bfin-parse.h: bfin-parse.c
bfin-parse.o: bfin-parse.c bfin-parse.h $(srcdir)/config/bfin-defs.h \
 $(INCDIR)/elf/common.h $(INCDIR)/elf/bfin.h $(BFDDIR)/libbfd.h

bfin-defs.h: ; @true
$(srcdir)/config/bfin-defs.h: ; @true

bfin-lex.c: $(srcdir)/config/bfin-lex.l
	$(SHELL) $(YLWRAP) $(srcdir)/config/bfin-lex.l lex.yy.c bfin-lex.c -- $(LEXCOMPILE)
bfin-lex.o: bfin-lex.c bfin-parse.h $(srcdir)/config/bfin-defs.h
	$(COMPILE) -c $< $(NO_WERROR)

# The instruction table specification lexical analyzer and parser.

itbl-lex.c: $(srcdir)/itbl-lex.l

# Disable -Werror, if it has been enabled, since old versions of bison/
# yacc will produce working code which contain compile time warnings.
itbl-lex.o: itbl-lex.c itbl-parse.h $(srcdir)/itbl-lex.h
	$(COMPILE) -c $< $(NO_WERROR)

# Disable -Werror, if it has been enabled, since old versions of bison/
# yacc will produce working code which contain compile time warnings.
itbl-parse.o: itbl-parse.c itbl-parse.h $(srcdir)/itbl-ops.h $(srcdir)/itbl-lex.h
	$(COMPILE) -c $< $(NO_WERROR)

itbl-ops.o: $(srcdir)/itbl-ops.c $(srcdir)/itbl-ops.h itbl-parse.h

itbl-parse.c itbl-parse.h: $(srcdir)/itbl-parse.y
	$(SHELL) $(YLWRAP) $(srcdir)/itbl-parse.y y.tab.c itbl-parse.c y.tab.h itbl-parse.h -- $(YACCCOMPILE) -d

# stand-alone itbl assembler & disassembler

EXTRA_PROGRAMS = itbl-test
itbl_test_SOURCES = itbl-parse.y itbl-lex.l
itbl_test_LDADD = itbl-tops.o itbl-test.o $(GASLIBS) @LEXLIB@

itbl-tops.o: $(srcdir)/itbl-ops.c $(srcdir)/itbl-ops.h itbl-parse.h
	$(COMPILE) -o itbl-tops.o -DSTAND_ALONE -c $(srcdir)/itbl-ops.c

itbl-test.o: $(srcdir)/testsuite/gas/all/itbl-test.c $(srcdir)/itbl-ops.h
	$(COMPILE) -c -DSTAND_ALONE $(srcdir)/testsuite/gas/all/itbl-test.c

# CGEN interface.

CGEN_CPU_PREFIX = @cgen_cpu_prefix@

cgen.o: cgen.c cgen.h cgen-desc.h subsegs.h \
	$(INCDIR)/obstack.h $(INCDIR)/opcode/cgen.h \
	$(srcdir)/../opcodes/$(CGEN_CPU_PREFIX)-desc.h \
	$(srcdir)/../opcodes/$(CGEN_CPU_PREFIX)-opc.h

# Remake the info files.

MOSTLYCLEANFILES = $(STAGESTUFF) core \
	testsuite/*.o testsuite/*.out testsuite/gas.log testsuite/gas.sum \
	testsuite/site.exp site.bak site.exp stage stage1 stage2

CLEANFILES = dep.sed DEPTC DEPTCA DEPOBJ DEPOBJA DEP2 DEP2A DEP1 DEPA DEP DEPDIR

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

.PHONY: install-exec-local install-data-local
.PHONY: install-exec-bindir install-exec-tooldir

install-exec-local: install-exec-bindir @install_tooldir@

install-exec-bindir: $(noinst_PROGRAMS)
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	@list='$(noinst_PROGRAMS)'; for p in $$list; do \
	  if test -f $$p; then \
	    echo " $(LIBTOOL)  --mode=install $(INSTALL_PROGRAM) $$p $(bindir)/`echo $$p|sed -e 's/$(EXEEXT)$$//' -e 's/-new//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	    $(LIBTOOL)  --mode=install $(INSTALL_PROGRAM) $$p $(DESTDIR)$(bindir)/`echo $$p|sed -e 's/$(EXEEXT)$$//' -e 's/-new//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	  else :; fi; \
	done

install-exec-tooldir: install-exec-bindir $(noinst_PROGRAMS)
	$(mkinstalldirs) $(DESTDIR)$(tooldir)/bin
	n=`echo as | sed '$(transform)'`; \
	if [ "$(bindir)/$$n$(EXEEXT)" != "$(tooldir)/bin/as$(EXEEXT)" ]; then \
	  rm -f $(DESTDIR)$(tooldir)/bin/as$(EXEEXT); \
	  ln $(DESTDIR)$(bindir)/$$n$(EXEEXT) $(DESTDIR)$(tooldir)/bin/as$(EXEEXT) >/dev/null 2>/dev/null \
	    || $(LIBTOOL) --mode=install $(INSTALL_PROGRAM) as-new$(EXEEXT) $(DESTDIR)$(tooldir)/bin/as$(EXEEXT); \
	else \
	  true ; \
	fi

# These exist for maintenance purposes.

.PHONY: bootstrap bootstrap2 bootstrap3 stage1 stage2 stage3 comparison

bootstrap: as-new
	$(MAKE) stage1
	rm -f stage && ln -s stage1 stage
	$(MAKE) CC="$(CC)" CFLAGS="-O -Bstage/ $(CFLAGS)" libdir=$(libdir) ALLOCA= $(noinst_PROGRAMS)
	$(MAKE) stage2
	rm -f stage && ln -s stage2 stage
	$(MAKE) CC="$(CC)" CFLAGS="-O -Bstage/ $(CFLAGS)" libdir=$(libdir) ALLOCA= $(noinst_PROGRAMS)
	$(MAKE) comparison against=stage2

bootstrap2:
	rm -f stage && ln -s stage1 stage
	$(MAKE) CC="$(CC)" CFLAGS="-O -Bstage/ $(CFLAGS)" libdir=$(libdir) ALLOCA= $(noinst_PROGRAMS)
	$(MAKE) stage2
	rm -f stage && ln -s stage2 stage
	$(MAKE) CC="$(CC)" CFLAGS="-O -Bstage/ $(CFLAGS)" libdir=$(libdir) ALLOCA= $(noinst_PROGRAMS)
	$(MAKE) comparison against=stage2

bootstrap3:
	rm -f stage && ln -s stage2 stage
	$(MAKE) CC="$(CC)" CFLAGS="-O -Bstage/ $(CFLAGS)" libdir=$(libdir) ALLOCA= $(noinst_PROGRAMS)
	$(MAKE) comparison against=stage2

# Copy the object files from a particular stage into a subdirectory.
stage1:
	-mkdir stage1
	-mv $(STAGESTUFF) stage1
	if [ -f stage1/as-new$(EXEEXT) -a ! -f stage1/as$(EXEEXT) ] ; then (cd stage1 ; ln -s as-new$(EXEEXT) as$(EXEEXT)) ; fi

stage2:
	-mkdir stage2
	-mv $(STAGESTUFF) stage2
	if [ -f stage2/as-new$(EXEEXT) -a ! -f stage2/as$(EXEEXT) ] ; then (cd stage2 ; ln -s as-new$(EXEEXT) as$(EXEEXT)) ; fi

stage3:
	-mkdir stage3
	-mv $(STAGESTUFF) stage3
	if [ -f stage3/as-new$(EXEEXT) -a ! -f stage3/as$(EXEEXT) ] ; then (cd stage3 ; ln -s as-new as$(EXEEXT)) ; fi

against=stage2

# This rule is derived from corresponding code in the Makefile.in for gcc.
# The "tail +16c" is to bypass headers which may include timestamps or
# temporary assembly file names.
comparison:
	x=0 ; \
	for file in *.o ; do \
	  tail +16c ./$$file > tmp-foo1; \
	  if tail +16c ${against}/$$file > tmp-foo2 2>/dev/null ; then \
	    if cmp tmp-foo1 tmp-foo2 ; then \
	      true ; \
	    else \
	      echo $$file differs ; \
	      x=1 ; \
	    fi ; \
	  else true; fi ; \
	done ; \
	exit $$x
	-rm -f tmp-foo*

.PHONY: de-stage1 de-stage2 de-stage3

de-stage1:
	- (cd stage1 ; rm -f as$(EXEEXT) ; mv -f * ..)
	- rmdir stage1

de-stage2:
	- (cd stage2 ; rm -f as$(EXEEXT) ; mv -f * ..)
	- rmdir stage2

de-stage3:
	- (cd stage3 ; rm -f as$(EXEEXT) ; mv -f * ..)
	- rmdir stage3

DEP_FILE_DEPS =  $(CFILES) $(HFILES) $(TARGET_CPU_CFILES) \
	$(TARGET_CPU_HFILES) $(OBJ_FORMAT_CFILES) $(OBJ_FORMAT_HFILES)

CONFIG_STATUS_DEPENDENCIES = $(BFDDIR)/configure.in $(srcdir)/configure.tgt

# Automatic dependency computation.  This is a real pain, because the
# dependencies change based on target_cpu_type and obj_format.
# Just to make things even more complicated, automake separates the
# dependency variable assignments from the dependency rules, and tacks
# on a .NOEXPORT at the end of Makefile.in.

DEP: dep.sed $(DEP_FILE_DEPS) DEPTC DEPOBJ DEP2
	rm -f DEP1 # delete because we use $? in DEP1 rule
	srcdir=`cd $(srcdir); pwd`; \
	$(MAKE) MKDEP="$(MKDEP)" srcdir="$${srcdir}" VPATH="$${srcdir}" DEP1
	rm -rf DEPDIR
	echo 'AMKDEP = #DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING BELOW.' > DEPA
	sed -f dep.sed < DEPTC >> DEPA
	sed -f dep.sed < DEPOBJ >> DEPA
	sed -f dep.sed < DEP2 >> DEPA
	echo 'BMKDEP = #DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING ABOVE.' >> DEPA
	echo '#MKDEP    DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING BELOW.' >> DEPA
	sed -f dep.sed < DEP1 >> DEPA
	echo '$$(OBJS): $$(DEP_@target''_cpu_type@_@obj''_format@)' >> DEPA
	echo '$$(TARG_CPU_O): $$(DEPTC_@target''_cpu_type@_@obj''_format@)' >> DEPA
	echo '$$(OBJ_FORMAT_O): $$(DEPOBJ_@target''_cpu_type@_@obj''_format@)' >> DEPA
	echo '#MKDEP    DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING ABOVE.' >> DEPA
	if grep ' /' DEPA > /dev/null 2> /dev/null; then \
	  echo 'make DEP failed!'; exit 1; \
	else \
	  mv -f DEPA $@; \
	fi

DEP1: $(CFILES) $(MULTI_CFILES)
	if [ -d DEPDIR ]; then true; else mkdir DEPDIR; fi
	srcdir=`cd $(srcdir); pwd`; \
	cd DEPDIR; \
	echo '' > targ-cpu.h; \
	echo '' > obj-format.h; \
	echo '' > targ-env.h; \
	echo '' > itbl-cpu.h; \
	echo '' > itbl-parse.h; \
	$(MKDEP) $(DEP_FLAGS) $? > DEP
	mv -f DEPDIR/DEP $@

# Work out the special dependencies for the tc-*.c files.
DEPTC: $(TARGET_CPU_CFILES)
	rm -f DEPTCA
	if [ -d DEPDIR ]; then true; else mkdir DEPDIR; fi
	srcdir=`cd $(srcdir); pwd`; \
	cd DEPDIR; \
	for c in $(CPU_TYPES); do \
	  for o in $(OBJ_FORMATS); do \
	    $(CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '#include "tc-'"$${c}"'.h"' > targ-cpu.h; \
	      echo '#include "obj-'"$${o}"'.h"' > obj-format.h; \
	      echo '#include "te-generic.h"' > targ-env.h; \
	      echo '' > itbl-cpu.h; \
	      echo '' > itbl-parse.h; \
	      echo '#include "opcodes/'"$${c}"'-desc.h"' > cgen-desc.h; \
	      rm -f dummy.c; \
	      cp $${srcdir}/config/tc-$${c}.c dummy.c; \
	      $(MKDEP) $(DEP_FLAGS) dummy.c | \
	        sed -e "s/dummy.o: dummy.c/DEPTC_$${c}_$${o} =/" >> ../DEPTCA; \
	      rm -f dummy.c; \
	    else true; fi; \
	  done; \
	done
	echo 'DEPTC_hppa_som = $$(srcdir)/config/tc-hppa.h subsegs.h \' >> DEPTCA
	echo '  $$(INCDIR)/obstack.h $$(BFDDIR)/libhppa.h \' >> DEPTCA
	echo '  $$(INCDIR)/opcode/hppa.h $$(BFDDIR)/som.h' >> DEPTCA
	for c in $(MULTI_CPU_TYPES); do \
	  echo "DEPTC_$${c}"'_multi = \' >> DEPTCA; \
	  for o in $(OBJ_FORMATS); do \
	    $(MULTI_CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '$$(DEPTC_'"$${c}_$${o}"') \' >> DEPTCA; \
	    else true; fi; \
	  done; \
	  echo '' >> DEPTCA; \
	done
	mv -f DEPTCA DEPTC

# Work out the special dependencies for the obj-*.c files.
DEPOBJ: $(OBJ_FORMAT_CFILES)
	rm -f DEPOBJA
	if [ -d DEPDIR ]; then true; else mkdir DEPDIR; fi
	srcdir=`cd $(srcdir); pwd`; \
	cd DEPDIR; \
	for c in $(CPU_TYPES); do \
	  for o in $(OBJ_FORMATS); do \
	    $(CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '#include "tc-'"$${c}"'.h"' > targ-cpu.h; \
	      echo '#include "obj-'"$${o}"'.h"' > obj-format.h; \
	      echo '#include "te-generic.h"' > targ-env.h; \
	      echo '' > itbl-cpu.h; \
	      echo '' > itbl-parse.h; \
	      rm -f dummy.c; \
	      cp $${srcdir}/config/obj-$${o}.c dummy.c; \
	      $(MKDEP) $(DEP_FLAGS) dummy.c | \
	        sed -e "s/dummy.o: dummy.c/DEPOBJ_$${c}_$${o} =/" >> ../DEPOBJA; \
	      rm -f dummy.c; \
	    else true; fi; \
	  done; \
	done
	echo 'DEPOBJ_hppa_som = $$(srcdir)/config/obj-som.h subsegs.h \' >> DEPOBJA
	echo '  $$(INCDIR)/obstack.h $$(BFDDIR)/libhppa.h \' >> DEPOBJA
	echo '  $$(BFDDIR)/som.h  $$(INCDIR)/aout/stab_gnu.h \' >> DEPOBJA
	echo '  $$(INCDIR)/aout/stab.def' >> DEPOBJA
	for c in $(MULTI_CPU_TYPES); do \
	  echo "DEPOBJ_$${c}"'_multi = \' >> DEPOBJA; \
	  for o in $(OBJ_FORMATS); do \
	    $(MULTI_CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '$$(DEPOBJ_'"$${c}_$${o}"') \' >> DEPOBJA; \
	    else true; fi; \
	  done; \
	  echo '' >> DEPOBJA; \
	done
	mv -f DEPOBJA DEPOBJ

# Work out the dependencies for each CPU/OBJ combination.
# Note that SOM is a special case, because it only works native.
DEP2: $(TARGET_CPU_HFILES) $(OBJ_FORMAT_HFILES)
	rm -f DEP2A
	if [ -d DEPDIR ]; then true; else mkdir DEPDIR; fi
	srcdir=`cd $(srcdir); pwd`; \
	cd DEPDIR; \
	for c in $(CPU_TYPES); do \
	  for o in $(OBJ_FORMATS); do \
	    $(CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '#include "tc-'"$${c}"'.h"' > targ-cpu.h; \
	      echo '#include "obj-'"$${o}"'.h"' > dummy.c; \
	      $(MKDEP) $(DEP_FLAGS) dummy.c | \
	        sed -e "s/dummy.o: dummy.c/DEP_$${c}_$${o} =/" >> ../DEP2A; \
	    else true; fi; \
	  done; \
	done
	echo 'DEP_hppa_som = $$(BFDDIR)/som.h' >> DEP2A
	for c in $(MULTI_CPU_TYPES); do \
	  echo "DEP_$${c}"'_multi = \' >> DEP2A; \
	  for o in $(OBJ_FORMATS); do \
	    $(MULTI_CPU_OBJ_VALID) \
	    if [ x$${valid} = xyes ]; then \
	      echo '$$(DEP_'"$${c}_$${o}"') \' >> DEP2A; \
	    else true; fi; \
	  done; \
	  echo '' >> DEP2A; \
	done
	mv -f DEP2A DEP2

dep.sed: dep-in.sed config.status
	srcdir=`cd $(srcdir); pwd`; \
	sed <$(srcdir)/dep-in.sed >dep.sed \
		-e "s!@INCDIR@!$${srcdir}/../include!" \
		-e "s!@BFDDIR@!$${srcdir}/../bfd!" \
		-e "s!@SRCDIR@!$${srcdir}!" \
		-e 's!@TOPDIR@!'`echo $(srcdir) | sed -e s,/gas,,`'!'

dep: DEP
	sed -e '/^.MKDEP.*WARNING BELOW./,/^.MKDEP.*WARNING ABOVE./d' \
	  < Makefile > tmp-Makefile
	cat DEP >> tmp-Makefile
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile Makefile

dep-in: DEP
	sed -e '/^.MKDEP.*WARNING BELOW./,/^.MKDEP.*WARNING ABOVE./d' \
	  < $(srcdir)/Makefile.in > tmp-Makefile.in
	cat DEP >> tmp-Makefile.in
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile.in $(srcdir)/Makefile.in

dep-am: DEP
	sed -e '/^.MKDEP.*WARNING BELOW./,/^.MKDEP.*WARNING ABOVE./d' \
	  < $(srcdir)/Makefile.am > tmp-Makefile.am
	cat DEP >> tmp-Makefile.am
	$(SHELL) $(srcdir)/../move-if-change tmp-Makefile.am $(srcdir)/Makefile.am

# HEED THE MKDEP WARNINGS.
# ANYTHING CHANGED OR ADDED BETWEEN THE WARNING LINES MAY GO AWAY.
.PHONY: dep dep-in dep-am

AMKDEP = #DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING BELOW.
DEPTC_alpha_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-alpha.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  subsegs.h $(INCDIR)/obstack.h struc-symbol.h ecoff.h \
  $(INCDIR)/opcode/alpha.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(INCDIR)/safe-ctype.h $(srcdir)/config/atof-vax.c
DEPTC_alpha_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-alpha.h subsegs.h \
  $(INCDIR)/obstack.h struc-symbol.h ecoff.h $(INCDIR)/coff/sym.h \
  $(INCDIR)/coff/ecoff.h $(INCDIR)/opcode/alpha.h $(INCDIR)/elf/alpha.h \
  $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h $(INCDIR)/safe-ctype.h $(srcdir)/config/atof-vax.c
DEPTC_alpha_evax = $(srcdir)/config/obj-evax.h $(srcdir)/config/tc-alpha.h \
  subsegs.h $(INCDIR)/obstack.h struc-symbol.h ecoff.h \
  $(INCDIR)/opcode/alpha.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(INCDIR)/safe-ctype.h $(srcdir)/config/atof-vax.c
DEPTC_arc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arc.h dwarf2dbg.h \
  struc-symbol.h $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/arc.h $(srcdir)/../opcodes/arc-ext.h \
  $(INCDIR)/elf/arc.h $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h
DEPTC_arm_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-arm.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h $(INCDIR)/opcode/arm.h \
  dwarf2dbg.h
DEPTC_arm_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-arm.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/arm.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h $(INCDIR)/opcode/arm.h \
  dwarf2dbg.h
DEPTC_arm_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arm.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h $(INCDIR)/opcode/arm.h \
  $(INCDIR)/elf/arm.h $(INCDIR)/elf/reloc-macros.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h dwarf2dbg.h
DEPTC_avr_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-avr.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/avr.h
DEPTC_bfin_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-bfin.h dwarf2dbg.h \
  struc-symbol.h $(srcdir)/config/bfin-defs.h $(INCDIR)/obstack.h \
  $(INCDIR)/safe-ctype.h dwarf2dbg.h $(BFDDIR)/libbfd.h \
  $(INCDIR)/hashtab.h $(INCDIR)/elf/common.h $(INCDIR)/elf/bfin.h \
  $(INCDIR)/elf/reloc-macros.h $(srcdir)/config/bfin-aux.h \
  $(srcdir)/config/bfin-defs.h $(INCDIR)/opcode/bfin.h
DEPTC_cr16_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-cr16.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h dwarf2dbg.h $(INCDIR)/opcode/cr16.h \
  $(INCDIR)/elf/cr16.h $(INCDIR)/elf/reloc-macros.h
DEPTC_cris_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-cris.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/cris.h \
  dwarf2dbg.h
DEPTC_cris_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-cris.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/cris.h dwarf2dbg.h
DEPTC_crx_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-crx.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h dwarf2dbg.h $(INCDIR)/opcode/crx.h \
  $(INCDIR)/elf/crx.h $(INCDIR)/elf/reloc-macros.h
DEPTC_d10v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d10v.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/d10v.h \
  $(INCDIR)/elf/ppc.h $(INCDIR)/elf/reloc-macros.h
DEPTC_d30v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d30v.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/d30v.h
DEPTC_dlx_elf = $(INCDIR)/safe-ctype.h $(srcdir)/config/obj-elf.h \
  $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h $(srcdir)/config/tc-dlx.h \
  dwarf2dbg.h $(srcdir)/config/tc-dlx.h $(INCDIR)/opcode/dlx.h
DEPTC_fr30_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-fr30.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/opcode/cgen-bitset.h \
  $(srcdir)/../opcodes/fr30-opc.h cgen.h
DEPTC_frv_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-frv.h subsegs.h \
  $(INCDIR)/obstack.h $(srcdir)/../opcodes/frv-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/frv-opc.h \
  cgen.h $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h $(INCDIR)/elf/common.h \
  $(INCDIR)/elf/frv.h $(INCDIR)/elf/reloc-macros.h
DEPTC_h8300_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-h8300.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/h8300.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h subsegs.h $(INCDIR)/obstack.h \
  dwarf2dbg.h $(INCDIR)/opcode/h8300.h $(INCDIR)/safe-ctype.h
DEPTC_h8300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-h8300.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h $(INCDIR)/opcode/h8300.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/elf/h8.h $(INCDIR)/elf/reloc-macros.h
DEPTC_hppa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-hppa.h $(BFDDIR)/elf32-hppa.h \
  $(BFDDIR)/elf-bfd.h $(BFDDIR)/libhppa.h $(INCDIR)/elf/hppa.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(BFDDIR)/libhppa.h $(INCDIR)/opcode/hppa.h dwarf2dbg.h
DEPTC_i370_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i370.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  struc-symbol.h $(INCDIR)/opcode/i370.h $(INCDIR)/elf/i370.h \
  $(INCDIR)/elf/reloc-macros.h
DEPTC_i386_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h $(INCDIR)/elf/x86-64.h $(INCDIR)/elf/reloc-macros.h
DEPTC_i386_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/x86_64.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h $(INCDIR)/elf/x86-64.h $(INCDIR)/elf/reloc-macros.h
DEPTC_i386_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i386.h $(srcdir)/../opcodes/i386-opc.h \
  $(INCDIR)/opcode/i386.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h $(INCDIR)/elf/x86-64.h $(INCDIR)/elf/reloc-macros.h
DEPTC_i860_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i860.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/i860.h $(INCDIR)/elf/i860.h $(INCDIR)/elf/reloc-macros.h
DEPTC_i960_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i960.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/i960.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/i960.h
DEPTC_i960_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i960.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h $(INCDIR)/opcode/i960.h
DEPTC_ia64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ia64.h $(INCDIR)/opcode/ia64.h \
  $(INCDIR)/elf/ia64.h $(INCDIR)/elf/reloc-macros.h $(INCDIR)/safe-ctype.h \
  dwarf2dbg.h subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/ia64.h \
  $(INCDIR)/elf/ia64.h
DEPTC_ip2k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ip2k.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/ip2k-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/ip2k-opc.h \
  cgen.h $(INCDIR)/elf/common.h $(INCDIR)/elf/ip2k.h \
  $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h
DEPTC_m32c_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32c.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/m32c-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/m32c-opc.h \
  $(srcdir)/../opcodes/cgen-types.h ../bfd/bfd_stdint.h \
  $(srcdir)/../opcodes/cgen-ops.h cgen.h $(INCDIR)/elf/common.h \
  $(INCDIR)/elf/m32c.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/libbfd.h \
  $(INCDIR)/hashtab.h $(INCDIR)/safe-ctype.h
DEPTC_m32r_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32r.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/opcode/cgen-bitset.h \
  $(srcdir)/../opcodes/m32r-opc.h cgen.h $(INCDIR)/elf/m32r.h \
  $(INCDIR)/elf/reloc-macros.h
DEPTC_m68hc11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68hc11.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/m68hc11.h dwarf2dbg.h $(INCDIR)/elf/m68hc11.h \
  $(INCDIR)/elf/reloc-macros.h
DEPTC_m68k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-m68k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h $(INCDIR)/opcode/m68k.h \
  $(srcdir)/config/m68k-parse.h
DEPTC_m68k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-m68k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/m68k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h $(INCDIR)/opcode/m68k.h \
  $(srcdir)/config/m68k-parse.h
DEPTC_m68k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68k.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h $(INCDIR)/opcode/m68k.h \
  $(srcdir)/config/m68k-parse.h $(INCDIR)/elf/m68k.h \
  $(INCDIR)/elf/reloc-macros.h
DEPTC_maxq_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-maxq.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/maxq.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h $(srcdir)/config/tc-maxq.h \
  $(INCDIR)/opcode/maxq.h
DEPTC_mcore_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mcore.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mcore.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/mcore-opc.h $(INCDIR)/safe-ctype.h
DEPTC_mcore_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mcore.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/mcore-opc.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/elf/mcore.h $(INCDIR)/elf/reloc-macros.h
DEPTC_mep_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mep.h dwarf2dbg.h \
  dwarf2dbg.h subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/mep-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/mep-opc.h \
  cgen.h $(INCDIR)/elf/common.h $(INCDIR)/elf/mep.h $(INCDIR)/elf/reloc-macros.h \
  $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h $(INCDIR)/xregex.h \
  $(INCDIR)/xregex2.h
DEPTC_mips_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mips.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mipspe.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/opcode/mips.h itbl-ops.h \
  dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h $(srcdir)/config/obj-elf.h \
  $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/external.h $(INCDIR)/elf/mips.h $(INCDIR)/elf/reloc-macros.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h
DEPTC_mips_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-mips.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/mips.h itbl-ops.h dwarf2dbg.h dw2gencfi.h \
  $(INCDIR)/elf/dwarf2.h $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/elf/mips.h $(INCDIR)/elf/reloc-macros.h \
  ecoff.h
DEPTC_mips_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mips.h subsegs.h \
  $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h $(INCDIR)/opcode/mips.h \
  itbl-ops.h dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(srcdir)/config/obj-elf.h $(INCDIR)/elf/mips.h $(INCDIR)/elf/reloc-macros.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h
DEPTC_mmix_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mmix.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/elf/mmix.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/opcode/mmix.h \
  $(INCDIR)/safe-ctype.h dwarf2dbg.h $(INCDIR)/obstack.h
DEPTC_mn10200_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10200.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/mn10200.h
DEPTC_mn10300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10300.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/mn10300.h dwarf2dbg.h
DEPTC_msp430_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-msp430.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/msp430.h \
  $(INCDIR)/safe-ctype.h dwarf2dbg.h
DEPTC_mt_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mt.h dwarf2dbg.h \
  dwarf2dbg.h subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/mt-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/mt-opc.h \
  cgen.h $(INCDIR)/elf/common.h $(INCDIR)/elf/mt.h $(INCDIR)/elf/reloc-macros.h \
  $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h
DEPTC_ns32k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-ns32k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/opcode/ns32k.h \
  $(INCDIR)/obstack.h
DEPTC_openrisc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-openrisc.h \
  dwarf2dbg.h subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/openrisc-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/openrisc-opc.h \
  cgen.h
DEPTC_or32_coff = $(INCDIR)/safe-ctype.h $(srcdir)/config/obj-coff.h \
  $(srcdir)/config/tc-or32.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/or32.h $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h \
  $(INCDIR)/opcode/or32.h $(INCDIR)/elf/or32.h $(INCDIR)/elf/reloc-macros.h
DEPTC_or32_elf = $(INCDIR)/safe-ctype.h $(srcdir)/config/obj-elf.h \
  $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h $(srcdir)/config/tc-or32.h \
  dwarf2dbg.h $(INCDIR)/opcode/or32.h $(INCDIR)/elf/or32.h \
  $(INCDIR)/elf/reloc-macros.h
DEPTC_pdp11_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-pdp11.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/pdp11.h
DEPTC_pdp11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pdp11.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/opcode/pdp11.h
DEPTC_pj_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pj.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h $(INCDIR)/opcode/pj.h
DEPTC_ppc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-ppc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/rs6000.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h subsegs.h \
  $(INCDIR)/obstack.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(INCDIR)/opcode/ppc.h
DEPTC_ppc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ppc.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h $(INCDIR)/opcode/ppc.h \
  $(INCDIR)/elf/ppc.h $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h
DEPTC_s390_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-s390.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  struc-symbol.h dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(INCDIR)/opcode/s390.h $(INCDIR)/elf/s390.h $(INCDIR)/elf/reloc-macros.h
DEPTC_score_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-score.h dwarf2dbg.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/score-inst.h $(INCDIR)/opcode/score-datadep.h \
  struc-symbol.h $(INCDIR)/elf/score.h $(INCDIR)/elf/reloc-macros.h \
  dwarf2dbg.h
DEPTC_sh_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sh.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/sh-opc.h $(INCDIR)/safe-ctype.h \
  struc-symbol.h dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h
DEPTC_sh_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh.h subsegs.h \
  $(INCDIR)/obstack.h $(srcdir)/../opcodes/sh-opc.h $(INCDIR)/safe-ctype.h \
  struc-symbol.h $(INCDIR)/elf/sh.h $(INCDIR)/elf/reloc-macros.h \
  dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h
DEPTC_sh64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh64.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/elf/sh.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/elf32-sh64.h \
  $(INCDIR)/safe-ctype.h $(srcdir)/../opcodes/sh64-opc.h \
  $(srcdir)/config/tc-sh.c subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/sh-opc.h $(INCDIR)/safe-ctype.h \
  struc-symbol.h dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h
DEPTC_sparc_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-sparc.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/sparc.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h
DEPTC_sparc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sparc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sparc.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/sparc.h \
  dw2gencfi.h $(INCDIR)/elf/dwarf2.h
DEPTC_sparc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sparc.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/opcode/sparc.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  $(INCDIR)/elf/sparc.h $(INCDIR)/elf/reloc-macros.h \
  dwarf2dbg.h
DEPTC_spu_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-spu.h $(INCDIR)/opcode/spu.h \
  $(INCDIR)/opcode/spu-insns.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h dwarf2dbg.h $(INCDIR)/opcode/spu-insns.h
DEPTC_tic30_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-tic30.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/tic30.h
DEPTC_tic30_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic30.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic30.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/tic30.h
DEPTC_tic4x_coff = $(INCDIR)/safe-ctype.h $(srcdir)/config/obj-coff.h \
  $(srcdir)/config/tc-tic4x.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/tic4x.h $(INCDIR)/coff/ti.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/opcode/tic4x.h subsegs.h \
  $(INCDIR)/obstack.h $(INCDIR)/obstack.h
DEPTC_tic54x_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic54x.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic54x.h $(INCDIR)/coff/ti.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  sb.h macro.h subsegs.h $(INCDIR)/obstack.h struc-symbol.h \
  $(INCDIR)/opcode/tic54x.h $(srcdir)/config/obj-coff.h
DEPTC_v850_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-v850.h $(INCDIR)/elf/v850.h \
  $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/opcode/v850.h \
  dwarf2dbg.h
DEPTC_vax_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-vax.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(srcdir)/config/vax-inst.h \
  $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/opcode/vax.h
DEPTC_vax_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-vax.h dwarf2dbg.h \
  $(srcdir)/config/vax-inst.h $(INCDIR)/obstack.h subsegs.h \
  $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h $(INCDIR)/elf/vax.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/opcode/vax.h
DEPTC_xc16x_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xc16x.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/../opcodes/xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/opcode/cgen-bitset.h \
  $(srcdir)/../opcodes/xc16x-opc.h cgen.h dwarf2dbg.h \
  $(INCDIR)/elf/xc16x.h $(INCDIR)/elf/reloc-macros.h
DEPTC_xstormy16_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xstormy16.h \
  dwarf2dbg.h subsegs.h $(INCDIR)/obstack.h $(srcdir)/../opcodes/xstormy16-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/opcode/cgen-bitset.h $(srcdir)/../opcodes/xstormy16-opc.h \
  cgen.h
DEPTC_xtensa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xtensa.h $(INCDIR)/xtensa-isa.h \
  $(INCDIR)/xtensa-config.h sb.h $(INCDIR)/safe-ctype.h \
  $(srcdir)/config/tc-xtensa.h subsegs.h $(INCDIR)/obstack.h \
  $(srcdir)/config/xtensa-relax.h $(srcdir)/config/xtensa-istack.h \
  dwarf2dbg.h struc-symbol.h $(INCDIR)/xtensa-config.h
DEPTC_z80_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z80.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z80.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h
DEPTC_z8k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z8k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z8k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/safe-ctype.h \
  $(srcdir)/../opcodes/z8k-opc.h
DEPTC_hppa_som = $(srcdir)/config/tc-hppa.h subsegs.h \
  $(INCDIR)/obstack.h $(BFDDIR)/libhppa.h $(INCDIR)/opcode/hppa.h \
  $(BFDDIR)/som.h
DEPTC_i386_multi = $(DEPTC_i386_aout) $(DEPTC_i386_coff) \
  $(DEPTC_i386_elf)
DEPTC_mips_multi = $(DEPTC_mips_coff) $(DEPTC_mips_ecoff) \
  $(DEPTC_mips_elf)
DEPTC_cris_multi = $(DEPTC_cris_aout) $(DEPTC_cris_elf)
DEPOBJ_alpha_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-alpha.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h \
  $(BFDDIR)/libecoff.h
DEPOBJ_alpha_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-alpha.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  $(INCDIR)/elf/alpha.h $(INCDIR)/elf/reloc-macros.h \
  $(INCDIR)/aout/aout64.h
DEPOBJ_alpha_evax = $(srcdir)/config/obj-evax.h $(srcdir)/config/tc-alpha.h
DEPOBJ_arc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arc.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_arm_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-arm.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_arm_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-arm.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/arm.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_arm_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arm.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_avr_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-avr.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_bfin_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-bfin.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_cris_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-cris.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_cris_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-cris.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_crx_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-crx.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_d10v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d10v.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_d30v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d30v.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_dlx_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-dlx.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_fr30_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-fr30.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_frv_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-frv.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_h8300_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-h8300.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/h8300.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_h8300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-h8300.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_hppa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-hppa.h $(BFDDIR)/elf32-hppa.h \
  $(BFDDIR)/elf-bfd.h $(BFDDIR)/libhppa.h $(INCDIR)/elf/hppa.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_i370_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i370.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/elf/i370.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/aout/aout64.h
DEPOBJ_i386_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_i386_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/x86_64.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_i386_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i386.h $(srcdir)/../opcodes/i386-opc.h \
  $(INCDIR)/opcode/i386.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/elf/x86-64.h $(INCDIR)/elf/reloc-macros.h \
  $(INCDIR)/aout/aout64.h
DEPOBJ_i860_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i860.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_i960_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i960.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/i960.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h
DEPOBJ_i960_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i960.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_ia64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ia64.h $(INCDIR)/opcode/ia64.h \
  $(INCDIR)/elf/ia64.h $(INCDIR)/elf/reloc-macros.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_ip2k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ip2k.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_m32c_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32c.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_m32r_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32r.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_m68hc11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68hc11.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_m68k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-m68k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_m68k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-m68k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/m68k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_m68k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68k.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_maxq_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-maxq.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/maxq.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_mcore_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mcore.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mcore.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_mcore_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mcore.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_mep_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mep.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/elf/mep.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/aout/aout64.h
DEPOBJ_mips_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mips.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mipspe.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_mips_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-mips.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h \
  $(BFDDIR)/libecoff.h
DEPOBJ_mips_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mips.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h \
  $(INCDIR)/elf/mips.h $(INCDIR)/elf/reloc-macros.h $(INCDIR)/aout/aout64.h
DEPOBJ_mmix_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mmix.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_mn10200_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10200.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_mn10300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10300.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_msp430_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-msp430.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_mt_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mt.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_ns32k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-ns32k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_openrisc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-openrisc.h \
  dwarf2dbg.h $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_or32_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-or32.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/or32.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h
DEPOBJ_or32_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-or32.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_pdp11_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-pdp11.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_pdp11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pdp11.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_pj_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pj.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_ppc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-ppc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/rs6000.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h subsegs.h $(INCDIR)/obstack.h
DEPOBJ_ppc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ppc.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/elf/ppc.h \
  $(INCDIR)/elf/reloc-macros.h $(INCDIR)/aout/aout64.h
DEPOBJ_s390_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-s390.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_score_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-score.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_sh_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sh.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_sh_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_sh64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh64.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/elf/sh.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/elf32-sh64.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_sparc_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-sparc.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_sparc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sparc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sparc.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_sparc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sparc.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_spu_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-spu.h $(INCDIR)/opcode/spu.h \
  $(INCDIR)/opcode/spu-insns.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_tic30_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-tic30.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_tic30_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic30.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic30.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_tic4x_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic4x.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic4x.h $(INCDIR)/coff/ti.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_tic54x_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic54x.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic54x.h $(INCDIR)/coff/ti.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_v850_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-v850.h $(INCDIR)/elf/v850.h \
  $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h $(INCDIR)/safe-ctype.h \
  subsegs.h $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_vax_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-vax.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h $(INCDIR)/aout/aout64.h \
  $(INCDIR)/obstack.h
DEPOBJ_vax_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-vax.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_xc16x_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xc16x.h dwarf2dbg.h \
  $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_xstormy16_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xstormy16.h \
  dwarf2dbg.h $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/obstack.h struc-symbol.h dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_xtensa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xtensa.h $(INCDIR)/xtensa-isa.h \
  $(INCDIR)/xtensa-config.h $(INCDIR)/safe-ctype.h subsegs.h \
  $(INCDIR)/obstack.h $(INCDIR)/obstack.h struc-symbol.h \
  dwarf2dbg.h $(INCDIR)/aout/aout64.h
DEPOBJ_z80_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z80.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z80.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_z8k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z8k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z8k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(INCDIR)/obstack.h \
  subsegs.h $(INCDIR)/obstack.h
DEPOBJ_hppa_som = $(srcdir)/config/obj-som.h subsegs.h \
  $(INCDIR)/obstack.h $(BFDDIR)/libhppa.h $(BFDDIR)/som.h \
  $(INCDIR)/aout/stab_gnu.h $(INCDIR)/aout/stab.def
DEPOBJ_i386_multi = $(DEPOBJ_i386_aout) $(DEPOBJ_i386_coff) \
  $(DEPOBJ_i386_elf)
DEPOBJ_mips_multi = $(DEPOBJ_mips_coff) $(DEPOBJ_mips_ecoff) \
  $(DEPOBJ_mips_elf)
DEPOBJ_cris_multi = $(DEPOBJ_cris_aout) $(DEPOBJ_cris_elf)
DEP_alpha_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-alpha.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h
DEP_alpha_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-alpha.h
DEP_alpha_evax = $(srcdir)/config/obj-evax.h $(srcdir)/config/tc-alpha.h
DEP_arc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arc.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_arm_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-arm.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_arm_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-arm.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/arm.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_arm_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-arm.h
DEP_avr_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-avr.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_bfin_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-bfin.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_cr16_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-cr16.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_cris_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-cris.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_cris_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-cris.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_crx_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-crx.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_d10v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d10v.h
DEP_d30v_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-d30v.h
DEP_dlx_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-dlx.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_fr30_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-fr30.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_frv_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-frv.h
DEP_h8300_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-h8300.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/h8300.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_h8300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-h8300.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/h8300.h $(INCDIR)/coff/external.h $(BFDDIR)/libcoff.h
DEP_hppa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-hppa.h $(BFDDIR)/elf32-hppa.h \
  $(BFDDIR)/elf-bfd.h $(BFDDIR)/libhppa.h $(INCDIR)/elf/hppa.h \
  $(INCDIR)/elf/reloc-macros.h
DEP_i370_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i370.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_i386_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_i386_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i386.h \
  $(srcdir)/../opcodes/i386-opc.h $(INCDIR)/opcode/i386.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/x86_64.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_i386_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i386.h $(srcdir)/../opcodes/i386-opc.h \
  $(INCDIR)/opcode/i386.h dwarf2dbg.h $(srcdir)/config/obj-coff.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/x86_64.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h
DEP_i860_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i860.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_i960_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-i960.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/i960.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h
DEP_i960_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-i960.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/i960.h $(BFDDIR)/libcoff.h
DEP_ia64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ia64.h $(INCDIR)/opcode/ia64.h \
  $(INCDIR)/elf/ia64.h $(INCDIR)/elf/reloc-macros.h
DEP_ip2k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ip2k.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_m32c_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32c.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_m32r_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m32r.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_m68hc11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68hc11.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/m68k.h $(INCDIR)/coff/external.h $(BFDDIR)/libcoff.h
DEP_m68k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-m68k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_m68k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-m68k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/m68k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_m68k_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-m68k.h
DEP_maxq_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-maxq.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/maxq.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_mcore_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mcore.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mcore.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_mcore_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mcore.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/mcore.h $(INCDIR)/coff/external.h $(BFDDIR)/libcoff.h
DEP_mep_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mep.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_mips_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-mips.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/mipspe.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_mips_ecoff = $(srcdir)/config/obj-ecoff.h $(srcdir)/config/tc-mips.h \
  ecoff.h $(INCDIR)/coff/sym.h $(INCDIR)/coff/ecoff.h
DEP_mips_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mips.h
DEP_mmix_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mmix.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_mn10200_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10200.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_mn10300_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mn10300.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_msp430_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-msp430.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_mt_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-mt.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_ns32k_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-ns32k.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_openrisc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-openrisc.h \
  dwarf2dbg.h $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_or32_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-or32.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/or32.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h
DEP_or32_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-or32.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/or32.h $(BFDDIR)/libcoff.h
DEP_pdp11_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-pdp11.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_pdp11_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pdp11.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_pj_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-pj.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_ppc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-ppc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/rs6000.h $(BFDDIR)/libcoff.h \
  $(INCDIR)/bfdlink.h
DEP_ppc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-ppc.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/rs6000.h $(BFDDIR)/libcoff.h
DEP_s390_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-s390.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_score_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-score.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_sh_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sh.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_sh_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh.h
DEP_sh64_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sh64.h $(srcdir)/config/tc-sh.h \
  $(INCDIR)/elf/sh.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/elf32-sh64.h
DEP_sparc_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-sparc.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_sparc_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-sparc.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/sparc.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_sparc_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-sparc.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(INCDIR)/coff/sparc.h $(INCDIR)/coff/external.h $(BFDDIR)/libcoff.h
DEP_spu_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-spu.h $(INCDIR)/opcode/spu.h \
  $(INCDIR)/opcode/spu-insns.h dwarf2dbg.h $(srcdir)/config/obj-coff.h \
  $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h
DEP_tic30_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-tic30.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_tic30_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic30.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic30.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_tic4x_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic4x.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic4x.h $(INCDIR)/coff/ti.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_tic54x_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-tic54x.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/tic54x.h $(INCDIR)/coff/ti.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_v850_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-v850.h $(INCDIR)/elf/v850.h \
  $(INCDIR)/elf/reloc-macros.h dwarf2dbg.h $(srcdir)/config/obj-coff.h \
  $(INCDIR)/coff/internal.h $(BFDDIR)/libcoff.h
DEP_vax_aout = $(srcdir)/config/obj-aout.h $(srcdir)/config/tc-vax.h \
  $(BFDDIR)/libaout.h $(INCDIR)/bfdlink.h
DEP_vax_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-vax.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_xc16x_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xc16x.h dwarf2dbg.h \
  $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_xstormy16_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xstormy16.h \
  dwarf2dbg.h $(srcdir)/config/obj-coff.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h
DEP_xtensa_elf = $(srcdir)/config/obj-elf.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(srcdir)/config/tc-xtensa.h $(INCDIR)/xtensa-isa.h \
  $(INCDIR)/xtensa-config.h
DEP_z80_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z80.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z80.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_z8k_coff = $(srcdir)/config/obj-coff.h $(srcdir)/config/tc-z8k.h \
  $(INCDIR)/coff/internal.h $(INCDIR)/coff/z8k.h $(INCDIR)/coff/external.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h
DEP_hppa_som = $(BFDDIR)/som.h
DEP_i386_multi = $(DEP_i386_aout) $(DEP_i386_coff) \
  $(DEP_i386_elf)
DEP_mips_multi = $(DEP_mips_coff) $(DEP_mips_ecoff) \
  $(DEP_mips_elf)
DEP_cris_multi = $(DEP_cris_aout) $(DEP_cris_elf)
BMKDEP = #DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING ABOVE.
#MKDEP    DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING BELOW.
app.o: app.c
as.o: as.c subsegs.h $(INCDIR)/obstack.h output-file.h \
  sb.h macro.h dwarf2dbg.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  ../bfd/bfdver.h
atof-generic.o: atof-generic.c $(INCDIR)/safe-ctype.h
cond.o: cond.c sb.h macro.h $(INCDIR)/obstack.h
depend.o: depend.c
dwarf2dbg.o: dwarf2dbg.c $(INCDIR)/safe-ctype.h dwarf2dbg.h \
  $(INCDIR)/filenames.h subsegs.h $(INCDIR)/obstack.h \
  $(INCDIR)/elf/dwarf2.h
dw2gencfi.o: dw2gencfi.c dw2gencfi.h $(INCDIR)/elf/dwarf2.h \
  subsegs.h $(INCDIR)/obstack.h
ecoff.o: ecoff.c ecoff.h
ehopt.o: ehopt.c subsegs.h $(INCDIR)/obstack.h $(INCDIR)/elf/dwarf2.h
expr.o: expr.c $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h
flonum-copy.o: flonum-copy.c
flonum-konst.o: flonum-konst.c
flonum-mult.o: flonum-mult.c
frags.o: frags.c subsegs.h $(INCDIR)/obstack.h
hash.o: hash.c $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h
input-file.o: input-file.c input-file.h $(INCDIR)/safe-ctype.h
input-scrub.o: input-scrub.c input-file.h sb.h
listing.o: listing.c $(INCDIR)/obstack.h $(INCDIR)/safe-ctype.h \
  input-file.h subsegs.h
literal.o: literal.c subsegs.h $(INCDIR)/obstack.h
macro.o: macro.c $(INCDIR)/safe-ctype.h sb.h macro.h
messages.o: messages.c
output-file.o: output-file.c output-file.h
read.o: read.c $(INCDIR)/safe-ctype.h subsegs.h $(INCDIR)/obstack.h \
  sb.h macro.h ecoff.h dw2gencfi.h $(INCDIR)/elf/dwarf2.h
sb.o: sb.c sb.h
stabs.o: stabs.c $(INCDIR)/obstack.h subsegs.h ecoff.h \
  $(INCDIR)/aout/stab_gnu.h $(INCDIR)/aout/stab.def
subsegs.o: subsegs.c subsegs.h $(INCDIR)/obstack.h
symbols.o: symbols.c $(INCDIR)/safe-ctype.h $(INCDIR)/obstack.h \
  subsegs.h struc-symbol.h
write.o: write.c subsegs.h $(INCDIR)/obstack.h output-file.h \
  dwarf2dbg.h $(BFDDIR)/libbfd.h $(INCDIR)/hashtab.h
itbl-ops.o: itbl-ops.c itbl-ops.h
e-crisaout.o: $(srcdir)/config/e-crisaout.c emul-target.h
e-criself.o: $(srcdir)/config/e-criself.c emul-target.h
e-i386aout.o: $(srcdir)/config/e-i386aout.c emul-target.h
e-i386coff.o: $(srcdir)/config/e-i386coff.c emul-target.h
e-i386elf.o: $(srcdir)/config/e-i386elf.c emul-target.h
e-mipsecoff.o: $(srcdir)/config/e-mipsecoff.c emul-target.h
e-mipself.o: $(srcdir)/config/e-mipself.c emul-target.h
$(OBJS): $(DEP_@target_cpu_type@_@obj_format@)
$(TARG_CPU_O): $(DEPTC_@target_cpu_type@_@obj_format@)
$(OBJ_FORMAT_O): $(DEPOBJ_@target_cpu_type@_@obj_format@)
#MKDEP    DO NOT PUT ANYTHING BETWEEN THIS LINE AND THE MATCHING WARNING ABOVE.
