#!/bin/sh
#
# Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

echo "create certificate request"
${hxtool} request-create \
	 --subject="CN=Love,DC=it,DC=su,DC=se" \
	 --key=FILE:$srcdir/data/key.der \
	 pkcs10-request.der || exit 1

echo "issue certificate"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "verify certificate"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "issue crl (no cert)"
${hxtool} crl-sign \
	--crl-file=crl.crl \
	--signer=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key || exit 1

echo "verify certificate (with CRL)"
${hxtool} verify \
	cert:FILE:cert-ee.pem \
	crl:FILE:crl.crl \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "issue crl (with cert)"
${hxtool} crl-sign \
	--crl-file=crl.crl \
	--signer=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	FILE:cert-ee.pem || exit 1

echo "verify certificate (included in CRL)"
${hxtool} verify \
	cert:FILE:cert-ee.pem \
	crl:FILE:crl.crl \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "issue crl (with cert)"
${hxtool} crl-sign \
	--crl-file=crl.crl \
	--lifetime='1 month' \
	--signer=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	FILE:cert-ee.pem || exit 1

echo "verify certificate (included in CRL, and lifetime 1 month)"
${hxtool} verify \
	cert:FILE:cert-ee.pem \
	crl:FILE:crl.crl \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "issue certificate (10years 1 month)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
          --lifetime="10years 1 month" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue certificate (with https ekus)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
	  --type="https-server" \
	  --type="https-client" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue certificate (pkinit KDC)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
	  --type="pkinit-kdc" \
          --pk-init-principal="krbtgt/TEST.H5L.SE@TEST.H5L.SE" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue certificate (pkinit client)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
	  --type="pkinit-client" \
          --pk-init-principal="lha@TEST.H5L.SE" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue certificate (hostnames)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
	  --type="https-server" \
          --hostname="www.test.h5l.se" \
          --hostname="ftp.test.h5l.se" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "verify certificate hostname (ok)"
${hxtool} verify --missing-revoke \
	--hostname=www.test.h5l.se \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "verify certificate hostname (fail)"
${hxtool} verify --missing-revoke \
	--hostname=www2.test.h5l.se \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "verify certificate hostname (fail)"
${hxtool} verify --missing-revoke \
	--hostname=2www.test.h5l.se \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "issue certificate (hostname in CN)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=www.test.h5l.se" \
	  --type="https-server" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "verify certificate hostname (ok)"
${hxtool} verify --missing-revoke \
	--hostname=www.test.h5l.se \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "verify certificate hostname (fail)"
${hxtool} verify --missing-revoke \
	--hostname=www2.test.h5l.se \
	cert:FILE:cert-ee.pem \
	anchor:FILE:$srcdir/data/ca.crt > /dev/null && exit 1

echo "issue certificate (email)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
          --email="lha@test.h5l.se" \
          --email="test@test.h5l.se" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue certificate (email, null subject DN)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="" \
          --email="lha@test.h5l.se" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-null.pem" || exit 1

echo "issue certificate (jabber)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --subject="cn=foo" \
          --jid="lha@test.h5l.se" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue self-signed cert"
${hxtool} issue-certificate \
	  --self-signed \
	  --ca-private-key=FILE:$srcdir/data/key.der \
	  --subject="cn=test" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue ca cert"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/ca.crt,$srcdir/data/ca.key \
	  --issue-ca \
	  --subject="cn=ca-cert" \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-ca.der" || exit 1

echo "issue self-signed ca cert"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
	  --ca-private-key=FILE:$srcdir/data/key.der \
	  --subject="cn=ca-root" \
	  --certificate="FILE:cert-ca.der" || exit 1

echo "issue proxy certificate"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:$srcdir/data/test.crt,$srcdir/data/test.key \
	  --issue-proxy \
	  --req="PKCS10:pkcs10-request.der" \
	  --certificate="FILE:cert-proxy.der" || exit 1

echo "verify proxy cert"
${hxtool} verify --missing-revoke \
    --allow-proxy-certificate \
    cert:FILE:cert-proxy.der \
    chain:FILE:$srcdir/data/test.crt \
    anchor:FILE:$srcdir/data/ca.crt > /dev/null || exit 1

echo "issue ca cert (generate rsa key)"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
 	  --serial-number="deadbeaf" \
	  --generate-key=rsa \
          --path-length=-1 \
	  --subject="cn=ca2-cert" \
	  --certificate="FILE:cert-ca.pem" || exit 1

echo "issue sub-ca cert (generate rsa key)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
	  --issue-ca \
 	  --serial-number="deadbeaf22" \
	  --generate-key=rsa \
	  --subject="cn=sub-ca2-cert" \
	  --certificate="FILE:cert-sub-ca.pem" || exit 1

