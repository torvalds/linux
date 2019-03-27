/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	parse_entry.c -- compile one terminfo or termcap entry
 *
 *	Get an exact in-core representation of an entry.  Don't
 *	try to resolve use or tc capabilities, that is someone
 *	else's job.  Depends on the lexical analyzer to get tokens
 *	from the input stream.
 */

#define __INTERNAL_CAPS_VISIBLE
#include <curses.priv.h>

#include <ctype.h>
#include <tic.h>

MODULE_ID("$Id: parse_entry.c,v 1.79 2012/10/27 21:43:45 tom Exp $")

#ifdef LINT
static short const parametrized[] =
{0};
#else
#include <parametrized.h>
#endif

static void postprocess_termcap(TERMTYPE *, bool);
static void postprocess_terminfo(TERMTYPE *);
static struct name_table_entry const *lookup_fullname(const char *name);

#if NCURSES_XNAMES

static struct name_table_entry const *
_nc_extend_names(ENTRY * entryp, char *name, int token_type)
{
    static struct name_table_entry temp;
    TERMTYPE *tp = &(entryp->tterm);
    unsigned offset = 0;
    unsigned actual;
    unsigned tindex;
    unsigned first, last, n;
    bool found;

    switch (token_type) {
    case BOOLEAN:
	first = 0;
	last = tp->ext_Booleans;
	offset = tp->ext_Booleans;
	tindex = tp->num_Booleans;
	break;
    case NUMBER:
	first = tp->ext_Booleans;
	last = tp->ext_Numbers + first;
	offset = (unsigned) (tp->ext_Booleans + tp->ext_Numbers);
	tindex = tp->num_Numbers;
	break;
    case STRING:
	first = (unsigned) (tp->ext_Booleans + tp->ext_Numbers);
	last = tp->ext_Strings + first;
	offset = (unsigned) (tp->ext_Booleans + tp->ext_Numbers + tp->ext_Strings);
	tindex = tp->num_Strings;
	break;
    case CANCEL:
	actual = NUM_EXT_NAMES(tp);
	for (n = 0; n < actual; n++) {
	    if (!strcmp(name, tp->ext_Names[n])) {
		if (n > (unsigned) (tp->ext_Booleans + tp->ext_Numbers)) {
		    token_type = STRING;
		} else if (n > tp->ext_Booleans) {
		    token_type = NUMBER;
		} else {
		    token_type = BOOLEAN;
		}
		return _nc_extend_names(entryp, name, token_type);
	    }
	}
	/* Well, we are given a cancel for a name that we don't recognize */
	return _nc_extend_names(entryp, name, STRING);
    default:
	return 0;
    }

    /* Adjust the 'offset' (insertion-point) to keep the lists of extended
     * names sorted.
     */
    for (n = first, found = FALSE; n < last; n++) {
	int cmp = strcmp(tp->ext_Names[n], name);
	if (cmp == 0)
	    found = TRUE;
	if (cmp >= 0) {
	    offset = n;
	    tindex = n - first;
	    switch (token_type) {
	    case BOOLEAN:
		tindex += BOOLCOUNT;
		break;
	    case NUMBER:
		tindex += NUMCOUNT;
		break;
	    case STRING:
		tindex += STRCOUNT;
		break;
	    }
	    break;
	}
    }

#define for_each_value(max) \
	for (last = (unsigned) (max - 1); last > tindex; last--)

    if (!found) {
	switch (token_type) {
	case BOOLEAN:
	    tp->ext_Booleans++;
	    tp->num_Booleans++;
	    TYPE_REALLOC(NCURSES_SBOOL, tp->num_Booleans, tp->Booleans);
	    for_each_value(tp->num_Booleans)
		tp->Booleans[last] = tp->Booleans[last - 1];
	    break;
	case NUMBER:
	    tp->ext_Numbers++;
	    tp->num_Numbers++;
	    TYPE_REALLOC(short, tp->num_Numbers, tp->Numbers);
	    for_each_value(tp->num_Numbers)
		tp->Numbers[last] = tp->Numbers[last - 1];
	    break;
	case STRING:
	    tp->ext_Strings++;
	    tp->num_Strings++;
	    TYPE_REALLOC(char *, tp->num_Strings, tp->Strings);
	    for_each_value(tp->num_Strings)
		tp->Strings[last] = tp->Strings[last - 1];
	    break;
	}
	actual = NUM_EXT_NAMES(tp);
	TYPE_REALLOC(char *, actual, tp->ext_Names);
	while (--actual > offset)
	    tp->ext_Names[actual] = tp->ext_Names[actual - 1];
	tp->ext_Names[offset] = _nc_save_str(name);
    }

    temp.nte_name = tp->ext_Names[offset];
    temp.nte_type = token_type;
    temp.nte_index = (short) tindex;
    temp.nte_link = -1;

    return &temp;
}
#endif /* NCURSES_XNAMES */

