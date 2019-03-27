/*
 * Copyright (c) 1999-2004, 2006, 2008, 2012 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: mfapi.h,v 8.83 2013-11-22 20:51:27 ca Exp $
 */

/*
**  MFAPI.H -- Global definitions for mail filter library and mail filters.
*/

#ifndef _LIBMILTER_MFAPI_H
# define _LIBMILTER_MFAPI_H	1

#ifndef SMFI_VERSION
# if _FFR_MDS_NEGOTIATE
#  define SMFI_VERSION	0x01000002	/* libmilter version number */

   /* first libmilter version that has MDS support */
#  define SMFI_VERSION_MDS	0x01000002
# else /* _FFR_MDS_NEGOTIATE */
#  define SMFI_VERSION	0x01000001	/* libmilter version number */
# endif /* _FFR_MDS_NEGOTIATE */
#endif /* ! SMFI_VERSION */

#define SM_LM_VRS_MAJOR(v)	(((v) & 0x7f000000) >> 24)
#define SM_LM_VRS_MINOR(v)	(((v) & 0x007fff00) >> 8)
#define SM_LM_VRS_PLVL(v)	((v) & 0x0000007f)

# include <sys/types.h>
# include <sys/socket.h>

#include "libmilter/mfdef.h"

# define LIBMILTER_API		extern


/* Only need to export C interface if used by C++ source code */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef _SOCK_ADDR
# define _SOCK_ADDR	struct sockaddr
#endif /* ! _SOCK_ADDR */

/*
**  libmilter functions return one of the following to indicate
**  success/failure(/continue):
*/

#define MI_SUCCESS	0
#define MI_FAILURE	(-1)
#if _FFR_WORKERS_POOL
# define MI_CONTINUE	1
#endif /* _FFR_WORKERS_POOL */

/* "forward" declarations */
typedef struct smfi_str SMFICTX;
typedef struct smfi_str *SMFICTX_PTR;

typedef struct smfiDesc smfiDesc_str;
typedef struct smfiDesc	*smfiDesc_ptr;

/*
**  Type which callbacks should return to indicate message status.
**  This may take on one of the SMFIS_* values listed below.
*/

typedef int	sfsistat;

#if defined(__linux__) && defined(__GNUC__) && defined(__cplusplus) && __GNUC_MINOR__ >= 8
# define SM__P(X)	__PMT(X)
#else /* __linux__ && __GNUC__ && __cplusplus && _GNUC_MINOR__ >= 8 */
# define SM__P(X)	__P(X)
#endif /* __linux__ && __GNUC__ && __cplusplus && _GNUC_MINOR__ >= 8 */

/* Some platforms don't define __P -- do it for them here: */
#ifndef __P
# ifdef __STDC__
#  define __P(X) X
# else /* __STDC__ */
#  define __P(X) ()
# endif /* __STDC__ */
#endif /* __P */

#if SM_CONF_STDBOOL_H
# include <stdbool.h>
#else /* SM_CONF_STDBOOL_H */
# ifndef __cplusplus
#  ifndef bool
#   ifndef __bool_true_false_are_defined
typedef int	bool;
#    define false	0
#    define true	1
#    define __bool_true_false_are_defined	1
#   endif /* ! __bool_true_false_are_defined */
#  endif /* bool */
# endif /* ! __cplusplus */
#endif /* SM_CONF_STDBOOL_H */

/*
**  structure describing one milter
*/

struct smfiDesc
{
	char		*xxfi_name;	/* filter name */
	int		xxfi_version;	/* version code -- do not change */
	unsigned long	xxfi_flags;	/* flags */

	/* connection info filter */
	sfsistat	(*xxfi_connect) SM__P((SMFICTX *, char *, _SOCK_ADDR *));

	/* SMTP HELO command filter */
	sfsistat	(*xxfi_helo) SM__P((SMFICTX *, char *));

