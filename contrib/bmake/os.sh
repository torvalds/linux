:
# NAME:
#	os.sh - operating system specifics
#
# DESCRIPTION:
#	This file is included at the start of processing. Its role is
#	to set the variables OS, OSREL, OSMAJOR, MACHINE and MACHINE_ARCH to
#	reflect the current system.
#	
#	It also sets variables such as MAILER, LOCAL_FS, PS_AXC to hide
#	certain aspects of different UNIX flavours. 
#
# SEE ALSO:
#	site.sh,funcs.sh
#
# AUTHOR:
#	Simon J. Gerraty <sjg@crufty.net>

# RCSid:
#	$Id: os.sh,v 1.55 2017/12/11 20:31:41 sjg Exp $
#
#	@(#) Copyright (c) 1994 Simon J. Gerraty
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

# this lets us skip sourcing it again
_OS_SH=:

OS=`uname`
OSREL=`uname -r`
OSMAJOR=`IFS=.; set $OSREL; echo $1`
MACHINE=`uname -m`
MACHINE_ARCH=`uname -p 2>/dev/null || echo $MACHINE`

# there is at least one case of `uname -p` outputting
# a bunch of usless drivel
case "$MACHINE_ARCH" in
unknown|*[!A-Za-z0-9_-]*) MACHINE_ARCH="$MACHINE";;
esac
        
