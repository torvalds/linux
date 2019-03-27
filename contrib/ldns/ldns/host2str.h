/**
 * host2str.h -  txt presentation of RRs
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
 * Contains functions to translate the main structures to their text
 * representation, as well as functions to print them.
 */

#ifndef LDNS_HOST2STR_H
#define LDNS_HOST2STR_H

#include <ldns/common.h>
#include <ldns/error.h>
#include <ldns/rr.h>
#include <ldns/rdata.h>
#include <ldns/packet.h>
#include <ldns/buffer.h>
#include <ldns/resolver.h>
#include <ldns/zone.h>
#include <ctype.h>

#include "ldns/util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_APL_IP4            1
#define LDNS_APL_IP6            2
#define LDNS_APL_MASK           0x7f
#define LDNS_APL_NEGATION       0x80

/** 
 * Represent a NULL pointer (instead of a pointer to a ldns_rr as "; (null)" 
 * as opposed to outputting nothing at all in such a case.
 */
/*	Flag Name			Flag Nr.	Has data associated
	---------------------------------------------------------------------*/
#define LDNS_COMMENT_NULLS		(1 <<  0)
/** Show key id with DNSKEY RR's as comment */
#define LDNS_COMMENT_KEY_ID		(1 <<  1)
/** Show if a DNSKEY is a ZSK or KSK as comment */
#define LDNS_COMMENT_KEY_TYPE		(1 <<  2)
/** Show DNSKEY key size as comment */
#define LDNS_COMMENT_KEY_SIZE		(1 <<  3)
/** Provide bubblebabble representation for DS RR's as comment */
#define LDNS_COMMENT_BUBBLEBABBLE	(1 <<  4)
/** Show when a NSEC3 RR has the optout flag set as comment */
#define LDNS_COMMENT_FLAGS		(1 <<  5)
/** Show the unhashed owner and next owner names for NSEC3 RR's as comment */
#define LDNS_COMMENT_NSEC3_CHAIN	(1 <<  6)	/* yes */
/** Print mark up */
#define LDNS_COMMENT_LAYOUT		(1 <<  7)
/** Also comment KEY_ID with RRSIGS **/
#define LDNS_COMMENT_RRSIGS		(1 <<  8)
#define LDNS_FMT_ZEROIZE_RRSIGS		(1 <<  9)
#define LDNS_FMT_PAD_SOA_SERIAL		(1 << 10)
#define LDNS_FMT_RFC3597		(1 << 11)	/* yes */

#define LDNS_FMT_FLAGS_WITH_DATA			    2

/** Show key id, type and size as comment for DNSKEY RR's */
#define LDNS_COMMENT_KEY		(LDNS_COMMENT_KEY_ID  \
					|LDNS_COMMENT_KEY_TYPE\
					|LDNS_COMMENT_KEY_SIZE)

/**
 * Output format specifier
 *
 * Determines how Packets, Resource Records and Resource record data fiels are
 * formatted when printing or converting to string.
 * Currently it is only used to specify what aspects of a Resource Record are
 * annotated in the comment section of the textual representation the record.
 * This is speciefed with flags and potential exra data (such as for example
 * a lookup map of hashes to real names for annotation NSEC3 records).
 */
struct ldns_struct_output_format
{
	/** Specification of how RR's should be formatted in text */
	int   flags;
	/** Potential extra data to be used with formatting RR's in text */
	void *data;
};
typedef struct ldns_struct_output_format ldns_output_format;

/**
 * Output format struct with additional data for flags that use them.
 * This struct may not be initialized directly. Use ldns_output_format_init
 * to initialize.
 */
struct ldns_struct_output_format_storage
{	int   flags;
	ldns_rbtree_t* hashmap;    /* for LDNS_COMMENT_NSEC3_CHAIN */
	ldns_rdf*      bitmap;     /* for LDNS_FMT_RFC3597     */
};
typedef struct ldns_struct_output_format_storage ldns_output_format_storage;

/**
 * Standard output format record that disables commenting in the textual 
 * representation of Resource Records completely.
 */
extern const ldns_output_format *ldns_output_format_nocomments;
/**
 * Standard output format record that annotated only DNSKEY RR's with commenti
 * text.
 */
extern const ldns_output_format *ldns_output_format_onlykeyids;
/**
 * The default output format record. Same as ldns_output_format_onlykeyids.
 */
extern const ldns_output_format *ldns_output_format_default;
/**
 * Standard output format record that shows all DNSKEY related information in
 * the comment text, plus the optout flag when set with NSEC3's, plus the
 * bubblebabble representation of DS RR's.
 */
