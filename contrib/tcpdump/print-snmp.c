/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 *     John Robert LoVerso. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * This implementation has been influenced by the CMU SNMP release,
 * by Steve Waldbusser.  However, this shares no code with that system.
 * Additional ASN.1 insight gained from Marshall T. Rose's _The_Open_Book_.
 * Earlier forms of this implementation were derived and/or inspired by an
 * awk script originally written by C. Philip Wood of LANL (but later
 * heavily modified by John Robert LoVerso).  The copyright notice for
 * that work is preserved below, even though it may not rightly apply
 * to this file.
 *
 * Support for SNMPv2c/SNMPv3 and the ability to link the module against
 * the libsmi was added by J. Schoenwaelder, Copyright (c) 1999.
 *
 * This started out as a very simple program, but the incremental decoding
 * (into the BE structure) complicated things.
 *
 #			Los Alamos National Laboratory
 #
 #	Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 #	This software was produced under a U.S. Government contract
 #	(W-7405-ENG-36) by Los Alamos National Laboratory, which is
 #	operated by the	University of California for the U.S. Department
 #	of Energy.  The U.S. Government is licensed to use, reproduce,
 #	and distribute this software.  Permission is granted to the
 #	public to copy and use this software without charge, provided
 #	that this Notice and any statement of authorship are reproduced
 #	on all copies.  Neither the Government nor the University makes
 #	any warranty, express or implied, or assumes any liability or
 #	responsibility for the use of this software.
 #	@(#)snmp.awk.x	1.1 (LANL) 1/15/90
 */

/* \summary: Simple Network Management Protocol (SNMP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#ifdef USE_LIBSMI
#include <smi.h>
#endif

#include "netdissect.h"

#undef OPAQUE  /* defined in <wingdi.h> */

static const char tstr[] = "[|snmp]";

/*
 * Universal ASN.1 types
 * (we only care about the tag values for those allowed in the Internet SMI)
 */
static const char *Universal[] = {
	"U-0",
	"Boolean",
	"Integer",
#define INTEGER 2
	"Bitstring",
	"String",
#define STRING 4
	"Null",
#define ASN_NULL 5
	"ObjID",
#define OBJECTID 6
	"ObjectDes",
	"U-8","U-9","U-10","U-11",	/* 8-11 */
	"U-12","U-13","U-14","U-15",	/* 12-15 */
	"Sequence",
#define SEQUENCE 16
	"Set"
};

/*
 * Application-wide ASN.1 types from the Internet SMI and their tags
 */
static const char *Application[] = {
	"IpAddress",
#define IPADDR 0
	"Counter",
#define COUNTER 1
	"Gauge",
#define GAUGE 2
	"TimeTicks",
#define TIMETICKS 3
	"Opaque",
#define OPAQUE 4
	"C-5",
	"Counter64"
#define COUNTER64 6
};

/*
 * Context-specific ASN.1 types for the SNMP PDUs and their tags
 */
static const char *Context[] = {
	"GetRequest",
#define GETREQ 0
	"GetNextRequest",
#define GETNEXTREQ 1
	"GetResponse",
#define GETRESP 2
	"SetRequest",
#define SETREQ 3
	"Trap",
#define TRAP 4
	"GetBulk",
#define GETBULKREQ 5
	"Inform",
#define INFORMREQ 6
	"V2Trap",
#define V2TRAP 7
	"Report"
#define REPORT 8
};

#define NOTIFY_CLASS(x)	    (x == TRAP || x == V2TRAP || x == INFORMREQ)
#define READ_CLASS(x)       (x == GETREQ || x == GETNEXTREQ || x == GETBULKREQ)
#define WRITE_CLASS(x)	    (x == SETREQ)
#define RESPONSE_CLASS(x)   (x == GETRESP)
#define INTERNAL_CLASS(x)   (x == REPORT)

/*
 * Context-specific ASN.1 types for the SNMP Exceptions and their tags
 */
static const char *Exceptions[] = {
	"noSuchObject",
#define NOSUCHOBJECT 0
	"noSuchInstance",
#define NOSUCHINSTANCE 1
	"endOfMibView",
#define ENDOFMIBVIEW 2
};

/*
 * Private ASN.1 types
 * The Internet SMI does not specify any
 */
static const char *Private[] = {
	"P-0"
};

/*
 * error-status values for any SNMP PDU
 */
static const char *ErrorStatus[] = {
	"noError",
	"tooBig",
	"noSuchName",
	"badValue",
	"readOnly",
	"genErr",
	"noAccess",
	"wrongType",
	"wrongLength",
	"wrongEncoding",
	"wrongValue",
	"noCreation",
	"inconsistentValue",
	"resourceUnavailable",
	"commitFailed",
	"undoFailed",
	"authorizationError",
	"notWritable",
	"inconsistentName"
};
#define DECODE_ErrorStatus(e) \
	( e >= 0 && (size_t)e < sizeof(ErrorStatus)/sizeof(ErrorStatus[0]) \
		? ErrorStatus[e] \
		: (snprintf(errbuf, sizeof(errbuf), "err=%u", e), errbuf))

/*
 * generic-trap values in the SNMP Trap-PDU
 */
static const char *GenericTrap[] = {
	"coldStart",
	"warmStart",
	"linkDown",
	"linkUp",
	"authenticationFailure",
	"egpNeighborLoss",
	"enterpriseSpecific"
#define GT_ENTERPRISE 6
};
#define DECODE_GenericTrap(t) \
	( t >= 0 && (size_t)t < sizeof(GenericTrap)/sizeof(GenericTrap[0]) \
		? GenericTrap[t] \
		: (snprintf(buf, sizeof(buf), "gt=%d", t), buf))

/*
 * ASN.1 type class table
 * Ties together the preceding Universal, Application, Context, and Private
 * type definitions.
 */
#define defineCLASS(x) { "x", x, sizeof(x)/sizeof(x[0]) } /* not ANSI-C */
static const struct {
	const char	*name;
	const char	**Id;
	    int	numIDs;
    } Class[] = {
	defineCLASS(Universal),
#define	UNIVERSAL	0
	defineCLASS(Application),
#define	APPLICATION	1
	defineCLASS(Context),
#define	CONTEXT		2
	defineCLASS(Private),
#define	PRIVATE		3
	defineCLASS(Exceptions),
#define EXCEPTIONS	4
};

/*
 * defined forms for ASN.1 types
 */
