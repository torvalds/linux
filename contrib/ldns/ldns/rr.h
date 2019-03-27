/*
 * rr.h -  resource record definitions
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
 * Contains the definition of ldns_rr and functions to manipulate those.
 */


#ifndef LDNS_RR_H
#define LDNS_RR_H

#include <ldns/common.h>
#include <ldns/rdata.h>
#include <ldns/buffer.h>
#include <ldns/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a dname label */
#define LDNS_MAX_LABELLEN     63
/** Maximum length of a complete dname */
#define LDNS_MAX_DOMAINLEN    255
/** Maximum number of pointers in 1 dname */
#define LDNS_MAX_POINTERS	65535
/** The bytes TTL, CLASS and length use up in an rr */
#define LDNS_RR_OVERHEAD	10

/* The first fields are contiguous and can be referenced instantly */
#define LDNS_RDATA_FIELD_DESCRIPTORS_COMMON 259



/**
 *  The different RR classes.
 */
enum ldns_enum_rr_class
{
	/** the Internet */
	LDNS_RR_CLASS_IN 	= 1,
	/** Chaos class */
	LDNS_RR_CLASS_CH	= 3,
	/** Hesiod (Dyer 87) */
	LDNS_RR_CLASS_HS	= 4,
    /** None class, dynamic update */
    LDNS_RR_CLASS_NONE      = 254,
	/** Any class */
	LDNS_RR_CLASS_ANY	= 255,

	LDNS_RR_CLASS_FIRST     = 0,
	LDNS_RR_CLASS_LAST      = 65535,
	LDNS_RR_CLASS_COUNT     = LDNS_RR_CLASS_LAST - LDNS_RR_CLASS_FIRST + 1
};
typedef enum ldns_enum_rr_class ldns_rr_class;

/**
 *  Used to specify whether compression is allowed.
 */
enum ldns_enum_rr_compress
{
	/** compression is allowed */
	LDNS_RR_COMPRESS,
	LDNS_RR_NO_COMPRESS
};
typedef enum ldns_enum_rr_compress ldns_rr_compress;

/**
 * The different RR types.
 */
