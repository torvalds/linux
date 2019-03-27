#
# Copyright (C) 2012 by Darren Reed.
#
# Redistribution and use in source and binary forms are permitted
# provided that this notice is preserved and due credit is given
# to the original author and the contributors.
#
# $FreeBSD$
# Id: Makefile,v 2.76.2.24 2007/09/26 10:04:03 darrenr Exp $
#
SHELL=/bin/sh
BINDEST=/usr/local/bin
SBINDEST=/sbin
MANDIR=/usr/local/man
#To test prototyping
CC=gcc -Wstrict-prototypes -Wmissing-prototypes -Wunused -Wuninitialized
#CC=gcc
#CC=cc -Dconst=
DEBUG=-g
# -O
CFLAGS=-I$$(TOP) -D_BSD_SOURCE
CPU=`uname -m`
CPUDIR=`uname -s|sed -e 's@/@@g'`-`uname -r`-`uname -m`
OBJ=.
#
# To enable this to work as a Loadable Kernel Module...
#
IPFLKM=-DIPFILTER_LKM
#
# To enable logging of blocked/passed packets...
#
IPFLOG=-DIPFILTER_LOG
#
# To enable loading filter rules compiled to C code...
#
#COMPIPF=-DIPFILTER_COMPILED
#
# To enable IPFilter compatibility with older CLI utilities
#
#COMPATIPF=-DIPFILTER_COMPAT
#
# To enable synchronisation between IPFilter hosts
#
#SYNC=-DIPFILTER_SYNC
#
# The facility you wish to log messages from ipmon to syslogd with.
#
LOGFAC=-DLOGFAC=LOG_SECURITY
#
# To enable rules to be written with BPF syntax, uncomment these two lines.
#
# WARNING: If you're building a commercial product based on IPFilter, using
#          this options *may* infringe at least one patent held by CheckPoint
#          (5,606,668.)
#
#IPFBPF=-DIPFILTER_BPF -I/usr/local/include
#LIBBPF=-L/usr/local/lib -lpcap
#
# HP-UX and Solaris require this uncommented for BPF.
#
#BPFILTER=bpf_filter.o
#
# LINUXKERNEL is the path to the top of your Linux kernel source tree.
# By default IPFilter looks for /usr/src/linux, but you may have to change
# it to /usr/src/linux-2.4 or similar.
#
LINUXKERNEL=/usr/src/kernels/2.6.29.5-191.fc11.i586
LINUX=`uname -r | awk -F. ' { printf"%d",$$1;for(i=1;i<NF&&i<3;i++){printf("%02d",$$(i+1));}}'`
#
#
#
#BUILDROOT=/usr/src/redhat/BUILD/ipfilter
BUILDROOT=${HOME}/rpmbuild/BUILDROOT/ipfilter-4.1.32-1.i386

#
# All of the compile-time options are here, used for compiling the userland
# tools for regression testing.  Well, all except for IPFILTER_LKM, of course.
#
ALLOPTS=-DIPFILTER_LOG -DIPFILTER_LOOKUP \
        -DIPFILTER_SYNC -DIPFILTER_CKSUM

#
# Uncomment the next 3 lines if you want to view the state table a la top(1)
# (requires that you have installed ncurses).
#STATETOP_CFLAGS=-DSTATETOP
#
# Where to find the ncurses include files (if not in default path),
#
#STATETOP_INC=
#STATETOP_INC=-I/usr/local/include
#
# How to link the ncurses library
#
#STATETOP_LIB=-lncurses
#STATETOP_LIB=-L/usr/local/lib -lncurses

