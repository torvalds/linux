/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: ex.h,v 10.31 2012/10/03 02:33:24 zy Exp $
 */

#define	PROMPTCHAR	':'		/* Prompt using a colon. */

typedef struct _excmdlist {		/* Ex command table structure. */
	CHAR_T *name;			/* Command name, underlying function. */
	int (*fn)(SCR *, EXCMD *);

#define	E_ADDR1		0x00000001	/* One address. */
#define	E_ADDR2		0x00000002	/* Two addresses. */
#define	E_ADDR2_ALL	0x00000004	/* Zero/two addresses; zero == all. */
#define	E_ADDR2_NONE	0x00000008	/* Zero/two addresses; zero == none. */
#define	E_ADDR_ZERO	0x00000010	/* 0 is a legal addr1. */
#define	E_ADDR_ZERODEF	0x00000020	/* 0 is default addr1 of empty files. */
#define	E_AUTOPRINT	0x00000040	/* Command always sets autoprint. */
#define	E_CLRFLAG	0x00000080	/* Clear the print (#, l, p) flags. */
#define	E_NEWSCREEN	0x00000100	/* Create a new screen. */
#define	E_SECURE	0x00000200	/* Permission denied if O_SECURE set. */
#define	E_VIONLY	0x00000400	/* Meaningful only in vi. */
#define	__INUSE1	0xfffff800	/* Same name space as EX_PRIVATE. */
	u_int16_t flags;

	char *syntax;			/* Syntax script. */
	char *usage;			/* Usage line. */
	char *help;			/* Help line. */
} EXCMDLIST;

#define	MAXCMDNAMELEN	12		/* Longest command name. */
extern EXCMDLIST const cmds[];		/* Table of ex commands. */

/*
 * !!!
 * QUOTING NOTE:
 *
 * Historically, .exrc files and EXINIT variables could only use ^V as an
 * escape character, neither ^Q or a user specified character worked.  We
 * enforce that here, just in case someone depends on it.
 */
#define	IS_ESCAPE(sp, cmdp, ch)						\
	(F_ISSET(cmdp, E_VLITONLY) ?					\
	    (ch) == CH_LITERAL : KEY_VAL(sp, ch) == K_VLNEXT)

#define	IS_SHELLMETA(sp, ch)						\
	((ch) <= CHAR_MAX && strchr(O_STR(sp, O_SHELLMETA), ch) != NULL)

/*
 * File state must be checked for each command -- any ex command may be entered
 * at any time, and most of them won't work well if a file hasn't yet been read
 * in.  Historic vi generally took the easy way out and dropped core.
 */
#define	NEEDFILE(sp, cmdp) {						\
	if ((sp)->ep == NULL) {						\
		ex_wemsg(sp, (cmdp)->cmd->name, EXM_NOFILEYET);		\
		return (1);						\
	}								\
}

/* Range structures for global and @ commands. */
typedef struct _range RANGE;
struct _range {				/* Global command range. */
	TAILQ_ENTRY(_range) q;		/* Linked list of ranges. */
	recno_t start, stop;		/* Start/stop of the range. */
};

/* Ex command structure. */
struct _excmd {
	SLIST_ENTRY(_excmd) q;		/* Linked list of commands. */

	char	 *if_name;		/* Associated file. */
	recno_t	  if_lno;		/* Associated line number. */

	/* Clear the structure for the ex parser. */
#define	CLEAR_EX_PARSER(cmdp)						\
	memset(&((cmdp)->cp), 0, ((char *)&(cmdp)->flags -		\
	    (char *)&((cmdp)->cp)) + sizeof((cmdp)->flags))

	CHAR_T	 *cp;			/* Current command text. */
	size_t	  clen;			/* Current command length. */

	CHAR_T	 *save_cmd;		/* Remaining command. */
	size_t	  save_cmdlen;		/* Remaining command length. */

	EXCMDLIST const *cmd;		/* Command: entry in command table. */
	EXCMDLIST rcmd;			/* Command: table entry/replacement. */

	TAILQ_HEAD(_rh, _range) rq[1];	/* @/global range: linked list. */
	recno_t   range_lno;		/* @/global range: set line number. */
	CHAR_T	 *o_cp;			/* Original @/global command. */
	size_t	  o_clen;		/* Original @/global command length. */
#define	AGV_AT		0x01		/* @ buffer execution. */
#define	AGV_AT_NORANGE	0x02		/* @ buffer execution without range. */
#define	AGV_GLOBAL	0x04		/* global command. */
#define	AGV_V		0x08		/* v command. */
#define	AGV_ALL		(AGV_AT | AGV_AT_NORANGE | AGV_GLOBAL | AGV_V)
	u_int8_t  agv_flags;

	/* Clear the structure before each ex command. */
#define	CLEAR_EX_CMD(cmdp) {						\
	u_int32_t L__f = F_ISSET(cmdp, E_PRESERVE);			\
	memset(&((cmdp)->buffer), 0, ((char *)&(cmdp)->flags -		\
	    (char *)&((cmdp)->buffer)) + sizeof((cmdp)->flags));	\
	F_SET(cmdp, L__f);						\
}

	CHAR_T	  buffer;		/* Command: named buffer. */
	recno_t	  lineno;		/* Command: line number. */
	long	  count;		/* Command: signed count. */
	long	  flagoff;		/* Command: signed flag offset. */
	int	  addrcnt;		/* Command: addresses (0, 1 or 2). */
	MARK	  addr1;		/* Command: 1st address. */
	MARK	  addr2;		/* Command: 2nd address. */
	ARGS	**argv;			/* Command: array of arguments. */
	int	  argc;			/* Command: count of arguments. */

