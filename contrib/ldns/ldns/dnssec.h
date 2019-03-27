/*
 * dnssec.h -- defines for the Domain Name System (SEC) (DNSSEC)
 *
 * Copyright (c) 2005-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 * A bunch of defines that are used in the DNS
 */

/**
 * \file dnssec.h
 *
 * This module contains base functions for DNSSEC operations
 * (RFC4033 t/m RFC4035).
 * 
 * Since those functions heavily rely op cryptographic operations,
 * this module is dependent on openssl.
 * 
 */
 

#ifndef LDNS_DNSSEC_H
#define LDNS_DNSSEC_H

#include <ldns/common.h>
#if LDNS_BUILD_CONFIG_HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/evp.h>
#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */
#include <ldns/packet.h>
#include <ldns/keys.h>
#include <ldns/zone.h>
#include <ldns/resolver.h>
#include <ldns/dnssec_zone.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_MAX_KEYLEN		2048
#define LDNS_DNSSEC_KEYPROTO	3
/* default time before sigs expire */
#define LDNS_DEFAULT_EXP_TIME	2419200 /* 4 weeks */

/** return values for the old-signature callback */
#define LDNS_SIGNATURE_LEAVE_ADD_NEW 0
#define LDNS_SIGNATURE_LEAVE_NO_ADD 1
#define LDNS_SIGNATURE_REMOVE_ADD_NEW 2
#define LDNS_SIGNATURE_REMOVE_NO_ADD 3

/**
 * Returns the first RRSIG rr that corresponds to the rrset 
 * with the given name and type
 * 
 * \param[in] name The dname of the RRset covered by the RRSIG to find
 * \param[in] type The type of the RRset covered by the RRSIG to find
 * \param[in] rrs List of rrs to search in
 * \returns Pointer to the first RRsig ldns_rr found, or NULL if it is
 * not present
 */
ldns_rr *ldns_dnssec_get_rrsig_for_name_and_type(const ldns_rdf *name,
									    const ldns_rr_type type,
									    const ldns_rr_list *rrs);

/**
 * Returns the DNSKEY that corresponds to the given RRSIG rr from the list, if
 * any
 *
 * \param[in] rrsig The rrsig to find the DNSKEY for
 * \param[in] rrs The rr list to find the key in
 * \return The DNSKEY that corresponds to the given RRSIG, or NULL if it was
 *         not found.
 */
ldns_rr *ldns_dnssec_get_dnskey_for_rrsig(const ldns_rr *rrsig, const ldns_rr_list *rrs);

/**
 * Returns the rdata field that contains the bitmap of the covered types of
 * the given NSEC record
 *
 * \param[in] nsec The nsec to get the covered type bitmap of
 * \return An ldns_rdf containing the bitmap, or NULL on error
 */
ldns_rdf *ldns_nsec_get_bitmap(const ldns_rr *nsec);


#define LDNS_NSEC3_MAX_ITERATIONS 65535

/**
 * Returns the dname of the closest (provable) encloser
 */
ldns_rdf *
ldns_dnssec_nsec3_closest_encloser(const ldns_rdf *qname,
							ldns_rr_type qtype,
							const ldns_rr_list *nsec3s);

/**
 * Checks whether the packet contains rrsigs
 */
bool
ldns_dnssec_pkt_has_rrsigs(const ldns_pkt *pkt);

/**
 * Returns a ldns_rr_list containing the signatures covering the given name
 * and type
 */
ldns_rr_list *ldns_dnssec_pkt_get_rrsigs_for_name_and_type(const ldns_pkt *pkt, const ldns_rdf *name, ldns_rr_type type);

/**
 * Returns a ldns_rr_list containing the signatures covering the given type
 */
ldns_rr_list *ldns_dnssec_pkt_get_rrsigs_for_type(const ldns_pkt *pkt, ldns_rr_type type);

/** 
 * calculates a keytag of a key for use in DNSSEC.
 *
 * \param[in] key the key as an RR to use for the calc.
 * \return the keytag
 */
uint16_t ldns_calc_keytag(const ldns_rr *key);

/**
 * Calculates keytag of DNSSEC key, operates on wireformat rdata.
 * \param[in] key the key as uncompressed wireformat rdata.
 * \param[in] keysize length of key data.
 * \return the keytag
 */
uint16_t ldns_calc_keytag_raw(const uint8_t* key, size_t keysize);

#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * converts a buffer holding key material to a DSA key in openssl.
 *
 * \param[in] key the key to convert
 * \return a DSA * structure with the key material
 */