static const char *Form[] = {
	"Primitive",
#define PRIMITIVE	0
	"Constructed",
#define CONSTRUCTED	1
};

/*
 * A structure for the OID tree for the compiled-in MIB.
 * This is stored as a general-order tree.
 */
static struct obj {
	const char	*desc;		/* name of object */
	u_char	oid;			/* sub-id following parent */
	u_char	type;			/* object type (unused) */
	struct obj *child, *next;	/* child and next sibling pointers */
} *objp = NULL;

/*
 * Include the compiled in SNMP MIB.  "mib.h" is produced by feeding
 * RFC-1156 format files into "makemib".  "mib.h" MUST define at least
 * a value for `mibroot'.
 *
 * In particular, this is gross, as this is including initialized structures,
 * and by right shouldn't be an "include" file.
 */
#include "mib.h"

/*
 * This defines a list of OIDs which will be abbreviated on output.
 * Currently, this includes the prefixes for the Internet MIB, the
 * private enterprises tree, and the experimental tree.
 */
#define OID_FIRST_OCTET(x, y)	(((x)*40) + (y))	/* X.690 8.19.4 */

#ifndef NO_ABREV_MIB
static const uint8_t mib_oid[] = { OID_FIRST_OCTET(1, 3), 6, 1, 2, 1 };
#endif
#ifndef NO_ABREV_ENTER
static const uint8_t enterprises_oid[] = { OID_FIRST_OCTET(1, 3), 6, 1, 4, 1 };
#endif
#ifndef NO_ABREV_EXPERI
static const uint8_t experimental_oid[] = { OID_FIRST_OCTET(1, 3), 6, 1, 3 };
#endif
#ifndef NO_ABBREV_SNMPMODS
static const uint8_t snmpModules_oid[] = { OID_FIRST_OCTET(1, 3), 6, 1, 6, 3 };
#endif

