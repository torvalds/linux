/*
 * Verify or create TLS authentication with DANE (RFC6698)
 *
 * (c) NLnetLabs 2012
 *
 * See the file LICENSE for the license.
 *
 */

#include <ldns/config.h>
#ifdef USE_DANE

#include <ldns/ldns.h>
#include <ldns/dane.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif

ldns_status
ldns_dane_create_tlsa_owner(ldns_rdf** tlsa_owner, const ldns_rdf* name,
		uint16_t port, ldns_dane_transport transport)
{
	char buf[LDNS_MAX_DOMAINLEN];
	size_t s;

	assert(tlsa_owner != NULL);
	assert(name != NULL);
	assert(ldns_rdf_get_type(name) == LDNS_RDF_TYPE_DNAME);

	s = (size_t)snprintf(buf, LDNS_MAX_DOMAINLEN, "X_%d", (int)port);
	buf[0] = (char)(s - 1);

	switch(transport) {
	case LDNS_DANE_TRANSPORT_TCP:
		s += snprintf(buf + s, LDNS_MAX_DOMAINLEN - s, "\004_tcp");
		break;
	
	case LDNS_DANE_TRANSPORT_UDP:
		s += snprintf(buf + s, LDNS_MAX_DOMAINLEN - s, "\004_udp");
		break;

	case LDNS_DANE_TRANSPORT_SCTP:
		s += snprintf(buf + s, LDNS_MAX_DOMAINLEN - s, "\005_sctp");
		break;
	
	default:
		return LDNS_STATUS_DANE_UNKNOWN_TRANSPORT;
	}
	if (s + ldns_rdf_size(name) > LDNS_MAX_DOMAINLEN) {
		return LDNS_STATUS_DOMAINNAME_OVERFLOW;
	}
	memcpy(buf + s, ldns_rdf_data(name), ldns_rdf_size(name));
	*tlsa_owner = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_DNAME,
			s + ldns_rdf_size(name), buf);
	if (*tlsa_owner == NULL) {
		return LDNS_STATUS_MEM_ERR;
	}
	return LDNS_STATUS_OK;
}


#ifdef HAVE_SSL
ldns_status
ldns_dane_cert2rdf(ldns_rdf** rdf, X509* cert,
		ldns_tlsa_selector      selector,
		ldns_tlsa_matching_type matching_type)
{
	unsigned char* buf = NULL;
	size_t len;

	X509_PUBKEY* xpubkey;
	EVP_PKEY* epubkey;

	unsigned char* digest;

	assert(rdf != NULL);
	assert(cert != NULL);

	switch(selector) {
	case LDNS_TLSA_SELECTOR_FULL_CERTIFICATE:

		len = (size_t)i2d_X509(cert, &buf);
		break;

	case LDNS_TLSA_SELECTOR_SUBJECTPUBLICKEYINFO:

#ifndef S_SPLINT_S
		xpubkey = X509_get_X509_PUBKEY(cert);
#endif
		if (! xpubkey) {
			return LDNS_STATUS_SSL_ERR;
		}
		epubkey = X509_PUBKEY_get(xpubkey);
		if (! epubkey) {
			return LDNS_STATUS_SSL_ERR;
		}
		len = (size_t)i2d_PUBKEY(epubkey, &buf);
		break;
	
	default:
		return LDNS_STATUS_DANE_UNKNOWN_SELECTOR;
	}

	switch(matching_type) {
	case LDNS_TLSA_MATCHING_TYPE_NO_HASH_USED:

		*rdf = ldns_rdf_new(LDNS_RDF_TYPE_HEX, len, buf);
		
		return *rdf ? LDNS_STATUS_OK : LDNS_STATUS_MEM_ERR;
		break;
	
	case LDNS_TLSA_MATCHING_TYPE_SHA256:

		digest = LDNS_XMALLOC(unsigned char, LDNS_SHA256_DIGEST_LENGTH);
		if (digest == NULL) {
			LDNS_FREE(buf);
			return LDNS_STATUS_MEM_ERR;
		}
		(void) ldns_sha256(buf, (unsigned int)len, digest);
		*rdf = ldns_rdf_new(LDNS_RDF_TYPE_HEX, LDNS_SHA256_DIGEST_LENGTH,
				digest);
		LDNS_FREE(buf);

		return *rdf ? LDNS_STATUS_OK : LDNS_STATUS_MEM_ERR;
		break;

	case LDNS_TLSA_MATCHING_TYPE_SHA512:

		digest = LDNS_XMALLOC(unsigned char, LDNS_SHA512_DIGEST_LENGTH);
		if (digest == NULL) {
			LDNS_FREE(buf);
			return LDNS_STATUS_MEM_ERR;
		}
		(void) ldns_sha512(buf, (unsigned int)len, digest);
		*rdf = ldns_rdf_new(LDNS_RDF_TYPE_HEX, LDNS_SHA512_DIGEST_LENGTH,
				digest);
		LDNS_FREE(buf);

		return *rdf ? LDNS_STATUS_OK : LDNS_STATUS_MEM_ERR;
		break;
	
	default:
		LDNS_FREE(buf);
		return LDNS_STATUS_DANE_UNKNOWN_MATCHING_TYPE;
	}
}


