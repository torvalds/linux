/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey, 1996 on                                       *
 ****************************************************************************/

/*
 *	tparm.c
 *
 */

#include <curses.priv.h>

#include <ctype.h>
#include <tic.h>

MODULE_ID("$Id: lib_tparm.c,v 1.90 2013/11/09 14:53:05 tom Exp $")

/*
 *	char *
 *	tparm(string, ...)
 *
 *	Substitute the given parameters into the given string by the following
 *	rules (taken from terminfo(5)):
 *
 *	     Cursor addressing and other strings  requiring  parame-
 *	ters in the terminal are described by a parameterized string
 *	capability, with escapes like %x in  it.   For  example,  to
 *	address  the  cursor, the cup capability is given, using two
 *	parameters: the row and column to  address  to.   (Rows  and
 *	columns  are  numbered  from  zero and refer to the physical
 *	screen visible to the user, not to any  unseen  memory.)  If
 *	the terminal has memory relative cursor addressing, that can
 *	be indicated by
 *
 *	     The parameter mechanism uses  a  stack  and  special  %
 *	codes  to manipulate it.  Typically a sequence will push one
 *	of the parameters onto the stack and then print it  in  some
 *	format.  Often more complex operations are necessary.
 *
 *	     The % encodings have the following meanings:
 *
 *	     %%        outputs `%'
 *	     %c        print pop() like %c in printf()
 *	     %s        print pop() like %s in printf()
 *           %[[:]flags][width[.precision]][doxXs]
 *                     as in printf, flags are [-+#] and space
 *                     The ':' is used to avoid making %+ or %-
 *                     patterns (see below).
 *
 *	     %p[1-9]   push ith parm
 *	     %P[a-z]   set dynamic variable [a-z] to pop()
 *	     %g[a-z]   get dynamic variable [a-z] and push it
 *	     %P[A-Z]   set static variable [A-Z] to pop()
 *	     %g[A-Z]   get static variable [A-Z] and push it
 *	     %l        push strlen(pop)
 *	     %'c'      push char constant c
 *	     %{nn}     push integer constant nn
 *
 *	     %+ %- %* %/ %m
 *	               arithmetic (%m is mod): push(pop() op pop())
 *	     %& %| %^  bit operations: push(pop() op pop())
 *	     %= %> %<  logical operations: push(pop() op pop())
 *	     %A %O     logical and & or operations for conditionals
 *	     %! %~     unary operations push(op pop())
 *	     %i        add 1 to first two parms (for ANSI terminals)
 *
 *	     %? expr %t thenpart %e elsepart %;
 *	               if-then-else, %e elsepart is optional.
 *	               else-if's are possible ala Algol 68:
 *	               %? c1 %t b1 %e c2 %t b2 %e c3 %t b3 %e c4 %t b4 %e b5 %;
 *
 *	For those of the above operators which are binary and not commutative,
 *	the stack works in the usual way, with
 *			%gx %gy %m
 *	resulting in x mod y, not the reverse.
 */

NCURSES_EXPORT_VAR(int) _nc_tparm_err = 0;

#define TPS(var) _nc_prescreen.tparm_state.var
#define popcount _nc_popcount	/* workaround for NetBSD 6.0 defect */

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_free_tparm(void)
{
    if (TPS(out_buff) != 0) {
	FreeAndNull(TPS(out_buff));
	TPS(out_size) = 0;
	TPS(out_used) = 0;
	FreeAndNull(TPS(fmt_buff));
	TPS(fmt_size) = 0;
    }
}
#endif

static NCURSES_INLINE void
get_space(size_t need)
{
    need += TPS(out_used);
    if (need > TPS(out_size)) {
	TPS(out_size) = need * 2;
	TYPE_REALLOC(char, TPS(out_size), TPS(out_buff));
    }
}

static NCURSES_INLINE void
save_text(const char *fmt, const char *s, int len)
{
    size_t s_len = strlen(s);
    if (len > (int) s_len)
	s_len = (size_t) len;

    get_space(s_len + 1);

    _nc_SPRINTF(TPS(out_buff) + TPS(out_used),
		_nc_SLIMIT(TPS(out_size) - TPS(out_used))
		fmt, s);
    TPS(out_used) += strlen(TPS(out_buff) + TPS(out_used));
}

