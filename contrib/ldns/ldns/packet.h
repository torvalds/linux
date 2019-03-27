/*
 * packet.h
 *
 * DNS packet definitions
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
 * Contains the definition of ldns_pkt and its parts, as well
 * as functions to manipulate those.
 */


#ifndef LDNS_PACKET_H
#define LDNS_PACKET_H

#define LDNS_MAX_PACKETLEN         65535

/* allow flags to be given to ldns_pkt_query_new */
#define LDNS_QR		1       /* Query Response flag */
#define LDNS_AA		2       /* Authoritative Answer - server flag */
#define LDNS_TC		4       /* TrunCated - server flag */
#define LDNS_RD		8       /* Recursion Desired - query flag */
#define LDNS_CD		16      /* Checking Disabled - query flag */
#define LDNS_RA		32      /* Recursion Available - server flag */
#define LDNS_AD		64      /* Authenticated Data - server flag */

#include <ldns/error.h>
#include <ldns/common.h>
#include <ldns/rr.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* opcodes for pkt's */
enum ldns_enum_pkt_opcode {
	LDNS_PACKET_QUERY = 0,
	LDNS_PACKET_IQUERY = 1,
	LDNS_PACKET_STATUS = 2, /* there is no 3?? DNS is weird */
	LDNS_PACKET_NOTIFY = 4,
	LDNS_PACKET_UPDATE = 5
};
typedef enum ldns_enum_pkt_opcode ldns_pkt_opcode;

/* rcodes for pkts */
enum ldns_enum_pkt_rcode {
	LDNS_RCODE_NOERROR = 0,
	LDNS_RCODE_FORMERR = 1,
	LDNS_RCODE_SERVFAIL = 2,
	LDNS_RCODE_NXDOMAIN = 3,
	LDNS_RCODE_NOTIMPL = 4,
	LDNS_RCODE_REFUSED = 5,
	LDNS_RCODE_YXDOMAIN = 6,
	LDNS_RCODE_YXRRSET = 7,
	LDNS_RCODE_NXRRSET = 8,
	LDNS_RCODE_NOTAUTH = 9,
	LDNS_RCODE_NOTZONE = 10
};
typedef enum ldns_enum_pkt_rcode ldns_pkt_rcode;

/**
 *  Header of a dns packet
 *
 * Contains the information about the packet itself, as specified in RFC1035
<pre>
4.1.1. Header section format

The header contains the following fields:

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      ID                       |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    QDCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ANCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    NSCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ARCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

where:

ID              A 16 bit identifier assigned by the program that
                generates any kind of query.  This identifier is copied
                the corresponding reply and can be used by the requester
                to match up replies to outstanding queries.

QR              A one bit field that specifies whether this message is a
                query (0), or a response (1).

OPCODE          A four bit field that specifies kind of query in this
                message.  This value is set by the originator of a query
                and copied into the response.  The values are:

                0               a standard query (QUERY)

                1               an inverse query (IQUERY)

                2               a server status request (STATUS)

                3-15            reserved for future use

AA              Authoritative Answer - this bit is valid in responses,
                and specifies that the responding name server is an
                authority for the domain name in question section.

                Note that the contents of the answer section may have
                multiple owner names because of aliases.  The AA bit

                corresponds to the name which matches the query name, or
                the first owner name in the answer section.

TC              TrunCation - specifies that this message was truncated
                due to length greater than that permitted on the
                transmission channel.

RD              Recursion Desired - this bit may be set in a query and
                is copied into the response.  If RD is set, it directs
                the name server to pursue the query recursively.
                Recursive query support is optional.

RA              Recursion Available - this be is set or cleared in a
                response, and denotes whether recursive query support is
                available in the name server.

Z               Reserved for future use.  Must be zero in all queries
                and responses.

RCODE           Response code - this 4 bit field is set as part of
                responses.  The values have the following
                interpretation:

                0               No error condition

                1               Format error - The name server was
                                unable to interpret the query.

                2               Server failure - The name server was
                                unable to process this query due to a
                                problem with the name server.

                3               Name Error - Meaningful only for
                                responses from an authoritative name
                                server, this code signifies that the
                                domain name referenced in the query does
                                not exist.

                4               Not Implemented - The name server does
                                not support the requested kind of query.

                5               Refused - The name server refuses to
                                perform the specified operation for
                                policy reasons.  For example, a name
                                server may not wish to provide the
                                information to the particular requester,
                                or a name server may not wish to perform
                                a particular operation (e.g., zone

                                transfer) for particular data.

                6-15            Reserved for future use.

QDCOUNT         an unsigned 16 bit integer specifying the number of
                entries in the question section.

ANCOUNT         an unsigned 16 bit integer specifying the number of
                resource records in the answer section.

NSCOUNT         an unsigned 16 bit integer specifying the number of name
                server resource records in the authority records
                section.

ARCOUNT         an unsigned 16 bit integer specifying the number of
                resource records in the additional records section.

</pre>
 */
