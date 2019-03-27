/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Routines to search a file for a pattern.
 */

#include "less.h"
#include "position.h"
#include "charset.h"

#define	MINPOS(a,b)	(((a) < (b)) ? (a) : (b))
#define	MAXPOS(a,b)	(((a) > (b)) ? (a) : (b))

extern int sigs;
extern int how_search;
extern int caseless;
extern int linenums;
extern int sc_height;
extern int jump_sline;
extern int bs_mode;
extern int ctldisp;
extern int status_col;
extern void *ml_search;
extern POSITION start_attnpos;
extern POSITION end_attnpos;
extern int utf_mode;
extern int screen_trashed;
#if HILITE_SEARCH
extern int hilite_search;
extern int size_linebuf;
extern int squished;
extern int can_goto_line;
static int hide_hilite;
static POSITION prep_startpos;
static POSITION prep_endpos;
static int is_caseless;
static int is_ucase_pattern;

/*
 * Structures for maintaining a set of ranges for hilites and filtered-out
 * lines. Each range is stored as a node within a red-black tree, and we
 * try to extend existing ranges (without creating overlaps) rather than
 * create new nodes if possible. We remember the last node found by a
 * search for constant-time lookup if the next search is near enough to
 * the previous. To aid that, we overlay a secondary doubly-linked list
 * on top of the red-black tree so we can find the preceding/succeeding
 * nodes also in constant time.
 *
 * Each node is allocated from a series of pools, each pool double the size
 * of the previous (for amortised constant time allocation). Since our only
 * tree operations are clear and node insertion, not node removal, we don't
 * need to maintain a usage bitmap or freelist and can just return nodes
 * from the pool in-order until capacity is reached.
 */
struct hilite
{
	POSITION hl_startpos;
	POSITION hl_endpos;
};
struct hilite_node
{
	struct hilite_node *parent;
	struct hilite_node *left;
	struct hilite_node *right;
	struct hilite_node *prev;
	struct hilite_node *next;
	int red;
	struct hilite r;
};
struct hilite_storage
{
	int capacity;
	int used;
	struct hilite_storage *next;
	struct hilite_node *nodes;
};
struct hilite_tree
{
	struct hilite_storage *first;
	struct hilite_storage *current;
	struct hilite_node *root;
	struct hilite_node *lookaside;
};
#define HILITE_INITIALIZER() { NULL, NULL, NULL, NULL }
#define HILITE_LOOKASIDE_STEPS 2

static struct hilite_tree hilite_anchor = HILITE_INITIALIZER();
static struct hilite_tree filter_anchor = HILITE_INITIALIZER();

#endif

/*
 * These are the static variables that represent the "remembered"
 * search pattern and filter pattern.
 */
struct pattern_info {
	PATTERN_TYPE compiled;
	char* text;
	int search_type;
};

#if NO_REGEX
#define info_compiled(info) ((void*)0)
#else
#define info_compiled(info) ((info)->compiled)
#endif
	
static struct pattern_info search_info;
static struct pattern_info filter_info;

/*
 * Are there any uppercase letters in this string?
 */
	static int
is_ucase(str)
	char *str;
{
	char *str_end = str + strlen(str);
	LWCHAR ch;

	while (str < str_end)
	{
		ch = step_char(&str, +1, str_end);
		if (IS_UPPER(ch))
			return (1);
	}
	return (0);
}

/*
 * Compile and save a search pattern.
 */
	static int
set_pattern(info, pattern, search_type)
	struct pattern_info *info;
	char *pattern;
	int search_type;
{
#if !NO_REGEX
	if (pattern == NULL)
		CLEAR_PATTERN(info->compiled);
	else if (compile_pattern(pattern, search_type, &info->compiled) < 0)
		return -1;
#endif
	/* Pattern compiled successfully; save the text too. */
	if (info->text != NULL)
		free(info->text);
	info->text = NULL;
	if (pattern != NULL)
	{
		info->text = (char *) ecalloc(1, strlen(pattern)+1);
		strcpy(info->text, pattern);
	}
	info->search_type = search_type;

	/*
	 * Ignore case if -I is set OR
	 * -i is set AND the pattern is all lowercase.
	 */
	is_ucase_pattern = is_ucase(pattern);
	if (is_ucase_pattern && caseless != OPT_ONPLUS)
		is_caseless = 0;
	else
		is_caseless = caseless;
	return 0;
}

/*
 * Discard a saved pattern.
 */
	static void
clear_pattern(info)
	struct pattern_info *info;
{
	if (info->text != NULL)
		free(info->text);
	info->text = NULL;
#if !NO_REGEX
	uncompile_pattern(&info->compiled);
#endif
}

/*
 * Initialize saved pattern to nothing.
 */
	static void
init_pattern(info)
	struct pattern_info *info;
{
	CLEAR_PATTERN(info->compiled);
	info->text = NULL;
	info->search_type = 0;
}

/*
 * Initialize search variables.
 */
	public void
init_search()
{
	init_pattern(&search_info);
	init_pattern(&filter_info);
}

/*
 * Determine which text conversions to perform before pattern matching.
 */
	static int