	/* envelope sender filter */
	sfsistat	(*xxfi_envfrom) SM__P((SMFICTX *, char **));

	/* envelope recipient filter */
	sfsistat	(*xxfi_envrcpt) SM__P((SMFICTX *, char **));

	/* header filter */
	sfsistat	(*xxfi_header) SM__P((SMFICTX *, char *, char *));

	/* end of header */
	sfsistat	(*xxfi_eoh) SM__P((SMFICTX *));

	/* body block */
	sfsistat	(*xxfi_body) SM__P((SMFICTX *, unsigned char *, size_t));

	/* end of message */
	sfsistat	(*xxfi_eom) SM__P((SMFICTX *));

	/* message aborted */
	sfsistat	(*xxfi_abort) SM__P((SMFICTX *));

	/* connection cleanup */
	sfsistat	(*xxfi_close) SM__P((SMFICTX *));

	/* any unrecognized or unimplemented command filter */
	sfsistat	(*xxfi_unknown) SM__P((SMFICTX *, const char *));

	/* SMTP DATA command filter */
	sfsistat	(*xxfi_data) SM__P((SMFICTX *));

	/* negotiation callback */
	sfsistat	(*xxfi_negotiate) SM__P((SMFICTX *,
					unsigned long, unsigned long,
					unsigned long, unsigned long,
					unsigned long *, unsigned long *,
					unsigned long *, unsigned long *));

#if 0
	/* signal handler callback, not yet implemented. */
	int		(*xxfi_signal) SM__P((int));
#endif

};

LIBMILTER_API int smfi_opensocket __P((bool));
LIBMILTER_API int smfi_register __P((struct smfiDesc));
LIBMILTER_API int smfi_main __P((void));
LIBMILTER_API int smfi_setbacklog __P((int));
LIBMILTER_API int smfi_setdbg __P((int));
LIBMILTER_API int smfi_settimeout __P((int));
LIBMILTER_API int smfi_setconn __P((char *));
LIBMILTER_API int smfi_stop __P((void));
LIBMILTER_API size_t smfi_setmaxdatasize __P((size_t));
LIBMILTER_API int smfi_version __P((unsigned int *, unsigned int *, unsigned int *));

/*
**  What the filter might do -- values to be ORed together for
**  smfiDesc.xxfi_flags.
*/

#define SMFIF_NONE	0x00000000L	/* no flags */
#define SMFIF_ADDHDRS	0x00000001L	/* filter may add headers */
#define SMFIF_CHGBODY	0x00000002L	/* filter may replace body */
#define SMFIF_MODBODY	SMFIF_CHGBODY	/* backwards compatible */
#define SMFIF_ADDRCPT	0x00000004L	/* filter may add recipients */
#define SMFIF_DELRCPT	0x00000008L	/* filter may delete recipients */
#define SMFIF_CHGHDRS	0x00000010L	/* filter may change/delete headers */
#define SMFIF_QUARANTINE 0x00000020L	/* filter may quarantine envelope */

/* filter may change "from" (envelope sender) */
#define SMFIF_CHGFROM	0x00000040L
#define SMFIF_ADDRCPT_PAR	0x00000080L	/* add recipients incl. args */

/* filter can send set of symbols (macros) that it wants */
#define SMFIF_SETSYMLIST	0x00000100L


/*
**  Macro "places";
**  Notes:
**  - must be coordinated with libmilter/engine.c and sendmail/milter.c
**  - the order MUST NOT be changed as it would break compatibility between
**	different versions. It's ok to append new entries however
**	(hence the list is not sorted by the SMT protocol steps).
*/

