## Process this file with automake to generate Makefile.in

AUTOMAKE_OPTIONS = 1.9 cygnus
ACLOCAL_AMFLAGS = -I .. -I ../config -I ../bfd

SUBDIRS = po

INCDIR = $(srcdir)/../include
BFDDIR = $(srcdir)/../bfd
MKDEP = gcc -MM

WARN_CFLAGS = @WARN_CFLAGS@
NO_WERROR = @NO_WERROR@
AM_CFLAGS = $(WARN_CFLAGS)

bfdlibdir = @bfdlibdir@
bfdincludedir = @bfdincludedir@

.PHONY: install-html
install-html:

bfdlib_LTLIBRARIES = libopcodes.la

# This is where bfd.h lives.
BFD_H = ../bfd/bfd.h

# This is where libiberty lives.
LIBIBERTY = ../libiberty/libiberty.a

# Header files.
HFILES = \
	cgen-ops.h cgen-types.h \
	fr30-desc.h fr30-opc.h \
	frv-desc.h frv-opc.h \
	h8500-opc.h \
	i386-opc.h \
	i386-tbl.h \
	ia64-asmtab.h \
	ia64-opc.h \
	ip2k-desc.h ip2k-opc.h \
	iq2000-desc.h iq2000-opc.h \
	m32c-desc.h m32c-opc.h \
	m32r-desc.h m32r-opc.h \
	mcore-opc.h \
	mep-desc.h mep-opc.h \
	mt-desc.h mt-opc.h \
	openrisc-desc.h openrisc-opc.h \
	score-opc.h \
	sh-opc.h \
	sh64-opc.h \
	sysdep.h \
	w65-opc.h \
	xc16x-desc.h xc16x-opc.h \
	xstormy16-desc.h xstormy16-opc.h \
	z8k-opc.h

# C source files that correspond to .o's.
CFILES = \
	alpha-dis.c \
	alpha-opc.c \
	arc-dis.c \
	arc-opc.c \
	arc-ext.c \
	arm-dis.c \
	avr-dis.c \
	bfin-dis.c \
	cgen-asm.c \
	cgen-bitset.c \
	cgen-dis.c \
	cgen-opc.c \
	cr16-dis.c \
	cr16-opc.c \
	cris-dis.c \
	cris-opc.c \
	crx-dis.c \
	crx-opc.c \
	d10v-dis.c \
	d10v-opc.c \
	d30v-dis.c \
	d30v-opc.c \
	dlx-dis.c \
	dis-buf.c \
	dis-init.c \
	disassemble.c \
	fr30-asm.c \
	fr30-desc.c \
	fr30-dis.c \
	fr30-ibld.c \
	fr30-opc.c \
	frv-asm.c \
	frv-desc.c \
	frv-dis.c \
	frv-ibld.c \
	frv-opc.c \
	h8300-dis.c \
	h8500-dis.c \
	hppa-dis.c \
	i370-dis.c \
	i370-opc.c \
	i386-dis.c \
	i386-opc.c \
	i386-gen.c \
	i860-dis.c \
	i960-dis.c \
	ia64-dis.c \
	ia64-opc-a.c \
	ia64-opc-b.c \
	ia64-opc-f.c \
	ia64-opc-i.c \
	ia64-opc-m.c \
	ia64-opc-d.c \
	ia64-opc.c \
	ia64-gen.c \
	ia64-asmtab.c \
	ip2k-asm.c \
	ip2k-desc.c \
	ip2k-dis.c \
	ip2k-ibld.c \
	ip2k-opc.c \
	iq2000-asm.c \
	iq2000-desc.c \
	iq2000-dis.c \
	iq2000-ibld.c \
	iq2000-opc.c \
	m32c-asm.c \
	m32c-desc.c \
	m32c-dis.c \
	m32c-ibld.c \
	m32c-opc.c \
	m32r-asm.c \
	m32r-desc.c \
	m32r-dis.c \
	m32r-ibld.c \
	m32r-opc.c \
	m32r-opinst.c \
	m68hc11-dis.c \
	m68hc11-opc.c \
	m68k-dis.c \
	m68k-opc.c \
	m88k-dis.c \
	maxq-dis.c \
	mcore-dis.c \
	mep-asm.c \
	mep-desc.c \
	mep-dis.c \
	mep-ibld.c \
	mep-opc.c \
	mips-dis.c \
	mips-opc.c \
	mips16-opc.c \
	m10200-dis.c \
	m10200-opc.c \
	m10300-dis.c \
	m10300-opc.c \
	mmix-dis.c \
	mmix-opc.c \
	mt-asm.c \
	mt-desc.c \
	mt-dis.c \
	mt-ibld.c \
	mt-opc.c \
	ns32k-dis.c \
	openrisc-asm.c \
	openrisc-desc.c \
	openrisc-dis.c \
	openrisc-ibld.c \
	openrisc-opc.c \
	or32-dis.c \
	or32-opc.c \
	pdp11-dis.c \
	pdp11-opc.c \
	pj-dis.c \
	pj-opc.c \
	ppc-dis.c \
	ppc-opc.c \
	s390-mkopc.c \
	s390-opc.c \
	s390-dis.c \
	score-dis.c \
	sh-dis.c \
	sh64-dis.c \
	sh64-opc.c \
	sparc-dis.c \
	sparc-opc.c \
	spu-dis.c \
	spu-opc.c \
	tic30-dis.c \
	tic4x-dis.c \
	tic54x-dis.c \
	tic54x-opc.c \
	tic80-dis.c \
	tic80-opc.c \
	v850-dis.c \
	v850-opc.c \
	vax-dis.c \
	w65-dis.c \
	xc16x-asm.c \
	xc16x-desc.c \
	xc16x-dis.c \
	xc16x-ibld.c \
	xc16x-opc.c \
	xstormy16-asm.c \
	xstormy16-desc.c \
	xstormy16-dis.c \
	xstormy16-ibld.c \
	xstormy16-opc.c \
	xtensa-dis.c \
	z80-dis.c \
	z8k-dis.c \
	z8kgen.c