get_cvt_ops()
{
	int ops = 0;
	if (is_caseless || bs_mode == BS_SPECIAL)
	{
		if (is_caseless) 
			ops |= CVT_TO_LC;
		if (bs_mode == BS_SPECIAL)
			ops |= CVT_BS;
		if (bs_mode != BS_CONTROL)
			ops |= CVT_CRLF;
	} else if (bs_mode != BS_CONTROL)
	{
		ops |= CVT_CRLF;
	}
	if (ctldisp == OPT_ONPLUS)
		ops |= CVT_ANSI;
	return (ops);
}

/*
 * Is there a previous (remembered) search pattern?
 */
	static int
prev_pattern(info)
	struct pattern_info *info;
{
#if !NO_REGEX
	if ((info->search_type & SRCH_NO_REGEX) == 0)
		return (!is_null_pattern(info->compiled));
#endif
	return (info->text != NULL);
}

#if HILITE_SEARCH
/*
 * Repaint the hilites currently displayed on the screen.
 * Repaint each line which contains highlighted text.
 * If on==0, force all hilites off.
 */
	public void
repaint_hilite(on)
	int on;
{
	int sindex;
	POSITION pos;
	int save_hide_hilite;

	if (squished)
		repaint();

	save_hide_hilite = hide_hilite;
	if (!on)
	{
		if (hide_hilite)
			return;
		hide_hilite = 1;
	}

	if (!can_goto_line)
	{
		repaint();
		hide_hilite = save_hide_hilite;
		return;
	}

	for (sindex = TOP;  sindex < TOP + sc_height-1;  sindex++)
	{
		pos = position(sindex);
		if (pos == NULL_POSITION)
			continue;
		(void) forw_line(pos);
		goto_line(sindex);
		put_line();
	}
	lower_left();
	hide_hilite = save_hide_hilite;
}

/*
 * Clear the attn hilite.
 */
	public void
clear_attn()
{
	int sindex;
	POSITION old_start_attnpos;
	POSITION old_end_attnpos;
	POSITION pos;
	POSITION epos;
	int moved = 0;

	if (start_attnpos == NULL_POSITION)
		return;
	old_start_attnpos = start_attnpos;
	old_end_attnpos = end_attnpos;
	start_attnpos = end_attnpos = NULL_POSITION;

	if (!can_goto_line)
	{
		repaint();
		return;
	}
	if (squished)
		repaint();

	for (sindex = TOP;  sindex < TOP + sc_height-1;  sindex++)
	{
		pos = position(sindex);
		if (pos == NULL_POSITION)
			continue;
		epos = position(sindex+1);
		if (pos <= old_end_attnpos &&
		     (epos == NULL_POSITION || epos > old_start_attnpos))
		{
			(void) forw_line(pos);
			goto_line(sindex);
			put_line();
			moved = 1;
		}
	}
	if (moved)
		lower_left();
}
#endif

/*
 * Hide search string highlighting.
 */
	public void
undo_search()
{
	if (!prev_pattern(&search_info))
	{
		if (hilite_anchor.first == NULL)
		{
			error("No previous regular expression", NULL_PARG);
			return;
		}
		clr_hilite(); /* Next time, hilite_anchor.first will be NULL. */
	}
	clear_pattern(&search_info);
#if HILITE_SEARCH
	hide_hilite = !hide_hilite;
	repaint_hilite(1);
#endif
}

#if HILITE_SEARCH
/*
 * Clear the hilite list.
 */
	public void
clr_hlist(anchor)
	struct hilite_tree *anchor;
{
	struct hilite_storage *hls;
	struct hilite_storage *nexthls;

	for (hls = anchor->first;  hls != NULL;  hls = nexthls)
	{
		nexthls = hls->next;
		free((void*)hls->nodes);
		free((void*)hls);
	}
	anchor->first = NULL;
	anchor->current = NULL;
	anchor->root = NULL;

	anchor->lookaside = NULL;

	prep_startpos = prep_endpos = NULL_POSITION;
}

	public void
clr_hilite()
{
	clr_hlist(&hilite_anchor);
}

	public void
clr_filter()
{
	clr_hlist(&filter_anchor);
}

	struct hilite_node*
hlist_last(anchor)
	struct hilite_tree *anchor;
{
	struct hilite_node *n = anchor->root;
	while (n != NULL && n->right != NULL)
		n = n->right;
	return n;
}

	struct hilite_node*
hlist_next(n)
	struct hilite_node *n;
{
	return n->next;
}

	struct hilite_node*
hlist_prev(n)
	struct hilite_node *n;
{
	return n->prev;
}

/*
 * Find the node covering pos, or the node after it if no node covers it,
 * or return NULL if pos is after the last range. Remember the found node,
 * to speed up subsequent searches for the same or similar positions (if
 * we return NULL, remember the last node.)
 */
	struct hilite_node*
