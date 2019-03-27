/* $FreeBSD$ */
/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Primitives for displaying the file on the screen,
 * scrolling either forward or backward.
 */

#include "less.h"
#include "position.h"

public int screen_trashed;
public int squished;
public int no_back_scroll = 0;
public int forw_prompt;
public int same_pos_bell = 1;

extern int sigs;
extern int top_scroll;
extern int quiet;
extern int sc_width, sc_height;
extern int less_is_more;
extern int plusoption;
extern int forw_scroll;
extern int back_scroll;
extern int ignore_eoi;
extern int clear_bg;
extern int final_attr;
extern int oldbot;
#if HILITE_SEARCH
extern int size_linebuf;
extern int hilite_search;
extern int status_col;
#endif
#if TAGS
extern char *tagoption;
#endif

/*
 * Sound the bell to indicate user is trying to move past end of file.
 */
	static void
eof_bell()
{
	if (quiet == NOT_QUIET)
		bell();
	else
		vbell();
}

/*
 * Check to see if the end of file is currently displayed.
 */
	public int
eof_displayed()
{
	POSITION pos;

	if (ignore_eoi)
		return (0);

	if (ch_length() == NULL_POSITION)
		/*
		 * If the file length is not known,
		 * we can't possibly be displaying EOF.
		 */
		return (0);

	/*
	 * If the bottom line is empty, we are at EOF.
	 * If the bottom line ends at the file length,
	 * we must be just at EOF.
	 */
	pos = position(BOTTOM_PLUS_ONE);
	return (pos == NULL_POSITION || pos == ch_length());
}

/*
 * Check to see if the entire file is currently displayed.
 */
	public int
entire_file_displayed()
{
	POSITION pos;

	/* Make sure last line of file is displayed. */
	if (!eof_displayed())
		return (0);

	/* Make sure first line of file is displayed. */
	pos = position(0);
	return (pos == NULL_POSITION || pos == 0);
}

/*
 * If the screen is "squished", repaint it.
 * "Squished" means the first displayed line is not at the top
 * of the screen; this can happen when we display a short file
 * for the first time.
 */
	public void
squish_check()
{
	if (!squished)
		return;
	squished = 0;
	repaint();
}

/*
 * Display n lines, scrolling forward, 
 * starting at position pos in the input file.
 * "force" means display the n lines even if we hit end of file.
 * "only_last" means display only the last screenful if n > screen size.
 * "nblank" is the number of blank lines to draw before the first
 *   real line.  If nblank > 0, the pos must be NULL_POSITION.
 *   The first real line after the blanks will start at ch_zero().
 */
	public void
forw(n, pos, force, only_last, nblank)
	int n;
	POSITION pos;
	int force;
	int only_last;
	int nblank;
{
	int nlines = 0;
	int do_repaint;
	static int first_time = 1;

	squish_check();

	/*
	 * do_repaint tells us not to display anything till the end, 
	 * then just repaint the entire screen.
	 * We repaint if we are supposed to display only the last 
	 * screenful and the request is for more than a screenful.
	 * Also if the request exceeds the forward scroll limit
	 * (but not if the request is for exactly a screenful, since
	 * repainting itself involves scrolling forward a screenful).
	 */
	do_repaint = (only_last && n > sc_height-1) || 
		(forw_scroll >= 0 && n > forw_scroll && n != sc_height-1);

#if HILITE_SEARCH
	if (hilite_search == OPT_ONPLUS || is_filtering() || status_col) {
		prep_hilite(pos, pos + 4*size_linebuf, ignore_eoi ? 1 : -1);
		pos = next_unfiltered(pos);
	}
#endif

	if (!do_repaint)
	{
		if (top_scroll && n >= sc_height - 1 && pos != ch_length())
		{
			/*
			 * Start a new screen.
			 * {{ This is not really desirable if we happen
			 *    to hit eof in the middle of this screen,
			 *    but we don't yet know if that will happen. }}
			 */
			pos_clear();
			add_forw_pos(pos);
			force = 1;
			if (less_is_more == 0) {
				clear();
				home();
			}
		}

		if (pos != position(BOTTOM_PLUS_ONE) || empty_screen())
		{
			/*
			 * This is not contiguous with what is
			 * currently displayed.  Clear the screen image 
			 * (position table) and start a new screen.
			 */
			pos_clear();
			add_forw_pos(pos);
			force = 1;
			if (top_scroll)
			{
				clear();
				home();
			} else if (!first_time)
			{
				putstr("...skipping...\n");
			}
		}
	}

	while (--n >= 0)
	{
		/*
		 * Read the next line of input.
		 */
		if (nblank > 0)
		{
			/*
			 * Still drawing blanks; don't get a line 
			 * from the file yet.
			 * If this is the last blank line, get ready to
			 * read a line starting at ch_zero() next time.
			 */
			if (--nblank == 0)
				pos = ch_zero();
		} else
		{
			/* 
			 * Get the next line from the file.
			 */
			pos = forw_line(pos);
#if HILITE_SEARCH
			pos = next_unfiltered(pos);
#endif
			if (pos == NULL_POSITION)
			{
				/*
				 * End of file: stop here unless the top line 
				 * is still empty, or "force" is true.
				 * Even if force is true, stop when the last
				 * line in the file reaches the top of screen.
				 */
				if (!force && position(TOP) != NULL_POSITION)
					break;
				if (!empty_lines(0, 0) && 
				    !empty_lines(1, 1) &&
				     empty_lines(2, sc_height-1))
					break;
			}
		}
		/*
		 * Add the position of the next line to the position table.
		 * Display the current line on the screen.
		 */
		add_forw_pos(pos);
		nlines++;
		if (do_repaint)
			continue;
		/*
		 * If this is the first screen displayed and
		 * we hit an early EOF (i.e. before the requested
		 * number of lines), we "squish" the display down
		 * at the bottom of the screen.
		 * But don't do this if a + option or a -t option
		 * was given.  These options can cause us to
		 * start the display after the beginning of the file,
		 * and it is not appropriate to squish in that case.
		 */
		if ((first_time || less_is_more) &&
		    pos == NULL_POSITION && !top_scroll && 
#if TAGS
		    tagoption == NULL &&
#endif
		    !plusoption)
		{
			squished = 1;
			continue;
		}
		put_line();
#if 0
		/* {{ 
		 * Can't call clear_eol here.  The cursor might be at end of line
		 * on an ignaw terminal, so clear_eol would clear the last char
		 * of the current line instead of all of the next line.
		 * If we really need to do this on clear_bg terminals, we need
		 * to find a better way.
		 * }}
		 */
		if (clear_bg && apply_at_specials(final_attr) != AT_NORMAL)
		{
			/*
			 * Writing the last character on the last line
			 * of the display may have scrolled the screen.
			 * If we were in standout mode, clear_bg terminals 
			 * will fill the new line with the standout color.
			 * Now we're in normal mode again, so clear the line.
			 */
			clear_eol();
		}
#endif
		forw_prompt = 1;
	}

	if (nlines == 0 && !ignore_eoi && same_pos_bell)
		eof_bell();
	else if (do_repaint)
		repaint();
	first_time = 0;
	(void) currline(BOTTOM);
}