ALL_MACHINES = \
	alpha-dis.lo \
	alpha-opc.lo \
	arc-dis.lo \
	arc-opc.lo \
	arc-ext.lo \
	arm-dis.lo \
	avr-dis.lo \
	bfin-dis.lo \
	cgen-asm.lo \
	cgen-bitset.lo \
	cgen-dis.lo \
	cgen-opc.lo \
	cr16-dis.lo \
	cr16-opc.lo \
	cris-dis.lo \
	cris-opc.lo \
	crx-dis.lo \
	crx-opc.lo \
	d10v-dis.lo \
	d10v-opc.lo \
	d30v-dis.lo \
	d30v-opc.lo \
	dlx-dis.lo \
	fr30-asm.lo \
	fr30-desc.lo \
	fr30-dis.lo \
	fr30-ibld.lo \
	fr30-opc.lo \
	frv-asm.lo \
	frv-desc.lo \
	frv-dis.lo \
	frv-ibld.lo \
	frv-opc.lo \
	h8300-dis.lo \
	h8500-dis.lo \
	hppa-dis.lo \
	i386-dis.lo \
	i386-opc.lo \
	i370-dis.lo \
	i370-opc.lo \
	i860-dis.lo \
	i960-dis.lo \
	ia64-dis.lo \
	ia64-opc.lo \
	ip2k-asm.lo \
	ip2k-desc.lo \
	ip2k-dis.lo \
	ip2k-ibld.lo \
	ip2k-opc.lo \
	iq2000-asm.lo \
	iq2000-desc.lo \
	iq2000-dis.lo \
	iq2000-ibld.lo \
	iq2000-opc.lo \
	m32c-asm.lo \
	m32c-desc.lo \
	m32c-dis.lo \
	m32c-ibld.lo \
	m32c-opc.lo \
	m32r-asm.lo \
	m32r-desc.lo \
	m32r-dis.lo \
	m32r-ibld.lo \
	m32r-opc.lo \
	m32r-opinst.lo \
	m68hc11-dis.lo \
	m68hc11-opc.lo \
	m68k-dis.lo \
	m68k-opc.lo \
	m88k-dis.lo \
	m10200-dis.lo \
	m10200-opc.lo \
	m10300-dis.lo \
	m10300-opc.lo \
	maxq-dis.lo \
	mcore-dis.lo \
	mep-asm.lo \
	mep-desc.lo \
	mep-dis.lo \
	mep-ibld.lo \
	mep-opc.lo \
	mips-dis.lo \
	mips-opc.lo \
	mips16-opc.lo \
	mmix-dis.lo \
	mmix-opc.lo \
	msp430-dis.lo \
	mt-asm.lo \
	mt-desc.lo \
	mt-dis.lo \
	mt-ibld.lo \
	mt-opc.lo \
	ns32k-dis.lo \
	openrisc-asm.lo \
	openrisc-desc.lo \
	openrisc-dis.lo \
	openrisc-ibld.lo \
	openrisc-opc.lo \
	or32-dis.lo \
	or32-opc.lo \
	pdp11-dis.lo \
	pdp11-opc.lo \
	pj-dis.lo \
	pj-opc.lo \
	ppc-dis.lo \
	ppc-opc.lo \
	s390-dis.lo \
	s390-opc.lo \
	score-dis.lo \
	sh-dis.lo \
	sh64-dis.lo \
	sh64-opc.lo \
	sparc-dis.lo \
	sparc-opc.lo \
	spu-dis.lo \
	spu-opc.lo \
	tic30-dis.lo \
	tic4x-dis.lo \
	tic54x-dis.lo \
	tic54x-opc.lo \
	tic80-dis.lo \
	tic80-opc.lo \
	v850-dis.lo \
	v850-opc.lo \
	vax-dis.lo \
	w65-dis.lo \
	xc16x-asm.lo \
	xc16x-desc.lo \
	xc16x-dis.lo \
	xc16x-ibld.lo \
	xc16x-opc.lo \
	xstormy16-asm.lo \
	xstormy16-desc.lo \
	xstormy16-dis.lo \
	xstormy16-ibld.lo \
	xstormy16-opc.lo \
	xtensa-dis.lo \
	z80-dis.lo \
	z8k-dis.lo

OFILES = @BFD_MACHINES@

# We should reconfigure whenever bfd/configure.in changes, because
# that's where the version number in Makefile comes from.
CONFIG_STATUS_DEPENDENCIES = $(BFDDIR)/configure.in

INCLUDES = -D_GNU_SOURCE -I. -I$(srcdir) -I../bfd -I$(INCDIR) -I$(BFDDIR) @HDEFINES@ @INCINTL@

disassemble.lo: disassemble.c $(INCDIR)/dis-asm.h
	$(LIBTOOL) --mode=compile $(COMPILE) -c @archdefs@ $(srcdir)/disassemble.c

libopcodes_la_SOURCES =  dis-buf.c disassemble.c dis-init.c
# It's desirable to list ../bfd/libbfd.la in DEPENDENCIES and LIBADD.
# Unfortunately this causes libtool to add -L$(libdir), referring to the
# planned install directory of libbfd.  This can cause us to pick up an
# old version of libbfd, or to pick up libbfd for the wrong architecture
# if host != build.
libopcodes_la_DEPENDENCIES = $(OFILES)
libopcodes_la_LIBADD = $(OFILES) @WIN32LIBADD@
libopcodes_la_LDFLAGS = -release `cat ../bfd/libtool-soversion` @WIN32LDFLAGS@