hlist_find(anchor, pos)
	struct hilite_tree *anchor;
	POSITION pos;
{
	struct hilite_node *n, *m;

	if (anchor->lookaside)
	{
		int steps = 0;
		int hit = 0;

		n = anchor->lookaside;

		for (;;)
		{
			if (pos < n->r.hl_endpos)
			{
				if (n->prev == NULL || pos >= n->prev->r.hl_endpos)
				{
					hit = 1;
					break;
				}
			} else if (n->next == NULL)
			{
				n = NULL;
				hit = 1;
				break;
			}

			/*
			 * If we don't find the right node within a small
			 * distance, don't keep doing a linear search!
			 */
			if (steps >= HILITE_LOOKASIDE_STEPS)
				break;
			steps++;

			if (pos < n->r.hl_endpos)
				anchor->lookaside = n = n->prev;
			else
				anchor->lookaside = n = n->next;
		}

		if (hit)
			return n;
	}

	n = anchor->root;
	m = NULL;

	while (n != NULL)
	{
		if (pos < n->r.hl_startpos)
		{
			if (n->left != NULL)
			{
				m = n;
				n = n->left;
				continue;
			}
			break;
		}
		if (pos >= n->r.hl_endpos)
		{
			if (n->right != NULL)
			{
				n = n->right;
				continue;
			}
			if (m != NULL)
			{
				n = m;
			} else
			{
				m = n;
				n = NULL;
			}
		}
		break;
	}

	if (n != NULL)
		anchor->lookaside = n;
	else if (m != NULL)
		anchor->lookaside = m;

	return n;
}

/*
 * Should any characters in a specified range be highlighted?
 */
	static int
is_hilited_range(pos, epos)
	POSITION pos;
	POSITION epos;
{
	struct hilite_node *n = hlist_find(&hilite_anchor, pos);
	return (n != NULL && (epos == NULL_POSITION || epos > n->r.hl_startpos));
}

/* 
 * Is a line "filtered" -- that is, should it be hidden?
 */
	public int
is_filtered(pos)
	POSITION pos;
{
	struct hilite_node *n;

	if (ch_getflags() & CH_HELPFILE)
		return (0);

	n = hlist_find(&filter_anchor, pos);
	return (n != NULL && pos >= n->r.hl_startpos);
}

/*
 * If pos is hidden, return the next position which isn't, otherwise
 * just return pos.
 */
	public POSITION
next_unfiltered(pos)
	POSITION pos;
{
	struct hilite_node *n;

	if (ch_getflags() & CH_HELPFILE)
		return (pos);

	n = hlist_find(&filter_anchor, pos);
	while (n != NULL && pos >= n->r.hl_startpos)
	{
		pos = n->r.hl_endpos;
		n = n->next;
	}
	return (pos);
}

/*
 * If pos is hidden, return the previous position which isn't or 0 if
 * we're filtered right to the beginning, otherwise just return pos.
 */
	public POSITION
prev_unfiltered(pos)
	POSITION pos;
{
	struct hilite_node *n;

	if (ch_getflags() & CH_HELPFILE)
		return (pos);

	n = hlist_find(&filter_anchor, pos);
	while (n != NULL && pos >= n->r.hl_startpos)
	{
		pos = n->r.hl_startpos;
		if (pos == 0)
			break;
		pos--;
		n = n->prev;
	}
	return (pos);
}


/*
 * Should any characters in a specified range be highlighted?
 * If nohide is nonzero, don't consider hide_hilite.
 */
	public int
is_hilited(pos, epos, nohide, p_matches)
	POSITION pos;
	POSITION epos;
	int nohide;
	int *p_matches;
{
	int match;

	if (p_matches != NULL)
		*p_matches = 0;

	if (!status_col &&
	    start_attnpos != NULL_POSITION && 
	    pos < end_attnpos &&
	     (epos == NULL_POSITION || epos > start_attnpos))
		/*
		 * The attn line overlaps this range.
		 */
		return (1);

	match = is_hilited_range(pos, epos);
	if (!match)
		return (0);

	if (p_matches == NULL)
		/*
		 * Kinda kludgy way to recognize that caller is checking for
		 * hilite in status column. In this case we want to return
		 * hilite status even if hiliting is disabled or hidden.
		 */
		return (1);

	/*
	 * Report matches, even if we're hiding highlights.
	 */
	*p_matches = 1;

	if (hilite_search == 0)
		/*
		 * Not doing highlighting.
		 */
		return (0);

	if (!nohide && hide_hilite)
		/*
		 * Highlighting is hidden.
		 */
		return (0);

	return (1);
}

/*
 * Tree node storage: get the current block of nodes if it has spare
 * capacity, or create a new one if not.
 */
	static struct hilite_storage*
hlist_getstorage(anchor)
	struct hilite_tree *anchor;
{
	int capacity = 1;
	struct hilite_storage *s;

	if (anchor->current)
	{
		if (anchor->current->used < anchor->current->capacity)
			return anchor->current;
		capacity = anchor->current->capacity * 2;
	}

	s = (struct hilite_storage *) ecalloc(1, sizeof(struct hilite_storage));
	s->nodes = (struct hilite_node *) ecalloc(capacity, sizeof(struct hilite_node));
	s->capacity = capacity;
	s->used = 0;
	s->next = NULL;
	if (anchor->current)
		anchor->current->next = s;
	else
		anchor->first = s;
	anchor->current = s;
	return s;
}

/*
 * Tree node storage: retrieve a new empty node to be inserted into the
 * tree.
 */
	static struct hilite_node*
hlist_getnode(anchor)
	struct hilite_tree *anchor;
{
	struct hilite_storage *s = hlist_getstorage(anchor);
	return &s->nodes[s->used++];
}

/*
 * Rotate the tree left around a pivot node.
 */
	static void
hlist_rotate_left(anchor, n)
	struct hilite_tree *anchor;
	struct hilite_node *n;
{
	struct hilite_node *np = n->parent;
	struct hilite_node *nr = n->right;
	struct hilite_node *nrl = n->right->left;

