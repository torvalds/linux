/*
 * wire2host.c
 *
 * conversion routines from the wire to the host
 * format.
 * This will usually just a re-ordering of the
 * data (as we store it in network format)
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */


#include <ldns/config.h>

#include <ldns/ldns.h>
/*#include <ldns/wire2host.h>*/

#include <strings.h>
#include <limits.h>



/*
 * Set of macro's to deal with the dns message header as specified
 * in RFC1035 in portable way.
 *
 */

/*
 *
 *                                    1  1  1  1  1  1
 *      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                      ID                       |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |QR|   Opcode  |AA|TC|RD|RA| Z|AD|CD|   RCODE   |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    QDCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    ANCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    NSCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *    |                    ARCOUNT                    |
 *    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 */


/* allocates memory to *dname! */
ldns_status
ldns_wire2dname(ldns_rdf **dname, const uint8_t *wire, size_t max, size_t *pos)
{
	uint8_t label_size;
	uint16_t pointer_target;
	uint8_t pointer_target_buf[2];
	size_t dname_pos = 0;
	size_t uncompressed_length = 0;
	size_t compression_pos = 0;
	uint8_t tmp_dname[LDNS_MAX_DOMAINLEN];
	unsigned int pointer_count = 0;

	if (pos == NULL) {
		return LDNS_STATUS_WIRE_RDATA_ERR;
	}
	if (*pos >= max) {
		return LDNS_STATUS_PACKET_OVERFLOW;
	}
	label_size = wire[*pos];
	while (label_size > 0) {
		/* compression */
		while (label_size >= 192) {
			if (compression_pos == 0) {
				compression_pos = *pos + 2;
			}

			pointer_count++;

			/* remove first two bits */
			if (*pos + 2 > max) {
				return LDNS_STATUS_PACKET_OVERFLOW;
			}
			pointer_target_buf[0] = wire[*pos] & 63;
			pointer_target_buf[1] = wire[*pos + 1];
			pointer_target = ldns_read_uint16(pointer_target_buf);

			if (pointer_target == 0) {
				return LDNS_STATUS_INVALID_POINTER;
			} else if (pointer_target >= max) {
				return LDNS_STATUS_INVALID_POINTER;
			} else if (pointer_count > LDNS_MAX_POINTERS) {
				return LDNS_STATUS_INVALID_POINTER;
			}
			*pos = pointer_target;
			label_size = wire[*pos];
		}
		if(label_size == 0)
			break; /* break from pointer to 0 byte */
		if (label_size > LDNS_MAX_LABELLEN) {
			return LDNS_STATUS_LABEL_OVERFLOW;
		}
		if (*pos + 1 + label_size > max) {
			return LDNS_STATUS_LABEL_OVERFLOW;
		}

		/* check space for labelcount itself */
		if (dname_pos + 1 > LDNS_MAX_DOMAINLEN) {
			return LDNS_STATUS_DOMAINNAME_OVERFLOW;
		}
		tmp_dname[dname_pos] = label_size;
		if (label_size > 0) {
			dname_pos++;
		}
		*pos = *pos + 1;
		if (dname_pos + label_size > LDNS_MAX_DOMAINLEN) {
			return LDNS_STATUS_DOMAINNAME_OVERFLOW;
		}
		memcpy(&tmp_dname[dname_pos], &wire[*pos], label_size);
		uncompressed_length += label_size + 1;
		dname_pos += label_size;
		*pos = *pos + label_size;

		if (*pos < max) {
			label_size = wire[*pos];
		}
	}

	if (compression_pos > 0) {
		*pos = compression_pos;
	} else {
		*pos = *pos + 1;
	}

	if (dname_pos >= LDNS_MAX_DOMAINLEN) {
		return LDNS_STATUS_DOMAINNAME_OVERFLOW;
	}

	tmp_dname[dname_pos] = 0;
	dname_pos++;

	*dname = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_DNAME,
			(uint16_t) dname_pos, tmp_dname);
	if (!*dname) {
		return LDNS_STATUS_MEM_ERR;
	}
	return LDNS_STATUS_OK;
}

/* maybe make this a goto error so data can be freed or something/ */
#define LDNS_STATUS_CHECK_RETURN(st) {if (st != LDNS_STATUS_OK) { return st; }}
#define LDNS_STATUS_CHECK_GOTO(st, label) {if (st != LDNS_STATUS_OK) { /*printf("STG %s:%d: status code %d\n", __FILE__, __LINE__, st);*/  goto label; }}

