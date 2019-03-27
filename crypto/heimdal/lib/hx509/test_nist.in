#!/bin/sh
#
# Copyright (c) 2004 - 2005 Kungliga Tekniska Högskolan
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
# $Id$
#

srcdir="@srcdir@"
objdir="@objdir@"
nistdir=${objdir}/PKITS_data
nistzip=${srcdir}/data/PKITS_data.zip

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

echo "nist tests"

if [ ! -d "$nistdir" ] ; then
    ( mkdir "$nistdir" && unzip -d "${nistdir}" "${nistzip}" ) >/dev/null || \
	{ rm -rf "$nistdir" ; exit 1; }
fi

while read id verify cert arg1 arg2 arg3 arg4 arg5 ; do
    expr "$id" : "#" > /dev/null && continue
    
    test "$id" = "end" && break

    args=""
    case "$arg1" in
    *.crt) args="$args chain:FILE:$nistdir/certs/$arg1" ;;
    *.crl) args="$args crl:FILE:$nistdir/crls/$arg1" ;;
    *) args="$args $arg1" ;;
    esac
    case "$arg2" in
    *.crt) args="$args chain:FILE:$nistdir/certs/$arg2" ;;
    *.crl) args="$args crl:FILE:$nistdir/crls/$arg2" ;;
    *) args="$args $arg2" ;;
    esac
    case "$arg3" in
    *.crt) args="$args chain:FILE:$nistdir/certs/$arg3" ;;
    *.crl) args="$args crl:FILE:$nistdir/crls/$arg3" ;;
    *) args="$args $arg3" ;;
    esac
    case "$arg4" in
    *.crt) args="$args chain:FILE:$nistdir/certs/$arg4" ;;
    *.crl) args="$args crl:FILE:$nistdir/crls/$arg4" ;;
    *) args="$args $arg4" ;;
    esac
    case "$arg5" in
    *.crt) args="$args chain:FILE:$nistdir/certs/$arg5" ;;
    *.crl) args="$args crl:FILE:$nistdir/crls/$arg5" ;;
    *) args="$args $arg5" ;;
    esac

    args="$args anchor:FILE:$nistdir/certs/TrustAnchorRootCertificate.crt"
    args="$args crl:FILE:$nistdir/crls/TrustAnchorRootCRL.crl"
    args="$args cert:FILE:$nistdir/certs/$cert"

    if ${hxtool} verify --time=2008-05-20 $args > /dev/null; then
	if test "$verify" = "f"; then
	    echo "verify passed on fail: $id $cert"
	    exit 1
	fi
    else
	if test "$verify" = "p"; then
	    echo "verify failed on pass: $id $cert"
	    exit 1
	fi
    fi

done < $srcdir/data/nist-data


echo "done!"

exit 0