	if (np != NULL)
	{
		if (n == np->left)
			np->left = nr;
		else
			np->right = nr;
	} else
	{
		anchor->root = nr;
	}
	nr->left = n;
	n->right = nrl;

	nr->parent = np;
	n->parent = nr;
	if (nrl != NULL)
		nrl->parent = n;
}

/*
 * Rotate the tree right around a pivot node.
 */
	static void
hlist_rotate_right(anchor, n)
	struct hilite_tree *anchor;
	struct hilite_node *n;
{
	struct hilite_node *np = n->parent;
	struct hilite_node *nl = n->left;
	struct hilite_node *nlr = n->left->right;

	if (np != NULL)
	{
		if (n == np->right)
			np->right = nl;
		else
			np->left = nl;
	} else
	{
		anchor->root = nl;
	}
	nl->right = n;
	n->left = nlr;

	nl->parent = np;
	n->parent = nl;
	if (nlr != NULL)
		nlr->parent = n;
}


/*
 * Add a new hilite to a hilite list.
 */
	static void
add_hilite(anchor, hl)
	struct hilite_tree *anchor;
	struct hilite *hl;
{
	struct hilite_node *p, *n, *u;

	/* Ignore empty ranges. */
	if (hl->hl_startpos >= hl->hl_endpos)
		return;

	p = anchor->root;

	/* Inserting the very first node is trivial. */
	if (p == NULL)
	{
		n = hlist_getnode(anchor);
		n->r = *hl;
		anchor->root = n;
		anchor->lookaside = n;
		return;
	}

	/*
	 * Find our insertion point. If we come across any overlapping
	 * or adjoining existing ranges, shrink our range and discard
	 * if it become empty.
	 */
	for (;;)
	{
		if (hl->hl_startpos < p->r.hl_startpos)
		{
			if (hl->hl_endpos > p->r.hl_startpos)
				hl->hl_endpos = p->r.hl_startpos;
			if (p->left != NULL)
			{
				p = p->left;
				continue;
			}
			break;
		}
		if (hl->hl_startpos < p->r.hl_endpos) {
			hl->hl_startpos = p->r.hl_endpos;
			if (hl->hl_startpos >= hl->hl_endpos)
				return;
		}
		if (p->right != NULL)
		{
			p = p->right;
			continue;
		}
		break;
	}

	/*
	 * Now we're at the right leaf, again check for contiguous ranges
	 * and extend the existing node if possible to avoid the
	 * insertion. Otherwise insert a new node at the leaf.
	 */
	if (hl->hl_startpos < p->r.hl_startpos) {
		if (hl->hl_endpos == p->r.hl_startpos)
		{
			p->r.hl_startpos = hl->hl_startpos;
			return;
		}
		if (p->prev != NULL && p->prev->r.hl_endpos == hl->hl_startpos)
		{
			p->prev->r.hl_endpos = hl->hl_endpos;
			return;
		}

		p->left = n = hlist_getnode(anchor);
		n->next = p;
		if (p->prev != NULL)
		{
			n->prev = p->prev;
			p->prev->next = n;
		}
		p->prev = n;
	} else {
		if (p->r.hl_endpos == hl->hl_startpos)
		{
			p->r.hl_endpos = hl->hl_endpos;
			return;
		}
		if (p->next != NULL && hl->hl_endpos == p->next->r.hl_startpos) {
			p->next->r.hl_startpos = hl->hl_startpos;
			return;
		}

		p->right = n = hlist_getnode(anchor);
		n->prev = p;
		if (p->next != NULL)
		{
			n->next = p->next;
			p->next->prev = n;
		}
		p->next = n;
	}
	n->parent = p;
	n->red = 1;
	n->r = *hl;

	/*
	 * The tree is in the correct order and covers the right ranges
	 * now, but may have become unbalanced. Rebalance it using the
	 * standard red-black tree constraints and operations.
	 */
	for (;;)
	{
		/* case 1 - current is root, root is always black */
		if (n->parent == NULL)
		{
			n->red = 0;
			break;
		}

		/* case 2 - parent is black, we can always be red */
		if (!n->parent->red)
			break;

		/*
		 * constraint: because the root must be black, if our
		 * parent is red it cannot be the root therefore we must
		 * have a grandparent
		 */

		/*
		 * case 3 - parent and uncle are red, repaint them black,
		 * the grandparent red, and start again at the grandparent.
		 */
		u = n->parent->parent->left;
		if (n->parent == u) 
			u = n->parent->parent->right;
		if (u != NULL && u->red)
		{
			n->parent->red = 0;
			u->red = 0;
			n = n->parent->parent;
			n->red = 1;
			continue;
		}

		/*
		 * case 4 - parent is red but uncle is black, parent and
		 * grandparent on opposite sides. We need to start
		 * changing the structure now. This and case 5 will shorten
		 * our branch and lengthen the sibling, between them
		 * restoring balance.
		 */
		if (n == n->parent->right &&
		    n->parent == n->parent->parent->left)
		{
			hlist_rotate_left(anchor, n->parent);
			n = n->left;
		} else if (n == n->parent->left &&
			   n->parent == n->parent->parent->right)
		{
			hlist_rotate_right(anchor, n->parent);
			n = n->right;
		}

		/*
		 * case 5 - parent is red but uncle is black, parent and
		 * grandparent on same side
		 */
		n->parent->red = 0;
		n->parent->parent->red = 1;
		if (n == n->parent->left)
			hlist_rotate_right(anchor, n->parent->parent);
		else
			hlist_rotate_left(anchor, n->parent->parent);
		break;
	}
}

