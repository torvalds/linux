/*
 * dane.h -- defines for the DNS-Based Authentication of Named Entities (DANE)
 *                           Transport Layer Security (TLS) Protocol: TLSA
 *
 * Copyright (c) 2012, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

/**
 * \file
 *
 * This module contains base functions for creating and verifying TLSA RR's
 * with PKIX certificates, certificate chains and validation stores.
 * (See RFC6394 and RFC6698).
 * 
 * Since those functions heavily rely op cryptographic operations,
 * this module is dependent on openssl.
 */
 

#ifndef LDNS_DANE_H
#define LDNS_DANE_H

#include <ldns/common.h>
#include <ldns/rdata.h>
#include <ldns/rr.h>
#if LDNS_BUILD_CONFIG_HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The different "Certificate usage" rdata field values for a TLSA RR.
 */
enum ldns_enum_tlsa_certificate_usage
{
	/** CA constraint */
	LDNS_TLSA_USAGE_PKIX_TA				=   0,
	LDNS_TLSA_USAGE_CA_CONSTRAINT			=   0,
	/** Sevice certificate constraint */
	LDNS_TLSA_USAGE_PKIX_EE				=   1,
	LDNS_TLSA_USAGE_SERVICE_CERTIFICATE_CONSTRAINT	=   1,
	/** Trust anchor assertion */
	LDNS_TLSA_USAGE_DANE_TA				=   2,
	LDNS_TLSA_USAGE_TRUST_ANCHOR_ASSERTION		=   2,
	/** Domain issued certificate */
	LDNS_TLSA_USAGE_DANE_EE				=   3,
	LDNS_TLSA_USAGE_DOMAIN_ISSUED_CERTIFICATE	=   3,
	/** Reserved for Private Use */
	LDNS_TLSA_USAGE_PRIVCERT			= 255
};
typedef enum ldns_enum_tlsa_certificate_usage ldns_tlsa_certificate_usage;

/**
 * The different "Selector" rdata field values for a TLSA RR.
 */
enum ldns_enum_tlsa_selector
{
	/** 
	 * Full certificate: the Certificate binary structure
	 * as defined in [RFC5280]
	 */
	LDNS_TLSA_SELECTOR_CERT			=   0,
	LDNS_TLSA_SELECTOR_FULL_CERTIFICATE	=   0,

	/** 
	 * SubjectPublicKeyInfo: DER-encoded binary structure
	 * as defined in [RFC5280]
	 */
	LDNS_TLSA_SELECTOR_SPKI			=   1,
	LDNS_TLSA_SELECTOR_SUBJECTPUBLICKEYINFO	=   1,

	/** Reserved for Private Use */
	LDNS_TLSA_SELECTOR_PRIVSEL		= 255
};
typedef enum ldns_enum_tlsa_selector ldns_tlsa_selector;

/**
 * The different "Matching type" rdata field values for a TLSA RR.
 */
enum ldns_enum_tlsa_matching_type
{
	/** Exact match on selected content */
	LDNS_TLSA_MATCHING_TYPE_FULL		=   0,
	LDNS_TLSA_MATCHING_TYPE_NO_HASH_USED	=   0,
	/** SHA-256 hash of selected content [RFC6234] */
	LDNS_TLSA_MATCHING_TYPE_SHA2_256	=   1,
	LDNS_TLSA_MATCHING_TYPE_SHA256		=   1,
	/** SHA-512 hash of selected content [RFC6234] */
	LDNS_TLSA_MATCHING_TYPE_SHA2_512	=   2,
	LDNS_TLSA_MATCHING_TYPE_SHA512		=   2,
	/** Reserved for Private Use */
	LDNS_TLSA_MATCHING_TYPE_PRIVMATCH	= 255
};
typedef enum ldns_enum_tlsa_matching_type ldns_tlsa_matching_type;

/**
 * Known transports to use with TLSA owner names.
 */
