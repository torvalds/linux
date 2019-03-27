/*
 * Copyright (c) 1998-2011 The TCPDUMP project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

/* \summary: Resource Public Key Infrastructure (RPKI) to Router Protocol printer */

/* specification: RFC 6810 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"

static const char tstr[] = "[|RPKI-RTR]";

/*
 * RPKI/Router PDU header
 *
 * Here's what the PDU header looks like.
 * The length does include the version and length fields.
 */
typedef struct rpki_rtr_pdu_ {
    u_char version;		/* Version number */
    u_char pdu_type;		/* PDU type */
    union {
	u_char session_id[2];	/* Session id */
	u_char error_code[2];	/* Error code */
    } u;
    u_char length[4];
} rpki_rtr_pdu;
#define RPKI_RTR_PDU_OVERHEAD (offsetof(rpki_rtr_pdu, rpki_rtr_pdu_msg))

/*
 * IPv4 Prefix PDU.
 */
typedef struct rpki_rtr_pdu_ipv4_prefix_ {
    rpki_rtr_pdu pdu_header;
    u_char flags;
    u_char prefix_length;
    u_char max_length;
    u_char zero;
    u_char prefix[4];
    u_char as[4];
} rpki_rtr_pdu_ipv4_prefix;

/*
 * IPv6 Prefix PDU.
 */
typedef struct rpki_rtr_pdu_ipv6_prefix_ {
    rpki_rtr_pdu pdu_header;
    u_char flags;
    u_char prefix_length;
    u_char max_length;
    u_char zero;
    u_char prefix[16];
    u_char as[4];
} rpki_rtr_pdu_ipv6_prefix;

/*
 * Error report PDU.
 */
typedef struct rpki_rtr_pdu_error_report_ {
    rpki_rtr_pdu pdu_header;
    u_char encapsulated_pdu_length[4]; /* Encapsulated PDU length */
    /* Copy of Erroneous PDU (variable, optional) */
    /* Length of Error Text (4 octets in network byte order) */
    /* Arbitrary Text of Error Diagnostic Message (variable, optional) */
} rpki_rtr_pdu_error_report;

/*
 * PDU type codes
 */
#define RPKI_RTR_SERIAL_NOTIFY_PDU	0
#define RPKI_RTR_SERIAL_QUERY_PDU	1
#define RPKI_RTR_RESET_QUERY_PDU	2
#define RPKI_RTR_CACHE_RESPONSE_PDU	3
#define RPKI_RTR_IPV4_PREFIX_PDU	4
#define RPKI_RTR_IPV6_PREFIX_PDU	6
#define RPKI_RTR_END_OF_DATA_PDU	7
#define RPKI_RTR_CACHE_RESET_PDU	8
#define RPKI_RTR_ERROR_REPORT_PDU	10

static const struct tok rpki_rtr_pdu_values[] = {
    { RPKI_RTR_SERIAL_NOTIFY_PDU, "Serial Notify" },
    { RPKI_RTR_SERIAL_QUERY_PDU, "Serial Query" },
    { RPKI_RTR_RESET_QUERY_PDU, "Reset Query" },
    { RPKI_RTR_CACHE_RESPONSE_PDU, "Cache Response" },
    { RPKI_RTR_IPV4_PREFIX_PDU, "IPV4 Prefix" },
    { RPKI_RTR_IPV6_PREFIX_PDU, "IPV6 Prefix" },
    { RPKI_RTR_END_OF_DATA_PDU, "End of Data" },
    { RPKI_RTR_CACHE_RESET_PDU, "Cache Reset" },
    { RPKI_RTR_ERROR_REPORT_PDU, "Error Report" },
    { 0, NULL}
};

static const struct tok rpki_rtr_error_codes[] = {
    { 0, "Corrupt Data" },
    { 1, "Internal Error" },
    { 2, "No Data Available" },
    { 3, "Invalid Request" },
    { 4, "Unsupported Protocol Version" },
    { 5, "Unsupported PDU Type" },
    { 6, "Withdrawal of Unknown Record" },
    { 7, "Duplicate Announcement Received" },
    { 0, NULL}
};

/*
 * Build a indentation string for a given indentation level.
 * XXX this should be really in util.c
 */
static char *
indent_string (u_int indent)
{
    static char buf[20];
    u_int idx;

    idx = 0;
    buf[idx] = '\0';

    /*
     * Does the static buffer fit ?
     */
    if (sizeof(buf) < ((indent/8) + (indent %8) + 2)) {
	return buf;
    }

    /*
     * Heading newline.
     */
    buf[idx] = '\n';
    idx++;

    while (indent >= 8) {
	buf[idx] = '\t';
	idx++;
	indent -= 8;
    }

    while (indent > 0) {
	buf[idx] = ' ';
	idx++;
	indent--;
    }

    /*
     * Trailing zero.
     */
    buf[idx] = '\0';

    return buf;
}

