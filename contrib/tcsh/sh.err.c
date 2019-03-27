/* $Header: /p/tcsh/cvsroot/tcsh/sh.err.c,v 3.57 2015/05/26 17:32:45 christos Exp $ */
/*
 * sh.err.c: Error printing routines. 
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define _h_sh_err		/* Don't redefine the errors	 */
#include "sh.h"
#include <assert.h>

RCSID("$tcsh: sh.err.c,v 3.57 2015/05/26 17:32:45 christos Exp $")

/*
 * C Shell
 */

#ifdef lint
#undef va_arg
#define va_arg(a, b) (a ? (b) 0 : (b) 0)
#endif

char   *seterr = NULL;	/* Holds last error if there was one */

#define ERR_FLAGS	0xf0000000
#define ERR_NAME	0x10000000
#define ERR_SILENT	0x20000000
#define ERR_OLD		0x40000000
#define ERR_INTERRUPT	0x80000000

#define ERR_SYNTAX	0
#define ERR_NOTALLOWED	1
#define ERR_WTOOLONG	2
#define ERR_LTOOLONG	3
#define ERR_DOLZERO	4
#define ERR_INCBR	5
#define ERR_EXPORD	6
#define ERR_BADMOD	7
#define ERR_SUBSCRIPT	8
#define ERR_BADNUM	9
#define ERR_NOMORE	10
#define ERR_FILENAME	11
#define ERR_GLOB	12
#define ERR_COMMAND	13
#define ERR_TOOFEW	14
#define ERR_TOOMANY	15
#define ERR_DANGER	16
#define ERR_EMPTYIF	17
#define ERR_IMPRTHEN	18
#define ERR_NOPAREN	19
#define ERR_NOTFOUND	20
#define ERR_MASK	21
#define ERR_LIMIT	22
#define ERR_TOOLARGE	23
#define ERR_SCALEF	24
#define ERR_UNDVAR	25
#define ERR_DEEP	26
#define ERR_BADSIG	27
#define ERR_UNKSIG	28
#define ERR_VARBEGIN	29
#define ERR_VARTOOLONG	30
#define ERR_VARALNUM	31
#define ERR_JOBCONTROL	32
#define ERR_EXPRESSION	33
#define ERR_NOHOMEDIR	34
#define ERR_CANTCHANGE	35
#define ERR_NULLCOM	36
#define ERR_ASSIGN	37
#define ERR_UNKNOWNOP	38
#define ERR_AMBIG	39
#define ERR_EXISTS	40
#define ERR_ARGC	41
#define ERR_INTR	42
#define ERR_RANGE	43
#define ERR_OVERFLOW	44
#define ERR_NOSUCHJOB	45
#define ERR_TERMINAL	46
#define ERR_NOTWHILE	47
#define ERR_NOPROC	48
#define ERR_NOMATCH	49
#define ERR_MISSING	50
#define ERR_UNMATCHED	51
#define ERR_NOMEM	52
#define ERR_PIPE	53
#define ERR_SYSTEM	54
#define ERR_STRING	55
#define ERR_JOBS	56
#define ERR_JOBARGS	57
#define ERR_JOBCUR	58
#define ERR_JOBPREV	59
#define ERR_JOBPAT	60
#define ERR_NESTING	61
#define ERR_JOBCTRLSUB	62
#define ERR_SYNC	63
#define ERR_STOPPED	64
#define ERR_NODIR	65
#define ERR_EMPTY	66
#define ERR_BADDIR	67
#define ERR_DIRUS	68
#define ERR_HFLAG	69
#define ERR_NOTLOGIN	70
#define ERR_DIV0	71
#define ERR_MOD0	72
#define ERR_BADSCALE	73
#define ERR_SUSPLOG	74
#define ERR_UNKUSER	75
#define ERR_NOHOME	76
#define ERR_HISTUS	77
#define ERR_SPDOLLT	78
#define ERR_NEWLINE	79
#define ERR_SPSTAR	80
#define ERR_DIGIT	81
#define ERR_VARILL	82
#define ERR_NLINDEX	83
#define ERR_EXPOVFL	84
#define ERR_VARSYN	85
#define ERR_BADBANG	86
#define ERR_NOSUBST	87
#define ERR_BADSUBST	88
#define ERR_LHS		89
#define ERR_RHSLONG	90
#define ERR_BADBANGMOD	91
#define ERR_MODFAIL	92
#define ERR_SUBOVFL	93
#define ERR_BADBANGARG	94
#define ERR_NOSEARCH	95
#define ERR_NOEVENT	96
#define ERR_TOOMANYRP	97
#define ERR_TOOMANYLP	98
#define ERR_BADPLP	99
#define ERR_MISRED	100
#define ERR_OUTRED	101
#define ERR_REDPAR	102
#define ERR_INRED	103
#define ERR_BADPLPS	104
#define ERR_ALIASLOOP	105
#define ERR_NOWATCH	106
#define ERR_NOSCHED	107
#define ERR_SCHEDUSAGE	108
#define ERR_SCHEDEV	109
#define ERR_SCHEDCOM	110
#define ERR_SCHEDTIME	111
#define ERR_SCHEDREL	112
#define ERR_TCNOSTR	113
#define ERR_SETTCUS	114
#define ERR_TCCAP	115
#define ERR_TCPARM	116
#define ERR_TCARGS	117
#define ERR_TCNARGS	118
#define ERR_TCUSAGE	119
#define ERR_ARCH	120
#define ERR_HISTLOOP	121
#define ERR_FILEINQ	122
#define ERR_SELOVFL	123
#define ERR_TCSHUSAGE   124
#define ERR_COMPCOM	125
#define ERR_COMPINV	126
#define ERR_COMPMIS	127
#define ERR_COMPINC	128
#define ERR_MFLAG	129
#define ERR_ULIMUS	130
#define ERR_READONLY	131
#define ERR_BADJOB	132
#define ERR_INVALID	133
#define ERR_BADCOLORVAR	134
#define ERR_EOF		135
#define NO_ERRORS	136

