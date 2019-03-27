/*	$NetBSD: make.c,v 1.96 2016/11/10 23:41:58 sjg Exp $	*/

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
static char rcsid[] = "$NetBSD: make.c,v 1.96 2016/11/10 23:41:58 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)make.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: make.c,v 1.96 2016/11/10 23:41:58 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * make.c --
 *	The functions which perform the examination of targets and
 *	their suitability for creation
 *
 * Interface:
 *	Make_Run 	    	Initialize things for the module and recreate
 *	    	  	    	whatever needs recreating. Returns TRUE if
 *	    	    	    	work was (or would have been) done and FALSE
 *	    	  	    	otherwise.
 *
 *	Make_Update	    	Update all parents of a given child. Performs
 *	    	  	    	various bookkeeping chores like the updating
 *	    	  	    	of the cmgn field of the parent, filling
 *	    	  	    	of the IMPSRC context variable, etc. It will
 *	    	  	    	place the parent on the toBeMade queue if it
 *	    	  	    	should be.
 *
 *	Make_TimeStamp	    	Function to set the parent's cmgn field
 *	    	  	    	based on a child's modification time.
 *
 *	Make_DoAllVar	    	Set up the various local variables for a
 *	    	  	    	target, including the .ALLSRC variable, making
 *	    	  	    	sure that any variable that needs to exist
 *	    	  	    	at the very least has the empty value.
 *
 *	Make_OODate 	    	Determine if a target is out-of-date.
 *
 *	Make_HandleUse	    	See if a child is a .USE node for a parent
 *				and perform the .USE actions if so.
 *
 *	Make_ExpandUse	    	Expand .USE nodes
 */

#include    "make.h"
#include    "hash.h"
#include    "dir.h"
#include    "job.h"

static unsigned int checked = 1;/* Sequence # to detect recursion */
static Lst     	toBeMade;	/* The current fringe of the graph. These
				 * are nodes which await examination by
				 * MakeOODate. It is added to by
				 * Make_Update and subtracted from by
				 * MakeStartJobs */

static int MakeAddChild(void *, void *);
static int MakeFindChild(void *, void *);
static int MakeUnmark(void *, void *);
static int MakeAddAllSrc(void *, void *);
static int MakeTimeStamp(void *, void *);
static int MakeHandleUse(void *, void *);
static Boolean MakeStartJobs(void);
static int MakePrintStatus(void *, void *);
static int MakeCheckOrder(void *, void *);
static int MakeBuildChild(void *, void *);
static int MakeBuildParent(void *, void *);

MAKE_ATTR_DEAD static void
make_abort(GNode *gn, int line)
{
    static int two = 2;

    fprintf(debug_file, "make_abort from line %d\n", line);
    Targ_PrintNode(gn, &two);
    Lst_ForEach(toBeMade, Targ_PrintNode, &two);
    Targ_PrintGraph(3);
    abort();
}

/*-
 *-----------------------------------------------------------------------
 * Make_TimeStamp --
 *	Set the cmgn field of a parent node based on the mtime stamp in its
 *	child. Called from MakeOODate via Lst_ForEach.
 *
 * Input:
 *	pgn		the current parent
 *	cgn		the child we've just examined
 *
 * Results:
 *	Always returns 0.
 *
 * Side Effects:
 *	The cmgn of the parent node will be changed if the mtime
 *	field of the child is greater than it.
 *-----------------------------------------------------------------------
 */
int
Make_TimeStamp(GNode *pgn, GNode *cgn)
{
    if (pgn->cmgn == NULL || cgn->mtime > pgn->cmgn->mtime) {
	pgn->cmgn = cgn;
    }
    return (0);
}

/*
 * Input:
 *	pgn		the current parent
 *	cgn		the child we've just examined
 *
 */
static int
MakeTimeStamp(void *pgn, void *cgn)
{
    return Make_TimeStamp((GNode *)pgn, (GNode *)cgn);
}

/*-
 *-----------------------------------------------------------------------
 * Make_OODate --
 *	See if a given node is out of date with respect to its sources.
 *	Used by Make_Run when deciding which nodes to place on the
 *	toBeMade queue initially and by Make_Update to screen out USE and
 *	EXEC nodes. In the latter case, however, any other sort of node
 *	must be considered out-of-date since at least one of its children
 *	will have been recreated.
 *
 * Input:
 *	gn		the node to check
 *
 * Results:
 *	TRUE if the node is out of date. FALSE otherwise.
 *
 * Side Effects:
 *	The mtime field of the node and the cmgn field of its parents
 *	will/may be changed.
 *-----------------------------------------------------------------------
 */