DSA *ldns_key_buf2dsa(const ldns_buffer *key);
/**
 * Like ldns_key_buf2dsa, but uses raw buffer.
 * \param[in] key the uncompressed wireformat of the key.
 * \param[in] len length of key data
 * \return a DSA * structure with the key material
 */
DSA *ldns_key_buf2dsa_raw(const unsigned char* key, size_t len);

/**
 * Utility function to calculate hash using generic EVP_MD pointer.
 * \param[in] data the data to hash.
 * \param[in] len  length of data.
 * \param[out] dest the destination of the hash, must be large enough.
 * \param[in] md the message digest to use.
 * \return true if worked, false on failure.
 */
int ldns_digest_evp(const unsigned char* data, unsigned int len, 
	unsigned char* dest, const EVP_MD* md);

/**
 * Converts a holding buffer with key material to EVP PKEY in openssl.
 * Only available if ldns was compiled with GOST.
 * \param[in] key data to convert
 * \param[in] keylen length of the key data
 * \return the key or NULL on error.
 */
EVP_PKEY* ldns_gost2pkey_raw(const unsigned char* key, size_t keylen);

/**
 * Converts a holding buffer with key material to EVP PKEY in openssl.
 * Only available if ldns was compiled with ECDSA.
 * \param[in] key data to convert
 * \param[in] keylen length of the key data
 * \param[in] algo precise algorithm to initialize ECC group values.
 * \return the key or NULL on error.
 */
EVP_PKEY* ldns_ecdsa2pkey_raw(const unsigned char* key, size_t keylen, uint8_t algo);

/**
 * Converts a holding buffer with key material to EVP PKEY in openssl.
 * Only available if ldns was compiled with ED25519.
 * \param[in] key data to convert
 * \param[in] keylen length of the key data
 * \return the key or NULL on error.
 */
EVP_PKEY* ldns_ed255192pkey_raw(const unsigned char* key, size_t keylen);

/**
 * Converts a holding buffer with key material to EVP PKEY in openssl.
 * Only available if ldns was compiled with ED448.
 * \param[in] key data to convert
 * \param[in] keylen length of the key data
 * \return the key or NULL on error.
 */
EVP_PKEY* ldns_ed4482pkey_raw(const unsigned char* key, size_t keylen);

#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */

#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * converts a buffer holding key material to a RSA key in openssl.
 *
 * \param[in] key the key to convert
 * \return a RSA * structure with the key material
 */
RSA *ldns_key_buf2rsa(const ldns_buffer *key);

/**
 * Like ldns_key_buf2rsa, but uses raw buffer.
 * \param[in] key the uncompressed wireformat of the key.
 * \param[in] len length of key data
 * \return a RSA * structure with the key material
 */
RSA *ldns_key_buf2rsa_raw(const unsigned char* key, size_t len);
#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */

/** 
 * returns a new DS rr that represents the given key rr.
 *
 * \param[in] *key the key to convert
 * \param[in] h the hash to use LDNS_SHA1/LDNS_SHA256
 *
 * \return ldns_rr* a new rr pointer to a DS
 */
ldns_rr *ldns_key_rr2ds(const ldns_rr *key, ldns_hash h);

/**
 * Create the type bitmap for an NSEC(3) record
 */
ldns_rdf *
ldns_dnssec_create_nsec_bitmap(ldns_rr_type rr_type_list[],
						 size_t size,
						 ldns_rr_type nsec_type);

/**
 * returns whether a rrset of the given type is found in the rrsets.
 *
 * \param[in] rrsets the rrsets to be tested
 * \param[in] type the type to test for
 * \return int 1 if the type was found, 0 otherwise.
 */
int
ldns_dnssec_rrsets_contains_type(const ldns_dnssec_rrsets *rrsets, ldns_rr_type type);

/**
 * Creates NSEC
 */
ldns_rr *
ldns_dnssec_create_nsec(const ldns_dnssec_name *from,
				    const ldns_dnssec_name *to,
				    ldns_rr_type nsec_type);


/**
 * Creates NSEC3
 */
ldns_rr *
ldns_dnssec_create_nsec3(const ldns_dnssec_name *from,
					const ldns_dnssec_name *to,
					const ldns_rdf *zone_name,
					uint8_t algorithm,
					uint8_t flags,
					uint16_t iterations,
					uint8_t salt_length,
					const uint8_t *salt);

/**
 * Create a NSEC record
 * \param[in] cur_owner the current owner which should be taken as the starting point
 * \param[in] next_owner the rrlist which the nsec rr should point to 
 * \param[in] rrs all rrs from the zone, to find all RR types of cur_owner in
 * \return a ldns_rr with the nsec record in it
 */
