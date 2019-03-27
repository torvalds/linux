/*
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
 * Copyright (c) 2009 Mojatatu Networks, Inc
 *
 */

/* \summary: Forwarding and Control Element Separation (ForCES) Protocol printer */

/* specification: RFC 5810 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = "[|forces]";

#define	ForCES_VERS	1
#define	ForCES_HDRL	24
#define	ForCES_ALNL	4U
#define TLV_HDRL	4
#define ILV_HDRL	8

#define TOM_RSVD 	0x0
#define TOM_ASSNSETUP 	0x1
#define TOM_ASSNTEARD 	0x2
#define TOM_CONFIG 	0x3
#define TOM_QUERY 	0x4
#define TOM_EVENTNOT 	0x5
#define TOM_PKTREDIR 	0x6
#define TOM_HEARTBT 	0x0F
#define TOM_ASSNSETREP 	0x11
#define TOM_CONFIGREP 	0x13
#define TOM_QUERYREP 	0x14

/*
 * tom_h Flags: resv1(8b):maxtlvs(4b):resv2(2b):mintlv(2b)
*/
#define ZERO_TTLV	0x01
#define ZERO_MORE_TTLV	0x02
#define ONE_MORE_TTLV	0x04
#define ZERO_TLV	0x00
#define ONE_TLV		0x10
#define TWO_TLV		0x20
#define MAX_TLV		0xF0

#define TTLV_T1		(ONE_MORE_TTLV|ONE_TLV)
#define TTLV_T2		(ONE_MORE_TTLV|MAX_TLV)

struct tom_h {
	uint32_t v;
	uint16_t flags;
	uint16_t op_msk;
	const char *s;
	int (*print) (netdissect_options *ndo, register const u_char * pptr, register u_int len,
		      uint16_t op_msk, int indent);
};

enum {
	TOM_RSV_I,
	TOM_ASS_I,
	TOM_AST_I,
	TOM_CFG_I,
	TOM_QRY_I,
	TOM_EVN_I,
	TOM_RED_I,
	TOM_HBT_I,
	TOM_ASR_I,
	TOM_CNR_I,
	TOM_QRR_I,
	_TOM_RSV_MAX
};
#define TOM_MAX_IND (_TOM_RSV_MAX - 1)

static inline int tom_valid(uint8_t tom)
{
	if (tom > 0) {
		if (tom >= 0x7 && tom <= 0xe)
			return 0;
		if (tom == 0x10)
			return 0;
		if (tom > 0x14)
			return 0;
		return 1;
	} else
		return 0;
}

static inline const char *ForCES_node(uint32_t node)
{
	if (node <= 0x3FFFFFFF)
		return "FE";
	if (node >= 0x40000000 && node <= 0x7FFFFFFF)
		return "CE";
	if (node >= 0xC0000000 && node <= 0xFFFFFFEF)
		return "AllMulticast";
	if (node == 0xFFFFFFFD)
		return "AllCEsBroadcast";
	if (node == 0xFFFFFFFE)
		return "AllFEsBroadcast";
	if (node == 0xFFFFFFFF)
		return "AllBroadcast";

	return "ForCESreserved";

}

static const struct tok ForCES_ACKs[] = {
	{0x0, "NoACK"},
	{0x1, "SuccessACK"},
	{0x2, "FailureACK"},
	{0x3, "AlwaysACK"},
	{0, NULL}
};

static const struct tok ForCES_EMs[] = {
	{0x0, "EMReserved"},
	{0x1, "execute-all-or-none"},
	{0x2, "execute-until-failure"},
	{0x3, "continue-execute-on-failure"},
	{0, NULL}
};

static const struct tok ForCES_ATs[] = {
	{0x0, "Standalone"},
	{0x1, "2PCtransaction"},
	{0, NULL}
};

static const struct tok ForCES_TPs[] = {
	{0x0, "StartofTransaction"},
	{0x1, "MiddleofTransaction"},
	{0x2, "EndofTransaction"},
	{0x3, "abort"},
	{0, NULL}
};

/*
 * Structure of forces header, naked of TLVs.
 */
struct forcesh {
	nd_uint8_t fm_vrsvd;	/* version and reserved */
#define ForCES_V(forcesh)	((forcesh)->fm_vrsvd >> 4)
	nd_uint8_t fm_tom;	/* type of message */
	nd_uint16_t fm_len;	/* total length * 4 bytes */
#define ForCES_BLN(forcesh)	((uint32_t)(EXTRACT_16BITS(&(forcesh)->fm_len) << 2))
	nd_uint32_t fm_sid;	/* Source ID */
#define ForCES_SID(forcesh)	EXTRACT_32BITS(&(forcesh)->fm_sid)
	nd_uint32_t fm_did;	/* Destination ID */
#define ForCES_DID(forcesh)	EXTRACT_32BITS(&(forcesh)->fm_did)
	nd_uint8_t fm_cor[8];	/* correlator */
	nd_uint32_t fm_flags;	/* flags */
#define ForCES_ACK(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0xC0000000) >> 30)
#define ForCES_PRI(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x38000000) >> 27)
#define ForCES_RS1(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x07000000) >> 24)
#define ForCES_EM(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x00C00000) >> 22)
#define ForCES_AT(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x00200000) >> 21)
#define ForCES_TP(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x00180000) >> 19)
#define ForCES_RS2(forcesh)	((EXTRACT_32BITS(&(forcesh)->fm_flags)&0x0007FFFF) >> 0)
};

#define ForCES_HLN_VALID(fhl,tlen) ((tlen) >= ForCES_HDRL && \
				   (fhl) >= ForCES_HDRL && \
				   (fhl) == (tlen))

#define F_LFB_RSVD 0x0
#define F_LFB_FEO 0x1
#define F_LFB_FEPO 0x2
static const struct tok ForCES_LFBs[] = {
	{F_LFB_RSVD, "Invalid TLV"},
	{F_LFB_FEO, "FEObj LFB"},
	{F_LFB_FEPO, "FEProtoObj LFB"},
	{0, NULL}
};

/* this is defined in RFC5810 section A.2 */
/*   http://www.iana.org/assignments/forces/forces.xhtml#oper-tlv-types */
enum {
	F_OP_RSV        = 0,
	F_OP_SET        = 1,
	F_OP_SETPROP    = 2,
	F_OP_SETRESP    = 3,
	F_OP_SETPRESP   = 4,
	F_OP_DEL        = 5,
	F_OP_DELRESP    = 6,
	F_OP_GET        = 7,
	F_OP_GETPROP    = 8,
	F_OP_GETRESP    = 9,
	F_OP_GETPRESP   = 10,
	F_OP_REPORT     = 11,
	F_OP_COMMIT     = 12,
	F_OP_RCOMMIT    = 13,
	F_OP_RTRCOMP    = 14,
	_F_OP_MAX
};
#define F_OP_MAX	(_F_OP_MAX - 1)

enum {
	B_OP_SET = 1 << (F_OP_SET - 1),
	B_OP_SETPROP = 1 << (F_OP_SETPROP - 1),
	B_OP_SETRESP = 1 << (F_OP_SETRESP - 1),
	B_OP_SETPRESP = 1 << (F_OP_SETPRESP - 1),
	B_OP_DEL = 1 << (F_OP_DEL - 1),
	B_OP_DELRESP = 1 << (F_OP_DELRESP - 1),
	B_OP_GET = 1 << (F_OP_GET - 1),
	B_OP_GETPROP = 1 << (F_OP_GETPROP - 1),
	B_OP_GETRESP = 1 << (F_OP_GETRESP - 1),
	B_OP_GETPRESP = 1 << (F_OP_GETPRESP - 1),
	B_OP_REPORT = 1 << (F_OP_REPORT - 1),
	B_OP_COMMIT = 1 << (F_OP_COMMIT - 1),
	B_OP_RCOMMIT = 1 << (F_OP_RCOMMIT - 1),
	B_OP_RTRCOMP = 1 << (F_OP_RTRCOMP - 1)
};