Boolean
Make_OODate(GNode *gn)
{
    Boolean         oodate;

    /*
     * Certain types of targets needn't even be sought as their datedness
     * doesn't depend on their modification time...
     */
    if ((gn->type & (OP_JOIN|OP_USE|OP_USEBEFORE|OP_EXEC)) == 0) {
	(void)Dir_MTime(gn, 1);
	if (DEBUG(MAKE)) {
	    if (gn->mtime != 0) {
		fprintf(debug_file, "modified %s...", Targ_FmtTime(gn->mtime));
	    } else {
		fprintf(debug_file, "non-existent...");
	    }
	}
    }

    /*
     * A target is remade in one of the following circumstances:
     *	its modification time is smaller than that of its youngest child
     *	    and it would actually be run (has commands or type OP_NOP)
     *	it's the object of a force operator
     *	it has no children, was on the lhs of an operator and doesn't exist
     *	    already.
     *
     * Libraries are only considered out-of-date if the archive module says
     * they are.
     *
     * These weird rules are brought to you by Backward-Compatibility and
     * the strange people who wrote 'Make'.
     */
    if (gn->type & (OP_USE|OP_USEBEFORE)) {
	/*
	 * If the node is a USE node it is *never* out of date
	 * no matter *what*.
	 */
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, ".USE node...");
	}
	oodate = FALSE;
    } else if ((gn->type & OP_LIB) &&
	       ((gn->mtime==0) || Arch_IsLib(gn))) {
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, "library...");
	}

	/*
	 * always out of date if no children and :: target
	 * or non-existent.
	 */
	oodate = (gn->mtime == 0 || Arch_LibOODate(gn) || 
		  (gn->cmgn == NULL && (gn->type & OP_DOUBLEDEP)));
    } else if (gn->type & OP_JOIN) {
	/*
	 * A target with the .JOIN attribute is only considered
	 * out-of-date if any of its children was out-of-date.
	 */
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, ".JOIN node...");
	}
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, "source %smade...", gn->flags & CHILDMADE ? "" : "not ");
	}
	oodate = (gn->flags & CHILDMADE) ? TRUE : FALSE;
    } else if (gn->type & (OP_FORCE|OP_EXEC|OP_PHONY)) {
	/*
	 * A node which is the object of the force (!) operator or which has
	 * the .EXEC attribute is always considered out-of-date.
	 */
	if (DEBUG(MAKE)) {
	    if (gn->type & OP_FORCE) {
		fprintf(debug_file, "! operator...");
	    } else if (gn->type & OP_PHONY) {
		fprintf(debug_file, ".PHONY node...");
	    } else {
		fprintf(debug_file, ".EXEC node...");
	    }
	}
	oodate = TRUE;
    } else if ((gn->cmgn != NULL && gn->mtime < gn->cmgn->mtime) ||
	       (gn->cmgn == NULL &&
		((gn->mtime == 0 && !(gn->type & OP_OPTIONAL))
		  || gn->type & OP_DOUBLEDEP)))
    {
	/*
	 * A node whose modification time is less than that of its
	 * youngest child or that has no children (cmgn == NULL) and
	 * either doesn't exist (mtime == 0) and it isn't optional
	 * or was the object of a * :: operator is out-of-date.
	 * Why? Because that's the way Make does it.
	 */
	if (DEBUG(MAKE)) {
	    if (gn->cmgn != NULL && gn->mtime < gn->cmgn->mtime) {
		fprintf(debug_file, "modified before source %s...",
		    gn->cmgn->path ? gn->cmgn->path : gn->cmgn->name);
	    } else if (gn->mtime == 0) {
		fprintf(debug_file, "non-existent and no sources...");
	    } else {
		fprintf(debug_file, ":: operator and no sources...");
	    }
	}
	oodate = TRUE;
    } else {
	/* 
	 * When a non-existing child with no sources
	 * (such as a typically used FORCE source) has been made and
	 * the target of the child (usually a directory) has the same
	 * timestamp as the timestamp just given to the non-existing child
	 * after it was considered made.
	 */
	if (DEBUG(MAKE)) {
	    if (gn->flags & FORCE)
		fprintf(debug_file, "non existing child...");
	}
	oodate = (gn->flags & FORCE) ? TRUE : FALSE;
    }

#ifdef USE_META
    if (useMeta) {
	oodate = meta_oodate(gn, oodate);
    }
#endif

    /*
     * If the target isn't out-of-date, the parents need to know its
     * modification time. Note that targets that appear to be out-of-date
     * but aren't, because they have no commands and aren't of type OP_NOP,
     * have their mtime stay below their children's mtime to keep parents from
     * thinking they're out-of-date.
     */
    if (!oodate) {
	Lst_ForEach(gn->parents, MakeTimeStamp, gn);
    }

    return (oodate);
}

/*-
 *-----------------------------------------------------------------------
 * MakeAddChild  --
 *	Function used by Make_Run to add a child to the list l.
 *	It will only add the child if its make field is FALSE.
 *
 * Input:
 *	gnp		the node to add
 *	lp		the list to which to add it
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	The given list is extended
 *-----------------------------------------------------------------------
 */
