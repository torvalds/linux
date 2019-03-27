/*
 * update.h
 *
 * Functions for RFC 2136 Dynamic Update
 *
 * Copyright (c) 2005-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 */

/**
 * \file
 *
 * Defines functions to perform UPDATE queries
 */


#ifndef LDNS_UPDATE_H
#define LDNS_UPDATE_H

#include <ldns/resolver.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * create an update packet from zone name, class and the rr lists
 * \param[in] zone_rdf name of the zone
 *            The returned packet will take ownership of zone_rdf, so the caller should not free it
 * \param[in] clas zone class
 * \param[in] pr_rrlist list of Prerequisite Section RRs
 * \param[in] up_rrlist list of Updates Section RRs
 * \param[in] ad_rrlist list of Additional Data Section RRs (currently unused)
 * \return the new packet
 */
ldns_pkt *ldns_update_pkt_new(ldns_rdf *zone_rdf, ldns_rr_class clas, const ldns_rr_list *pr_rrlist, const ldns_rr_list *up_rrlist, const ldns_rr_list *ad_rrlist);

/**
 * add tsig credentials to
 * a packet from a resolver
 * \param[in] p packet to copy to
 * \param[in] r resolver to copy from
 *
 * \return status wether successfull or not
 */
ldns_status ldns_update_pkt_tsig_add(ldns_pkt *p, const ldns_resolver *r);

/* access functions */

/**
 * Get the zo count
 * \param[in] p the packet
 * \return the zo count
 */
uint16_t ldns_update_zocount(const ldns_pkt *p);
/**
 * Get the zo count
 * \param[in] p the packet
 * \return the pr count
 */
uint16_t ldns_update_prcount(const ldns_pkt *p);
/**
 * Get the zo count
 * \param[in] p the packet
 * \return the up count
 */
uint16_t ldns_update_upcount(const ldns_pkt *p);
/**
 * Get the zo count
 * \param[in] p the packet
 * \return the ad count
 */
uint16_t ldns_update_ad(const ldns_pkt *p);
/**
 * Set the zo count
 * \param[in] p the packet
 * \param[in] c the zo count to set
 */
void ldns_update_set_zo(ldns_pkt *p, uint16_t c);
/**
 * Set the pr count
 * \param[in] p the packet
 * \param[in] c the pr count to set
 */
void ldns_update_set_prcount(ldns_pkt *p, uint16_t c);
/**
 * Set the up count
 * \param[in] p the packet
 * \param[in] c the up count to set
 */
void ldns_update_set_upcount(ldns_pkt *p, uint16_t c);
/**
 * Set the ad count
 * \param[in] p the packet
 * \param[in] c the ad count to set
 */
void ldns_update_set_adcount(ldns_pkt *p, uint16_t c);

/* soa functions that need to be configured */
/*
 * Not sure if we want to keep these like this, therefore
 * not documented
 */
ldns_status ldns_update_soa_mname(ldns_rdf *zone, ldns_resolver *r, ldns_rr_class c, ldns_rdf **mname);
/* 
 * Not sure if we want to keep these like this, therefore
 * not documented
 */
ldns_status ldns_update_soa_zone_mname(const char *fqdn, ldns_resolver *r, ldns_rr_class c, ldns_rdf **zone_rdf, ldns_rdf **mname_rdf);

#ifdef __cplusplus
}
#endif

#endif  /* LDNS_UPDATE_H */
