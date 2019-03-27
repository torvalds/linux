/*
 * Copyright (c) 1998-2001, 2003 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: stab.c,v 8.92 2013-11-22 20:51:56 ca Exp $")

/*
**  STAB -- manage the symbol table
**
**	Parameters:
**		name -- the name to be looked up or inserted.
**		type -- the type of symbol.
**		op -- what to do:
**			ST_ENTER -- enter the name if not already present.
**			ST_FIND -- find it only.
**
**	Returns:
**		pointer to a STAB entry for this name.
**		NULL if not found and not entered.
**
**	Side Effects:
**		can update the symbol table.
*/

#define STABSIZE	2003
#define SM_LOWER(c)	((isascii(c) && isupper(c)) ? tolower(c) : (c))

static STAB	*SymTab[STABSIZE];

STAB *
stab(name, type, op)
	char *name;
	int type;
	int op;
{
	register STAB *s;
	register STAB **ps;
	register int hfunc;
	register char *p;
	int len;

	if (tTd(36, 5))
		sm_dprintf("STAB: %s %d ", name, type);

	/*
	**  Compute the hashing function
	*/

	hfunc = type;
	for (p = name; *p != '\0'; p++)
		hfunc = ((hfunc << 1) ^ (SM_LOWER(*p) & 0377)) % STABSIZE;

	if (tTd(36, 9))
		sm_dprintf("(hfunc=%d) ", hfunc);

	ps = &SymTab[hfunc];
	if (type == ST_MACRO || type == ST_RULESET || type == ST_NAMECANON)
	{
		while ((s = *ps) != NULL &&
		       (s->s_symtype != type || strcmp(name, s->s_name)))
			ps = &s->s_next;
	}
	else
	{
		while ((s = *ps) != NULL &&
		       (s->s_symtype != type || sm_strcasecmp(name, s->s_name)))
			ps = &s->s_next;
	}

	/*
	**  Dispose of the entry.
	*/

	if (s != NULL || op == ST_FIND)
	{
		if (tTd(36, 5))
		{
			if (s == NULL)
				sm_dprintf("not found\n");
			else
			{
				long *lp = (long *) s->s_class;

				sm_dprintf("type %d val %lx %lx %lx %lx\n",
					s->s_symtype, lp[0], lp[1], lp[2], lp[3]);
			}
		}
		return s;
	}

	/*
	**  Make a new entry and link it in.
	*/

	if (tTd(36, 5))
		sm_dprintf("entered\n");

	/* determine size of new entry */
	switch (type)
	{
	  case ST_CLASS:
		len = sizeof(s->s_class);
		break;

	  case ST_MAILER:
		len = sizeof(s->s_mailer);
		break;

	  case ST_ALIAS:
		len = sizeof(s->s_alias);
		break;

	  case ST_MAPCLASS:
		len = sizeof(s->s_mapclass);
		break;

	  case ST_MAP:
		len = sizeof(s->s_map);
		break;

	  case ST_HOSTSIG:
		len = sizeof(s->s_hostsig);
		break;

	  case ST_NAMECANON:
		len = sizeof(s->s_namecanon);
		break;

	  case ST_MACRO:
		len = sizeof(s->s_macro);
		break;

	  case ST_RULESET:
		len = sizeof(s->s_ruleset);
		break;

	  case ST_HEADER:
		len = sizeof(s->s_header);
		break;

	  case ST_SERVICE:
		len = sizeof(s->s_service);
		break;

#if LDAPMAP
	  case ST_LMAP:
		len = sizeof(s->s_lmap);
		break;
#endif /* LDAPMAP */

#if MILTER
	  case ST_MILTER:
		len = sizeof(s->s_milter);
		break;
#endif /* MILTER */

	  case ST_QUEUE:
		len = sizeof(s->s_quegrp);
		break;

#if SOCKETMAP
	  case ST_SOCKETMAP:
		len = sizeof(s->s_socketmap);
		break;
#endif /* SOCKETMAP */

	  default:
		/*
		**  Each mailer has its own MCI stab entry:
		**
		**  s = stab(host, ST_MCI + m->m_mno, ST_ENTER);
		**
		**  Therefore, anything ST_MCI or larger is an s_mci.
		*/

		if (type >= ST_MCI)
			len = sizeof(s->s_mci);
		else
		{
			syserr("stab: unknown symbol type %d", type);
			len = sizeof(s->s_value);
		}
		break;
	}
	len += sizeof(*s) - sizeof(s->s_value);

	if (tTd(36, 15))
		sm_dprintf("size of stab entry: %d\n", len);

	/* make new entry */
	s = (STAB *) sm_pmalloc_x(len);
	memset((char *) s, '\0', len);
	s->s_name = sm_pstrdup_x(name);
	s->s_symtype = type;

	/* link it in */
	*ps = s;

	/* set a default value for rulesets */
	if (type == ST_RULESET)
		s->s_ruleset = -1;

	return s;
}
/*
**  STABAPPLY -- apply function to all stab entries
**
**	Parameters:
**		func -- the function to apply.  It will be given two
**			parameters (the stab entry and the arg).
**		arg -- an arbitrary argument, passed to func.
**
**	Returns:
**		none.
*/

void
stabapply(func, arg)
	void (*func)__P((STAB *, int));
	int arg;
{
	register STAB **shead;
	register STAB *s;

	for (shead = SymTab; shead < &SymTab[STABSIZE]; shead++)
	{
		for (s = *shead; s != NULL; s = s->s_next)
		{
			if (tTd(36, 90))
				sm_dprintf("stabapply: trying %d/%s\n",
					s->s_symtype, s->s_name);
			func(s, arg);
		}
	}
}
/*
**  QUEUEUP_MACROS -- queueup the macros in a class
**
**	Write the macros listed in the specified class into the
**	file referenced by qfp.
**
**	Parameters:
**		class -- class ID.
**		qfp -- file pointer to the queue file.
**		e -- the envelope.
**
**	Returns:
**		none.
*/