struct optlv_h {
	uint16_t flags;
	uint16_t op_msk;
	const char *s;
	int (*print) (netdissect_options *ndo, register const u_char * pptr, register u_int len,
		      uint16_t op_msk, int indent);
};

static int genoptlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			 uint16_t op_msk, int indent);
static int recpdoptlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			    uint16_t op_msk, int indent);
static int invoptlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			  uint16_t op_msk, int indent);

#define OP_MIN_SIZ 8
struct pathdata_h {
	nd_uint16_t pflags;
	nd_uint16_t pIDcnt;
};

#define	B_FULLD		0x1
#define	B_SPARD 	0x2
#define B_RESTV		0x4
#define B_KEYIN		0x8
#define B_APPND		0x10
#define B_TRNG		0x20

static const struct optlv_h OPTLV_msg[F_OP_MAX + 1] = {
	/* F_OP_RSV */ {ZERO_TTLV, 0, "Invalid OPTLV", invoptlv_print},
	/* F_OP_SET */ {TTLV_T2, B_FULLD | B_SPARD, " Set", recpdoptlv_print},
	/* F_OP_SETPROP */
	    {TTLV_T2, B_FULLD | B_SPARD, " SetProp", recpdoptlv_print},
	/* F_OP_SETRESP */ {TTLV_T2, B_RESTV, " SetResp", recpdoptlv_print},
	/* F_OP_SETPRESP */ {TTLV_T2, B_RESTV, " SetPropResp", recpdoptlv_print},
	/* F_OP_DEL */ {ZERO_TTLV, 0, " Del", recpdoptlv_print},
	/* F_OP_DELRESP */ {TTLV_T2, B_RESTV, " DelResp", recpdoptlv_print},
	/* F_OP_GET */ {ZERO_TTLV, 0, " Get", recpdoptlv_print},
	/* F_OP_GETPROP */ {ZERO_TTLV, 0, " GetProp", recpdoptlv_print},
	/* F_OP_GETRESP */
	    {TTLV_T2, B_FULLD | B_SPARD | B_RESTV, " GetResp", recpdoptlv_print},
	/* F_OP_GETPRESP */
	    {TTLV_T2, B_FULLD | B_RESTV, " GetPropResp", recpdoptlv_print},
	/* F_OP_REPORT */
	    {TTLV_T2, B_FULLD | B_SPARD, " Report", recpdoptlv_print},
	/* F_OP_COMMIT */ {ZERO_TTLV, 0, " Commit", NULL},
	/* F_OP_RCOMMIT */ {TTLV_T1, B_RESTV, " RCommit", genoptlv_print},
	/* F_OP_RTRCOMP */ {ZERO_TTLV, 0, " RTRCOMP", NULL},
};

static inline const struct optlv_h *get_forces_optlv_h(uint16_t opt)
{
	if (opt > F_OP_MAX || opt <= F_OP_RSV)
		return &OPTLV_msg[F_OP_RSV];

	return &OPTLV_msg[opt];
}

#define IND_SIZE 256
#define IND_CHR ' '
#define IND_PREF '\n'
#define IND_SUF 0x0
static char ind_buf[IND_SIZE];

static inline char *indent_pr(int indent, int nlpref)
{
	int i = 0;
	char *r = ind_buf;

	if (indent > (IND_SIZE - 1))
		indent = IND_SIZE - 1;

	if (nlpref) {
		r[i] = IND_PREF;
		i++;
		indent--;
	}

	while (--indent >= 0)
		r[i++] = IND_CHR;

	r[i] = IND_SUF;
	return r;
}

static inline int op_valid(uint16_t op, uint16_t mask)
{
	int opb = 1 << (op - 1);

	if (op == 0)
		return 0;
	if (opb & mask)
		return 1;
	/* I guess we should allow vendor operations? */
	if (op >= 0x8000)
		return 1;
	return 0;
}

#define F_TLV_RSVD	0x0000
#define F_TLV_REDR	0x0001
#define F_TLV_ASRS	0x0010
#define F_TLV_ASRT	0x0011
#define F_TLV_LFBS	0x1000
#define F_TLV_PDAT	0x0110
#define F_TLV_KEYI	0x0111
#define F_TLV_FULD	0x0112
#define F_TLV_SPAD	0x0113
#define F_TLV_REST	0x0114
#define F_TLV_METD	0x0115
#define F_TLV_REDD	0x0116
#define F_TLV_TRNG	0x0117


#define F_TLV_VNST	0x8000

static const struct tok ForCES_TLV[] = {
	{F_TLV_RSVD, "Invalid TLV"},
	{F_TLV_REDR, "REDIRECT TLV"},
	{F_TLV_ASRS, "ASResult TLV"},
	{F_TLV_ASRT, "ASTreason TLV"},
	{F_TLV_LFBS, "LFBselect TLV"},
	{F_TLV_PDAT, "PATH-DATA TLV"},
	{F_TLV_KEYI, "KEYINFO TLV"},
	{F_TLV_FULD, "FULLDATA TLV"},
	{F_TLV_SPAD, "SPARSEDATA TLV"},
	{F_TLV_REST, "RESULT TLV"},
	{F_TLV_METD, "METADATA TLV"},
	{F_TLV_REDD, "REDIRECTDATA TLV"},
	{0, NULL}
};

#define TLV_HLN	4
static inline int ttlv_valid(uint16_t ttlv)
{
	if (ttlv > 0) {
		if (ttlv == 1 || ttlv == 0x1000)
			return 1;
		if (ttlv >= 0x10 && ttlv <= 0x11)
			return 1;
		if (ttlv >= 0x110 && ttlv <= 0x116)
			return 1;
		if (ttlv >= 0x8000)
			return 0;	/* XXX: */
	}

	return 0;
}

struct forces_ilv {
	nd_uint32_t type;
	nd_uint32_t length;
};

struct forces_tlv {
	nd_uint16_t type;
	nd_uint16_t length;
};

#define F_ALN_LEN(len) ( ((len)+ForCES_ALNL-1) & ~(ForCES_ALNL-1) )
#define	GET_TOP_TLV(fhdr) ((const struct forces_tlv *)((fhdr) + sizeof (struct forcesh)))
#define TLV_SET_LEN(len)  (F_ALN_LEN(TLV_HDRL) + (len))
#define TLV_ALN_LEN(len)  F_ALN_LEN(TLV_SET_LEN(len))
#define TLV_RDAT_LEN(tlv) ((int)(EXTRACT_16BITS(&(tlv)->length) - TLV_SET_LEN(0))
#define TLV_DATA(tlvp)   ((const void*)(((const char*)(tlvp)) + TLV_SET_LEN(0)))
#define GO_NXT_TLV(tlv,rlen) ((rlen) -= F_ALN_LEN(EXTRACT_16BITS(&(tlv)->length)), \
		              (const struct forces_tlv*)(((const char*)(tlv)) \
				      + F_ALN_LEN(EXTRACT_16BITS(&(tlv)->length))))
