# @(#) Makefile 1.23 97/03/21 19:27:20
# $FreeBSD$

what:
	@echo
	@echo "Usage: edit the REAL_DAEMON_DIR definition in the Makefile then:"
	@echo
	@echo "	make sys-type"
	@echo
	@echo "If you are in a hurry you can try instead:"
	@echo
	@echo "	make REAL_DAEMON_DIR=/foo/bar sys-type"
	@echo
	@echo "And for a version with language extensions enabled:"
	@echo
	@echo "	make REAL_DAEMON_DIR=/foo/bar STYLE=-DPROCESS_OPTIONS sys-type"
	@echo
	@echo "This Makefile knows about the following sys-types:"
	@echo
	@echo "	generic (most bsd-ish systems with sys5 compatibility)"
	@echo "	386bsd aix alpha apollo bsdos convex-ultranet dell-gcc dgux dgux543"
	@echo "	dynix epix esix freebsd hpux irix4 irix5 irix6 isc iunix"
	@echo "	linux machten mips(untested) ncrsvr4 netbsd next osf power_unix_211"
	@echo "	ptx-2.x ptx-generic pyramid sco sco-nis sco-od2 sco-os5 sinix sunos4"
	@echo "	sunos40 sunos5 solaris8 sysv4 tandem ultrix unicos7 unicos8 unixware1 unixware2"
	@echo "	uts215 uxp"
	@echo
	@echo "If none of these match your environment, edit the system"
	@echo "dependencies sections in the Makefile and do a 'make other'."
	@echo

#######################################################
# Choice between easy and advanced installation recipe.
# 
# Advanced installation: vendor-provided daemons are left alone, and the
# inetd configuration file is edited. In this case, the REAL_DAEMON_DIR
# macro should reflect the actual directory with (most of) your
# vendor-provided network daemons.  These names can be found in the
# inetd.conf file. Usually, the telnet, ftp and finger daemons all live
# in the same directory.
# 
# Uncomment the appropriate line if you are going to edit inetd.conf.
#
# Ultrix 4.x SunOS 4.x ConvexOS 10.x Dynix/ptx
#REAL_DAEMON_DIR=/usr/etc
#
# SysV.4 Solaris 2.x OSF AIX
#REAL_DAEMON_DIR=/usr/sbin
#
# BSD 4.4
#REAL_DAEMON_DIR=/usr/libexec
#
# HP-UX SCO Unicos
#REAL_DAEMON_DIR=/etc

# Easy installation: vendor-provided network daemons are moved to "some
# other" directory, and the tcpd wrapper fills in the "holes". For this
# mode of operation, the REAL_DAEMON_DIR macro should be set to the "some
# other" directory.  The "..." is here for historical reasons only; you
# should probably use some other name. 
# 
# Uncomment the appropriate line if you are going to move your daemons.
#
# Ultrix 4.x SunOS 4.x ConvexOS 10.x Dynix/ptx
#REAL_DAEMON_DIR=/usr/etc/...
#
# SysV.4 Solaris 2.x OSF AIX
#REAL_DAEMON_DIR=/usr/sbin/...
#
# BSD 4.4
#REAL_DAEMON_DIR=/usr/libexec/...
#
# HP-UX SCO Unicos
#REAL_DAEMON_DIR=/etc/...

# End of mandatory section
##########################

##########################################
# Ready-to-use system-dependent templates.
#
# Ready-to-use templates are available for many systems (see the "echo"
# commands at the start of this Makefile).  The templates take care of
# all system dependencies: after editing the REAL_DAEMON_DIR definition
# above, do a "make sunos4" (or whatever system type is appropriate).
#
# If your system is not listed (or something that comes close enough), you
# have to edit the system dependencies section below and do a "make other".  
#
# Send templates for other UNIX versions to wietse@wzv.win.tue.nl.

# This is good for many BSD+SYSV hybrids with NIS (formerly YP).
generic aix osf alpha dynix:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= all

# Ditto, with vsyslog
sunos4:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP VSYSLOG= TLI= all

# Generic with resolver library.
generic-resolver:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lresolv RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= all

# The NeXT loader needs "-m" or it barfs on redefined library functions.
next:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-m RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP=-DNETGROUP TLI= all

# SunOS for the 386 was frozen at release 4.0.x.
sunos40:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ="setenv.o strcasecmp.o" \
	NETGROUP=-DNETGROUP VSYSLOG= TLI= all

# Ultrix is like aix, next, etc., but has miscd and setenv().
ultrix:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ= \
	NETGROUP=-DNETGROUP TLI= all miscd

# This works on EP/IX 1.4.3 and will likely work on Mips (reggers@julian.uwo.ca)
epix:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP=-DNETGROUP TLI= SYSTYPE="-systype bsd43" all

