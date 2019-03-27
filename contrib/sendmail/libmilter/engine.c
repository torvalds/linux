/*
 *  Copyright (c) 1999-2004, 2006-2008 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: engine.c,v 8.168 2013-11-22 20:51:36 ca Exp $")

#include "libmilter.h"

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */

/* generic argument for functions in the command table */
struct arg_struct
{
	size_t		a_len;		/* length of buffer */
	char		*a_buf;		/* argument string */
	int		a_idx;		/* index for macro array */
	SMFICTX_PTR	a_ctx;		/* context */
};

typedef struct arg_struct genarg;

/* structure for commands received from MTA */
struct cmdfct_t
{
	char	cm_cmd;				/* command */
	int	cm_argt;			/* type of arguments expected */
	int	cm_next;			/* next state */
	int	cm_todo;			/* what to do next */
	int	cm_macros;			/* index for macros */
	int	(*cm_fct) __P((genarg *));	/* function to execute */
};

typedef struct cmdfct_t cmdfct;

/* possible values for cm_argt */
#define	CM_BUF	0
#define	CM_NULLOK 1

/* possible values for cm_todo */
#define	CT_CONT		0x0000	/* continue reading commands */
#define	CT_IGNO		0x0001	/* continue even when error  */

/* not needed right now, done via return code instead */
#define	CT_KEEP		0x0004	/* keep buffer (contains symbols) */
#define	CT_END		0x0008	/* last command of session, stop replying */

/* index in macro array: macros only for these commands */
#define	CI_NONE		(-1)
#define	CI_CONN		0
#define	CI_HELO		1
#define	CI_MAIL		2
#define CI_RCPT		3
#define CI_DATA		4
#define CI_EOM		5
#define CI_EOH		6
#define CI_LAST		CI_EOH
#if CI_LAST < CI_DATA
ERROR: do not compile with CI_LAST < CI_DATA
#endif
#if CI_LAST < CI_EOM
ERROR: do not compile with CI_LAST < CI_EOM
#endif
#if CI_LAST < CI_EOH
ERROR: do not compile with CI_LAST < CI_EOH
#endif
#if CI_LAST < CI_ENVRCPT
ERROR: do not compile with CI_LAST < CI_ENVRCPT
#endif
#if CI_LAST < CI_ENVFROM
ERROR: do not compile with CI_LAST < CI_ENVFROM
#endif
#if CI_LAST < CI_HELO
ERROR: do not compile with CI_LAST < CI_HELO
#endif
#if CI_LAST < CI_CONNECT
ERROR: do not compile with CI_LAST < CI_CONNECT
#endif
#if CI_LAST >= MAX_MACROS_ENTRIES
ERROR: do not compile with CI_LAST >= MAX_MACROS_ENTRIES
#endif

/* function prototypes */
static int	st_abortfct __P((genarg *));
static int	st_macros __P((genarg *));
static int	st_optionneg __P((genarg *));
static int	st_bodychunk __P((genarg *));
static int	st_connectinfo __P((genarg *));
static int	st_bodyend __P((genarg *));
static int	st_helo __P((genarg *));
static int	st_header __P((genarg *));
static int	st_sender __P((genarg *));
static int	st_rcpt __P((genarg *));
static int	st_unknown __P((genarg *));
static int	st_data __P((genarg *));
static int	st_eoh __P((genarg *));
static int	st_quit __P((genarg *));
static int	sendreply __P((sfsistat, socket_t, struct timeval *, SMFICTX_PTR));
static void	fix_stm __P((SMFICTX_PTR));
static bool	trans_ok __P((int, int));
static char	**dec_argv __P((char *, size_t));
static int	dec_arg2 __P((char *, size_t, char **, char **));
static void	mi_clr_symlist __P((SMFICTX_PTR));

#if _FFR_WORKERS_POOL
static bool     mi_rd_socket_ready __P((int));
#endif /* _FFR_WORKERS_POOL */

/* states */
#define ST_NONE	(-1)
#define ST_INIT	0	/* initial state */
#define ST_OPTS	1	/* option negotiation */
#define ST_CONN	2	/* connection info */
#define ST_HELO	3	/* helo */
#define ST_MAIL	4	/* mail from */
#define ST_RCPT	5	/* rcpt to */
#define ST_DATA	6	/* data */
#define ST_HDRS	7	/* headers */
#define ST_EOHS	8	/* end of headers */
#define ST_BODY	9	/* body */
#define ST_ENDM	10	/* end of message */
#define ST_QUIT	11	/* quit */
#define ST_ABRT	12	/* abort */
#define ST_UNKN 13	/* unknown SMTP command */
#define ST_Q_NC	14	/* quit, new connection follows */
#define ST_LAST	ST_Q_NC	/* last valid state */
#define ST_SKIP	16	/* not a state but required for the state table */

/* in a mail transaction? must be before eom according to spec. */
#define ST_IN_MAIL(st)	((st) >= ST_MAIL && (st) < ST_ENDM)

/*
**  set of next states
**  each state (ST_*) corresponds to bit in an int value (1 << state)
**  each state has a set of allowed transitions ('or' of bits of states)
**  so a state transition is valid if the mask of the next state
**  is set in the NX_* value
**  this function is coded in trans_ok(), see below.
*/

#define MI_MASK(x)	(0x0001 << (x))	/* generate a bit "mask" for a state */
#define NX_INIT	(MI_MASK(ST_OPTS))
#define NX_OPTS	(MI_MASK(ST_CONN) | MI_MASK(ST_UNKN))
#define NX_CONN	(MI_MASK(ST_HELO) | MI_MASK(ST_MAIL) | MI_MASK(ST_UNKN))
#define NX_HELO	(MI_MASK(ST_HELO) | MI_MASK(ST_MAIL) | MI_MASK(ST_UNKN))
#define NX_MAIL	(MI_MASK(ST_RCPT) | MI_MASK(ST_ABRT) | MI_MASK(ST_UNKN))
#define NX_RCPT	(MI_MASK(ST_HDRS) | MI_MASK(ST_EOHS) | MI_MASK(ST_DATA) | \
		 MI_MASK(ST_BODY) | MI_MASK(ST_ENDM) | \
		 MI_MASK(ST_RCPT) | MI_MASK(ST_ABRT) | MI_MASK(ST_UNKN))
#define NX_DATA	(MI_MASK(ST_EOHS) | MI_MASK(ST_HDRS) | MI_MASK(ST_ABRT))
#define NX_HDRS	(MI_MASK(ST_EOHS) | MI_MASK(ST_HDRS) | MI_MASK(ST_ABRT))
#define NX_EOHS	(MI_MASK(ST_BODY) | MI_MASK(ST_ENDM) | MI_MASK(ST_ABRT))
#define NX_BODY	(MI_MASK(ST_ENDM) | MI_MASK(ST_BODY) | MI_MASK(ST_ABRT))
#define NX_ENDM	(MI_MASK(ST_QUIT) | MI_MASK(ST_MAIL) | MI_MASK(ST_UNKN) | \
		MI_MASK(ST_Q_NC))
#define NX_QUIT	0
#define NX_ABRT	0
#define NX_UNKN (MI_MASK(ST_HELO) | MI_MASK(ST_MAIL) | \
		 MI_MASK(ST_RCPT) | MI_MASK(ST_ABRT) | \
		 MI_MASK(ST_DATA) | \
		 MI_MASK(ST_BODY) | MI_MASK(ST_UNKN) | \
		 MI_MASK(ST_ABRT) | MI_MASK(ST_QUIT) | MI_MASK(ST_Q_NC))
