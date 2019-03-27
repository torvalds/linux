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
pass="--pass=PASS:password"
nistdir=${objdir}/PKITS_data
nistzip=${srcdir}/data/PKITS_data.zip

# nistzip is not distributed part of the distribution
test -f "$nistzip" || exit 77

stat="--statistic-file=${objdir}/statfile"

hxtool="${TESTS_ENVIRONMENT} ./hxtool ${stat}"

if ${hxtool} info | grep 'rsa: hcrypto null RSA' > /dev/null ; then
    exit 77
fi
if ${hxtool} info | grep 'rand: not available' > /dev/null ; then
    exit 77
fi

if [ ! -d "$nistdir" ] ; then
    ( mkdir "$nistdir" &&  cd "$nistdir" && unzip "$nistzip" ) >/dev/null || \
	{ rm -rf "$nistdir" ; exit 1; }
fi

echo "nist pkcs12 tests"

for a in $nistdir/pkcs12/*.p12 ; do

    if ${hxtool} validate $pass PKCS12:$a > /dev/null; then
	:
    else
	echo "$a failed"
	exit 1
    fi

done

echo "done!"

exit 0