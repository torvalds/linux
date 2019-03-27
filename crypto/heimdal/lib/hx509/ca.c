/*
 * Copyright (c) 2006 - 2010 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hx_locl.h"
#include <pkinit_asn1.h>

/**
 * @page page_ca Hx509 CA functions
 *
 * See the library functions here: @ref hx509_ca
 */

struct hx509_ca_tbs {
    hx509_name subject;
    SubjectPublicKeyInfo spki;
    ExtKeyUsage eku;
    GeneralNames san;
    unsigned key_usage;
    heim_integer serial;
    struct {
	unsigned int proxy:1;
	unsigned int ca:1;
	unsigned int key:1;
	unsigned int serial:1;
	unsigned int domaincontroller:1;
	unsigned int xUniqueID:1;
    } flags;
    time_t notBefore;
    time_t notAfter;
    int pathLenConstraint; /* both for CA and Proxy */
    CRLDistributionPoints crldp;
    heim_bit_string subjectUniqueID;
    heim_bit_string issuerUniqueID;

};

/**
 * Allocate an to-be-signed certificate object that will be converted
 * into an certificate.
 *
 * @param context A hx509 context.
 * @param tbs returned to-be-signed certicate object, free with
 * hx509_ca_tbs_free().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_init(hx509_context context, hx509_ca_tbs *tbs)
{
    *tbs = calloc(1, sizeof(**tbs));
    if (*tbs == NULL)
	return ENOMEM;

    return 0;
}

/**
 * Free an To Be Signed object.
 *
 * @param tbs object to free.
 *
 * @ingroup hx509_ca
 */

void
hx509_ca_tbs_free(hx509_ca_tbs *tbs)
{
    if (tbs == NULL || *tbs == NULL)
	return;

    free_SubjectPublicKeyInfo(&(*tbs)->spki);
    free_GeneralNames(&(*tbs)->san);
    free_ExtKeyUsage(&(*tbs)->eku);
    der_free_heim_integer(&(*tbs)->serial);
    free_CRLDistributionPoints(&(*tbs)->crldp);
    der_free_bit_string(&(*tbs)->subjectUniqueID);
    der_free_bit_string(&(*tbs)->issuerUniqueID);
    hx509_name_free(&(*tbs)->subject);

    memset(*tbs, 0, sizeof(**tbs));
    free(*tbs);
    *tbs = NULL;
}

/**
 * Set the absolute time when the certificate is valid from. If not
 * set the current time will be used.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param t time the certificated will start to be valid
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_notBefore(hx509_context context,
			   hx509_ca_tbs tbs,
			   time_t t)
{
    tbs->notBefore = t;
    return 0;
}

/**
 * Set the absolute time when the certificate is valid to.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param t time when the certificate will expire
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_notAfter(hx509_context context,
			   hx509_ca_tbs tbs,
			   time_t t)
{
    tbs->notAfter = t;
    return 0;
}

/**
 * Set the relative time when the certificiate is going to expire.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param delta seconds to the certificate is going to expire.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_notAfter_lifetime(hx509_context context,
				   hx509_ca_tbs tbs,
				   time_t delta)
{
    return hx509_ca_tbs_set_notAfter(context, tbs, time(NULL) + delta);
}

static const struct units templatebits[] = {
    { "ExtendedKeyUsage", HX509_CA_TEMPLATE_EKU },
    { "KeyUsage", HX509_CA_TEMPLATE_KU },
    { "SPKI", HX509_CA_TEMPLATE_SPKI },
    { "notAfter", HX509_CA_TEMPLATE_NOTAFTER },
    { "notBefore", HX509_CA_TEMPLATE_NOTBEFORE },
    { "serial", HX509_CA_TEMPLATE_SERIAL },
    { "subject", HX509_CA_TEMPLATE_SUBJECT },
    { NULL, 0 }
};

/**
 * Make of template units, use to build flags argument to
 * hx509_ca_tbs_set_template() with parse_units().
 *
 * @return an units structure.
 *
 * @ingroup hx509_ca
 */

const struct units *
hx509_ca_tbs_template_units(void)
{
    return templatebits;
}