# libtool will build .libs/libopcodes.a.  We create libopcodes.a in
# the build directory so that we don't have to convert all the
# programs that use libopcodes.a simultaneously.  This is a hack which
# should be removed if everything else starts using libtool.  FIXME.

noinst_LIBRARIES = libopcodes.a

stamp-lib: libopcodes.la
	libtooldir=`$(LIBTOOL) --config | sed -n -e 's/^objdir=//p'`; \
	if [ -f $$libtooldir/libopcodes.a ]; then \
	  cp $$libtooldir/libopcodes.a libopcodes.tmp; \
	  $(RANLIB) libopcodes.tmp; \
	  $(SHELL) $(srcdir)/../move-if-change libopcodes.tmp libopcodes.a; \
	else true; fi
	touch stamp-lib

libopcodes.a: stamp-lib ; @true

POTFILES = $(HFILES) $(CFILES)
po/POTFILES.in: @MAINT@ Makefile
	for f in $(POTFILES); do echo $$f; done | LC_COLLATE= sort > tmp \
	  && mv tmp $(srcdir)/po/POTFILES.in

install-bfdlibLTLIBRARIES: @INSTALL_LIBBFD_TRUE@install_libopcodes
	@$(NORMAL_INSTALL)

uninstall-bfdlibLTLIBRARIES: @INSTALL_LIBBFD_TRUE@uninstall_libopcodes
	@$(NORMAL_UNINSTALL)

.PHONY: install_libopcodes uninstall_libopcodes
install_libopcodes: $(bfdlib_LTLIBRARIES)
	$(mkinstalldirs) $(DESTDIR)$(bfdlibdir)
	$(mkinstalldirs) $(DESTDIR)$(bfdincludedir)
	@list='$(bfdlib_LTLIBRARIES)'; for p in $$list; do \
	  if test -f $$p; then \
	    echo "$(LIBTOOL)  --mode=install $(INSTALL) $$p $(DESTDIR)$(bfdlibdir)/$$p"; \
	    $(LIBTOOL)  --mode=install $(INSTALL) $$p $(DESTDIR)$(bfdlibdir)/$$p; \
	  else :; fi; \
	done
	$(INSTALL_DATA) $(INCDIR)/dis-asm.h $(DESTDIR)$(bfdincludedir)/dis-asm.h

uninstall_libopcodes:
	list='$(bfdlib_LTLIBRARIES)'; for p in $$list; do \
	  $(LIBTOOL)  --mode=uninstall rm -f $(DESTDIR)$(bfdlibdir)/$$p; \
	done
	rm -f $(DESTDIR)$(bfdincludedir)/dis-asm.h

CLEANFILES = \
	stamp-ip2k stamp-m32c stamp-m32r stamp-fr30 stamp-frv \
	stamp-openrisc stamp-iq2000 stamp-mep stamp-mt stamp-xstormy16 stamp-xc16x\
	libopcodes.a stamp-lib dep.sed DEP DEPA DEP1 DEP2


CGENDIR = @cgendir@
CPUDIR = $(CGENDIR)/cpu
CGEN = "`if test -f ../guile/libguile/guile ; then echo ../guile/libguile/guile; else echo guile ; fi` -l ${cgendir}/guile.scm -s"
CGENFLAGS = -v

CGENDEPS = \
	$(CGENDIR)/desc.scm $(CGENDIR)/desc-cpu.scm \
	$(CGENDIR)/opcodes.scm $(CGENDIR)/opc-asmdis.scm \
	$(CGENDIR)/opc-ibld.scm $(CGENDIR)/opc-itab.scm \
	$(CGENDIR)/opc-opinst.scm \
	cgen-asm.in cgen-dis.in cgen-ibld.in

CGEN_CPUS = fr30 frv ip2k m32c m32r mep mt openrisc xc16x xstormy16

if CGEN_MAINT
IP2K_DEPS = stamp-ip2k
M32C_DEPS = stamp-m32c
M32R_DEPS = stamp-m32r
FR30_DEPS = stamp-fr30
FRV_DEPS = stamp-frv
MEP_DEPS = stamp-mep
MT_DEPS = stamp-mt
OPENRISC_DEPS = stamp-openrisc
IQ2000_DEPS = stamp-iq2000
XC16X_DEPS = stamp-xc16x
XSTORMY16_DEPS = stamp-xstormy16
else
IP2K_DEPS =
M32C_DEPS =
M32R_DEPS =
FR30_DEPS =
FRV_DEPS =
MEP_DEPS =
MT_DEPS =
OPENRISC_DEPS = 
IQ2000_DEPS = 
XC16X_DEPS = 
XSTORMY16_DEPS = 
endif

run-cgen:
	$(SHELL) $(srcdir)/cgen.sh opcodes $(srcdir) $(CGEN) \
	  $(CGENDIR) "$(CGENFLAGS)" $(arch) $(prefix) $(archfile) $(opcfile) \
	  "$(options)" "$(extrafiles)"
	touch stamp-${prefix}
.PHONY: run-cgen

# Maintainer utility rule to regenerate all cgen files.
run-cgen-all:
	for c in $(CGEN_CPUS) ; \
	do \
		$(MAKE) stamp-$$c || exit 1 ; \
	done
.PHONY: run-cgen-all

# For now, require developers to configure with --enable-cgen-maint.
$(srcdir)/ip2k-desc.h $(srcdir)/ip2k-desc.c $(srcdir)/ip2k-opc.h $(srcdir)/ip2k-opc.c $(srcdir)/ip2k-ibld.c $(srcdir)/ip2k-asm.c $(srcdir)/ip2k-dis.c: $(IP2K_DEPS)
	@true