#define SMFIM_NOMACROS	(-1)	/* Do NOT use, internal only */
#define SMFIM_FIRST	0	/* Do NOT use, internal marker only */
#define SMFIM_CONNECT	0	/* connect */
#define SMFIM_HELO	1	/* HELO/EHLO */
#define SMFIM_ENVFROM	2	/* MAIL From */
#define SMFIM_ENVRCPT	3	/* RCPT To */
#define SMFIM_DATA	4	/* DATA */
#define SMFIM_EOM	5	/* end of message (final dot) */
#define SMFIM_EOH	6	/* end of header */
#define SMFIM_LAST	6	/* Do NOT use, internal marker only */

/*
**  Continue processing message/connection.
*/

#define SMFIS_CONTINUE	0

/*
**  Reject the message/connection.
**  No further routines will be called for this message
**  (or connection, if returned from a connection-oriented routine).
*/

#define SMFIS_REJECT	1

/*
**  Accept the message,
**  but silently discard the message.
**  No further routines will be called for this message.
**  This is only meaningful from message-oriented routines.
*/

#define SMFIS_DISCARD	2

/*
**  Accept the message/connection.
**  No further routines will be called for this message
**  (or connection, if returned from a connection-oriented routine;
**  in this case, it causes all messages on this connection
**  to be accepted without filtering).
*/

#define SMFIS_ACCEPT	3

/*
**  Return a temporary failure, i.e.,
**  the corresponding SMTP command will return a 4xx status code.
**  In some cases this may prevent further routines from
**  being called on this message or connection,
**  although in other cases (e.g., when processing an envelope
**  recipient) processing of the message will continue.
*/

#define SMFIS_TEMPFAIL	4

/*
**  Do not send a reply to the MTA
*/

#define SMFIS_NOREPLY	7

/*
**  Skip over rest of same callbacks, e.g., body.
*/

#define SMFIS_SKIP	8

/* xxfi_negotiate: use all existing protocol options/actions */
#define SMFIS_ALL_OPTS	10

#if 0
/*
**  Filter Routine Details
*/

/* connection info filter */
extern sfsistat	xxfi_connect __P((SMFICTX *, char *, _SOCK_ADDR *));

/*
**  xxfi_connect(ctx, hostname, hostaddr) Invoked on each connection
**
**	char *hostname; Host domain name, as determined by a reverse lookup
**		on the host address.
**	_SOCK_ADDR *hostaddr; Host address, as determined by a getpeername
**		call on the SMTP socket.
*/

/* SMTP HELO command filter */
extern sfsistat	xxfi_helo __P((SMFICTX *, char *));

/*
**  xxfi_helo(ctx, helohost) Invoked on SMTP HELO/EHLO command
**
**	char *helohost; Value passed to HELO/EHLO command, which should be
**		the domain name of the sending host (but is, in practice,
**		anything the sending host wants to send).
*/

/* envelope sender filter */
extern sfsistat	xxfi_envfrom __P((SMFICTX *, char **));

/*
**  xxfi_envfrom(ctx, argv) Invoked on envelope from
**
**	char **argv; Null-terminated SMTP command arguments;
**		argv[0] is guaranteed to be the sender address.
**		Later arguments are the ESMTP arguments.
*/

/* envelope recipient filter */
extern sfsistat	xxfi_envrcpt __P((SMFICTX *, char **));

/*
**  xxfi_envrcpt(ctx, argv) Invoked on each envelope recipient
**
**	char **argv; Null-terminated SMTP command arguments;
**		argv[0] is guaranteed to be the recipient address.
**		Later arguments are the ESMTP arguments.
*/

/* unknown command filter */

extern sfsistat	*xxfi_unknown __P((SMFICTX *, const char *));

/*
**  xxfi_unknown(ctx, arg) Invoked when SMTP command is not recognized or not
**  implemented.
**	const char *arg; Null-terminated SMTP command
*/

/* header filter */
extern sfsistat	xxfi_header __P((SMFICTX *, char *, char *));

/*
**  xxfi_header(ctx, headerf, headerv) Invoked on each message header. The
**  content of the header may have folded white space (that is, multiple
**  lines with following white space) included.
**
**	char *headerf; Header field name
**	char *headerv; Header field value
*/