/*
 * Print a single PDU.
 */
static u_int
rpki_rtr_pdu_print (netdissect_options *ndo, const u_char *tptr, const u_int len,
	const u_char recurse, const u_int indent)
{
    const rpki_rtr_pdu *pdu_header;
    u_int pdu_type, pdu_len, hexdump;
    const u_char *msg;

    /* Protocol Version */
    ND_TCHECK_8BITS(tptr);
    if (*tptr != 0) {
	/* Skip the rest of the input buffer because even if this is
	 * a well-formed PDU of a future RPKI-Router protocol version
	 * followed by a well-formed PDU of RPKI-Router protocol
	 * version 0, there is no way to know exactly how to skip the
	 * current PDU.
	 */
	ND_PRINT((ndo, "%sRPKI-RTRv%u (unknown)", indent_string(8), *tptr));
	return len;
    }
    if (len < sizeof(rpki_rtr_pdu)) {
	ND_PRINT((ndo, "(%u bytes is too few to decode)", len));
	goto invalid;
    }
    ND_TCHECK2(*tptr, sizeof(rpki_rtr_pdu));
    pdu_header = (const rpki_rtr_pdu *)tptr;
    pdu_type = pdu_header->pdu_type;
    pdu_len = EXTRACT_32BITS(pdu_header->length);
    /* Do not check bounds with pdu_len yet, do it in the case blocks
     * below to make it possible to decode at least the beginning of
     * a truncated Error Report PDU or a truncated encapsulated PDU.
     */
    hexdump = FALSE;

    ND_PRINT((ndo, "%sRPKI-RTRv%u, %s PDU (%u), length: %u",
	   indent_string(8),
	   pdu_header->version,
	   tok2str(rpki_rtr_pdu_values, "Unknown", pdu_type),
	   pdu_type, pdu_len));
    if (pdu_len < sizeof(rpki_rtr_pdu) || pdu_len > len)
	goto invalid;

    switch (pdu_type) {

	/*
	 * The following PDUs share the message format.
	 */
    case RPKI_RTR_SERIAL_NOTIFY_PDU:
    case RPKI_RTR_SERIAL_QUERY_PDU:
    case RPKI_RTR_END_OF_DATA_PDU:
	if (pdu_len != sizeof(rpki_rtr_pdu) + 4)
	    goto invalid;
	ND_TCHECK2(*tptr, pdu_len);
        msg = (const u_char *)(pdu_header + 1);
	ND_PRINT((ndo, "%sSession ID: 0x%04x, Serial: %u",
	       indent_string(indent+2),
	       EXTRACT_16BITS(pdu_header->u.session_id),
	       EXTRACT_32BITS(msg)));
	break;

	/*
	 * The following PDUs share the message format.
	 */
    case RPKI_RTR_RESET_QUERY_PDU:
    case RPKI_RTR_CACHE_RESET_PDU:
	if (pdu_len != sizeof(rpki_rtr_pdu))
	    goto invalid;
	/* no additional boundary to check */

	/*
	 * Zero payload PDUs.
	 */
	break;

    case RPKI_RTR_CACHE_RESPONSE_PDU:
	if (pdu_len != sizeof(rpki_rtr_pdu))
	    goto invalid;
	/* no additional boundary to check */
	ND_PRINT((ndo, "%sSession ID: 0x%04x",
	       indent_string(indent+2),
	       EXTRACT_16BITS(pdu_header->u.session_id)));
	break;

    case RPKI_RTR_IPV4_PREFIX_PDU:
	{
	    const rpki_rtr_pdu_ipv4_prefix *pdu;

	    if (pdu_len != sizeof(rpki_rtr_pdu) + 12)
		goto invalid;
	    ND_TCHECK2(*tptr, pdu_len);
	    pdu = (const rpki_rtr_pdu_ipv4_prefix *)tptr;
	    ND_PRINT((ndo, "%sIPv4 Prefix %s/%u-%u, origin-as %u, flags 0x%02x",
		   indent_string(indent+2),
		   ipaddr_string(ndo, pdu->prefix),
		   pdu->prefix_length, pdu->max_length,
		   EXTRACT_32BITS(pdu->as), pdu->flags));
	}
	break;

    case RPKI_RTR_IPV6_PREFIX_PDU:
	{
	    const rpki_rtr_pdu_ipv6_prefix *pdu;

	    if (pdu_len != sizeof(rpki_rtr_pdu) + 24)
		goto invalid;
	    ND_TCHECK2(*tptr, pdu_len);
	    pdu = (const rpki_rtr_pdu_ipv6_prefix *)tptr;
	    ND_PRINT((ndo, "%sIPv6 Prefix %s/%u-%u, origin-as %u, flags 0x%02x",
		   indent_string(indent+2),
		   ip6addr_string(ndo, pdu->prefix),
		   pdu->prefix_length, pdu->max_length,
		   EXTRACT_32BITS(pdu->as), pdu->flags));
	}
	break;

    case RPKI_RTR_ERROR_REPORT_PDU:
	{
	    const rpki_rtr_pdu_error_report *pdu;
	    u_int encapsulated_pdu_length, text_length, tlen, error_code;

	    tlen = sizeof(rpki_rtr_pdu);
	    /* Do not test for the "Length of Error Text" data element yet. */
	    if (pdu_len < tlen + 4)
		goto invalid;
	    ND_TCHECK2(*tptr, tlen + 4);
	    /* Safe up to and including the "Length of Encapsulated PDU"
	     * data element, more data elements may be present.
	     */
	    pdu = (const rpki_rtr_pdu_error_report *)tptr;
	    encapsulated_pdu_length = EXTRACT_32BITS(pdu->encapsulated_pdu_length);
	    tlen += 4;

	    error_code = EXTRACT_16BITS(pdu->pdu_header.u.error_code);
	    ND_PRINT((ndo, "%sError code: %s (%u), Encapsulated PDU length: %u",
		   indent_string(indent+2),
		   tok2str(rpki_rtr_error_codes, "Unknown", error_code),
		   error_code, encapsulated_pdu_length));

	    if (encapsulated_pdu_length) {
		/* Section 5.10 of RFC 6810 says:
		 * "An Error Report PDU MUST NOT be sent for an Error Report PDU."
		 *
		 * However, as far as the protocol encoding goes Error Report PDUs can
		 * happen to be nested in each other, however many times, in which case
		 * the decoder should still print such semantically incorrect PDUs.
		 *
		 * That said, "the Erroneous PDU field MAY be truncated" (ibid), thus
		 * to keep things simple this implementation decodes only the two
		 * outermost layers of PDUs and makes bounds checks in the outer and
		 * the inner PDU independently.
		 */
		if (pdu_len < tlen + encapsulated_pdu_length)
		    goto invalid;
		if (! recurse) {
		    ND_TCHECK2(*tptr, tlen + encapsulated_pdu_length);
		}
		else {
		    ND_PRINT((ndo, "%s-----encapsulated PDU-----", indent_string(indent+4)));
		    rpki_rtr_pdu_print(ndo, tptr + tlen,
			encapsulated_pdu_length, 0, indent + 2);
		}
		tlen += encapsulated_pdu_length;
	    }

	    if (pdu_len < tlen + 4)
		goto invalid;
	    ND_TCHECK2(*tptr, tlen + 4);
	    /* Safe up to and including the "Length of Error Text" data element,
	     * one more data element may be present.
	     */

	    /*
	     * Extract, trail-zero and print the Error message.
	     */
	    text_length = EXTRACT_32BITS(tptr + tlen);
	    tlen += 4;

	    if (text_length) {
		if (pdu_len < tlen + text_length)
		    goto invalid;
		/* fn_printn() makes the bounds check */
		ND_PRINT((ndo, "%sError text: ", indent_string(indent+2)));
		if (fn_printn(ndo, tptr + tlen, text_length, ndo->ndo_snapend))
			goto trunc;
	    }
	}
	break;

    default:
	ND_TCHECK2(*tptr, pdu_len);

	/*
	 * Unknown data, please hexdump.
	 */
	hexdump = TRUE;
    }

    /* do we also want to see a hex dump ? */
    if (ndo->ndo_vflag > 1 || (ndo->ndo_vflag && hexdump)) {
	print_unknown_data(ndo,tptr,"\n\t  ", pdu_len);
    }
    return pdu_len;

invalid:
    ND_PRINT((ndo, "%s", istr));
    ND_TCHECK2(*tptr, len);
    return len;
trunc:
    ND_PRINT((ndo, "\n\t%s", tstr));
    return len;
}

void
rpki_rtr_print(netdissect_options *ndo, register const u_char *pptr, register u_int len)
{
    if (!ndo->ndo_vflag) {
	ND_PRINT((ndo, ", RPKI-RTR"));
	return;
    }
    while (len) {
	u_int pdu_len = rpki_rtr_pdu_print(ndo, pptr, len, 1, 8);
	len -= pdu_len;
	pptr += pdu_len;
    }
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 4
 * End:
 */
