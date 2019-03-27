/*
 * Copyright (c) 1998-2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: parseaddr.c,v 8.407 2013-11-22 20:51:56 ca Exp $")

#include <sm/sendmail.h>
#include "map.h"

static void	allocaddr __P((ADDRESS *, int, char *, ENVELOPE *));
static int	callsubr __P((char**, int, ENVELOPE *));
static char	*map_lookup __P((STAB *, char *, char **, int *, ENVELOPE *));
static ADDRESS	*buildaddr __P((char **, ADDRESS *, int, ENVELOPE *));
static bool	hasctrlchar __P((register char *, bool, bool));

/* replacement for illegal characters in addresses */
#define BAD_CHAR_REPLACEMENT	'?'

/*
**  PARSEADDR -- Parse an address
**
**	Parses an address and breaks it up into three parts: a
**	net to transmit the message on, the host to transmit it
**	to, and a user on that host.  These are loaded into an
**	ADDRESS header with the values squirreled away if necessary.
**	The "user" part may not be a real user; the process may
**	just reoccur on that machine.  For example, on a machine
**	with an arpanet connection, the address
**		csvax.bill@berkeley
**	will break up to a "user" of 'csvax.bill' and a host
**	of 'berkeley' -- to be transmitted over the arpanet.
**
**	Parameters:
**		addr -- the address to parse.
**		a -- a pointer to the address descriptor buffer.
**			If NULL, an address will be created.
**		flags -- describe detail for parsing.  See RF_ definitions
**			in sendmail.h.
**		delim -- the character to terminate the address, passed
**			to prescan.
**		delimptr -- if non-NULL, set to the location of the
**			delim character that was found.
**		e -- the envelope that will contain this address.
**		isrcpt -- true if the address denotes a recipient; false
**			indicates a sender.
**
**	Returns:
**		A pointer to the address descriptor header (`a' if
**			`a' is non-NULL).
**		NULL on error.
**
**	Side Effects:
**		e->e_to = addr
*/

/* following delimiters are inherent to the internal algorithms */
#define DELIMCHARS	"()<>,;\r\n"	/* default word delimiters */

ADDRESS *
parseaddr(addr, a, flags, delim, delimptr, e, isrcpt)
	char *addr;
	register ADDRESS *a;
	int flags;
	int delim;
	char **delimptr;
	register ENVELOPE *e;
	bool isrcpt;
{
	char **pvp;
	auto char *delimptrbuf;
	bool qup;
	char pvpbuf[PSBUFSIZE];

	/*
	**  Initialize and prescan address.
	*/

	e->e_to = addr;
	if (tTd(20, 1))
		sm_dprintf("\n--parseaddr(%s)\n", addr);

	if (delimptr == NULL)
		delimptr = &delimptrbuf;

	pvp = prescan(addr, delim, pvpbuf, sizeof(pvpbuf), delimptr,
			ExtTokenTab, false);
	if (pvp == NULL)
	{
		if (tTd(20, 1))
			sm_dprintf("parseaddr-->NULL\n");
		return NULL;
	}

	if (invalidaddr(addr, delim == '\0' ? NULL : *delimptr, isrcpt))
	{
		if (tTd(20, 1))
			sm_dprintf("parseaddr-->bad address\n");
		return NULL;
	}

	/*
	**  Save addr if we are going to have to.
	**
	**	We have to do this early because there is a chance that
	**	the map lookups in the rewriting rules could clobber
	**	static memory somewhere.
	*/

	if (bitset(RF_COPYPADDR, flags) && addr != NULL)
	{
		char savec = **delimptr;

		if (savec != '\0')
			**delimptr = '\0';
		e->e_to = addr = sm_rpool_strdup_x(e->e_rpool, addr);
		if (savec != '\0')
			**delimptr = savec;
	}

	/*
	**  Apply rewriting rules.
	**	Ruleset 0 does basic parsing.  It must resolve.
	*/

	qup = false;
	if (REWRITE(pvp, 3, e) == EX_TEMPFAIL)
		qup = true;
	if (REWRITE(pvp, 0, e) == EX_TEMPFAIL)
		qup = true;

	/*
	**  Build canonical address from pvp.
	*/

	a = buildaddr(pvp, a, flags, e);

	if (hasctrlchar(a->q_user, isrcpt, true))
	{
		if (tTd(20, 1))
			sm_dprintf("parseaddr-->bad q_user\n");

		/*
		**  Just mark the address as bad so DSNs work.
		**  hasctrlchar() has to make sure that the address
		**  has been sanitized, e.g., shortened.
		*/

		a->q_state = QS_BADADDR;
	}

	/*
	**  Make local copies of the host & user and then
	**  transport them out.
	*/

	allocaddr(a, flags, addr, e);
	if (QS_IS_BADADDR(a->q_state))
	{
		/* weed out bad characters in the printable address too */
		(void) hasctrlchar(a->q_paddr, isrcpt, false);
		return a;
	}

	/*
	**  Select a queue directory for recipient addresses.
	**	This is done here and in split_across_queue_groups(),
	**	but the latter applies to addresses after aliasing,
	**	and only if splitting is done.
	*/

	if ((a->q_qgrp == NOAQGRP || a->q_qgrp == ENVQGRP) &&
	    !bitset(RF_SENDERADDR|RF_HEADERADDR|RF_RM_ADDR, flags) &&
	    OpMode != MD_INITALIAS)
	{
		int r;

		/* call ruleset which should return a queue group name */
		r = rscap(RS_QUEUEGROUP, a->q_user, NULL, e, &pvp, pvpbuf,
			  sizeof(pvpbuf));
		if (r == EX_OK &&
		    pvp != NULL && pvp[0] != NULL &&
		    (pvp[0][0] & 0377) == CANONNET &&
		    pvp[1] != NULL && pvp[1][0] != '\0')
		{
			r = name2qid(pvp[1]);
			if (r == NOQGRP && LogLevel > 10)
				sm_syslog(LOG_INFO, NOQID,
					"can't find queue group name %s, selection ignored",
					pvp[1]);
			if (tTd(20, 4) && r != NOQGRP)
				sm_syslog(LOG_INFO, NOQID,
					"queue group name %s -> %d",
					pvp[1], r);
			a->q_qgrp = r == NOQGRP ? ENVQGRP : r;
		}
	}

	/*
	**  If there was a parsing failure, mark it for queueing.
	*/

	if (qup && OpMode != MD_INITALIAS)
	{
		char *msg = "Transient parse error -- message queued for future delivery";

		if (e->e_sendmode == SM_DEFER)
			msg = "Deferring message until queue run";
		if (tTd(20, 1))
			sm_dprintf("parseaddr: queueing message\n");
		message(msg);
		if (e->e_message == NULL && e->e_sendmode != SM_DEFER)
			e->e_message = sm_rpool_strdup_x(e->e_rpool, msg);
		a->q_state = QS_QUEUEUP;
		a->q_status = "4.4.3";
	}

	/*
	**  Compute return value.
	*/

	if (tTd(20, 1))
	{
		sm_dprintf("parseaddr-->");
		printaddr(sm_debug_file(), a, false);
	}

	return a;
}
/*
**  INVALIDADDR -- check for address containing characters used for macros
**
**	Parameters:
**		addr -- the address to check.
**		  note: this is the complete address (including display part)
**		delimptr -- if non-NULL: end of address to check, i.e.,
**			a pointer in the address string.
**		isrcpt -- true iff the address is for a recipient.
**
**	Returns:
**		true -- if the address has characters that are reservered
**			for macros or is too long.
**		false -- otherwise.
*/

bool
invalidaddr(addr, delimptr, isrcpt)
	register char *addr;
	char *delimptr;
	bool isrcpt;
{
	bool result = false;
	char savedelim = '\0';
	char *b = addr;
	int len = 0;

	if (delimptr != NULL)
	{
		/* delimptr points to the end of the address to test */
		savedelim = *delimptr;
		if (savedelim != '\0')	/* if that isn't '\0' already: */
			*delimptr = '\0';	/* set it */
	}
	for (; *addr != '\0'; addr++)
	{
		if (!EightBitAddrOK && (*addr & 0340) == 0200)
		{
			setstat(EX_USAGE);
			result = true;
			*addr = BAD_CHAR_REPLACEMENT;
		}
		if (++len > MAXNAME - 1)
		{
			char saved = *addr;

			*addr = '\0';
			usrerr("553 5.1.0 Address \"%s\" too long (%d bytes max)",
			       b, MAXNAME - 1);
			*addr = saved;
			result = true;
			goto delim;
		}
	}
	if (result)
	{
		if (isrcpt)
			usrerr("501 5.1.3 8-bit character in mailbox address \"%s\"",
			       b);
		else
			usrerr("501 5.1.7 8-bit character in mailbox address \"%s\"",
			       b);
	}
delim:
	if (delimptr != NULL && savedelim != '\0')
		*delimptr = savedelim;	/* restore old character at delimptr */
	return result;
}
/*
**  HASCTRLCHAR -- check for address containing meta-characters
**
**  Checks that the address contains no meta-characters, and contains
**  no "non-printable" characters unless they are quoted or escaped.
**  Quoted or escaped characters are literals.
**
**	Parameters:
**		addr -- the address to check.
**		isrcpt -- true if the address is for a recipient; false
**			indicates a from.
**		complain -- true if an error should issued if the address
**			is invalid and should be "repaired".
**
**	Returns:
**		true -- if the address has any "weird" characters or
**			non-printable characters or if a quote is unbalanced.
**		false -- otherwise.
*/

static bool
hasctrlchar(addr, isrcpt, complain)
	register char *addr;
	bool isrcpt, complain;
{
	bool quoted = false;
	int len = 0;
	char *result = NULL;
	char *b = addr;

	if (addr == NULL)
		return false;
	for (; *addr != '\0'; addr++)
	{
		if (++len > MAXNAME - 1)
		{
			if (complain)
			{
				(void) shorten_rfc822_string(b, MAXNAME - 1);
				usrerr("553 5.1.0 Address \"%s\" too long (%d bytes max)",
				       b, MAXNAME - 1);
				return true;
			}
			result = "too long";
		}
		if (!EightBitAddrOK && !quoted && (*addr < 32 || *addr == 127))
		{
			result = "non-printable character";
			*addr = BAD_CHAR_REPLACEMENT;
			continue;
		}
		if (*addr == '"')
			quoted = !quoted;
		else if (*addr == '\\')
		{
			/* XXX Generic problem: no '\0' in strings. */
			if (*++addr == '\0')
			{
				result = "trailing \\ character";
				*--addr = BAD_CHAR_REPLACEMENT;
				break;
			}
		}
		if (!EightBitAddrOK && (*addr & 0340) == 0200)
		{
			setstat(EX_USAGE);
			result = "8-bit character";
			*addr = BAD_CHAR_REPLACEMENT;
			continue;
		}
	}
	if (quoted)
		result = "unbalanced quote"; /* unbalanced quote */
	if (result != NULL && complain)
	{
		if (isrcpt)
			usrerr("501 5.1.3 Syntax error in mailbox address \"%s\" (%s)",
			       b, result);
		else
			usrerr("501 5.1.7 Syntax error in mailbox address \"%s\" (%s)",
			       b, result);
	}
	return result != NULL;
}
/*
**  ALLOCADDR -- do local allocations of address on demand.
**
**	Also lowercases the host name if requested.
**
**	Parameters:
**		a -- the address to reallocate.
**		flags -- the copy flag (see RF_ definitions in sendmail.h
**			for a description).
**		paddr -- the printname of the address.
**		e -- envelope
**
**	Returns:
**		none.
**
**	Side Effects:
**		Copies portions of a into local buffers as requested.
*/