# Freebsd and linux by default have no NIS.
386bsd bsdos:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ= NETGROUP= TLI= \
	EXTRA_CFLAGS=-DSYS_ERRLIST_DEFINED VSYSLOG= all

freebsd:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	RANLIB=ranlib ARFLAGS=$(ARFLAGS) AUX_OBJ= NETGROUP=-DNETGROUP TLI= \
	EXTRA_CFLAGS="-DSYS_ERRLIST_DEFINED -DINET6 -DUSE_GETIPNODEBY" \
	VSYSLOG= all

netbsd:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ= NETGROUP= TLI= \
	EXTRA_CFLAGS="-DSYS_ERRLIST_DEFINED -DINET6 \
	-Dss_family=__ss_family -Dss_len=__ss_len" VSYSLOG= all

linux:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o NETGROUP= TLI= \
	EXTRA_CFLAGS="-DSYS_ERRLIST_DEFINED -DBROKEN_SO_LINGER -DINET6=1 \
	-Dss_family=__ss_family -Dss_len=__ss_len" all

linux-old:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="/usr/inet6/lib/libinet6.a -lresolv" \
	RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o NETGROUP= TLI= \
	EXTRA_CFLAGS="-DSYS_ERRLIST_DEFINED -DBROKEN_SO_LINGER -DINET6=1 -Dss_family=sin6_family -Dsockaddr_storage=sockaddr_in6 -I/usr/inet6/include" all

# This is good for many SYSV+BSD hybrids with NIS, probably also for HP-UX 7.x.
hpux hpux8 hpux9 hpux10:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=echo ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= all

# ConvexOS-10.x with UltraNet support (ukkonen@csc.fi).
convex-ultranet:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lulsock RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP=-DNETGROUP TLI= all

# Generic support for the Dynix/PTX version of TLI.
ptx-generic:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -linet -lnsl" RANLIB=echo ARFLAGS=rv \
	AUX_OBJ="setenv.o strcasecmp.o ptx.o" NETGROUP= TLI=-DPTX all

# With UDP support optimized for PTX 2.x (timw@sequent.com).
ptx-2.x:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -linet -lnsl" RANLIB=echo ARFLAGS=rv \
	AUX_OBJ="setenv.o strcasecmp.o tli-sequent.o" NETGROUP= \
	TLI=-DTLI_SEQUENT all

# IRIX 4.0.x has a special ar(1) flag.
irix4:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lc -lsun" RANLIB=echo ARFLAGS=rvs AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= all

# IRIX 5.2 is SYSV4 with several broken things (such as -lsocket -lnsl).
irix5:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lsun RANLIB=echo ARFLAGS=rv VSYSLOG= \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI= all

# IRIX 6.2 (tucker@math.unc.edu). Must find a better value than 200000.
irix6:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=echo ARFLAGS=rv VSYSLOG= \
	NETGROUP=-DNETGROUP EXTRA_CFLAGS="-DBSD=200000" TLI= all

# SunOS 5.x is another SYSV4 variant.
sunos5:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=echo ARFLAGS=rv VSYSLOG= \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI=-DTLI \
	BUGS="$(BUGS) -DSOLARIS_24_GETHOSTBYNAME_BUG" all

# SunOS 5.8 is another SYSV4 variant, but has IPv6 support
solaris8:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=echo ARFLAGS=rv VSYSLOG= \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI=-DTLI \
	EXTRA_CFLAGS="-DINET6 -DUSE_GETIPNODEBY -DNO_CLONE_DEVICE \
	-DINT32_T" all

# Generic SYSV40
esix sysv4:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=echo ARFLAGS=rv \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI=-DTLI all

# DG/UX 5.4.1 and 5.4.2 have an unusual inet_addr() interface.
dgux:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lnsl RANLIB=echo ARFLAGS=rv \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI=-DTLI \
	BUGS="$(BUGS) -DINET_ADDR_BUG" all

dgux543:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lnsl RANLIB=echo ARFLAGS=rv \
	NETGROUP=-DNETGROUP AUX_OBJ=setenv.o TLI=-DTLI all

# NCR UNIX 02.02.01 and 02.03.00 (Alex Chircop, msu@unimt.mt)
ncrsvr4:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lresolv -lnsl -lsocket" RANLIB=echo ARFLAGS=rv \
	AUX_OBJ="setenv.o strcasecmp.o" NETGROUP= TLI=-DTLI \
	EXTRA_CFLAGS="" FROM_OBJ=ncr.o all

# Tandem SYSV4 (eqawas@hedgehog.ac.cowan.edu.au)
tandem:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=echo ARFLAGS=rv \
	NETGROUP= AUX_OBJ="setenv.o strcasecmp.o" TLI=-DTLI all

