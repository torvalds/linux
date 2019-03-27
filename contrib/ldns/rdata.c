/*
 * rdata.c
 *
 * rdata implementation
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */

#include <ldns/config.h>

#include <ldns/ldns.h>

/*
 * Access functions 
 * do this as functions to get type checking
 */

/* read */
size_t
ldns_rdf_size(const ldns_rdf *rd)
{
	assert(rd != NULL);
	return rd->_size;
}

ldns_rdf_type
ldns_rdf_get_type(const ldns_rdf *rd)
{
	assert(rd != NULL);
	return rd->_type;
}

uint8_t *
ldns_rdf_data(const ldns_rdf *rd)
{
	assert(rd != NULL);
	return rd->_data;
}

/* write */
void
ldns_rdf_set_size(ldns_rdf *rd, size_t size)
{
	assert(rd != NULL);
	rd->_size = size;
}

void
ldns_rdf_set_type(ldns_rdf *rd, ldns_rdf_type type)
{
	assert(rd != NULL);
	rd->_type = type;
}

void
ldns_rdf_set_data(ldns_rdf *rd, void *data)
{
	/* only copy the pointer */
	assert(rd != NULL);
	rd->_data = data;
}

/* for types that allow it, return
 * the native/host order type */
uint8_t
ldns_rdf2native_int8(const ldns_rdf *rd)
{
	uint8_t data;

	/* only allow 8 bit rdfs */
	if (ldns_rdf_size(rd) != LDNS_RDF_SIZE_BYTE) {
		return 0;
	}
	
	memcpy(&data, ldns_rdf_data(rd), sizeof(data));
	return data;
}

uint16_t
ldns_rdf2native_int16(const ldns_rdf *rd)
{
	uint16_t data;

	/* only allow 16 bit rdfs */
	if (ldns_rdf_size(rd) != LDNS_RDF_SIZE_WORD) {
		return 0;
	}
	
	memcpy(&data, ldns_rdf_data(rd), sizeof(data));
	return ntohs(data);
}

uint32_t
ldns_rdf2native_int32(const ldns_rdf *rd)
{
	uint32_t data;

	/* only allow 32 bit rdfs */
	if (ldns_rdf_size(rd) != LDNS_RDF_SIZE_DOUBLEWORD) {
		return 0;
	}
	
	memcpy(&data, ldns_rdf_data(rd), sizeof(data));
	return ntohl(data);
}

time_t
ldns_rdf2native_time_t(const ldns_rdf *rd)
{
	uint32_t data;

	/* only allow 32 bit rdfs */
	if (ldns_rdf_size(rd) != LDNS_RDF_SIZE_DOUBLEWORD ||
			ldns_rdf_get_type(rd) != LDNS_RDF_TYPE_TIME) {
		return 0;
	}
	memcpy(&data, ldns_rdf_data(rd), sizeof(data));
	return (time_t)ntohl(data);
}

ldns_rdf *
ldns_native2rdf_int8(ldns_rdf_type type, uint8_t value)
{
	return ldns_rdf_new_frm_data(type, LDNS_RDF_SIZE_BYTE, &value);
}

ldns_rdf *
ldns_native2rdf_int16(ldns_rdf_type type, uint16_t value)
{
	uint16_t *rdf_data = LDNS_XMALLOC(uint16_t, 1);
        ldns_rdf* rdf;
	if (!rdf_data) {
		return NULL;
	}
	ldns_write_uint16(rdf_data, value);
	rdf = ldns_rdf_new(type, LDNS_RDF_SIZE_WORD, rdf_data);
        if(!rdf)
                LDNS_FREE(rdf_data);
        return rdf;
}

ldns_rdf *
ldns_native2rdf_int32(ldns_rdf_type type, uint32_t value)
{
	uint32_t *rdf_data = LDNS_XMALLOC(uint32_t, 1);
        ldns_rdf* rdf;
	if (!rdf_data) {
		return NULL;
	}
	ldns_write_uint32(rdf_data, value);
	rdf = ldns_rdf_new(type, LDNS_RDF_SIZE_DOUBLEWORD, rdf_data);
        if(!rdf)
                LDNS_FREE(rdf_data);
        return rdf;
}