echo "issue ee cert (generate rsa key)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
	  --generate-key=rsa \
	  --subject="cn=cert-ee2" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue sub-ca ee cert (generate rsa key)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-sub-ca.pem \
	  --generate-key=rsa \
	  --subject="cn=cert-sub-ee2" \
	  --certificate="FILE:cert-sub-ee.pem" || exit 1

echo "verify certificate (ee)"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-ee.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

echo "verify certificate (sub-ee)"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-sub-ee.pem \
	chain:FILE:cert-sub-ca.pem \
	anchor:FILE:cert-ca.pem || exit 1

echo "sign CMS signature (generate key)"
${hxtool} cms-create-sd \
	--certificate=FILE:cert-ee.pem \
	"$srcdir/test_name.c" \
	sd.data > /dev/null || exit 1

echo "verify CMS signature (generate key)"
${hxtool} cms-verify-sd \
	--missing-revoke \
	--anchors=FILE:cert-ca.pem \
	sd.data sd.data.out > /dev/null || exit 1
cmp "$srcdir/test_name.c" sd.data.out || exit 1

echo "extend ca cert"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
          --lifetime="2years" \
 	  --serial-number="deadbeaf" \
	  --ca-private-key=FILE:cert-ca.pem \
	  --subject="cn=ca2-cert" \
	  --certificate="FILE:cert-ca.pem" || exit 1

echo "verify certificate generated by previous ca"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-ee.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

echo "extend ca cert (template)"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
          --lifetime="3years" \
	  --template-certificate="FILE:cert-ca.pem" \
	  --template-fields="serialNumber,notBefore,subject" \
          --path-length=-1 \
	  --ca-private-key=FILE:cert-ca.pem \
	  --certificate="FILE:cert-ca.pem" || exit 1

echo "verify certificate generated by previous ca"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-ee.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

echo "extend sub-ca cert (template)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
	  --issue-ca \
          --lifetime="2years" \
	  --template-certificate="FILE:cert-sub-ca.pem" \
	  --template-fields="serialNumber,notBefore,subject,SPKI" \
	  --certificate="FILE:cert-sub-ca2.pem" || exit 1

echo "verify certificate (sub-ee) with extended chain"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-sub-ee.pem \
	chain:FILE:cert-sub-ca.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

echo "+++++++++++ test basic constraints"

echo "extend ca cert (too low path-length constraint)"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
          --lifetime="3years" \
	  --template-certificate="FILE:cert-ca.pem" \
	  --template-fields="serialNumber,notBefore,subject" \
          --path-length=0 \
	  --ca-private-key=FILE:cert-ca.pem \
	  --certificate="FILE:cert-ca.pem" || exit 1

echo "verify failure of certificate (sub-ee) with path-length constraint"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-sub-ee.pem \
	chain:FILE:cert-sub-ca.pem \
	anchor:FILE:cert-ca.pem > /dev/null && exit 1

echo "extend ca cert (exact path-length constraint)"
${hxtool} issue-certificate \
	  --self-signed \
	  --issue-ca \
          --lifetime="3years" \
	  --template-certificate="FILE:cert-ca.pem" \
	  --template-fields="serialNumber,notBefore,subject" \
          --path-length=1 \
	  --ca-private-key=FILE:cert-ca.pem \
	  --certificate="FILE:cert-ca.pem" || exit 1

echo "verify certificate (sub-ee) with exact path-length constraint"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-sub-ee.pem \
	chain:FILE:cert-sub-ca.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

echo "Check missing basicConstrants.isCa"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
          --lifetime="2years" \
	  --template-certificate="FILE:cert-sub-ca.pem" \
	  --template-fields="serialNumber,notBefore,subject,SPKI" \
	  --certificate="FILE:cert-sub-ca2.pem" || exit 1

echo "verify failure certificate (sub-ee) with missing isCA"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-sub-ee.pem \
	chain:FILE:cert-sub-ca2.pem \
	anchor:FILE:cert-ca.pem > /dev/null && exit 1

echo "issue ee cert (crl uri)"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
	  --req="PKCS10:pkcs10-request.der" \
	  --crl-uri="http://www.test.h5l.se/crl1.crl" \
	  --subject="cn=cert-ee-crl-uri" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "issue null subject cert"
${hxtool} issue-certificate \
	  --ca-certificate=FILE:cert-ca.pem \
	  --req="PKCS10:pkcs10-request.der" \
	  --subject="" \
	  --email="lha@test.h5l.se" \
	  --certificate="FILE:cert-ee.pem" || exit 1

echo "verify certificate null subject"
${hxtool} verify --missing-revoke \
	cert:FILE:cert-ee.pem \
	anchor:FILE:cert-ca.pem > /dev/null || exit 1

exit 0