/*
 * Hilight every character in a range of displayed characters.
 */
	static void
create_hilites(linepos, start_index, end_index, chpos)
	POSITION linepos;
	int start_index;
	int end_index;
	int *chpos;
{
	struct hilite hl;
	int i;

	/* Start the first hilite. */
	hl.hl_startpos = linepos + chpos[start_index];

	/*
	 * Step through the displayed chars.
	 * If the source position (before cvt) of the char is one more
	 * than the source pos of the previous char (the usual case),
	 * just increase the size of the current hilite by one.
	 * Otherwise (there are backspaces or something involved),
	 * finish the current hilite and start a new one.
	 */
	for (i = start_index+1;  i <= end_index;  i++)
	{
		if (chpos[i] != chpos[i-1] + 1 || i == end_index)
		{
			hl.hl_endpos = linepos + chpos[i-1] + 1;
			add_hilite(&hilite_anchor, &hl);
			/* Start new hilite unless this is the last char. */
			if (i < end_index)
			{
				hl.hl_startpos = linepos + chpos[i];
			}
		}
	}
}

/*
 * Make a hilite for each string in a physical line which matches 
 * the current pattern.
 * sp,ep delimit the first match already found.
 */
	static void
hilite_line(linepos, line, line_len, chpos, sp, ep, cvt_ops)
	POSITION linepos;
	char *line;
	int line_len;
	int *chpos;
	char *sp;
	char *ep;
	int cvt_ops;
{
	char *searchp;
	char *line_end = line + line_len;

	/*
	 * sp and ep delimit the first match in the line.
	 * Mark the corresponding file positions, then
	 * look for further matches and mark them.
	 * {{ This technique, of calling match_pattern on subsequent
	 *    substrings of the line, may mark more than is correct
	 *    if the pattern starts with "^".  This bug is fixed
	 *    for those regex functions that accept a notbol parameter
	 *    (currently POSIX, PCRE and V8-with-regexec2). }}
	 */
	searchp = line;
	do {
		if (sp == NULL || ep == NULL)
			return;
		create_hilites(linepos, sp-line, ep-line, chpos);
		/*
		 * If we matched more than zero characters,
		 * move to the first char after the string we matched.
		 * If we matched zero, just move to the next char.
		 */
		if (ep > searchp)
			searchp = ep;
		else if (searchp != line_end)
			searchp++;
		else /* end of line */
			break;
	} while (match_pattern(info_compiled(&search_info), search_info.text,
			searchp, line_end - searchp, &sp, &ep, 1, search_info.search_type));
}
#endif

#if HILITE_SEARCH
/*
 * Find matching text which is currently on screen and highlight it.
 */
	static void
hilite_screen()
{
	struct scrpos scrpos;

	get_scrpos(&scrpos, TOP);
	if (scrpos.pos == NULL_POSITION)
		return;
	prep_hilite(scrpos.pos, position(BOTTOM_PLUS_ONE), -1);
	repaint_hilite(1);
}

/*
 * Change highlighting parameters.
 */
	public void
chg_hilite()
{
	/*
	 * Erase any highlights currently on screen.
	 */
	clr_hilite();
	hide_hilite = 0;

	if (hilite_search == OPT_ONPLUS)
		/*
		 * Display highlights.
		 */
		hilite_screen();
}
#endif

/*
 * Figure out where to start a search.
 */
	static POSITION
search_pos(search_type)
	int search_type;
{
	POSITION pos;
	int sindex;

	if (empty_screen())
	{
		/*
		 * Start at the beginning (or end) of the file.
		 * The empty_screen() case is mainly for 
		 * command line initiated searches;
		 * for example, "+/xyz" on the command line.
		 * Also for multi-file (SRCH_PAST_EOF) searches.
		 */
		if (search_type & SRCH_FORW)
		{
			pos = ch_zero();
		} else
		{
			pos = ch_length();
			if (pos == NULL_POSITION)
			{
				(void) ch_end_seek();
				pos = ch_length();
			}
		}
		sindex = 0;
	} else 
	{
		int add_one = 0;

		if (how_search == OPT_ON)
		{
			/*
			 * Search does not include current screen.
			 */
			if (search_type & SRCH_FORW)
				sindex = sc_height-1; /* BOTTOM_PLUS_ONE */
			else
				sindex = 0; /* TOP */
		} else if (how_search == OPT_ONPLUS && !(search_type & SRCH_AFTER_TARGET))
		{
			/*
			 * Search includes all of displayed screen.
			 */
			if (search_type & SRCH_FORW)
				sindex = 0; /* TOP */
			else
				sindex = sc_height-1; /* BOTTOM_PLUS_ONE */
		} else 
		{
			/*
			 * Search includes the part of current screen beyond the jump target.
			 * It starts at the jump target (if searching backwards),
			 * or at the jump target plus one (if forwards).
			 */
			sindex = sindex_from_sline(jump_sline);
			if (search_type & SRCH_FORW) 
				add_one = 1;
		}
		pos = position(sindex);
		if (add_one)
			pos = forw_raw_line(pos, (char **)NULL, (int *)NULL);
	}

	/*
	 * If the line is empty, look around for a plausible starting place.
	 */
	if (search_type & SRCH_FORW) 
	{
		while (pos == NULL_POSITION)
		{
			if (++sindex >= sc_height)
				break;
			pos = position(sindex);
		}
	} else 
	{
		while (pos == NULL_POSITION)
		{
			if (--sindex < 0)
				break;
			pos = position(sindex);
		}
	}
	return (pos);
}