# Amdahl UTS 2.1.5 (Richard.Richmond@bridge.bst.bls.com)
uts215:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket" RANLIB=echo \
	ARFLAGS=rv AUX_OBJ=setenv.o NETGROUP=-DNO_NETGROUP TLI= all

# UXP/DS System V.4 clone (vic@uida0.uida.es).
uxp:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-L/usr/ucblib -lsocket -lnsl -lucb" \
	RANLIB=echo ARFLAGS=rv NETGROUP=-DNETGROUP \
	AUX_OBJ=setenv.o TLI="-DTLI -DDRS_XTI" all

# DELL System V.4 Issue 2.2 using gcc (kim@tac.nyc.ny.us, jurban@norden1.com)
dell-gcc:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=ranlib ARFLAGS=rv CC=gcc \
	AUX_OBJ="setenv.o strcasecmp.o" TLI=-DTLI all

# SCO 3.2v4.1 no frills (jedwards@sol1.solinet.net).
sco:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl_s" RANLIB=echo ARFLAGS=rv \
	NETGROUP= AUX_OBJ=setenv.o TLI= all

# SCO OpenDesktop 2.0, release 3.2 (peter@midnight.com). Please simplify.
sco-od2:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lrpcsvc -lrpc -lyp -lrpc -lrpcsvc -lsocket" \
	RANLIB=echo ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= all

# SCO 3.2v4.2 with TCP/IP 1.2.1 (Eduard.Vopicka@vse.cz). Please simplify.
sco-nis:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lyp -lrpc -lsocket -lyp -lc_s -lc" \
	RANLIB=echo ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= EXTRA_CFLAGS="-nointl -DNO_NETGRENT" all

# SCO 3.2v5.0.0 OpenServer 5 (bob@odt.handy.com, bill@razorlogic.com)
sco-os5:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lrpcsvc -lsocket" RANLIB=echo ARFLAGS=rv VSYSLOG= \
	AUX_OBJ=setenv.o NETGROUP=-DNETGROUP TLI= all

# sinix 5.42 setjmp workaround (szrzs023@ub3.ub.uni-kiel.de)
sinix:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl -L/usr/ccs/lib -lc -L/usr/ucblib -lucb" \
	RANLIB=echo ARFLAGS=rv AUX_OBJ=setenv.o TLI=-DTLI all

# Domain SR10.4. Build under bsd, run under either sysv3 or bsd43.
apollo:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=setenv.o \
	NETGROUP=-DNETGROUP TLI= SYSTYPE="-A run,any -A sys,any" all

# Pyramid OSx 5.1, using the BSD universe.
pyramid:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ="environ.o vfprintf.o" \
	STRINGS="-Dstrchr=index -Dstrrchr=rindex -Dmemcmp=bcmp -Dno_memcpy" \
	NETGROUP="-DNETGROUP -DUSE_GETDOMAIN" TLI= all

# Untested.
mips:
	@echo "Warning: some definitions may be wrong."
	make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP=-DNETGROUP TLI= SYSTYPE="-sysname bsd43" all

# Cray (tested with UNICOS 7.0.4).
unicos7:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS=-lnet RANLIB=echo ARFLAGS=rv \
	EXTRA_CFLAGS=-DINADDR_NONE="\"((unsigned long) -1)\"" \
	AUX_OBJ="setenv.o strcasecmp.o" NETGROUP= TLI= all

# Unicos 8.x, Cray-YMP (Bruce Kelly).
unicos8:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=echo AR=bld ARFLAGS=rv \
	AUX_OBJ= NETGROUP= TLI= all

# Power_UNIX 2.1.1 (amantel@lerc.nasa.gov)
power_unix_211:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lnsl -lsocket -lgen -lresolv" RANLIB=echo ARFLAGS=rv \
	NETGROUP= AUX_OBJ=setenv.o TLI=-DTLI BUGS="$(BUGS)" all

# ISC (fc@all.net)
isc:
	make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-linet -lnsl_s -ldbm" RANLIB=echo ARFLAGS=rv \
	AUX_OBJ="setenv.o strcasecmp.o" EXTRA_CFLAGS="-DENOTCONN=ENAVAIL" \
	NETGROUP= TLI= all

# Interactive UNIX R3.2 version 4.0 (Bobby D. Wright).
iunix:
	make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-linet -lnsl_s -ldbm" RANLIB=echo ARFLAGS=rv \
	AUX_OBJ=environ.o strcasecmp.o NETGROUP= TLI= all

# RTU 6.0 on a Masscomp 5400 (ben@piglet.cr.usgs.gov). When using the
# advanced installation, increment argv before actually looking at it.
rtu:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP= TLI= all

