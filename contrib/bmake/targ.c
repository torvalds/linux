/*	$NetBSD: targ.c,v 1.62 2017/04/16 19:53:58 riastradh Exp $	*/

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
static char rcsid[] = "$NetBSD: targ.c,v 1.62 2017/04/16 19:53:58 riastradh Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)targ.c	8.2 (Berkeley) 3/19/94";
#else
__RCSID("$NetBSD: targ.c,v 1.62 2017/04/16 19:53:58 riastradh Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * targ.c --
 *	Functions for maintaining the Lst allTargets. Target nodes are
 * kept in two structures: a Lst, maintained by the list library, and a
 * hash table, maintained by the hash library.
 *
 * Interface:
 *	Targ_Init 	    	Initialization procedure.
 *
 *	Targ_End 	    	Cleanup the module
 *
 *	Targ_List 	    	Return the list of all targets so far.
 *
 *	Targ_NewGN	    	Create a new GNode for the passed target
 *	    	  	    	(string). The node is *not* placed in the
 *	    	  	    	hash table, though all its fields are
 *	    	  	    	initialized.
 *
 *	Targ_FindNode	    	Find the node for a given target, creating
 *	    	  	    	and storing it if it doesn't exist and the
 *	    	  	    	flags are right (TARG_CREATE)
 *
 *	Targ_FindList	    	Given a list of names, find nodes for all
 *	    	  	    	of them. If a name doesn't exist and the
 *	    	  	    	TARG_NOCREATE flag was given, an error message
 *	    	  	    	is printed. Else, if a name doesn't exist,
 *	    	  	    	its node is created.
 *
 *	Targ_Ignore	    	Return TRUE if errors should be ignored when
 *	    	  	    	creating the given target.
 *
 *	Targ_Silent	    	Return TRUE if we should be silent when
 *	    	  	    	creating the given target.
 *
 *	Targ_Precious	    	Return TRUE if the target is precious and
 *	    	  	    	should not be removed if we are interrupted.
 *
 *	Targ_Propagate		Propagate information between related
 *				nodes.	Should be called after the
 *				makefiles are parsed but before any
 *				action is taken.
 *
 * Debugging:
 *	Targ_PrintGraph	    	Print out the entire graphm all variables
 *	    	  	    	and statistics for the directory cache. Should
 *	    	  	    	print something for suffixes, too, but...
 */

#include	  <stdio.h>
#include	  <time.h>

#include	  "make.h"
#include	  "hash.h"
#include	  "dir.h"

static Lst        allTargets;	/* the list of all targets found so far */
#ifdef CLEANUP
static Lst	  allGNs;	/* List of all the GNodes */
#endif
static Hash_Table targets;	/* a hash table of same */

#define HTSIZE	191		/* initial size of hash table */

static int TargPrintOnlySrc(void *, void *);
static int TargPrintName(void *, void *);
#ifdef CLEANUP
static void TargFreeGN(void *);
#endif
static int TargPropagateCohort(void *, void *);
static int TargPropagateNode(void *, void *);

/*-
 *-----------------------------------------------------------------------
 * Targ_Init --
 *	Initialize this module
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The allTargets list and the targets hash table are initialized
 *-----------------------------------------------------------------------
 */
void
Targ_Init(void)
{
    allTargets = Lst_Init(FALSE);
    Hash_InitTable(&targets, HTSIZE);
}

/*-
 *-----------------------------------------------------------------------
 * Targ_End --
 *	Finalize this module
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	All lists and gnodes are cleared
 *-----------------------------------------------------------------------
 */
void
Targ_End(void)
{
#ifdef CLEANUP
    Lst_Destroy(allTargets, NULL);
    if (allGNs)
	Lst_Destroy(allGNs, TargFreeGN);
    Hash_DeleteTable(&targets);
#endif
}

/*-
 *-----------------------------------------------------------------------
 * Targ_List --
 *	Return the list of all targets
 *
 * Results:
 *	The list of all targets.
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
Lst
Targ_List(void)
{
    return allTargets;
}

/*-
 *-----------------------------------------------------------------------
 * Targ_NewGN  --
 *	Create and initialize a new graph node
 *
 * Input:
 *	name		the name to stick in the new node
 *
 * Results:
 *	An initialized graph node with the name field filled with a copy
 *	of the passed name
 *
 * Side Effects:
 *	The gnode is added to the list of all gnodes.
 *-----------------------------------------------------------------------
 */
GNode *
Targ_NewGN(const char *name)
{
    GNode *gn;

    gn = bmake_malloc(sizeof(GNode));
    gn->name = bmake_strdup(name);
    gn->uname = NULL;
    gn->path = NULL;
    if (name[0] == '-' && name[1] == 'l') {
	gn->type = OP_LIB;
    } else {
	gn->type = 0;
    }
    gn->unmade =    	0;
    gn->unmade_cohorts = 0;
    gn->cohort_num[0] = 0;
    gn->centurion =    	NULL;
    gn->made = 	    	UNMADE;
    gn->flags = 	0;
    gn->checked =	0;
    gn->mtime =		0;
    gn->cmgn =		NULL;
    gn->iParents =  	Lst_Init(FALSE);
    gn->cohorts =   	Lst_Init(FALSE);
    gn->parents =   	Lst_Init(FALSE);
    gn->children =  	Lst_Init(FALSE);
    gn->order_pred =  	Lst_Init(FALSE);
    gn->order_succ =  	Lst_Init(FALSE);
    Hash_InitTable(&gn->context, 0);
    gn->commands =  	Lst_Init(FALSE);
    gn->suffix =	NULL;
    gn->lineno =	0;
    gn->fname = 	NULL;

#ifdef CLEANUP
    if (allGNs == NULL)
	allGNs = Lst_Init(FALSE);
    Lst_AtEnd(allGNs, gn);
#endif

    return (gn);
}

#ifdef CLEANUP
/*-
 *-----------------------------------------------------------------------
 * TargFreeGN  --
 *	Destroy a GNode
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
static void
TargFreeGN(void *gnp)
{
    GNode *gn = (GNode *)gnp;


    free(gn->name);
    free(gn->uname);
    free(gn->path);
    /* gn->fname points to name allocated when file was opened, don't free */

    Lst_Destroy(gn->iParents, NULL);
    Lst_Destroy(gn->cohorts, NULL);
    Lst_Destroy(gn->parents, NULL);
    Lst_Destroy(gn->children, NULL);
    Lst_Destroy(gn->order_succ, NULL);
    Lst_Destroy(gn->order_pred, NULL);
    Hash_DeleteTable(&gn->context);
    Lst_Destroy(gn->commands, NULL);
    free(gn);
}
#endif


