/*
 * Copyright (c) 1998-2003, 2006, 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1994, 1996-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>
#include <string.h>

SM_RCSID("@(#)$Id: mime.c,v 8.149 2013-11-22 20:51:56 ca Exp $")

/*
**  MIME support.
**
**	I am indebted to John Beck of Hewlett-Packard, who contributed
**	his code to me for inclusion.  As it turns out, I did not use
**	his code since he used a "minimum change" approach that used
**	several temp files, and I wanted a "minimum impact" approach
**	that would avoid copying.  However, looking over his code
**	helped me cement my understanding of the problem.
**
**	I also looked at, but did not directly use, Nathaniel
**	Borenstein's "code.c" module.  Again, it functioned as
**	a file-to-file translator, which did not fit within my
**	design bounds, but it was a useful base for understanding
**	the problem.
*/

/* use "old" mime 7 to 8 algorithm by default */
#ifndef MIME7TO8_OLD
# define MIME7TO8_OLD	1
#endif /* ! MIME7TO8_OLD */

#if MIME8TO7
static int	isboundary __P((char *, char **));
static int	mimeboundary __P((char *, char **));
static int	mime_getchar __P((SM_FILE_T *, char **, int *));
static int	mime_getchar_crlf __P((SM_FILE_T *, char **, int *));

/* character set for hex and base64 encoding */
static char	Base16Code[] =	"0123456789ABCDEF";
static char	Base64Code[] =	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* types of MIME boundaries */
# define MBT_SYNTAX	0	/* syntax error */
# define MBT_NOTSEP	1	/* not a boundary */
# define MBT_INTERMED	2	/* intermediate boundary (no trailing --) */
# define MBT_FINAL	3	/* final boundary (trailing -- included) */

static char	*MimeBoundaryNames[] =
{
	"SYNTAX",	"NOTSEP",	"INTERMED",	"FINAL"
};

static bool	MapNLtoCRLF;

/*
**  MIME8TO7 -- output 8 bit body in 7 bit format
**
**	The header has already been output -- this has to do the
**	8 to 7 bit conversion.  It would be easy if we didn't have
**	to deal with nested formats (multipart/xxx and message/rfc822).
**
**	We won't be called if we don't have to do a conversion, and
**	appropriate MIME-Version: and Content-Type: fields have been
**	output.  Any Content-Transfer-Encoding: field has not been
**	output, and we can add it here.
**
**	Parameters:
**		mci -- mailer connection information.
**		header -- the header for this body part.
**		e -- envelope.
**		boundaries -- the currently pending message boundaries.
**			NULL if we are processing the outer portion.
**		flags -- to tweak processing.
**		level -- recursion level.
**
**	Returns:
**		An indicator of what terminated the message part:
**		  MBT_FINAL -- the final boundary
**		  MBT_INTERMED -- an intermediate boundary
**		  MBT_NOTSEP -- an end of file
**		  SM_IO_EOF -- I/O error occurred
*/

struct args
{
	char	*a_field;	/* name of field */
	char	*a_value;	/* value of that field */
};