# Unixware sans NIS (mc@telebase.com). Compiler dislikes strcasecmp.c.
unixware1:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl -lc -L/usr/ucblib -lucb" RANLIB=echo ARFLAGS=rv \
	NETGROUP=$(NETGROUP) AUX_OBJ=environ.o TLI=-DTLI all

unixware2:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl -lgen -lc -L/usr/ucblib -lucb" RANLIB=echo \
	ARFLAGS=rv NETGROUP=$(NETGROUP) AUX_OBJ=environ.o TLI=-DTLI all

u6000:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS="-lsocket -lnsl" RANLIB=echo ARFLAGS=rv \
	NETGROUP=-DNETGROUP AUX_OBJ="setenv.o strcasecmp.o" TLI=-DTLI all

# MachTen
machten:
	@make REAL_DAEMON_DIR=$(REAL_DAEMON_DIR) STYLE=$(STYLE) \
	LIBS= RANLIB=ranlib ARFLAGS=rv AUX_OBJ=environ.o \
	NETGROUP= TLI= all

###############################################################
# System dependencies: TLI (transport-level interface) support.
# 
# Uncomment the following macro if your system has System V.4-style TLI
# support (/usr/include/sys/timod.h, /etc/netconfig, and the netdir(3)
# routines).
#
#TLI	= -DTLI

###############################################################################
# System dependencies: differences between ranlib(1) and ar(1) implementations.
#
# Some C compilers (Ultrix 4.x) insist that ranlib(1) be run on an object
# library; some don't care as long as the modules are in the right order;
# some systems don't even have a ranlib(1) command. Make your choice.

RANLIB	= ranlib	# have ranlib (BSD-ish UNIX)
#RANLIB	= echo		# no ranlib (SYSV-ish UNIX)

ARFLAGS	?= rv		# most systems
#ARFLAGS= rvs		# IRIX 4.0.x

AR	= ar
#AR	= bld		# Unicos 8.x

#############################################################################
# System dependencies: routines that are not present in the system libraries.
# 
# If your system library does not have set/putenv() or strcasecmp(), use
# the ones provided with this source distribution. The environ.c module
# implements setenv(), getenv(), and putenv().

#AUX_OBJ= setenv.o
#AUX_OBJ= environ.o
#AUX_OBJ= environ.o strcasecmp.o

# Uncomment the following if your C library does not provide the
# strchr/strrchr/memcmp routines, but comes with index/rindex/bcmp.
#
#STRINGS= -Dstrchr=index -Dstrrchr=rindex -Dmemcmp=bcmp -Dno_memcpy

#################################################################
# System dependencies: selection of non-default object libraries.
#
# Most System V implementations require that you explicitly specify the
# networking libraries. There is no general consensus, though.
#
#LIBS	= -lsocket -lnsl		# SysV.4 Solaris 2.x
#LIBS	= -lsun				# IRIX
#LIBS	= -lsocket -linet -lnsl -lnfs	# PTX
#LIBS	= -linet -lnsl_s -ldbm		# ISC
#LIBS	= -lnet				# Unicos 7
#LIBS	= -linet -lsyslog -ldbm
#LIBS	= -lsyslog -lsocket -lnsl

######################################################
# System dependencies: system-specific compiler flags.
#
# Apollo Domain/OS offers both bsd and sys5 environments, sometimes
# on the same machine.  If your Apollo is primarily sys5.3 and also
# has bsd4.3, uncomment the following to build under bsd and run under
# either environment.
#
#SYSTYPE= -A run,any -A sys,any

# For MIPS RISC/os 4_52.p3, uncomment the following definition.
#
#SYSTYPE= -sysname bsd43

##################################################
# System dependencies: working around system bugs.
#
# -DGETPEERNAME_BUG works around a getpeername(2) bug in some versions of
# Apollo or SYSV.4 UNIX:  the wrapper would report that all UDP requests
# come from address 0.0.0.0. The workaround does no harm on other systems.
#
# -DBROKEN_FGETS works around an fgets(3) bug in some System V versions
# (IRIX):  fgets() gives up too fast when reading from a network socket.
# The workaround does no harm on other systems.
#
# Some UNIX systems (IRIX) make the error of calling the strtok() library
# routine from other library routines such as, e.g., gethostbyname/addr().
# The result is that hosts can slip through the wrapper allow/deny filters.
# Compile with -DLIBC_CALLS_STRTOK to avoid the vendor's strtok() routine.
# The workaround does no harm on other systems.
#
# DG/UX 5.4.1 comes with an inet_ntoa() function that returns a structure
# instead of a long integer. Compile with -DINET_ADDR_BUG to work around
# this mutant behavour. Fixed in 5.4R3.
#
# Solaris 2.4 gethostbyname(), in DNS through NIS mode, puts only one
# address in the host address list; all other addresses are treated as
# host name aliases. Compile with -DSOLARIS_24_GETHOSTBYNAME_BUG to work
# around this. The workaround does no harm on other Solaris versions.

