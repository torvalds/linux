/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


#define	END_OPTION_STRING	('$')

/*
 * Types of options.
 */
#define	BOOL		01	/* Boolean option: 0 or 1 */
#define	TRIPLE		02	/* Triple-valued option: 0, 1 or 2 */
#define	NUMBER		04	/* Numeric option */
#define	STRING		010	/* String-valued option */
#define	NOVAR		020	/* No associated variable */
#define	REPAINT		040	/* Repaint screen after toggling option */
#define	NO_TOGGLE	0100	/* Option cannot be toggled with "-" cmd */
#define	HL_REPAINT	0200	/* Repaint hilites after toggling option */
#define	NO_QUERY	0400	/* Option cannot be queried with "_" cmd */
#define	INIT_HANDLER	01000	/* Call option handler function at startup */

#define	OTYPE		(BOOL|TRIPLE|NUMBER|STRING|NOVAR)

#define OLETTER_NONE    '\1'     /* Invalid option letter */

/*
 * Argument to a handling function tells what type of activity:
 */
#define	INIT	0	/* Initialization (from command line) */
#define	QUERY	1	/* Query (from _ or - command) */
#define	TOGGLE	2	/* Change value (from - command) */

/* Flag to toggle_option to specify how to "toggle" */
#define	OPT_NO_TOGGLE	0
#define	OPT_TOGGLE	1
#define	OPT_UNSET	2
#define	OPT_SET		3
#define OPT_NO_PROMPT	0100

/* Error code from findopt_name */
#define OPT_AMBIG       1

struct optname
{
	char *oname;            /* Long (GNU-style) option name */
	struct optname *onext;  /* List of synonymous option names */
};

#define OPTNAME_MAX	32	/* Max length of long option name */

struct loption
{
	char oletter;		/* The controlling letter (a-z) */
	struct optname *onames; /* Long (GNU-style) option name */
	int otype;		/* Type of the option */
	int odefault;		/* Default value */
	int *ovar;		/* Pointer to the associated variable */
	void (*ofunc) LESSPARAMS ((int, char*)); /* Pointer to special handling function */
	char *odesc[3];		/* Description of each value */
};

