/*	$NetBSD: compat.c,v 1.107 2017/07/20 19:29:54 sjg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990 The Regents of the University of California.
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
 * Copyright (c) 1988, 1989 by Adam de Boor
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
static char rcsid[] = "$NetBSD: compat.c,v 1.107 2017/07/20 19:29:54 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)compat.c	8.2 (Berkeley) 3/19/94";
#else
__RCSID("$NetBSD: compat.c,v 1.107 2017/07/20 19:29:54 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * compat.c --
 *	The routines in this file implement the full-compatibility
 *	mode of PMake. Most of the special functionality of PMake
 *	is available in this mode. Things not supported:
 *	    - different shells.
 *	    - friendly variable substitution.
 *
 * Interface:
 *	Compat_Run	    Initialize things for this module and recreate
 *	    	  	    thems as need creatin'
 */

#ifdef HAVE_CONFIG_H
# include   "config.h"
#endif
#include    <sys/types.h>
#include    <sys/stat.h>
#include    "wait.h"

#include    <ctype.h>
#include    <errno.h>
#include    <signal.h>
#include    <stdio.h>

#include    "make.h"
#include    "hash.h"
#include    "dir.h"
#include    "job.h"
#include    "metachar.h"
#include    "pathnames.h"


static GNode	    *curTarg = NULL;
static GNode	    *ENDNode;
static void CompatInterrupt(int);
static pid_t compatChild;
static int compatSigno;

/*
 * CompatDeleteTarget -- delete a failed, interrupted, or otherwise
 * duffed target if not inhibited by .PRECIOUS.
 */
static void
CompatDeleteTarget(GNode *gn)
{
    if ((gn != NULL) && !Targ_Precious (gn)) {
	char	  *p1;
	char 	  *file = Var_Value(TARGET, gn, &p1);

	if (!noExecute && eunlink(file) != -1) {
	    Error("*** %s removed", file);
	}

	free(p1);
    }
}

/*-
 *-----------------------------------------------------------------------
 * CompatInterrupt --
 *	Interrupt the creation of the current target and remove it if
 *	it ain't precious.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The target is removed and the process exits. If .INTERRUPT exists,
 *	its commands are run first WITH INTERRUPTS IGNORED..
 *
 * XXX: is .PRECIOUS supposed to inhibit .INTERRUPT? I doubt it, but I've
 * left the logic alone for now. - dholland 20160826
 *
 *-----------------------------------------------------------------------
 */
static void
CompatInterrupt(int signo)
{
    GNode   *gn;

    CompatDeleteTarget(curTarg);

    if ((curTarg != NULL) && !Targ_Precious (curTarg)) {
	/*
	 * Run .INTERRUPT only if hit with interrupt signal
	 */
	if (signo == SIGINT) {
	    gn = Targ_FindNode(".INTERRUPT", TARG_NOCREATE);
	    if (gn != NULL) {
		Compat_Make(gn, gn);
	    }
	}
    }
    if (signo == SIGQUIT)
	_exit(signo);
    /*
     * If there is a child running, pass the signal on
     * we will exist after it has exited.
     */
    compatSigno = signo;
    if (compatChild > 0) {
	KILLPG(compatChild, signo);
    } else {
	bmake_signal(signo, SIG_DFL);
	kill(myPid, signo);
    }
}

/*-
 *-----------------------------------------------------------------------
 * CompatRunCommand --
 *	Execute the next command for a target. If the command returns an
 *	error, the node's made field is set to ERROR and creation stops.
 *
 * Input:
 *	cmdp		Command to execute
 *	gnp		Node from which the command came
 *
 * Results:
 *	0 if the command succeeded, 1 if an error occurred.
 *
 * Side Effects:
 *	The node's 'made' field may be set to ERROR.
 *
 *-----------------------------------------------------------------------
 */