struct ldns_struct_hdr
{
	/**  Id of a packet */
	uint16_t _id;
	/**  Query bit (0=query, 1=answer) */
	bool _qr;
	/**  Authoritative answer */
	bool _aa;
	/**  Packet truncated */
	bool _tc;
	/**  Recursion desired */
	bool _rd;
	/**  Checking disabled */
	bool _cd;
	/**  Recursion available */
	bool _ra;
	/**  Authentic data */
	bool _ad;
	/**  Query type */
	ldns_pkt_opcode _opcode;	 /* XXX 8 bits? */
	/**  Response code */
	uint8_t _rcode;
	/**  question sec */
	uint16_t _qdcount;
	/**  answer sec */
	uint16_t _ancount;
	/**  auth sec */
	uint16_t _nscount;
	/**  add sec */
	uint16_t _arcount;
};
typedef struct ldns_struct_hdr ldns_hdr;

/**
 * DNS packet
 *
 * This structure contains a complete DNS packet (either a query or an answer)
 *
 * It is the complete representation of what you actually send to a
 * nameserver, and what it sends back (assuming you are the client here).
 */
struct ldns_struct_pkt
{
	/** Header section */
	ldns_hdr *_header;
	/* extra items needed in a packet */
	/** an rdf (A or AAAA) with the IP address of the server it is from */
	ldns_rdf *_answerfrom;
        /** Timestamp of the time the packet was sent or created */
	struct timeval timestamp;
	/** The duration of the query this packet is an answer to */
	uint32_t _querytime;
	/** The size of the wire format of the packet in octets */
	size_t _size;
	/** Optional tsig rr */
	ldns_rr *_tsig_rr;
	/** EDNS0 available buffer size, see RFC2671 */
	uint16_t _edns_udp_size;
	/** EDNS0 Extended rcode */
	uint8_t _edns_extended_rcode;
	/** EDNS Version */
	uint8_t _edns_version;
        /* OPT pseudo-RR presence flag */
        uint8_t _edns_present;
	/** Reserved EDNS data bits */
	uint16_t _edns_z;
	/** Arbitrary EDNS rdata */
	ldns_rdf *_edns_data;
	/**  Question section */
	ldns_rr_list	*_question;
	/**  Answer section */
	ldns_rr_list	*_answer;
	/**  Authority section */
	ldns_rr_list	*_authority;
	/**  Additional section */
	ldns_rr_list	*_additional;
};
typedef struct ldns_struct_pkt ldns_pkt;

/**
 * The sections of a packet
 */
enum ldns_enum_pkt_section {
	LDNS_SECTION_QUESTION = 0,
	LDNS_SECTION_ANSWER = 1,
	LDNS_SECTION_AUTHORITY = 2,
	LDNS_SECTION_ADDITIONAL = 3,
	/** bogus section, if not interested */
	LDNS_SECTION_ANY = 4,
	/** used to get all non-question rrs from a packet */
	LDNS_SECTION_ANY_NOQUESTION = 5
};
typedef enum ldns_enum_pkt_section ldns_pkt_section;	

/**
 * The different types of packets
 */
enum ldns_enum_pkt_type {
	LDNS_PACKET_QUESTION,
	LDNS_PACKET_REFERRAL,
	LDNS_PACKET_ANSWER,
	LDNS_PACKET_NXDOMAIN,
	LDNS_PACKET_NODATA,
	LDNS_PACKET_UNKNOWN
};
typedef enum ldns_enum_pkt_type ldns_pkt_type;

/* prototypes */

/* read */

/**
 * Read the packet id
 * \param[in] p the packet
 * \return the packet id
 */
uint16_t ldns_pkt_id(const ldns_pkt *p);
/**
 * Read the packet's qr bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_qr(const ldns_pkt *p);
/**
 * Read the packet's aa bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_aa(const ldns_pkt *p);
/**
 * Read the packet's tc bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_tc(const ldns_pkt *p);
/**
 * Read the packet's rd bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_rd(const ldns_pkt *p);
/**
 * Read the packet's cd bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_cd(const ldns_pkt *p);
/**
 * Read the packet's ra bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_ra(const ldns_pkt *p);
/**
 * Read the packet's ad bit
 * \param[in] p the packet
 * \return value of the bit
 */
