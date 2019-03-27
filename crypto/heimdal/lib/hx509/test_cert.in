#!/bin/sh
#
# Copyright (c) 2007 Kungliga Tekniska Högskolan
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
# $Id: test_chain.in 20809 2007-06-03 03:19:06Z lha $
#

srcdir="@srcdir@"
objdir="@objdir@"

hxtool="${TESTS_ENVIRONMENT} ./hxtool ${stat}"
if ${hxtool} info | grep 'rsa: hcrypto null RSA' > /dev/null ; then
    exit 77
fi
if ${hxtool} info | grep 'rand: not available' > /dev/null ; then
    exit 77
fi

echo "print DIR"
${hxtool} print --content DIR:$srcdir/data > /dev/null || exit 1

echo "print FILE"
for a in $srcdir/data/*.crt; do 
    ${hxtool} print --content FILE:"$a" > /dev/null 2>/dev/null
done

echo "print NULL"
${hxtool} print --content NULL: > /dev/null || exit 1

echo "copy dance"
${hxtool} certificate-copy \
    FILE:${srcdir}/data/test.crt PEM-FILE:cert-pem.tmp || exit 1

${hxtool} certificate-copy PEM-FILE:cert-pem.tmp DER-FILE:cert-der.tmp || exit 1
${hxtool} certificate-copy DER-FILE:cert-der.tmp PEM-FILE:cert-pem2.tmp || exit 1

cmp cert-pem.tmp cert-pem2.tmp || exit 1

echo "verify n0ll cert (fail)"
${hxtool} verify --missing-revoke \
	--hostname=foo.com \
	cert:FILE:$srcdir/data/n0ll.pem \
	anchor:FILE:$srcdir/data/n0ll.pem && exit 1

echo "verify n0ll cert (fail)"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/n0ll.pem \
	anchor:FILE:$srcdir/data/n0ll.pem && exit 1

echo "check that windows cert with utf16 in printable string works"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/win-u16-in-printablestring.der \
	anchor:FILE:$srcdir/data/win-u16-in-printablestring.der || exit 1

exit 0