#define	E_C_BUFFER	0x00001		/* Buffer name specified. */
#define	E_C_CARAT	0x00002		/*  ^ flag. */
#define	E_C_COUNT	0x00004		/* Count specified. */
#define	E_C_COUNT_NEG	0x00008		/* Count was signed negative. */
#define	E_C_COUNT_POS	0x00010		/* Count was signed positive. */
#define	E_C_DASH	0x00020		/*  - flag. */
#define	E_C_DOT		0x00040		/*  . flag. */
#define	E_C_EQUAL	0x00080		/*  = flag. */
#define	E_C_FORCE	0x00100		/*  ! flag. */
#define	E_C_HASH	0x00200		/*  # flag. */
#define	E_C_LIST	0x00400		/*  l flag. */
#define	E_C_PLUS	0x00800		/*  + flag. */
#define	E_C_PRINT	0x01000		/*  p flag. */
	u_int16_t iflags;		/* User input information. */

#define	__INUSE2	0x000007ff	/* Same name space as EXCMDLIST. */
#define	E_BLIGNORE	0x00000800	/* Ignore blank lines. */
#define	E_NAMEDISCARD	0x00001000	/* Free/discard the name. */
#define	E_NOAUTO	0x00002000	/* Don't do autoprint output. */
#define	E_NOPRDEF	0x00004000	/* Don't print as default. */
#define	E_NRSEP		0x00008000	/* Need to line adjust ex output. */
#define	E_OPTNUM	0x00010000	/* Number edit option affected. */
#define	E_VLITONLY	0x00020000	/* Use ^V quoting only. */
#define	E_PRESERVE	0x0003f800	/* Bits to preserve across commands. */

#define	E_ABSMARK	0x00040000	/* Set the absolute mark. */
#define	E_ADDR_DEF	0x00080000	/* Default addresses used. */
#define	E_DELTA		0x00100000	/* Search address with delta. */
#define	E_MODIFY	0x00200000	/* File name expansion modified arg. */
#define	E_MOVETOEND	0x00400000	/* Move to the end of the file first. */
#define	E_NEWLINE	0x00800000	/* Found ending <newline>. */
#define	E_SEARCH_WMSG	0x01000000	/* Display search-wrapped message. */
#define	E_USELASTCMD	0x02000000	/* Use the last command. */
#define	E_VISEARCH	0x04000000	/* It's really a vi search command. */
	u_int32_t flags;		/* Current flags. */
};

/* Ex private, per-screen memory. */
typedef struct _ex_private {
					/* Tag file list. */
	TAILQ_HEAD(_tagfh, _tagf) tagfq[1];
	TAILQ_HEAD(_tqh, _tagq) tq[1];	/* Tag queue. */
	SLIST_HEAD(_csch, _csc) cscq[1];/* Cscope connection list. */
	CHAR_T	*tag_last;		/* Saved last tag string. */

	CHAR_T	*lastbcomm;		/* Last bang command. */

	ARGS   **args;			/* Command: argument list. */
	int	 argscnt;		/* Command: argument list count. */
	int	 argsoff;		/* Command: offset into arguments. */

	u_int32_t fdef;			/* Saved E_C_* default command flags. */

	char	*ibp;			/* File line input buffer. */
	size_t	 ibp_len;		/* File line input buffer length. */
	CONVWIN	 ibcw;			/* File line input conversion buffer. */

	/*
	 * Buffers for the ex output.  The screen/vi support doesn't do any
	 * character buffering of any kind.  We do it here so that we're not
	 * calling the screen output routines on every character.
	 *
	 * XXX
	 * Change to grow dynamically.
	 */
	char	 obp[1024];		/* Ex output buffer. */
	size_t	 obp_len;		/* Ex output buffer length. */

#define	EXP_CSCINIT	0x01		/* Cscope initialized. */
	u_int8_t flags;
} EX_PRIVATE;
#define	EXP(sp)	((EX_PRIVATE *)((sp)->ex_private))

/*
 * Filter actions:
 *
 *	FILTER_BANG	!:	filter text through the utility.
 *	FILTER_RBANG	!:	read from the utility (without stdin).
 *	FILTER_READ	read:	read from the utility (with stdin).
 *	FILTER_WRITE	write:	write to the utility, display its output.
 */
enum filtertype { FILTER_BANG, FILTER_RBANG, FILTER_READ, FILTER_WRITE };

/* Ex common error messages. */
typedef enum {
	EXM_EMPTYBUF,			/* Empty buffer. */
	EXM_FILECOUNT,			/* Too many file names. */
	EXM_NOCANON,			/* No terminal interface. */
	EXM_NOCANON_F,			/* EXM_NOCANO: filter version. */
	EXM_NOFILEYET,			/* Illegal until a file read in. */
	EXM_NOPREVBUF,			/* No previous buffer specified. */
	EXM_NOPREVRE,			/* No previous RE specified. */
	EXM_NOSUSPEND,			/* No suspension. */
	EXM_SECURE,			/* Illegal if secure edit option set. */
	EXM_SECURE_F,			/* EXM_SECURE: filter version */
	EXM_USAGE			/* Standard usage message. */
} exm_t;

/* Ex address error types. */
enum badaddr { A_COMBO, A_EMPTY, A_EOF, A_NOTSET, A_ZERO };

/* Ex common tag error messages. */
typedef enum {
	TAG_BADLNO,		/* Tag line doesn't exist. */
	TAG_EMPTY,		/* Tags stack is empty. */
	TAG_SEARCH		/* Tags search pattern wasn't found. */
} tagmsg_t;

#include "ex_def.h"
#include "extern.h"
