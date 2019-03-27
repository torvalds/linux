/*
 * util.h
 * util.c header file 
 * in ldns
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#ifndef _DRILL_UTIL_H_
#define _DRILL_UTIL_H_
#include <ldns/ldns.h>


/**
 * Read  keys from filename and append to key_list.
 */
ldns_status read_key_file(const char *filename, ldns_rr_list *key_list,
		bool silently);

/**
 * return a address rdf, either A or AAAA 
 * NULL if anything goes wrong
 */
ldns_rdf * ldns_rdf_new_addr_frm_str(char *);

/**
 * print all the ds of the keys in the packet
 */
void print_ds_of_keys(ldns_pkt *p);

/**
 * print some rdfs of a signature
 */
void print_rrsig_abbr(FILE *fp, ldns_rr *sig);
/**
 * print some rdfs of a dnskey
 */
void print_dnskey_abbr(FILE *fp, ldns_rr *key);
/**
 * print some rdfs of a ds
 */
void print_ds_abbr(FILE *fp, ldns_rr *ds);

/**
 * print some rdfs of a rr in a rr_list
 */
void print_rr_list_abbr(FILE *fp, ldns_rr_list *sig, const char *usr);

/**
 * Alloc some memory, with error checking
 */
void *xmalloc(size_t s);

/** 
 * Realloc some memory, with error checking
 */
void *xrealloc(void *p, size_t s);

/**
 * Free the data
 */
void xfree(void *q);
#endif /* _DRILL_UTIL_H_ */
