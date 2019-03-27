/*
 * tsig.c
 *
 * contains the functions needed for TSIG [RFC2845]
 *
 * (c) 2005-2006 NLnet Labs
 * See the file LICENSE for the license
 */

#include <ldns/config.h>

#include <ldns/ldns.h>

#include <strings.h>

#ifdef HAVE_SSL
#include <openssl/hmac.h>
#include <openssl/md5.h>
#endif /* HAVE_SSL */

const char *
ldns_tsig_algorithm(const ldns_tsig_credentials *tc)
{
	return tc->algorithm;
}

const char *
ldns_tsig_keyname(const ldns_tsig_credentials *tc)
{
	return tc->keyname;
}

const char *
ldns_tsig_keydata(const ldns_tsig_credentials *tc)
{
	return tc->keydata;
}

char *
ldns_tsig_keyname_clone(const ldns_tsig_credentials *tc)
{
	return strdup(tc->keyname);
}

char *
ldns_tsig_keydata_clone(const ldns_tsig_credentials *tc)
{
	return strdup(tc->keydata);
}

/*
 *  Makes an exact copy of the wire, but with the tsig rr removed
 */
static uint8_t *
ldns_tsig_prepare_pkt_wire(const uint8_t *wire, size_t wire_len, size_t *result_len)
{
	uint8_t *wire2 = NULL;
	uint16_t qd_count;
	uint16_t an_count;
	uint16_t ns_count;
	uint16_t ar_count;
	ldns_rr *rr;

	size_t pos;
	uint16_t i;

	ldns_status status;

	if(wire_len < LDNS_HEADER_SIZE) {
		return NULL;
	}
	/* fake parse the wire */
	qd_count = LDNS_QDCOUNT(wire);
	an_count = LDNS_ANCOUNT(wire);
	ns_count = LDNS_NSCOUNT(wire);
	ar_count = LDNS_ARCOUNT(wire);

	if (ar_count > 0) {
		ar_count--;
	} else {
		return NULL;
	}

	pos = LDNS_HEADER_SIZE;

	for (i = 0; i < qd_count; i++) {
		status = ldns_wire2rr(&rr, wire, wire_len, &pos, LDNS_SECTION_QUESTION);
		if (status != LDNS_STATUS_OK) {
			return NULL;
		}
		ldns_rr_free(rr);
	}

	for (i = 0; i < an_count; i++) {
		status = ldns_wire2rr(&rr, wire, wire_len, &pos, LDNS_SECTION_ANSWER);
		if (status != LDNS_STATUS_OK) {
			return NULL;
		}
		ldns_rr_free(rr);
	}

	for (i = 0; i < ns_count; i++) {
		status = ldns_wire2rr(&rr, wire, wire_len, &pos, LDNS_SECTION_AUTHORITY);
		if (status != LDNS_STATUS_OK) {
			return NULL;
		}
		ldns_rr_free(rr);
	}

	for (i = 0; i < ar_count; i++) {
		status = ldns_wire2rr(&rr, wire, wire_len, &pos,
				LDNS_SECTION_ADDITIONAL);
		if (status != LDNS_STATUS_OK) {
			return NULL;
		}
		ldns_rr_free(rr);
	}

	*result_len = pos;
	wire2 = LDNS_XMALLOC(uint8_t, *result_len);
	if(!wire2) {
		return NULL;
	}
	memcpy(wire2, wire, *result_len);

	ldns_write_uint16(wire2 + LDNS_ARCOUNT_OFF, ar_count);

	return wire2;
}

#ifdef HAVE_SSL
static const EVP_MD *
ldns_digest_function(char *name)
{
	/* these are the mandatory algorithms from RFC4635 */
	/* The optional algorithms are not yet implemented */
	if (strcasecmp(name, "hmac-sha512.") == 0) {
#ifdef HAVE_EVP_SHA512
		return EVP_sha512();
#else
		return NULL;
#endif
	} else if (strcasecmp(name, "hmac-shac384.") == 0) {
#ifdef HAVE_EVP_SHA384
		return EVP_sha384();
#else
		return NULL;
#endif
	} else if (strcasecmp(name, "hmac-sha256.") == 0) {
#ifdef HAVE_EVP_SHA256
		return EVP_sha256();
#else
		return NULL;
#endif
	} else if (strcasecmp(name, "hmac-sha1.") == 0) {
		return EVP_sha1();
	} else if (strcasecmp(name, "hmac-md5.sig-alg.reg.int.") == 0) {
		return EVP_md5();
	} else {
		return NULL;
	}
}
#endif

