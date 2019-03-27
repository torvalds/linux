/*
 * Copyright (c) 1991, 1992, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Lawrence Berkeley Laboratory,
 * Berkeley, CA.  The name of the University may not be used to
 * endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Initial contribution from Jeff Honig (jch@MITCHELL.CIT.CORNELL.EDU).
 */

/* \summary: Exterior Gateway Protocol (EGP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

struct egp_packet {
	uint8_t  egp_version;
#define	EGP_VERSION	2
	uint8_t  egp_type;
#define  EGPT_ACQUIRE	3
#define  EGPT_REACH	5
#define  EGPT_POLL	2
#define  EGPT_UPDATE	1
#define  EGPT_ERROR	8
	uint8_t  egp_code;
#define  EGPC_REQUEST	0
#define  EGPC_CONFIRM	1
#define  EGPC_REFUSE	2
#define  EGPC_CEASE	3
#define  EGPC_CEASEACK	4
#define  EGPC_HELLO	0
#define  EGPC_HEARDU	1
	uint8_t  egp_status;
#define  EGPS_UNSPEC	0
#define  EGPS_ACTIVE	1
#define  EGPS_PASSIVE	2
#define  EGPS_NORES	3
#define  EGPS_ADMIN	4
#define  EGPS_GODOWN	5
#define  EGPS_PARAM	6
#define  EGPS_PROTO	7
#define  EGPS_INDET	0
#define  EGPS_UP	1
#define  EGPS_DOWN	2
#define  EGPS_UNSOL	0x80
	uint16_t  egp_checksum;
	uint16_t  egp_as;
	uint16_t  egp_sequence;
	union {
		uint16_t  egpu_hello;
		uint8_t egpu_gws[2];
		uint16_t  egpu_reason;
#define  EGPR_UNSPEC	0
#define  EGPR_BADHEAD	1
#define  EGPR_BADDATA	2
#define  EGPR_NOREACH	3
#define  EGPR_XSPOLL	4
#define  EGPR_NORESP	5
#define  EGPR_UVERSION	6
	} egp_handg;
#define  egp_hello  egp_handg.egpu_hello
#define  egp_intgw  egp_handg.egpu_gws[0]
#define  egp_extgw  egp_handg.egpu_gws[1]
#define  egp_reason  egp_handg.egpu_reason
	union {
		uint16_t  egpu_poll;
		uint32_t egpu_sourcenet;
	} egp_pands;
#define  egp_poll  egp_pands.egpu_poll
#define  egp_sourcenet  egp_pands.egpu_sourcenet
};

static const char *egp_acquire_codes[] = {
	"request",
	"confirm",
	"refuse",
	"cease",
	"cease_ack"
};

static const char *egp_acquire_status[] = {
	"unspecified",
	"active_mode",
	"passive_mode",
	"insufficient_resources",
	"administratively_prohibited",
	"going_down",
	"parameter_violation",
	"protocol_violation"
};

static const char *egp_reach_codes[] = {
	"hello",
	"i-h-u"
};

static const char *egp_status_updown[] = {
	"indeterminate",
	"up",
	"down"
};

static const char *egp_reasons[] = {
	"unspecified",
	"bad_EGP_header_format",
	"bad_EGP_data_field_format",
	"reachability_info_unavailable",
	"excessive_polling_rate",
	"no_response",
	"unsupported_version"
};

static void
egpnrprint(netdissect_options *ndo,
           register const struct egp_packet *egp, u_int length)
{
	register const uint8_t *cp;
	uint32_t addr;
	register uint32_t net;
	register u_int netlen;
	int gateways, distances, networks;
	int t_gateways;
	const char *comma;

	addr = egp->egp_sourcenet;
	if (IN_CLASSA(addr)) {
		net = addr & IN_CLASSA_NET;
		netlen = 1;
	} else if (IN_CLASSB(addr)) {
		net = addr & IN_CLASSB_NET;
		netlen = 2;
	} else if (IN_CLASSC(addr)) {
		net = addr & IN_CLASSC_NET;
		netlen = 3;
	} else {
		net = 0;
		netlen = 0;
	}
	cp = (const uint8_t *)(egp + 1);
	length -= sizeof(*egp);

	t_gateways = egp->egp_intgw + egp->egp_extgw;
	for (gateways = 0; gateways < t_gateways; ++gateways) {
		/* Pickup host part of gateway address */
		addr = 0;
		if (length < 4 - netlen)
			goto trunc;
		ND_TCHECK2(cp[0], 4 - netlen);
		switch (netlen) {

		case 1:
			addr = *cp++;
			/* fall through */
		case 2:
			addr = (addr << 8) | *cp++;
			/* fall through */
		case 3:
			addr = (addr << 8) | *cp++;
		}
		addr |= net;
		length -= 4 - netlen;
		if (length < 1)
			goto trunc;
		ND_TCHECK2(cp[0], 1);
		distances = *cp++;
		length--;
		ND_PRINT((ndo, " %s %s ",
		       gateways < (int)egp->egp_intgw ? "int" : "ext",
		       ipaddr_string(ndo, &addr)));

		comma = "";
		ND_PRINT((ndo, "("));
		while (--distances >= 0) {
			if (length < 2)
				goto trunc;
			ND_TCHECK2(cp[0], 2);
			ND_PRINT((ndo, "%sd%d:", comma, (int)*cp++));
			comma = ", ";
			networks = *cp++;
			length -= 2;
			while (--networks >= 0) {
				/* Pickup network number */
				if (length < 1)
					goto trunc;
				ND_TCHECK2(cp[0], 1);
				addr = (uint32_t)*cp++ << 24;
				length--;
				if (IN_CLASSB(addr)) {
					if (length < 1)
						goto trunc;
					ND_TCHECK2(cp[0], 1);
					addr |= (uint32_t)*cp++ << 16;
					length--;
				} else if (!IN_CLASSA(addr)) {
					if (length < 2)
						goto trunc;
					ND_TCHECK2(cp[0], 2);
					addr |= (uint32_t)*cp++ << 16;
					addr |= (uint32_t)*cp++ << 8;
					length -= 2;
				}
				ND_PRINT((ndo, " %s", ipaddr_string(ndo, &addr)));
			}
		}
		ND_PRINT((ndo, ")"));
	}
	return;