/* Ordinary PKIX validation of cert (with extra_certs to help)
 * against the CA's in store
 */
static ldns_status
ldns_dane_pkix_validate(X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* store)
{
	X509_STORE_CTX* vrfy_ctx;
	ldns_status s;

	if (! store) {
		return LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
	}
	vrfy_ctx = X509_STORE_CTX_new();
	if (! vrfy_ctx) {

		return LDNS_STATUS_SSL_ERR;

	} else if (X509_STORE_CTX_init(vrfy_ctx, store,
				cert, extra_certs) != 1) {
		s = LDNS_STATUS_SSL_ERR;

	} else if (X509_verify_cert(vrfy_ctx) == 1) {

		s = LDNS_STATUS_OK;

	} else {
		s = LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
	}
	X509_STORE_CTX_free(vrfy_ctx);
	return s;
}


/* Orinary PKIX validation of cert (with extra_certs to help)
 * against the CA's in store, but also return the validation chain.
 */
static ldns_status
ldns_dane_pkix_validate_and_get_chain(STACK_OF(X509)** chain, X509* cert,
		STACK_OF(X509)* extra_certs, X509_STORE* store)
{
	ldns_status s;
	X509_STORE* empty_store = NULL;
	X509_STORE_CTX* vrfy_ctx;

	assert(chain != NULL);

	if (! store) {
		store = empty_store = X509_STORE_new();
	}
	s = LDNS_STATUS_SSL_ERR;
	vrfy_ctx = X509_STORE_CTX_new();
	if (! vrfy_ctx) {

		goto exit_free_empty_store;

	} else if (X509_STORE_CTX_init(vrfy_ctx, store,
					cert, extra_certs) != 1) {
		goto exit_free_vrfy_ctx;

	} else if (X509_verify_cert(vrfy_ctx) == 1) {

		s = LDNS_STATUS_OK;

	} else {
		s = LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
	}
	*chain = X509_STORE_CTX_get1_chain(vrfy_ctx);
	if (! *chain) {
		s = LDNS_STATUS_SSL_ERR;
	}

exit_free_vrfy_ctx:
	X509_STORE_CTX_free(vrfy_ctx);

exit_free_empty_store:
	if (empty_store) {
		X509_STORE_free(empty_store);
	}
	return s;
}


/* Return the validation chain that can be build out of cert, with extra_certs.
 */