/*
 * Search a subset of the file, specified by start/end position.
 */
	static int
search_range(pos, endpos, search_type, matches, maxlines, plinepos, pendpos)
	POSITION pos;
	POSITION endpos;
	int search_type;
	int matches;
	int maxlines;
	POSITION *plinepos;
	POSITION *pendpos;
{
	char *line;
	char *cline;
	int line_len;
	LINENUM linenum;
	char *sp, *ep;
	int line_match;
	int cvt_ops;
	int cvt_len;
	int *chpos;
	POSITION linepos, oldpos;

	linenum = find_linenum(pos);
	oldpos = pos;
	for (;;)
	{
		/*
		 * Get lines until we find a matching one or until
		 * we hit end-of-file (or beginning-of-file if we're 
		 * going backwards), or until we hit the end position.
		 */
		if (ABORT_SIGS())
		{
			/*
			 * A signal aborts the search.
			 */
			return (-1);
		}

		if ((endpos != NULL_POSITION && pos >= endpos) || maxlines == 0)
		{
			/*
			 * Reached end position without a match.
			 */
			if (pendpos != NULL)
				*pendpos = pos;
			return (matches);
		}
		if (maxlines > 0)
			maxlines--;

		if (search_type & SRCH_FORW)
		{
			/*
			 * Read the next line, and save the 
			 * starting position of that line in linepos.
			 */
			linepos = pos;
			pos = forw_raw_line(pos, &line, &line_len);
			if (linenum != 0)
				linenum++;
		} else
		{
			/*
			 * Read the previous line and save the
			 * starting position of that line in linepos.
			 */
			pos = back_raw_line(pos, &line, &line_len);
			linepos = pos;
			if (linenum != 0)
				linenum--;
		}

		if (pos == NULL_POSITION)
		{
			/*
			 * Reached EOF/BOF without a match.
			 */
			if (pendpos != NULL)
				*pendpos = oldpos;
			return (matches);
		}

		/*
		 * If we're using line numbers, we might as well
		 * remember the information we have now (the position
		 * and line number of the current line).
		 * Don't do it for every line because it slows down
		 * the search.  Remember the line number only if
		 * we're "far" from the last place we remembered it.
		 */
		if (linenums && abs((int)(pos - oldpos)) > 2048)
			add_lnum(linenum, pos);
		oldpos = pos;

		if (is_filtered(linepos))
			continue;

		/*
		 * If it's a caseless search, convert the line to lowercase.
		 * If we're doing backspace processing, delete backspaces.
		 */
		cvt_ops = get_cvt_ops();
		cvt_len = cvt_length(line_len, cvt_ops);
		cline = (char *) ecalloc(1, cvt_len);
		chpos = cvt_alloc_chpos(cvt_len);
		cvt_text(cline, line, chpos, &line_len, cvt_ops);

#if HILITE_SEARCH
		/*
		 * Check to see if the line matches the filter pattern.
		 * If so, add an entry to the filter list.
		 */
		if (((search_type & SRCH_FIND_ALL) ||
		     prep_startpos == NULL_POSITION ||
		     linepos < prep_startpos || linepos >= prep_endpos) &&
		    prev_pattern(&filter_info)) {
			int line_filter = match_pattern(info_compiled(&filter_info), filter_info.text,
				cline, line_len, &sp, &ep, 0, filter_info.search_type);
			if (line_filter)
			{
				struct hilite hl;
				hl.hl_startpos = linepos;
				hl.hl_endpos = pos;
				add_hilite(&filter_anchor, &hl);
				free(cline);
				free(chpos);
				continue;
			}
		}
#endif

		/*
		 * Test the next line to see if we have a match.
		 * We are successful if we either want a match and got one,
		 * or if we want a non-match and got one.
		 */
		if (prev_pattern(&search_info))
		{
			line_match = match_pattern(info_compiled(&search_info), search_info.text,
				cline, line_len, &sp, &ep, 0, search_type);
			if (line_match)
			{
				/*
				 * Got a match.
				 */
				if (search_type & SRCH_FIND_ALL)
				{
#if HILITE_SEARCH
					/*
					 * We are supposed to find all matches in the range.
					 * Just add the matches in this line to the 
					 * hilite list and keep searching.
					 */
					hilite_line(linepos, cline, line_len, chpos, sp, ep, cvt_ops);
#endif
				} else if (--matches <= 0)
				{
					/*
					 * Found the one match we're looking for.
					 * Return it.
					 */
#if HILITE_SEARCH
					if (hilite_search == OPT_ON)
					{
						/*
						 * Clear the hilite list and add only
						 * the matches in this one line.
						 */
						clr_hilite();
						hilite_line(linepos, cline, line_len, chpos, sp, ep, cvt_ops);
					}
#endif
					free(cline);
					free(chpos);
					if (plinepos != NULL)
						*plinepos = linepos;
					return (0);
				}
			}
		}
		free(cline);
		free(chpos);
	}
}