stamp-ip2k: $(CGENDEPS) $(CPUDIR)/ip2k.cpu $(CPUDIR)/ip2k.opc
	$(MAKE) run-cgen arch=ip2k prefix=ip2k options= \
		archfile=$(CPUDIR)/ip2k.cpu opcfile=$(CPUDIR)/ip2k.opc extrafiles=

$(srcdir)/m32c-desc.h $(srcdir)/m32c-desc.c $(srcdir)/m32c-opc.h $(srcdir)/m32c-opc.c $(srcdir)/m32c-ibld.c $(srcdir)/m32c-asm.c $(srcdir)/m32c-dis.c: $(M32C_DEPS)
#	@true
stamp-m32c: $(CGENDEPS) $(srcdir)/../cpu/m32c.cpu $(srcdir)/../cpu/m32c.opc
	$(MAKE) run-cgen arch=m32c prefix=m32c options= \
		archfile=$(srcdir)/../cpu/m32c.cpu \
		opcfile=$(srcdir)/../cpu/m32c.opc extrafiles=
m32c_opc_h = m32c-opc.h cgen-types.h cgen-ops.h
m32c-asm.lo: m32c-asm.c sysdep.h config.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h m32c-desc.h \
  $(INCDIR)/opcode/cgen.h $(m32c_opc_h) opintl.h
m32c-desc.lo: m32c-desc.c sysdep.h config.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h m32c-desc.h \
  $(INCDIR)/opcode/cgen.h $(m32c_opc_h) opintl.h
m32c-dis.lo: m32c-dis.c sysdep.h config.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h m32c-desc.h $(INCDIR)/opcode/cgen.h \
  $(m32c_opc_h) opintl.h
m32c-ibld.lo: m32c-ibld.c sysdep.h config.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h m32c-desc.h $(INCDIR)/opcode/cgen.h \
  $(m32c_opc_h) opintl.h
m32c-opc.lo: m32c-opc.c sysdep.h config.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h m32c-desc.h \
  $(INCDIR)/opcode/cgen.h $(m32c_opc_h)

$(srcdir)/m32r-desc.h $(srcdir)/m32r-desc.c $(srcdir)/m32r-opc.h $(srcdir)/m32r-opc.c $(srcdir)/m32r-ibld.c $(srcdir)/m32r-opinst.c $(srcdir)/m32r-asm.c $(srcdir)/m32r-dis.c: $(M32R_DEPS)
	@true
stamp-m32r: $(CGENDEPS) $(srcdir)/../cpu/m32r.cpu $(srcdir)/../cpu/m32r.opc
	$(MAKE) run-cgen arch=m32r prefix=m32r options=opinst \
		archfile=$(srcdir)/../cpu/m32r.cpu \
		opcfile=$(srcdir)/../cpu/m32r.opc extrafiles=opinst

$(srcdir)/fr30-desc.h $(srcdir)/fr30-desc.c $(srcdir)/fr30-opc.h $(srcdir)/fr30-opc.c $(srcdir)/fr30-ibld.c $(srcdir)/fr30-asm.c $(srcdir)/fr30-dis.c: $(FR30_DEPS)
	@true
stamp-fr30: $(CGENDEPS) $(CPUDIR)/fr30.cpu $(CPUDIR)/fr30.opc
	$(MAKE) run-cgen arch=fr30 prefix=fr30 options= \
		archfile=$(CPUDIR)/fr30.cpu opcfile=$(CPUDIR)/fr30.opc extrafiles=

$(srcdir)/frv-desc.h $(srcdir)/frv-desc.c $(srcdir)/frv-opc.h $(srcdir)/frv-opc.c $(srcdir)/frv-ibld.c $(srcdir)/frv-asm.c $(srcdir)/frv-dis.c: $(FRV_DEPS)
	@true
stamp-frv: $(CGENDEPS) $(srcdir)/../cpu/frv.cpu $(srcdir)/../cpu/frv.opc
	$(MAKE) run-cgen arch=frv prefix=frv options= \
		archfile=$(srcdir)/../cpu/frv.cpu opcfile=$(srcdir)/../cpu/frv.opc extrafiles=

$(srcdir)/mep-desc.h $(srcdir)/mep-desc.c $(srcdir)/mep-opc.h $(srcdir)/mep-opc.c $(srcdir)/mep-ibld.c $(srcdir)/mep-asm.c $(srcdir)/mep-dis.c: $(MEP_DEPS)
	@true
stamp-mep: $(CGENDEPS) $(CPUDIR)/mep.cpu $(CPUDIR)/mep-default.cpu $(CPUDIR)/mep-core.cpu $(CPUDIR)/mep-h1.cpu $(CPUDIR)/mep-ext-cop.cpu $(CPUDIR)/mep-sample-ucidsp.cpu $(CPUDIR)/mep-rhcop.cpu $(CPUDIR)/mep-fmax.cpu $(CPUDIR)/mep.opc
	$(MAKE) run-cgen arch=mep prefix=mep options= \
		archfile=$(CPUDIR)/mep.cpu opcfile=$(CPUDIR)/mep.opc extrafiles=

$(srcdir)/mt-desc.h $(srcdir)/mt-desc.c $(srcdir)/mt-opc.h $(srcdir)/mt-opc.c $(srcdir)/mt-ibld.c $(srcdir)/mt-asm.c $(srcdir)/mt-dis.c: $(MT_DEPS)
	@true
stamp-mt: $(CGENDEPS) $(srcdir)/../cpu/mt.cpu $(srcdir)/../cpu/mt.opc
	$(MAKE) run-cgen arch=mt prefix=mt options= \
		archfile=$(srcdir)/../cpu/mt.cpu \
		opcfile=$(srcdir)/../cpu/mt.opc extrafiles=