# we need this here, and it is not always available...
Which() {
	case "$1" in
	-*) t=$1; shift;;
	*) t=-x;;
	esac
	case "$1" in
	/*)	test $t $1 && echo $1;;
	*)
		# some shells cannot correctly handle `IFS`
		# in conjunction with the for loop.
		_dirs=`IFS=:; echo ${2:-$PATH}`
		for d in $_dirs
		do
			test $t $d/$1 && { echo $d/$1; break; }
		done
		;;
	esac
}

# tr is insanely non-portable wrt char classes, so we need to
# spell out the alphabet. sed y/// would work too.
toUpper() {
	${TR:-tr} abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

toLower() {
	${TR:-tr} ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
}

K=
case $OS in
AIX)	# everyone loves to be different...
	OSMAJOR=`uname -v`
	OSREL="$OSMAJOR.`uname -r`"
	LOCAL_FS=jfs
	PS_AXC=-e
	SHARE_ARCH=$OS/$OSMAJOR.X
	;;
SunOS)
	CHOWN=`Which chown /usr/etc:/usr/bin`
	export CHOWN
	
	# Great! Solaris keeps moving arch(1)
	# should just bite the bullet and use uname -p
	arch=`Which arch /usr/bin:/usr/ucb`
	
	MAILER=/usr/ucb/Mail
	LOCAL_FS=4.2
	
	case "$OSREL" in
	4.0*)
		# uname -m just says sun which could be anything
		# so use arch(1).
		MACHINE_ARCH=`arch`
		MACHINE=$MACHINE_ARCH
		;;
	4*)
		MACHINE_ARCH=`arch`
		;;
	5*)
		K=-k
		LOCAL_FS=ufs
		MAILER=mailx
		PS_AXC=-e
		# can you believe that ln on Solaris defaults to
		# overwriting an existing file!!!!! We want one that works!
		test -x /usr/xpg4/bin/ln && LN=${LN:-/usr/xpg4/bin/ln}
		# wonderful, 5.8's tr again require's []'s
		# but /usr/xpg4/bin/tr causes problems if LC_COLLATE is set!
		# use toUpper/toLower instead.
		;;
	esac
	case "$OS/$MACHINE_ARCH" in
	*sun386)	SHARE_ARCH=$MACHINE_ARCH;;
	esac
	;;
*BSD)
	K=-k
	MAILER=/usr/bin/Mail
	LOCAL_FS=local
	: $-,$ENV
	case "$-,$ENV" in
	*i*,*) ;;
	*,|*ENVFILE*) ;;
	*) ENV=;;
	esac
	# NetBSD at least has good backward compatibility
	# so NetBSD/i386 is good enough
	case $OS in
	NetBSD)
	        LOCALBASE=/usr/pkg
		HOST_ARCH=$MACHINE
		SHARE_ARCH=$OS/$HOST_ARCH
		;;
	OpenBSD)
		arch=`Which arch /usr/bin:/usr/ucb:$PATH`
		MACHINE_ARCH=`$arch -s`
		;;
	esac
	NAWK=awk
	export NAWK
	;;
HP-UX)
	TMP_DIRS="/tmp /usr/tmp"
	LOCAL_FS=hfs
	MAILER=mailx
	# don't rely on /bin/sh, its broken
	_shell=/bin/ksh; ENV=
	# also, no one would be interested in OSMAJOR=A
	case "$OSREL" in
	?.09*)	OSMAJOR=9; PS_AXC=-e;;
	?.10*)	OSMAJOR=10; PS_AXC=-e;;
	esac
	;;
IRIX)
	LOCAL_FS=efs
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
	# Not really any such thing as Linux, but
	# this covers red-hat and hopefully others.
	case $MACHINE in
	i?86)	MACHINE_ARCH=i386;; # we don't care about i686 vs i586
	esac
	LOCAL_FS=ext2
	PS_AXC=axc
	[ -x /usr/bin/md5sum ] && { MD5=/usr/bin/md5sum; export MD5; }
	;;
QNX)
	case $MACHINE in
	x86pc)	MACHINE_ARCH=i386;;
	esac
	;;
Haiku)
	case $MACHINE in
	BeBox)	MACHINE_ARCH=powerpc;;
	BeMac)	MACHINE_ARCH=powerpc;;
	BePC)	MACHINE_ARCH=i386;;
	esac
	;;
esac
LOCALBASE=${LOCALBASE:-/usr/local}

HOSTNAME=${HOSTNAME:-`( hostname ) 2>/dev/null`}
HOSTNAME=${HOSTNAME:-`( uname -n ) 2>/dev/null`}
case "$HOSTNAME" in
*.*)	HOST=`IFS=.; set -- $HOSTNAME; echo $1`;;
*)	HOST=$HOSTNAME;;
esac

TMP_DIRS=${TMP_DIRS:-"/tmp /var/tmp"}
MACHINE_ARCH=${MACHINE_ARCH:-$MACHINE}
case "$MACHINE_ARCH" in
x86*64|amd64) MACHINE32_ARCH=i386;;
*64) MACHINE32_ARCH=`echo $MACHINE_ARCH | sed 's,64,32,'`;;
*) MACHINE32_ARCH=$MACHINE_ARCH;;
esac
HOST_ARCH=${HOST_ARCH:-$MACHINE_ARCH}
HOST_ARCH32=${HOST_ARCH32:-$MACHINE32_ARCH}
# we mount server:/share/arch/$SHARE_ARCH as /usr/local
SHARE_ARCH_DEFAULT=$OS/$OSMAJOR.X/$HOST_ARCH
SHARE_ARCH=${SHARE_ARCH:-$SHARE_ARCH_DEFAULT}
LN=${LN:-ln}
TR=${TR:-tr}

# Some people like have /share/$HOST_TARGET/bin etc.
HOST_TARGET=`echo ${OS}${OSMAJOR}-$HOST_ARCH | tr -d / | toLower`
HOST_TARGET32=`echo ${OS}${OSMAJOR}-$HOST_ARCH32 | tr -d / | toLower`
export HOST_TARGET HOST_TARGET32

case `echo -n .` in -n*) N=; C="\c";; *) N=-n; C=;; esac

Echo() {
	case "$1" in
	-n) _n=$N _c=$C; shift;;
	*) _n= _c=;;
	esac
	echo $_n "$@" $_c
}

export HOSTNAME HOST	    
export OS MACHINE MACHINE_ARCH OSREL OSMAJOR LOCAL_FS TMP_DIRS MAILER N C K PS_AXC
export LN SHARE_ARCH TR
export LOCALBASE

case /$0 in
*/os.sh)
	for v in $*
	do
		eval vv=\$$v
		echo "$v='$vv'"
	done
	;;
*/host_target32) echo $HOST_TARGET32;;
*/host_target) echo $HOST_TARGET;;
esac