static const char *elst[NO_ERRORS] INIT_ZERO_STRUCT;

/*
 * Init the elst depending on the locale
 */
void
errinit(void)
{
#ifdef NLS_CATALOGS
    size_t i;

    for (i = 0; i < NO_ERRORS; i++)
	xfree((char *)(intptr_t)elst[i]);
#  if defined(__FreeBSD__) || defined(hpux) || defined(__MidnightBSD__)
#  define NLS_MAXSET 30
    for (i = 1; i <= NLS_MAXSET; i++)
	CGETS(i, 1, "" );
#  endif
#endif

    elst[ERR_SYNTAX] = CSAVS(1, 1, "Syntax Error");
    elst[ERR_NOTALLOWED] = CSAVS(1, 2, "%s is not allowed");
    elst[ERR_WTOOLONG] = CSAVS(1, 3, "Word too long");
    elst[ERR_LTOOLONG] = CSAVS(1, 4, "$< line too long");
    elst[ERR_DOLZERO] = CSAVS(1, 5, "No file for $0");
    elst[ERR_INCBR] = CSAVS(1, 6, "Incomplete [] modifier");
    elst[ERR_EXPORD] = CSAVS(1, 7, "$ expansion must end before ]");
    elst[ERR_BADMOD] = CSAVS(1, 8, "Bad : modifier in $ '%c'");
    elst[ERR_SUBSCRIPT] = CSAVS(1, 9, "Subscript error");
    elst[ERR_BADNUM] = CSAVS(1, 10, "Badly formed number");
    elst[ERR_NOMORE] = CSAVS(1, 11, "No more words");
    elst[ERR_FILENAME] = CSAVS(1, 12, "Missing file name");
    elst[ERR_GLOB] = CSAVS(1, 13, "Internal glob error");
    elst[ERR_COMMAND] = CSAVS(1, 14, "Command not found");
    elst[ERR_TOOFEW] = CSAVS(1, 15, "Too few arguments");
    elst[ERR_TOOMANY] = CSAVS(1, 16, "Too many arguments");
    elst[ERR_DANGER] = CSAVS(1, 17, "Too dangerous to alias that");
    elst[ERR_EMPTYIF] = CSAVS(1, 18, "Empty if");
    elst[ERR_IMPRTHEN] = CSAVS(1, 19, "Improper then");
    elst[ERR_NOPAREN] = CSAVS(1, 20, "Words not parenthesized");
    elst[ERR_NOTFOUND] = CSAVS(1, 21, "%s not found");
    elst[ERR_MASK] = CSAVS(1, 22, "Improper mask");
    elst[ERR_LIMIT] = CSAVS(1, 23, "No such limit");
    elst[ERR_TOOLARGE] = CSAVS(1, 24, "Argument too large");
    elst[ERR_SCALEF] = CSAVS(1, 25, "Improper or unknown scale factor");
    elst[ERR_UNDVAR] = CSAVS(1, 26, "Undefined variable");
    elst[ERR_DEEP] = CSAVS(1, 27, "Directory stack not that deep");
    elst[ERR_BADSIG] = CSAVS(1, 28, "Bad signal number");
    elst[ERR_UNKSIG] = CSAVS(1, 29, "Unknown signal; kill -l lists signals");
    elst[ERR_VARBEGIN] = CSAVS(1, 30, "Variable name must begin with a letter");
    elst[ERR_VARTOOLONG] = CSAVS(1, 31, "Variable name too long");
    elst[ERR_VARALNUM] = CSAVS(1, 32,
	"Variable name must contain alphanumeric characters");
    elst[ERR_JOBCONTROL] = CSAVS(1, 33, "No job control in this shell");
    elst[ERR_EXPRESSION] = CSAVS(1, 34, "Expression Syntax");
    elst[ERR_NOHOMEDIR] = CSAVS(1, 35, "No home directory");
    elst[ERR_CANTCHANGE] = CSAVS(1, 36, "Can't change to home directory");
    elst[ERR_NULLCOM] = CSAVS(1, 37, "Invalid null command");
    elst[ERR_ASSIGN] = CSAVS(1, 38, "Assignment missing expression");
    elst[ERR_UNKNOWNOP] = CSAVS(1, 39, "Unknown operator");
    elst[ERR_AMBIG] = CSAVS(1, 40, "Ambiguous");
    elst[ERR_EXISTS] = CSAVS(1, 41, "%s: File exists");
    elst[ERR_ARGC] = CSAVS(1, 42, "Argument for -c ends in backslash");
    elst[ERR_INTR] = CSAVS(1, 43, "Interrupted");
    elst[ERR_RANGE] = CSAVS(1, 44, "Subscript out of range");
    elst[ERR_OVERFLOW] = CSAVS(1, 45, "Line overflow");
    elst[ERR_NOSUCHJOB] = CSAVS(1, 46, "No such job");
    elst[ERR_TERMINAL] = CSAVS(1, 47, "Can't from terminal");
    elst[ERR_NOTWHILE] = CSAVS(1, 48, "Not in while/foreach");
    elst[ERR_NOPROC] = CSAVS(1, 49, "No more processes");
    elst[ERR_NOMATCH] = CSAVS(1, 50, "No match");
    elst[ERR_MISSING] = CSAVS(1, 51, "Missing '%c'");
    elst[ERR_UNMATCHED] = CSAVS(1, 52, "Unmatched '%c'");
    elst[ERR_NOMEM] = CSAVS(1, 53, "Out of memory");
    elst[ERR_PIPE] = CSAVS(1, 54, "Can't make pipe");
    elst[ERR_SYSTEM] = CSAVS(1, 55, "%s: %s");
    elst[ERR_STRING] = CSAVS(1, 56, "%s");
    elst[ERR_JOBS] = CSAVS(1, 57, "Usage: jobs [ -l ]");
    elst[ERR_JOBARGS] = CSAVS(1, 58, "Arguments should be jobs or process id's");
    elst[ERR_JOBCUR] = CSAVS(1, 59, "No current job");
    elst[ERR_JOBPREV] = CSAVS(1, 60, "No previous job");
    elst[ERR_JOBPAT] = CSAVS(1, 61, "No job matches pattern");
    elst[ERR_NESTING] = CSAVS(1, 62, "Fork nesting > %d; maybe `...` loop");
    elst[ERR_JOBCTRLSUB] = CSAVS(1, 63, "No job control in subshells");
    elst[ERR_SYNC] = CSAVS(1, 64, "Sync fault: Process %d not found");
    elst[ERR_STOPPED] =
#ifdef SUSPENDED
	CSAVS(1, 65, "%sThere are suspended jobs");
#else
	CSAVS(1, 66, "%sThere are stopped jobs");
#endif /* SUSPENDED */
    elst[ERR_NODIR] = CSAVS(1, 67, "No other directory");
    elst[ERR_EMPTY] = CSAVS(1, 68, "Directory stack empty");
    elst[ERR_BADDIR] = CSAVS(1, 69, "Bad directory");
    elst[ERR_DIRUS] = CSAVS(1, 70, "Usage: %s [-%s]%s");
    elst[ERR_HFLAG] = CSAVS(1, 71, "No operand for -h flag");
    elst[ERR_NOTLOGIN] = CSAVS(1, 72, "Not a login shell");
    elst[ERR_DIV0] = CSAVS(1, 73, "Division by 0");
    elst[ERR_MOD0] = CSAVS(1, 74, "Mod by 0");
    elst[ERR_BADSCALE] = CSAVS(1, 75, "Bad scaling; did you mean \"%s\"?");
    elst[ERR_SUSPLOG] = CSAVS(1, 76, "Can't suspend a login shell (yet)");
    elst[ERR_UNKUSER] = CSAVS(1, 77, "Unknown user: %s");
    elst[ERR_NOHOME] = CSAVS(1, 78, "No $home variable set");
    elst[ERR_HISTUS] = CSAVS(1, 79,
	"Usage: history [-%s] [# number of events]");
    elst[ERR_SPDOLLT] = CSAVS(1, 80, "$, ! or < not allowed with $# or $?");
    elst[ERR_NEWLINE] = CSAVS(1, 81, "Newline in variable name");
    elst[ERR_SPSTAR] = CSAVS(1, 82, "* not allowed with $# or $?");
    elst[ERR_DIGIT] = CSAVS(1, 83, "$?<digit> or $#<digit> not allowed");
    elst[ERR_VARILL] = CSAVS(1, 84, "Illegal variable name");
    elst[ERR_NLINDEX] = CSAVS(1, 85, "Newline in variable index");
    elst[ERR_EXPOVFL] = CSAVS(1, 86, "Expansion buffer overflow");
    elst[ERR_VARSYN] = CSAVS(1, 87, "Variable syntax");
    elst[ERR_BADBANG] = CSAVS(1, 88, "Bad ! form");
    elst[ERR_NOSUBST] = CSAVS(1, 89, "No previous substitute");
    elst[ERR_BADSUBST] = CSAVS(1, 90, "Bad substitute");
    elst[ERR_LHS] = CSAVS(1, 91, "No previous left hand side");
    elst[ERR_RHSLONG] = CSAVS(1, 92, "Right hand side too long");
    elst[ERR_BADBANGMOD] = CSAVS(1, 93, "Bad ! modifier: '%c'");
    elst[ERR_MODFAIL] = CSAVS(1, 94, "Modifier failed");
    elst[ERR_SUBOVFL] = CSAVS(1, 95, "Substitution buffer overflow");
    elst[ERR_BADBANGARG] = CSAVS(1, 96, "Bad ! arg selector");
    elst[ERR_NOSEARCH] = CSAVS(1, 97, "No prev search");
    elst[ERR_NOEVENT] = CSAVS(1, 98, "%s: Event not found");
    elst[ERR_TOOMANYRP] = CSAVS(1, 99, "Too many )'s");
    elst[ERR_TOOMANYLP] = CSAVS(1, 100, "Too many ('s");
    elst[ERR_BADPLP] = CSAVS(1, 101, "Badly placed (");
    elst[ERR_MISRED] = CSAVS(1, 102, "Missing name for redirect");
    elst[ERR_OUTRED] = CSAVS(1, 103, "Ambiguous output redirect");
    elst[ERR_REDPAR] = CSAVS(1, 104, "Can't << within ()'s");
    elst[ERR_INRED] = CSAVS(1, 105, "Ambiguous input redirect");
    elst[ERR_BADPLPS] = CSAVS(1, 106, "Badly placed ()'s");
    elst[ERR_ALIASLOOP] = CSAVS(1, 107, "Alias loop");
    elst[ERR_NOWATCH] = CSAVS(1, 108, "No $watch variable set");
    elst[ERR_NOSCHED] = CSAVS(1, 109, "No scheduled events");
    elst[ERR_SCHEDUSAGE] = CSAVS(1, 110,
	"Usage: sched -<item#>.\nUsage: sched [+]hh:mm <command>");
    elst[ERR_SCHEDEV] = CSAVS(1, 111, "Not that many scheduled events");
    elst[ERR_SCHEDCOM] = CSAVS(1, 112, "No command to run");
    elst[ERR_SCHEDTIME] = CSAVS(1, 113, "Invalid time for event");
    elst[ERR_SCHEDREL] = CSAVS(1, 114, "Relative time inconsistent with am/pm");
    elst[ERR_TCNOSTR] = CSAVS(1, 115, "Out of termcap string space");
    elst[ERR_SETTCUS] = CSAVS(1, 116, "Usage: settc %s [yes|no]");
    elst[ERR_TCCAP] = CSAVS(1, 117, "Unknown capability `%s'");
    elst[ERR_TCPARM] = CSAVS(1, 118, "Unknown termcap parameter '%%%c'");
    elst[ERR_TCARGS] = CSAVS(1, 119, "Too many arguments for `%s' (%d)");
    elst[ERR_TCNARGS] = CSAVS(1, 120, "`%s' requires %d arguments");
    elst[ERR_TCUSAGE] = CSAVS(1, 121,
	"Usage: echotc [-v|-s] [<capability> [<args>]]");
    elst[ERR_ARCH] = CSAVS(1, 122, "%s: %s. Binary file not executable");
    elst[ERR_HISTLOOP] = CSAVS(1, 123, "!# History loop");
    elst[ERR_FILEINQ] = CSAVS(1, 124, "Malformed file inquiry");
    elst[ERR_SELOVFL] = CSAVS(1, 125, "Selector overflow");
#ifdef apollo
    elst[ERR_TCSHUSAGE] = CSAVS(1, 126,
"Unknown option: `-%s'\nUsage: %s [ -bcdefilmnqstvVxX -Dname[=value] ] [ argument ... ]");
#else /* !apollo */
# ifdef convex
    elst[ERR_TCSHUSAGE] = CSAVS(1, 127,
"Unknown option: `-%s'\nUsage: %s [ -bcdefFilmnqstvVxX ] [ argument ... ]");
# else /* rest */
    elst[ERR_TCSHUSAGE] = CSAVS(1, 128,
"Unknown option: `-%s'\nUsage: %s [ -bcdefilmnqstvVxX ] [ argument ... ]");
# endif /* convex */
#endif /* apollo */
    elst[ERR_COMPCOM] = CSAVS(1, 129, "\nInvalid completion: \"%s\"");
    elst[ERR_COMPINV] = CSAVS(1, 130, "\nInvalid %s: '%c'");
    elst[ERR_COMPMIS] = CSAVS(1, 131,
	"\nMissing separator '%c' after %s \"%s\"");
    elst[ERR_COMPINC] = CSAVS(1, 132, "\nIncomplete %s: \"%s\"");
    elst[ERR_MFLAG] = CSAVS(1, 133, "No operand for -m flag");
    elst[ERR_ULIMUS] = CSAVS(1, 134, "Usage: unlimit [-fh] [limits]");
    elst[ERR_READONLY] = CSAVS(1, 135, "$%S is read-only");
    elst[ERR_BADJOB] = CSAVS(1, 136, "No such job (badjob)");
    elst[ERR_BADCOLORVAR] = CSAVS(1, 137, "Unknown colorls variable '%c%c'");
    elst[ERR_EOF] = CSAVS(1, 138, "Unexpected end of file");
}