#define NX_Q_NC	(MI_MASK(ST_CONN) | MI_MASK(ST_UNKN))
#define NX_SKIP MI_MASK(ST_SKIP)

static int next_states[] =
{
	  NX_INIT
	, NX_OPTS
	, NX_CONN
	, NX_HELO
	, NX_MAIL
	, NX_RCPT
	, NX_DATA
	, NX_HDRS
	, NX_EOHS
	, NX_BODY
	, NX_ENDM
	, NX_QUIT
	, NX_ABRT
	, NX_UNKN
	, NX_Q_NC
};

#define SIZE_NEXT_STATES	(sizeof(next_states) / sizeof(next_states[0]))

/* commands received by milter */
static cmdfct cmds[] =
{
  {SMFIC_ABORT,		CM_NULLOK,	ST_ABRT,  CT_CONT,  CI_NONE, st_abortfct}
, {SMFIC_MACRO,		CM_BUF,		ST_NONE,  CT_KEEP,  CI_NONE, st_macros	}
, {SMFIC_BODY,		CM_BUF,		ST_BODY,  CT_CONT,  CI_NONE, st_bodychunk}
, {SMFIC_CONNECT,	CM_BUF,		ST_CONN,  CT_CONT,  CI_CONN, st_connectinfo}
, {SMFIC_BODYEOB,	CM_NULLOK,	ST_ENDM,  CT_CONT,  CI_EOM,  st_bodyend	}
, {SMFIC_HELO,		CM_BUF,		ST_HELO,  CT_CONT,  CI_HELO, st_helo	}
, {SMFIC_HEADER,	CM_BUF,		ST_HDRS,  CT_CONT,  CI_NONE, st_header	}
, {SMFIC_MAIL,		CM_BUF,		ST_MAIL,  CT_CONT,  CI_MAIL, st_sender	}
, {SMFIC_OPTNEG,	CM_BUF,		ST_OPTS,  CT_CONT,  CI_NONE, st_optionneg}
, {SMFIC_EOH,		CM_NULLOK,	ST_EOHS,  CT_CONT,  CI_EOH,  st_eoh	}
, {SMFIC_QUIT,		CM_NULLOK,	ST_QUIT,  CT_END,   CI_NONE, st_quit	}
, {SMFIC_DATA,		CM_NULLOK,	ST_DATA,  CT_CONT,  CI_DATA, st_data	}
, {SMFIC_RCPT,		CM_BUF,		ST_RCPT,  CT_IGNO,  CI_RCPT, st_rcpt	}
, {SMFIC_UNKNOWN,	CM_BUF,		ST_UNKN,  CT_IGNO,  CI_NONE, st_unknown	}
, {SMFIC_QUIT_NC,	CM_NULLOK,	ST_Q_NC,  CT_CONT,  CI_NONE, st_quit	}
};

/*
**  Additional (internal) reply codes;
**  must be coordinated wit libmilter/mfapi.h
*/

#define _SMFIS_KEEP	20
#define _SMFIS_ABORT	21
#define _SMFIS_OPTIONS	22
#define _SMFIS_NOREPLY	SMFIS_NOREPLY
#define _SMFIS_FAIL	(-1)
#define _SMFIS_NONE	(-2)

/*
**  MI_ENGINE -- receive commands and process them
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		MI_FAILURE/MI_SUCCESS
*/

int
mi_engine(ctx)
	SMFICTX_PTR ctx;
{
	size_t len;
	int i;
	socket_t sd;
	int ret = MI_SUCCESS;
	int ncmds = sizeof(cmds) / sizeof(cmdfct);
	int curstate = ST_INIT;
	int newstate;
	bool call_abort;
	sfsistat r;
	char cmd;
	char *buf = NULL;
	genarg arg;
	struct timeval timeout;
	int (*f) __P((genarg *));
	sfsistat (*fi_abort) __P((SMFICTX *));
	sfsistat (*fi_close) __P((SMFICTX *));

	arg.a_ctx = ctx;
	sd = ctx->ctx_sd;
	fi_abort = ctx->ctx_smfi->xxfi_abort;
#if _FFR_WORKERS_POOL
	curstate = ctx->ctx_state;
	if (curstate == ST_INIT)
	{
		mi_clr_macros(ctx, 0);
		fix_stm(ctx);
	}
#else   /* _FFR_WORKERS_POOL */
	mi_clr_macros(ctx, 0);
	fix_stm(ctx);
#endif  /* _FFR_WORKERS_POOL */
	r = _SMFIS_NONE;
	do
	{
		/* call abort only if in a mail transaction */
		call_abort = ST_IN_MAIL(curstate);
		timeout.tv_sec = ctx->ctx_timeout;
		timeout.tv_usec = 0;
		if (mi_stop() == MILTER_ABRT)
		{
			if (ctx->ctx_dbg > 3)
				sm_dprintf("[%lu] milter_abort\n",
					(long) ctx->ctx_id);
			ret = MI_FAILURE;
			break;
		}

		/*
		**  Notice: buf is allocated by mi_rd_cmd() and it will
		**  usually be free()d after it has been used in f().
		**  However, if the function returns _SMFIS_KEEP then buf
		**  contains macros and will not be free()d.
		**  Hence r must be set to _SMFIS_NONE if a new buf is
		**  allocated to avoid problem with housekeeping, esp.
		**  if the code "break"s out of the loop.
		*/

#if _FFR_WORKERS_POOL
		/* Is the socket ready to be read ??? */
		if (!mi_rd_socket_ready(sd))
		{
			ret = MI_CONTINUE;
			break;
		}
#endif  /* _FFR_WORKERS_POOL */

		r = _SMFIS_NONE;
		if ((buf = mi_rd_cmd(sd, &timeout, &cmd, &len,
				     ctx->ctx_smfi->xxfi_name)) == NULL &&
		    cmd < SMFIC_VALIDCMD)
		{
			if (ctx->ctx_dbg > 5)
				sm_dprintf("[%lu] mi_engine: mi_rd_cmd error (%x)\n",
					(long) ctx->ctx_id, (int) cmd);

			/*
			**  eof is currently treated as failure ->
			**  abort() instead of close(), otherwise use:
			**  if (cmd != SMFIC_EOF)
			*/

			ret = MI_FAILURE;
			break;
		}
		if (ctx->ctx_dbg > 4)
			sm_dprintf("[%lu] got cmd '%c' len %d\n",
				(long) ctx->ctx_id, cmd, (int) len);
		for (i = 0; i < ncmds; i++)
		{
			if (cmd == cmds[i].cm_cmd)
				break;
		}
		if (i >= ncmds)
		{
			/* unknown command */
			if (ctx->ctx_dbg > 1)
				sm_dprintf("[%lu] cmd '%c' unknown\n",
					(long) ctx->ctx_id, cmd);
			ret = MI_FAILURE;
			break;
		}
		if ((f = cmds[i].cm_fct) == NULL)
		{
			/* stop for now */
			if (ctx->ctx_dbg > 1)
				sm_dprintf("[%lu] cmd '%c' not impl\n",
					(long) ctx->ctx_id, cmd);
			ret = MI_FAILURE;
			break;
		}

		/* is new state ok? */
		newstate = cmds[i].cm_next;
		if (ctx->ctx_dbg > 5)
			sm_dprintf("[%lu] cur %x new %x nextmask %x\n",
				(long) ctx->ctx_id,
				curstate, newstate, next_states[curstate]);

		if (newstate != ST_NONE && !trans_ok(curstate, newstate))
		{
			if (ctx->ctx_dbg > 1)
				sm_dprintf("[%lu] abort: cur %d (%x) new %d (%x) next %x\n",
					(long) ctx->ctx_id,
					curstate, MI_MASK(curstate),
					newstate, MI_MASK(newstate),
					next_states[curstate]);

			/* call abort only if in a mail transaction */
			if (fi_abort != NULL && call_abort)
				(void) (*fi_abort)(ctx);

			/*
			**  try to reach the new state from HELO
			**  if it can't be reached, ignore the command.
			*/

			curstate = ST_HELO;
			if (!trans_ok(curstate, newstate))
			{
				if (buf != NULL)
				{
					free(buf);
					buf = NULL;
				}
				continue;
			}
		}
		if (cmds[i].cm_argt != CM_NULLOK && buf == NULL)
		{
			/* stop for now */
			if (ctx->ctx_dbg > 1)
				sm_dprintf("[%lu] cmd='%c', buf=NULL\n",
					(long) ctx->ctx_id, cmd);
			ret = MI_FAILURE;
			break;
		}
		arg.a_len = len;
		arg.a_buf = buf;
		if (newstate != ST_NONE)
		{
			curstate = newstate;
			ctx->ctx_state = curstate;
		}
		arg.a_idx = cmds[i].cm_macros;
		call_abort = ST_IN_MAIL(curstate);

		/* call function to deal with command */
		MI_MONITOR_BEGIN(ctx, cmd);
		r = (*f)(&arg);
		MI_MONITOR_END(ctx, cmd);
		if (r != _SMFIS_KEEP && buf != NULL)
		{
			free(buf);
			buf = NULL;
		}
		if (sendreply(r, sd, &timeout, ctx) != MI_SUCCESS)
		{
			ret = MI_FAILURE;
			break;
		}

		if (r == SMFIS_ACCEPT)
		{
			/* accept mail, no further actions taken */
			curstate = ST_HELO;
		}
		else if (r == SMFIS_REJECT || r == SMFIS_DISCARD ||
			 r ==  SMFIS_TEMPFAIL)
		{
			/*
			**  further actions depend on current state
			**  if the IGNO bit is set: "ignore" the error,
			**  i.e., stay in the current state
			*/
			if (!bitset(CT_IGNO, cmds[i].cm_todo))
				curstate = ST_HELO;
		}
		else if (r == _SMFIS_ABORT)
		{
			if (ctx->ctx_dbg > 5)
				sm_dprintf("[%lu] function returned abort\n",
					(long) ctx->ctx_id);
			ret = MI_FAILURE;
			break;
		}
	} while (!bitset(CT_END, cmds[i].cm_todo));

	ctx->ctx_state = curstate;

	if (ret == MI_FAILURE)
	{
		/* call abort only if in a mail transaction */
		if (fi_abort != NULL && call_abort)
			(void) (*fi_abort)(ctx);
	}

	/* has close been called? */
	if (ctx->ctx_state != ST_QUIT
#if _FFR_WORKERS_POOL
	   && ret != MI_CONTINUE
#endif /* _FFR_WORKERS_POOL */
	   )
	{
		if ((fi_close = ctx->ctx_smfi->xxfi_close) != NULL)
			(void) (*fi_close)(ctx);
	}
	if (r != _SMFIS_KEEP && buf != NULL)
		free(buf);
#if !_FFR_WORKERS_POOL
	mi_clr_macros(ctx, 0);
#endif /* _FFR_WORKERS_POOL */
	return ret;
}