ldns_rr * ldns_create_nsec(ldns_rdf *cur_owner, ldns_rdf *next_owner, ldns_rr_list *rrs);

/**
 * Calculates the hashed name using the given parameters
 * \param[in] *name The owner name to calculate the hash for 
 * \param[in] algorithm The hash algorithm to use
 * \param[in] iterations The number of hash iterations to use
 * \param[in] salt_length The length of the salt in bytes
 * \param[in] salt The salt to use
 * \return The hashed owner name rdf, without the domain name
 */
ldns_rdf *ldns_nsec3_hash_name(const ldns_rdf *name, uint8_t algorithm, uint16_t iterations, uint8_t salt_length, const uint8_t *salt);

/**
 * Sets all the NSEC3 options. The rr to set them in must be initialized with _new() and
 * type LDNS_RR_TYPE_NSEC3
 * \param[in] *rr The RR to set the values in
 * \param[in] algorithm The NSEC3 hash algorithm 
 * \param[in] flags The flags field 
 * \param[in] iterations The number of hash iterations
 * \param[in] salt_length The length of the salt in bytes 
 * \param[in] salt The salt bytes
 */
void ldns_nsec3_add_param_rdfs(ldns_rr *rr,
						 uint8_t algorithm,
						 uint8_t flags,
						 uint16_t iterations,
						 uint8_t salt_length,
						 const uint8_t *salt);

/* this will NOT return the NSEC3  completed, you will have to run the
   finalize function on the rrlist later! */
ldns_rr *
ldns_create_nsec3(const ldns_rdf *cur_owner,
                  const ldns_rdf *cur_zone,
                  const ldns_rr_list *rrs,
                  uint8_t algorithm,
                  uint8_t flags,
                  uint16_t iterations,
                  uint8_t salt_length,
                  const uint8_t *salt,
                  bool emptynonterminal);

/**
 * Returns the hash algorithm used in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The algorithm identifier, or 0 on error
 */
uint8_t ldns_nsec3_algorithm(const ldns_rr *nsec3_rr);

/**
 * Returns flags field
 */
uint8_t
ldns_nsec3_flags(const ldns_rr *nsec3_rr);

/**
 * Returns true if the opt-out flag has been set in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return true if the RR has type NSEC3 and the opt-out bit has been set, false otherwise
 */
bool ldns_nsec3_optout(const ldns_rr *nsec3_rr);

/**
 * Returns the number of hash iterations used in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The number of iterations
 */
uint16_t ldns_nsec3_iterations(const ldns_rr *nsec3_rr);

/**
 * Returns the salt used in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The salt rdf, or NULL on error
 */
ldns_rdf *ldns_nsec3_salt(const ldns_rr *nsec3_rr);

/**
 * Returns the length of the salt used in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The length of the salt in bytes
 */
uint8_t ldns_nsec3_salt_length(const ldns_rr *nsec3_rr);

/**
 * Returns the salt bytes used in the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The salt in bytes, this is alloced, so you need to free it
 */
uint8_t *ldns_nsec3_salt_data(const ldns_rr *nsec3_rr);

/**
 * Returns the first label of the next ownername in the NSEC3 chain (ie. without the domain)
 * \param[in] nsec3_rr The RR to read from
 * \return The first label of the next owner name in the NSEC3 chain, or NULL on error 
 */
ldns_rdf *ldns_nsec3_next_owner(const ldns_rr *nsec3_rr);

/**
 * Returns the bitmap specifying the covered types of the given NSEC3 RR
 * \param[in] *nsec3_rr The RR to read from
 * \return The covered type bitmap rdf
 */
ldns_rdf *ldns_nsec3_bitmap(const ldns_rr *nsec3_rr);

/**
 * Calculates the hashed name using the parameters of the given NSEC3 RR
 * \param[in] *nsec The RR to use the parameters from
 * \param[in] *name The owner name to calculate the hash for 
 * \return The hashed owner name rdf, without the domain name
 */
ldns_rdf *ldns_nsec3_hash_name_frm_nsec3(const ldns_rr *nsec, const ldns_rdf *name);

/**
 * Check if RR type t is enumerated and set in the RR type bitmap rdf.
 * \param[in] bitmap the RR type bitmap rdf to look in
 * \param[in] type the type to check for
 * \return true when t is found and set, otherwise return false
 */
bool ldns_nsec_bitmap_covers_type(const ldns_rdf* bitmap, ldns_rr_type type);

