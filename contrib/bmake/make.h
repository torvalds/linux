/*	$NetBSD: make.h,v 1.104 2018/02/12 21:38:09 sjg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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
 *	from: @(#)make.h	8.3 (Berkeley) 6/13/95
 */

/*
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)make.h	8.3 (Berkeley) 6/13/95
 */

/*-
 * make.h --
 *	The global definitions for pmake
 */

#ifndef _MAKE_H_
#define _MAKE_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/param.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <unistd.h>
#include <sys/cdefs.h>

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#if defined(__GNUC__)
#define	MAKE_GNUC_PREREQ(x, y)						\
	((__GNUC__ == (x) && __GNUC_MINOR__ >= (y)) ||			\
	 (__GNUC__ > (x)))
#else /* defined(__GNUC__) */
#define	MAKE_GNUC_PREREQ(x, y)	0
#endif /* defined(__GNUC__) */

#if MAKE_GNUC_PREREQ(2, 7)
#define	MAKE_ATTR_UNUSED	__attribute__((__unused__))
#else
#define	MAKE_ATTR_UNUSED	/* delete */
#endif

#if MAKE_GNUC_PREREQ(2, 5)
#define	MAKE_ATTR_DEAD		__attribute__((__noreturn__))
#elif defined(__GNUC__)
#define	MAKE_ATTR_DEAD		__volatile
#else
#define	MAKE_ATTR_DEAD		/* delete */
#endif

#if MAKE_GNUC_PREREQ(2, 7)
#define MAKE_ATTR_PRINTFLIKE(fmtarg, firstvararg)	\
	    __attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#else
#define MAKE_ATTR_PRINTFLIKE(fmtarg, firstvararg)	/* delete */
#endif

#include "sprite.h"
#include "lst.h"
#include "hash.h"
#include "make-conf.h"
#include "buf.h"
#include "make_malloc.h"

/*
 * some vendors don't have this --sjg
 */
#if defined(S_IFDIR) && !defined(S_ISDIR)
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#if defined(sun) && (defined(__svr4__) || defined(__SVR4))
#define POSIX_SIGNALS
#endif

/*-
 * The structure for an individual graph node. Each node has several
 * pieces of data associated with it.
 *	1) the name of the target it describes
 *	2) the location of the target file in the file system.
 *	3) the type of operator used to define its sources (qv. parse.c)
 *	4) whether it is involved in this invocation of make
 *	5) whether the target has been remade
 *	6) whether any of its children has been remade
 *	7) the number of its children that are, as yet, unmade
 *	8) its modification time
 *	9) the modification time of its youngest child (qv. make.c)
 *	10) a list of nodes for which this is a source (parents)
 *	11) a list of nodes on which this depends (children)
 *	12) a list of nodes that depend on this, as gleaned from the
 *	    transformation rules (iParents)
 *	13) a list of ancestor nodes, which includes parents, iParents,
 *	    and recursive parents of parents
 *	14) a list of nodes of the same name created by the :: operator
 *	15) a list of nodes that must be made (if they're made) before
 *	    this node can be, but that do not enter into the datedness of
 *	    this node.
 *	16) a list of nodes that must be made (if they're made) before
 *	    this node or any child of this node can be, but that do not
 *	    enter into the datedness of this node.
 *	17) a list of nodes that must be made (if they're made) after
 *	    this node is, but that do not depend on this node, in the
 *	    normal sense.
 *	18) a Lst of ``local'' variables that are specific to this target
 *	   and this target only (qv. var.c [$@ $< $?, etc.])
 *	19) a Lst of strings that are commands to be given to a shell
 *	   to create this target.
 */
