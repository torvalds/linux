/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_map.c,v 10.11 2001/06/25 15:19:17 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"

/*
 * ex_map -- :map[!] [input] [replacement]
 *	Map a key/string or display mapped keys.
 *
 * Historical note:
 *	Historic vi maps were fairly bizarre, and likely to differ in
 *	very subtle and strange ways from this implementation.  Two
 *	things worth noting are that vi would often hang or drop core
 *	if the map was strange enough (ex: map X "xy$@x^V), or, simply
 *	not work.  One trick worth remembering is that if you put a
 *	mark at the start of the map, e.g. map X mx"xy ...), or if you
 *	put the map in a .exrc file, things would often work much better.
 *	No clue why.
 *
 * PUBLIC: int ex_map(SCR *, EXCMD *);
 */
int
ex_map(SCR *sp, EXCMD *cmdp)
{
	seq_t stype;
	CHAR_T *input, *p;

	stype = FL_ISSET(cmdp->iflags, E_C_FORCE) ? SEQ_INPUT : SEQ_COMMAND;

	switch (cmdp->argc) {
	case 0:
		if (seq_dump(sp, stype, 1) == 0)
			msgq(sp, M_INFO, stype == SEQ_INPUT ?
			    "132|No input map entries" :
			    "133|No command map entries");
		return (0);
	case 2:
		input = cmdp->argv[0]->bp;
		break;
	default:
		abort();
	}

	/*
	 * If the mapped string is #[0-9]* (and wasn't quoted) then store the
	 * function key mapping.  If the screen specific routine has been set,
	 * call it as well.  Note, the SEQ_FUNCMAP type is persistent across
	 * screen types, maybe the next screen type will get it right.
	 */
	if (input[0] == '#' && isdigit(input[1])) {
		for (p = input + 2; isdigit(*p); ++p);
		if (p[0] != '\0')
			goto nofunc;

		if (seq_set(sp, NULL, 0, input, cmdp->argv[0]->len,
		    cmdp->argv[1]->bp, cmdp->argv[1]->len, stype,
		    SEQ_FUNCMAP | SEQ_USERDEF))
			return (1);
		return (sp->gp->scr_fmap == NULL ? 0 :
		    sp->gp->scr_fmap(sp, stype, input, cmdp->argv[0]->len,
		    cmdp->argv[1]->bp, cmdp->argv[1]->len));
	}

	/* Some single keys may not be remapped in command mode. */
nofunc:	if (stype == SEQ_COMMAND && input[1] == '\0')
		switch (KEY_VAL(sp, input[0])) {
		case K_COLON:
		case K_ESCAPE:
		case K_NL:
			msgq(sp, M_ERR,
			    "134|The %s character may not be remapped",
			    KEY_NAME(sp, input[0]));
			return (1);
		}
	return (seq_set(sp, NULL, 0, input, cmdp->argv[0]->len,
	    cmdp->argv[1]->bp, cmdp->argv[1]->len, stype, SEQ_USERDEF));
}

/*
 * ex_unmap -- (:unmap[!] key)
 *	Unmap a key.
 *
 * PUBLIC: int ex_unmap(SCR *, EXCMD *);
 */
int
ex_unmap(SCR *sp, EXCMD *cmdp)
{
	if (seq_delete(sp, cmdp->argv[0]->bp, cmdp->argv[0]->len,
	    FL_ISSET(cmdp->iflags, E_C_FORCE) ? SEQ_INPUT : SEQ_COMMAND)) {
		msgq_wstr(sp, M_INFO,
		    cmdp->argv[0]->bp, "135|\"%s\" isn't currently mapped");
		return (1);
	}
	return (0);
}