/*-
 *-----------------------------------------------------------------------
 * Targ_FindNode  --
 *	Find a node in the list using the given name for matching
 *
 * Input:
 *	name		the name to find
 *	flags		flags governing events when target not
 *			found
 *
 * Results:
 *	The node in the list if it was. If it wasn't, return NULL of
 *	flags was TARG_NOCREATE or the newly created and initialized node
 *	if it was TARG_CREATE
 *
 * Side Effects:
 *	Sometimes a node is created and added to the list
 *-----------------------------------------------------------------------
 */
GNode *
Targ_FindNode(const char *name, int flags)
{
    GNode         *gn;	      /* node in that element */
    Hash_Entry	  *he = NULL; /* New or used hash entry for node */
    Boolean	  isNew;      /* Set TRUE if Hash_CreateEntry had to create */
			      /* an entry for the node */

    if (!(flags & (TARG_CREATE | TARG_NOHASH))) {
	he = Hash_FindEntry(&targets, name);
	if (he == NULL)
	    return NULL;
	return (GNode *)Hash_GetValue(he);
    }

    if (!(flags & TARG_NOHASH)) {
	he = Hash_CreateEntry(&targets, name, &isNew);
	if (!isNew)
	    return (GNode *)Hash_GetValue(he);
    }

    gn = Targ_NewGN(name);
    if (!(flags & TARG_NOHASH))
	Hash_SetValue(he, gn);
    Var_Append(".ALLTARGETS", name, VAR_GLOBAL);
    (void)Lst_AtEnd(allTargets, gn);
    if (doing_depend)
	gn->flags |= FROM_DEPEND;
    return gn;
}

/*-
 *-----------------------------------------------------------------------
 * Targ_FindList --
 *	Make a complete list of GNodes from the given list of names
 *
 * Input:
 *	name		list of names to find
 *	flags		flags used if no node is found for a given name
 *
 * Results:
 *	A complete list of graph nodes corresponding to all instances of all
 *	the names in names.
 *
 * Side Effects:
 *	If flags is TARG_CREATE, nodes will be created for all names in
 *	names which do not yet have graph nodes. If flags is TARG_NOCREATE,
 *	an error message will be printed for each name which can't be found.
 * -----------------------------------------------------------------------
 */