ldns_status
ldns_wire2rdf(ldns_rr *rr, const uint8_t *wire, size_t max, size_t *pos)
{
	size_t end;
	size_t cur_rdf_length;
	uint8_t rdf_index;
	uint8_t *data;
	uint16_t rd_length;
	ldns_rdf *cur_rdf = NULL;
	ldns_rdf_type cur_rdf_type;
	const ldns_rr_descriptor *descriptor;
	ldns_status status;

	assert(rr != NULL);

	descriptor = ldns_rr_descript(ldns_rr_get_type(rr));

	if (*pos + 2 > max) {
		return LDNS_STATUS_PACKET_OVERFLOW;
	}

	rd_length = ldns_read_uint16(&wire[*pos]);
	*pos = *pos + 2;

	if (*pos + rd_length > max) {
		return LDNS_STATUS_PACKET_OVERFLOW;
	}

	end = *pos + (size_t) rd_length;

	rdf_index = 0;
	while (*pos < end &&
			rdf_index < ldns_rr_descriptor_maximum(descriptor)) {

		cur_rdf_length = 0;

		cur_rdf_type = ldns_rr_descriptor_field_type(
				descriptor, rdf_index);

		/* handle special cases immediately, set length
		   for fixed length rdata and do them below */
		switch (cur_rdf_type) {
		case LDNS_RDF_TYPE_DNAME:
			status = ldns_wire2dname(&cur_rdf, wire, max, pos);
			LDNS_STATUS_CHECK_RETURN(status);
			break;
		case LDNS_RDF_TYPE_CLASS:
		case LDNS_RDF_TYPE_ALG:
		case LDNS_RDF_TYPE_CERTIFICATE_USAGE:
		case LDNS_RDF_TYPE_SELECTOR:
		case LDNS_RDF_TYPE_MATCHING_TYPE:
		case LDNS_RDF_TYPE_INT8:
			cur_rdf_length = LDNS_RDF_SIZE_BYTE;
			break;
		case LDNS_RDF_TYPE_TYPE:
		case LDNS_RDF_TYPE_INT16:
		case LDNS_RDF_TYPE_CERT_ALG:
			cur_rdf_length = LDNS_RDF_SIZE_WORD;
			break;
		case LDNS_RDF_TYPE_TIME:
		case LDNS_RDF_TYPE_INT32:
		case LDNS_RDF_TYPE_A:
		case LDNS_RDF_TYPE_PERIOD:
			cur_rdf_length = LDNS_RDF_SIZE_DOUBLEWORD;
			break;
		case LDNS_RDF_TYPE_TSIGTIME:
		case LDNS_RDF_TYPE_EUI48:
			cur_rdf_length = LDNS_RDF_SIZE_6BYTES;
			break;
		case LDNS_RDF_TYPE_ILNP64:
		case LDNS_RDF_TYPE_EUI64:
			cur_rdf_length = LDNS_RDF_SIZE_8BYTES;
			break;
		case LDNS_RDF_TYPE_AAAA:
			cur_rdf_length = LDNS_RDF_SIZE_16BYTES;
			break;
		case LDNS_RDF_TYPE_STR:
		case LDNS_RDF_TYPE_NSEC3_SALT:
		case LDNS_RDF_TYPE_TAG:
			/* len is stored in first byte
			 * it should be in the rdf too, so just
			 * copy len+1 from this position
			 */
			cur_rdf_length = ((size_t) wire[*pos]) + 1;
			break;

		case LDNS_RDF_TYPE_INT16_DATA:
			if (*pos + 2 > end) {
				return LDNS_STATUS_PACKET_OVERFLOW;
			}
			cur_rdf_length =
				(size_t) ldns_read_uint16(&wire[*pos]) + 2;
			break;
		case LDNS_RDF_TYPE_HIP:
			if (*pos + 4 > end) {
				return LDNS_STATUS_PACKET_OVERFLOW;
			}
			cur_rdf_length =
				(size_t) wire[*pos] + 
				(size_t) ldns_read_uint16(&wire[*pos + 2]) + 4;
			break;
		case LDNS_RDF_TYPE_B32_EXT:
		case LDNS_RDF_TYPE_NSEC3_NEXT_OWNER:
			/* length is stored in first byte */
			cur_rdf_length = ((size_t) wire[*pos]) + 1;
			break;
		case LDNS_RDF_TYPE_APL:
		case LDNS_RDF_TYPE_B64:
		case LDNS_RDF_TYPE_HEX:
		case LDNS_RDF_TYPE_NSEC:
		case LDNS_RDF_TYPE_UNKNOWN:
		case LDNS_RDF_TYPE_SERVICE:
		case LDNS_RDF_TYPE_LOC:
		case LDNS_RDF_TYPE_WKS:
		case LDNS_RDF_TYPE_NSAP:
		case LDNS_RDF_TYPE_ATMA:
		case LDNS_RDF_TYPE_IPSECKEY:
		case LDNS_RDF_TYPE_LONG_STR:
		case LDNS_RDF_TYPE_NONE:
			/*
			 * Read to end of rr rdata
			 */
			cur_rdf_length = end - *pos;
			break;
		}

		/* fixed length rdata */
		if (cur_rdf_length > 0) {
			if (cur_rdf_length + *pos > end) {
				return LDNS_STATUS_PACKET_OVERFLOW;
			}
			data = LDNS_XMALLOC(uint8_t, rd_length);
			if (!data) {
				return LDNS_STATUS_MEM_ERR;
			}
			memcpy(data, &wire[*pos], cur_rdf_length);

			cur_rdf = ldns_rdf_new(cur_rdf_type,
					cur_rdf_length, data);
			*pos = *pos + cur_rdf_length;
		}

		if (cur_rdf) {
			ldns_rr_push_rdf(rr, cur_rdf);
			cur_rdf = NULL;
		}

		rdf_index++;

	} /* while (rdf_index < ldns_rr_descriptor_maximum(descriptor)) */


	return LDNS_STATUS_OK;
}