ldns_rdf *
ldns_native2rdf_int16_data(size_t size, uint8_t *data)
{
	uint8_t *rdf_data = LDNS_XMALLOC(uint8_t, size + 2);
        ldns_rdf* rdf;
	if (!rdf_data) {
		return NULL;
	}
	ldns_write_uint16(rdf_data, size);
	memcpy(rdf_data + 2, data, size);
	rdf = ldns_rdf_new(LDNS_RDF_TYPE_INT16_DATA, size + 2, rdf_data);
        if(!rdf)
                LDNS_FREE(rdf_data);
        return rdf;
}

/* note: data must be allocated memory */
ldns_rdf *
ldns_rdf_new(ldns_rdf_type type, size_t size, void *data)
{
	ldns_rdf *rd;
	rd = LDNS_MALLOC(ldns_rdf);
	if (!rd) {
		return NULL;
	}
	ldns_rdf_set_size(rd, size);
	ldns_rdf_set_type(rd, type);
	ldns_rdf_set_data(rd, data);
	return rd;
}

ldns_rdf *
ldns_rdf_new_frm_data(ldns_rdf_type type, size_t size, const void *data)
{
	ldns_rdf *rdf;

	/* if the size is too big, fail */
	if (size > LDNS_MAX_RDFLEN) {
		return NULL;
	}

	/* allocate space */
	rdf = LDNS_MALLOC(ldns_rdf);
	if (!rdf) {
		return NULL;
	}
	rdf->_data = LDNS_XMALLOC(uint8_t, size);
	if (!rdf->_data) {
		LDNS_FREE(rdf);
		return NULL;
	}
	
	/* set the values */
	ldns_rdf_set_type(rdf, type);
	ldns_rdf_set_size(rdf, size);
	memcpy(rdf->_data, data, size);

	return rdf;
}

ldns_rdf *
ldns_rdf_clone(const ldns_rdf *rd)
{
	assert(rd != NULL);
	return (ldns_rdf_new_frm_data( ldns_rdf_get_type(rd),
		ldns_rdf_size(rd), ldns_rdf_data(rd)));
}

void
ldns_rdf_deep_free(ldns_rdf *rd)
{
	if (rd) {
		if (rd->_data) {
			LDNS_FREE(rd->_data);
		}
		LDNS_FREE(rd);
	}
}

void 
ldns_rdf_free(ldns_rdf *rd)
{
	if (rd) {
		LDNS_FREE(rd);
	}
}

