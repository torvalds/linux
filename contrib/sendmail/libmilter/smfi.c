/*
 *  Copyright (c) 1999-2007 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: smfi.c,v 8.84 2013-11-22 20:51:36 ca Exp $")
#include <sm/varargs.h>
#include "libmilter.h"

static int smfi_header __P((SMFICTX *, int, int, char *, char *));
static int myisenhsc __P((const char *, int));

/* for smfi_set{ml}reply, let's be generous. 256/16 should be sufficient */
#define MAXREPLYLEN	980	/* max. length of a reply string */
#define MAXREPLIES	32	/* max. number of reply strings */

/*
**  SMFI_HEADER -- send a header to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		cmd -- Header modification command
**		hdridx -- Header index
**		headerf -- Header field name
**		headerv -- Header field value
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
smfi_header(ctx, cmd, hdridx, headerf, headerv)
	SMFICTX *ctx;
	int cmd;
	int hdridx;
	char *headerf;
	char *headerv;
{
	size_t len, l1, l2, offset;
	int r;
	mi_int32 v;
	char *buf;
	struct timeval timeout;

	if (headerf == NULL || *headerf == '\0' || headerv == NULL)
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	l1 = strlen(headerf) + 1;
	l2 = strlen(headerv) + 1;
	len = l1 + l2;
	if (hdridx >= 0)
		len += MILTER_LEN_BYTES;
	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;
	offset = 0;
	if (hdridx >= 0)
	{
		v = htonl(hdridx);
		(void) memcpy(&(buf[0]), (void *) &v, MILTER_LEN_BYTES);
		offset += MILTER_LEN_BYTES;
	}
	(void) memcpy(buf + offset, headerf, l1);
	(void) memcpy(buf + offset + l1, headerv, l2);
	r = mi_wr_cmd(ctx->ctx_sd, &timeout, cmd, buf, len);
	free(buf);
	return r;
}

/*
**  SMFI_ADDHEADER -- send a new header to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		headerf -- Header field name
**		headerv -- Header field value
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_addheader(ctx, headerf, headerv)
	SMFICTX *ctx;
	char *headerf;
	char *headerv;
{
	if (!mi_sendok(ctx, SMFIF_ADDHDRS))
		return MI_FAILURE;

	return smfi_header(ctx, SMFIR_ADDHEADER, -1, headerf, headerv);
}

/*
**  SMFI_INSHEADER -- send a new header to the MTA (to be inserted)
**
**	Parameters:
**		ctx -- Opaque context structure
**  		hdridx -- index into header list where insertion should occur
**		headerf -- Header field name
**		headerv -- Header field value
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_insheader(ctx, hdridx, headerf, headerv)
	SMFICTX *ctx;
	int hdridx;
	char *headerf;
	char *headerv;
{
	if (!mi_sendok(ctx, SMFIF_ADDHDRS) || hdridx < 0)
		return MI_FAILURE;

	return smfi_header(ctx, SMFIR_INSHEADER, hdridx, headerf, headerv);
}

/*
**  SMFI_CHGHEADER -- send a changed header to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		headerf -- Header field name
**		hdridx -- Header index value
**		headerv -- Header field value
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_chgheader(ctx, headerf, hdridx, headerv)
	SMFICTX *ctx;
	char *headerf;
	mi_int32 hdridx;
	char *headerv;
{
	if (!mi_sendok(ctx, SMFIF_CHGHDRS) || hdridx < 0)
		return MI_FAILURE;
	if (headerv == NULL)
		headerv = "";

	return smfi_header(ctx, SMFIR_CHGHEADER, hdridx, headerf, headerv);
}

#if 0
/*
**  BUF_CRT_SEND -- construct buffer to send from arguments
**
**	Parameters:
**		ctx -- Opaque context structure
**		cmd -- command
**		arg0 -- first argument
**		argv -- list of arguments (NULL terminated)
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
buf_crt_send __P((SMFICTX *, int cmd, char *, char **));

static int
buf_crt_send(ctx, cmd, arg0, argv)
	SMFICTX *ctx;
	int cmd;
	char *arg0;
	char **argv;
{
	size_t len, l0, l1, offset;
	int r;
	char *buf, *arg, **argvl;
	struct timeval timeout;

	if (arg0 == NULL || *arg0 == '\0')
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	l0 = strlen(arg0) + 1;
	len = l0;
	argvl = argv;
	while (argvl != NULL && (arg = *argv) != NULL && *arg != '\0')
	{
		l1 = strlen(arg) + 1;
		len += l1;
		SM_ASSERT(len > l1);
	}

	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;
	(void) memcpy(buf, arg0, l0);
	offset = l0;

	argvl = argv;
	while (argvl != NULL && (arg = *argv) != NULL && *arg != '\0')
	{
		l1 = strlen(arg) + 1;
		SM_ASSERT(offset < len);
		SM_ASSERT(offset + l1 <= len);
		(void) memcpy(buf + offset, arg, l1);
		offset += l1;
		SM_ASSERT(offset > l1);
	}

	r = mi_wr_cmd(ctx->ctx_sd, &timeout, cmd, buf, len);
	free(buf);
	return r;
}
#endif /* 0 */

