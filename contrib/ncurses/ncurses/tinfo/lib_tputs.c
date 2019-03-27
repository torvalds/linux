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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 *	tputs.c
 *		delay_output()
 *		_nc_outch()
 *		tputs()
 *
 */

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

#include <ctype.h>
#include <termcap.h>		/* ospeed */
#include <tic.h>

MODULE_ID("$Id: lib_tputs.c,v 1.93 2013/01/12 20:57:32 tom Exp $")

NCURSES_EXPORT_VAR(char) PC = 0;              /* used by termcap library */
NCURSES_EXPORT_VAR(NCURSES_OSPEED) ospeed = 0;        /* used by termcap library */

NCURSES_EXPORT_VAR(int) _nc_nulls_sent = 0;   /* used by 'tack' program */

#if NCURSES_NO_PADDING
NCURSES_EXPORT(void)
_nc_set_no_padding(SCREEN *sp)
{
    bool no_padding = (getenv("NCURSES_NO_PADDING") != 0);

    if (sp)
	sp->_no_padding = no_padding;
    else
	_nc_prescreen._no_padding = no_padding;

    TR(TRACE_CHARPUT | TRACE_MOVE, ("padding will%s be used",
				    GetNoPadding(sp) ? " not" : ""));
}
#endif

#if NCURSES_SP_FUNCS
#define SetOutCh(func) if (SP_PARM) SP_PARM->_outch = func; else _nc_prescreen._outch = func
#define GetOutCh()     (SP_PARM ? SP_PARM->_outch : _nc_prescreen._outch)
#else
#define SetOutCh(func) static_outch = func
#define GetOutCh()     static_outch
static NCURSES_SP_OUTC static_outch = NCURSES_SP_NAME(_nc_outch);
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(delay_output) (NCURSES_SP_DCLx int ms)
{
    T((T_CALLED("delay_output(%p,%d)"), (void *) SP_PARM, ms));

    if (!HasTInfoTerminal(SP_PARM))
	returnCode(ERR);

    if (no_pad_char) {
	NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
	napms(ms);
    } else {
	NCURSES_SP_OUTC my_outch = GetOutCh();
	register int nullcount;

	nullcount = (ms * _nc_baudrate(ospeed)) / (BAUDBYTE * 1000);
	for (_nc_nulls_sent += nullcount; nullcount > 0; nullcount--)
	    my_outch(NCURSES_SP_ARGx PC);
	if (my_outch == NCURSES_SP_NAME(_nc_outch))
	    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    }

    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
delay_output(int ms)
{
    return NCURSES_SP_NAME(delay_output) (CURRENT_SCREEN, ms);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_DCL0)
{
    if (SP_PARM != 0 && SP_PARM->_ofd >= 0) {
	if (SP_PARM->out_inuse) {
	    size_t amount = SP->out_inuse;
	    /*
	     * Help a little, if the write is interrupted, by first resetting
	     * our amount.
	     */
	    SP->out_inuse = 0;
	    IGNORE_RC(write(SP_PARM->_ofd, SP_PARM->out_buffer, amount));
	}
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_flush(void)
{
    NCURSES_SP_NAME(_nc_flush) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_outch) (NCURSES_SP_DCLx int ch)
{
    int rc = OK;

    COUNT_OUTCHARS(1);

    if (HasTInfoTerminal(SP_PARM)
	&& SP_PARM != 0) {
	if (SP_PARM->out_buffer != 0) {
	    if (SP_PARM->out_inuse + 1 >= SP_PARM->out_limit)
		NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
	    SP_PARM->out_buffer[SP_PARM->out_inuse++] = (char) ch;
	} else {
	    char tmp = (char) ch;
	    /*
	     * POSIX says write() is safe in a signal handler, but the
	     * buffered I/O is not.
	     */
	    if (write(fileno(NC_OUTPUT(SP_PARM)), &tmp, (size_t) 1) == -1)
		rc = ERR;
	}
    } else {
	char tmp = (char) ch;
	if (write(fileno(stdout), &tmp, (size_t) 1) == -1)
	    rc = ERR;
    }
    return rc;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_outch(int ch)
{
    return NCURSES_SP_NAME(_nc_outch) (CURRENT_SCREEN, ch);
}
#endif

/*
 * This is used for the putp special case.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_putchar) (NCURSES_SP_DCLx int ch)
{
    (void) SP_PARM;
    return putchar(ch);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_putchar(int ch)
{
    return putchar(ch);
}
#endif

/*
 * putp is special - per documentation it calls tputs with putchar as the
 * parameter for outputting characters.  This means that it uses stdio, which
 * is not signal-safe.  Applications call this entrypoint; we do not call it
 * from within the library.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(putp) (NCURSES_SP_DCLx const char *string)
{
    return NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				   string, 1, NCURSES_SP_NAME(_nc_putchar));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
putp(const char *string)
{
    return NCURSES_SP_NAME(putp) (CURRENT_SCREEN, string);
}
#endif

/*
 * Use these entrypoints rather than "putp" within the library.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_putp) (NCURSES_SP_DCLx
			   const char *name GCC_UNUSED,
			   const char *string)
{
    int rc = ERR;

    if (string != 0) {
	TPUTS_TRACE(name);
	rc = NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				     string, 1, NCURSES_SP_NAME(_nc_outch));
    }
    return rc;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_putp(const char *name, const char *string)
{
    return NCURSES_SP_NAME(_nc_putp) (CURRENT_SCREEN, name, string);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(tputs) (NCURSES_SP_DCLx
			const char *string,
			int affcnt,
			NCURSES_SP_OUTC outc)
{
    NCURSES_SP_OUTC my_outch = GetOutCh();
    bool always_delay;
    bool normal_delay;
    int number;
#if BSD_TPUTS
    int trailpad;
#endif /* BSD_TPUTS */

#ifdef TRACE
    char addrbuf[32];

    if (USE_TRACEF(TRACE_TPUTS)) {
	if (outc == NCURSES_SP_NAME(_nc_outch))
	    _nc_STRCPY(addrbuf, "_nc_outch", sizeof(addrbuf));
	else
	    _nc_SPRINTF(addrbuf, _nc_SLIMIT(sizeof(addrbuf)) "%p", outc);
	if (_nc_tputs_trace) {
	    _tracef("tputs(%s = %s, %d, %s) called", _nc_tputs_trace,
		    _nc_visbuf(string), affcnt, addrbuf);
	} else {
	    _tracef("tputs(%s, %d, %s) called", _nc_visbuf(string), affcnt, addrbuf);
	}
	TPUTS_TRACE(NULL);
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    if (SP_PARM != 0 && !HasTInfoTerminal(SP_PARM))
	return ERR;

    if (!VALID_STRING(string))
	return ERR;

    if (
#if NCURSES_SP_FUNCS
	   (SP_PARM != 0 && SP_PARM->_term == 0)
#else
	   cur_term == 0
#endif
	) {
	always_delay = FALSE;
	normal_delay = TRUE;
    } else {
	always_delay = (string == bell) || (string == flash_screen);
	normal_delay =
	    !xon_xoff
	    && padding_baud_rate
#if NCURSES_NO_PADDING
	    && !GetNoPadding(SP_PARM)
#endif
	    && (_nc_baudrate(ospeed) >= padding_baud_rate);
    }

#if BSD_TPUTS
    /*
     * This ugly kluge deals with the fact that some ancient BSD programs
     * (like nethack) actually do the likes of tputs("50") to get delays.
     */
    trailpad = 0;
    if (isdigit(UChar(*string))) {
	while (isdigit(UChar(*string))) {
	    trailpad = trailpad * 10 + (*string - '0');
	    string++;
	}
	trailpad *= 10;
	if (*string == '.') {
	    string++;
	    if (isdigit(UChar(*string))) {
		trailpad += (*string - '0');
		string++;
	    }
	    while (isdigit(UChar(*string)))
		string++;
	}

	if (*string == '*') {
	    trailpad *= affcnt;
	    string++;
	}
    }
#endif /* BSD_TPUTS */

    SetOutCh(outc);		/* redirect delay_output() */
    while (*string) {
	if (*string != '$')
	    (*outc) (NCURSES_SP_ARGx *string);
	else {
	    string++;
	    if (*string != '<') {
		(*outc) (NCURSES_SP_ARGx '$');
		if (*string)
		    (*outc) (NCURSES_SP_ARGx *string);
	    } else {
		bool mandatory;

		string++;
		if ((!isdigit(UChar(*string)) && *string != '.')
		    || !strchr(string, '>')) {
		    (*outc) (NCURSES_SP_ARGx '$');
		    (*outc) (NCURSES_SP_ARGx '<');
		    continue;
		}

		number = 0;
		while (isdigit(UChar(*string))) {
		    number = number * 10 + (*string - '0');
		    string++;
		}
		number *= 10;
		if (*string == '.') {
		    string++;
		    if (isdigit(UChar(*string))) {
			number += (*string - '0');
			string++;
		    }
		    while (isdigit(UChar(*string)))
			string++;
		}

		mandatory = FALSE;
		while (*string == '*' || *string == '/') {
		    if (*string == '*') {
			number *= affcnt;
			string++;
		    } else {	/* if (*string == '/') */
			mandatory = TRUE;
			string++;
		    }
		}

		if (number > 0
		    && (always_delay
			|| normal_delay
			|| mandatory))
		    NCURSES_SP_NAME(delay_output) (NCURSES_SP_ARGx number / 10);

	    }			/* endelse (*string == '<') */
	}			/* endelse (*string == '$') */

	if (*string == '\0')
	    break;

	string++;
    }

#if BSD_TPUTS
    /*
     * Emit any BSD-style prefix padding that we've accumulated now.
     */
    if (trailpad > 0
	&& (always_delay || normal_delay))
	delay_output(trailpad / 10);
#endif /* BSD_TPUTS */

    SetOutCh(my_outch);
    return OK;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_outc_wrapper(SCREEN *sp, int c)
{
    if (0 == sp) {
	return (ERR);
    } else {
	return sp->jump(c);
    }
}

NCURSES_EXPORT(int)
tputs(const char *string, int affcnt, int (*outc) (int))
{
    SetSafeOutcWrapper(outc);
    return NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx string, affcnt, _nc_outc_wrapper);
}
#endif