bool ldns_pkt_ad(const ldns_pkt *p);
/**
 * Read the packet's code
 * \param[in] p the packet
 * \return the opcode
 */
ldns_pkt_opcode ldns_pkt_get_opcode(const ldns_pkt *p);
/**
 * Return the packet's respons code
 * \param[in] p the packet
 * \return the respons code
 */
ldns_pkt_rcode ldns_pkt_get_rcode(const ldns_pkt *p);
/**
 * Return the packet's qd count 
 * \param[in] p the packet
 * \return the qd count
 */
uint16_t ldns_pkt_qdcount(const ldns_pkt *p);
/**
 * Return the packet's an count
 * \param[in] p the packet
 * \return the an count
 */
uint16_t ldns_pkt_ancount(const ldns_pkt *p);
/**
 * Return the packet's ns count
 * \param[in] p the packet
 * \return the ns count
 */
uint16_t ldns_pkt_nscount(const ldns_pkt *p);
/**
 * Return the packet's ar count
 * \param[in] p the packet
 * \return the ar count
 */
uint16_t ldns_pkt_arcount(const ldns_pkt *p);

/** 
 * Return the packet's answerfrom
 * \param[in] p packet
 * \return the name of the server
 */
ldns_rdf *ldns_pkt_answerfrom(const ldns_pkt *p);

/**
 * Return the packet's timestamp
 * \param[in] p the packet
 * \return the timestamp
 */
struct timeval ldns_pkt_timestamp(const ldns_pkt *p);
/**
 * Return the packet's querytime
 * \param[in] p the packet
 * \return the querytime
 */
uint32_t ldns_pkt_querytime(const ldns_pkt *p);

/**
 * Return the packet's size in bytes
 * \param[in] p the packet
 * \return the size
 */
size_t ldns_pkt_size(const ldns_pkt *p);

/**
 * Return the number of RRs in the given section.
 * Returns the sum of all RRs when LDNS_SECTION_ANY is given.
 * Returns the sum of all non-question RRs when LDNS_SECTION_ANY_NOQUESTION
 * is given.
 * \param[in] p the packet
 * \param[in] s the section
 * \return the number of RRs in the given section
 */
uint16_t ldns_pkt_section_count(const ldns_pkt *p, ldns_pkt_section s);

/**
 * Return the packet's tsig pseudo rr's
 * \param[in] p the packet
 * \return the tsig rr
 */
ldns_rr *ldns_pkt_tsig(const ldns_pkt *p);

/**
 * Return the packet's question section
 * \param[in] p the packet
 * \return the section
 */
ldns_rr_list *ldns_pkt_question(const ldns_pkt *p);
/**
 * Return the packet's answer section
 * \param[in] p the packet
 * \return the section
 */
ldns_rr_list *ldns_pkt_answer(const ldns_pkt *p);
/**
 * Return the packet's authority section
 * \param[in] p the packet
 * \return the section
 */
ldns_rr_list *ldns_pkt_authority(const ldns_pkt *p);
/**
 * Return the packet's additional section
 * \param[in] p the packet
 * \return the section
 */
ldns_rr_list *ldns_pkt_additional(const ldns_pkt *p);
/**
 * Return the packet's question, answer, authority and additional sections
 * concatenated, in a new rr_list clone.
 * \param[in] p the packet
 * \return the rrs
 */
ldns_rr_list *ldns_pkt_all(const ldns_pkt *p);
/**
 * Return the packet's answer, authority and additional sections concatenated, 
 * in a new rr_list clone.  Like ldns_pkt_all but without the questions.
 * \param[in] p the packet
 * \return the rrs except the question rrs
 */
ldns_rr_list *ldns_pkt_all_noquestion(const ldns_pkt *p);

/**
 * return all the rr_list's in the packet. Clone the lists, instead
 * of returning pointers. 
 * \param[in] p the packet to look in
 * \param[in] s what section(s) to return
 * \return ldns_rr_list with the rr's or NULL if none were found
 */
ldns_rr_list *ldns_pkt_get_section_clone(const ldns_pkt *p, ldns_pkt_section s);

/**
 * return all the rr with a specific name from a packet. Optionally
 * specify from which section in the packet
 * \param[in] p the packet
 * \param[in] r the name
 * \param[in] s the packet's section
 * \return a list with the rr's or NULL if none were found
 */
