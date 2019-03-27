#! /bin/sh

# Copyright 2002, 2009, 2010 Harlan Stenn.  Used by NTP with permission.
# Author: Harlan Stenn <harlan+cvo@pfcs.com>

# Possible output formats:
#
# CVO=...		Shell variable
# CVO=... ; export CVO	Old-style exported envariable
# export CVO=...	New-style exported envariable
# setenv CVO ...	csh-style exported envariable

TS="$*"

CVO_raw=`config.guess`
CVO=$CVO_raw

set 1 `echo $CVO | tr '-' ' '`
shift

case "$#" in
 4) # eg: i686-pc-linux-gnu
    CVO_CPU=$1
    CVO_VENDOR=$2
    cvo_KERN=$3			# Note the capitalization...
    CVO_OS=$4
    case "$cvo_KERN" in
     linux)			# Braindamage.  We want OS, not kernel info
	if lsb_release > /dev/null 2>&1
	then
	    CVO_OS=`lsb_release --id --short | tr '[:upper:]' '[:lower:]'`
	    CVO_OS="$CVO_OS`lsb_release --release --short`"
	elif test -f /etc/debian_version
	then
	    set `cat /etc/debian_version`
	    CVO_OS=debian$1
	    CVO_KOSVER=`uname -r`
	elif test -f /etc/mandrake-release
	then
	    set `cat /etc/mandrake-release`
	    CVO_OS=mandrake$4
	    CVO_KOSVER=`uname -r`
	elif test -f /etc/redhat-release
	then
	    set `cat /etc/redhat-release`
	    case "$1" in
	     CentOS)
		case "$2" in
		 Linux)
		    CVO_OS=centos$4
		    ;;
		 *) CVO_OS=centos$3
		    ;;
		esac
	        ;;
	     Fedora)
	        CVO_OS=fedora$3
	        ;;
	    *)
		case "$3" in
		 Enterprise)
		    CVO_OS=redhat$7.E
		    ;;
		 Linux)
		    CVO_OS=redhat$5
		    ;;
		esac
		;;
	    esac
	    CVO_KOSVER=`uname -r`
	elif test -f /etc/slackware-version
	then
	    set `cat /etc/slackware-version`
	    CVO_OS=slackware$2
	    CVO_KOSVER=`uname -r`
	elif test -f /etc/SuSE-release
	then
	    set `cat /etc/SuSE-release`
	    CVO_OS=suse$9
	    CVO_KOSVER=`uname -r`
	else
	    CVO_OS=$cvo_KERN`uname -r`

	fi
	;;
     nto)	# QNX
	CVO_KOSVER=`uname -r`
	;;
     *)
	echo "gronk - I don't understand <$CVO>!"
	exit 1
	;;
    esac
    ;;
 3) CVO_CPU=$1
    CVO_VENDOR=$2
    CVO_OS=$3
    ;;
 *) echo "gronk - config.guess returned $# pieces, not 3 pieces!"
    exit 1
    ;;
esac

case "$CVO_OS" in
 cygwin)
    # Decisions, decisions.
    # uname -r is the cygwin version #, eg: 1.3.3(0.46/3/2)
    # uname -s returns something like CYGWIN_NT-5.0
    CVO_OS="$CVO_OS`uname -r | sed 's/(.*//'`"
    ;;
esac
set `echo $CVO_OS | sed 's/\([0-9]\)/ \1/'`

case "$#" in
 2) ;;
 *) echo "gronk - <$CVO_OS> expanded to $#, not 2 pieces!"
    exit 1
    ;;
esac

CVO_OSNAME=$1
CVO_OSVER=$2

case "$CVO_OSNAME" in
 solaris)
    CVO_KOSVER=`uname -v`
    ;;
esac

CVO=$CVO_CPU-$CVO_VENDOR-$CVO_OS

case "$TS" in
 '')
    set | grep CVO
    ;;
 *)
    # keys['cvo'] = "cvo.CVO['CVO']"
    TS=`echo $TS | sed -e s/@cvo@/$CVO/g`
    # keys['cpu'] = "cvo.CVO['CVO_CPU']"
    TS=`echo $TS | sed -e s/@cpu@/$CVO_CPU/g`
    # keys['kosver'] = "cvo.CVO['CVO_KOSVER']"
    TS=`echo $TS | sed -e s/@kosver@/$CVO_KOSVER/g`
    # keys['os'] = "cvo.CVO['CVO_OS']"
    TS=`echo $TS | sed -e s/@os@/$CVO_OS/g`
    # keys['osname'] = "cvo.CVO['CVO_OSNAME']"
    TS=`echo $TS | sed -e s/@osname@/$CVO_OSNAME/g`
    # keys['osver'] = "cvo.CVO['CVO_OSVER']"
    TS=`echo $TS | sed -e s/@osver@/$CVO_OSVER/g`
    # keys['vendor'] = "cvo.CVO['CVO_VENDOR']"
    TS=`echo $TS | sed -e s/@vendor@/$CVO_VENDOR/g`
    # keys['raw'] = "cvo.CVO['CVO_raw']"
    TS=`echo $TS | sed -e s/@raw@/$CVO_raw/g`

    echo $TS
    ;;
esac