#ifdef HAVE_SSL
static ldns_status
ldns_tsig_mac_new(ldns_rdf **tsig_mac, const uint8_t *pkt_wire, size_t pkt_wire_size,
		const char *key_data, const ldns_rdf *key_name_rdf, const ldns_rdf *fudge_rdf,
		const ldns_rdf *algorithm_rdf, const ldns_rdf *time_signed_rdf, const ldns_rdf *error_rdf,
		const ldns_rdf *other_data_rdf, const ldns_rdf *orig_mac_rdf, int tsig_timers_only)
{
	ldns_status status;
	char *wireformat;
	int wiresize;
	unsigned char *mac_bytes = NULL;
	unsigned char *key_bytes = NULL;
	int key_size;
	const EVP_MD *digester;
	char *algorithm_name = NULL;
	unsigned int md_len = EVP_MAX_MD_SIZE;
	ldns_rdf *result = NULL;
	ldns_buffer *data_buffer = NULL;
	ldns_rdf *canonical_key_name_rdf = NULL;
	ldns_rdf *canonical_algorithm_rdf = NULL;
	
	if (key_name_rdf == NULL || algorithm_rdf == NULL) {
		return LDNS_STATUS_NULL;
	}
	canonical_key_name_rdf  = ldns_rdf_clone(key_name_rdf);
	if (canonical_key_name_rdf == NULL) {
		return LDNS_STATUS_MEM_ERR;
	}
	canonical_algorithm_rdf = ldns_rdf_clone(algorithm_rdf);
	if (canonical_algorithm_rdf == NULL) {
		ldns_rdf_deep_free(canonical_key_name_rdf);
		return LDNS_STATUS_MEM_ERR;
	}
	/*
	 * prepare the digestable information
	 */
	data_buffer = ldns_buffer_new(LDNS_MAX_PACKETLEN);
	if (!data_buffer) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}
	/* if orig_mac is not NULL, add it too */
	if (orig_mac_rdf) {
		(void) ldns_rdf2buffer_wire(data_buffer, orig_mac_rdf);
 	}
	ldns_buffer_write(data_buffer, pkt_wire, pkt_wire_size);
	if (!tsig_timers_only) {
		ldns_dname2canonical(canonical_key_name_rdf);
		(void)ldns_rdf2buffer_wire(data_buffer, 
				canonical_key_name_rdf);
		ldns_buffer_write_u16(data_buffer, LDNS_RR_CLASS_ANY);
		ldns_buffer_write_u32(data_buffer, 0);
		ldns_dname2canonical(canonical_algorithm_rdf);
		(void)ldns_rdf2buffer_wire(data_buffer, 
				canonical_algorithm_rdf);
	}
	(void)ldns_rdf2buffer_wire(data_buffer, time_signed_rdf);
	(void)ldns_rdf2buffer_wire(data_buffer, fudge_rdf);
	if (!tsig_timers_only) {
		(void)ldns_rdf2buffer_wire(data_buffer, error_rdf);
		(void)ldns_rdf2buffer_wire(data_buffer, other_data_rdf);
	}

	wireformat = (char *) data_buffer->_data;
	wiresize = (int) ldns_buffer_position(data_buffer);

	algorithm_name = ldns_rdf2str(algorithm_rdf);
	if(!algorithm_name) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}

	/* prepare the key */
	key_bytes = LDNS_XMALLOC(unsigned char,
			ldns_b64_pton_calculate_size(strlen(key_data)));
	if(!key_bytes) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}
	key_size = ldns_b64_pton(key_data, key_bytes,
	ldns_b64_pton_calculate_size(strlen(key_data)));
	if (key_size < 0) {
		status = LDNS_STATUS_INVALID_B64;
		goto clean;
	}
	/* hmac it */
	/* 2 spare bytes for the length */
	mac_bytes = LDNS_XMALLOC(unsigned char, md_len+2);
	if(!mac_bytes) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}
	memset(mac_bytes, 0, md_len+2);

	digester = ldns_digest_function(algorithm_name);

	if (digester) {
		(void) HMAC(digester, key_bytes, key_size, (void *)wireformat,
		            (size_t) wiresize, mac_bytes + 2, &md_len);

		ldns_write_uint16(mac_bytes, md_len);
		result = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_INT16_DATA, md_len + 2,
				mac_bytes);
	} else {
		status = LDNS_STATUS_CRYPTO_UNKNOWN_ALGO;
		goto clean;
	}
	*tsig_mac = result;
	status = LDNS_STATUS_OK;
  clean:
	LDNS_FREE(mac_bytes);
	LDNS_FREE(key_bytes);
	LDNS_FREE(algorithm_name);
	ldns_buffer_free(data_buffer);
	ldns_rdf_deep_free(canonical_algorithm_rdf);
	ldns_rdf_deep_free(canonical_key_name_rdf);
	return status;
}
#endif /*  HAVE_SSL */