enum ldns_enum_rr_type
{
	/**  a host address */
	LDNS_RR_TYPE_A = 1,
	/**  an authoritative name server */
	LDNS_RR_TYPE_NS = 2,
	/**  a mail destination (Obsolete - use MX) */
	LDNS_RR_TYPE_MD = 3,
	/**  a mail forwarder (Obsolete - use MX) */
	LDNS_RR_TYPE_MF = 4,
	/**  the canonical name for an alias */
	LDNS_RR_TYPE_CNAME = 5,
	/**  marks the start of a zone of authority */
	LDNS_RR_TYPE_SOA = 6,
	/**  a mailbox domain name (EXPERIMENTAL) */
	LDNS_RR_TYPE_MB = 7,
	/**  a mail group member (EXPERIMENTAL) */
	LDNS_RR_TYPE_MG = 8,
	/**  a mail rename domain name (EXPERIMENTAL) */
	LDNS_RR_TYPE_MR = 9,
	/**  a null RR (EXPERIMENTAL) */
	LDNS_RR_TYPE_NULL = 10,
	/**  a well known service description */
	LDNS_RR_TYPE_WKS = 11,
	/**  a domain name pointer */
	LDNS_RR_TYPE_PTR = 12,
	/**  host information */
	LDNS_RR_TYPE_HINFO = 13,
	/**  mailbox or mail list information */
	LDNS_RR_TYPE_MINFO = 14,
	/**  mail exchange */
	LDNS_RR_TYPE_MX = 15,
	/**  text strings */
	LDNS_RR_TYPE_TXT = 16,
	/**  RFC1183 */
	LDNS_RR_TYPE_RP = 17,
	/**  RFC1183 */
	LDNS_RR_TYPE_AFSDB = 18,
	/**  RFC1183 */
	LDNS_RR_TYPE_X25 = 19,
	/**  RFC1183 */
	LDNS_RR_TYPE_ISDN = 20,
	/**  RFC1183 */
	LDNS_RR_TYPE_RT = 21,
	/**  RFC1706 */
	LDNS_RR_TYPE_NSAP = 22,
	/**  RFC1348 */
	LDNS_RR_TYPE_NSAP_PTR = 23,
	/**  2535typecode */
	LDNS_RR_TYPE_SIG = 24,
	/**  2535typecode */
	LDNS_RR_TYPE_KEY = 25,
	/**  RFC2163 */
	LDNS_RR_TYPE_PX = 26,
	/**  RFC1712 */
	LDNS_RR_TYPE_GPOS = 27,
	/**  ipv6 address */
	LDNS_RR_TYPE_AAAA = 28,
	/**  LOC record  RFC1876 */
	LDNS_RR_TYPE_LOC = 29,
	/**  2535typecode */
	LDNS_RR_TYPE_NXT = 30,
	/**  draft-ietf-nimrod-dns-01.txt */
	LDNS_RR_TYPE_EID = 31,
	/**  draft-ietf-nimrod-dns-01.txt */
	LDNS_RR_TYPE_NIMLOC = 32,
	/**  SRV record RFC2782 */
	LDNS_RR_TYPE_SRV = 33,
	/**  http://www.jhsoft.com/rfc/af-saa-0069.000.rtf */
	LDNS_RR_TYPE_ATMA = 34,
	/**  RFC2915 */
	LDNS_RR_TYPE_NAPTR = 35,
	/**  RFC2230 */
	LDNS_RR_TYPE_KX = 36,
	/**  RFC2538 */
	LDNS_RR_TYPE_CERT = 37,
	/**  RFC2874 */
	LDNS_RR_TYPE_A6 = 38,
	/**  RFC2672 */
	LDNS_RR_TYPE_DNAME = 39,
	/**  dnsind-kitchen-sink-02.txt */
	LDNS_RR_TYPE_SINK = 40,
	/**  Pseudo OPT record... */
	LDNS_RR_TYPE_OPT = 41,
	/**  RFC3123 */
	LDNS_RR_TYPE_APL = 42,
	/**  RFC4034, RFC3658 */
	LDNS_RR_TYPE_DS = 43,
	/**  SSH Key Fingerprint */
	LDNS_RR_TYPE_SSHFP = 44, /* RFC 4255 */
	/**  IPsec Key */
	LDNS_RR_TYPE_IPSECKEY = 45, /* RFC 4025 */
	/**  DNSSEC */
	LDNS_RR_TYPE_RRSIG = 46, /* RFC 4034 */
	LDNS_RR_TYPE_NSEC = 47, /* RFC 4034 */
	LDNS_RR_TYPE_DNSKEY = 48, /* RFC 4034 */

	LDNS_RR_TYPE_DHCID = 49, /* RFC 4701 */
	/* NSEC3 */
	LDNS_RR_TYPE_NSEC3 = 50, /* RFC 5155 */
	LDNS_RR_TYPE_NSEC3PARAM = 51, /* RFC 5155 */
	LDNS_RR_TYPE_NSEC3PARAMS = 51,
	LDNS_RR_TYPE_TLSA = 52, /* RFC 6698 */
	LDNS_RR_TYPE_SMIMEA = 53, /* draft-ietf-dane-smime */

	LDNS_RR_TYPE_HIP = 55, /* RFC 5205 */

	/** draft-reid-dnsext-zs */
	LDNS_RR_TYPE_NINFO = 56,
	/** draft-reid-dnsext-rkey */
	LDNS_RR_TYPE_RKEY = 57,
        /** draft-ietf-dnsop-trust-history */
        LDNS_RR_TYPE_TALINK = 58,
	LDNS_RR_TYPE_CDS = 59, /* RFC 7344 */
	LDNS_RR_TYPE_CDNSKEY = 60, /* RFC 7344 */
	LDNS_RR_TYPE_OPENPGPKEY = 61, /* RFC 7929 */
	LDNS_RR_TYPE_CSYNC = 62, /* RFC 7477 */

	LDNS_RR_TYPE_SPF = 99, /* RFC 4408 */

	LDNS_RR_TYPE_UINFO = 100,
	LDNS_RR_TYPE_UID = 101,
	LDNS_RR_TYPE_GID = 102,
	LDNS_RR_TYPE_UNSPEC = 103,

