/*	$NetBSD: parse.c,v 1.231 2018/12/22 00:36:32 sjg Exp $	*/

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
 */

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: parse.c,v 1.231 2018/12/22 00:36:32 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)parse.c	8.3 (Berkeley) 3/19/94";
#else
__RCSID("$NetBSD: parse.c,v 1.231 2018/12/22 00:36:32 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * parse.c --
 *	Functions to parse a makefile.
 *
 *	One function, Parse_Init, must be called before any functions
 *	in this module are used. After that, the function Parse_File is the
 *	main entry point and controls most of the other functions in this
 *	module.
 *
 *	Most important structures are kept in Lsts. Directories for
 *	the .include "..." function are kept in the 'parseIncPath' Lst, while
 *	those for the .include <...> are kept in the 'sysIncPath' Lst. The
 *	targets currently being defined are kept in the 'targets' Lst.
 *
 *	The variables 'fname' and 'lineno' are used to track the name
 *	of the current file and the line number in that file so that error
 *	messages can be more meaningful.
 *
 * Interface:
 *	Parse_Init	    	    Initialization function which must be
 *	    	  	    	    called before anything else in this module
 *	    	  	    	    is used.
 *
 *	Parse_End		    Cleanup the module
 *
 *	Parse_File	    	    Function used to parse a makefile. It must
 *	    	  	    	    be given the name of the file, which should
 *	    	  	    	    already have been opened, and a function
 *	    	  	    	    to call to read a character from the file.
 *
 *	Parse_IsVar	    	    Returns TRUE if the given line is a
 *	    	  	    	    variable assignment. Used by MainParseArgs
 *	    	  	    	    to determine if an argument is a target
 *	    	  	    	    or a variable assignment. Used internally
 *	    	  	    	    for pretty much the same thing...
 *
 *	Parse_Error	    	    Function called when an error occurs in
 *	    	  	    	    parsing. Used by the variable and
 *	    	  	    	    conditional modules.
 *	Parse_MainName	    	    Returns a Lst of the main target to create.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "make.h"
#include "hash.h"
#include "dir.h"
#include "job.h"
#include "buf.h"
#include "pathnames.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_MMAP
#include <sys/mman.h>

#ifndef MAP_COPY
#define MAP_COPY MAP_PRIVATE
#endif
#ifndef MAP_FILE
#define MAP_FILE 0
#endif
#endif

////////////////////////////////////////////////////////////
// types and constants

/*
 * Structure for a file being read ("included file")
 */
typedef struct IFile {
    char      	    *fname;         /* name of file */
    int             lineno;         /* current line number in file */
    int             first_lineno;   /* line number of start of text */
    int             cond_depth;     /* 'if' nesting when file opened */
    Boolean         depending;      /* state of doing_depend on EOF */
    char            *P_str;         /* point to base of string buffer */
    char            *P_ptr;         /* point to next char of string buffer */
    char            *P_end;         /* point to the end of string buffer */
    char            *(*nextbuf)(void *, size_t *); /* Function to get more data */
    void            *nextbuf_arg;   /* Opaque arg for nextbuf() */
    struct loadedfile *lf;          /* loadedfile object, if any */
} IFile;


/*
 * These values are returned by ParseEOF to tell Parse_File whether to
 * CONTINUE parsing, i.e. it had only reached the end of an include file,
 * or if it's DONE.
 */
#define CONTINUE	1
#define DONE		0

/*
 * Tokens for target attributes
 */
typedef enum {
    Begin,  	    /* .BEGIN */
    Default,	    /* .DEFAULT */
    DeleteOnError,  /* .DELETE_ON_ERROR */
    End,    	    /* .END */
    dotError,	    /* .ERROR */
    Ignore,	    /* .IGNORE */
    Includes,	    /* .INCLUDES */
    Interrupt,	    /* .INTERRUPT */
    Libs,	    /* .LIBS */
    Meta,	    /* .META */
    MFlags,	    /* .MFLAGS or .MAKEFLAGS */
    Main,	    /* .MAIN and we don't have anything user-specified to
		     * make */
    NoExport,	    /* .NOEXPORT */
    NoMeta,	    /* .NOMETA */
    NoMetaCmp,	    /* .NOMETA_CMP */
    NoPath,	    /* .NOPATH */
    Not,	    /* Not special */
    NotParallel,    /* .NOTPARALLEL */
    Null,   	    /* .NULL */
    ExObjdir,	    /* .OBJDIR */
    Order,  	    /* .ORDER */
    Parallel,	    /* .PARALLEL */
    ExPath,	    /* .PATH */
    Phony,	    /* .PHONY */
#ifdef POSIX
    Posix,	    /* .POSIX */
#endif
    Precious,	    /* .PRECIOUS */
    ExShell,	    /* .SHELL */
    Silent,	    /* .SILENT */
    SingleShell,    /* .SINGLESHELL */
    Stale,	    /* .STALE */
    Suffixes,	    /* .SUFFIXES */
    Wait,	    /* .WAIT */
    Attribute	    /* Generic attribute */
} ParseSpecial;

/*
 * Other tokens
 */
#define LPAREN	'('
#define RPAREN	')'


////////////////////////////////////////////////////////////
// result data

/*
 * The main target to create. This is the first target on the first
 * dependency line in the first makefile.
 */
static GNode *mainNode;

////////////////////////////////////////////////////////////
// eval state

/* targets we're working on */
static Lst targets;

#ifdef CLEANUP
/* command lines for targets */
static Lst targCmds;
#endif

/*
 * specType contains the SPECial TYPE of the current target. It is
 * Not if the target is unspecial. If it *is* special, however, the children
 * are linked as children of the parent but not vice versa. This variable is
 * set in ParseDoDependency
 */
static ParseSpecial specType;

/*
 * Predecessor node for handling .ORDER. Initialized to NULL when .ORDER
 * seen, then set to each successive source on the line.
 */
static GNode	*predecessor;

////////////////////////////////////////////////////////////
// parser state

/* true if currently in a dependency line or its commands */
static Boolean inLine;

/* number of fatal errors */
static int fatals = 0;

/*
 * Variables for doing includes
 */

/* current file being read */
static IFile *curFile;

/* stack of IFiles generated by .includes */
static Lst includes;

/* include paths (lists of directories) */
Lst parseIncPath;	/* dirs for "..." includes */
Lst sysIncPath;		/* dirs for <...> includes */
Lst defIncPath;		/* default for sysIncPath */

////////////////////////////////////////////////////////////
// parser tables

/*
 * The parseKeywords table is searched using binary search when deciding
 * if a target or source is special. The 'spec' field is the ParseSpecial
 * type of the keyword ("Not" if the keyword isn't special as a target) while
 * the 'op' field is the operator to apply to the list of targets if the
 * keyword is used as a source ("0" if the keyword isn't special as a source)
 */
static const struct {
    const char   *name;    	/* Name of keyword */
    ParseSpecial  spec;	    	/* Type when used as a target */
    int	    	  op;	    	/* Operator when used as a source */
} parseKeywords[] = {
{ ".BEGIN", 	  Begin,    	0 },
{ ".DEFAULT",	  Default,  	0 },
{ ".DELETE_ON_ERROR", DeleteOnError, 0 },
{ ".END",   	  End,	    	0 },
{ ".ERROR",   	  dotError,    	0 },
{ ".EXEC",	  Attribute,   	OP_EXEC },
{ ".IGNORE",	  Ignore,   	OP_IGNORE },
{ ".INCLUDES",	  Includes, 	0 },
{ ".INTERRUPT",	  Interrupt,	0 },
{ ".INVISIBLE",	  Attribute,   	OP_INVISIBLE },
{ ".JOIN",  	  Attribute,   	OP_JOIN },
{ ".LIBS",  	  Libs,	    	0 },
{ ".MADE",	  Attribute,	OP_MADE },
{ ".MAIN",	  Main,		0 },
{ ".MAKE",  	  Attribute,   	OP_MAKE },
{ ".MAKEFLAGS",	  MFlags,   	0 },
{ ".META",	  Meta,		OP_META },
{ ".MFLAGS",	  MFlags,   	0 },
{ ".NOMETA",	  NoMeta,	OP_NOMETA },
{ ".NOMETA_CMP",  NoMetaCmp,	OP_NOMETA_CMP },
{ ".NOPATH",	  NoPath,	OP_NOPATH },
{ ".NOTMAIN",	  Attribute,   	OP_NOTMAIN },
{ ".NOTPARALLEL", NotParallel,	0 },
{ ".NO_PARALLEL", NotParallel,	0 },
{ ".NULL",  	  Null,	    	0 },
{ ".OBJDIR",	  ExObjdir,	0 },
{ ".OPTIONAL",	  Attribute,   	OP_OPTIONAL },
{ ".ORDER", 	  Order,    	0 },
{ ".PARALLEL",	  Parallel,	0 },
{ ".PATH",	  ExPath,	0 },
{ ".PHONY",	  Phony,	OP_PHONY },
#ifdef POSIX
{ ".POSIX",	  Posix,	0 },
#endif
{ ".PRECIOUS",	  Precious, 	OP_PRECIOUS },
{ ".RECURSIVE",	  Attribute,	OP_MAKE },
{ ".SHELL", 	  ExShell,    	0 },
{ ".SILENT",	  Silent,   	OP_SILENT },
{ ".SINGLESHELL", SingleShell,	0 },
{ ".STALE",	  Stale,	0 },
{ ".SUFFIXES",	  Suffixes, 	0 },
{ ".USE",   	  Attribute,   	OP_USE },
{ ".USEBEFORE",   Attribute,   	OP_USEBEFORE },
{ ".WAIT",	  Wait, 	0 },
};

////////////////////////////////////////////////////////////
// local functions

static int ParseIsEscaped(const char *, const char *);
static void ParseErrorInternal(const char *, size_t, int, const char *, ...)
    MAKE_ATTR_PRINTFLIKE(4,5);
static void ParseVErrorInternal(FILE *, const char *, size_t, int, const char *, va_list)
    MAKE_ATTR_PRINTFLIKE(5, 0);
static int ParseFindKeyword(const char *);
static int ParseLinkSrc(void *, void *);
static int ParseDoOp(void *, void *);
static void ParseDoSrc(int, const char *);
static int ParseFindMain(void *, void *);
static int ParseAddDir(void *, void *);
static int ParseClearPath(void *, void *);
static void ParseDoDependency(char *);
static int ParseAddCmd(void *, void *);
static void ParseHasCommands(void *);
static void ParseDoInclude(char *);
static void ParseSetParseFile(const char *);
static void ParseSetIncludedFile(void);
#ifdef GMAKEEXPORT
static void ParseGmakeExport(char *);
#endif
static int ParseEOF(void);
static char *ParseReadLine(void);
static void ParseFinishLine(void);
static void ParseMark(GNode *);

////////////////////////////////////////////////////////////
// file loader

struct loadedfile {
	const char *path;		/* name, for error reports */
	char *buf;			/* contents buffer */
	size_t len;			/* length of contents */
	size_t maplen;			/* length of mmap area, or 0 */
	Boolean used;			/* XXX: have we used the data yet */
};

/*
 * Constructor/destructor for loadedfile
 */
static struct loadedfile *
loadedfile_create(const char *path)
{
	struct loadedfile *lf;

	lf = bmake_malloc(sizeof(*lf));
	lf->path = (path == NULL ? "(stdin)" : path);
	lf->buf = NULL;
	lf->len = 0;
	lf->maplen = 0;
	lf->used = FALSE;
	return lf;
}

static void
loadedfile_destroy(struct loadedfile *lf)
{
	if (lf->buf != NULL) {
		if (lf->maplen > 0) {
#ifdef HAVE_MMAP
			munmap(lf->buf, lf->maplen);
#endif
		} else {
			free(lf->buf);
		}
	}
	free(lf);
}

/*
 * nextbuf() operation for loadedfile, as needed by the weird and twisted
 * logic below. Once that's cleaned up, we can get rid of lf->used...
 */
static char *
loadedfile_nextbuf(void *x, size_t *len)
{
	struct loadedfile *lf = x;

	if (lf->used) {
		return NULL;
	}
	lf->used = TRUE;
	*len = lf->len;
	return lf->buf;
}

/*
 * Try to get the size of a file.
 */
static ReturnStatus
load_getsize(int fd, size_t *ret)
{
	struct stat st;

	if (fstat(fd, &st) < 0) {
		return FAILURE;
	}

	if (!S_ISREG(st.st_mode)) {
		return FAILURE;
	}

	/*
	 * st_size is an off_t, which is 64 bits signed; *ret is
	 * size_t, which might be 32 bits unsigned or 64 bits
	 * unsigned. Rather than being elaborate, just punt on
	 * files that are more than 2^31 bytes. We should never
	 * see a makefile that size in practice...
	 *
	 * While we're at it reject negative sizes too, just in case.
	 */
	if (st.st_size < 0 || st.st_size > 0x7fffffff) {
		return FAILURE;
	}

	*ret = (size_t) st.st_size;
	return SUCCESS;
}

/*
 * Read in a file.
 *
 * Until the path search logic can be moved under here instead of
 * being in the caller in another source file, we need to have the fd
 * passed in already open. Bleh.
 *
 * If the path is NULL use stdin and (to insure against fd leaks)
 * assert that the caller passed in -1.
 */
static struct loadedfile *
loadfile(const char *path, int fd)
{
	struct loadedfile *lf;
#ifdef HAVE_MMAP
	static long pagesize = 0;
#endif
	ssize_t result;
	size_t bufpos;

	lf = loadedfile_create(path);

	if (path == NULL) {
		assert(fd == -1);
		fd = STDIN_FILENO;
	} else {
#if 0 /* notyet */
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			...
			Error("%s: %s", path, strerror(errno));
			exit(1);
		}
#endif
	}

#ifdef HAVE_MMAP
	if (load_getsize(fd, &lf->len) == SUCCESS) {
		/* found a size, try mmap */
#ifdef _SC_PAGESIZE
		if (pagesize == 0)
			pagesize = sysconf(_SC_PAGESIZE);
#endif
		if (pagesize <= 0) {
			pagesize = 0x1000;
		}
		/* round size up to a page */
		lf->maplen = pagesize * ((lf->len + pagesize - 1)/pagesize);

		/*
		 * XXX hack for dealing with empty files; remove when
		 * we're no longer limited by interfacing to the old
		 * logic elsewhere in this file.
		 */
		if (lf->maplen == 0) {
			lf->maplen = pagesize;
		}

		/*
		 * FUTURE: remove PROT_WRITE when the parser no longer
		 * needs to scribble on the input.
		 */
		lf->buf = mmap(NULL, lf->maplen, PROT_READ|PROT_WRITE,
			       MAP_FILE|MAP_COPY, fd, 0);
		if (lf->buf != MAP_FAILED) {
			/* succeeded */
			if (lf->len == lf->maplen && lf->buf[lf->len - 1] != '\n') {
				char *b = bmake_malloc(lf->len + 1);
				b[lf->len] = '\n';
				memcpy(b, lf->buf, lf->len++);
				munmap(lf->buf, lf->maplen);
				lf->maplen = 0;
				lf->buf = b;
			}
			goto done;
		}
	}
#endif
	/* cannot mmap; load the traditional way */

	lf->maplen = 0;
	lf->len = 1024;
	lf->buf = bmake_malloc(lf->len);

	bufpos = 0;
	while (1) {
		assert(bufpos <= lf->len);
		if (bufpos == lf->len) {
			if (lf->len > SIZE_MAX/2) {
				errno = EFBIG;
				Error("%s: file too large", path);
				exit(1);
			}
			lf->len *= 2;
			lf->buf = bmake_realloc(lf->buf, lf->len);
		}
		assert(bufpos < lf->len);
		result = read(fd, lf->buf + bufpos, lf->len - bufpos);
		if (result < 0) {
			Error("%s: read error: %s", path, strerror(errno));
			exit(1);
		}
		if (result == 0) {
			break;
		}
		bufpos += result;
	}
	assert(bufpos <= lf->len);
	lf->len = bufpos;

	/* truncate malloc region to actual length (maybe not useful) */
	if (lf->len > 0) {
		/* as for mmap case, ensure trailing \n */
		if (lf->buf[lf->len - 1] != '\n')
			lf->len++;
		lf->buf = bmake_realloc(lf->buf, lf->len);
		lf->buf[lf->len - 1] = '\n';
	}

#ifdef HAVE_MMAP
done:
#endif
	if (path != NULL) {
		close(fd);
	}
	return lf;
}