/**
 * Initialize the to-be-signed certificate object from a template certifiate.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param flags bit field selecting what to copy from the template
 * certifiate.
 * @param cert template certificate.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_template(hx509_context context,
			  hx509_ca_tbs tbs,
			  int flags,
			  hx509_cert cert)
{
    int ret;

    if (flags & HX509_CA_TEMPLATE_SUBJECT) {
	if (tbs->subject)
	    hx509_name_free(&tbs->subject);
	ret = hx509_cert_get_subject(cert, &tbs->subject);
	if (ret) {
	    hx509_set_error_string(context, 0, ret,
				   "Failed to get subject from template");
	    return ret;
	}
    }
    if (flags & HX509_CA_TEMPLATE_SERIAL) {
	der_free_heim_integer(&tbs->serial);
	ret = hx509_cert_get_serialnumber(cert, &tbs->serial);
	tbs->flags.serial = !ret;
	if (ret) {
	    hx509_set_error_string(context, 0, ret,
				   "Failed to copy serial number");
	    return ret;
	}
    }
    if (flags & HX509_CA_TEMPLATE_NOTBEFORE)
	tbs->notBefore = hx509_cert_get_notBefore(cert);
    if (flags & HX509_CA_TEMPLATE_NOTAFTER)
	tbs->notAfter = hx509_cert_get_notAfter(cert);
    if (flags & HX509_CA_TEMPLATE_SPKI) {
	free_SubjectPublicKeyInfo(&tbs->spki);
	ret = hx509_cert_get_SPKI(context, cert, &tbs->spki);
	tbs->flags.key = !ret;
	if (ret)
	    return ret;
    }
    if (flags & HX509_CA_TEMPLATE_KU) {
	KeyUsage ku;
	ret = _hx509_cert_get_keyusage(context, cert, &ku);
	if (ret)
	    return ret;
	tbs->key_usage = KeyUsage2int(ku);
    }
    if (flags & HX509_CA_TEMPLATE_EKU) {
	ExtKeyUsage eku;
	size_t i;
	ret = _hx509_cert_get_eku(context, cert, &eku);
	if (ret)
	    return ret;
	for (i = 0; i < eku.len; i++) {
	    ret = hx509_ca_tbs_add_eku(context, tbs, &eku.val[i]);
	    if (ret) {
		free_ExtKeyUsage(&eku);
		return ret;
	    }
	}
	free_ExtKeyUsage(&eku);
    }
    return 0;
}

/**
 * Make the to-be-signed certificate object a CA certificate. If the
 * pathLenConstraint is negative path length constraint is used.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param pathLenConstraint path length constraint, negative, no
 * constraint.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_ca(hx509_context context,
		    hx509_ca_tbs tbs,
		    int pathLenConstraint)
{
    tbs->flags.ca = 1;
    tbs->pathLenConstraint = pathLenConstraint;
    return 0;
}

/**
 * Make the to-be-signed certificate object a proxy certificate. If the
 * pathLenConstraint is negative path length constraint is used.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param pathLenConstraint path length constraint, negative, no
 * constraint.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_proxy(hx509_context context,
		       hx509_ca_tbs tbs,
		       int pathLenConstraint)
{
    tbs->flags.proxy = 1;
    tbs->pathLenConstraint = pathLenConstraint;
    return 0;
}


/**
 * Make the to-be-signed certificate object a windows domain controller certificate.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_domaincontroller(hx509_context context,
				  hx509_ca_tbs tbs)
{
    tbs->flags.domaincontroller = 1;
    return 0;
}

/**
 * Set the subject public key info (SPKI) in the to-be-signed certificate
 * object. SPKI is the public key and key related parameters in the
 * certificate.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param spki subject public key info to use for the to-be-signed certificate object.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_spki(hx509_context context,
		      hx509_ca_tbs tbs,
		      const SubjectPublicKeyInfo *spki)
{
    int ret;
    free_SubjectPublicKeyInfo(&tbs->spki);
    ret = copy_SubjectPublicKeyInfo(spki, &tbs->spki);
    tbs->flags.key = !ret;
    return ret;
}

/**
 * Set the serial number to use for to-be-signed certificate object.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param serialNumber serial number to use for the to-be-signed
 * certificate object.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_serialnumber(hx509_context context,
			      hx509_ca_tbs tbs,
			      const heim_integer *serialNumber)
{
    int ret;
    der_free_heim_integer(&tbs->serial);
    ret = der_copy_heim_integer(serialNumber, &tbs->serial);
    tbs->flags.serial = !ret;
    return ret;
}

/**
 * An an extended key usage to the to-be-signed certificate object.
 * Duplicates will detected and not added.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param oid extended key usage to add.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_eku(hx509_context context,
		     hx509_ca_tbs tbs,
		     const heim_oid *oid)
{
    void *ptr;
    int ret;
    unsigned i;

    /* search for duplicates */
    for (i = 0; i < tbs->eku.len; i++) {
	if (der_heim_oid_cmp(oid, &tbs->eku.val[i]) == 0)
	    return 0;
    }

    ptr = realloc(tbs->eku.val, sizeof(tbs->eku.val[0]) * (tbs->eku.len + 1));
    if (ptr == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    tbs->eku.val = ptr;
    ret = der_copy_oid(oid, &tbs->eku.val[tbs->eku.len]);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "out of memory");
	return ret;
    }
    tbs->eku.len += 1;
    return 0;
}