	LDNS_RR_TYPE_NID = 104, /* RFC 6742 */
	LDNS_RR_TYPE_L32 = 105, /* RFC 6742 */
	LDNS_RR_TYPE_L64 = 106, /* RFC 6742 */
	LDNS_RR_TYPE_LP = 107, /* RFC 6742 */

	LDNS_RR_TYPE_EUI48 = 108, /* RFC 7043 */
	LDNS_RR_TYPE_EUI64 = 109, /* RFC 7043 */

	LDNS_RR_TYPE_TKEY = 249, /* RFC 2930 */
	LDNS_RR_TYPE_TSIG = 250,
	LDNS_RR_TYPE_IXFR = 251,
	LDNS_RR_TYPE_AXFR = 252,
	/**  A request for mailbox-related records (MB, MG or MR) */
	LDNS_RR_TYPE_MAILB = 253,
	/**  A request for mail agent RRs (Obsolete - see MX) */
	LDNS_RR_TYPE_MAILA = 254,
	/**  any type (wildcard) */
	LDNS_RR_TYPE_ANY = 255,
	LDNS_RR_TYPE_URI = 256, /* RFC 7553 */
	LDNS_RR_TYPE_CAA = 257, /* RFC 6844 */
	LDNS_RR_TYPE_AVC = 258, /* Cisco's DNS-AS RR, see www.dns-as.org */

	/** DNSSEC Trust Authorities */
	LDNS_RR_TYPE_TA = 32768,
	/* RFC 4431, 5074, DNSSEC Lookaside Validation */
	LDNS_RR_TYPE_DLV = 32769,

	/* type codes from nsec3 experimental phase
	LDNS_RR_TYPE_NSEC3 = 65324,
	LDNS_RR_TYPE_NSEC3PARAMS = 65325, */
	LDNS_RR_TYPE_FIRST = 0,
	LDNS_RR_TYPE_LAST  = 65535,
	LDNS_RR_TYPE_COUNT = LDNS_RR_TYPE_LAST - LDNS_RR_TYPE_FIRST + 1
};
typedef enum ldns_enum_rr_type ldns_rr_type;

/**
 * Resource Record
 *
 * This is the basic DNS element that contains actual data
 *
 * From RFC1035:
 * <pre>
3.2.1. Format

All RRs have the same top level format shown below:

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                                               /
    /                      NAME                     /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     CLASS                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TTL                      |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                   RDLENGTH                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
    /                     RDATA                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

where:

NAME            an owner name, i.e., the name of the node to which this
                resource record pertains.

TYPE            two octets containing one of the RR TYPE codes.

CLASS           two octets containing one of the RR CLASS codes.

TTL             a 32 bit signed integer that specifies the time interval
                that the resource record may be cached before the source
                of the information should again be consulted.  Zero
                values are interpreted to mean that the RR can only be
                used for the transaction in progress, and should not be
                cached.  For example, SOA records are always distributed
                with a zero TTL to prohibit caching.  Zero values can
                also be used for extremely volatile data.

RDLENGTH        an unsigned 16 bit integer that specifies the length in
                octets of the RDATA field.

RDATA           a variable length string of octets that describes the
                resource.  The format of this information varies
                according to the TYPE and CLASS of the resource record.
 * </pre>
 *
 * The actual amount and type of rdata fields depend on the RR type of the
 * RR, and can be found by using \ref ldns_rr_descriptor functions.
 */
struct ldns_struct_rr
{
	/**  Owner name, uncompressed */
	ldns_rdf	*_owner;
	/**  Time to live  */
	uint32_t	_ttl;
	/**  Number of data fields */
	size_t	        _rd_count;
	/**  the type of the RR. A, MX etc. */
	ldns_rr_type	_rr_type;
	/**  Class of the resource record.  */
	ldns_rr_class	_rr_class;
	/* everything in the rdata is in network order */
	/**  The array of rdata's */
	ldns_rdf	 **_rdata_fields;
	/**  question rr [it would be nicer if thous is after _rd_count]
		 ABI change: Fix this in next major release
	 */
	bool		_rr_question;
};
typedef struct ldns_struct_rr ldns_rr;

/**
 * List or Set of Resource Records
 *
 * Contains a list of rr's <br>
 * No official RFC-like checks are made
 */
