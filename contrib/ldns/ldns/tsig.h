/*
 * tsig.h -- defines for TSIG [RFC2845]
 *
 * Copyright (c) 2005-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 */

#ifndef LDNS_TSIG_H
#define LDNS_TSIG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 *
 * Defines functions for TSIG usage
 */


/**
 * Contains credentials for TSIG
*/
typedef struct ldns_tsig_credentials_struct
{
    const char *algorithm;
    const char *keyname;
    const char *keydata;
    /* XXX More eventually. */
} ldns_tsig_credentials;

const char *ldns_tsig_algorithm(const ldns_tsig_credentials *);
const char *ldns_tsig_keyname(const ldns_tsig_credentials *);
const char *ldns_tsig_keydata(const ldns_tsig_credentials *);
char *ldns_tsig_keyname_clone(const ldns_tsig_credentials *);
char *ldns_tsig_keydata_clone(const ldns_tsig_credentials *);

/**
 * verifies the tsig rr for the given packet and key.
 * The wire must be given too because tsig does not sign normalized packets.
 * \param[in] pkt the packet to verify
 * \param[in] wire needed to verify the mac
 * \param[in] wire_size size of wire
 * \param[in] key_name the name of the shared key
 * \param[in] key_data the key in base 64 format
 * \param[in] mac original mac
 * \return true if tsig is correct, false if not, or if tsig is not set
 */
bool ldns_pkt_tsig_verify(ldns_pkt *pkt, const uint8_t *wire, size_t wire_size, const char *key_name, const char *key_data, const ldns_rdf *mac);

/**
 * verifies the tsig rr for the given packet and key.
 * The wire must be given too because tsig does not sign normalized packets.
 * \param[in] pkt the packet to verify
 * \param[in] wire needed to verify the mac
 * \param[in] wire_size size of wire
 * \param[in] key_name the name of the shared key
 * \param[in] key_data the key in base 64 format
 * \param[in] mac original mac
 * \param[in] tsig_timers_only must be zero for the first packet and positive for subsequent packets. If zero, all digest
   components are used to verify the _mac. If non-zero, only the TSIG timers are used to verify the mac.
 * \return true if tsig is correct, false if not, or if tsig is not set
 */
bool ldns_pkt_tsig_verify_next(ldns_pkt *pkt, const uint8_t *wire, size_t wire_size, const char *key_name, const char *key_data, const ldns_rdf *mac,
    int tsig_timers_only);

/**
 * creates a tsig rr for the given packet and key.
 * \param[in] pkt the packet to sign
 * \param[in] key_name the name of the shared key
 * \param[in] key_data the key in base 64 format
 * \param[in] fudge seconds of error permitted in time signed
 * \param[in] algorithm_name the name of the algorithm used
 * \param[in] query_mac is added to the digest if not NULL (so NULL is for signing queries, not NULL is for signing answers)
 * \return status (OK if success)
 */
ldns_status ldns_pkt_tsig_sign(ldns_pkt *pkt, const char *key_name, const char *key_data, uint16_t fudge,
    const char *algorithm_name, const ldns_rdf *query_mac);

/**
 * creates a tsig rr for the given packet and key.
 * \param[in] pkt the packet to sign
 * \param[in] key_name the name of the shared key
 * \param[in] key_data the key in base 64 format
 * \param[in] fudge seconds of error permitted in time signed
 * \param[in] algorithm_name the name of the algorithm used
 * \param[in] query_mac is added to the digest if not NULL (so NULL is for signing queries, not NULL is for signing answers)
 * \param[in] tsig_timers_only must be zero for the first packet and positive for subsequent packets. If zero, all digest
   components are used to create the query_mac. If non-zero, only the TSIG timers are used to create the query_mac.
 * \return status (OK if success)
 */
ldns_status ldns_pkt_tsig_sign_next(ldns_pkt *pkt, const char *key_name, const char *key_data, uint16_t fudge,
    const char *algorithm_name, const ldns_rdf *query_mac, int tsig_timers_only);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_TSIG_H */
