/*
 * rdata.h
 *
 * rdata definitions
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
 * Defines ldns_rdf and functions to manipulate those.
 */


#ifndef LDNS_RDATA_H
#define LDNS_RDATA_H

#include <ldns/common.h>
#include <ldns/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_MAX_RDFLEN	65535

#define LDNS_RDF_SIZE_BYTE              1
#define LDNS_RDF_SIZE_WORD              2
#define LDNS_RDF_SIZE_DOUBLEWORD        4
#define LDNS_RDF_SIZE_6BYTES            6
#define LDNS_RDF_SIZE_8BYTES            8
#define LDNS_RDF_SIZE_16BYTES           16

#define LDNS_NSEC3_VARS_OPTOUT_MASK 0x01

/**
 * The different types of RDATA fields.
 */
enum ldns_enum_rdf_type
{
	/** none */
	LDNS_RDF_TYPE_NONE,
	/** domain name */
	LDNS_RDF_TYPE_DNAME,
	/** 8 bits */
	LDNS_RDF_TYPE_INT8,
	/** 16 bits */
	LDNS_RDF_TYPE_INT16,
	/** 32 bits */
	LDNS_RDF_TYPE_INT32,
	/** A record */
	LDNS_RDF_TYPE_A,
	/** AAAA record */
	LDNS_RDF_TYPE_AAAA,
	/** txt string */
	LDNS_RDF_TYPE_STR,
	/** apl data */
	LDNS_RDF_TYPE_APL,
	/** b32 string */
	LDNS_RDF_TYPE_B32_EXT,
	/** b64 string */
	LDNS_RDF_TYPE_B64,
	/** hex string */
	LDNS_RDF_TYPE_HEX,
	/** nsec type codes */
	LDNS_RDF_TYPE_NSEC,
	/** a RR type */
	LDNS_RDF_TYPE_TYPE,
	/** a class */
	LDNS_RDF_TYPE_CLASS,
	/** certificate algorithm */
	LDNS_RDF_TYPE_CERT_ALG,
	/** a key algorithm */
	LDNS_RDF_TYPE_ALG,
	/** unknown types */
	LDNS_RDF_TYPE_UNKNOWN,
	/** time (32 bits) */
	LDNS_RDF_TYPE_TIME,
	/** period */
	LDNS_RDF_TYPE_PERIOD,
	/** tsig time 48 bits */
	LDNS_RDF_TYPE_TSIGTIME,
	/** Represents the Public Key Algorithm, HIT and Public Key fields
	    for the HIP RR types.  A HIP specific rdf type is used because of
	    the unusual layout in wireformat (see RFC 5205 Section 5) */
	LDNS_RDF_TYPE_HIP,
	/** variable length any type rdata where the length
	    is specified by the first 2 bytes */
	LDNS_RDF_TYPE_INT16_DATA,
	/** protocol and port bitmaps */
	LDNS_RDF_TYPE_SERVICE,
	/** location data */
	LDNS_RDF_TYPE_LOC,
	/** well known services */
	LDNS_RDF_TYPE_WKS,
	/** NSAP */
	LDNS_RDF_TYPE_NSAP,
	/** ATMA */
	LDNS_RDF_TYPE_ATMA,
	/** IPSECKEY */
	LDNS_RDF_TYPE_IPSECKEY,
	/** nsec3 hash salt */
	LDNS_RDF_TYPE_NSEC3_SALT,
	/** nsec3 base32 string (with length byte on wire */
	LDNS_RDF_TYPE_NSEC3_NEXT_OWNER,

	/** 4 shorts represented as 4 * 16 bit hex numbers
	 *  separated by colons. For NID and L64.
	 */
	LDNS_RDF_TYPE_ILNP64,

	/** 6 * 8 bit hex numbers separated by dashes. For EUI48. */
	LDNS_RDF_TYPE_EUI48,
	/** 8 * 8 bit hex numbers separated by dashes. For EUI64. */
	LDNS_RDF_TYPE_EUI64,