struct ldns_struct_rr_list
{
	size_t _rr_count;
	size_t _rr_capacity;
	ldns_rr **_rrs;
};
typedef struct ldns_struct_rr_list ldns_rr_list;

/**
 * Contains all information about resource record types.
 *
 * This structure contains, for all rr types, the rdata fields that are defined.
 */
struct ldns_struct_rr_descriptor
{
	/** Type of the RR that is described here */
	ldns_rr_type    _type;
	/** Textual name of the RR type.  */
	const char *_name;
	/** Minimum number of rdata fields in the RRs of this type.  */
	uint8_t     _minimum;
	/** Maximum number of rdata fields in the RRs of this type.  */
	uint8_t     _maximum;
	/** Wireformat specification for the rr, i.e. the types of rdata fields in their respective order. */
	const ldns_rdf_type *_wireformat;
	/** Special rdf types */
	ldns_rdf_type _variable;
	/** Specifies whether compression can be used for dnames in this RR type. */
	ldns_rr_compress _compress;
	/** The number of DNAMEs in the _wireformat string, for parsing. */
	uint8_t _dname_count;
};
typedef struct ldns_struct_rr_descriptor ldns_rr_descriptor;


/**
 * Create a rr type bitmap rdf providing enough space to set all 
 * known (to ldns) rr types.
 * \param[out] rdf the constructed rdf
 * \return LDNS_STATUS_OK if all went well.
 */
ldns_status ldns_rdf_bitmap_known_rr_types_space(ldns_rdf** rdf);

/**
 * Create a rr type bitmap rdf with at least all known (to ldns) rr types set.
 * \param[out] rdf the constructed rdf
 * \return LDNS_STATUS_OK if all went well.
 */
ldns_status ldns_rdf_bitmap_known_rr_types(ldns_rdf** rdf);


/**
 * creates a new rr structure.
 * \return ldns_rr *
 */
ldns_rr* ldns_rr_new(void);

/**
 * creates a new rr structure, based on the given type.
 * alloc enough space to hold all the rdf's
 */
ldns_rr* ldns_rr_new_frm_type(ldns_rr_type t);

/**
 * frees an RR structure
 * \param[in] *rr the RR to be freed
 * \return void
 */
void ldns_rr_free(ldns_rr *rr);

/**
 * creates an rr from a string.
 * The string should be a fully filled-in rr, like
 * ownername &lt;space&gt; TTL &lt;space&gt; CLASS &lt;space&gt;
 * TYPE &lt;space&gt; RDATA.
 * \param[out] n the rr to return
 * \param[in] str the string to convert
 * \param[in] default_ttl default ttl value for the rr.
 *            If 0 DEF_TTL will be used
 * \param[in] origin when the owner is relative add this.
 *	The caller must ldns_rdf_deep_free it.
 * \param[out] prev the previous ownername. if this value is not NULL,
 * the function overwrites this with the ownername found in this
 * string. The caller must then ldns_rdf_deep_free it.
 * \return a status msg describing an error or LDNS_STATUS_OK
 */
ldns_status ldns_rr_new_frm_str(ldns_rr **n, const char *str,
                                uint32_t default_ttl, const ldns_rdf *origin,
                                ldns_rdf **prev);

/**
 * creates an rr for the question section from a string, i.e.
 * without RDATA fields
 * Origin and previous RR functionality are the same as in
 * ldns_rr_new_frm_str()
 * \param[out] n the rr to return
 * \param[in] str the string to convert
 * \param[in] origin when the owner is relative add this.
 *	The caller must ldns_rdf_deep_free it.
 * \param prev the previous ownername. the function overwrite this with
 * the current found ownername. The caller must ldns_rdf_deep_free it.
 * \return a status msg describing an error or LDNS_STATUS_OK
 */
ldns_status ldns_rr_new_question_frm_str(ldns_rr **n, const char *str,
                                const ldns_rdf *origin, ldns_rdf **prev);