static NCURSES_INLINE void
save_number(const char *fmt, int number, int len)
{
    if (len < 30)
	len = 30;		/* actually log10(MAX_INT)+1 */

    get_space((size_t) len + 1);

    _nc_SPRINTF(TPS(out_buff) + TPS(out_used),
		_nc_SLIMIT(TPS(out_size) - TPS(out_used))
		fmt, number);
    TPS(out_used) += strlen(TPS(out_buff) + TPS(out_used));
}

static NCURSES_INLINE void
save_char(int c)
{
    if (c == 0)
	c = 0200;
    get_space((size_t) 1);
    TPS(out_buff)[TPS(out_used)++] = (char) c;
}

static NCURSES_INLINE void
npush(int x)
{
    if (TPS(stack_ptr) < STACKSIZE) {
	TPS(stack)[TPS(stack_ptr)].num_type = TRUE;
	TPS(stack)[TPS(stack_ptr)].data.num = x;
	TPS(stack_ptr)++;
    } else {
	DEBUG(2, ("npush: stack overflow: %s", _nc_visbuf(TPS(tparam_base))));
	_nc_tparm_err++;
    }
}

static NCURSES_INLINE int
npop(void)
{
    int result = 0;
    if (TPS(stack_ptr) > 0) {
	TPS(stack_ptr)--;
	if (TPS(stack)[TPS(stack_ptr)].num_type)
	    result = TPS(stack)[TPS(stack_ptr)].data.num;
    } else {
	DEBUG(2, ("npop: stack underflow: %s", _nc_visbuf(TPS(tparam_base))));
	_nc_tparm_err++;
    }
    return result;
}

static NCURSES_INLINE void
spush(char *x)
{
    if (TPS(stack_ptr) < STACKSIZE) {
	TPS(stack)[TPS(stack_ptr)].num_type = FALSE;
	TPS(stack)[TPS(stack_ptr)].data.str = x;
	TPS(stack_ptr)++;
    } else {
	DEBUG(2, ("spush: stack overflow: %s", _nc_visbuf(TPS(tparam_base))));
	_nc_tparm_err++;
    }
}

static NCURSES_INLINE char *
spop(void)
{
    static char dummy[] = "";	/* avoid const-cast */
    char *result = dummy;
    if (TPS(stack_ptr) > 0) {
	TPS(stack_ptr)--;
	if (!TPS(stack)[TPS(stack_ptr)].num_type
	    && TPS(stack)[TPS(stack_ptr)].data.str != 0)
	    result = TPS(stack)[TPS(stack_ptr)].data.str;
    } else {
	DEBUG(2, ("spop: stack underflow: %s", _nc_visbuf(TPS(tparam_base))));
	_nc_tparm_err++;
    }
    return result;
}

static NCURSES_INLINE const char *
parse_format(const char *s, char *format, int *len)
{
    *len = 0;
    if (format != 0) {
	bool done = FALSE;
	bool allowminus = FALSE;
	bool dot = FALSE;
	bool err = FALSE;
	char *fmt = format;
	int my_width = 0;
	int my_prec = 0;
	int value = 0;

	*len = 0;
	*format++ = '%';
	while (*s != '\0' && !done) {
	    switch (*s) {
	    case 'c':		/* FALLTHRU */
	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
	    case 's':
		*format++ = *s;
		done = TRUE;
		break;
	    case '.':
		*format++ = *s++;
		if (dot) {
		    err = TRUE;
		} else {	/* value before '.' is the width */
		    dot = TRUE;
		    my_width = value;
		}
		value = 0;
		break;
	    case '#':
		*format++ = *s++;
		break;
	    case ' ':
		*format++ = *s++;
		break;
	    case ':':
		s++;
		allowminus = TRUE;
		break;
	    case '-':
		if (allowminus) {
		    *format++ = *s++;
		} else {
		    done = TRUE;
		}
		break;
	    default:
		if (isdigit(UChar(*s))) {
		    value = (value * 10) + (*s - '0');
		    if (value > 10000)
			err = TRUE;
		    *format++ = *s++;
		} else {
		    done = TRUE;
		}
	    }
	}

	/*
	 * If we found an error, ignore (and remove) the flags.
	 */
	if (err) {
	    my_width = my_prec = value = 0;
	    format = fmt;
	    *format++ = '%';
	    *format++ = *s;
	}

	/*
	 * Any value after '.' is the precision.  If we did not see '.', then
	 * the value is the width.
	 */
	if (dot)
	    my_prec = value;
	else
	    my_width = value;

	*format = '\0';
	/* return maximum string length in print */
	*len = (my_width > my_prec) ? my_width : my_prec;
    }
    return s;
}