/* Cleanup data. */
struct cleanup_entry
{
    void *var;
    void (*fn) (void *);
#ifdef CLEANUP_DEBUG
    const char *file;
    size_t line;
#endif
};

static struct cleanup_entry *cleanup_stack INIT_ZERO; /* = NULL; */
static size_t cleanup_sp INIT_ZERO; /* = 0; Next free entry */
static size_t cleanup_mark INIT_ZERO; /* = 0; Last entry to handle before unwinding */
static size_t cleanup_stack_size INIT_ZERO; /* = 0 */

/* fn() will be run with all signals blocked, so it should not do anything
   risky. */
void
cleanup_push_internal(void *var, void (*fn) (void *)
#ifdef CLEANUP_DEBUG
    , const char *file, size_t line
#endif
)
{
    struct cleanup_entry *ce;

    if (cleanup_sp == cleanup_stack_size) {
	if (cleanup_stack_size == 0)
	    cleanup_stack_size = 64; /* Arbitrary */
	else
	    cleanup_stack_size *= 2;
	cleanup_stack = xrealloc(cleanup_stack,
				 cleanup_stack_size * sizeof (*cleanup_stack));
    }
    ce = cleanup_stack + cleanup_sp;
    ce->var = var;
    ce->fn = fn;
#ifdef CLEANUP_DEBUG
    ce->file = file;
    ce->line = line;
#endif
    cleanup_sp++;
}

