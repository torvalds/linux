/*	$OpenBSD: csh.h,v 1.34 2020/08/30 22:23:47 mortimer Exp $	*/
/*	$NetBSD: csh.h,v 1.9 1995/03/21 09:02:40 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)csh.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Fundamental definitions which may vary from system to system.
 *
 *	BUFSIZ		The i/o buffering size; also limits word size
 *	MAILINTVL	How often to mailcheck; more often is more expensive
 */

#define FORKSLEEP	10	/* delay loop on non-interactive fork failure */
#define	MAILINTVL	600	/* 10 minutes */

/*
 * The shell moves std in/out/diag and the old std input away from units
 * 0, 1, and 2 so that it is easy to set up these standards for invoked
 * commands.
 */
#define	FSHTTY	15		/* /dev/tty when manip pgrps */
#define	FSHIN	16		/* Preferred desc for shell input */
#define	FSHOUT	17		/* ... shell output */
#define	FSHERR	18		/* ... shell diagnostics */
#define	FOLDSTD	19		/* ... old std input */

typedef short Char;

#define SAVE(a) (Strsave(str2short(a)))

/*
 * Make sure a variable is not stored in register a by taking its address
 * This is used where variables might be clobbered by longjmp.
 */
#define UNREGISTER(a)	(void) &a

typedef void *ioctl_t;		/* Third arg of ioctl */

#include "const.h"
#include "char.h"
#include "error.h"

#include <stdio.h>
extern FILE *cshin, *cshout, *csherr;

#define	isdir(d)	(S_ISDIR(d.st_mode))

typedef int bool;

#define	eq(a, b)	(Strcmp(a, b) == 0)

/* globone() flags */
#define G_ERROR		0	/* default action: error if multiple words */
#define G_IGNORE	1	/* ignore the rest of the words */
#define G_APPEND	2	/* make a sentence by cat'ing the words */

/*
 * Global flags
 */
extern bool    chkstop;		/* Warned of stopped jobs... allow exit */
extern bool    didfds;		/* Have setup i/o fd's for child */
extern bool    doneinp;		/* EOF indicator after reset from readc */
extern bool    exiterr;		/* Exit if error or non-zero exit status */
extern bool    child;		/* Child shell ... errors cause exit */
extern bool    haderr;		/* Reset was because of an error */
extern bool    intty;		/* Input is a tty */
extern bool    intact;		/* We are interactive... therefore prompt */
extern bool    justpr;		/* Just print because of :p hist mod */
extern bool    loginsh;		/* We are a loginsh -> .login/.logout */
extern bool    neednote;	/* Need to pnotify() */
extern bool    noexec;		/* Don't execute, just syntax check */
extern bool    pjobs;		/* want to print jobs if interrupted */
extern bool    setintr;		/* Set interrupts on/off -> Wait intr... */
extern bool    timflg;		/* Time the next waited for command */
extern bool    havhash;		/* path hashing is available */

extern bool    filec;		/* doing filename expansion */
extern bool    needprompt;	/* print prompt, used by filec */

/*
 * Global i/o info
 */
extern Char   *arginp;		/* Argument input for sh -c and internal `xx` */
extern int     onelflg;		/* 2 -> need line for -t, 1 -> exit on read */
extern Char   *ffile;		/* Name of shell file for $0 */

extern char   *seterr;		/* Error message from scanner/parser */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

extern struct timespec time0;	/* Time at which the shell started */
extern struct rusage ru0;

/*
 * Miscellany
 */
extern Char   *doldol;		/* Character pid for $$ */
extern int	backpid;	/* Pid of the last background process */
extern uid_t	uid, euid;	/* Invokers uid */
extern gid_t	gid, egid;	/* Invokers gid */
extern time_t  chktim;		/* Time mail last checked */
extern pid_t	shpgrp;		/* Pgrp of shell */
extern pid_t	tpgrp;		/* Terminal process group */

/* If tpgrp is -1, leave tty alone! */
extern pid_t	opgrp;		/* Initial pgrp and tty pgrp */