static int
MakeAddChild(void *gnp, void *lp)
{
    GNode          *gn = (GNode *)gnp;
    Lst            l = (Lst) lp;

    if ((gn->flags & REMAKE) == 0 && !(gn->type & (OP_USE|OP_USEBEFORE))) {
	if (DEBUG(MAKE))
	    fprintf(debug_file, "MakeAddChild: need to examine %s%s\n",
		gn->name, gn->cohort_num);
	(void)Lst_EnQueue(l, gn);
    }
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * MakeFindChild  --
 *	Function used by Make_Run to find the pathname of a child
 *	that was already made.
 *
 * Input:
 *	gnp		the node to find
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	The path and mtime of the node and the cmgn of the parent are
 *	updated; the unmade children count of the parent is decremented.
 *-----------------------------------------------------------------------
 */
static int
MakeFindChild(void *gnp, void *pgnp)
{
    GNode          *gn = (GNode *)gnp;
    GNode          *pgn = (GNode *)pgnp;

    (void)Dir_MTime(gn, 0);
    Make_TimeStamp(pgn, gn);
    pgn->unmade--;

    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * Make_HandleUse --
 *	Function called by Make_Run and SuffApplyTransform on the downward
 *	pass to handle .USE and transformation nodes. It implements the
 *	.USE and transformation functionality by copying the node's commands,
 *	type flags and children to the parent node.
 *
 *	A .USE node is much like an explicit transformation rule, except
 *	its commands are always added to the target node, even if the
 *	target already has commands.
 *
 * Input:
 *	cgn		The .USE node
 *	pgn		The target of the .USE node
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	Children and commands may be added to the parent and the parent's
 *	type may be changed.
 *
 *-----------------------------------------------------------------------
 */
void
Make_HandleUse(GNode *cgn, GNode *pgn)
{
    LstNode	ln; 	/* An element in the children list */

#ifdef DEBUG_SRC
    if ((cgn->type & (OP_USE|OP_USEBEFORE|OP_TRANSFORM)) == 0) {
	fprintf(debug_file, "Make_HandleUse: called for plain node %s\n", cgn->name);
	return;
    }
#endif

    if ((cgn->type & (OP_USE|OP_USEBEFORE)) || Lst_IsEmpty(pgn->commands)) {
	    if (cgn->type & OP_USEBEFORE) {
		/*
		 * .USEBEFORE --
		 *	prepend the child's commands to the parent.
		 */
		Lst cmds = pgn->commands;
		pgn->commands = Lst_Duplicate(cgn->commands, NULL);
		(void)Lst_Concat(pgn->commands, cmds, LST_CONCNEW);
		Lst_Destroy(cmds, NULL);
	    } else {
		/*
		 * .USE or target has no commands --
		 *	append the child's commands to the parent.
		 */
		(void)Lst_Concat(pgn->commands, cgn->commands, LST_CONCNEW);
	    }
    }

    if (Lst_Open(cgn->children) == SUCCESS) {
	while ((ln = Lst_Next(cgn->children)) != NULL) {
	    GNode *tgn, *gn = (GNode *)Lst_Datum(ln);

	    /*
	     * Expand variables in the .USE node's name
	     * and save the unexpanded form.
	     * We don't need to do this for commands.
	     * They get expanded properly when we execute.
	     */
	    if (gn->uname == NULL) {
		gn->uname = gn->name;
	    } else {
		free(gn->name);
	    }
	    gn->name = Var_Subst(NULL, gn->uname, pgn, VARF_WANTRES);
	    if (gn->name && gn->uname && strcmp(gn->name, gn->uname) != 0) {
		/* See if we have a target for this node. */
		tgn = Targ_FindNode(gn->name, TARG_NOCREATE);
		if (tgn != NULL)
		    gn = tgn;
	    }

	    (void)Lst_AtEnd(pgn->children, gn);
	    (void)Lst_AtEnd(gn->parents, pgn);
	    pgn->unmade += 1;
	}
	Lst_Close(cgn->children);
    }

    pgn->type |= cgn->type & ~(OP_OPMASK|OP_USE|OP_USEBEFORE|OP_TRANSFORM);
}

/*-
 *-----------------------------------------------------------------------
 * MakeHandleUse --
 *	Callback function for Lst_ForEach, used by Make_Run on the downward
 *	pass to handle .USE nodes. Should be called before the children
 *	are enqueued to be looked at by MakeAddChild.
 *	This function calls Make_HandleUse to copy the .USE node's commands,
 *	type flags and children to the parent node.
 *
 * Input:
 *	cgnp		the child we've just examined
 *	pgnp		the current parent
 *
 * Results:
 *	returns 0.
 *
 * Side Effects:
 *	After expansion, .USE child nodes are removed from the parent
 *
 *-----------------------------------------------------------------------
 */
static int
MakeHandleUse(void *cgnp, void *pgnp)
{
    GNode	*cgn = (GNode *)cgnp;
    GNode	*pgn = (GNode *)pgnp;
    LstNode	ln; 	/* An element in the children list */
    int		unmarked;

    unmarked = ((cgn->type & OP_MARK) == 0);
    cgn->type |= OP_MARK;

    if ((cgn->type & (OP_USE|OP_USEBEFORE)) == 0)
	return (0);

    if (unmarked)
	Make_HandleUse(cgn, pgn);

    /*
     * This child node is now "made", so we decrement the count of
     * unmade children in the parent... We also remove the child
     * from the parent's list to accurately reflect the number of decent
     * children the parent has. This is used by Make_Run to decide
     * whether to queue the parent or examine its children...
     */
    if ((ln = Lst_Member(pgn->children, cgn)) != NULL) {
	Lst_Remove(pgn->children, ln);
	pgn->unmade--;
    }
    return (0);
}


/*-
 *-----------------------------------------------------------------------
 * Make_Recheck --
 *	Check the modification time of a gnode, and update it as described
 *	in the comments below.
 *
 * Results:
 *	returns 0 if the gnode does not exist, or its filesystem
 *	time if it does.
 *
 * Side Effects:
 *	the gnode's modification time and path name are affected.
 *
 *-----------------------------------------------------------------------
 */
time_t
Make_Recheck(GNode *gn)
{
    time_t mtime = Dir_MTime(gn, 1);

#ifndef RECHECK
    /*
     * We can't re-stat the thing, but we can at least take care of rules
     * where a target depends on a source that actually creates the
     * target, but only if it has changed, e.g.
     *
     * parse.h : parse.o
     *
     * parse.o : parse.y
     *  	yacc -d parse.y
     *  	cc -c y.tab.c
     *  	mv y.tab.o parse.o
     *  	cmp -s y.tab.h parse.h || mv y.tab.h parse.h
     *
     * In this case, if the definitions produced by yacc haven't changed
     * from before, parse.h won't have been updated and gn->mtime will
     * reflect the current modification time for parse.h. This is
     * something of a kludge, I admit, but it's a useful one..
     * XXX: People like to use a rule like
     *
     * FRC:
     *
     * To force things that depend on FRC to be made, so we have to
     * check for gn->children being empty as well...
     */
    if (!Lst_IsEmpty(gn->commands) || Lst_IsEmpty(gn->children)) {
	gn->mtime = now;
    }
#else
    /*
     * This is what Make does and it's actually a good thing, as it
     * allows rules like
     *
     *	cmp -s y.tab.h parse.h || cp y.tab.h parse.h
     *
     * to function as intended. Unfortunately, thanks to the stateless
     * nature of NFS (by which I mean the loose coupling of two clients
     * using the same file from a common server), there are times
     * when the modification time of a file created on a remote
     * machine will not be modified before the local stat() implied by
     * the Dir_MTime occurs, thus leading us to believe that the file
     * is unchanged, wreaking havoc with files that depend on this one.
     *
     * I have decided it is better to make too much than to make too
     * little, so this stuff is commented out unless you're sure it's ok.
     * -- ardeb 1/12/88
     */
    /*
     * Christos, 4/9/92: If we are  saving commands pretend that
     * the target is made now. Otherwise archives with ... rules
     * don't work!
     */
    if (NoExecute(gn) || (gn->type & OP_SAVE_CMDS) ||
	    (mtime == 0 && !(gn->type & OP_WAIT))) {
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, " recheck(%s): update time from %s to now\n",
		   gn->name, Targ_FmtTime(gn->mtime));
	}
	gn->mtime = now;
    }
    else {
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, " recheck(%s): current update time: %s\n",
		   gn->name, Targ_FmtTime(gn->mtime));
	}
    }