#define OBJ_ABBREV_ENTRY(prefix, obj) \
	{ prefix, &_ ## obj ## _obj, obj ## _oid, sizeof (obj ## _oid) }
static const struct obj_abrev {
	const char *prefix;		/* prefix for this abrev */
	struct obj *node;		/* pointer into object table */
	const uint8_t *oid;		/* ASN.1 encoded OID */
	size_t oid_len;			/* length of OID */
} obj_abrev_list[] = {
#ifndef NO_ABREV_MIB
	/* .iso.org.dod.internet.mgmt.mib */
	OBJ_ABBREV_ENTRY("",	mib),
#endif
#ifndef NO_ABREV_ENTER
	/* .iso.org.dod.internet.private.enterprises */
	OBJ_ABBREV_ENTRY("E:",	enterprises),
#endif
#ifndef NO_ABREV_EXPERI
	/* .iso.org.dod.internet.experimental */
	OBJ_ABBREV_ENTRY("X:",	experimental),
#endif
#ifndef NO_ABBREV_SNMPMODS
	/* .iso.org.dod.internet.snmpV2.snmpModules */
	OBJ_ABBREV_ENTRY("S:",	snmpModules),
#endif
	{ 0,0,0,0 }
};

/*
 * This is used in the OID print routine to walk down the object tree
 * rooted at `mibroot'.
 */
#define OBJ_PRINT(o, suppressdot) \
{ \
	if (objp) { \
		do { \
			if ((o) == objp->oid) \
				break; \
		} while ((objp = objp->next) != NULL); \
	} \
	if (objp) { \
		ND_PRINT((ndo, suppressdot?"%s":".%s", objp->desc)); \
		objp = objp->child; \
	} else \
		ND_PRINT((ndo, suppressdot?"%u":".%u", (o))); \
}

/*
 * This is the definition for the Any-Data-Type storage used purely for
 * temporary internal representation while decoding an ASN.1 data stream.
 */
struct be {
	uint32_t asnlen;
	union {
		const uint8_t *raw;
		int32_t integer;
		uint32_t uns;
		const u_char *str;
		uint64_t uns64;
	} data;
	u_short id;
	u_char form, class;		/* tag info */
	u_char type;
#define BE_ANY		255
#define BE_NONE		0
#define BE_NULL		1
#define BE_OCTET	2
#define BE_OID		3
#define BE_INT		4
#define BE_UNS		5
#define BE_STR		6
#define BE_SEQ		7
#define BE_INETADDR	8
#define BE_PDU		9
#define BE_UNS64	10
#define BE_NOSUCHOBJECT	128
#define BE_NOSUCHINST	129
#define BE_ENDOFMIBVIEW	130
};

/*
 * SNMP versions recognized by this module
 */
static const char *SnmpVersion[] = {
	"SNMPv1",
#define SNMP_VERSION_1	0
	"SNMPv2c",
#define SNMP_VERSION_2	1
	"SNMPv2u",
#define SNMP_VERSION_2U	2
	"SNMPv3"
#define SNMP_VERSION_3	3
};

/*
 * Defaults for SNMP PDU components
 */
#define DEF_COMMUNITY "public"

/*
 * constants for ASN.1 decoding
 */
#define OIDMUX 40
#define ASNLEN_INETADDR 4
#define ASN_SHIFT7 7
#define ASN_SHIFT8 8
#define ASN_BIT8 0x80
#define ASN_LONGLEN 0x80

#define ASN_ID_BITS 0x1f
#define ASN_FORM_BITS 0x20
#define ASN_FORM_SHIFT 5
#define ASN_CLASS_BITS 0xc0
#define ASN_CLASS_SHIFT 6

#define ASN_ID_EXT 0x1f		/* extension ID in tag field */

/*
 * This decodes the next ASN.1 object in the stream pointed to by "p"
 * (and of real-length "len") and stores the intermediate data in the
 * provided BE object.
 *
 * This returns -l if it fails (i.e., the ASN.1 stream is not valid).
 * O/w, this returns the number of bytes parsed from "p".
 */
static int
asn1_parse(netdissect_options *ndo,
           register const u_char *p, u_int len, struct be *elem)
{
	u_char form, class, id;
	int i, hdr;

	elem->asnlen = 0;
	elem->type = BE_ANY;
	if (len < 1) {
		ND_PRINT((ndo, "[nothing to parse]"));
		return -1;
	}
	ND_TCHECK(*p);

	/*
	 * it would be nice to use a bit field, but you can't depend on them.
	 *  +---+---+---+---+---+---+---+---+
	 *  + class |frm|        id         |
	 *  +---+---+---+---+---+---+---+---+
	 *    7   6   5   4   3   2   1   0
	 */
	id = *p & ASN_ID_BITS;		/* lower 5 bits, range 00-1f */
#ifdef notdef
	form = (*p & 0xe0) >> 5;	/* move upper 3 bits to lower 3 */
	class = form >> 1;		/* bits 7&6 -> bits 1&0, range 0-3 */
	form &= 0x1;			/* bit 5 -> bit 0, range 0-1 */
#else
	form = (u_char)(*p & ASN_FORM_BITS) >> ASN_FORM_SHIFT;
	class = (u_char)(*p & ASN_CLASS_BITS) >> ASN_CLASS_SHIFT;
#endif
	elem->form = form;
	elem->class = class;
	elem->id = id;
	p++; len--; hdr = 1;
	/* extended tag field */
	if (id == ASN_ID_EXT) {
		/*
		 * The ID follows, as a sequence of octets with the
		 * 8th bit set and the remaining 7 bits being
		 * the next 7 bits of the value, terminated with
		 * an octet with the 8th bit not set.
		 *
		 * First, assemble all the octets with the 8th
		 * bit set.  XXX - this doesn't handle a value
		 * that won't fit in 32 bits.
		 */
		id = 0;
		ND_TCHECK(*p);
		while (*p & ASN_BIT8) {
			if (len < 1) {
				ND_PRINT((ndo, "[Xtagfield?]"));
				return -1;
			}
			id = (id << 7) | (*p & ~ASN_BIT8);
			len--;
			hdr++;
			p++;
			ND_TCHECK(*p);
		}
		if (len < 1) {
			ND_PRINT((ndo, "[Xtagfield?]"));
			return -1;
		}
		ND_TCHECK(*p);
		elem->id = id = (id << 7) | *p;
		--len;
		++hdr;
		++p;
	}
	if (len < 1) {
		ND_PRINT((ndo, "[no asnlen]"));
		return -1;
	}
	ND_TCHECK(*p);
	elem->asnlen = *p;
	p++; len--; hdr++;
	if (elem->asnlen & ASN_BIT8) {
		uint32_t noct = elem->asnlen % ASN_BIT8;
		elem->asnlen = 0;
		if (len < noct) {
			ND_PRINT((ndo, "[asnlen? %d<%d]", len, noct));
			return -1;
		}
		ND_TCHECK2(*p, noct);
		for (; noct-- > 0; len--, hdr++)
			elem->asnlen = (elem->asnlen << ASN_SHIFT8) | *p++;
	}
	if (len < elem->asnlen) {
		ND_PRINT((ndo, "[len%d<asnlen%u]", len, elem->asnlen));
		return -1;
	}
	if (form >= sizeof(Form)/sizeof(Form[0])) {
		ND_PRINT((ndo, "[form?%d]", form));
		return -1;
	}
	if (class >= sizeof(Class)/sizeof(Class[0])) {
		ND_PRINT((ndo, "[class?%c/%d]", *Form[form], class));
		return -1;
	}
	if ((int)id >= Class[class].numIDs) {
		ND_PRINT((ndo, "[id?%c/%s/%d]", *Form[form], Class[class].name, id));
		return -1;
	}
	ND_TCHECK2(*p, elem->asnlen);

	switch (form) {
	case PRIMITIVE:
		switch (class) {
		case UNIVERSAL:
			switch (id) {
			case STRING:
				elem->type = BE_STR;
				elem->data.str = p;
				break;

			case INTEGER: {
				register int32_t data;
				elem->type = BE_INT;
				data = 0;

				if (elem->asnlen == 0) {
					ND_PRINT((ndo, "[asnlen=0]"));
					return -1;
				}
				if (*p & ASN_BIT8)	/* negative */
					data = -1;
				for (i = elem->asnlen; i-- > 0; p++)
					data = (data << ASN_SHIFT8) | *p;
				elem->data.integer = data;
				break;
			}

			case OBJECTID:
				elem->type = BE_OID;
				elem->data.raw = (const uint8_t *)p;
				break;

			case ASN_NULL:
				elem->type = BE_NULL;
				elem->data.raw = NULL;
				break;

			default:
				elem->type = BE_OCTET;
				elem->data.raw = (const uint8_t *)p;
				ND_PRINT((ndo, "[P/U/%s]", Class[class].Id[id]));
				break;
			}
			break;

		case APPLICATION:
			switch (id) {
			case IPADDR:
				elem->type = BE_INETADDR;
				elem->data.raw = (const uint8_t *)p;
				break;

			case COUNTER:
			case GAUGE:
			case TIMETICKS: {
				register uint32_t data;
				elem->type = BE_UNS;
				data = 0;
				for (i = elem->asnlen; i-- > 0; p++)
					data = (data << 8) + *p;
				elem->data.uns = data;
				break;
			}

			case COUNTER64: {
				register uint64_t data64;
			        elem->type = BE_UNS64;
				data64 = 0;
				for (i = elem->asnlen; i-- > 0; p++)
					data64 = (data64 << 8) + *p;
				elem->data.uns64 = data64;
				break;
			}

			default:
				elem->type = BE_OCTET;
				elem->data.raw = (const uint8_t *)p;
				ND_PRINT((ndo, "[P/A/%s]",
					Class[class].Id[id]));
				break;
			}
			break;

		case CONTEXT:
			switch (id) {
			case NOSUCHOBJECT:
				elem->type = BE_NOSUCHOBJECT;
				elem->data.raw = NULL;
				break;

			case NOSUCHINSTANCE:
				elem->type = BE_NOSUCHINST;
				elem->data.raw = NULL;
				break;

			case ENDOFMIBVIEW:
				elem->type = BE_ENDOFMIBVIEW;
				elem->data.raw = NULL;
				break;
			}
			break;

		default:
			ND_PRINT((ndo, "[P/%s/%s]", Class[class].name, Class[class].Id[id]));
			elem->type = BE_OCTET;
			elem->data.raw = (const uint8_t *)p;
			break;
		}
		break;

	case CONSTRUCTED:
		switch (class) {
		case UNIVERSAL:
			switch (id) {
			case SEQUENCE:
				elem->type = BE_SEQ;
				elem->data.raw = (const uint8_t *)p;
				break;

			default:
				elem->type = BE_OCTET;
				elem->data.raw = (const uint8_t *)p;
				ND_PRINT((ndo, "C/U/%s", Class[class].Id[id]));
				break;
			}
			break;

		case CONTEXT:
			elem->type = BE_PDU;
			elem->data.raw = (const uint8_t *)p;
			break;

		default:
			elem->type = BE_OCTET;
			elem->data.raw = (const uint8_t *)p;
			ND_PRINT((ndo, "C/%s/%s", Class[class].name, Class[class].Id[id]));
			break;
		}
		break;
	}
	p += elem->asnlen;
	len -= elem->asnlen;
	return elem->asnlen + hdr;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
asn1_print_octets(netdissect_options *ndo, struct be *elem)
{
	const u_char *p = (const u_char *)elem->data.raw;
	uint32_t asnlen = elem->asnlen;
	uint32_t i;

	ND_TCHECK2(*p, asnlen);
	for (i = asnlen; i-- > 0; p++)
		ND_PRINT((ndo, "_%.2x", *p));
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
asn1_print_string(netdissect_options *ndo, struct be *elem)
{
	register int printable = 1, first = 1;
	const u_char *p;
	uint32_t asnlen = elem->asnlen;
	uint32_t i;

	p = elem->data.str;
	ND_TCHECK2(*p, asnlen);
	for (i = asnlen; printable && i-- > 0; p++)
		printable = ND_ISPRINT(*p);
	p = elem->data.str;
	if (printable) {
		ND_PRINT((ndo, "\""));
		if (fn_printn(ndo, p, asnlen, ndo->ndo_snapend)) {
			ND_PRINT((ndo, "\""));
			goto trunc;
		}
		ND_PRINT((ndo, "\""));
	} else {
		for (i = asnlen; i-- > 0; p++) {
			ND_PRINT((ndo, first ? "%.2x" : "_%.2x", *p));
			first = 0;
		}
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

/*
 * Display the ASN.1 object represented by the BE object.
 * This used to be an integral part of asn1_parse() before the intermediate
 * BE form was added.
 */
static int
asn1_print(netdissect_options *ndo,
           struct be *elem)
{
	const u_char *p;
	uint32_t asnlen = elem->asnlen;
	uint32_t i;

	switch (elem->type) {

	case BE_OCTET:
		if (asn1_print_octets(ndo, elem) == -1)
			return -1;
		break;

	case BE_NULL:
		break;

	case BE_OID: {
		int o = 0, first = -1;

		p = (const u_char *)elem->data.raw;
		i = asnlen;
		if (!ndo->ndo_nflag && asnlen > 2) {
			const struct obj_abrev *a = &obj_abrev_list[0];
			for (; a->node; a++) {
				if (i < a->oid_len)
					continue;
				if (!ND_TTEST2(*p, a->oid_len))
					continue;
				if (memcmp(a->oid, p, a->oid_len) == 0) {
					objp = a->node->child;
					i -= a->oid_len;
					p += a->oid_len;
					ND_PRINT((ndo, "%s", a->prefix));
					first = 1;
					break;
				}
			}
		}

		for (; i-- > 0; p++) {
			ND_TCHECK(*p);
			o = (o << ASN_SHIFT7) + (*p & ~ASN_BIT8);
			if (*p & ASN_LONGLEN)
			        continue;

			/*
			 * first subitem encodes two items with
			 * 1st*OIDMUX+2nd
			 * (see X.690:1997 clause 8.19 for the details)
			 */
			if (first < 0) {
			        int s;
				if (!ndo->ndo_nflag)
					objp = mibroot;
				first = 0;
				s = o / OIDMUX;
				if (s > 2) s = 2;
				OBJ_PRINT(s, first);
				o -= s * OIDMUX;
			}
			OBJ_PRINT(o, first);
			if (--first < 0)
				first = 0;
			o = 0;
		}
		break;
	}

	case BE_INT:
		ND_PRINT((ndo, "%d", elem->data.integer));
		break;

	case BE_UNS:
		ND_PRINT((ndo, "%u", elem->data.uns));
		break;

	case BE_UNS64:
		ND_PRINT((ndo, "%" PRIu64, elem->data.uns64));
		break;

	case BE_STR:
		if (asn1_print_string(ndo, elem) == -1)
			return -1;
		break;

	case BE_SEQ:
		ND_PRINT((ndo, "Seq(%u)", elem->asnlen));
		break;

	case BE_INETADDR:
		if (asnlen != ASNLEN_INETADDR)
			ND_PRINT((ndo, "[inetaddr len!=%d]", ASNLEN_INETADDR));
		p = (const u_char *)elem->data.raw;
		ND_TCHECK2(*p, asnlen);
		for (i = asnlen; i-- != 0; p++) {
			ND_PRINT((ndo, (i == asnlen-1) ? "%u" : ".%u", *p));
		}
		break;

	case BE_NOSUCHOBJECT:
	case BE_NOSUCHINST:
	case BE_ENDOFMIBVIEW:
		ND_PRINT((ndo, "[%s]", Class[EXCEPTIONS].Id[elem->id]));
		break;

	case BE_PDU:
		ND_PRINT((ndo, "%s(%u)", Class[CONTEXT].Id[elem->id], elem->asnlen));
		break;

	case BE_ANY:
		ND_PRINT((ndo, "[BE_ANY!?]"));
		break;

	default:
		ND_PRINT((ndo, "[be!?]"));
		break;
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#ifdef notdef
/*
 * This is a brute force ASN.1 printer: recurses to dump an entire structure.
 * This will work for any ASN.1 stream, not just an SNMP PDU.
 *
 * By adding newlines and spaces at the correct places, this would print in
 * Rose-Normal-Form.
 *
 * This is not currently used.
 */
static void
asn1_decode(u_char *p, u_int length)
{
	struct be elem;
	int i = 0;

	while (i >= 0 && length > 0) {
		i = asn1_parse(ndo, p, length, &elem);
		if (i >= 0) {
			ND_PRINT((ndo, " "));
			if (asn1_print(ndo, &elem) < 0)
				return;
			if (elem.type == BE_SEQ || elem.type == BE_PDU) {
				ND_PRINT((ndo, " {"));
				asn1_decode(elem.data.raw, elem.asnlen);
				ND_PRINT((ndo, " }"));
			}
			length -= i;
			p += i;
		}
	}
}
#endif

#ifdef USE_LIBSMI

struct smi2be {
    SmiBasetype basetype;
    int be;
};

static const struct smi2be smi2betab[] = {
    { SMI_BASETYPE_INTEGER32,		BE_INT },
    { SMI_BASETYPE_OCTETSTRING,		BE_STR },
    { SMI_BASETYPE_OCTETSTRING,		BE_INETADDR },
    { SMI_BASETYPE_OBJECTIDENTIFIER,	BE_OID },
    { SMI_BASETYPE_UNSIGNED32,		BE_UNS },
    { SMI_BASETYPE_INTEGER64,		BE_NONE },
    { SMI_BASETYPE_UNSIGNED64,		BE_UNS64 },
    { SMI_BASETYPE_FLOAT32,		BE_NONE },
    { SMI_BASETYPE_FLOAT64,		BE_NONE },
    { SMI_BASETYPE_FLOAT128,		BE_NONE },
    { SMI_BASETYPE_ENUM,		BE_INT },
    { SMI_BASETYPE_BITS,		BE_STR },
    { SMI_BASETYPE_UNKNOWN,		BE_NONE }
};

static int
smi_decode_oid(netdissect_options *ndo,
               struct be *elem, unsigned int *oid,
               unsigned int oidsize, unsigned int *oidlen)
{
	const u_char *p = (const u_char *)elem->data.raw;
	uint32_t asnlen = elem->asnlen;
	int o = 0, first = -1, i = asnlen;
	unsigned int firstval;

	for (*oidlen = 0; i-- > 0; p++) {
		ND_TCHECK(*p);
	        o = (o << ASN_SHIFT7) + (*p & ~ASN_BIT8);
		if (*p & ASN_LONGLEN)
		    continue;

		/*
		 * first subitem encodes two items with 1st*OIDMUX+2nd
		 * (see X.690:1997 clause 8.19 for the details)
		 */
		if (first < 0) {
	        	first = 0;
			firstval = o / OIDMUX;
			if (firstval > 2) firstval = 2;
			o -= firstval * OIDMUX;
			if (*oidlen < oidsize) {
			    oid[(*oidlen)++] = firstval;
			}
		}
		if (*oidlen < oidsize) {
			oid[(*oidlen)++] = o;
		}
		o = 0;
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int smi_check_type(SmiBasetype basetype, int be)
{
    int i;

    for (i = 0; smi2betab[i].basetype != SMI_BASETYPE_UNKNOWN; i++) {
	if (smi2betab[i].basetype == basetype && smi2betab[i].be == be) {
	    return 1;
	}
    }

    return 0;
}

static int smi_check_a_range(SmiType *smiType, SmiRange *smiRange,
			     struct be *elem)
{
    int ok = 1;

    switch (smiType->basetype) {
    case SMI_BASETYPE_OBJECTIDENTIFIER:
    case SMI_BASETYPE_OCTETSTRING:
	if (smiRange->minValue.value.unsigned32
	    == smiRange->maxValue.value.unsigned32) {
	    ok = (elem->asnlen == smiRange->minValue.value.unsigned32);
	} else {
	    ok = (elem->asnlen >= smiRange->minValue.value.unsigned32
		  && elem->asnlen <= smiRange->maxValue.value.unsigned32);
	}
	break;

    case SMI_BASETYPE_INTEGER32:
	ok = (elem->data.integer >= smiRange->minValue.value.integer32
	      && elem->data.integer <= smiRange->maxValue.value.integer32);
	break;

    case SMI_BASETYPE_UNSIGNED32:
	ok = (elem->data.uns >= smiRange->minValue.value.unsigned32
	      && elem->data.uns <= smiRange->maxValue.value.unsigned32);
	break;

    case SMI_BASETYPE_UNSIGNED64:
	/* XXX */
	break;

	/* case SMI_BASETYPE_INTEGER64: SMIng */
	/* case SMI_BASETYPE_FLOAT32: SMIng */
	/* case SMI_BASETYPE_FLOAT64: SMIng */
	/* case SMI_BASETYPE_FLOAT128: SMIng */

    case SMI_BASETYPE_ENUM:
    case SMI_BASETYPE_BITS:
    case SMI_BASETYPE_UNKNOWN:
	ok = 1;
	break;

    default:
	ok = 0;
	break;
    }

    return ok;
}

static int smi_check_range(SmiType *smiType, struct be *elem)
{
        SmiRange *smiRange;
	int ok = 1;

	for (smiRange = smiGetFirstRange(smiType);
	     smiRange;
	     smiRange = smiGetNextRange(smiRange)) {

	    ok = smi_check_a_range(smiType, smiRange, elem);

	    if (ok) {
		break;
	    }
	}

	if (ok) {
	    SmiType *parentType;
	    parentType = smiGetParentType(smiType);
	    if (parentType) {
		ok = smi_check_range(parentType, elem);
	    }
	}

	return ok;
}

static SmiNode *
smi_print_variable(netdissect_options *ndo,
                   struct be *elem, int *status)
{
	unsigned int oid[128], oidlen;
	SmiNode *smiNode = NULL;
	unsigned int i;

	if (!nd_smi_module_loaded) {
		*status = asn1_print(ndo, elem);
		return NULL;
	}
	*status = smi_decode_oid(ndo, elem, oid, sizeof(oid) / sizeof(unsigned int),
	    &oidlen);
	if (*status < 0)
		return NULL;
	smiNode = smiGetNodeByOID(oidlen, oid);
	if (! smiNode) {
		*status = asn1_print(ndo, elem);
		return NULL;
	}
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "%s::", smiGetNodeModule(smiNode)->name));
	}
	ND_PRINT((ndo, "%s", smiNode->name));
	if (smiNode->oidlen < oidlen) {
		for (i = smiNode->oidlen; i < oidlen; i++) {
			ND_PRINT((ndo, ".%u", oid[i]));
		}
	}
	*status = 0;
	return smiNode;
}

static int
smi_print_value(netdissect_options *ndo,
                SmiNode *smiNode, u_short pduid, struct be *elem)
{
	unsigned int i, oid[128], oidlen;
	SmiType *smiType;
	SmiNamedNumber *nn;
	int done = 0;

	if (! smiNode || ! (smiNode->nodekind
			    & (SMI_NODEKIND_SCALAR | SMI_NODEKIND_COLUMN))) {
	    return asn1_print(ndo, elem);
	}

	if (elem->type == BE_NOSUCHOBJECT
	    || elem->type == BE_NOSUCHINST
	    || elem->type == BE_ENDOFMIBVIEW) {
	    return asn1_print(ndo, elem);
	}

	if (NOTIFY_CLASS(pduid) && smiNode->access < SMI_ACCESS_NOTIFY) {
	    ND_PRINT((ndo, "[notNotifyable]"));
	}

	if (READ_CLASS(pduid) && smiNode->access < SMI_ACCESS_READ_ONLY) {
	    ND_PRINT((ndo, "[notReadable]"));
	}

	if (WRITE_CLASS(pduid) && smiNode->access < SMI_ACCESS_READ_WRITE) {
	    ND_PRINT((ndo, "[notWritable]"));
	}

	if (RESPONSE_CLASS(pduid)
	    && smiNode->access == SMI_ACCESS_NOT_ACCESSIBLE) {
	    ND_PRINT((ndo, "[noAccess]"));
	}

	smiType = smiGetNodeType(smiNode);
	if (! smiType) {
	    return asn1_print(ndo, elem);
	}

	if (! smi_check_type(smiType->basetype, elem->type)) {
	    ND_PRINT((ndo, "[wrongType]"));
	}

	if (! smi_check_range(smiType, elem)) {
	    ND_PRINT((ndo, "[outOfRange]"));
	}

	/* resolve bits to named bits */

	/* check whether instance identifier is valid */

	/* apply display hints (integer, octetstring) */

	/* convert instance identifier to index type values */

	switch (elem->type) {
	case BE_OID:
	        if (smiType->basetype == SMI_BASETYPE_BITS) {
		        /* print bit labels */
		} else {
			if (nd_smi_module_loaded &&
			    smi_decode_oid(ndo, elem, oid,
					   sizeof(oid)/sizeof(unsigned int),
					   &oidlen) == 0) {
				smiNode = smiGetNodeByOID(oidlen, oid);
				if (smiNode) {
				        if (ndo->ndo_vflag) {
						ND_PRINT((ndo, "%s::", smiGetNodeModule(smiNode)->name));
					}
					ND_PRINT((ndo, "%s", smiNode->name));
					if (smiNode->oidlen < oidlen) {
					        for (i = smiNode->oidlen;
						     i < oidlen; i++) {
						        ND_PRINT((ndo, ".%u", oid[i]));
						}
					}
					done++;
				}
			}
		}
		break;

	case BE_INT:
	        if (smiType->basetype == SMI_BASETYPE_ENUM) {
		        for (nn = smiGetFirstNamedNumber(smiType);
			     nn;
			     nn = smiGetNextNamedNumber(nn)) {
			         if (nn->value.value.integer32
				     == elem->data.integer) {
				         ND_PRINT((ndo, "%s", nn->name));
					 ND_PRINT((ndo, "(%d)", elem->data.integer));
					 done++;
					 break;
				}
			}
		}
		break;
	}

	if (! done) {
		return asn1_print(ndo, elem);
	}
	return 0;
}
#endif

/*
 * General SNMP header
 *	SEQUENCE {
 *		version INTEGER {version-1(0)},
 *		community OCTET STRING,
 *		data ANY	-- PDUs
 *	}
 * PDUs for all but Trap: (see rfc1157 from page 15 on)
 *	SEQUENCE {
 *		request-id INTEGER,
 *		error-status INTEGER,
 *		error-index INTEGER,
 *		varbindlist SEQUENCE OF
 *			SEQUENCE {
 *				name ObjectName,
 *				value ObjectValue
 *			}
 *	}
 * PDU for Trap:
 *	SEQUENCE {
 *		enterprise OBJECT IDENTIFIER,
 *		agent-addr NetworkAddress,
 *		generic-trap INTEGER,
 *		specific-trap INTEGER,
 *		time-stamp TimeTicks,
 *		varbindlist SEQUENCE OF
 *			SEQUENCE {
 *				name ObjectName,
 *				value ObjectValue
 *			}
 *	}
 */

/*
 * Decode SNMP varBind
 */
static void
varbind_print(netdissect_options *ndo,
              u_short pduid, const u_char *np, u_int length)
{
	struct be elem;
	int count = 0, ind;
#ifdef USE_LIBSMI
	SmiNode *smiNode = NULL;
#endif
	int status;

	/* Sequence of varBind */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_SEQ) {
		ND_PRINT((ndo, "[!SEQ of varbind]"));
		asn1_print(ndo, &elem);
		return;
	}
	if ((u_int)count < length)
		ND_PRINT((ndo, "[%d extra after SEQ of varbind]", length - count));
	/* descend */
	length = elem.asnlen;
	np = (const u_char *)elem.data.raw;

	for (ind = 1; length > 0; ind++) {
		const u_char *vbend;
		u_int vblength;

		ND_PRINT((ndo, " "));

		/* Sequence */
		if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
			return;
		if (elem.type != BE_SEQ) {
			ND_PRINT((ndo, "[!varbind]"));
			asn1_print(ndo, &elem);
			return;
		}
		vbend = np + count;
		vblength = length - count;
		/* descend */
		length = elem.asnlen;
		np = (const u_char *)elem.data.raw;

		/* objName (OID) */
		if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
			return;
		if (elem.type != BE_OID) {
			ND_PRINT((ndo, "[objName!=OID]"));
			asn1_print(ndo, &elem);
			return;
		}
#ifdef USE_LIBSMI
		smiNode = smi_print_variable(ndo, &elem, &status);
#else
		status = asn1_print(ndo, &elem);
#endif
		if (status < 0)
			return;
		length -= count;
		np += count;

		if (pduid != GETREQ && pduid != GETNEXTREQ
		    && pduid != GETBULKREQ)
			ND_PRINT((ndo, "="));

		/* objVal (ANY) */
		if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
			return;
		if (pduid == GETREQ || pduid == GETNEXTREQ
		    || pduid == GETBULKREQ) {
			if (elem.type != BE_NULL) {
				ND_PRINT((ndo, "[objVal!=NULL]"));
				if (asn1_print(ndo, &elem) < 0)
					return;
			}
		} else {
		        if (elem.type != BE_NULL) {
#ifdef USE_LIBSMI
				status = smi_print_value(ndo, smiNode, pduid, &elem);
#else
				status = asn1_print(ndo, &elem);
#endif
			}
			if (status < 0)
				return;
		}
		length = vblength;
		np = vbend;
	}
}

/*
 * Decode SNMP PDUs: GetRequest, GetNextRequest, GetResponse, SetRequest,
 * GetBulk, Inform, V2Trap, and Report
 */
static void
snmppdu_print(netdissect_options *ndo,
              u_short pduid, const u_char *np, u_int length)
{
	struct be elem;
	int count = 0, error_status;

	/* reqId (Integer) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[reqId!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, "R=%d ", elem.data.integer));
	length -= count;
	np += count;

	/* errorStatus (Integer) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[errorStatus!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	error_status = 0;
	if ((pduid == GETREQ || pduid == GETNEXTREQ || pduid == SETREQ
	    || pduid == INFORMREQ || pduid == V2TRAP || pduid == REPORT)
	    && elem.data.integer != 0) {
		char errbuf[20];
		ND_PRINT((ndo, "[errorStatus(%s)!=0]",
			DECODE_ErrorStatus(elem.data.integer)));
	} else if (pduid == GETBULKREQ) {
		ND_PRINT((ndo, " N=%d", elem.data.integer));
	} else if (elem.data.integer != 0) {
		char errbuf[20];
		ND_PRINT((ndo, " %s", DECODE_ErrorStatus(elem.data.integer)));
		error_status = elem.data.integer;
	}
	length -= count;
	np += count;

	/* errorIndex (Integer) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[errorIndex!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	if ((pduid == GETREQ || pduid == GETNEXTREQ || pduid == SETREQ
	    || pduid == INFORMREQ || pduid == V2TRAP || pduid == REPORT)
	    && elem.data.integer != 0)
		ND_PRINT((ndo, "[errorIndex(%d)!=0]", elem.data.integer));
	else if (pduid == GETBULKREQ)
		ND_PRINT((ndo, " M=%d", elem.data.integer));
	else if (elem.data.integer != 0) {
		if (!error_status)
			ND_PRINT((ndo, "[errorIndex(%d) w/o errorStatus]", elem.data.integer));
		else
			ND_PRINT((ndo, "@%d", elem.data.integer));
	} else if (error_status) {
		ND_PRINT((ndo, "[errorIndex==0]"));
	}
	length -= count;
	np += count;

	varbind_print(ndo, pduid, np, length);
	return;
}

/*
 * Decode SNMP Trap PDU
 */
static void
trappdu_print(netdissect_options *ndo,
              const u_char *np, u_int length)
{
	struct be elem;
	int count = 0, generic;

	ND_PRINT((ndo, " "));

	/* enterprise (oid) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_OID) {
		ND_PRINT((ndo, "[enterprise!=OID]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (asn1_print(ndo, &elem) < 0)
		return;
	length -= count;
	np += count;

	ND_PRINT((ndo, " "));

	/* agent-addr (inetaddr) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INETADDR) {
		ND_PRINT((ndo, "[agent-addr!=INETADDR]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (asn1_print(ndo, &elem) < 0)
		return;
	length -= count;
	np += count;

	/* generic-trap (Integer) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[generic-trap!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	generic = elem.data.integer;
	{
		char buf[20];
		ND_PRINT((ndo, " %s", DECODE_GenericTrap(generic)));
	}
	length -= count;
	np += count;

	/* specific-trap (Integer) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[specific-trap!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (generic != GT_ENTERPRISE) {
		if (elem.data.integer != 0)
			ND_PRINT((ndo, "[specific-trap(%d)!=0]", elem.data.integer));
	} else
		ND_PRINT((ndo, " s=%d", elem.data.integer));
	length -= count;
	np += count;

	ND_PRINT((ndo, " "));

	/* time-stamp (TimeTicks) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_UNS) {			/* XXX */
		ND_PRINT((ndo, "[time-stamp!=TIMETICKS]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (asn1_print(ndo, &elem) < 0)
		return;
	length -= count;
	np += count;

	varbind_print(ndo, TRAP, np, length);
	return;
}

/*
 * Decode arbitrary SNMP PDUs.
 */
static void
pdu_print(netdissect_options *ndo,
          const u_char *np, u_int length, int version)
{
	struct be pdu;
	int count = 0;

	/* PDU (Context) */
	if ((count = asn1_parse(ndo, np, length, &pdu)) < 0)
		return;
	if (pdu.type != BE_PDU) {
		ND_PRINT((ndo, "[no PDU]"));
		return;
	}
	if ((u_int)count < length)
		ND_PRINT((ndo, "[%d extra after PDU]", length - count));
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "{ "));
	}
	if (asn1_print(ndo, &pdu) < 0)
		return;
	ND_PRINT((ndo, " "));
	/* descend into PDU */
	length = pdu.asnlen;
	np = (const u_char *)pdu.data.raw;

	if (version == SNMP_VERSION_1 &&
	    (pdu.id == GETBULKREQ || pdu.id == INFORMREQ ||
	     pdu.id == V2TRAP || pdu.id == REPORT)) {
	        ND_PRINT((ndo, "[v2 PDU in v1 message]"));
		return;
	}

	if (version == SNMP_VERSION_2 && pdu.id == TRAP) {
		ND_PRINT((ndo, "[v1 PDU in v2 message]"));
		return;
	}

	switch (pdu.id) {
	case TRAP:
		trappdu_print(ndo, np, length);
		break;
	case GETREQ:
	case GETNEXTREQ:
	case GETRESP:
	case SETREQ:
	case GETBULKREQ:
	case INFORMREQ:
	case V2TRAP:
	case REPORT:
		snmppdu_print(ndo, pdu.id, np, length);
		break;
	}

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " } "));
	}
}

/*
 * Decode a scoped SNMP PDU.
 */
static void
scopedpdu_print(netdissect_options *ndo,
                const u_char *np, u_int length, int version)
{
	struct be elem;
	int count = 0;

	/* Sequence */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_SEQ) {
		ND_PRINT((ndo, "[!scoped PDU]"));
		asn1_print(ndo, &elem);
		return;
	}
	length = elem.asnlen;
	np = (const u_char *)elem.data.raw;

	/* contextEngineID (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[contextEngineID!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	ND_PRINT((ndo, "E="));
	if (asn1_print_octets(ndo, &elem) == -1)
		return;
	ND_PRINT((ndo, " "));

	/* contextName (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[contextName!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	ND_PRINT((ndo, "C="));
	if (asn1_print_string(ndo, &elem) == -1)
		return;
	ND_PRINT((ndo, " "));

	pdu_print(ndo, np, length, version);
}

/*
 * Decode SNMP Community Header (SNMPv1 and SNMPv2c)
 */
static void
community_print(netdissect_options *ndo,
                const u_char *np, u_int length, int version)
{
	struct be elem;
	int count = 0;

	/* Community (String) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[comm!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	/* default community */
	if (!(elem.asnlen == sizeof(DEF_COMMUNITY) - 1 &&
	    strncmp((const char *)elem.data.str, DEF_COMMUNITY,
	            sizeof(DEF_COMMUNITY) - 1) == 0)) {
		/* ! "public" */
		ND_PRINT((ndo, "C="));
		if (asn1_print_string(ndo, &elem) == -1)
			return;
		ND_PRINT((ndo, " "));
	}
	length -= count;
	np += count;

	pdu_print(ndo, np, length, version);
}

/*
 * Decode SNMPv3 User-based Security Message Header (SNMPv3)
 */
static void
usm_print(netdissect_options *ndo,
          const u_char *np, u_int length)
{
        struct be elem;
	int count = 0;

	/* Sequence */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_SEQ) {
		ND_PRINT((ndo, "[!usm]"));
		asn1_print(ndo, &elem);
		return;
	}
	length = elem.asnlen;
	np = (const u_char *)elem.data.raw;

	/* msgAuthoritativeEngineID (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgAuthoritativeEngineID!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	/* msgAuthoritativeEngineBoots (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[msgAuthoritativeEngineBoots!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, "B=%d ", elem.data.integer));
	length -= count;
	np += count;

	/* msgAuthoritativeEngineTime (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[msgAuthoritativeEngineTime!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, "T=%d ", elem.data.integer));
	length -= count;
	np += count;

	/* msgUserName (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgUserName!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
        np += count;

	ND_PRINT((ndo, "U="));
	if (asn1_print_string(ndo, &elem) == -1)
		return;
	ND_PRINT((ndo, " "));

	/* msgAuthenticationParameters (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgAuthenticationParameters!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
        np += count;

	/* msgPrivacyParameters (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgPrivacyParameters!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
        np += count;

	if ((u_int)count < length)
		ND_PRINT((ndo, "[%d extra after usm SEQ]", length - count));
}

/*
 * Decode SNMPv3 Message Header (SNMPv3)
 */
static void
v3msg_print(netdissect_options *ndo,
            const u_char *np, u_int length)
{
	struct be elem;
	int count = 0;
	u_char flags;
	int model;
	const u_char *xnp = np;
	int xlength = length;

	/* Sequence */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_SEQ) {
		ND_PRINT((ndo, "[!message]"));
		asn1_print(ndo, &elem);
		return;
	}
	length = elem.asnlen;
	np = (const u_char *)elem.data.raw;

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "{ "));
	}

	/* msgID (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[msgID!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	/* msgMaxSize (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[msgMaxSize!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	/* msgFlags (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgFlags!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	if (elem.asnlen != 1) {
		ND_PRINT((ndo, "[msgFlags size %d]", elem.asnlen));
		return;
	}
	flags = elem.data.str[0];
	if (flags != 0x00 && flags != 0x01 && flags != 0x03
	    && flags != 0x04 && flags != 0x05 && flags != 0x07) {
		ND_PRINT((ndo, "[msgFlags=0x%02X]", flags));
		return;
	}
	length -= count;
	np += count;

	ND_PRINT((ndo, "F=%s%s%s ",
	          flags & 0x01 ? "a" : "",
	          flags & 0x02 ? "p" : "",
	          flags & 0x04 ? "r" : ""));

	/* msgSecurityModel (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[msgSecurityModel!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}
	model = elem.data.integer;
	length -= count;
	np += count;

	if ((u_int)count < length)
		ND_PRINT((ndo, "[%d extra after message SEQ]", length - count));

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "} "));
	}

	if (model == 3) {
	    if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "{ USM "));
	    }
	} else {
	    ND_PRINT((ndo, "[security model %d]", model));
            return;
	}

	np = xnp + (np - xnp);
	length = xlength - (np - xnp);

	/* msgSecurityParameters (OCTET STRING) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_STR) {
		ND_PRINT((ndo, "[msgSecurityParameters!=STR]"));
		asn1_print(ndo, &elem);
		return;
	}
	length -= count;
	np += count;

	if (model == 3) {
	    usm_print(ndo, elem.data.str, elem.asnlen);
	    if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "} "));
	    }
	}

	if (ndo->ndo_vflag) {
	    ND_PRINT((ndo, "{ ScopedPDU "));
	}

	scopedpdu_print(ndo, np, length, 3);

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "} "));
	}
}

/*
 * Decode SNMP header and pass on to PDU printing routines
 */