int
mime8to7(mci, header, e, boundaries, flags, level)
	register MCI *mci;
	HDR *header;
	register ENVELOPE *e;
	char **boundaries;
	int flags;
	int level;
{
	register char *p;
	int linelen;
	int blen;
	int bt;
	off_t offset;
	size_t sectionsize, sectionhighbits;
	int i;
	char *type;
	char *subtype;
	char *cte;
	char **pvp;
	int argc = 0;
	char *bp;
	bool use_qp = false;
	struct args argv[MAXMIMEARGS];
	char bbuf[128];
	char buf[MAXLINE];
	char pvpbuf[MAXLINE];
	extern unsigned char MimeTokenTab[256];

	if (level > MAXMIMENESTING)
	{
		if (!bitset(EF_TOODEEP, e->e_flags))
		{
			if (tTd(43, 4))
				sm_dprintf("mime8to7: too deep, level=%d\n",
					   level);
			usrerr("mime8to7: recursion level %d exceeded",
				level);
			e->e_flags |= EF_DONT_MIME|EF_TOODEEP;
		}
	}
	if (tTd(43, 1))
	{
		sm_dprintf("mime8to7: flags = %x, boundaries =", flags);
		if (boundaries[0] == NULL)
			sm_dprintf(" <none>");
		else
		{
			for (i = 0; boundaries[i] != NULL; i++)
				sm_dprintf(" %s", boundaries[i]);
		}
		sm_dprintf("\n");
	}
	MapNLtoCRLF = true;
	p = hvalue("Content-Transfer-Encoding", header);
	if (p == NULL ||
	    (pvp = prescan(p, '\0', pvpbuf, sizeof(pvpbuf), NULL,
			   MimeTokenTab, false)) == NULL ||
	    pvp[0] == NULL)
	{
		cte = NULL;
	}
	else
	{
		cataddr(pvp, NULL, buf, sizeof(buf), '\0', false);
		cte = sm_rpool_strdup_x(e->e_rpool, buf);
	}

	type = subtype = NULL;
	p = hvalue("Content-Type", header);
	if (p == NULL)
	{
		if (bitset(M87F_DIGEST, flags))
			p = "message/rfc822";
		else
			p = "text/plain";
	}
	if (p != NULL &&
	    (pvp = prescan(p, '\0', pvpbuf, sizeof(pvpbuf), NULL,
			   MimeTokenTab, false)) != NULL &&
	    pvp[0] != NULL)
	{
		if (tTd(43, 40))
		{
			for (i = 0; pvp[i] != NULL; i++)
				sm_dprintf("pvp[%d] = \"%s\"\n", i, pvp[i]);
		}
		type = *pvp++;
		if (*pvp != NULL && strcmp(*pvp, "/") == 0 &&
		    *++pvp != NULL)
		{
			subtype = *pvp++;
		}

		/* break out parameters */
		while (*pvp != NULL && argc < MAXMIMEARGS)
		{
			/* skip to semicolon separator */
			while (*pvp != NULL && strcmp(*pvp, ";") != 0)
				pvp++;
			if (*pvp++ == NULL || *pvp == NULL)
				break;

			/* complain about empty values */
			if (strcmp(*pvp, ";") == 0)
			{
				usrerr("mime8to7: Empty parameter in Content-Type header");

				/* avoid bounce loops */
				e->e_flags |= EF_DONT_MIME;
				continue;
			}

			/* extract field name */
			argv[argc].a_field = *pvp++;

			/* see if there is a value */
			if (*pvp != NULL && strcmp(*pvp, "=") == 0 &&
			    (*++pvp == NULL || strcmp(*pvp, ";") != 0))
			{
				argv[argc].a_value = *pvp;
				argc++;
			}
		}
	}

	/* check for disaster cases */
	if (type == NULL)
		type = "-none-";
	if (subtype == NULL)
		subtype = "-none-";

	/* don't propagate some flags more than one level into the message */
	flags &= ~M87F_DIGEST;

	/*
	**  Check for cases that can not be encoded.
	**
	**	For example, you can't encode certain kinds of types
	**	or already-encoded messages.  If we find this case,
	**	just copy it through.
	*/

	(void) sm_snprintf(buf, sizeof(buf), "%.100s/%.100s", type, subtype);
	if (wordinclass(buf, 'n') || (cte != NULL && !wordinclass(cte, 'e')))
		flags |= M87F_NO8BIT;

# ifdef USE_B_CLASS
	if (wordinclass(buf, 'b') || wordinclass(type, 'b'))
		MapNLtoCRLF = false;
# endif /* USE_B_CLASS */
	if (wordinclass(buf, 'q') || wordinclass(type, 'q'))
		use_qp = true;

	/*
	**  Multipart requires special processing.
	**
	**	Do a recursive descent into the message.
	*/

	if (sm_strcasecmp(type, "multipart") == 0 &&
	    (!bitset(M87F_NO8BIT, flags) || bitset(M87F_NO8TO7, flags)) &&
	    !bitset(EF_TOODEEP, e->e_flags)
	   )
	{

		if (sm_strcasecmp(subtype, "digest") == 0)
			flags |= M87F_DIGEST;

		for (i = 0; i < argc; i++)
		{
			if (sm_strcasecmp(argv[i].a_field, "boundary") == 0)
				break;
		}
		if (i >= argc || argv[i].a_value == NULL)
		{
			usrerr("mime8to7: Content-Type: \"%s\": %s boundary",
				i >= argc ? "missing" : "bogus", p);
			p = "---";

			/* avoid bounce loops */
			e->e_flags |= EF_DONT_MIME;
		}
		else
		{
			p = argv[i].a_value;
			stripquotes(p);
		}
		if (sm_strlcpy(bbuf, p, sizeof(bbuf)) >= sizeof(bbuf))
		{
			usrerr("mime8to7: multipart boundary \"%s\" too long",
				p);

			/* avoid bounce loops */
			e->e_flags |= EF_DONT_MIME;
		}

		if (tTd(43, 1))
			sm_dprintf("mime8to7: multipart boundary \"%s\"\n",
				bbuf);
		for (i = 0; i < MAXMIMENESTING; i++)
		{
			if (boundaries[i] == NULL)
				break;
		}
		if (i >= MAXMIMENESTING)
		{
			if (tTd(43, 4))
				sm_dprintf("mime8to7: too deep, i=%d\n", i);
			if (!bitset(EF_TOODEEP, e->e_flags))
				usrerr("mime8to7: multipart nesting boundary too deep");

			/* avoid bounce loops */
			e->e_flags |= EF_DONT_MIME|EF_TOODEEP;
		}
		else
		{
			boundaries[i] = bbuf;
			boundaries[i + 1] = NULL;
		}
		mci->mci_flags |= MCIF_INMIME;

		/* skip the early "comment" prologue */
		if (!putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
		bt = MBT_FINAL;
		while ((blen = sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
					sizeof(buf))) >= 0)
		{
			bt = mimeboundary(buf, boundaries);
			if (bt != MBT_NOTSEP)
				break;
			if (!putxline(buf, blen, mci,
					PXLF_MAPFROM|PXLF_STRIP8BIT))
				goto writeerr;
			if (tTd(43, 99))
				sm_dprintf("  ...%s", buf);
		}
		if (sm_io_eof(e->e_dfp))
			bt = MBT_FINAL;
		while (bt != MBT_FINAL)
		{
			auto HDR *hdr = NULL;

			(void) sm_strlcpyn(buf, sizeof(buf), 2, "--", bbuf);
			if (!putline(buf, mci))
				goto writeerr;
			if (tTd(43, 35))
				sm_dprintf("  ...%s\n", buf);
			collect(e->e_dfp, false, &hdr, e, false);
			if (tTd(43, 101))
				putline("+++after collect", mci);
			if (!putheader(mci, hdr, e, flags))
				goto writeerr;
			if (tTd(43, 101))
				putline("+++after putheader", mci);
			bt = mime8to7(mci, hdr, e, boundaries, flags,
				      level + 1);
			if (bt == SM_IO_EOF)
				goto writeerr;
		}
		(void) sm_strlcpyn(buf, sizeof(buf), 3, "--", bbuf, "--");
		if (!putline(buf, mci))
			goto writeerr;
		if (tTd(43, 35))
			sm_dprintf("  ...%s\n", buf);
		boundaries[i] = NULL;
		mci->mci_flags &= ~MCIF_INMIME;

		/* skip the late "comment" epilogue */
		while ((blen = sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
					sizeof(buf))) >= 0)
		{
			bt = mimeboundary(buf, boundaries);
			if (bt != MBT_NOTSEP)
				break;
			if (!putxline(buf, blen, mci,
					PXLF_MAPFROM|PXLF_STRIP8BIT))
				goto writeerr;
			if (tTd(43, 99))
				sm_dprintf("  ...%s", buf);
		}
		if (sm_io_eof(e->e_dfp))
			bt = MBT_FINAL;
		if (tTd(43, 3))
			sm_dprintf("\t\t\tmime8to7=>%s (multipart)\n",
				MimeBoundaryNames[bt]);
		return bt;
	}

	/*
	**  Message/xxx types -- recurse exactly once.
	**
	**	Class 's' is predefined to have "rfc822" only.
	*/

	if (sm_strcasecmp(type, "message") == 0)
	{
		if (!wordinclass(subtype, 's') ||
		    bitset(EF_TOODEEP, e->e_flags))
		{
			flags |= M87F_NO8BIT;
		}
		else
		{
			auto HDR *hdr = NULL;

			if (!putline("", mci))
				goto writeerr;

			mci->mci_flags |= MCIF_INMIME;
			collect(e->e_dfp, false, &hdr, e, false);
			if (tTd(43, 101))
				putline("+++after collect", mci);
			if (!putheader(mci, hdr, e, flags))
				goto writeerr;
			if (tTd(43, 101))
				putline("+++after putheader", mci);
			if (hvalue("MIME-Version", hdr) == NULL &&
			    !bitset(M87F_NO8TO7, flags) &&
			    !putline("MIME-Version: 1.0", mci))
				goto writeerr;
			bt = mime8to7(mci, hdr, e, boundaries, flags,
				      level + 1);
			mci->mci_flags &= ~MCIF_INMIME;
			return bt;
		}
	}

	/*
	**  Non-compound body type
	**
	**	Compute the ratio of seven to eight bit characters;
	**	use that as a heuristic to decide how to do the
	**	encoding.
	*/

	sectionsize = sectionhighbits = 0;
	if (!bitset(M87F_NO8BIT|M87F_NO8TO7, flags))
	{
		/* remember where we were */
		offset = sm_io_tell(e->e_dfp, SM_TIME_DEFAULT);
		if (offset == -1)
			syserr("mime8to7: cannot sm_io_tell on %cf%s",
			       DATAFL_LETTER, e->e_id);

		/* do a scan of this body type to count character types */
		while ((blen = sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
					sizeof(buf))) >= 0)
		{
			if (mimeboundary(buf, boundaries) != MBT_NOTSEP)
				break;
			for (i = 0; i < blen; i++)
			{
				/* count bytes with the high bit set */
				sectionsize++;
				if (bitset(0200, buf[i]))
					sectionhighbits++;
			}

			/*
			**  Heuristic: if 1/4 of the first 4K bytes are 8-bit,
			**  assume base64.  This heuristic avoids double-reading
			**  large graphics or video files.
			*/

			if (sectionsize >= 4096 &&
			    sectionhighbits > sectionsize / 4)
				break;
		}

		/* return to the original offset for processing */
		/* XXX use relative seeks to handle >31 bit file sizes? */
		if (sm_io_seek(e->e_dfp, SM_TIME_DEFAULT, offset, SEEK_SET) < 0)
			syserr("mime8to7: cannot sm_io_fseek on %cf%s",
			       DATAFL_LETTER, e->e_id);
		else
			sm_io_clearerr(e->e_dfp);
	}

	/*
	**  Heuristically determine encoding method.
	**	If more than 1/8 of the total characters have the
	**	eighth bit set, use base64; else use quoted-printable.
	**	However, only encode binary encoded data as base64,
	**	since otherwise the NL=>CRLF mapping will be a problem.
	*/

	if (tTd(43, 8))
	{
		sm_dprintf("mime8to7: %ld high bit(s) in %ld byte(s), cte=%s, type=%s/%s\n",
			(long) sectionhighbits, (long) sectionsize,
			cte == NULL ? "[none]" : cte,
			type == NULL ? "[none]" : type,
			subtype == NULL ? "[none]" : subtype);
	}
	if (cte != NULL && sm_strcasecmp(cte, "binary") == 0)
		sectionsize = sectionhighbits;
	linelen = 0;
	bp = buf;
	if (sectionhighbits == 0)
	{
		/* no encoding necessary */
		if (cte != NULL &&
		    bitset(MCIF_CVT8TO7|MCIF_CVT7TO8|MCIF_INMIME,
			   mci->mci_flags) &&
		    !bitset(M87F_NO8TO7, flags))
		{
			/*
			**  Skip _unless_ in MIME mode and potentially
			**  converting from 8 bit to 7 bit MIME.  See
			**  putheader() for the counterpart where the
			**  CTE header is skipped in the opposite
			**  situation.
			*/

			(void) sm_snprintf(buf, sizeof(buf),
				"Content-Transfer-Encoding: %.200s", cte);
			if (!putline(buf, mci))
				goto writeerr;
			if (tTd(43, 36))
				sm_dprintf("  ...%s\n", buf);
		}
		if (!putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
		while ((blen = sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
					sizeof(buf))) >= 0)
		{
			if (!bitset(MCIF_INLONGLINE, mci->mci_flags))
			{
				bt = mimeboundary(buf, boundaries);
				if (bt != MBT_NOTSEP)
					break;
			}
			if (!putxline(buf, blen, mci,
				      PXLF_MAPFROM|PXLF_NOADDEOL))
				goto writeerr;
		}
		if (sm_io_eof(e->e_dfp))
			bt = MBT_FINAL;
	}
	else if (!MapNLtoCRLF ||
		 (sectionsize / 8 < sectionhighbits && !use_qp))
	{
		/* use base64 encoding */
		int c1, c2;

		if (tTd(43, 36))
			sm_dprintf("  ...Content-Transfer-Encoding: base64\n");
		if (!putline("Content-Transfer-Encoding: base64", mci))
			goto writeerr;
		(void) sm_snprintf(buf, sizeof(buf),
			"X-MIME-Autoconverted: from 8bit to base64 by %s id %s",
			MyHostName, e->e_id);
		if (!putline(buf, mci) || !putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
		while ((c1 = mime_getchar_crlf(e->e_dfp, boundaries, &bt)) !=
			SM_IO_EOF)
		{
			if (linelen > 71)
			{
				*bp = '\0';
				if (!putline(buf, mci))
					goto writeerr;
				linelen = 0;
				bp = buf;
			}
			linelen += 4;
			*bp++ = Base64Code[(c1 >> 2)];
			c1 = (c1 & 0x03) << 4;
			c2 = mime_getchar_crlf(e->e_dfp, boundaries, &bt);
			if (c2 == SM_IO_EOF)
			{
				*bp++ = Base64Code[c1];
				*bp++ = '=';
				*bp++ = '=';
				break;
			}
			c1 |= (c2 >> 4) & 0x0f;
			*bp++ = Base64Code[c1];
			c1 = (c2 & 0x0f) << 2;
			c2 = mime_getchar_crlf(e->e_dfp, boundaries, &bt);
			if (c2 == SM_IO_EOF)
			{
				*bp++ = Base64Code[c1];
				*bp++ = '=';
				break;
			}
			c1 |= (c2 >> 6) & 0x03;
			*bp++ = Base64Code[c1];
			*bp++ = Base64Code[c2 & 0x3f];
		}
		*bp = '\0';
		if (!putline(buf, mci))
			goto writeerr;
	}
	else
	{
		/* use quoted-printable encoding */
		int c1, c2;
		int fromstate;
		BITMAP256 badchars;

		/* set up map of characters that must be mapped */
		clrbitmap(badchars);
		for (c1 = 0x00; c1 < 0x20; c1++)
			setbitn(c1, badchars);
		clrbitn('\t', badchars);
		for (c1 = 0x7f; c1 < 0x100; c1++)
			setbitn(c1, badchars);
		setbitn('=', badchars);
		if (bitnset(M_EBCDIC, mci->mci_mailer->m_flags))
			for (p = "!\"#$@[\\]^`{|}~"; *p != '\0'; p++)
				setbitn(*p, badchars);

		if (tTd(43, 36))
			sm_dprintf("  ...Content-Transfer-Encoding: quoted-printable\n");
		if (!putline("Content-Transfer-Encoding: quoted-printable",
				mci))
			goto writeerr;
		(void) sm_snprintf(buf, sizeof(buf),
			"X-MIME-Autoconverted: from 8bit to quoted-printable by %s id %s",
			MyHostName, e->e_id);
		if (!putline(buf, mci) || !putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
		fromstate = 0;
		c2 = '\n';
		while ((c1 = mime_getchar(e->e_dfp, boundaries, &bt)) !=
			SM_IO_EOF)
		{
			if (c1 == '\n')
			{
				if (c2 == ' ' || c2 == '\t')
				{
					*bp++ = '=';
					*bp++ = Base16Code[(c2 >> 4) & 0x0f];
					*bp++ = Base16Code[c2 & 0x0f];
				}
				if (buf[0] == '.' && bp == &buf[1])
				{
					buf[0] = '=';
					*bp++ = Base16Code[('.' >> 4) & 0x0f];
					*bp++ = Base16Code['.' & 0x0f];
				}
				*bp = '\0';
				if (!putline(buf, mci))
					goto writeerr;
				linelen = fromstate = 0;
				bp = buf;
				c2 = c1;
				continue;
			}
			if (c2 == ' ' && linelen == 4 && fromstate == 4 &&
			    bitnset(M_ESCFROM, mci->mci_mailer->m_flags))
			{
				*bp++ = '=';
				*bp++ = '2';
				*bp++ = '0';
				linelen += 3;
			}
			else if (c2 == ' ' || c2 == '\t')
			{
				*bp++ = c2;
				linelen++;
			}
			if (linelen > 72 &&
			    (linelen > 75 || c1 != '.' ||
			     (linelen > 73 && c2 == '.')))
			{
				if (linelen > 73 && c2 == '.')
					bp--;
				else
					c2 = '\n';
				*bp++ = '=';
				*bp = '\0';
				if (!putline(buf, mci))
					goto writeerr;
				linelen = fromstate = 0;
				bp = buf;
				if (c2 == '.')
				{
					*bp++ = '.';
					linelen++;
				}
			}
			if (bitnset(bitidx(c1), badchars))
			{
				*bp++ = '=';
				*bp++ = Base16Code[(c1 >> 4) & 0x0f];
				*bp++ = Base16Code[c1 & 0x0f];
				linelen += 3;
			}
			else if (c1 != ' ' && c1 != '\t')
			{
				if (linelen < 4 && c1 == "From"[linelen])
					fromstate++;
				*bp++ = c1;
				linelen++;
			}
			c2 = c1;
		}

		/* output any saved character */
		if (c2 == ' ' || c2 == '\t')
		{
			*bp++ = '=';
			*bp++ = Base16Code[(c2 >> 4) & 0x0f];
			*bp++ = Base16Code[c2 & 0x0f];
			linelen += 3;
		}

		if (linelen > 0 || boundaries[0] != NULL)
		{
			*bp = '\0';
			if (!putline(buf, mci))
				goto writeerr;
		}

	}
	if (tTd(43, 3))
		sm_dprintf("\t\t\tmime8to7=>%s (basic)\n", MimeBoundaryNames[bt]);
	return bt;

  writeerr:
	return SM_IO_EOF;
}
/*
**  MIME_GETCHAR -- get a character for MIME processing
**
**	Treats boundaries as SM_IO_EOF.
**
**	Parameters:
**		fp -- the input file.
**		boundaries -- the current MIME boundaries.
**		btp -- if the return value is SM_IO_EOF, *btp is set to
**			the type of the boundary.
**
**	Returns:
**		The next character in the input stream.
*/

static int
mime_getchar(fp, boundaries, btp)
	register SM_FILE_T *fp;
	char **boundaries;
	int *btp;
{
	int c;
	static unsigned char *bp = NULL;
	static int buflen = 0;
	static bool atbol = true;	/* at beginning of line */
	static int bt = MBT_SYNTAX;	/* boundary type of next SM_IO_EOF */
	static unsigned char buf[128];	/* need not be a full line */
	int start = 0;			/* indicates position of - in buffer */

	if (buflen == 1 && *bp == '\n')
	{
		/* last \n in buffer may be part of next MIME boundary */
		c = *bp;
	}
	else if (buflen > 0)
	{
		buflen--;
		return *bp++;
	}
	else
		c = sm_io_getc(fp, SM_TIME_DEFAULT);
	bp = buf;
	buflen = 0;
	if (c == '\n')
	{
		/* might be part of a MIME boundary */
		*bp++ = c;
		atbol = true;
		c = sm_io_getc(fp, SM_TIME_DEFAULT);
		if (c == '\n')
		{
			(void) sm_io_ungetc(fp, SM_TIME_DEFAULT, c);
			return c;
		}
		start = 1;
	}
	if (c != SM_IO_EOF)
		*bp++ = c;
	else
		bt = MBT_FINAL;
	if (atbol && c == '-')
	{
		/* check for a message boundary */
		c = sm_io_getc(fp, SM_TIME_DEFAULT);
		if (c != '-')
		{
			if (c != SM_IO_EOF)
				*bp++ = c;
			else
				bt = MBT_FINAL;
			buflen = bp - buf - 1;
			bp = buf;
			return *bp++;
		}

		/* got "--", now check for rest of separator */
		*bp++ = '-';
		while (bp < &buf[sizeof(buf) - 2] &&
		       (c = sm_io_getc(fp, SM_TIME_DEFAULT)) != SM_IO_EOF &&
		       c != '\n')
		{
			*bp++ = c;
		}
		*bp = '\0';	/* XXX simply cut off? */
		bt = mimeboundary((char *) &buf[start], boundaries);
		switch (bt)
		{
		  case MBT_FINAL:
		  case MBT_INTERMED:
			/* we have a message boundary */
			buflen = 0;
			*btp = bt;
			return SM_IO_EOF;
		}

		if (bp < &buf[sizeof(buf) - 2] && c != SM_IO_EOF)
			*bp++ = c;
	}

	atbol = c == '\n';
	buflen = bp - buf - 1;
	if (buflen < 0)
	{
		*btp = bt;
		return SM_IO_EOF;
	}
	bp = buf;
	return *bp++;
}
/*
**  MIME_GETCHAR_CRLF -- do mime_getchar, but translate NL => CRLF
**
**	Parameters:
**		fp -- the input file.
**		boundaries -- the current MIME boundaries.
**		btp -- if the return value is SM_IO_EOF, *btp is set to
**			the type of the boundary.
**
**	Returns:
**		The next character in the input stream.
*/

static int
mime_getchar_crlf(fp, boundaries, btp)
	register SM_FILE_T *fp;
	char **boundaries;
	int *btp;
{
	static bool sendlf = false;
	int c;

	if (sendlf)
	{
		sendlf = false;
		return '\n';
	}
	c = mime_getchar(fp, boundaries, btp);
	if (c == '\n' && MapNLtoCRLF)
	{
		sendlf = true;
		return '\r';
	}
	return c;
}
/*
**  MIMEBOUNDARY -- determine if this line is a MIME boundary & its type
**
**	Parameters:
**		line -- the input line.
**		boundaries -- the set of currently pending boundaries.
**
**	Returns:
**		MBT_NOTSEP -- if this is not a separator line
**		MBT_INTERMED -- if this is an intermediate separator
**		MBT_FINAL -- if this is a final boundary
**		MBT_SYNTAX -- if this is a boundary for the wrong
**			enclosure -- i.e., a syntax error.
*/

static int
mimeboundary(line, boundaries)
	register char *line;
	char **boundaries;
{
	int type = MBT_NOTSEP;
	int i;
	int savec;

	if (line[0] != '-' || line[1] != '-' || boundaries == NULL)
		return MBT_NOTSEP;
	i = strlen(line);
	if (i > 0 && line[i - 1] == '\n')
		i--;

	/* strip off trailing whitespace */
	while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t'
#if _FFR_MIME_CR_OK
		|| line[i - 1] == '\r'
#endif /* _FFR_MIME_CR_OK */
	       ))
		i--;
	savec = line[i];
	line[i] = '\0';

	if (tTd(43, 5))
		sm_dprintf("mimeboundary: line=\"%s\"... ", line);

	/* check for this as an intermediate boundary */
	if (isboundary(&line[2], boundaries) >= 0)
		type = MBT_INTERMED;
	else if (i > 2 && strncmp(&line[i - 2], "--", 2) == 0)
	{
		/* check for a final boundary */
		line[i - 2] = '\0';
		if (isboundary(&line[2], boundaries) >= 0)
			type = MBT_FINAL;
		line[i - 2] = '-';
	}

	line[i] = savec;
	if (tTd(43, 5))
		sm_dprintf("%s\n", MimeBoundaryNames[type]);
	return type;
}
/*
**  DEFCHARSET -- return default character set for message
**
**	The first choice for character set is for the mailer
**	corresponding to the envelope sender.  If neither that
**	nor the global configuration file has a default character
**	set defined, return "unknown-8bit" as recommended by
**	RFC 1428 section 3.
**
**	Parameters:
**		e -- the envelope for this message.
**
**	Returns:
**		The default character set for that mailer.
*/