#define isUPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define isLOWER(c) ((c) >= 'a' && (c) <= 'z')

/*
 * Analyze the string to see how many parameters we need from the varargs list,
 * and what their types are.  We will only accept string parameters if they
 * appear as a %l or %s format following an explicit parameter reference (e.g.,
 * %p2%s).  All other parameters are numbers.
 *
 * 'number' counts coarsely the number of pop's we see in the string, and
 * 'popcount' shows the highest parameter number in the string.  We would like
 * to simply use the latter count, but if we are reading termcap strings, there
 * may be cases that we cannot see the explicit parameter numbers.
 */
NCURSES_EXPORT(int)
_nc_tparm_analyze(const char *string, char *p_is_s[NUM_PARM], int *popcount)
{
    size_t len2;
    int i;
    int lastpop = -1;
    int len;
    int number = 0;
    const char *cp = string;
    static char dummy[] = "";

    if (cp == 0)
	return 0;

    if ((len2 = strlen(cp)) > TPS(fmt_size)) {
	TPS(fmt_size) = len2 + TPS(fmt_size) + 2;
	TPS(fmt_buff) = typeRealloc(char, TPS(fmt_size), TPS(fmt_buff));
	if (TPS(fmt_buff) == 0)
	    return 0;
    }

    memset(p_is_s, 0, sizeof(p_is_s[0]) * NUM_PARM);
    *popcount = 0;

    while ((cp - string) < (int) len2) {
	if (*cp == '%') {
	    cp++;
	    cp = parse_format(cp, TPS(fmt_buff), &len);
	    switch (*cp) {
	    default:
		break;

	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
	    case 'c':		/* FALLTHRU */
		if (lastpop <= 0)
		    number++;
		lastpop = -1;
		break;

	    case 'l':
	    case 's':
		if (lastpop > 0)
		    p_is_s[lastpop - 1] = dummy;
		++number;
		break;

	    case 'p':
		cp++;
		i = (UChar(*cp) - '0');
		if (i >= 0 && i <= NUM_PARM) {
		    lastpop = i;
		    if (lastpop > *popcount)
			*popcount = lastpop;
		}
		break;

	    case 'P':
		++number;
		++cp;
		break;

	    case 'g':
		cp++;
		break;

	    case S_QUOTE:
		cp += 2;
		lastpop = -1;
		break;

	    case L_BRACE:
		cp++;
		while (isdigit(UChar(*cp))) {
		    cp++;
		}
		break;

	    case '+':
	    case '-':
	    case '*':
	    case '/':
	    case 'm':
	    case 'A':
	    case 'O':
	    case '&':
	    case '|':
	    case '^':
	    case '=':
	    case '<':
	    case '>':
		lastpop = -1;
		number += 2;
		break;

	    case '!':
	    case '~':
		lastpop = -1;
		++number;
		break;

	    case 'i':
		/* will add 1 to first (usually two) parameters */
		break;
	    }
	}
	if (*cp != '\0')
	    cp++;
    }

    if (number > NUM_PARM)
	number = NUM_PARM;
    return number;
}