int
CompatRunCommand(void *cmdp, void *gnp)
{
    char    	  *cmdStart;	/* Start of expanded command */
    char 	  *cp, *bp;
    Boolean 	  silent,   	/* Don't print command */
	    	  doIt;		/* Execute even if -n */
    volatile Boolean errCheck; 	/* Check errors */
    WAIT_T 	  reason;   	/* Reason for child's death */
    int	    	  status;   	/* Description of child's death */
    pid_t	  cpid;	    	/* Child actually found */
    pid_t	  retstat;    	/* Result of wait */
    LstNode 	  cmdNode;  	/* Node where current command is located */
    const char  ** volatile av;	/* Argument vector for thing to exec */
    char	** volatile mav;/* Copy of the argument vector for freeing */
    int	    	  argc;	    	/* Number of arguments in av or 0 if not
				 * dynamically allocated */
    Boolean 	  local;    	/* TRUE if command should be executed
				 * locally */
    Boolean 	  useShell;    	/* TRUE if command should be executed
				 * using a shell */
    char	  * volatile cmd = (char *)cmdp;
    GNode	  *gn = (GNode *)gnp;

    silent = gn->type & OP_SILENT;
    errCheck = !(gn->type & OP_IGNORE);
    doIt = FALSE;
    
    cmdNode = Lst_Member(gn->commands, cmd);
    cmdStart = Var_Subst(NULL, cmd, gn, VARF_WANTRES);

    /*
     * brk_string will return an argv with a NULL in av[0], thus causing
     * execvp to choke and die horribly. Besides, how can we execute a null
     * command? In any case, we warn the user that the command expanded to
     * nothing (is this the right thing to do?).
     */

    if (*cmdStart == '\0') {
	free(cmdStart);
	return(0);
    }
    cmd = cmdStart;
    Lst_Replace(cmdNode, cmdStart);

    if ((gn->type & OP_SAVE_CMDS) && (gn != ENDNode)) {
	(void)Lst_AtEnd(ENDNode->commands, cmdStart);
	return(0);
    }
    if (strcmp(cmdStart, "...") == 0) {
	gn->type |= OP_SAVE_CMDS;
	return(0);
    }

    while ((*cmd == '@') || (*cmd == '-') || (*cmd == '+')) {
	switch (*cmd) {
	case '@':
	    silent = DEBUG(LOUD) ? FALSE : TRUE;
	    break;
	case '-':
	    errCheck = FALSE;
	    break;
	case '+':
	    doIt = TRUE;
	    if (!shellName)		/* we came here from jobs */
		Shell_Init();
	    break;
	}
	cmd++;
    }

    while (isspace((unsigned char)*cmd))
	cmd++;

    /*
     * If we did not end up with a command, just skip it.
     */
    if (!*cmd)
	return (0);

#if !defined(MAKE_NATIVE)
    /*
     * In a non-native build, the host environment might be weird enough
     * that it's necessary to go through a shell to get the correct
     * behaviour.  Or perhaps the shell has been replaced with something
     * that does extra logging, and that should not be bypassed.
     */
    useShell = TRUE;
#else
    /*
     * Search for meta characters in the command. If there are no meta
     * characters, there's no need to execute a shell to execute the
     * command.
     *
     * Additionally variable assignments and empty commands
     * go to the shell. Therefore treat '=' and ':' like shell
     * meta characters as documented in make(1).
     */
    
    useShell = needshell(cmd, FALSE);
#endif

    /*
     * Print the command before echoing if we're not supposed to be quiet for
     * this one. We also print the command if -n given.
     */
    if (!silent || NoExecute(gn)) {
	printf("%s\n", cmd);
	fflush(stdout);
    }

    /*
     * If we're not supposed to execute any commands, this is as far as
     * we go...
     */
    if (!doIt && NoExecute(gn)) {
	return (0);
    }
    if (DEBUG(JOB))
	fprintf(debug_file, "Execute: '%s'\n", cmd);

again:
    if (useShell) {
	/*
	 * We need to pass the command off to the shell, typically
	 * because the command contains a "meta" character.
	 */
	static const char *shargv[5];
	int shargc;

	shargc = 0;
	shargv[shargc++] = shellPath;
	/*
	 * The following work for any of the builtin shell specs.
	 */
	if (errCheck && shellErrFlag) {
	    shargv[shargc++] = shellErrFlag;
	}
	if (DEBUG(SHELL))
		shargv[shargc++] = "-xc";
	else
		shargv[shargc++] = "-c";
	shargv[shargc++] = cmd;
	shargv[shargc++] = NULL;
	av = shargv;
	argc = 0;
	bp = NULL;
	mav = NULL;
    } else {
	/*
	 * No meta-characters, so no need to exec a shell. Break the command
	 * into words to form an argument vector we can execute.
	 */
	mav = brk_string(cmd, &argc, TRUE, &bp);
	if (mav == NULL) {
		useShell = 1;
		goto again;
	}
	av = (void *)mav;
    }

    local = TRUE;

#ifdef USE_META
    if (useMeta) {
	meta_compat_start();
    }
#endif
    
    /*
     * Fork and execute the single command. If the fork fails, we abort.
     */
    compatChild = cpid = vFork();
    if (cpid < 0) {
	Fatal("Could not fork");
    }
    if (cpid == 0) {
	Var_ExportVars();
#ifdef USE_META
	if (useMeta) {
	    meta_compat_child();
	}
#endif
	if (local)
	    (void)execvp(av[0], (char *const *)UNCONST(av));
	else
	    (void)execv(av[0], (char *const *)UNCONST(av));
	execError("exec", av[0]);
	_exit(1);
    }

    free(mav);
    free(bp);

    Lst_Replace(cmdNode, NULL);

#ifdef USE_META
    if (useMeta) {
	meta_compat_parent();
    }
#endif

    /*
     * The child is off and running. Now all we can do is wait...
     */
    while (1) {

	while ((retstat = wait(&reason)) != cpid) {
	    if (retstat > 0)
		JobReapChild(retstat, reason, FALSE); /* not ours? */
	    if (retstat == -1 && errno != EINTR) {
		break;
	    }
	}

	if (retstat > -1) {
	    if (WIFSTOPPED(reason)) {
		status = WSTOPSIG(reason);		/* stopped */
	    } else if (WIFEXITED(reason)) {
		status = WEXITSTATUS(reason);		/* exited */
#if defined(USE_META) && defined(USE_FILEMON_ONCE)
		if (useMeta) {
		    meta_cmd_finish(NULL);
		}
#endif
		if (status != 0) {
		    if (DEBUG(ERROR)) {
		        fprintf(debug_file, "\n*** Failed target:  %s\n*** Failed command: ",
			    gn->name);
		        for (cp = cmd; *cp; ) {
    			    if (isspace((unsigned char)*cp)) {
				fprintf(debug_file, " ");
			        while (isspace((unsigned char)*cp))
				    cp++;
			    } else {
				fprintf(debug_file, "%c", *cp);
			        cp++;
			    }
		        }
			fprintf(debug_file, "\n");
		    }
		    printf("*** Error code %d", status);
		}
	    } else {
		status = WTERMSIG(reason);		/* signaled */
		printf("*** Signal %d", status);
	    }


	    if (!WIFEXITED(reason) || (status != 0)) {
		if (errCheck) {
#ifdef USE_META
		    if (useMeta) {
			meta_job_error(NULL, gn, 0, status);
		    }
#endif
		    gn->made = ERROR;
		    if (keepgoing) {
			/*
			 * Abort the current target, but let others
			 * continue.
			 */
			printf(" (continuing)\n");
		    } else {
			printf("\n");
		    }
		    if (deleteOnError) {
			    CompatDeleteTarget(gn);
		    }
		} else {
		    /*
		     * Continue executing commands for this target.
		     * If we return 0, this will happen...
		     */
		    printf(" (ignored)\n");
		    status = 0;
		}
	    }
	    break;
	} else {
	    Fatal("error in wait: %d: %s", retstat, strerror(errno));
	    /*NOTREACHED*/
	}
    }
    free(cmdStart);
    compatChild = 0;
    if (compatSigno) {
	bmake_signal(compatSigno, SIG_DFL);
	kill(myPid, compatSigno);
    }
    
    return (status);
}