/**
 * creates a new rr from a file containing a string.
 * \param[out] rr the new rr
 * \param[in] fp the file pointer to use
 * \param[in] default_ttl pointer to a default ttl for the rr. If NULL DEF_TTL will be used
 *            the pointer will be updated if the file contains a $TTL directive
 * \param[in] origin when the owner is relative add this
 * 	      the pointer will be updated if the file contains a $ORIGIN directive
 *	      The caller must ldns_rdf_deep_free it.
 * \param[in] prev when the owner is whitespaces use this as the * ownername
 *            the pointer will be updated after the call
 *	      The caller must ldns_rdf_deep_free it.
 * \return a ldns_status with an error or LDNS_STATUS_OK
 */
ldns_status ldns_rr_new_frm_fp(ldns_rr **rr, FILE *fp, uint32_t *default_ttl, ldns_rdf **origin, ldns_rdf **prev);

/**
 * creates a new rr from a file containing a string.
 * \param[out] rr the new rr
 * \param[in] fp the file pointer to use
 * \param[in] default_ttl a default ttl for the rr. If NULL DEF_TTL will be used
 *            the pointer will be updated if the file contains a $TTL directive
 * \param[in] origin when the owner is relative add this
 * 	      the pointer will be updated if the file contains a $ORIGIN directive
 *	      The caller must ldns_rdf_deep_free it.
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \param[in] prev when the owner is whitespaces use this as the * ownername
 *            the pointer will be updated after the call
 *	      The caller must ldns_rdf_deep_free it.
 * \return a ldns_status with an error or LDNS_STATUS_OK
 */
ldns_status ldns_rr_new_frm_fp_l(ldns_rr **rr, FILE *fp, uint32_t *default_ttl, ldns_rdf **origin, ldns_rdf **prev, int *line_nr);

/**
 * sets the owner in the rr structure.
 * \param[in] *rr rr to operate on
 * \param[in] *owner set to this owner
 * \return void
 */
void ldns_rr_set_owner(ldns_rr *rr, ldns_rdf *owner);

/**
 * sets the question flag in the rr structure.
 * \param[in] *rr rr to operate on
 * \param[in] question question flag
 * \return void
 */
void ldns_rr_set_question(ldns_rr *rr, bool question);

/**
 * sets the ttl in the rr structure.
 * \param[in] *rr rr to operate on
 * \param[in] ttl set to this ttl
 * \return void
 */
void ldns_rr_set_ttl(ldns_rr *rr, uint32_t ttl);

/**
 * sets the rd_count in the rr.
 * \param[in] *rr rr to operate on
 * \param[in] count set to this count
 * \return void
 */
void ldns_rr_set_rd_count(ldns_rr *rr, size_t count);

/**
 * sets the type in the rr.
 * \param[in] *rr rr to operate on
 * \param[in] rr_type set to this type
 * \return void
 */
void ldns_rr_set_type(ldns_rr *rr, ldns_rr_type rr_type);

/**
 * sets the class in the rr.
 * \param[in] *rr rr to operate on
 * \param[in] rr_class set to this class
 * \return void
 */
void ldns_rr_set_class(ldns_rr *rr, ldns_rr_class rr_class);

/**
 * sets a rdf member, it will be set on the
 * position given. The old value is returned, like pop.
 * \param[in] *rr the rr to operate on
 * \param[in] *f the rdf to set
 * \param[in] position the position the set the rdf
 * \return  the old value in the rr, NULL on failyre
 */
ldns_rdf* ldns_rr_set_rdf(ldns_rr *rr, const ldns_rdf *f, size_t position);

/**
 * sets rd_field member, it will be
 * placed in the next available spot.
 * \param[in] *rr rr to operate on
 * \param[in] *f the data field member to set
 * \return bool
 */
bool ldns_rr_push_rdf(ldns_rr *rr, const ldns_rdf *f);

/**
 * removes a rd_field member, it will be
 * popped from the last position.
 * \param[in] *rr rr to operate on
 * \return rdf which was popped (null if nothing)
 */
ldns_rdf* ldns_rr_pop_rdf(ldns_rr *rr);

/**
 * returns the rdata field member counter.
 * \param[in] *rr rr to operate on
 * \param[in] nr the number of the rdf to return
 * \return ldns_rdf *
 */
ldns_rdf* ldns_rr_rdf(const ldns_rr *rr, size_t nr);