char *
defcharset(e)
	register ENVELOPE *e;
{
	if (e != NULL && e->e_from.q_mailer != NULL &&
	    e->e_from.q_mailer->m_defcharset != NULL)
		return e->e_from.q_mailer->m_defcharset;
	if (DefaultCharSet != NULL)
		return DefaultCharSet;
	return "unknown-8bit";
}
/*
**  ISBOUNDARY -- is a given string a currently valid boundary?
**
**	Parameters:
**		line -- the current input line.
**		boundaries -- the list of valid boundaries.
**
**	Returns:
**		The index number in boundaries if the line is found.
**		-1 -- otherwise.
**
*/

static int
isboundary(line, boundaries)
	char *line;
	char **boundaries;
{
	register int i;

	for (i = 0; i <= MAXMIMENESTING && boundaries[i] != NULL; i++)
	{
		if (strcmp(line, boundaries[i]) == 0)
			return i;
	}
	return -1;
}
#endif /* MIME8TO7 */

#if MIME7TO8
static int	mime_fromqp __P((unsigned char *, unsigned char **, int));

/*
**  MIME7TO8 -- output 7 bit encoded MIME body in 8 bit format
**
**  This is a hack. Supports translating the two 7-bit body-encodings
**  (quoted-printable and base64) to 8-bit coded bodies.
**
**  There is not much point in supporting multipart here, as the UA
**  will be able to deal with encoded MIME bodies if it can parse MIME
**  multipart messages.
**
**  Note also that we won't be called unless it is a text/plain MIME
**  message, encoded base64 or QP and mailer flag '9' has been defined
**  on mailer.
**
**  Contributed by Marius Olaffson <marius@rhi.hi.is>.
**
**	Parameters:
**		mci -- mailer connection information.
**		header -- the header for this body part.
**		e -- envelope.
**
**	Returns:
**		true iff body was written successfully
*/