static void
allocaddr(a, flags, paddr, e)
	register ADDRESS *a;
	int flags;
	char *paddr;
	ENVELOPE *e;
{
	if (tTd(24, 4))
		sm_dprintf("allocaddr(flags=%x, paddr=%s)\n", flags, paddr);

	a->q_paddr = paddr;

	if (a->q_user == NULL)
		a->q_user = "";
	if (a->q_host == NULL)
		a->q_host = "";

	if (bitset(RF_COPYPARSE, flags))
	{
		a->q_host = sm_rpool_strdup_x(e->e_rpool, a->q_host);
		if (a->q_user != a->q_paddr)
			a->q_user = sm_rpool_strdup_x(e->e_rpool, a->q_user);
	}

	if (a->q_paddr == NULL)
		a->q_paddr = sm_rpool_strdup_x(e->e_rpool, a->q_user);
	a->q_qgrp = NOAQGRP;
}

/*
**  PRESCAN -- Prescan name and make it canonical
**
**	Scans a name and turns it into a set of tokens.  This process
**	deletes blanks and comments (in parentheses) (if the token type
**	for left paren is SPC).
**
**	This routine knows about quoted strings and angle brackets.
**
**	There are certain subtleties to this routine.  The one that
**	comes to mind now is that backslashes on the ends of names
**	are silently stripped off; this is intentional.  The problem
**	is that some versions of sndmsg (like at LBL) set the kill
**	character to something other than @ when reading addresses;
**	so people type "csvax.eric\@berkeley" -- which screws up the
**	berknet mailer.
**
**	Parameters:
**		addr -- the name to chomp.
**		delim -- the delimiter for the address, normally
**			'\0' or ','; \0 is accepted in any case.
**			If '\t' then we are reading the .cf file.
**		pvpbuf -- place to put the saved text -- note that
**			the pointers are static.
**		pvpbsize -- size of pvpbuf.
**		delimptr -- if non-NULL, set to the location of the
**			terminating delimiter.
**		toktab -- if set, a token table to use for parsing.
**			If NULL, use the default table.
**		ignore -- if true, ignore unbalanced addresses
**
**	Returns:
**		A pointer to a vector of tokens.
**		NULL on error.
*/

/* states and character types */
#define OPR		0	/* operator */
#define ATM		1	/* atom */
#define QST		2	/* in quoted string */
#define SPC		3	/* chewing up spaces */
#define ONE		4	/* pick up one character */
#define ILL		5	/* illegal character */

#define NSTATES	6	/* number of states */
#define TYPE		017	/* mask to select state type */

/* meta bits for table */
#define M		020	/* meta character; don't pass through */
#define B		040	/* cause a break */
#define MB		M|B	/* meta-break */

static short StateTab[NSTATES][NSTATES] =
{
   /*	oldst	chtype>	OPR	ATM	QST	SPC	ONE	ILL	*/
	/*OPR*/	{	OPR|B,	ATM|B,	QST|B,	SPC|MB,	ONE|B,	ILL|MB	},
	/*ATM*/	{	OPR|B,	ATM,	QST|B,	SPC|MB,	ONE|B,	ILL|MB	},
	/*QST*/	{	QST,	QST,	OPR,	QST,	QST,	QST	},
	/*SPC*/	{	OPR,	ATM,	QST,	SPC|M,	ONE,	ILL|MB	},
	/*ONE*/	{	OPR,	OPR,	OPR,	OPR,	OPR,	ILL|MB	},
	/*ILL*/	{	OPR|B,	ATM|B,	QST|B,	SPC|MB,	ONE|B,	ILL|M	}
};

/* these all get modified with the OperatorChars */

/* token type table for external strings */
unsigned char	ExtTokenTab[256] =
{
    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,SPC,SPC,SPC,SPC,SPC,ATM,ATM,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	SPC,ATM,QST,ATM,ATM,ATM,ATM,ATM, SPC,SPC,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,

    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM
};

/* token type table for internal strings */
unsigned char	IntTokenTab[256] =
{
    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,SPC,SPC,SPC,SPC,SPC,ATM,ATM,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	SPC,ATM,QST,ATM,ATM,ATM,ATM,ATM, SPC,SPC,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,

    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	OPR,OPR,ONE,OPR,OPR,OPR,OPR,OPR, OPR,OPR,OPR,OPR,OPR,OPR,OPR,OPR,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	OPR,OPR,OPR,ONE,ONE,ONE,OPR,OPR, OPR,OPR,OPR,OPR,OPR,OPR,OPR,OPR,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ONE
};

/* token type table for MIME parsing */
unsigned char	MimeTokenTab[256] =
{
    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,SPC,SPC,SPC,SPC,SPC,ILL,ILL,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	SPC,ATM,QST,ATM,ATM,ATM,ATM,ATM, SPC,SPC,ATM,ATM,OPR,ATM,ATM,OPR,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,OPR,OPR,OPR,OPR,OPR,OPR,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	OPR,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,OPR,OPR,OPR,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,

    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ILL,ILL,ILL,ILL,ILL,ILL,ILL,ILL, ILL,ILL,ILL,ILL,ILL,ILL,ILL,ONE
};

/* token type table: don't strip comments */
unsigned char	TokTypeNoC[256] =
{
    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,SPC,SPC,SPC,SPC,SPC,ATM,ATM,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	SPC,ATM,QST,ATM,ATM,ATM,ATM,ATM, OPR,OPR,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,

    /*	nul soh stx etx eot enq ack bel  bs  ht  nl  vt  np  cr  so  si   */
	OPR,OPR,ONE,OPR,OPR,OPR,OPR,OPR, OPR,OPR,OPR,OPR,OPR,OPR,OPR,OPR,
    /*	dle dc1 dc2 dc3 dc4 nak syn etb  can em  sub esc fs  gs  rs  us   */
	OPR,OPR,OPR,ONE,ONE,ONE,OPR,OPR, OPR,OPR,OPR,OPR,OPR,OPR,OPR,OPR,
    /*  sp  !   "   #   $   %   &   '    (   )   *   +   ,   -   .   /    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	0   1   2   3   4   5   6   7    8   9   :   ;   <   =   >   ?    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	@   A   B   C   D   E   F   G    H   I   J   K   L   M   N   O    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  P   Q   R   S   T   U   V   W    X   Y   Z   [   \   ]   ^   _    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*	`   a   b   c   d   e   f   g    h   i   j   k   l   m   n   o    */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM,
    /*  p   q   r   s   t   u   v   w    x   y   z   {   |   }   ~   del  */
	ATM,ATM,ATM,ATM,ATM,ATM,ATM,ATM, ATM,ATM,ATM,ATM,ATM,ATM,ATM,ONE
};


#define NOCHAR		(-1)	/* signal nothing in lookahead token */

char **
prescan(addr, delim, pvpbuf, pvpbsize, delimptr, toktab, ignore)
	char *addr;
	int delim;
	char pvpbuf[];
	int pvpbsize;
	char **delimptr;
	unsigned char *toktab;
	bool ignore;
{
	register char *p;
	register char *q;
	register int c;
	char **avp;
	bool bslashmode;
	bool route_syntax;
	int cmntcnt;
	int anglecnt;
	char *tok;
	int state;
	int newstate;
	char *saveto = CurEnv->e_to;
	static char *av[MAXATOM + 1];
	static bool firsttime = true;

	if (firsttime)
	{
		/* initialize the token type table */
		char obuf[50];

		firsttime = false;
		if (OperatorChars == NULL)
		{
			if (ConfigLevel < 7)
				OperatorChars = macvalue('o', CurEnv);
			if (OperatorChars == NULL)
				OperatorChars = ".:@[]";
		}
		expand(OperatorChars, obuf, sizeof(obuf) - sizeof(DELIMCHARS),
		       CurEnv);
		(void) sm_strlcat(obuf, DELIMCHARS, sizeof(obuf));
		for (p = obuf; *p != '\0'; p++)
		{
			if (IntTokenTab[*p & 0xff] == ATM)
				IntTokenTab[*p & 0xff] = OPR;
			if (ExtTokenTab[*p & 0xff] == ATM)
				ExtTokenTab[*p & 0xff] = OPR;
			if (TokTypeNoC[*p & 0xff] == ATM)
				TokTypeNoC[*p & 0xff] = OPR;
		}
	}
	if (toktab == NULL)
		toktab = ExtTokenTab;

	/* make sure error messages don't have garbage on them */
	errno = 0;

	q = pvpbuf;
	bslashmode = false;
	route_syntax = false;
	cmntcnt = 0;
	anglecnt = 0;
	avp = av;
	state = ATM;
	c = NOCHAR;
	p = addr;
	CurEnv->e_to = p;
	if (tTd(22, 11))
	{
		sm_dprintf("prescan: ");
		xputs(sm_debug_file(), p);
		sm_dprintf("\n");
	}

	do
	{
		/* read a token */
		tok = q;
		for (;;)
		{
			/* store away any old lookahead character */
			if (c != NOCHAR && !bslashmode)
			{
				/* see if there is room */
				if (q >= &pvpbuf[pvpbsize - 5])
				{
	addrtoolong:
					usrerr("553 5.1.1 Address too long");
					if (strlen(addr) > MAXNAME)
						addr[MAXNAME] = '\0';
	returnnull:
					if (delimptr != NULL)
					{
						if (p > addr)
							--p;
						*delimptr = p;
					}
					CurEnv->e_to = saveto;
					return NULL;
				}

				/* squirrel it away */
#if !ALLOW_255
				if ((char) c == (char) -1 && !tTd(82, 101) &&
				    !EightBitAddrOK)
					c &= 0x7f;
#endif /* !ALLOW_255 */
				*q++ = c;
			}

			/* read a new input character */
			c = (*p++) & 0x00ff;
			if (c == '\0')
			{
				/* diagnose and patch up bad syntax */
				if (ignore)
					break;
				else if (state == QST)
				{
					usrerr("553 Unbalanced '\"'");
					c = '"';
				}
				else if (cmntcnt > 0)
				{
					usrerr("553 Unbalanced '('");
					c = ')';
				}
				else if (anglecnt > 0)
				{
					c = '>';
					usrerr("553 Unbalanced '<'");
				}
				else
					break;

				p--;
			}
			else if (c == delim && cmntcnt <= 0 && state != QST)
			{
				if (anglecnt <= 0)
					break;

				/* special case for better error management */
				if (delim == ',' && !route_syntax && !ignore)
				{
					usrerr("553 Unbalanced '<'");
					c = '>';
					p--;
				}
			}

			if (tTd(22, 101))
				sm_dprintf("c=%c, s=%d; ", c, state);

			/* chew up special characters */
			*q = '\0';
			if (bslashmode)
			{
				bslashmode = false;

				/* kludge \! for naive users */
				if (cmntcnt > 0)
				{
					c = NOCHAR;
					continue;
				}
				else if (c != '!' || state == QST)
				{
					/* see if there is room */
					if (q >= &pvpbuf[pvpbsize - 5])
						goto addrtoolong;
					*q++ = '\\';
					continue;
				}
			}

			if (c == '\\')
			{
				bslashmode = true;
			}
			else if (state == QST)
			{
				/* EMPTY */
				/* do nothing, just avoid next clauses */
			}
			else if (c == '(' && toktab['('] == SPC)
			{
				cmntcnt++;
				c = NOCHAR;
			}
			else if (c == ')' && toktab['('] == SPC)
			{
				if (cmntcnt <= 0)
				{
					if (!ignore)
					{
						usrerr("553 Unbalanced ')'");
						c = NOCHAR;
					}
				}
				else
					cmntcnt--;
			}
			else if (cmntcnt > 0)
			{
				c = NOCHAR;
			}
			else if (c == '<')
			{
				char *ptr = p;

				anglecnt++;
				while (isascii(*ptr) && isspace(*ptr))
					ptr++;
				if (*ptr == '@')
					route_syntax = true;
			}
			else if (c == '>')
			{
				if (anglecnt <= 0)
				{
					if (!ignore)
					{
						usrerr("553 Unbalanced '>'");
						c = NOCHAR;
					}
				}
				else
					anglecnt--;
				route_syntax = false;
			}
			else if (delim == ' ' && isascii(c) && isspace(c))
				c = ' ';

			if (c == NOCHAR)
				continue;

			/* see if this is end of input */
			if (c == delim && anglecnt <= 0 && state != QST)
				break;

			newstate = StateTab[state][toktab[c & 0xff]];
			if (tTd(22, 101))
				sm_dprintf("ns=%02o\n", newstate);
			state = newstate & TYPE;
			if (state == ILL)
			{
				if (isascii(c) && isprint(c))
					usrerr("553 Illegal character %c", c);
				else
					usrerr("553 Illegal character 0x%02x",
					       c & 0x0ff);
			}
			if (bitset(M, newstate))
				c = NOCHAR;
			if (bitset(B, newstate))
				break;
		}

		/* new token */
		if (tok != q)
		{
			/* see if there is room */
			if (q >= &pvpbuf[pvpbsize - 5])
				goto addrtoolong;
			*q++ = '\0';
			if (tTd(22, 36))
			{
				sm_dprintf("tok=");
				xputs(sm_debug_file(), tok);
				sm_dprintf("\n");
			}
			if (avp >= &av[MAXATOM])
			{
				usrerr("553 5.1.0 prescan: too many tokens");
				goto returnnull;
			}
			if (q - tok > MAXNAME)
			{
				usrerr("553 5.1.0 prescan: token too long");
				goto returnnull;
			}
			*avp++ = tok;
		}
	} while (c != '\0' && (c != delim || anglecnt > 0));
	*avp = NULL;
	if (delimptr != NULL)
	{
		if (p > addr)
			p--;
		*delimptr = p;
	}
	if (tTd(22, 12))
	{
		sm_dprintf("prescan==>");
		printav(sm_debug_file(), av);
	}
	CurEnv->e_to = saveto;
	if (av[0] == NULL)
	{
		if (tTd(22, 1))
			sm_dprintf("prescan: null leading token\n");
		return NULL;
	}
	return av;
}
/*
**  REWRITE -- apply rewrite rules to token vector.
**
**	This routine is an ordered production system.  Each rewrite
**	rule has a LHS (called the pattern) and a RHS (called the
**	rewrite); 'rwr' points the the current rewrite rule.
**
**	For each rewrite rule, 'avp' points the address vector we
**	are trying to match against, and 'pvp' points to the pattern.
**	If pvp points to a special match value (MATCHZANY, MATCHANY,
**	MATCHONE, MATCHCLASS, MATCHNCLASS) then the address in avp
**	matched is saved away in the match vector (pointed to by 'mvp').
**
**	When a match between avp & pvp does not match, we try to
**	back out.  If we back up over MATCHONE, MATCHCLASS, or MATCHNCLASS
**	we must also back out the match in mvp.  If we reach a
**	MATCHANY or MATCHZANY we just extend the match and start
**	over again.
**
**	When we finally match, we rewrite the address vector
**	and try over again.
**
**	Parameters:
**		pvp -- pointer to token vector.
**		ruleset -- the ruleset to use for rewriting.
**		reclevel -- recursion level (to catch loops).
**		e -- the current envelope.
**		maxatom -- maximum length of buffer (usually MAXATOM)
**
**	Returns:
**		A status code.  If EX_TEMPFAIL, higher level code should
**			attempt recovery.
**
**	Side Effects:
**		pvp is modified.
*/

