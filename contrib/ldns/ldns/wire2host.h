/*
 * wire2host.h - from wire conversion routines
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
 * Contains functions that translate dns data from the wire format (as sent
 * by servers and clients) to the internal structures.
 */
 
#ifndef LDNS_WIRE2HOST_H
#define LDNS_WIRE2HOST_H

#include <ldns/rdata.h>
#include <ldns/common.h>
#include <ldns/error.h>
#include <ldns/rr.h>
#include <ldns/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The length of the header */
#define	LDNS_HEADER_SIZE	12

/* First octet of flags */
#define	LDNS_RD_MASK		0x01U
#define	LDNS_RD_SHIFT	0
#define	LDNS_RD_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_RD_MASK)
#define	LDNS_RD_SET(wirebuf)	(*(wirebuf+2) |= LDNS_RD_MASK)
#define	LDNS_RD_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_RD_MASK)

#define LDNS_TC_MASK		0x02U
#define LDNS_TC_SHIFT	1
#define	LDNS_TC_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_TC_MASK)
#define	LDNS_TC_SET(wirebuf)	(*(wirebuf+2) |= LDNS_TC_MASK)
#define	LDNS_TC_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_TC_MASK)

#define	LDNS_AA_MASK		0x04U
#define	LDNS_AA_SHIFT	2
#define	LDNS_AA_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_AA_MASK)
#define	LDNS_AA_SET(wirebuf)	(*(wirebuf+2) |= LDNS_AA_MASK)
#define	LDNS_AA_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_AA_MASK)

#define	LDNS_OPCODE_MASK	0x78U
#define	LDNS_OPCODE_SHIFT	3
#define	LDNS_OPCODE_WIRE(wirebuf)	((*(wirebuf+2) & LDNS_OPCODE_MASK) >> LDNS_OPCODE_SHIFT)
#define	LDNS_OPCODE_SET(wirebuf, opcode) \
	(*(wirebuf+2) = ((*(wirebuf+2)) & ~LDNS_OPCODE_MASK) | ((opcode) << LDNS_OPCODE_SHIFT))

#define	LDNS_QR_MASK		0x80U
#define	LDNS_QR_SHIFT	7
#define	LDNS_QR_WIRE(wirebuf)	(*(wirebuf+2) & LDNS_QR_MASK)
#define	LDNS_QR_SET(wirebuf)	(*(wirebuf+2) |= LDNS_QR_MASK)
#define	LDNS_QR_CLR(wirebuf)	(*(wirebuf+2) &= ~LDNS_QR_MASK)

/* Second octet of flags */
#define	LDNS_RCODE_MASK	0x0fU
#define	LDNS_RCODE_SHIFT	0
#define	LDNS_RCODE_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_RCODE_MASK)
#define	LDNS_RCODE_SET(wirebuf, rcode) \
	(*(wirebuf+3) = ((*(wirebuf+3)) & ~LDNS_RCODE_MASK) | (rcode))

#define	LDNS_CD_MASK		0x10U
#define	LDNS_CD_SHIFT	4
#define	LDNS_CD_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_CD_MASK)
#define	LDNS_CD_SET(wirebuf)	(*(wirebuf+3) |= LDNS_CD_MASK)
#define	LDNS_CD_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_CD_MASK)

#define	LDNS_AD_MASK		0x20U
#define	LDNS_AD_SHIFT	5
#define	LDNS_AD_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_AD_MASK)
#define	LDNS_AD_SET(wirebuf)	(*(wirebuf+3) |= LDNS_AD_MASK)
#define	LDNS_AD_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_AD_MASK)

#define	LDNS_Z_MASK		0x40U
#define	LDNS_Z_SHIFT		6
#define	LDNS_Z_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_Z_MASK)
#define	LDNS_Z_SET(wirebuf)	(*(wirebuf+3) |= LDNS_Z_MASK)
#define	LDNS_Z_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_Z_MASK)

#define	LDNS_RA_MASK		0x80U
#define	LDNS_RA_SHIFT	7
#define	LDNS_RA_WIRE(wirebuf)	(*(wirebuf+3) & LDNS_RA_MASK)
#define	LDNS_RA_SET(wirebuf)	(*(wirebuf+3) |= LDNS_RA_MASK)
#define	LDNS_RA_CLR(wirebuf)	(*(wirebuf+3) &= ~LDNS_RA_MASK)

/* Query ID */
#define	LDNS_ID_WIRE(wirebuf)		(ldns_read_uint16(wirebuf))
#define	LDNS_ID_SET(wirebuf, id)	(ldns_write_uint16(wirebuf, id))