/*
 * search for a pattern in history. If found, compile that pattern.
 */
	static int 
hist_pattern(search_type) 
	int search_type;
{
#if CMD_HISTORY
	char *pattern;

	set_mlist(ml_search, 0);
	pattern = cmd_lastpattern();
	if (pattern == NULL)
		return (0);

	if (set_pattern(&search_info, pattern, search_type) < 0)
		return (0);

#if HILITE_SEARCH
	if (hilite_search == OPT_ONPLUS && !hide_hilite)
		hilite_screen();
#endif

	return (1);
#else /* CMD_HISTORY */
	return (0);
#endif /* CMD_HISTORY */
}

/*
 * Change the caseless-ness of searches.  
 * Updates the internal search state to reflect a change in the -i flag.
 */
	public void
chg_caseless()
{
	if (!is_ucase_pattern)
		/*
		 * Pattern did not have uppercase.
		 * Just set the search caselessness to the global caselessness.
		 */
		is_caseless = caseless;
	else
	{
		/*
		 * Pattern did have uppercase.
		 * Regenerate the pattern using the new state.
		 */
		clear_pattern(&search_info);
		hist_pattern(search_info.search_type);
	}
}

/*
 * Search for the n-th occurrence of a specified pattern, 
 * either forward or backward.
 * Return the number of matches not yet found in this file
 * (that is, n minus the number of matches found).
 * Return -1 if the search should be aborted.
 * Caller may continue the search in another file 
 * if less than n matches are found in this file.
 */
	public int
search(search_type, pattern, n)
	int search_type;
	char *pattern;
	int n;
{
	POSITION pos;

	if (pattern == NULL || *pattern == '\0')
	{
		/*
		 * A null pattern means use the previously compiled pattern.
		 */
		search_type |= SRCH_AFTER_TARGET;
		if (!prev_pattern(&search_info) && !hist_pattern(search_type))
		{
			error("No previous regular expression", NULL_PARG);
			return (-1);
		}
		if ((search_type & SRCH_NO_REGEX) != 
		      (search_info.search_type & SRCH_NO_REGEX))
		{
			error("Please re-enter search pattern", NULL_PARG);
			return -1;
		}
#if HILITE_SEARCH
		if (hilite_search == OPT_ON || status_col)
		{
			/*
			 * Erase the highlights currently on screen.
			 * If the search fails, we'll redisplay them later.
			 */
			repaint_hilite(0);
		}
		if (hilite_search == OPT_ONPLUS && hide_hilite)
		{
			/*
			 * Highlight any matches currently on screen,
			 * before we actually start the search.
			 */
			hide_hilite = 0;
			hilite_screen();
		}
		hide_hilite = 0;
#endif
	} else
	{
		/*
		 * Compile the pattern.
		 */
		if (set_pattern(&search_info, pattern, search_type) < 0)
			return (-1);
#if HILITE_SEARCH
		if (hilite_search || status_col)
		{
			/*
			 * Erase the highlights currently on screen.
			 * Also permanently delete them from the hilite list.
			 */
			repaint_hilite(0);
			hide_hilite = 0;
			clr_hilite();
		}
		if (hilite_search == OPT_ONPLUS || status_col)
		{
			/*
			 * Highlight any matches currently on screen,
			 * before we actually start the search.
			 */
			hilite_screen();
		}
#endif
	}

	/*
	 * Figure out where to start the search.
	 */
	pos = search_pos(search_type);
	if (pos == NULL_POSITION)
	{
		/*
		 * Can't find anyplace to start searching from.
		 */
		if (search_type & SRCH_PAST_EOF)
			return (n);
		if (hilite_search == OPT_ON || status_col)
			repaint_hilite(1);
		error("Nothing to search", NULL_PARG);
		return (-1);
	}

	n = search_range(pos, NULL_POSITION, search_type, n, -1,
			&pos, (POSITION*)NULL);
	if (n != 0)
	{
		/*
		 * Search was unsuccessful.
		 */
#if HILITE_SEARCH
		if ((hilite_search == OPT_ON || status_col) && n > 0)
			/*
			 * Redisplay old hilites.
			 */
			repaint_hilite(1);
#endif
		return (n);
	}

	if (!(search_type & SRCH_NO_MOVE))
	{
		/*
		 * Go to the matching line.
		 */
		jump_loc(pos, jump_sline);
	}

#if HILITE_SEARCH
	if (hilite_search == OPT_ON || status_col)
		/*
		 * Display new hilites in the matching line.
		 */
		repaint_hilite(1);
#endif
	return (0);
}


#if HILITE_SEARCH
/*
 * Prepare hilites in a given range of the file.
 *
 * The pair (prep_startpos,prep_endpos) delimits a contiguous region
 * of the file that has been "prepared"; that is, scanned for matches for
 * the current search pattern, and hilites have been created for such matches.
 * If prep_startpos == NULL_POSITION, the prep region is empty.
 * If prep_endpos == NULL_POSITION, the prep region extends to EOF.
 * prep_hilite asks that the range (spos,epos) be covered by the prep region.
 */
	public void