/* end of header */
extern sfsistat	xxfi_eoh __P((SMFICTX *));

/*
**  xxfi_eoh(ctx) Invoked at end of header
*/

/* body block */
extern sfsistat	xxfi_body __P((SMFICTX *, unsigned char *, size_t));

/*
**  xxfi_body(ctx, bodyp, bodylen) Invoked for each body chunk. There may
**  be multiple body chunks passed to the filter. End-of-lines are
**  represented as received from SMTP (normally Carriage-Return/Line-Feed).
**
**	unsigned char *bodyp; Pointer to body data
**	size_t bodylen; Length of body data
*/

/* end of message */
extern sfsistat	xxfi_eom __P((SMFICTX *));

/*
**  xxfi_eom(ctx) Invoked at end of message. This routine can perform
**  special operations such as modifying the message header, body, or
**  envelope.
*/

/* message aborted */
extern sfsistat	xxfi_abort __P((SMFICTX *));

/*
**  xxfi_abort(ctx) Invoked if message is aborted outside of the control of
**  the filter, for example, if the SMTP sender issues an RSET command. If
**  xxfi_abort is called, xxfi_eom will not be called and vice versa.
*/

/* connection cleanup */
extern sfsistat	xxfi_close __P((SMFICTX *));

/*
**  xxfi_close(ctx) Invoked at end of the connection. This is called on
**  close even if the previous mail transaction was aborted.
*/
#endif /* 0 */

/*
**  Additional information is passed in to the vendor filter routines using
**  symbols. Symbols correspond closely to sendmail macros. The symbols
**  defined depend on the context. The value of a symbol is accessed using:
*/

/* Return the value of a symbol. */
LIBMILTER_API char * smfi_getsymval __P((SMFICTX *, char *));

/*
**  Return the value of a symbol.
**
**	SMFICTX *ctx; Opaque context structure
**	char *symname; The name of the symbol to access.
*/

/*
**  Vendor filter routines that want to pass additional information back to
**  the MTA for use in SMTP replies may call smfi_setreply before returning.
*/

LIBMILTER_API int smfi_setreply __P((SMFICTX *, char *, char *, char *));

/*
**  Alternatively, smfi_setmlreply can be called if a multi-line SMTP reply
**  is needed.
*/

LIBMILTER_API int smfi_setmlreply __P((SMFICTX *, const char *, const char *, ...));

/*
**  Set the specific reply code to be used in response to the active
**  command. If not specified, a generic reply code is used.
**
**	SMFICTX *ctx; Opaque context structure
**	char *rcode; The three-digit (RFC 821) SMTP reply code to be
**		returned, e.g., ``551''.
**	char *xcode; The extended (RFC 2034) reply code, e.g., ``5.7.6''.
**	char *message; The text part of the SMTP reply.
*/

/*
**  The xxfi_eom routine is called at the end of a message (essentially,
**  after the final DATA dot). This routine can call some special routines
**  to modify the envelope, header, or body of the message before the
**  message is enqueued. These routines must not be called from any vendor
**  routine other than xxfi_eom.
*/

LIBMILTER_API int smfi_addheader __P((SMFICTX *, char *, char *));

/*
**  Add a header to the message. It is not checked for standards
**  compliance; the mail filter must ensure that no protocols are violated
**  as a result of adding this header.
**
**	SMFICTX *ctx; Opaque context structure
**	char *headerf; Header field name
**	char *headerv; Header field value
*/

LIBMILTER_API int smfi_chgheader __P((SMFICTX *, char *, int, char *));

/*
**  Change/delete a header in the message.  It is not checked for standards
**  compliance; the mail filter must ensure that no protocols are violated
**  as a result of adding this header.
**
**	SMFICTX *ctx; Opaque context structure
**	char *headerf; Header field name
**	int index; The Nth occurence of header field name
**	char *headerv; New header field value (empty for delete header)
*/