static void
cleanup_ignore_fn(void *dummy)
{
    USE(dummy);
}

void
cleanup_ignore(void *var)
{
    struct cleanup_entry *ce;

    ce = cleanup_stack + cleanup_sp;
    while (ce != cleanup_stack) {
        ce--;
	if (ce->var == var) {
	    ce->fn = cleanup_ignore_fn;
	    return;
	}
    }
    abort();
}

void
cleanup_until(void *last_var)
{
    while (cleanup_sp != 0) {
	struct cleanup_entry ce;

	cleanup_sp--;
	ce = cleanup_stack[cleanup_sp];
	ce.fn(ce.var);
	if (ce.var == last_var)
	    return;
    }
    abort();
}

int
cleanup_reset(void)
{
    return cleanup_sp > cleanup_mark;
}

void
cleanup_until_mark(void)
{
    while (cleanup_sp > cleanup_mark) {
	struct cleanup_entry ce;

	cleanup_sp--;
	ce = cleanup_stack[cleanup_sp];
	ce.fn(ce.var);
    }
}

size_t
cleanup_push_mark(void)
{
    size_t old_mark;

    old_mark = cleanup_mark;
    cleanup_mark = cleanup_sp;
    return old_mark;
}

void
cleanup_pop_mark(size_t mark)
{
    assert (mark <= cleanup_sp);
    cleanup_mark = mark;
}