extern const ldns_output_format *ldns_output_format_bubblebabble;

/**
 * Initialize output format storage to the default value.
 * \param[in] fmt A reference to an output_format_ storage struct
 * \return The initialized storage struct typecasted to ldns_output_format
 */
INLINE
ldns_output_format* ldns_output_format_init(ldns_output_format_storage* fmt) {
	fmt->flags   = ldns_output_format_default->flags;
	fmt->hashmap = NULL;
	fmt->bitmap  = NULL;
	return (ldns_output_format*)fmt;
}

/**
 * Set an output format flag.
 */
INLINE void ldns_output_format_set(ldns_output_format* fmt, int flag) {
        fmt->flags |= flag;
}

/**
 * Clear an output format flag.
 */
INLINE void ldns_output_format_clear(ldns_output_format* fmt, int flag) {
        fmt->flags &= !flag;
}

/**
 * Makes sure the LDNS_FMT_RFC3597 is set in the output format.
 * Marks the type to be printed in RFC3597 format.
 * /param[in] fmt the output format to update
 * /param[in] the type to be printed in RFC3597 format
 * /return LDNS_STATUS_OK on success
 */
ldns_status
ldns_output_format_set_type(ldns_output_format* fmt, ldns_rr_type type);

/**
 * Makes sure the LDNS_FMT_RFC3597 is set in the output format.
 * Marks the type to not be printed in RFC3597 format. When no other types
 * have been marked before, all known types (except the given one) will be
 * marked for printing in RFC3597 format.
 * /param[in] fmt the output format to update
 * /param[in] the type not to be printed in RFC3597 format
 * /return LDNS_STATUS_OK on success
 */
ldns_status
ldns_output_format_clear_type(ldns_output_format* fmt, ldns_rr_type type);

/**
 * Converts an ldns packet opcode value to its mnemonic, and adds that
 * to the output buffer
 * \param[in] *output the buffer to add the data to
 * \param[in] opcode to find the string representation of
 * \return LDNS_STATUS_OK on success, or a buffer failure mode on error
 */
ldns_status
ldns_pkt_opcode2buffer_str(ldns_buffer *output, ldns_pkt_opcode opcode);

/**
 * Converts an ldns packet rcode value to its mnemonic, and adds that
 * to the output buffer
 * \param[in] *output the buffer to add the data to
 * \param[in] rcode to find the string representation of
 * \return LDNS_STATUS_OK on success, or a buffer failure mode on error
 */
ldns_status
ldns_pkt_rcode2buffer_str(ldns_buffer *output, ldns_pkt_rcode rcode);

/**
 * Converts an ldns algorithm type to its mnemonic, and adds that
 * to the output buffer
 * \param[in] *output the buffer to add the data to
 * \param[in] algorithm to find the string representation of
 * \return LDNS_STATUS_OK on success, or a buffer failure mode on error
 */
ldns_status
ldns_algorithm2buffer_str(ldns_buffer *output,
                          ldns_algorithm algorithm);

/**
 * Converts an ldns certificate algorithm type to its mnemonic, 
 * and adds that to the output buffer
 * \param[in] *output the buffer to add the data to
 * \param[in] cert_algorithm to find the string representation of
 * \return LDNS_STATUS_OK on success, or a buffer failure mode on error
 */
ldns_status
ldns_cert_algorithm2buffer_str(ldns_buffer *output,
                               ldns_cert_algorithm cert_algorithm);


/**
 * Converts a packet opcode to its mnemonic and returns that as
 * an allocated null-terminated string.
 * Remember to free it.
 *
 * \param[in] opcode the opcode to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt_opcode2str(ldns_pkt_opcode opcode);

/**
 * Converts a packet rcode to its mnemonic and returns that as
 * an allocated null-terminated string.
 * Remember to free it.
 *
 * \param[in] rcode the rcode to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt_rcode2str(ldns_pkt_rcode rcode);

/**
 * Converts a signing algorithms to its mnemonic and returns that as
 * an allocated null-terminated string.
 * Remember to free it.
 *
 * \param[in] algorithm the algorithm to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt_algorithm2str(ldns_algorithm algorithm);

/**
 * Converts a cert algorithm to its mnemonic and returns that as
 * an allocated null-terminated string.
 * Remember to free it.
 *
 * \param[in] cert_algorithm to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt_cert_algorithm2str(ldns_cert_algorithm cert_algorithm);

/** 
 * Converts an LDNS_RDF_TYPE_A rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_a(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_AAAA rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_aaaa(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_STR rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_str(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_B64 rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_b64(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_B32_EXT rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_b32_ext(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_HEX rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_hex(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_TYPE rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_type(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_CLASS rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_class(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_ALG rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_alg(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts an ldns_rr_type value to its string representation,
 * and places it in the given buffer
 * \param[in] *output The buffer to add the data to
 * \param[in] type the ldns_rr_type to convert
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rr_type2buffer_str(ldns_buffer *output,
                                    const ldns_rr_type type);

/**
 * Converts an ldns_rr_type value to its string representation,
 * and returns that string. For unknown types, the string
 * "TYPE<id>" is returned. This function allocates data that must be
 * freed by the caller
 * \param[in] type the ldns_rr_type to convert
 * \return a newly allocated string
 */