/*
 * To be able to redirect i/o for builtins easily, the shell moves the i/o
 * descriptors it uses away from 0,1,2.
 * Ideally these should be in units which are closed across exec's
 * (this saves work) but for version 6, this is not usually possible.
 * The desired initial values for these descriptors are F{SHIN,...}.
 */
extern int   SHIN;		/* Current shell input (script) */
extern int   SHOUT;		/* Shell output */
extern int   SHERR;		/* Diagnostic output... shell errs go here */
extern int   OLDSTD;		/* Old standard input (def for cmds) */

/*
 * Error control
 *
 * Errors in scanning and parsing set up an error message to be printed
 * at the end and complete.  Other errors always cause a reset.
 * Because of source commands and .cshrc we need nested error catches.
 */

#include <setjmp.h>
extern jmp_buf reslab;
extern int exitset;

#define	setexit()	(setjmp(reslab))
#define	reset()		longjmp(reslab, 1)
 /* Should use structure assignment here */
#define	getexit(a)	memcpy((a), reslab, sizeof reslab)
#define	resexit(a)	memcpy(reslab, (a), sizeof reslab)

extern Char   *gointr;		/* Label for an onintr transfer */

#include <signal.h>
extern sig_t parintr;		/* Parents interrupt catch */
extern sig_t parterm;		/* Parents terminate catch */

/*
 * Lexical definitions.
 *
 * All lexical space is allocated dynamically.
 * The eighth/sixteenth bit of characters is used to prevent recognition,
 * and eventually stripped.
 */
#define	META		0200
#define	ASCII		0177
#define	QUOTE 		0100000U /* 16nth char bit used for 'ing */
#define	TRIM		0077777	/* Mask to strip quote bit */

/*
 * Each level of input has a buffered input structure.
 * There are one or more blocks of buffered input for each level,
 * exactly one if the input is seekable and tell is available.
 * In other cases, the shell buffers enough blocks to keep all loops
 * in the buffer.
 */
extern struct Bin {
    off_t   Bfseekp;		/* Seek pointer */
    off_t   Bfbobp;		/* Seekp of beginning of buffers */
    off_t   Bfeobp;		/* Seekp of end of buffers */
    int     Bfblocks;		/* Number of buffer blocks */
    Char  **Bfbuf;		/* The array of buffer blocks */
}       B;

/*
 * This structure allows us to seek inside aliases
 */
struct Ain {
    int type;
#define I_SEEK -1		/* Invalid seek */
#define A_SEEK	0		/* Alias seek */
#define F_SEEK	1		/* File seek */
#define E_SEEK	2		/* Eval seek */
    union {
	off_t _f_seek;
	Char* _c_seek;
    } fc;
#define f_seek fc._f_seek
#define c_seek fc._c_seek
    Char **a_seek;
} ;
extern int aret;		/* What was the last character returned */
#define SEEKEQ(a, b) ((a)->type == (b)->type && \
		      (a)->f_seek == (b)->f_seek && \
		      (a)->a_seek == (b)->a_seek)

#define	fseekp	B.Bfseekp
#define	fbobp	B.Bfbobp
#define	feobp	B.Bfeobp
#define	fblocks	B.Bfblocks
#define	fbuf	B.Bfbuf

/*
 * The shell finds commands in loops by re-seeking the input
 * For whiles, in particular, it re-seeks to the beginning of the
 * line the while was on; hence the while placement restrictions.
 */
extern struct Ain lineloc;

extern bool    cantell;		/* Is current source tellable ? */

/*
 * Input lines are parsed into doubly linked circular
 * lists of words of the following form.
 */
struct wordent {
    Char   *word;
    struct wordent *prev;
    struct wordent *next;
};

/*
 * During word building, both in the initial lexical phase and
 * when expanding $ variable substitutions, expansion by `!' and `$'
 * must be inhibited when reading ahead in routines which are themselves
 * processing `!' and `$' expansion or after characters such as `\' or in
 * quotations.  The following flags are passed to the getC routines
 * telling them which of these substitutions are appropriate for the
 * next character to be returned.
 */
