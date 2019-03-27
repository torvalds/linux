#!/bin/sh
#
# $FreeBSD$
#

set -e

error() {
	echo "$@" >&2
	exit 1
}

ldns=$(dirname $(realpath $0))
cd $ldns

# Run autotools before we drop LOCALBASE out of PATH
(cd $ldns && libtoolize --copy && autoheader && autoconf)
(cd $ldns/drill && aclocal && autoheader && autoconf)

# Ensure we use the correct toolchain and clean our environment
export CC=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCC)
export CPP=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCPP)
unset CFLAGS CPPFLAGS LDFLAGS LD_LIBRARY_PATH LIBS
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

cd $ldns
./configure --prefix= --exec-prefix=/usr

cd $ldns/drill
./configure --prefix= --exec-prefix=/usr