ldns_rr_list *ldns_pkt_rr_list_by_name(const ldns_pkt *p, const ldns_rdf *r, ldns_pkt_section s);
/**
 * return all the rr with a specific type from a packet. Optionally
 * specify from which section in the packet
 * \param[in] p the packet
 * \param[in] t the type
 * \param[in] s the packet's section
 * \return a list with the rr's or NULL if none were found
 */
ldns_rr_list *ldns_pkt_rr_list_by_type(const ldns_pkt *p, ldns_rr_type t, ldns_pkt_section s);
/**
 * return all the rr with a specific type and type from a packet. Optionally
 * specify from which section in the packet
 * \param[in] packet the packet
 * \param[in] ownername the name
 * \param[in] type the type
 * \param[in] sec the packet's section
 * \return a list with the rr's or NULL if none were found
 */
ldns_rr_list *ldns_pkt_rr_list_by_name_and_type(const ldns_pkt *packet, const ldns_rdf *ownername, ldns_rr_type type, ldns_pkt_section sec);


/**
 * check to see if an rr exist in the packet
 * \param[in] pkt the packet to examine
 * \param[in] sec in which section to look
 * \param[in] rr the rr to look for
 */
bool ldns_pkt_rr(const ldns_pkt *pkt, ldns_pkt_section sec, const ldns_rr *rr);


/**
 * sets the flags in a packet.
 * \param[in] pkt the packet to operate on
 * \param[in] flags ORed values: LDNS_QR| LDNS_AR for instance
 * \return true on success otherwise false
 */
bool ldns_pkt_set_flags(ldns_pkt *pkt, uint16_t flags);

/**
 * Set the packet's id
 * \param[in] p the packet
 * \param[in] id the id to set
 */
void ldns_pkt_set_id(ldns_pkt *p, uint16_t id);
/**
 * Set the packet's id to a random value
 * \param[in] p the packet
 */
void ldns_pkt_set_random_id(ldns_pkt *p);
/**
 * Set the packet's qr bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_qr(ldns_pkt *p, bool b);
/**
 * Set the packet's aa bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_aa(ldns_pkt *p, bool b);
/**
 * Set the packet's tc bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_tc(ldns_pkt *p, bool b);
/**
 * Set the packet's rd bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_rd(ldns_pkt *p, bool b);
/**
 * Set the packet's cd bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_cd(ldns_pkt *p, bool b);
/**
 * Set the packet's ra bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_ra(ldns_pkt *p, bool b);
/**
 * Set the packet's ad bit
 * \param[in] p the packet
 * \param[in] b the value to set (boolean)
 */
void ldns_pkt_set_ad(ldns_pkt *p, bool b);

/**
 * Set the packet's opcode
 * \param[in] p the packet
 * \param[in] c the opcode
 */
void ldns_pkt_set_opcode(ldns_pkt *p, ldns_pkt_opcode c);
/**
 * Set the packet's respons code
 * \param[in] p the packet
 * \param[in] c the rcode
 */
void ldns_pkt_set_rcode(ldns_pkt *p, uint8_t c);
/**
 * Set the packet's qd count
 * \param[in] p the packet
 * \param[in] c the count
 */
void ldns_pkt_set_qdcount(ldns_pkt *p, uint16_t c);
/**
 * Set the packet's an count
 * \param[in] p the packet
 * \param[in] c the count
 */
void ldns_pkt_set_ancount(ldns_pkt *p, uint16_t c);
/**
 * Set the packet's ns count
 * \param[in] p the packet
 * \param[in] c the count
 */
void ldns_pkt_set_nscount(ldns_pkt *p, uint16_t c);
/**
 * Set the packet's arcount
 * \param[in] p the packet
 * \param[in] c the count
 */
void ldns_pkt_set_arcount(ldns_pkt *p, uint16_t c);
/**
 * Set the packet's answering server
 * \param[in] p the packet
 * \param[in] r the address
 */
void ldns_pkt_set_answerfrom(ldns_pkt *p, ldns_rdf *r);
/**
 * Set the packet's query time
 * \param[in] p the packet
 * \param[in] t the querytime in msec
 */
void ldns_pkt_set_querytime(ldns_pkt *p, uint32_t t);
/**
 * Set the packet's size
 * \param[in] p the packet
 * \param[in] s the size
 */
void ldns_pkt_set_size(ldns_pkt *p, size_t s);