	/** A non-zero sequence of US-ASCII letters and numbers in lower case.
	 *  For CAA.
	 */
	LDNS_RDF_TYPE_TAG,

	/** A <character-string> encoding of the value field as specified 
	 * [RFC1035], Section 5.1., encoded as remaining rdata.
	 * For CAA.
	 */
	LDNS_RDF_TYPE_LONG_STR,

	/** Since RFC7218 TLSA records can be given with mnemonics,
	 * hence these rdata field types.  But as with DNSKEYs, the output
	 * is always numeric.
	 */
	LDNS_RDF_TYPE_CERTIFICATE_USAGE,
	LDNS_RDF_TYPE_SELECTOR,
	LDNS_RDF_TYPE_MATCHING_TYPE,

	/* Aliases */
	LDNS_RDF_TYPE_BITMAP = LDNS_RDF_TYPE_NSEC
};
typedef enum ldns_enum_rdf_type ldns_rdf_type;

/**
 * algorithms used in CERT rrs
 */
enum ldns_enum_cert_algorithm
{
        LDNS_CERT_PKIX		= 1,
        LDNS_CERT_SPKI		= 2,
        LDNS_CERT_PGP		= 3,
        LDNS_CERT_IPKIX         = 4,
        LDNS_CERT_ISPKI         = 5,
        LDNS_CERT_IPGP          = 6,
        LDNS_CERT_ACPKIX        = 7,
        LDNS_CERT_IACPKIX       = 8,
        LDNS_CERT_URI		= 253,
        LDNS_CERT_OID		= 254
};
typedef enum ldns_enum_cert_algorithm ldns_cert_algorithm;



/**
 * Resource record data field.
 *
 * The data is a network ordered array of bytes, which size is specified by
 * the (16-bit) size field. To correctly parse it, use the type
 * specified in the (16-bit) type field with a value from \ref ldns_rdf_type.
 */
struct ldns_struct_rdf
{
	/** The size of the data (in octets) */
	size_t _size;
	/** The type of the data */
	ldns_rdf_type _type;
	/** Pointer to the data (raw octets) */
	void  *_data;
};
typedef struct ldns_struct_rdf ldns_rdf;

/* prototypes */

/* write access functions */

/**
 * sets the size of the rdf.
 * \param[in] *rd the rdf to operate on
 * \param[in] size the new size
 * \return void
 */
void ldns_rdf_set_size(ldns_rdf *rd, size_t size);

/**
 * sets the size of the rdf.
 * \param[in] *rd the rdf to operate on
 * \param[in] type the new type
 * \return void
 */
void ldns_rdf_set_type(ldns_rdf *rd, ldns_rdf_type type);

/**
 * sets the size of the rdf.
 * \param[in] *rd the rdf to operate on
 * \param[in] *data pointer to the new data
 * \return void
 */
void ldns_rdf_set_data(ldns_rdf *rd, void *data);

/* read access */

/**
 * returns the size of the rdf.
 * \param[in] *rd the rdf to read from
 * \return uint16_t with the size
 */
size_t ldns_rdf_size(const ldns_rdf *rd);

/**
 * returns the type of the rdf. We need to insert _get_
 * here to prevent conflict the the rdf_type TYPE.
 * \param[in] *rd the rdf to read from
 * \return ldns_rdf_type with the type
 */
ldns_rdf_type ldns_rdf_get_type(const ldns_rdf *rd);

/**
 * returns the data of the rdf.
 * \param[in] *rd the rdf to read from
 *
 * \return uint8_t* pointer to the rdf's data
 */
uint8_t *ldns_rdf_data(const ldns_rdf *rd);

/* creator functions */