/* Counter of the question section */
#define LDNS_QDCOUNT_OFF		4
#define	LDNS_QDCOUNT(wirebuf)		(ldns_read_uint16(wirebuf+LDNS_QDCOUNT_OFF))

/* Counter of the answer section */
#define LDNS_ANCOUNT_OFF		6
#define	LDNS_ANCOUNT(wirebuf)		(ldns_read_uint16(wirebuf+LDNS_ANCOUNT_OFF))

/* Counter of the authority section */
#define LDNS_NSCOUNT_OFF		8
#define	LDNS_NSCOUNT(wirebuf)		(ldns_read_uint16(wirebuf+LDNS_NSCOUNT_OFF))

/* Counter of the additional section */
#define LDNS_ARCOUNT_OFF		10
#define	LDNS_ARCOUNT(wirebuf)		(ldns_read_uint16(wirebuf+LDNS_ARCOUNT_OFF))

/**
 * converts the data on the uint8_t bytearray (in wire format) to a DNS packet.
 * This function will initialize and allocate memory space for the packet 
 * structure.
 * 
 * \param[in] packet pointer to the structure to hold the packet
 * \param[in] data pointer to the buffer with the data
 * \param[in] len the length of the data buffer (in bytes)
 * \return LDNS_STATUS_OK if everything succeeds, error otherwise
 */
ldns_status ldns_wire2pkt(ldns_pkt **packet, const uint8_t *data, size_t len);

/**
 * converts the data in the ldns_buffer (in wire format) to a DNS packet.
 * This function will initialize and allocate memory space for the packet 
 * structure.
 * 
 * \param[in] packet pointer to the structure to hold the packet
 * \param[in] buffer the buffer with the data
 * \return LDNS_STATUS_OK if everything succeeds, error otherwise
 */
ldns_status ldns_buffer2pkt_wire(ldns_pkt **packet, const ldns_buffer *buffer);

/**
 * converts the data on the uint8_t bytearray (in wire format) to a DNS 
 * dname rdata field. This function will initialize and allocate memory
 * space for the dname structure. The length of the wiredata of this rdf 
 * is added to the *pos value.
 *
 * \param[in] dname pointer to the structure to hold the rdata value
 * \param[in] wire pointer to the buffer with the data
 * \param[in] max the length of the data buffer (in bytes)
 * \param[in] pos the position of the rdf in the buffer (ie. the number of bytes 
 *            from the start of the buffer)
 * \return LDNS_STATUS_OK if everything succeeds, error otherwise
 */
ldns_status ldns_wire2dname(ldns_rdf **dname, const uint8_t *wire, size_t max, size_t *pos);

/**
 * converts the data on the uint8_t bytearray (in wire format) to DNS 
 * rdata fields, and adds them to the list of rdfs of the given rr.
 * This function will initialize and allocate memory space for the dname
 * structures.
 * The length of the wiredata of these rdfs is added to the *pos value.
 *
 * All rdfs belonging to the RR are read; the rr should have no rdfs
 * yet. An error is returned if the format cannot be parsed.
 *
 * \param[in] rr pointer to the ldns_rr structure to hold the rdata value
 * \param[in] wire pointer to the buffer with the data
 * \param[in] max the length of the data buffer (in bytes)
 * \param[in] pos the position of the rdf in the buffer (ie. the number of bytes 
 *            from the start of the buffer)
 * \return LDNS_STATUS_OK if everything succeeds, error otherwise
 */
ldns_status ldns_wire2rdf(ldns_rr *rr, const uint8_t *wire, size_t max, size_t *pos);

/**
 * converts the data on the uint8_t bytearray (in wire format) to a DNS 
 * resource record.
 * This function will initialize and allocate memory space for the rr
 * structure.
 * The length of the wiredata of this rr is added to the *pos value.
 * 
 * \param[in] rr pointer to the structure to hold the rdata value
 * \param[in] wire pointer to the buffer with the data
 * \param[in] max the length of the data buffer (in bytes)
 * \param[in] pos the position of the rr in the buffer (ie. the number of bytes 
 *            from the start of the buffer)
 * \param[in] section the section in the packet the rr is meant for
 * \return LDNS_STATUS_OK if everything succeeds, error otherwise
 */
ldns_status ldns_wire2rr(ldns_rr **rr, const uint8_t *wire, size_t max, size_t *pos, ldns_pkt_section section);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_WIRE2HOST_H */