char *ldns_rr_type2str(const ldns_rr_type type);

/**
 * Converts an ldns_rr_class value to its string representation,
 * and places it in the given buffer
 * \param[in] *output The buffer to add the data to
 * \param[in] klass the ldns_rr_class to convert
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rr_class2buffer_str(ldns_buffer *output,
                                     const ldns_rr_class klass);

/**
 * Converts an ldns_rr_class value to its string representation,
 * and returns that string. For unknown types, the string
 * "CLASS<id>" is returned. This function allocates data that must be
 * freed by the caller
 * \param[in] klass the ldns_rr_class to convert
 * \return a newly allocated string
 */
char *ldns_rr_class2str(const ldns_rr_class klass);


/** 
 * Converts an LDNS_RDF_TYPE_CERT rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_cert_alg(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_LOC rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_loc(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_UNKNOWN rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_unknown(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_NSAP rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_nsap(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_ATMA rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_atma(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_WKS rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_wks(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_NSEC rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_nsec(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_PERIOD rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_period(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_TSIGTIME rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_tsigtime(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_APL rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_apl(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_INT16_DATA rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_int16_data(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_IPSECKEY rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_ipseckey(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts the data in the rdata field to presentation
 * format (as char *) and appends it to the given buffer
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] rdf the pointer to the rdafa field containing the data
 * \return status
 */
ldns_status ldns_rdf2buffer_str(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts the data in the resource record to presentation
 * format (as char *) and appends it to the given buffer.
 * The presentation format of DNSKEY record is annotated with comments giving
 * the id, type and size of the key.
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] rr the pointer to the rr field to convert
 * \return status
 */
ldns_status ldns_rr2buffer_str(ldns_buffer *output, const ldns_rr *rr);

/**
 * Converts the data in the resource record to presentation
 * format (as char *) and appends it to the given buffer.
 * The presentation format is annotated with comments giving
 * additional information on the record.
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] fmt how to format the textual representation of the 
 *            resource record.
 * \param[in] rr the pointer to the rr field to convert
 * \return status
 */
ldns_status ldns_rr2buffer_str_fmt(ldns_buffer *output, 
		const ldns_output_format *fmt, const ldns_rr *rr);

/**
 * Converts the data in the DNS packet to presentation
 * format (as char *) and appends it to the given buffer
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] pkt the pointer to the packet to convert
 * \return status
 */
ldns_status ldns_pkt2buffer_str(ldns_buffer *output, const ldns_pkt *pkt);

/**
 * Converts the data in the DNS packet to presentation
 * format (as char *) and appends it to the given buffer
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] fmt how to format the textual representation of the packet
 * \param[in] pkt the pointer to the packet to convert
 * \return status
 */
ldns_status ldns_pkt2buffer_str_fmt(ldns_buffer *output,
		const ldns_output_format *fmt, const ldns_pkt *pkt);

/** 
 * Converts an LDNS_RDF_TYPE_NSEC3_SALT rdata element to string format and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_nsec3_salt(ldns_buffer *output, const ldns_rdf *rdf);


/**
 * Converts the data in the DNS packet to presentation
 * format (as char *) and appends it to the given buffer
 *
 * \param[in] output pointer to the buffer to append the data to
 * \param[in] k the pointer to the private key to convert
 * \return status
 */
ldns_status ldns_key2buffer_str(ldns_buffer *output, const ldns_key *k);