static ldns_status
ldns_dane_pkix_get_chain(STACK_OF(X509)** chain,
		X509* cert, STACK_OF(X509)* extra_certs)
{
	ldns_status s;
	X509_STORE* empty_store = NULL;
	X509_STORE_CTX* vrfy_ctx;

	assert(chain != NULL);

	empty_store = X509_STORE_new();
	s = LDNS_STATUS_SSL_ERR;
	vrfy_ctx = X509_STORE_CTX_new();
	if (! vrfy_ctx) {

		goto exit_free_empty_store;

	} else if (X509_STORE_CTX_init(vrfy_ctx, empty_store,
					cert, extra_certs) != 1) {
		goto exit_free_vrfy_ctx;
	}
	(void) X509_verify_cert(vrfy_ctx);
	*chain = X509_STORE_CTX_get1_chain(vrfy_ctx);
	if (! *chain) {
		s = LDNS_STATUS_SSL_ERR;
	} else {
		s = LDNS_STATUS_OK;
	}
exit_free_vrfy_ctx:
	X509_STORE_CTX_free(vrfy_ctx);

exit_free_empty_store:
	X509_STORE_free(empty_store);
	return s;
}


/* Pop n+1 certs and return the last popped.
 */
static ldns_status
ldns_dane_get_nth_cert_from_validation_chain(
		X509** cert, STACK_OF(X509)* chain, int n, bool ca)
{
	if (n >= sk_X509_num(chain) || n < 0) {
		return LDNS_STATUS_DANE_OFFSET_OUT_OF_RANGE;
	}
	*cert = sk_X509_pop(chain);
	while (n-- > 0) {
		X509_free(*cert);
		*cert = sk_X509_pop(chain);
	}
	if (ca && ! X509_check_ca(*cert)) {
		return LDNS_STATUS_DANE_NON_CA_CERTIFICATE;
	}
	return LDNS_STATUS_OK;
}


/* Create validation chain with cert and extra_certs and returns the last
 * self-signed (if present).
 */
static ldns_status
ldns_dane_pkix_get_last_self_signed(X509** out_cert,
		X509* cert, STACK_OF(X509)* extra_certs)
{
	ldns_status s;
	X509_STORE* empty_store = NULL;
	X509_STORE_CTX* vrfy_ctx;

	assert(out_cert != NULL);

	empty_store = X509_STORE_new();
	s = LDNS_STATUS_SSL_ERR;
	vrfy_ctx = X509_STORE_CTX_new();
	if (! vrfy_ctx) {
		goto exit_free_empty_store;

	} else if (X509_STORE_CTX_init(vrfy_ctx, empty_store,
					cert, extra_certs) != 1) {
		goto exit_free_vrfy_ctx;

	}
	(void) X509_verify_cert(vrfy_ctx);
	if (X509_STORE_CTX_get_error(vrfy_ctx) == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN ||
	    X509_STORE_CTX_get_error(vrfy_ctx) == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT){

		*out_cert = X509_STORE_CTX_get_current_cert( vrfy_ctx);
		s = LDNS_STATUS_OK;
	} else {
		s = LDNS_STATUS_DANE_PKIX_NO_SELF_SIGNED_TRUST_ANCHOR;
	}
exit_free_vrfy_ctx:
	X509_STORE_CTX_free(vrfy_ctx);

exit_free_empty_store:
	X509_STORE_free(empty_store);
	return s;
}