/*-
 *-----------------------------------------------------------------------
 * Compat_Make --
 *	Make a target.
 *
 * Input:
 *	gnp		The node to make
 *	pgnp		Parent to abort if necessary
 *
 * Results:
 *	0
 *
 * Side Effects:
 *	If an error is detected and not being ignored, the process exits.
 *
 *-----------------------------------------------------------------------
 */
int
Compat_Make(void *gnp, void *pgnp)
{
    GNode *gn = (GNode *)gnp;
    GNode *pgn = (GNode *)pgnp;

    if (!shellName)		/* we came here from jobs */
	Shell_Init();
    if (gn->made == UNMADE && (gn == pgn || (pgn->type & OP_MADE) == 0)) {
	/*
	 * First mark ourselves to be made, then apply whatever transformations
	 * the suffix module thinks are necessary. Once that's done, we can
	 * descend and make all our children. If any of them has an error
	 * but the -k flag was given, our 'make' field will be set FALSE again.
	 * This is our signal to not attempt to do anything but abort our
	 * parent as well.
	 */
	gn->flags |= REMAKE;
	gn->made = BEINGMADE;
	if ((gn->type & OP_MADE) == 0)
	    Suff_FindDeps(gn);
	Lst_ForEach(gn->children, Compat_Make, gn);
	if ((gn->flags & REMAKE) == 0) {
	    gn->made = ABORTED;
	    pgn->flags &= ~REMAKE;
	    goto cohorts;
	}

	if (Lst_Member(gn->iParents, pgn) != NULL) {
	    char *p1;
	    Var_Set(IMPSRC, Var_Value(TARGET, gn, &p1), pgn, 0);
	    free(p1);
	}

	/*
	 * All the children were made ok. Now cmgn->mtime contains the
	 * modification time of the newest child, we need to find out if we
	 * exist and when we were modified last. The criteria for datedness
	 * are defined by the Make_OODate function.
	 */
	if (DEBUG(MAKE)) {
	    fprintf(debug_file, "Examining %s...", gn->name);
	}
	if (! Make_OODate(gn)) {
	    gn->made = UPTODATE;
	    if (DEBUG(MAKE)) {
		fprintf(debug_file, "up-to-date.\n");
	    }
	    goto cohorts;
	} else if (DEBUG(MAKE)) {
	    fprintf(debug_file, "out-of-date.\n");
	}

	/*
	 * If the user is just seeing if something is out-of-date, exit now
	 * to tell him/her "yes".
	 */
	if (queryFlag) {
	    exit(1);
	}

	/*
	 * We need to be re-made. We also have to make sure we've got a $?
	 * variable. To be nice, we also define the $> variable using
	 * Make_DoAllVar().
	 */
	Make_DoAllVar(gn);

	/*
	 * Alter our type to tell if errors should be ignored or things
	 * should not be printed so CompatRunCommand knows what to do.
	 */
	if (Targ_Ignore(gn)) {
	    gn->type |= OP_IGNORE;
	}
	if (Targ_Silent(gn)) {
	    gn->type |= OP_SILENT;
	}

	if (Job_CheckCommands(gn, Fatal)) {
	    /*
	     * Our commands are ok, but we still have to worry about the -t
	     * flag...
	     */
	    if (!touchFlag || (gn->type & OP_MAKE)) {
		curTarg = gn;
#ifdef USE_META
		if (useMeta && !NoExecute(gn)) {
		    meta_job_start(NULL, gn);
		}
#endif
		Lst_ForEach(gn->commands, CompatRunCommand, gn);
		curTarg = NULL;
	    } else {
		Job_Touch(gn, gn->type & OP_SILENT);
	    }
	} else {
	    gn->made = ERROR;
	}
#ifdef USE_META
	if (useMeta && !NoExecute(gn)) {
	    if (meta_job_finish(NULL) != 0)
		gn->made = ERROR;
	}
#endif

	if (gn->made != ERROR) {
	    /*
	     * If the node was made successfully, mark it so, update
	     * its modification time and timestamp all its parents. Note
	     * that for .ZEROTIME targets, the timestamping isn't done.
	     * This is to keep its state from affecting that of its parent.
	     */
	    gn->made = MADE;
	    pgn->flags |= Make_Recheck(gn) == 0 ? FORCE : 0;
	    if (!(gn->type & OP_EXEC)) {
		pgn->flags |= CHILDMADE;
		Make_TimeStamp(pgn, gn);
	    }
	} else if (keepgoing) {
	    pgn->flags &= ~REMAKE;
	} else {
	    PrintOnError(gn, "\nStop.");
	    exit(1);
	}
    } else if (gn->made == ERROR) {
	/*
	 * Already had an error when making this beastie. Tell the parent
	 * to abort.
	 */
	pgn->flags &= ~REMAKE;
    } else {
	if (Lst_Member(gn->iParents, pgn) != NULL) {
	    char *p1;
	    Var_Set(IMPSRC, Var_Value(TARGET, gn, &p1), pgn, 0);
	    free(p1);
	}
	switch(gn->made) {
	    case BEINGMADE:
		Error("Graph cycles through %s", gn->name);
		gn->made = ERROR;
		pgn->flags &= ~REMAKE;
		break;
	    case MADE:
		if ((gn->type & OP_EXEC) == 0) {
		    pgn->flags |= CHILDMADE;
		    Make_TimeStamp(pgn, gn);
		}
		break;
	    case UPTODATE:
		if ((gn->type & OP_EXEC) == 0) {
		    Make_TimeStamp(pgn, gn);
		}
		break;
	    default:
		break;
	}
    }

cohorts:
    Lst_ForEach(gn->cohorts, Compat_Make, pgnp);
    return (0);
}