void
sigint_cleanup(void *xsa)
{
    const struct sigaction *sa;

    sa = xsa;
    sigaction(SIGINT, sa, NULL);
}

void
sigprocmask_cleanup(void *xmask)
{
    sigset_t *mask;

    mask = xmask;
    sigprocmask(SIG_SETMASK, mask, NULL);
}

void
open_cleanup(void *xptr)
{
    int *ptr;

    ptr = xptr;
    xclose(*ptr);
}

void
opendir_cleanup(void *xdir)
{
    DIR *dir;

    dir = xdir;
    xclosedir(dir);
}

void
xfree_indirect(void *xptr)
{
    void **ptr; /* This is actually type punning :( */

    ptr = xptr;
    xfree(*ptr);
}

void
reset(void)
{
    cleanup_until_mark();
    _reset();
    abort();
}

/*
 * The parser and scanner set up errors for later by calling seterr,
 * which sets the variable err as a side effect; later to be tested,
 * e.g. in process.
 */
void
/*VARARGS1*/
seterror(unsigned int id, ...)
{
    if (seterr == 0) {
	va_list va;

	va_start(va, id);
	if (id >= sizeof(elst) / sizeof(elst[0]))
	    id = ERR_INVALID;
	seterr = xvasprintf(elst[id], va);
	va_end(va);
    }
}