static size_t milter_addsymlist __P((SMFICTX_PTR, char *, char **));

static size_t
milter_addsymlist(ctx, buf, newbuf)
	SMFICTX_PTR ctx;
	char *buf;
	char **newbuf;
{
	size_t len;
	int i;
	mi_int32 v;
	char *buffer;

	SM_ASSERT(ctx != NULL);
	SM_ASSERT(buf != NULL);
	SM_ASSERT(newbuf != NULL);
	len = 0;
	for (i = 0; i < MAX_MACROS_ENTRIES; i++)
	{
		if (ctx->ctx_mac_list[i] != NULL)
		{
			len += strlen(ctx->ctx_mac_list[i]) + 1 +
				MILTER_LEN_BYTES;
		}
	}
	if (len > 0)
	{
		size_t offset;

		SM_ASSERT(len + MILTER_OPTLEN > len);
		len += MILTER_OPTLEN;
		buffer = malloc(len);
		if (buffer != NULL)
		{
			(void) memcpy(buffer, buf, MILTER_OPTLEN);
			offset = MILTER_OPTLEN;
			for (i = 0; i < MAX_MACROS_ENTRIES; i++)
			{
				size_t l;

				if (ctx->ctx_mac_list[i] == NULL)
					continue;

				SM_ASSERT(offset + MILTER_LEN_BYTES < len);
				v = htonl(i);
				(void) memcpy(buffer + offset, (void *) &v,
						MILTER_LEN_BYTES);
				offset += MILTER_LEN_BYTES;
				l = strlen(ctx->ctx_mac_list[i]) + 1;
				SM_ASSERT(offset + l <= len);
				(void) memcpy(buffer + offset,
						ctx->ctx_mac_list[i], l);
				offset += l;
			}
		}
		else
		{
			/* oops ... */
		}
	}
	else
	{
		len = MILTER_OPTLEN;
		buffer = buf;
	}
	*newbuf = buffer;
	return len;
}

/*
**  GET_NR_BIT -- get "no reply" bit matching state
**
**	Parameters:
**		state -- current protocol stage
**
**	Returns:
**		0: no matching bit
**		>0: the matching "no reply" bit
*/

static unsigned long get_nr_bit __P((int));

static unsigned long
get_nr_bit(state)
	int state;
{
	unsigned long bit;

	switch (state)
	{
	  case ST_CONN:
		bit = SMFIP_NR_CONN;
		break;
	  case ST_HELO:
		bit = SMFIP_NR_HELO;
		break;
	  case ST_MAIL:
		bit = SMFIP_NR_MAIL;
		break;
	  case ST_RCPT:
		bit = SMFIP_NR_RCPT;
		break;
	  case ST_DATA:
		bit = SMFIP_NR_DATA;
		break;
	  case ST_UNKN:
		bit = SMFIP_NR_UNKN;
		break;
	  case ST_HDRS:
		bit = SMFIP_NR_HDR;
		break;
	  case ST_EOHS:
		bit = SMFIP_NR_EOH;
		break;
	  case ST_BODY:
		bit = SMFIP_NR_BODY;
		break;
	  default:
		bit = 0;
		break;
	}
	return bit;
}

