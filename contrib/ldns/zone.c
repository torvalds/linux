/* zone.c
 *
 * Functions for ldns_zone structure
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 * See the file LICENSE for the license
 */
#include <ldns/config.h>

#include <ldns/ldns.h>

#include <strings.h>
#include <limits.h>

ldns_rr *
ldns_zone_soa(const ldns_zone *z)
{
        return z->_soa;
}

size_t
ldns_zone_rr_count(const ldns_zone *z)
{
	return ldns_rr_list_rr_count(z->_rrs);
}

void
ldns_zone_set_soa(ldns_zone *z, ldns_rr *soa)
{
	z->_soa = soa;
}

ldns_rr_list *
ldns_zone_rrs(const ldns_zone *z)
{
	return z->_rrs;
}

void
ldns_zone_set_rrs(ldns_zone *z, ldns_rr_list *rrlist)
{
	z->_rrs = rrlist;
}

bool
ldns_zone_push_rr_list(ldns_zone *z, const ldns_rr_list *list)
{
	return ldns_rr_list_cat(ldns_zone_rrs(z), list);
}

bool
ldns_zone_push_rr(ldns_zone *z, ldns_rr *rr)
{
	return ldns_rr_list_push_rr(ldns_zone_rrs(z), rr);
}


/*
 * Get the list of glue records in a zone
 * XXX: there should be a way for this to return error, other than NULL, 
 *      since NULL is a valid return
 */
ldns_rr_list *
ldns_zone_glue_rr_list(const ldns_zone *z)
{
	/* when do we find glue? It means we find an IP address
	 * (AAAA/A) for a nameserver listed in the zone
	 *
	 * Alg used here:
	 * first find all the zonecuts (NS records)
	 * find all the AAAA or A records (can be done it the 
	 * above loop).
	 *
	 * Check if the aaaa/a list are subdomains under the
	 * NS domains.
	 * If yes -> glue, if no -> not glue
	 */

	ldns_rr_list *zone_cuts;
	ldns_rr_list *addr;
	ldns_rr_list *glue;
	ldns_rr *r, *ns, *a;
	ldns_rdf *dname_a, *ns_owner;
	size_t i,j;

	zone_cuts = NULL;
	addr = NULL;
	glue = NULL;

	/* we cannot determine glue in a 'zone' without a SOA */
	if (!ldns_zone_soa(z)) {
		return NULL;
	}

	zone_cuts = ldns_rr_list_new();
	if (!zone_cuts) goto memory_error;
	addr = ldns_rr_list_new();
	if (!addr) goto memory_error;
	glue = ldns_rr_list_new();
	if (!glue) goto memory_error;

	for(i = 0; i < ldns_zone_rr_count(z); i++) {
		r = ldns_rr_list_rr(ldns_zone_rrs(z), i);
		if (ldns_rr_get_type(r) == LDNS_RR_TYPE_A ||
				ldns_rr_get_type(r) == LDNS_RR_TYPE_AAAA) {
			/* possibly glue */
			if (!ldns_rr_list_push_rr(addr, r)) goto memory_error;
			continue;
		}
		if (ldns_rr_get_type(r) == LDNS_RR_TYPE_NS) {
			/* multiple zones will end up here -
			 * for now; not a problem
			 */
			/* don't add NS records for the current zone itself */
			if (ldns_rdf_compare(ldns_rr_owner(r), 
						ldns_rr_owner(ldns_zone_soa(z))) != 0) {
				if (!ldns_rr_list_push_rr(zone_cuts, r)) goto memory_error;
			}
			continue;
		}
	}

	/* will sorting make it quicker ?? */
	for(i = 0; i < ldns_rr_list_rr_count(zone_cuts); i++) {
		ns = ldns_rr_list_rr(zone_cuts, i);
		ns_owner = ldns_rr_owner(ns);

		for(j = 0; j < ldns_rr_list_rr_count(addr); j++) {
			a = ldns_rr_list_rr(addr, j);
			dname_a = ldns_rr_owner(a);

			if (ldns_dname_is_subdomain(dname_a, ns_owner) ||
				ldns_dname_compare(dname_a, ns_owner) == 0) {
				/* GLUE! */
				if (!ldns_rr_list_push_rr(glue, a)) goto memory_error;
			}
		}
	}
	
	ldns_rr_list_free(addr);
	ldns_rr_list_free(zone_cuts);

	if (ldns_rr_list_rr_count(glue) == 0) {
		ldns_rr_list_free(glue);
		return NULL;
	} else {
		return glue;
	}

memory_error:
	if (zone_cuts) {
		LDNS_FREE(zone_cuts);
	}
	if (addr) {
		ldns_rr_list_free(addr);
	}
	if (glue) {
		ldns_rr_list_free(glue);
	}
	return NULL;
}