struct match
{
	char	**match_first;		/* first token matched */
	char	**match_last;		/* last token matched */
	char	**match_pattern;	/* pointer to pattern */
};

int
rewrite(pvp, ruleset, reclevel, e, maxatom)
	char **pvp;
	int ruleset;
	int reclevel;
	register ENVELOPE *e;
	int maxatom;
{
	register char *ap;		/* address pointer */
	register char *rp;		/* rewrite pointer */
	register char *rulename;	/* ruleset name */
	register char *prefix;
	register char **avp;		/* address vector pointer */
	register char **rvp;		/* rewrite vector pointer */
	register struct match *mlp;	/* cur ptr into mlist */
	register struct rewrite *rwr;	/* pointer to current rewrite rule */
	int ruleno;			/* current rule number */
	int rstat = EX_OK;		/* return status */
	int loopcount;
	struct match mlist[MAXMATCH];	/* stores match on LHS */
	char *npvp[MAXATOM + 1];	/* temporary space for rebuild */
	char buf[MAXLINE];
	char name[6];

	/*
	**  mlp will not exceed mlist[] because readcf enforces
	**	the upper limit of entries when reading rulesets.
	*/

	if (ruleset < 0 || ruleset >= MAXRWSETS)
	{
		syserr("554 5.3.5 rewrite: illegal ruleset number %d", ruleset);
		return EX_CONFIG;
	}
	rulename = RuleSetNames[ruleset];
	if (rulename == NULL)
	{
		(void) sm_snprintf(name, sizeof(name), "%d", ruleset);
		rulename = name;
	}
	if (OpMode == MD_TEST)
		prefix = "";
	else
		prefix = "rewrite: ruleset ";
	if (OpMode == MD_TEST)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "%s%-16.16s   input:", prefix, rulename);
		printav(smioout, pvp);
	}
	else if (tTd(21, 1))
	{
		sm_dprintf("%s%-16.16s   input:", prefix, rulename);
		printav(sm_debug_file(), pvp);
	}
	if (reclevel++ > MaxRuleRecursion)
	{
		syserr("rewrite: excessive recursion (max %d), ruleset %s",
			MaxRuleRecursion, rulename);
		return EX_CONFIG;
	}
	if (pvp == NULL)
		return EX_USAGE;
	if (maxatom <= 0)
		return EX_USAGE;

	/*
	**  Run through the list of rewrite rules, applying
	**	any that match.
	*/

	ruleno = 1;
	loopcount = 0;
	for (rwr = RewriteRules[ruleset]; rwr != NULL; )
	{
		int status;

		/* if already canonical, quit now */
		if (pvp[0] != NULL && (pvp[0][0] & 0377) == CANONNET)
			break;

		if (tTd(21, 12))
		{
			if (tTd(21, 15))
				sm_dprintf("-----trying rule (line %d):",
				       rwr->r_line);
			else
				sm_dprintf("-----trying rule:");
			printav(sm_debug_file(), rwr->r_lhs);
		}

		/* try to match on this rule */
		mlp = mlist;
		rvp = rwr->r_lhs;
		avp = pvp;
		if (++loopcount > 100)
		{
			syserr("554 5.3.5 Infinite loop in ruleset %s, rule %d",
				rulename, ruleno);
			if (tTd(21, 1))
			{
				sm_dprintf("workspace: ");
				printav(sm_debug_file(), pvp);
			}
			break;
		}

		while ((ap = *avp) != NULL || *rvp != NULL)
		{
			rp = *rvp;
			if (tTd(21, 35))
			{
				sm_dprintf("ADVANCE rp=");
				xputs(sm_debug_file(), rp);
				sm_dprintf(", ap=");
				xputs(sm_debug_file(), ap);
				sm_dprintf("\n");
			}
			if (rp == NULL)
			{
				/* end-of-pattern before end-of-address */
				goto backup;
			}
			if (ap == NULL &&
			    (rp[0] & 0377) != MATCHZANY &&
			    (rp[0] & 0377) != MATCHZERO)
			{
				/* end-of-input with patterns left */
				goto backup;
			}

			switch (rp[0] & 0377)
			{
			  case MATCHCLASS:
				/* match any phrase in a class */
				mlp->match_pattern = rvp;
				mlp->match_first = avp;
	extendclass:
				ap = *avp;
				if (ap == NULL)
					goto backup;
				mlp->match_last = avp++;
				cataddr(mlp->match_first, mlp->match_last,
					buf, sizeof(buf), '\0', true);
				if (!wordinclass(buf, rp[1]))
				{
					if (tTd(21, 36))
					{
						sm_dprintf("EXTEND  rp=");
						xputs(sm_debug_file(), rp);
						sm_dprintf(", ap=");
						xputs(sm_debug_file(), ap);
						sm_dprintf("\n");
					}
					goto extendclass;
				}
				if (tTd(21, 36))
					sm_dprintf("CLMATCH\n");
				mlp++;
				break;

			  case MATCHNCLASS:
				/* match any token not in a class */
				if (wordinclass(ap, rp[1]))
					goto backup;

				/* FALLTHROUGH */

			  case MATCHONE:
			  case MATCHANY:
				/* match exactly one token */
				mlp->match_pattern = rvp;
				mlp->match_first = avp;
				mlp->match_last = avp++;
				mlp++;
				break;

			  case MATCHZANY:
				/* match zero or more tokens */
				mlp->match_pattern = rvp;
				mlp->match_first = avp;
				mlp->match_last = avp - 1;
				mlp++;
				break;

			  case MATCHZERO:
				/* match zero tokens */
				break;

			  case MACRODEXPAND:
				/*
				**  Match against run-time macro.
				**  This algorithm is broken for the
				**  general case (no recursive macros,
				**  improper tokenization) but should
				**  work for the usual cases.
				*/

				ap = macvalue(rp[1], e);
				mlp->match_first = avp;
				if (tTd(21, 2))
					sm_dprintf("rewrite: LHS $&{%s} => \"%s\"\n",
						macname(rp[1]),
						ap == NULL ? "(NULL)" : ap);

				if (ap == NULL)
					break;
				while (*ap != '\0')
				{
					if (*avp == NULL ||
					    sm_strncasecmp(ap, *avp,
							   strlen(*avp)) != 0)
					{
						/* no match */
						avp = mlp->match_first;
						goto backup;
					}
					ap += strlen(*avp++);
				}

				/* match */
				break;

			  default:
				/* must have exact match */
				if (sm_strcasecmp(rp, ap))
					goto backup;
				avp++;
				break;
			}

			/* successful match on this token */
			rvp++;
			continue;

	  backup:
			/* match failed -- back up */
			while (--mlp >= mlist)
			{
				rvp = mlp->match_pattern;
				rp = *rvp;
				avp = mlp->match_last + 1;
				ap = *avp;

				if (tTd(21, 36))
				{
					sm_dprintf("BACKUP  rp=");
					xputs(sm_debug_file(), rp);
					sm_dprintf(", ap=");
					xputs(sm_debug_file(), ap);
					sm_dprintf("\n");
				}

				if (ap == NULL)
				{
					/* run off the end -- back up again */
					continue;
				}

				if ((rp[0] & 0377) == MATCHANY ||
				    (rp[0] & 0377) == MATCHZANY)
				{
					/* extend binding and continue */
					mlp->match_last = avp++;
					rvp++;
					mlp++;
					break;
				}
				if ((rp[0] & 0377) == MATCHCLASS)
				{
					/* extend binding and try again */
					mlp->match_last = avp;
					goto extendclass;
				}
			}

			if (mlp < mlist)
			{
				/* total failure to match */
				break;
			}
		}

		/*
		**  See if we successfully matched
		*/

		if (mlp < mlist || *rvp != NULL)
		{
			if (tTd(21, 10))
				sm_dprintf("----- rule fails\n");
			rwr = rwr->r_next;
			ruleno++;
			loopcount = 0;
			continue;
		}

		rvp = rwr->r_rhs;
		if (tTd(21, 12))
		{
			sm_dprintf("-----rule matches:");
			printav(sm_debug_file(), rvp);
		}

		rp = *rvp;
		if (rp != NULL)
		{
			if ((rp[0] & 0377) == CANONUSER)
			{
				rvp++;
				rwr = rwr->r_next;
				ruleno++;
				loopcount = 0;
			}
			else if ((rp[0] & 0377) == CANONHOST)
			{
				rvp++;
				rwr = NULL;
			}
		}

		/* substitute */
		for (avp = npvp; *rvp != NULL; rvp++)
		{
			register struct match *m;
			register char **pp;

			rp = *rvp;
			if ((rp[0] & 0377) == MATCHREPL)
			{
				/* substitute from LHS */
				m = &mlist[rp[1] - '1'];
				if (m < mlist || m >= mlp)
				{
					syserr("554 5.3.5 rewrite: ruleset %s: replacement $%c out of bounds",
						rulename, rp[1]);
					return EX_CONFIG;
				}
				if (tTd(21, 15))
				{
					sm_dprintf("$%c:", rp[1]);
					pp = m->match_first;
					while (pp <= m->match_last)
					{
						sm_dprintf(" %p=\"", *pp);
						sm_dflush();
						sm_dprintf("%s\"", *pp++);
					}
					sm_dprintf("\n");
				}
				pp = m->match_first;
				while (pp <= m->match_last)
				{
					if (avp >= &npvp[maxatom])
						goto toolong;
					*avp++ = *pp++;
				}
			}
			else
			{
				/* some sort of replacement */
				if (avp >= &npvp[maxatom])
				{
	toolong:
					syserr("554 5.3.0 rewrite: expansion too long");
					if (LogLevel > 9)
						sm_syslog(LOG_ERR, e->e_id,
							"rewrite: expansion too long, ruleset=%s, ruleno=%d",
							rulename, ruleno);
					return EX_DATAERR;
				}
				if ((rp[0] & 0377) != MACRODEXPAND)
				{
					/* vanilla replacement from RHS */
					*avp++ = rp;
				}
				else
				{
					/* $&{x} replacement */
					char *mval = macvalue(rp[1], e);
					char **xpvp;
					size_t trsize = 0;
					static size_t pvpb1_size = 0;
					static char **pvpb1 = NULL;
					char pvpbuf[PSBUFSIZE];

					if (tTd(21, 2))
						sm_dprintf("rewrite: RHS $&{%s} => \"%s\"\n",
							macname(rp[1]),
							mval == NULL ? "(NULL)" : mval);
					if (mval == NULL || *mval == '\0')
						continue;

					/* save the remainder of the input */
					for (xpvp = pvp; *xpvp != NULL; xpvp++)
						trsize += sizeof(*xpvp);
					if (trsize > pvpb1_size)
					{
						if (pvpb1 != NULL)
							sm_free(pvpb1);
						pvpb1 = (char **)
							sm_pmalloc_x(trsize);
						pvpb1_size = trsize;
					}

					memmove((char *) pvpb1,
						(char *) pvp,
						trsize);

					/* scan the new replacement */
					xpvp = prescan(mval, '\0', pvpbuf,
						       sizeof(pvpbuf), NULL,
						       NULL, false);
					if (xpvp == NULL)
					{
						/* prescan pre-printed error */
						return EX_DATAERR;
					}

					/* insert it into the output stream */
					while (*xpvp != NULL)
					{
						if (tTd(21, 19))
							sm_dprintf(" ... %s\n",
								*xpvp);
						*avp++ = sm_rpool_strdup_x(
							e->e_rpool, *xpvp);
						if (avp >= &npvp[maxatom])
							goto toolong;
						xpvp++;
					}
					if (tTd(21, 19))
						sm_dprintf(" ... DONE\n");

					/* restore the old trailing input */
					memmove((char *) pvp,
						(char *) pvpb1,
						trsize);
				}
			}
		}
		*avp++ = NULL;

		/*
		**  Check for any hostname/keyword lookups.
		*/

		for (rvp = npvp; *rvp != NULL; rvp++)
		{
			char **hbrvp;
			char **xpvp;
			size_t trsize;
			char *replac;
			int endtoken;
			bool external;
			STAB *map;
			char *mapname;
			char **key_rvp;
			char **arg_rvp;
			char **default_rvp;
			char cbuf[MAXKEY];
			char *pvpb1[MAXATOM + 1];
			char *argvect[MAX_MAP_ARGS];
			char pvpbuf[PSBUFSIZE];
			char *nullpvp[1];

			hbrvp = rvp;
			if ((rvp[0][0] & 0377) == HOSTBEGIN)
			{
				endtoken = HOSTEND;
				mapname = "host";
			}
			else if ((rvp[0][0] & 0377) == LOOKUPBEGIN)
			{
				endtoken = LOOKUPEND;
				mapname = *++rvp;
				if (mapname == NULL)
				{
					syserr("554 5.3.0 rewrite: missing mapname");
					/* NOTREACHED */
					SM_ASSERT(0);
				}
			}
			else
				continue;

			/*
			**  Got a hostname/keyword lookup.
			**
			**	This could be optimized fairly easily.
			*/

			map = stab(mapname, ST_MAP, ST_FIND);
			if (map == NULL)
				syserr("554 5.3.0 rewrite: map %s not found",
					mapname);

			/* extract the match part */
			key_rvp = ++rvp;
			if (key_rvp == NULL)
			{
				syserr("554 5.3.0 rewrite: missing key for map %s",
					mapname);
				/* NOTREACHED */
				SM_ASSERT(0);
			}
			default_rvp = NULL;
			arg_rvp = argvect;
			xpvp = NULL;
			replac = pvpbuf;
			while (*rvp != NULL && ((rvp[0][0] & 0377) != endtoken))
			{
				int nodetype = rvp[0][0] & 0377;

				if (nodetype != CANONHOST &&
				    nodetype != CANONUSER)
				{
					rvp++;
					continue;
				}

				*rvp++ = NULL;

				if (xpvp != NULL)
				{
					cataddr(xpvp, NULL, replac,
						&pvpbuf[sizeof(pvpbuf)] - replac,
						'\0', false);
					if (arg_rvp <
					    &argvect[MAX_MAP_ARGS - 1])
						*++arg_rvp = replac;
					replac += strlen(replac) + 1;
					xpvp = NULL;
				}
				switch (nodetype)
				{
				  case CANONHOST:
					xpvp = rvp;
					break;

				  case CANONUSER:
					default_rvp = rvp;
					break;
				}
			}
			if (*rvp != NULL)
				*rvp++ = NULL;
			if (xpvp != NULL)
			{
				cataddr(xpvp, NULL, replac,
					&pvpbuf[sizeof(pvpbuf)] - replac,
					'\0', false);
				if (arg_rvp < &argvect[MAX_MAP_ARGS - 1])
					*++arg_rvp = replac;
			}
			if (arg_rvp >= &argvect[MAX_MAP_ARGS - 1])
				argvect[MAX_MAP_ARGS - 1] = NULL;
			else
				*++arg_rvp = NULL;

			/* save the remainder of the input string */
			trsize = (avp - rvp + 1) * sizeof(*rvp);
			memmove((char *) pvpb1, (char *) rvp, trsize);

			/* look it up */
			cataddr(key_rvp, NULL, cbuf, sizeof(cbuf),
				map == NULL ? '\0' : map->s_map.map_spacesub,
				true);
			argvect[0] = cbuf;
			replac = map_lookup(map, cbuf, argvect, &rstat, e);
			external = replac != NULL;

			/* if no replacement, use default */
			if (replac == NULL && default_rvp != NULL)
			{
				/* create the default */
				cataddr(default_rvp, NULL, cbuf, sizeof(cbuf),
					'\0', false);
				replac = cbuf;
			}

			if (replac == NULL)
			{
				xpvp = key_rvp;
			}
			else if (*replac == '\0')
			{
				/* null replacement */
				nullpvp[0] = NULL;
				xpvp = nullpvp;
			}
			else
			{
				/* scan the new replacement */
				xpvp = prescan(replac, '\0', pvpbuf,
					       sizeof(pvpbuf), NULL,
					       external ? NULL : IntTokenTab,
					       false);
				if (xpvp == NULL)
				{
					/* prescan already printed error */
					return EX_DATAERR;
				}
			}

			/* append it to the token list */
			for (avp = hbrvp; *xpvp != NULL; xpvp++)
			{
				*avp++ = sm_rpool_strdup_x(e->e_rpool, *xpvp);
				if (avp >= &npvp[maxatom])
					goto toolong;
			}

			/* restore the old trailing information */
			rvp = avp - 1;
			for (xpvp = pvpb1; (*avp++ = *xpvp++) != NULL; )
				if (avp >= &npvp[maxatom])
					goto toolong;
		}

		/*
		**  Check for subroutine calls.
		*/

		status = callsubr(npvp, reclevel, e);
		if (rstat == EX_OK || status == EX_TEMPFAIL)
			rstat = status;

		/* copy vector back into original space. */
		for (avp = npvp; *avp++ != NULL;)
			continue;
		memmove((char *) pvp, (char *) npvp,
		      (int) (avp - npvp) * sizeof(*avp));

		if (tTd(21, 4))
		{
			sm_dprintf("rewritten as:");
			printav(sm_debug_file(), pvp);
		}
	}

	if (OpMode == MD_TEST)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "%s%-16.16s returns:", prefix, rulename);
		printav(smioout, pvp);
	}
	else if (tTd(21, 1))
	{
		sm_dprintf("%s%-16.16s returns:", prefix, rulename);
		printav(sm_debug_file(), pvp);
	}
	return rstat;
}
/*
**  CALLSUBR -- call subroutines in rewrite vector
**
**	Parameters:
**		pvp -- pointer to token vector.
**		reclevel -- the current recursion level.
**		e -- the current envelope.
**
**	Returns:
**		The status from the subroutine call.
**
**	Side Effects:
**		pvp is modified.
*/

