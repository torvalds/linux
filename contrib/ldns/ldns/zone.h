/**
 * zone.h
 *
 * zone definitions
 *  - what is it
 *  - get_glue function
 *  - search etc
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Defines the ldns_zone structure and functions to manipulate it.
 */
 

#ifndef LDNS_ZONE_H
#define LDNS_ZONE_H

#include <ldns/common.h>
#include <ldns/rdata.h>
#include <ldns/rr.h>
#include <ldns/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * DNS Zone
 *
 * A list of RR's with some
 * extra information which comes from the SOA RR
 * Note: nothing has been done to make this efficient (yet).
 */
struct ldns_struct_zone
{
	/** the soa defines a zone */
	ldns_rr 	*_soa;
	/* basicly a zone is a list of rr's */
	ldns_rr_list 	*_rrs;
	/* we could change this to be a b-tree etc etc todo */
};
typedef struct ldns_struct_zone ldns_zone;	
	
/**
 * create a new ldns_zone structure
 * \return a pointer to a ldns_zone structure
 */
ldns_zone * ldns_zone_new(void);

/**
 * Return the soa record of a zone
 * \param[in] z the zone to read from
 * \return the soa record in the zone
 */
ldns_rr * ldns_zone_soa(const ldns_zone *z);

/**
 * Returns the number of resource records in the zone, NOT counting the SOA record
 * \param[in] z the zone to read from
 * \return the number of rr's in the zone
 */
size_t ldns_zone_rr_count(const ldns_zone *z);

/**
 * Set the zone's soa record
 * \param[in] z the zone to put the new soa in
 * \param[in] soa the soa to set
 */
void ldns_zone_set_soa(ldns_zone *z, ldns_rr *soa);

/**
 * Get a list of a zone's content. Note that the SOA
 * isn't included in this list. You need to get the 
 * with ldns_zone_soa.
 * \param[in] z the zone to read from
 * \return the rrs from this zone
 */
ldns_rr_list * ldns_zone_rrs(const ldns_zone *z);

/**
 * Set the zone's contents
 * \param[in] z the zone to put the new soa in
 * \param[in] rrlist the rrlist to use
 */
void ldns_zone_set_rrs(ldns_zone *z, ldns_rr_list *rrlist);

/**
 * push an rrlist to a zone structure. This function use pointer
 * copying, so the rr_list structure inside z is modified!
 * \param[in] z the zone to add to
 * \param[in] list the list to add
 * \return a true on succes otherwise falsed
 */
bool ldns_zone_push_rr_list(ldns_zone *z, const ldns_rr_list *list);

/**
 * push an single rr to a zone structure. This function use pointer
 * copying, so the rr_list structure inside z is modified!
 * \param[in] z the zone to add to
 * \param[in] rr the rr to add
 * \return a true on succes otherwise falsed
 */
bool ldns_zone_push_rr(ldns_zone *z, ldns_rr *rr);

/**
 * Retrieve all resource records from the zone that are glue
 * records. The resulting list does are pointer references
 * to the zone's data.
 *
 * Due to the current zone implementation (as a list of rr's), this
 * function is extremely slow. Another (probably better) way to do this
 * is to use an ldns_dnssec_zone structure and the 
 * ldns_dnssec_mark_and_get_glue() function.
 *
 * \param[in] z the zone to look for glue
 * \return the rr_list with the glue
 */
ldns_rr_list *ldns_zone_glue_rr_list(const ldns_zone *z);

/**
 * Create a new zone from a file
 * \param[out] z the new zone
 * \param[in] *fp the filepointer to use
 * \param[in] *origin the zones' origin
 * \param[in] ttl default ttl to use
 * \param[in] c default class to use (IN)
 *
 * \return ldns_status mesg with an error or LDNS_STATUS_OK
 */
ldns_status ldns_zone_new_frm_fp(ldns_zone **z, FILE *fp, const ldns_rdf *origin, uint32_t ttl, ldns_rr_class c);

/**
 * Create a new zone from a file, keep track of the line numbering
 * \param[out] z the new zone
 * \param[in] *fp the filepointer to use
 * \param[in] *origin the zones' origin
 * \param[in] ttl default ttl to use
 * \param[in] c default class to use (IN)
 * \param[out] line_nr used for error msg, to get to the line number
 *
 * \return ldns_status mesg with an error or LDNS_STATUS_OK
 */
ldns_status ldns_zone_new_frm_fp_l(ldns_zone **z, FILE *fp, const ldns_rdf *origin, uint32_t ttl, ldns_rr_class c, int *line_nr);

/**
 * Frees the allocated memory for the zone, and the rr_list structure in it
 * \param[in] zone the zone to free
 */
void ldns_zone_free(ldns_zone *zone);

/**
 * Frees the allocated memory for the zone, the soa rr in it, 
 * and the rr_list structure in it, including the rr's in that. etc.
 * \param[in] zone the zone to free
 */
void ldns_zone_deep_free(ldns_zone *zone);

/**
 * Sort the rrs in a zone, with the current impl. this is slow
 * \param[in] zone the zone to sort
 */
void ldns_zone_sort(ldns_zone *zone);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_ZONE_H */
