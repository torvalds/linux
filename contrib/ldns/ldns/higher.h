/**
 * \file higher.h
 *
 * Specifies some higher level functions that could
 * be useful for certain applications
 */

/*
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

#ifndef LDNS_HIGHER_H
#define LDNS_HIGHER_H

#include <ldns/resolver.h>
#include <ldns/rdata.h>
#include <ldns/rr.h>
#include <ldns/host2str.h>
#include <ldns/tsig.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ask the resolver about name
 * and return all address records
 * \param[in] r the resolver to use
 * \param[in] name the name to look for
 * \param[in] c the class to use
 * \param[in] flags give some optional flags to the query
 */
ldns_rr_list *ldns_get_rr_list_addr_by_name(ldns_resolver *r, const ldns_rdf *name, ldns_rr_class c, uint16_t flags);

/**
 * ask the resolver about the address
 * and return the name
 * \param[in] r the resolver to use
 * \param[in] addr the addr to look for
 * \param[in] c the class to use
 * \param[in] flags give some optional flags to the query
 */
ldns_rr_list *ldns_get_rr_list_name_by_addr(ldns_resolver *r, const ldns_rdf *addr, ldns_rr_class c, uint16_t flags);

/**
 * wade through fp (a /etc/hosts like file)
 * and return a rr_list containing all the 
 * defined hosts in there
 * \param[in] fp the file pointer to use
 * \return ldns_rr_list * with the names
 */
ldns_rr_list *ldns_get_rr_list_hosts_frm_fp(FILE *fp);

/**
 * wade through fp (a /etc/hosts like file)
 * and return a rr_list containing all the 
 * defined hosts in there
 * \param[in] fp the file pointer to use
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \return ldns_rr_list * with the names
 */
ldns_rr_list *ldns_get_rr_list_hosts_frm_fp_l(FILE *fp, int *line_nr);

/**
 * wade through fp (a /etc/hosts like file)
 * and return a rr_list containing all the 
 * defined hosts in there
 * \param[in] filename the filename to use (NULL for /etc/hosts)
 * \return ldns_rr_list * with the names
 */
ldns_rr_list *ldns_get_rr_list_hosts_frm_file(char *filename);

/**
 * This function is a wrapper function for ldns_get_rr_list_name_by_addr
 * and ldns_get_rr_list_addr_by_name. It's name is from the getaddrinfo() 
 * library call. It tries to mimic that call, but without the lowlevel
 * stuff.
 * \param[in] res The resolver. If this value is NULL then a resolver will
 * be created by ldns_getaddrinfo.
 * \param[in] node the name or ip address to look up
 * \param[in] c the class to look in
 * \param[out] list put the found RR's in this list
 * \return the number of RR found.
 */
uint16_t ldns_getaddrinfo(ldns_resolver *res, const ldns_rdf *node, ldns_rr_class c, ldns_rr_list **list);

/**
 * Check if t is enumerated in the nsec type rdata
 * \param[in] nsec the NSEC Record to look in
 * \param[in] t the type to check for
 * \return true when t is found, otherwise return false
 */
bool ldns_nsec_type_check(const ldns_rr *nsec, ldns_rr_type t);

/**
 * Print a number of rdf's of the RR. The rdfnum-list must 
 * be ended by -1, otherwise unpredictable things might happen.
 * rdfs may be printed multiple times
 * \param[in] fp FILE * to write to
 * \param[in] r RR to write
 * \param[in] rdfnum a list of rdf to print.
 */
void ldns_print_rr_rdf(FILE *fp, ldns_rr *r, int rdfnum, ...);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_HIGHER_H */