/*
 *	int
 *	_nc_parse_entry(entry, literal, silent)
 *
 *	Compile one entry.  Doesn't try to resolve use or tc capabilities.
 *
 *	found-forward-use = FALSE
 *	re-initialise internal arrays
 *	get_token();
 *	if the token was not a name in column 1, complain and die
 *	save names in entry's string table
 *	while (get_token() is not EOF and not NAMES)
 *	        check for existence and type-correctness
 *	        enter cap into structure
 *	        if STRING
 *	            save string in entry's string table
 *	push back token
 */

#define BAD_TC_USAGE if (!bad_tc_usage) \
 	{ bad_tc_usage = TRUE; \
	 _nc_warning("Legacy termcap allows only a trailing tc= clause"); }

#define MAX_NUMBER 0x7fff	/* positive shorts only */

NCURSES_EXPORT(int)
_nc_parse_entry(struct entry *entryp, int literal, bool silent)
{
    int token_type;
    struct name_table_entry const *entry_ptr;
    char *ptr, *base;
    bool bad_tc_usage = FALSE;

    token_type = _nc_get_token(silent);

    if (token_type == EOF)
	return (EOF);
    if (token_type != NAMES)
	_nc_err_abort("Entry does not start with terminal names in column one");

    _nc_init_entry(&entryp->tterm);

    entryp->cstart = _nc_comment_start;
    entryp->cend = _nc_comment_end;
    entryp->startline = _nc_start_line;
    DEBUG(2, ("Comment range is %ld to %ld", entryp->cstart, entryp->cend));

    /*
     * Strip off the 2-character termcap name, if present.  Originally termcap
     * used that as an indexing aid.  We can retain 2-character terminfo names,
     * but note that they would be lost if we translate to/from termcap.  This
     * feature is supposedly obsolete since "newer" BSD implementations do not
     * use it; however our reference for this feature is SunOS 4.x, which
     * implemented it.  Note that the resulting terminal type was never the
     * 2-character name, but was instead the first alias after that.
     */
    ptr = _nc_curr_token.tk_name;
    if (_nc_syntax == SYN_TERMCAP
#if NCURSES_XNAMES
	&& !_nc_user_definable
#endif
	) {
	if (ptr[2] == '|') {
	    ptr += 3;
	    _nc_curr_token.tk_name[2] = '\0';
	}
    }

    entryp->tterm.str_table = entryp->tterm.term_names = _nc_save_str(ptr);

    if (entryp->tterm.str_table == 0)
	return (ERR);

    DEBUG(1, ("Starting '%s'", ptr));

    /*
     * We do this because the one-token lookahead in the parse loop
     * results in the terminal type getting prematurely set to correspond
     * to that of the next entry.
     */
    _nc_set_type(_nc_first_name(entryp->tterm.term_names));

    /* check for overly-long names and aliases */
    for (base = entryp->tterm.term_names; (ptr = strchr(base, '|')) != 0;
	 base = ptr + 1) {
	if (ptr - base > MAX_ALIAS) {
	    _nc_warning("%s `%.*s' may be too long",
			(base == entryp->tterm.term_names)
			? "primary name"
			: "alias",
			(int) (ptr - base), base);
	}
    }

    entryp->nuses = 0;

    for (token_type = _nc_get_token(silent);
	 token_type != EOF && token_type != NAMES;
	 token_type = _nc_get_token(silent)) {
	bool is_use = (strcmp(_nc_curr_token.tk_name, "use") == 0);
	bool is_tc = !is_use && (strcmp(_nc_curr_token.tk_name, "tc") == 0);
	if (is_use || is_tc) {
	    entryp->uses[entryp->nuses].name = _nc_save_str(_nc_curr_token.tk_valstring);
	    entryp->uses[entryp->nuses].line = _nc_curr_line;
	    entryp->nuses++;
	    if (entryp->nuses > 1 && is_tc) {
		BAD_TC_USAGE
	    }
	} else {
	    /* normal token lookup */
	    entry_ptr = _nc_find_entry(_nc_curr_token.tk_name,
				       _nc_get_hash_table(_nc_syntax));

	    /*
	     * Our kluge to handle aliasing.  The reason it's done
	     * this ugly way, with a linear search, is so the hashing
	     * machinery doesn't have to be made really complicated
	     * (also we get better warnings this way).  No point in
	     * making this case fast, aliased caps aren't common now
	     * and will get rarer.
	     */
	    if (entry_ptr == NOTFOUND) {
		const struct alias *ap;

		if (_nc_syntax == SYN_TERMCAP) {
		    if (entryp->nuses != 0) {
			BAD_TC_USAGE
		    }
		    for (ap = _nc_get_alias_table(TRUE); ap->from; ap++)
			if (strcmp(ap->from, _nc_curr_token.tk_name) == 0) {
			    if (ap->to == (char *) 0) {
				_nc_warning("%s (%s termcap extension) ignored",
					    ap->from, ap->source);
				goto nexttok;
			    }

			    entry_ptr = _nc_find_entry(ap->to,
						       _nc_get_hash_table(TRUE));
			    if (entry_ptr && !silent)
				_nc_warning("%s (%s termcap extension) aliased to %s",
					    ap->from, ap->source, ap->to);
			    break;
			}
		} else {	/* if (_nc_syntax == SYN_TERMINFO) */
		    for (ap = _nc_get_alias_table(FALSE); ap->from; ap++)
			if (strcmp(ap->from, _nc_curr_token.tk_name) == 0) {
			    if (ap->to == (char *) 0) {
				_nc_warning("%s (%s terminfo extension) ignored",
					    ap->from, ap->source);
				goto nexttok;
			    }

			    entry_ptr = _nc_find_entry(ap->to,
						       _nc_get_hash_table(FALSE));
			    if (entry_ptr && !silent)
				_nc_warning("%s (%s terminfo extension) aliased to %s",
					    ap->from, ap->source, ap->to);
			    break;
			}

		    if (entry_ptr == NOTFOUND) {
			entry_ptr = lookup_fullname(_nc_curr_token.tk_name);
		    }
		}
	    }
#if NCURSES_XNAMES
	    /*
	     * If we have extended-names active, we will automatically
	     * define a name based on its context.
	     */
	    if (entry_ptr == NOTFOUND
		&& _nc_user_definable
		&& (entry_ptr = _nc_extend_names(entryp,
						 _nc_curr_token.tk_name,
						 token_type)) != 0) {
		if (_nc_tracing >= DEBUG_LEVEL(1))
		    _nc_warning("extended capability '%s'", _nc_curr_token.tk_name);
	    }
#endif /* NCURSES_XNAMES */

	    /* can't find this cap name, not even as an alias */
	    if (entry_ptr == NOTFOUND) {
		if (!silent)
		    _nc_warning("unknown capability '%s'",
				_nc_curr_token.tk_name);
		continue;
	    }

	    /* deal with bad type/value combinations. */
	    if (token_type != CANCEL && entry_ptr->nte_type != token_type) {
		/*
		 * Nasty special cases here handle situations in which type
		 * information can resolve name clashes.  Normal lookup
		 * finds the last instance in the capability table of a
		 * given name, regardless of type.  find_type_entry looks
		 * for a first matching instance with given type.  So as
		 * long as all ambiguous names occur in pairs of distinct
		 * type, this will do the job.
		 */

		if (token_type == NUMBER
		    && !strcmp("ma", _nc_curr_token.tk_name)) {
		    /* tell max_attributes from arrow_key_map */
		    entry_ptr = _nc_find_type_entry("ma", NUMBER,
						    _nc_syntax != 0);
		    assert(entry_ptr != 0);

		} else if (token_type == STRING
			   && !strcmp("MT", _nc_curr_token.tk_name)) {
		    /* map terminfo's string MT to MT */
		    entry_ptr = _nc_find_type_entry("MT", STRING,
						    _nc_syntax != 0);
		    assert(entry_ptr != 0);

		} else if (token_type == BOOLEAN
			   && entry_ptr->nte_type == STRING) {
		    /* treat strings without following "=" as empty strings */
		    token_type = STRING;
		} else {
		    /* we couldn't recover; skip this token */
		    if (!silent) {
			const char *type_name;
			switch (entry_ptr->nte_type) {
			case BOOLEAN:
			    type_name = "boolean";
			    break;
			case STRING:
			    type_name = "string";
			    break;
			case NUMBER:
			    type_name = "numeric";
			    break;
			default:
			    type_name = "unknown";
			    break;
			}
			_nc_warning("wrong type used for %s capability '%s'",
				    type_name, _nc_curr_token.tk_name);
		    }
		    continue;
		}
	    }

	    /* now we know that the type/value combination is OK */
	    switch (token_type) {
	    case CANCEL:
		switch (entry_ptr->nte_type) {
		case BOOLEAN:
		    entryp->tterm.Booleans[entry_ptr->nte_index] = CANCELLED_BOOLEAN;
		    break;

		case NUMBER:
		    entryp->tterm.Numbers[entry_ptr->nte_index] = CANCELLED_NUMERIC;
		    break;

		case STRING:
		    entryp->tterm.Strings[entry_ptr->nte_index] = CANCELLED_STRING;
		    break;
		}
		break;

	    case BOOLEAN:
		entryp->tterm.Booleans[entry_ptr->nte_index] = TRUE;
		break;

	    case NUMBER:
		if (_nc_curr_token.tk_valnumber > MAX_NUMBER) {
		    entryp->tterm.Numbers[entry_ptr->nte_index] = MAX_NUMBER;
		} else {
		    entryp->tterm.Numbers[entry_ptr->nte_index] =
			(short) _nc_curr_token.tk_valnumber;
		}
		break;

	    case STRING:
		ptr = _nc_curr_token.tk_valstring;
		if (_nc_syntax == SYN_TERMCAP)
		    ptr = _nc_captoinfo(_nc_curr_token.tk_name,
					ptr,
					parametrized[entry_ptr->nte_index]);
		entryp->tterm.Strings[entry_ptr->nte_index] = _nc_save_str(ptr);
		break;

	    default:
		if (!silent)
		    _nc_warning("unknown token type");
		_nc_panic_mode((char) ((_nc_syntax == SYN_TERMCAP) ? ':' : ','));
		continue;
	    }
	}			/* end else cur_token.name != "use" */
      nexttok:
	continue;		/* cannot have a label w/o statement */
    }				/* endwhile (not EOF and not NAMES) */

    _nc_push_token(token_type);
    _nc_set_type(_nc_first_name(entryp->tterm.term_names));

    /*
     * Try to deduce as much as possible from extension capabilities
     * (this includes obsolete BSD capabilities).  Sigh...it would be more
     * space-efficient to call this after use resolution, but it has
     * to be done before entry allocation is wrapped up.
     */
    if (!literal) {
	if (_nc_syntax == SYN_TERMCAP) {
	    bool has_base_entry = FALSE;
	    unsigned i;

	    /*
	     * Don't insert defaults if this is a `+' entry meant only
	     * for inclusion in other entries (not sure termcap ever
	     * had these, actually).
	     */
	    if (strchr(entryp->tterm.term_names, '+'))
		has_base_entry = TRUE;
	    else
		/*
		 * Otherwise, look for a base entry that will already
		 * have picked up defaults via translation.
		 */
		for (i = 0; i < entryp->nuses; i++)
		    if (!strchr((char *) entryp->uses[i].name, '+'))
			has_base_entry = TRUE;

	    postprocess_termcap(&entryp->tterm, has_base_entry);
	} else
	    postprocess_terminfo(&entryp->tterm);
    }
    _nc_wrap_entry(entryp, FALSE);

    return (OK);
}

