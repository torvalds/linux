/*
 * Copyright (c) 1999-2007 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: mfdef.h,v 8.40 2013-11-22 20:51:27 ca Exp $
 */

/*
**  mfdef.h -- Global definitions for mail filter and MTA.
*/

#ifndef _LIBMILTER_MFDEF_H
# define _LIBMILTER_MFDEF_H	1

#ifndef SMFI_PROT_VERSION
# define SMFI_PROT_VERSION	6	/* MTA - libmilter protocol version */
#endif /* SMFI_PROT_VERSION */

/* Shared protocol constants */
#define MILTER_LEN_BYTES	4	/* length of 32 bit integer in bytes */
#define MILTER_OPTLEN	(MILTER_LEN_BYTES * 3) /* length of options */
#define MILTER_CHUNK_SIZE	65535	/* body chunk size */
#define MILTER_MAX_DATA_SIZE	65535	/* default milter command data limit */

#if _FFR_MDS_NEGOTIATE
# define MILTER_MDS_64K	((64 * 1024) - 1)
# define MILTER_MDS_256K ((256 * 1024) - 1)
# define MILTER_MDS_1M	((1024 * 1024) - 1)
#endif /* _FFR_MDS_NEGOTIATE */

/* These apply to SMFIF_* flags */
#define SMFI_V1_ACTS	0x0000000FL	/* The actions of V1 filter */
#define SMFI_V2_ACTS	0x0000003FL	/* The actions of V2 filter */
#define SMFI_CURR_ACTS	0x000001FFL	/* actions of current version */

/* address families */
#define SMFIA_UNKNOWN		'U'	/* unknown */
#define SMFIA_UNIX		'L'	/* unix/local */
#define SMFIA_INET		'4'	/* inet */
#define SMFIA_INET6		'6'	/* inet6 */

/* commands: don't use anything smaller than ' ' */
#define SMFIC_ABORT		'A'	/* Abort */
#define SMFIC_BODY		'B'	/* Body chunk */
#define SMFIC_CONNECT		'C'	/* Connection information */
#define SMFIC_MACRO		'D'	/* Define macro */
#define SMFIC_BODYEOB		'E'	/* final body chunk (End) */
#define SMFIC_HELO		'H'	/* HELO/EHLO */
#define SMFIC_QUIT_NC		'K'	/* QUIT but new connection follows */
#define SMFIC_HEADER		'L'	/* Header */
#define SMFIC_MAIL		'M'	/* MAIL from */
#define SMFIC_EOH		'N'	/* EOH */
#define SMFIC_OPTNEG		'O'	/* Option negotiation */
#define SMFIC_QUIT		'Q'	/* QUIT */
#define SMFIC_RCPT		'R'	/* RCPT to */
#define SMFIC_DATA		'T'	/* DATA */
#define SMFIC_UNKNOWN		'U'	/* Any unknown command */

/* actions (replies) */
#define SMFIR_ADDRCPT		'+'	/* add recipient */
#define SMFIR_DELRCPT		'-'	/* remove recipient */
#define SMFIR_ADDRCPT_PAR	'2'	/* add recipient (incl. ESMTP args) */
#define SMFIR_SHUTDOWN		'4'	/* 421: shutdown (internal to MTA) */
#define SMFIR_ACCEPT		'a'	/* accept */
#define SMFIR_REPLBODY		'b'	/* replace body (chunk) */
#define SMFIR_CONTINUE		'c'	/* continue */
#define SMFIR_DISCARD		'd'	/* discard */
#define SMFIR_CHGFROM		'e'	/* change envelope sender (from) */
#define SMFIR_CONN_FAIL		'f'	/* cause a connection failure */
#define SMFIR_ADDHEADER		'h'	/* add header */
#define SMFIR_INSHEADER		'i'	/* insert header */
#define SMFIR_SETSYMLIST	'l'	/* set list of symbols (macros) */
#define SMFIR_CHGHEADER		'm'	/* change header */
#define SMFIR_PROGRESS		'p'	/* progress */
#define SMFIR_QUARANTINE	'q'	/* quarantine */
#define SMFIR_REJECT		'r'	/* reject */
#define SMFIR_SKIP		's'	/* skip */
#define SMFIR_TEMPFAIL		't'	/* tempfail */
#define SMFIR_REPLYCODE		'y'	/* reply code etc */

/* What the MTA can send/filter wants in protocol */
#define SMFIP_NOCONNECT 0x00000001L	/* MTA should not send connect info */
#define SMFIP_NOHELO	0x00000002L	/* MTA should not send HELO info */
#define SMFIP_NOMAIL	0x00000004L	/* MTA should not send MAIL info */
#define SMFIP_NORCPT	0x00000008L	/* MTA should not send RCPT info */
#define SMFIP_NOBODY	0x00000010L	/* MTA should not send body */
#define SMFIP_NOHDRS	0x00000020L	/* MTA should not send headers */
#define SMFIP_NOEOH	0x00000040L	/* MTA should not send EOH */
#define SMFIP_NR_HDR	0x00000080L	/* No reply for headers */
#define SMFIP_NOHREPL	SMFIP_NR_HDR	/* No reply for headers */
#define SMFIP_NOUNKNOWN 0x00000100L /* MTA should not send unknown commands */
#define SMFIP_NODATA    0x00000200L	/* MTA should not send DATA */
#define SMFIP_SKIP	0x00000400L	/* MTA understands SMFIS_SKIP */
#define SMFIP_RCPT_REJ	0x00000800L /* MTA should also send rejected RCPTs */
#define SMFIP_NR_CONN	0x00001000L	/* No reply for connect */
#define SMFIP_NR_HELO	0x00002000L	/* No reply for HELO */
#define SMFIP_NR_MAIL	0x00004000L	/* No reply for MAIL */
#define SMFIP_NR_RCPT	0x00008000L	/* No reply for RCPT */
#define SMFIP_NR_DATA	0x00010000L	/* No reply for DATA */
#define SMFIP_NR_UNKN	0x00020000L	/* No reply for UNKN */
#define SMFIP_NR_EOH	0x00040000L	/* No reply for eoh */
#define SMFIP_NR_BODY	0x00080000L	/* No reply for body chunk */
#define SMFIP_HDR_LEADSPC 0x00100000L	/* header value leading space */
#define SMFIP_MDS_256K	0x10000000L	/* MILTER_MAX_DATA_SIZE=256K */
#define SMFIP_MDS_1M	0x20000000L	/* MILTER_MAX_DATA_SIZE=1M */
/* #define SMFIP_	0x40000000L	reserved: see SMFI_INTERNAL*/

#define SMFI_V1_PROT	0x0000003FL	/* The protocol of V1 filter */
#define SMFI_V2_PROT	0x0000007FL	/* The protocol of V2 filter */

/* all defined protocol bits */
#define SMFI_CURR_PROT	0x001FFFFFL

/* internal flags: only used between MTA and libmilter */
#define SMFI_INTERNAL	0x70000000L

#if _FFR_MILTER_CHECK
# define SMFIP_TEST	0x80000000L
#endif /* _FFR_MILTER_CHECK */

#endif /* !_LIBMILTER_MFDEF_H */