ldns_status
ldns_dane_select_certificate(X509** selected_cert,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store,
		ldns_tlsa_certificate_usage cert_usage, int offset)
{
	ldns_status s;
	STACK_OF(X509)* pkix_validation_chain = NULL;

	assert(selected_cert != NULL);
	assert(cert != NULL);

	/* With PKIX validation explicitly turned off (pkix_validation_store
	 *  == NULL), treat the "CA constraint" and "Service certificate
	 * constraint" the same as "Trust anchor assertion" and "Domain issued
	 * certificate" respectively.
	 */
	if (pkix_validation_store == NULL) {
		switch (cert_usage) {

		case LDNS_TLSA_USAGE_CA_CONSTRAINT:

			cert_usage = LDNS_TLSA_USAGE_TRUST_ANCHOR_ASSERTION;
			break;

		case LDNS_TLSA_USAGE_SERVICE_CERTIFICATE_CONSTRAINT:

			cert_usage = LDNS_TLSA_USAGE_DOMAIN_ISSUED_CERTIFICATE;
			break;

		default:
			break;
		}
	}

	/* Now what to do with each Certificate usage...
	 */
	switch (cert_usage) {

	case LDNS_TLSA_USAGE_CA_CONSTRAINT:

		s = ldns_dane_pkix_validate_and_get_chain(
				&pkix_validation_chain,
				cert, extra_certs,
				pkix_validation_store);
		if (! pkix_validation_chain) {
			return s;
		}
		if (s == LDNS_STATUS_OK) {
			if (offset == -1) {
				offset = 0;
			}
			s = ldns_dane_get_nth_cert_from_validation_chain(
					selected_cert, pkix_validation_chain,
					offset, true);
		}
		sk_X509_pop_free(pkix_validation_chain, X509_free);
		return s;
		break;


	case LDNS_TLSA_USAGE_SERVICE_CERTIFICATE_CONSTRAINT:

		*selected_cert = cert;
		return ldns_dane_pkix_validate(cert, extra_certs,
				pkix_validation_store);
		break;


	case LDNS_TLSA_USAGE_TRUST_ANCHOR_ASSERTION:

		if (offset == -1) {
			s = ldns_dane_pkix_get_last_self_signed(
					selected_cert, cert, extra_certs);
			return s;
		} else {
			s = ldns_dane_pkix_get_chain(
					&pkix_validation_chain,
					cert, extra_certs);
			if (s == LDNS_STATUS_OK) {
				s =
				ldns_dane_get_nth_cert_from_validation_chain(
					selected_cert, pkix_validation_chain,
					offset, false);
			} else if (! pkix_validation_chain) {
				return s;
			}
			sk_X509_pop_free(pkix_validation_chain, X509_free);
			return s;
		}
		break;


	case LDNS_TLSA_USAGE_DOMAIN_ISSUED_CERTIFICATE:

		*selected_cert = cert;
		return LDNS_STATUS_OK;
		break;
	
	default:
		return LDNS_STATUS_DANE_UNKNOWN_CERTIFICATE_USAGE;
		break;
	}
}


ldns_status
ldns_dane_create_tlsa_rr(ldns_rr** tlsa,
		ldns_tlsa_certificate_usage certificate_usage,
		ldns_tlsa_selector          selector,
		ldns_tlsa_matching_type     matching_type,
		X509* cert)
{
	ldns_rdf* rdf;
	ldns_status s;

	assert(tlsa != NULL);
	assert(cert != NULL);

	/* create rr */
	*tlsa = ldns_rr_new_frm_type(LDNS_RR_TYPE_TLSA);
	if (*tlsa == NULL) {
		return LDNS_STATUS_MEM_ERR;
	}

	rdf = ldns_native2rdf_int8(LDNS_RDF_TYPE_INT8,
			(uint8_t)certificate_usage);
	if (rdf == NULL) {
		goto memerror;
	}
	(void) ldns_rr_set_rdf(*tlsa, rdf, 0);

	rdf = ldns_native2rdf_int8(LDNS_RDF_TYPE_INT8, (uint8_t)selector);
	if (rdf == NULL) {
		goto memerror;
	}
	(void) ldns_rr_set_rdf(*tlsa, rdf, 1);

	rdf = ldns_native2rdf_int8(LDNS_RDF_TYPE_INT8, (uint8_t)matching_type);
	if (rdf == NULL) {
		goto memerror;
	}
	(void) ldns_rr_set_rdf(*tlsa, rdf, 2);

	s = ldns_dane_cert2rdf(&rdf, cert, selector, matching_type);
	if (s == LDNS_STATUS_OK) {
		(void) ldns_rr_set_rdf(*tlsa, rdf, 3);
		return LDNS_STATUS_OK;
	}
	ldns_rr_free(*tlsa);
	*tlsa = NULL;
	return s;

memerror:
	ldns_rr_free(*tlsa);
	*tlsa = NULL;
	return LDNS_STATUS_MEM_ERR;
}