NCURSES_EXPORT(int)
_nc_capcmp(const char *s, const char *t)
/* compare two string capabilities, stripping out padding */
{
    if (!VALID_STRING(s) && !VALID_STRING(t))
	return (0);
    else if (!VALID_STRING(s) || !VALID_STRING(t))
	return (1);

    for (;;) {
	if (s[0] == '$' && s[1] == '<') {
	    for (s += 2;; s++)
		if (!(isdigit(UChar(*s))
		      || *s == '.'
		      || *s == '*'
		      || *s == '/'
		      || *s == '>'))
		    break;
	}

	if (t[0] == '$' && t[1] == '<') {
	    for (t += 2;; t++)
		if (!(isdigit(UChar(*t))
		      || *t == '.'
		      || *t == '*'
		      || *t == '/'
		      || *t == '>'))
		    break;
	}

	/* we've now pushed s and t past any padding they were pointing at */

	if (*s == '\0' && *t == '\0')
	    return (0);

	if (*s != *t)
	    return (*t - *s);

	/* else *s == *t but one is not NUL, so continue */
	s++, t++;
    }
}

static void
append_acs0(string_desc * dst, int code, int src)
{
    if (src != 0) {
	char temp[3];
	temp[0] = (char) code;
	temp[1] = (char) src;
	temp[2] = 0;
	_nc_safe_strcat(dst, temp);
    }
}

