#!/bin/sh
#
# $NetBSD: install-sh.in,v 1.6 2012/01/11 13:07:31 hans Exp $
# This script now also installs multiple files, but might choke on installing
# multiple files with spaces in the file names.
#
# install - install a program, script, or datafile
# This comes from X11R5 (mit/util/scripts/install.sh).
#
# Copyright 1991 by the Massachusetts Institute of Technology
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation, and that the name of M.I.T. not be used in advertising or
# publicity pertaining to distribution of the software without specific,
# written prior permission.  M.I.T. makes no representations about the
# suitability of this software for any purpose.  It is provided "as is"
# without express or implied warranty.
#
# Calling this script install-sh is preferred over install.sh, to prevent
# `make' implicit rules from creating a file called install from it
# when there is no Makefile.
#
# This script is compatible with the BSD install script, but was written
# from scratch.

# set DOITPROG to echo to test this script

# Don't use :- since 4.3BSD and earlier shells don't like it.
doit="${DOITPROG-}"


# put in absolute paths if you don't have them in your path; or use env. vars.

awkprog="${AWKPROG-awk}"
mvprog="${MVPROG-mv}"
cpprog="${CPPROG-cp}"
chmodprog="${CHMODPROG-chmod}"
chownprog="${CHOWNPROG-chown}"
chgrpprog="${CHGRPPROG-chgrp}"
stripprog="${STRIPPROG-strip}"
rmprog="${RMPROG-rm}"
mkdirprog="${MKDIRPROG-mkdir}"

instcmd="$cpprog"
instflags=""
pathcompchmodcmd="$chmodprog 755"
chmodcmd="$chmodprog 755"
chowncmd=""
chgrpcmd=""
stripcmd=""
stripflags=""
rmcmd="$rmprog -f"
mvcmd="$mvprog"
src=""
msrc=""
dst=""
dir_arg=""
suffix=""
suffixfmt=""

while [ x"$1" != x ]; do
    case $1 in
	-b) suffix=".old"
	    shift
	    continue;;

	-B) suffixfmt="$2"
	    shift
	    shift
	    continue;;

	-c) instcmd="$cpprog"
	    shift
	    continue;;

	-d) dir_arg=true
	    shift
	    continue;;

	-m) chmodcmd="$chmodprog $2"
	    shift
	    shift
	    continue;;

	-m*)
	    chmodcmd="$chmodprog ${1#-m}"
	    shift
	    continue;;

	-o) chowncmd="$chownprog $2"
	    shift
	    shift
	    continue;;

	-g) chgrpcmd="$chgrpprog $2"
	    shift
	    shift
	    continue;;

	-s) stripcmd="$stripprog"
	    shift
	    continue;;

	-S) stripcmd="$stripprog"
	    stripflags="-S $2 $stripflags"
	    shift
	    shift
	    continue;;

	-p) instflags="-p"
	    shift
	    continue;;

	*)  if [ x"$msrc" = x ]
	    then
		msrc="$dst"
	    else
		msrc="$msrc $dst"
	    fi
	    src="$dst"
	    dst="$1"
	    shift
	    continue;;
    esac
done

if [ x"$dir_arg" = x ]
then
	dstisfile=""
	if [ ! -d "$dst" ]
	then
		if [ x"$msrc" = x"$src" ]
		then
			dstisfile=true
		else
			echo "install: destination is not a directory"
			exit 1
		fi
	fi
else
	msrc="$msrc $dst"
fi

if [ x"$msrc" = x ]
then
	echo "install: no destination specified"
	exit 1
fi      

for srcarg in $msrc; do

if [ x"$dir_arg" != x ]; then

	dstarg="$srcarg"
else
	dstarg="$dst"

# Waiting for this to be detected by the "$instcmd $srcarg $dsttmp" command
# might cause directories to be created, which would be especially bad 
# if $src (and thus $dsttmp) contains '*'.

	if [ -f "$srcarg" ]
	then
		doinst="$instcmd $instflags"
	elif [ -d "$srcarg" ]
	then
		echo "install: $srcarg: not a regular file"
		exit 1
	elif [ "$srcarg" = "/dev/null" ]
	then
		doinst="$cpprog"
	else
		echo "install:  $srcarg does not exist"
		exit 1
	fi
	