/*
**  SENDREPLY -- send a reply to the MTA
**
**	Parameters:
**		r -- reply code
**		sd -- socket descriptor
**		timeout_ptr -- (ptr to) timeout to use for sending
**		ctx -- context structure
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
sendreply(r, sd, timeout_ptr, ctx)
	sfsistat r;
	socket_t sd;
	struct timeval *timeout_ptr;
	SMFICTX_PTR ctx;
{
	int ret;
	unsigned long bit;

	ret = MI_SUCCESS;

	bit = get_nr_bit(ctx->ctx_state);
	if (bit != 0 && (ctx->ctx_pflags & bit) != 0 && r != SMFIS_NOREPLY)
	{
		if (r >= SMFIS_CONTINUE && r < _SMFIS_KEEP)
		{
			/* milter said it wouldn't reply, but it lied... */
			smi_log(SMI_LOG_ERR,
				"%s: milter claimed not to reply in state %d but did anyway %d\n",
				ctx->ctx_smfi->xxfi_name,
				ctx->ctx_state, r);

		}

		/*
		**  Force specified behavior, otherwise libmilter
		**  and MTA will fail to communicate properly.
		*/

		switch (r)
		{
		  case SMFIS_CONTINUE:
		  case SMFIS_TEMPFAIL:
		  case SMFIS_REJECT:
		  case SMFIS_DISCARD:
		  case SMFIS_ACCEPT:
		  case SMFIS_SKIP:
		  case _SMFIS_OPTIONS:
			r = SMFIS_NOREPLY;
			break;
		}
	}

	switch (r)
	{
	  case SMFIS_CONTINUE:
		ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_CONTINUE, NULL, 0);
		break;
	  case SMFIS_TEMPFAIL:
	  case SMFIS_REJECT:
		if (ctx->ctx_reply != NULL &&
		    ((r == SMFIS_TEMPFAIL && *ctx->ctx_reply == '4') ||
		     (r == SMFIS_REJECT && *ctx->ctx_reply == '5')))
		{
			ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_REPLYCODE,
					ctx->ctx_reply,
					strlen(ctx->ctx_reply) + 1);
			free(ctx->ctx_reply);
			ctx->ctx_reply = NULL;
		}
		else
		{
			ret = mi_wr_cmd(sd, timeout_ptr, r == SMFIS_REJECT ?
					SMFIR_REJECT : SMFIR_TEMPFAIL, NULL, 0);
		}
		break;
	  case SMFIS_DISCARD:
		ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_DISCARD, NULL, 0);
		break;
	  case SMFIS_ACCEPT:
		ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_ACCEPT, NULL, 0);
		break;
	  case SMFIS_SKIP:
		ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_SKIP, NULL, 0);
		break;
	  case _SMFIS_OPTIONS:
		{
			mi_int32 v;
			size_t len;
			char *buffer;
			char buf[MILTER_OPTLEN];

			v = htonl(ctx->ctx_prot_vers2mta);
			(void) memcpy(&(buf[0]), (void *) &v,
				      MILTER_LEN_BYTES);
			v = htonl(ctx->ctx_aflags);
			(void) memcpy(&(buf[MILTER_LEN_BYTES]), (void *) &v,
				      MILTER_LEN_BYTES);
			v = htonl(ctx->ctx_pflags2mta);
			(void) memcpy(&(buf[MILTER_LEN_BYTES * 2]),
				      (void *) &v, MILTER_LEN_BYTES);
			len = milter_addsymlist(ctx, buf, &buffer);
			if (buffer != NULL)
				ret = mi_wr_cmd(sd, timeout_ptr, SMFIC_OPTNEG,
						buffer, len);
			else
				ret = MI_FAILURE;
		}
		break;
	  case SMFIS_NOREPLY:
		if (bit != 0 &&
		    (ctx->ctx_pflags & bit) != 0 &&
		    (ctx->ctx_mta_pflags & bit) == 0)
		{
			/*
			**  milter doesn't want to send a reply,
			**  but the MTA doesn't have that feature: fake it.
			*/

			ret = mi_wr_cmd(sd, timeout_ptr, SMFIR_CONTINUE, NULL,
					0);
		}
		break;
	  default:	/* don't send a reply */
		break;
	}
	return ret;
}

/*
**  MI_CLR_MACROS -- clear set of macros starting from a given index
**
**	Parameters:
**		ctx -- context structure
**		m -- index from which to clear all macros
**
**	Returns:
**		None.
*/

void
mi_clr_macros(ctx, m)
	SMFICTX_PTR ctx;
	int m;
{
	int i;

	for (i = m; i < MAX_MACROS_ENTRIES; i++)
	{
		if (ctx->ctx_mac_ptr[i] != NULL)
		{
			free(ctx->ctx_mac_ptr[i]);
			ctx->ctx_mac_ptr[i] = NULL;
		}
		if (ctx->ctx_mac_buf[i] != NULL)
		{
			free(ctx->ctx_mac_buf[i]);
			ctx->ctx_mac_buf[i] = NULL;
		}
	}
}

/*
**  MI_CLR_SYMLIST -- clear list of macros
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		None.
*/

static void
mi_clr_symlist(ctx)
	SMFICTX *ctx;
{
	int i;

	SM_ASSERT(ctx != NULL);
	for (i = SMFIM_FIRST; i <= SMFIM_LAST; i++)
	{
		if (ctx->ctx_mac_list[i] != NULL)
		{
			free(ctx->ctx_mac_list[i]);
			ctx->ctx_mac_list[i] = NULL;
		}
	}
}

/*
**  MI_CLR_CTX -- clear context
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		None.
*/

void
mi_clr_ctx(ctx)
	SMFICTX *ctx;
{
	SM_ASSERT(ctx != NULL);
	if (ValidSocket(ctx->ctx_sd))
	{
		(void) closesocket(ctx->ctx_sd);
		ctx->ctx_sd = INVALID_SOCKET;
	}
	if (ctx->ctx_reply != NULL)
	{
		free(ctx->ctx_reply);
		ctx->ctx_reply = NULL;
	}
	if (ctx->ctx_privdata != NULL)
	{
		smi_log(SMI_LOG_WARN,
			"%s: private data not NULL",
			ctx->ctx_smfi->xxfi_name);
	}
	mi_clr_macros(ctx, 0);
	mi_clr_symlist(ctx);
	free(ctx);
}

/*
**  ST_OPTIONNEG -- negotiate options
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		abort/send options/continue
*/