LIBMILTER_API int smfi_insheader __P((SMFICTX *, int, char *, char *));

/*
**  Insert a header into the message.  It is not checked for standards
**  compliance; the mail filter must ensure that no protocols are violated
**  as a result of adding this header.
**
**	SMFICTX *ctx; Opaque context structure
**  	int idx; index into the header list where the insertion should happen
**	char *headerh; Header field name
**	char *headerv; Header field value
*/

LIBMILTER_API int smfi_chgfrom __P((SMFICTX *, char *, char *));

/*
**  Modify envelope sender address
**
**	SMFICTX *ctx; Opaque context structure
**	char *mail; New envelope sender address
**	char *args; ESMTP arguments
*/


LIBMILTER_API int smfi_addrcpt __P((SMFICTX *, char *));

/*
**  Add a recipient to the envelope
**
**	SMFICTX *ctx; Opaque context structure
**	char *rcpt; Recipient to be added
*/

LIBMILTER_API int smfi_addrcpt_par __P((SMFICTX *, char *, char *));

/*
**  Add a recipient to the envelope
**
**	SMFICTX *ctx; Opaque context structure
**	char *rcpt; Recipient to be added
**	char *args; ESMTP arguments
*/


LIBMILTER_API int smfi_delrcpt __P((SMFICTX *, char *));

/*
**  Send a "no-op" up to the MTA to tell it we're still alive, so long
**  milter-side operations don't time out.
**
**	SMFICTX *ctx; Opaque context structure
*/

LIBMILTER_API int smfi_progress __P((SMFICTX *));

/*
**  Delete a recipient from the envelope
**
**	SMFICTX *ctx; Opaque context structure
**	char *rcpt; Envelope recipient to be deleted. This should be in
**		exactly the form passed to xxfi_envrcpt or the address may
**		not be deleted.
*/

LIBMILTER_API int smfi_replacebody __P((SMFICTX *, unsigned char *, int));

/*
**  Replace the body of the message. This routine may be called multiple
**  times if the body is longer than convenient to send in one call. End of
**  line should be represented as Carriage-Return/Line Feed.
**
**	char *bodyp; Pointer to block of body information to insert
**	int bodylen; Length of data pointed at by bodyp
*/

/*
**  If the message is aborted (for example, if the SMTP sender sends the
**  envelope but then does a QUIT or RSET before the data is sent),
**  xxfi_abort is called. This can be used to reset state.
*/

/*
**  Quarantine an envelope
**
**	SMFICTX *ctx; Opaque context structure
**	char *reason: explanation
*/

LIBMILTER_API int smfi_quarantine __P((SMFICTX *ctx, char *reason));

/*
**  Connection-private data (specific to an SMTP connection) can be
**  allocated using the smfi_setpriv routine; routines can access private
**  data using smfi_getpriv.
*/

LIBMILTER_API int smfi_setpriv __P((SMFICTX *, void *));

/*
**  Set the private data pointer
**
**	SMFICTX *ctx; Opaque context structure
**	void *privatedata; Pointer to private data area
*/

LIBMILTER_API void *smfi_getpriv __P((SMFICTX *));

/*
**  Get the private data pointer
**
**	SMFICTX *ctx; Opaque context structure
**	void *privatedata; Pointer to private data area
*/

LIBMILTER_API int smfi_setsymlist __P((SMFICTX *, int, char *));

/*
**  Set list of symbols (macros) to receive
**
**	SMFICTX *ctx; Opaque context structure
**	int where; where in the SMTP dialogue should the macros be sent
**	char *macros; list of macros (space separated)
*/

#if _FFR_THREAD_MONITOR
LIBMILTER_API int smfi_set_max_exec_time __P((unsigned int));
#endif /* _FFR_THREAD_MONITOR */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _LIBMILTER_MFAPI_H */