#BUGS = -DGETPEERNAME_BUG -DBROKEN_FGETS -DLIBC_CALLS_STRTOK
#BUGS = -DGETPEERNAME_BUG -DBROKEN_FGETS -DINET_ADDR_BUG
#BUGS = -DGETPEERNAME_BUG -DBROKEN_FGETS -DSOLARIS_24_GETHOSTBYNAME_BUG

##########################################################################
# System dependencies: whether or not your system has NIS (or YP) support.
#
# If your system supports NIS or YP-style netgroups, enable the following
# macro definition. Netgroups are used only for host access control.
#
#NETGROUP= -DNETGROUP

###############################################################
# System dependencies: whether or not your system has vsyslog()
#
# If your system supports vsyslog(), comment out the following definition.
# If in doubt leave it in, it won't harm.

#VSYSLOG	= -Dvsyslog=myvsyslog

# End of the system dependencies.
#################################

##############################
# Start of the optional stuff.

###########################################
# Optional: Turning on language extensions
#
# Instead of the default access control language that is documented in
# the hosts_access.5 document, the wrappers can be configured to
# implement an extensible language documented in the hosts_options.5
# document.  This language is implemented by the "options.c" source
# module, which also gives hints on how to add your own extensions.
# Uncomment the next definition to turn on the language extensions
# (examples: allow, deny, banners, twist and spawn).
# 
#STYLE	= -DPROCESS_OPTIONS	# Enable language extensions.

################################################################
# Optional: Changing the default disposition of logfile records
#
# By default, logfile entries are written to the same file as used for
# sendmail transaction logs. See your /etc/syslog.conf file for actual
# path names of logfiles. The tutorial section in the README file
# gives a brief introduction to the syslog daemon.
# 
# Change the FACILITY definition below if you disagree with the default
# disposition. Some syslog versions (including Ultrix 4.x) do not provide
# this flexibility.
# 
# If nothing shows up on your system, it may be that the syslog records
# are sent to a dedicated loghost. It may also be that no syslog daemon
# is running at all. The README file gives pointers to surrogate syslog
# implementations for systems that have no syslog library routines or
# no syslog daemons. When changing the syslog.conf file, remember that
# there must be TABs between fields.
#
# The LOG_XXX names below are taken from the /usr/include/syslog.h file.

FACILITY= LOG_MAIL	# LOG_MAIL is what most sendmail daemons use

# The syslog priority at which successful connections are logged.

SEVERITY= LOG_INFO	# LOG_INFO is normally not logged to the console

###########################
# Optional: Reduce DNS load
#
# When looking up the address for a host.domain name, the typical DNS
# code will first append substrings of your own domain, so it tries
# host.domain.your.own.domain, then host.domain.own.domain, and then
# host.domain. The APPEND_DOT feature stops this waste of cycles. It is
# off by default because it causes problems on sites that don't use DNS
# and with Solaris < 2.4. APPEND_DOT will not work with hostnames taken
# from /etc/hosts or from NIS maps. It does work with DNS through NIS.
#
# DOT= -DAPPEND_DOT

##################################################
# Optional: Always attempt remote username lookups
#
# By default, the wrappers look up the remote username only when the
# access control rules require them to do so.
#
# Username lookups require that the remote host runs a daemon that
# supports an RFC 931 like protocol.  Remote user name lookups are not
# possible for UDP-based connections, and can cause noticeable delays
# with connections from non-UNIX PCs.  On some systems, remote username
# lookups can trigger a kernel bug, causing loss of service. The README
# file describes how to find out if your UNIX kernel has that problem.
# 
# Uncomment the following definition if the wrappers should always
# attempt to get the remote user name. If this is not enabled you can
# still do selective username lookups as documented in the hosts_access.5
# and hosts_options.5 manual pages (`nroff -man' format).
#
#AUTH	= -DALWAYS_RFC931
#
# The default username lookup timeout is 10 seconds. This may not be long
# enough for slow hosts or networks, but is enough to irritate PC users.

RFC931_TIMEOUT = 10

######################################################
# Optional: Changing the default file protection mask
#
# On many systems, network daemons and other system processes are started
# with a zero umask value, so that world-writable files may be produced.
# It is a good idea to edit your /etc/rc* files so that they begin with
# an explicit umask setting.  On our site we use `umask 022' because it
# does not break anything yet gives adequate protection against tampering.
# 
# The following macro specifies the default umask for processes run under
# control of the daemon wrappers. Comment it out only if you are certain
# that inetd and its children are started with a safe umask value.

