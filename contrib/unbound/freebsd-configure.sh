#!/bin/sh
#
# $FreeBSD$
#

set -e

error() {
	echo "$@" >&2
	exit 1
}

unbound=$(dirname $(realpath $0))
cd $unbound

# Run autotools before we drop LOCALBASE out of PATH
(cd $unbound && libtoolize --copy && autoheader && autoconf)

# Ensure we use the correct toolchain and clean our environment
export CC=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCC)
export CPP=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCPP)
unset CFLAGS CPPFLAGS LDFLAGS LD_LIBRARY_PATH LIBS
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

ldnssrc=$(realpath $unbound/../ldns)
[ -f $ldnssrc/ldns/ldns.h ] || error "can't find LDNS sources"
export CFLAGS="-I$ldnssrc"

ldnsbld=$(realpath $unbound/../../lib/libldns)
[ -f $ldnsbld/Makefile ] || error "can't find LDNS build directory"

ldnsobj=$(realpath $(make -C$ldnsbld -V.OBJDIR))
[ -f $ldnsobj/libprivateldns.a ] || error "can't find LDNS object directory"
export LDFLAGS="-L$ldnsobj"

cd $unbound
./configure \
	--prefix= --exec-prefix=/usr \
	--with-conf-file=/var/unbound/unbound.conf \
	--with-run-dir=/var/unbound \
	--with-username=unbound
