#!/bin/sh
#
# $FreeBSD$
#

configure_args="
    --prefix=/usr
    --sysconfdir=/etc/ssh
    --with-pam
    --with-ssl-dir=/usr
    --with-tcp-wrappers
    --with-libedit
    --with-ssl-engine
    --without-xauth
"

set -e

openssh=$(dirname $(realpath $0))
cd $openssh

# Run autotools before we drop LOCALBASE out of PATH
(cd $openssh && libtoolize --copy && autoheader && autoconf)

# Ensure we use the correct toolchain and clean our environment
export CC=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCC)
export CPP=$(echo ".include <bsd.lib.mk>" | make -f /dev/stdin -VCPP)
unset CFLAGS CPPFLAGS LDFLAGS LD_LIBRARY_PATH LIBS
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# Generate config.h with krb5 and stash it
sh configure $configure_args --with-kerberos5=/usr
mv config.log config.log.orig
mv config.h config.h.orig

# Generate config.h without krb5
sh configure $configure_args --without-kerberos5

# Extract the difference
echo '/* $Free''BSD$ */' > krb5_config.h
diff -u config.h.orig config.h |
	sed -n '/^-#define/s/^-//p' |
	grep -Ff /dev/stdin config.h.orig >> krb5_config.h