UMASK	= -DDAEMON_UMASK=022

#######################################
# Optional: Turning off access control
#
# By default, host access control is enabled.  To disable host access
# control, comment out the following definition.  Host access control
# can also be turned off at runtime by providing no or empty access
# control tables.

ACCESS	= -DHOSTS_ACCESS

########################################################
# Optional: Changing the access control table pathnames
#
# The HOSTS_ALLOW and HOSTS_DENY macros define where the programs will
# look for access control information. Watch out for the quotes and
# backslashes when you make changes.

TABLES	= -DHOSTS_DENY=\"/etc/hosts.deny\" -DHOSTS_ALLOW=\"/etc/hosts.allow\"

####################################################
# Optional: dealing with host name/address conflicts
#
# By default, the software tries to protect against hosts that claim to
# have someone elses host name. This is relevant for network services
# whose authentication depends on host names, such as rsh and rlogin.
#
# With paranoid mode on, connections will be rejected when the host name
# does not match the host address. Connections will also be rejected when
# the host name is available but cannot be verified.
#
# Comment out the following definition if you want more control over such
# requests. When paranoid mode is off and a host name double check fails,
# the client can be matched with the PARANOID access control pattern.
#
# Paranoid mode implies hostname lookup. In order to disable hostname
# lookups altogether, see the next section.

PARANOID= -DPARANOID

########################################
# Optional: turning off hostname lookups
#
# By default, the software always attempts to look up the client
# hostname.  With selective hostname lookups, the client hostname
# lookup is postponed until the name is required by an access control
# rule or by a %letter expansion.
# 
# In order to perform selective hostname lookups, disable paranoid
# mode (see previous section) and comment out the following definition.

HOSTNAME= -DALWAYS_HOSTNAME

#############################################
# Optional: Turning on host ADDRESS checking
#
# Optionally, the software tries to protect against hosts that pretend to
# have someone elses host address. This is relevant for network services
# whose authentication depends on host names, such as rsh and rlogin,
# because the network address is used to look up the remote host name.
# 
# The protection is to refuse TCP connections with IP source routing
# options.
#
# This feature cannot be used with SunOS 4.x because of a kernel bug in
# the implementation of the getsockopt() system call. Kernel panics have
# been observed for SunOS 4.1.[1-3]. Symptoms are "BAD TRAP" and "Data
# fault" while executing the tcp_ctloutput() kernel function.
#
# Reportedly, Sun patch 100804-03 or 101790 fixes this for SunOS 4.1.x.
#
# Uncomment the following macro definition if your getsockopt() is OK.
#
# -DKILL_IP_OPTIONS is not needed on modern UNIX systems that can stop
# source-routed traffic in the kernel. Examples: 4.4BSD derivatives,
# Solaris 2.x, and Linux. See your system documentation for details.
#
# KILL_OPT= -DKILL_IP_OPTIONS

## End configuration options
############################

# Protection against weird shells or weird make programs.

SHELL	= /bin/sh
.c.o:;	$(CC) $(CFLAGS) -c $*.c

CFLAGS	= -O -DFACILITY=$(FACILITY) $(ACCESS) $(PARANOID) $(NETGROUP) \
	$(BUGS) $(SYSTYPE) $(AUTH) $(UMASK) \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" $(STYLE) $(KILL_OPT) \
	-DSEVERITY=$(SEVERITY) -DRFC931_TIMEOUT=$(RFC931_TIMEOUT) \
	$(UCHAR) $(TABLES) $(STRINGS) $(TLI) $(EXTRA_CFLAGS) $(DOT) \
	$(VSYSLOG) $(HOSTNAME)

LIB_OBJ= hosts_access.o options.o shell_cmd.o rfc931.o eval.o \
	hosts_ctl.o refuse.o percent_x.o clean_exit.o $(AUX_OBJ) \
	$(FROM_OBJ) fix_options.o socket.o tli.o workarounds.o \
	update.o misc.o diag.o percent_m.o myvsyslog.o

FROM_OBJ= fromhost.o

KIT	= README miscd.c tcpd.c fromhost.c hosts_access.c shell_cmd.c \
	tcpd.h tcpdmatch.c Makefile hosts_access.5 strcasecmp.c BLURB rfc931.c \
	tcpd.8 eval.c hosts_access.3 hosts_ctl.c percent_x.c options.c \
	clean_exit.c environ.c patchlevel.h fix_options.c workarounds.c \
	socket.c tli.c DISCLAIMER fakelog.c safe_finger.c hosts_options.5 \
	CHANGES try-from.c update.c ptx.c vfprintf.c tli-sequent.c \
	tli-sequent.h misc.c diag.c ncr.c tcpdchk.c percent_m.c \
	myvsyslog.c mystdarg.h printf.ck README.IRIX Banners.Makefile \
	refuse.c tcpdchk.8 setenv.c inetcf.c inetcf.h scaffold.c \
	scaffold.h tcpdmatch.8 README.NIS