////////////////////////////////////////////////////////////
// old code

/*-
 *----------------------------------------------------------------------
 * ParseIsEscaped --
 *	Check if the current character is escaped on the current line
 *
 * Results:
 *	0 if the character is not backslash escaped, 1 otherwise
 *
 * Side Effects:
 *	None
 *----------------------------------------------------------------------
 */
static int
ParseIsEscaped(const char *line, const char *c)
{
    int active = 0;
    for (;;) {
	if (line == c)
	    return active;
	if (*--c != '\\')
	    return active;
	active = !active;
    }
}

/*-
 *----------------------------------------------------------------------
 * ParseFindKeyword --
 *	Look in the table of keywords for one matching the given string.
 *
 * Input:
 *	str		String to find
 *
 * Results:
 *	The index of the keyword, or -1 if it isn't there.
 *
 * Side Effects:
 *	None
 *----------------------------------------------------------------------
 */
static int
ParseFindKeyword(const char *str)
{
    int    start, end, cur;
    int    diff;

    start = 0;
    end = (sizeof(parseKeywords)/sizeof(parseKeywords[0])) - 1;

    do {
	cur = start + ((end - start) / 2);
	diff = strcmp(str, parseKeywords[cur].name);

	if (diff == 0) {
	    return (cur);
	} else if (diff < 0) {
	    end = cur - 1;
	} else {
	    start = cur + 1;
	}
    } while (start <= end);
    return (-1);
}

/*-
 * ParseVErrorInternal  --
 *	Error message abort function for parsing. Prints out the context
 *	of the error (line number and file) as well as the message with
 *	two optional arguments.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	"fatals" is incremented if the level is PARSE_FATAL.
 */
/* VARARGS */
static void
ParseVErrorInternal(FILE *f, const char *cfname, size_t clineno, int type,
    const char *fmt, va_list ap)
{
	static Boolean fatal_warning_error_printed = FALSE;

	(void)fprintf(f, "%s: ", progname);

	if (cfname != NULL) {
		(void)fprintf(f, "\"");
		if (*cfname != '/' && strcmp(cfname, "(stdin)") != 0) {
			char *cp, *cp2;
			const char *dir, *fname;

			/*
			 * Nothing is more annoying than not knowing
			 * which Makefile is the culprit; we try ${.PARSEDIR}
			 * and apply realpath(3) if not absolute.
			 */
			dir = Var_Value(".PARSEDIR", VAR_GLOBAL, &cp);
			if (dir == NULL)
				dir = ".";
			if (*dir != '/') {
				dir = cp2 = realpath(dir, NULL);
				free(cp);
				cp = cp2; /* cp2 set to NULL by Var_Value */
			}
			fname = Var_Value(".PARSEFILE", VAR_GLOBAL, &cp2);
			if (fname == NULL) {
				if ((fname = strrchr(cfname, '/')))
					fname++;
				else
					fname = cfname;
			}
			(void)fprintf(f, "%s/%s", dir, fname);
			free(cp2);
			free(cp);
		} else
			(void)fprintf(f, "%s", cfname);

		(void)fprintf(f, "\" line %d: ", (int)clineno);
	}
	if (type == PARSE_WARNING)
		(void)fprintf(f, "warning: ");
	(void)vfprintf(f, fmt, ap);
	(void)fprintf(f, "\n");
	(void)fflush(f);
	if (type == PARSE_INFO)
		return;
	if (type == PARSE_FATAL || parseWarnFatal)
		fatals += 1;
	if (parseWarnFatal && !fatal_warning_error_printed) {
		Error("parsing warnings being treated as errors");
		fatal_warning_error_printed = TRUE;
	}
}

/*-
 * ParseErrorInternal  --
 *	Error function
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	None
 */
/* VARARGS */
static void
ParseErrorInternal(const char *cfname, size_t clineno, int type,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)fflush(stdout);
	ParseVErrorInternal(stderr, cfname, clineno, type, fmt, ap);
	va_end(ap);

	if (debug_file != stderr && debug_file != stdout) {
		va_start(ap, fmt);
		ParseVErrorInternal(debug_file, cfname, clineno, type, fmt, ap);
		va_end(ap);
	}
}

/*-
 * Parse_Error  --
 *	External interface to ParseErrorInternal; uses the default filename
 *	Line number.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	None
 */
/* VARARGS */
void
Parse_Error(int type, const char *fmt, ...)
{
	va_list ap;
	const char *fname;
	size_t lineno;

	if (curFile == NULL) {
		fname = NULL;
		lineno = 0;
	} else {
		fname = curFile->fname;
		lineno = curFile->lineno;
	}

	va_start(ap, fmt);
	(void)fflush(stdout);
	ParseVErrorInternal(stderr, fname, lineno, type, fmt, ap);
	va_end(ap);

	if (debug_file != stderr && debug_file != stdout) {
		va_start(ap, fmt);
		ParseVErrorInternal(debug_file, fname, lineno, type, fmt, ap);
		va_end(ap);
	}
}


/*
 * ParseMessage
 *	Parse a .info .warning or .error directive
 *
 *	The input is the line minus the ".".  We substitute
 *	variables, print the message and exit(1) (for .error) or just print
 *	a warning if the directive is malformed.
 */
static Boolean
ParseMessage(char *line)
{
    int mtype;

    switch(*line) {
    case 'i':
	mtype = PARSE_INFO;
	break;
    case 'w':
	mtype = PARSE_WARNING;
	break;
    case 'e':
	mtype = PARSE_FATAL;
	break;
    default:
	Parse_Error(PARSE_WARNING, "invalid syntax: \".%s\"", line);
	return FALSE;
    }

    while (isalpha((unsigned char)*line))
	line++;
    if (!isspace((unsigned char)*line))
	return FALSE;			/* not for us */
    while (isspace((unsigned char)*line))
	line++;

    line = Var_Subst(NULL, line, VAR_CMD, VARF_WANTRES);
    Parse_Error(mtype, "%s", line);
    free(line);

    if (mtype == PARSE_FATAL) {
	/* Terminate immediately. */
	exit(1);
    }
    return TRUE;
}

/*-
 *---------------------------------------------------------------------
 * ParseLinkSrc  --
 *	Link the parent node to its new child. Used in a Lst_ForEach by
 *	ParseDoDependency. If the specType isn't 'Not', the parent
 *	isn't linked as a parent of the child.
 *
 * Input:
 *	pgnp		The parent node
 *	cgpn		The child node
 *
 * Results:
 *	Always = 0
 *
 * Side Effects:
 *	New elements are added to the parents list of cgn and the
 *	children list of cgn. the unmade field of pgn is updated
 *	to reflect the additional child.
 *---------------------------------------------------------------------
 */
