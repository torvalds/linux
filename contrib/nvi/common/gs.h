/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: gs.h,v 11.0 2012/10/17 06:34:37 zy Exp $
 */

#define	TEMPORARY_FILE_STRING	"/tmp"	/* Default temporary file name. */

#include <nl_types.h>

/*
 * File reference structure (FREF).  The structure contains the name of the
 * file, along with the information that follows the name.
 *
 * !!!
 * The read-only bit follows the file name, not the file itself.
 */
struct _fref {
	TAILQ_ENTRY(_fref) q;		/* Linked list of file references. */
	char	*name;			/* File name. */
	char	*tname;			/* Backing temporary file name. */

	recno_t	 lno;			/* 1-N: file cursor line. */
	size_t	 cno;			/* 0-N: file cursor column. */

#define	FR_CURSORSET	0x0001		/* If lno/cno values valid. */
#define	FR_DONTDELETE	0x0002		/* Don't delete the temporary file. */
#define	FR_EXNAMED	0x0004		/* Read/write renamed the file. */
#define	FR_NAMECHANGE	0x0008		/* If the name changed. */
#define	FR_NEWFILE	0x0010		/* File doesn't really exist yet. */
#define	FR_RECOVER	0x0020		/* File is being recovered. */
#define	FR_TMPEXIT	0x0040		/* Modified temporary file, no exit. */
#define	FR_TMPFILE	0x0080		/* If file has no name. */
#define	FR_UNLOCKED	0x0100		/* File couldn't be locked. */
	u_int16_t flags;
};

/* Action arguments to scr_exadjust(). */
typedef enum { EX_TERM_CE, EX_TERM_SCROLL } exadj_t;

/* Screen attribute arguments to scr_attr(). */
typedef enum { SA_ALTERNATE, SA_INVERSE } scr_attr_t;

/* Key type arguments to scr_keyval(). */
typedef enum { KEY_VEOF, KEY_VERASE, KEY_VKILL, KEY_VWERASE } scr_keyval_t;

/*
 * GS:
 *
 * Structure that describes global state of the running program.
 */
struct _gs {
	char	*progname;		/* Programe name. */

	int	 id;			/* Last allocated screen id. */
	TAILQ_HEAD(_dqh, _scr) dq[1];	/* Displayed screens. */
	TAILQ_HEAD(_hqh, _scr) hq[1];	/* Hidden screens. */

	SCR	*ccl_sp;		/* Colon command-line screen. */

	void	*cl_private;		/* Curses support private area. */

					/* File references. */
	TAILQ_HEAD(_frefh, _fref) frefq[1];

#define	GO_COLUMNS	0		/* Global options: columns. */
#define	GO_LINES	1		/* Global options: lines. */
#define	GO_SECURE	2		/* Global options: secure. */
#define	GO_TERM		3		/* Global options: terminal type. */
	OPTION	 opts[GO_TERM + 1];

	nl_catd	 catd;			/* Message catalog descriptor. */
	MSGH	 msgq[1];		/* User message list. */
#define	DEFAULT_NOPRINT	'\1'		/* Emergency non-printable character. */
	int	 noprint;		/* Cached, unprintable character. */

	char	*tmp_bp;		/* Temporary buffer. */
	size_t	 tmp_blen;		/* Temporary buffer size. */

	/*
	 * Ex command structures (EXCMD).  Defined here because ex commands
	 * exist outside of any particular screen or file.
	 */
#define	EXCMD_RUNNING(gp)	(SLIST_FIRST((gp)->ecq)->clen != 0)
					/* Ex command linked list. */
	SLIST_HEAD(_excmdh, _excmd) ecq[1];
	EXCMD	 excmd;			/* Default ex command structure. */
	char	 *if_name;		/* Current associated file. */
	recno_t	  if_lno;		/* Current associated line number. */

	char	*c_option;		/* Ex initial, command-line command. */

#ifdef DEBUG
	FILE	*tracefp;		/* Trace file pointer. */
#endif

	EVENT	*i_event;		/* Array of input events. */
	size_t	 i_nelem;		/* Number of array elements. */
	size_t	 i_cnt;			/* Count of events. */
	size_t	 i_next;		/* Offset of next event. */