/*-
 *-----------------------------------------------------------------------
 * Compat_Run --
 *	Initialize this mode and start making.
 *
 * Input:
 *	targs		List of target nodes to re-create
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Guess what?
 *
 *-----------------------------------------------------------------------
 */
void
Compat_Run(Lst targs)
{
    GNode   	  *gn = NULL;/* Current root target */
    int	    	  errors;   /* Number of targets not remade due to errors */

    if (!shellName)
	Shell_Init();

    if (bmake_signal(SIGINT, SIG_IGN) != SIG_IGN) {
	bmake_signal(SIGINT, CompatInterrupt);
    }
    if (bmake_signal(SIGTERM, SIG_IGN) != SIG_IGN) {
	bmake_signal(SIGTERM, CompatInterrupt);
    }
    if (bmake_signal(SIGHUP, SIG_IGN) != SIG_IGN) {
	bmake_signal(SIGHUP, CompatInterrupt);
    }
    if (bmake_signal(SIGQUIT, SIG_IGN) != SIG_IGN) {
	bmake_signal(SIGQUIT, CompatInterrupt);
    }

    ENDNode = Targ_FindNode(".END", TARG_CREATE);
    ENDNode->type = OP_SPECIAL;
    /*
     * If the user has defined a .BEGIN target, execute the commands attached
     * to it.
     */
    if (!queryFlag) {
	gn = Targ_FindNode(".BEGIN", TARG_NOCREATE);
	if (gn != NULL) {
	    Compat_Make(gn, gn);
            if (gn->made == ERROR) {
                PrintOnError(gn, "\nStop.");
                exit(1);
            }
	}
    }

    /*
     * Expand .USE nodes right now, because they can modify the structure
     * of the tree.
     */
    Make_ExpandUse(targs);

    /*
     * For each entry in the list of targets to create, call Compat_Make on
     * it to create the thing. Compat_Make will leave the 'made' field of gn
     * in one of several states:
     *	    UPTODATE	    gn was already up-to-date
     *	    MADE  	    gn was recreated successfully
     *	    ERROR 	    An error occurred while gn was being created
     *	    ABORTED	    gn was not remade because one of its inferiors
     *	    	  	    could not be made due to errors.
     */
    errors = 0;
    while (!Lst_IsEmpty (targs)) {
	gn = (GNode *)Lst_DeQueue(targs);
	Compat_Make(gn, gn);

	if (gn->made == UPTODATE) {
	    printf("`%s' is up to date.\n", gn->name);
	} else if (gn->made == ABORTED) {
	    printf("`%s' not remade because of errors.\n", gn->name);
	    errors += 1;
	}
    }

    /*
     * If the user has defined a .END target, run its commands.
     */
    if (errors == 0) {
	Compat_Make(ENDNode, ENDNode);
	if (gn->made == ERROR) {
	    PrintOnError(gn, "\nStop.");
	    exit(1);
	}
    }
}
