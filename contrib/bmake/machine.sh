:
# derrived from /etc/rc_d/os.sh

# RCSid:
#	$Id: machine.sh,v 1.18 2017/08/13 19:11:28 sjg Exp $
#
#	@(#) Copyright (c) 1994-2002 Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

OS=`uname`
OSREL=`uname -r`
OSMAJOR=`IFS=.; set $OSREL; echo $1`
machine=`uname -p 2>/dev/null || uname -m`
MACHINE=

# there is at least one case of `uname -p` outputting
# a bunch of usless drivel
case "$machine" in
unknown|*[!A-Za-z0-9_-]*)
        machine=`uname -m`
        ;;
esac

# Great! Solaris keeps moving arch(1)
# we need this here, and it is not always available...
Which() {
	# some shells cannot correctly handle `IFS`
	# in conjunction with the for loop.
	_dirs=`IFS=:; echo ${2:-$PATH}`
	for d in $_dirs
	do
		test -x $d/$1 && { echo $d/$1; break; }
	done
}

case $OS in
AIX)	# from http://gnats.netbsd.org/29386
	OSMAJOR=`uname -v`
	OSMINOR=`uname -r`
	MACHINE=$OS$OSMAJOR.$OSMINOR
	MACHINE_ARCH=`bootinfo -T`
	;;
OpenBSD)
	MACHINE=$OS$OSMAJOR.$machine
	arch=`Which arch /usr/bin:/usr/ucb:$PATH`
	MACHINE_ARCH=`$arch -s`;
	;;
Bitrig)
	MACHINE=$OS$OSMAJOR.$machine
	MACHINE_ARCH=`uname -m`;
	;;
*BSD)
	MACHINE=$OS$OSMAJOR.$machine
	;;
SunOS)
	arch=`Which arch /usr/bin:/usr/ucb:$PATH`
	test "$arch" && machine_arch=`$arch`

	case "$OSREL" in
	4.0*) MACHINE_ARCH=$machine_arch MACHINE=$machine_arch;;
	4*) MACHINE_ARCH=$machine_arch;;
	esac
	;;
HP-UX)
	MACHINE_ARCH=`IFS="/-."; set $machine; echo $1`
	;;
Interix)
	MACHINE=i386
	MACHINE_ARCH=i386
	;;
UnixWare)
	OSREL=`uname -v`
	OSMAJOR=`IFS=.; set $OSREL; echo $1`
	MACHINE_ARCH=`uname -m`
	;;
Linux)
        case "$machine" in
	i?86) MACHINE_ARCH=i386;;# does anyone really care about 686 vs 586?
	esac
        ;;
esac

MACHINE=${MACHINE:-$OS$OSMAJOR}
MACHINE_ARCH=${MACHINE_ARCH:-$machine}

(
case "$0" in
arch*)	echo $MACHINE_ARCH;;
*)
	case "$1" in
	"")	echo $MACHINE;;
	*)	echo $MACHINE_ARCH;;
	esac
	;;
esac
) | tr ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
