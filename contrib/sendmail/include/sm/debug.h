/*
 * Copyright (c) 2000, 2001, 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: debug.h,v 1.17 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm debugging and tracing
**  See libsm/debug.html for documentation.
*/

#ifndef SM_DEBUG_H
# define SM_DEBUG_H

# include <sm/gen.h>
# include <sm/io.h>

/*
**  abstractions for printing trace messages
*/

extern SM_FILE_T *
sm_debug_file __P((void));

extern void
sm_debug_setfile __P(( SM_FILE_T *));

extern void PRINTFLIKE(1, 2)
sm_dprintf __P((char *_fmt, ...));

extern void
sm_dflush __P((void));

extern void
sm_debug_close __P((void));

/*
**  abstractions for setting and testing debug activation levels
*/

extern void
sm_debug_addsettings_x __P((const char *));

extern void
sm_debug_addsetting_x __P((const char *, int));

# define SM_DEBUG_UNKNOWN	((SM_ATOMIC_UINT_T)(-1))

extern const char SmDebugMagic[];

typedef struct sm_debug SM_DEBUG_T;
struct sm_debug
{
	const char *sm_magic;	/* points to SmDebugMagic */

	/*
	**  debug_level is the activation level of this debug
	**  object.  Level 0 means no debug activity.
	**  It is initialized to SM_DEBUG_UNKNOWN, which indicates
	**  that the true value is unknown.  If debug_level ==
	**  SM_DEBUG_UNKNOWN, then the access functions will look up
	**  its true value in the internal table of debug settings.
	*/

	SM_ATOMIC_UINT_T debug_level;

	/*
	**  debug_name is the name used to reference this SM_DEBUG
	**  structure via the sendmail -d option.
	*/

	char *debug_name;

	/*
	**  debug_desc is a literal character string of the form
	**  "@(#)$Debug: <name> - <short description> $"
	*/

	char *debug_desc;

	/*
	**  We keep a linked list of initialized SM_DEBUG structures
	**  so that when sm_debug_addsetting is called, we can reset
	**  them all back to the uninitialized state.
	*/

	SM_DEBUG_T *debug_next;
};

# ifndef SM_DEBUG_CHECK
#  define SM_DEBUG_CHECK 1
# endif /* ! SM_DEBUG_CHECK */

# if SM_DEBUG_CHECK
/*
**  This macro is cleverly designed so that if the debug object is below
**  the specified level, then the only overhead is a single comparison
**  (except for the first time this macro is invoked).
*/

#  define sm_debug_active(debug, level) \
	    ((debug)->debug_level >= (level) && \
	     ((debug)->debug_level != SM_DEBUG_UNKNOWN || \
	      sm_debug_loadactive(debug, level)))

#  define sm_debug_level(debug) \
	    ((debug)->debug_level == SM_DEBUG_UNKNOWN \
	     ? sm_debug_loadlevel(debug) : (debug)->debug_level)

#  define sm_debug_unknown(debug) ((debug)->debug_level == SM_DEBUG_UNKNOWN)
# else /* SM_DEBUG_CHECK */
#  define sm_debug_active(debug, level)	0
#  define sm_debug_level(debug)		0
#  define sm_debug_unknown(debug)	0
# endif /* SM_DEBUG_CHECK */

extern bool
sm_debug_loadactive __P((SM_DEBUG_T *, int));

extern int
sm_debug_loadlevel __P((SM_DEBUG_T *));

# define SM_DEBUG_INITIALIZER(name, desc) { \
		SmDebugMagic, \
		SM_DEBUG_UNKNOWN, \
		name, \
		desc, \
		NULL}

#endif /* ! SM_DEBUG_H */