Lst
Targ_FindList(Lst names, int flags)
{
    Lst            nodes;	/* result list */
    LstNode	   ln;		/* name list element */
    GNode	   *gn;		/* node in tLn */
    char    	   *name;

    nodes = Lst_Init(FALSE);

    if (Lst_Open(names) == FAILURE) {
	return (nodes);
    }
    while ((ln = Lst_Next(names)) != NULL) {
	name = (char *)Lst_Datum(ln);
	gn = Targ_FindNode(name, flags);
	if (gn != NULL) {
	    /*
	     * Note: Lst_AtEnd must come before the Lst_Concat so the nodes
	     * are added to the list in the order in which they were
	     * encountered in the makefile.
	     */
	    (void)Lst_AtEnd(nodes, gn);
	} else if (flags == TARG_NOCREATE) {
	    Error("\"%s\" -- target unknown.", name);
	}
    }
    Lst_Close(names);
    return (nodes);
}

/*-
 *-----------------------------------------------------------------------
 * Targ_Ignore  --
 *	Return true if should ignore errors when creating gn
 *
 * Input:
 *	gn		node to check for
 *
 * Results:
 *	TRUE if should ignore errors
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
Boolean
Targ_Ignore(GNode *gn)
{
    if (ignoreErrors || gn->type & OP_IGNORE) {
	return (TRUE);
    } else {
	return (FALSE);
    }
}

/*-
 *-----------------------------------------------------------------------
 * Targ_Silent  --
 *	Return true if be silent when creating gn
 *
 * Input:
 *	gn		node to check for
 *
 * Results:
 *	TRUE if should be silent
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
Boolean
Targ_Silent(GNode *gn)
{
    if (beSilent || gn->type & OP_SILENT) {
	return (TRUE);
    } else {
	return (FALSE);
    }
}

/*-
 *-----------------------------------------------------------------------
 * Targ_Precious --
 *	See if the given target is precious
 *
 * Input:
 *	gn		the node to check
 *
 * Results:
 *	TRUE if it is precious. FALSE otherwise
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
Boolean
Targ_Precious(GNode *gn)
{
    if (allPrecious || (gn->type & (OP_PRECIOUS|OP_DOUBLEDEP))) {
	return (TRUE);
    } else {
	return (FALSE);
    }
}

/******************* DEBUG INFO PRINTING ****************/

static GNode	  *mainTarg;	/* the main target, as set by Targ_SetMain */
/*-
 *-----------------------------------------------------------------------
 * Targ_SetMain --
 *	Set our idea of the main target we'll be creating. Used for
 *	debugging output.
 *
 * Input:
 *	gn		The main target we'll create
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	"mainTarg" is set to the main target's node.
 *-----------------------------------------------------------------------
 */
void
Targ_SetMain(GNode *gn)
{
    mainTarg = gn;
}

static int
TargPrintName(void *gnp, void *pflags MAKE_ATTR_UNUSED)
{
    GNode *gn = (GNode *)gnp;

    fprintf(debug_file, "%s%s ", gn->name, gn->cohort_num);

    return 0;
}


int
Targ_PrintCmd(void *cmd, void *dummy MAKE_ATTR_UNUSED)
{
    fprintf(debug_file, "\t%s\n", (char *)cmd);
    return 0;
}

/*-
 *-----------------------------------------------------------------------
 * Targ_FmtTime --
 *	Format a modification time in some reasonable way and return it.
 *
 * Results:
 *	The time reformatted.
 *
 * Side Effects:
 *	The time is placed in a static area, so it is overwritten
 *	with each call.
 *
 *-----------------------------------------------------------------------
 */
char *
Targ_FmtTime(time_t tm)
{
    struct tm	  	*parts;
    static char	  	buf[128];

    parts = localtime(&tm);
    (void)strftime(buf, sizeof buf, "%k:%M:%S %b %d, %Y", parts);
    return(buf);
}

/*-
 *-----------------------------------------------------------------------
 * Targ_PrintType --
 *	Print out a type field giving only those attributes the user can
 *	set.
 *
 * Results:
 *
 * Side Effects:
 *
 *-----------------------------------------------------------------------
 */
