#!/bin/sh
#
# Copyright (c) 2004 - 2008 Kungliga Tekniska Högskolan
# (Royal Institute of Technology, Stockholm, Sweden). 
# All rights reserved. 
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions 
# are met: 
#
# 1. Redistributions of source code must retain the above copyright 
#    notice, this list of conditions and the following disclaimer. 
#
# 2. Redistributions in binary form must reproduce the above copyright 
#    notice, this list of conditions and the following disclaimer in the 
#    documentation and/or other materials provided with the distribution. 
#
# 3. Neither the name of the Institute nor the names of its contributors 
#    may be used to endorse or promote products derived from this software 
#    without specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
# ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
# SUCH DAMAGE. 
#
# $Id: test_nist.in 21787 2007-08-02 08:50:24Z lha $
#

srcdir="@srcdir@"
objdir="@objdir@"
nistdir=${objdir}/PKITS_data
nistzip=${srcdir}/data/PKITS_data.zip
egrep="@egrep@"

limit="${1:-nolimit}"

stat="--statistic-file=${objdir}/statfile"

hxtool="${TESTS_ENVIRONMENT} ./hxtool ${stat}"

# nistzip is not distributed part of the distribution
test -f "$nistzip" || exit 77

if ${hxtool} info | grep 'rsa: hcrypto null RSA' > /dev/null ; then
    exit 77
fi
if ${hxtool} info | grep 'rand: not available' > /dev/null ; then
    exit 77
fi

#--------- Try to find unzip

oldifs=$IFS
IFS=:
set -- $PATH
IFS=$oldifs
found=

for p in "$@" ; do
    test -x "$p/unzip" && { found=1 ; break; }
done
test "X$found" = "X" && exit 77

#---------


echo "nist tests, version 2"

if [ ! -d "$nistdir" ] ; then
    ( mkdir "$nistdir" && unzip -d "${nistdir}" "${nistzip}" ) >/dev/null || \
	{ rm -rf "$nistdir" ; exit 1; }
fi

ec=
name=
description=
while read result cert other ; do
    if expr "$result" : "#" > /dev/null; then
	name=${cert}
	description="${other}"
	continue
    fi
    
    test nolimit != "${limit}" && ! expr "$name" : "$limit" > /dev/null && continue

    test "$result" = "end" && break

    args=
    args="$args cert:FILE:$nistdir/certs/$cert"
    args="$args chain:DIR:$nistdir/certs"
    args="$args anchor:FILE:$nistdir/certs/TrustAnchorRootCertificate.crt"

    for a in $nistdir/crls/*.crl; do
	args="$args crl:FILE:$a"
    done

    cmd="${hxtool} verify --time=2008-05-20 $args"
    eval ${cmd} > /dev/null
    res=$?

    case "${result},${res}" in
    0,0) r="PASSs";;
    0,*) r="FAILs";;
    [123],0) r="FAILf";;
    [123],*) r="PASSf";;
    *) echo="unknown result ${result},${res}" ; exit 1 ;;
    esac
    if ${egrep} "^${name} FAIL" $srcdir/data/nist-result2 > /dev/null; then
	if expr "$r" : "PASS" >/dev/null; then
	    echo "${name} passed when expected not to"
	    echo "# ${description}" > nist2-passed-${name}.tmp
	    ec=1
	fi
    elif ${egrep} "^${name} EITHER" $srcdir/data/nist-result2 > /dev/null; then
	:
    elif expr "$r" : "FAIL.*" >/dev/null ; then
	echo "$r ${name} ${description}"
	echo "# ${description}" > nist2-failed-${name}.tmp
	echo "$cmd" >> nist2-failed-${name}.tmp
	ec=1
    fi

done < $srcdir/data/nist-data2


echo "done!"

exit $ec