$(srcdir)/openrisc-desc.h $(srcdir)/openrisc-desc.c $(srcdir)/openrisc-opc.h $(srcdir)/openrisc-opc.c $(srcdir)/openrisc-ibld.c $(srcdir)/openrisc-asm.c $(srcdir)/openrisc-dis.c: $(OPENRISC_DEPS)
	@true
stamp-openrisc: $(CGENDEPS) $(CPUDIR)/openrisc.cpu $(CPUDIR)/openrisc.opc
	$(MAKE) run-cgen arch=openrisc prefix=openrisc options= \
		archfile=$(CPUDIR)/openrisc.cpu opcfile=$(CPUDIR)/openrisc.opc extrafiles=

$(srcdir)/iq2000-desc.h $(srcdir)/iq2000-desc.c $(srcdir)/iq2000-opc.h $(srcdir)/iq2000-opc.c $(srcdir)/iq2000-ibld.c $(srcdir)/iq2000-asm.c $(srcdir)/iq2000-dis.c: $(IQ2000_DEPS)
	@true
stamp-iq2000: $(CGENDEPS) $(srcdir)/../cpu/iq2000.cpu \
		$(srcdir)/../cpu/iq2000.opc $(srcdir)/../cpu/iq2000m.cpu \
		$(srcdir)/../cpu/iq10.cpu
	$(MAKE) run-cgen arch=iq2000 prefix=iq2000 options= \
		archfile=$(srcdir)/../cpu/iq2000.cpu \
		opcfile=$(srcdir)/../cpu/iq2000.opc extrafiles=

$(srcdir)/xstormy16-desc.h $(srcdir)/xstormy16-desc.c $(srcdir)/xstormy16-opc.h $(srcdir)/xstormy16-opc.c $(srcdir)/xstormy16-ibld.c $(srcdir)/xstormy16-asm.c $(srcdir)/xstormy16-dis.c: $(XSTORMY16_DEPS)
	@true
stamp-xstormy16: $(CGENDEPS) $(CPUDIR)/xstormy16.cpu $(CPUDIR)/xstormy16.opc
	$(MAKE) run-cgen arch=xstormy16 prefix=xstormy16 options= \
		archfile=$(CPUDIR)/xstormy16.cpu opcfile=$(CPUDIR)/xstormy16.opc extrafiles=

$(srcdir)/xc16x-desc.h $(srcdir)/xc16x-desc.c $(srcdir)/xc16x-opc.h $(srcdir)/xc16x-opc.c $(srcdir)/xc16x-ibld.c $(srcdir)/xc16x-asm.c $(srcdir)/xc16x-dis.c: $(XC16X_DEPS)
	@true
stamp-xc16x: $(CGENDEPS) $(CPUDIR)/xc16x.cpu $(CPUDIR)/xc16x.opc
	$(MAKE) run-cgen arch=xc16x prefix=xc16x options= \
		archfile=$(CPUDIR)/xc16x.cpu opcfile=$(CPUDIR)/xc16x.opc extrafiles=

i386-gen: i386-gen.o
	$(LINK) i386-gen.o $(LIBIBERTY)

i386-gen.o: i386-gen.c i386-opc.h

i386-tbl.h: @MAINT@ i386-gen i386-opc.tbl i386-reg.tbl
	./i386-gen --srcdir $(srcdir) > $(srcdir)/i386-tbl.h

ia64-gen: ia64-gen.o
	$(LINK) ia64-gen.o $(LIBIBERTY)

ia64-gen.o: ia64-gen.c ia64-opc.c ia64-opc-a.c ia64-opc-b.c ia64-opc-f.c \
  ia64-opc-i.c ia64-opc-m.c ia64-opc-d.c ia64-opc.h

ia64-asmtab.c: @MAINT@ ia64-gen ia64-ic.tbl ia64-raw.tbl ia64-waw.tbl ia64-war.tbl
	./ia64-gen --srcdir $(srcdir) > $(srcdir)/ia64-asmtab.c

s390-mkopc: s390-mkopc.c
	$(CC_FOR_BUILD) -o s390-mkopc $(srcdir)/s390-mkopc.c

s390-opc.tab: s390-mkopc s390-opc.txt
	./s390-mkopc < $(srcdir)/s390-opc.txt > s390-opc.tab

sh-dis.lo: sh-dis.c
	$(LIBTOOL) --mode=compile $(COMPILE) -c @archdefs@ $<

# This dependency stuff is copied from BFD.

DEP: dep.sed $(CFILES) $(HFILES) config.h
	rm -f DEP1
	$(MAKE) MKDEP="$(MKDEP)" DEP1
	sed -f dep.sed < DEP1 > DEPA
	echo '# IF YOU PUT ANYTHING HERE IT WILL GO AWAY' >> DEPA
	if grep ' /' DEPA > /dev/null 2> /dev/null; then \
	  echo 'make DEP failed!'; exit 1; \
	else \
	  mv -f DEPA $@; \
	fi

DEP1: $(CFILES)
	echo '# DO NOT DELETE THIS LINE -- mkdep uses it.' > DEP2
	echo '# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.' >> DEP2
	$(MKDEP) $(INCLUDES) $(CFLAGS) $? >> DEP2
	mv -f DEP2 $@

dep.sed: dep-in.sed config.status
	sed <$(srcdir)/dep-in.sed >dep.sed	\
		-e 's!@BFD_H@!$(BFD_H)!'	\
		-e 's!@INCDIR@!$(INCDIR)!'	\
		-e 's!@BFDDIR@!$(BFDDIR)!'	\
		-e 's!@SRCDIR@!$(srcdir)!'	\
		-e 's!@TOPDIR@!'`echo $(srcdir) | sed -e s,/opcodes$$,,`'!'

