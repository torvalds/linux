/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: options.h,v 10.21 2012/02/10 20:24:58 zy Exp $
 */

/*
 * Edit option information.  Historically, if you set a boolean or numeric
 * edit option value to its "default" value, it didn't show up in the :set
 * display, i.e. it wasn't considered "changed".  String edit options would
 * show up as changed, regardless.  We maintain a parallel set of values
 * which are the default values and never consider an edit option changed
 * if it was reset to the default value.
 *
 * Macros to retrieve boolean, integral and string option values, and to
 * set, clear and test boolean option values.  Some options (secure, lines,
 * columns, terminal type) are global in scope, and are therefore stored
 * in the global area.  The offset in the global options array is stored
 * in the screen's value field.  This is set up when the options are first
 * initialized.
 */
#define	O_V(sp, o, fld)							\
	(F_ISSET(&(sp)->opts[(o)], OPT_GLOBAL) ?			\
	    (sp)->gp->opts[(sp)->opts[(o)].o_cur.val].fld :		\
	    (sp)->opts[(o)].fld)

/* Global option macros. */
#define	OG_CLR(gp, o)		((gp)->opts[(o)].o_cur.val) = 0
#define	OG_SET(gp, o)		((gp)->opts[(o)].o_cur.val) = 1
#define	OG_STR(gp, o)		((gp)->opts[(o)].o_cur.str)
#define	OG_VAL(gp, o)		((gp)->opts[(o)].o_cur.val)
#define	OG_ISSET(gp, o)		OG_VAL(gp, o)

#define	OG_D_STR(gp, o)		((gp)->opts[(o)].o_def.str)
#define	OG_D_VAL(gp, o)		((gp)->opts[(o)].o_def.val)

/*
 * Flags to o_set(); need explicit OS_STR as can be setting the value to
 * NULL.
 */
#define	OS_DEF		0x01		/* Set the default value. */
#define	OS_NOFREE	0x02		/* Don't free the old string. */
#define	OS_STR		0x04		/* Set to string argument. */
#define	OS_STRDUP	0x08		/* Copy then set to string argument. */

struct _option {
	union {
		u_long	 val;		/* Value or boolean. */
		char	*str;		/* String. */
	} o_cur;
#define	O_CLR(sp, o)		o_set(sp, o, 0, NULL, 0)
#define	O_SET(sp, o)		o_set(sp, o, 0, NULL, 1)
#define	O_STR(sp, o)		O_V(sp, o, o_cur.str)
#define	O_VAL(sp, o)		O_V(sp, o, o_cur.val)
#define	O_ISSET(sp, o)		O_VAL(sp, o)

	union {
		u_long	 val;		/* Value or boolean. */
		char	*str;		/* String. */
	} o_def;
#define	O_D_CLR(sp, o)		o_set(sp, o, OS_DEF, NULL, 0)
#define	O_D_SET(sp, o)		o_set(sp, o, OS_DEF, NULL, 1)
#define	O_D_STR(sp, o)		O_V(sp, o, o_def.str)
#define	O_D_VAL(sp, o)		O_V(sp, o, o_def.val)
#define	O_D_ISSET(sp, o)	O_D_VAL(sp, o)

#define	OPT_GLOBAL	0x01		/* Option is global. */
#define	OPT_SELECTED	0x02		/* Selected for display. */
	u_int8_t flags;
};

/* List of option names, associated update functions and information. */
struct _optlist {
	CHAR_T	*name;			/* Name. */
					/* Change function. */
	int	(*func)(SCR *, OPTION *, char *, u_long *);
					/* Type of object. */
	enum { OPT_0BOOL, OPT_1BOOL, OPT_NUM, OPT_STR } type;

#define	OPT_ADISP	0x001		/* Always display the option. */
#define	OPT_ALWAYS	0x002		/* Always call the support function. */
#define	OPT_NDISP	0x004		/* Never display the option. */
#define	OPT_NOSAVE	0x008		/* Mkexrc command doesn't save. */
#define	OPT_NOSET	0x010		/* Option may not be set. */
#define	OPT_NOUNSET	0x020		/* Option may not be unset. */
#define	OPT_NOZERO	0x040		/* Option may not be set to 0. */
#define	OPT_PAIRS	0x080		/* String with even length. */
	u_int8_t flags;
};

/* Option argument to opts_dump(). */
enum optdisp { NO_DISPLAY, ALL_DISPLAY, CHANGED_DISPLAY, SELECT_DISPLAY };

/* Options array. */
extern OPTLIST const optlist[];

#include "options_def.h"
