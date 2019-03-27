/**
 * str2wire.h -  read txt presentation of RRs
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Parses text to wireformat.
 */

#ifndef LDNS_STR2WIRE_H
#define LDNS_STR2WIRE_H

/* include rrdef for MAX_DOMAINLEN constant */
#include <sldns/rrdef.h>

#ifdef __cplusplus
extern "C" {
#endif
struct sldns_struct_lookup_table;

/** buffer to read an RR, cannot be larger than 64K because of packet size */
#define LDNS_RR_BUF_SIZE 65535 /* bytes */
#define LDNS_DEFAULT_TTL	3600

/*
 * To convert class and type to string see
 * sldns_get_rr_class_by_name(str)
 * sldns_get_rr_type_by_name(str)
 * from rrdef.h
 */

/**
 * Convert text string into dname wireformat, mallocless, with user buffer.
 * @param str: the text string with the domain name.
 * @param buf: the result buffer, suggested size LDNS_MAX_DOMAINLEN+1
 * @param len: length of the buffer on input, length of the result on output.
 * @return 0 on success, otherwise an error.
 */
int sldns_str2wire_dname_buf(const char* str, uint8_t* buf, size_t* len);

/**
 * Same as sldns_str2wire_dname_buf, but concatenates origin if the domain
 * name is relative (does not end in '.').
 * @param str: the text string with the domain name.
 * @param buf: the result buffer, suggested size LDNS_MAX_DOMAINLEN+1
 * @param len: length of the buffer on input, length of the result on output.
 * @param origin: the origin to append or NULL (nothing is appended).
 * @param origin_len: length of origin.
 * @return 0 on success, otherwise an error.
 */
int sldns_str2wire_dname_buf_origin(const char* str, uint8_t* buf, size_t* len,
	uint8_t* origin, size_t origin_len);

/**
 * Convert text string into dname wireformat
 * @param str: the text string with the domain name.
 * @param len: returned length of wireformat.
 * @return wireformat dname (malloced) or NULL on failure.
 */
uint8_t* sldns_str2wire_dname(const char* str, size_t* len);

/**
 * Convert text RR to wireformat, with user buffer.
 * @param str: the RR data in text presentation format.
 * @param rr: the buffer where the result is stored into.  This buffer has
 * 	the wire-dname(uncompressed), type, class, ttl, rdatalen, rdata.
 * 	These values are probably not aligned, and in network format.
 * 	Use the sldns_wirerr_get_xxx functions to access them safely.
 * 	buffer size LDNS_RR_BUF_SIZE is suggested.
 * @param len: on input the length of the buffer, on output the amount of
 * 	the buffer used for the rr.
 * @param dname_len: if non-NULL, filled with the dname length as result.
 * 	Because after the dname you find the type, class, ttl, rdatalen, rdata.
 * @param default_ttl: TTL used if no TTL available.
 * @param origin: used for origin dname (if not NULL)
 * @param origin_len: length of origin.
 * @param prev: used for prev_rr dname (if not NULL)
 * @param prev_len: length of prev.
 * @return 0 on success, an error on failure.
 */
int sldns_str2wire_rr_buf(const char* str, uint8_t* rr, size_t* len,
	size_t* dname_len, uint32_t default_ttl, uint8_t* origin,
	size_t origin_len, uint8_t* prev, size_t prev_len);

/**
 * Same as sldns_str2wire_rr_buf, but there is no rdata, it returns an RR
 * with zero rdata and no ttl.  It has name, type, class.
 * You can access those with the sldns_wirerr_get_type and class functions.
 * @param str: the RR data in text presentation format.
 * @param rr: the buffer where the result is stored into.
 * @param len: on input the length of the buffer, on output the amount of
 * 	the buffer used for the rr.
 * @param dname_len: if non-NULL, filled with the dname length as result.
 * 	Because after the dname you find the type, class, ttl, rdatalen, rdata.
 * @param origin: used for origin dname (if not NULL)
 * @param origin_len: length of origin.
 * @param prev: used for prev_rr dname (if not NULL)
 * @param prev_len: length of prev.
 * @return 0 on success, an error on failure.
 */
int sldns_str2wire_rr_question_buf(const char* str, uint8_t* rr, size_t* len,
	size_t* dname_len, uint8_t* origin, size_t origin_len, uint8_t* prev,
	size_t prev_len);

/**
 * Get the type of the RR.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return type in host byteorder
 */
uint16_t sldns_wirerr_get_type(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Get the class of the RR.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return class in host byteorder
 */
uint16_t sldns_wirerr_get_class(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Get the ttl of the RR.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return ttl in host byteorder
 */
uint32_t sldns_wirerr_get_ttl(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Get the rdata length of the RR.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return rdata length in host byteorder
 * 	If the rdata length is larger than the rr-len allows, it is truncated.
 * 	So, that it is safe to read the data length returned
 * 	from this function from the rdata pointer of sldns_wirerr_get_rdata.
 */
uint16_t sldns_wirerr_get_rdatalen(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Get the rdata pointer of the RR.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return rdata pointer
 */
uint8_t* sldns_wirerr_get_rdata(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Get the rdata pointer of the RR. prefixed with rdata length.
 * @param rr: the RR in wire format.
 * @param len: rr length.
 * @param dname_len: dname length to skip.
 * @return pointer to rdatalength, followed by the rdata.
 */
uint8_t* sldns_wirerr_get_rdatawl(uint8_t* rr, size_t len, size_t dname_len);

/**
 * Parse result codes
 */
#define LDNS_WIREPARSE_MASK 0x0fff
#define LDNS_WIREPARSE_SHIFT 12
#define LDNS_WIREPARSE_ERROR(e) ((e)&LDNS_WIREPARSE_MASK)
#define LDNS_WIREPARSE_OFFSET(e) (((e)&~LDNS_WIREPARSE_MASK)>>LDNS_WIREPARSE_SHIFT)
/* use lookuptable to get error string, sldns_wireparse_errors */
#define LDNS_WIREPARSE_ERR_OK 0
#define LDNS_WIREPARSE_ERR_GENERAL 342
#define LDNS_WIREPARSE_ERR_DOMAINNAME_OVERFLOW 343
#define LDNS_WIREPARSE_ERR_DOMAINNAME_UNDERFLOW 344
#define LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL 345
#define LDNS_WIREPARSE_ERR_LABEL_OVERFLOW 346
#define LDNS_WIREPARSE_ERR_EMPTY_LABEL 347
#define LDNS_WIREPARSE_ERR_SYNTAX_BAD_ESCAPE 348
#define LDNS_WIREPARSE_ERR_SYNTAX 349
#define LDNS_WIREPARSE_ERR_SYNTAX_TTL 350
#define LDNS_WIREPARSE_ERR_SYNTAX_TYPE 351
#define LDNS_WIREPARSE_ERR_SYNTAX_CLASS 352
#define LDNS_WIREPARSE_ERR_SYNTAX_RDATA 353
#define LDNS_WIREPARSE_ERR_SYNTAX_MISSING_VALUE 354
#define LDNS_WIREPARSE_ERR_INVALID_STR 355
#define LDNS_WIREPARSE_ERR_SYNTAX_B64 356
#define LDNS_WIREPARSE_ERR_SYNTAX_B32_EXT 357
#define LDNS_WIREPARSE_ERR_SYNTAX_HEX 358
#define LDNS_WIREPARSE_ERR_CERT_BAD_ALGORITHM 359
#define LDNS_WIREPARSE_ERR_SYNTAX_TIME 360
#define LDNS_WIREPARSE_ERR_SYNTAX_PERIOD 361
#define LDNS_WIREPARSE_ERR_SYNTAX_ILNP64 362
#define LDNS_WIREPARSE_ERR_SYNTAX_EUI48 363
#define LDNS_WIREPARSE_ERR_SYNTAX_EUI64 364
#define LDNS_WIREPARSE_ERR_SYNTAX_TAG 365
#define LDNS_WIREPARSE_ERR_NOT_IMPL 366
#define LDNS_WIREPARSE_ERR_SYNTAX_INT 367
#define LDNS_WIREPARSE_ERR_SYNTAX_IP4 368
#define LDNS_WIREPARSE_ERR_SYNTAX_IP6 369
#define LDNS_WIREPARSE_ERR_SYNTAX_INTEGER_OVERFLOW 370
#define LDNS_WIREPARSE_ERR_INCLUDE 371
#define LDNS_WIREPARSE_ERR_PARENTHESIS 372

/**
 * Get reference to a constant string for the (parse) error.
 * @param e: error return value
 * @return string.
 */
const char* sldns_get_errorstr_parse(int e);

/**
 * wire parse state for parsing files
 */
struct sldns_file_parse_state {
	/** the origin domain name, if len!=0. uncompressed wireformat */
	uint8_t origin[LDNS_MAX_DOMAINLEN+1];
	/** length of origin domain name, in bytes. 0 if not set. */
	size_t origin_len;
	/** the previous domain name, if len!=0. uncompressed wireformat*/
	uint8_t prev_rr[LDNS_MAX_DOMAINLEN+1];
	/** length of the previous domain name, in bytes. 0 if not set. */
	size_t prev_rr_len;
	/** default TTL, this is used if the text does not specify a TTL,
	 * host byteorder */
	uint32_t default_ttl;
	/** line number information */
	int lineno;
};

/**
 * Read one RR from zonefile with buffer for the data.
 * @param in: file that is read from (one RR, multiple lines if it spans them).
 * @param rr: this is malloced by the user and the result is stored here,
 * 	if an RR is read.  If no RR is read this is signalled with the
 * 	return len set to 0 (for ORIGIN, TTL directives).
 * 	The read line is available in the rr_buf (zero terminated), for
 * 	$DIRECTIVE style elements.
 * @param len: on input, the length of the rr buffer.  on output the rr len.
 * 	Buffer size of 64k should be enough.
 * @param dname_len: returns the length of the dname initial part of the rr.
 * @param parse_state: pass a pointer to user-allocated struct.
 * 	Contents are maintained by this function.
 * 	If you pass NULL then ORIGIN and TTL directives are not honored.
 * 	You can start out with a particular origin by pre-filling it.
 * 	otherwise, zero the structure before passing it.
 * 	lineno is incremented when a newline is passed by the parser,
 * 	you should initialize it at 1 at the start of the file.
 * @return 0 on success, error on failure.
 */
int sldns_fp2wire_rr_buf(FILE* in, uint8_t* rr, size_t* len, size_t* dname_len,
	struct sldns_file_parse_state* parse_state);

/**
 * Convert one rdf in rdata to wireformat and parse from string.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @param rdftype: the type of the rdf.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_rdf_buf(const char* str, uint8_t* rd, size_t* len,
	sldns_rdf_type rdftype);

/**
 * Convert rdf of type LDNS_RDF_TYPE_INT8 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_int8_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_INT16 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_int16_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_INT32 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_int32_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_A from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_a_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_AAAA from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_aaaa_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_STR from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_str_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_APL from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_apl_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_B64 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_b64_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_B32_EXT from string to wireformat.
 * And also LDNS_RDF_TYPE_NSEC3_NEXT_OWNER.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_b32_ext_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_HEX from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_hex_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_NSEC from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_nsec_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_TYPE from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_type_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_CLASS from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_class_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_CERT_ALG from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_cert_alg_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_ALG from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_alg_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_TIME from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_time_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_PERIOD from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_period_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_TSIGTIME from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_tsigtime_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_TSIGERROR from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_tsigerror_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_LOC from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_loc_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_WKS from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_wks_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_NSAP from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_nsap_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_ATMA from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_atma_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_IPSECKEY from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_ipseckey_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_NSEC3_SALT from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_nsec3_salt_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_ILNP64 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_ilnp64_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_EUI48 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_eui48_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_EUI64 from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_eui64_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_TAG from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_tag_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_LONG_STR from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_long_str_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_HIP from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_hip_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Convert rdf of type LDNS_RDF_TYPE_INT16_DATA from string to wireformat.
 * @param str: the text to convert for this rdata element.
 * @param rd: rdata buffer for the wireformat.
 * @param len: length of rd buffer on input, used length on output.
 * @return 0 on success, error on failure.
 */
int sldns_str2wire_int16_data_buf(const char* str, uint8_t* rd, size_t* len);

/**
 * Strip whitespace from the start and the end of line.
 * @param line: modified with 0 to shorten it.
 * @return new start with spaces skipped.
 */
char * sldns_strip_ws(char *line);
#ifdef __cplusplus
}
#endif

#endif /* LDNS_STR2WIRE_H */