/**
 * allocates a new rdf structure and fills it.
 * This function DOES NOT copy the contents from
 * the buffer, unlinke ldns_rdf_new_frm_data()
 * \param[in] type type of the rdf
 * \param[in] size size of the buffer
 * \param[in] data pointer to the buffer to be copied
 * \return the new rdf structure or NULL on failure
 */
ldns_rdf *ldns_rdf_new(ldns_rdf_type type, size_t size, void *data);

/**
 * allocates a new rdf structure and fills it.
 * This function _does_ copy the contents from
 * the buffer, unlinke ldns_rdf_new()
 * \param[in] type type of the rdf
 * \param[in] size size of the buffer
 * \param[in] data pointer to the buffer to be copied
 * \return the new rdf structure or NULL on failure
 */
ldns_rdf *ldns_rdf_new_frm_data(ldns_rdf_type type, size_t size, const void *data);

/**
 * creates a new rdf from a string.
 * \param[in] type   type to use
 * \param[in] str string to use
 * \return ldns_rdf* or NULL in case of an error
 */
ldns_rdf *ldns_rdf_new_frm_str(ldns_rdf_type type, const char *str);

/**
 * creates a new rdf from a file containing a string.
 * \param[out] r the new rdf
 * \param[in] type   type to use
 * \param[in] fp the file pointer  to use
 * \return LDNS_STATUS_OK or the error
 */
ldns_status ldns_rdf_new_frm_fp(ldns_rdf **r, ldns_rdf_type type, FILE *fp);

/**
 * creates a new rdf from a file containing a string.
 * \param[out] r the new rdf
 * \param[in] type   type to use
 * \param[in] fp the file pointer  to use
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \return LDNS_STATUS_OK or the error
 */
ldns_status ldns_rdf_new_frm_fp_l(ldns_rdf **r, ldns_rdf_type type, FILE *fp, int *line_nr);

/* destroy functions */

/**
 * frees a rdf structure, leaving the
 * data pointer intact.
 * \param[in] rd the pointer to be freed
 * \return void
 */
void ldns_rdf_free(ldns_rdf *rd);

/**
 * frees a rdf structure _and_ frees the
 * data. rdf should be created with _new_frm_data
 * \param[in] rd the rdf structure to be freed
 * \return void
 */
void ldns_rdf_deep_free(ldns_rdf *rd);

/* conversion functions */

/**
 * returns the rdf containing the native uint8_t repr.
 * \param[in] type the ldns_rdf type to use
 * \param[in] value the uint8_t to use
 * \return ldns_rdf* with the converted value
 */
ldns_rdf *ldns_native2rdf_int8(ldns_rdf_type type, uint8_t value);

/**
 * returns the rdf containing the native uint16_t representation.
 * \param[in] type the ldns_rdf type to use
 * \param[in] value the uint16_t to use
 * \return ldns_rdf* with the converted value
 */
ldns_rdf *ldns_native2rdf_int16(ldns_rdf_type type, uint16_t value);

/**
 * returns an rdf that contains the given int32 value.
 *
 * Because multiple rdf types can contain an int32, the
 * type must be specified
 * \param[in] type the ldns_rdf type to use
 * \param[in] value the uint32_t to use
 * \return ldns_rdf* with the converted value
 */
ldns_rdf *ldns_native2rdf_int32(ldns_rdf_type type, uint32_t value);

/**
 * returns an int16_data rdf that contains the data in the
 * given array, preceded by an int16 specifying the length.
 *
 * The memory is copied, and an LDNS_RDF_TYPE_INT16DATA is returned
 * \param[in] size the size of the data
 * \param[in] *data pointer to the actual data
 *
 * \return ldns_rd* the rdf with the data
 */
ldns_rdf *ldns_native2rdf_int16_data(size_t size, uint8_t *data);

/**
 * reverses an rdf, only actually useful for AAAA and A records.
 * The returned rdf has the type LDNS_RDF_TYPE_DNAME!
 * \param[in] *rd rdf to be reversed
 * \return the reversed rdf (a newly created rdf)
 */