static int
callsubr(pvp, reclevel, e)
	char **pvp;
	int reclevel;
	ENVELOPE *e;
{
	char **avp;
	register int i;
	int subr, j;
	int nsubr;
	int status;
	int rstat = EX_OK;
#define MAX_SUBR	16
	int subrnumber[MAX_SUBR];
	int subrindex[MAX_SUBR];

	nsubr = 0;

	/*
	**  Look for subroutine calls in pvp, collect them into subr*[]
	**  We will perform the calls in the next loop, because we will
	**  call the "last" subroutine first to avoid recursive calls
	**  and too much copying.
	*/

	for (avp = pvp, j = 0; *avp != NULL; avp++, j++)
	{
		if ((avp[0][0] & 0377) == CALLSUBR && avp[1] != NULL)
		{
			stripquotes(avp[1]);
			subr = strtorwset(avp[1], NULL, ST_FIND);
			if (subr < 0)
			{
				syserr("554 5.3.5 Unknown ruleset %s", avp[1]);
				return EX_CONFIG;
			}

			/*
			**  XXX instead of doing this we could optimize
			**  the rules after reading them: just remove
			**  calls to empty rulesets
			*/

			/* subroutine is an empty ruleset?  don't call it */
			if (RewriteRules[subr] == NULL)
			{
				if (tTd(21, 3))
					sm_dprintf("-----skip subr %s (%d)\n",
						avp[1], subr);
				for (i = 2; avp[i] != NULL; i++)
					avp[i - 2] = avp[i];
				avp[i - 2] = NULL;
				continue;
			}
			if (++nsubr >= MAX_SUBR)
			{
				syserr("554 5.3.0 Too many subroutine calls (%d max)",
					MAX_SUBR);
				return EX_CONFIG;
			}
			subrnumber[nsubr] = subr;
			subrindex[nsubr] = j;
		}
	}

	/*
	**  Perform the actual subroutines calls, "last" one first, i.e.,
	**  go from the right to the left through all calls,
	**  do the rewriting in place.
	*/

	for (; nsubr > 0; nsubr--)
	{
		subr = subrnumber[nsubr];
		avp = pvp + subrindex[nsubr];

		/* remove the subroutine call and name */
		for (i = 2; avp[i] != NULL; i++)
			avp[i - 2] = avp[i];
		avp[i - 2] = NULL;

		/*
		**  Now we need to call the ruleset specified for
		**  the subroutine. We can do this in place since
		**  we call the "last" subroutine first.
		*/

		status = rewrite(avp, subr, reclevel, e,
				MAXATOM - subrindex[nsubr]);
		if (status != EX_OK && status != EX_TEMPFAIL)
			return status;
		if (rstat == EX_OK || status == EX_TEMPFAIL)
			rstat = status;
	}
	return rstat;
}
/*
**  MAP_LOOKUP -- do lookup in map
**
**	Parameters:
**		smap -- the map to use for the lookup.
**		key -- the key to look up.
**		argvect -- arguments to pass to the map lookup.
**		pstat -- a pointer to an integer in which to store the
**			status from the lookup.
**		e -- the current envelope.
**
**	Returns:
**		The result of the lookup.
**		NULL -- if there was no data for the given key.
*/