#ifdef HAVE_SSL
bool
ldns_pkt_tsig_verify(ldns_pkt *pkt, const uint8_t *wire, size_t wirelen, const char *key_name,
	const char *key_data, const ldns_rdf *orig_mac_rdf)
{
	return ldns_pkt_tsig_verify_next(pkt, wire, wirelen, key_name, key_data, orig_mac_rdf, 0);
}

bool
ldns_pkt_tsig_verify_next(ldns_pkt *pkt, const uint8_t *wire, size_t wirelen, const char* key_name,
	const char *key_data, const ldns_rdf *orig_mac_rdf, int tsig_timers_only)
{
	ldns_rdf *fudge_rdf;
	ldns_rdf *algorithm_rdf;
	ldns_rdf *time_signed_rdf;
	ldns_rdf *orig_id_rdf;
	ldns_rdf *error_rdf;
	ldns_rdf *other_data_rdf;
	ldns_rdf *pkt_mac_rdf;
	ldns_rdf *my_mac_rdf;
	ldns_rdf *key_name_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, key_name);
	uint16_t pkt_id, orig_pkt_id;
	ldns_status status;

	uint8_t *prepared_wire = NULL;
	size_t prepared_wire_size = 0;

	ldns_rr *orig_tsig = ldns_pkt_tsig(pkt);

	if (!orig_tsig || ldns_rr_rd_count(orig_tsig) <= 6) {
		ldns_rdf_deep_free(key_name_rdf);
		return false;
	}
	algorithm_rdf = ldns_rr_rdf(orig_tsig, 0);
	time_signed_rdf = ldns_rr_rdf(orig_tsig, 1);
	fudge_rdf = ldns_rr_rdf(orig_tsig, 2);
	pkt_mac_rdf = ldns_rr_rdf(orig_tsig, 3);
	orig_id_rdf = ldns_rr_rdf(orig_tsig, 4);
	error_rdf = ldns_rr_rdf(orig_tsig, 5);
	other_data_rdf = ldns_rr_rdf(orig_tsig, 6);

	/* remove temporarily */
	ldns_pkt_set_tsig(pkt, NULL);
	/* temporarily change the id to the original id */
	pkt_id = ldns_pkt_id(pkt);
	orig_pkt_id = ldns_rdf2native_int16(orig_id_rdf);
	ldns_pkt_set_id(pkt, orig_pkt_id);

	prepared_wire = ldns_tsig_prepare_pkt_wire(wire, wirelen, &prepared_wire_size);

	status = ldns_tsig_mac_new(&my_mac_rdf, prepared_wire, prepared_wire_size,
			key_data, key_name_rdf, fudge_rdf, algorithm_rdf,
			time_signed_rdf, error_rdf, other_data_rdf, orig_mac_rdf, tsig_timers_only);

	LDNS_FREE(prepared_wire);

	if (status != LDNS_STATUS_OK) {
		ldns_rdf_deep_free(key_name_rdf);
		return false;
	}
	/* Put back the values */
	ldns_pkt_set_tsig(pkt, orig_tsig);
	ldns_pkt_set_id(pkt, pkt_id);

	ldns_rdf_deep_free(key_name_rdf);

	if (ldns_rdf_compare(pkt_mac_rdf, my_mac_rdf) == 0) {
		ldns_rdf_deep_free(my_mac_rdf);
		return true;
	} else {
		ldns_rdf_deep_free(my_mac_rdf);
		return false;
	}
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
ldns_status
ldns_pkt_tsig_sign(ldns_pkt *pkt, const char *key_name, const char *key_data,
	uint16_t fudge, const char *algorithm_name, const ldns_rdf *query_mac)
{
	return ldns_pkt_tsig_sign_next(pkt, key_name, key_data, fudge, algorithm_name, query_mac, 0);
}