static int
ParseLinkSrc(void *pgnp, void *cgnp)
{
    GNode          *pgn = (GNode *)pgnp;
    GNode          *cgn = (GNode *)cgnp;

    if ((pgn->type & OP_DOUBLEDEP) && !Lst_IsEmpty (pgn->cohorts))
	pgn = (GNode *)Lst_Datum(Lst_Last(pgn->cohorts));
    (void)Lst_AtEnd(pgn->children, cgn);
    if (specType == Not)
	    (void)Lst_AtEnd(cgn->parents, pgn);
    pgn->unmade += 1;
    if (DEBUG(PARSE)) {
	fprintf(debug_file, "# %s: added child %s - %s\n", __func__,
	    pgn->name, cgn->name);
	Targ_PrintNode(pgn, 0);
	Targ_PrintNode(cgn, 0);
    }
    return (0);
}

/*-
 *---------------------------------------------------------------------
 * ParseDoOp  --
 *	Apply the parsed operator to the given target node. Used in a
 *	Lst_ForEach call by ParseDoDependency once all targets have
 *	been found and their operator parsed. If the previous and new
 *	operators are incompatible, a major error is taken.
 *
 * Input:
 *	gnp		The node to which the operator is to be applied
 *	opp		The operator to apply
 *
 * Results:
 *	Always 0
 *
 * Side Effects:
 *	The type field of the node is altered to reflect any new bits in
 *	the op.
 *---------------------------------------------------------------------
 */
static int
ParseDoOp(void *gnp, void *opp)
{
    GNode          *gn = (GNode *)gnp;
    int             op = *(int *)opp;
    /*
     * If the dependency mask of the operator and the node don't match and
     * the node has actually had an operator applied to it before, and
     * the operator actually has some dependency information in it, complain.
     */
    if (((op & OP_OPMASK) != (gn->type & OP_OPMASK)) &&
	!OP_NOP(gn->type) && !OP_NOP(op))
    {
	Parse_Error(PARSE_FATAL, "Inconsistent operator for %s", gn->name);
	return (1);
    }

    if ((op == OP_DOUBLEDEP) && ((gn->type & OP_OPMASK) == OP_DOUBLEDEP)) {
	/*
	 * If the node was the object of a :: operator, we need to create a
	 * new instance of it for the children and commands on this dependency
	 * line. The new instance is placed on the 'cohorts' list of the
	 * initial one (note the initial one is not on its own cohorts list)
	 * and the new instance is linked to all parents of the initial
	 * instance.
	 */
	GNode	*cohort;

	/*
	 * Propagate copied bits to the initial node.  They'll be propagated
	 * back to the rest of the cohorts later.
	 */
	gn->type |= op & ~OP_OPMASK;

	cohort = Targ_FindNode(gn->name, TARG_NOHASH);
	if (doing_depend)
	    ParseMark(cohort);
	/*
	 * Make the cohort invisible as well to avoid duplicating it into
	 * other variables. True, parents of this target won't tend to do
	 * anything with their local variables, but better safe than
	 * sorry. (I think this is pointless now, since the relevant list
	 * traversals will no longer see this node anyway. -mycroft)
	 */
	cohort->type = op | OP_INVISIBLE;
	(void)Lst_AtEnd(gn->cohorts, cohort);
	cohort->centurion = gn;
	gn->unmade_cohorts += 1;
	snprintf(cohort->cohort_num, sizeof cohort->cohort_num, "#%d",
		gn->unmade_cohorts);
    } else {
	/*
	 * We don't want to nuke any previous flags (whatever they were) so we
	 * just OR the new operator into the old
	 */
	gn->type |= op;
    }

    return (0);
}

/*-
 *---------------------------------------------------------------------
 * ParseDoSrc  --
 *	Given the name of a source, figure out if it is an attribute
 *	and apply it to the targets if it is. Else decide if there is
 *	some attribute which should be applied *to* the source because
 *	of some special target and apply it if so. Otherwise, make the
 *	source be a child of the targets in the list 'targets'
 *
 * Input:
 *	tOp		operator (if any) from special targets
 *	src		name of the source to handle
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Operator bits may be added to the list of targets or to the source.
 *	The targets may have a new source added to their lists of children.
 *---------------------------------------------------------------------
 */
static void
ParseDoSrc(int tOp, const char *src)
{
    GNode	*gn = NULL;
    static int wait_number = 0;
    char wait_src[16];

    if (*src == '.' && isupper ((unsigned char)src[1])) {
	int keywd = ParseFindKeyword(src);
	if (keywd != -1) {
	    int op = parseKeywords[keywd].op;
	    if (op != 0) {
		Lst_ForEach(targets, ParseDoOp, &op);
		return;
	    }
	    if (parseKeywords[keywd].spec == Wait) {
		/*
		 * We add a .WAIT node in the dependency list.
		 * After any dynamic dependencies (and filename globbing)
		 * have happened, it is given a dependency on the each
		 * previous child back to and previous .WAIT node.
		 * The next child won't be scheduled until the .WAIT node
		 * is built.
		 * We give each .WAIT node a unique name (mainly for diag).
		 */
		snprintf(wait_src, sizeof wait_src, ".WAIT_%u", ++wait_number);
		gn = Targ_FindNode(wait_src, TARG_NOHASH);
		if (doing_depend)
		    ParseMark(gn);
		gn->type = OP_WAIT | OP_PHONY | OP_DEPENDS | OP_NOTMAIN;
		Lst_ForEach(targets, ParseLinkSrc, gn);
		return;
	    }
	}
    }

    switch (specType) {
    case Main:
	/*
	 * If we have noted the existence of a .MAIN, it means we need
	 * to add the sources of said target to the list of things
	 * to create. The string 'src' is likely to be free, so we
	 * must make a new copy of it. Note that this will only be
	 * invoked if the user didn't specify a target on the command
	 * line. This is to allow #ifmake's to succeed, or something...
	 */
	(void)Lst_AtEnd(create, bmake_strdup(src));
	/*
	 * Add the name to the .TARGETS variable as well, so the user can
	 * employ that, if desired.
	 */
	Var_Append(".TARGETS", src, VAR_GLOBAL);
	return;

    case Order:
	/*
	 * Create proper predecessor/successor links between the previous
	 * source and the current one.
	 */
	gn = Targ_FindNode(src, TARG_CREATE);
	if (doing_depend)
	    ParseMark(gn);
	if (predecessor != NULL) {
	    (void)Lst_AtEnd(predecessor->order_succ, gn);
	    (void)Lst_AtEnd(gn->order_pred, predecessor);
	    if (DEBUG(PARSE)) {
		fprintf(debug_file, "# %s: added Order dependency %s - %s\n",
		    __func__, predecessor->name, gn->name);
		Targ_PrintNode(predecessor, 0);
		Targ_PrintNode(gn, 0);
	    }
	}
	/*
	 * The current source now becomes the predecessor for the next one.
	 */
	predecessor = gn;
	break;

    default:
	/*
	 * If the source is not an attribute, we need to find/create
	 * a node for it. After that we can apply any operator to it
	 * from a special target or link it to its parents, as
	 * appropriate.
	 *
	 * In the case of a source that was the object of a :: operator,
	 * the attribute is applied to all of its instances (as kept in
	 * the 'cohorts' list of the node) or all the cohorts are linked
	 * to all the targets.
	 */

	/* Find/create the 'src' node and attach to all targets */
	gn = Targ_FindNode(src, TARG_CREATE);
	if (doing_depend)
	    ParseMark(gn);
	if (tOp) {
	    gn->type |= tOp;
	} else {
	    Lst_ForEach(targets, ParseLinkSrc, gn);
	}
	break;
    }
}

/*-
 *-----------------------------------------------------------------------
 * ParseFindMain --
 *	Find a real target in the list and set it to be the main one.
 *	Called by ParseDoDependency when a main target hasn't been found
 *	yet.
 *
 * Input:
 *	gnp		Node to examine
 *
 * Results:
 *	0 if main not found yet, 1 if it is.
 *
 * Side Effects:
 *	mainNode is changed and Targ_SetMain is called.
 *
 *-----------------------------------------------------------------------
 */
static int
ParseFindMain(void *gnp, void *dummy MAKE_ATTR_UNUSED)
{
    GNode   	  *gn = (GNode *)gnp;
    if ((gn->type & OP_NOTARGET) == 0) {
	mainNode = gn;
	Targ_SetMain(gn);
	return 1;
    } else {
	return 0;
    }
}

/*-
 *-----------------------------------------------------------------------
 * ParseAddDir --
 *	Front-end for Dir_AddDir to make sure Lst_ForEach keeps going
 *
 * Results:
 *	=== 0
 *
 * Side Effects:
 *	See Dir_AddDir.
 *
 *-----------------------------------------------------------------------
 */
static int
ParseAddDir(void *path, void *name)
{
    (void)Dir_AddDir((Lst) path, (char *)name);
    return(0);
}

/*-
 *-----------------------------------------------------------------------
 * ParseClearPath --
 *	Front-end for Dir_ClearPath to make sure Lst_ForEach keeps going
 *
 * Results:
 *	=== 0
 *
 * Side Effects:
 *	See Dir_ClearPath
 *
 *-----------------------------------------------------------------------
 */
static int
ParseClearPath(void *path, void *dummy MAKE_ATTR_UNUSED)
{
    Dir_ClearPath((Lst) path);
    return 0;
}

/*-
 *---------------------------------------------------------------------
 * ParseDoDependency  --
 *	Parse the dependency line in line.
 *
 * Input:
 *	line		the line to parse
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The nodes of the sources are linked as children to the nodes of the
 *	targets. Some nodes may be created.
 *
 *	We parse a dependency line by first extracting words from the line and
 * finding nodes in the list of all targets with that name. This is done
 * until a character is encountered which is an operator character. Currently
 * these are only ! and :. At this point the operator is parsed and the
 * pointer into the line advanced until the first source is encountered.
 * 	The parsed operator is applied to each node in the 'targets' list,
 * which is where the nodes found for the targets are kept, by means of
 * the ParseDoOp function.
 *	The sources are read in much the same way as the targets were except
 * that now they are expanded using the wildcarding scheme of the C-Shell
 * and all instances of the resulting words in the list of all targets
 * are found. Each of the resulting nodes is then linked to each of the
 * targets as one of its children.
 *	Certain targets are handled specially. These are the ones detailed
 * by the specType variable.
 *	The storing of transformation rules is also taken care of here.
 * A target is recognized as a transformation rule by calling
 * Suff_IsTransform. If it is a transformation rule, its node is gotten
 * from the suffix module via Suff_AddTransform rather than the standard
 * Targ_FindNode in the target module.
 *---------------------------------------------------------------------
 */