/*
 * Display n lines, scrolling backward.
 */
	public void
back(n, pos, force, only_last)
	int n;
	POSITION pos;
	int force;
	int only_last;
{
	int nlines = 0;
	int do_repaint;

	squish_check();
	do_repaint = (n > get_back_scroll() || (only_last && n > sc_height-1));
#if HILITE_SEARCH
	if (hilite_search == OPT_ONPLUS || is_filtering() || status_col) {
		prep_hilite((pos < 3*size_linebuf) ?  0 : pos - 3*size_linebuf, pos, -1);
	}
#endif
	while (--n >= 0)
	{
		/*
		 * Get the previous line of input.
		 */
#if HILITE_SEARCH
		pos = prev_unfiltered(pos);
#endif

		pos = back_line(pos);
		if (pos == NULL_POSITION)
		{
			/*
			 * Beginning of file: stop here unless "force" is true.
			 */
			if (!force)
				break;
		}
		/*
		 * Add the position of the previous line to the position table.
		 * Display the line on the screen.
		 */
		add_back_pos(pos);
		nlines++;
		if (!do_repaint)
		{
			home();
			add_line();
			put_line();
		}
	}

	if (nlines == 0 && same_pos_bell)
		eof_bell();
	else if (do_repaint)
		repaint();
	else if (!oldbot)
		lower_left();
	(void) currline(BOTTOM);
}

/*
 * Display n more lines, forward.
 * Start just after the line currently displayed at the bottom of the screen.
 */
	public void
forward(n, force, only_last)
	int n;
	int force;
	int only_last;
{
	POSITION pos;

	if (get_quit_at_eof() && eof_displayed() && !(ch_getflags() & CH_HELPFILE))
	{
		/*
		 * If the -e flag is set and we're trying to go
		 * forward from end-of-file, go on to the next file.
		 */
		if (edit_next(1))
			quit(QUIT_OK);
		return;
	}

	pos = position(BOTTOM_PLUS_ONE);
	if (pos == NULL_POSITION && (!force || empty_lines(2, sc_height-1)))
	{
		if (ignore_eoi)
		{
			/*
			 * ignore_eoi is to support A_F_FOREVER.
			 * Back up until there is a line at the bottom
			 * of the screen.
			 */
			if (empty_screen())
				pos = ch_zero();
			else
			{
				do
				{
					back(1, position(TOP), 1, 0);
					pos = position(BOTTOM_PLUS_ONE);
				} while (pos == NULL_POSITION);
			}
		} else
		{
			eof_bell();
			return;
		}
	}
	forw(n, pos, force, only_last, 0);
}

/*
 * Display n more lines, backward.
 * Start just before the line currently displayed at the top of the screen.
 */
	public void
backward(n, force, only_last)
	int n;
	int force;
	int only_last;
{
	POSITION pos;

	pos = position(TOP);
	if (pos == NULL_POSITION && (!force || position(BOTTOM) == 0))
	{
		eof_bell();
		return;   
	}
	back(n, pos, force, only_last);
}

/*
 * Get the backwards scroll limit.
 * Must call this function instead of just using the value of
 * back_scroll, because the default case depends on sc_height and
 * top_scroll, as well as back_scroll.
 */
	public int
get_back_scroll()
{
	if (no_back_scroll)
		return (0);
	if (back_scroll >= 0)
		return (back_scroll);
	if (top_scroll)
		return (sc_height - 2);
	return (10000); /* infinity */
}

/*
 * Return number of displayable lines in the file.
 * Stop counting at screen height + 1.
 */
	public int
get_line_count()
{
	int nlines;
	POSITION pos = ch_zero();

	for (nlines = 0;  nlines <= sc_height;  nlines++)
	{
		pos = forw_line(pos);
		if (pos == NULL_POSITION) break;
	}
	return nlines;
}