#ifdef USE_DANE_VERIFY
/* Return tlsas that actually are TLSA resource records with known values
 * for the Certificate usage, Selector and Matching type rdata fields.
 */
static ldns_rr_list*
ldns_dane_filter_unusable_records(const ldns_rr_list* tlsas)
{
	size_t i;
	ldns_rr_list* r = ldns_rr_list_new();
	ldns_rr* tlsa_rr;

	if (! r) {
		return NULL;
	}
	for (i = 0; i < ldns_rr_list_rr_count(tlsas); i++) {
		tlsa_rr = ldns_rr_list_rr(tlsas, i);
		if (ldns_rr_get_type(tlsa_rr) == LDNS_RR_TYPE_TLSA &&
		    ldns_rr_rd_count(tlsa_rr) == 4 &&
		    ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 0)) <= 3 &&
		    ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 1)) <= 1 &&
		    ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 2)) <= 2) {

			if (! ldns_rr_list_push_rr(r, tlsa_rr)) {
				ldns_rr_list_free(r);
				return NULL;
			}
		}
	}
	return r;
}


#if !defined(USE_DANE_TA_USAGE)
/* Return whether cert/selector/matching_type matches data.
 */
static ldns_status
ldns_dane_match_cert_with_data(X509* cert, ldns_tlsa_selector selector,
		ldns_tlsa_matching_type matching_type, ldns_rdf* data)
{
	ldns_status s;
	ldns_rdf* match_data;

	s = ldns_dane_cert2rdf(&match_data, cert, selector, matching_type);
	if (s == LDNS_STATUS_OK) {
		if (ldns_rdf_compare(data, match_data) != 0) {
			s = LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH;
		}
		ldns_rdf_free(match_data);
	}
	return s;
}


/* Return whether any certificate from the chain with selector/matching_type
 * matches data.
 * ca should be true if the certificate has to be a CA certificate too.
 */
static ldns_status
ldns_dane_match_any_cert_with_data(STACK_OF(X509)* chain,
		ldns_tlsa_selector      selector,
		ldns_tlsa_matching_type matching_type,
		ldns_rdf* data, bool ca)
{
	ldns_status s = LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH;
	size_t n, i;
	X509* cert;

	n = (size_t)sk_X509_num(chain);
	for (i = 0; i < n; i++) {
		cert = sk_X509_pop(chain);
		if (! cert) {
			s = LDNS_STATUS_SSL_ERR;
			break;
		}
		s = ldns_dane_match_cert_with_data(cert,
				selector, matching_type, data);
		if (ca && s == LDNS_STATUS_OK && ! X509_check_ca(cert)) {
			s = LDNS_STATUS_DANE_NON_CA_CERTIFICATE;
		}
		X509_free(cert);
		if (s != LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH) {
			break;
		}
		/* when s == LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH,
		 * try to match the next certificate
		 */
	}
	return s;
}
#endif /* !defined(USE_DANE_TA_USAGE) */
#endif /* USE_DANE_VERIFY */

#ifdef USE_DANE_VERIFY
ldns_status
ldns_dane_verify_rr(const ldns_rr* tlsa_rr,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store)
{
#if defined(USE_DANE_TA_USAGE)
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	X509_STORE_CTX *store_ctx = NULL;
#else
	STACK_OF(X509)* pkix_validation_chain = NULL;
#endif
	ldns_status s = LDNS_STATUS_OK;

	ldns_tlsa_certificate_usage usage;
	ldns_tlsa_selector          selector;
	ldns_tlsa_matching_type     mtype;
	ldns_rdf*                   data;

	if (! tlsa_rr || ldns_rr_get_type(tlsa_rr) != LDNS_RR_TYPE_TLSA ||
			ldns_rr_rd_count(tlsa_rr) != 4 ||
			ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 0)) > 3 ||
			ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 1)) > 1 ||
			ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 2)) > 2 ) {
		/* No (usable) TLSA, so regular PKIX validation
		 */
		return ldns_dane_pkix_validate(cert, extra_certs,
				pkix_validation_store);
	}
	usage    = ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 0));
	selector = ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 1));
	mtype    = ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr, 2));
	data     =                      ldns_rr_rdf(tlsa_rr, 3) ;