	CB	*dcbp;			/* Default cut buffer pointer. */
	CB	 dcb_store;		/* Default cut buffer storage. */
	SLIST_HEAD(_cuth, _cb) cutq[1];	/* Linked list of cut buffers. */

#define	MAX_BIT_SEQ	0x7f		/* Max + 1 fast check character. */
	SLIST_HEAD(_seqh, _seq) seqq[1];/* Linked list of maps, abbrevs. */
	bitstr_t bit_decl(seqb, MAX_BIT_SEQ + 1);

#define	MAX_FAST_KEY	0xff		/* Max fast check character.*/
#define	KEY_LEN(sp, ch)							\
	(((ch) & ~MAX_FAST_KEY) == 0 ?					\
	    sp->gp->cname[(unsigned char)ch].len : v_key_len(sp, ch))
#define	KEY_NAME(sp, ch)						\
	(((ch) & ~MAX_FAST_KEY) == 0 ?					\
	    sp->gp->cname[(unsigned char)ch].name : v_key_name(sp, ch))
	struct {
		char	 name[MAX_CHARACTER_COLUMNS + 1];
		u_int8_t len;
	} cname[MAX_FAST_KEY + 1];	/* Fast lookup table. */

#define	KEY_VAL(sp, ch)							\
	(((ch) & ~MAX_FAST_KEY) == 0 ? 					\
	    sp->gp->special_key[(unsigned char)ch] : v_key_val(sp,ch))
	e_key_t				/* Fast lookup table. */
	    special_key[MAX_FAST_KEY + 1];

/* Flags. */
#define	G_ABBREV	0x0001		/* If have abbreviations. */
#define	G_BELLSCHED	0x0002		/* Bell scheduled. */
#define	G_INTERRUPTED	0x0004		/* Interrupted. */
#define	G_RECOVER_SET	0x0008		/* Recover system initialized. */
#define	G_SCRIPTED	0x0010		/* Ex script session. */
#define	G_SCRWIN	0x0020		/* Scripting windows running. */
#define	G_SNAPSHOT	0x0040		/* Always snapshot files. */
#define	G_SRESTART	0x0080		/* Screen restarted. */
#define	G_TMP_INUSE	0x0100		/* Temporary buffer in use. */
	u_int32_t flags;

	/* Screen interface functions. */
					/* Add a string to the screen. */
	int	(*scr_addstr)(SCR *, const char *, size_t);
					/* Add a string to the screen. */
	int	(*scr_waddstr)(SCR *, const CHAR_T *, size_t);
					/* Toggle a screen attribute. */
	int	(*scr_attr)(SCR *, scr_attr_t, int);
					/* Terminal baud rate. */
	int	(*scr_baud)(SCR *, u_long *);
					/* Beep/bell/flash the terminal. */
	int	(*scr_bell)(SCR *);
					/* Display a busy message. */
	void	(*scr_busy)(SCR *, const char *, busy_t);
					/* Prepare child. */
	int	(*scr_child)(SCR *);
					/* Clear to the end of the line. */
	int	(*scr_clrtoeol)(SCR *);
					/* Return the cursor location. */
	int	(*scr_cursor)(SCR *, size_t *, size_t *);
					/* Delete a line. */
	int	(*scr_deleteln)(SCR *);
					/* Discard a screen. */
	int	(*scr_discard)(SCR *, SCR **);
					/* Get a keyboard event. */
	int	(*scr_event)(SCR *, EVENT *, u_int32_t, int);
					/* Ex: screen adjustment routine. */
	int	(*scr_ex_adjust)(SCR *, exadj_t);
	int	(*scr_fmap)		/* Set a function key. */
	   (SCR *, seq_t, CHAR_T *, size_t, CHAR_T *, size_t);
					/* Get terminal key value. */
	int	(*scr_keyval)(SCR *, scr_keyval_t, CHAR_T *, int *);
					/* Insert a line. */
	int	(*scr_insertln)(SCR *);
					/* Handle an option change. */
	int	(*scr_optchange)(SCR *, int, char *, u_long *);
					/* Move the cursor. */
	int	(*scr_move)(SCR *, size_t, size_t);
					/* Message or ex output. */
	void	(*scr_msg)(SCR *, mtype_t, char *, size_t);
					/* Refresh the screen. */
	int	(*scr_refresh)(SCR *, int);
					/* Rename the file. */
	int	(*scr_rename)(SCR *, char *, int);
					/* Reply to an event. */
	int	(*scr_reply)(SCR *, int, char *);
					/* Set the screen type. */
	int	(*scr_screen)(SCR *, u_int32_t);
					/* Split the screen. */
	int	(*scr_split)(SCR *, SCR *);
					/* Suspend the editor. */
	int	(*scr_suspend)(SCR *, int *);
					/* Print usage message. */
	void	(*scr_usage)(void);
};