/* TODO:
         can *pos be incremented at READ_INT? or maybe use something like
         RR_CLASS(wire)?
	 uhhm Jelte??
*/
ldns_status
ldns_wire2rr(ldns_rr **rr_p, const uint8_t *wire, size_t max,
             size_t *pos, ldns_pkt_section section)
{
	ldns_rdf *owner = NULL;
	ldns_rr *rr = ldns_rr_new();
	ldns_status status;

	status = ldns_wire2dname(&owner, wire, max, pos);
	LDNS_STATUS_CHECK_GOTO(status, status_error);

	ldns_rr_set_owner(rr, owner);

	if (*pos + 4 > max) {
		status = LDNS_STATUS_PACKET_OVERFLOW;
		goto status_error;
	}

	ldns_rr_set_type(rr, ldns_read_uint16(&wire[*pos]));
	*pos = *pos + 2;

	ldns_rr_set_class(rr, ldns_read_uint16(&wire[*pos]));
	*pos = *pos + 2;

	if (section != LDNS_SECTION_QUESTION) {
		if (*pos + 4 > max) {
			status = LDNS_STATUS_PACKET_OVERFLOW;
			goto status_error;
		}
		ldns_rr_set_ttl(rr, ldns_read_uint32(&wire[*pos]));

		*pos = *pos + 4;
		status = ldns_wire2rdf(rr, wire, max, pos);

		LDNS_STATUS_CHECK_GOTO(status, status_error);
        ldns_rr_set_question(rr, false);
	} else {
        ldns_rr_set_question(rr, true);
    }

	*rr_p = rr;
	return LDNS_STATUS_OK;

status_error:
	ldns_rr_free(rr);
	return status;
}

static ldns_status
ldns_wire2pkt_hdr(ldns_pkt *packet, const uint8_t *wire, size_t max, size_t *pos)
{
	if (*pos + LDNS_HEADER_SIZE > max) {
		return LDNS_STATUS_WIRE_INCOMPLETE_HEADER;
	} else {
		ldns_pkt_set_id(packet, LDNS_ID_WIRE(wire));
		ldns_pkt_set_qr(packet, LDNS_QR_WIRE(wire));
		ldns_pkt_set_opcode(packet, LDNS_OPCODE_WIRE(wire));
		ldns_pkt_set_aa(packet, LDNS_AA_WIRE(wire));
		ldns_pkt_set_tc(packet, LDNS_TC_WIRE(wire));
		ldns_pkt_set_rd(packet, LDNS_RD_WIRE(wire));
		ldns_pkt_set_ra(packet, LDNS_RA_WIRE(wire));
		ldns_pkt_set_ad(packet, LDNS_AD_WIRE(wire));
		ldns_pkt_set_cd(packet, LDNS_CD_WIRE(wire));
		ldns_pkt_set_rcode(packet, LDNS_RCODE_WIRE(wire));

		ldns_pkt_set_qdcount(packet, LDNS_QDCOUNT(wire));
		ldns_pkt_set_ancount(packet, LDNS_ANCOUNT(wire));
		ldns_pkt_set_nscount(packet, LDNS_NSCOUNT(wire));
		ldns_pkt_set_arcount(packet, LDNS_ARCOUNT(wire));

		*pos += LDNS_HEADER_SIZE;

		return LDNS_STATUS_OK;
	}
}