/*
**  SEND2 -- construct buffer to send from arguments
**
**	Parameters:
**		ctx -- Opaque context structure
**		cmd -- command
**		arg0 -- first argument
**		argv -- list of arguments (NULL terminated)
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
send2 __P((SMFICTX *, int cmd, char *, char *));

static int
send2(ctx, cmd, arg0, arg1)
	SMFICTX *ctx;
	int cmd;
	char *arg0;
	char *arg1;
{
	size_t len, l0, l1, offset;
	int r;
	char *buf;
	struct timeval timeout;

	if (arg0 == NULL || *arg0 == '\0')
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	l0 = strlen(arg0) + 1;
	len = l0;
	if (arg1 != NULL)
	{
		l1 = strlen(arg1) + 1;
		len += l1;
		SM_ASSERT(len > l1);
	}

	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;
	(void) memcpy(buf, arg0, l0);
	offset = l0;

	if (arg1 != NULL)
	{
		l1 = strlen(arg1) + 1;
		SM_ASSERT(offset < len);
		SM_ASSERT(offset + l1 <= len);
		(void) memcpy(buf + offset, arg1, l1);
		offset += l1;
		SM_ASSERT(offset > l1);
	}

	r = mi_wr_cmd(ctx->ctx_sd, &timeout, cmd, buf, len);
	free(buf);
	return r;
}

/*
**  SMFI_CHGFROM -- change enveloper sender ("from") address
**
**	Parameters:
**		ctx -- Opaque context structure
**		from -- new envelope sender address ("MAIL From")
**		args -- ESMTP arguments
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_chgfrom(ctx, from, args)
	SMFICTX *ctx;
	char *from;
	char *args;
{
	if (from == NULL || *from == '\0')
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_CHGFROM))
		return MI_FAILURE;
	return send2(ctx, SMFIR_CHGFROM, from, args);
}

/*
**  SMFI_SETSYMLIST -- set list of macros that the MTA should send.
**
**	Parameters:
**		ctx -- Opaque context structure
**		where -- SMTP stage
**		macros -- list of macros
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_setsymlist(ctx, where, macros)
	SMFICTX *ctx;
	int where;
	char *macros;
{
	SM_ASSERT(ctx != NULL);

	if (macros == NULL)
		return MI_FAILURE;
	if (where < SMFIM_FIRST || where > SMFIM_LAST)
		return MI_FAILURE;
	if (where < 0 || where >= MAX_MACROS_ENTRIES)
		return MI_FAILURE;

	if (ctx->ctx_mac_list[where] != NULL)
		return MI_FAILURE;

	ctx->ctx_mac_list[where] = strdup(macros);
	if (ctx->ctx_mac_list[where] == NULL)
		return MI_FAILURE;

	return MI_SUCCESS;
}

/*
**  SMFI_ADDRCPT_PAR -- send an additional recipient to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		rcpt -- recipient address
**		args -- ESMTP arguments
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_addrcpt_par(ctx, rcpt, args)
	SMFICTX *ctx;
	char *rcpt;
	char *args;
{
	if (rcpt == NULL || *rcpt == '\0')
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_ADDRCPT_PAR))
		return MI_FAILURE;
	return send2(ctx, SMFIR_ADDRCPT_PAR, rcpt, args);
}

/*
**  SMFI_ADDRCPT -- send an additional recipient to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		rcpt -- recipient address
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_addrcpt(ctx, rcpt)
	SMFICTX *ctx;
	char *rcpt;
{
	size_t len;
	struct timeval timeout;

	if (rcpt == NULL || *rcpt == '\0')
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_ADDRCPT))
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	len = strlen(rcpt) + 1;
	return mi_wr_cmd(ctx->ctx_sd, &timeout, SMFIR_ADDRCPT, rcpt, len);
}

/*
**  SMFI_DELRCPT -- send a recipient to be removed to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		rcpt -- recipient address
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_delrcpt(ctx, rcpt)
	SMFICTX *ctx;
	char *rcpt;
{
	size_t len;
	struct timeval timeout;

	if (rcpt == NULL || *rcpt == '\0')
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_DELRCPT))
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	len = strlen(rcpt) + 1;
	return mi_wr_cmd(ctx->ctx_sd, &timeout, SMFIR_DELRCPT, rcpt, len);
}

/*
**  SMFI_REPLACEBODY -- send a body chunk to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		bodyp -- body chunk
**		bodylen -- length of body chunk
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_replacebody(ctx, bodyp, bodylen)
	SMFICTX *ctx;
	unsigned char *bodyp;
	int bodylen;
{
	int len, off, r;
	struct timeval timeout;

	if (bodylen < 0 ||
	    (bodyp == NULL && bodylen > 0))
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_CHGBODY))
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;

	/* split body chunk if necessary */
	off = 0;
	do
	{
		len = (bodylen >= MILTER_CHUNK_SIZE) ? MILTER_CHUNK_SIZE :
						       bodylen;
		if ((r = mi_wr_cmd(ctx->ctx_sd, &timeout, SMFIR_REPLBODY,
				(char *) (bodyp + off), len)) != MI_SUCCESS)
			return r;
		off += len;
		bodylen -= len;
	} while (bodylen > 0);
	return MI_SUCCESS;
}