dep: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < Makefile > tmp-Makefile
	cat DEP >> tmp-Makefile
	$(srcdir)/../move-if-change tmp-Makefile Makefile

dep-in: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < $(srcdir)/Makefile.in > tmp-Makefile.in
	cat DEP >> tmp-Makefile.in
	$(srcdir)/../move-if-change tmp-Makefile.in $(srcdir)/Makefile.in

dep-am: DEP
	sed -e '/^..DO NOT DELETE THIS LINE/,$$d' < $(srcdir)/Makefile.am > tmp-Makefile.am
	cat DEP >> tmp-Makefile.am
	$(srcdir)/../move-if-change tmp-Makefile.am $(srcdir)/Makefile.am

.PHONY: dep dep-in dep-am

# What appears below is generated by a hacked mkdep using gcc -MM.

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
alpha-dis.lo: alpha-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/alpha.h
alpha-opc.lo: alpha-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/alpha.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h opintl.h
arc-dis.lo: arc-dis.c $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/arc.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h $(INCDIR)/elf/arc.h $(INCDIR)/elf/reloc-macros.h \
  opintl.h arc-dis.h arc-ext.h
arc-opc.lo: arc-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/opcode/arc.h \
  opintl.h
arc-ext.lo: arc-ext.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h arc-ext.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
arm-dis.lo: arm-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/arm.h opintl.h $(INCDIR)/safe-ctype.h \
  $(INCDIR)/floatformat.h $(INCDIR)/ansidecl.h $(INCDIR)/coff/internal.h \
  $(BFDDIR)/libcoff.h $(INCDIR)/bfdlink.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/elf/internal.h $(INCDIR)/elf/arm.h $(INCDIR)/elf/reloc-macros.h
avr-dis.lo: avr-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/avr.h
bfin-dis.lo: bfin-dis.c $(INCDIR)/opcode/bfin.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
cgen-asm.lo: cgen-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  opintl.h
cgen-bitset.lo: cgen-bitset.c $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/opcode/cgen-bitset.h
cgen-dis.lo: cgen-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h
cgen-opc.lo: cgen-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h
cr16-dis.lo: cr16-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/opcode/cr16.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h
cr16-opc.lo: cr16-opc.c $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cr16.h
cris-dis.lo: cris-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/opcode/cris.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h
cris-opc.lo: cris-opc.c $(INCDIR)/opcode/cris.h
crx-dis.lo: crx-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/opcode/crx.h
crx-opc.lo: crx-opc.c $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/crx.h
d10v-dis.lo: d10v-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/d10v.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
d10v-opc.lo: d10v-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/d10v.h
d30v-dis.lo: d30v-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/d30v.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h opintl.h
d30v-opc.lo: d30v-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/d30v.h
dlx-dis.lo: dlx-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/dlx.h
dis-buf.lo: dis-buf.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  opintl.h
dis-init.lo: dis-init.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H)
disassemble.lo: disassemble.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
fr30-asm.lo: fr30-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h fr30-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
fr30-desc.lo: fr30-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h fr30-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
fr30-dis.lo: fr30-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h fr30-opc.h \
  opintl.h
fr30-ibld.lo: fr30-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  fr30-opc.h opintl.h $(INCDIR)/safe-ctype.h
fr30-opc.lo: fr30-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  fr30-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h fr30-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
frv-asm.lo: frv-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  frv-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h frv-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
frv-desc.lo: frv-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  frv-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h frv-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
frv-dis.lo: frv-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  frv-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h frv-opc.h \
  opintl.h
frv-ibld.lo: frv-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h frv-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  frv-opc.h opintl.h $(INCDIR)/safe-ctype.h
frv-opc.lo: frv-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  frv-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h frv-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/elf/frv.h \
  $(INCDIR)/elf/reloc-macros.h
h8300-dis.lo: h8300-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/h8300.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h opintl.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h
h8500-dis.lo: h8500-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  h8500-opc.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h opintl.h
hppa-dis.lo: hppa-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFDDIR)/libhppa.h $(INCDIR)/opcode/hppa.h
i370-dis.lo: i370-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/i370.h
i370-opc.lo: i370-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/i370.h
i386-dis.lo: i386-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h sysdep.h config.h \
  $(INCDIR)/ansidecl.h opintl.h $(INCDIR)/opcode/i386.h
i386-opc.lo: i386-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h i386-opc.h \
  $(INCDIR)/opcode/i386.h i386-tbl.h
i386-gen.lo: i386-gen.c $(INCDIR)/getopt.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h i386-opc.h \
  $(INCDIR)/opcode/i386.h
i860-dis.lo: i860-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/opcode/i860.h
i960-dis.lo: i960-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-dis.lo: ia64-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H)
ia64-opc-a.lo: ia64-opc-a.c ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-opc-b.lo: ia64-opc-b.c ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-opc-f.lo: ia64-opc-f.c ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-opc-i.lo: ia64-opc-i.c ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-opc-m.lo: ia64-opc-m.c ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
ia64-opc-d.lo: ia64-opc-d.c
ia64-opc.lo: ia64-opc.c $(INCDIR)/ansidecl.h sysdep.h \
  config.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  ia64-asmtab.h $(INCDIR)/opcode/ia64.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h ia64-asmtab.c
ia64-gen.lo: ia64-gen.c $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h sysdep.h \
  config.h $(INCDIR)/getopt.h ia64-opc.h $(INCDIR)/opcode/ia64.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h ia64-opc-a.c \
  ia64-opc-i.c ia64-opc-m.c ia64-opc-b.c ia64-opc-f.c \
  ia64-opc-x.c ia64-opc-d.c