#define ILV_SET_LEN(len)  (F_ALN_LEN(ILV_HDRL) + (len))
#define ILV_ALN_LEN(len)  F_ALN_LEN(ILV_SET_LEN(len))
#define ILV_RDAT_LEN(ilv) ((int)(EXTRACT_32BITS(&(ilv)->length)) - ILV_SET_LEN(0))
#define ILV_DATA(ilvp)   ((const void*)(((const char*)(ilvp)) + ILV_SET_LEN(0)))
#define GO_NXT_ILV(ilv,rlen) ((rlen) -= F_ALN_LEN(EXTRACT_32BITS(&(ilv)->length)), \
		              (const struct forces_ilv *)(((const char*)(ilv)) \
				      + F_ALN_LEN(EXTRACT_32BITS(&(ilv)->length))))
#define INVALID_RLEN 1
#define INVALID_STLN 2
#define INVALID_LTLN 3
#define INVALID_ALEN 4

static const struct tok ForCES_TLV_err[] = {
	{INVALID_RLEN, "Invalid total length"},
	{INVALID_STLN, "xLV too short"},
	{INVALID_LTLN, "xLV too long"},
	{INVALID_ALEN, "data padding missing"},
	{0, NULL}
};

static inline u_int tlv_valid(const struct forces_tlv *tlv, u_int rlen)
{
	if (rlen < TLV_HDRL)
		return INVALID_RLEN;
	if (EXTRACT_16BITS(&tlv->length) < TLV_HDRL)
		return INVALID_STLN;
	if (EXTRACT_16BITS(&tlv->length) > rlen)
		return INVALID_LTLN;
	if (rlen < F_ALN_LEN(EXTRACT_16BITS(&tlv->length)))
		return INVALID_ALEN;

	return 0;
}

static inline int ilv_valid(const struct forces_ilv *ilv, u_int rlen)
{
	if (rlen < ILV_HDRL)
		return INVALID_RLEN;
	if (EXTRACT_32BITS(&ilv->length) < ILV_HDRL)
		return INVALID_STLN;
	if (EXTRACT_32BITS(&ilv->length) > rlen)
		return INVALID_LTLN;
	if (rlen < F_ALN_LEN(EXTRACT_32BITS(&ilv->length)))
		return INVALID_ALEN;

	return 0;
}

static int lfbselect_print(netdissect_options *, register const u_char * pptr, register u_int len,
			   uint16_t op_msk, int indent);
static int redirect_print(netdissect_options *, register const u_char * pptr, register u_int len,
			  uint16_t op_msk, int indent);
static int asrtlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			uint16_t op_msk, int indent);
static int asttlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			uint16_t op_msk, int indent);

struct forces_lfbsh {
	nd_uint32_t class;
	nd_uint32_t instance;
};

#define ASSNS_OPS (B_OP_REPORT)
#define CFG_OPS	(B_OP_SET|B_OP_SETPROP|B_OP_DEL|B_OP_COMMIT|B_OP_RTRCOMP)
#define CFG_ROPS (B_OP_SETRESP|B_OP_SETPRESP|B_OP_DELRESP|B_OP_RCOMMIT)
#define CFG_QY (B_OP_GET|B_OP_GETPROP)
#define CFG_QYR (B_OP_GETRESP|B_OP_GETPRESP)
#define CFG_EVN (B_OP_REPORT)

static const struct tom_h ForCES_msg[TOM_MAX_IND + 1] = {
	/* TOM_RSV_I */ {TOM_RSVD, ZERO_TTLV, 0, "Invalid message", NULL},
	/* TOM_ASS_I */ {TOM_ASSNSETUP, ZERO_MORE_TTLV | TWO_TLV, ASSNS_OPS,
		       "Association Setup", lfbselect_print},
	/* TOM_AST_I */
	    {TOM_ASSNTEARD, TTLV_T1, 0, "Association TearDown", asttlv_print},
	/* TOM_CFG_I */ {TOM_CONFIG, TTLV_T2, CFG_OPS, "Config", lfbselect_print},
	/* TOM_QRY_I */ {TOM_QUERY, TTLV_T2, CFG_QY, "Query", lfbselect_print},
	/* TOM_EVN_I */ {TOM_EVENTNOT, TTLV_T1, CFG_EVN, "Event Notification",
		       lfbselect_print},
	/* TOM_RED_I */
	    {TOM_PKTREDIR, TTLV_T2, 0, "Packet Redirect", redirect_print},
	/* TOM_HBT_I */ {TOM_HEARTBT, ZERO_TTLV, 0, "HeartBeat", NULL},
	/* TOM_ASR_I */
	    {TOM_ASSNSETREP, TTLV_T1, 0, "Association Response", asrtlv_print},
	/* TOM_CNR_I */ {TOM_CONFIGREP, TTLV_T2, CFG_ROPS, "Config Response",
		       lfbselect_print},
	/* TOM_QRR_I */
	    {TOM_QUERYREP, TTLV_T2, CFG_QYR, "Query Response", lfbselect_print},
};

static inline const struct tom_h *get_forces_tom(uint8_t tom)
{
	int i;
	for (i = TOM_RSV_I; i <= TOM_MAX_IND; i++) {
		const struct tom_h *th = &ForCES_msg[i];
		if (th->v == tom)
			return th;
	}
	return &ForCES_msg[TOM_RSV_I];
}

struct pdata_ops {
	uint32_t v;
	uint16_t flags;
	uint16_t op_msk;
	const char *s;
	int (*print) (netdissect_options *, register const u_char * pptr, register u_int len,
		      uint16_t op_msk, int indent);
};

enum {
	PD_RSV_I,
	PD_SEL_I,
	PD_FDT_I,
	PD_SDT_I,
	PD_RES_I,
	PD_PDT_I,
	_PD_RSV_MAX
};
#define PD_MAX_IND (_TOM_RSV_MAX - 1)

static inline int pd_valid(uint16_t pd)
{
	if (pd >= F_TLV_PDAT && pd <= F_TLV_REST)
		return 1;
	return 0;
}

static inline void
chk_op_type(netdissect_options *ndo,
            uint16_t type, uint16_t msk, uint16_t omsk)
{
	if (type != F_TLV_PDAT) {
		if (msk & B_KEYIN) {
			if (type != F_TLV_KEYI) {
				ND_PRINT((ndo, "Based on flags expected KEYINFO TLV!\n"));
			}
		} else {
			if (!(msk & omsk)) {
				ND_PRINT((ndo, "Illegal DATA encoding for type 0x%x programmed %x got %x \n",
				          type, omsk, msk));
			}
		}
	}

}

#define F_SELKEY 1
#define F_SELTABRANGE 2
#define F_TABAPPEND 4

struct res_val {
	nd_uint8_t result;
	nd_uint8_t resv1;
	nd_uint16_t resv2;
};

static int prestlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			 uint16_t op_msk, int indent);
static int pkeyitlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			  uint16_t op_msk, int indent);
static int fdatatlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			  uint16_t op_msk, int indent);
static int sdatatlv_print(netdissect_options *, register const u_char * pptr, register u_int len,
			  uint16_t op_msk, int indent);

static const struct pdata_ops ForCES_pdata[PD_MAX_IND + 1] = {
	/* PD_RSV_I */ {0, 0, 0, "Invalid message", NULL},
	/* PD_SEL_I */ {F_TLV_KEYI, 0, 0, "KEYINFO TLV", pkeyitlv_print},
	/* PD_FDT_I */ {F_TLV_FULD, 0, B_FULLD, "FULLDATA TLV", fdatatlv_print},
	/* PD_SDT_I */ {F_TLV_SPAD, 0, B_SPARD, "SPARSEDATA TLV", sdatatlv_print},
	/* PD_RES_I */ {F_TLV_REST, 0, B_RESTV, "RESULT TLV", prestlv_print},
	/* PD_PDT_I */
	    {F_TLV_PDAT, 0, 0, "Inner PATH-DATA TLV", recpdoptlv_print},
};