ldns_rdf *ldns_rdf_address_reverse(const ldns_rdf *rd);

/**
 * returns the native uint8_t representation from the rdf.
 * \param[in] rd the ldns_rdf to operate on
 * \return uint8_t the value extracted
 */
uint8_t 	ldns_rdf2native_int8(const ldns_rdf *rd);

/**
 * returns the native uint16_t representation from the rdf.
 * \param[in] rd the ldns_rdf to operate on
 * \return uint16_t the value extracted
 */
uint16_t	ldns_rdf2native_int16(const ldns_rdf *rd);

/**
 * returns the native uint32_t representation from the rdf.
 * \param[in] rd the ldns_rdf to operate on
 * \return uint32_t the value extracted
 */
uint32_t ldns_rdf2native_int32(const ldns_rdf *rd);

/**
 * returns the native time_t representation from the rdf.
 * \param[in] rd the ldns_rdf to operate on
 * \return time_t the value extracted (32 bits currently)
 */
time_t ldns_rdf2native_time_t(const ldns_rdf *rd);

/**
 * converts a ttl value (like 5d2h) to a long.
 * \param[in] nptr the start of the string
 * \param[out] endptr points to the last char in case of error
 * \return the convert duration value
 */
uint32_t ldns_str2period(const char *nptr, const char **endptr);

/**
 * removes \\DDD, \\[space] and other escapes from the input.
 * See RFC 1035, section 5.1.
 * \param[in] word what to check
 * \param[in] length the string
 * \return ldns_status mesg
 */
ldns_status ldns_octet(char *word, size_t *length);

/**
 * clones a rdf structure. The data is copied.
 * \param[in] rd rdf to be copied
 * \return a new rdf structure
 */
ldns_rdf *ldns_rdf_clone(const ldns_rdf *rd);

/**
 * compares two rdf's on their wire formats.
 * (To order dnames according to rfc4034, use ldns_dname_compare)
 * \param[in] rd1 the first one
 * \param[in] rd2 the second one
 * \return 0 if equal
 * \return -1 if rd1 comes before rd2
 * \return +1 if rd2 comes before rd1
 */
int ldns_rdf_compare(const ldns_rdf *rd1, const ldns_rdf *rd2);

/**
 * Gets the algorithm value, the HIT and Public Key data from the rdf with
 * type LDNS_RDF_TYPE_HIP.
 * \param[in] rdf the rdf with type LDNS_RDF_TYPE_HIP
 * \param[out] alg      the algorithm
 * \param[out] hit_size the size of the HIT data
 * \param[out] hit      the hit data
 * \param[out] pk_size  the size of the Public Key data
 * \param[out] pk       the  Public Key data
 * \return LDNS_STATUS_OK on success, and the error otherwise
 */
ldns_status ldns_rdf_hip_get_alg_hit_pk(ldns_rdf *rdf, uint8_t* alg,
		uint8_t *hit_size, uint8_t** hit,
		uint16_t *pk_size, uint8_t** pk);

/**
 * Creates a new LDNS_RDF_TYPE_HIP rdf from given data.
 * \param[out] rdf      the newly created LDNS_RDF_TYPE_HIP rdf
 * \param[in]  alg      the algorithm
 * \param[in]  hit_size the size of the HIT data
 * \param[in]  hit      the hit data
 * \param[in]  pk_size  the size of the Public Key data
 * \param[in]  pk       the  Public Key data
 * \return LDNS_STATUS_OK on success, and the error otherwise
 */
ldns_status ldns_rdf_hip_new_frm_alg_hit_pk(ldns_rdf** rdf, uint8_t alg,
		uint8_t hit_size, uint8_t *hit, uint16_t pk_size, uint8_t *pk);

#ifdef __cplusplus
}
#endif

#endif	/* LDNS_RDATA_H */