static char index_64[128] =
{
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
	52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
	-1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
	15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
	-1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
	41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};

# define CHAR64(c)  (((c) < 0 || (c) > 127) ? -1 : index_64[(c)])

bool
mime7to8(mci, header, e)
	register MCI *mci;
	HDR *header;
	register ENVELOPE *e;
{
	int pxflags, blen;
	register char *p;
	char *cte;
	char **pvp;
	unsigned char *fbufp;
	char buf[MAXLINE];
	unsigned char fbuf[MAXLINE + 1];
	char pvpbuf[MAXLINE];
	extern unsigned char MimeTokenTab[256];

	p = hvalue("Content-Transfer-Encoding", header);
	if (p == NULL ||
	    (pvp = prescan(p, '\0', pvpbuf, sizeof(pvpbuf), NULL,
			   MimeTokenTab, false)) == NULL ||
	    pvp[0] == NULL)
	{
		/* "can't happen" -- upper level should have caught this */
		syserr("mime7to8: unparsable CTE %s", p == NULL ? "<NULL>" : p);

		/* avoid bounce loops */
		e->e_flags |= EF_DONT_MIME;

		/* cheap failsafe algorithm -- should work on text/plain */
		if (p != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
				"Content-Transfer-Encoding: %s", p);
			if (!putline(buf, mci))
				goto writeerr;
		}
		if (!putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
		while ((blen = sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
					sizeof(buf))) >= 0)
		{
			if (!putxline(buf, blen, mci, PXLF_MAPFROM))
				goto writeerr;
		}
		return true;
	}
	cataddr(pvp, NULL, buf, sizeof(buf), '\0', false);
	cte = sm_rpool_strdup_x(e->e_rpool, buf);

	mci->mci_flags |= MCIF_INHEADER;
	if (!putline("Content-Transfer-Encoding: 8bit", mci))
		goto writeerr;
	(void) sm_snprintf(buf, sizeof(buf),
		"X-MIME-Autoconverted: from %.200s to 8bit by %s id %s",
		cte, MyHostName, e->e_id);
	if (!putline(buf, mci) || !putline("", mci))
		goto writeerr;
	mci->mci_flags &= ~MCIF_INHEADER;

	/*
	**  Translate body encoding to 8-bit.  Supports two types of
	**  encodings; "base64" and "quoted-printable". Assume qp if
	**  it is not base64.
	*/

	pxflags = PXLF_MAPFROM;
	if (sm_strcasecmp(cte, "base64") == 0)
	{
		int c1, c2, c3, c4;

		fbufp = fbuf;
		while ((c1 = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT)) !=
			SM_IO_EOF)
		{
			if (isascii(c1) && isspace(c1))
				continue;

			do
			{
				c2 = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT);
			} while (isascii(c2) && isspace(c2));
			if (c2 == SM_IO_EOF)
				break;

			do
			{
				c3 = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT);
			} while (isascii(c3) && isspace(c3));
			if (c3 == SM_IO_EOF)
				break;

			do
			{
				c4 = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT);
			} while (isascii(c4) && isspace(c4));
			if (c4 == SM_IO_EOF)
				break;

			if (c1 == '=' || c2 == '=')
				continue;
			c1 = CHAR64(c1);
			c2 = CHAR64(c2);