static void
append_acs(string_desc * dst, int code, char *src)
{
    if (src != 0 && strlen(src) == 1) {
	append_acs0(dst, code, *src);
    }
}

/*
 * The ko capability, if present, consists of a comma-separated capability
 * list.  For each capability, we may assume there is a keycap that sends the
 * string which is the value of that capability.
 */
typedef struct {
    const char *from;
    const char *to;
} assoc;
static assoc const ko_xlate[] =
{
    {"al", "kil1"},		/* insert line key  -> KEY_IL    */
    {"bt", "kcbt"},		/* back tab         -> KEY_BTAB  */
    {"cd", "ked"},		/* clear-to-eos key -> KEY_EOL   */
    {"ce", "kel"},		/* clear-to-eol key -> KEY_EOS   */
    {"cl", "kclr"},		/* clear key        -> KEY_CLEAR */
    {"ct", "tbc"},		/* clear all tabs   -> KEY_CATAB */
    {"dc", "kdch1"},		/* delete char      -> KEY_DC    */
    {"dl", "kdl1"},		/* delete line      -> KEY_DL    */
    {"do", "kcud1"},		/* down key         -> KEY_DOWN  */
    {"ei", "krmir"},		/* exit insert key  -> KEY_EIC   */
    {"ho", "khome"},		/* home key         -> KEY_HOME  */
    {"ic", "kich1"},		/* insert char key  -> KEY_IC    */
    {"im", "kIC"},		/* insert-mode key  -> KEY_SIC   */
    {"le", "kcub1"},		/* le key           -> KEY_LEFT  */
    {"nd", "kcuf1"},		/* nd key           -> KEY_RIGHT */
    {"nl", "kent"},		/* new line key     -> KEY_ENTER */
    {"st", "khts"},		/* set-tab key      -> KEY_STAB  */
    {"ta", CANCELLED_STRING},
    {"up", "kcuu1"},		/* up-arrow key     -> KEY_UP    */
    {(char *) 0, (char *) 0},
};