#define	DODOL	1
#define	DOEXCL	2
#define	DOALL	DODOL|DOEXCL

/*
 * Labuf implements a general buffer for lookahead during lexical operations.
 * Text which is to be placed in the input stream can be stuck here.
 * We stick parsed ahead $ constructs during initial input,
 * process id's from `$$', and modified variable values (from qualifiers
 * during expansion in sh.dol.c) here.
 */
extern Char   *lap;

/*
 * Parser structure
 *
 * Each command is parsed to a tree of command structures and
 * flags are set bottom up during this process, to be propagated down
 * as needed during the semantics/execution pass (sh.sem.c).
 */
struct command {
    short   t_dtyp;		/* Type of node 		 */
#define	NODE_COMMAND	1	/* t_dcom <t_dlef >t_drit	 */
#define	NODE_PAREN	2	/* ( t_dspr ) <t_dlef >t_drit	 */
#define	NODE_PIPE	3	/* t_dlef | t_drit		 */
#define	NODE_LIST	4	/* t_dlef ; t_drit		 */
#define	NODE_OR		5	/* t_dlef || t_drit		 */
#define	NODE_AND	6	/* t_dlef && t_drit		 */
    short   t_dflg;		/* Flags, e.g. F_AMPERSAND|... 	 */
#define	F_SAVE	(F_NICE|F_TIME|F_NOHUP)	/* save these when re-doing 	 */

#define	F_AMPERSAND	(1<<0)	/* executes in background	 */
#define	F_APPEND	(1<<1)	/* output is redirected >>	 */
#define	F_PIPEIN	(1<<2)	/* input is a pipe		 */
#define	F_PIPEOUT	(1<<3)	/* output is a pipe		 */
#define	F_NOFORK	(1<<4)	/* don't fork, last ()ized cmd	 */
#define	F_NOINTERRUPT	(1<<5)	/* should be immune from intr's */
/* spare */
#define	F_STDERR	(1<<7)	/* redirect unit 2 with unit 1	 */
#define	F_OVERWRITE	(1<<8)	/* output was !			 */
#define	F_READ		(1<<9)	/* input redirection is <<	 */
#define	F_REPEAT	(1<<10)	/* reexec aft if, repeat,...	 */
#define	F_NICE		(1<<11)	/* t_nice is meaningful 	 */
#define	F_NOHUP		(1<<12)	/* nohup this command 		 */
#define	F_TIME		(1<<13)	/* time this command 		 */
    union {
	Char   *T_dlef;		/* Input redirect word 		 */
	struct command *T_dcar;	/* Left part of list/pipe 	 */
    }       L;
    union {
	Char   *T_drit;		/* Output redirect word 	 */
	struct command *T_dcdr;	/* Right part of list/pipe 	 */
    }       R;
#define	t_dlef	L.T_dlef
#define	t_dcar	L.T_dcar
#define	t_drit	R.T_drit
#define	t_dcdr	R.T_dcdr
    Char  **t_dcom;		/* Command/argument vector 	 */
    struct command *t_dspr;	/* Pointer to ()'d subtree 	 */
    int   t_nice;
};


/*
 * These are declared here because they want to be
 * initialized in sh.init.c (to allow them to be made readonly)
 */

extern struct biltins {
    char   *bname;
    void    (*bfunct)(Char **, struct command *);
    short   minargs, maxargs;
}       bfunc[];
extern int nbfunc;

extern struct srch {
    char   *s_name;
    short   s_value;
}       srchn[];
extern int nsrchn;

/*
 * The keywords for the parser
 */