#if MIME7TO8_OLD
#define CHK_EOL if (*--fbufp != '\n' || (fbufp > fbuf && *--fbufp != '\r')) \
			++fbufp;
#else /* MIME7TO8_OLD */
#define CHK_EOL if (*--fbufp != '\n' || (fbufp > fbuf && *--fbufp != '\r')) \
		{					\
			++fbufp;			\
			pxflags |= PXLF_NOADDEOL;	\
		}
#endif /* MIME7TO8_OLD */

#define PUTLINE64	\
	do		\
	{		\
		if (*fbufp++ == '\n' || fbufp >= &fbuf[MAXLINE])	\
		{							\
			CHK_EOL;					\
			if (!putxline((char *) fbuf, fbufp - fbuf, mci, pxflags)) \
				goto writeerr;				\
			pxflags &= ~PXLF_NOADDEOL;			\
			fbufp = fbuf;					\
		}	\
	} while (0)

			*fbufp = (c1 << 2) | ((c2 & 0x30) >> 4);
			PUTLINE64;
			if (c3 == '=')
				continue;
			c3 = CHAR64(c3);
			*fbufp = ((c2 & 0x0f) << 4) | ((c3 & 0x3c) >> 2);
			PUTLINE64;
			if (c4 == '=')
				continue;
			c4 = CHAR64(c4);
			*fbufp = ((c3 & 0x03) << 6) | c4;
			PUTLINE64;
		}
	}
	else
	{
		int off;

		/* quoted-printable */
		pxflags |= PXLF_NOADDEOL;
		fbufp = fbuf;
		while (sm_io_fgets(e->e_dfp, SM_TIME_DEFAULT, buf,
				   sizeof(buf)) >= 0)
		{
			off = mime_fromqp((unsigned char *) buf, &fbufp,
					  &fbuf[MAXLINE] - fbufp);
again:
			if (off < -1)
				continue;

			if (fbufp - fbuf > 0)
			{
				if (!putxline((char *) fbuf, fbufp - fbuf - 1,
						mci, pxflags))
					goto writeerr;
			}
			fbufp = fbuf;
			if (off >= 0 && buf[off] != '\0')
			{
				off = mime_fromqp((unsigned char *) (buf + off),
						  &fbufp,
						  &fbuf[MAXLINE] - fbufp);
				goto again;
			}
		}
	}

	/* force out partial last line */
	if (fbufp > fbuf)
	{
		*fbufp = '\0';
		if (!putxline((char *) fbuf, fbufp - fbuf, mci, pxflags))
			goto writeerr;
	}

	/*
	**  The decoded text may end without an EOL.  Since this function
	**  is only called for text/plain MIME messages, it is safe to
	**  add an extra one at the end just in case.  This is a hack,
	**  but so is auto-converting MIME in the first place.
	*/

	if (!putline("", mci))
		goto writeerr;

	if (tTd(43, 3))
		sm_dprintf("\t\t\tmime7to8 => %s to 8bit done\n", cte);
	return true;

  writeerr:
	return false;
}
/*
**  The following is based on Borenstein's "codes.c" module, with simplifying
**  changes as we do not deal with multipart, and to do the translation in-core,
**  with an attempt to prevent overrun of output buffers.
**
**  What is needed here are changes to defend this code better against
**  bad encodings. Questionable to always return 0xFF for bad mappings.
*/