/**
 * Add CRL distribution point URI to the to-be-signed certificate
 * object.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param uri uri to the CRL.
 * @param issuername name of the issuer.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_crl_dp_uri(hx509_context context,
			    hx509_ca_tbs tbs,
			    const char *uri,
			    hx509_name issuername)
{
    DistributionPoint dp;
    int ret;

    memset(&dp, 0, sizeof(dp));

    dp.distributionPoint = ecalloc(1, sizeof(*dp.distributionPoint));

    {
	DistributionPointName name;
	GeneralName gn;
	size_t size;

	name.element = choice_DistributionPointName_fullName;
	name.u.fullName.len = 1;
	name.u.fullName.val = &gn;

	gn.element = choice_GeneralName_uniformResourceIdentifier;
	gn.u.uniformResourceIdentifier.data = rk_UNCONST(uri);
	gn.u.uniformResourceIdentifier.length = strlen(uri);

	ASN1_MALLOC_ENCODE(DistributionPointName,
			   dp.distributionPoint->data,
			   dp.distributionPoint->length,
			   &name, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret,
				   "Failed to encoded DistributionPointName");
	    goto out;
	}
	if (dp.distributionPoint->length != size)
	    _hx509_abort("internal ASN.1 encoder error");
    }

    if (issuername) {
#if 1
	/**
	 * issuername not supported
	 */
	hx509_set_error_string(context, 0, EINVAL,
			       "CRLDistributionPoints.name.issuername not yet supported");
	return EINVAL;
#else
	GeneralNames *crlissuer;
	GeneralName gn;
	Name n;

	crlissuer = calloc(1, sizeof(*crlissuer));
	if (crlissuer == NULL) {
	    return ENOMEM;
	}
	memset(&gn, 0, sizeof(gn));

	gn.element = choice_GeneralName_directoryName;
	ret = hx509_name_to_Name(issuername, &n);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "out of memory");
	    goto out;
	}

	gn.u.directoryName.element = n.element;
	gn.u.directoryName.u.rdnSequence = n.u.rdnSequence;

	ret = add_GeneralNames(&crlissuer, &gn);
	free_Name(&n);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "out of memory");
	    goto out;
	}

	dp.cRLIssuer = &crlissuer;
#endif
    }

    ret = add_CRLDistributionPoints(&tbs->crldp, &dp);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

out:
    free_DistributionPoint(&dp);

    return ret;
}

/**
 * Add Subject Alternative Name otherName to the to-be-signed
 * certificate object.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param oid the oid of the OtherName.
 * @param os data in the other name.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_otherName(hx509_context context,
			       hx509_ca_tbs tbs,
			       const heim_oid *oid,
			       const heim_octet_string *os)
{
    GeneralName gn;

    memset(&gn, 0, sizeof(gn));
    gn.element = choice_GeneralName_otherName;
    gn.u.otherName.type_id = *oid;
    gn.u.otherName.value = *os;

    return add_GeneralNames(&tbs->san, &gn);
}

/**
 * Add Kerberos Subject Alternative Name to the to-be-signed
 * certificate object. The principal string is a UTF8 string.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param principal Kerberos principal to add to the certificate.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_pkinit(hx509_context context,
			    hx509_ca_tbs tbs,
			    const char *principal)
{
    heim_octet_string os;
    KRB5PrincipalName p;
    size_t size;
    int ret;
    char *s = NULL;

    memset(&p, 0, sizeof(p));

    /* parse principal */
    {
	const char *str;
	char *q;
	int n;

	/* count number of component */
	n = 1;
	for(str = principal; *str != '\0' && *str != '@'; str++){
	    if(*str=='\\'){
		if(str[1] == '\0' || str[1] == '@') {
		    ret = HX509_PARSING_NAME_FAILED;
		    hx509_set_error_string(context, 0, ret,
					   "trailing \\ in principal name");
		    goto out;
		}
		str++;
	    } else if(*str == '/')
		n++;
	}
	p.principalName.name_string.val =
	    calloc(n, sizeof(*p.principalName.name_string.val));
	if (p.principalName.name_string.val == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "malloc: out of memory");
	    goto out;
	}
	p.principalName.name_string.len = n;

	p.principalName.name_type = KRB5_NT_PRINCIPAL;
	q = s = strdup(principal);
	if (q == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "malloc: out of memory");
	    goto out;
	}
	p.realm = strrchr(q, '@');
	if (p.realm == NULL) {
	    ret = HX509_PARSING_NAME_FAILED;
	    hx509_set_error_string(context, 0, ret, "Missing @ in principal");
	    goto out;
	};
	*p.realm++ = '\0';

	n = 0;
	while (q) {
	    p.principalName.name_string.val[n++] = q;
	    q = strchr(q, '/');
	    if (q)
		*q++ = '\0';
	}
    }

    ASN1_MALLOC_ENCODE(KRB5PrincipalName, os.data, os.length, &p, &size, ret);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
    if (size != os.length)
	_hx509_abort("internal ASN.1 encoder error");

    ret = hx509_ca_tbs_add_san_otherName(context,
					 tbs,
					 &asn1_oid_id_pkinit_san,
					 &os);
    free(os.data);