/*
**  SMFI_QUARANTINE -- quarantine an envelope
**
**	Parameters:
**		ctx -- Opaque context structure
**		reason -- why?
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_quarantine(ctx, reason)
	SMFICTX *ctx;
	char *reason;
{
	size_t len;
	int r;
	char *buf;
	struct timeval timeout;

	if (reason == NULL || *reason == '\0')
		return MI_FAILURE;
	if (!mi_sendok(ctx, SMFIF_QUARANTINE))
		return MI_FAILURE;
	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;
	len = strlen(reason) + 1;
	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;
	(void) memcpy(buf, reason, len);
	r = mi_wr_cmd(ctx->ctx_sd, &timeout, SMFIR_QUARANTINE, buf, len);
	free(buf);
	return r;
}

/*
**  MYISENHSC -- check whether a string contains an enhanced status code
**
**	Parameters:
**		s -- string with possible enhanced status code.
**		delim -- delim for enhanced status code.
**
**	Returns:
**		0  -- no enhanced status code.
**		>4 -- length of enhanced status code.
**
**	Side Effects:
**		none.
*/

static int
myisenhsc(s, delim)
	const char *s;
	int delim;
{
	int l, h;

	if (s == NULL)
		return 0;
	if (!((*s == '2' || *s == '4' || *s == '5') && s[1] == '.'))
		return 0;
	h = 0;
	l = 2;
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
		++h;
	if (h == 0 || s[l + h] != '.')
		return 0;
	l += h + 1;
	h = 0;
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
		++h;
	if (h == 0 || s[l + h] != delim)
		return 0;
	return l + h;
}