static char *
map_lookup(smap, key, argvect, pstat, e)
	STAB *smap;
	char key[];
	char **argvect;
	int *pstat;
	ENVELOPE *e;
{
	auto int status = EX_OK;
	MAP *map;
	char *replac;

	if (smap == NULL)
		return NULL;

	map = &smap->s_map;
	DYNOPENMAP(map);

	if (e->e_sendmode == SM_DEFER &&
	    bitset(MF_DEFER, map->map_mflags))
	{
		/* don't do any map lookups */
		if (tTd(60, 1))
			sm_dprintf("map_lookup(%s, %s) => DEFERRED\n",
				smap->s_name, key);
		*pstat = EX_TEMPFAIL;
		return NULL;
	}

	if (!bitset(MF_KEEPQUOTES, map->map_mflags))
		stripquotes(key);

	if (tTd(60, 1))
	{
		sm_dprintf("map_lookup(%s, ", smap->s_name);
		xputs(sm_debug_file(), key);
		if (tTd(60, 5))
		{
			int i;

			for (i = 0; argvect[i] != NULL; i++)
				sm_dprintf(", %%%d=%s", i, argvect[i]);
		}
		sm_dprintf(") => ");
	}
	replac = (*map->map_class->map_lookup)(map, key, argvect, &status);
	if (tTd(60, 1))
		sm_dprintf("%s (%d)\n",
			replac != NULL ? replac : "NOT FOUND",
			status);

	/* should recover if status == EX_TEMPFAIL */
	if (status == EX_TEMPFAIL && !bitset(MF_NODEFER, map->map_mflags))
	{
		*pstat = EX_TEMPFAIL;
		if (tTd(60, 1))
			sm_dprintf("map_lookup(%s, %s) tempfail: errno=%d\n",
				smap->s_name, key, errno);
		if (e->e_message == NULL)
		{
			char mbuf[320];

			(void) sm_snprintf(mbuf, sizeof(mbuf),
				"%.80s map: lookup (%s): deferred",
				smap->s_name,
				shortenstring(key, MAXSHORTSTR));
			e->e_message = sm_rpool_strdup_x(e->e_rpool, mbuf);
		}
	}
	if (status == EX_TEMPFAIL && map->map_tapp != NULL)
	{
		size_t i = strlen(key) + strlen(map->map_tapp) + 1;
		static char *rwbuf = NULL;
		static size_t rwbuflen = 0;

		if (i > rwbuflen)
		{
			if (rwbuf != NULL)
				sm_free(rwbuf);
			rwbuflen = i;
			rwbuf = (char *) sm_pmalloc_x(rwbuflen);
		}
		(void) sm_strlcpyn(rwbuf, rwbuflen, 2, key, map->map_tapp);
		if (tTd(60, 4))
			sm_dprintf("map_lookup tempfail: returning \"%s\"\n",
				rwbuf);
		return rwbuf;
	}
	return replac;
}
/*
**  INITERRMAILERS -- initialize error and discard mailers
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		initializes error and discard mailers.
*/

static MAILER discardmailer;
static MAILER errormailer;
static char *discardargv[] = { "DISCARD", NULL };
static char *errorargv[] = { "ERROR", NULL };

void
initerrmailers()
{
	if (discardmailer.m_name == NULL)
	{
		/* initialize the discard mailer */
		discardmailer.m_name = "*discard*";
		discardmailer.m_mailer = "DISCARD";
		discardmailer.m_argv = discardargv;
	}
	if (errormailer.m_name == NULL)
	{
		/* initialize the bogus mailer */
		errormailer.m_name = "*error*";
		errormailer.m_mailer = "ERROR";
		errormailer.m_argv = errorargv;
	}
}
/*
**  BUILDADDR -- build address from token vector.
**
**	Parameters:
**		tv -- token vector.
**		a -- pointer to address descriptor to fill.
**			If NULL, one will be allocated.
**		flags -- info regarding whether this is a sender or
**			a recipient.
**		e -- the current envelope.
**
**	Returns:
**		NULL if there was an error.
**		'a' otherwise.
**
**	Side Effects:
**		fills in 'a'
*/

static struct errcodes
{
	char	*ec_name;		/* name of error code */
	int	ec_code;		/* numeric code */
} ErrorCodes[] =
{
	{ "usage",		EX_USAGE	},
	{ "nouser",		EX_NOUSER	},
	{ "nohost",		EX_NOHOST	},
	{ "unavailable",	EX_UNAVAILABLE	},
	{ "software",		EX_SOFTWARE	},
	{ "tempfail",		EX_TEMPFAIL	},
	{ "protocol",		EX_PROTOCOL	},
	{ "config",		EX_CONFIG	},
	{ NULL,			EX_UNAVAILABLE	}
};

static ADDRESS *
buildaddr(tv, a, flags, e)
	register char **tv;
	register ADDRESS *a;
	int flags;
	register ENVELOPE *e;
{
	bool tempfail = false;
	int maxatom;
	struct mailer **mp;
	register struct mailer *m;
	register char *p;
	char *mname;
	char **hostp;
	char hbuf[MAXNAME + 1];
	static char ubuf[MAXNAME + 2];

	if (tTd(24, 5))
	{
		sm_dprintf("buildaddr, flags=%x, tv=", flags);
		printav(sm_debug_file(), tv);
	}

	maxatom = MAXATOM;
	if (a == NULL)
		a = (ADDRESS *) sm_rpool_malloc_x(e->e_rpool, sizeof(*a));
	memset((char *) a, '\0', sizeof(*a));
	hbuf[0] = '\0';

	/* set up default error return flags */
	a->q_flags |= DefaultNotify;

	/* figure out what net/mailer to use */
	if (*tv == NULL || (**tv & 0377) != CANONNET)
	{
		syserr("554 5.3.5 buildaddr: no mailer in parsed address");
badaddr:
		/*
		**  ExitStat may have been set by an earlier map open
		**  failure (to a permanent error (EX_OSERR) in syserr())
		**  so we also need to check if this particular $#error
		**  return wanted a 4XX failure.
		**
		**  XXX the real fix is probably to set ExitStat correctly,
		**  i.e., to EX_TEMPFAIL if the map open is just a temporary
		**  error.
		*/

		if (ExitStat == EX_TEMPFAIL || tempfail)
			a->q_state = QS_QUEUEUP;
		else
		{
			a->q_state = QS_BADADDR;
			a->q_mailer = &errormailer;
		}
		return a;
	}
	mname = *++tv;
	--maxatom;

	/* extract host and user portions */
	if (*++tv != NULL && (**tv & 0377) == CANONHOST)
	{
		hostp = ++tv;
		--maxatom;
	}
	else
		hostp = NULL;
	--maxatom;
	while (*tv != NULL && (**tv & 0377) != CANONUSER)
	{
		tv++;
		--maxatom;
	}
	if (*tv == NULL)
	{
		syserr("554 5.3.5 buildaddr: no user");
		goto badaddr;
	}
	if (tv == hostp)
		hostp = NULL;
	else if (hostp != NULL)
		cataddr(hostp, tv - 1, hbuf, sizeof(hbuf), '\0', false);
	cataddr(++tv, NULL, ubuf, sizeof(ubuf), ' ', false);
	--maxatom;

	/* save away the host name */
	if (sm_strcasecmp(mname, "error") == 0)
	{
		/* Set up triplet for use by -bv */
		a->q_mailer = &errormailer;
		a->q_user = sm_rpool_strdup_x(e->e_rpool, ubuf);
		/* XXX wrong place? */

		if (hostp != NULL)
		{
			register struct errcodes *ep;

			a->q_host = sm_rpool_strdup_x(e->e_rpool, hbuf);
			if (strchr(hbuf, '.') != NULL)
			{
				a->q_status = sm_rpool_strdup_x(e->e_rpool,
								hbuf);
				setstat(dsntoexitstat(hbuf));
			}
			else if (isascii(hbuf[0]) && isdigit(hbuf[0]))
			{
				setstat(atoi(hbuf));
			}
			else
			{
				for (ep = ErrorCodes; ep->ec_name != NULL; ep++)
					if (sm_strcasecmp(ep->ec_name, hbuf) == 0)
						break;
				setstat(ep->ec_code);
			}
		}
		else
		{
			a->q_host = NULL;
			setstat(EX_UNAVAILABLE);
		}
		stripquotes(ubuf);
		if (ISSMTPCODE(ubuf) && ubuf[3] == ' ')
		{
			char fmt[16];
			int off;

			if ((off = isenhsc(ubuf + 4, ' ')) > 0)
			{
				ubuf[off + 4] = '\0';
				off += 5;
			}
			else
			{
				off = 4;
				ubuf[3] = '\0';
			}
			(void) sm_strlcpyn(fmt, sizeof(fmt), 2, ubuf, " %s");
			if (off > 4)
				usrerr(fmt, ubuf + off);
			else if (isenhsc(hbuf, '\0') > 0)
				usrerrenh(hbuf, fmt, ubuf + off);
			else
				usrerr(fmt, ubuf + off);
			/* XXX ubuf[off - 1] = ' '; */
			if (ubuf[0] == '4')
				tempfail = true;
		}
		else
		{
			usrerr("553 5.3.0 %s", ubuf);
		}
		goto badaddr;
	}

	for (mp = Mailer; (m = *mp++) != NULL; )
	{
		if (sm_strcasecmp(m->m_name, mname) == 0)
			break;
	}
	if (m == NULL)
	{
		syserr("554 5.3.5 buildaddr: unknown mailer %s", mname);
		goto badaddr;
	}
	a->q_mailer = m;

	/* figure out what host (if any) */
	if (hostp == NULL)
	{
		if (!bitnset(M_LOCALMAILER, m->m_flags))
		{
			syserr("554 5.3.5 buildaddr: no host");
			goto badaddr;
		}
		a->q_host = NULL;
	}
	else
		a->q_host = sm_rpool_strdup_x(e->e_rpool, hbuf);

	/* figure out the user */
	p = ubuf;
	if (bitnset(M_CHECKUDB, m->m_flags) && *p == '@')
	{
		p++;
		tv++;
		--maxatom;
		a->q_flags |= QNOTREMOTE;
	}

	/* do special mapping for local mailer */
	if (*p == '"')
		p++;
	if (*p == '|' && bitnset(M_CHECKPROG, m->m_flags))
		a->q_mailer = m = ProgMailer;
	else if (*p == '/' && bitnset(M_CHECKFILE, m->m_flags))
		a->q_mailer = m = FileMailer;
	else if (*p == ':' && bitnset(M_CHECKINCLUDE, m->m_flags))
	{
		/* may be :include: */
		stripquotes(ubuf);
		if (sm_strncasecmp(ubuf, ":include:", 9) == 0)
		{
			/* if :include:, don't need further rewriting */
			a->q_mailer = m = InclMailer;
			a->q_user = sm_rpool_strdup_x(e->e_rpool, &ubuf[9]);
			return a;
		}
	}

	/* rewrite according recipient mailer rewriting rules */
	macdefine(&e->e_macro, A_PERM, 'h', a->q_host);

	if (ConfigLevel >= 10 ||
	    !bitset(RF_SENDERADDR|RF_HEADERADDR, flags))
	{
		/* sender addresses done later */
		(void) rewrite(tv, 2, 0, e, maxatom);
		if (m->m_re_rwset > 0)
		       (void) rewrite(tv, m->m_re_rwset, 0, e, maxatom);
	}
	(void) rewrite(tv, 4, 0, e, maxatom);

	/* save the result for the command line/RCPT argument */
	cataddr(tv, NULL, ubuf, sizeof(ubuf), '\0', true);
	a->q_user = sm_rpool_strdup_x(e->e_rpool, ubuf);

	/*
	**  Do mapping to lower case as requested by mailer
	*/

	if (a->q_host != NULL && !bitnset(M_HST_UPPER, m->m_flags))
		makelower(a->q_host);
	if (!bitnset(M_USR_UPPER, m->m_flags))
		makelower(a->q_user);

	if (tTd(24, 6))
	{
		sm_dprintf("buildaddr => ");
		printaddr(sm_debug_file(), a, false);
	}
	return a;
}

