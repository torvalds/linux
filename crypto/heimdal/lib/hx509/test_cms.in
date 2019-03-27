#!/bin/sh
#
# Copyright (c) 2005 Kungliga Tekniska Högskolan
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

if ${hxtool} info | grep 'ecdsa: hcrypto null' > /dev/null ; then
    echo "not testing ECDSA since hcrypto doesnt support ECDSA"
else
    echo "create signed data (ec)"
    ${hxtool} cms-create-sd \
    	--certificate=FILE:$srcdir/data/secp160r2TestClient.pem \
    	"$srcdir/test_chain.in" \
    	sd.data > /dev/null || exit 1
    
    echo "verify signed data (ec)"
    ${hxtool} cms-verify-sd \
    	--missing-revoke \
    	--anchors=FILE:$srcdir/data/secp160r1TestCA.cert.pem \
    	sd.data sd.data.out > /dev/null || exit 1
    cmp "$srcdir/test_chain.in" sd.data.out || exit 1
fi
    
echo "create signed data"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (no signer)"
${hxtool} cms-create-sd \
        --no-signer \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (no signer)"
${hxtool} cms-verify-sd \
	--missing-revoke \
        --no-signer-allowed \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > signer.tmp || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1
grep "unsigned" signer.tmp > /dev/null || exit 1

echo "verify signed data (no signer) (test failure)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out 2> signer.tmp && exit 1
grep "No signers where found" signer.tmp > /dev/null || exit 1

echo "create signed data (id-by-name)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--id-by-name \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "verify signed data (EE cert as anchor)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/test.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (password)"
${hxtool} cms-create-sd \
	--pass=PASS:foobar \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test-pw.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (combined)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.combined.crt \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data  (content info)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--content-info \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (content info)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data  (content type)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--content-type=1.1.1.1 \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (content type)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (pem)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--pem \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (pem)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--pem \
        sd.data sd.data.out > /dev/null
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (pem, detached)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--detached-signature \
	--pem \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (pem, detached)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--pem \
        --signed-content="$srcdir/test_chain.in" \
        sd.data sd.data.out > /dev/null
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (p12)"
${hxtool} cms-create-sd \
	--pass=PASS:foobar \
	--certificate=PKCS12:$srcdir/data/test.p12 \
	--signer=friendlyname-test \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	"$srcdir/data/test-signed-data" sd.data.out > /dev/null || exit 1
cmp "$srcdir/data/static-file" sd.data.out || exit 1

echo "verify signed data (no attr)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	"$srcdir/data/test-signed-data-noattr" sd.data.out > /dev/null || exit 1
cmp "$srcdir/data/static-file" sd.data.out || exit 1

echo "verify failure signed data (no attr, no certs)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	"$srcdir/data/test-signed-data-noattr-nocerts" \
	sd.data.out > /dev/null 2>/dev/null && exit 1

echo "verify signed data (no attr, no certs)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--certificate=FILE:$srcdir/data/test.crt \
	--content-info \
	"$srcdir/data/test-signed-data-noattr-nocerts" \
	sd.data.out > /dev/null || exit 1
cmp "$srcdir/data/static-file" sd.data.out || exit 1

echo "verify signed data - sha1"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	"$srcdir/data/test-signed-sha-1" sd.data.out > /dev/null || exit 1
cmp "$srcdir/data/static-file" sd.data.out || exit 1

echo "verify signed data - sha256"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--content-info \
	"$srcdir/data/test-signed-sha-256" sd.data.out > /dev/null || exit 1
cmp "$srcdir/data/static-file" sd.data.out || exit 1

#echo "verify signed data - sha512"
#${hxtool} cms-verify-sd \
#	--missing-revoke \
#	--anchors=FILE:$srcdir/data/ca.crt \
#	--content-info \
#	"$srcdir/data/test-signed-sha-512" sd.data.out > /dev/null || exit 1
#cmp "$srcdir/data/static-file" sd.data.out || exit 1


echo "create signed data (subcert, no certs)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify failure signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null 2> /dev/null && exit 1

echo "verify success signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--certificate=FILE:$srcdir/data/sub-ca.crt \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (subcert, certs)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	--pool=FILE:$srcdir/data/sub-ca.crt \
	--anchors=FILE:$srcdir/data/ca.crt \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify success signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (subcert, certs, no-root)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	--pool=FILE:$srcdir/data/sub-ca.crt \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify success signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (subcert, no-subca, no-root)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify failure signed data"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null 2>/dev/null && exit 1

echo "create signed data (sd cert)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test-ds-only.crt,$srcdir/data/test-ds-only.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "create signed data (ke cert)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test-ke-only.crt,$srcdir/data/test-ke-only.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null 2>/dev/null && exit 1

