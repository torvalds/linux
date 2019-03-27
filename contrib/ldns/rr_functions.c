/*
 * rr_function.c
 *
 * function that operate on specific rr types
 *
 * (c) NLnet Labs, 2004-2006
 * See the file LICENSE for the license
 */

/*
 * These come strait from perldoc Net::DNS::RR::xxx
 * first the read variant, then the write. This is
 * not complete.
 */

#include <ldns/config.h>

#include <ldns/ldns.h>

#include <limits.h>
#include <strings.h>

/**
 * return a specific rdf
 * \param[in] type type of RR
 * \param[in] rr   the rr itself
 * \param[in] pos  at which postion to get it
 * \return the rdf sought
 */
static ldns_rdf *
ldns_rr_function(ldns_rr_type type, const ldns_rr *rr, size_t pos)
{
        if (!rr || ldns_rr_get_type(rr) != type) {
                return NULL;
        }
        return ldns_rr_rdf(rr, pos);
}

/**
 * set a specific rdf
 * \param[in] type type of RR
 * \param[in] rr   the rr itself
 * \param[in] rdf  the rdf to set
 * \param[in] pos  at which postion to set it
 * \return true or false
 */
static bool
ldns_rr_set_function(ldns_rr_type type, ldns_rr *rr, ldns_rdf *rdf, size_t pos)
{
        ldns_rdf *pop;
        if (!rr || ldns_rr_get_type(rr) != type) {
                return false;
        }
        pop = ldns_rr_set_rdf(rr, rdf, pos);
 	ldns_rdf_deep_free(pop);
        return true;
}

/* A/AAAA records */
ldns_rdf *
ldns_rr_a_address(const ldns_rr *r)
{
	/* 2 types to check, cannot use the macro */
	if (!r || (ldns_rr_get_type(r) != LDNS_RR_TYPE_A &&
			ldns_rr_get_type(r) != LDNS_RR_TYPE_AAAA)) {
		return NULL;
	}
	return ldns_rr_rdf(r, 0);
}

bool
ldns_rr_a_set_address(ldns_rr *r, ldns_rdf *f)
{
	/* 2 types to check, cannot use the macro... */
	ldns_rdf *pop;
	if (!r || (ldns_rr_get_type(r) != LDNS_RR_TYPE_A &&
			ldns_rr_get_type(r) != LDNS_RR_TYPE_AAAA)) {
		return false;
	}
	pop = ldns_rr_set_rdf(r, f, 0);
	if (pop) {
		LDNS_FREE(pop);
		return true;
	} else {
		return false;
	}
}

/* NS record */
ldns_rdf *
ldns_rr_ns_nsdname(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_NS, r, 0);
}

/* MX record */
ldns_rdf *
ldns_rr_mx_preference(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_MX, r, 0);
}

ldns_rdf *
ldns_rr_mx_exchange(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_MX, r, 1);
}

/* RRSIG record */
ldns_rdf *
ldns_rr_rrsig_typecovered(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 0);
}

bool
ldns_rr_rrsig_set_typecovered(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 0);
}

ldns_rdf *
ldns_rr_rrsig_algorithm(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 1);
}

bool
ldns_rr_rrsig_set_algorithm(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 1);
}

ldns_rdf *
ldns_rr_rrsig_labels(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 2);
}

bool
ldns_rr_rrsig_set_labels(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 2);
}

ldns_rdf *
ldns_rr_rrsig_origttl(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 3);
}

bool
ldns_rr_rrsig_set_origttl(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 3);
}

ldns_rdf *
ldns_rr_rrsig_expiration(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 4);
}

bool
ldns_rr_rrsig_set_expiration(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 4);
}

ldns_rdf *
ldns_rr_rrsig_inception(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 5);
}

bool
ldns_rr_rrsig_set_inception(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 5);
}

ldns_rdf *
ldns_rr_rrsig_keytag(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 6);
}

bool
ldns_rr_rrsig_set_keytag(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 6);
}

ldns_rdf *
ldns_rr_rrsig_signame(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 7);
}

bool
ldns_rr_rrsig_set_signame(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 7);
}

ldns_rdf *
ldns_rr_rrsig_sig(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_RRSIG, r, 8);
}

bool
ldns_rr_rrsig_set_sig(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_RRSIG, r, f, 8);
}

/* DNSKEY record */
ldns_rdf *
ldns_rr_dnskey_flags(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_DNSKEY, r, 0);
}

bool
ldns_rr_dnskey_set_flags(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_DNSKEY, r, f, 0);
}

ldns_rdf *
ldns_rr_dnskey_protocol(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_DNSKEY, r, 1);
}

bool
ldns_rr_dnskey_set_protocol(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_DNSKEY, r, f, 1);
}

ldns_rdf *
ldns_rr_dnskey_algorithm(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_DNSKEY, r, 2);
}

bool
ldns_rr_dnskey_set_algorithm(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_DNSKEY, r, f, 2);
}

ldns_rdf *
ldns_rr_dnskey_key(const ldns_rr *r)
{
	return ldns_rr_function(LDNS_RR_TYPE_DNSKEY, r, 3);
}

bool
ldns_rr_dnskey_set_key(ldns_rr *r, ldns_rdf *f)
{
	return ldns_rr_set_function(LDNS_RR_TYPE_DNSKEY, r, f, 3);
}