static inline const struct pdata_ops *get_forces_pd(uint16_t pd)
{
	int i;
	for (i = PD_RSV_I + 1; i <= PD_MAX_IND; i++) {
		const struct pdata_ops *pdo = &ForCES_pdata[i];
		if (pdo->v == pd)
			return pdo;
	}
	return &ForCES_pdata[TOM_RSV_I];
}

enum {
	E_SUCCESS,
	E_INVALID_HEADER,
	E_LENGTH_MISMATCH,
	E_VERSION_MISMATCH,
	E_INVALID_DESTINATION_PID,
	E_LFB_UNKNOWN,
	E_LFB_NOT_FOUND,
	E_LFB_INSTANCE_ID_NOT_FOUND,
	E_INVALID_PATH,
	E_COMPONENT_DOES_NOT_EXIST,
	E_EXISTS,
	E_NOT_FOUND,
	E_READ_ONLY,
	E_INVALID_ARRAY_CREATION,
	E_VALUE_OUT_OF_RANGE,
	E_CONTENTS_TOO_LONG,
	E_INVALID_PARAMETERS,
	E_INVALID_MESSAGE_TYPE,
	E_INVALID_FLAGS,
	E_INVALID_TLV,
	E_EVENT_ERROR,
	E_NOT_SUPPORTED,
	E_MEMORY_ERROR,
	E_INTERNAL_ERROR,
	/* 0x18-0xFE are reserved .. */
	E_UNSPECIFIED_ERROR = 0XFF
};

static const struct tok ForCES_errs[] = {
	{E_SUCCESS, "SUCCESS"},
	{E_INVALID_HEADER, "INVALID HEADER"},
	{E_LENGTH_MISMATCH, "LENGTH MISMATCH"},
	{E_VERSION_MISMATCH, "VERSION MISMATCH"},
	{E_INVALID_DESTINATION_PID, "INVALID DESTINATION PID"},
	{E_LFB_UNKNOWN, "LFB UNKNOWN"},
	{E_LFB_NOT_FOUND, "LFB NOT FOUND"},
	{E_LFB_INSTANCE_ID_NOT_FOUND, "LFB INSTANCE ID NOT FOUND"},
	{E_INVALID_PATH, "INVALID PATH"},
	{E_COMPONENT_DOES_NOT_EXIST, "COMPONENT DOES NOT EXIST"},
	{E_EXISTS, "EXISTS ALREADY"},
	{E_NOT_FOUND, "NOT FOUND"},
	{E_READ_ONLY, "READ ONLY"},
	{E_INVALID_ARRAY_CREATION, "INVALID ARRAY CREATION"},
	{E_VALUE_OUT_OF_RANGE, "VALUE OUT OF RANGE"},
	{E_CONTENTS_TOO_LONG, "CONTENTS TOO LONG"},
	{E_INVALID_PARAMETERS, "INVALID PARAMETERS"},
	{E_INVALID_MESSAGE_TYPE, "INVALID MESSAGE TYPE"},
	{E_INVALID_FLAGS, "INVALID FLAGS"},
	{E_INVALID_TLV, "INVALID TLV"},
	{E_EVENT_ERROR, "EVENT ERROR"},
	{E_NOT_SUPPORTED, "NOT SUPPORTED"},
	{E_MEMORY_ERROR, "MEMORY ERROR"},
	{E_INTERNAL_ERROR, "INTERNAL ERROR"},
	{E_UNSPECIFIED_ERROR, "UNSPECIFIED ERROR"},
	{0, NULL}
};

#define RESLEN	4