static void
ParseDoDependency(char *line)
{
    char  	   *cp;		/* our current position */
    GNode 	   *gn = NULL;	/* a general purpose temporary node */
    int             op;		/* the operator on the line */
    char            savec;	/* a place to save a character */
    Lst    	    paths;   	/* List of search paths to alter when parsing
				 * a list of .PATH targets */
    int	    	    tOp;    	/* operator from special target */
    Lst	    	    sources;	/* list of archive source names after
				 * expansion */
    Lst 	    curTargs;	/* list of target names to be found and added
				 * to the targets list */
    char	   *lstart = line;

    if (DEBUG(PARSE))
	fprintf(debug_file, "ParseDoDependency(%s)\n", line);
    tOp = 0;

    specType = Not;
    paths = NULL;

    curTargs = Lst_Init(FALSE);

    /*
     * First, grind through the targets.
     */

    do {
	/*
	 * Here LINE points to the beginning of the next word, and
	 * LSTART points to the actual beginning of the line.
	 */

	/* Find the end of the next word. */
	for (cp = line; *cp && (ParseIsEscaped(lstart, cp) ||
		     !(isspace((unsigned char)*cp) ||
			 *cp == '!' || *cp == ':' || *cp == LPAREN));
		 cp++) {
	    if (*cp == '$') {
		/*
		 * Must be a dynamic source (would have been expanded
		 * otherwise), so call the Var module to parse the puppy
		 * so we can safely advance beyond it...There should be
		 * no errors in this, as they would have been discovered
		 * in the initial Var_Subst and we wouldn't be here.
		 */
		int 	length;
		void    *freeIt;

		(void)Var_Parse(cp, VAR_CMD, VARF_UNDEFERR|VARF_WANTRES,
				&length, &freeIt);
		free(freeIt);
		cp += length-1;
	    }
	}

	/*
	 * If the word is followed by a left parenthesis, it's the
	 * name of an object file inside an archive (ar file).
	 */
	if (!ParseIsEscaped(lstart, cp) && *cp == LPAREN) {
	    /*
	     * Archives must be handled specially to make sure the OP_ARCHV
	     * flag is set in their 'type' field, for one thing, and because
	     * things like "archive(file1.o file2.o file3.o)" are permissible.
	     * Arch_ParseArchive will set 'line' to be the first non-blank
	     * after the archive-spec. It creates/finds nodes for the members
	     * and places them on the given list, returning SUCCESS if all
	     * went well and FAILURE if there was an error in the
	     * specification. On error, line should remain untouched.
	     */
	    if (Arch_ParseArchive(&line, targets, VAR_CMD) != SUCCESS) {
		Parse_Error(PARSE_FATAL,
			     "Error in archive specification: \"%s\"", line);
		goto out;
	    } else {
		/* Done with this word; on to the next. */
		cp = line;
		continue;
	    }
	}

	if (!*cp) {
	    /*
	     * We got to the end of the line while we were still
	     * looking at targets.
	     *
	     * Ending a dependency line without an operator is a Bozo
	     * no-no.  As a heuristic, this is also often triggered by
	     * undetected conflicts from cvs/rcs merges.
	     */
	    if ((strncmp(line, "<<<<<<", 6) == 0) ||
		(strncmp(line, "======", 6) == 0) ||
		(strncmp(line, ">>>>>>", 6) == 0))
		Parse_Error(PARSE_FATAL,
		    "Makefile appears to contain unresolved cvs/rcs/??? merge conflicts");
	    else
		Parse_Error(PARSE_FATAL, lstart[0] == '.' ? "Unknown directive"
				     : "Need an operator");
	    goto out;
	}

	/* Insert a null terminator. */
	savec = *cp;
	*cp = '\0';

	/*
	 * Got the word. See if it's a special target and if so set
	 * specType to match it.
	 */
	if (*line == '.' && isupper ((unsigned char)line[1])) {
	    /*
	     * See if the target is a special target that must have it
	     * or its sources handled specially.
	     */
	    int keywd = ParseFindKeyword(line);
	    if (keywd != -1) {
		if (specType == ExPath && parseKeywords[keywd].spec != ExPath) {
		    Parse_Error(PARSE_FATAL, "Mismatched special targets");
		    goto out;
		}

		specType = parseKeywords[keywd].spec;
		tOp = parseKeywords[keywd].op;

		/*
		 * Certain special targets have special semantics:
		 *	.PATH		Have to set the dirSearchPath
		 *			variable too
		 *	.MAIN		Its sources are only used if
		 *			nothing has been specified to
		 *			create.
		 *	.DEFAULT    	Need to create a node to hang
		 *			commands on, but we don't want
		 *			it in the graph, nor do we want
		 *			it to be the Main Target, so we
		 *			create it, set OP_NOTMAIN and
		 *			add it to the list, setting
		 *			DEFAULT to the new node for
		 *			later use. We claim the node is
		 *	    	    	A transformation rule to make
		 *	    	    	life easier later, when we'll
		 *	    	    	use Make_HandleUse to actually
		 *	    	    	apply the .DEFAULT commands.
		 *	.PHONY		The list of targets
		 *	.NOPATH		Don't search for file in the path
		 *	.STALE
		 *	.BEGIN
		 *	.END
		 *	.ERROR
		 *	.DELETE_ON_ERROR
		 *	.INTERRUPT  	Are not to be considered the
		 *			main target.
		 *  	.NOTPARALLEL	Make only one target at a time.
		 *  	.SINGLESHELL	Create a shell for each command.
		 *  	.ORDER	    	Must set initial predecessor to NULL
		 */
		switch (specType) {
		case ExPath:
		    if (paths == NULL) {
			paths = Lst_Init(FALSE);
		    }
		    (void)Lst_AtEnd(paths, dirSearchPath);
		    break;
		case Main:
		    if (!Lst_IsEmpty(create)) {
			specType = Not;
		    }
		    break;
		case Begin:
		case End:
		case Stale:
		case dotError:
		case Interrupt:
		    gn = Targ_FindNode(line, TARG_CREATE);
		    if (doing_depend)
			ParseMark(gn);
		    gn->type |= OP_NOTMAIN|OP_SPECIAL;
		    (void)Lst_AtEnd(targets, gn);
		    break;
		case Default:
		    gn = Targ_NewGN(".DEFAULT");
		    gn->type |= (OP_NOTMAIN|OP_TRANSFORM);
		    (void)Lst_AtEnd(targets, gn);
		    DEFAULT = gn;
		    break;
		case DeleteOnError:
		    deleteOnError = TRUE;
		    break;
		case NotParallel:
		    maxJobs = 1;
		    break;
		case SingleShell:
		    compatMake = TRUE;
		    break;
		case Order:
		    predecessor = NULL;
		    break;
		default:
		    break;
		}
	    } else if (strncmp(line, ".PATH", 5) == 0) {
		/*
		 * .PATH<suffix> has to be handled specially.
		 * Call on the suffix module to give us a path to
		 * modify.
		 */
		Lst 	path;

		specType = ExPath;
		path = Suff_GetPath(&line[5]);
		if (path == NULL) {
		    Parse_Error(PARSE_FATAL,
				 "Suffix '%s' not defined (yet)",
				 &line[5]);
		    goto out;
		} else {
		    if (paths == NULL) {
			paths = Lst_Init(FALSE);
		    }
		    (void)Lst_AtEnd(paths, path);
		}
	    }
	}

	/*
	 * Have word in line. Get or create its node and stick it at
	 * the end of the targets list
	 */
	if ((specType == Not) && (*line != '\0')) {
	    if (Dir_HasWildcards(line)) {
		/*
		 * Targets are to be sought only in the current directory,
		 * so create an empty path for the thing. Note we need to
		 * use Dir_Destroy in the destruction of the path as the
		 * Dir module could have added a directory to the path...
		 */
		Lst	    emptyPath = Lst_Init(FALSE);

		Dir_Expand(line, emptyPath, curTargs);

		Lst_Destroy(emptyPath, Dir_Destroy);
	    } else {
		/*
		 * No wildcards, but we want to avoid code duplication,
		 * so create a list with the word on it.
		 */
		(void)Lst_AtEnd(curTargs, line);
	    }

	    /* Apply the targets. */

	    while(!Lst_IsEmpty(curTargs)) {
		char	*targName = (char *)Lst_DeQueue(curTargs);

		if (!Suff_IsTransform (targName)) {
		    gn = Targ_FindNode(targName, TARG_CREATE);
		} else {
		    gn = Suff_AddTransform(targName);
		}
		if (doing_depend)
		    ParseMark(gn);

		(void)Lst_AtEnd(targets, gn);
	    }
	} else if (specType == ExPath && *line != '.' && *line != '\0') {
	    Parse_Error(PARSE_WARNING, "Extra target (%s) ignored", line);
	}

	/* Don't need the inserted null terminator any more. */
	*cp = savec;

	/*
	 * If it is a special type and not .PATH, it's the only target we
	 * allow on this line...
	 */
	if (specType != Not && specType != ExPath) {
	    Boolean warning = FALSE;

	    while (*cp && (ParseIsEscaped(lstart, cp) ||
		((*cp != '!') && (*cp != ':')))) {
		if (ParseIsEscaped(lstart, cp) ||
		    (*cp != ' ' && *cp != '\t')) {
		    warning = TRUE;
		}
		cp++;
	    }
	    if (warning) {
		Parse_Error(PARSE_WARNING, "Extra target ignored");
	    }
	} else {
	    while (*cp && isspace ((unsigned char)*cp)) {
		cp++;
	    }
	}
	line = cp;
    } while (*line && (ParseIsEscaped(lstart, line) ||
	((*line != '!') && (*line != ':'))));

    /*
     * Don't need the list of target names anymore...
     */
    Lst_Destroy(curTargs, NULL);
    curTargs = NULL;

    if (!Lst_IsEmpty(targets)) {
	switch(specType) {
	    default:
		Parse_Error(PARSE_WARNING, "Special and mundane targets don't mix. Mundane ones ignored");
		break;
	    case Default:
	    case Stale:
	    case Begin:
	    case End:
	    case dotError:
	    case Interrupt:
		/*
		 * These four create nodes on which to hang commands, so
		 * targets shouldn't be empty...
		 */
	    case Not:
		/*
		 * Nothing special here -- targets can be empty if it wants.
		 */
		break;
	}
    }

    /*
     * Have now parsed all the target names. Must parse the operator next. The
     * result is left in  op .
     */
    if (*cp == '!') {
	op = OP_FORCE;
    } else if (*cp == ':') {
	if (cp[1] == ':') {
	    op = OP_DOUBLEDEP;
	    cp++;
	} else {
	    op = OP_DEPENDS;
	}
    } else {
	Parse_Error(PARSE_FATAL, lstart[0] == '.' ? "Unknown directive"
		    : "Missing dependency operator");
	goto out;
    }

    /* Advance beyond the operator */
    cp++;

    /*
     * Apply the operator to the target. This is how we remember which
     * operator a target was defined with. It fails if the operator
     * used isn't consistent across all references.
     */
    Lst_ForEach(targets, ParseDoOp, &op);

    /*
     * Onward to the sources.
     *
     * LINE will now point to the first source word, if any, or the
     * end of the string if not.
     */
    while (*cp && isspace ((unsigned char)*cp)) {
	cp++;
    }
    line = cp;

    /*
     * Several special targets take different actions if present with no
     * sources:
     *	a .SUFFIXES line with no sources clears out all old suffixes
     *	a .PRECIOUS line makes all targets precious
     *	a .IGNORE line ignores errors for all targets
     *	a .SILENT line creates silence when making all targets
     *	a .PATH removes all directories from the search path(s).
     */
    if (!*line) {
	switch (specType) {
	    case Suffixes:
		Suff_ClearSuffixes();
		break;
	    case Precious:
		allPrecious = TRUE;
		break;
	    case Ignore:
		ignoreErrors = TRUE;
		break;
	    case Silent:
		beSilent = TRUE;
		break;
	    case ExPath:
		Lst_ForEach(paths, ParseClearPath, NULL);
		Dir_SetPATH();
		break;
#ifdef POSIX
            case Posix:
                Var_Set("%POSIX", "1003.2", VAR_GLOBAL, 0);
                break;
#endif
	    default:
		break;
	}
    } else if (specType == MFlags) {
	/*
	 * Call on functions in main.c to deal with these arguments and
	 * set the initial character to a null-character so the loop to
	 * get sources won't get anything
	 */
	Main_ParseArgLine(line);
	*line = '\0';
    } else if (specType == ExShell) {
	if (Job_ParseShell(line) != SUCCESS) {
	    Parse_Error(PARSE_FATAL, "improper shell specification");
	    goto out;
	}
	*line = '\0';
    } else if ((specType == NotParallel) || (specType == SingleShell) ||
	    (specType == DeleteOnError)) {
	*line = '\0';
    }

    /*
     * NOW GO FOR THE SOURCES
     */
    if ((specType == Suffixes) || (specType == ExPath) ||
	(specType == Includes) || (specType == Libs) ||
	(specType == Null) || (specType == ExObjdir))
    {
	while (*line) {
	    /*
	     * If the target was one that doesn't take files as its sources
	     * but takes something like suffixes, we take each
	     * space-separated word on the line as a something and deal
	     * with it accordingly.
	     *
	     * If the target was .SUFFIXES, we take each source as a
	     * suffix and add it to the list of suffixes maintained by the
	     * Suff module.
	     *
	     * If the target was a .PATH, we add the source as a directory
	     * to search on the search path.
	     *
	     * If it was .INCLUDES, the source is taken to be the suffix of
	     * files which will be #included and whose search path should
	     * be present in the .INCLUDES variable.
	     *
	     * If it was .LIBS, the source is taken to be the suffix of
	     * files which are considered libraries and whose search path
	     * should be present in the .LIBS variable.
	     *
	     * If it was .NULL, the source is the suffix to use when a file
	     * has no valid suffix.
	     *
	     * If it was .OBJDIR, the source is a new definition for .OBJDIR,
	     * and will cause make to do a new chdir to that path.
	     */
	    while (*cp && !isspace ((unsigned char)*cp)) {
		cp++;
	    }
	    savec = *cp;
	    *cp = '\0';
	    switch (specType) {
		case Suffixes:
		    Suff_AddSuffix(line, &mainNode);
		    break;
		case ExPath:
		    Lst_ForEach(paths, ParseAddDir, line);
		    break;
		case Includes:
		    Suff_AddInclude(line);
		    break;
		case Libs:
		    Suff_AddLib(line);
		    break;
		case Null:
		    Suff_SetNull(line);
		    break;
		case ExObjdir:
		    Main_SetObjdir("%s", line);
		    break;
		default:
		    break;
	    }
	    *cp = savec;
	    if (savec != '\0') {
		cp++;
	    }
	    while (*cp && isspace ((unsigned char)*cp)) {
		cp++;
	    }
	    line = cp;
	}
	if (paths) {
	    Lst_Destroy(paths, NULL);
	    paths = NULL;
	}
	if (specType == ExPath)
	    Dir_SetPATH();
    } else {
	assert(paths == NULL);
	while (*line) {
	    /*
	     * The targets take real sources, so we must beware of archive
	     * specifications (i.e. things with left parentheses in them)
	     * and handle them accordingly.
	     */
	    for (; *cp && !isspace ((unsigned char)*cp); cp++) {
		if ((*cp == LPAREN) && (cp > line) && (cp[-1] != '$')) {
		    /*
		     * Only stop for a left parenthesis if it isn't at the
		     * start of a word (that'll be for variable changes
		     * later) and isn't preceded by a dollar sign (a dynamic
		     * source).
		     */
		    break;
		}
	    }

	    if (*cp == LPAREN) {
		sources = Lst_Init(FALSE);
		if (Arch_ParseArchive(&line, sources, VAR_CMD) != SUCCESS) {
		    Parse_Error(PARSE_FATAL,
				 "Error in source archive spec \"%s\"", line);
		    goto out;
		}

		while (!Lst_IsEmpty (sources)) {
		    gn = (GNode *)Lst_DeQueue(sources);
		    ParseDoSrc(tOp, gn->name);
		}
		Lst_Destroy(sources, NULL);
		cp = line;
	    } else {
		if (*cp) {
		    *cp = '\0';
		    cp += 1;
		}

		ParseDoSrc(tOp, line);
	    }
	    while (*cp && isspace ((unsigned char)*cp)) {
		cp++;
	    }
	    line = cp;
	}
    }

    if (mainNode == NULL) {
	/*
	 * If we have yet to decide on a main target to make, in the
	 * absence of any user input, we want the first target on
	 * the first dependency line that is actually a real target
	 * (i.e. isn't a .USE or .EXEC rule) to be made.
	 */
	Lst_ForEach(targets, ParseFindMain, NULL);
    }

out:
    assert(paths == NULL);
    if (curTargs)
	    Lst_Destroy(curTargs, NULL);
}