ldns_status
ldns_pkt_tsig_sign_next(ldns_pkt *pkt, const char *key_name, const char *key_data,
	uint16_t fudge, const char *algorithm_name, const ldns_rdf *query_mac, int tsig_timers_only)
{
	ldns_rr *tsig_rr;
	ldns_rdf *key_name_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, key_name);
	ldns_rdf *fudge_rdf = NULL;
	ldns_rdf *orig_id_rdf = NULL;
	ldns_rdf *algorithm_rdf;
	ldns_rdf *error_rdf = NULL;
	ldns_rdf *mac_rdf = NULL;
	ldns_rdf *other_data_rdf = NULL;

	ldns_status status = LDNS_STATUS_OK;

	uint8_t *pkt_wire = NULL;
	size_t pkt_wire_len;

	struct timeval tv_time_signed;
	uint8_t *time_signed = NULL;
	ldns_rdf *time_signed_rdf = NULL;

	algorithm_rdf = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, algorithm_name);
	if(!key_name_rdf || !algorithm_rdf) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}

	/* eww don't have create tsigtime rdf yet :( */
	/* bleh :p */
	if (gettimeofday(&tv_time_signed, NULL) == 0) {
		time_signed = LDNS_XMALLOC(uint8_t, 6);
		if(!time_signed) {
			status = LDNS_STATUS_MEM_ERR;
			goto clean;
		}
		ldns_write_uint64_as_uint48(time_signed,
				(uint64_t)tv_time_signed.tv_sec);
	} else {
		status = LDNS_STATUS_INTERNAL_ERR;
		goto clean;
	}

	time_signed_rdf = ldns_rdf_new(LDNS_RDF_TYPE_TSIGTIME, 6, time_signed);
	if(!time_signed_rdf) {
		LDNS_FREE(time_signed);
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}

	fudge_rdf = ldns_native2rdf_int16(LDNS_RDF_TYPE_INT16, fudge);

	orig_id_rdf = ldns_native2rdf_int16(LDNS_RDF_TYPE_INT16, ldns_pkt_id(pkt));

	error_rdf = ldns_native2rdf_int16(LDNS_RDF_TYPE_INT16, 0);

	other_data_rdf = ldns_native2rdf_int16_data(0, NULL);

	if(!fudge_rdf || !orig_id_rdf || !error_rdf || !other_data_rdf) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}

	if (ldns_pkt2wire(&pkt_wire, pkt, &pkt_wire_len) != LDNS_STATUS_OK) {
		status = LDNS_STATUS_ERR;
		goto clean;
	}

	status = ldns_tsig_mac_new(&mac_rdf, pkt_wire, pkt_wire_len,
			key_data, key_name_rdf, fudge_rdf, algorithm_rdf,
			time_signed_rdf, error_rdf, other_data_rdf, query_mac, tsig_timers_only);

	if (!mac_rdf) {
		goto clean;
	}

	LDNS_FREE(pkt_wire);

	/* Create the TSIG RR */
	tsig_rr = ldns_rr_new();
	if(!tsig_rr) {
		status = LDNS_STATUS_MEM_ERR;
		goto clean;
	}
	ldns_rr_set_owner(tsig_rr, key_name_rdf);
	ldns_rr_set_class(tsig_rr, LDNS_RR_CLASS_ANY);
	ldns_rr_set_type(tsig_rr, LDNS_RR_TYPE_TSIG);
	ldns_rr_set_ttl(tsig_rr, 0);

	ldns_rr_push_rdf(tsig_rr, algorithm_rdf);
	ldns_rr_push_rdf(tsig_rr, time_signed_rdf);
	ldns_rr_push_rdf(tsig_rr, fudge_rdf);
	ldns_rr_push_rdf(tsig_rr, mac_rdf);
	ldns_rr_push_rdf(tsig_rr, orig_id_rdf);
	ldns_rr_push_rdf(tsig_rr, error_rdf);
	ldns_rr_push_rdf(tsig_rr, other_data_rdf);

	ldns_pkt_set_tsig(pkt, tsig_rr);

	return status;

  clean:
	LDNS_FREE(pkt_wire);
	ldns_rdf_free(key_name_rdf);
	ldns_rdf_free(algorithm_rdf);
	ldns_rdf_free(time_signed_rdf);
	ldns_rdf_free(fudge_rdf);
	ldns_rdf_free(orig_id_rdf);
	ldns_rdf_free(error_rdf);
	ldns_rdf_free(other_data_rdf);
	return status;
}
#endif /* HAVE_SSL */