ldns_rdf *
ldns_rdf_new_frm_str(ldns_rdf_type type, const char *str)
{
	ldns_rdf *rdf = NULL;
	ldns_status status;

	switch (type) {
	case LDNS_RDF_TYPE_DNAME:
		status = ldns_str2rdf_dname(&rdf, str);
		break;
	case LDNS_RDF_TYPE_INT8:
		status = ldns_str2rdf_int8(&rdf, str);
		break;
	case LDNS_RDF_TYPE_INT16:
		status = ldns_str2rdf_int16(&rdf, str);
		break;
	case LDNS_RDF_TYPE_INT32:
		status = ldns_str2rdf_int32(&rdf, str);
		break;
	case LDNS_RDF_TYPE_A:
		status = ldns_str2rdf_a(&rdf, str);
		break;
	case LDNS_RDF_TYPE_AAAA:
		status = ldns_str2rdf_aaaa(&rdf, str);
		break;
	case LDNS_RDF_TYPE_STR:
		status = ldns_str2rdf_str(&rdf, str);
		break;
	case LDNS_RDF_TYPE_APL:
		status = ldns_str2rdf_apl(&rdf, str);
		break;
	case LDNS_RDF_TYPE_B64:
		status = ldns_str2rdf_b64(&rdf, str);
		break;
	case LDNS_RDF_TYPE_B32_EXT:
		status = ldns_str2rdf_b32_ext(&rdf, str);
		break;
	case LDNS_RDF_TYPE_HEX:
		status = ldns_str2rdf_hex(&rdf, str);
		break;
	case LDNS_RDF_TYPE_NSEC:
		status = ldns_str2rdf_nsec(&rdf, str);
		break;
	case LDNS_RDF_TYPE_TYPE:
		status = ldns_str2rdf_type(&rdf, str);
		break;
	case LDNS_RDF_TYPE_CLASS:
		status = ldns_str2rdf_class(&rdf, str);
		break;
	case LDNS_RDF_TYPE_CERT_ALG:
		status = ldns_str2rdf_cert_alg(&rdf, str);
		break;
	case LDNS_RDF_TYPE_ALG:
		status = ldns_str2rdf_alg(&rdf, str);
		break;
	case LDNS_RDF_TYPE_UNKNOWN:
		status = ldns_str2rdf_unknown(&rdf, str);
		break;
	case LDNS_RDF_TYPE_TIME:
		status = ldns_str2rdf_time(&rdf, str);
		break;
	case LDNS_RDF_TYPE_PERIOD:
		status = ldns_str2rdf_period(&rdf, str);
		break;
	case LDNS_RDF_TYPE_HIP:
		status = ldns_str2rdf_hip(&rdf, str);
		break;
	case LDNS_RDF_TYPE_SERVICE:
		status = ldns_str2rdf_service(&rdf, str);
		break;
	case LDNS_RDF_TYPE_LOC:
		status = ldns_str2rdf_loc(&rdf, str);
		break;
	case LDNS_RDF_TYPE_WKS:
		status = ldns_str2rdf_wks(&rdf, str);
		break;
	case LDNS_RDF_TYPE_NSAP:
		status = ldns_str2rdf_nsap(&rdf, str);
		break;
	case LDNS_RDF_TYPE_ATMA:
		status = ldns_str2rdf_atma(&rdf, str);
		break;
	case LDNS_RDF_TYPE_IPSECKEY:
		status = ldns_str2rdf_ipseckey(&rdf, str);
		break;
	case LDNS_RDF_TYPE_NSEC3_SALT:
		status = ldns_str2rdf_nsec3_salt(&rdf, str);
		break;
	case LDNS_RDF_TYPE_NSEC3_NEXT_OWNER:
		status = ldns_str2rdf_b32_ext(&rdf, str);
		break;
	case LDNS_RDF_TYPE_ILNP64:
		status = ldns_str2rdf_ilnp64(&rdf, str);
		break;
	case LDNS_RDF_TYPE_EUI48:
		status = ldns_str2rdf_eui48(&rdf, str);
		break;
	case LDNS_RDF_TYPE_EUI64:
		status = ldns_str2rdf_eui64(&rdf, str);
		break;
	case LDNS_RDF_TYPE_TAG:
		status = ldns_str2rdf_tag(&rdf, str);
		break;
	case LDNS_RDF_TYPE_LONG_STR:
		status = ldns_str2rdf_long_str(&rdf, str);
		break;
	case LDNS_RDF_TYPE_CERTIFICATE_USAGE:
		status = ldns_str2rdf_certificate_usage(&rdf, str);
		break;
	case LDNS_RDF_TYPE_SELECTOR:
		status = ldns_str2rdf_selector(&rdf, str);
		break;
	case LDNS_RDF_TYPE_MATCHING_TYPE:
		status = ldns_str2rdf_matching_type(&rdf, str);
		break;
	case LDNS_RDF_TYPE_NONE:
	default:
		/* default default ??? */
		status = LDNS_STATUS_ERR;
		break;
	}
	if (LDNS_STATUS_OK == status) {
		ldns_rdf_set_type(rdf, type);
		return rdf;
	}
	if (rdf) {
		LDNS_FREE(rdf);
	}
	return NULL;
}