ldns_zone *
ldns_zone_new(void)
{
	ldns_zone *z;

	z = LDNS_MALLOC(ldns_zone);
	if (!z) {
		return NULL;
	}

	z->_rrs = ldns_rr_list_new();
	if (!z->_rrs) {
		LDNS_FREE(z);
		return NULL;
	}
	ldns_zone_set_soa(z, NULL);
	return z;
}

/* we regocnize:
 * $TTL, $ORIGIN
 */
ldns_status
ldns_zone_new_frm_fp(ldns_zone **z, FILE *fp, const ldns_rdf *origin, uint32_t ttl, ldns_rr_class c)
{
	return ldns_zone_new_frm_fp_l(z, fp, origin, ttl, c, NULL);
}

/* XXX: class is never used */
ldns_status
ldns_zone_new_frm_fp_l(ldns_zone **z, FILE *fp, const ldns_rdf *origin, uint32_t ttl,
        ldns_rr_class ATTR_UNUSED(c), int *line_nr)
{
	ldns_zone *newzone;
	ldns_rr *rr;
	uint32_t my_ttl;
	ldns_rdf *my_origin;
	ldns_rdf *my_prev;
	bool soa_seen = false; 	/* 2 soa are an error */
	ldns_status s;
	ldns_status ret;

	/* most cases of error are memory problems */
	ret = LDNS_STATUS_MEM_ERR;

	newzone = NULL;
	my_origin = NULL;
	my_prev = NULL;

	my_ttl    = ttl;
	
	if (origin) {
		my_origin = ldns_rdf_clone(origin);
		if (!my_origin) goto error;
		/* also set the prev */
		my_prev   = ldns_rdf_clone(origin);
		if (!my_prev) goto error;
	}

	newzone = ldns_zone_new();
	if (!newzone) goto error;

	while(!feof(fp)) {
		s = ldns_rr_new_frm_fp_l(&rr, fp, &my_ttl, &my_origin, &my_prev, line_nr);
		switch (s) {
		case LDNS_STATUS_OK:
			if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA) {
				if (soa_seen) {
					/* second SOA 
					 * just skip, maybe we want to say
					 * something??? */
					ldns_rr_free(rr);
					continue;
				}
				soa_seen = true;
				ldns_zone_set_soa(newzone, rr);
				/* set origin to soa if not specified */
				if (!my_origin) {
					my_origin = ldns_rdf_clone(ldns_rr_owner(rr));
				}
				continue;
			}
			
			/* a normal RR - as sofar the DNS is normal */
			if (!ldns_zone_push_rr(newzone, rr)) goto error;

		case LDNS_STATUS_SYNTAX_EMPTY:
			/* empty line was seen */
		case LDNS_STATUS_SYNTAX_TTL:
			/* the function set the ttl */
			break;
		case LDNS_STATUS_SYNTAX_ORIGIN:
			/* the function set the origin */
			break;
		case LDNS_STATUS_SYNTAX_INCLUDE:
			ret = LDNS_STATUS_SYNTAX_INCLUDE_ERR_NOTIMPL;
			break;
		default:
			ret = s;
			goto error;
		}
	}

	if (my_origin) {
		ldns_rdf_deep_free(my_origin);
	}
	if (my_prev) {
		ldns_rdf_deep_free(my_prev);
	}
	if (z) {
		*z = newzone;
	} else {
		ldns_zone_free(newzone);
	}

	return LDNS_STATUS_OK;

error:
	if (my_origin) {
		ldns_rdf_deep_free(my_origin);
	}
	if (my_prev) {
		ldns_rdf_deep_free(my_prev);
	}
	if (newzone) {
		ldns_zone_free(newzone);
	}
	return ret;
}

void
ldns_zone_sort(ldns_zone *zone)
{
	ldns_rr_list *zrr;
	assert(zone != NULL);

	zrr = ldns_zone_rrs(zone);
	ldns_rr_list_sort(zrr);
}

void
ldns_zone_free(ldns_zone *zone) 
{
	ldns_rr_list_free(zone->_rrs);
	LDNS_FREE(zone);
}

void
ldns_zone_deep_free(ldns_zone *zone) 
{
	ldns_rr_free(zone->_soa);
	ldns_rr_list_deep_free(zone->_rrs);
	LDNS_FREE(zone);
}