#
# Uncomment this when building IPv6 capability.
#
INET6=-DUSE_INET6
#
# For packets which don't match any pass rules or any block rules, set either
# FR_PASS or FR_BLOCK (respectively).  It defaults to FR_PASS if left
# undefined.  This is ignored for ipftest, which can thus return three
# results: pass, block and nomatch.  This is the sort of "block unless
# explicitly allowed" type #define switch.
#
POLICY=-DIPF_DEFAULT_PASS=FR_PASS
#
MFLAGS1='CFLAGS=$(CFLAGS) $(ARCHINC) $(SOLARIS2) $(SGIREV) $(INET6)' \
	"IPFLOG=$(IPFLOG)" "LOGFAC=$(LOGFAC)" "POLICY=$(POLICY)" \
	"SOLARIS2=$(SOLARIS2)" "DEBUG=$(DEBUG)" "DCPU=$(CPU)" \
        "LIBBPF=$(LIBBPF)" "CPUDIR=$(CPUDIR)" "IPFBPF=$(IPFBPF)" \
        'STATETOP_CFLAGS=$(STATETOP_CFLAGS)' "BPFILTER=$(BPFILTER)" \
        'STATETOP_INC=$(STATETOP_INC)' 'STATETOP_LIB=$(STATETOP_LIB)' \
	"BITS=$(BITS)" "OBJ=$(OBJ)" "LOOKUP=$(LOOKUP)" "COMPIPF=$(COMPIPF)" \
	"COMPATIPF=$(COMPATIPF)" \
	'SYNC=$(SYNC)' 'ALLOPTS=$(ALLOPTS)' 'LIBBPF=$(LIBBPF)'
MFLAGS=$(MFLAGS1) "IPFLKM=$(IPFLKM)"
MACHASSERT=`/bin/ls -1 /usr/sys/*/mach_assert.h | head -1`
#
SHELL=/bin/sh
#
########## ########## ########## ########## ########## ########## ##########
#
CP=/bin/cp
RM=/bin/rm
CHMOD=/bin/chmod
INSTALL=install
#

all:
	@echo "Chose one of the following targets for making IP filter:"
	@echo ""
	@echo "solaris	- auto-selects SunOS4.1.x/Solaris 2.3-6/Solaris2.4-6x86"
	@echo "netbsd	- compile for NetBSD"
	@echo "openbsd	- compile for OpenBSD"
	@echo "freebsd20	- compile for FreeBSD 2.0, 2.1 or earlier"
	@echo "freebsd22	- compile for FreeBSD-2.2 or greater"
	@echo "freebsd  - compile for all other versions of FreeBSD"
	@echo "bsd	- compile for generic 4.4BSD systems"
	@echo "bsdi	- compile for BSD/OS"
	@echo "irix	- compile for SGI IRIX"
	@echo "hpux	- compile for HP-UX 11.00"
	@echo "osf	- compile for OSF/Tru64 5.1"
	@echo ""

tests:
	@if [ -d test ]; then (cd test; make) \
	else echo test directory not present, sorry; fi

retest:
	@if [ -d test ]; then (cd test; make clean && make) \
	else echo test directory not present, sorry; fi

