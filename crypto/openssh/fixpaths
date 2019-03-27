#!/bin/sh
#
# fixpaths  - substitute makefile variables into text files
# Usage: fixpaths -Dsomething=somethingelse ...

die() {
	echo $*
	exit -1
}

test -n "`echo $1|grep -- -D`" || \
	die $0: nothing to do - no substitutions listed!

test -n "`echo $1|grep -- '-D[^=]\+=[^ ]\+'`" || \
	die $0: error in command line arguments.

test -n "`echo $*|grep -- ' [^-]'`" || \
	die Usage: $0 '[-Dstring=replacement] [[infile] ...]'

sed `echo $*|sed -e 's/-D\([^=]\+\)=\([^ ]*\)/-e s=\1=\2=g/g'`

exit 0
