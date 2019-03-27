/**
 * wire2str.h -  txt presentation of RRs
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Contains functions to translate the wireformat to text
 * representation, as well as functions to print them.
 */

#ifndef LDNS_WIRE2STR_H
#define LDNS_WIRE2STR_H

#ifdef __cplusplus
extern "C" {
#endif
struct sldns_struct_lookup_table;

/* lookup tables for standard DNS stuff  */
/** Taken from RFC 2535, section 7.  */
extern struct sldns_struct_lookup_table* sldns_algorithms;
/** DS record hash algorithms */
extern struct sldns_struct_lookup_table* sldns_hashes;
/** Taken from RFC 2538, section 2.1.  */
extern struct sldns_struct_lookup_table* sldns_cert_algorithms;
/** Response codes */
extern struct sldns_struct_lookup_table* sldns_rcodes;
/** Operation codes */
extern struct sldns_struct_lookup_table* sldns_opcodes;
/** EDNS flags */
extern struct sldns_struct_lookup_table* sldns_edns_flags;
/** EDNS option codes */
extern struct sldns_struct_lookup_table* sldns_edns_options;
/** error string from wireparse */
extern struct sldns_struct_lookup_table* sldns_wireparse_errors;
/** tsig errors are the rcodes with extra (higher) values */
extern struct sldns_struct_lookup_table* sldns_tsig_errors;

/**
 * Convert wireformat packet to a string representation
 * @param data: wireformat packet data (starting at ID bytes).
 * @param len: length of packet.
 * @return string(malloced) or NULL on failure.
 */
char* sldns_wire2str_pkt(uint8_t* data, size_t len);

/**
 * Convert wireformat RR to a string representation.
 * @param rr: the wireformat RR, in uncompressed form.  Starts at the domain
 * 	name start, ends with the rdata of the RR.
 * @param len: length of the rr wireformat.
 * @return string(malloced) or NULL on failure.
 */
char* sldns_wire2str_rr(uint8_t* rr, size_t len);

/**
 * Conver wire dname to a string.
 * @param dname: the dname in uncompressed wireformat.
 * @param dname_len: length of the dname.
 * @return string or NULL on failure.
 */
char* sldns_wire2str_dname(uint8_t* dname, size_t dname_len);

/**
 * Convert wire RR type to a string, 'MX', 'TYPE1234'...
 * @param rrtype: the RR type in host order.
 * @return malloced string with the RR type or NULL on malloc failure.
 */
char* sldns_wire2str_type(uint16_t rrtype);

/**
 * Convert wire RR class to a string, 'IN', 'CLASS1'.
 * @param rrclass: the RR class in host order.
 * @return malloced string with the RR class or NULL on malloc failure.
 */
char* sldns_wire2str_class(uint16_t rrclass);

/**
 * Convert wire packet rcode to a string, 'NOERROR', 'NXDOMAIN'...
 * @param rcode: as integer, host order
 * @return malloced string with the rcode or NULL on malloc failure.
 */
char* sldns_wire2str_rcode(int rcode);

/**
 * Print to string, move string along for next content. With va_list.
 * @param str: string buffer.  Adjusted at end to after the output.
 * @param slen: length of the string buffer.  Adjusted at end.
 * @param format: printf format string.
 * @param args: arguments for printf.
 * @return number of characters needed. Can be larger than slen.
 */
int sldns_str_vprint(char** str, size_t* slen, const char* format, va_list args);

/**
 * Print to string, move string along for next content.
 * @param str: string buffer.  Adjusted at end to after the output.
 * @param slen: length of the string buffer.  Adjusted at end.
 * @param format: printf format string and arguments for it.
 * @return number of characters needed. Can be larger than slen.
 */
int sldns_str_print(char** str, size_t* slen, const char* format, ...)
	ATTR_FORMAT(printf, 3, 4);

/**
 * Convert wireformat packet to a string representation with user buffer
 * It appends every RR with default comments.
 * For more formatter options use the function: TBD(TODO)
 * @param data: wireformat packet data (starting at ID bytes).
 * @param data_len: length of packet.
 * @param str: the string buffer for the output.
 * 	If you pass NULL as the str the return value of the function is
 * 	the str_len you need for the entire packet.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_pkt_buf(uint8_t* data, size_t data_len, char* str,
	size_t str_len);

/**
 * Scan wireformat packet to a string representation with user buffer
 * It appends every RR with default comments.
 * For more formatter options use the function: TBD(TODO)
 * @param data: wireformat packet data (starting at ID bytes).
 * @param data_len: length of packet.
 * @param str: the string buffer for the output.
 * @param str_len: the size of the string buffer.
 * @return number of characters for string.
 * returns the number of characters that are needed (except terminating null),
 * so it may return a value larger than str_len.
 * On error you get less output (i.e. shorter output in str (null terminated))
 * On exit the data, data_len, str and str_len values are adjusted to move them
 * from their original position along the input and output for the content
 * that has been consumed (and produced) by this function.  If the end of the
 * output string is reached, *str_len is set to 0.  The output string is null
 * terminated (shortening the output if necessary).  If the end of the input
 * is reached *data_len is set to 0.
 */
int sldns_wire2str_pkt_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat rr to string, with user buffers.  It shifts the arguments
 * to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rr_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat question rr to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rrquestion_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat RR to string in unknown RR format, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rr_unknown_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

/**
 * Print to string the RR-information comment in default format,
 * with user buffers.  Moves string along.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rr: wireformat data.
 * @param rrlen: length of data buffer.
 * @param dname_off: offset in buffer behind owner dname, the compressed size
 * 	of the owner name.
 * @param rrtype: type of the RR, host format.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rr_comment_print(char** str, size_t* str_len, uint8_t* rr,
	size_t rrlen, size_t dname_off, uint16_t rrtype);

/**
 * Scan wireformat packet header to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_header_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat rdata to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.  The length of the rdata in the
 * 	buffer.  The rdatalen itself has already been scanned, the data
 * 	points to the rdata after the rdatalen.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rrtype: RR type of Rdata, host format.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rdata_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint16_t rrtype, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat rdata to string in unknown format, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer, the length of the rdata in buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rdata_unknown_scan(uint8_t** data, size_t* data_len,
	char** str, size_t* str_len);

/**
 * Scan wireformat domain name to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_dname_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat rr type to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_type_scan(uint8_t** data, size_t* data_len, char** str,
        size_t* str_len);

/**
 * Scan wireformat rr class to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_class_scan(uint8_t** data, size_t* data_len, char** str,
        size_t* str_len);

/**
 * Scan wireformat rr ttl to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_ttl_scan(uint8_t** data, size_t* data_len, char** str,
        size_t* str_len);


/**
 * Print host format rr type to string.  Moves string along, user buffers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rrtype: host format rr type.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_type_print(char** str, size_t* str_len, uint16_t rrtype);

/**
 * Print host format rr class to string.  Moves string along, user buffers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rrclass: host format rr class.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_class_print(char** str, size_t* str_len, uint16_t rrclass);

/**
 * Print host format rcode to string.  Moves string along, user buffers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rcode: host format rcode number.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_rcode_print(char** str, size_t* str_len, int rcode);

/**
 * Print host format opcode to string.  Moves string along, user buffers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param opcode: host format opcode number.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_opcode_print(char** str, size_t* str_len, int opcode);

/**
 * Print host format EDNS0 option to string.  Moves string along, user buffers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param opcode: host format option number.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_option_code_print(char** str, size_t* str_len,
	uint16_t opcode);

/**
 * Convert RR to string presentation format, on one line.  User buffer.
 * @param rr: wireformat RR data
 * @param rr_len: length of the rr wire data.
 * @param str: the string buffer to write to.
 * 	If you pass NULL as the str, the return value of the function is
 * 	the str_len you need for the entire packet.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rr_buf(uint8_t* rr, size_t rr_len, char* str,
	size_t str_len);

/**
 * Convert question RR to string presentation format, on one line.  User buffer.
 * @param rr: wireformat RR data
 * @param rr_len: length of the rr wire data.
 * @param str: the string buffer to write to.
 * 	If you pass NULL as the str, the return value of the function is
 * 	the str_len you need for the entire packet.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rrquestion_buf(uint8_t* rr, size_t rr_len, char* str,
	size_t str_len);

/**
 * 3597 printout of an RR in unknown rr format.
 * There are more format and comment options available for printout
 * with the function: TBD(TODO)
 * @param rr: wireformat RR data
 * @param rr_len: length of the rr wire data.
 * @param str: the string buffer to write to.
 * 	If you pass NULL as the str, the return value of the function is
 * 	the str_len you need for the entire rr.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rr_unknown_buf(uint8_t* rr, size_t rr_len, char* str,
	size_t str_len);

/**
 * This creates the comment to print after the RR. ; keytag=... , and other
 * basic comments for RRs.
 * There are more format and comment options available for printout
 * with the function: TBD(TODO)
 * @param rr: wireformat RR data
 * @param rr_len: length of the rr wire data.
 * @param dname_len: length of the dname in front of the RR.
 * @param str: the string buffer to write to.
 * 	If you pass NULL as the str, the return value of the function is
 * 	the str_len you need for the entire comment.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rr_comment_buf(uint8_t* rr, size_t rr_len, size_t dname_len,
	char* str, size_t str_len);

/**
 * Convert RDATA to string presentation format, on one line.  User buffer.
 * @param rdata: wireformat rdata part of an RR.
 * @param rdata_len: length of the rr wire data.
 * @param str: the string buffer to write to.
 * 	If you pass NULL as the str, the return value of the function is
 * 	the str_len you need for the entire packet.  It does not include
 * 	the 0 byte at the end.
 * @param str_len: the size of the string buffer.  If more is needed, it'll
 * 	silently truncate the output to fit in the buffer.
 * @param rrtype: rr type of the data
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rdata_buf(uint8_t* rdata, size_t rdata_len, char* str,
	size_t str_len, uint16_t rrtype);

/**
 * Convert wire RR type to a string, 'MX', 'TYPE12'.  With user buffer.
 * @param rrtype: the RR type in host order.
 * @param str: the string to write to.
 * @param len: length of str.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_type_buf(uint16_t rrtype, char* str, size_t len);

/**
 * Convert wire RR class to a string, 'IN', 'CLASS12'.  With user buffer.
 * @param rrclass: the RR class in host order.
 * @param str: the string to write to.
 * @param len: length of str.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_class_buf(uint16_t rrclass, char* str, size_t len);

/**
 * Convert wire RR rcode to a string, 'NOERROR', 'NXDOMAIN'.  With user buffer.
 * @param rcode: rcode as integer in host order
 * @param str: the string to write to.
 * @param len: length of str.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_rcode_buf(int rcode, char* str, size_t len);

/**
 * Convert host format opcode to a string. 'QUERY', 'NOTIFY', 'UPDATE'.
 * With user buffer.
 * @param opcode: opcode as integer in host order
 * @param str: the string to write to.
 * @param len: length of str.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_opcode_buf(int opcode, char* str, size_t len);

/**
 * Convert wire dname to a string, "example.com.".  With user buffer.
 * @param dname: the dname in uncompressed wireformat.
 * @param dname_len: length of the dname.
 * @param str: the string to write to.
 * @param len: length of string.
 * @return the number of characters for this element, excluding zerobyte.
 * 	Is larger or equal than str_len if output was truncated.
 */
int sldns_wire2str_dname_buf(uint8_t* dname, size_t dname_len, char* str,
	size_t len);

/**
 * Scan wireformat rdf field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param rdftype: the type of the rdata field, enum sldns_rdf_type.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_rdf_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, int rdftype, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat int8 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_int8_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat int16 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_int16_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat int32 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_int32_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat period field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_period_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat tsigtime field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_tsigtime_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat ip4 A field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_a_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat ip6 AAAA field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_aaaa_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat str field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_str_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat apl field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_apl_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat b32_ext field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_b32_ext_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat b64 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_b64_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat hex field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_hex_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat nsec bitmap field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_nsec_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat nsec3_salt field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_nsec3_salt_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat cert_alg field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_cert_alg_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat alg field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_alg_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat type unknown field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_unknown_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat time field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_time_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat LOC field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_loc_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat WKS field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_wks_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat NSAP field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_nsap_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat ATMA field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_atma_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat IPSECKEY field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet for decompression, if NULL no decompression.
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_ipseckey_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

/**
 * Scan wireformat HIP (algo, HIT, pubkey) field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_hip_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat int16_data field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_int16_data_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat tsigerror field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_tsigerror_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat nsec3_next_owner field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_nsec3_next_owner_scan(uint8_t** data, size_t* data_len,
	char** str, size_t* str_len);

/**
 * Scan wireformat ILNP64 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_ilnp64_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat EUI48 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_eui48_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat EUI64 field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_eui64_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat TAG field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_tag_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Scan wireformat long_str field to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @return number of characters (except null) needed to print.
 * 	Can return -1 on failure.
 */
int sldns_wire2str_long_str_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len);