#if defined(USE_DANE_TA_USAGE)
	/* Rely on OpenSSL dane functions.
	 *
	 * OpenSSL does not provide offline dane verification.  The dane unit
	 * tests within openssl use the undocumented SSL_get0_dane() and 
	 * X509_STORE_CTX_set0_dane() to convey dane parameters set on SSL and
	 * SSL_CTX to a X509_STORE_CTX that can be used to do offline
	 * verification.  We use these undocumented means with the ldns
	 * dane function prototypes which did only offline dane verification.
	 */
	if (!(ssl_ctx = SSL_CTX_new(TLS_client_method())))
		s = LDNS_STATUS_MEM_ERR;

	else if (SSL_CTX_dane_enable(ssl_ctx) <= 0)
		s = LDNS_STATUS_SSL_ERR;

	else if (SSL_CTX_dane_set_flags(
				ssl_ctx, DANE_FLAG_NO_DANE_EE_NAMECHECKS),
			!(ssl = SSL_new(ssl_ctx)))
		s = LDNS_STATUS_MEM_ERR;

	else if (SSL_set_connect_state(ssl),
			(SSL_dane_enable(ssl, NULL) <= 0))
		s = LDNS_STATUS_SSL_ERR;

	else if (SSL_dane_tlsa_add(ssl, usage, selector, mtype,
				ldns_rdf_data(data), ldns_rdf_size(data)) <= 0)
		s = LDNS_STATUS_SSL_ERR;

	else if (!(store_ctx =  X509_STORE_CTX_new()))
		s = LDNS_STATUS_MEM_ERR;

	else if (!X509_STORE_CTX_init(store_ctx, pkix_validation_store, cert, extra_certs))
		s = LDNS_STATUS_SSL_ERR;

	else {
		int ret;

		X509_STORE_CTX_set_default(store_ctx,
				SSL_is_server(ssl) ? "ssl_client" : "ssl_server");
		X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx),
				SSL_get0_param(ssl));
		X509_STORE_CTX_set0_dane(store_ctx, SSL_get0_dane(ssl));
		if (SSL_get_verify_callback(ssl))
			X509_STORE_CTX_set_verify_cb(store_ctx, SSL_get_verify_callback(ssl));

		ret = X509_verify_cert(store_ctx);
		if (!ret) {
			if (X509_STORE_CTX_get_error(store_ctx) == X509_V_ERR_DANE_NO_MATCH)
				s = LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH;
			else
				s = LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
		}
		X509_STORE_CTX_cleanup(store_ctx);
	}
	if (store_ctx)
		X509_STORE_CTX_free(store_ctx);
	if (ssl)
		SSL_free(ssl);
	if (ssl_ctx)
		SSL_CTX_free(ssl_ctx);
	return s;