ia64-asmtab.lo: ia64-asmtab.c
ip2k-asm.lo: ip2k-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  ip2k-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h ip2k-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
ip2k-desc.lo: ip2k-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  ip2k-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h ip2k-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
ip2k-dis.lo: ip2k-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  ip2k-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h ip2k-opc.h \
  opintl.h
ip2k-ibld.lo: ip2k-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h ip2k-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  ip2k-opc.h opintl.h $(INCDIR)/safe-ctype.h
ip2k-opc.lo: ip2k-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  ip2k-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h ip2k-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
iq2000-asm.lo: iq2000-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  iq2000-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h iq2000-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
iq2000-desc.lo: iq2000-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  iq2000-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h iq2000-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
iq2000-dis.lo: iq2000-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  iq2000-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h iq2000-opc.h \
  opintl.h
iq2000-ibld.lo: iq2000-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h iq2000-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  iq2000-opc.h opintl.h $(INCDIR)/safe-ctype.h
iq2000-opc.lo: iq2000-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  iq2000-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h iq2000-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
m32c-asm.lo: m32c-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32c-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32c-opc.h \
  cgen-types.h ../bfd/bfd_stdint.h cgen-ops.h opintl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
m32c-desc.lo: m32c-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32c-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32c-opc.h \
  cgen-types.h ../bfd/bfd_stdint.h cgen-ops.h opintl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/xregex.h \
  $(INCDIR)/xregex2.h
m32c-dis.lo: m32c-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  m32c-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32c-opc.h \
  cgen-types.h ../bfd/bfd_stdint.h cgen-ops.h opintl.h \
  $(INCDIR)/elf/m32c.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/elf-bfd.h \
  $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h \
  $(INCDIR)/bfdlink.h
m32c-ibld.lo: m32c-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h m32c-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  m32c-opc.h cgen-types.h ../bfd/bfd_stdint.h cgen-ops.h \
  opintl.h $(INCDIR)/safe-ctype.h
m32c-opc.lo: m32c-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32c-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32c-opc.h \
  cgen-types.h ../bfd/bfd_stdint.h cgen-ops.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h
m32r-asm.lo: m32r-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32r-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
m32r-desc.lo: m32r-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32r-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
m32r-dis.lo: m32r-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32r-opc.h \
  opintl.h
m32r-ibld.lo: m32r-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  m32r-opc.h opintl.h $(INCDIR)/safe-ctype.h
m32r-opc.lo: m32r-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32r-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
m32r-opinst.lo: m32r-opinst.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  m32r-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h m32r-opc.h
m68hc11-dis.lo: m68hc11-dis.c $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/m68hc11.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
m68hc11-opc.lo: m68hc11-opc.c $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/m68hc11.h
m68k-dis.lo: m68k-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/floatformat.h $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h \
  opintl.h $(INCDIR)/opcode/m68k.h
m68k-opc.lo: m68k-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/m68k.h
m88k-dis.lo: m88k-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/m88k.h opintl.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h
maxq-dis.lo: maxq-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/maxq.h
mcore-dis.lo: mcore-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  mcore-opc.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h
mep-asm.lo: mep-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mep-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mep-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
mep-desc.lo: mep-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mep-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mep-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
mep-dis.lo: mep-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  mep-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mep-opc.h \
  opintl.h $(INCDIR)/elf/mep.h $(INCDIR)/elf/reloc-macros.h \
  $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h $(INCDIR)/elf/internal.h \
  $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h
mep-ibld.lo: mep-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h mep-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  mep-opc.h opintl.h $(INCDIR)/safe-ctype.h
mep-opc.lo: mep-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mep-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mep-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/elf/mep.h \
  $(INCDIR)/elf/reloc-macros.h
mips-dis.lo: mips-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/opcode/mips.h \
  opintl.h $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h \
  $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h \
  $(INCDIR)/elf/mips.h $(INCDIR)/elf/reloc-macros.h
mips-opc.lo: mips-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mips.h
mips16-opc.lo: mips16-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mips.h
m10200-dis.lo: m10200-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mn10200.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h opintl.h
m10200-opc.lo: m10200-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mn10200.h
m10300-dis.lo: m10300-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mn10300.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h opintl.h
m10300-opc.lo: m10300-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/mn10300.h
mmix-dis.lo: mmix-dis.c $(INCDIR)/opcode/mmix.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(BFD_H) opintl.h
mmix-opc.lo: mmix-opc.c $(INCDIR)/opcode/mmix.h $(INCDIR)/symcat.h
mt-asm.lo: mt-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mt-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mt-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
mt-desc.lo: mt-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mt-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mt-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
mt-dis.lo: mt-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  mt-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mt-opc.h \
  opintl.h
mt-ibld.lo: mt-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h mt-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  mt-opc.h opintl.h $(INCDIR)/safe-ctype.h
mt-opc.lo: mt-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  mt-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h mt-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
ns32k-dis.lo: ns32k-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/opcode/ns32k.h opintl.h
openrisc-asm.lo: openrisc-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  openrisc-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h openrisc-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
openrisc-desc.lo: openrisc-desc.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/symcat.h openrisc-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h openrisc-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
openrisc-dis.lo: openrisc-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  openrisc-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h openrisc-opc.h \
  opintl.h
openrisc-ibld.lo: openrisc-ibld.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(BFD_H) $(INCDIR)/symcat.h openrisc-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h openrisc-opc.h \
  opintl.h $(INCDIR)/safe-ctype.h
openrisc-opc.lo: openrisc-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  openrisc-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h openrisc-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
or32-dis.lo: or32-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/opcode/or32.h \
  $(INCDIR)/safe-ctype.h