/**
 * Print EDNS LLQ option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_llq_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS UL option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_ul_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS NSID option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_nsid_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS DAU option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_dau_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS DHU option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_dhu_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS N3U option data to string.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_n3u_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print EDNS SUBNET option data to string. User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_subnet_print(char** str, size_t* str_len,
	uint8_t* option_data, size_t option_len);

/**
 * Print an EDNS option as OPT: VALUE.  User buffers, moves string pointers.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param option_code: host format EDNS option code.
 * @param option_data: buffer with EDNS option code data.
 * @param option_len: length of the data for this option.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_option_print(char** str, size_t* str_len,
	uint16_t option_code, uint8_t* option_data, size_t option_len);

/**
 * Scan wireformat EDNS OPT to string, with user buffers.
 * It shifts the arguments to move along (see sldns_wire2str_pkt_scan).
 * @param data: wireformat data.
 * @param data_len: length of data buffer.
 * @param str: string buffer.
 * @param str_len: length of string buffer.
 * @param pkt: packet with header and other info (may be NULL)
 * @param pktlen: length of packet buffer.
 * @return number of characters (except null) needed to print.
 */
int sldns_wire2str_edns_scan(uint8_t** data, size_t* data_len, char** str,
	size_t* str_len, uint8_t* pkt, size_t pktlen);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_WIRE2STR_H */
