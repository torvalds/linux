/*
 * dname.h
 *
 * dname definitions
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file dname.h
 *
 * dname contains function to read and manipulate domain names.
 *
 * Example domain names are "www.nlnetlabs.nl." and "." (the root)
 *
 * If a domain name ends with a dot ("."), it is called a Fully Qualified
 * Domain Name (FQDN). In certain places (for instance when reading a zone
 * file), an origin (which is just another domain name) non-FQDNs will be
 * placed after the current. For instance, if i have a zone file where the
 * origin has been set to "nl.", and my file contains the name
 * "www.nlnetlabs", it will result in "www.nlnetlabs.nl.". Internally, dnames are
 * always absolute (the dot is added when it is missing and there is no origin).
 *
 * An FQDN is also
 * known as an absolute domain name, therefore the function to check this is
 * called \ref ldns_dname_str_absolute
 *
 * Domain names are stored in \ref ldns_rdf structures, with the type
 * \ref LDNS_RDF_TYPE_DNAME
 *
 * This module is *NOT* about the RR type called DNAME.
 */


#ifndef LDNS_DNAME_H
#define LDNS_DNAME_H

#include <ldns/common.h>
#include <ldns/rdata.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_DNAME_NORMALIZE        tolower

/**
 * concatenates two dnames together
 * \param[in] rd1 the leftside
 * \param[in] rd2 the rightside
 * \return a new rdf with leftside/rightside
 */
ldns_rdf *ldns_dname_cat_clone(const ldns_rdf *rd1, const ldns_rdf *rd2);

/**
 * concatenates rd2 after rd1 (rd2 is copied, rd1 is modified)
 * \param[in] rd1 the leftside
 * \param[in] rd2 the rightside
 * \return LDNS_STATUS_OK on success
 */
ldns_status 	ldns_dname_cat(ldns_rdf *rd1, const ldns_rdf *rd2);

/**
 * Returns a clone of the given dname with the labels
 * reversed
 * \param[in] d the dname to reverse
 * \return clone of the dname with the labels reversed.
 */
ldns_rdf *ldns_dname_reverse(const ldns_rdf *d);

/**
 * Clones the given dname from the nth label on
 * \param[in] d The dname to clone
 * \param[in] n the label nr to clone from, if this is 0, the complete
 *              dname is cloned
 * \return A newly allocated *rdf structure, containing the cloned dname,
 *         or NULL if either d was NULL, not a dname, or if n >=
 *         label_count
 */
ldns_rdf *
ldns_dname_clone_from(const ldns_rdf *d, uint16_t n);

/**
 * chop one label off the left side of a dname. so
 * wwww.nlnetlabs.nl, becomes nlnetlabs.nl
 * This new name is a clone and must be freed with ldns_deep_free()
 * \param[in] d the dname to chop
 * \return the remaining dname
 */
ldns_rdf *ldns_dname_left_chop(const ldns_rdf *d);

/**
 * count the number of labels inside a LDNS_RDF_DNAME type rdf.
 * \param[in] *r the rdf
 * \return the number of labels
 */
uint8_t  ldns_dname_label_count(const ldns_rdf *r);

/**
 * creates a new dname rdf from a string.
 * \param[in] str string to use
 * \return ldns_rdf* or NULL in case of an error
 */
ldns_rdf *ldns_dname_new_frm_str(const char *str);

/**
 * Create a new dname rdf from a string. The data pointer
 * is stored in the rdf, not a copy of the data
 * \param[in] s the size of the new dname
 * \param[in] *data pointer to the actual data
 *
 * \return ldns_rdf*
 */
ldns_rdf *ldns_dname_new(uint16_t s, void *data);

/**
 * Create a new dname rdf from data (the data is copied)
 * \param[in] size the size of the data
 * \param[in] *data pointer to the actual data
 *
 * \return ldns_rdf*
 */
ldns_rdf *ldns_dname_new_frm_data(uint16_t size, const void *data);

/**
 * Put a dname into canonical fmt - ie. lowercase it
 * \param[in] rdf the dname to lowercase
 * \return void
 */
void ldns_dname2canonical(const ldns_rdf *rdf);

/**
 * test wether the name sub falls under parent (i.e. is a subdomain
 * of parent). This function will return false if the given dnames are
 * equal.
 * \param[in] sub the name to test
 * \param[in] parent the parent's name
 * \return true if sub falls under parent, otherwise false
 */
bool ldns_dname_is_subdomain(const ldns_rdf *sub, const ldns_rdf *parent);

/**
 * Compares the two dname rdf's according to the algorithm for ordering
 * in RFC4034 Section 6.
 * \param[in] dname1 First dname rdf to compare
 * \param[in] dname2 Second dname rdf to compare
 * \return -1 if dname1 comes before dname2, 1 if dname1 comes after dname2, and 0 if they are equal.
 */
int ldns_dname_compare(const ldns_rdf *dname1, const ldns_rdf *dname2);
int ldns_dname_compare_v(const void *, const void *);

/**
 * Checks whether the dname matches the given wildcard
 * \param[in] dname The dname to check
 * \param[in] wildcard The wildcard to check with
 * \return 1 If the wildcard matches, OR if 'wildcard' is not a wildcard and
 *           the names are *exactly* the same
 *         0 If the wildcard does not match, or if it is not a wildcard and
 *           the names are not the same
 */
int ldns_dname_match_wildcard(const ldns_rdf *dname, const ldns_rdf *wildcard);

/**
 * check if middle lays in the interval defined by prev and next
 * prev <= middle < next. This is useful for nsec checking
 * \param[in] prev the previous dname
 * \param[in] middle the dname to check
 * \param[in] next the next dname
 * return 0 on error or unknown, -1 when middle is in the interval, +1 when not
 */
int ldns_dname_interval(const ldns_rdf *prev, const ldns_rdf *middle, const ldns_rdf *next);

/**
 * Checks whether the given dname string is absolute (i.e. ends with a '.')
 * \param[in] *dname_str a string representing the dname
 * \return true or false
 */
bool ldns_dname_str_absolute(const char *dname_str);

/**
 * Checks whether the given dname is absolute (i.e. ends with a '.')
 * \param[in] *dname a rdf representing the dname
 * \return true or false
 */
bool ldns_dname_absolute(const ldns_rdf *dname);

/**
 * look inside the rdf and if it is an LDNS_RDF_TYPE_DNAME
 * try and retrieve a specific label. The labels are numbered
 * starting from 0 (left most).
 * \param[in] rdf the rdf to look in
 * \param[in] labelpos return the label with this number
 * \return a ldns_rdf* with the label as name or NULL on error
 */
ldns_rdf * ldns_dname_label(const ldns_rdf *rdf, uint8_t labelpos);

/**
 * Check if dname is a wildcard, starts with *.
 * \param[in] dname: the rdf to look in
 * \return true if a wildcard, false if not.
 */
int ldns_dname_is_wildcard(const ldns_rdf* dname);

#ifdef __cplusplus
}
#endif

#endif	/* LDNS_DNAME_H */