# If destination is a directory, append the input filename; if your system
# does not like double slashes in filenames, you may need to add some logic

	if [ -d "$dstarg" ]
	then
		dstarg="$dstarg"/`basename "$srcarg"`
	fi
fi

## this sed command emulates the dirname command
dstdir=`echo "$dstarg" | sed -e 's,[^/]*$,,;s,/$,,;s,^$,.,'`

# Make sure that the destination directory exists.
#  this part is taken from Noah Friedman's mkinstalldirs script

# Skip lots of stat calls in the usual case.
if [ ! -d "$dstdir" ]; then
defaultIFS='	
'
IFS="${IFS-${defaultIFS}}"

oIFS="${IFS}"
# Some sh's can't handle IFS=/ for some reason.
IFS='%'
set - `echo ${dstdir} | sed -e 's@/@%@g' -e 's@^%@/@'`
IFS="${oIFS}"

pathcomp=''

while [ $# -ne 0 ] ; do
	pathcomp="${pathcomp}${1}"
	shift

	if [ ! -d "${pathcomp}" ] ;
        then
		$doit $mkdirprog "${pathcomp}"
        	if [ x"$chowncmd" != x ]; then $doit $chowncmd "${pathcomp}"; else true ; fi &&
        	if [ x"$chgrpcmd" != x ]; then $doit $chgrpcmd "${pathcomp}"; else true ; fi &&
        	if [ x"$pathcompchmodcmd" != x ]; then $doit $pathcompchmodcmd "${pathcomp}"; else true ; fi

	else
		true
	fi

	pathcomp="${pathcomp}/"
done
fi

	if [ x"$dir_arg" != x ]
	then
		if [ -d "$dstarg" ]; then
			true
		else
			$doit $mkdirprog "$dstarg" &&

			if [ x"$chowncmd" != x ]; then $doit $chowncmd "$dstarg"; else true ; fi &&
			if [ x"$chgrpcmd" != x ]; then $doit $chgrpcmd "$dstarg"; else true ; fi &&
			if [ x"$chmodcmd" != x ]; then $doit $chmodcmd "$dstarg"; else true ; fi
		fi
	else

		if [ x"$dstisfile" = x ]
		then
			file=$srcarg
		else
			file=$dst
		fi

		dstfile=`basename "$file"`
		dstfinal="$dstdir/$dstfile"

# Make a temp file name in the proper directory.

		dsttmp=$dstdir/#inst.$$#

# Make a backup file name in the proper directory.
		case x$suffixfmt in
		*%*)	suffix=`echo x |
			$awkprog -v bname="$dstfinal" -v fmt="$suffixfmt" '
			{ cnt = 0;
			  do {
				sfx = sprintf(fmt, cnt++);
				name = bname sfx;
			  } while (system("test -f " name) == 0);
			  print sfx; }' -`;;
		x)	;;
		*)	suffix="$suffixfmt";;
		esac
		dstbackup="$dstfinal$suffix"

# Move or copy the file name to the temp name

		$doit $doinst $srcarg "$dsttmp" &&

		trap "rm -f ${dsttmp}" 0 &&

# and set any options; do chmod last to preserve setuid bits

# If any of these fail, we abort the whole thing.  If we want to
# ignore errors from any of these, just make sure not to ignore
# errors from the above "$doit $instcmd $src $dsttmp" command.

		if [ x"$chowncmd" != x ]; then $doit $chowncmd "$dsttmp"; else true;fi &&
		if [ x"$chgrpcmd" != x ]; then $doit $chgrpcmd "$dsttmp"; else true;fi &&
		if [ x"$stripcmd" != x ]; then $doit $stripcmd $stripflags "$dsttmp"; else true;fi &&
		if [ x"$chmodcmd" != x ]; then $doit $chmodcmd "$dsttmp"; else true;fi &&

# Now rename the file to the real destination.

		if [ x"$suffix" != x ] && [ -f "$dstfinal" ]
		then
			$doit $mvcmd "$dstfinal" "$dstbackup"
		else
			$doit $rmcmd -f "$dstfinal"
		fi &&
		$doit $mvcmd "$dsttmp" "$dstfinal"
	fi

done &&


exit 0