/*-
 *---------------------------------------------------------------------
 * Parse_IsVar  --
 *	Return TRUE if the passed line is a variable assignment. A variable
 *	assignment consists of a single word followed by optional whitespace
 *	followed by either a += or an = operator.
 *	This function is used both by the Parse_File function and main when
 *	parsing the command-line arguments.
 *
 * Input:
 *	line		the line to check
 *
 * Results:
 *	TRUE if it is. FALSE if it ain't
 *
 * Side Effects:
 *	none
 *---------------------------------------------------------------------
 */
Boolean
Parse_IsVar(char *line)
{
    Boolean wasSpace = FALSE;	/* set TRUE if found a space */
    char ch;
    int level = 0;
#define ISEQOPERATOR(c) \
	(((c) == '+') || ((c) == ':') || ((c) == '?') || ((c) == '!'))

    /*
     * Skip to variable name
     */
    for (;(*line == ' ') || (*line == '\t'); line++)
	continue;

    /* Scan for one of the assignment operators outside a variable expansion */
    while ((ch = *line++) != 0) {
	if (ch == '(' || ch == '{') {
	    level++;
	    continue;
	}
	if (ch == ')' || ch == '}') {
	    level--;
	    continue;
	}
	if (level != 0)
	    continue;
	while (ch == ' ' || ch == '\t') {
	    ch = *line++;
	    wasSpace = TRUE;
	}
#ifdef SUNSHCMD
	if (ch == ':' && strncmp(line, "sh", 2) == 0) {
	    line += 2;
	    continue;
	}
#endif
	if (ch == '=')
	    return TRUE;
	if (*line == '=' && ISEQOPERATOR(ch))
	    return TRUE;
	if (wasSpace)
	    return FALSE;
    }

    return FALSE;
}

/*-
 *---------------------------------------------------------------------
 * Parse_DoVar  --
 *	Take the variable assignment in the passed line and do it in the
 *	global context.
 *
 *	Note: There is a lexical ambiguity with assignment modifier characters
 *	in variable names. This routine interprets the character before the =
 *	as a modifier. Therefore, an assignment like
 *	    C++=/usr/bin/CC
 *	is interpreted as "C+ +=" instead of "C++ =".
 *
 * Input:
 *	line		a line guaranteed to be a variable assignment.
 *			This reduces error checks
 *	ctxt		Context in which to do the assignment
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	the variable structure of the given variable name is altered in the
 *	global context.
 *---------------------------------------------------------------------
 */
void
Parse_DoVar(char *line, GNode *ctxt)
{
    char	   *cp;	/* pointer into line */
    enum {
	VAR_SUBST, VAR_APPEND, VAR_SHELL, VAR_NORMAL
    }	    	    type;   	/* Type of assignment */
    char            *opc;	/* ptr to operator character to
				 * null-terminate the variable name */
    Boolean	   freeCp = FALSE; /* TRUE if cp needs to be freed,
				    * i.e. if any variable expansion was
				    * performed */
    int depth;

    /*
     * Skip to variable name
     */
    while ((*line == ' ') || (*line == '\t')) {
	line++;
    }

    /*
     * Skip to operator character, nulling out whitespace as we go
     * XXX Rather than counting () and {} we should look for $ and
     * then expand the variable.
     */
    for (depth = 0, cp = line + 1; depth > 0 || *cp != '='; cp++) {
	if (*cp == '(' || *cp == '{') {
	    depth++;
	    continue;
	}
	if (*cp == ')' || *cp == '}') {
	    depth--;
	    continue;
	}
	if (depth == 0 && isspace ((unsigned char)*cp)) {
	    *cp = '\0';
	}
    }
    opc = cp-1;		/* operator is the previous character */
    *cp++ = '\0';	/* nuke the = */

    /*
     * Check operator type
     */
    switch (*opc) {
	case '+':
	    type = VAR_APPEND;
	    *opc = '\0';
	    break;

	case '?':
	    /*
	     * If the variable already has a value, we don't do anything.
	     */
	    *opc = '\0';
	    if (Var_Exists(line, ctxt)) {
		return;
	    } else {
		type = VAR_NORMAL;
	    }
	    break;

	case ':':
	    type = VAR_SUBST;
	    *opc = '\0';
	    break;

	case '!':
	    type = VAR_SHELL;
	    *opc = '\0';
	    break;

	default:
#ifdef SUNSHCMD
	    while (opc > line && *opc != ':')
		opc--;

	    if (strncmp(opc, ":sh", 3) == 0) {
		type = VAR_SHELL;
		*opc = '\0';
		break;
	    }
#endif
	    type = VAR_NORMAL;
	    break;
    }

    while (isspace ((unsigned char)*cp)) {
	cp++;
    }

    if (type == VAR_APPEND) {
	Var_Append(line, cp, ctxt);
    } else if (type == VAR_SUBST) {
	/*
	 * Allow variables in the old value to be undefined, but leave their
	 * invocation alone -- this is done by forcing oldVars to be false.
	 * XXX: This can cause recursive variables, but that's not hard to do,
	 * and this allows someone to do something like
	 *
	 *  CFLAGS = $(.INCLUDES)
	 *  CFLAGS := -I.. $(CFLAGS)
	 *
	 * And not get an error.
	 */
	Boolean	  oldOldVars = oldVars;

	oldVars = FALSE;

	/*
	 * make sure that we set the variable the first time to nothing
	 * so that it gets substituted!
	 */
	if (!Var_Exists(line, ctxt))
	    Var_Set(line, "", ctxt, 0);

	cp = Var_Subst(NULL, cp, ctxt, VARF_WANTRES|VARF_ASSIGN);
	oldVars = oldOldVars;
	freeCp = TRUE;

	Var_Set(line, cp, ctxt, 0);
    } else if (type == VAR_SHELL) {
	char *res;
	const char *error;

	if (strchr(cp, '$') != NULL) {
	    /*
	     * There's a dollar sign in the command, so perform variable
	     * expansion on the whole thing. The resulting string will need
	     * freeing when we're done, so set freeCmd to TRUE.
	     */
	    cp = Var_Subst(NULL, cp, VAR_CMD, VARF_UNDEFERR|VARF_WANTRES);
	    freeCp = TRUE;
	}

	res = Cmd_Exec(cp, &error);
	Var_Set(line, res, ctxt, 0);
	free(res);

	if (error)
	    Parse_Error(PARSE_WARNING, error, cp);
    } else {
	/*
	 * Normal assignment -- just do it.
	 */
	Var_Set(line, cp, ctxt, 0);
    }
    if (strcmp(line, MAKEOVERRIDES) == 0)
	Main_ExportMAKEFLAGS(FALSE);	/* re-export MAKEFLAGS */
    else if (strcmp(line, ".CURDIR") == 0) {
	/*
	 * Somone is being (too?) clever...
	 * Let's pretend they know what they are doing and
	 * re-initialize the 'cur' Path.
	 */
	Dir_InitCur(cp);
	Dir_SetPATH();
    } else if (strcmp(line, MAKE_JOB_PREFIX) == 0) {
	Job_SetPrefix();
    } else if (strcmp(line, MAKE_EXPORTED) == 0) {
	Var_Export(cp, 0);
    }
    if (freeCp)
	free(cp);
}


/*
 * ParseMaybeSubMake --
 * 	Scan the command string to see if it a possible submake node
 * Input:
 *	cmd		the command to scan
 * Results:
 *	TRUE if the command is possibly a submake, FALSE if not.
 */
static Boolean
ParseMaybeSubMake(const char *cmd)
{
    size_t i;
    static struct {
	const char *name;
	size_t len;
    } vals[] = {
#define MKV(A)	{	A, sizeof(A) - 1	}
	MKV("${MAKE}"),
	MKV("${.MAKE}"),
	MKV("$(MAKE)"),
	MKV("$(.MAKE)"),
	MKV("make"),
    };
    for (i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
	char *ptr;
	if ((ptr = strstr(cmd, vals[i].name)) == NULL)
	    continue;
	if ((ptr == cmd || !isalnum((unsigned char)ptr[-1]))
	    && !isalnum((unsigned char)ptr[vals[i].len]))
	    return TRUE;
    }
    return FALSE;
}

