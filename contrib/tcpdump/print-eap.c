/*
 * Copyright (c) 2004 - Michael Richardson <mcr@xelerance.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: Extensible Authentication Protocol (EAP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#define	EAP_FRAME_TYPE_PACKET		0
#define	EAP_FRAME_TYPE_START		1
#define	EAP_FRAME_TYPE_LOGOFF		2
#define	EAP_FRAME_TYPE_KEY		3
#define	EAP_FRAME_TYPE_ENCAP_ASF_ALERT	4

struct eap_frame_t {
    unsigned char   version;
    unsigned char   type;
    unsigned char   length[2];
};

static const struct tok eap_frame_type_values[] = {
    { EAP_FRAME_TYPE_PACKET,      	"EAP packet" },
    { EAP_FRAME_TYPE_START,    		"EAPOL start" },
    { EAP_FRAME_TYPE_LOGOFF,      	"EAPOL logoff" },
    { EAP_FRAME_TYPE_KEY,      		"EAPOL key" },
    { EAP_FRAME_TYPE_ENCAP_ASF_ALERT, 	"Encapsulated ASF alert" },
    { 0, NULL}
};

/* RFC 3748 */
struct eap_packet_t {
    unsigned char	code;
    unsigned char	id;
    unsigned char	length[2];
};

#define		EAP_REQUEST	1
#define		EAP_RESPONSE	2
#define		EAP_SUCCESS	3
#define		EAP_FAILURE	4

static const struct tok eap_code_values[] = {
    { EAP_REQUEST,	"Request" },
    { EAP_RESPONSE,	"Response" },
    { EAP_SUCCESS,	"Success" },
    { EAP_FAILURE,	"Failure" },
    { 0, NULL}
};

#define		EAP_TYPE_NO_PROPOSED	0
#define		EAP_TYPE_IDENTITY	1
#define		EAP_TYPE_NOTIFICATION	2
#define		EAP_TYPE_NAK		3
#define		EAP_TYPE_MD5_CHALLENGE	4
#define		EAP_TYPE_OTP		5
#define		EAP_TYPE_GTC		6
#define		EAP_TYPE_TLS		13		/* RFC 2716 */
#define		EAP_TYPE_SIM		18		/* RFC 4186 */
#define		EAP_TYPE_TTLS		21		/* draft-funk-eap-ttls-v0-01.txt */
#define		EAP_TYPE_AKA		23		/* RFC 4187 */
#define		EAP_TYPE_FAST		43		/* RFC 4851 */
#define		EAP_TYPE_EXPANDED_TYPES	254
#define		EAP_TYPE_EXPERIMENTAL	255

static const struct tok eap_type_values[] = {
    { EAP_TYPE_NO_PROPOSED,	"No proposed" },
    { EAP_TYPE_IDENTITY,	"Identity" },
    { EAP_TYPE_NOTIFICATION,    "Notification" },
    { EAP_TYPE_NAK,      	"Nak" },
    { EAP_TYPE_MD5_CHALLENGE,   "MD5-challenge" },
    { EAP_TYPE_OTP,      	"OTP" },
    { EAP_TYPE_GTC,      	"GTC" },
    { EAP_TYPE_TLS,      	"TLS" },
    { EAP_TYPE_SIM,      	"SIM" },
    { EAP_TYPE_TTLS,      	"TTLS" },
    { EAP_TYPE_AKA,      	"AKA" },
    { EAP_TYPE_FAST,      	"FAST" },
    { EAP_TYPE_EXPANDED_TYPES,  "Expanded types" },
    { EAP_TYPE_EXPERIMENTAL,    "Experimental" },
    { 0, NULL}
};

#define EAP_TLS_EXTRACT_BIT_L(x) 	(((x)&0x80)>>7)

/* RFC 2716 - EAP TLS bits */
#define EAP_TLS_FLAGS_LEN_INCLUDED		(1 << 7)
#define EAP_TLS_FLAGS_MORE_FRAGMENTS		(1 << 6)
#define EAP_TLS_FLAGS_START			(1 << 5)

static const struct tok eap_tls_flags_values[] = {
	{ EAP_TLS_FLAGS_LEN_INCLUDED, "L bit" },
	{ EAP_TLS_FLAGS_MORE_FRAGMENTS, "More fragments bit"},
	{ EAP_TLS_FLAGS_START, "Start bit"},
	{ 0, NULL}
};

#define EAP_TTLS_VERSION(x)		((x)&0x07)

/* EAP-AKA and EAP-SIM - RFC 4187 */
#define EAP_AKA_CHALLENGE		1
#define EAP_AKA_AUTH_REJECT		2
#define EAP_AKA_SYNC_FAILURE		4
#define EAP_AKA_IDENTITY		5
#define EAP_SIM_START			10
#define EAP_SIM_CHALLENGE		11
#define EAP_AKA_NOTIFICATION		12
#define EAP_AKA_REAUTH			13
#define EAP_AKA_CLIENT_ERROR		14

static const struct tok eap_aka_subtype_values[] = {
    { EAP_AKA_CHALLENGE,	"Challenge" },
    { EAP_AKA_AUTH_REJECT,	"Auth reject" },
    { EAP_AKA_SYNC_FAILURE,	"Sync failure" },
    { EAP_AKA_IDENTITY,		"Identity" },
    { EAP_SIM_START,		"Start" },
    { EAP_SIM_CHALLENGE,	"Challenge" },
    { EAP_AKA_NOTIFICATION,	"Notification" },
    { EAP_AKA_REAUTH,		"Reauth" },
    { EAP_AKA_CLIENT_ERROR,	"Client error" },
    { 0, NULL}
};