out:
    if (p.principalName.name_string.val)
	free (p.principalName.name_string.val);
    if (s)
	free(s);
    return ret;
}

/*
 *
 */

static int
add_utf8_san(hx509_context context,
	     hx509_ca_tbs tbs,
	     const heim_oid *oid,
	     const char *string)
{
    const PKIXXmppAddr ustring = (const PKIXXmppAddr)(intptr_t)string;
    heim_octet_string os;
    size_t size;
    int ret;

    os.length = 0;
    os.data = NULL;

    ASN1_MALLOC_ENCODE(PKIXXmppAddr, os.data, os.length, &ustring, &size, ret);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
    if (size != os.length)
	_hx509_abort("internal ASN.1 encoder error");

    ret = hx509_ca_tbs_add_san_otherName(context,
					 tbs,
					 oid,
					 &os);
    free(os.data);
out:
    return ret;
}

/**
 * Add Microsoft UPN Subject Alternative Name to the to-be-signed
 * certificate object. The principal string is a UTF8 string.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param principal Microsoft UPN string.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_ms_upn(hx509_context context,
			    hx509_ca_tbs tbs,
			    const char *principal)
{
    return add_utf8_san(context, tbs, &asn1_oid_id_pkinit_ms_san, principal);
}

/**
 * Add a Jabber/XMPP jid Subject Alternative Name to the to-be-signed
 * certificate object. The jid is an UTF8 string.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param jid string of an a jabber id in UTF8.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_jid(hx509_context context,
			 hx509_ca_tbs tbs,
			 const char *jid)
{
    return add_utf8_san(context, tbs, &asn1_oid_id_pkix_on_xmppAddr, jid);
}


/**
 * Add a Subject Alternative Name hostname to to-be-signed certificate
 * object. A domain match starts with ., an exact match does not.
 *
 * Example of a an domain match: .domain.se matches the hostname
 * host.domain.se.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param dnsname a hostame.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_hostname(hx509_context context,
			      hx509_ca_tbs tbs,
			      const char *dnsname)
{
    GeneralName gn;

    memset(&gn, 0, sizeof(gn));
    gn.element = choice_GeneralName_dNSName;
    gn.u.dNSName.data = rk_UNCONST(dnsname);
    gn.u.dNSName.length = strlen(dnsname);

    return add_GeneralNames(&tbs->san, &gn);
}

/**
 * Add a Subject Alternative Name rfc822 (email address) to
 * to-be-signed certificate object.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param rfc822Name a string to a email address.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_add_san_rfc822name(hx509_context context,
				hx509_ca_tbs tbs,
				const char *rfc822Name)
{
    GeneralName gn;

    memset(&gn, 0, sizeof(gn));
    gn.element = choice_GeneralName_rfc822Name;
    gn.u.rfc822Name.data = rk_UNCONST(rfc822Name);
    gn.u.rfc822Name.length = strlen(rfc822Name);

    return add_GeneralNames(&tbs->san, &gn);
}

/**
 * Set the subject name of a to-be-signed certificate object.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param subject the name to set a subject.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_subject(hx509_context context,
			 hx509_ca_tbs tbs,
			 hx509_name subject)
{
    if (tbs->subject)
	hx509_name_free(&tbs->subject);
    return hx509_name_copy(context, subject, &tbs->subject);
}

/**
 * Set the issuerUniqueID and subjectUniqueID
 *
 * These are only supposed to be used considered with version 2
 * certificates, replaced by the two extensions SubjectKeyIdentifier
 * and IssuerKeyIdentifier. This function is to allow application
 * using legacy protocol to issue them.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param issuerUniqueID to be set
 * @param subjectUniqueID to be set
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_set_unique(hx509_context context,
			hx509_ca_tbs tbs,
			const heim_bit_string *subjectUniqueID,
			const heim_bit_string *issuerUniqueID)
{
    int ret;

    der_free_bit_string(&tbs->subjectUniqueID);
    der_free_bit_string(&tbs->issuerUniqueID);

    if (subjectUniqueID) {
	ret = der_copy_bit_string(subjectUniqueID, &tbs->subjectUniqueID);
	if (ret)
	    return ret;
    }

    if (issuerUniqueID) {
	ret = der_copy_bit_string(issuerUniqueID, &tbs->issuerUniqueID);
	if (ret)
	    return ret;
    }

    return 0;
}

/**
 * Expand the the subject name in the to-be-signed certificate object
 * using hx509_name_expand().
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param env enviroment variable to expand variables in the subject
 * name, see hx509_env_init().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_tbs_subject_expand(hx509_context context,
			    hx509_ca_tbs tbs,
			    hx509_env env)
{
    return hx509_name_expand(context, tbs->subject, env);
}

/*
 *
 */