/*
 * This routine fills in string caps that either had defaults under
 * termcap or can be manufactured from obsolete termcap capabilities.
 * It was lifted from Ross Ridge's mytinfo package.
 */

static const char C_CR[] = "\r";
static const char C_LF[] = "\n";
static const char C_BS[] = "\b";
static const char C_HT[] = "\t";

/*
 * Note that WANTED and PRESENT are not simple inverses!  If a capability
 * has been explicitly cancelled, it's not considered WANTED.
 */
#define WANTED(s)	((s) == ABSENT_STRING)
#define PRESENT(s)	(((s) != ABSENT_STRING) && ((s) != CANCELLED_STRING))

/*
 * This bit of legerdemain turns all the terminfo variable names into
 * references to locations in the arrays Booleans, Numbers, and Strings ---
 * precisely what's needed.
 */

#undef CUR
#define CUR tp->

static void
postprocess_termcap(TERMTYPE *tp, bool has_base)
{
    char buf[MAX_LINE * 2 + 2];
    string_desc result;

    /*
     * TERMCAP DEFAULTS AND OBSOLETE-CAPABILITY TRANSLATIONS
     *
     * This first part of the code is the functional inverse of the
     * fragment in capdefaults.c.
     * ----------------------------------------------------------------------
     */

    /* if there was a tc entry, assume we picked up defaults via that */
    if (!has_base) {
	if (WANTED(init_3string) && termcap_init2)
	    init_3string = _nc_save_str(termcap_init2);

	if (WANTED(reset_2string) && termcap_reset)
	    reset_2string = _nc_save_str(termcap_reset);

	if (WANTED(carriage_return)) {
	    if (carriage_return_delay > 0) {
		_nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
			    "%s$<%d>", C_CR, carriage_return_delay);
		carriage_return = _nc_save_str(buf);
	    } else
		carriage_return = _nc_save_str(C_CR);
	}
	if (WANTED(cursor_left)) {
	    if (backspace_delay > 0) {
		_nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
			    "%s$<%d>", C_BS, backspace_delay);
		cursor_left = _nc_save_str(buf);
	    } else if (backspaces_with_bs == 1)
		cursor_left = _nc_save_str(C_BS);
	    else if (PRESENT(backspace_if_not_bs))
		cursor_left = backspace_if_not_bs;
	}
	/* vi doesn't use "do", but it does seem to use nl (or '\n') instead */
	if (WANTED(cursor_down)) {
	    if (PRESENT(linefeed_if_not_lf))
		cursor_down = linefeed_if_not_lf;
	    else if (linefeed_is_newline != 1) {
		if (new_line_delay > 0) {
		    _nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
				"%s$<%d>", C_LF, new_line_delay);
		    cursor_down = _nc_save_str(buf);
		} else
		    cursor_down = _nc_save_str(C_LF);
	    }
	}
	if (WANTED(scroll_forward) && crt_no_scrolling != 1) {
	    if (PRESENT(linefeed_if_not_lf))
		cursor_down = linefeed_if_not_lf;
	    else if (linefeed_is_newline != 1) {
		if (new_line_delay > 0) {
		    _nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
				"%s$<%d>", C_LF, new_line_delay);
		    scroll_forward = _nc_save_str(buf);
		} else
		    scroll_forward = _nc_save_str(C_LF);
	    }
	}
	if (WANTED(newline)) {
	    if (linefeed_is_newline == 1) {
		if (new_line_delay > 0) {
		    _nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
				"%s$<%d>", C_LF, new_line_delay);
		    newline = _nc_save_str(buf);
		} else
		    newline = _nc_save_str(C_LF);
	    } else if (PRESENT(carriage_return) && PRESENT(scroll_forward)) {
		_nc_str_init(&result, buf, sizeof(buf));
		if (_nc_safe_strcat(&result, carriage_return)
		    && _nc_safe_strcat(&result, scroll_forward))
		    newline = _nc_save_str(buf);
	    } else if (PRESENT(carriage_return) && PRESENT(cursor_down)) {
		_nc_str_init(&result, buf, sizeof(buf));
		if (_nc_safe_strcat(&result, carriage_return)
		    && _nc_safe_strcat(&result, cursor_down))
		    newline = _nc_save_str(buf);
	    }
	}
    }

    /*
     * Inverse of capdefaults.c code ends here.
     * ----------------------------------------------------------------------
     *
     * TERMCAP-TO TERMINFO MAPPINGS FOR SOURCE TRANSLATION
     *
     * These translations will *not* be inverted by tgetent().
     */

    if (!has_base) {
	/*
	 * We wait until now to decide if we've got a working cr because even
	 * one that doesn't work can be used for newline. Unfortunately the
	 * space allocated for it is wasted.
	 */
	if (return_does_clr_eol == 1 || no_correctly_working_cr == 1)
	    carriage_return = ABSENT_STRING;

	/*
	 * Supposedly most termcap entries have ta now and '\t' is no longer a
	 * default, but it doesn't seem to be true...
	 */
	if (WANTED(tab)) {
	    if (horizontal_tab_delay > 0) {
		_nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
			    "%s$<%d>", C_HT, horizontal_tab_delay);
		tab = _nc_save_str(buf);
	    } else
		tab = _nc_save_str(C_HT);
	}
	if (init_tabs == ABSENT_NUMERIC && has_hardware_tabs == TRUE)
	    init_tabs = 8;

	/*
	 * Assume we can beep with ^G unless we're given bl@.
	 */
	if (WANTED(bell))
	    bell = _nc_save_str("\007");
    }

    /*
     * Translate the old termcap :pt: capability to it#8 + ht=\t
     */
    if (has_hardware_tabs == TRUE) {
	if (init_tabs != 8 && init_tabs != ABSENT_NUMERIC)
	    _nc_warning("hardware tabs with a width other than 8: %d", init_tabs);
	else {
	    if (tab && _nc_capcmp(tab, C_HT))
		_nc_warning("hardware tabs with a non-^I tab string %s",
			    _nc_visbuf(tab));
	    else {
		if (WANTED(tab))
		    tab = _nc_save_str(C_HT);
		init_tabs = 8;
	    }
	}
    }
    /*
     * Now translate the ko capability, if there is one.  This
     * isn't from mytinfo...
     */
    if (PRESENT(other_non_function_keys)) {
	char *base;
	char *bp, *cp, *dp;
	struct name_table_entry const *from_ptr;
	struct name_table_entry const *to_ptr;
	assoc const *ap;
	char buf2[MAX_TERMINFO_LENGTH];
	bool foundim;

	/* we're going to use this for a special case later */
	dp = strchr(other_non_function_keys, 'i');
	foundim = (dp != 0) && (dp[1] == 'm');

	/* look at each comma-separated capability in the ko string... */
	for (base = other_non_function_keys;
	     (cp = strchr(base, ',')) != 0;
	     base = cp + 1) {
	    size_t len = (unsigned) (cp - base);

	    for (ap = ko_xlate; ap->from; ap++) {
		if (len == strlen(ap->from)
		    && strncmp(ap->from, base, len) == 0)
		    break;
	    }
	    if (!(ap->from && ap->to)) {
		_nc_warning("unknown capability `%.*s' in ko string",
			    (int) len, base);
		continue;
	    } else if (ap->to == CANCELLED_STRING)	/* ignore it */
		continue;

	    /* now we know we found a match in ko_table, so... */

	    from_ptr = _nc_find_entry(ap->from, _nc_get_hash_table(TRUE));
	    to_ptr = _nc_find_entry(ap->to, _nc_get_hash_table(FALSE));

	    if (!from_ptr || !to_ptr)	/* should never happen! */
		_nc_err_abort("ko translation table is invalid, I give up");

	    if (WANTED(tp->Strings[from_ptr->nte_index])) {
		_nc_warning("no value for ko capability %s", ap->from);
		continue;
	    }

	    if (tp->Strings[to_ptr->nte_index]) {
		/* There's no point in warning about it if it's the same
		 * string; that's just an inefficiency.
		 */
		if (strcmp(
			      tp->Strings[from_ptr->nte_index],
			      tp->Strings[to_ptr->nte_index]) != 0)
		    _nc_warning("%s (%s) already has an explicit value %s, ignoring ko",
				ap->to, ap->from,
				_nc_visbuf(tp->Strings[to_ptr->nte_index]));
		continue;
	    }

	    /*
	     * The magic moment -- copy the mapped key string over,
	     * stripping out padding.
	     */
	    for (dp = buf2, bp = tp->Strings[from_ptr->nte_index]; *bp; bp++) {
		if (bp[0] == '$' && bp[1] == '<') {
		    while (*bp && *bp != '>') {
			++bp;
		    }
		} else
		    *dp++ = *bp;
	    }
	    *dp = '\0';

	    tp->Strings[to_ptr->nte_index] = _nc_save_str(buf2);
	}

	/*
	 * Note: ko=im and ko=ic both want to grab the `Insert'
	 * keycap.  There's a kich1 but no ksmir, so the ic capability
	 * got mapped to kich1 and im to kIC to avoid a collision.
	 * If the description has im but not ic, hack kIC back to kich1.
	 */
	if (foundim && WANTED(key_ic) && key_sic) {
	    key_ic = key_sic;
	    key_sic = ABSENT_STRING;
	}
    }

    if (!has_base) {
	if (!hard_copy) {
	    if (WANTED(key_backspace))
		key_backspace = _nc_save_str(C_BS);
	    if (WANTED(key_left))
		key_left = _nc_save_str(C_BS);
	    if (WANTED(key_down))
		key_down = _nc_save_str(C_LF);
	}
    }

    /*
     * Translate XENIX forms characters.
     */
    if (PRESENT(acs_ulcorner) ||
	PRESENT(acs_llcorner) ||
	PRESENT(acs_urcorner) ||
	PRESENT(acs_lrcorner) ||
	PRESENT(acs_ltee) ||
	PRESENT(acs_rtee) ||
	PRESENT(acs_btee) ||
	PRESENT(acs_ttee) ||
	PRESENT(acs_hline) ||
	PRESENT(acs_vline) ||
	PRESENT(acs_plus)) {
	char buf2[MAX_TERMCAP_LENGTH];

	_nc_str_init(&result, buf2, sizeof(buf2));
	_nc_safe_strcat(&result, acs_chars);

	append_acs(&result, 'j', acs_lrcorner);
	append_acs(&result, 'k', acs_urcorner);
	append_acs(&result, 'l', acs_ulcorner);
	append_acs(&result, 'm', acs_llcorner);
	append_acs(&result, 'n', acs_plus);
	append_acs(&result, 'q', acs_hline);
	append_acs(&result, 't', acs_ltee);
	append_acs(&result, 'u', acs_rtee);
	append_acs(&result, 'v', acs_btee);
	append_acs(&result, 'w', acs_ttee);
	append_acs(&result, 'x', acs_vline);

	if (buf2[0]) {
	    acs_chars = _nc_save_str(buf2);
	    _nc_warning("acsc string synthesized from XENIX capabilities");
	}
    } else if (acs_chars == 0
	       && enter_alt_charset_mode != 0
	       && exit_alt_charset_mode != 0) {
	acs_chars = _nc_save_str(VT_ACSC);
    }
}