/*
**  CATADDR -- concatenate pieces of addresses (putting in <LWSP> subs)
**
**	Parameters:
**		pvp -- parameter vector to rebuild.
**		evp -- last parameter to include.  Can be NULL to
**			use entire pvp.
**		buf -- buffer to build the string into.
**		sz -- size of buf.
**		spacesub -- the space separator character;
**			'\0': SpaceSub.
**			NOSPACESEP: no separator
**		external -- convert to external form?
**			(no metacharacters; METAQUOTEs removed, see below)
**
**	Returns:
**		none.
**
**	Side Effects:
**		Destroys buf.
**
**	Notes:
**	There are two formats for strings: internal and external.
**	The external format is just an eight-bit clean string (no
**	null bytes, everything else OK).  The internal format can
**	include sendmail metacharacters.  The special character
**	METAQUOTE essentially quotes the character following, stripping
**	it of all special semantics.
**
**	The cataddr routine needs to be aware of whether it is producing
**	an internal or external form as output (it only takes internal
**	form as input).
**
**	The parseaddr routine has a similar issue on input, but that
**	is flagged on the basis of which token table is passed in.
*/

void
cataddr(pvp, evp, buf, sz, spacesub, external)
	char **pvp;
	char **evp;
	char *buf;
	register int sz;
	int spacesub;
	bool external;
{
	bool oatomtok, natomtok;
	char *p;

	oatomtok = natomtok = false;
	if (tTd(59, 14))
	{
		sm_dprintf("cataddr(%d) <==", external);
		printav(sm_debug_file(), pvp);
	}

	if (sz <= 0)
		return;

	if (spacesub == '\0')
		spacesub = SpaceSub;

	if (pvp == NULL)
	{
		*buf = '\0';
		return;
	}
	p = buf;
	sz -= 2;
	while (*pvp != NULL && sz > 0)
	{
		char *q;

		natomtok = (IntTokenTab[**pvp & 0xff] == ATM);
		if (oatomtok && natomtok && spacesub != NOSPACESEP)
		{
			*p++ = spacesub;
			if (--sz <= 0)
				break;
		}
		for (q = *pvp; *q != '\0'; )
		{
			int c;

			if (--sz <= 0)
				break;
			*p++ = c = *q++;

			/*
			**  If the current character (c) is METAQUOTE and we
			**  want the "external" form and the next character
			**  is not NUL, then overwrite METAQUOTE with that
			**  character (i.e., METAQUOTE ch is changed to
			**  ch).  p[-1] is used because p is advanced (above).
			*/

			if ((c & 0377) == METAQUOTE && external && *q != '\0')
				p[-1] = *q++;
		}
		if (sz <= 0)
			break;
		oatomtok = natomtok;
		if (pvp++ == evp)
			break;
	}

#if 0
	/*
	**  Silently truncate long strings: even though this doesn't
	**  seem like a good idea it is necessary because header checks
	**  send the whole header value to rscheck() and hence rewrite().
	**  The latter however sometimes uses a "short" buffer (e.g.,
	**  cbuf[MAXNAME + 1]) to call cataddr() which then triggers this
	**  error function.  One possible fix to the problem is to pass
	**  flags to rscheck() and rewrite() to distinguish the various
	**  calls and only trigger the error if necessary.  For now just
	**  undo the change from 8.13.0.
	*/

	if (sz <= 0)
		usrerr("cataddr: string too long");
#endif
	*p = '\0';

	if (tTd(59, 14))
		sm_dprintf("  cataddr => %s\n", str2prt(buf));
}

/*
**  SAMEADDR -- Determine if two addresses are the same
**
**	This is not just a straight comparison -- if the mailer doesn't
**	care about the host we just ignore it, etc.
**
**	Parameters:
**		a, b -- pointers to the internal forms to compare.
**
**	Returns:
**		true -- they represent the same mailbox.
**		false -- they don't.
**
**	Side Effects:
**		none.
*/

bool
sameaddr(a, b)
	register ADDRESS *a;
	register ADDRESS *b;
{
	register ADDRESS *ca, *cb;

	/* if they don't have the same mailer, forget it */
	if (a->q_mailer != b->q_mailer)
		return false;

	/*
	**  Addresses resolving to error mailer
	**  should not be considered identical
	*/

	if (a->q_mailer == &errormailer)
		return false;

	/* if the user isn't the same, we can drop out */
	if (strcmp(a->q_user, b->q_user) != 0)
		return false;

	/* do the required flags match? */
	if (!ADDR_FLAGS_MATCH(a, b))
		return false;

	/* if we have good uids for both but they differ, these are different */
	if (a->q_mailer == ProgMailer)
	{
		ca = getctladdr(a);
		cb = getctladdr(b);
		if (ca != NULL && cb != NULL &&
		    bitset(QGOODUID, ca->q_flags & cb->q_flags) &&
		    ca->q_uid != cb->q_uid)
			return false;
	}

	/* otherwise compare hosts (but be careful for NULL ptrs) */
	if (a->q_host == b->q_host)
	{
		/* probably both null pointers */
		return true;
	}
	if (a->q_host == NULL || b->q_host == NULL)
	{
		/* only one is a null pointer */
		return false;
	}
	if (strcmp(a->q_host, b->q_host) != 0)
		return false;

	return true;
}
/*
**  PRINTADDR -- print address (for debugging)
**
**	Parameters:
**		a -- the address to print
**		follow -- follow the q_next chain.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

struct qflags
{
	char		*qf_name;
	unsigned long	qf_bit;
};

/* :'a,.s;^#define \(Q[A-Z]*\)	.*;	{ "\1",	\1	},; */
static struct qflags	AddressFlags[] =
{
	{ "QGOODUID",		QGOODUID	},
	{ "QPRIMARY",		QPRIMARY	},
	{ "QNOTREMOTE",		QNOTREMOTE	},
	{ "QSELFREF",		QSELFREF	},
	{ "QBOGUSSHELL",	QBOGUSSHELL	},
	{ "QUNSAFEADDR",	QUNSAFEADDR	},
	{ "QPINGONSUCCESS",	QPINGONSUCCESS	},
	{ "QPINGONFAILURE",	QPINGONFAILURE	},
	{ "QPINGONDELAY",	QPINGONDELAY	},
	{ "QHASNOTIFY",		QHASNOTIFY	},
	{ "QRELAYED",		QRELAYED	},
	{ "QEXPANDED",		QEXPANDED	},
	{ "QDELIVERED",		QDELIVERED	},
	{ "QDELAYED",		QDELAYED	},
	{ "QTHISPASS",		QTHISPASS	},
	{ "QALIAS",		QALIAS		},
	{ "QBYTRACE",		QBYTRACE	},
	{ "QBYNDELAY",		QBYNDELAY	},
	{ "QBYNRELAY",		QBYNRELAY	},
	{ "QINTBCC",		QINTBCC		},
	{ "QDYNMAILER",		QDYNMAILER	},
	{ "QRCPTOK",		QRCPTOK		},
	{ NULL,			0		}
};

void
printaddr(fp, a, follow)
	SM_FILE_T *fp;
	register ADDRESS *a;
	bool follow;
{
	register MAILER *m;
	MAILER pseudomailer;
	register struct qflags *qfp;
	bool firstone;

	if (a == NULL)
	{
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "[NULL]\n");
		return;
	}

	while (a != NULL)
	{
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%p=", a);
		(void) sm_io_flush(fp, SM_TIME_DEFAULT);

		/* find the mailer -- carefully */
		m = a->q_mailer;
		if (m == NULL)
		{
			m = &pseudomailer;
			m->m_mno = -1;
			m->m_name = "NULL";
		}

		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "%s:\n\tmailer %d (%s), host `%s'\n",
				     a->q_paddr == NULL ? "<null>" : a->q_paddr,
				     m->m_mno, m->m_name,
				     a->q_host == NULL ? "<null>" : a->q_host);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\tuser `%s', ruser `%s'\n",
				     a->q_user,
				     a->q_ruser == NULL ? "<null>" : a->q_ruser);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "\tstate=");
		switch (a->q_state)
		{
		  case QS_OK:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "OK");
			break;

		  case QS_DONTSEND:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "DONTSEND");
			break;

		  case QS_BADADDR:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "BADADDR");
			break;

		  case QS_QUEUEUP:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "QUEUEUP");
			break;

		  case QS_RETRY:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "RETRY");
			break;

		  case QS_SENT:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "SENT");
			break;

		  case QS_VERIFIED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "VERIFIED");
			break;

		  case QS_EXPANDED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "EXPANDED");
			break;

		  case QS_SENDER:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "SENDER");
			break;

		  case QS_CLONED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "CLONED");
			break;

		  case QS_DISCARDED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "DISCARDED");
			break;

		  case QS_REPLACED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "REPLACED");
			break;

		  case QS_REMOVED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "REMOVED");
			break;

		  case QS_DUPLICATE:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "DUPLICATE");
			break;

		  case QS_INCLUDED:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "INCLUDED");
			break;

		  default:
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
					     "%d", a->q_state);
			break;
		}
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     ", next=%p, alias %p, uid %d, gid %d\n",
				     a->q_next, a->q_alias,
				     (int) a->q_uid, (int) a->q_gid);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "\tflags=%lx<",
				     a->q_flags);
		firstone = true;
		for (qfp = AddressFlags; qfp->qf_name != NULL; qfp++)
		{
			if (!bitset(qfp->qf_bit, a->q_flags))
				continue;
			if (!firstone)
				(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
						     ",");
			firstone = false;
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s",
					     qfp->qf_name);
		}
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, ">\n");
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\towner=%s, home=\"%s\", fullname=\"%s\"\n",
				     a->q_owner == NULL ? "(none)" : a->q_owner,
				     a->q_home == NULL ? "(none)" : a->q_home,
				     a->q_fullname == NULL ? "(none)" : a->q_fullname);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\torcpt=\"%s\", statmta=%s, status=%s\n",
				     a->q_orcpt == NULL ? "(none)" : a->q_orcpt,
				     a->q_statmta == NULL ? "(none)" : a->q_statmta,
				     a->q_status == NULL ? "(none)" : a->q_status);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\tfinalrcpt=\"%s\"\n",
				     a->q_finalrcpt == NULL ? "(none)" : a->q_finalrcpt);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\trstatus=\"%s\"\n",
				     a->q_rstatus == NULL ? "(none)" : a->q_rstatus);
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
				     "\tstatdate=%s\n",
				     a->q_statdate == 0 ? "(none)" : ctime(&a->q_statdate));

		if (!follow)
			return;
		a = a->q_next;
	}
}
/*
**  EMPTYADDR -- return true if this address is empty (``<>'')
**
**	Parameters:
**		a -- pointer to the address
**
**	Returns:
**		true -- if this address is "empty" (i.e., no one should
**			ever generate replies to it.
**		false -- if it is a "regular" (read: replyable) address.
*/

