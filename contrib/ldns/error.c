/*
 * a error2str function to make sense of all the
 * error codes we have laying ardoun
 *
 * a Net::DNS like library for C
 * LibDNS Team @ NLnet Labs
 * (c) NLnet Labs, 2005-2006
 * See the file LICENSE for the license
 */

#include <ldns/config.h>

#include <ldns/ldns.h>

ldns_lookup_table ldns_error_str[] = {
	{ LDNS_STATUS_OK, "All OK" },
	{ LDNS_STATUS_EMPTY_LABEL, "Empty label" },
        { LDNS_STATUS_LABEL_OVERFLOW, "Label length overflow" },
        { LDNS_STATUS_DOMAINNAME_OVERFLOW, "Domainname length overflow" },
        { LDNS_STATUS_DOMAINNAME_UNDERFLOW, "Domainname length underflow (zero length)" },
        { LDNS_STATUS_DDD_OVERFLOW, "\\DDD sequence overflow (>255)" },
        { LDNS_STATUS_PACKET_OVERFLOW, "Packet size overflow" },
        { LDNS_STATUS_INVALID_POINTER, "Invalid compression pointer" },
        { LDNS_STATUS_MEM_ERR, "General memory error" },
        { LDNS_STATUS_INTERNAL_ERR, "Internal error, this should not happen" },
        { LDNS_STATUS_SSL_ERR, "Error in SSL library" },
        { LDNS_STATUS_ERR, "General LDNS error" },
        { LDNS_STATUS_INVALID_INT, "Conversion error, integer expected" },
        { LDNS_STATUS_INVALID_IP4, "Conversion error, ip4 addr expected" },
        { LDNS_STATUS_INVALID_IP6, "Conversion error, ip6 addr expected" },
        { LDNS_STATUS_INVALID_STR, "Conversion error, string expected" },
	{ LDNS_STATUS_INVALID_B32_EXT, "Conversion error, b32 ext encoding expected" },
        { LDNS_STATUS_INVALID_B64, "Conversion error, b64 encoding expected" },
        { LDNS_STATUS_INVALID_HEX, "Conversion error, hex encoding expected" },
        { LDNS_STATUS_INVALID_TIME, "Conversion error, time encoding expected" },
        { LDNS_STATUS_NETWORK_ERR, "Could not send or receive, because of network error" },
        { LDNS_STATUS_ADDRESS_ERR, "Could not start AXFR, because of address error" },
        { LDNS_STATUS_FILE_ERR, "Could not open the files" },
        { LDNS_STATUS_UNKNOWN_INET, "Uknown address family" },
        { LDNS_STATUS_NOT_IMPL, "This function is not implemented (yet), please notify the developers - or not..." },
	{ LDNS_STATUS_NULL, "Supplied value pointer null" },
        { LDNS_STATUS_CRYPTO_UNKNOWN_ALGO, "Unknown cryptographic algorithm" },
        { LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL, "Cryptographic algorithm not implemented" },
        { LDNS_STATUS_CRYPTO_NO_RRSIG, "No DNSSEC signature(s)" },
        { LDNS_STATUS_CRYPTO_NO_DNSKEY, "No DNSSEC public key(s)" },
        { LDNS_STATUS_CRYPTO_TYPE_COVERED_ERR, "The signature does not cover this RRset" },
        { LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY, "No signatures found for trusted DNSSEC public key(s)" },
        { LDNS_STATUS_CRYPTO_NO_DS, "No DS record(s)" },
        { LDNS_STATUS_CRYPTO_NO_TRUSTED_DS, "Could not validate DS record(s)" },
        { LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY, "No keys with the keytag and algorithm from the RRSIG found" },
        { LDNS_STATUS_CRYPTO_VALIDATED, "Valid DNSSEC signature" },
        { LDNS_STATUS_CRYPTO_BOGUS, "Bogus DNSSEC signature" },
        { LDNS_STATUS_CRYPTO_SIG_EXPIRED, "DNSSEC signature has expired" },
        { LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED, "DNSSEC signature not incepted yet" },
	{ LDNS_STATUS_CRYPTO_TSIG_BOGUS, "Bogus TSIG signature" },
	{ LDNS_STATUS_CRYPTO_TSIG_ERR, "Could not create TSIG signature" },
        { LDNS_STATUS_CRYPTO_EXPIRATION_BEFORE_INCEPTION, "DNSSEC signature has expiration date earlier than inception date" },
	{ LDNS_STATUS_ENGINE_KEY_NOT_LOADED, "Unable to load private key from engine" },
        { LDNS_STATUS_NSEC3_ERR, "Error in NSEC3 denial of existence proof" },
	{ LDNS_STATUS_RES_NO_NS, "No (valid) nameservers defined in the resolver" },
	{ LDNS_STATUS_RES_QUERY, "No correct query given to resolver" },
	{ LDNS_STATUS_WIRE_INCOMPLETE_HEADER, "header section incomplete" },
	{ LDNS_STATUS_WIRE_INCOMPLETE_QUESTION, "question section incomplete" },
	{ LDNS_STATUS_WIRE_INCOMPLETE_ANSWER, "answer section incomplete" },
	{ LDNS_STATUS_WIRE_INCOMPLETE_AUTHORITY, "authority section incomplete" },
	{ LDNS_STATUS_WIRE_INCOMPLETE_ADDITIONAL, "additional section incomplete" },
	{ LDNS_STATUS_NO_DATA, "No data" },
	{ LDNS_STATUS_EXISTS_ERR, "Element already exists" },
	{ LDNS_STATUS_CERT_BAD_ALGORITHM, "Bad algorithm type for CERT record" },
	{ LDNS_STATUS_SYNTAX_TYPE_ERR, "Syntax error, could not parse the RR's type" },
	{ LDNS_STATUS_SYNTAX_CLASS_ERR, "Syntax error, could not parse the RR's class" },
	{ LDNS_STATUS_SYNTAX_TTL_ERR, "Syntax error, could not parse the RR's TTL" },
	{ LDNS_STATUS_SYNTAX_INCLUDE_ERR_NOTIMPL, "Syntax error, $INCLUDE not implemented" },
	{ LDNS_STATUS_SYNTAX_RDATA_ERR, "Syntax error, could not parse the RR's rdata" },
	{ LDNS_STATUS_SYNTAX_DNAME_ERR, "Syntax error, could not parse the RR's dname(s)" },
	{ LDNS_STATUS_SYNTAX_VERSION_ERR, "Syntax error, version mismatch" },
	{ LDNS_STATUS_SYNTAX_ALG_ERR, "Syntax error, algorithm unknown or non parseable" },
	{ LDNS_STATUS_SYNTAX_KEYWORD_ERR, "Syntax error, unknown keyword in input" },
	{ LDNS_STATUS_SYNTAX_ERR, "Syntax error, could not parse the RR" },
	{ LDNS_STATUS_SYNTAX_EMPTY, "Empty line was returned" },
	{ LDNS_STATUS_SYNTAX_TTL, "$TTL directive was seen in the zone" },
	{ LDNS_STATUS_SYNTAX_ORIGIN, "$ORIGIN directive was seen in the zone" },
	{ LDNS_STATUS_SYNTAX_INCLUDE, "$INCLUDE directive was seen in the zone" },
	{ LDNS_STATUS_SYNTAX_ITERATIONS_OVERFLOW, "Iterations count for NSEC3 record higher than maximum" },
	{ LDNS_STATUS_SYNTAX_MISSING_VALUE_ERR, "Syntax error, value expected" },
	{ LDNS_STATUS_SYNTAX_INTEGER_OVERFLOW, "Syntax error, integer value too large" },
	{ LDNS_STATUS_SYNTAX_BAD_ESCAPE, "Syntax error, bad escape sequence" },
	{ LDNS_STATUS_SOCKET_ERROR, "Error creating socket" },
	{ LDNS_STATUS_DNSSEC_EXISTENCE_DENIED, "Existence denied by NSEC" },
	{ LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED, "RR not covered by the given NSEC RRs" },
	{ LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED, "wildcard not covered by the given NSEC RRs" },
	{ LDNS_STATUS_DNSSEC_NSEC3_ORIGINAL_NOT_FOUND, "original of NSEC3 hashed name could not be found" },
	{ LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG, "The RRSIG has to few rdata fields" },
	{ LDNS_STATUS_MISSING_RDATA_FIELDS_KEY, "The DNSKEY has to few rdata fields" },
	{ LDNS_STATUS_CRYPTO_SIG_EXPIRED_WITHIN_MARGIN, 
		"DNSSEC signature will expire too soon" },
	{ LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED_WITHIN_MARGIN,
		"DNSSEC signature not incepted long enough" },
	{ LDNS_STATUS_DANE_UNKNOWN_CERTIFICATE_USAGE,
		"Unknown TLSA Certificate Usage" },
	{ LDNS_STATUS_DANE_UNKNOWN_SELECTOR, "Unknown TLSA Selector" },
	{ LDNS_STATUS_DANE_UNKNOWN_MATCHING_TYPE,
		"Unknown TLSA Matching Type" },
	{ LDNS_STATUS_DANE_UNKNOWN_PROTOCOL,
		"Unknown protocol. Only IPv4 and IPv6 are understood" },
	{ LDNS_STATUS_DANE_UNKNOWN_TRANSPORT,
		"Unknown transport. Should be one of {tcp, udp, sctp}" },
	{ LDNS_STATUS_DANE_MISSING_EXTRA_CERTS,  /* Trust anchor assertion */
		"More than one certificate should be provided" },
	{ LDNS_STATUS_DANE_EXTRA_CERTS_NOT_USED, /* Trust anchor assertion */
		"Non of the extra certificates is used to sign the first" },
	{ LDNS_STATUS_DANE_OFFSET_OUT_OF_RANGE,   /* Trust anchor assertion */
		"The offset was out of range" },
	{ LDNS_STATUS_DANE_INSECURE,             /* Unused by library */
		"The queried resource records were insecure" },
	{ LDNS_STATUS_DANE_BOGUS,             /* Unused by library */
		"The queried resource records were bogus" },
	{ LDNS_STATUS_DANE_TLSA_DID_NOT_MATCH,
		"The TLSA record(s) "
		"did not match with the server certificate (chain)" },
	{ LDNS_STATUS_DANE_NON_CA_CERTIFICATE,
		"The certificate was not a CA certificate" },
	{ LDNS_STATUS_DANE_PKIX_DID_NOT_VALIDATE,
		"Could not PKIX validate" },
	{ LDNS_STATUS_DANE_PKIX_NO_SELF_SIGNED_TRUST_ANCHOR,
		"The validation path "
		"did not end in a self-signed certificate" },
        { LDNS_STATUS_INVALID_ILNP64, 
		"Conversion error, 4 colon separated hex numbers expected" },
        { LDNS_STATUS_INVALID_EUI48, 
		"Conversion error, 6 two character hex numbers "
		"separated by dashes expected (i.e. xx-xx-xx-xx-xx-xx" },
        { LDNS_STATUS_INVALID_EUI64, 
		"Conversion error, 8 two character hex numbers "
		"separated by dashes expected (i.e. xx-xx-xx-xx-xx-xx-xx-xx" },
	{ LDNS_STATUS_WIRE_RDATA_ERR, "invalid rdata in wire format" },
        { LDNS_STATUS_INVALID_TAG, 
		"Conversion error, a non-zero sequence of US-ASCII letters "
		"and numbers in lower case expected" },
        { LDNS_STATUS_TYPE_NOT_IN_BITMAP, 
		"The RR type bitmap rdata field did not have "
		"a bit reserved for the specific RR type" },
        { LDNS_STATUS_INVALID_RDF_TYPE, 
		"The rdata field was not of the expected type" },
        { LDNS_STATUS_RDATA_OVERFLOW, "Rdata size overflow" },
	{ LDNS_STATUS_SYNTAX_SUPERFLUOUS_TEXT_ERR,
		"Syntax error, superfluous text present" },
        { LDNS_STATUS_NSEC3_DOMAINNAME_OVERFLOW,
		"The NSEC3 domainname length overflow" },
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(HAVE_LIBRESSL)
        { LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA,
		"ldns needs to be linked with OpenSSL >= 1.1.0 to be able "
       		"to verify the DANE-TA usage type." },
#else
        { LDNS_STATUS_DANE_NEED_OPENSSL_GE_1_1_FOR_DANE_TA,
		"ldns depends on the availability of the SSL_get0_dane() and "
		"X509_STORE_CTX_set0_dane() functions within OpenSSL >= 1.1.0 "
		"to be able to verify the DANE-TA usage type." },
#endif
	{ 0, NULL }
};

const char *
ldns_get_errorstr_by_id(ldns_status err)
{
        ldns_lookup_table *lt;

        lt = ldns_lookup_by_id(ldns_error_str, err);

        if (lt) {
                return lt->name;
        }
        return NULL;
}