static int
st_optionneg(g)
	genarg *g;
{
	mi_int32 i, v, fake_pflags, internal_pflags;
	SMFICTX_PTR ctx;
#if _FFR_MILTER_CHECK
	bool testmode = false;
#endif /* _FFR_MILTER_CHECK */
	int (*fi_negotiate) __P((SMFICTX *,
					unsigned long, unsigned long,
					unsigned long, unsigned long,
					unsigned long *, unsigned long *,
					unsigned long *, unsigned long *));

	if (g == NULL || g->a_ctx->ctx_smfi == NULL)
		return SMFIS_CONTINUE;
	ctx = g->a_ctx;
	mi_clr_macros(ctx, g->a_idx + 1);
	ctx->ctx_prot_vers = SMFI_PROT_VERSION;

	/* check for minimum length */
	if (g->a_len < MILTER_OPTLEN)
	{
		smi_log(SMI_LOG_ERR,
			"%s: st_optionneg[%ld]: len too short %d < %d",
			ctx->ctx_smfi->xxfi_name,
			(long) ctx->ctx_id, (int) g->a_len,
			MILTER_OPTLEN);
		return _SMFIS_ABORT;
	}

	/* protocol version */
	(void) memcpy((void *) &i, (void *) &(g->a_buf[0]), MILTER_LEN_BYTES);
	v = ntohl(i);

#define SMFI_PROT_VERSION_MIN	2

	/* check for minimum version */
	if (v < SMFI_PROT_VERSION_MIN)
	{
		smi_log(SMI_LOG_ERR,
			"%s: st_optionneg[%ld]: protocol version too old %d < %d",
			ctx->ctx_smfi->xxfi_name,
			(long) ctx->ctx_id, v, SMFI_PROT_VERSION_MIN);
		return _SMFIS_ABORT;
	}
	ctx->ctx_mta_prot_vers = v;
	if (ctx->ctx_prot_vers < ctx->ctx_mta_prot_vers)
		ctx->ctx_prot_vers2mta = ctx->ctx_prot_vers;
	else
		ctx->ctx_prot_vers2mta = ctx->ctx_mta_prot_vers;

	(void) memcpy((void *) &i, (void *) &(g->a_buf[MILTER_LEN_BYTES]),
		      MILTER_LEN_BYTES);
	v = ntohl(i);

	/* no flags? set to default value for V1 actions */
	if (v == 0)
		v = SMFI_V1_ACTS;
	ctx->ctx_mta_aflags = v;	/* MTA action flags */

	internal_pflags = 0;
	(void) memcpy((void *) &i, (void *) &(g->a_buf[MILTER_LEN_BYTES * 2]),
		      MILTER_LEN_BYTES);
	v = ntohl(i);

	/* no flags? set to default value for V1 protocol */
	if (v == 0)
		v = SMFI_V1_PROT;
#if _FFR_MDS_NEGOTIATE
	else if (ctx->ctx_smfi->xxfi_version >= SMFI_VERSION_MDS)
	{
		/*
		**  Allow changing the size only if milter is compiled
		**  against a version that supports this.
		**  If a milter is dynamically linked against a newer
		**  libmilter version, we don't want to "surprise"
		**  it with a larger buffer as it may rely on it
		**  even though it is not documented as a limit.
		*/

		if (bitset(SMFIP_MDS_1M, v))
		{
			internal_pflags |= SMFIP_MDS_1M;
			(void) smfi_setmaxdatasize(MILTER_MDS_1M);
		}
		else if (bitset(SMFIP_MDS_256K, v))
		{
			internal_pflags |= SMFIP_MDS_256K;
			(void) smfi_setmaxdatasize(MILTER_MDS_256K);
		}
	}
# if 0
	/* don't log this for now... */
	else if (ctx->ctx_smfi->xxfi_version < SMFI_VERSION_MDS &&
		 bitset(SMFIP_MDS_1M|SMFIP_MDS_256K, v))
	{
		smi_log(SMI_LOG_WARN,
			"%s: st_optionneg[%ld]: milter version=%X, trying flags=%X",
			ctx->ctx_smfi->xxfi_name,
			(long) ctx->ctx_id, ctx->ctx_smfi->xxfi_version, v);
	}
# endif /* 0 */
#endif /* _FFR_MDS_NEGOTIATE */

	/*
	**  MTA protocol flags.
	**  We pass the internal flags to the milter as "read only",
	**  i.e., a milter can read them so it knows which size
	**  will be used, but any changes by a milter will be ignored
	**  (see below, search for SMFI_INTERNAL).
	*/

	ctx->ctx_mta_pflags = (v & ~SMFI_INTERNAL) | internal_pflags;

	/*
	**  Copy flags from milter struct into libmilter context;
	**  this variable will be used later on to check whether
	**  the MTA "actions" can fulfill the milter requirements,
	**  but it may be overwritten by the negotiate callback.
	*/

	ctx->ctx_aflags = ctx->ctx_smfi->xxfi_flags;
	fake_pflags = SMFIP_NR_CONN
			|SMFIP_NR_HELO
			|SMFIP_NR_MAIL
			|SMFIP_NR_RCPT
			|SMFIP_NR_DATA
			|SMFIP_NR_UNKN
			|SMFIP_NR_HDR
			|SMFIP_NR_EOH
			|SMFIP_NR_BODY
			;

	if (g->a_ctx->ctx_smfi != NULL &&
	    g->a_ctx->ctx_smfi->xxfi_version > 4 &&
	    (fi_negotiate = g->a_ctx->ctx_smfi->xxfi_negotiate) != NULL)
	{
		int r;
		unsigned long m_aflags, m_pflags, m_f2, m_f3;

		/*
		**  let milter decide whether the features offered by the
		**  MTA are "good enough".
		**  Notes:
		**  - libmilter can "fake" some features (e.g., SMFIP_NR_HDR)
		**  - m_f2, m_f3 are for future extensions
		*/

		m_f2 = m_f3 = 0;
		m_aflags = ctx->ctx_mta_aflags;
		m_pflags = ctx->ctx_pflags;
		if ((SMFIP_SKIP & ctx->ctx_mta_pflags) != 0)
			m_pflags |= SMFIP_SKIP;
		r = fi_negotiate(g->a_ctx,
				ctx->ctx_mta_aflags,
				ctx->ctx_mta_pflags|fake_pflags,
				0, 0,
				&m_aflags, &m_pflags, &m_f2, &m_f3);

#if _FFR_MILTER_CHECK
		testmode = bitset(SMFIP_TEST, m_pflags);
		if (testmode)
			m_pflags &= ~SMFIP_TEST;
#endif /* _FFR_MILTER_CHECK */

		/*
		**  Types of protocol flags (pflags):
		**  1. do NOT send protocol step X
		**  2. MTA can do/understand something extra (SKIP,
		**	send unknown RCPTs)
		**  3. MTA can deal with "no reply" for various protocol steps
		**  Note: this mean that it isn't possible to simply set all
		**	flags to get "everything":
		**	setting a flag of type 1 turns off a step
		**		(it should be the other way around:
		**		a flag means a protocol step can be sent)
		**	setting a flag of type 3 requires that milter
		**	never sends a reply for the corresponding step.
		**  Summary: the "negation" of protocol flags is causing
		**	problems, but at least for type 3 there is no simple
		**	solution.
		**
		**  What should "all options" mean?
		**  send all protocol steps _except_ those for which there is
		**	no callback (currently registered in ctx_pflags)
		**  expect SKIP as return code?		Yes
		**  send unknown RCPTs?			No,
		**				must be explicitly requested?
		**  "no reply" for some protocol steps?	No,
		**				must be explicitly requested.
		*/

		if (SMFIS_ALL_OPTS == r)
		{
			ctx->ctx_aflags = ctx->ctx_mta_aflags;
			ctx->ctx_pflags2mta = ctx->ctx_pflags;
			if ((SMFIP_SKIP & ctx->ctx_mta_pflags) != 0)
				ctx->ctx_pflags2mta |= SMFIP_SKIP;
		}
		else if (r != SMFIS_CONTINUE)
		{
			smi_log(SMI_LOG_ERR,
				"%s: st_optionneg[%ld]: xxfi_negotiate returned %d (protocol options=0x%lx, actions=0x%lx)",
				ctx->ctx_smfi->xxfi_name,
				(long) ctx->ctx_id, r, ctx->ctx_mta_pflags,
				ctx->ctx_mta_aflags);
			return _SMFIS_ABORT;
		}
		else
		{
			ctx->ctx_aflags = m_aflags;
			ctx->ctx_pflags = m_pflags;
			ctx->ctx_pflags2mta = m_pflags;
		}

		/* check whether some flags need to be "faked" */
		i = ctx->ctx_pflags2mta;
		if ((ctx->ctx_mta_pflags & i) != i)
		{
			unsigned int idx;
			unsigned long b;

			/*
			**  If some behavior can be faked (set in fake_pflags),
			**  but the MTA doesn't support it, then unset
			**  that flag in the value that is sent to the MTA.
			*/

			for (idx = 0; idx < 32; idx++)
			{
				b = 1 << idx;
				if ((ctx->ctx_mta_pflags & b) != b &&
				    (fake_pflags & b) == b)
					ctx->ctx_pflags2mta &= ~b;
			}
		}
	}
	else
	{
		/*
		**  Set the protocol flags based on the values determined
		**  in mi_listener() which checked the defined callbacks.
		*/

		ctx->ctx_pflags2mta = ctx->ctx_pflags;
	}

	/* check whether actions and protocol requirements can be satisfied */
	i = ctx->ctx_aflags;
	if ((i & ctx->ctx_mta_aflags) != i)
	{
		smi_log(SMI_LOG_ERR,
			"%s: st_optionneg[%ld]: 0x%lx does not fulfill action requirements 0x%x",
			ctx->ctx_smfi->xxfi_name,
			(long) ctx->ctx_id, ctx->ctx_mta_aflags, i);
		return _SMFIS_ABORT;
	}

	i = ctx->ctx_pflags2mta;
	if ((ctx->ctx_mta_pflags & i) != i)
	{
		/*
		**  Older MTAs do not support some protocol steps.
		**  As this protocol is a bit "wierd" (it asks for steps
		**  NOT to be taken/sent) we have to check whether we
		**  should turn off those "negative" requests.
		**  Currently these are only SMFIP_NODATA and SMFIP_NOUNKNOWN.
		*/

		if (bitset(SMFIP_NODATA, ctx->ctx_pflags2mta) &&
		    !bitset(SMFIP_NODATA, ctx->ctx_mta_pflags))
			ctx->ctx_pflags2mta &= ~SMFIP_NODATA;
		if (bitset(SMFIP_NOUNKNOWN, ctx->ctx_pflags2mta) &&
		    !bitset(SMFIP_NOUNKNOWN, ctx->ctx_mta_pflags))
			ctx->ctx_pflags2mta &= ~SMFIP_NOUNKNOWN;
		i = ctx->ctx_pflags2mta;
	}

	if ((ctx->ctx_mta_pflags & i) != i)
	{
		smi_log(SMI_LOG_ERR,
			"%s: st_optionneg[%ld]: 0x%lx does not fulfill protocol requirements 0x%x",
			ctx->ctx_smfi->xxfi_name,
			(long) ctx->ctx_id, ctx->ctx_mta_pflags, i);
		return _SMFIS_ABORT;
	}
	fix_stm(ctx);

	if (ctx->ctx_dbg > 3)
		sm_dprintf("[%lu] milter_negotiate:"
			" mta_actions=0x%lx, mta_flags=0x%lx"
			" actions=0x%lx, flags=0x%lx\n"
			, (long) ctx->ctx_id
			, ctx->ctx_mta_aflags, ctx->ctx_mta_pflags
			, ctx->ctx_aflags, ctx->ctx_pflags);

#if _FFR_MILTER_CHECK
	if (ctx->ctx_dbg > 3)
		sm_dprintf("[%lu] milter_negotiate:"
			" testmode=%d, pflags2mta=%X, internal_pflags=%X\n"
			, (long) ctx->ctx_id, testmode
			, ctx->ctx_pflags2mta, internal_pflags);

	/* in test mode: take flags without further modifications */
	if (!testmode)
		/* Warning: check statement below! */
#endif /* _FFR_MILTER_CHECK */

	/*
	**  Remove the internal flags that might have been set by a milter
	**  and set only those determined above.
	*/

	ctx->ctx_pflags2mta = (ctx->ctx_pflags2mta & ~SMFI_INTERNAL)
			      | internal_pflags;
	return _SMFIS_OPTIONS;
}