static int
prestlv_print(netdissect_options *ndo,
              register const u_char * pptr, register u_int len,
              uint16_t op_msk _U_, int indent)
{
	const struct forces_tlv *tlv = (const struct forces_tlv *)pptr;
	register const u_char *tdp = (const u_char *) TLV_DATA(tlv);
	const struct res_val *r = (const struct res_val *)tdp;
	u_int dlen;

	/*
	 * pdatacnt_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	if (dlen != RESLEN) {
		ND_PRINT((ndo, "illegal RESULT-TLV: %d bytes!\n", dlen));
		return -1;
	}

	ND_TCHECK(*r);
	if (r->result >= 0x18 && r->result <= 0xFE) {
		ND_PRINT((ndo, "illegal reserved result code: 0x%x!\n", r->result));
		return -1;
	}

	if (ndo->ndo_vflag >= 3) {
		char *ib = indent_pr(indent, 0);
		ND_PRINT((ndo, "%s  Result: %s (code 0x%x)\n", ib,
		       tok2str(ForCES_errs, NULL, r->result), r->result));
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
fdatatlv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk _U_, int indent)
{
	const struct forces_tlv *tlv = (const struct forces_tlv *)pptr;
	u_int rlen;
	register const u_char *tdp = (const u_char *) TLV_DATA(tlv);
	uint16_t type;

	/*
	 * pdatacnt_print() or pkeyitlv_print() has ensured that len
	 * (the TLV length) >= TLV_HDRL.
	 */
	rlen = len - TLV_HDRL;
	ND_TCHECK(*tlv);
	type = EXTRACT_16BITS(&tlv->type);
	if (type != F_TLV_FULD) {
		ND_PRINT((ndo, "Error: expecting FULLDATA!\n"));
		return -1;
	}

	if (ndo->ndo_vflag >= 3) {
		char *ib = indent_pr(indent + 2, 1);
		ND_PRINT((ndo, "%s[", &ib[1]));
		hex_print_with_offset(ndo, ib, tdp, rlen, 0);
		ND_PRINT((ndo, "\n%s]\n", &ib[1]));
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
sdatailv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk _U_, int indent)
{
	u_int rlen;
	const struct forces_ilv *ilv = (const struct forces_ilv *)pptr;
	int invilv;

	if (len < ILV_HDRL) {
		ND_PRINT((ndo, "Error: BAD SPARSEDATA-TLV!\n"));
		return -1;
	}
	rlen = len;
	indent += 1;
	while (rlen != 0) {
#if 0
		ND_PRINT((ndo, "Jamal - outstanding length <%d>\n", rlen));
#endif
		char *ib = indent_pr(indent, 1);
		register const u_char *tdp = (const u_char *) ILV_DATA(ilv);
		ND_TCHECK(*ilv);
		invilv = ilv_valid(ilv, rlen);
		if (invilv) {
			ND_PRINT((ndo, "%s[", &ib[1]));
			hex_print_with_offset(ndo, ib, tdp, rlen, 0);
			ND_PRINT((ndo, "\n%s]\n", &ib[1]));
			return -1;
		}
		if (ndo->ndo_vflag >= 3) {
			int ilvl = EXTRACT_32BITS(&ilv->length);
			ND_PRINT((ndo, "\n%s ILV: type %x length %d\n", &ib[1],
			       EXTRACT_32BITS(&ilv->type), ilvl));
			hex_print_with_offset(ndo, "\t\t[", tdp, ilvl-ILV_HDRL, 0);
		}

		ilv = GO_NXT_ILV(ilv, rlen);
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
sdatatlv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk, int indent)
{
	const struct forces_tlv *tlv = (const struct forces_tlv *)pptr;
	u_int rlen;
	register const u_char *tdp = (const u_char *) TLV_DATA(tlv);
	uint16_t type;

	/*
	 * pdatacnt_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	rlen = len - TLV_HDRL;
	ND_TCHECK(*tlv);
	type = EXTRACT_16BITS(&tlv->type);
	if (type != F_TLV_SPAD) {
		ND_PRINT((ndo, "Error: expecting SPARSEDATA!\n"));
		return -1;
	}

	return sdatailv_print(ndo, tdp, rlen, op_msk, indent);

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
pkeyitlv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk, int indent)
{
	const struct forces_tlv *tlv = (const struct forces_tlv *)pptr;
	register const u_char *tdp = (const u_char *) TLV_DATA(tlv);
	register const u_char *dp = tdp + 4;
	const struct forces_tlv *kdtlv = (const struct forces_tlv *)dp;
	uint32_t id;
	char *ib = indent_pr(indent, 0);
	uint16_t type, tll;
	u_int invtlv;

	ND_TCHECK(*tdp);
	id = EXTRACT_32BITS(tdp);
	ND_PRINT((ndo, "%sKeyinfo: Key 0x%x\n", ib, id));
	ND_TCHECK(*kdtlv);
	type = EXTRACT_16BITS(&kdtlv->type);
	invtlv = tlv_valid(kdtlv, len);

	if (invtlv) {
		ND_PRINT((ndo, "%s TLV type 0x%x len %d\n",
		       tok2str(ForCES_TLV_err, NULL, invtlv), type,
		       EXTRACT_16BITS(&kdtlv->length)));
		return -1;
	}
	/*
	 * At this point, tlv_valid() has ensured that the TLV
	 * length is large enough but not too large (it doesn't
	 * go past the end of the containing TLV).
	 */
	tll = EXTRACT_16BITS(&kdtlv->length);
	dp = (const u_char *) TLV_DATA(kdtlv);
	return fdatatlv_print(ndo, dp, tll, op_msk, indent);

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#define PTH_DESC_SIZE 12

static int
pdatacnt_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t IDcnt, uint16_t op_msk, int indent)
{
	u_int i;
	uint32_t id;
	char *ib = indent_pr(indent, 0);

	if ((op_msk & B_APPND) && ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "%sTABLE APPEND\n", ib));
	}
	for (i = 0; i < IDcnt; i++) {
		ND_TCHECK2(*pptr, 4);
		if (len < 4)
			goto trunc;
		id = EXTRACT_32BITS(pptr);
		if (ndo->ndo_vflag >= 3)
			ND_PRINT((ndo, "%sID#%02u: %d\n", ib, i + 1, id));
		len -= 4;
		pptr += 4;
	}

	if ((op_msk & B_TRNG) || (op_msk & B_KEYIN)) {
		if (op_msk & B_TRNG) {
			uint32_t starti, endi;

			if (len < PTH_DESC_SIZE) {
				ND_PRINT((ndo, "pathlength %d with key/range too short %d\n",
				       len, PTH_DESC_SIZE));
				return -1;
			}

			pptr += sizeof(struct forces_tlv);
			len -= sizeof(struct forces_tlv);

			starti = EXTRACT_32BITS(pptr);
			pptr += 4;
			len -= 4;

			endi = EXTRACT_32BITS(pptr);
			pptr += 4;
			len -= 4;

			if (ndo->ndo_vflag >= 3)
				ND_PRINT((ndo, "%sTable range: [%d,%d]\n", ib, starti, endi));
		}

		if (op_msk & B_KEYIN) {
			const struct forces_tlv *keytlv;
			uint16_t tll;

			if (len < PTH_DESC_SIZE) {
				ND_PRINT((ndo, "pathlength %d with key/range too short %d\n",
				       len, PTH_DESC_SIZE));
				return -1;
			}

			/* skip keyid */
			pptr += 4;
			len -= 4;
			keytlv = (const struct forces_tlv *)pptr;
			/* skip header */
			pptr += sizeof(struct forces_tlv);
			len -= sizeof(struct forces_tlv);
			/* skip key content */
			tll = EXTRACT_16BITS(&keytlv->length);
			if (tll < TLV_HDRL) {
				ND_PRINT((ndo, "key content length %u < %u\n",
					tll, TLV_HDRL));
				return -1;
			}
			tll -= TLV_HDRL;
			if (len < tll) {
				ND_PRINT((ndo, "key content too short\n"));
				return -1;
			}
			pptr += tll;
			len -= tll;
		}

	}

	if (len) {
		const struct forces_tlv *pdtlv = (const struct forces_tlv *)pptr;
		uint16_t type;
		uint16_t tll;
		int pad = 0;
		u_int aln;
		u_int invtlv;

		ND_TCHECK(*pdtlv);
		type = EXTRACT_16BITS(&pdtlv->type);
		invtlv = tlv_valid(pdtlv, len);
		if (invtlv) {
			ND_PRINT((ndo, "%s Outstanding bytes %d for TLV type 0x%x TLV len %d\n",
			          tok2str(ForCES_TLV_err, NULL, invtlv), len, type,
			          EXTRACT_16BITS(&pdtlv->length)));
			goto pd_err;
		}
		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		tll = EXTRACT_16BITS(&pdtlv->length) - TLV_HDRL;
		aln = F_ALN_LEN(EXTRACT_16BITS(&pdtlv->length));
		if (aln > EXTRACT_16BITS(&pdtlv->length)) {
			if (aln > len) {
				ND_PRINT((ndo,
				          "Invalid padded pathdata TLV type 0x%x len %d missing %d pad bytes\n",
				          type, EXTRACT_16BITS(&pdtlv->length), aln - len));
			} else {
				pad = aln - EXTRACT_16BITS(&pdtlv->length);
			}
		}
		if (pd_valid(type)) {
			const struct pdata_ops *ops = get_forces_pd(type);

			if (ndo->ndo_vflag >= 3 && ops->v != F_TLV_PDAT) {
				if (pad)
					ND_PRINT((ndo, "%s  %s (Length %d DataLen %d pad %d Bytes)\n",
					          ib, ops->s, EXTRACT_16BITS(&pdtlv->length), tll, pad));
				else
					ND_PRINT((ndo, "%s  %s (Length %d DataLen %d Bytes)\n",
					          ib, ops->s, EXTRACT_16BITS(&pdtlv->length), tll));
			}

			chk_op_type(ndo, type, op_msk, ops->op_msk);

			if (ops->print(ndo, (const u_char *)pdtlv,
					tll + pad + TLV_HDRL, op_msk,
					indent + 2) == -1)
				return -1;
			len -= (TLV_HDRL + pad + tll);
		} else {
			ND_PRINT((ndo, "Invalid path data content type 0x%x len %d\n",
			       type, EXTRACT_16BITS(&pdtlv->length)));
pd_err:
			if (EXTRACT_16BITS(&pdtlv->length)) {
                                hex_print_with_offset(ndo, "Bad Data val\n\t  [",
						      pptr, len, 0);
				ND_PRINT((ndo, "]\n"));

				return -1;
			}
		}
	}
	return len;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
pdata_print(netdissect_options *ndo,
            register const u_char * pptr, register u_int len,
            uint16_t op_msk, int indent)
{
	const struct pathdata_h *pdh = (const struct pathdata_h *)pptr;
	char *ib = indent_pr(indent, 0);
	u_int minsize = 0;
	int more_pd = 0;
	uint16_t idcnt = 0;

	ND_TCHECK(*pdh);
	if (len < sizeof(struct pathdata_h))
		goto trunc;
	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "\n%sPathdata: Flags 0x%x ID count %d\n",
		       ib, EXTRACT_16BITS(&pdh->pflags), EXTRACT_16BITS(&pdh->pIDcnt)));
	}

	if (EXTRACT_16BITS(&pdh->pflags) & F_SELKEY) {
		op_msk |= B_KEYIN;
	}

	/* Table GET Range operation */
	if (EXTRACT_16BITS(&pdh->pflags) & F_SELTABRANGE) {
		op_msk |= B_TRNG;
	}
	/* Table SET append operation */
	if (EXTRACT_16BITS(&pdh->pflags) & F_TABAPPEND) {
		op_msk |= B_APPND;
	}

	pptr += sizeof(struct pathdata_h);
	len -= sizeof(struct pathdata_h);
	idcnt = EXTRACT_16BITS(&pdh->pIDcnt);
	minsize = idcnt * 4;
	if (len < minsize) {
		ND_PRINT((ndo, "\t\t\ttruncated IDs expected %uB got %uB\n", minsize,
		       len));
		hex_print_with_offset(ndo, "\t\t\tID Data[", pptr, len, 0);
		ND_PRINT((ndo, "]\n"));
		return -1;
	}

	if ((op_msk & B_TRNG) && (op_msk & B_KEYIN)) {
		ND_PRINT((ndo, "\t\t\tIllegal to have both Table ranges and keys\n"));
		return -1;
	}

	more_pd = pdatacnt_print(ndo, pptr, len, idcnt, op_msk, indent);
	if (more_pd > 0) {
		int consumed = len - more_pd;
		pptr += consumed;
		len = more_pd;
		/* XXX: Argh, recurse some more */
		return recpdoptlv_print(ndo, pptr, len, op_msk, indent+1);
	} else
		return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