prep_hilite(spos, epos, maxlines)
	POSITION spos;
	POSITION epos;
	int maxlines;
{
	POSITION nprep_startpos = prep_startpos;
	POSITION nprep_endpos = prep_endpos;
	POSITION new_epos;
	POSITION max_epos;
	int result;
	int i;

/*
 * Search beyond where we're asked to search, so the prep region covers
 * more than we need.  Do one big search instead of a bunch of small ones.
 */
#define	SEARCH_MORE (3*size_linebuf)

	if (!prev_pattern(&search_info) && !is_filtering())
		return;

	/*
	 * Make sure our prep region always starts at the beginning of
	 * a line. (search_range takes care of the end boundary below.)
	 */
	spos = back_raw_line(spos+1, (char **)NULL, (int *)NULL);

	/*
	 * If we're limited to a max number of lines, figure out the
	 * file position we should stop at.
	 */
	if (maxlines < 0)
		max_epos = NULL_POSITION;
	else
	{
		max_epos = spos;
		for (i = 0;  i < maxlines;  i++)
			max_epos = forw_raw_line(max_epos, (char **)NULL, (int *)NULL);
	}

	/*
	 * Find two ranges:
	 * The range that we need to search (spos,epos); and the range that
	 * the "prep" region will then cover (nprep_startpos,nprep_endpos).
	 */

	if (prep_startpos == NULL_POSITION ||
	    (epos != NULL_POSITION && epos < prep_startpos) ||
	    spos > prep_endpos)
	{
		/*
		 * New range is not contiguous with old prep region.
		 * Discard the old prep region and start a new one.
		 */
		clr_hilite();
		clr_filter();
		if (epos != NULL_POSITION)
			epos += SEARCH_MORE;
		nprep_startpos = spos;
	} else
	{
		/*
		 * New range partially or completely overlaps old prep region.
		 */
		if (epos == NULL_POSITION)
		{
			/*
			 * New range goes to end of file.
			 */
			;
		} else if (epos > prep_endpos)
		{
			/*
			 * New range ends after old prep region.
			 * Extend prep region to end at end of new range.
			 */
			epos += SEARCH_MORE;
		} else /* (epos <= prep_endpos) */
		{
			/*
			 * New range ends within old prep region.
			 * Truncate search to end at start of old prep region.
			 */
			epos = prep_startpos;
		}

		if (spos < prep_startpos)
		{
			/*
			 * New range starts before old prep region.
			 * Extend old prep region backwards to start at 
			 * start of new range.
			 */
			if (spos < SEARCH_MORE)
				spos = 0;
			else
				spos -= SEARCH_MORE;
			nprep_startpos = spos;
		} else /* (spos >= prep_startpos) */
		{
			/*
			 * New range starts within or after old prep region.
			 * Trim search to start at end of old prep region.
			 */
			spos = prep_endpos;
		}
	}

	if (epos != NULL_POSITION && max_epos != NULL_POSITION &&
	    epos > max_epos)
		/*
		 * Don't go past the max position we're allowed.
		 */
		epos = max_epos;

	if (epos == NULL_POSITION || epos > spos)
	{
		int search_type = SRCH_FORW | SRCH_FIND_ALL;
		search_type |= (search_info.search_type & SRCH_NO_REGEX);
		for (;;) 
		{
			result = search_range(spos, epos, search_type, 0, maxlines, (POSITION*)NULL, &new_epos);
			if (result < 0)
				return;
			if (prep_endpos == NULL_POSITION || new_epos > prep_endpos)
				nprep_endpos = new_epos;

			/*
			 * Check both ends of the resulting prep region to
			 * make sure they're not filtered. If they are,
			 * keep going at least one more line until we find
			 * something that isn't filtered, or hit the end.
			 */
			if (prep_endpos == NULL_POSITION || nprep_endpos > prep_endpos)
			{
				if (new_epos >= nprep_endpos && is_filtered(new_epos-1))
				{
					spos = nprep_endpos;
					epos = forw_raw_line(nprep_endpos, (char **)NULL, (int *)NULL);
					if (epos == NULL_POSITION)
						break;
					maxlines = 1;
					continue;
				}
			}

			if (prep_startpos == NULL_POSITION || nprep_startpos < prep_startpos)
			{
				if (nprep_startpos > 0 && is_filtered(nprep_startpos))
				{
					epos = nprep_startpos;
					spos = back_raw_line(nprep_startpos, (char **)NULL, (int *)NULL);
					if (spos == NULL_POSITION)
						break;
					nprep_startpos = spos;
					maxlines = 1;
					continue;
				}
			}
			break;
		}
	}
	prep_startpos = nprep_startpos;
	prep_endpos = nprep_endpos;
}

/*
 * Set the pattern to be used for line filtering.
 */
	public void
set_filter_pattern(pattern, search_type)
	char *pattern;
	int search_type;
{
	clr_filter();
	if (pattern == NULL || *pattern == '\0')
		clear_pattern(&filter_info);
	else
		set_pattern(&filter_info, pattern, search_type);
	screen_trashed = 1;
}

/*
 * Is there a line filter in effect?
 */
	public int
is_filtering()
{
	if (ch_getflags() & CH_HELPFILE)
		return (0);
	return prev_pattern(&filter_info);
}
#endif

#if HAVE_V8_REGCOMP
/*
 * This function is called by the V8 regcomp to report 
 * errors in regular expressions.
 */
public int reg_show_error = 1;

	void 
regerror(s) 
	char *s; 
{
	PARG parg;

	if (!reg_show_error)
		return;
	parg.p_string = s;
	error("%s", &parg);
}
#endif