void
Targ_PrintType(int type)
{
    int    tbit;

#define PRINTBIT(attr)	case CONCAT(OP_,attr): fprintf(debug_file, "." #attr " "); break
#define PRINTDBIT(attr) case CONCAT(OP_,attr): if (DEBUG(TARG))fprintf(debug_file, "." #attr " "); break

    type &= ~OP_OPMASK;

    while (type) {
	tbit = 1 << (ffs(type) - 1);
	type &= ~tbit;

	switch(tbit) {
	    PRINTBIT(OPTIONAL);
	    PRINTBIT(USE);
	    PRINTBIT(EXEC);
	    PRINTBIT(IGNORE);
	    PRINTBIT(PRECIOUS);
	    PRINTBIT(SILENT);
	    PRINTBIT(MAKE);
	    PRINTBIT(JOIN);
	    PRINTBIT(INVISIBLE);
	    PRINTBIT(NOTMAIN);
	    PRINTDBIT(LIB);
	    /*XXX: MEMBER is defined, so CONCAT(OP_,MEMBER) gives OP_"%" */
	    case OP_MEMBER: if (DEBUG(TARG))fprintf(debug_file, ".MEMBER "); break;
	    PRINTDBIT(ARCHV);
	    PRINTDBIT(MADE);
	    PRINTDBIT(PHONY);
	}
    }
}

static const char *
made_name(enum enum_made made)
{
    switch (made) {
    case UNMADE:     return "unmade";
    case DEFERRED:   return "deferred";
    case REQUESTED:  return "requested";
    case BEINGMADE:  return "being made";
    case MADE:       return "made";
    case UPTODATE:   return "up-to-date";
    case ERROR:      return "error when made";
    case ABORTED:    return "aborted";
    default:         return "unknown enum_made value";
    }
}

/*-
 *-----------------------------------------------------------------------
 * TargPrintNode --
 *	print the contents of a node
 *-----------------------------------------------------------------------
 */