genoptlv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk, int indent)
{
	const struct forces_tlv *pdtlv = (const struct forces_tlv *)pptr;
	uint16_t type;
	int tll;
	u_int invtlv;
	char *ib = indent_pr(indent, 0);

	ND_TCHECK(*pdtlv);
	type = EXTRACT_16BITS(&pdtlv->type);
	tll = EXTRACT_16BITS(&pdtlv->length) - TLV_HDRL;
	invtlv = tlv_valid(pdtlv, len);
	ND_PRINT((ndo, "genoptlvprint - %s TLV type 0x%x len %d\n",
	       tok2str(ForCES_TLV, NULL, type), type, EXTRACT_16BITS(&pdtlv->length)));
	if (!invtlv) {
		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		register const u_char *dp = (const u_char *) TLV_DATA(pdtlv);
		if (!ttlv_valid(type)) {
			ND_PRINT((ndo, "%s TLV type 0x%x len %d\n",
			       tok2str(ForCES_TLV_err, NULL, invtlv), type,
			       EXTRACT_16BITS(&pdtlv->length)));
			return -1;
		}
		if (ndo->ndo_vflag >= 3)
			ND_PRINT((ndo, "%s%s, length %d (data length %d Bytes)",
			       ib, tok2str(ForCES_TLV, NULL, type),
			       EXTRACT_16BITS(&pdtlv->length), tll));

		return pdata_print(ndo, dp, tll, op_msk, indent + 1);
	} else {
		ND_PRINT((ndo, "\t\t\tInvalid ForCES TLV type=%x", type));
		return -1;
	}

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
recpdoptlv_print(netdissect_options *ndo,
                 register const u_char * pptr, register u_int len,
                 uint16_t op_msk, int indent)
{
	const struct forces_tlv *pdtlv = (const struct forces_tlv *)pptr;
	int tll;
	u_int invtlv;
	uint16_t type;
	register const u_char *dp;
	char *ib;

	while (len != 0) {
		ND_TCHECK(*pdtlv);
		invtlv = tlv_valid(pdtlv, len);
		if (invtlv) {
			break;
		}

		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		ib = indent_pr(indent, 0);
		type = EXTRACT_16BITS(&pdtlv->type);
		dp = (const u_char *) TLV_DATA(pdtlv);
		tll = EXTRACT_16BITS(&pdtlv->length) - TLV_HDRL;

		if (ndo->ndo_vflag >= 3)
			ND_PRINT((ndo, "%s%s, length %d (data encapsulated %d Bytes)",
			          ib, tok2str(ForCES_TLV, NULL, type),
			          EXTRACT_16BITS(&pdtlv->length),
			          EXTRACT_16BITS(&pdtlv->length) - TLV_HDRL));

		if (pdata_print(ndo, dp, tll, op_msk, indent + 1) == -1)
			return -1;
		pdtlv = GO_NXT_TLV(pdtlv, len);
	}

	if (len) {
		ND_PRINT((ndo,
		          "\n\t\tMessy PATHDATA TLV header, type (0x%x)\n\t\texcess of %d Bytes ",
		          EXTRACT_16BITS(&pdtlv->type), len - EXTRACT_16BITS(&pdtlv->length)));
		return -1;
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
invoptlv_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk _U_, int indent)
{
	char *ib = indent_pr(indent, 1);

	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "%sData[", &ib[1]));
		hex_print_with_offset(ndo, ib, pptr, len, 0);
		ND_PRINT((ndo, "%s]\n", ib));
	}
	return -1;
}

static int
otlv_print(netdissect_options *ndo,
           const struct forces_tlv *otlv, uint16_t op_msk _U_, int indent)
{
	int rc = 0;
	register const u_char *dp = (const u_char *) TLV_DATA(otlv);
	uint16_t type;
	int tll;
	char *ib = indent_pr(indent, 0);
	const struct optlv_h *ops;

	/*
	 * lfbselect_print() has ensured that EXTRACT_16BITS(&otlv->length)
	 * >= TLV_HDRL.
	 */
	ND_TCHECK(*otlv);
	type = EXTRACT_16BITS(&otlv->type);
	tll = EXTRACT_16BITS(&otlv->length) - TLV_HDRL;
	ops = get_forces_optlv_h(type);
	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "%sOper TLV %s(0x%x) length %d\n", ib, ops->s, type,
		       EXTRACT_16BITS(&otlv->length)));
	}
	/* rest of ops must at least have 12B {pathinfo} */
	if (tll < OP_MIN_SIZ) {
		ND_PRINT((ndo, "\t\tOper TLV %s(0x%x) length %d\n", ops->s, type,
		       EXTRACT_16BITS(&otlv->length)));
		ND_PRINT((ndo, "\t\tTruncated data size %d minimum required %d\n", tll,
		       OP_MIN_SIZ));
		return invoptlv_print(ndo, dp, tll, ops->op_msk, indent);

	}

	/* XXX - do anything with ops->flags? */
        if(ops->print) {
                rc = ops->print(ndo, dp, tll, ops->op_msk, indent + 1);
        }
	return rc;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#define ASTDLN	4
#define ASTMCD	255
static int
asttlv_print(netdissect_options *ndo,
             register const u_char * pptr, register u_int len,
             uint16_t op_msk _U_, int indent)
{
	uint32_t rescode;
	u_int dlen;
	char *ib = indent_pr(indent, 0);

	/*
	 * forces_type_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	if (dlen != ASTDLN) {
		ND_PRINT((ndo, "illegal ASTresult-TLV: %d bytes!\n", dlen));
		return -1;
	}
	ND_TCHECK2(*pptr, 4);
	rescode = EXTRACT_32BITS(pptr);
	if (rescode > ASTMCD) {
		ND_PRINT((ndo, "illegal ASTresult result code: %d!\n", rescode));
		return -1;
	}

	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "Teardown reason:\n%s", ib));
		switch (rescode) {
		case 0:
			ND_PRINT((ndo, "Normal Teardown"));
			break;
		case 1:
			ND_PRINT((ndo, "Loss of Heartbeats"));
			break;
		case 2:
			ND_PRINT((ndo, "Out of bandwidth"));
			break;
		case 3:
			ND_PRINT((ndo, "Out of Memory"));
			break;
		case 4:
			ND_PRINT((ndo, "Application Crash"));
			break;
		default:
			ND_PRINT((ndo, "Unknown Teardown reason"));
			break;
		}
		ND_PRINT((ndo, "(%x)\n%s", rescode, ib));
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#define ASRDLN	4
#define ASRMCD	3
static int
asrtlv_print(netdissect_options *ndo,
             register const u_char * pptr, register u_int len,
             uint16_t op_msk _U_, int indent)
{
	uint32_t rescode;
	u_int dlen;
	char *ib = indent_pr(indent, 0);

	/*
	 * forces_type_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	if (dlen != ASRDLN) {	/* id, instance, oper tlv */
		ND_PRINT((ndo, "illegal ASRresult-TLV: %d bytes!\n", dlen));
		return -1;
	}
	ND_TCHECK2(*pptr, 4);
	rescode = EXTRACT_32BITS(pptr);

	if (rescode > ASRMCD) {
		ND_PRINT((ndo, "illegal ASRresult result code: %d!\n", rescode));
		return -1;
	}

	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "\n%s", ib));
		switch (rescode) {
		case 0:
			ND_PRINT((ndo, "Success "));
			break;
		case 1:
			ND_PRINT((ndo, "FE ID invalid "));
			break;
		case 2:
			ND_PRINT((ndo, "permission denied "));
			break;
		default:
			ND_PRINT((ndo, "Unknown "));
			break;
		}
		ND_PRINT((ndo, "(%x)\n%s", rescode, ib));
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#if 0
/*
 * XXX - not used.
 */
