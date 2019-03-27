/*
 * host2wire.h - 2wire conversion routines
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
 * Contains all functions to translate the main structures to wire format
 */

#ifndef LDNS_HOST2WIRE_H
#define LDNS_HOST2WIRE_H

#include <ldns/common.h>
#include <ldns/error.h>
#include <ldns/rr.h>
#include <ldns/rdata.h>
#include <ldns/packet.h>
#include <ldns/buffer.h>
#include <ctype.h>

#include "ldns/util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copies the dname data to the buffer in wire format
 * \param[out] *buffer buffer to append the result to
 * \param[in] *name rdata dname to convert
 * \return ldns_status
 */
ldns_status ldns_dname2buffer_wire(ldns_buffer *buffer, const ldns_rdf *name);

/**
 * Copies the dname data to the buffer in wire format
 * \param[out] *buffer buffer to append the result to
 * \param[in] *name rdata dname to convert
 * \param[out] *compression_data data structure holding state for compression
 * \return ldns_status
 */
ldns_status ldns_dname2buffer_wire_compress(ldns_buffer *buffer, const ldns_rdf *name, ldns_rbtree_t *compression_data);

/**
 * Copies the rdata data to the buffer in wire format
 * \param[out] *output buffer to append the result to
 * \param[in] *rdf rdata to convert
 * \return ldns_status
 */
ldns_status ldns_rdf2buffer_wire(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Copies the rdata data to the buffer in wire format
 * \param[out] *output buffer to append the result to
 * \param[in] *rdf rdata to convert
 * \param[out] *compression_data data structure holding state for compression
 * \return ldns_status
 */
ldns_status ldns_rdf2buffer_wire_compress(ldns_buffer *output, const ldns_rdf *rdf, ldns_rbtree_t *compression_data);

/**
 * Copies the rdata data to the buffer in wire format
 * If the rdata is a dname, the letters will be lowercased
 * during the conversion
 * \param[out] *output buffer to append the result to
 * \param[in] *rdf rdata to convert
 * \return ldns_status
 */
ldns_status ldns_rdf2buffer_wire_canonical(ldns_buffer *output,
								   const ldns_rdf *rdf);

/**
 * Copies the rr data to the buffer in wire format
 * \param[out] *output buffer to append the result to
 * \param[in] *rr resource record to convert
 * \param[in] section the section in the packet this rr is supposed to be in
 *            (to determine whether to add rdata or not)
 * \return ldns_status
 */
ldns_status ldns_rr2buffer_wire(ldns_buffer *output,
						  const ldns_rr *rr,
						  int section);

/**
 * Copies the rr data to the buffer in wire format while doing DNAME compression
 * \param[out] *output buffer to append the result to
 * \param[in] *rr resource record to convert
 * \param[in] section the section in the packet this rr is supposed to be in
 *            (to determine whether to add rdata or not)
 * \param[out] *compression_data data structure holding state information for compression
 * \return ldns_status
 */
ldns_status ldns_rr2buffer_wire_compress(ldns_buffer *output,
						  const ldns_rr *rr,
						  int section,
						  ldns_rbtree_t *compression_data);

/**
 * Copies the rr data to the buffer in wire format, in canonical format
 * according to RFC3597 (every dname in rdata fields of RR's mentioned in
 * that RFC will be lowercased)
 * \param[out] *output buffer to append the result to
 * \param[in] *rr resource record to convert
 * \param[in] section the section in the packet this rr is supposed to be in
 *            (to determine whether to add rdata or not)
 * \return ldns_status
 */
ldns_status ldns_rr2buffer_wire_canonical(ldns_buffer *output,
								  const ldns_rr *rr,
								  int section);


/**
 * Converts a rrsig to wireformat BUT EXCLUDE the rrsig rdata
 * This is needed in DNSSEC verification
 * \param[out] output buffer to append the result to
 * \param[in] sigrr signature rr to operate on
 * \return ldns_status
 */
ldns_status ldns_rrsig2buffer_wire(ldns_buffer *output, const ldns_rr *sigrr);

/**
 * Converts an rr's rdata to wireformat, while excluding
 * the ownername and all the stuff before the rdata.
 * This is needed in DNSSEC keytag calculation, the ds
 * calcalution from the key and maybe elsewhere.
 *
 * \param[out] *output buffer where to put the result
 * \param[in] *rr rr to operate on
 * \return ldns_status
 */
ldns_status ldns_rr_rdata2buffer_wire(ldns_buffer *output, const ldns_rr *rr);

/**
 * Copies the packet data to the buffer in wire format
 * \param[out] *output buffer to append the result to
 * \param[in] *pkt packet to convert
 * \return ldns_status
 */
ldns_status ldns_pkt2buffer_wire(ldns_buffer *output, const ldns_pkt *pkt);

/**
 * Copies the rr_list data to the buffer in wire format
 * \param[out] *output buffer to append the result to
 * \param[in] *rrlist rr_list to to convert
 * \return ldns_status
 */
ldns_status ldns_rr_list2buffer_wire(ldns_buffer *output, const ldns_rr_list *rrlist);

/**
 * Allocates an array of uint8_t at dest, and puts the wireformat of the
 * given rdf in that array. The result_size value contains the
 * length of the array, if it succeeds, and 0 otherwise (in which case
 * the function also returns NULL)
 *
 * \param[out] dest pointer to the array of bytes to be created
 * \param[in] rdf the rdata field to convert
 * \param[out] size the size of the converted result
 */
ldns_status ldns_rdf2wire(uint8_t **dest, const ldns_rdf *rdf, size_t *size);

/**
 * Allocates an array of uint8_t at dest, and puts the wireformat of the
 * given rr in that array. The result_size value contains the
 * length of the array, if it succeeds, and 0 otherwise (in which case
 * the function also returns NULL)
 *
 * If the section argument is LDNS_SECTION_QUESTION, data like ttl and rdata
 * are not put into the result
 *
 * \param[out] dest pointer to the array of bytes to be created
 * \param[in] rr the rr to convert
 * \param[in] section the rr section, determines how the rr is written.
 * \param[out] size the size of the converted result
 */
ldns_status ldns_rr2wire(uint8_t **dest, const ldns_rr *rr, int section, size_t *size);

/**
 * Allocates an array of uint8_t at dest, and puts the wireformat of the
 * given packet in that array. The result_size value contains the
 * length of the array, if it succeeds, and 0 otherwise (in which case
 * the function also returns NULL)
 */
ldns_status ldns_pkt2wire(uint8_t **dest, const ldns_pkt *p, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_HOST2WIRE_H */