ldns_status
ldns_rdf_new_frm_fp(ldns_rdf **rdf, ldns_rdf_type type, FILE *fp)
{
	return ldns_rdf_new_frm_fp_l(rdf, type, fp, NULL);
}

ldns_status
ldns_rdf_new_frm_fp_l(ldns_rdf **rdf, ldns_rdf_type type, FILE *fp, int *line_nr)
{
	char *line;
	ldns_rdf *r;
	ssize_t t;

	line = LDNS_XMALLOC(char, LDNS_MAX_LINELEN + 1);
	if (!line) {
		return LDNS_STATUS_MEM_ERR;
	}

	/* read an entire line in from the file */
	if ((t = ldns_fget_token_l(fp, line, LDNS_PARSE_SKIP_SPACE, 0, line_nr)) == -1 || t == 0) {
		LDNS_FREE(line);
		return LDNS_STATUS_SYNTAX_RDATA_ERR;
	}
	r =  ldns_rdf_new_frm_str(type, (const char*) line);
	LDNS_FREE(line);
	if (rdf) {
		*rdf = r;
		return LDNS_STATUS_OK;
	} else {
		return LDNS_STATUS_NULL;
	}
}

ldns_rdf *
ldns_rdf_address_reverse(const ldns_rdf *rd)
{
	uint8_t buf_4[LDNS_IP4ADDRLEN];
	uint8_t buf_6[LDNS_IP6ADDRLEN * 2];
	ldns_rdf *rev;
	ldns_rdf *in_addr;
	ldns_rdf *ret_dname;
	uint8_t octet;
	uint8_t nnibble;
	uint8_t nibble;
	uint8_t i, j;

	char *char_dname;
	int nbit;

	if (ldns_rdf_get_type(rd) != LDNS_RDF_TYPE_A &&
			ldns_rdf_get_type(rd) != LDNS_RDF_TYPE_AAAA) {
		return NULL;
	}

	in_addr = NULL;
	ret_dname = NULL;

	switch(ldns_rdf_get_type(rd)) {
		case LDNS_RDF_TYPE_A:
			/* the length of the buffer is 4 */
			buf_4[3] = ldns_rdf_data(rd)[0];
			buf_4[2] = ldns_rdf_data(rd)[1];
			buf_4[1] = ldns_rdf_data(rd)[2];
			buf_4[0] = ldns_rdf_data(rd)[3];
			in_addr = ldns_dname_new_frm_str("in-addr.arpa.");
			if (!in_addr) {
				return NULL;
			}
			/* make a new rdf and convert that back  */
			rev = ldns_rdf_new_frm_data( LDNS_RDF_TYPE_A,
				LDNS_IP4ADDRLEN, (void*)&buf_4);
			if (!rev) {
				LDNS_FREE(in_addr);
				return NULL;
			}

			/* convert rev to a string */
			char_dname = ldns_rdf2str(rev);
			if (!char_dname) {
				LDNS_FREE(in_addr);
				ldns_rdf_deep_free(rev);
				return NULL;
			}
			/* transform back to rdf with type dname */
			ret_dname = ldns_dname_new_frm_str(char_dname);
			if (!ret_dname) {
				LDNS_FREE(in_addr);
				ldns_rdf_deep_free(rev);
				LDNS_FREE(char_dname);
				return NULL;
			}
			/* not needed anymore */
			ldns_rdf_deep_free(rev);
			LDNS_FREE(char_dname);
			break;
		case LDNS_RDF_TYPE_AAAA:
			/* some foo magic to reverse the nibbles ... */

			for (nbit = 127; nbit >= 0; nbit = nbit - 4) {
				/* calculate octet (8 bit) */
				octet = ( ((unsigned int) nbit) & 0x78) >> 3;
				/* calculate nibble */
				nnibble = ( ((unsigned int) nbit) & 0x04) >> 2;
				/* extract nibble */
				nibble = (ldns_rdf_data(rd)[octet] & ( 0xf << (4 * (1 -
						 nnibble)) ) ) >> ( 4 * (1 - 
						nnibble));

				buf_6[(LDNS_IP6ADDRLEN * 2 - 1) -
					(octet * 2 + nnibble)] = 
						(uint8_t)ldns_int_to_hexdigit((int)nibble);
			}

			char_dname = LDNS_XMALLOC(char, (LDNS_IP6ADDRLEN * 4));
			if (!char_dname) {
				return NULL;
			}
			char_dname[LDNS_IP6ADDRLEN * 4 - 1] = '\0'; /* closure */

			/* walk the string and add . 's */
			for (i = 0, j = 0; i < LDNS_IP6ADDRLEN * 2; i++, j = j + 2) {
				char_dname[j] = (char)buf_6[i];
				if (i != LDNS_IP6ADDRLEN * 2 - 1) {
					char_dname[j + 1] = '.';
				}
			}
			in_addr = ldns_dname_new_frm_str("ip6.arpa.");
			if (!in_addr) {
				LDNS_FREE(char_dname);
				return NULL;
			}

			/* convert rev to a string */
			ret_dname = ldns_dname_new_frm_str(char_dname);
			LDNS_FREE(char_dname);
			if (!ret_dname) {
				ldns_rdf_deep_free(in_addr);
				return NULL;
			}
			break;
		default:
			break;
	}
	/* add the suffix */
	rev = ldns_dname_cat_clone(ret_dname, in_addr);

	ldns_rdf_deep_free(ret_dname);
	ldns_rdf_deep_free(in_addr);
	return rev;
}