bool
emptyaddr(a)
	register ADDRESS *a;
{
	return a->q_paddr == NULL || strcmp(a->q_paddr, "<>") == 0 ||
	       a->q_user == NULL || strcmp(a->q_user, "<>") == 0;
}
/*
**  REMOTENAME -- return the name relative to the current mailer
**
**	Parameters:
**		name -- the name to translate.
**		m -- the mailer that we want to do rewriting relative to.
**		flags -- fine tune operations.
**		pstat -- pointer to status word.
**		e -- the current envelope.
**
**	Returns:
**		the text string representing this address relative to
**			the receiving mailer.
**
**	Side Effects:
**		none.
**
**	Warnings:
**		The text string returned is tucked away locally;
**			copy it if you intend to save it.
*/

char *
remotename(name, m, flags, pstat, e)
	char *name;
	struct mailer *m;
	int flags;
	int *pstat;
	register ENVELOPE *e;
{
	register char **pvp;
	char *SM_NONVOLATILE fancy;
	char *oldg;
	int rwset;
	static char buf[MAXNAME + 1];
	char lbuf[MAXNAME + 1];
	char pvpbuf[PSBUFSIZE];
	char addrtype[4];

	if (tTd(12, 1))
	{
		sm_dprintf("remotename(");
		xputs(sm_debug_file(), name);
		sm_dprintf(")\n");
	}

	/* don't do anything if we are tagging it as special */
	if (bitset(RF_SENDERADDR, flags))
	{
		rwset = bitset(RF_HEADERADDR, flags) ? m->m_sh_rwset
						     : m->m_se_rwset;
		addrtype[2] = 's';
	}
	else
	{
		rwset = bitset(RF_HEADERADDR, flags) ? m->m_rh_rwset
						     : m->m_re_rwset;
		addrtype[2] = 'r';
	}
	if (rwset < 0)
		return name;
	addrtype[1] = ' ';
	addrtype[3] = '\0';
	addrtype[0] = bitset(RF_HEADERADDR, flags) ? 'h' : 'e';
	macdefine(&e->e_macro, A_TEMP, macid("{addr_type}"), addrtype);

	/*
	**  Do a heuristic crack of this name to extract any comment info.
	**	This will leave the name as a comment and a $g macro.
	*/

	if (bitset(RF_CANONICAL, flags) || bitnset(M_NOCOMMENT, m->m_flags))
		fancy = "\201g";
	else
		fancy = crackaddr(name, e);

	/*
	**  Turn the name into canonical form.
	**	Normally this will be RFC 822 style, i.e., "user@domain".
	**	If this only resolves to "user", and the "C" flag is
	**	specified in the sending mailer, then the sender's
	**	domain will be appended.
	*/

	pvp = prescan(name, '\0', pvpbuf, sizeof(pvpbuf), NULL, NULL, false);
	if (pvp == NULL)
		return name;
	if (REWRITE(pvp, 3, e) == EX_TEMPFAIL)
		*pstat = EX_TEMPFAIL;
	if (bitset(RF_ADDDOMAIN, flags) && e->e_fromdomain != NULL)
	{
		/* append from domain to this address */
		register char **pxp = pvp;
		int l = MAXATOM;	/* size of buffer for pvp */

		/* see if there is an "@domain" in the current name */
		while (*pxp != NULL && strcmp(*pxp, "@") != 0)
		{
			pxp++;
			--l;
		}
		if (*pxp == NULL)
		{
			/* no.... append the "@domain" from the sender */
			register char **qxq = e->e_fromdomain;

			while ((*pxp++ = *qxq++) != NULL)
			{
				if (--l <= 0)
				{
					*--pxp = NULL;
					usrerr("553 5.1.0 remotename: too many tokens");
					*pstat = EX_UNAVAILABLE;
					break;
				}
			}
			if (REWRITE(pvp, 3, e) == EX_TEMPFAIL)
				*pstat = EX_TEMPFAIL;
		}
	}

	/*
	**  Do more specific rewriting.
	**	Rewrite using ruleset 1 or 2 depending on whether this is
	**		a sender address or not.
	**	Then run it through any receiving-mailer-specific rulesets.
	*/

	if (bitset(RF_SENDERADDR, flags))
	{
		if (REWRITE(pvp, 1, e) == EX_TEMPFAIL)
			*pstat = EX_TEMPFAIL;
	}
	else
	{
		if (REWRITE(pvp, 2, e) == EX_TEMPFAIL)
			*pstat = EX_TEMPFAIL;
	}
	if (rwset > 0)
	{
		if (REWRITE(pvp, rwset, e) == EX_TEMPFAIL)
			*pstat = EX_TEMPFAIL;
	}

	/*
	**  Do any final sanitation the address may require.
	**	This will normally be used to turn internal forms
	**	(e.g., user@host.LOCAL) into external form.  This
	**	may be used as a default to the above rules.
	*/

	if (REWRITE(pvp, 4, e) == EX_TEMPFAIL)
		*pstat = EX_TEMPFAIL;

	/*
	**  Now restore the comment information we had at the beginning.
	*/

	cataddr(pvp, NULL, lbuf, sizeof(lbuf), '\0', false);
	oldg = macget(&e->e_macro, 'g');
	macset(&e->e_macro, 'g', lbuf);

	SM_TRY
		/* need to make sure route-addrs have <angle brackets> */
		if (bitset(RF_CANONICAL, flags) && lbuf[0] == '@')
			expand("<\201g>", buf, sizeof(buf), e);
		else
			expand(fancy, buf, sizeof(buf), e);
	SM_FINALLY
		macset(&e->e_macro, 'g', oldg);
	SM_END_TRY

	if (tTd(12, 1))
	{
		sm_dprintf("remotename => `");
		xputs(sm_debug_file(), buf);
		sm_dprintf("', stat=%d\n", *pstat);
	}
	return buf;
}
/*
**  MAPLOCALUSER -- run local username through ruleset 5 for final redirection
**
**	Parameters:
**		a -- the address to map (but just the user name part).
**		sendq -- the sendq in which to install any replacement
**			addresses.
**		aliaslevel -- the alias nesting depth.
**		e -- the envelope.
**
**	Returns:
**		none.
*/

#define Q_COPYFLAGS	(QPRIMARY|QBOGUSSHELL|QUNSAFEADDR|\
			 Q_PINGFLAGS|QHASNOTIFY|\
			 QRELAYED|QEXPANDED|QDELIVERED|QDELAYED|\
			 QBYTRACE|QBYNDELAY|QBYNRELAY)

void
maplocaluser(a, sendq, aliaslevel, e)
	register ADDRESS *a;
	ADDRESS **sendq;
	int aliaslevel;
	ENVELOPE *e;
{
	register char **pvp;
	register ADDRESS *SM_NONVOLATILE a1 = NULL;
	char pvpbuf[PSBUFSIZE];

	if (tTd(29, 1))
	{
		sm_dprintf("maplocaluser: ");
		printaddr(sm_debug_file(), a, false);
	}
	pvp = prescan(a->q_user, '\0', pvpbuf, sizeof(pvpbuf), NULL, NULL,
			false);
	if (pvp == NULL)
	{
		if (tTd(29, 9))
			sm_dprintf("maplocaluser: cannot prescan %s\n",
				a->q_user);
		return;
	}

	macdefine(&e->e_macro, A_PERM, 'h', a->q_host);
	macdefine(&e->e_macro, A_PERM, 'u', a->q_user);
	macdefine(&e->e_macro, A_PERM, 'z', a->q_home);

	macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), "e r");
	if (REWRITE(pvp, 5, e) == EX_TEMPFAIL)
	{
		if (tTd(29, 9))
			sm_dprintf("maplocaluser: rewrite tempfail\n");
		a->q_state = QS_QUEUEUP;
		a->q_status = "4.4.3";
		return;
	}
	if (pvp[0] == NULL || (pvp[0][0] & 0377) != CANONNET)
	{
		if (tTd(29, 9))
			sm_dprintf("maplocaluser: doesn't resolve\n");
		return;
	}

	SM_TRY
		a1 = buildaddr(pvp, NULL, 0, e);
	SM_EXCEPT(exc, "E:mta.quickabort")

		/*
		**  mark address as bad, S5 returned an error
		**	and we gave that back to the SMTP client.
		*/

		a->q_state = QS_DONTSEND;
		sm_exc_raisenew_x(&EtypeQuickAbort, 2);
	SM_END_TRY

	/* if non-null, mailer destination specified -- has it changed? */
	if (a1 == NULL || sameaddr(a, a1))
	{
		if (tTd(29, 9))
			sm_dprintf("maplocaluser: address unchanged\n");
		return;
	}

	/* make new address take on flags and print attributes of old */
	a1->q_flags &= ~Q_COPYFLAGS;
	a1->q_flags |= a->q_flags & Q_COPYFLAGS;
	a1->q_paddr = sm_rpool_strdup_x(e->e_rpool, a->q_paddr);
	a1->q_finalrcpt = a->q_finalrcpt;
	a1->q_orcpt = a->q_orcpt;

	/* mark old address as dead; insert new address */
	a->q_state = QS_REPLACED;
	if (tTd(29, 5))
	{
		sm_dprintf("maplocaluser: QS_REPLACED ");
		printaddr(sm_debug_file(), a, false);
	}
	a1->q_alias = a;
	allocaddr(a1, RF_COPYALL, sm_rpool_strdup_x(e->e_rpool, a->q_paddr), e);
	(void) recipient(a1, sendq, aliaslevel, e);
}
/*
**  DEQUOTE_INIT -- initialize dequote map
**
**	Parameters:
**		map -- the internal map structure.
**		args -- arguments.
**
**	Returns:
**		true.
*/

bool
dequote_init(map, args)
	MAP *map;
	char *args;
{
	register char *p = args;

	/* there is no check whether there is really an argument */
	map->map_mflags |= MF_KEEPQUOTES;
	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'a':
			map->map_app = ++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		  case 'S':
		  case 's':
			map->map_spacesub = *++p;
			break;
		}
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p = '\0';
	}
	if (map->map_app != NULL)
		map->map_app = newstr(map->map_app);

	return true;
}
/*
**  DEQUOTE_MAP -- unquote an address
**
**	Parameters:
**		map -- the internal map structure (ignored).
**		name -- the name to dequote.
**		av -- arguments (ignored).
**		statp -- pointer to status out-parameter.
**
**	Returns:
**		NULL -- if there were no quotes, or if the resulting
**			unquoted buffer would not be acceptable to prescan.
**		else -- The dequoted buffer.
*/