/*
**  ST_CONNECTINFO -- receive connection information
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_connectinfo(g)
	genarg *g;
{
	size_t l;
	size_t i;
	char *s, family;
	unsigned short port = 0;
	_SOCK_ADDR sockaddr;
	sfsistat (*fi_connect) __P((SMFICTX *, char *, _SOCK_ADDR *));

	if (g == NULL)
		return _SMFIS_ABORT;
	mi_clr_macros(g->a_ctx, g->a_idx + 1);
	if (g->a_ctx->ctx_smfi == NULL ||
	    (fi_connect = g->a_ctx->ctx_smfi->xxfi_connect) == NULL)
		return SMFIS_CONTINUE;

	s = g->a_buf;
	i = 0;
	l = g->a_len;
	while (s[i] != '\0' && i <= l)
		++i;
	if (i + 1 >= l)
		return _SMFIS_ABORT;

	/* Move past trailing \0 in host string */
	i++;
	family = s[i++];
	(void) memset(&sockaddr, '\0', sizeof sockaddr);
	if (family != SMFIA_UNKNOWN)
	{
		if (i + sizeof port >= l)
		{
			smi_log(SMI_LOG_ERR,
				"%s: connect[%ld]: wrong len %d >= %d",
				g->a_ctx->ctx_smfi->xxfi_name,
				(long) g->a_ctx->ctx_id, (int) i, (int) l);
			return _SMFIS_ABORT;
		}
		(void) memcpy((void *) &port, (void *) (s + i),
			      sizeof port);
		i += sizeof port;

		/* make sure string is terminated */
		if (s[l - 1] != '\0')
			return _SMFIS_ABORT;
# if NETINET
		if (family == SMFIA_INET)
		{
			if (inet_aton(s + i, (struct in_addr *) &sockaddr.sin.sin_addr)
			    != 1)
			{
				smi_log(SMI_LOG_ERR,
					"%s: connect[%ld]: inet_aton failed",
					g->a_ctx->ctx_smfi->xxfi_name,
					(long) g->a_ctx->ctx_id);
				return _SMFIS_ABORT;
			}
			sockaddr.sa.sa_family = AF_INET;
			if (port > 0)
				sockaddr.sin.sin_port = port;
		}
		else
# endif /* NETINET */
# if NETINET6
		if (family == SMFIA_INET6)
		{
			if (mi_inet_pton(AF_INET6, s + i,
					 &sockaddr.sin6.sin6_addr) != 1)
			{
				smi_log(SMI_LOG_ERR,
					"%s: connect[%ld]: mi_inet_pton failed",
					g->a_ctx->ctx_smfi->xxfi_name,
					(long) g->a_ctx->ctx_id);
				return _SMFIS_ABORT;
			}
			sockaddr.sa.sa_family = AF_INET6;
			if (port > 0)
				sockaddr.sin6.sin6_port = port;
		}
		else
# endif /* NETINET6 */
# if NETUNIX
		if (family == SMFIA_UNIX)
		{
			if (sm_strlcpy(sockaddr.sunix.sun_path, s + i,
			    sizeof sockaddr.sunix.sun_path) >=
			    sizeof sockaddr.sunix.sun_path)
			{
				smi_log(SMI_LOG_ERR,
					"%s: connect[%ld]: path too long",
					g->a_ctx->ctx_smfi->xxfi_name,
					(long) g->a_ctx->ctx_id);
				return _SMFIS_ABORT;
			}
			sockaddr.sunix.sun_family = AF_UNIX;
		}
		else
# endif /* NETUNIX */
		{
			smi_log(SMI_LOG_ERR,
				"%s: connect[%ld]: unknown family %d",
				g->a_ctx->ctx_smfi->xxfi_name,
				(long) g->a_ctx->ctx_id, family);
			return _SMFIS_ABORT;
		}
	}
	return (*fi_connect)(g->a_ctx, g->a_buf,
			     family != SMFIA_UNKNOWN ? &sockaddr : NULL);
}