/**
 * returns the owner name of an rr structure.
 * \param[in] *rr rr to operate on
 * \return ldns_rdf *
 */
ldns_rdf* ldns_rr_owner(const ldns_rr *rr);

/**
 * returns the question flag of an rr structure.
 * \param[in] *rr rr to operate on
 * \return bool true if question
 */
bool ldns_rr_is_question(const ldns_rr *rr);

/**
 * returns the ttl of an rr structure.
 * \param[in] *rr the rr to read from
 * \return the ttl of the rr
 */
uint32_t ldns_rr_ttl(const ldns_rr *rr);

/**
 * returns the rd_count of an rr structure.
 * \param[in] *rr the rr to read from
 * \return the rd count of the rr
 */
size_t ldns_rr_rd_count(const ldns_rr *rr);

/**
 * returns the type of the rr.
 * \param[in] *rr the rr to read from
 * \return the type of the rr
 */
ldns_rr_type ldns_rr_get_type(const ldns_rr *rr);

/**
 * returns the class of the rr.
 * \param[in] *rr the rr to read from
 * \return the class of the rr
 */
ldns_rr_class ldns_rr_get_class(const ldns_rr *rr);

/* rr_lists */

/**
 * returns the number of rr's in an rr_list.
 * \param[in] rr_list  the rr_list to read from
 * \return the number of rr's
 */
size_t ldns_rr_list_rr_count(const ldns_rr_list *rr_list);

/**
 * sets the number of rr's in an rr_list.
 * \param[in] rr_list the rr_list to set the count on
 * \param[in] count the number of rr in this list
 * \return void
 */
void ldns_rr_list_set_rr_count(ldns_rr_list *rr_list, size_t count);

/**
 * set a rr on a specific index in a ldns_rr_list
 * \param[in] rr_list the rr_list to use
 * \param[in] r the rr to set
 * \param[in] count index into the rr_list
 * \return the old rr which was stored in the rr_list, or
 * NULL is the index was too large
 * set a specific rr */
ldns_rr * ldns_rr_list_set_rr(ldns_rr_list *rr_list, const ldns_rr *r, size_t count);

/**
 * returns a specific rr of an rrlist.
 * \param[in] rr_list the rr_list to read from
 * \param[in] nr return this rr
 * \return the rr at position nr
 */
ldns_rr* ldns_rr_list_rr(const ldns_rr_list *rr_list, size_t nr);

/**
 * creates a new rr_list structure.
 * \return a new rr_list structure
 */
ldns_rr_list* ldns_rr_list_new(void);

/**
 * frees an rr_list structure.
 * \param[in] rr_list the list to free
 */
void ldns_rr_list_free(ldns_rr_list *rr_list);

/**
 * frees an rr_list structure and all rrs contained therein.
 * \param[in] rr_list the list to free
 */
void ldns_rr_list_deep_free(ldns_rr_list *rr_list);

/**
 * concatenates two ldns_rr_lists together. This modifies
 * *left (to extend it and add the pointers from *right).
 * \param[in] left the leftside
 * \param[in] right the rightside
 * \return a left with right concatenated to it
 */
bool ldns_rr_list_cat(ldns_rr_list *left, const ldns_rr_list *right);

/**
 * concatenates two ldns_rr_lists together, but makes clones of the rr's 
 * (instead of pointer copying).
 * \param[in] left the leftside
 * \param[in] right the rightside
 * \return a new rr_list with leftside/rightside concatenated
 */
ldns_rr_list* ldns_rr_list_cat_clone(const ldns_rr_list *left, const ldns_rr_list *right);

/**
 * pushes an rr to an rrlist.
 * \param[in] rr_list the rr_list to push to 
 * \param[in] rr the rr to push 
 * \return false on error, otherwise true
 */
bool ldns_rr_list_push_rr(ldns_rr_list *rr_list, const ldns_rr *rr);

/**
 * pushes an rr_list to an rrlist.
 * \param[in] rr_list the rr_list to push to 
 * \param[in] push_list the rr_list to push 
 * \return false on error, otherwise true
 */
bool ldns_rr_list_push_rr_list(ldns_rr_list *rr_list, const ldns_rr_list *push_list);