typedef struct GNode {
    char            *name;     	/* The target's name */
    char            *uname;    	/* The unexpanded name of a .USE node */
    char    	    *path;     	/* The full pathname of the file */
    int             type;      	/* Its type (see the OP flags, below) */

    int             flags;
#define REMAKE		0x1    	/* this target needs to be (re)made */
#define	CHILDMADE	0x2	/* children of this target were made */
#define FORCE		0x4	/* children don't exist, and we pretend made */
#define DONE_WAIT	0x8	/* Set by Make_ProcessWait() */
#define DONE_ORDER	0x10	/* Build requested by .ORDER processing */
#define FROM_DEPEND	0x20	/* Node created from .depend */
#define DONE_ALLSRC	0x40	/* We do it once only */
#define CYCLE		0x1000  /* Used by MakePrintStatus */
#define DONECYCLE	0x2000  /* Used by MakePrintStatus */
#define INTERNAL	0x4000	/* Internal use only */
    enum enum_made {
	UNMADE, DEFERRED, REQUESTED, BEINGMADE,
	MADE, UPTODATE, ERROR, ABORTED
    }	    	    made;    	/* Set to reflect the state of processing
				 * on this node:
				 *  UNMADE - Not examined yet
				 *  DEFERRED - Examined once (building child)
				 *  REQUESTED - on toBeMade list
				 *  BEINGMADE - Target is already being made.
				 *  	Indicates a cycle in the graph.
				 *  MADE - Was out-of-date and has been made
				 *  UPTODATE - Was already up-to-date
				 *  ERROR - An error occurred while it was being
				 *  	made (used only in compat mode)
				 *  ABORTED - The target was aborted due to
				 *  	an error making an inferior (compat).
				 */
    int             unmade;    	/* The number of unmade children */

    time_t          mtime;     	/* Its modification time */
    struct GNode    *cmgn;    	/* The youngest child */

    Lst     	    iParents;  	/* Links to parents for which this is an
				 * implied source, if any */
    Lst	    	    cohorts;  	/* Other nodes for the :: operator */
    Lst             parents;   	/* Nodes that depend on this one */
    Lst             children;  	/* Nodes on which this one depends */
    Lst             order_pred;	/* .ORDER nodes we need made */
    Lst             order_succ;	/* .ORDER nodes who need us */

    char	    cohort_num[8]; /* #n for this cohort */
    int		    unmade_cohorts;/* # of unmade instances on the
				      cohorts list */
    struct GNode    *centurion;	/* Pointer to the first instance of a ::
				   node; only set when on a cohorts list */
    unsigned int    checked;    /* Last time we tried to makle this node */

    Hash_Table      context;	/* The local variables */
    Lst             commands;  	/* Creation commands */

    struct _Suff    *suffix;	/* Suffix for the node (determined by
				 * Suff_FindDeps and opaque to everyone
				 * but the Suff module) */
    const char	    *fname;	/* filename where the GNode got defined */
    int		     lineno;	/* line number where the GNode got defined */
} GNode;

/*
 * The OP_ constants are used when parsing a dependency line as a way of
 * communicating to other parts of the program the way in which a target
 * should be made. These constants are bitwise-OR'ed together and
 * placed in the 'type' field of each node. Any node that has
 * a 'type' field which satisfies the OP_NOP function was never never on
 * the lefthand side of an operator, though it may have been on the
 * righthand side...
 */
#define OP_DEPENDS	0x00000001  /* Execution of commands depends on
				     * kids (:) */
#define OP_FORCE	0x00000002  /* Always execute commands (!) */
#define OP_DOUBLEDEP	0x00000004  /* Execution of commands depends on kids
				     * per line (::) */
#define OP_OPMASK	(OP_DEPENDS|OP_FORCE|OP_DOUBLEDEP)

#define OP_OPTIONAL	0x00000008  /* Don't care if the target doesn't
				     * exist and can't be created */
#define OP_USE		0x00000010  /* Use associated commands for parents */
#define OP_EXEC	  	0x00000020  /* Target is never out of date, but always
				     * execute commands anyway. Its time
				     * doesn't matter, so it has none...sort
				     * of */
#define OP_IGNORE	0x00000040  /* Ignore errors when creating the node */
#define OP_PRECIOUS	0x00000080  /* Don't remove the target when
				     * interrupted */
#define OP_SILENT	0x00000100  /* Don't echo commands when executed */
#define OP_MAKE		0x00000200  /* Target is a recursive make so its
				     * commands should always be executed when
				     * it is out of date, regardless of the
				     * state of the -n or -t flags */