ldns_status
ldns_buffer2pkt_wire(ldns_pkt **packet, const ldns_buffer *buffer)
{
	/* lazy */
	return ldns_wire2pkt(packet, ldns_buffer_begin(buffer),
				ldns_buffer_limit(buffer));

}

ldns_status
ldns_wire2pkt(ldns_pkt **packet_p, const uint8_t *wire, size_t max)
{
	size_t pos = 0;
	uint16_t i;
	ldns_rr *rr;
	ldns_pkt *packet = ldns_pkt_new();
	ldns_status status = LDNS_STATUS_OK;
	uint8_t have_edns = 0;

	uint8_t data[4];

	status = ldns_wire2pkt_hdr(packet, wire, max, &pos);
	LDNS_STATUS_CHECK_GOTO(status, status_error);

	for (i = 0; i < ldns_pkt_qdcount(packet); i++) {

		status = ldns_wire2rr(&rr, wire, max, &pos, LDNS_SECTION_QUESTION);
		if (status == LDNS_STATUS_PACKET_OVERFLOW) {
			status = LDNS_STATUS_WIRE_INCOMPLETE_QUESTION;
		}
		LDNS_STATUS_CHECK_GOTO(status, status_error);
		if (!ldns_rr_list_push_rr(ldns_pkt_question(packet), rr)) {
			ldns_pkt_free(packet);
			return LDNS_STATUS_INTERNAL_ERR;
		}
	}
	for (i = 0; i < ldns_pkt_ancount(packet); i++) {
		status = ldns_wire2rr(&rr, wire, max, &pos, LDNS_SECTION_ANSWER);
		if (status == LDNS_STATUS_PACKET_OVERFLOW) {
			status = LDNS_STATUS_WIRE_INCOMPLETE_ANSWER;
		}
		LDNS_STATUS_CHECK_GOTO(status, status_error);
		if (!ldns_rr_list_push_rr(ldns_pkt_answer(packet), rr)) {
			ldns_pkt_free(packet);
			return LDNS_STATUS_INTERNAL_ERR;
		}
	}
	for (i = 0; i < ldns_pkt_nscount(packet); i++) {
		status = ldns_wire2rr(&rr, wire, max, &pos, LDNS_SECTION_AUTHORITY);
		if (status == LDNS_STATUS_PACKET_OVERFLOW) {
			status = LDNS_STATUS_WIRE_INCOMPLETE_AUTHORITY;
		}
		LDNS_STATUS_CHECK_GOTO(status, status_error);
		if (!ldns_rr_list_push_rr(ldns_pkt_authority(packet), rr)) {
			ldns_pkt_free(packet);
			return LDNS_STATUS_INTERNAL_ERR;
		}
	}
	for (i = 0; i < ldns_pkt_arcount(packet); i++) {
		status = ldns_wire2rr(&rr, wire, max, &pos, LDNS_SECTION_ADDITIONAL);
		if (status == LDNS_STATUS_PACKET_OVERFLOW) {
			status = LDNS_STATUS_WIRE_INCOMPLETE_ADDITIONAL;
		}
		LDNS_STATUS_CHECK_GOTO(status, status_error);

		if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_OPT) {
			ldns_pkt_set_edns_udp_size(packet, ldns_rr_get_class(rr));
			ldns_write_uint32(data, ldns_rr_ttl(rr));
			ldns_pkt_set_edns_extended_rcode(packet, data[0]);
			ldns_pkt_set_edns_version(packet, data[1]);
			ldns_pkt_set_edns_z(packet, ldns_read_uint16(&data[2]));
			/* edns might not have rdfs */
			if (ldns_rr_rdf(rr, 0)) {
				ldns_pkt_set_edns_data(packet, ldns_rdf_clone(ldns_rr_rdf(rr, 0)));
			}
			ldns_rr_free(rr);
			have_edns += 1;
		} else if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_TSIG) {
			ldns_pkt_set_tsig(packet, rr);
			ldns_pkt_set_arcount(packet, ldns_pkt_arcount(packet) - 1);
		} else if (!ldns_rr_list_push_rr(ldns_pkt_additional(packet), rr)) {
			ldns_pkt_free(packet);
			return LDNS_STATUS_INTERNAL_ERR;
		}
	}
	ldns_pkt_set_size(packet, max);
	if(have_edns)
		ldns_pkt_set_arcount(packet, ldns_pkt_arcount(packet)
                        - have_edns);
        packet->_edns_present = have_edns;

	*packet_p = packet;
	return status;

status_error:
	ldns_pkt_free(packet);
	return status;
}