static void
postprocess_terminfo(TERMTYPE *tp)
{
    /*
     * TERMINFO-TO-TERMINFO MAPPINGS FOR SOURCE TRANSLATION
     * ----------------------------------------------------------------------
     */

    /*
     * Translate AIX forms characters.
     */
    if (PRESENT(box_chars_1)) {
	char buf2[MAX_TERMCAP_LENGTH];
	string_desc result;

	_nc_str_init(&result, buf2, sizeof(buf2));
	_nc_safe_strcat(&result, acs_chars);

	append_acs0(&result, 'l', box_chars_1[0]);	/* ACS_ULCORNER */
	append_acs0(&result, 'q', box_chars_1[1]);	/* ACS_HLINE */
	append_acs0(&result, 'k', box_chars_1[2]);	/* ACS_URCORNER */
	append_acs0(&result, 'x', box_chars_1[3]);	/* ACS_VLINE */
	append_acs0(&result, 'j', box_chars_1[4]);	/* ACS_LRCORNER */
	append_acs0(&result, 'm', box_chars_1[5]);	/* ACS_LLCORNER */
	append_acs0(&result, 'w', box_chars_1[6]);	/* ACS_TTEE */
	append_acs0(&result, 'u', box_chars_1[7]);	/* ACS_RTEE */
	append_acs0(&result, 'v', box_chars_1[8]);	/* ACS_BTEE */
	append_acs0(&result, 't', box_chars_1[9]);	/* ACS_LTEE */
	append_acs0(&result, 'n', box_chars_1[10]);	/* ACS_PLUS */

	if (buf2[0]) {
	    acs_chars = _nc_save_str(buf2);
	    _nc_warning("acsc string synthesized from AIX capabilities");
	    box_chars_1 = ABSENT_STRING;
	}
    }
    /*
     * ----------------------------------------------------------------------
     */
}

/*
 * Do a linear search through the terminfo tables to find a given full-name.
 * We don't expect to do this often, so there's no hashing function.
 *
 * In effect, this scans through the 3 lists of full-names, and looks them
 * up in _nc_info_table, which is organized so that the nte_index fields are
 * sorted, but the nte_type fields are not necessarily grouped together.
 */
static struct name_table_entry const *
lookup_fullname(const char *find)
{
    int state = -1;

    for (;;) {
	int count = 0;
	NCURSES_CONST char *const *names;

	switch (++state) {
	case BOOLEAN:
	    names = boolfnames;
	    break;
	case STRING:
	    names = strfnames;
	    break;
	case NUMBER:
	    names = numfnames;
	    break;
	default:
	    return NOTFOUND;
	}

	for (count = 0; names[count] != 0; count++) {
	    if (!strcmp(names[count], find)) {
		struct name_table_entry const *entry_ptr = _nc_get_table(FALSE);
		while (entry_ptr->nte_type != state
		       || entry_ptr->nte_index != count)
		    entry_ptr++;
		return entry_ptr;
	    }
	}
    }
}

/* parse_entry.c ends here */