#define OP_JOIN 	0x00000400  /* Target is out-of-date only if any of its
				     * children was out-of-date */
#define	OP_MADE		0x00000800  /* Assume the children of the node have
				     * been already made */
#define OP_SPECIAL	0x00001000  /* Special .BEGIN, .END, .INTERRUPT */
#define	OP_USEBEFORE	0x00002000  /* Like .USE, only prepend commands */
#define OP_INVISIBLE	0x00004000  /* The node is invisible to its parents.
				     * I.e. it doesn't show up in the parents's
				     * local variables. */
#define OP_NOTMAIN	0x00008000  /* The node is exempt from normal 'main
				     * target' processing in parse.c */
#define OP_PHONY	0x00010000  /* Not a file target; run always */
#define OP_NOPATH	0x00020000  /* Don't search for file in the path */
#define OP_WAIT 	0x00040000  /* .WAIT phony node */
#define OP_NOMETA	0x00080000  /* .NOMETA do not create a .meta file */
#define OP_META		0x00100000  /* .META we _do_ want a .meta file */
#define OP_NOMETA_CMP	0x00200000  /* Do not compare commands in .meta file */
#define OP_SUBMAKE	0x00400000  /* Possibly a submake node */
/* Attributes applied by PMake */
#define OP_TRANSFORM	0x80000000  /* The node is a transformation rule */
#define OP_MEMBER 	0x40000000  /* Target is a member of an archive */
#define OP_LIB	  	0x20000000  /* Target is a library */
#define OP_ARCHV  	0x10000000  /* Target is an archive construct */
#define OP_HAS_COMMANDS	0x08000000  /* Target has all the commands it should.
				     * Used when parsing to catch multiple
				     * commands for a target */
#define OP_SAVE_CMDS	0x04000000  /* Saving commands on .END (Compat) */
#define OP_DEPS_FOUND	0x02000000  /* Already processed by Suff_FindDeps */
#define	OP_MARK		0x01000000  /* Node found while expanding .ALLSRC */

#define NoExecute(gn) ((gn->type & OP_MAKE) ? noRecursiveExecute : noExecute)
/*
 * OP_NOP will return TRUE if the node with the given type was not the
 * object of a dependency operator
 */
#define OP_NOP(t)	(((t) & OP_OPMASK) == 0x00000000)

#define OP_NOTARGET (OP_NOTMAIN|OP_USE|OP_EXEC|OP_TRANSFORM)

/*
 * The TARG_ constants are used when calling the Targ_FindNode and
 * Targ_FindList functions in targ.c. They simply tell the functions what to
 * do if the desired node(s) is (are) not found. If the TARG_CREATE constant
 * is given, a new, empty node will be created for the target, placed in the
 * table of all targets and its address returned. If TARG_NOCREATE is given,
 * a NULL pointer will be returned.
 */
#define TARG_NOCREATE	0x00	  /* don't create it */
#define TARG_CREATE	0x01	  /* create node if not found */
#define TARG_NOHASH	0x02	  /* don't look in/add to hash table */

/*
 * These constants are all used by the Str_Concat function to decide how the
 * final string should look. If STR_ADDSPACE is given, a space will be
 * placed between the two strings. If STR_ADDSLASH is given, a '/' will
 * be used instead of a space. If neither is given, no intervening characters
 * will be placed between the two strings in the final output. If the
 * STR_DOFREE bit is set, the two input strings will be freed before
 * Str_Concat returns.
 */
#define STR_ADDSPACE	0x01	/* add a space when Str_Concat'ing */
#define STR_ADDSLASH	0x02	/* add a slash when Str_Concat'ing */

/*
 * Error levels for parsing. PARSE_FATAL means the process cannot continue
 * once the makefile has been parsed. PARSE_WARNING means it can. Passed
 * as the first argument to Parse_Error.
 */
#define PARSE_INFO	3
#define PARSE_WARNING	2
#define PARSE_FATAL	1

/*
 * Values returned by Cond_Eval.
 */
