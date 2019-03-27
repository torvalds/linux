#!/bin/sh
#
# Copyright (c) 2006 Kungliga Tekniska Högskolan
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

stat="--statistic-file=${objdir}/statfile"

hxtool="${TESTS_ENVIRONMENT} ./hxtool ${stat}"

if ${hxtool} info | grep 'rsa: hcrypto null RSA' > /dev/null ; then
    exit 77
fi
if ${hxtool} info | grep 'rand: not available' > /dev/null ; then
    exit 77
fi


echo "Bleichenbacher good cert (from eay)"
${hxtool} verify --missing-revoke \
    --time=2006-09-25 \
    cert:FILE:$srcdir/data/bleichenbacher-good.pem \
    anchor:FILE:$srcdir/data/bleichenbacher-good.pem > /dev/null || exit 1

echo "Bleichenbacher bad cert (from eay)"
${hxtool} verify --missing-revoke \
    --time=2006-09-25 \
    cert:FILE:$srcdir/data/bleichenbacher-bad.pem \
    anchor:FILE:$srcdir/data/bleichenbacher-bad.pem > /dev/null && exit 1

echo "Bleichenbacher good cert (from yutaka)"
${hxtool} verify --missing-revoke \
    --time=2006-09-25 \
    cert:FILE:$srcdir/data/yutaka-pad-ok-cert.pem \
    anchor:FILE:$srcdir/data/yutaka-pad-ok-ca.pem > /dev/null || exit 1

echo "Bleichenbacher bad cert (from yutaka)"
${hxtool} verify --missing-revoke \
    --time=2006-09-25 \
    cert:FILE:$srcdir/data/yutaka-pad-broken-cert.pem \
    anchor:FILE:$srcdir/data/yutaka-pad-broken-ca.pem > /dev/null && exit 1

# Ralf-Philipp Weinmann <weinmann@cdc.informatik.tu-darmstadt.de>
# Andrew Pyshkin <pychkine@cdc.informatik.tu-darmstadt.de>
echo "Bleichenbacher bad cert (sf pad correct)"
${hxtool} verify --missing-revoke \
    --time=2006-09-25 \
    cert:FILE:$srcdir/data/bleichenbacher-sf-pad-correct.pem \
    anchor:FILE:$srcdir/data/sf-class2-root.pem > /dev/null && exit 1

echo Read 50 kilobyte random data
${hxtool} random-data 50kilobyte > random-data || exit 1

echo "crypto select1"
${hxtool} crypto-select > test || { echo "select1"; exit 1; }
cmp test ${srcdir}/tst-crypto-select1 > /dev/null || \
	{ echo "select1 failure"; exit 1; }

echo "crypto select1"
${hxtool} crypto-select --type=digest > test || { echo "select1"; exit 1; }
cmp test ${srcdir}/tst-crypto-select1 > /dev/null || \
	{ echo "select1 failure"; exit 1; }

echo "crypto select2"
${hxtool} crypto-select --type=public-sig > test || { echo "select2"; exit 1; }
cmp test ${srcdir}/tst-crypto-select2 > /dev/null || \
	{ echo "select2 failure"; exit 1; }

echo "crypto select3"
${hxtool} crypto-select \
	--type=public-sig \
	--peer-cmstype=1.2.840.113549.1.1.4 \
	 > test || { echo "select3"; exit 1; }
cmp test ${srcdir}/tst-crypto-select3 > /dev/null || \
	{ echo "select3 failure"; exit 1; }

echo "crypto select4"
${hxtool} crypto-select \
	--type=public-sig \
	--peer-cmstype=1.2.840.113549.1.1.5 \
	--peer-cmstype=1.2.840.113549.1.1.4 \
	 > test || { echo "select4"; exit 1; }
cmp test ${srcdir}/tst-crypto-select4 > /dev/null || \
	{ echo "select4 failure"; exit 1; }

echo "crypto select5"
${hxtool} crypto-select \
	--type=public-sig \
	--peer-cmstype=1.2.840.113549.1.1.11 \
	--peer-cmstype=1.2.840.113549.1.1.5 \
	 > test || { echo "select5"; exit 1; }
cmp test ${srcdir}/tst-crypto-select5 > /dev/null || \
	{ echo "select5 failure"; exit 1; }

echo "crypto select6"
${hxtool} crypto-select \
	--type=public-sig \
	--peer-cmstype=1.2.840.113549.2.5 \
	--peer-cmstype=1.2.840.113549.1.1.5 \
	 > test || { echo "select6"; exit 1; }
cmp test ${srcdir}/tst-crypto-select6 > /dev/null || \
	{ echo "select6 failure"; exit 1; }

echo "crypto select7"
${hxtool} crypto-select \
	--type=secret \
	--peer-cmstype=2.16.840.1.101.3.4.1.42 \
	--peer-cmstype=1.2.840.113549.3.7 \
	--peer-cmstype=1.2.840.113549.1.1.5 \
	 > test || { echo "select7"; exit 1; }
cmp test ${srcdir}/tst-crypto-select7 > /dev/null || \
	{ echo "select7 failure"; exit 1; }

#echo "crypto available1"
#${hxtool} crypto-available \
#	--type=all \
#	> test || { echo "available1"; exit 1; }
#cmp test ${srcdir}/tst-crypto-available1 > /dev/null || \
#	{ echo "available1 failure"; exit 1; }

echo "crypto available2"
${hxtool} crypto-available \
	--type=digest \
	> test || { echo "available2"; exit 1; }
cmp test ${srcdir}/tst-crypto-available2 > /dev/null || \
	{ echo "available2 failure"; exit 1; }

#echo "crypto available3"
#${hxtool} crypto-available \
#	--type=public-sig \
#	> test || { echo "available3"; exit 1; }
#cmp test ${srcdir}/tst-crypto-available3 > /dev/null || \
#	{ echo "available3 failure"; exit 1; }

echo "copy keystore FILE existing -> FILE"
${hxtool} certificate-copy \
    FILE:${srcdir}/data/test.crt,${srcdir}/data/test.key \
    FILE:out.pem || exit 1

echo "copy keystore FILE -> FILE"
${hxtool} certificate-copy \
    FILE:out.pem \
    FILE:out2.pem || exit 1

echo "copy keystore FILE -> PKCS12"
${hxtool} certificate-copy \
    FILE:out.pem \
    PKCS12:out2.pem || exit 1

echo "print certificate with utf8"
${hxtool} print \
	FILE:$srcdir/data/j.pem >/dev/null 2>/dev/null || exit 1

echo "Make sure that we can parse EC private keys"
${hxtool} print --content \
    FILE:$srcdir/data/pkinit-ec.crt,$srcdir/data/pkinit-ec.key \
    > /dev/null || exit 1

exit 0