or32-opc.lo: or32-opc.c $(INCDIR)/safe-ctype.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/or32.h
pdp11-dis.lo: pdp11-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/pdp11.h
pdp11-opc.lo: pdp11-opc.c $(INCDIR)/opcode/pdp11.h
pj-dis.lo: pj-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/pj.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
pj-opc.lo: pj-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/pj.h
ppc-dis.lo: ppc-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/ppc.h
ppc-opc.lo: ppc-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/ppc.h opintl.h
s390-mkopc.lo: s390-mkopc.c
s390-opc.lo: s390-opc.c $(INCDIR)/ansidecl.h $(INCDIR)/opcode/s390.h \
  s390-opc.tab
s390-dis.lo: s390-dis.c $(INCDIR)/ansidecl.h sysdep.h \
  config.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/s390.h
score-dis.lo: score-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  score-opc.h opintl.h $(BFD_H) $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h \
  $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h \
  $(INCDIR)/elf/internal.h $(INCDIR)/elf/score.h $(INCDIR)/elf/reloc-macros.h
sh-dis.lo: sh-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  sh-opc.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/dis-asm.h $(BFD_H)
sh64-dis.lo: sh64-dis.c $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h sysdep.h config.h \
  $(INCDIR)/ansidecl.h sh64-opc.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(BFDDIR)/elf-bfd.h $(INCDIR)/elf/common.h \
  $(INCDIR)/elf/internal.h $(INCDIR)/elf/external.h $(INCDIR)/bfdlink.h \
  $(INCDIR)/elf/sh.h $(INCDIR)/elf/reloc-macros.h $(BFDDIR)/elf32-sh64.h
sh64-opc.lo: sh64-opc.c sh64-opc.h
sparc-dis.lo: sparc-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/sparc.h $(INCDIR)/ansidecl.h $(INCDIR)/dis-asm.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h opintl.h
sparc-opc.lo: sparc-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/sparc.h $(INCDIR)/ansidecl.h
spu-dis.lo: spu-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/spu.h $(INCDIR)/opcode/spu-insns.h
spu-opc.lo: spu-opc.c $(INCDIR)/opcode/spu.h $(INCDIR)/opcode/spu-insns.h \
  $(INCDIR)/opcode/spu-insns.h
tic30-dis.lo: tic30-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/tic30.h
tic4x-dis.lo: tic4x-dis.c $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/tic4x.h
tic54x-dis.lo: tic54x-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/tic54x.h $(INCDIR)/coff/tic54x.h $(INCDIR)/coff/ti.h
tic54x-opc.lo: tic54x-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(INCDIR)/opcode/tic54x.h
tic80-dis.lo: tic80-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/tic80.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
tic80-opc.lo: tic80-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/tic80.h
v850-dis.lo: v850-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/v850.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h opintl.h
v850-opc.lo: v850-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/v850.h opintl.h
vax-dis.lo: vax-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/opcode/vax.h $(INCDIR)/dis-asm.h $(BFD_H) \
  $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
w65-dis.lo: w65-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  w65-opc.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h
xc16x-asm.lo: xc16x-asm.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xc16x-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
xc16x-desc.lo: xc16x-desc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xc16x-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
xc16x-dis.lo: xc16x-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xc16x-opc.h \
  opintl.h
xc16x-ibld.lo: xc16x-ibld.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  $(BFD_H) $(INCDIR)/symcat.h xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  xc16x-opc.h opintl.h $(INCDIR)/safe-ctype.h
xc16x-opc.lo: xc16x-opc.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h $(INCDIR)/symcat.h \
  xc16x-desc.h $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xc16x-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
xstormy16-asm.lo: xstormy16-asm.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/symcat.h xstormy16-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xstormy16-opc.h \
  opintl.h $(INCDIR)/xregex.h $(INCDIR)/xregex2.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/safe-ctype.h
xstormy16-desc.lo: xstormy16-desc.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/symcat.h xstormy16-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xstormy16-opc.h \
  opintl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/xregex.h $(INCDIR)/xregex2.h
xstormy16-dis.lo: xstormy16-dis.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(BFD_H) $(INCDIR)/symcat.h $(INCDIR)/libiberty.h \
  $(INCDIR)/ansidecl.h xstormy16-desc.h $(INCDIR)/opcode/cgen-bitset.h \
  $(INCDIR)/opcode/cgen.h $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h \
  xstormy16-opc.h opintl.h
xstormy16-ibld.lo: xstormy16-ibld.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(BFD_H) $(INCDIR)/symcat.h xstormy16-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xstormy16-opc.h \
  opintl.h $(INCDIR)/safe-ctype.h
xstormy16-opc.lo: xstormy16-opc.c sysdep.h config.h \
  $(INCDIR)/ansidecl.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h $(INCDIR)/symcat.h xstormy16-desc.h \
  $(INCDIR)/opcode/cgen-bitset.h $(INCDIR)/opcode/cgen.h \
  $(INCDIR)/symcat.h $(INCDIR)/opcode/cgen-bitset.h xstormy16-opc.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
xtensa-dis.lo: xtensa-dis.c $(INCDIR)/xtensa-isa.h \
  $(INCDIR)/ansidecl.h $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h \
  sysdep.h config.h $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h \
  $(INCDIR)/symcat.h
z80-dis.lo: z80-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h
z8k-dis.lo: z8k-dis.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/dis-asm.h $(BFD_H) $(INCDIR)/ansidecl.h $(INCDIR)/symcat.h \
  z8k-opc.h
z8kgen.lo: z8kgen.c sysdep.h config.h $(INCDIR)/ansidecl.h \
  $(INCDIR)/libiberty.h $(INCDIR)/ansidecl.h
# IF YOU PUT ANYTHING HERE IT WILL GO AWAY