/*-
 * ParseAddCmd  --
 *	Lst_ForEach function to add a command line to all targets
 *
 * Input:
 *	gnp		the node to which the command is to be added
 *	cmd		the command to add
 *
 * Results:
 *	Always 0
 *
 * Side Effects:
 *	A new element is added to the commands list of the node,
 *	and the node can be marked as a submake node if the command is
 *	determined to be that.
 */
static int
ParseAddCmd(void *gnp, void *cmd)
{
    GNode *gn = (GNode *)gnp;

    /* Add to last (ie current) cohort for :: targets */
    if ((gn->type & OP_DOUBLEDEP) && !Lst_IsEmpty (gn->cohorts))
	gn = (GNode *)Lst_Datum(Lst_Last(gn->cohorts));

    /* if target already supplied, ignore commands */
    if (!(gn->type & OP_HAS_COMMANDS)) {
	(void)Lst_AtEnd(gn->commands, cmd);
	if (ParseMaybeSubMake(cmd))
	    gn->type |= OP_SUBMAKE;
	ParseMark(gn);
    } else {
#ifdef notyet
	/* XXX: We cannot do this until we fix the tree */
	(void)Lst_AtEnd(gn->commands, cmd);
	Parse_Error(PARSE_WARNING,
		     "overriding commands for target \"%s\"; "
		     "previous commands defined at %s: %d ignored",
		     gn->name, gn->fname, gn->lineno);
#else
	Parse_Error(PARSE_WARNING,
		     "duplicate script for target \"%s\" ignored",
		     gn->name);
	ParseErrorInternal(gn->fname, gn->lineno, PARSE_WARNING,
			    "using previous script for \"%s\" defined here",
			    gn->name);
#endif
    }
    return(0);
}

/*-
 *-----------------------------------------------------------------------
 * ParseHasCommands --
 *	Callback procedure for Parse_File when destroying the list of
 *	targets on the last dependency line. Marks a target as already
 *	having commands if it does, to keep from having shell commands
 *	on multiple dependency lines.
 *
 * Input:
 *	gnp		Node to examine
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	OP_HAS_COMMANDS may be set for the target.
 *
 *-----------------------------------------------------------------------
 */
static void
ParseHasCommands(void *gnp)
{
    GNode *gn = (GNode *)gnp;
    if (!Lst_IsEmpty(gn->commands)) {
	gn->type |= OP_HAS_COMMANDS;
    }
}

/*-
 *-----------------------------------------------------------------------
 * Parse_AddIncludeDir --
 *	Add a directory to the path searched for included makefiles
 *	bracketed by double-quotes. Used by functions in main.c
 *
 * Input:
 *	dir		The name of the directory to add
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The directory is appended to the list.
 *
 *-----------------------------------------------------------------------
 */
void
Parse_AddIncludeDir(char *dir)
{
    (void)Dir_AddDir(parseIncPath, dir);
}

/*-
 *---------------------------------------------------------------------
 * ParseDoInclude  --
 *	Push to another file.
 *
 *	The input is the line minus the `.'. A file spec is a string
 *	enclosed in <> or "". The former is looked for only in sysIncPath.
 *	The latter in . and the directories specified by -I command line
 *	options
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	A structure is added to the includes Lst and readProc, lineno,
 *	fname and curFILE are altered for the new file
 *---------------------------------------------------------------------
 */

static void
Parse_include_file(char *file, Boolean isSystem, Boolean depinc, int silent)
{
    struct loadedfile *lf;
    char          *fullname;	/* full pathname of file */
    char          *newName;
    char          *prefEnd, *incdir;
    int           fd;
    int           i;

    /*
     * Now we know the file's name and its search path, we attempt to
     * find the durn thing. A return of NULL indicates the file don't
     * exist.
     */
    fullname = file[0] == '/' ? bmake_strdup(file) : NULL;

    if (fullname == NULL && !isSystem) {
	/*
	 * Include files contained in double-quotes are first searched for
	 * relative to the including file's location. We don't want to
	 * cd there, of course, so we just tack on the old file's
	 * leading path components and call Dir_FindFile to see if
	 * we can locate the beast.
	 */

	incdir = bmake_strdup(curFile->fname);
	prefEnd = strrchr(incdir, '/');
	if (prefEnd != NULL) {
	    *prefEnd = '\0';
	    /* Now do lexical processing of leading "../" on the filename */
	    for (i = 0; strncmp(file + i, "../", 3) == 0; i += 3) {
		prefEnd = strrchr(incdir + 1, '/');
		if (prefEnd == NULL || strcmp(prefEnd, "/..") == 0)
		    break;
		*prefEnd = '\0';
	    }
	    newName = str_concat(incdir, file + i, STR_ADDSLASH);
	    fullname = Dir_FindFile(newName, parseIncPath);
	    if (fullname == NULL)
		fullname = Dir_FindFile(newName, dirSearchPath);
	    free(newName);
	}
	free(incdir);

	if (fullname == NULL) {
	    /*
    	     * Makefile wasn't found in same directory as included makefile.
	     * Search for it first on the -I search path,
	     * then on the .PATH search path, if not found in a -I directory.
	     * If we have a suffix specific path we should use that.
	     */
	    char *suff;
	    Lst	suffPath = NULL;

	    if ((suff = strrchr(file, '.'))) {
		suffPath = Suff_GetPath(suff);
		if (suffPath != NULL) {
		    fullname = Dir_FindFile(file, suffPath);
		}
	    }
	    if (fullname == NULL) {
		fullname = Dir_FindFile(file, parseIncPath);
		if (fullname == NULL) {
		    fullname = Dir_FindFile(file, dirSearchPath);
		}
	    }
	}
    }

    /* Looking for a system file or file still not found */
    if (fullname == NULL) {
	/*
	 * Look for it on the system path
	 */
	fullname = Dir_FindFile(file,
		    Lst_IsEmpty(sysIncPath) ? defIncPath : sysIncPath);
    }

    if (fullname == NULL) {
	if (!silent)
	    Parse_Error(PARSE_FATAL, "Could not find %s", file);
	return;
    }

    /* Actually open the file... */
    fd = open(fullname, O_RDONLY);
    if (fd == -1) {
	if (!silent)
	    Parse_Error(PARSE_FATAL, "Cannot open %s", fullname);
	free(fullname);
	return;
    }

    /* load it */
    lf = loadfile(fullname, fd);

    ParseSetIncludedFile();
    /* Start reading from this file next */
    Parse_SetInput(fullname, 0, -1, loadedfile_nextbuf, lf);
    curFile->lf = lf;
    if (depinc)
	doing_depend = depinc;		/* only turn it on */
}

static void
ParseDoInclude(char *line)
{
    char          endc;	    	/* the character which ends the file spec */
    char          *cp;		/* current position in file spec */
    int		  silent = (*line != 'i') ? 1 : 0;
    char	  *file = &line[7 + silent];

    /* Skip to delimiter character so we know where to look */
    while (*file == ' ' || *file == '\t')
	file++;

    if (*file != '"' && *file != '<') {
	Parse_Error(PARSE_FATAL,
	    ".include filename must be delimited by '\"' or '<'");
	return;
    }

    /*
     * Set the search path on which to find the include file based on the
     * characters which bracket its name. Angle-brackets imply it's
     * a system Makefile while double-quotes imply it's a user makefile
     */
    if (*file == '<') {
	endc = '>';
    } else {
	endc = '"';
    }

    /* Skip to matching delimiter */
    for (cp = ++file; *cp && *cp != endc; cp++)
	continue;

    if (*cp != endc) {
	Parse_Error(PARSE_FATAL,
		     "Unclosed %cinclude filename. '%c' expected",
		     '.', endc);
	return;
    }
    *cp = '\0';

    /*
     * Substitute for any variables in the file name before trying to
     * find the thing.
     */
    file = Var_Subst(NULL, file, VAR_CMD, VARF_WANTRES);

    Parse_include_file(file, endc == '>', (*line == 'd'), silent);
    free(file);
}


/*-
 *---------------------------------------------------------------------
 * ParseSetIncludedFile  --
 *	Set the .INCLUDEDFROMFILE variable to the contents of .PARSEFILE
 *	and the .INCLUDEDFROMDIR variable to the contents of .PARSEDIR
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The .INCLUDEDFROMFILE variable is overwritten by the contents
 *	of .PARSEFILE and the .INCLUDEDFROMDIR variable is overwriten
 *	by the contents of .PARSEDIR
 *---------------------------------------------------------------------
 */
static void
ParseSetIncludedFile(void)
{
    char *pf, *fp = NULL;
    char *pd, *dp = NULL;

    pf = Var_Value(".PARSEFILE", VAR_GLOBAL, &fp);
    Var_Set(".INCLUDEDFROMFILE", pf, VAR_GLOBAL, 0);
    pd = Var_Value(".PARSEDIR", VAR_GLOBAL, &dp);
    Var_Set(".INCLUDEDFROMDIR", pd, VAR_GLOBAL, 0);

    if (DEBUG(PARSE))
	fprintf(debug_file, "%s: ${.INCLUDEDFROMDIR} = `%s' "
	    "${.INCLUDEDFROMFILE} = `%s'\n", __func__, pd, pf);

    free(fp);
    free(dp);
}
/*-
 *---------------------------------------------------------------------
 * ParseSetParseFile  --
 *	Set the .PARSEDIR and .PARSEFILE variables to the dirname and
 *	basename of the given filename
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The .PARSEDIR and .PARSEFILE variables are overwritten by the
 *	dirname and basename of the given filename.
 *---------------------------------------------------------------------
 */
static void
ParseSetParseFile(const char *filename)
{
    char *slash, *dirname;
    const char *pd, *pf;
    int len;

    slash = strrchr(filename, '/');
    if (slash == NULL) {
	Var_Set(".PARSEDIR", pd = curdir, VAR_GLOBAL, 0);
	Var_Set(".PARSEFILE", pf = filename, VAR_GLOBAL, 0);
	dirname= NULL;
    } else {
	len = slash - filename;
	dirname = bmake_malloc(len + 1);
	memcpy(dirname, filename, len);
	dirname[len] = '\0';
	Var_Set(".PARSEDIR", pd = dirname, VAR_GLOBAL, 0);
	Var_Set(".PARSEFILE", pf = slash + 1, VAR_GLOBAL, 0);
    }
    if (DEBUG(PARSE))
	fprintf(debug_file, "%s: ${.PARSEDIR} = `%s' ${.PARSEFILE} = `%s'\n",
	    __func__, pd, pf);
    free(dirname);
}

/*
 * Track the makefiles we read - so makefiles can
 * set dependencies on them.
 * Avoid adding anything more than once.
 */

static void
ParseTrackInput(const char *name)
{
    char *old;
    char *ep;
    char *fp = NULL;
    size_t name_len = strlen(name);
    
    old = Var_Value(MAKE_MAKEFILES, VAR_GLOBAL, &fp);
    if (old) {
	ep = old + strlen(old) - name_len;
	/* does it contain name? */
	for (; old != NULL; old = strchr(old, ' ')) {
	    if (*old == ' ')
		old++;
	    if (old >= ep)
		break;			/* cannot contain name */
	    if (memcmp(old, name, name_len) == 0
		    && (old[name_len] == 0 || old[name_len] == ' '))
		goto cleanup;
	}
    }
    Var_Append (MAKE_MAKEFILES, name, VAR_GLOBAL);
 cleanup:
    if (fp) {
	free(fp);
    }
}


/*-
 *---------------------------------------------------------------------
 * Parse_setInput  --
 *	Start Parsing from the given source
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	A structure is added to the includes Lst and readProc, lineno,
 *	fname and curFile are altered for the new file
 *---------------------------------------------------------------------
 */