#else
	switch (usage) {
	case LDNS_TLSA_USAGE_CA_CONSTRAINT:
		s = ldns_dane_pkix_validate_and_get_chain(
				&pkix_validation_chain, 
				cert, extra_certs,
				pkix_validation_store);
		if (! pkix_validation_chain) {
			return s;
		}
		if (s == LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE) {
			/*
			 * NO PKIX validation. We still try to match *any*
			 * certificate from the chain, so we return
			 * TLSA errors over PKIX errors.
			 *
			 * i.e. When the TLSA matches no certificate, we return
			 * TLSA_DID_NOT_MATCH and not PKIX_DID_NOT_VALIDATE
			 */
			s = ldns_dane_match_any_cert_with_data(
					pkix_validation_chain,
					selector, mtype, data, true);

			if (s == LDNS_STATUS_OK) {
				/* A TLSA record did match a cert from the
				 * chain, thus the error is failed PKIX
				 * validation.
				 */
				s = LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
			}

		} else if (s == LDNS_STATUS_OK) { 
			/* PKIX validated, does the TLSA match too? */

			s = ldns_dane_match_any_cert_with_data(
					pkix_validation_chain,
					selector, mtype, data, true);
		}
		sk_X509_pop_free(pkix_validation_chain, X509_free);
		return s;
		break;

	case LDNS_TLSA_USAGE_SERVICE_CERTIFICATE_CONSTRAINT:

		s = ldns_dane_match_cert_with_data(cert,
				selector, mtype, data);

		if (s == LDNS_STATUS_OK) {
			return ldns_dane_pkix_validate(cert, extra_certs,
					pkix_validation_store);
		}
		return s;
		break;

	case LDNS_TLSA_USAGE_TRUST_ANCHOR_ASSERTION:
#if 0
		s = ldns_dane_pkix_get_chain(&pkix_validation_chain,
				cert, extra_certs);

		if (s == LDNS_STATUS_OK) {
			s = ldns_dane_match_any_cert_with_data(
					pkix_validation_chain,
					selector, mtype, data, false);

		} else if (! pkix_validation_chain) {
			return s;
		}
		sk_X509_pop_free(pkix_validation_chain, X509_free);
		return s;
#else
		return LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA;
#endif
		break;

	case LDNS_TLSA_USAGE_DOMAIN_ISSUED_CERTIFICATE:
		return ldns_dane_match_cert_with_data(cert,
				selector, mtype, data);
		break;

	default:
		break;
	}
#endif
	return LDNS_STATUS_DANE_UNKNOWN_CERTIFICATE_USAGE;
}