/**
 * Set the packet's timestamp
 * \param[in] p the packet
 * \param[in] timeval the timestamp
 */
void ldns_pkt_set_timestamp(ldns_pkt *p, struct timeval timeval);
/**
 * Set a packet's section count to x
 * \param[in] p the packet
 * \param[in] s the section
 * \param[in] x the section count
 */
void ldns_pkt_set_section_count(ldns_pkt *p, ldns_pkt_section s, uint16_t x);
/**
 * Set the packet's tsig rr
 * \param[in] p the packet
 * \param[in] t the tsig rr
 */
void ldns_pkt_set_tsig(ldns_pkt *p, ldns_rr *t);

/**
 * looks inside the packet to determine
 * what kind of packet it is, AUTH, NXDOMAIN, REFERRAL, etc.
 * \param[in] p the packet to examine
 * \return the type of packet
 */
ldns_pkt_type ldns_pkt_reply_type(const ldns_pkt *p);

/**
 * return the packet's edns udp size
 * \param[in] packet the packet
 * \return the size
 */
uint16_t ldns_pkt_edns_udp_size(const ldns_pkt *packet);
/**
 * return the packet's edns extended rcode
 * \param[in] packet the packet
 * \return the rcode
 */
uint8_t ldns_pkt_edns_extended_rcode(const ldns_pkt *packet);
/**
 * return the packet's edns version
 * \param[in] packet the packet
 * \return the version
 */
uint8_t ldns_pkt_edns_version(const ldns_pkt *packet);
/**
 * return the packet's edns z value
 * \param[in] packet the packet
 * \return the z value
 */
uint16_t ldns_pkt_edns_z(const ldns_pkt *packet);
/**
 * return the packet's edns data
 * \param[in] packet the packet
 * \return the data
 */
ldns_rdf *ldns_pkt_edns_data(const ldns_pkt *packet);

/**
 * return the packet's edns do bit
 * \param[in] packet the packet
 * \return the bit's value
 */
bool ldns_pkt_edns_do(const ldns_pkt *packet);
/**
 * Set the packet's edns do bit
 * \param[in] packet the packet
 * \param[in] value the bit's new value
 */
void ldns_pkt_set_edns_do(ldns_pkt *packet, bool value);

/**
 * returns true if this packet needs and EDNS rr to be sent.
 * At the moment the only reason is an expected packet
 * size larger than 512 bytes, but for instance dnssec would
 * be a good reason too.
 *
 * \param[in] packet the packet to check
 * \return true if packet needs edns rr
 */
bool ldns_pkt_edns(const ldns_pkt *packet);

/**
 * Set the packet's edns udp size
 * \param[in] packet the packet
 * \param[in] s the size
 */
void ldns_pkt_set_edns_udp_size(ldns_pkt *packet, uint16_t s);
/**
 * Set the packet's edns extended rcode
 * \param[in] packet the packet
 * \param[in] c the code
 */
void ldns_pkt_set_edns_extended_rcode(ldns_pkt *packet, uint8_t c);
/**
 * Set the packet's edns version
 * \param[in] packet the packet
 * \param[in] v the version
 */
void ldns_pkt_set_edns_version(ldns_pkt *packet, uint8_t v);
/**
 * Set the packet's edns z value
 * \param[in] packet the packet
 * \param[in] z the value
 */
void ldns_pkt_set_edns_z(ldns_pkt *packet, uint16_t z);
/**
 * Set the packet's edns data
 * \param[in] packet the packet
 * \param[in] data the data
 */
void ldns_pkt_set_edns_data(ldns_pkt *packet, ldns_rdf *data);

/**
 * allocates and initializes a ldns_pkt structure.
 * \return pointer to the new packet
 */
ldns_pkt *ldns_pkt_new(void);

/**
 * frees the packet structure and all data that it contains.
 * \param[in] packet The packet structure to free
 * \return void
 */
void ldns_pkt_free(ldns_pkt *packet);

/**
 * creates a query packet for the given name, type, class.
 * \param[out] p the packet to be returned
 * \param[in] rr_name the name to query for (as string)
 * \param[in] rr_type the type to query for
 * \param[in] rr_class the class to query for
 * \param[in] flags packet flags
 * \return LDNS_STATUS_OK or a ldns_status mesg with the error
 */
ldns_status ldns_pkt_query_new_frm_str(ldns_pkt **p, const char *rr_name, ldns_rr_type rr_type, ldns_rr_class rr_class , uint16_t flags);