/*
**  ST_EOH -- end of headers
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_eoh(g)
	genarg *g;
{
	sfsistat (*fi_eoh) __P((SMFICTX *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi != NULL &&
	    (fi_eoh = g->a_ctx->ctx_smfi->xxfi_eoh) != NULL)
		return (*fi_eoh)(g->a_ctx);
	return SMFIS_CONTINUE;
}

/*
**  ST_DATA -- DATA command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_data(g)
	genarg *g;
{
	sfsistat (*fi_data) __P((SMFICTX *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi != NULL &&
	    g->a_ctx->ctx_smfi->xxfi_version > 3 &&
	    (fi_data = g->a_ctx->ctx_smfi->xxfi_data) != NULL)
		return (*fi_data)(g->a_ctx);
	return SMFIS_CONTINUE;
}

/*
**  ST_HELO -- helo/ehlo command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_helo(g)
	genarg *g;
{
	sfsistat (*fi_helo) __P((SMFICTX *, char *));

	if (g == NULL)
		return _SMFIS_ABORT;
	mi_clr_macros(g->a_ctx, g->a_idx + 1);
	if (g->a_ctx->ctx_smfi != NULL &&
	    (fi_helo = g->a_ctx->ctx_smfi->xxfi_helo) != NULL)
	{
		/* paranoia: check for terminating '\0' */
		if (g->a_len == 0 || g->a_buf[g->a_len - 1] != '\0')
			return MI_FAILURE;
		return (*fi_helo)(g->a_ctx, g->a_buf);
	}
	return SMFIS_CONTINUE;
}

/*
**  ST_HEADER -- header line
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_header(g)
	genarg *g;
{
	char *hf, *hv;
	sfsistat (*fi_header) __P((SMFICTX *, char *, char *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi == NULL ||
	    (fi_header = g->a_ctx->ctx_smfi->xxfi_header) == NULL)
		return SMFIS_CONTINUE;
	if (dec_arg2(g->a_buf, g->a_len, &hf, &hv) == MI_SUCCESS)
		return (*fi_header)(g->a_ctx, hf, hv);
	else
		return _SMFIS_ABORT;
}

#define ARGV_FCT(lf, rf, idx)					\
	char **argv;						\
	sfsistat (*lf) __P((SMFICTX *, char **));		\
	int r;							\
								\
	if (g == NULL)						\
		return _SMFIS_ABORT;				\
	mi_clr_macros(g->a_ctx, g->a_idx + 1);			\
	if (g->a_ctx->ctx_smfi == NULL ||			\
	    (lf = g->a_ctx->ctx_smfi->rf) == NULL)		\
		return SMFIS_CONTINUE;				\
	if ((argv = dec_argv(g->a_buf, g->a_len)) == NULL)	\
		return _SMFIS_ABORT;				\
	r = (*lf)(g->a_ctx, argv);				\
	free(argv);						\
	return r;

/*
**  ST_SENDER -- MAIL FROM command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_sender(g)
	genarg *g;
{
	ARGV_FCT(fi_envfrom, xxfi_envfrom, CI_MAIL)
}

/*
**  ST_RCPT -- RCPT TO command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_rcpt(g)
	genarg *g;
{
	ARGV_FCT(fi_envrcpt, xxfi_envrcpt, CI_RCPT)
}

/*
**  ST_UNKNOWN -- unrecognized or unimplemented command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_unknown(g)
	genarg *g;
{
	sfsistat (*fi_unknown) __P((SMFICTX *, const char *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi != NULL &&
	    g->a_ctx->ctx_smfi->xxfi_version > 2 &&
	    (fi_unknown = g->a_ctx->ctx_smfi->xxfi_unknown) != NULL)
		return (*fi_unknown)(g->a_ctx, (const char *) g->a_buf);
	return SMFIS_CONTINUE;
}

/*
**  ST_MACROS -- deal with macros received from the MTA
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue/keep
**
**	Side effects:
**		set pointer in macro array to current values.
*/

static int
st_macros(g)
	genarg *g;
{
	int i;
	char **argv;

	if (g == NULL || g->a_len < 1)
		return _SMFIS_FAIL;
	if ((argv = dec_argv(g->a_buf + 1, g->a_len - 1)) == NULL)
		return _SMFIS_FAIL;
	switch (g->a_buf[0])
	{
	  case SMFIC_CONNECT:
		i = CI_CONN;
		break;
	  case SMFIC_HELO:
		i = CI_HELO;
		break;
	  case SMFIC_MAIL:
		i = CI_MAIL;
		break;
	  case SMFIC_RCPT:
		i = CI_RCPT;
		break;
	  case SMFIC_DATA:
		i = CI_DATA;
		break;
	  case SMFIC_BODYEOB:
		i = CI_EOM;
		break;
	  case SMFIC_EOH:
		i = CI_EOH;
		break;
	  default:
		free(argv);
		return _SMFIS_FAIL;
	}
	if (g->a_ctx->ctx_mac_ptr[i] != NULL)
		free(g->a_ctx->ctx_mac_ptr[i]);
	if (g->a_ctx->ctx_mac_buf[i] != NULL)
		free(g->a_ctx->ctx_mac_buf[i]);
	g->a_ctx->ctx_mac_ptr[i] = argv;
	g->a_ctx->ctx_mac_buf[i] = g->a_buf;
	return _SMFIS_KEEP;
}

/*
**  ST_QUIT -- quit command
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		noreply
*/

/* ARGSUSED */
static int
st_quit(g)
	genarg *g;
{
	sfsistat (*fi_close) __P((SMFICTX *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi != NULL &&
	    (fi_close = g->a_ctx->ctx_smfi->xxfi_close) != NULL)
		(void) (*fi_close)(g->a_ctx);
	mi_clr_macros(g->a_ctx, 0);
	return _SMFIS_NOREPLY;
}

/*
**  ST_BODYCHUNK -- deal with a piece of the mail body
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
*/

static int
st_bodychunk(g)
	genarg *g;
{
	sfsistat (*fi_body) __P((SMFICTX *, unsigned char *, size_t));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g->a_ctx->ctx_smfi != NULL &&
	    (fi_body = g->a_ctx->ctx_smfi->xxfi_body) != NULL)
		return (*fi_body)(g->a_ctx, (unsigned char *)g->a_buf,
				  g->a_len);
	return SMFIS_CONTINUE;
}

/*
**  ST_BODYEND -- deal with the last piece of the mail body
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		continue or filter-specified value
**
**	Side effects:
**		sends a reply for the body part (if non-empty).
*/

static int
st_bodyend(g)
	genarg *g;
{
	sfsistat r;
	sfsistat (*fi_body) __P((SMFICTX *, unsigned char *, size_t));
	sfsistat (*fi_eom) __P((SMFICTX *));

	if (g == NULL)
		return _SMFIS_ABORT;
	r = SMFIS_CONTINUE;
	if (g->a_ctx->ctx_smfi != NULL)
	{
		if ((fi_body = g->a_ctx->ctx_smfi->xxfi_body) != NULL &&
		    g->a_len > 0)
		{
			socket_t sd;
			struct timeval timeout;

			timeout.tv_sec = g->a_ctx->ctx_timeout;
			timeout.tv_usec = 0;
			sd = g->a_ctx->ctx_sd;
			r = (*fi_body)(g->a_ctx, (unsigned char *)g->a_buf,
				       g->a_len);
			if (r != SMFIS_CONTINUE &&
			    sendreply(r, sd, &timeout, g->a_ctx) != MI_SUCCESS)
				return _SMFIS_ABORT;
		}
	}
	if (r == SMFIS_CONTINUE &&
	    (fi_eom = g->a_ctx->ctx_smfi->xxfi_eom) != NULL)
		return (*fi_eom)(g->a_ctx);
	return r;
}