/**
 * pops the last rr from an rrlist.
 * \param[in] rr_list the rr_list to pop from
 * \return NULL if nothing to pop. Otherwise the popped RR
 */
ldns_rr* ldns_rr_list_pop_rr(ldns_rr_list *rr_list);

/**
 * pops an  rr_list of size s from an rrlist.
 * \param[in] rr_list the rr_list to pop from
 * \param[in] size the number of rr's to pop 
 * \return NULL if nothing to pop. Otherwise the popped rr_list
 */
ldns_rr_list* ldns_rr_list_pop_rr_list(ldns_rr_list *rr_list, size_t size);

/**
 * returns true if the given rr is one of the rrs in the
 * list, or if it is equal to one
 * \param[in] rr_list the rr_list to check
 * \param[in] rr the rr to check
 * \return true if rr_list contains rr, false otherwise
 */
bool ldns_rr_list_contains_rr(const ldns_rr_list *rr_list, const ldns_rr *rr); 

/**
 * checks if an rr_list is a rrset.
 * \param[in] rr_list the rr_list to check
 * \return true if it is an rrset otherwise false
 */
bool ldns_is_rrset(const ldns_rr_list *rr_list);

/**
 * pushes an rr to an rrset (which really are rr_list's).
 * \param[in] *rr_list the rrset to push the rr to
 * \param[in] *rr the rr to push
 * \return true if the push succeeded otherwise false
 */
bool ldns_rr_set_push_rr(ldns_rr_list *rr_list, ldns_rr *rr);

/**
 * pops the last rr from an rrset. This function is there only
 * for the symmetry.
 * \param[in] rr_list the rr_list to pop from
 * \return NULL if nothing to pop. Otherwise the popped RR
 *
 */
ldns_rr* ldns_rr_set_pop_rr(ldns_rr_list *rr_list);

/**
 * pops the first rrset from the list,
 * the list must be sorted, so that all rr's from each rrset
 * are next to each other
 */
ldns_rr_list *ldns_rr_list_pop_rrset(ldns_rr_list *rr_list);


/**
 * retrieves a rrtype by looking up its name.
 * \param[in] name a string with the name
 * \return the type which corresponds with the name
 */
ldns_rr_type ldns_get_rr_type_by_name(const char *name);

/**
 * retrieves a class by looking up its name.
 * \param[in] name string with the name
 * \return the cass which corresponds with the name
 */
ldns_rr_class ldns_get_rr_class_by_name(const char *name);

/**
 * clones a rr and all its data
 * \param[in] rr the rr to clone
 * \return the new rr or NULL on failure
 */
ldns_rr* ldns_rr_clone(const ldns_rr *rr);

/**
 * clones an rrlist.
 * \param[in] rrlist the rrlist to clone
 * \return the cloned rr list
 */
ldns_rr_list* ldns_rr_list_clone(const ldns_rr_list *rrlist);

/**
 * sorts an rr_list (canonical wire format). the sorting is done inband.
 * \param[in] unsorted the rr_list to be sorted
 * \return void
 */
void ldns_rr_list_sort(ldns_rr_list *unsorted);

/**
 * compares two rrs. The TTL is not looked at.
 * \param[in] rr1 the first one
 * \param[in] rr2 the second one
 * \return 0 if equal
 *         -1 if rr1 comes before rr2
 *         +1 if rr2 comes before rr1
 */
int ldns_rr_compare(const ldns_rr *rr1, const ldns_rr *rr2);

/**
 * compares two rrs, up to the rdata.
 * \param[in] rr1 the first one
 * \param[in] rr2 the second one
 * \return 0 if equal
 *         -1 if rr1 comes before rr2
 *         +1 if rr2 comes before rr1
 */
int ldns_rr_compare_no_rdata(const ldns_rr *rr1, const ldns_rr *rr2);

/**
 * compares the wireformat of two rrs, contained in the given buffers.
 * \param[in] rr1_buf the first one
 * \param[in] rr2_buf the second one
 * \return 0 if equal
 *         -1 if rr1_buf comes before rr2_buf
 *         +1 if rr2_buf comes before rr1_buf
 */
int ldns_rr_compare_wire(const ldns_buffer *rr1_buf, const ldns_buffer *rr2_buf);