enum ldns_enum_dane_transport
{
	/** TCP */
	LDNS_DANE_TRANSPORT_TCP  = 0,
	/** UDP */
	LDNS_DANE_TRANSPORT_UDP  = 1,
	/** SCTP */
	LDNS_DANE_TRANSPORT_SCTP = 2
};
typedef enum ldns_enum_dane_transport ldns_dane_transport;


#if LDNS_BUILD_CONFIG_USE_DANE
/**
 * Creates a dname consisting of the given name, prefixed by the service port
 * and type of transport: _<EM>port</EM>._<EM>transport</EM>.<EM>name</EM>.
 *
 * \param[out] tlsa_owner The created dname.
 * \param[in] name The dname that should be prefixed.
 * \param[in] port The service port number for wich the name should be created.
 * \param[in] transport The transport for which the name should be created.
 * \return LDNS_STATUS_OK on success or an error code otherwise.
 */
ldns_status ldns_dane_create_tlsa_owner(ldns_rdf** tlsa_owner,
		const ldns_rdf* name, uint16_t port,
		ldns_dane_transport transport);


#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * Creates a LDNS_RDF_TYPE_HEX type rdf based on the binary data chosen by
 * the selector and encoded using matching_type.
 *
 * \param[out] rdf The created created rdf of type LDNS_RDF_TYPE_HEX.
 * \param[in] cert The certificate from which the data is selected
 * \param[in] selector The full certificate or the public key
 * \param[in] matching_type The full data or the SHA256 or SHA512 hash
 *            of the selected data
 * \return LDNS_STATUS_OK on success or an error code otherwise.
 */
ldns_status ldns_dane_cert2rdf(ldns_rdf** rdf, X509* cert,
		ldns_tlsa_selector      selector,
		ldns_tlsa_matching_type matching_type);


/**
 * Selects the certificate from cert, extra_certs or the pkix_validation_store
 * based on the value of cert_usage and index.
 *
 * \param[out] selected_cert The selected cert.
 * \param[in] cert The certificate to validate (or not)
 * \param[in] extra_certs Intermediate certificates that might be necessary
 *            during validation. May be NULL, except when the certificate 
 *            usage is "Trust Anchor Assertion" because the trust anchor has
 *            to be provided.(otherwise choose a "Domain issued certificate!"
 * \param[in] pkix_validation_store Used when the certificate usage is 
 *            "CA constraint" or "Service Certificate Constraint" to 
 *            validate the certificate and, in case of "CA constraint",
 *            select the CA.
 *            When pkix_validation_store is NULL, validation is explicitly
 *            turned off and the behaviour is then the same as for "Trust
 *            anchor assertion" and "Domain issued certificate" respectively.
 * \param[in] cert_usage Which certificate to use and how to validate.
 * \param[in] index Used to select the trust anchor when certificate usage
 *            is "Trust Anchor Assertion". 0 is the last certificate in the
 *            validation chain. 1 the one but last, etc. When index is -1,
 *            the last certificate is used that MUST be self-signed.
 *            This can help to make sure that the intended (self signed)
 *            trust anchor is actually present in extra_certs (which is a
 *            DANE requirement).
 *
 * \return LDNS_STATUS_OK on success or an error code otherwise.
 */
ldns_status ldns_dane_select_certificate(X509** selected_cert,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store,
		ldns_tlsa_certificate_usage cert_usage, int index);

/**
 * Creates a TLSA resource record from the certificate.
 * No PKIX validation is performed! The given certificate is used as data
 * regardless the value of certificate_usage.
 *
 * \param[out] tlsa The created TLSA resource record.
 * \param[in] certificate_usage The value for the Certificate Usage field
 * \param[in] selector The value for the Selector field
 * \param[in] matching_type The value for the Matching Type field
 * \param[in] cert The certificate which data will be represented
 *
 * \return LDNS_STATUS_OK on success or an error code otherwise.
 */
ldns_status ldns_dane_create_tlsa_rr(ldns_rr** tlsa,
		ldns_tlsa_certificate_usage certificate_usage,
		ldns_tlsa_selector          selector,
		ldns_tlsa_matching_type     matching_type,
		X509* cert);