#define COND_PARSE	0   	/* Parse the next lines */
#define COND_SKIP 	1   	/* Skip the next lines */
#define COND_INVALID	2   	/* Not a conditional statement */

/*
 * Definitions for the "local" variables. Used only for clarity.
 */
#define TARGET	  	  "@" 	/* Target of dependency */
#define OODATE	  	  "?" 	/* All out-of-date sources */
#define ALLSRC	  	  ">" 	/* All sources */
#define IMPSRC	  	  "<" 	/* Source implied by transformation */
#define PREFIX	  	  "*" 	/* Common prefix */
#define ARCHIVE	  	  "!" 	/* Archive in "archive(member)" syntax */
#define MEMBER	  	  "%" 	/* Member in "archive(member)" syntax */

#define FTARGET           "@F"  /* file part of TARGET */
#define DTARGET           "@D"  /* directory part of TARGET */
#define FIMPSRC           "<F"  /* file part of IMPSRC */
#define DIMPSRC           "<D"  /* directory part of IMPSRC */
#define FPREFIX           "*F"  /* file part of PREFIX */
#define DPREFIX           "*D"  /* directory part of PREFIX */

/*
 * Global Variables
 */
extern Lst  	create;	    	/* The list of target names specified on the
				 * command line. used to resolve #if
				 * make(...) statements */
extern Lst     	dirSearchPath; 	/* The list of directories to search when
				 * looking for targets */

extern Boolean	compatMake;	/* True if we are make compatible */
extern Boolean	ignoreErrors;  	/* True if should ignore all errors */
extern Boolean  beSilent;    	/* True if should print no commands */
extern Boolean  noExecute;    	/* True if should execute nothing */
extern Boolean  noRecursiveExecute;    	/* True if should execute nothing */
extern Boolean  allPrecious;   	/* True if every target is precious */
extern Boolean  deleteOnError;	/* True if failed targets should be deleted */
extern Boolean  keepgoing;    	/* True if should continue on unaffected
				 * portions of the graph when have an error
				 * in one portion */
extern Boolean 	touchFlag;    	/* TRUE if targets should just be 'touched'
				 * if out of date. Set by the -t flag */
extern Boolean 	queryFlag;    	/* TRUE if we aren't supposed to really make
				 * anything, just see if the targets are out-
				 * of-date */
extern Boolean	doing_depend;	/* TRUE if processing .depend */

extern Boolean	checkEnvFirst;	/* TRUE if environment should be searched for
				 * variables before the global context */
extern Boolean	jobServer;	/* a jobServer already exists */

extern Boolean	parseWarnFatal;	/* TRUE if makefile parsing warnings are
				 * treated as errors */

extern Boolean	varNoExportEnv;	/* TRUE if we should not export variables
				 * set on the command line to the env. */

extern GNode    *DEFAULT;    	/* .DEFAULT rule */

extern GNode	*VAR_INTERNAL;	/* Variables defined internally by make
				 * which should not override those set by
				 * makefiles.
				 */
extern GNode    *VAR_GLOBAL;   	/* Variables defined in a global context, e.g
				 * in the Makefile itself */
extern GNode    *VAR_CMD;    	/* Variables defined on the command line */
extern GNode	*VAR_FOR;	/* Iteration variables */
extern char    	var_Error[];   	/* Value returned by Var_Parse when an error
				 * is encountered. It actually points to
				 * an empty string, so naive callers needn't
				 * worry about it. */

extern time_t 	now;	    	/* The time at the start of this whole
				 * process */

extern Boolean	oldVars;    	/* Do old-style variable substitution */

extern Lst	sysIncPath;	/* The system include path. */
extern Lst	defIncPath;	/* The default include path. */

extern char	curdir[];	/* Startup directory */
extern char	*progname;	/* The program name */
extern char	*makeDependfile; /* .depend */
extern char	**savedEnv;	 /* if we replaced environ this will be non-NULL */

/*
 * We cannot vfork() in a child of vfork().
 * Most systems do not enforce this but some do.
 */
#define vFork() ((getpid() == myPid) ? vfork() : fork())
extern pid_t	myPid;