void
Parse_SetInput(const char *name, int line, int fd,
	char *(*nextbuf)(void *, size_t *), void *arg)
{
    char *buf;
    size_t len;

    if (name == NULL)
	name = curFile->fname;
    else
	ParseTrackInput(name);

    if (DEBUG(PARSE))
	fprintf(debug_file, "%s: file %s, line %d, fd %d, nextbuf %p, arg %p\n",
	    __func__, name, line, fd, nextbuf, arg);

    if (fd == -1 && nextbuf == NULL)
	/* sanity */
	return;

    if (curFile != NULL)
	/* Save exiting file info */
	Lst_AtFront(includes, curFile);

    /* Allocate and fill in new structure */
    curFile = bmake_malloc(sizeof *curFile);

    /*
     * Once the previous state has been saved, we can get down to reading
     * the new file. We set up the name of the file to be the absolute
     * name of the include file so error messages refer to the right
     * place.
     */
    curFile->fname = bmake_strdup(name);
    curFile->lineno = line;
    curFile->first_lineno = line;
    curFile->nextbuf = nextbuf;
    curFile->nextbuf_arg = arg;
    curFile->lf = NULL;
    curFile->depending = doing_depend;	/* restore this on EOF */

    assert(nextbuf != NULL);

    /* Get first block of input data */
    buf = curFile->nextbuf(curFile->nextbuf_arg, &len);
    if (buf == NULL) {
        /* Was all a waste of time ... */
	if (curFile->fname)
	    free(curFile->fname);
	free(curFile);
	return;
    }
    curFile->P_str = buf;
    curFile->P_ptr = buf;
    curFile->P_end = buf+len;

    curFile->cond_depth = Cond_save_depth();
    ParseSetParseFile(name);
}

/*-
 *-----------------------------------------------------------------------
 * IsInclude --
 *	Check if the line is an include directive
 *
 * Results:
 *	TRUE if it is.
 *
 * Side Effects:
 *	None
 *
 *-----------------------------------------------------------------------
 */
static Boolean
IsInclude(const char *line, Boolean sysv)
{
	static const char inc[] = "include";
	static const size_t inclen = sizeof(inc) - 1;

	// 'd' is not valid for sysv
	int o = strchr(&("ds-"[sysv]), *line) != NULL;

	if (strncmp(line + o, inc, inclen) != 0)
		return FALSE;

	// Space is not mandatory for BSD .include
	return !sysv || isspace((unsigned char)line[inclen + o]);
}


#ifdef SYSVINCLUDE
/*-
 *-----------------------------------------------------------------------
 * IsSysVInclude --
 *	Check if the line is a SYSV include directive
 *
 * Results:
 *	TRUE if it is.
 *
 * Side Effects:
 *	None
 *
 *-----------------------------------------------------------------------
 */
static Boolean
IsSysVInclude(const char *line)
{
	const char *p;

	if (!IsInclude(line, TRUE))
		return FALSE;

	/* Avoid interpeting a dependency line as an include */
	for (p = line; (p = strchr(p, ':')) != NULL;) {
		if (*++p == '\0') {
			/* end of line -> dependency */
			return FALSE;
		}
		if (*p == ':' || isspace((unsigned char)*p)) {
			/* :: operator or ': ' -> dependency */
			return FALSE;
		}
	}
	return TRUE;
}

/*-
 *---------------------------------------------------------------------
 * ParseTraditionalInclude  --
 *	Push to another file.
 *
 *	The input is the current line. The file name(s) are
 *	following the "include".
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	A structure is added to the includes Lst and readProc, lineno,
 *	fname and curFILE are altered for the new file
 *---------------------------------------------------------------------
 */
static void
ParseTraditionalInclude(char *line)
{
    char          *cp;		/* current position in file spec */
    int		   done = 0;
    int		   silent = (line[0] != 'i') ? 1 : 0;
    char	  *file = &line[silent + 7];
    char	  *all_files;

    if (DEBUG(PARSE)) {
	    fprintf(debug_file, "%s: %s\n", __func__, file);
    }

    /*
     * Skip over whitespace
     */
    while (isspace((unsigned char)*file))
	file++;

    /*
     * Substitute for any variables in the file name before trying to
     * find the thing.
     */
    all_files = Var_Subst(NULL, file, VAR_CMD, VARF_WANTRES);

    if (*file == '\0') {
	Parse_Error(PARSE_FATAL,
		     "Filename missing from \"include\"");
	goto out;
    }

    for (file = all_files; !done; file = cp + 1) {
	/* Skip to end of line or next whitespace */
	for (cp = file; *cp && !isspace((unsigned char) *cp); cp++)
	    continue;

	if (*cp)
	    *cp = '\0';
	else
	    done = 1;

	Parse_include_file(file, FALSE, FALSE, silent);
    }
out:
    free(all_files);
}
#endif

#ifdef GMAKEEXPORT
/*-
 *---------------------------------------------------------------------
 * ParseGmakeExport  --
 *	Parse export <variable>=<value>
 *
 *	And set the environment with it.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	None
 *---------------------------------------------------------------------
 */
static void
ParseGmakeExport(char *line)
{
    char	  *variable = &line[6];
    char	  *value;

    if (DEBUG(PARSE)) {
	    fprintf(debug_file, "%s: %s\n", __func__, variable);
    }

    /*
     * Skip over whitespace
     */
    while (isspace((unsigned char)*variable))
	variable++;

    for (value = variable; *value && *value != '='; value++)
	continue;

    if (*value != '=') {
	Parse_Error(PARSE_FATAL,
		     "Variable/Value missing from \"export\"");
	return;
    }
    *value++ = '\0';			/* terminate variable */

    /*
     * Expand the value before putting it in the environment.
     */
    value = Var_Subst(NULL, value, VAR_CMD, VARF_WANTRES);
    setenv(variable, value, 1);
    free(value);
}
#endif

/*-
 *---------------------------------------------------------------------
 * ParseEOF  --
 *	Called when EOF is reached in the current file. If we were reading
 *	an include file, the includes stack is popped and things set up
 *	to go back to reading the previous file at the previous location.
 *
 * Results:
 *	CONTINUE if there's more to do. DONE if not.
 *
 * Side Effects:
 *	The old curFILE, is closed. The includes list is shortened.
 *	lineno, curFILE, and fname are changed if CONTINUE is returned.
 *---------------------------------------------------------------------
 */
static int
ParseEOF(void)
{
    char *ptr;
    size_t len;

    assert(curFile->nextbuf != NULL);

    doing_depend = curFile->depending;	/* restore this */
    /* get next input buffer, if any */
    ptr = curFile->nextbuf(curFile->nextbuf_arg, &len);
    curFile->P_ptr = ptr;
    curFile->P_str = ptr;
    curFile->P_end = ptr + len;
    curFile->lineno = curFile->first_lineno;
    if (ptr != NULL) {
	/* Iterate again */
	return CONTINUE;
    }

    /* Ensure the makefile (or loop) didn't have mismatched conditionals */
    Cond_restore_depth(curFile->cond_depth);

    if (curFile->lf != NULL) {
	    loadedfile_destroy(curFile->lf);
	    curFile->lf = NULL;
    }

    /* Dispose of curFile info */
    /* Leak curFile->fname because all the gnodes have pointers to it */
    free(curFile->P_str);
    free(curFile);

    curFile = Lst_DeQueue(includes);

    if (curFile == NULL) {
	/* We've run out of input */
	Var_Delete(".PARSEDIR", VAR_GLOBAL);
	Var_Delete(".PARSEFILE", VAR_GLOBAL);
	Var_Delete(".INCLUDEDFROMDIR", VAR_GLOBAL);
	Var_Delete(".INCLUDEDFROMFILE", VAR_GLOBAL);
	return DONE;
    }

    if (DEBUG(PARSE))
	fprintf(debug_file, "ParseEOF: returning to file %s, line %d\n",
	    curFile->fname, curFile->lineno);

    /* Restore the PARSEDIR/PARSEFILE variables */
    ParseSetParseFile(curFile->fname);
    return (CONTINUE);
}

#define PARSE_RAW 1
#define PARSE_SKIP 2

static char *
ParseGetLine(int flags, int *length)
{
    IFile *cf = curFile;
    char *ptr;
    char ch;
    char *line;
    char *line_end;
    char *escaped;
    char *comment;
    char *tp;

    /* Loop through blank lines and comment lines */
    for (;;) {
	cf->lineno++;
	line = cf->P_ptr;
	ptr = line;
	line_end = line;
	escaped = NULL;
	comment = NULL;
	for (;;) {
	    if (cf->P_end != NULL && ptr == cf->P_end) {
		/* end of buffer */
		ch = 0;
		break;
	    }
	    ch = *ptr;
	    if (ch == 0 || (ch == '\\' && ptr[1] == 0)) {
		if (cf->P_end == NULL)
		    /* End of string (aka for loop) data */
		    break;
		/* see if there is more we can parse */
		while (ptr++ < cf->P_end) {
		    if ((ch = *ptr) == '\n') {
			if (ptr > line && ptr[-1] == '\\')
			    continue;
			Parse_Error(PARSE_WARNING,
			    "Zero byte read from file, skipping rest of line.");
			break;
		    }
		}
		if (cf->nextbuf != NULL) {
		    /*
		     * End of this buffer; return EOF and outer logic
		     * will get the next one. (eww)
		     */
		    break;
		}
		Parse_Error(PARSE_FATAL, "Zero byte read from file");
		return NULL;
	    }

	    if (ch == '\\') {
		/* Don't treat next character as special, remember first one */
		if (escaped == NULL)
		    escaped = ptr;
		if (ptr[1] == '\n')
		    cf->lineno++;
		ptr += 2;
		line_end = ptr;
		continue;
	    }
	    if (ch == '#' && comment == NULL) {
		/* Remember first '#' for comment stripping */
		/* Unless previous char was '[', as in modifier :[#] */
		if (!(ptr > line && ptr[-1] == '['))
		    comment = line_end;
	    }
	    ptr++;
	    if (ch == '\n')
		break;
	    if (!isspace((unsigned char)ch))
		/* We are not interested in trailing whitespace */
		line_end = ptr;
	}

	/* Save next 'to be processed' location */
	cf->P_ptr = ptr;

	/* Check we have a non-comment, non-blank line */
	if (line_end == line || comment == line) {
	    if (ch == 0)
		/* At end of file */
		return NULL;
	    /* Parse another line */
	    continue;
	}

	/* We now have a line of data */
	*line_end = 0;

	if (flags & PARSE_RAW) {
	    /* Leave '\' (etc) in line buffer (eg 'for' lines) */
	    *length = line_end - line;
	    return line;
	}

	if (flags & PARSE_SKIP) {
	    /* Completely ignore non-directives */
	    if (line[0] != '.')
		continue;
	    /* We could do more of the .else/.elif/.endif checks here */
	}
	break;
    }

    /* Brutally ignore anything after a non-escaped '#' in non-commands */
    if (comment != NULL && line[0] != '\t') {
	line_end = comment;
	*line_end = 0;
    }

    /* If we didn't see a '\\' then the in-situ data is fine */
    if (escaped == NULL) {
	*length = line_end - line;
	return line;
    }

    /* Remove escapes from '\n' and '#' */
    tp = ptr = escaped;
    escaped = line;
    for (; ; *tp++ = ch) {
	ch = *ptr++;
	if (ch != '\\') {
	    if (ch == 0)
		break;
	    continue;
	}

	ch = *ptr++;
	if (ch == 0) {
	    /* Delete '\\' at end of buffer */
	    tp--;
	    break;
	}

	if (ch == '#' && line[0] != '\t')
	    /* Delete '\\' from before '#' on non-command lines */
	    continue;

	if (ch != '\n') {
	    /* Leave '\\' in buffer for later */
	    *tp++ = '\\';
	    /* Make sure we don't delete an escaped ' ' from the line end */
	    escaped = tp + 1;
	    continue;
	}

	/* Escaped '\n' replace following whitespace with a single ' ' */
	while (ptr[0] == ' ' || ptr[0] == '\t')
	    ptr++;
	ch = ' ';
    }

    /* Delete any trailing spaces - eg from empty continuations */
    while (tp > escaped && isspace((unsigned char)tp[-1]))
	tp--;

    *tp = 0;
    *length = tp - line;
    return line;
}