#endif
    return mtime;
}

/*-
 *-----------------------------------------------------------------------
 * Make_Update  --
 *	Perform update on the parents of a node. Used by JobFinish once
 *	a node has been dealt with and by MakeStartJobs if it finds an
 *	up-to-date node.
 *
 * Input:
 *	cgn		the child node
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	The unmade field of pgn is decremented and pgn may be placed on
 *	the toBeMade queue if this field becomes 0.
 *
 * 	If the child was made, the parent's flag CHILDMADE field will be
 *	set true.
 *
 *	If the child is not up-to-date and still does not exist,
 *	set the FORCE flag on the parents.
 *
 *	If the child wasn't made, the cmgn field of the parent will be
 *	altered if the child's mtime is big enough.
 *
 *	Finally, if the child is the implied source for the parent, the
 *	parent's IMPSRC variable is set appropriately.
 *
 *-----------------------------------------------------------------------
 */
void
Make_Update(GNode *cgn)
{
    GNode 	*pgn;	/* the parent node */
    char  	*cname;	/* the child's name */
    LstNode	ln; 	/* Element in parents and iParents lists */
    time_t	mtime = -1;
    char	*p1;
    Lst		parents;
    GNode	*centurion;

    /* It is save to re-examine any nodes again */
    checked++;

    cname = Var_Value(TARGET, cgn, &p1);
    free(p1);

    if (DEBUG(MAKE))
	fprintf(debug_file, "Make_Update: %s%s\n", cgn->name, cgn->cohort_num);

    /*
     * If the child was actually made, see what its modification time is
     * now -- some rules won't actually update the file. If the file still
     * doesn't exist, make its mtime now.
     */
    if (cgn->made != UPTODATE) {
	mtime = Make_Recheck(cgn);
    }

    /*
     * If this is a `::' node, we must consult its first instance
     * which is where all parents are linked.
     */
    if ((centurion = cgn->centurion) != NULL) {
	if (!Lst_IsEmpty(cgn->parents))
		Punt("%s%s: cohort has parents", cgn->name, cgn->cohort_num);
	centurion->unmade_cohorts -= 1;
	if (centurion->unmade_cohorts < 0)
	    Error("Graph cycles through centurion %s", centurion->name);
    } else {
	centurion = cgn;
    }
    parents = centurion->parents;

    /* If this was a .ORDER node, schedule the RHS */
    Lst_ForEach(centurion->order_succ, MakeBuildParent, Lst_First(toBeMade));

    /* Now mark all the parents as having one less unmade child */
    if (Lst_Open(parents) == SUCCESS) {
	while ((ln = Lst_Next(parents)) != NULL) {
	    pgn = (GNode *)Lst_Datum(ln);
	    if (DEBUG(MAKE))
		fprintf(debug_file, "inspect parent %s%s: flags %x, "
			    "type %x, made %d, unmade %d ",
			pgn->name, pgn->cohort_num, pgn->flags,
			pgn->type, pgn->made, pgn->unmade-1);

	    if (!(pgn->flags & REMAKE)) {
		/* This parent isn't needed */
		if (DEBUG(MAKE))
		    fprintf(debug_file, "- not needed\n");
		continue;
	    }
	    if (mtime == 0 && !(cgn->type & OP_WAIT))
		pgn->flags |= FORCE;

	    /*
	     * If the parent has the .MADE attribute, its timestamp got
	     * updated to that of its newest child, and its unmake
	     * child count got set to zero in Make_ExpandUse().
	     * However other things might cause us to build one of its
	     * children - and so we mustn't do any processing here when
	     * the child build finishes.
	     */
	    if (pgn->type & OP_MADE) {
		if (DEBUG(MAKE))
		    fprintf(debug_file, "- .MADE\n");
		continue;
	    }

	    if ( ! (cgn->type & (OP_EXEC|OP_USE|OP_USEBEFORE))) {
		if (cgn->made == MADE)
		    pgn->flags |= CHILDMADE;
		(void)Make_TimeStamp(pgn, cgn);
	    }

	    /*
	     * A parent must wait for the completion of all instances
	     * of a `::' dependency.
	     */
	    if (centurion->unmade_cohorts != 0 || centurion->made < MADE) {
		if (DEBUG(MAKE))
		    fprintf(debug_file,
			    "- centurion made %d, %d unmade cohorts\n",
			    centurion->made, centurion->unmade_cohorts);
		continue;
	    }

	    /* One more child of this parent is now made */
	    pgn->unmade -= 1;
	    if (pgn->unmade < 0) {
		if (DEBUG(MAKE)) {
		    fprintf(debug_file, "Graph cycles through %s%s\n",
			pgn->name, pgn->cohort_num);
		    Targ_PrintGraph(2);
		}
		Error("Graph cycles through %s%s", pgn->name, pgn->cohort_num);
	    }

	    /* We must always rescan the parents of .WAIT and .ORDER nodes. */
	    if (pgn->unmade != 0 && !(centurion->type & OP_WAIT)
		    && !(centurion->flags & DONE_ORDER)) {
		if (DEBUG(MAKE))
		    fprintf(debug_file, "- unmade children\n");
		continue;
	    }
	    if (pgn->made != DEFERRED) {
		/*
		 * Either this parent is on a different branch of the tree,
		 * or it on the RHS of a .WAIT directive
		 * or it is already on the toBeMade list.
		 */
		if (DEBUG(MAKE))
		    fprintf(debug_file, "- not deferred\n");
		continue;
	    }
	    if (pgn->order_pred
		    && Lst_ForEach(pgn->order_pred, MakeCheckOrder, 0)) {
		/* A .ORDER rule stops us building this */
		continue;
	    }
	    if (DEBUG(MAKE)) {
		static int two = 2;
		fprintf(debug_file, "- %s%s made, schedule %s%s (made %d)\n",
			cgn->name, cgn->cohort_num,
			pgn->name, pgn->cohort_num, pgn->made);
		Targ_PrintNode(pgn, &two);
	    }
	    /* Ok, we can schedule the parent again */
	    pgn->made = REQUESTED;
	    (void)Lst_EnQueue(toBeMade, pgn);
	}
	Lst_Close(parents);
    }

    /*
     * Set the .PREFIX and .IMPSRC variables for all the implied parents
     * of this node.
     */
    if (Lst_Open(cgn->iParents) == SUCCESS) {
	char	*cpref = Var_Value(PREFIX, cgn, &p1);

	while ((ln = Lst_Next(cgn->iParents)) != NULL) {
	    pgn = (GNode *)Lst_Datum(ln);
	    if (pgn->flags & REMAKE) {
		Var_Set(IMPSRC, cname, pgn, 0);
		if (cpref != NULL)
		    Var_Set(PREFIX, cpref, pgn, 0);
	    }
	}
	free(p1);
	Lst_Close(cgn->iParents);
    }
}