/**
 * Converts an LDNS_RDF_TYPE_INT8 rdata element to string format and adds it to the output buffer
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_int8(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts an LDNS_RDF_TYPE_INT16 rdata element to string format and adds it to the output buffer
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_int16(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts an LDNS_RDF_TYPE_INT32 rdata element to string format and adds it to the output buffer
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_int32(ldns_buffer *output, const ldns_rdf *rdf);

/**
 * Converts an LDNS_RDF_TYPE_TIME rdata element to string format and adds it to the output buffer
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_time(ldns_buffer *output, const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_ILNP64 rdata element to 4 hexadecimal numbers
 * separated by colons and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_ilnp64(ldns_buffer *output,
		const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_EUI48 rdata element to 6 hexadecimal numbers
 * separated by dashes and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_eui48(ldns_buffer *output,
		const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_EUI64 rdata element to 8 hexadecimal numbers
 * separated by dashes and adds it to the output buffer 
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_eui64(ldns_buffer *output,
		const ldns_rdf *rdf);

/** 
 * Adds the LDNS_RDF_TYPE_TAG rdata to the output buffer,
 * provided it contains only alphanumeric characters.
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_tag(ldns_buffer *output,
		const ldns_rdf *rdf);

/** 
 * Adds the LDNS_RDF_TYPE_LONG_STR rdata to the output buffer, in-between 
 * double quotes and all non printable characters properly escaped.
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_long_str(ldns_buffer *output,
	       	const ldns_rdf *rdf);

/** 
 * Converts an LDNS_RDF_TYPE_HIP rdata element to presentation format for
 * the algorithm, HIT and Public Key and adds it the output buffer .
 * \param[in] *rdf The rdata to convert
 * \param[in] *output The buffer to add the data to
 * \return LDNS_STATUS_OK on success, and error status on failure
 */
ldns_status ldns_rdf2buffer_str_hip(ldns_buffer *output,
		const ldns_rdf *rdf);

/**
 * Converts the data in the rdata field to presentation format and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] rdf The rdata field to convert
 * \return null terminated char * data, or NULL on error
 */
char *ldns_rdf2str(const ldns_rdf *rdf);

/**
 * Converts the data in the resource record to presentation format and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] rr The rdata field to convert
 * \return null terminated char * data, or NULL on error
 */
char *ldns_rr2str(const ldns_rr *rr);

/**
 * Converts the data in the resource record to presentation format and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] fmt how to format the resource record
 * \param[in] rr The rdata field to convert
 * \return null terminated char * data, or NULL on error
 */
char *ldns_rr2str_fmt(const ldns_output_format *fmt, const ldns_rr *rr);

/**
 * Converts the data in the DNS packet to presentation format and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] pkt The rdata field to convert
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt2str(const ldns_pkt *pkt);

/**
 * Converts the data in the DNS packet to presentation format and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] fmt how to format the packet
 * \param[in] pkt The rdata field to convert
 * \return null terminated char * data, or NULL on error
 */
char *ldns_pkt2str_fmt(const ldns_output_format *fmt, const ldns_pkt *pkt);

/**
 * Converts a private key to the test presentation fmt and
 * returns that as a char *.
 * Remember to free it.
 *
 * \param[in] k the key to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_key2str(const ldns_key *k);

/**
 * Converts a list of resource records to presentation format
 * and returns that as a char *.
 * Remember to free it.
 *
 * \param[in] rr_list the rr_list to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_rr_list2str(const ldns_rr_list *rr_list);

/**
 * Converts a list of resource records to presentation format
 * and returns that as a char *.
 * Remember to free it.
 *
 * \param[in] fmt how to format the list of resource records
 * \param[in] rr_list the rr_list to convert to text
 * \return null terminated char * data, or NULL on error
 */
char *ldns_rr_list2str_fmt(
		const ldns_output_format *fmt, const ldns_rr_list *rr_list);

/**
 * Returns a copy of the data in the buffer as a null terminated
 * char * string. The returned string must be freed by the caller.
 * The buffer must be in write modus and may thus not have been flipped.
 *
 * \param[in] buffer buffer containing char * data
 * \return null terminated char * data, or NULL on error
 */
char *ldns_buffer2str(ldns_buffer *buffer);

/**
 * Exports and returns the data in the buffer as a null terminated
 * char * string. The returned string must be freed by the caller.
 * The buffer must be in write modus and may thus not have been flipped.
 * The buffer is fixed after this function returns.
 *
 * \param[in] buffer buffer containing char * data
 * \return null terminated char * data, or NULL on error
 */
char *ldns_buffer_export2str(ldns_buffer *buffer);