static char index_hex[128] =
{
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,
	-1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

# define HEXCHAR(c)  (((c) < 0 || (c) > 127) ? -1 : index_hex[(c)])

/*
**  MIME_FROMQP -- decode quoted printable string
**
**	Parameters:
**		infile -- input (encoded) string
**		outfile -- output string
**		maxlen -- size of output buffer
**
**	Returns:
**		-2 if decoding failure
**		-1 if infile completely decoded into outfile
**		>= 0 is the position in infile decoding
**			reached before maxlen was reached
*/

static int
mime_fromqp(infile, outfile, maxlen)
	unsigned char *infile;
	unsigned char **outfile;
	int maxlen;		/* Max # of chars allowed in outfile */
{
	int c1, c2;
	int nchar = 0;
	unsigned char *b;

	/* decrement by one for trailing '\0', at least one other char */
	if (--maxlen < 1)
		return 0;

	b = infile;
	while ((c1 = *infile++) != '\0' && nchar < maxlen)
	{
		if (c1 == '=')
		{
			if ((c1 = *infile++) == '\0')
				break;

			if (c1 == '\n' || (c1 = HEXCHAR(c1)) == -1)
			{
				/* ignore it and the rest of the buffer */
				return -2;
			}
			else
			{
				do
				{
					if ((c2 = *infile++) == '\0')
					{
						c2 = -1;
						break;
					}
				} while ((c2 = HEXCHAR(c2)) == -1);

				if (c2 == -1)
					break;
				nchar++;
				*(*outfile)++ = c1 << 4 | c2;
			}
		}
		else
		{
			nchar++;
			*(*outfile)++ = c1;
			if (c1 == '\n')
				break;
		}
	}
	*(*outfile)++ = '\0';
	if (nchar >= maxlen)
		return (infile - b - 1);
	return -1;
}
#endif /* MIME7TO8 */
