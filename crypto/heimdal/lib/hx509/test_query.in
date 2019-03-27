#!/bin/sh
#
# Copyright (c) 2005 - 2008 Kungliga Tekniska Högskolan
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

echo "try printing"
${hxtool} print \
	--pass=PASS:foobar \
        --info --content \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null || exit 1

echo "try printing"
${hxtool} print \
	--pass=PASS:foobar \
        --info --content \
	FILE:$srcdir/data/kdc.crt  >/dev/null 2>/dev/null || exit 1

${hxtool} print \
	--pass=PASS:foobar \
	--info \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null || exit 1

echo "make sure entry is found (friendlyname)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=friendlyname-test  \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null || exit 1

echo "make sure entry is not found  (friendlyname)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=friendlyname-test-not  \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (eku)"
${hxtool} query \
	--eku=1.3.6.1.5.2.3.5  \
	FILE:$srcdir/data/kdc.crt  >/dev/null 2>/dev/null || exit 1

echo "make sure entry is not found  (eku)"
${hxtool} query \
	--eku=1.3.6.1.5.2.3.6  \
	FILE:$srcdir/data/kdc.crt >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (friendlyname, no-pw)"
${hxtool} query \
	--friendlyname=friendlyname-cert  \
	PKCS12:$srcdir/data/test-nopw.p12 >/dev/null 2>/dev/null || exit 1

echo "check for ca cert (friendlyname)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=ca  \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null || exit 1

echo "make sure entry is not found (friendlyname)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=friendlyname-test \
	PKCS12:$srcdir/data/sub-cert.p12 >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (friendlyname|private key)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=friendlyname-test  \
	--private-key \
	PKCS12:$srcdir/data/test.p12 > /dev/null || exit 1

echo "make sure entry is not found (friendlyname|private key)"
${hxtool} query \
	--pass=PASS:foobar \
	--friendlyname=ca  \
	--private-key \
	PKCS12:$srcdir/data/test.p12 >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (cert ds)"
${hxtool} query \
	--digitalSignature \
	FILE:$srcdir/data/test.crt >/dev/null 2>/dev/null || exit 1

echo "make sure entry is found (cert ke)"
${hxtool} query \
	--keyEncipherment \
	FILE:$srcdir/data/test.crt >/dev/null 2>/dev/null || exit 1

echo "make sure entry is found (cert ke + ds)"
${hxtool} query \
	--digitalSignature \
	--keyEncipherment \
	FILE:$srcdir/data/test.crt >/dev/null 2>/dev/null || exit 1

echo "make sure entry is found (cert-ds ds)"
${hxtool} query \
	--digitalSignature \
	FILE:$srcdir/data/test-ds-only.crt >/dev/null 2>/dev/null || exit 1

echo "make sure entry is not found (cert-ds ke)"
${hxtool} query \
	--keyEncipherment \
	FILE:$srcdir/data/test-ds-only.crt >/dev/null 2>/dev/null && exit 1

echo "make sure entry is not found (cert-ds ke + ds)"
${hxtool} query \
	--digitalSignature \
	--keyEncipherment \
	FILE:$srcdir/data/test-ds-only.crt >/dev/null 2>/dev/null && exit 1

echo "make sure entry is not found (cert-ke ds)"
${hxtool} query \
	--digitalSignature \
	FILE:$srcdir/data/test-ke-only.crt >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (cert-ke ke)"
${hxtool} query \
	--keyEncipherment \
	FILE:$srcdir/data/test-ke-only.crt >/dev/null 2>/dev/null || exit 1

echo "make sure entry is not found (cert-ke ke + ds)"
${hxtool} query \
	--digitalSignature \
	--keyEncipherment \
	FILE:$srcdir/data/test-ke-only.crt >/dev/null 2>/dev/null && exit 1

echo "make sure entry is found (eku) in query language"
${hxtool} query \
	--expr='"1.3.6.1.5.2.3.5" IN %{certificate.eku}'  \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1

echo "make sure entry is not found (eku) in query language"
${hxtool} query \
	--expr='"1.3.6.1.5.2.3.6" IN %{certificate.eku}'  \
	FILE:$srcdir/data/kdc.crt > /dev/null && exit 1

echo "make sure entry is found (subject) in query language"
${hxtool} query \
	--expr='%{certificate.subject} == "CN=kdc,C=SE"'  \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1

echo "make sure entry is found using TAILMATCH (subject) in query language"
${hxtool} query \
	--expr='%{certificate.subject} TAILMATCH "C=SE"'  \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1

echo "make sure entry is not found using TAILMATCH (subject) in query language"
${hxtool} query \
	--expr='%{certificate.subject} TAILMATCH "C=FI"'  \
	FILE:$srcdir/data/kdc.crt > /dev/null && exit 1

echo "make sure entry is found (issuer) in query language"
${hxtool} query \
	--expr='%{certificate.issuer} == "C=SE,CN=hx509 Test Root CA"'  \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1

echo "make sure entry match with EKU and TAILMATCH in query language"
${hxtool} query \
	--expr='"1.3.6.1.5.2.3.5" IN %{certificate.eku} AND %{certificate.subject} TAILMATCH "C=SE"'  \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1

echo "make sure entry match with hash.sha1"
${hxtool} query \
	--expr='"%{certificate.hash.sha1}EQ "412120212A2CBFD777DE5499ECB4724345F33F16"' \
	FILE:$srcdir/data/kdc.crt > /dev/null || exit 1


exit 0