ldns_status
ldns_rdf_hip_get_alg_hit_pk(ldns_rdf *rdf, uint8_t* alg,
                            uint8_t *hit_size, uint8_t** hit,
                            uint16_t *pk_size, uint8_t** pk)
{
	uint8_t *data;
	size_t rdf_size;

	if (! rdf || ! alg || ! hit || ! hit_size || ! pk || ! pk_size) {
		return LDNS_STATUS_INVALID_POINTER;
	} else if (ldns_rdf_get_type(rdf) != LDNS_RDF_TYPE_HIP) {
		return LDNS_STATUS_INVALID_RDF_TYPE;
	} else if ((rdf_size = ldns_rdf_size(rdf)) < 6) {
		return LDNS_STATUS_WIRE_RDATA_ERR;
	}
	data = ldns_rdf_data(rdf);
	*hit_size = data[0];
	*alg      = data[1];
	*pk_size  = ldns_read_uint16(data + 2);
	*hit      = data + 4;
	*pk       = data + 4 + *hit_size;
	if (*hit_size == 0 || *pk_size == 0 ||
			rdf_size < (size_t) *hit_size + *pk_size + 4) {
		return LDNS_STATUS_WIRE_RDATA_ERR;
	}
	return LDNS_STATUS_OK;
}

ldns_status
ldns_rdf_hip_new_frm_alg_hit_pk(ldns_rdf** rdf, uint8_t alg,
                                uint8_t hit_size, uint8_t *hit,
				uint16_t pk_size, uint8_t *pk)
{
	uint8_t *data;

	if (! rdf) {
		return LDNS_STATUS_INVALID_POINTER;
	}
	if (4 + hit_size + pk_size > LDNS_MAX_RDFLEN) {
		return LDNS_STATUS_RDATA_OVERFLOW;
	}
	data = LDNS_XMALLOC(uint8_t, 4 + hit_size + pk_size);
	if (data == NULL) {
		return LDNS_STATUS_MEM_ERR;
	}
	data[0] = hit_size;
	data[1] = alg;
	ldns_write_uint16(data + 2, pk_size);
	memcpy(data + 4, hit, hit_size);
	memcpy(data + 4 + hit_size, pk, pk_size);
	*rdf = ldns_rdf_new(LDNS_RDF_TYPE_HIP, 4 + hit_size + pk_size, data);
	if (! *rdf) {
		LDNS_FREE(data);
		return LDNS_STATUS_MEM_ERR;
	}
	return LDNS_STATUS_OK;
}