#define	MAKEFLAGS	".MAKEFLAGS"
#define	MAKEOVERRIDES	".MAKEOVERRIDES"
#define	MAKE_JOB_PREFIX	".MAKE.JOB.PREFIX" /* prefix for job target output */
#define	MAKE_EXPORTED	".MAKE.EXPORTED"   /* variables we export */
#define	MAKE_MAKEFILES	".MAKE.MAKEFILES"  /* all the makefiles we read */
#define	MAKE_LEVEL	".MAKE.LEVEL"	   /* recursion level */
#define MAKEFILE_PREFERENCE ".MAKE.MAKEFILE_PREFERENCE"
#define MAKE_DEPENDFILE	".MAKE.DEPENDFILE" /* .depend */
#define MAKE_MODE	".MAKE.MODE"
#ifndef MAKE_LEVEL_ENV
# define MAKE_LEVEL_ENV	"MAKELEVEL"
#endif

/*
 * debug control:
 *	There is one bit per module.  It is up to the module what debug
 *	information to print.
 */
FILE *debug_file;		/* Output written here - default stdout */
extern int debug;
#define	DEBUG_ARCH	0x00001
#define	DEBUG_COND	0x00002
#define	DEBUG_DIR	0x00004
#define	DEBUG_GRAPH1	0x00008
#define	DEBUG_GRAPH2	0x00010
#define	DEBUG_JOB	0x00020
#define	DEBUG_MAKE	0x00040
#define	DEBUG_SUFF	0x00080
#define	DEBUG_TARG	0x00100
#define	DEBUG_VAR	0x00200
#define DEBUG_FOR	0x00400
#define DEBUG_SHELL	0x00800
#define DEBUG_ERROR	0x01000
#define DEBUG_LOUD	0x02000
#define DEBUG_META	0x04000

#define DEBUG_GRAPH3	0x10000
#define DEBUG_SCRIPT	0x20000
#define DEBUG_PARSE	0x40000
#define DEBUG_CWD	0x80000

#define CONCAT(a,b)	a##b

#define	DEBUG(module)	(debug & CONCAT(DEBUG_,module))

#include "nonints.h"

int Make_TimeStamp(GNode *, GNode *);
Boolean Make_OODate(GNode *);
void Make_ExpandUse(Lst);
time_t Make_Recheck(GNode *);
void Make_HandleUse(GNode *, GNode *);
void Make_Update(GNode *);
void Make_DoAllVar(GNode *);
Boolean Make_Run(Lst);
char * Check_Cwd_Cmd(const char *);
void Check_Cwd(const char **);
void PrintOnError(GNode *, const char *);
void Main_ExportMAKEFLAGS(Boolean);
Boolean Main_SetObjdir(const char *, ...) MAKE_ATTR_PRINTFLIKE(1, 2);
int mkTempFile(const char *, char **);
int str2Lst_Append(Lst, char *, const char *);
int cached_lstat(const char *, void *);
int cached_stat(const char *, void *);

#define	VARF_UNDEFERR	1
#define	VARF_WANTRES	2
#define	VARF_ASSIGN	4

#ifdef __GNUC__
#define UNCONST(ptr)	({ 		\
    union __unconst {			\
	const void *__cp;		\
	void *__p;			\
    } __d;				\
    __d.__cp = ptr, __d.__p; })
#else
#define UNCONST(ptr)	(void *)(ptr)
#endif

#ifndef MIN
#define MIN(a, b) ((a < b) ? a : b)
#endif
#ifndef MAX
#define MAX(a, b) ((a > b) ? a : b)
#endif

/* At least GNU/Hurd systems lack hardcoded MAXPATHLEN/PATH_MAX */
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifndef MAXPATHLEN
#define MAXPATHLEN	BMAKE_PATH_MAX
#endif
#ifndef PATH_MAX
#define PATH_MAX	MAXPATHLEN
#endif

#if defined(SYSV)
#define KILLPG(pid, sig)	kill(-(pid), (sig))
#else
#define KILLPG(pid, sig)	killpg((pid), (sig))
#endif

#endif /* _MAKE_H_ */