LIB	= libwrap.a

all other: config-check tcpd tcpdmatch try-from safe_finger tcpdchk

# Invalidate all object files when the compiler options (CFLAGS) have changed.

config-check:
	@set +e; test -n "$(REAL_DAEMON_DIR)" || { make; exit 1; }
	@set +e; echo $(CFLAGS) >/tmp/cflags.$$$$ ; \
	if cmp cflags /tmp/cflags.$$$$ ; \
	then rm /tmp/cflags.$$$$ ; \
	else mv /tmp/cflags.$$$$ cflags ; \
	fi >/dev/null 2>/dev/null

$(LIB):	$(LIB_OBJ)
	rm -f $(LIB)
	$(AR) $(ARFLAGS) $(LIB) $(LIB_OBJ)
	-$(RANLIB) $(LIB)

tcpd:	tcpd.o $(LIB)
	$(CC) $(CFLAGS) -o $@ tcpd.o $(LIB) $(LIBS)

miscd:	miscd.o $(LIB)
	$(CC) $(CFLAGS) -o $@ miscd.o $(LIB) $(LIBS)

safe_finger: safe_finger.o $(LIB)
	$(CC) $(CFLAGS) -o $@ safe_finger.o $(LIB) $(LIBS)

TCPDMATCH_OBJ = tcpdmatch.o fakelog.o inetcf.o scaffold.o

tcpdmatch: $(TCPDMATCH_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TCPDMATCH_OBJ) $(LIB) $(LIBS)

try-from: try-from.o fakelog.o $(LIB)
	$(CC) $(CFLAGS) -o $@ try-from.o fakelog.o $(LIB) $(LIBS)

TCPDCHK_OBJ = tcpdchk.o fakelog.o inetcf.o scaffold.o

tcpdchk: $(TCPDCHK_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TCPDCHK_OBJ) $(LIB) $(LIBS)

shar:	$(KIT)
	@shar $(KIT)

kit:	$(KIT)
	@makekit $(KIT)

files:
	@echo $(KIT)

archive:
	$(ARCHIVE) $(KIT)

clean:
	rm -f tcpd miscd safe_finger tcpdmatch tcpdchk try-from *.[oa] core \
	cflags

tidy:	clean
	chmod -R a+r .
	chmod 755 .

# Enable all bells and whistles for linting.

lint: tcpd_lint miscd_lint match_lint chk_lint

tcpd_lint:
	lint -DFACILITY=LOG_MAIL -DHOSTS_ACCESS -DPARANOID -DNETGROUP \
	-DGETPEERNAME_BUG -DDAEMON_UMASK=022 -DSEVERITY=$(SEVERITY) \
	$(TABLES) -DKILL_IP_OPTIONS -DPROCESS_OPTIONS \
	-DRFC931_TIMEOUT=$(RFC931_TIMEOUT) -DALWAYS_RFC931 \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" \
	-Dvsyslog=myvsyslog \
	tcpd.c fromhost.c socket.c tli.c hosts_access.c \
	shell_cmd.c refuse.c rfc931.c eval.c percent_x.c clean_exit.c \
	options.c setenv.c fix_options.c workarounds.c update.c misc.c \
	diag.c myvsyslog.c percent_m.c

miscd_lint:
	lint -DFACILITY=LOG_MAIL -DHOSTS_ACCESS -DPARANOID -DNETGROUP \
	-DGETPEERNAME_BUG -DDAEMON_UMASK=022 -DSEVERITY=$(SEVERITY) \
	$(TABLES) -DKILL_IP_OPTIONS -DPROCESS_OPTIONS \
	-DRFC931_TIMEOUT=$(RFC931_TIMEOUT) -DALWAYS_RFC931 \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" \
	-Dvsyslog=myvsyslog \
	miscd.c fromhost.c socket.c tli.c hosts_access.c \
	shell_cmd.c refuse.c rfc931.c eval.c percent_x.c clean_exit.c \
	options.c setenv.c fix_options.c workarounds.c update.c misc.c \
	diag.c myvsyslog.c percent_m.c

match_lint:
	lint -DFACILITY=LOG_MAIL -DSEVERITY=$(SEVERITY) -DHOSTS_ACCESS \
	-DPARANOID $(TABLES) -DNETGROUP -DPROCESS_OPTIONS -DRFC931_TIMEOUT=10 \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" \
	-Dvsyslog=myvsyslog \
	tcpdmatch.c hosts_access.c eval.c percent_x.c options.c workarounds.c \
	update.c socket.c misc.c diag.c myvsyslog.c percent_m.c setenv.c \
	inetcf.c scaffold.c