static int
gentltlv_print(netdissect_options *ndo,
               register const u_char * pptr _U_, register u_int len,
               uint16_t op_msk _U_, int indent _U_)
{
	u_int dlen = len - TLV_HDRL;

	if (dlen < 4) {		/* at least 32 bits must exist */
		ND_PRINT((ndo, "truncated TLV: %d bytes missing! ", 4 - dlen));
		return -1;
	}
	return 0;
}
#endif

#define RD_MIN 8

static int
print_metailv(netdissect_options *ndo,
              register const u_char * pptr, uint16_t op_msk _U_, int indent)
{
	u_int rlen;
	char *ib = indent_pr(indent, 0);
	/* XXX: check header length */
	const struct forces_ilv *ilv = (const struct forces_ilv *)pptr;

	/*
	 * print_metatlv() has ensured that len (what remains in the
	 * ILV) >= ILV_HDRL.
	 */
	rlen = EXTRACT_32BITS(&ilv->length) - ILV_HDRL;
	ND_TCHECK(*ilv);
	ND_PRINT((ndo, "%sMetaID 0x%x length %d\n", ib, EXTRACT_32BITS(&ilv->type),
	       EXTRACT_32BITS(&ilv->length)));
	if (ndo->ndo_vflag >= 3) {
		hex_print_with_offset(ndo, "\t\t[", ILV_DATA(ilv), rlen, 0);
		ND_PRINT((ndo, " ]\n"));
	}
	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
print_metatlv(netdissect_options *ndo,
              register const u_char * pptr, register u_int len,
              uint16_t op_msk _U_, int indent)
{
	u_int dlen;
	char *ib = indent_pr(indent, 0);
	u_int rlen;
	const struct forces_ilv *ilv = (const struct forces_ilv *)pptr;
	int invilv;

	/*
	 * redirect_print() has ensured that len (what remains in the
	 * TLV) >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	rlen = dlen;
	ND_PRINT((ndo, "\n%s METADATA length %d \n", ib, rlen));
	while (rlen != 0) {
		ND_TCHECK(*ilv);
		invilv = ilv_valid(ilv, rlen);
		if (invilv) {
			break;
		}

		/*
		 * At this point, ilv_valid() has ensured that the ILV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		print_metailv(ndo, (const u_char *) ilv, 0, indent + 1);
		ilv = GO_NXT_ILV(ilv, rlen);
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}


static int
print_reddata(netdissect_options *ndo,
              register const u_char * pptr, register u_int len,
              uint16_t op_msk _U_, int indent)
{
	u_int dlen;
	char *ib = indent_pr(indent, 0);
	u_int rlen;

	dlen = len - TLV_HDRL;
	rlen = dlen;
	ND_PRINT((ndo, "\n%s Redirect Data length %d \n", ib, rlen));

	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "\t\t["));
		hex_print_with_offset(ndo, "\n\t\t", pptr, rlen, 0);
		ND_PRINT((ndo, "\n\t\t]"));
	}

	return 0;
}

static int
redirect_print(netdissect_options *ndo,
               register const u_char * pptr, register u_int len,
               uint16_t op_msk _U_, int indent)
{
	const struct forces_tlv *tlv = (const struct forces_tlv *)pptr;
	u_int dlen;
	u_int rlen;
	u_int invtlv;

	/*
	 * forces_type_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	if (dlen <= RD_MIN) {
		ND_PRINT((ndo, "\n\t\ttruncated Redirect TLV: %d bytes missing! ",
		       RD_MIN - dlen));
		return -1;
	}

	rlen = dlen;
	indent += 1;
	while (rlen != 0) {
		ND_TCHECK(*tlv);
		invtlv = tlv_valid(tlv, rlen);
		if (invtlv) {
			ND_PRINT((ndo, "Bad Redirect data\n"));
			break;
		}

		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		if (EXTRACT_16BITS(&tlv->type) == F_TLV_METD) {
			print_metatlv(ndo, (const u_char *) TLV_DATA(tlv),
				      EXTRACT_16BITS(&tlv->length), 0, indent);
		} else if ((EXTRACT_16BITS(&tlv->type) == F_TLV_REDD)) {
			print_reddata(ndo, (const u_char *) TLV_DATA(tlv),
				      EXTRACT_16BITS(&tlv->length), 0, indent);
		} else {
			ND_PRINT((ndo, "Unknown REDIRECT TLV 0x%x len %d\n",
			       EXTRACT_16BITS(&tlv->type),
			       EXTRACT_16BITS(&tlv->length)));
		}

		tlv = GO_NXT_TLV(tlv, rlen);
	}

	if (rlen) {
		ND_PRINT((ndo,
		          "\n\t\tMessy Redirect TLV header, type (0x%x)\n\t\texcess of %d Bytes ",
		          EXTRACT_16BITS(&tlv->type),
		          rlen - EXTRACT_16BITS(&tlv->length)));
		return -1;
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

#define OP_OFF 8
#define OP_MIN 12

static int
lfbselect_print(netdissect_options *ndo,
                register const u_char * pptr, register u_int len,
                uint16_t op_msk, int indent)
{
	const struct forces_lfbsh *lfbs;
	const struct forces_tlv *otlv;
	char *ib = indent_pr(indent, 0);
	u_int dlen;
	u_int rlen;
	u_int invtlv;

	/*
	 * forces_type_print() has ensured that len (the TLV length)
	 * >= TLV_HDRL.
	 */
	dlen = len - TLV_HDRL;
	if (dlen <= OP_MIN) {	/* id, instance, oper tlv header .. */
		ND_PRINT((ndo, "\n\t\ttruncated lfb selector: %d bytes missing! ",
		       OP_MIN - dlen));
		return -1;
	}

	/*
	 * At this point, we know that dlen > OP_MIN; OP_OFF < OP_MIN, so
	 * we also know that it's > OP_OFF.
	 */
	rlen = dlen - OP_OFF;

	lfbs = (const struct forces_lfbsh *)pptr;
	ND_TCHECK(*lfbs);
	if (ndo->ndo_vflag >= 3) {
		ND_PRINT((ndo, "\n%s%s(Classid %x) instance %x\n",
		       ib, tok2str(ForCES_LFBs, NULL, EXTRACT_32BITS(&lfbs->class)),
		       EXTRACT_32BITS(&lfbs->class),
		       EXTRACT_32BITS(&lfbs->instance)));
	}

	otlv = (const struct forces_tlv *)(lfbs + 1);

	indent += 1;
	while (rlen != 0) {
		ND_TCHECK(*otlv);
		invtlv = tlv_valid(otlv, rlen);
		if (invtlv)
			break;

		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the containing TLV).
		 */
		if (op_valid(EXTRACT_16BITS(&otlv->type), op_msk)) {
			otlv_print(ndo, otlv, 0, indent);
		} else {
			if (ndo->ndo_vflag < 3)
				ND_PRINT((ndo, "\n"));
			ND_PRINT((ndo,
			          "\t\tINValid oper-TLV type 0x%x length %d for this ForCES message\n",
			          EXTRACT_16BITS(&otlv->type), EXTRACT_16BITS(&otlv->length)));
			invoptlv_print(ndo, (const u_char *)otlv, rlen, 0, indent);
		}
		otlv = GO_NXT_TLV(otlv, rlen);
	}

	if (rlen) {
		ND_PRINT((ndo,
		          "\n\t\tMessy oper TLV header, type (0x%x)\n\t\texcess of %d Bytes ",
		          EXTRACT_16BITS(&otlv->type), rlen - EXTRACT_16BITS(&otlv->length)));
		return -1;
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

static int
forces_type_print(netdissect_options *ndo,
                  register const u_char * pptr, const struct forcesh *fhdr _U_,
                  register u_int mlen, const struct tom_h *tops)
{
	const struct forces_tlv *tltlv;
	u_int rlen;
	u_int invtlv;
	int rc = 0;
	int ttlv = 0;

	/*
	 * forces_print() has already checked that mlen >= ForCES_HDRL
	 * by calling ForCES_HLN_VALID().
	 */
	rlen = mlen - ForCES_HDRL;

	if (rlen > TLV_HLN) {
		if (tops->flags & ZERO_TTLV) {
			ND_PRINT((ndo, "<0x%x>Illegal Top level TLV!\n", tops->flags));
			return -1;
		}
	} else {
		if (tops->flags & ZERO_MORE_TTLV)
			return 0;
		if (tops->flags & ONE_MORE_TTLV) {
			ND_PRINT((ndo, "\tTop level TLV Data missing!\n"));
			return -1;
		}
	}

	if (tops->flags & ZERO_TTLV) {
		return 0;
	}

	ttlv = tops->flags >> 4;
	tltlv = GET_TOP_TLV(pptr);

	/*XXX: 15 top level tlvs will probably be fine
	   You are nuts if you send more ;-> */
	while (rlen != 0) {
		ND_TCHECK(*tltlv);
		invtlv = tlv_valid(tltlv, rlen);
		if (invtlv)
			break;

		/*
		 * At this point, tlv_valid() has ensured that the TLV
		 * length is large enough but not too large (it doesn't
		 * go past the end of the packet).
		 */
		if (!ttlv_valid(EXTRACT_16BITS(&tltlv->type))) {
			ND_PRINT((ndo, "\n\tInvalid ForCES Top TLV type=0x%x",
			       EXTRACT_16BITS(&tltlv->type)));
			return -1;
		}

		if (ndo->ndo_vflag >= 3)
			ND_PRINT((ndo, "\t%s, length %d (data length %d Bytes)",
			       tok2str(ForCES_TLV, NULL, EXTRACT_16BITS(&tltlv->type)),
			       EXTRACT_16BITS(&tltlv->length),
			       EXTRACT_16BITS(&tltlv->length) - TLV_HDRL));

		rc = tops->print(ndo, (const u_char *) TLV_DATA(tltlv),
				 EXTRACT_16BITS(&tltlv->length), tops->op_msk, 9);
		if (rc < 0) {
			return -1;
		}
		tltlv = GO_NXT_TLV(tltlv, rlen);
		ttlv--;
		if (ttlv <= 0)
			break;
	}
	/*
	 * XXX - if ttlv != 0, does that mean that the packet was too
	 * short, and didn't have *enough* TLVs in it?
	 */
	if (rlen) {
		ND_PRINT((ndo, "\tMess TopTLV header: min %u, total %d advertised %d ",
		       TLV_HDRL, rlen, EXTRACT_16BITS(&tltlv->length)));
		return -1;
	}

	return 0;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return -1;
}

void
forces_print(netdissect_options *ndo,
             register const u_char * pptr, register u_int len)
{
	const struct forcesh *fhdr;
	u_int mlen;
	uint32_t flg_raw;
	const struct tom_h *tops;
	int rc = 0;

	fhdr = (const struct forcesh *)pptr;
	ND_TCHECK(*fhdr);
	if (!tom_valid(fhdr->fm_tom)) {
		ND_PRINT((ndo, "Invalid ForCES message type %d\n", fhdr->fm_tom));
		goto error;
	}

	mlen = ForCES_BLN(fhdr);

	tops = get_forces_tom(fhdr->fm_tom);
	if (tops->v == TOM_RSVD) {
		ND_PRINT((ndo, "\n\tUnknown ForCES message type=0x%x", fhdr->fm_tom));
		goto error;
	}

	ND_PRINT((ndo, "\n\tForCES %s ", tops->s));
	if (!ForCES_HLN_VALID(mlen, len)) {
		ND_PRINT((ndo,
		          "Illegal ForCES pkt len - min %u, total recvd %d, advertised %d ",
		          ForCES_HDRL, len, ForCES_BLN(fhdr)));
		goto error;
	}

	ND_TCHECK2(*(pptr + 20), 4);
	flg_raw = EXTRACT_32BITS(pptr + 20);
	if (ndo->ndo_vflag >= 1) {
		ND_PRINT((ndo, "\n\tForCES Version %d len %uB flags 0x%08x ",
		       ForCES_V(fhdr), mlen, flg_raw));
		ND_PRINT((ndo,
		       "\n\tSrcID 0x%x(%s) DstID 0x%x(%s) Correlator 0x%" PRIx64,
		       ForCES_SID(fhdr), ForCES_node(ForCES_SID(fhdr)),
		       ForCES_DID(fhdr), ForCES_node(ForCES_DID(fhdr)),
		       EXTRACT_64BITS(fhdr->fm_cor)));

	}
	if (ndo->ndo_vflag >= 2) {
		ND_PRINT((ndo,
		     "\n\tForCES flags:\n\t  %s(0x%x), prio=%d, %s(0x%x),\n\t  %s(0x%x), %s(0x%x)\n",
		     tok2str(ForCES_ACKs, "ACKUnknown", ForCES_ACK(fhdr)),
		     ForCES_ACK(fhdr),
		     ForCES_PRI(fhdr),
		     tok2str(ForCES_EMs, "EMUnknown", ForCES_EM(fhdr)),
		     ForCES_EM(fhdr),
		     tok2str(ForCES_ATs, "ATUnknown", ForCES_AT(fhdr)),
		     ForCES_AT(fhdr),
		     tok2str(ForCES_TPs, "TPUnknown", ForCES_TP(fhdr)),
		     ForCES_TP(fhdr)));
		ND_PRINT((ndo,
		     "\t  Extra flags: rsv(b5-7) 0x%x rsv(b13-31) 0x%x\n",
		     ForCES_RS1(fhdr), ForCES_RS2(fhdr)));
	}
	rc = forces_type_print(ndo, pptr, fhdr, mlen, tops);
	if (rc < 0) {
error:
		hex_print_with_offset(ndo, "\n\t[", pptr, len, 0);
		ND_PRINT((ndo, "\n\t]"));
		return;
	}

	if (ndo->ndo_vflag >= 4) {
		ND_PRINT((ndo, "\n\t  Raw ForCES message\n\t ["));
		hex_print_with_offset(ndo, "\n\t ", pptr, len, 0);
		ND_PRINT((ndo, "\n\t ]"));
	}
	ND_PRINT((ndo, "\n"));
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}
/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
