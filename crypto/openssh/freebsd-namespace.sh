#!/bin/sh
#
# Namespace munging inspired by an equivalent hack in NetBSD's tree: add
# the "Fssh_" prefix to every symbol in libssh which doesn't already have
# it.  This prevents collisions between symbols in libssh and symbols in
# other libraries or applications which link with libssh, either directly
# or indirectly (e.g. through PAM loading pam_ssh).
#
# $FreeBSD$
#

set -e

eval "unset $(env | sed -nE 's/^(LC_[A-Z]+)=.*$/\1/p')"
export LANG=C

error() {
	echo "$@" >&2
	exit 1
}

# Locate the source directories
self=$(realpath ${0})
srcdir=${self%/*}
header=${srcdir}/ssh_namespace.h
top_srcdir=${srcdir%/crypto/openssh}
libssh_srcdir=${top_srcdir}/secure/lib/libssh

if [ ! -d ${srcdir} -o \
     ! -f ${header} -o \
     ! -d ${libssh_srcdir} -o \
     ! -f ${libssh_srcdir}/Makefile ] ; then
	error "Where is the libssh Makefile?"
fi

ncpu=$(sysctl -n hw.ncpu)
ssh_make() {
	make -C${libssh_srcdir} -j$((ncpu + 1)) "$@"
}

# Clear out, recreate and locate the libssh build directory
ssh_make cleandir
ssh_make cleandir
ssh_make obj
libssh_builddir=$(realpath $(ssh_make -V.OBJDIR))
libssh=libprivatessh.a

# Clear the existing header
cat >${header} <<EOF
/*
 * This file was machine-generated.  Do not edit manually.
 * Run crypto/openssh/freebsd-namespace.sh to regenerate.
 */
EOF

# Build libssh
ssh_make depend
ssh_make ${libssh}
if [ ! -f ${libssh_builddir}/${libssh} ] ; then
	error "Where is ${libssh}?"
fi

# Extract symbols
nm ${libssh_builddir}/${libssh} | awk '
     /^[0-9a-z]+ [Tt] [A-Za-z_][0-9A-Za-z_]*$/ && $3 !~ /^Fssh_/ {
         printf("#define %-39s Fssh_%s\n", $3, $3)
     }
' | unexpand -a | sort -u >>${header}

# Clean and rebuild the library
ssh_make clean
ssh_make ${libssh}

# Double-check
nm ${libssh_builddir}/${libssh} | awk '
    /^[0-9a-z]+ [Tt] [A-Za-z_][0-9A-Za-z_]*$/ && $3 !~ /^Fssh_/ {
         printf("ERROR: %s was not renamed!\n", $3);
         err++;
    }
    END {
        if (err > 0)
            exit(1);
    }
'