ldns_status
ldns_dane_verify(const ldns_rr_list* tlsas,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store)
{
#if defined(USE_DANE_TA_USAGE)
	SSL_CTX *ssl_ctx = NULL;
	ldns_rdf *basename_rdf = NULL;
	char *basename = NULL;
	SSL *ssl = NULL;
	X509_STORE_CTX *store_ctx = NULL;
#else
	ldns_status ps;
#endif
	size_t i;
	ldns_rr* tlsa_rr;
	ldns_rr_list *usable_tlsas;
	ldns_status s = LDNS_STATUS_OK;

	assert(cert != NULL);

	if (! tlsas || ldns_rr_list_rr_count(tlsas) == 0)
		/* No TLSA's, so regular PKIX validation
		 */
		return ldns_dane_pkix_validate(cert, extra_certs,
				pkix_validation_store);

/* To enable name checks (which we don't) */
#if defined(USE_DANE_TA_USAGE) && 0
	else if (!(basename_rdf = ldns_dname_clone_from(
					ldns_rr_list_owner(tlsas), 2)))
		/* Could nog get DANE base name */
		s = LDNS_STATUS_ERR;

	else if (!(basename = ldns_rdf2str(basename_rdf)))
		s = LDNS_STATUS_MEM_ERR;

	else if (strlen(basename) && (basename[strlen(basename)-1]  = 0))
		s = LDNS_STATUS_ERR; /* Intended to be unreachable */
#endif

	else if (!(usable_tlsas = ldns_dane_filter_unusable_records(tlsas)))
		return LDNS_STATUS_MEM_ERR;

	else if (ldns_rr_list_rr_count(usable_tlsas) == 0) {
		/* No TLSA's, so regular PKIX validation
		 */
		ldns_rr_list_free(usable_tlsas);
		return ldns_dane_pkix_validate(cert, extra_certs,
				pkix_validation_store);
	}
#if defined(USE_DANE_TA_USAGE)
	/* Rely on OpenSSL dane functions.
	 *
	 * OpenSSL does not provide offline dane verification.  The dane unit
	 * tests within openssl use the undocumented SSL_get0_dane() and 
	 * X509_STORE_CTX_set0_dane() to convey dane parameters set on SSL and
	 * SSL_CTX to a X509_STORE_CTX that can be used to do offline
	 * verification.  We use these undocumented means with the ldns
	 * dane function prototypes which did only offline dane verification.
	 */
	if (!(ssl_ctx = SSL_CTX_new(TLS_client_method())))
		s = LDNS_STATUS_MEM_ERR;

	else if (SSL_CTX_dane_enable(ssl_ctx) <= 0)
		s = LDNS_STATUS_SSL_ERR;

	else if (SSL_CTX_dane_set_flags(
				ssl_ctx, DANE_FLAG_NO_DANE_EE_NAMECHECKS),
			!(ssl = SSL_new(ssl_ctx)))
		s = LDNS_STATUS_MEM_ERR;

	else if (SSL_set_connect_state(ssl),
			(SSL_dane_enable(ssl, basename) <= 0))
		s = LDNS_STATUS_SSL_ERR;

	else for (i = 0; i < ldns_rr_list_rr_count(usable_tlsas); i++) {
		ldns_tlsa_certificate_usage usage;
		ldns_tlsa_selector          selector;
		ldns_tlsa_matching_type     mtype;
		ldns_rdf*                   data;

		tlsa_rr = ldns_rr_list_rr(usable_tlsas, i);
		usage   = ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr,0));
		selector= ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr,1));
		mtype   = ldns_rdf2native_int8(ldns_rr_rdf(tlsa_rr,2));
		data    =                      ldns_rr_rdf(tlsa_rr,3) ;

		if (SSL_dane_tlsa_add(ssl, usage, selector, mtype,
					ldns_rdf_data(data),
					ldns_rdf_size(data)) <= 0) {
			s = LDNS_STATUS_SSL_ERR;
			break;
		}
	}
	if (!s && !(store_ctx =  X509_STORE_CTX_new()))
		s = LDNS_STATUS_MEM_ERR;

	else if (!X509_STORE_CTX_init(store_ctx, pkix_validation_store, cert, extra_certs))
		s = LDNS_STATUS_SSL_ERR;

	else {
		int ret;

		X509_STORE_CTX_set_default(store_ctx,
				SSL_is_server(ssl) ? "ssl_client" : "ssl_server");
		X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx),
				SSL_get0_param(ssl));
		X509_STORE_CTX_set0_dane(store_ctx, SSL_get0_dane(ssl));
		if (SSL_get_verify_callback(ssl))
			X509_STORE_CTX_set_verify_cb(store_ctx, SSL_get_verify_callback(ssl));

		ret = X509_verify_cert(store_ctx);
		if (!ret) {
			if (X509_STORE_CTX_get_error(store_ctx) == X509_V_ERR_DANE_NO_MATCH)
				s = LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH;
			else
				s = LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE;
		}
		X509_STORE_CTX_cleanup(store_ctx);
	}
	if (store_ctx)
		X509_STORE_CTX_free(store_ctx);
	if (ssl)
		SSL_free(ssl);
	if (ssl_ctx)
		SSL_CTX_free(ssl_ctx);
	if (basename)
		free(basename);
	ldns_rdf_deep_free(basename_rdf);
#else
	for (i = 0; i < ldns_rr_list_rr_count(usable_tlsas); i++) {
		tlsa_rr = ldns_rr_list_rr(usable_tlsas, i);
		ps = s;
		s = ldns_dane_verify_rr(tlsa_rr, cert, extra_certs,
				pkix_validation_store);

		if (s != LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH &&
		    s != LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE &&
		    s != LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA) {

			/* which would be LDNS_STATUS_OK (match)
			 * or some fatal error preventing use from
			 * trying the next TLSA record.
			 */
			break;
		}
		s = (s > ps ? s : ps); /* pref NEED_OPENSSL_GE_1_1_FOR_DANE_TA
		                        * over PKIX_DID_NOT_VALIDATE
					* over TLSA_DID_NOT_MATCH
					*/
	}
#endif
	ldns_rr_list_free(usable_tlsas);
	return s;
}
#endif /* USE_DANE_VERIFY */
#endif /* HAVE_SSL */
#endif /* USE_DANE */
