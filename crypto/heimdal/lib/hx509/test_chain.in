#!/bin/sh
#
# Copyright (c) 2004 - 2006 Kungliga Tekniska Högskolan
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

echo "cert -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/test.crt \
	chain:FILE:$srcdir/data/test.crt \
	chain:FILE:$srcdir/data/ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "cert -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/test.crt \
	chain:FILE:$srcdir/data/ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "cert -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/test.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "sub-cert -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "sub-cert -> sub-ca -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	chain:FILE:$srcdir/data/ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "sub-cert -> sub-ca"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	anchor:FILE:$srcdir/data/sub-ca.crt > /dev/null || exit 1

echo "sub-cert -> sub-ca -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	chain:FILE:$srcdir/data/ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "sub-cert -> sub-ca -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/ca.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "sub-cert -> sub-ca -> root"
${hxtool} verify --missing-revoke \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "max depth 2 (ok)"
${hxtool} verify --missing-revoke \
	--max-depth=2 \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "max depth 1 (fail)"
${hxtool} verify --missing-revoke \
	--max-depth=1 \
	cert:FILE:$srcdir/data/sub-cert.crt \
	chain:FILE:$srcdir/data/sub-ca.crt \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "ocsp non-ca responder"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp1-ocsp.der > /dev/null || exit 1

echo "ocsp ca responder"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp1-ca.der > /dev/null || exit 1

echo "ocsp no-ca responder, missing cert"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp1-ocsp-no-cert.der > /dev/null && exit 1

echo "ocsp no-ca responder, missing cert, in pool"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp1-ocsp-no-cert.der \
    chain:FILE:$srcdir/data/ocsp-responder.crt > /dev/null || exit 1

echo "ocsp no-ca responder, keyHash"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp1-keyhash.der > /dev/null || exit 1

echo "ocsp revoked cert"
${hxtool} verify \
    cert:FILE:$srcdir/data/revoke.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    ocsp:FILE:$srcdir/data/ocsp-resp2.der > /dev/null && exit 1

for a in resp1-ocsp-no-cert resp1-ca resp1-keyhash resp2 ; do
	echo "ocsp print reply $a"
	${hxtool} ocsp-print \
	    $srcdir/data/ocsp-${a}.der > /dev/null || exit 1
done

echo "ocsp verify exists"
${hxtool} ocsp-verify \
	--ocsp-file=$srcdir/data/ocsp-resp1-ca.der \
	FILE:$srcdir/data/test.crt > /dev/null || exit 1

echo "ocsp verify not exists"
${hxtool} ocsp-verify \
    --ocsp-file=$srcdir/data/ocsp-resp1.der \
	FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "ocsp verify revoked"
${hxtool} ocsp-verify \
    --ocsp-file=$srcdir/data/ocsp-resp2.der \
	FILE:$srcdir/data/revoke.crt > /dev/null && exit 1

echo "crl non-revoked cert"
${hxtool} verify \
    cert:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    crl:FILE:$srcdir/data/crl1.der > /dev/null || exit 1

echo "crl revoked cert"
${hxtool} verify \
    cert:FILE:$srcdir/data/revoke.crt \
    anchor:FILE:$srcdir/data/ca.crt \
    crl:FILE:$srcdir/data/crl1.der > /dev/null && exit 1

if ${hxtool} info | grep 'ecdsa: hcrypto null' > /dev/null ; then
    echo "not testing ECDSA since hcrypto doesnt support ECDSA"
else
    echo "eccert -> root"
    ${hxtool} verify --missing-revoke \
    	cert:FILE:$srcdir/data/secp160r2TestServer.cert.pem \
    	anchor:FILE:$srcdir/data/secp160r1TestCA.cert.pem > /dev/null || exit 1
    
    echo "eccert -> root"
    ${hxtool} verify --missing-revoke \
    	cert:FILE:$srcdir/data/secp160r2TestClient.cert.pem \
    	anchor:FILE:$srcdir/data/secp160r1TestCA.cert.pem > /dev/null || exit 1
fi

echo "proxy cert"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/proxy-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "proxy cert (negative)"
${hxtool} verify --missing-revoke \
    cert:FILE:$srcdir/data/proxy-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "proxy cert (level fail)"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/proxy-level-test.crt \
    chain:FILE:$srcdir/data/proxy-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "not a proxy cert"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/no-proxy-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "proxy cert (max level 10)"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/proxy10-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "proxy cert (second level)"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/proxy10-child-test.crt \
    chain:FILE:$srcdir/data/proxy10-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "proxy cert (third level)"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:$srcdir/data/proxy10-child-child-test.crt \
    chain:FILE:$srcdir/data/proxy10-child-test.crt \
    chain:FILE:$srcdir/data/proxy10-test.crt \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

exit 0