/**
 * Checks if RR type t is enumerated in the type bitmap rdf and sets the bit.
 * \param[in] bitmap the RR type bitmap rdf to look in
 * \param[in] type the type to for which the bit to set
 * \return LDNS_STATUS_OK on success. LDNS_STATUS_TYPE_NOT_IN_BITMAP is 
 *         returned when the bitmap does not contain the bit to set.
 */
ldns_status ldns_nsec_bitmap_set_type(ldns_rdf* bitmap, ldns_rr_type type);

/**
 * Checks if RR type t is enumerated in the type bitmap rdf and clears the bit.
 * \param[in] bitmap the RR type bitmap rdf to look in
 * \param[in] type the type to for which the bit to clear
 * \return LDNS_STATUS_OK on success. LDNS_STATUS_TYPE_NOT_IN_BITMAP is 
 *         returned when the bitmap does not contain the bit to clear.
 */
ldns_status ldns_nsec_bitmap_clear_type(ldns_rdf* bitmap, ldns_rr_type type);

/**
 * Checks coverage of NSEC(3) RR name span
 * Remember that nsec and name must both be in canonical form (ie use
 * \ref ldns_rr2canonical and \ref ldns_dname2canonical prior to calling this
 * function)
 *
 * \param[in] nsec The NSEC RR to check
 * \param[in] name The owner dname to check, if the nsec record is a NSEC3 record, this should be the hashed name
 * \return true if the NSEC RR covers the owner name
 */
bool ldns_nsec_covers_name(const ldns_rr *nsec, const ldns_rdf *name);

#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * verify a packet 
 * \param[in] p the packet
 * \param[in] t the rr set type to check
 * \param[in] o the rr set name to check
 * \param[in] k list of keys
 * \param[in] s list of sigs (may be null)
 * \param[out] good_keys keys which validated the packet
 * \return status 
 * 
 */
ldns_status ldns_pkt_verify(const ldns_pkt *p, ldns_rr_type t, const ldns_rdf *o, const ldns_rr_list *k, const ldns_rr_list *s, ldns_rr_list *good_keys);

/**
 * verify a packet 
 * \param[in] p the packet
 * \param[in] t the rr set type to check
 * \param[in] o the rr set name to check
 * \param[in] k list of keys
 * \param[in] s list of sigs (may be null)
 * \param[in] check_time the time for which the validation is performed
 * \param[out] good_keys keys which validated the packet
 * \return status 
 * 
 */
ldns_status ldns_pkt_verify_time(const ldns_pkt *p, ldns_rr_type t, const ldns_rdf *o, const ldns_rr_list *k, const ldns_rr_list *s, time_t check_time, ldns_rr_list *good_keys);

#endif

/**
 * chains nsec3 list
 */
ldns_status
ldns_dnssec_chain_nsec3_list(ldns_rr_list *nsec3_rrs);

/**
 * compare for nsec3 sort
 */
int
qsort_rr_compare_nsec3(const void *a, const void *b);

/**
 * sort nsec3 list
 */
void
ldns_rr_list_sort_nsec3(ldns_rr_list *unsorted);

/** 
 * Default callback function to always leave present signatures, and
 * add new ones
 * \param[in] sig The signature to check for removal (unused)
 * \param[in] n Optional argument (unused)
 * \return LDNS_SIGNATURE_LEAVE_ADD_NEW
 */
int ldns_dnssec_default_add_to_signatures(ldns_rr *sig, void *n);
/** 
 * Default callback function to always leave present signatures, and
 * add no new ones for the keys of these signatures
 * \param[in] sig The signature to check for removal (unused)
 * \param[in] n Optional argument (unused)
 * \return LDNS_SIGNATURE_LEAVE_NO_ADD
 */
int ldns_dnssec_default_leave_signatures(ldns_rr *sig, void *n);
/** 
 * Default callback function to always remove present signatures, but
 * add no new ones
 * \param[in] sig The signature to check for removal (unused)
 * \param[in] n Optional argument (unused)
 * \return LDNS_SIGNATURE_REMOVE_NO_ADD
 */
int ldns_dnssec_default_delete_signatures(ldns_rr *sig, void *n);
/** 
 * Default callback function to always leave present signatures, and
 * add new ones
 * \param[in] sig The signature to check for removal (unused)
 * \param[in] n Optional argument (unused)
 * \return LDNS_SIGNATURE_REMOVE_ADD_NEW
 */
int ldns_dnssec_default_replace_signatures(ldns_rr *sig, void *n);