/**
 * creates an IXFR request packet for the given name, class.
 * adds the SOA record to the authority section.
 * \param[out] p the packet to be returned
 * \param[in] rr_name the name to query for (as string)
 * \param[in] rr_class the class to query for
 * \param[in] flags packet flags
 * \param[in] soa soa record to be added to the authority section (not copied).
 * \return LDNS_STATUS_OK or a ldns_status mesg with the error
 */
ldns_status ldns_pkt_ixfr_request_new_frm_str(ldns_pkt **p, const char *rr_name, ldns_rr_class rr_class, uint16_t flags, ldns_rr* soa);

/**
 * creates a packet with a query in it for the given name, type and class.
 * \param[in] rr_name the name to query for (not copied).
 *            The returned packet will take ownership of rr_name, so the caller should not free it.
 * \param[in] rr_type the type to query for
 * \param[in] rr_class the class to query for
 * \param[in] flags packet flags
 * \return ldns_pkt* a pointer to the new pkt
 */
ldns_pkt *ldns_pkt_query_new(ldns_rdf *rr_name, ldns_rr_type rr_type, ldns_rr_class rr_class, uint16_t flags);

/**
 * creates an IXFR request packet for the given name, type and class.
 * adds the SOA record to the authority section.
 * \param[in] rr_name the name to query for (not copied).
 *            The returned packet will take ownership of rr_name, so the caller should not free it.
 * \param[in] rr_class the class to query for
 * \param[in] flags packet flags
 * \param[in] soa soa record to be added to the authority section (not copied).
 * \return ldns_pkt* a pointer to the new pkt
 */
ldns_pkt *ldns_pkt_ixfr_request_new(ldns_rdf *rr_name, ldns_rr_class rr_class, uint16_t flags, ldns_rr* soa);

/**
 * clones the given packet, creating a fully allocated copy
 *
 * \param[in] pkt the packet to clone
 * \return ldns_pkt* pointer to the new packet
 */
ldns_pkt *ldns_pkt_clone(const ldns_pkt *pkt);

/**
 * directly set the additional section
 * \param[in] p packet to operate on
 * \param[in] rr rrlist to set
 */
void ldns_pkt_set_additional(ldns_pkt *p, ldns_rr_list *rr);

/**
 * directly set the answer section
 * \param[in] p packet to operate on
 * \param[in] rr rrlist to set
 */
void ldns_pkt_set_answer(ldns_pkt *p, ldns_rr_list *rr);

/**
 * directly set the question section
 * \param[in] p packet to operate on
 * \param[in] rr rrlist to set
 */
void ldns_pkt_set_question(ldns_pkt *p, ldns_rr_list *rr);

/**
 * directly set the auhority section
 * \param[in] p packet to operate on
 * \param[in] rr rrlist to set
 */
void ldns_pkt_set_authority(ldns_pkt *p, ldns_rr_list *rr);

/**
 * push an rr on a packet
 * \param[in] packet packet to operate on
 * \param[in] section where to put it
 * \param[in] rr rr to push
 * \return a boolean which is true when the rr was added
 */
bool ldns_pkt_push_rr(ldns_pkt *packet, ldns_pkt_section section, ldns_rr *rr);

/**
 * push an rr on a packet, provided the RR is not there.
 * \param[in] pkt packet to operate on
 * \param[in] sec where to put it
 * \param[in] rr rr to push
 * \return a boolean which is true when the rr was added
 */
bool ldns_pkt_safe_push_rr(ldns_pkt *pkt, ldns_pkt_section sec, ldns_rr *rr);

/**
 * push a rr_list on a packet
 * \param[in] packet packet to operate on
 * \param[in] section where to put it
 * \param[in] list the rr_list to push
 * \return a boolean which is true when the rr was added
 */
bool ldns_pkt_push_rr_list(ldns_pkt *packet, ldns_pkt_section section, ldns_rr_list *list);

/**
 * push an rr_list to a packet, provided the RRs are not already there.
 * \param[in] pkt packet to operate on
 * \param[in] sec where to put it
 * \param[in] list the rr_list to push
 * \return a boolean which is true when the rr was added
 */
bool ldns_pkt_safe_push_rr_list(ldns_pkt *pkt, ldns_pkt_section sec, ldns_rr_list *list);

/**
 * check if a packet is empty
 * \param[in] p packet
 * \return true: empty, false: not empty
 */
bool ldns_pkt_empty(ldns_pkt *p);

#ifdef __cplusplus
}
#endif

#endif  /* LDNS_PACKET_H */