static NCURSES_INLINE char *
tparam_internal(int use_TPARM_ARG, const char *string, va_list ap)
{
    char *p_is_s[NUM_PARM];
    TPARM_ARG param[NUM_PARM];
    int popcount = 0;
    int number;
    int num_args;
    int len;
    int level;
    int x, y;
    int i;
    const char *cp = string;
    size_t len2;

    if (cp == NULL)
	return NULL;

    TPS(out_used) = 0;
    len2 = strlen(cp);

    /*
     * Find the highest parameter-number referred to in the format string.
     * Use this value to limit the number of arguments copied from the
     * variable-length argument list.
     */
    number = _nc_tparm_analyze(cp, p_is_s, &popcount);
    if (TPS(fmt_buff) == 0)
	return NULL;

    if (number > NUM_PARM)
	number = NUM_PARM;
    if (popcount > NUM_PARM)
	popcount = NUM_PARM;
    num_args = max(popcount, number);

    for (i = 0; i < num_args; i++) {
	/*
	 * A few caps (such as plab_norm) have string-valued parms.
	 * We'll have to assume that the caller knows the difference, since
	 * a char* and an int may not be the same size on the stack.  The
	 * normal prototype for this uses 9 long's, which is consistent with
	 * our va_arg() usage.
	 */
	if (p_is_s[i] != 0) {
	    p_is_s[i] = va_arg(ap, char *);
	    param[i] = 0;
	} else if (use_TPARM_ARG) {
	    param[i] = va_arg(ap, TPARM_ARG);
	} else {
	    param[i] = (TPARM_ARG) va_arg(ap, int);
	}
    }

    /*
     * This is a termcap compatibility hack.  If there are no explicit pop
     * operations in the string, load the stack in such a way that
     * successive pops will grab successive parameters.  That will make
     * the expansion of (for example) \E[%d;%dH work correctly in termcap
     * style, which means tparam() will expand termcap strings OK.
     */
    TPS(stack_ptr) = 0;
    if (popcount == 0) {
	popcount = number;
	for (i = number - 1; i >= 0; i--) {
	    if (p_is_s[i])
		spush(p_is_s[i]);
	    else
		npush((int) param[i]);
	}
    }
#ifdef TRACE
    if (USE_TRACEF(TRACE_CALLS)) {
	for (i = 0; i < num_args; i++) {
	    if (p_is_s[i] != 0)
		save_text(", %s", _nc_visbuf(p_is_s[i]), 0);
	    else
		save_number(", %d", (int) param[i], 0);
	}
	_tracef(T_CALLED("%s(%s%s)"), TPS(tname), _nc_visbuf(cp), TPS(out_buff));
	TPS(out_used) = 0;
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    while ((cp - string) < (int) len2) {
	if (*cp != '%') {
	    save_char(UChar(*cp));
	} else {
	    TPS(tparam_base) = cp++;
	    cp = parse_format(cp, TPS(fmt_buff), &len);
	    switch (*cp) {
	    default:
		break;
	    case '%':
		save_char('%');
		break;

	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
		save_number(TPS(fmt_buff), npop(), len);
		break;

	    case 'c':		/* FALLTHRU */
		save_char(npop());
		break;

	    case 'l':
		npush((int) strlen(spop()));
		break;

	    case 's':
		save_text(TPS(fmt_buff), spop(), len);
		break;

	    case 'p':
		cp++;
		i = (UChar(*cp) - '1');
		if (i >= 0 && i < NUM_PARM) {
		    if (p_is_s[i])
			spush(p_is_s[i]);
		    else
			npush((int) param[i]);
		}
		break;

	    case 'P':
		cp++;
		if (isUPPER(*cp)) {
		    i = (UChar(*cp) - 'A');
		    TPS(static_vars)[i] = npop();
		} else if (isLOWER(*cp)) {
		    i = (UChar(*cp) - 'a');
		    TPS(dynamic_var)[i] = npop();
		}
		break;

	    case 'g':
		cp++;
		if (isUPPER(*cp)) {
		    i = (UChar(*cp) - 'A');
		    npush(TPS(static_vars)[i]);
		} else if (isLOWER(*cp)) {
		    i = (UChar(*cp) - 'a');
		    npush(TPS(dynamic_var)[i]);
		}
		break;

	    case S_QUOTE:
		cp++;
		npush(UChar(*cp));
		cp++;
		break;

	    case L_BRACE:
		number = 0;
		cp++;
		while (isdigit(UChar(*cp))) {
		    number = (number * 10) + (UChar(*cp) - '0');
		    cp++;
		}
		npush(number);
		break;

	    case '+':
		npush(npop() + npop());
		break;

	    case '-':
		y = npop();
		x = npop();
		npush(x - y);
		break;

	    case '*':
		npush(npop() * npop());
		break;

	    case '/':
		y = npop();
		x = npop();
		npush(y ? (x / y) : 0);
		break;

	    case 'm':
		y = npop();
		x = npop();
		npush(y ? (x % y) : 0);
		break;

	    case 'A':
		npush(npop() && npop());
		break;

	    case 'O':
		npush(npop() || npop());
		break;

	    case '&':
		npush(npop() & npop());
		break;

	    case '|':
		npush(npop() | npop());
		break;

	    case '^':
		npush(npop() ^ npop());
		break;

	    case '=':
		y = npop();
		x = npop();
		npush(x == y);
		break;

	    case '<':
		y = npop();
		x = npop();
		npush(x < y);
		break;

	    case '>':
		y = npop();
		x = npop();
		npush(x > y);
		break;

	    case '!':
		npush(!npop());
		break;

	    case '~':
		npush(~npop());
		break;

	    case 'i':
		if (p_is_s[0] == 0)
		    param[0]++;
		if (p_is_s[1] == 0)
		    param[1]++;
		break;

	    case '?':
		break;

	    case 't':
		x = npop();
		if (!x) {
		    /* scan forward for %e or %; at level zero */
		    cp++;
		    level = 0;
		    while (*cp) {
			if (*cp == '%') {
			    cp++;
			    if (*cp == '?')
				level++;
			    else if (*cp == ';') {
				if (level > 0)
				    level--;
				else
				    break;
			    } else if (*cp == 'e' && level == 0)
				break;
			}

			if (*cp)
			    cp++;
		    }
		}
		break;

	    case 'e':
		/* scan forward for a %; at level zero */
		cp++;
		level = 0;
		while (*cp) {
		    if (*cp == '%') {
			cp++;
			if (*cp == '?')
			    level++;
			else if (*cp == ';') {
			    if (level > 0)
				level--;
			    else
				break;
			}
		    }

		    if (*cp)
			cp++;
		}
		break;

	    case ';':
		break;

	    }			/* endswitch (*cp) */
	}			/* endelse (*cp == '%') */

	if (*cp == '\0')
	    break;

	cp++;
    }				/* endwhile (*cp) */

    get_space((size_t) 1);
    TPS(out_buff)[TPS(out_used)] = '\0';

    T((T_RETURN("%s"), _nc_visbuf(TPS(out_buff))));
    return (TPS(out_buff));
}

#if NCURSES_TPARM_VARARGS
#define tparm_varargs tparm
#else
#define tparm_proto tparm
#endif

NCURSES_EXPORT(char *)
tparm_varargs(NCURSES_CONST char *string,...)
{
    va_list ap;
    char *result;

    _nc_tparm_err = 0;
    va_start(ap, string);
#ifdef TRACE
    TPS(tname) = "tparm";
#endif /* TRACE */
    result = tparam_internal(TRUE, string, ap);
    va_end(ap);
    return result;
}

#if !NCURSES_TPARM_VARARGS
NCURSES_EXPORT(char *)
tparm_proto(NCURSES_CONST char *string,
	    TPARM_ARG a1,
	    TPARM_ARG a2,
	    TPARM_ARG a3,
	    TPARM_ARG a4,
	    TPARM_ARG a5,
	    TPARM_ARG a6,
	    TPARM_ARG a7,
	    TPARM_ARG a8,
	    TPARM_ARG a9)
{
    return tparm_varargs(string, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}
#endif /* NCURSES_TPARM_VARARGS */

NCURSES_EXPORT(char *)
tiparm(const char *string,...)
{
    va_list ap;
    char *result;

    _nc_tparm_err = 0;
    va_start(ap, string);
#ifdef TRACE
    TPS(tname) = "tiparm";
#endif /* TRACE */
    result = tparam_internal(FALSE, string, ap);
    va_end(ap);
    return result;
}