void
queueup_macros(class, qfp, e)
	int class;
	SM_FILE_T *qfp;
	ENVELOPE *e;
{
	register STAB **shead;
	register STAB *s;

	if (e == NULL)
		return;

	class = bitidx(class);
	for (shead = SymTab; shead < &SymTab[STABSIZE]; shead++)
	{
		for (s = *shead; s != NULL; s = s->s_next)
		{
			int m;
			char *p;

			if (s->s_symtype == ST_CLASS &&
			    bitnset(bitidx(class), s->s_class) &&
			    (m = macid(s->s_name)) != 0 &&
			    (p = macvalue(m, e)) != NULL)
			{
				(void) sm_io_fprintf(qfp, SM_TIME_DEFAULT,
						      "$%s%s\n",
						      s->s_name,
						      denlstring(p, true,
								 false));
			}
		}
	}
}
/*
**  COPY_CLASS -- copy class members from one class to another
**
**	Parameters:
**		src -- source class.
**		dst -- destination class.
**
**	Returns:
**		none.
*/

void
copy_class(src, dst)
	int src;
	int dst;
{
	register STAB **shead;
	register STAB *s;

	src = bitidx(src);
	dst = bitidx(dst);
	for (shead = SymTab; shead < &SymTab[STABSIZE]; shead++)
	{
		for (s = *shead; s != NULL; s = s->s_next)
		{
			if (s->s_symtype == ST_CLASS &&
			    bitnset(src, s->s_class))
				setbitn(dst, s->s_class);
		}
	}
}

/*
**  RMEXPSTAB -- remove expired entries from SymTab.
**
**	These entries need to be removed in long-running processes,
**	e.g., persistent queue runners, to avoid consuming memory.
**
**	XXX It might be useful to restrict the maximum TTL to avoid
**		caching data very long.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		can remove entries from the symbol table.
*/

#define SM_STAB_FREE(x)	\
	do \
	{ \
		char *o = (x); \
		(x) = NULL; \
		if (o != NULL) \
			sm_free(o); \
	} while (0)

void
rmexpstab()
{
	int i;
	STAB *s, *p, *f;
	time_t now;

	now = curtime();
	for (i = 0; i < STABSIZE; i++)
	{
		p = NULL;
		s = SymTab[i];
		while (s != NULL)
		{
			switch (s->s_symtype)
			{
			  case ST_HOSTSIG:
				if (s->s_hostsig.hs_exp >= now)
					goto next;	/* not expired */
				SM_STAB_FREE(s->s_hostsig.hs_sig); /* XXX */
				break;

			  case ST_NAMECANON:
				if (s->s_namecanon.nc_exp >= now)
					goto next;	/* not expired */
				SM_STAB_FREE(s->s_namecanon.nc_cname); /* XXX */
				break;

			  default:
				if (s->s_symtype >= ST_MCI)
				{
					/* call mci_uncache? */
					SM_STAB_FREE(s->s_mci.mci_status);
					SM_STAB_FREE(s->s_mci.mci_rstatus);
					SM_STAB_FREE(s->s_mci.mci_heloname);
#if 0
					/* not dynamically allocated */
					SM_STAB_FREE(s->s_mci.mci_host);
					SM_STAB_FREE(s->s_mci.mci_tolist);
#endif /* 0 */
#if SASL
					/* should always by NULL */
					SM_STAB_FREE(s->s_mci.mci_sasl_string);
#endif /* SASL */
					if (s->s_mci.mci_rpool != NULL)
					{
						sm_rpool_free(s->s_mci.mci_rpool);
						s->s_mci.mci_macro.mac_rpool = NULL;
						s->s_mci.mci_rpool = NULL;
					}
					break;
				}
  next:
				p = s;
				s = s->s_next;
				continue;
			}

			/* remove entry */
			SM_STAB_FREE(s->s_name); /* XXX */
			f = s;
			s = s->s_next;
			sm_free(f);	/* XXX */
			if (p == NULL)
				SymTab[i] = s;
			else
				p->s_next = s;
		}
	}
}

#if SM_HEAP_CHECK
/*
**  DUMPSTAB -- dump symbol table.
**
**	For debugging.
*/

#define MAXSTTYPES	(ST_MCI + 1)

void
dumpstab()
{
	int i, t, total, types[MAXSTTYPES];
	STAB *s;
	static int prevt[MAXSTTYPES], prev = 0;

	total = 0;
	for (i = 0; i < MAXSTTYPES; i++)
		types[i] = 0;
	for (i = 0; i < STABSIZE; i++)
	{
		s = SymTab[i];
		while (s != NULL)
		{
			++total;
			t = s->s_symtype;
			if (t > MAXSTTYPES - 1)
				t = MAXSTTYPES - 1;
			types[t]++;
			s = s->s_next;
		}
	}
	sm_syslog(LOG_INFO, NOQID, "stab: total=%d (%d)", total, total - prev);
	prev = total;
	for (i = 0; i < MAXSTTYPES; i++)
	{
		if (types[i] != 0)
		{
			sm_syslog(LOG_INFO, NOQID, "stab: type[%2d]=%2d (%d)",
				i, types[i], types[i] - prevt[i]);
		}
		prevt[i] = types[i];
	}
}
#endif /* SM_HEAP_CHECK */