void
fixerror(void)
{
    didfds = 0;			/* Forget about 0,1,2 */
    /*
     * Go away if -e or we are a child shell
     */
    if (!exitset || exiterr || child)
	xexit(1);

    /*
     * Reset the state of the input. This buffered seek to end of file will
     * also clear the while/foreach stack.
     */
    btoeof();

    setcopy(STRstatus, STR1, VAR_READWRITE);/*FIXRESET*/
#ifdef BSDJOBS
    if (tpgrp > 0)
	(void) tcsetpgrp(FSHTTY, tpgrp);
#endif
}

/*
 * Print the error with the given id.
 *
 * Special ids:
 *	ERR_SILENT: Print nothing.
 *	ERR_OLD: Print the previously set error
 *	ERR_NAME: If this bit is set, print the name of the function
 *		  in bname
 *
 * This routine always resets or exits.  The flag haderr
 * is set so the routine who catches the unwind can propogate
 * it if they want.
 *
 * Note that any open files at the point of error will eventually
 * be closed in the routine process in sh.c which is the only
 * place error unwinds are ever caught.
 */
void
/*VARARGS*/
stderror(unsigned int id, ...)
{
    va_list va;
    int flags;

    va_start(va, id);

    /*
     * Reset don't free flag for buggy os's
     */
    dont_free = 0;

    flags = (int) id & ERR_FLAGS;
    id &= ~ERR_FLAGS;

    /* Pyramid's OS/x has a subtle bug in <varargs.h> which prevents calling
     * va_end more than once in the same function. -- sterling@netcom.com
     */
    assert(!((flags & ERR_OLD) && seterr == NULL));

    if (id >= sizeof(elst) / sizeof(elst[0]))
	id = ERR_INVALID;

    if (!(flags & ERR_SILENT)) {
	/*
	 * Must flush before we print as we wish output before the error
	 * to go on (some form of) standard output, while output after
	 * goes on (some form of) diagnostic output. If didfds then output
	 * will go to 1/2 else to FSHOUT/FSHDIAG. See flush in sh.print.c.
	 */
	flush();/*FIXRESET*/
	haderr = 1;		/* Now to diagnostic output */
	if (flags & ERR_NAME)
	    xprintf("%s: ", bname);/*FIXRESET*/
	if ((flags & ERR_OLD)) {
	    /* Old error. */
	    xprintf("%s.\n", seterr);/*FIXRESET*/
	} else {
	    xvprintf(elst[id], va);/*FIXRESET*/
	    xprintf(".\n");/*FIXRESET*/
	}
    }
    va_end(va);

    if (seterr) {
	xfree(seterr);
	seterr = NULL;
    }

    fixerror();

    reset();		/* Unwind */
}