ldns_status
ldns_octet(char *word, size_t *length)
{
    char *s; 
    char *p;
    *length = 0;

    for (s = p = word; *s != '\0'; s++,p++) {
        switch (*s) {
            case '.':
                if (s[1] == '.') {
		    return LDNS_STATUS_EMPTY_LABEL;
                }
                *p = *s;
                (*length)++;
                break;
            case '\\':
                if ('0' <= s[1] && s[1] <= '9' &&
                    '0' <= s[2] && s[2] <= '9' &&
                    '0' <= s[3] && s[3] <= '9') {
                    /* \DDD seen */
                    int val = ((s[1] - '0') * 100 +
                           (s[2] - '0') * 10 + (s[3] - '0'));

                    if (0 <= val && val <= 255) {
                        /* this also handles \0 */
                        s += 3;
                        *p = val;
                        (*length)++;
                    } else {
                        return LDNS_STATUS_DDD_OVERFLOW;
                    }
                } else {
                    /* an espaced character, like \<space> ? 
                    * remove the '\' keep the rest */
                    *p = *++s;
                    (*length)++;
                }
                break;
            case '\"':
                /* non quoted " Is either first or the last character in
                 * the string */

                *p = *++s; /* skip it */
                (*length)++;
		/* I'm not sure if this is needed in libdns... MG */
                if ( *s == '\0' ) {
                    /* ok, it was the last one */
                    *p  = '\0'; 
		    return LDNS_STATUS_OK;
                }
                break;
            default:
                *p = *s;
                (*length)++;
                break;
        }
    }
    *p = '\0';
    return LDNS_STATUS_OK;
}

int
ldns_rdf_compare(const ldns_rdf *rd1, const ldns_rdf *rd2)
{
	uint16_t i1, i2, i;
	uint8_t *d1, *d2;

	/* only when both are not NULL we can say anything about them */
	if (!rd1 && !rd2) {
		return 0;
	}
	if (!rd1 || !rd2) {
		return -1;
	}
	i1 = ldns_rdf_size(rd1);
	i2 = ldns_rdf_size(rd2);

	if (i1 < i2) {
		return -1;
	} else if (i1 > i2) {
		return +1;
	} else {
		d1 = (uint8_t*)ldns_rdf_data(rd1);
		d2 = (uint8_t*)ldns_rdf_data(rd2);
		for(i = 0; i < i1; i++) {
			if (d1[i] < d2[i]) {
				return -1;
			} else if (d1[i] > d2[i]) {
				return +1;
			}
		}
	}
	return 0;
}

uint32_t
ldns_str2period(const char *nptr, const char **endptr)
{
	int sign = 0;
	uint32_t i = 0;
	uint32_t seconds = 0;

	for(*endptr = nptr; **endptr; (*endptr)++) {
		switch (**endptr) {
			case ' ':
			case '\t':
				break;
			case '-':
				if(sign == 0) {
					sign = -1;
				} else {
					return seconds;
				}
				break;
			case '+':
				if(sign == 0) {
					sign = 1;
				} else {
					return seconds;
				}
				break;
			case 's':
			case 'S':
				seconds += i;
				i = 0;
				break;
			case 'm':
			case 'M':
				seconds += i * 60;
				i = 0;
				break;
			case 'h':
			case 'H':
				seconds += i * 60 * 60;
				i = 0;
				break;
			case 'd':
			case 'D':
				seconds += i * 60 * 60 * 24;
				i = 0;
				break;
			case 'w':
			case 'W':
				seconds += i * 60 * 60 * 24 * 7;
				i = 0;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				i *= 10;
				i += (**endptr - '0');
				break;
			default:
				seconds += i;
				/* disregard signedness */
				return seconds;
		}
	}
	seconds += i;
	/* disregard signedness */
	return seconds;
}