int
Targ_PrintNode(void *gnp, void *passp)
{
    GNode         *gn = (GNode *)gnp;
    int	    	  pass = passp ? *(int *)passp : 0;

    fprintf(debug_file, "# %s%s, flags %x, type %x, made %d\n",
	    gn->name, gn->cohort_num, gn->flags, gn->type, gn->made);
    if (gn->flags == 0)
	return 0;

    if (!OP_NOP(gn->type)) {
	fprintf(debug_file, "#\n");
	if (gn == mainTarg) {
	    fprintf(debug_file, "# *** MAIN TARGET ***\n");
	}
	if (pass >= 2) {
	    if (gn->unmade) {
		fprintf(debug_file, "# %d unmade children\n", gn->unmade);
	    } else {
		fprintf(debug_file, "# No unmade children\n");
	    }
	    if (! (gn->type & (OP_JOIN|OP_USE|OP_USEBEFORE|OP_EXEC))) {
		if (gn->mtime != 0) {
		    fprintf(debug_file, "# last modified %s: %s\n",
			      Targ_FmtTime(gn->mtime),
			      made_name(gn->made));
		} else if (gn->made != UNMADE) {
		    fprintf(debug_file, "# non-existent (maybe): %s\n",
			      made_name(gn->made));
		} else {
		    fprintf(debug_file, "# unmade\n");
		}
	    }
	    if (!Lst_IsEmpty (gn->iParents)) {
		fprintf(debug_file, "# implicit parents: ");
		Lst_ForEach(gn->iParents, TargPrintName, NULL);
		fprintf(debug_file, "\n");
	    }
	} else {
	    if (gn->unmade)
		fprintf(debug_file, "# %d unmade children\n", gn->unmade);
	}
	if (!Lst_IsEmpty (gn->parents)) {
	    fprintf(debug_file, "# parents: ");
	    Lst_ForEach(gn->parents, TargPrintName, NULL);
	    fprintf(debug_file, "\n");
	}
	if (!Lst_IsEmpty (gn->order_pred)) {
	    fprintf(debug_file, "# order_pred: ");
	    Lst_ForEach(gn->order_pred, TargPrintName, NULL);
	    fprintf(debug_file, "\n");
	}
	if (!Lst_IsEmpty (gn->order_succ)) {
	    fprintf(debug_file, "# order_succ: ");
	    Lst_ForEach(gn->order_succ, TargPrintName, NULL);
	    fprintf(debug_file, "\n");
	}

	fprintf(debug_file, "%-16s", gn->name);
	switch (gn->type & OP_OPMASK) {
	    case OP_DEPENDS:
		fprintf(debug_file, ": "); break;
	    case OP_FORCE:
		fprintf(debug_file, "! "); break;
	    case OP_DOUBLEDEP:
		fprintf(debug_file, ":: "); break;
	}
	Targ_PrintType(gn->type);
	Lst_ForEach(gn->children, TargPrintName, NULL);
	fprintf(debug_file, "\n");
	Lst_ForEach(gn->commands, Targ_PrintCmd, NULL);
	fprintf(debug_file, "\n\n");
	if (gn->type & OP_DOUBLEDEP) {
	    Lst_ForEach(gn->cohorts, Targ_PrintNode, &pass);
	}
    }
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * TargPrintOnlySrc --
 *	Print only those targets that are just a source.
 *
 * Results:
 *	0.
 *
 * Side Effects:
 *	The name of each file is printed preceded by #\t
 *
 *-----------------------------------------------------------------------
 */
static int
TargPrintOnlySrc(void *gnp, void *dummy MAKE_ATTR_UNUSED)
{
    GNode   	  *gn = (GNode *)gnp;
    if (!OP_NOP(gn->type))
	return 0;

    fprintf(debug_file, "#\t%s [%s] ",
	    gn->name, gn->path ? gn->path : gn->name);
    Targ_PrintType(gn->type);
    fprintf(debug_file, "\n");

    return 0;
}

/*-
 *-----------------------------------------------------------------------
 * Targ_PrintGraph --
 *	print the entire graph. heh heh
 *
 * Input:
 *	pass		Which pass this is. 1 => no processing
 *			2 => processing done
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	lots o' output
 *-----------------------------------------------------------------------
 */
void
Targ_PrintGraph(int pass)
{
    fprintf(debug_file, "#*** Input graph:\n");
    Lst_ForEach(allTargets, Targ_PrintNode, &pass);
    fprintf(debug_file, "\n\n");
    fprintf(debug_file, "#\n#   Files that are only sources:\n");
    Lst_ForEach(allTargets, TargPrintOnlySrc, NULL);
    fprintf(debug_file, "#*** Global Variables:\n");
    Var_Dump(VAR_GLOBAL);
    fprintf(debug_file, "#*** Command-line Variables:\n");
    Var_Dump(VAR_CMD);
    fprintf(debug_file, "\n");
    Dir_PrintDirectories();
    fprintf(debug_file, "\n");
    Suff_PrintAll();
}

/*-
 *-----------------------------------------------------------------------
 * TargPropagateNode --
 *	Propagate information from a single node to related nodes if
 *	appropriate.
 *
 * Input:
 *	gnp		The node that we are processing.
 *
 * Results:
 *	Always returns 0, for the benefit of Lst_ForEach().
 *
 * Side Effects:
 *	Information is propagated from this node to cohort or child
 *	nodes.
 *
 *	If the node was defined with "::", then TargPropagateCohort()
 *	will be called for each cohort node.
 *
 *	If the node has recursive predecessors, then
 *	TargPropagateRecpred() will be called for each recursive
 *	predecessor.
 *-----------------------------------------------------------------------
 */
static int
TargPropagateNode(void *gnp, void *junk MAKE_ATTR_UNUSED)
{
    GNode	  *gn = (GNode *)gnp;

    if (gn->type & OP_DOUBLEDEP)
	Lst_ForEach(gn->cohorts, TargPropagateCohort, gnp);
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * TargPropagateCohort --
 *	Propagate some bits in the type mask from a node to
 *	a related cohort node.
 *
 * Input:
 *	cnp		The node that we are processing.
 *	gnp		Another node that has cnp as a cohort.
 *
 * Results:
 *	Always returns 0, for the benefit of Lst_ForEach().
 *
 * Side Effects:
 *	cnp's type bitmask is modified to incorporate some of the
 *	bits from gnp's type bitmask.  (XXX need a better explanation.)
 *-----------------------------------------------------------------------
 */
static int
TargPropagateCohort(void *cgnp, void *pgnp)
{
    GNode	  *cgn = (GNode *)cgnp;
    GNode	  *pgn = (GNode *)pgnp;

    cgn->type |= pgn->type & ~OP_OPMASK;
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * Targ_Propagate --
 *	Propagate information between related nodes.  Should be called
 *	after the makefiles are parsed but before any action is taken.
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	Information is propagated between related nodes throughout the
 *	graph.
 *-----------------------------------------------------------------------
 */
void
Targ_Propagate(void)
{
    Lst_ForEach(allTargets, TargPropagateNode, NULL);
}