trunc:
	ND_PRINT((ndo, "[|]"));
}

void
egp_print(netdissect_options *ndo,
          register const uint8_t *bp, register u_int length)
{
	register const struct egp_packet *egp;
	register int status;
	register int code;
	register int type;

	egp = (const struct egp_packet *)bp;
	if (length < sizeof(*egp) || !ND_TTEST(*egp)) {
		ND_PRINT((ndo, "[|egp]"));
		return;
	}

        if (!ndo->ndo_vflag) {
            ND_PRINT((ndo, "EGPv%u, AS %u, seq %u, length %u",
                   egp->egp_version,
                   EXTRACT_16BITS(&egp->egp_as),
                   EXTRACT_16BITS(&egp->egp_sequence),
                   length));
            return;
        } else
            ND_PRINT((ndo, "EGPv%u, length %u",
                   egp->egp_version,
                   length));

	if (egp->egp_version != EGP_VERSION) {
		ND_PRINT((ndo, "[version %d]", egp->egp_version));
		return;
	}

	type = egp->egp_type;
	code = egp->egp_code;
	status = egp->egp_status;

	switch (type) {
	case EGPT_ACQUIRE:
		ND_PRINT((ndo, " acquire"));
		switch (code) {
		case EGPC_REQUEST:
		case EGPC_CONFIRM:
			ND_PRINT((ndo, " %s", egp_acquire_codes[code]));
			switch (status) {
			case EGPS_UNSPEC:
			case EGPS_ACTIVE:
			case EGPS_PASSIVE:
				ND_PRINT((ndo, " %s", egp_acquire_status[status]));
				break;

			default:
				ND_PRINT((ndo, " [status %d]", status));
				break;
			}
			ND_PRINT((ndo, " hello:%d poll:%d",
			       EXTRACT_16BITS(&egp->egp_hello),
			       EXTRACT_16BITS(&egp->egp_poll)));
			break;

		case EGPC_REFUSE:
		case EGPC_CEASE:
		case EGPC_CEASEACK:
			ND_PRINT((ndo, " %s", egp_acquire_codes[code]));
			switch (status ) {
			case EGPS_UNSPEC:
			case EGPS_NORES:
			case EGPS_ADMIN:
			case EGPS_GODOWN:
			case EGPS_PARAM:
			case EGPS_PROTO:
				ND_PRINT((ndo, " %s", egp_acquire_status[status]));
				break;

			default:
				ND_PRINT((ndo, "[status %d]", status));
				break;
			}
			break;

		default:
			ND_PRINT((ndo, "[code %d]", code));
			break;
		}
		break;

	case EGPT_REACH:
		switch (code) {

		case EGPC_HELLO:
		case EGPC_HEARDU:
			ND_PRINT((ndo, " %s", egp_reach_codes[code]));
			if (status <= EGPS_DOWN)
				ND_PRINT((ndo, " state:%s", egp_status_updown[status]));
			else
				ND_PRINT((ndo, " [status %d]", status));
			break;

		default:
			ND_PRINT((ndo, "[reach code %d]", code));
			break;
		}
		break;

	case EGPT_POLL:
		ND_PRINT((ndo, " poll"));
		if (egp->egp_status <= EGPS_DOWN)
			ND_PRINT((ndo, " state:%s", egp_status_updown[status]));
		else
			ND_PRINT((ndo, " [status %d]", status));
		ND_PRINT((ndo, " net:%s", ipaddr_string(ndo, &egp->egp_sourcenet)));
		break;

	case EGPT_UPDATE:
		ND_PRINT((ndo, " update"));
		if (status & EGPS_UNSOL) {
			status &= ~EGPS_UNSOL;
			ND_PRINT((ndo, " unsolicited"));
		}
		if (status <= EGPS_DOWN)
			ND_PRINT((ndo, " state:%s", egp_status_updown[status]));
		else
			ND_PRINT((ndo, " [status %d]", status));
		ND_PRINT((ndo, " %s int %d ext %d",
		       ipaddr_string(ndo, &egp->egp_sourcenet),
		       egp->egp_intgw,
		       egp->egp_extgw));
		if (ndo->ndo_vflag)
			egpnrprint(ndo, egp, length);
		break;

	case EGPT_ERROR:
		ND_PRINT((ndo, " error"));
		if (status <= EGPS_DOWN)
			ND_PRINT((ndo, " state:%s", egp_status_updown[status]));
		else
			ND_PRINT((ndo, " [status %d]", status));

		if (EXTRACT_16BITS(&egp->egp_reason) <= EGPR_UVERSION)
			ND_PRINT((ndo, " %s", egp_reasons[EXTRACT_16BITS(&egp->egp_reason)]));
		else
			ND_PRINT((ndo, " [reason %d]", EXTRACT_16BITS(&egp->egp_reason)));
		break;

	default:
		ND_PRINT((ndo, "[type %d]", type));
		break;
	}
}