/*-
 *---------------------------------------------------------------------
 * ParseReadLine --
 *	Read an entire line from the input file. Called only by Parse_File.
 *
 * Results:
 *	A line w/o its newline
 *
 * Side Effects:
 *	Only those associated with reading a character
 *---------------------------------------------------------------------
 */
static char *
ParseReadLine(void)
{
    char 	  *line;    	/* Result */
    int	    	  lineLength;	/* Length of result */
    int	    	  lineno;	/* Saved line # */
    int	    	  rval;

    for (;;) {
	line = ParseGetLine(0, &lineLength);
	if (line == NULL)
	    return NULL;

	if (line[0] != '.')
	    return line;

	/*
	 * The line might be a conditional. Ask the conditional module
	 * about it and act accordingly
	 */
	switch (Cond_Eval(line)) {
	case COND_SKIP:
	    /* Skip to next conditional that evaluates to COND_PARSE.  */
	    do {
		line = ParseGetLine(PARSE_SKIP, &lineLength);
	    } while (line && Cond_Eval(line) != COND_PARSE);
	    if (line == NULL)
		break;
	    continue;
	case COND_PARSE:
	    continue;
	case COND_INVALID:    /* Not a conditional line */
	    /* Check for .for loops */
	    rval = For_Eval(line);
	    if (rval == 0)
		/* Not a .for line */
		break;
	    if (rval < 0)
		/* Syntax error - error printed, ignore line */
		continue;
	    /* Start of a .for loop */
	    lineno = curFile->lineno;
	    /* Accumulate loop lines until matching .endfor */
	    do {
		line = ParseGetLine(PARSE_RAW, &lineLength);
		if (line == NULL) {
		    Parse_Error(PARSE_FATAL,
			     "Unexpected end of file in for loop.");
		    break;
		}
	    } while (For_Accum(line));
	    /* Stash each iteration as a new 'input file' */
	    For_Run(lineno);
	    /* Read next line from for-loop buffer */
	    continue;
	}
	return (line);
    }
}

/*-
 *-----------------------------------------------------------------------
 * ParseFinishLine --
 *	Handle the end of a dependency group.
 *
 * Results:
 *	Nothing.
 *
 * Side Effects:
 *	inLine set FALSE. 'targets' list destroyed.
 *
 *-----------------------------------------------------------------------
 */
static void
ParseFinishLine(void)
{
    if (inLine) {
	Lst_ForEach(targets, Suff_EndTransform, NULL);
	Lst_Destroy(targets, ParseHasCommands);
	targets = NULL;
	inLine = FALSE;
    }
}


/*-
 *---------------------------------------------------------------------
 * Parse_File --
 *	Parse a file into its component parts, incorporating it into the
 *	current dependency graph. This is the main function and controls
 *	almost every other function in this module
 *
 * Input:
 *	name		the name of the file being read
 *	fd		Open file to makefile to parse
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	closes fd.
 *	Loads. Nodes are added to the list of all targets, nodes and links
 *	are added to the dependency graph. etc. etc. etc.
 *---------------------------------------------------------------------
 */
void
Parse_File(const char *name, int fd)
{
    char	  *cp;		/* pointer into the line */
    char          *line;	/* the line we're working on */
    struct loadedfile *lf;

    lf = loadfile(name, fd);

    inLine = FALSE;
    fatals = 0;

    if (name == NULL) {
	    name = "(stdin)";
    }

    Parse_SetInput(name, 0, -1, loadedfile_nextbuf, lf);
    curFile->lf = lf;

    do {
	for (; (line = ParseReadLine()) != NULL; ) {
	    if (DEBUG(PARSE))
		fprintf(debug_file, "ParseReadLine (%d): '%s'\n",
			curFile->lineno, line);
	    if (*line == '.') {
		/*
		 * Lines that begin with the special character may be
		 * include or undef directives.
		 * On the other hand they can be suffix rules (.c.o: ...)
		 * or just dependencies for filenames that start '.'.
		 */
		for (cp = line + 1; isspace((unsigned char)*cp); cp++) {
		    continue;
		}
		if (IsInclude(cp, FALSE)) {
		    ParseDoInclude(cp);
		    continue;
		}
		if (strncmp(cp, "undef", 5) == 0) {
		    char *cp2;
		    for (cp += 5; isspace((unsigned char) *cp); cp++)
			continue;
		    for (cp2 = cp; !isspace((unsigned char) *cp2) &&
				   (*cp2 != '\0'); cp2++)
			continue;
		    *cp2 = '\0';
		    Var_Delete(cp, VAR_GLOBAL);
		    continue;
		} else if (strncmp(cp, "export", 6) == 0) {
		    for (cp += 6; isspace((unsigned char) *cp); cp++)
			continue;
		    Var_Export(cp, 1);
		    continue;
		} else if (strncmp(cp, "unexport", 8) == 0) {
		    Var_UnExport(cp);
		    continue;
		} else if (strncmp(cp, "info", 4) == 0 ||
			   strncmp(cp, "error", 5) == 0 ||
			   strncmp(cp, "warning", 7) == 0) {
		    if (ParseMessage(cp))
			continue;
		}		    
	    }

	    if (*line == '\t') {
		/*
		 * If a line starts with a tab, it can only hope to be
		 * a creation command.
		 */
		cp = line + 1;
	      shellCommand:
		for (; isspace ((unsigned char)*cp); cp++) {
		    continue;
		}
		if (*cp) {
		    if (!inLine)
			Parse_Error(PARSE_FATAL,
				     "Unassociated shell command \"%s\"",
				     cp);
		    /*
		     * So long as it's not a blank line and we're actually
		     * in a dependency spec, add the command to the list of
		     * commands of all targets in the dependency spec
		     */
		    if (targets) {
			cp = bmake_strdup(cp);
			Lst_ForEach(targets, ParseAddCmd, cp);
#ifdef CLEANUP
			Lst_AtEnd(targCmds, cp);
#endif
		    }
		}
		continue;
	    }

#ifdef SYSVINCLUDE
	    if (IsSysVInclude(line)) {
		/*
		 * It's an S3/S5-style "include".
		 */
		ParseTraditionalInclude(line);
		continue;
	    }
#endif
#ifdef GMAKEEXPORT
	    if (strncmp(line, "export", 6) == 0 &&
		isspace((unsigned char) line[6]) &&
		strchr(line, ':') == NULL) {
		/*
		 * It's a Gmake "export".
		 */
		ParseGmakeExport(line);
		continue;
	    }
#endif
	    if (Parse_IsVar(line)) {
		ParseFinishLine();
		Parse_DoVar(line, VAR_GLOBAL);
		continue;
	    }

#ifndef POSIX
	    /*
	     * To make life easier on novices, if the line is indented we
	     * first make sure the line has a dependency operator in it.
	     * If it doesn't have an operator and we're in a dependency
	     * line's script, we assume it's actually a shell command
	     * and add it to the current list of targets.
	     */
	    cp = line;
	    if (isspace((unsigned char) line[0])) {
		while ((*cp != '\0') && isspace((unsigned char) *cp))
		    cp++;
		while (*cp && (ParseIsEscaped(line, cp) ||
			(*cp != ':') && (*cp != '!'))) {
		    cp++;
		}
		if (*cp == '\0') {
		    if (inLine) {
			Parse_Error(PARSE_WARNING,
				     "Shell command needs a leading tab");
			goto shellCommand;
		    }
		}
	    }
#endif
	    ParseFinishLine();

	    /*
	     * For some reason - probably to make the parser impossible -
	     * a ';' can be used to separate commands from dependencies.
	     * Attempt to avoid ';' inside substitution patterns.
	     */
	    {
		int level = 0;

		for (cp = line; *cp != 0; cp++) {
		    if (*cp == '\\' && cp[1] != 0) {
			cp++;
			continue;
		    }
		    if (*cp == '$' &&
			(cp[1] == '(' || cp[1] == '{')) {
			level++;
			continue;
		    }
		    if (level > 0) {
			if (*cp == ')' || *cp == '}') {
			    level--;
			    continue;
			}
		    } else if (*cp == ';') {
			break;
		    }
		}
	    }
	    if (*cp != 0)
		/* Terminate the dependency list at the ';' */
		*cp++ = 0;
	    else
		cp = NULL;

	    /*
	     * We now know it's a dependency line so it needs to have all
	     * variables expanded before being parsed. Tell the variable
	     * module to complain if some variable is undefined...
	     */
	    line = Var_Subst(NULL, line, VAR_CMD, VARF_UNDEFERR|VARF_WANTRES);

	    /*
	     * Need a non-circular list for the target nodes
	     */
	    if (targets)
		Lst_Destroy(targets, NULL);

	    targets = Lst_Init(FALSE);
	    inLine = TRUE;

	    ParseDoDependency(line);
	    free(line);

	    /* If there were commands after a ';', add them now */
	    if (cp != NULL) {
		goto shellCommand;
	    }
	}
	/*
	 * Reached EOF, but it may be just EOF of an include file...
	 */
    } while (ParseEOF() == CONTINUE);

    if (fatals) {
	(void)fflush(stdout);
	(void)fprintf(stderr,
	    "%s: Fatal errors encountered -- cannot continue",
	    progname);
	PrintOnError(NULL, NULL);
	exit(1);
    }
}

/*-
 *---------------------------------------------------------------------
 * Parse_Init --
 *	initialize the parsing module
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	the parseIncPath list is initialized...
 *---------------------------------------------------------------------
 */
void
Parse_Init(void)
{
    mainNode = NULL;
    parseIncPath = Lst_Init(FALSE);
    sysIncPath = Lst_Init(FALSE);
    defIncPath = Lst_Init(FALSE);
    includes = Lst_Init(FALSE);
#ifdef CLEANUP
    targCmds = Lst_Init(FALSE);
#endif
}

void
Parse_End(void)
{
#ifdef CLEANUP
    Lst_Destroy(targCmds, (FreeProc *)free);
    if (targets)
	Lst_Destroy(targets, NULL);
    Lst_Destroy(defIncPath, Dir_Destroy);
    Lst_Destroy(sysIncPath, Dir_Destroy);
    Lst_Destroy(parseIncPath, Dir_Destroy);
    Lst_Destroy(includes, NULL);	/* Should be empty now */
#endif
}


/*-
 *-----------------------------------------------------------------------
 * Parse_MainName --
 *	Return a Lst of the main target to create for main()'s sake. If
 *	no such target exists, we Punt with an obnoxious error message.
 *
 * Results:
 *	A Lst of the single node to create.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
Lst
Parse_MainName(void)
{
    Lst           mainList;	/* result list */

    mainList = Lst_Init(FALSE);

    if (mainNode == NULL) {
	Punt("no target to make.");
    	/*NOTREACHED*/
    } else if (mainNode->type & OP_DOUBLEDEP) {
	(void)Lst_AtEnd(mainList, mainNode);
	Lst_Concat(mainList, mainNode->cohorts, LST_CONCNEW);
    }
    else
	(void)Lst_AtEnd(mainList, mainNode);
    Var_Append(".TARGETS", mainNode->name, VAR_GLOBAL);
    return (mainList);
}

/*-
 *-----------------------------------------------------------------------
 * ParseMark --
 *	Add the filename and lineno to the GNode so that we remember
 *	where it was first defined.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static void
ParseMark(GNode *gn)
{
    gn->fname = curFile->fname;
    gn->lineno = curFile->lineno;
}