/* ARGSUSED2 */
char *
dequote_map(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	register char *p;
	register char *q;
	register char c;
	int anglecnt = 0;
	int cmntcnt = 0;
	int quotecnt = 0;
	int spacecnt = 0;
	bool quotemode = false;
	bool bslashmode = false;
	char spacesub = map->map_spacesub;

	for (p = q = name; (c = *p++) != '\0'; )
	{
		if (bslashmode)
		{
			bslashmode = false;
			*q++ = c;
			continue;
		}

		if (c == ' ' && spacesub != '\0')
			c = spacesub;

		switch (c)
		{
		  case '\\':
			bslashmode = true;
			break;

		  case '(':
			cmntcnt++;
			break;

		  case ')':
			if (cmntcnt-- <= 0)
				return NULL;
			break;

		  case ' ':
		  case '\t':
			spacecnt++;
			break;
		}

		if (cmntcnt > 0)
		{
			*q++ = c;
			continue;
		}

		switch (c)
		{
		  case '"':
			quotemode = !quotemode;
			quotecnt++;
			continue;

		  case '<':
			anglecnt++;
			break;

		  case '>':
			if (anglecnt-- <= 0)
				return NULL;
			break;
		}
		*q++ = c;
	}

	if (anglecnt != 0 || cmntcnt != 0 || bslashmode ||
	    quotemode || quotecnt <= 0 || spacecnt != 0)
		return NULL;
	*q++ = '\0';
	return map_rewrite(map, name, strlen(name), NULL);
}
/*
**  RSCHECK -- check string(s) for validity using rewriting sets
**
**	Parameters:
**		rwset -- the rewriting set to use.
**		p1 -- the first string to check.
**		p2 -- the second string to check -- may be null.
**		e -- the current envelope.
**		flags -- control some behavior, see RSF_ in sendmail.h
**		logl -- logging level.
**		host -- NULL or relay host.
**		logid -- id for sm_syslog.
**		addr -- if not NULL and ruleset returns $#error:
**				store mailer triple here.
**		addrstr -- if not NULL and ruleset does not return $#:
**				address string
**
**	Returns:
**		EX_OK -- if the rwset doesn't resolve to $#error
**		else -- the failure status (message printed)
*/

int
rscheck(rwset, p1, p2, e, flags, logl, host, logid, addr, addrstr)
	char *rwset;
	char *p1;
	char *p2;
	ENVELOPE *e;
	int flags;
	int logl;
	char *host;
	char *logid;
	ADDRESS *addr;
	char **addrstr;
{
	char *volatile buf;
	size_t bufsize;
	int saveexitstat;
	int volatile rstat = EX_OK;
	char **pvp;
	int rsno;
	bool volatile discard = false;
	bool saveQuickAbort = QuickAbort;
	bool saveSuprErrs = SuprErrs;
	bool quarantine = false;
	char ubuf[BUFSIZ * 2];
	char buf0[MAXLINE];
	char pvpbuf[PSBUFSIZE];
	extern char MsgBuf[];

	if (tTd(48, 2))
		sm_dprintf("rscheck(%s, %s, %s)\n", rwset, p1,
			p2 == NULL ? "(NULL)" : p2);

	rsno = strtorwset(rwset, NULL, ST_FIND);
	if (rsno < 0)
		return EX_OK;

	if (p2 != NULL)
	{
		bufsize = strlen(p1) + strlen(p2) + 2;
		if (bufsize > sizeof(buf0))
			buf = sm_malloc_x(bufsize);
		else
		{
			buf = buf0;
			bufsize = sizeof(buf0);
		}
		(void) sm_snprintf(buf, bufsize, "%s%c%s", p1, CONDELSE, p2);
	}
	else
	{
		bufsize = strlen(p1) + 1;
		if (bufsize > sizeof(buf0))
			buf = sm_malloc_x(bufsize);
		else
		{
			buf = buf0;
			bufsize = sizeof(buf0);
		}
		(void) sm_strlcpy(buf, p1, bufsize);
	}
	SM_TRY
	{
		SuprErrs = true;
		QuickAbort = false;
		pvp = prescan(buf, '\0', pvpbuf, sizeof(pvpbuf), NULL,
			      bitset(RSF_RMCOMM, flags) ?
					IntTokenTab : TokTypeNoC,
			      bitset(RSF_RMCOMM, flags) ? false : true);
		SuprErrs = saveSuprErrs;
		if (pvp == NULL)
		{
			if (tTd(48, 2))
				sm_dprintf("rscheck: cannot prescan input\n");
	/*
			syserr("rscheck: cannot prescan input: \"%s\"",
				shortenstring(buf, MAXSHORTSTR));
			rstat = EX_DATAERR;
	*/
			goto finis;
		}
		if (bitset(RSF_UNSTRUCTURED, flags))
			SuprErrs = true;
		(void) REWRITE(pvp, rsno, e);
		if (bitset(RSF_UNSTRUCTURED, flags))
			SuprErrs = saveSuprErrs;

		if (pvp[0] != NULL && (pvp[0][0] & 0377) != CANONNET &&
		    bitset(RSF_ADDR, flags) && addrstr != NULL)
		{
			cataddr(&(pvp[0]), NULL, ubuf, sizeof(ubuf),
				bitset(RSF_STRING, flags) ? NOSPACESEP : ' ',
				true);
			*addrstr = sm_rpool_strdup_x(e->e_rpool, ubuf);
			goto finis;
		}

		if (pvp[0] == NULL || (pvp[0][0] & 0377) != CANONNET ||
		    pvp[1] == NULL || (strcmp(pvp[1], "error") != 0 &&
				       strcmp(pvp[1], "discard") != 0))
		{
			goto finis;
		}

		if (strcmp(pvp[1], "discard") == 0)
		{
			if (tTd(48, 2))
				sm_dprintf("rscheck: discard mailer selected\n");
			e->e_flags |= EF_DISCARD;
			discard = true;
		}
		else if (strcmp(pvp[1], "error") == 0 &&
			 pvp[2] != NULL && (pvp[2][0] & 0377) == CANONHOST &&
			 pvp[3] != NULL && strcmp(pvp[3], "quarantine") == 0)
		{
			if (pvp[4] == NULL ||
			    (pvp[4][0] & 0377) != CANONUSER ||
			    pvp[5] == NULL)
				e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool,
								 rwset);
			else
			{
				cataddr(&(pvp[5]), NULL, ubuf,
					sizeof(ubuf), ' ', true);
				e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool,
								 ubuf);
			}
			macdefine(&e->e_macro, A_PERM,
				  macid("{quarantine}"), e->e_quarmsg);
			quarantine = true;
		}
		else
		{
			auto ADDRESS a1;
			int savelogusrerrs = LogUsrErrs;
			static bool logged = false;

			/* got an error -- process it */
			saveexitstat = ExitStat;
			LogUsrErrs = false;
			(void) buildaddr(pvp, &a1, 0, e);
			if (addr != NULL)
			{
				addr->q_mailer = a1.q_mailer;
				addr->q_user = a1.q_user;
				addr->q_host = a1.q_host;
			}
			LogUsrErrs = savelogusrerrs;
			rstat = ExitStat;
			ExitStat = saveexitstat;
			if (!logged)
			{
				if (bitset(RSF_COUNT, flags))
					markstats(e, &a1, STATS_REJECT);
				logged = true;
			}
		}

		if (LogLevel > logl)
		{
			char *relay;
			char *p;
			char lbuf[MAXLINE];

			p = lbuf;
			if (p2 != NULL)
			{
				(void) sm_snprintf(p, SPACELEFT(lbuf, p),
					", arg2=%s",
					p2);
				p += strlen(p);
			}

			if (host != NULL)
				relay = host;
			else
				relay = macvalue('_', e);
			if (relay != NULL)
			{
				(void) sm_snprintf(p, SPACELEFT(lbuf, p),
					", relay=%s", relay);
				p += strlen(p);
			}
			*p = '\0';
			if (discard)
				sm_syslog(LOG_NOTICE, logid,
					  "ruleset=%s, arg1=%s%s, discard",
					  rwset, p1, lbuf);
			else if (quarantine)
				sm_syslog(LOG_NOTICE, logid,
					  "ruleset=%s, arg1=%s%s, quarantine=%s",
					  rwset, p1, lbuf, ubuf);
			else
				sm_syslog(LOG_NOTICE, logid,
					  "ruleset=%s, arg1=%s%s, reject=%s",
					  rwset, p1, lbuf, MsgBuf);
		}

	 finis: ;
	}
	SM_FINALLY
	{
		/* clean up */
		if (buf != buf0)
			sm_free(buf);
		QuickAbort = saveQuickAbort;
	}
	SM_END_TRY

	setstat(rstat);

	/* rulesets don't set errno */
	errno = 0;
	if (rstat != EX_OK && QuickAbort)
		sm_exc_raisenew_x(&EtypeQuickAbort, 2);
	return rstat;
}
/*
**  RSCAP -- call rewriting set to return capabilities
**
**	Parameters:
**		rwset -- the rewriting set to use.
**		p1 -- the first string to check.
**		p2 -- the second string to check -- may be null.
**		e -- the current envelope.
**		pvp -- pointer to token vector.
**		pvpbuf -- buffer space.
**		size -- size of buffer space.
**
**	Returns:
**		EX_UNAVAILABLE -- ruleset doesn't exist.
**		EX_DATAERR -- prescan() failed.
**		EX_OK -- rewrite() was successful.
**		else -- return status from rewrite().
*/

int
rscap(rwset, p1, p2, e, pvp, pvpbuf, size)
	char *rwset;
	char *p1;
	char *p2;
	ENVELOPE *e;
	char ***pvp;
	char *pvpbuf;
	int size;
{
	char *volatile buf;
	size_t bufsize;
	int volatile rstat = EX_OK;
	int rsno;
	bool saveQuickAbort = QuickAbort;
	bool saveSuprErrs = SuprErrs;
	char buf0[MAXLINE];
	extern char MsgBuf[];

	if (tTd(48, 2))
		sm_dprintf("rscap(%s, %s, %s)\n", rwset, p1,
			p2 == NULL ? "(NULL)" : p2);

	SM_REQUIRE(pvp != NULL);
	rsno = strtorwset(rwset, NULL, ST_FIND);
	if (rsno < 0)
		return EX_UNAVAILABLE;

	if (p2 != NULL)
	{
		bufsize = strlen(p1) + strlen(p2) + 2;
		if (bufsize > sizeof(buf0))
			buf = sm_malloc_x(bufsize);
		else
		{
			buf = buf0;
			bufsize = sizeof(buf0);
		}
		(void) sm_snprintf(buf, bufsize, "%s%c%s", p1, CONDELSE, p2);
	}
	else
	{
		bufsize = strlen(p1) + 1;
		if (bufsize > sizeof(buf0))
			buf = sm_malloc_x(bufsize);
		else
		{
			buf = buf0;
			bufsize = sizeof(buf0);
		}
		(void) sm_strlcpy(buf, p1, bufsize);
	}
	SM_TRY
	{
		SuprErrs = true;
		QuickAbort = false;
		*pvp = prescan(buf, '\0', pvpbuf, size, NULL, IntTokenTab,
				false);
		if (*pvp != NULL)
			rstat = rewrite(*pvp, rsno, 0, e, size);
		else
		{
			if (tTd(48, 2))
				sm_dprintf("rscap: cannot prescan input\n");
			rstat = EX_DATAERR;
		}
	}
	SM_FINALLY
	{
		/* clean up */
		if (buf != buf0)
			sm_free(buf);
		SuprErrs = saveSuprErrs;
		QuickAbort = saveQuickAbort;

		/* prevent information leak, this may contain rewrite error */
		MsgBuf[0] = '\0';
	}
	SM_END_TRY
	return rstat;
}
