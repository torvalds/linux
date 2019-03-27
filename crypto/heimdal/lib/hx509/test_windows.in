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

echo "Create trust anchor"
${hxtool} issue-certificate \
    --self-signed \
    --issue-ca \
    --generate-key=rsa \
    --subject="CN=Windows-CA,DC=heimdal,DC=pki" \
    --lifetime=10years \
    --certificate="FILE:wca.pem" || exit 1

echo "Create domain controller cert"
${hxtool} issue-certificate \
    --type="pkinit-kdc" \
    --pk-init-principal="krbtgt/HEIMDAL.PKI@HEIMDAL.PKI" \
    --hostname=kdc.heimdal.pki \
    --generate-key=rsa \
    --subject="CN=kdc.heimdal.pki,dc=heimdal,dc=pki" \
    --certificate="FILE:wdc.pem" \
    --domain-controller \
    --crl-uri="http://www.test.h5l.se/test-hemdal-pki-crl1.crl" \
    --ca-certificate=FILE:wca.pem || exit 1


echo "Create user cert"
${hxtool} issue-certificate \
    --type="pkinit-client" \
    --pk-init-principal="user@HEIMDAL.PKI" \
    --generate-key=rsa \
    --subject="CN=User,DC=heimdal,DC=pki" \
    --ms-upn="user@heimdal.pki" \
    --crl-uri="http://www.test.h5l.se/test-hemdal-pki-crl1.crl" \
    --certificate="FILE:wuser.pem" \
    --ca-certificate=FILE:wca.pem || exit 1

echo "Create crl"
${hxtool} crl-sign \
	--crl-file=wcrl.crl \
	--signer=FILE:wca.pem || exit 1

exit 0