/**
 * returns true of the given rr's are equal.
 * Also returns true if one record is a DS that represents the
 * same DNSKEY record as the other record
 * \param[in] rr1 the first rr
 * \param[in] rr2 the second rr
 * \return true if equal otherwise false
 */
bool ldns_rr_compare_ds(const ldns_rr *rr1, const ldns_rr *rr2);

/**
 * compares two rr listss.
 * \param[in] rrl1 the first one
 * \param[in] rrl2 the second one
 * \return 0 if equal
 *         -1 if rrl1 comes before rrl2
 *         +1 if rrl2 comes before rrl1
 */
int ldns_rr_list_compare(const ldns_rr_list *rrl1, const ldns_rr_list *rrl2);

/** 
 * calculates the uncompressed size of an RR.
 * \param[in] r the rr to operate on
 * \return size of the rr
 */
size_t ldns_rr_uncompressed_size(const ldns_rr *r);

/** 
 * converts each dname in a rr to its canonical form.
 * \param[in] rr the rr to work on
 * \return void
 */
void ldns_rr2canonical(ldns_rr *rr);

/** 
 * converts each dname in each rr in a rr_list to its canonical form.
 * \param[in] rr_list the rr_list to work on
 * \return void
 */
void ldns_rr_list2canonical(const ldns_rr_list *rr_list);

/** 
 * counts the number of labels of the ownername.
 * \param[in] rr count the labels of this rr
 * \return the number of labels
 */
uint8_t ldns_rr_label_count(const ldns_rr *rr);

/**
 * returns the resource record descriptor for the given rr type.
 *
 * \param[in] type the type value of the rr type
 *\return the ldns_rr_descriptor for this type
 */
const ldns_rr_descriptor *ldns_rr_descript(uint16_t type);

/**
 * returns the minimum number of rdata fields of the rr type this descriptor describes.
 *
 * \param[in]  descriptor for an rr type
 * \return the minimum number of rdata fields
 */
size_t ldns_rr_descriptor_minimum(const ldns_rr_descriptor *descriptor);

/**
 * returns the maximum number of rdata fields of the rr type this descriptor describes.
 *
 * \param[in]  descriptor for an rr type
 * \return the maximum number of rdata fields
 */
size_t ldns_rr_descriptor_maximum(const ldns_rr_descriptor *descriptor);

/**
 * returns the rdf type for the given rdata field number of the rr type for the given descriptor.
 *
 * \param[in] descriptor for an rr type
 * \param[in] field the field number
 * \return the rdf type for the field
 */
ldns_rdf_type ldns_rr_descriptor_field_type(const ldns_rr_descriptor *descriptor, size_t field);

/**
 * Return the rr_list which matches the rdf at position field. Think
 * type-covered stuff for RRSIG
 * 
 * \param[in] l the rr_list to look in
 * \param[in] r the rdf to use for the comparison
 * \param[in] pos at which position can we find the rdf
 * 
 * \return a new rr list with only the RRs that match 
 *
 */
ldns_rr_list *ldns_rr_list_subtype_by_rdf(const ldns_rr_list *l, const ldns_rdf *r, size_t pos);

/**
 * convert an rdf of type LDNS_RDF_TYPE_TYPE to an actual
 * LDNS_RR_TYPE. This is useful in the case when inspecting
 * the rrtype covered field of an RRSIG.
 * \param[in] rd the rdf to look at
 * \return a ldns_rr_type with equivalent LDNS_RR_TYPE
 *
 */
ldns_rr_type    ldns_rdf2rr_type(const ldns_rdf *rd);

/**
 * Returns the type of the first element of the RR
 * If there are no elements present, 0 is returned
 * 
 * \param[in] rr_list The rr list
 * \return rr_type of the first element, or 0 if the list is empty
 */
ldns_rr_type
ldns_rr_list_type(const ldns_rr_list *rr_list);

/**
 * Returns the owner domain name rdf of the first element of the RR
 * If there are no elements present, NULL is returned
 * 
 * \param[in] rr_list The rr list
 * \return dname of the first element, or NULL if the list is empty
 */
ldns_rdf *
ldns_rr_list_owner(const ldns_rr_list *rr_list);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_RR_H */