void
snmp_print(netdissect_options *ndo,
           const u_char *np, u_int length)
{
	struct be elem;
	int count = 0;
	int version = 0;

	ND_PRINT((ndo, " "));

	/* initial Sequence */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_SEQ) {
		ND_PRINT((ndo, "[!init SEQ]"));
		asn1_print(ndo, &elem);
		return;
	}
	if ((u_int)count < length)
		ND_PRINT((ndo, "[%d extra after iSEQ]", length - count));
	/* descend */
	length = elem.asnlen;
	np = (const u_char *)elem.data.raw;

	/* Version (INTEGER) */
	if ((count = asn1_parse(ndo, np, length, &elem)) < 0)
		return;
	if (elem.type != BE_INT) {
		ND_PRINT((ndo, "[version!=INT]"));
		asn1_print(ndo, &elem);
		return;
	}

	switch (elem.data.integer) {
	case SNMP_VERSION_1:
	case SNMP_VERSION_2:
	case SNMP_VERSION_3:
		if (ndo->ndo_vflag)
			ND_PRINT((ndo, "{ %s ", SnmpVersion[elem.data.integer]));
		break;
	default:
	        ND_PRINT((ndo, "SNMP [version = %d]", elem.data.integer));
		return;
	}
	version = elem.data.integer;
	length -= count;
	np += count;

	switch (version) {
	case SNMP_VERSION_1:
        case SNMP_VERSION_2:
		community_print(ndo, np, length, version);
		break;
	case SNMP_VERSION_3:
		v3msg_print(ndo, np, length);
		break;
	default:
		ND_PRINT((ndo, "[version = %d]", elem.data.integer));
		break;
	}

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, "} "));
	}
}