/*
 * Print EAP requests / responses
 */
void
eap_print(netdissect_options *ndo,
          register const u_char *cp,
          u_int length)
{
    const struct eap_frame_t *eap;
    const u_char *tptr;
    u_int tlen, type, subtype;
    int count=0, len;

    tptr = cp;
    tlen = length;
    eap = (const struct eap_frame_t *)cp;
    ND_TCHECK(*eap);

    /* in non-verbose mode just lets print the basic info */
    if (ndo->ndo_vflag < 1) {
	ND_PRINT((ndo, "%s (%u) v%u, len %u",
               tok2str(eap_frame_type_values, "unknown", eap->type),
               eap->type,
               eap->version,
               EXTRACT_16BITS(eap->length)));
	return;
    }

    ND_PRINT((ndo, "%s (%u) v%u, len %u",
           tok2str(eap_frame_type_values, "unknown", eap->type),
           eap->type,
           eap->version,
           EXTRACT_16BITS(eap->length)));

    tptr += sizeof(const struct eap_frame_t);
    tlen -= sizeof(const struct eap_frame_t);

    switch (eap->type) {
    case EAP_FRAME_TYPE_PACKET:
        ND_TCHECK_8BITS(tptr);
        type = *(tptr);
        ND_TCHECK_16BITS(tptr+2);
        len = EXTRACT_16BITS(tptr+2);
        ND_PRINT((ndo, ", %s (%u), id %u, len %u",
               tok2str(eap_code_values, "unknown", type),
               type,
               *(tptr+1),
               len));

        ND_TCHECK2(*tptr, len);

        if (type <= 2) { /* For EAP_REQUEST and EAP_RESPONSE only */
            ND_TCHECK_8BITS(tptr+4);
            subtype = *(tptr+4);
            ND_PRINT((ndo, "\n\t\t Type %s (%u)",
                   tok2str(eap_type_values, "unknown", subtype),
                   subtype));

            switch (subtype) {
            case EAP_TYPE_IDENTITY:
                if (len - 5 > 0) {
                    ND_PRINT((ndo, ", Identity: "));
                    safeputs(ndo, tptr + 5, len - 5);
                }
                break;

            case EAP_TYPE_NOTIFICATION:
                if (len - 5 > 0) {
                    ND_PRINT((ndo, ", Notification: "));
                    safeputs(ndo, tptr + 5, len - 5);
                }
                break;

            case EAP_TYPE_NAK:
                count = 5;

                /*
                 * one or more octets indicating
                 * the desired authentication
                 * type one octet per type
                 */
                while (count < len) {
                    ND_TCHECK_8BITS(tptr+count);
                    ND_PRINT((ndo, " %s (%u),",
                           tok2str(eap_type_values, "unknown", *(tptr+count)),
                           *(tptr + count)));
                    count++;
                }
                break;

            case EAP_TYPE_TTLS:
            case EAP_TYPE_TLS:
                ND_TCHECK_8BITS(tptr + 5);
                if (subtype == EAP_TYPE_TTLS)
                    ND_PRINT((ndo, " TTLSv%u",
                           EAP_TTLS_VERSION(*(tptr + 5))));
                ND_PRINT((ndo, " flags [%s] 0x%02x,",
                       bittok2str(eap_tls_flags_values, "none", *(tptr+5)),
                       *(tptr + 5)));

                if (EAP_TLS_EXTRACT_BIT_L(*(tptr+5))) {
                    ND_TCHECK_32BITS(tptr + 6);
		    ND_PRINT((ndo, " len %u", EXTRACT_32BITS(tptr + 6)));
                }
                break;

            case EAP_TYPE_FAST:
                ND_TCHECK_8BITS(tptr + 5);
                ND_PRINT((ndo, " FASTv%u",
                       EAP_TTLS_VERSION(*(tptr + 5))));
                ND_PRINT((ndo, " flags [%s] 0x%02x,",
                       bittok2str(eap_tls_flags_values, "none", *(tptr+5)),
                       *(tptr + 5)));

                if (EAP_TLS_EXTRACT_BIT_L(*(tptr+5))) {
                    ND_TCHECK_32BITS(tptr + 6);
                    ND_PRINT((ndo, " len %u", EXTRACT_32BITS(tptr + 6)));
                }

                /* FIXME - TLV attributes follow */
                break;

            case EAP_TYPE_AKA:
            case EAP_TYPE_SIM:
                ND_TCHECK_8BITS(tptr + 5);
                ND_PRINT((ndo, " subtype [%s] 0x%02x,",
                       tok2str(eap_aka_subtype_values, "unknown", *(tptr+5)),
                       *(tptr + 5)));

                /* FIXME - TLV attributes follow */
                break;

            case EAP_TYPE_MD5_CHALLENGE:
            case EAP_TYPE_OTP:
            case EAP_TYPE_GTC:
            case EAP_TYPE_EXPANDED_TYPES:
            case EAP_TYPE_EXPERIMENTAL:
            default:
                break;
            }
        }
        break;

    case EAP_FRAME_TYPE_LOGOFF:
    case EAP_FRAME_TYPE_ENCAP_ASF_ALERT:
    default:
        break;
    }
    return;

 trunc:
    ND_PRINT((ndo, "\n\t[|EAP]"));
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
