/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


#include "less.h"
#include "position.h"

extern IFILE curr_ifile;
extern int sc_height;
extern int jump_sline;

/*
 * The table of marks.
 * Each mark is identified by a lowercase or uppercase letter.
 * The final one is lmark, for the "last mark"; addressed by the apostrophe.
 */
#define	NMARKS		((2*26)+1)	/* a-z, A-Z, lastmark */
#define	LASTMARK	(NMARKS-1)
static struct mark marks[NMARKS];

/*
 * Initialize the mark table to show no marks are set.
 */
	public void
init_mark()
{
	int i;

	for (i = 0;  i < NMARKS;  i++)
		marks[i].m_scrpos.pos = NULL_POSITION;
}

/*
 * See if a mark letter is valid (between a and z).
 */
	static struct mark *
getumark(c)
	int c;
{
	if (c >= 'a' && c <= 'z')
		return (&marks[c-'a']);

	if (c >= 'A' && c <= 'Z')
		return (&marks[c-'A'+26]);

	error("Invalid mark letter", NULL_PARG);
	return (NULL);
}

/*
 * Get the mark structure identified by a character.
 * The mark struct may come either from the mark table
 * or may be constructed on the fly for certain characters like ^, $.
 */
	static struct mark *
getmark(c)
	int c;
{
	struct mark *m;
	static struct mark sm;

	switch (c)
	{
	case '^':
		/*
		 * Beginning of the current file.
		 */
		m = &sm;
		m->m_scrpos.pos = ch_zero();
		m->m_scrpos.ln = 0;
		m->m_ifile = curr_ifile;
		break;
	case '$':
		/*
		 * End of the current file.
		 */
		if (ch_end_seek())
		{
			error("Cannot seek to end of file", NULL_PARG);
			return (NULL);
		}
		m = &sm;
		m->m_scrpos.pos = ch_tell();
		m->m_scrpos.ln = sc_height;
		m->m_ifile = curr_ifile;
		break;
	case '.':
		/*
		 * Current position in the current file.
		 */
		m = &sm;
		get_scrpos(&m->m_scrpos, TOP);
		m->m_ifile = curr_ifile;
		break;
	case '\'':
		/*
		 * The "last mark".
		 */
		m = &marks[LASTMARK];
		break;
	default:
		/*
		 * Must be a user-defined mark.
		 */
		m = getumark(c);
		if (m == NULL)
			break;
		if (m->m_scrpos.pos == NULL_POSITION)
		{
			error("Mark not set", NULL_PARG);
			return (NULL);
		}
		break;
	}
	return (m);
}

/*
 * Is a mark letter is invalid?
 */
	public int
badmark(c)
	int c;
{
	return (getmark(c) == NULL);
}

/*
 * Set a user-defined mark.
 */
	public void
setmark(c, where)
	int c;
	int where;
{
	struct mark *m;
	struct scrpos scrpos;

	m = getumark(c);
	if (m == NULL)
		return;
	get_scrpos(&scrpos, where);
	m->m_scrpos = scrpos;
	m->m_ifile = curr_ifile;
}

/*
 * Clear a user-defined mark.
 */
	public void
clrmark(c)
	int c;
{
	struct mark *m;

	m = getumark(c);
	if (m == NULL)
		return;
	m->m_scrpos.pos = NULL_POSITION;
}

/*
 * Set lmark (the mark named by the apostrophe).
 */
	public void
lastmark()
{
	struct scrpos scrpos;

	if (ch_getflags() & CH_HELPFILE)
		return;
	get_scrpos(&scrpos, TOP);
	if (scrpos.pos == NULL_POSITION)
		return;
	marks[LASTMARK].m_scrpos = scrpos;
	marks[LASTMARK].m_ifile = curr_ifile;
}

/*
 * Go to a mark.
 */
	public void
gomark(c)
	int c;
{
	struct mark *m;
	struct scrpos scrpos;

	m = getmark(c);
	if (m == NULL)
		return;

	/*
	 * If we're trying to go to the lastmark and 
	 * it has not been set to anything yet,
	 * set it to the beginning of the current file.
	 */
	if (m == &marks[LASTMARK] && m->m_scrpos.pos == NULL_POSITION)
	{
		m->m_ifile = curr_ifile;
		m->m_scrpos.pos = ch_zero();
		m->m_scrpos.ln = jump_sline;
	}

	/*
	 * If we're using lmark, we must save the screen position now,
	 * because if we call edit_ifile() below, lmark will change.
	 * (We save the screen position even if we're not using lmark.)
	 */
	scrpos = m->m_scrpos;
	if (m->m_ifile != curr_ifile)
	{
		/*
		 * Not in the current file; edit the correct file.
		 */
		if (edit_ifile(m->m_ifile))
			return;
	}

	jump_loc(scrpos.pos, scrpos.ln);
}

/*
 * Return the position associated with a given mark letter.
 *
 * We don't return which screen line the position 
 * is associated with, but this doesn't matter much,
 * because it's always the first non-blank line on the screen.
 */
	public POSITION
markpos(c)
	int c;
{
	struct mark *m;

	m = getmark(c);
	if (m == NULL)
		return (NULL_POSITION);

	if (m->m_ifile != curr_ifile)
	{
		error("Mark not in current file", NULL_PARG);
		return (NULL_POSITION);
	}
	return (m->m_scrpos.pos);
}

/*
 * Return the mark associated with a given position, if any.
 */
	public char
posmark(pos)
	POSITION pos;
{
	int i;

	/* Only lower case and upper case letters */
	for (i = 0;  i < 26*2;  i++)
	{
		if (marks[i].m_ifile == curr_ifile && marks[i].m_scrpos.pos == pos)
		{
			if (i < 26) return 'a' + i;
			return 'A' + i - 26;
		}
	}
	return 0;
}

/*
 * Clear the marks associated with a specified ifile.
 */
	public void
unmark(ifile)
	IFILE ifile;
{
	int i;

	for (i = 0;  i < NMARKS;  i++)
		if (marks[i].m_ifile == ifile)
			marks[i].m_scrpos.pos = NULL_POSITION;
}