include:
	-mkdir -p net netinet
	if [ ! -f netinet/done ] ; then \
		(cd netinet; ln -s ../*.h .; ln -s ../ip_*_pxy.c .;); \
		(cd netinet; ln -s ../ipsend/tcpip.h tcpip.h); \
		touch netinet/done; \
	fi
	-(cd netinet; ln -s ../ip_rules.h ip_rules.h)

sunos solaris: include
	MAKE="$(MAKE)" MAKEFLAGS="$(MAKEFLAGS)" BPFILTER=$(BPFILTER) \
		CC="$(CC)" DEBUG="$(DEBUG)" ./buildsunos

freebsd:
	make freebsd`uname -r|cut -c1`

freebsd22: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	-rm -f BSD/$(CPUDIR)/ioconf.h
	-if [ x$(IPFILKERN) != x ] ; then \
		if [ -f /sys/compile/$(IPFILKERN)/ioconf.h ] ; then \
			ln -s /sys/compile/$(IPFILKERN)/ioconf.h BSD/$$y; \
		else \
			ln -s /sys/$(IPFILKERN)/ioconf.h BSD/$$y; \
		fi \
	else \
		x=`uname -v|sed -e 's@^.*:\(/[^: ]*\).*$$@\1/ioconf.h@'`; \
		y=`uname -s|sed -e 's@/@@g'`-`uname -r`-`uname -m`; \
		if [ ! -f $$x ] ; then \
			echo -n "Can't find ioconf.h at $$x "; \
			exit 1;\
		else \
			ln -s $$x BSD/$$y ; \
		fi \
	fi
	make freebsd20

freebsd5 freebsd6 freebsd7 freebsd8: include
	if [ x$(INET6) = x ] ; then \
		echo "#undef INET6" > opt_inet6.h; \
	else \
		echo "#define INET6" > opt_inet6.h; \
	fi
	if [ "x$(IPFBPF)" = "x" ] ; then \
		echo "#undef NBPF" > opt_bpf.h; \
		echo "#undef NBPFILTER" > opt_bpf.h; \
		echo "#undef DEV_BPF" > opt_bpf.h; \
	else \
		echo "#define NBPF" > opt_bpf.h; \
		echo "#define NBPFILTER" > opt_bpf.h; \
		echo "#define DEV_BPF" > opt_bpf.h; \
	fi
	if [ x$(ENABLE_PFIL) = x ] ; then \
		echo "#undef PFIL_HOOKS" > opt_pfil.h; \
	else \
		echo "#define PFIL_HOOKS" > opt_pfil.h; \
	fi

	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) "ML=mlfk_ipl.c" "MLD=mlfk_ipl.c" "LKM=ipf.ko.5" "LKMR=ipfrule.ko.5" "DLKM=-DKLD_MODULE" "MLR=mlfk_rule.o"; cd ..)
#	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS1); cd ..)

freebsd4 : include
	if [ x$(INET6) = x ] ; then \
		echo "#undef INET6" > opt_inet6.h; \
	else \
		echo "#define INET6" > opt_inet6.h; \
	fi
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) "ML=mlfk_ipl.c" "MLD=mlfk_ipl.c" "LKM=ipf.ko" "LKMR=ipfrule.ko" "DLKM=-DKLD_MODULE" "MLR=mlfk_rule.o"; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS1); cd ..)

freebsd3 freebsd30: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS1) "ML=mlf_ipl.c" "MLR=mlf_rule.o" LKM= LKMR=; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS1); cd ..)

netbsd: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	@if [ ! -d /sys -o ! -d /sys/arch ] ; then \
		echo "*****************************************************"; \
		echo "*                                                   *"; \
		echo "* Please extract source code to create /sys and     *";\
		echo "* /sys/arch and run 'config GENERIC'                *"; \
		echo "*                                                   *"; \
		echo "*****************************************************"; \
		exit 1; \
	fi
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) 'DLKM=-D_LKM' "ML=mln_ipl.c" LKMR= "MLR=mln_rule.o"; cd ..)
#	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS); cd ..)

openbsd: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) 'DLKM=-D_LKM' "ML=mlo_ipl.c" LKMR= "MLR=mlo_rule.o"; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS); cd ..)

freebsd20 freebsd21: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) "ML=mlf_ipl.c" "MLR=mlf_rule.o"; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS); cd ..)

osf tru64: null include
	make setup "TARGOS=OSF" "CPUDIR=`OSF/cpurev`"
	(cd OSF/`OSF/cpurev`; make build TRU64=`uname -v` TOP=../.. "DEBUG=-g" $(MFLAGS) "MACHASSERT=$(MACHASSERT)" "OSREV=`../cpurev`"; cd ..)
	(cd OSF/`OSF/cpurev`; make -f Makefile.ipsend build TRU64=`uname -v` TOP=../.. $(MFLAGS) "OSREV=`../cpurev`"; cd ..)

aix: null include
	make setup "TARGOS=AIX" "CPUDIR=`AIX/cpurev`"
	(cd AIX/`AIX/cpurev`; make build AIX=`uname -v` TOP=../.. "DEBUG=-g" $(MFLAGS) "OSREV=`../cpurev`" BITS=`../bootbits.sh`; cd ..)
#	(cd AIX/`AIX/cpurev`; make -f Makefile.ipsend build AIX=`uname -v` TOP=../.. $(MFLAGS) "OSREV=`../cpurev`"; cd ..)

bsd: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build TOP=../.. $(MFLAGS) 'DLKM=-D_LKM' "ML=mln_ipl.c" "MLR=mln_rule.o"; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build TOP=../.. $(MFLAGS); cd ..)

bsdi bsdos: include
	make setup "TARGOS=BSD" "CPUDIR=$(CPUDIR)"
	(cd BSD/$(CPUDIR); make build "CC=$(CC)" TOP=../.. $(MFLAGS) LKM= LKMR= ; cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend build "CC=$(CC)" TOP=../.. $(MFLAGS); cd ..)

irix IRIX: include
	make setup TARGOS=IRIX CPUDIR=`IRIX/cpurev`
	if [ "x${SGIREV}" = "x" ] ; then \
		make irix "SGIREV=-D_KMEMUSER -DIRIX=`IRIX/getrev`"; \
	else \
		(cd IRIX/`IRIX/cpurev`; smake -l -J 1 build TOP=../.. $(DEST) $(MFLAGS) IRIX=`../getrev` SGI=$$(IRIX) CPUDIR=`../cpurev`; cd ..); \
		(cd IRIX/`IRIX/cpurev`; make -f Makefile.ipsend build TOP=../.. $(DEST) $(MFLAGS) IRIX=`../getrev` SGI=$$(IRIX) CPUDIR=`../cpurev`; cd ..); \
	fi

setup:
	-if [ ! -d $(TARGOS)/$(CPUDIR) ] ; then mkdir $(TARGOS)/$(CPUDIR); fi
	-rm -f $(TARGOS)/$(CPUDIR)/Makefile $(TARGOS)/$(CPUDIR)/Makefile.ipsend
	-ln -s ../Makefile $(TARGOS)/$(CPUDIR)/Makefile
	-ln -s ../Makefile.ipsend $(TARGOS)/$(CPUDIR)/Makefile.ipsend
	-if [ -f $(TARGOS)/Makefile.common ] ; then \
		rm -f $(TARGOS)/$(CPUDIR)/Makefile.common; \
		ln -s ../Makefile.common $(TARGOS)/$(CPUDIR)/Makefile.common;\
	fi

clean: clean-include
	/bin/rm -rf h y.output
	${RM} -f core *.o ipt fils ipf ipfstat ipftest ipmon if_ipl ipflkm \
	vnode_if.h $(LKM) *~
	/bin/rm -rf sparcv7 sparcv9 mdbgen_build
	(cd SunOS4; $(MAKE) TOP=.. clean)
	-(cd SunOS5; $(MAKE) TOP=.. clean)
	(cd BSD; $(MAKE) TOP=.. clean)
	(cd HPUX; $(MAKE) BITS=32 TOP=.. clean)
	(cd Linux; $(MAKE) TOP=.. clean)
	(cd OSF; $(MAKE) TOP=.. clean)
	(cd AIX; $(MAKE) TOP=.. clean)
	if [ "`uname -s`" = "IRIX" ]; then (cd IRIX; $(MAKE) clean); fi
	[ -d test ] && (cd test; $(MAKE) clean)
	(cd ipsend; $(MAKE) clean)

clean-include:
	sh -c 'if [ -d netinet ] ; then cd netinet; for i in *; do if [ -h $$i ] ; then /bin/rm -f $$i; fi; done fi'
	sh -c 'if [ -d net ] ; then cd net; for i in *; do if [ -h $$i ] ; then /bin/rm -f $$i; fi; done fi'
	${RM} -f netinet/done net/done

clean-bsd: clean-include
	(cd BSD; make TOP=.. clean)

clean-hpux: clean-include
	(cd HPUX; $(MAKE) BITS=32 clean)

clean-osf: clean-include
	(cd OSF; make clean)

clean-aix: clean-include
	(cd AIX; make clean)

clean-linux: clean-include
	(cd Linux; make clean)

clean-sunos4: clean-include
	(cd SunOS4; make clean)

clean-sunos5: clean-include
	(cd SunOS5; $(MAKE) clean)
	/bin/rm -rf sparcv?

clean-irix: clean-include
	(cd IRIX; $(MAKE) clean)

h/xti.h:
	mkdir -p h
	ln -s /usr/include/sys/xti.h h

hpux: include h/xti.h
	make setup CPUDIR=`HPUX/cpurev` TARGOS=HPUX
	(cd HPUX/`HPUX/cpurev`; $(MAKE) build TOP=../.. $(DEST) $(MFLAGS) "BITS=`getconf KERNEL_BITS`" `../makeargs`; cd ..)
	(cd HPUX/`HPUX/cpurev`; $(MAKE) -f Makefile.ipsend build TOP=../.. $(DEST) $(MFLAGS) "BITS=`getconf KERNEL_BITS`" `../makeargs`; cd ..)

sunos4 solaris1:
	(cd SunOS4; make build TOP=.. "CC=$(CC)" $(DEST) $(MFLAGS); cd ..)
	(cd SunOS4; make -f Makefile.ipsend build "CC=$(CC)" TOP=.. $(DEST) $(MFLAGS); cd ..)

sunos5 solaris2: null
	(cd SunOS5/$(CPUDIR); $(MAKE) build TOP=../.. "CC=$(CC)" $(DEST) $(MFLAGS) "SOLARIS2=$(SOLARIS2)" INSTANCE=$(INSTANCE); cd ..)
	(cd SunOS5/$(CPUDIR); $(MAKE) -f Makefile.ipsend build TOP=../.. "CC=$(CC)" $(DEST) $(MFLAGS); cd ..)

linux: include
	(cd Linux; make build LINUX=$(LINUX) TOP=.. "DEBUG=-g" "CC=$(CC)" $(MFLAGS) OBJ=$(CPUDIR) LINUXKERNEL=$(LINUXKERNEL); cd ..)
	(cd Linux; make ipflkm LINUX=$(LINUX) TOP=.. "DEBUG=-g" "CC=$(CC)" $(MFLAGS) OBJ=$(CPUDIR) LINUXKERNEL=$(LINUXKERNEL) WORKDIR=`pwd`; cd ..)
#	(cd Linux; make -f Makefile.ipsend build LINUX=$(LINUX) TOP=.. "CC=$(CC)" $(MFLAGS); cd ..)

install-linux: linux
	(cd Linux/; make LINUX=$(LINUX) TOP=.. "DEBUG=-g" "CC=$(CC)" $(MFLAGS) OBJ=$(CPUDIR) ROOTDIR=$(BUILDROOT) install ; cd ..)

install-bsd:
	(cd BSD/$(CPUDIR); make install "TOP=../.." $(MFLAGS); cd ..)
	(cd BSD/$(CPUDIR); make -f Makefile.ipsend INSTALL=$(INSTALL) install "TOP=../.." $(MFLAGS); cd ..)

install-sunos4: solaris
	(cd SunOS4; $(MAKE) CPU=$(CPU) TOP=.. install)

install-sunos5: solaris null
	(cd SunOS5; $(MAKE) TOP=.. install)

install-aix:
	(cd AIX/`AIX/cpurev`; make install "TOP=../.." $(MFLAGS); cd ..)
#	(cd AIX/`AIX/cpurev`; make -f Makefile.ipsend INSTALL=$(INSTALL) install "TOP=../.." $(MFLAGS); cd ..)

install-hpux: hpux
	(cd HPUX/`HPUX/cpurev`; $(MAKE) CPU=$(CPU) TOP=../.. "BITS=`getconf KERNEL_BITS`" install)

install-irix: irix
	(cd IRIX; smake install CPU=$(CPU) TOP=.. $(DEST) $(MFLAGS) CPUDIR=`./cpurev`)

install-osf install-tru64:
	(cd OSF/`OSF/cpurev`; make install "TOP=../.." $(MFLAGS); cd ..)

do-cvs:
	find . -type d -name CVS -print | xargs /bin/rm -rf
	find . -type f -name .cvsignore -print | xargs /bin/rm -f
	/bin/rm -f ip_msnrpc_pxy.c ip_sunrpc_pxy.c

ip_rules.c ip_rules.h: rules/ip_rules tools/ipfcomp.c
	-./ipf -n -cc -f rules/ip_rules 2>/dev/null 1>&2

null:
	@if [ "`$(MAKE) -v 2>&1 | sed -ne 's/GNU.*/GNU/p'`" = "GNU" ] ; then \
		echo 'Do not use GNU make (gmake) to compile IPFilter'; \
		exit 1; \
	fi
	-@echo make ok

mdb:
	/bin/rm -rf mdbgen_build
	mdbgen -D_KERNEL -DIPFILTER_LOG -DIPFILTER_LOOKUP -DSUNDDI \
	       -DIPFILTER_SCAN -DIPFILTER_LKM -DSOLARIS2=10 -n ipf_mdb -k \
	       -I/home/dr146992/pfil -I/home/dr146992/ipf -f \
	       /usr/include/netinet/in_systm.h,/usr/include/sys/ethernet.h,/usr/include/netinet/in.h,/usr/include/netinet/ip.h,/usr/include/netinet/ip_var.h,/usr/include/netinet/tcp.h,/usr/include/netinet/tcpip.h,/usr/include/netinet/ip_icmp.h,/usr/include/netinet/udp.h,ip_compat.h,ip_fil.h,ip_nat.h,ip_state.h,ip_proxy.h,ip_scan.h