/**
 * BEWARE!  We strongly recommend to use OpenSSL 1.1.0 dane verification
 * functions instead of the ones provided by ldns.  When OpenSSL 1.1.0 was
 * available ldns will use the OpenSSL 1.1.0 dane verification functions
 * under the hood.  When ldns was linked with OpenSSL < 1.1.0, this function
 * will not be able to verify TLSA records with DANE-TA usage types.
 * 
 * BEWARE! The ldns dane verification functions do *not* do server name
 * checks.  The user has to perform additional server name checks themselves!
 *
 * Verify if the given TLSA resource record matches the given certificate.
 * Reporting on a TLSA rr mismatch (LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH)
 * is preferred over PKIX failure  (LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE).
 * So when PKIX validation is required by the TLSA Certificate usage,
 * but the TLSA data does not match, LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH
 * is returned whether the PKIX validated or not.
 *
 * When ldns is linked with OpenSSL < 1.1.0 and this function is available,
 * then the DANE-TA usage type will not be verified, and on a tlsa_rr with
 * this usage type,
 * LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA will be returned.
 *
 * \param[in] tlsa_rr The resource record that specifies what and how to
 *            match the certificate. With tlsa_rr == NULL, regular PKIX
 *            validation is performed.
 * \param[in] cert The certificate to match (and validate)
 * \param[in] extra_certs Intermediate certificates that might be necessary
 *            creating the validation chain.
 * \param[in] pkix_validation_store Used when the certificate usage is 
 *            "CA constraint" or "Service Certificate Constraint" to 
 *            validate the certificate.
 *
 * \return LDNS_STATUS_OK on success,
 *         LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA when the
 *         provided TLSA had the DANE-TA usage type,
 *         LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH on TLSA data mismatch,
 *         LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE when TLSA matched,
 *         but the PKIX validation failed, or other ldns_status errors.
 */
ldns_status ldns_dane_verify_rr(const ldns_rr* tlsa_rr,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store);

/**
 * BEWARE!  We strongly recommend to use OpenSSL 1.1.0 dane verification
 * functions instead of the ones provided by ldns.  When OpenSSL 1.1.0 was
 * available ldns will use the OpenSSL 1.1.0 dane verification functions
 * under the hood.  When ldns was linked with OpenSSL < 1.1.0, this function
 * will not be able to verify TLSA records with DANE-TA usage types.
 * 
 * BEWARE! The ldns dane verification functions do *not* do server name
 * checks.  The user has to perform additional server name checks themselves!
 *
 * Verify if any of the given TLSA resource records matches the given
 * certificate.
 *
 * \param[in] tlsas The resource records that specify what and how to
 *            match the certificate. One must match for this function
 *            to succeed. With tlsas == NULL or the number of TLSA records
 *            in tlsas == 0, regular PKIX validation is performed.
 * \param[in] cert The certificate to match (and validate)
 * \param[in] extra_certs Intermediate certificates that might be necessary
 *            creating the validation chain.
 * \param[in] pkix_validation_store Used when the certificate usage is 
 *            "CA constraint" or "Service Certificate Constraint" to 
 *            validate the certificate.
 *
 * \return LDNS_STATUS_OK on success,
 *         LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA when at least one
 *         of the TLSA's had usage type DANE-TA and none of the TLSA's matched
 *         or PKIX validated,
 *         LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE when one of the TLSA's
 *         matched but the PKIX validation failed,
 *         LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH when none of the TLSA's matched,
 *         or other ldns_status errors.
 */
ldns_status ldns_dane_verify(const ldns_rr_list* tlsas,
		X509* cert, STACK_OF(X509)* extra_certs,
		X509_STORE* pkix_validation_store);
#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */
#endif /* LDNS_BUILD_CONFIG_USE_DANE */

#ifdef __cplusplus
}
#endif

#endif /* LDNS_DANE_H */