static int
add_extension(hx509_context context,
	      TBSCertificate *tbsc,
	      int critical_flag,
	      const heim_oid *oid,
	      const heim_octet_string *data)
{
    Extension ext;
    int ret;

    memset(&ext, 0, sizeof(ext));

    if (critical_flag) {
	ext.critical = malloc(sizeof(*ext.critical));
	if (ext.critical == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	*ext.critical = TRUE;
    }

    ret = der_copy_oid(oid, &ext.extnID);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
    ret = der_copy_octet_string(data, &ext.extnValue);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
    ret = add_Extensions(tbsc->extensions, &ext);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
out:
    free_Extension(&ext);
    return ret;
}

static int
build_proxy_prefix(hx509_context context, const Name *issuer, Name *subject)
{
    char *tstr;
    time_t t;
    int ret;

    ret = copy_Name(issuer, subject);
    if (ret) {
	hx509_set_error_string(context, 0, ret,
			       "Failed to copy subject name");
	return ret;
    }

    t = time(NULL);
    asprintf(&tstr, "ts-%lu", (unsigned long)t);
    if (tstr == NULL) {
	hx509_set_error_string(context, 0, ENOMEM,
			       "Failed to copy subject name");
	return ENOMEM;
    }
    /* prefix with CN=<ts>,...*/
    ret = _hx509_name_modify(context, subject, 1, &asn1_oid_id_at_commonName, tstr);
    free(tstr);
    if (ret)
	free_Name(subject);
    return ret;
}

static int
ca_sign(hx509_context context,
	hx509_ca_tbs tbs,
	hx509_private_key signer,
	const AuthorityKeyIdentifier *ai,
	const Name *issuername,
	hx509_cert *certificate)
{
    heim_octet_string data;
    Certificate c;
    TBSCertificate *tbsc;
    size_t size;
    int ret;
    const AlgorithmIdentifier *sigalg;
    time_t notBefore;
    time_t notAfter;
    unsigned key_usage;

    sigalg = _hx509_crypto_default_sig_alg;

    memset(&c, 0, sizeof(c));

    /*
     * Default values are: Valid since 24h ago, valid one year into
     * the future, KeyUsage digitalSignature and keyEncipherment set,
     * and keyCertSign for CA certificates.
     */
    notBefore = tbs->notBefore;
    if (notBefore == 0)
	notBefore = time(NULL) - 3600 * 24;
    notAfter = tbs->notAfter;
    if (notAfter == 0)
	notAfter = time(NULL) + 3600 * 24 * 365;

    key_usage = tbs->key_usage;
    if (key_usage == 0) {
	KeyUsage ku;
	memset(&ku, 0, sizeof(ku));
	ku.digitalSignature = 1;
	ku.keyEncipherment = 1;
	key_usage = KeyUsage2int(ku);
    }

    if (tbs->flags.ca) {
	KeyUsage ku;
	memset(&ku, 0, sizeof(ku));
	ku.keyCertSign = 1;
	ku.cRLSign = 1;
	key_usage |= KeyUsage2int(ku);
    }

    /*
     *
     */

    tbsc = &c.tbsCertificate;

    if (tbs->flags.key == 0) {
	ret = EINVAL;
	hx509_set_error_string(context, 0, ret, "No public key set");
	return ret;
    }
    /*
     * Don't put restrictions on proxy certificate's subject name, it
     * will be generated below.
     */
    if (!tbs->flags.proxy) {
	if (tbs->subject == NULL) {
	    hx509_set_error_string(context, 0, EINVAL, "No subject name set");
	    return EINVAL;
	}
	if (hx509_name_is_null_p(tbs->subject) && tbs->san.len == 0) {
	    hx509_set_error_string(context, 0, EINVAL,
				   "NULL subject and no SubjectAltNames");
	    return EINVAL;
	}
    }
    if (tbs->flags.ca && tbs->flags.proxy) {
	hx509_set_error_string(context, 0, EINVAL, "Can't be proxy and CA "
			       "at the same time");
	return EINVAL;
    }
    if (tbs->flags.proxy) {
	if (tbs->san.len > 0) {
	    hx509_set_error_string(context, 0, EINVAL,
				   "Proxy certificate is not allowed "
				   "to have SubjectAltNames");
	    return EINVAL;
	}
    }

    /* version         [0]  Version OPTIONAL, -- EXPLICIT nnn DEFAULT 1, */
    tbsc->version = calloc(1, sizeof(*tbsc->version));
    if (tbsc->version == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }
    *tbsc->version = rfc3280_version_3;
    /* serialNumber         CertificateSerialNumber, */
    if (tbs->flags.serial) {
	ret = der_copy_heim_integer(&tbs->serial, &tbsc->serialNumber);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
    } else {
	tbsc->serialNumber.length = 20;
	tbsc->serialNumber.data = malloc(tbsc->serialNumber.length);
	if (tbsc->serialNumber.data == NULL){
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	/* XXX diffrent */
	RAND_bytes(tbsc->serialNumber.data, tbsc->serialNumber.length);
	((unsigned char *)tbsc->serialNumber.data)[0] &= 0x7f;
    }
    /* signature            AlgorithmIdentifier, */
    ret = copy_AlgorithmIdentifier(sigalg, &tbsc->signature);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to copy sigature alg");
	goto out;
    }
    /* issuer               Name, */
    if (issuername)
	ret = copy_Name(issuername, &tbsc->issuer);
    else
	ret = hx509_name_to_Name(tbs->subject, &tbsc->issuer);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to copy issuer name");
	goto out;
    }
    /* validity             Validity, */
    tbsc->validity.notBefore.element = choice_Time_generalTime;
    tbsc->validity.notBefore.u.generalTime = notBefore;
    tbsc->validity.notAfter.element = choice_Time_generalTime;
    tbsc->validity.notAfter.u.generalTime = notAfter;
    /* subject              Name, */
    if (tbs->flags.proxy) {
	ret = build_proxy_prefix(context, &tbsc->issuer, &tbsc->subject);
	if (ret)
	    goto out;
    } else {
	ret = hx509_name_to_Name(tbs->subject, &tbsc->subject);
	if (ret) {
	    hx509_set_error_string(context, 0, ret,
				   "Failed to copy subject name");
	    goto out;
	}
    }
    /* subjectPublicKeyInfo SubjectPublicKeyInfo, */
    ret = copy_SubjectPublicKeyInfo(&tbs->spki, &tbsc->subjectPublicKeyInfo);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to copy spki");
	goto out;
    }
    /* issuerUniqueID  [1]  IMPLICIT BIT STRING OPTIONAL */
    if (tbs->issuerUniqueID.length) {
	tbsc->issuerUniqueID = calloc(1, sizeof(*tbsc->issuerUniqueID));
	if (tbsc->issuerUniqueID == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	ret = der_copy_bit_string(&tbs->issuerUniqueID, tbsc->issuerUniqueID);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
    }
    /* subjectUniqueID [2]  IMPLICIT BIT STRING OPTIONAL */
    if (tbs->subjectUniqueID.length) {
	tbsc->subjectUniqueID = calloc(1, sizeof(*tbsc->subjectUniqueID));
	if (tbsc->subjectUniqueID == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}

	ret = der_copy_bit_string(&tbs->subjectUniqueID, tbsc->subjectUniqueID);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
    }

    /* extensions      [3]  EXPLICIT Extensions OPTIONAL */
    tbsc->extensions = calloc(1, sizeof(*tbsc->extensions));
    if (tbsc->extensions == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "Out of memory");
	goto out;
    }

    /* Add the text BMP string Domaincontroller to the cert */
    if (tbs->flags.domaincontroller) {
	data.data = rk_UNCONST("\x1e\x20\x00\x44\x00\x6f\x00\x6d"
			       "\x00\x61\x00\x69\x00\x6e\x00\x43"
			       "\x00\x6f\x00\x6e\x00\x74\x00\x72"
			       "\x00\x6f\x00\x6c\x00\x6c\x00\x65"
			       "\x00\x72");
	data.length = 34;

	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_ms_cert_enroll_domaincontroller,
			    &data);
	if (ret)
	    goto out;
    }

    /* add KeyUsage */
    {
	KeyUsage ku;

	ku = int2KeyUsage(key_usage);
	ASN1_MALLOC_ENCODE(KeyUsage, data.data, data.length, &ku, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 1,
			    &asn1_oid_id_x509_ce_keyUsage, &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* add ExtendedKeyUsage */
    if (tbs->eku.len > 0) {
	ASN1_MALLOC_ENCODE(ExtKeyUsage, data.data, data.length,
			   &tbs->eku, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_x509_ce_extKeyUsage, &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* add Subject Alternative Name */
    if (tbs->san.len > 0) {
	ASN1_MALLOC_ENCODE(GeneralNames, data.data, data.length,
			   &tbs->san, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_x509_ce_subjectAltName,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* Add Authority Key Identifier */
    if (ai) {
	ASN1_MALLOC_ENCODE(AuthorityKeyIdentifier, data.data, data.length,
			   ai, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_x509_ce_authorityKeyIdentifier,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* Add Subject Key Identifier */
    {
	SubjectKeyIdentifier si;
	unsigned char hash[SHA_DIGEST_LENGTH];

	{
	    EVP_MD_CTX *ctx;

	    ctx = EVP_MD_CTX_create();
	    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
	    EVP_DigestUpdate(ctx, tbs->spki.subjectPublicKey.data,
			     tbs->spki.subjectPublicKey.length / 8);
	    EVP_DigestFinal_ex(ctx, hash, NULL);
	    EVP_MD_CTX_destroy(ctx);
	}

	si.data = hash;
	si.length = sizeof(hash);

	ASN1_MALLOC_ENCODE(SubjectKeyIdentifier, data.data, data.length,
			   &si, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_x509_ce_subjectKeyIdentifier,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* Add BasicConstraints */
    {
	BasicConstraints bc;
	int aCA = 1;
	unsigned int path;

	memset(&bc, 0, sizeof(bc));

	if (tbs->flags.ca) {
	    bc.cA = &aCA;
	    if (tbs->pathLenConstraint >= 0) {
		path = tbs->pathLenConstraint;
		bc.pathLenConstraint = &path;
	    }
	}

	ASN1_MALLOC_ENCODE(BasicConstraints, data.data, data.length,
			   &bc, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	/* Critical if this is a CA */
	ret = add_extension(context, tbsc, tbs->flags.ca,
			    &asn1_oid_id_x509_ce_basicConstraints,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    /* add Proxy */
    if (tbs->flags.proxy) {
	ProxyCertInfo info;

	memset(&info, 0, sizeof(info));

	if (tbs->pathLenConstraint >= 0) {
	    info.pCPathLenConstraint =
		malloc(sizeof(*info.pCPathLenConstraint));
	    if (info.pCPathLenConstraint == NULL) {
		ret = ENOMEM;
		hx509_set_error_string(context, 0, ret, "Out of memory");
		goto out;
	    }
	    *info.pCPathLenConstraint = tbs->pathLenConstraint;
	}

	ret = der_copy_oid(&asn1_oid_id_pkix_ppl_inheritAll,
			   &info.proxyPolicy.policyLanguage);
	if (ret) {
	    free_ProxyCertInfo(&info);
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}

	ASN1_MALLOC_ENCODE(ProxyCertInfo, data.data, data.length,
			   &info, &size, ret);
	free_ProxyCertInfo(&info);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, 0,
			    &asn1_oid_id_pkix_pe_proxyCertInfo,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    if (tbs->crldp.len) {

	ASN1_MALLOC_ENCODE(CRLDistributionPoints, data.data, data.length,
			   &tbs->crldp, &size, ret);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	if (size != data.length)
	    _hx509_abort("internal ASN.1 encoder error");
	ret = add_extension(context, tbsc, FALSE,
			    &asn1_oid_id_x509_ce_cRLDistributionPoints,
			    &data);
	free(data.data);
	if (ret)
	    goto out;
    }

    ASN1_MALLOC_ENCODE(TBSCertificate, data.data, data.length,tbsc, &size, ret);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "malloc out of memory");
	goto out;
    }
    if (data.length != size)
	_hx509_abort("internal ASN.1 encoder error");

    ret = _hx509_create_signature_bitstring(context,
					    signer,
					    sigalg,
					    &data,
					    &c.signatureAlgorithm,
					    &c.signatureValue);
    free(data.data);
    if (ret)
	goto out;

    ret = hx509_cert_init(context, &c, certificate);
    if (ret)
	goto out;

    free_Certificate(&c);

    return 0;

out:
    free_Certificate(&c);
    return ret;
}

static int
get_AuthorityKeyIdentifier(hx509_context context,
			   const Certificate *certificate,
			   AuthorityKeyIdentifier *ai)
{
    SubjectKeyIdentifier si;
    int ret;

    ret = _hx509_find_extension_subject_key_id(certificate, &si);
    if (ret == 0) {
	ai->keyIdentifier = calloc(1, sizeof(*ai->keyIdentifier));
	if (ai->keyIdentifier == NULL) {
	    free_SubjectKeyIdentifier(&si);
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	ret = der_copy_octet_string(&si, ai->keyIdentifier);
	free_SubjectKeyIdentifier(&si);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
    } else {
	GeneralNames gns;
	GeneralName gn;
	Name name;

	memset(&gn, 0, sizeof(gn));
	memset(&gns, 0, sizeof(gns));
	memset(&name, 0, sizeof(name));

	ai->authorityCertIssuer =
	    calloc(1, sizeof(*ai->authorityCertIssuer));
	if (ai->authorityCertIssuer == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
	ai->authorityCertSerialNumber =
	    calloc(1, sizeof(*ai->authorityCertSerialNumber));
	if (ai->authorityCertSerialNumber == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}

	/*
	 * XXX unbreak when asn1 compiler handle IMPLICIT
	 *
	 * This is so horrible.
	 */

	ret = copy_Name(&certificate->tbsCertificate.subject, &name);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}

	memset(&gn, 0, sizeof(gn));
	gn.element = choice_GeneralName_directoryName;
	gn.u.directoryName.element =
	    choice_GeneralName_directoryName_rdnSequence;
	gn.u.directoryName.u.rdnSequence = name.u.rdnSequence;

	ret = add_GeneralNames(&gns, &gn);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}

	ai->authorityCertIssuer->val = gns.val;
	ai->authorityCertIssuer->len = gns.len;

	ret = der_copy_heim_integer(&certificate->tbsCertificate.serialNumber,
				    ai->authorityCertSerialNumber);
	if (ai->authorityCertSerialNumber == NULL) {
	    ret = ENOMEM;
	    hx509_set_error_string(context, 0, ret, "Out of memory");
	    goto out;
	}
    }
out:
    if (ret)
	free_AuthorityKeyIdentifier(ai);
    return ret;
}


/**
 * Sign a to-be-signed certificate object with a issuer certificate.
 *
 * The caller needs to at least have called the following functions on the
 * to-be-signed certificate object:
 * - hx509_ca_tbs_init()
 * - hx509_ca_tbs_set_subject()
 * - hx509_ca_tbs_set_spki()
 *
 * When done the to-be-signed certificate object should be freed with
 * hx509_ca_tbs_free().
 *
 * When creating self-signed certificate use hx509_ca_sign_self() instead.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param signer the CA certificate object to sign with (need private key).
 * @param certificate return cerificate, free with hx509_cert_free().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_sign(hx509_context context,
	      hx509_ca_tbs tbs,
	      hx509_cert signer,
	      hx509_cert *certificate)
{
    const Certificate *signer_cert;
    AuthorityKeyIdentifier ai;
    int ret;

    memset(&ai, 0, sizeof(ai));

    signer_cert = _hx509_get_cert(signer);

    ret = get_AuthorityKeyIdentifier(context, signer_cert, &ai);
    if (ret)
	goto out;

    ret = ca_sign(context,
		  tbs,
		  _hx509_cert_private_key(signer),
		  &ai,
		  &signer_cert->tbsCertificate.subject,
		  certificate);

out:
    free_AuthorityKeyIdentifier(&ai);

    return ret;
}

/**
 * Work just like hx509_ca_sign() but signs it-self.
 *
 * @param context A hx509 context.
 * @param tbs object to be signed.
 * @param signer private key to sign with.
 * @param certificate return cerificate, free with hx509_cert_free().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_ca
 */

int
hx509_ca_sign_self(hx509_context context,
		   hx509_ca_tbs tbs,
		   hx509_private_key signer,
		   hx509_cert *certificate)
{
    return ca_sign(context,
		   tbs,
		   signer,
		   NULL,
		   NULL,
		   certificate);
}
