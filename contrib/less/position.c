/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Routines dealing with the "position" table.
 * This is a table which tells the position (in the input file) of the
 * first char on each currently displayed line.
 *
 * {{ The position table is scrolled by moving all the entries.
 *    Would be better to have a circular table 
 *    and just change a couple of pointers. }}
 */

#include "less.h"
#include "position.h"

static POSITION *table = NULL;	/* The position table */
static int table_size;

extern int sc_width, sc_height;

/*
 * Return the starting file position of a line displayed on the screen.
 * The line may be specified as a line number relative to the top
 * of the screen, but is usually one of these special cases:
 *	the top (first) line on the screen
 *	the second line on the screen
 *	the bottom line on the screen
 *	the line after the bottom line on the screen
 */
	public POSITION
position(sindex)
	int sindex;
{
	switch (sindex)
	{
	case BOTTOM:
		sindex = sc_height - 2;
		break;
	case BOTTOM_PLUS_ONE:
		sindex = sc_height - 1;
		break;
	case MIDDLE:
		sindex = (sc_height - 1) / 2;
		break;
	}
	return (table[sindex]);
}

/*
 * Add a new file position to the bottom of the position table.
 */
	public void
add_forw_pos(pos)
	POSITION pos;
{
	int i;

	/*
	 * Scroll the position table up.
	 */
	for (i = 1;  i < sc_height;  i++)
		table[i-1] = table[i];
	table[sc_height - 1] = pos;
}

/*
 * Add a new file position to the top of the position table.
 */
	public void
add_back_pos(pos)
	POSITION pos;
{
	int i;

	/*
	 * Scroll the position table down.
	 */
	for (i = sc_height - 1;  i > 0;  i--)
		table[i] = table[i-1];
	table[0] = pos;
}

/*
 * Initialize the position table, done whenever we clear the screen.
 */
	public void
pos_clear()
{
	int i;

	for (i = 0;  i < sc_height;  i++)
		table[i] = NULL_POSITION;
}

/*
 * Allocate or reallocate the position table.
 */
	public void
pos_init()
{
	struct scrpos scrpos;

	if (sc_height <= table_size)
		return;
	/*
	 * If we already have a table, remember the first line in it
	 * before we free it, so we can copy that line to the new table.
	 */
	if (table != NULL)
	{
		get_scrpos(&scrpos, TOP);
		free((char*)table);
	} else
		scrpos.pos = NULL_POSITION;
	table = (POSITION *) ecalloc(sc_height, sizeof(POSITION));
	table_size = sc_height;
	pos_clear();
	if (scrpos.pos != NULL_POSITION)
		table[scrpos.ln-1] = scrpos.pos;
}

/*
 * See if the byte at a specified position is currently on the screen.
 * Check the position table to see if the position falls within its range.
 * Return the position table entry if found, -1 if not.
 */
	public int
onscreen(pos)
	POSITION pos;
{
	int i;

	if (pos < table[0])
		return (-1);
	for (i = 1;  i < sc_height;  i++)
		if (pos < table[i])
			return (i-1);
	return (-1);
}

/*
 * See if the entire screen is empty.
 */
	public int
empty_screen()
{
	return (empty_lines(0, sc_height-1));
}

	public int
empty_lines(s, e)
	int s;
	int e;
{
	int i;

	for (i = s;  i <= e;  i++)
		if (table[i] != NULL_POSITION && table[i] != 0)
			return (0);
	return (1);
}

/*
 * Get the current screen position.
 * The screen position consists of both a file position and
 * a screen line number where the file position is placed on the screen.
 * Normally the screen line number is 0, but if we are positioned
 * such that the top few lines are empty, we may have to set
 * the screen line to a number > 0.
 */
	public void
get_scrpos(scrpos, where)
	struct scrpos *scrpos;
	int where;
{
	int i;
	int dir;
	int last;

	switch (where)
	{
	case TOP: i = 0; dir = +1; last = sc_height-2; break;
	default:  i = sc_height-2; dir = -1; last = 0; break;
	}

	/*
	 * Find the first line on the screen which has something on it,
	 * and return the screen line number and the file position.
	 */
	for (;; i += dir)
	{
		if (table[i] != NULL_POSITION)
		{
			scrpos->ln = i+1;
			scrpos->pos = table[i];
			return;
		}
		if (i == last) break;
	}
	/*
	 * The screen is empty.
	 */
	scrpos->pos = NULL_POSITION;
}

/*
 * Adjust a screen line number to be a simple positive integer
 * in the range { 0 .. sc_height-2 }.
 * (The bottom line, sc_height-1, is reserved for prompts, etc.)
 * The given "sline" may be in the range { 1 .. sc_height-1 }
 * to refer to lines relative to the top of the screen (starting from 1),
 * or it may be in { -1 .. -(sc_height-1) } to refer to lines
 * relative to the bottom of the screen.
 */
	public int
sindex_from_sline(sline)
	int sline;
{
	/*
	 * Negative screen line number means
	 * relative to the bottom of the screen.
	 */
	if (sline < 0)
		sline += sc_height;
	/*
	 * Can't be less than 1 or greater than sc_height.
	 */
	if (sline <= 0)
		sline = 1;
	if (sline > sc_height)
		sline = sc_height;
	/*
	 * Return zero-based line number, not one-based.
	 */
	return (sline-1);
}