/*
**  ST_ABORTFCT -- deal with aborts
**
**	Parameters:
**		g -- generic argument structure
**
**	Returns:
**		abort or filter-specified value
*/

static int
st_abortfct(g)
	genarg *g;
{
	sfsistat (*fi_abort) __P((SMFICTX *));

	if (g == NULL)
		return _SMFIS_ABORT;
	if (g != NULL && g->a_ctx->ctx_smfi != NULL &&
	    (fi_abort = g->a_ctx->ctx_smfi->xxfi_abort) != NULL)
		(void) (*fi_abort)(g->a_ctx);
	return _SMFIS_NOREPLY;
}

/*
**  TRANS_OK -- is the state transition ok?
**
**	Parameters:
**		old -- old state
**		new -- new state
**
**	Returns:
**		state transition ok
*/

static bool
trans_ok(old, new)
	int old, new;
{
	int s, n;

	s = old;
	if (s >= SIZE_NEXT_STATES)
		return false;
	do
	{
		/* is this state transition allowed? */
		if ((MI_MASK(new) & next_states[s]) != 0)
			return true;

		/*
		**  no: try next state;
		**  this works since the relevant states are ordered
		**  strict sequentially
		*/

		n = s + 1;
		if (n >= SIZE_NEXT_STATES)
			return false;

		/*
		**  can we actually "skip" this state?
		**  see fix_stm() which sets this bit for those
		**  states which the filter program is not interested in
		*/

		if (bitset(NX_SKIP, next_states[n]))
			s = n;
		else
			return false;
	} while (s < SIZE_NEXT_STATES);
	return false;
}

/*
**  FIX_STM -- add "skip" bits to the state transition table
**
**	Parameters:
**		ctx -- context structure
**
**	Returns:
**		None.
**
**	Side effects:
**		may change state transition table.
*/

static void
fix_stm(ctx)
	SMFICTX_PTR ctx;
{
	unsigned long fl;

	if (ctx == NULL || ctx->ctx_smfi == NULL)
		return;
	fl = ctx->ctx_pflags;
	if (bitset(SMFIP_NOCONNECT, fl))
		next_states[ST_CONN] |= NX_SKIP;
	if (bitset(SMFIP_NOHELO, fl))
		next_states[ST_HELO] |= NX_SKIP;
	if (bitset(SMFIP_NOMAIL, fl))
		next_states[ST_MAIL] |= NX_SKIP;
	if (bitset(SMFIP_NORCPT, fl))
		next_states[ST_RCPT] |= NX_SKIP;
	if (bitset(SMFIP_NOHDRS, fl))
		next_states[ST_HDRS] |= NX_SKIP;
	if (bitset(SMFIP_NOEOH, fl))
		next_states[ST_EOHS] |= NX_SKIP;
	if (bitset(SMFIP_NOBODY, fl))
		next_states[ST_BODY] |= NX_SKIP;
	if (bitset(SMFIP_NODATA, fl))
		next_states[ST_DATA] |= NX_SKIP;
	if (bitset(SMFIP_NOUNKNOWN, fl))
		next_states[ST_UNKN] |= NX_SKIP;
}

/*
**  DEC_ARGV -- split a buffer into a list of strings, NULL terminated
**
**	Parameters:
**		buf -- buffer with several strings
**		len -- length of buffer
**
**	Returns:
**		array of pointers to the individual strings
*/

static char **
dec_argv(buf, len)
	char *buf;
	size_t len;
{
	char **s;
	size_t i;
	int elem, nelem;

	nelem = 0;
	for (i = 0; i < len; i++)
	{
		if (buf[i] == '\0')
			++nelem;
	}
	if (nelem == 0)
		return NULL;

	/* last entry is only for the name */
	s = (char **)malloc((nelem + 1) * (sizeof *s));
	if (s == NULL)
		return NULL;
	s[0] = buf;
	for (i = 0, elem = 0; i < len && elem < nelem; i++)
	{
		if (buf[i] == '\0')
		{
			++elem;
			if (i + 1 >= len)
				s[elem] = NULL;
			else
				s[elem] = &(buf[i + 1]);
		}
	}

	/* overwrite last entry (already done above, just paranoia) */
	s[elem] = NULL;
	return s;
}

/*
**  DEC_ARG2 -- split a buffer into two strings
**
**	Parameters:
**		buf -- buffer with two strings
**		len -- length of buffer
**		s1,s2 -- pointer to result strings
**
**	Returns:
**		MI_FAILURE/MI_SUCCESS
*/

static int
dec_arg2(buf, len, s1, s2)
	char *buf;
	size_t len;
	char **s1;
	char **s2;
{
	size_t i;

	/* paranoia: check for terminating '\0' */
	if (len == 0 || buf[len - 1] != '\0')
		return MI_FAILURE;
	*s1 = buf;
	for (i = 1; i < len && buf[i] != '\0'; i++)
		continue;
	if (i >= len - 1)
		return MI_FAILURE;
	*s2 = buf + i + 1;
	return MI_SUCCESS;
}

/*
**  MI_SENDOK -- is it ok for the filter to send stuff to the MTA?
**
**	Parameters:
**		ctx -- context structure
**		flag -- flag to check
**
**	Returns:
**		sending allowed (in current state)
*/

bool
mi_sendok(ctx, flag)
	SMFICTX_PTR ctx;
	int flag;
{
	if (ctx == NULL || ctx->ctx_smfi == NULL)
		return false;

	/* did the milter request this operation? */
	if (flag != 0 && !bitset(flag, ctx->ctx_aflags))
		return false;

	/* are we in the correct state? It must be "End of Message". */
	return ctx->ctx_state == ST_ENDM;
}

#if _FFR_WORKERS_POOL
/*
**  MI_RD_SOCKET_READY - checks if the socket is ready for read(2)
**
**	Parameters:
**		sd -- socket_t
**
**	Returns:
**		true iff socket is ready for read(2)
*/

#define MI_RD_CMD_TO  1
#define MI_RD_MAX_ERR 16

static bool
mi_rd_socket_ready (sd)
	socket_t sd;
{
	int n;
	int nerr = 0;
#if SM_CONF_POLL
	struct pollfd pfd;
#else /* SM_CONF_POLL */
	fd_set	rd_set, exc_set;
#endif /* SM_CONF_POLL */

	do
	{
#if SM_CONF_POLL
		pfd.fd = sd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		n = poll(&pfd, 1, MI_RD_CMD_TO);
#else /* SM_CONF_POLL */
		struct timeval timeout;

		FD_ZERO(&rd_set);
		FD_ZERO(&exc_set);
		FD_SET(sd, &rd_set);
		FD_SET(sd, &exc_set);

		timeout.tv_sec = MI_RD_CMD_TO / 1000;
		timeout.tv_usec = 0;
		n = select(sd + 1, &rd_set, NULL, &exc_set, &timeout);
#endif /* SM_CONF_POLL */

		if (n < 0)
		{
			if (errno == EINTR)
			{
				nerr++;
				continue;
			}
			return true;
		}

		if (n == 0)
			return false;
		break;
	} while (nerr < MI_RD_MAX_ERR);
	if (nerr >= MI_RD_MAX_ERR)
		return false;

#if SM_CONF_POLL
	return (pfd.revents != 0);
#else /* SM_CONF_POLL */
	return FD_ISSET(sd, &rd_set) || FD_ISSET(sd, &exc_set);
#endif /* SM_CONF_POLL */
}
#endif /* _FFR_WORKERS_POOL */