/**
 * Prints the data in the rdata field to the given file stream
 * (in presentation format)
 *
 * \param[in] output the file stream to print to
 * \param[in] rdf the rdata field to print
 * \return void
 */
void ldns_rdf_print(FILE *output, const ldns_rdf *rdf);

/**
 * Prints the data in the resource record to the given file stream
 * (in presentation format)
 *
 * \param[in] output the file stream to print to
 * \param[in] rr the resource record to print
 * \return void
 */
void ldns_rr_print(FILE *output, const ldns_rr *rr);

/**
 * Prints the data in the resource record to the given file stream
 * (in presentation format)
 *
 * \param[in] output the file stream to print to
 * \param[in] fmt format of the textual representation
 * \param[in] rr the resource record to print
 * \return void
 */
void ldns_rr_print_fmt(FILE *output, 
		const ldns_output_format *fmt, const ldns_rr *rr);

/**
 * Prints the data in the DNS packet to the given file stream
 * (in presentation format)
 *
 * \param[in] output the file stream to print to
 * \param[in] pkt the packet to print
 * \return void
 */
void ldns_pkt_print(FILE *output, const ldns_pkt *pkt);

/**
 * Prints the data in the DNS packet to the given file stream
 * (in presentation format)
 *
 * \param[in] output the file stream to print to
 * \param[in] fmt format of the textual representation
 * \param[in] pkt the packet to print
 * \return void
 */
void ldns_pkt_print_fmt(FILE *output, 
		const ldns_output_format *fmt, const ldns_pkt *pkt);

/**
 * Converts a rr_list to presentation format and appends it to
 * the output buffer
 * \param[in] output the buffer to append output to
 * \param[in] list the ldns_rr_list to print
 * \return ldns_status
 */
ldns_status ldns_rr_list2buffer_str(ldns_buffer *output, const ldns_rr_list *list);

/**
 * Converts a rr_list to presentation format and appends it to
 * the output buffer
 * \param[in] output the buffer to append output to
 * \param[in] fmt format of the textual representation
 * \param[in] list the ldns_rr_list to print
 * \return ldns_status
 */
ldns_status ldns_rr_list2buffer_str_fmt(ldns_buffer *output, 
		const ldns_output_format *fmt, const ldns_rr_list *list);

/**
 * Converts the header of a packet to presentation format and appends it to
 * the output buffer
 * \param[in] output the buffer to append output to
 * \param[in] pkt the packet to convert the header of
 * \return ldns_status
 */
ldns_status ldns_pktheader2buffer_str(ldns_buffer *output, const ldns_pkt *pkt);

/**
 * print a rr_list to output
 * \param[in] output the fd to print to
 * \param[in] list the rr_list to print
 */
void ldns_rr_list_print(FILE *output, const ldns_rr_list *list);

/**
 * print a rr_list to output
 * \param[in] output the fd to print to
 * \param[in] fmt format of the textual representation
 * \param[in] list the rr_list to print
 */
void ldns_rr_list_print_fmt(FILE *output, 
		const ldns_output_format *fmt, const ldns_rr_list *list);

/**
 * Print a resolver (in sofar that is possible) state
 * to output.
 * \param[in] output the fd to print to
 * \param[in] r the resolver to print
 */
void ldns_resolver_print(FILE *output, const ldns_resolver *r);

/**
 * Print a resolver (in sofar that is possible) state
 * to output.
 * \param[in] output the fd to print to
 * \param[in] fmt format of the textual representation
 * \param[in] r the resolver to print
 */
void ldns_resolver_print_fmt(FILE *output, 
		const ldns_output_format *fmt, const ldns_resolver *r);

/**
 * Print a zone structure * to output. Note the SOA record
 * is included in this output
 * \param[in] output the fd to print to
 * \param[in] z the zone to print
 */
void ldns_zone_print(FILE *output, const ldns_zone *z);

/**
 * Print a zone structure * to output. Note the SOA record
 * is included in this output
 * \param[in] output the fd to print to
 * \param[in] fmt format of the textual representation
 * \param[in] z the zone to print
 */
void ldns_zone_print_fmt(FILE *output, 
		const ldns_output_format *fmt, const ldns_zone *z);

/**
 * Print the ldns_rdf containing a dname to the buffer
 * \param[in] output the buffer to print to
 * \param[in] dname the dname to print
 * \return ldns_status message if the printing succeeded
 */
ldns_status ldns_rdf2buffer_str_dname(ldns_buffer *output, const ldns_rdf *dname);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_HOST2STR_H */