/*-
 *-----------------------------------------------------------------------
 * MakeAddAllSrc --
 *	Add a child's name to the ALLSRC and OODATE variables of the given
 *	node. Called from Make_DoAllVar via Lst_ForEach. A child is added only
 *	if it has not been given the .EXEC, .USE or .INVISIBLE attributes.
 *	.EXEC and .USE children are very rarely going to be files, so...
 *	If the child is a .JOIN node, its ALLSRC is propagated to the parent.
 *
 *	A child is added to the OODATE variable if its modification time is
 *	later than that of its parent, as defined by Make, except if the
 *	parent is a .JOIN node. In that case, it is only added to the OODATE
 *	variable if it was actually made (since .JOIN nodes don't have
 *	modification times, the comparison is rather unfair...)..
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	The ALLSRC variable for the given node is extended.
 *-----------------------------------------------------------------------
 */
static int
MakeUnmark(void *cgnp, void *pgnp MAKE_ATTR_UNUSED)
{
    GNode	*cgn = (GNode *)cgnp;

    cgn->type &= ~OP_MARK;
    return (0);
}

/*
 * Input:
 *	cgnp		The child to add
 *	pgnp		The parent to whose ALLSRC variable it should
 *			be added
 *
 */
static int
MakeAddAllSrc(void *cgnp, void *pgnp)
{
    GNode	*cgn = (GNode *)cgnp;
    GNode	*pgn = (GNode *)pgnp;

    if (cgn->type & OP_MARK)
	return (0);
    cgn->type |= OP_MARK;

    if ((cgn->type & (OP_EXEC|OP_USE|OP_USEBEFORE|OP_INVISIBLE)) == 0) {
	char *child, *allsrc;
	char *p1 = NULL, *p2 = NULL;

	if (cgn->type & OP_ARCHV)
	    child = Var_Value(MEMBER, cgn, &p1);
	else
	    child = cgn->path ? cgn->path : cgn->name;
	if (cgn->type & OP_JOIN) {
	    allsrc = Var_Value(ALLSRC, cgn, &p2);
	} else {
	    allsrc = child;
	}
	if (allsrc != NULL)
		Var_Append(ALLSRC, allsrc, pgn);
	free(p2);
	if (pgn->type & OP_JOIN) {
	    if (cgn->made == MADE) {
		Var_Append(OODATE, child, pgn);
	    }
	} else if ((pgn->mtime < cgn->mtime) ||
		   (cgn->mtime >= now && cgn->made == MADE))
	{
	    /*
	     * It goes in the OODATE variable if the parent is younger than the
	     * child or if the child has been modified more recently than
	     * the start of the make. This is to keep pmake from getting
	     * confused if something else updates the parent after the
	     * make starts (shouldn't happen, I know, but sometimes it
	     * does). In such a case, if we've updated the kid, the parent
	     * is likely to have a modification time later than that of
	     * the kid and anything that relies on the OODATE variable will
	     * be hosed.
	     *
	     * XXX: This will cause all made children to go in the OODATE
	     * variable, even if they're not touched, if RECHECK isn't defined,
	     * since cgn->mtime is set to now in Make_Update. According to
	     * some people, this is good...
	     */
	    Var_Append(OODATE, child, pgn);
	}
	free(p1);
    }
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * Make_DoAllVar --
 *	Set up the ALLSRC and OODATE variables. Sad to say, it must be
 *	done separately, rather than while traversing the graph. This is
 *	because Make defined OODATE to contain all sources whose modification
 *	times were later than that of the target, *not* those sources that
 *	were out-of-date. Since in both compatibility and native modes,
 *	the modification time of the parent isn't found until the child
 *	has been dealt with, we have to wait until now to fill in the
 *	variable. As for ALLSRC, the ordering is important and not
 *	guaranteed when in native mode, so it must be set here, too.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The ALLSRC and OODATE variables of the given node is filled in.
 *	If the node is a .JOIN node, its TARGET variable will be set to
 * 	match its ALLSRC variable.
 *-----------------------------------------------------------------------
 */
void
Make_DoAllVar(GNode *gn)
{
    if (gn->flags & DONE_ALLSRC)
	return;
    
    Lst_ForEach(gn->children, MakeUnmark, gn);
    Lst_ForEach(gn->children, MakeAddAllSrc, gn);

    if (!Var_Exists (OODATE, gn)) {
	Var_Set(OODATE, "", gn, 0);
    }
    if (!Var_Exists (ALLSRC, gn)) {
	Var_Set(ALLSRC, "", gn, 0);
    }

    if (gn->type & OP_JOIN) {
	char *p1;
	Var_Set(TARGET, Var_Value(ALLSRC, gn, &p1), gn, 0);
	free(p1);
    }
    gn->flags |= DONE_ALLSRC;
}

/*-
 *-----------------------------------------------------------------------
 * MakeStartJobs --
 *	Start as many jobs as possible.
 *
 * Results:
 *	If the query flag was given to pmake, no job will be started,
 *	but as soon as an out-of-date target is found, this function
 *	returns TRUE. At all other times, this function returns FALSE.
 *
 * Side Effects:
 *	Nodes are removed from the toBeMade queue and job table slots
 *	are filled.
 *
 *-----------------------------------------------------------------------
 */

static int
MakeCheckOrder(void *v_bn, void *ignore MAKE_ATTR_UNUSED)
{
    GNode *bn = v_bn;

    if (bn->made >= MADE || !(bn->flags & REMAKE))
	return 0;
    if (DEBUG(MAKE))
	fprintf(debug_file, "MakeCheckOrder: Waiting for .ORDER node %s%s\n",
		bn->name, bn->cohort_num);
    return 1;
}

static int
MakeBuildChild(void *v_cn, void *toBeMade_next)
{
    GNode *cn = v_cn;

    if (DEBUG(MAKE))
	fprintf(debug_file, "MakeBuildChild: inspect %s%s, made %d, type %x\n",
	    cn->name, cn->cohort_num, cn->made, cn->type);
    if (cn->made > DEFERRED)
	return 0;

    /* If this node is on the RHS of a .ORDER, check LHSs. */
    if (cn->order_pred && Lst_ForEach(cn->order_pred, MakeCheckOrder, 0)) {
	/* Can't build this (or anything else in this child list) yet */
	cn->made = DEFERRED;
	return 0;			/* but keep looking */
    }

    if (DEBUG(MAKE))
	fprintf(debug_file, "MakeBuildChild: schedule %s%s\n",
		cn->name, cn->cohort_num);

    cn->made = REQUESTED;
    if (toBeMade_next == NULL)
	Lst_AtEnd(toBeMade, cn);
    else
	Lst_InsertBefore(toBeMade, toBeMade_next, cn);

    if (cn->unmade_cohorts != 0)
	Lst_ForEach(cn->cohorts, MakeBuildChild, toBeMade_next);

    /*
     * If this node is a .WAIT node with unmade chlidren
     * then don't add the next sibling.
     */
    return cn->type & OP_WAIT && cn->unmade > 0;
}

/* When a .ORDER LHS node completes we do this on each RHS */
static int
MakeBuildParent(void *v_pn, void *toBeMade_next)
{
    GNode *pn = v_pn;

    if (pn->made != DEFERRED)
	return 0;

    if (MakeBuildChild(pn, toBeMade_next) == 0) {
	/* Mark so that when this node is built we reschedule its parents */
	pn->flags |= DONE_ORDER;
    }

    return 0;
}

static Boolean
MakeStartJobs(void)
{
    GNode	*gn;
    int		have_token = 0;

    while (!Lst_IsEmpty (toBeMade)) {
	/* Get token now to avoid cycling job-list when we only have 1 token */
	if (!have_token && !Job_TokenWithdraw())
	    break;
	have_token = 1;

	gn = (GNode *)Lst_DeQueue(toBeMade);
	if (DEBUG(MAKE))
	    fprintf(debug_file, "Examining %s%s...\n",
		    gn->name, gn->cohort_num);

	if (gn->made != REQUESTED) {
	    if (DEBUG(MAKE))
		fprintf(debug_file, "state %d\n", gn->made);

	    make_abort(gn, __LINE__);
	}

	if (gn->checked == checked) {
	    /* We've already looked at this node since a job finished... */
	    if (DEBUG(MAKE))
		fprintf(debug_file, "already checked %s%s\n",
			gn->name, gn->cohort_num);
	    gn->made = DEFERRED;
	    continue;
	}
	gn->checked = checked;

	if (gn->unmade != 0) {
	    /*
	     * We can't build this yet, add all unmade children to toBeMade,
	     * just before the current first element.
	     */
	    gn->made = DEFERRED;
	    Lst_ForEach(gn->children, MakeBuildChild, Lst_First(toBeMade));
	    /* and drop this node on the floor */
	    if (DEBUG(MAKE))
		fprintf(debug_file, "dropped %s%s\n", gn->name, gn->cohort_num);
	    continue;
	}

	gn->made = BEINGMADE;
	if (Make_OODate(gn)) {
	    if (DEBUG(MAKE)) {
		fprintf(debug_file, "out-of-date\n");
	    }
	    if (queryFlag) {
		return (TRUE);
	    }
	    Make_DoAllVar(gn);
	    Job_Make(gn);
	    have_token = 0;
	} else {
	    if (DEBUG(MAKE)) {
		fprintf(debug_file, "up-to-date\n");
	    }
	    gn->made = UPTODATE;
	    if (gn->type & OP_JOIN) {
		/*
		 * Even for an up-to-date .JOIN node, we need it to have its
		 * context variables so references to it get the correct
		 * value for .TARGET when building up the context variables
		 * of its parent(s)...
		 */
		Make_DoAllVar(gn);
	    }
	    Make_Update(gn);
	}
    }

    if (have_token)
	Job_TokenReturn();

    return (FALSE);
}

/*-
 *-----------------------------------------------------------------------
 * MakePrintStatus --
 *	Print the status of a top-level node, viz. it being up-to-date
 *	already or not created due to an error in a lower level.
 *	Callback function for Make_Run via Lst_ForEach.
 *
 * Input:
 *	gnp		Node to examine
 *	cyclep		True if gn->unmade being non-zero implies a
 *			cycle in the graph, not an error in an
 *			inferior.
 *
 * Results:
 *	Always returns 0.
 *
 * Side Effects:
 *	A message may be printed.
 *
 *-----------------------------------------------------------------------
 */
static int
MakePrintStatusOrder(void *ognp, void *gnp)
{
    GNode *ogn = ognp;
    GNode *gn = gnp;

    if (!(ogn->flags & REMAKE) || ogn->made > REQUESTED)
	/* not waiting for this one */
	return 0;

    printf("    `%s%s' has .ORDER dependency against %s%s "
		"(made %d, flags %x, type %x)\n",
	    gn->name, gn->cohort_num,
	    ogn->name, ogn->cohort_num, ogn->made, ogn->flags, ogn->type);
    if (DEBUG(MAKE) && debug_file != stdout)
	fprintf(debug_file, "    `%s%s' has .ORDER dependency against %s%s "
		    "(made %d, flags %x, type %x)\n",
		gn->name, gn->cohort_num,
		ogn->name, ogn->cohort_num, ogn->made, ogn->flags, ogn->type);
    return 0;
}

static int
MakePrintStatus(void *gnp, void *v_errors)
{
    GNode   	*gn = (GNode *)gnp;
    int 	*errors = v_errors;

    if (gn->flags & DONECYCLE)
	/* We've completely processed this node before, don't do it again. */
	return 0;

    if (gn->unmade == 0) {
	gn->flags |= DONECYCLE;
	switch (gn->made) {
	case UPTODATE:
	    printf("`%s%s' is up to date.\n", gn->name, gn->cohort_num);
	    break;
	case MADE:
	    break;
	case UNMADE:
	case DEFERRED:
	case REQUESTED:
	case BEINGMADE:
	    (*errors)++;
	    printf("`%s%s' was not built (made %d, flags %x, type %x)!\n",
		    gn->name, gn->cohort_num, gn->made, gn->flags, gn->type);
	    if (DEBUG(MAKE) && debug_file != stdout)
		fprintf(debug_file,
			"`%s%s' was not built (made %d, flags %x, type %x)!\n",
			gn->name, gn->cohort_num, gn->made, gn->flags, gn->type);
	    /* Most likely problem is actually caused by .ORDER */
	    Lst_ForEach(gn->order_pred, MakePrintStatusOrder, gn);
	    break;
	default:
	    /* Errors - already counted */
	    printf("`%s%s' not remade because of errors.\n",
		    gn->name, gn->cohort_num);
	    if (DEBUG(MAKE) && debug_file != stdout)
		fprintf(debug_file, "`%s%s' not remade because of errors.\n",
			gn->name, gn->cohort_num);
	    break;
	}
	return 0;
    }

    if (DEBUG(MAKE))
	fprintf(debug_file, "MakePrintStatus: %s%s has %d unmade children\n",
		gn->name, gn->cohort_num, gn->unmade);
    /*
     * If printing cycles and came to one that has unmade children,
     * print out the cycle by recursing on its children.
     */
    if (!(gn->flags & CYCLE)) {
	/* Fist time we've seen this node, check all children */
	gn->flags |= CYCLE;
	Lst_ForEach(gn->children, MakePrintStatus, errors);
	/* Mark that this node needn't be processed again */
	gn->flags |= DONECYCLE;
	return 0;
    }

    /* Only output the error once per node */
    gn->flags |= DONECYCLE;
    Error("Graph cycles through `%s%s'", gn->name, gn->cohort_num);
    if ((*errors)++ > 100)
	/* Abandon the whole error report */
	return 1;

    /* Reporting for our children will give the rest of the loop */
    Lst_ForEach(gn->children, MakePrintStatus, errors);
    return 0;
}


/*-
 *-----------------------------------------------------------------------
 * Make_ExpandUse --
 *	Expand .USE nodes and create a new targets list
 *
 * Input:
 *	targs		the initial list of targets
 *
 * Side Effects:
 *-----------------------------------------------------------------------
 */
void
Make_ExpandUse(Lst targs)
{
    GNode  *gn;		/* a temporary pointer */
    Lst    examine; 	/* List of targets to examine */

    examine = Lst_Duplicate(targs, NULL);

    /*
     * Make an initial downward pass over the graph, marking nodes to be made
     * as we go down. We call Suff_FindDeps to find where a node is and
     * to get some children for it if it has none and also has no commands.
     * If the node is a leaf, we stick it on the toBeMade queue to
     * be looked at in a minute, otherwise we add its children to our queue
     * and go on about our business.
     */
    while (!Lst_IsEmpty (examine)) {
	gn = (GNode *)Lst_DeQueue(examine);
    
	if (gn->flags & REMAKE)
	    /* We've looked at this one already */
	    continue;
	gn->flags |= REMAKE;
	if (DEBUG(MAKE))
	    fprintf(debug_file, "Make_ExpandUse: examine %s%s\n",
		    gn->name, gn->cohort_num);

	if ((gn->type & OP_DOUBLEDEP) && !Lst_IsEmpty (gn->cohorts)) {
	    /* Append all the 'cohorts' to the list of things to examine */
	    Lst new;
	    new = Lst_Duplicate(gn->cohorts, NULL);
	    Lst_Concat(new, examine, LST_CONCLINK);
	    examine = new;
	}

	/*
	 * Apply any .USE rules before looking for implicit dependencies
	 * to make sure everything has commands that should...
	 * Make sure that the TARGET is set, so that we can make
	 * expansions.
	 */
	if (gn->type & OP_ARCHV) {
	    char *eoa, *eon;
	    eoa = strchr(gn->name, '(');
	    eon = strchr(gn->name, ')');
	    if (eoa == NULL || eon == NULL)
		continue;
	    *eoa = '\0';
	    *eon = '\0';
	    Var_Set(MEMBER, eoa + 1, gn, 0);
	    Var_Set(ARCHIVE, gn->name, gn, 0);
	    *eoa = '(';
	    *eon = ')';
	}

	(void)Dir_MTime(gn, 0);
	Var_Set(TARGET, gn->path ? gn->path : gn->name, gn, 0);
	Lst_ForEach(gn->children, MakeUnmark, gn);
	Lst_ForEach(gn->children, MakeHandleUse, gn);

	if ((gn->type & OP_MADE) == 0)
	    Suff_FindDeps(gn);
	else {
	    /* Pretend we made all this node's children */
	    Lst_ForEach(gn->children, MakeFindChild, gn);
	    if (gn->unmade != 0)
		    printf("Warning: %s%s still has %d unmade children\n",
			    gn->name, gn->cohort_num, gn->unmade);
	}

	if (gn->unmade != 0)
	    Lst_ForEach(gn->children, MakeAddChild, examine);
    }

    Lst_Destroy(examine, NULL);
}

/*-
 *-----------------------------------------------------------------------
 * Make_ProcessWait --
 *	Convert .WAIT nodes into dependencies
 *
 * Input:
 *	targs		the initial list of targets
 *
 *-----------------------------------------------------------------------
 */

static int
link_parent(void *cnp, void *pnp)
{
    GNode *cn = cnp;
    GNode *pn = pnp;

    Lst_AtEnd(pn->children, cn);
    Lst_AtEnd(cn->parents, pn);
    pn->unmade++;
    return 0;
}

static int
add_wait_dep(void *v_cn, void *v_wn)
{
    GNode *cn = v_cn;
    GNode *wn = v_wn;

    if (cn == wn)
	return 1;

    if (cn == NULL || wn == NULL) {
	printf("bad wait dep %p %p\n", cn, wn);
	exit(4);
    }
    if (DEBUG(MAKE))
	 fprintf(debug_file, ".WAIT: add dependency %s%s -> %s\n",
		cn->name, cn->cohort_num, wn->name);

    Lst_AtEnd(wn->children, cn);
    wn->unmade++;
    Lst_AtEnd(cn->parents, wn);
    return 0;
}

static void
Make_ProcessWait(Lst targs)
{
    GNode  *pgn;	/* 'parent' node we are examining */
    GNode  *cgn;	/* Each child in turn */
    LstNode owln;	/* Previous .WAIT node */
    Lst    examine; 	/* List of targets to examine */
    LstNode ln;

    /*
     * We need all the nodes to have a common parent in order for the
     * .WAIT and .ORDER scheduling to work.
     * Perhaps this should be done earlier...
     */

    pgn = Targ_NewGN(".MAIN");
    pgn->flags = REMAKE;
    pgn->type = OP_PHONY | OP_DEPENDS;
    /* Get it displayed in the diag dumps */
    Lst_AtFront(Targ_List(), pgn);

    Lst_ForEach(targs, link_parent, pgn);

    /* Start building with the 'dummy' .MAIN' node */
    MakeBuildChild(pgn, NULL);

    examine = Lst_Init(FALSE);
    Lst_AtEnd(examine, pgn);

    while (!Lst_IsEmpty (examine)) {
	pgn = Lst_DeQueue(examine);
   
	/* We only want to process each child-list once */
	if (pgn->flags & DONE_WAIT)
	    continue;
	pgn->flags |= DONE_WAIT;
	if (DEBUG(MAKE))
	    fprintf(debug_file, "Make_ProcessWait: examine %s\n", pgn->name);

	if ((pgn->type & OP_DOUBLEDEP) && !Lst_IsEmpty (pgn->cohorts)) {
	    /* Append all the 'cohorts' to the list of things to examine */
	    Lst new;
	    new = Lst_Duplicate(pgn->cohorts, NULL);
	    Lst_Concat(new, examine, LST_CONCLINK);
	    examine = new;
	}

	owln = Lst_First(pgn->children);
	Lst_Open(pgn->children);
	for (; (ln = Lst_Next(pgn->children)) != NULL; ) {
	    cgn = Lst_Datum(ln);
	    if (cgn->type & OP_WAIT) {
		/* Make the .WAIT node depend on the previous children */
		Lst_ForEachFrom(pgn->children, owln, add_wait_dep, cgn);
		owln = ln;
	    } else {
		Lst_AtEnd(examine, cgn);
	    }
	}
	Lst_Close(pgn->children);
    }

    Lst_Destroy(examine, NULL);
}

/*-
 *-----------------------------------------------------------------------
 * Make_Run --
 *	Initialize the nodes to remake and the list of nodes which are
 *	ready to be made by doing a breadth-first traversal of the graph
 *	starting from the nodes in the given list. Once this traversal
 *	is finished, all the 'leaves' of the graph are in the toBeMade
 *	queue.
 *	Using this queue and the Job module, work back up the graph,
 *	calling on MakeStartJobs to keep the job table as full as
 *	possible.
 *
 * Input:
 *	targs		the initial list of targets
 *
 * Results:
 *	TRUE if work was done. FALSE otherwise.
 *
 * Side Effects:
 *	The make field of all nodes involved in the creation of the given
 *	targets is set to 1. The toBeMade list is set to contain all the
 *	'leaves' of these subgraphs.
 *-----------------------------------------------------------------------
 */
Boolean
Make_Run(Lst targs)
{
    int	    	    errors; 	/* Number of errors the Job module reports */

    /* Start trying to make the current targets... */
    toBeMade = Lst_Init(FALSE);

    Make_ExpandUse(targs);
    Make_ProcessWait(targs);

    if (DEBUG(MAKE)) {
	 fprintf(debug_file, "#***# full graph\n");
	 Targ_PrintGraph(1);
    }

    if (queryFlag) {
	/*
	 * We wouldn't do any work unless we could start some jobs in the
	 * next loop... (we won't actually start any, of course, this is just
	 * to see if any of the targets was out of date)
	 */
	return (MakeStartJobs());
    }
    /*
     * Initialization. At the moment, no jobs are running and until some
     * get started, nothing will happen since the remaining upward
     * traversal of the graph is performed by the routines in job.c upon
     * the finishing of a job. So we fill the Job table as much as we can
     * before going into our loop.
     */
    (void)MakeStartJobs();

    /*
     * Main Loop: The idea here is that the ending of jobs will take
     * care of the maintenance of data structures and the waiting for output
     * will cause us to be idle most of the time while our children run as
     * much as possible. Because the job table is kept as full as possible,
     * the only time when it will be empty is when all the jobs which need
     * running have been run, so that is the end condition of this loop.
     * Note that the Job module will exit if there were any errors unless the
     * keepgoing flag was given.
     */
    while (!Lst_IsEmpty(toBeMade) || jobTokensRunning > 0) {
	Job_CatchOutput();
	(void)MakeStartJobs();
    }

    errors = Job_Finish();

    /*
     * Print the final status of each target. E.g. if it wasn't made
     * because some inferior reported an error.
     */
    if (DEBUG(MAKE))
	 fprintf(debug_file, "done: errors %d\n", errors);
    if (errors == 0) {
	Lst_ForEach(targs, MakePrintStatus, &errors);
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, "done: errors %d\n", errors);
	    if (errors)
		Targ_PrintGraph(4);
	}
    }
    return errors != 0;
}