size_t
ldns_rr_dnskey_key_size_raw(const unsigned char* keydata,
                            const size_t len,
                            const ldns_algorithm alg)
{
	/* for DSA keys */
	uint8_t t;
	
	/* for RSA keys */
	uint16_t exp;
	uint16_t int16;
	
	switch ((ldns_signing_algorithm)alg) {
	case LDNS_SIGN_DSA:
	case LDNS_SIGN_DSA_NSEC3:
		if (len > 0) {
			t = keydata[0];
			return (64 + t*8)*8;
		} else {
			return 0;
		}
		break;
	case LDNS_SIGN_RSAMD5:
	case LDNS_SIGN_RSASHA1:
	case LDNS_SIGN_RSASHA1_NSEC3:
#ifdef USE_SHA2
	case LDNS_SIGN_RSASHA256:
	case LDNS_SIGN_RSASHA512:
#endif
		if (len > 0) {
			if (keydata[0] == 0) {
				/* big exponent */
				if (len > 3) {
					memmove(&int16, keydata + 1, 2);
					exp = ntohs(int16);
					return (len - exp - 3)*8;
				} else {
					return 0;
				}
			} else {
				exp = keydata[0];
				return (len-exp-1)*8;
			}
		} else {
			return 0;
		}
		break;
#ifdef USE_GOST
	case LDNS_SIGN_ECC_GOST:
		return 512;
#endif
#ifdef USE_ECDSA
        case LDNS_SIGN_ECDSAP256SHA256:
                return 256;
        case LDNS_SIGN_ECDSAP384SHA384:
                return 384;
#endif
#ifdef USE_ED25519
	case LDNS_SIGN_ED25519:
		return 256;
#endif
#ifdef USE_ED448
	case LDNS_SIGN_ED448:
		return 456;
#endif
	case LDNS_SIGN_HMACMD5:
		return len;
	default:
		return 0;
	}
}

size_t 
ldns_rr_dnskey_key_size(const ldns_rr *key) 
{
	if (!key || !ldns_rr_dnskey_key(key) 
			|| !ldns_rr_dnskey_algorithm(key)) {
		return 0;
	}
	return ldns_rr_dnskey_key_size_raw((unsigned char*)ldns_rdf_data(ldns_rr_dnskey_key(key)),
	                                   ldns_rdf_size(ldns_rr_dnskey_key(key)),
	                                   ldns_rdf2native_int8(ldns_rr_dnskey_algorithm(key))
	                                  );
}

uint32_t ldns_soa_serial_identity(uint32_t ATTR_UNUSED(unused), void *data)
{
	return (uint32_t) (intptr_t) data;
}

uint32_t ldns_soa_serial_increment(uint32_t s, void *ATTR_UNUSED(unused))
{
	return ldns_soa_serial_increment_by(s, (void *)1);
}

uint32_t ldns_soa_serial_increment_by(uint32_t s, void *data)
{
	return s + (intptr_t) data;
}

uint32_t ldns_soa_serial_datecounter(uint32_t s, void *data)
{
	struct tm tm;
	char s_str[11];
	int32_t new_s;
	time_t t = data ? (time_t) (intptr_t) data : ldns_time(NULL);

	(void) strftime(s_str, 11, "%Y%m%d00", localtime_r(&t, &tm));
	new_s = (int32_t) atoi(s_str);
	return new_s - ((int32_t) s) <= 0 ? s+1 : ((uint32_t) new_s);
}

uint32_t ldns_soa_serial_unixtime(uint32_t s, void *data)
{
	int32_t new_s = data ? (int32_t) (intptr_t) data 
			     : (int32_t) ldns_time(NULL);
	return new_s - ((int32_t) s) <= 0 ? s+1 : ((uint32_t) new_s);
}

void
ldns_rr_soa_increment(ldns_rr *soa)
{
	ldns_rr_soa_increment_func_data(soa, ldns_soa_serial_increment, NULL);
}

void
ldns_rr_soa_increment_func(ldns_rr *soa, ldns_soa_serial_increment_func_t f)
{
	ldns_rr_soa_increment_func_data(soa, f, NULL);
}

void
ldns_rr_soa_increment_func_data(ldns_rr *soa, 
		ldns_soa_serial_increment_func_t f, void *data)
{
	ldns_rdf *prev_soa_serial_rdf;
	if ( !soa || !f || ldns_rr_get_type(soa) != LDNS_RR_TYPE_SOA 
			|| !ldns_rr_rdf(soa, 2)) {
		return;
	}
	prev_soa_serial_rdf = ldns_rr_set_rdf(
		  soa
		, ldns_native2rdf_int32(
			  LDNS_RDF_TYPE_INT32
			, (*f)( ldns_rdf2native_int32(
					ldns_rr_rdf(soa, 2))
			      , data
			)
		)
		, 2
	);
	LDNS_FREE(prev_soa_serial_rdf);
}

void
ldns_rr_soa_increment_func_int(ldns_rr *soa, 
		ldns_soa_serial_increment_func_t f, int data)
{
	ldns_rr_soa_increment_func_data(soa, f, (void *) (intptr_t) data);
}