/*
**  SMFI_SETREPLY -- set the reply code for the next reply to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		rcode -- The three-digit (RFC 821) SMTP reply code.
**		xcode -- The extended (RFC 2034) reply code.
**		message -- The text part of the SMTP reply.
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_setreply(ctx, rcode, xcode, message)
	SMFICTX *ctx;
	char *rcode;
	char *xcode;
	char *message;
{
	size_t len;
	char *buf;

	if (rcode == NULL || ctx == NULL)
		return MI_FAILURE;

	/* ### <sp> \0 */
	len = strlen(rcode) + 2;
	if (len != 5)
		return MI_FAILURE;
	if ((rcode[0] != '4' && rcode[0] != '5') ||
	    !isascii(rcode[1]) || !isdigit(rcode[1]) ||
	    !isascii(rcode[2]) || !isdigit(rcode[2]))
		return MI_FAILURE;
	if (xcode != NULL)
	{
		if (!myisenhsc(xcode, '\0'))
			return MI_FAILURE;
		len += strlen(xcode) + 1;
	}
	if (message != NULL)
	{
		size_t ml;

		/* XXX check also for unprintable chars? */
		if (strpbrk(message, "\r\n") != NULL)
			return MI_FAILURE;
		ml = strlen(message);
		if (ml > MAXREPLYLEN)
			return MI_FAILURE;
		len += ml + 1;
	}
	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;		/* oops */
	(void) sm_strlcpy(buf, rcode, len);
	(void) sm_strlcat(buf, " ", len);
	if (xcode != NULL)
		(void) sm_strlcat(buf, xcode, len);
	if (message != NULL)
	{
		if (xcode != NULL)
			(void) sm_strlcat(buf, " ", len);
		(void) sm_strlcat(buf, message, len);
	}
	if (ctx->ctx_reply != NULL)
		free(ctx->ctx_reply);
	ctx->ctx_reply = buf;
	return MI_SUCCESS;
}

/*
**  SMFI_SETMLREPLY -- set multiline reply code for the next reply to the MTA
**
**	Parameters:
**		ctx -- Opaque context structure
**		rcode -- The three-digit (RFC 821) SMTP reply code.
**		xcode -- The extended (RFC 2034) reply code.
**		txt, ... -- The text part of the SMTP reply,
**			MUST be terminated with NULL.
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
#if SM_VA_STD
smfi_setmlreply(SMFICTX *ctx, const char *rcode, const char *xcode, ...)
#else /* SM_VA_STD */
smfi_setmlreply(ctx, rcode, xcode, va_alist)
	SMFICTX *ctx;
	const char *rcode;
	const char *xcode;
	va_dcl
#endif /* SM_VA_STD */
{
	size_t len;
	size_t rlen;
	int args;
	char *buf, *txt;
	const char *xc;
	char repl[16];
	SM_VA_LOCAL_DECL

	if (rcode == NULL || ctx == NULL)
		return MI_FAILURE;

	/* ### <sp> */
	len = strlen(rcode) + 1;
	if (len != 4)
		return MI_FAILURE;
	if ((rcode[0] != '4' && rcode[0] != '5') ||
	    !isascii(rcode[1]) || !isdigit(rcode[1]) ||
	    !isascii(rcode[2]) || !isdigit(rcode[2]))
		return MI_FAILURE;
	if (xcode != NULL)
	{
		if (!myisenhsc(xcode, '\0'))
			return MI_FAILURE;
		xc = xcode;
	}
	else
	{
		if (rcode[0] == '4')
			xc = "4.0.0";
		else
			xc = "5.0.0";
	}

	/* add trailing space */
	len += strlen(xc) + 1;
	rlen = len;
	args = 0;
	SM_VA_START(ap, xcode);
	while ((txt = SM_VA_ARG(ap, char *)) != NULL)
	{
		size_t tl;

		tl = strlen(txt);
		if (tl > MAXREPLYLEN)
			break;

		/* this text, reply codes, \r\n */
		len += tl + 2 + rlen;
		if (++args > MAXREPLIES)
			break;

		/* XXX check also for unprintable chars? */
		if (strpbrk(txt, "\r\n") != NULL)
			break;
	}
	SM_VA_END(ap);
	if (txt != NULL)
		return MI_FAILURE;

	/* trailing '\0' */
	++len;
	buf = malloc(len);
	if (buf == NULL)
		return MI_FAILURE;		/* oops */
	(void) sm_strlcpyn(buf, len, 3, rcode, args == 1 ? " " : "-", xc);
	(void) sm_strlcpyn(repl, sizeof repl, 4, rcode, args == 1 ? " " : "-",
			   xc, " ");
	SM_VA_START(ap, xcode);
	txt = SM_VA_ARG(ap, char *);
	if (txt != NULL)
	{
		(void) sm_strlcat2(buf, " ", txt, len);
		while ((txt = SM_VA_ARG(ap, char *)) != NULL)
		{
			if (--args <= 1)
				repl[3] = ' ';
			(void) sm_strlcat2(buf, "\r\n", repl, len);
			(void) sm_strlcat(buf, txt, len);
		}
	}
	if (ctx->ctx_reply != NULL)
		free(ctx->ctx_reply);
	ctx->ctx_reply = buf;
	SM_VA_END(ap);
	return MI_SUCCESS;
}