echo "create signed data (sd + ke certs)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test-ke-only.crt,$srcdir/data/test-ke-only.key \
	--certificate=FILE:$srcdir/data/test-ds-only.crt,$srcdir/data/test-ds-only.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "create signed data (ke + sd certs)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test-ds-only.crt,$srcdir/data/test-ds-only.key \
	--certificate=FILE:$srcdir/data/test-ke-only.crt,$srcdir/data/test-ke-only.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "create signed data (detached)"
${hxtool} cms-create-sd \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--detached-signature \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (detached)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--signed-content="$srcdir/test_chain.in" \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "verify failure signed data (detached)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null 2>/dev/null && exit 1

echo "create signed data (rsa)"
${hxtool} cms-create-sd \
	--peer-alg=1.2.840.113549.1.1.1 \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	"$srcdir/test_chain.in" \
	sd.data > /dev/null || exit 1

echo "verify signed data (rsa)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	sd.data sd.data.out > /dev/null 2>/dev/null || exit 1
cmp "$srcdir/test_chain.in" sd.data.out || exit 1

echo "create signed data (pem, detached)"
cp "$srcdir/test_chain.in" sd
${hxtool} cms-sign \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--detached-signature \
	--pem \
	sd > /dev/null || exit 1

echo "verify signed data (pem, detached)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:$srcdir/data/ca.crt \
	--pem \
	sd.pem > /dev/null

echo "create signed data (no certs, detached sig)"
cp "$srcdir/test_chain.in" sd
${hxtool} cms-sign \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--detached-signature \
	--no-embedded-certs \
	"$srcdir/data/static-file" \
	sd > /dev/null || exit 1

echo "create signed data (leif only, detached sig)"
cp "$srcdir/test_chain.in" sd
${hxtool} cms-sign \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--detached-signature \
	--embed-leaf-only \
	"$srcdir/data/static-file" \
	sd > /dev/null || exit 1

echo "create signed data (no certs, detached sig, 2 signers)"
cp "$srcdir/test_chain.in" sd
${hxtool} cms-sign \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	--detached-signature \
	--no-embedded-certs \
	"$srcdir/data/static-file" \
	sd > /dev/null || exit 1

echo "create signed data (no certs, detached sig, 3 signers)"
cp "$srcdir/test_chain.in" sd
${hxtool} cms-sign \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--certificate=FILE:$srcdir/data/sub-cert.crt,$srcdir/data/sub-cert.key \
	--certificate=FILE:$srcdir/data/test-ds-only.crt,$srcdir/data/test-ds-only.key \
	--detached-signature \
	--no-embedded-certs \
	"$srcdir/data/static-file" \
	sd > /dev/null || exit 1

echo "envelope data (content-type)"
${hxtool} cms-envelope \
	--certificate=FILE:$srcdir/data/test.crt \
	--content-type=1.1.1.1 \
	"$srcdir/data/static-file" \
	ev.data > /dev/null || exit 1

echo "unenvelope data (content-type)"
${hxtool} cms-unenvelope \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	ev.data ev.data.out \
	FILE:$srcdir/data/test.crt,$srcdir/data/test.key > /dev/null || exit 1
cmp "$srcdir/data/static-file" ev.data.out || exit 1

echo "envelope data (content-info)"
${hxtool} cms-envelope \
	--certificate=FILE:$srcdir/data/test.crt \
	--content-info \
	"$srcdir/data/static-file" \
	ev.data > /dev/null || exit 1

echo "unenvelope data (content-info)"
${hxtool} cms-unenvelope \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--content-info \
	ev.data ev.data.out \
	FILE:$srcdir/data/test.crt,$srcdir/data/test.key > /dev/null || exit 1
cmp "$srcdir/data/static-file" ev.data.out || exit 1

for a in des-ede3 aes-128 aes-256; do

	rm -f ev.data ev.data.out
	echo "envelope data ($a)"
	${hxtool} cms-envelope \
	        --encryption-type="$a-cbc" \
		--certificate=FILE:$srcdir/data/test.crt \
		"$srcdir/data/static-file" \
		ev.data  || exit 1

	echo "unenvelope data ($a)"
	${hxtool} cms-unenvelope \
		--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
		ev.data ev.data.out > /dev/null || exit 1
	cmp "$srcdir/data/static-file" ev.data.out || exit 1
done

for a in rc2-40 rc2-64 rc2-128 des-ede3 aes-128 aes-256; do
    echo "static unenvelope data ($a)"

    rm -f ev.data.out
    ${hxtool} cms-unenvelope \
	--certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	--content-info \
	--allow-weak \
	"$srcdir/data/test-enveloped-$a" ev.data.out > /dev/null || exit 1
    cmp "$srcdir/data/static-file" ev.data.out || exit 1
done

exit 0