#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * Converts the DSA signature from ASN1 representation (RFC2459, as 
 * used by OpenSSL) to raw signature data as used in DNS (rfc2536)
 *
 * \param[in] sig The signature in RFC2459 format
 * \param[in] sig_len The length of the signature
 * \return a new rdf with the signature
 */
ldns_rdf *
ldns_convert_dsa_rrsig_asn12rdf(const ldns_buffer *sig,
						  const long sig_len);

/**
 * Converts the RRSIG signature RDF (in rfc2536 format) to a buffer
 * with the signature in rfc2459 format
 *
 * \param[out] target_buffer buffer to place the signature data
 * \param[in] sig_rdf The signature rdf to convert
 * \return LDNS_STATUS_OK on success, error code otherwise
 */
ldns_status
ldns_convert_dsa_rrsig_rdf2asn1(ldns_buffer *target_buffer,
						  const ldns_rdf *sig_rdf);

/**
 * Converts the ECDSA signature from ASN1 representation (as 
 * used by OpenSSL) to raw signature data as used in DNS
 * This routine is only present if ldns is compiled with ecdsa support.
 * The older ldns_convert_ecdsa_rrsig_asn12rdf routine could not (always)
 * construct a valid rdf because it did not have the num_bytes parameter.
 * The num_bytes parameter is 32 for p256 and 48 for p384 (bits/8).
 *
 * \param[in] sig The signature in ASN1 format
 * \param[in] sig_len The length of the signature
 * \param[in] num_bytes number of bytes for values in the curve, the curve
 * 		size divided by 8.
 * \return a new rdf with the signature
 */
ldns_rdf *
ldns_convert_ecdsa_rrsig_asn1len2rdf(const ldns_buffer *sig,
	const long sig_len, int num_bytes);

/**
 * Converts the RRSIG signature RDF (from DNS) to a buffer with the 
 * signature in ASN1 format as openssl uses it.
 * This routine is only present if ldns is compiled with ecdsa support.
 *
 * \param[out] target_buffer buffer to place the signature data in ASN1.
 * \param[in] sig_rdf The signature rdf to convert
 * \return LDNS_STATUS_OK on success, error code otherwise
 */
ldns_status
ldns_convert_ecdsa_rrsig_rdf2asn1(ldns_buffer *target_buffer,
        const ldns_rdf *sig_rdf);

/**
 * Converts the ECDSA signature from ASN1 representation (as
 * used by OpenSSL) to raw signature data as used in DNS
 * This routine is only present if ldns is compiled with ED25519 support.
 *
 * \param[in] sig The signature in ASN1 format
 * \param[in] sig_len The length of the signature
 * \return a new rdf with the signature
 */
ldns_rdf *
ldns_convert_ed25519_rrsig_asn12rdf(const ldns_buffer *sig, long sig_len);

/**
 * Converts the RRSIG signature RDF (from DNS) to a buffer with the
 * signature in ASN1 format as openssl uses it.
 * This routine is only present if ldns is compiled with ED25519 support.
 *
 * \param[out] target_buffer buffer to place the signature data in ASN1.
 * \param[in] sig_rdf The signature rdf to convert
 * \return LDNS_STATUS_OK on success, error code otherwise
 */
ldns_status
ldns_convert_ed25519_rrsig_rdf2asn1(ldns_buffer *target_buffer,
        const ldns_rdf *sig_rdf);

/**
 * Converts the ECDSA signature from ASN1 representation (as
 * used by OpenSSL) to raw signature data as used in DNS
 * This routine is only present if ldns is compiled with ED448 support.
 *
 * \param[in] sig The signature in ASN1 format
 * \param[in] sig_len The length of the signature
 * \return a new rdf with the signature
 */
ldns_rdf *
ldns_convert_ed448_rrsig_asn12rdf(const ldns_buffer *sig, long sig_len);

/**
 * Converts the RRSIG signature RDF (from DNS) to a buffer with the
 * signature in ASN1 format as openssl uses it.
 * This routine is only present if ldns is compiled with ED448 support.
 *
 * \param[out] target_buffer buffer to place the signature data in ASN1.
 * \param[in] sig_rdf The signature rdf to convert
 * \return LDNS_STATUS_OK on success, error code otherwise
 */
ldns_status
ldns_convert_ed448_rrsig_rdf2asn1(ldns_buffer *target_buffer,
        const ldns_rdf *sig_rdf);

#endif /* LDNS_BUILD_CONFIG_HAVE_SSL */

#ifdef __cplusplus
}
#endif

#endif /* LDNS_DNSSEC_H */