/*
**  SMFI_SETPRIV -- set private data
**
**	Parameters:
**		ctx -- Opaque context structure
**		privatedata -- pointer to private data
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_setpriv(ctx, privatedata)
	SMFICTX *ctx;
	void *privatedata;
{
	if (ctx == NULL)
		return MI_FAILURE;
	ctx->ctx_privdata = privatedata;
	return MI_SUCCESS;
}

/*
**  SMFI_GETPRIV -- get private data
**
**	Parameters:
**		ctx -- Opaque context structure
**
**	Returns:
**		pointer to private data
*/

void *
smfi_getpriv(ctx)
	SMFICTX *ctx;
{
	if (ctx == NULL)
		return NULL;
	return ctx->ctx_privdata;
}

/*
**  SMFI_GETSYMVAL -- get the value of a macro
**
**	See explanation in mfapi.h about layout of the structures.
**
**	Parameters:
**		ctx -- Opaque context structure
**		symname -- name of macro
**
**	Returns:
**		value of macro (NULL in case of failure)
*/

char *
smfi_getsymval(ctx, symname)
	SMFICTX *ctx;
	char *symname;
{
	int i;
	char **s;
	char one[2];
	char braces[4];

	if (ctx == NULL || symname == NULL || *symname == '\0')
		return NULL;

	if (strlen(symname) == 3 && symname[0] == '{' && symname[2] == '}')
	{
		one[0] = symname[1];
		one[1] = '\0';
	}
	else
		one[0] = '\0';
	if (strlen(symname) == 1)
	{
		braces[0] = '{';
		braces[1] = *symname;
		braces[2] = '}';
		braces[3] = '\0';
	}
	else
		braces[0] = '\0';

	/* search backwards through the macro array */
	for (i = MAX_MACROS_ENTRIES - 1 ; i >= 0; --i)
	{
		if ((s = ctx->ctx_mac_ptr[i]) == NULL ||
		    ctx->ctx_mac_buf[i] == NULL)
			continue;
		while (s != NULL && *s != NULL)
		{
			if (strcmp(*s, symname) == 0)
				return *++s;
			if (one[0] != '\0' && strcmp(*s, one) == 0)
				return *++s;
			if (braces[0] != '\0' && strcmp(*s, braces) == 0)
				return *++s;
			++s;	/* skip over macro value */
			++s;	/* points to next macro name */
		}
	}
	return NULL;
}

/*
**  SMFI_PROGRESS -- send "progress" message to the MTA to prevent premature
**		     timeouts during long milter-side operations
**
**	Parameters:
**		ctx -- Opaque context structure
**
**	Return value:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_progress(ctx)
	SMFICTX *ctx;
{
	struct timeval timeout;

	if (ctx == NULL)
		return MI_FAILURE;

	timeout.tv_sec = ctx->ctx_timeout;
	timeout.tv_usec = 0;

	return mi_wr_cmd(ctx->ctx_sd, &timeout, SMFIR_PROGRESS, NULL, 0);
}

/*
**  SMFI_VERSION -- return (runtime) version of libmilter
**
**	Parameters:
**		major -- (pointer to) major version
**		minor -- (pointer to) minor version
**		patchlevel -- (pointer to) patchlevel version
**
**	Return value:
**		MI_SUCCESS
*/

int
smfi_version(major, minor, patchlevel)
	unsigned int *major;
	unsigned int *minor;
	unsigned int *patchlevel;
{
	if (major != NULL)
		*major = SM_LM_VRS_MAJOR(SMFI_VERSION);
	if (minor != NULL)
		*minor = SM_LM_VRS_MINOR(SMFI_VERSION);
	if (patchlevel != NULL)
		*patchlevel = SM_LM_VRS_PLVL(SMFI_VERSION);
	return MI_SUCCESS;
}