#define	T_BREAK		0
#define	T_BRKSW		1
#define	T_CASE		2
#define	T_DEFAULT 	3
#define	T_ELSE		4
#define	T_END		5
#define	T_ENDIF		6
#define	T_ENDSW		7
#define	T_EXIT		8
#define	T_FOREACH	9
#define	T_GOTO		10
#define	T_IF		11
#define	T_LABEL		12
#define	T_LET		13
#define	T_SET		14
#define	T_SWITCH	15
#define	T_TEST		16
#define	T_THEN		17
#define	T_WHILE		18

/*
 * Structure defining the existing while/foreach loops at this
 * source level.  Loops are implemented by seeking back in the
 * input.  For foreach (fe), the word list is attached here.
 */
extern struct whyle {
    struct Ain   w_start;	/* Point to restart loop */
    struct Ain   w_end;		/* End of loop (0 if unknown) */
    Char  **w_fe, **w_fe0;	/* Current/initial wordlist for fe */
    Char   *w_fename;		/* Name for fe */
    struct whyle *w_next;	/* Next (more outer) loop */
}      *whyles;

/*
 * Variable structure
 *
 * Aliases and variables are stored in AVL balanced binary trees.
 */
extern struct varent {
    Char  **vec;		/* Array of words which is the value */
    Char   *v_name;		/* Name of variable/alias */
    struct varent *v_link[3];	/* The links, see below */
    int     v_bal;		/* Balance factor */
}       shvhed, aliases;

#define v_left		v_link[0]
#define v_right		v_link[1]
#define v_parent	v_link[2]

struct varent *adrof1(Char *, struct varent *);

#define adrof(v)	adrof1(v, &shvhed)
#define value(v)	value1(v, &shvhed)

/*
 * The following are for interfacing redo substitution in
 * aliases to the lexical routines.
 */
extern struct wordent *alhistp;	/* Argument list (first) */
extern struct wordent *alhistt;	/* Node after last in arg list */
extern Char  **alvec, *alvecp;	/* The (remnants of) alias vector */

/*
 * Filename/command name expansion variables
 */
extern int   gflag;		/* After tglob -> is globbing needed? */

#define MAXVARLEN 30		/* Maximum number of char in a variable name */

/*
 * Variables for filename expansion
 */
extern Char **gargv;		/* Pointer to the (stack) arglist */
extern long gargc;		/* Number args in gargv */

/*
 * Variables for command expansion.
 */
extern Char **pargv;		/* Pointer to the argv list space */
extern long pargc;		/* Count of arguments in pargv */
extern Char   *pargs;		/* Pointer to start current word */
extern long    pnleft;		/* Number of chars left in pargs */
extern Char   *pargcp;		/* Current index into pargs */

/*
 * History list
 *
 * Each history list entry contains an embedded wordlist
 * from the scanner, a number for the event, and a reference count
 * to aid in discarding old entries.
 *
 * Essentially "invisible" entries are put on the history list
 * when history substitution includes modifiers, and thrown away
 * at the next discarding since their event numbers are very negative.
 */
extern struct Hist {
    struct wordent Hlex;
    int     Hnum;
    int     Href;
    struct Hist *Hnext;
}       Histlist;

extern struct wordent paraml;	/* Current lexical word list */
extern int     eventno;		/* Next events number */
extern int     lastev;		/* Last event reference (default) */

extern Char    HIST;		/* history invocation character */
extern Char    HISTSUB;		/* auto-substitute character */

/*
 * setname is a macro to save space (see sh.err.c)
 */
extern char   *bname;

#define	setname(a)	(bname = (a))

extern Char   *Vsav;
extern Char   *Vdp;
extern Char   *Vexpath;
extern char  **Vt;

extern Char  **evalvec;
extern Char   *evalp;

/* word_chars is set by default to WORD_CHARS but can be overridden by
   the worchars variable--if unset, reverts to WORD_CHARS */

extern Char   *word_chars;

#define WORD_CHARS "*?_-.[]~="	/* default chars besides alnums in words */

extern Char   *STR_SHELLPATH;

#include <paths.h>
extern Char   *STR_BSHELL;
extern Char   *STR_WORD_CHARS;
extern Char  **STR_environ;