chk_lint:
	lint -DFACILITY=LOG_MAIL -DSEVERITY=$(SEVERITY) -DHOSTS_ACCESS \
	-DPARANOID $(TABLES) -DNETGROUP -DPROCESS_OPTIONS -DRFC931_TIMEOUT=10 \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" \
	-Dvsyslog=myvsyslog \
	tcpdchk.c eval.c percent_x.c options.c update.c workarounds.c \
	setenv.c misc.c diag.c myvsyslog.c percent_m.c inetcf.c scaffold.c

printfck:
	printfck -f printf.ck \
	tcpd.c fromhost.c socket.c tli.c hosts_access.c \
	shell_cmd.c refuse.c rfc931.c eval.c percent_x.c clean_exit.c \
	options.c setenv.c fix_options.c workarounds.c update.c misc.c \
	diag.c myvsyslog.c percent_m.c >aap.c
	lint -DFACILITY=LOG_MAIL -DHOSTS_ACCESS -DPARANOID -DNETGROUP \
	-DGETPEERNAME_BUG -DDAEMON_UMASK=022 -DSEVERITY=$(SEVERITY) \
	$(TABLES) -DKILL_IP_OPTIONS -DPROCESS_OPTIONS \
	-DRFC931_TIMEOUT=$(RFC931_TIMEOUT) -DALWAYS_RFC931 \
	-DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\" -Dvsyslog=myvsyslog aap.c
	printfck -f printf.ck \
	tcpdchk.c eval.c percent_x.c options.c update.c workarounds.c \
	setenv.c misc.c diag.c myvsyslog.c percent_m.c inetcf.c scaffold.c \
	>aap.c
	lint -DFACILITY=LOG_MAIL -DSEVERITY=$(SEVERITY) -DHOSTS_ACCESS \
	-DPARANOID $(TABLES) -DNETGROUP -DPROCESS_OPTIONS -DRFC931_TIMEOUT=10 \
	-Dvsyslog=myvsyslog -DREAL_DAEMON_DIR=\"$(REAL_DAEMON_DIR)\"

# Internal compilation dependencies.

clean_exit.o: cflags
clean_exit.o: tcpd.h
diag.o: cflags
diag.o: mystdarg.h
diag.o: tcpd.h
environ.o: cflags
eval.o: cflags
eval.o: tcpd.h
fakelog.o: cflags
fakelog.o: mystdarg.h
fix_options.o: cflags
fix_options.o: tcpd.h
fromhost.o: cflags
fromhost.o: tcpd.h
hosts_access.o: cflags
hosts_access.o: tcpd.h
hosts_ctl.o: cflags
hosts_ctl.o: tcpd.h
inetcf.o: cflags
inetcf.o: inetcf.h
inetcf.o: tcpd.h
misc.o: cflags
misc.o: tcpd.h
miscd.o: cflags
miscd.o: patchlevel.h
miscd.o: tcpd.h
myvsyslog.o: cflags
myvsyslog.o: mystdarg.h
myvsyslog.o: tcpd.h
ncr.o: cflags
ncr.o: tcpd.h
options.o: cflags
options.o: tcpd.h
percent_m.o: cflags
percent_m.o: mystdarg.h
percent_x.o: cflags
percent_x.o: tcpd.h
ptx.o: cflags
ptx.o: tcpd.h
refuse.o: cflags
refuse.o: tcpd.h
rfc931.o: cflags
rfc931.o: tcpd.h
safe_finger.o: cflags
scaffold.o: cflags
scaffold.o: scaffold.h
scaffold.o: tcpd.h
setenv.o: cflags
shell_cmd.o: cflags
shell_cmd.o: tcpd.h
socket.o: cflags
socket.o: tcpd.h
strcasecmp.o: cflags
tcpd.o: cflags
tcpd.o: patchlevel.h
tcpd.o: tcpd.h
tcpdchk.o: cflags
tcpdchk.o: inetcf.h
tcpdchk.o: scaffold.h
tcpdchk.o: tcpd.h
tcpdmatch.o: cflags
tcpdmatch.o: scaffold.h
tcpdmatch.o: tcpd.h
tli-sequent.o: cflags
tli-sequent.o: tcpd.h
tli-sequent.o: tli-sequent.h
tli.o: cflags
tli.o: tcpd.h
try-from.o: cflags
try-from.o: tcpd.h
update.o: cflags
update.o: mystdarg.h
update.o: tcpd.h
vfprintf.o: cflags
workarounds.o: cflags
workarounds.o: tcpd.h
