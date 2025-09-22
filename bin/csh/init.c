/*	$OpenBSD: init.c,v 1.8 2014/10/16 18:23:26 deraadt Exp $	*/
/*	$NetBSD: init.c,v 1.6 1995/03/21 09:03:05 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <stdarg.h>

#include "csh.h"
#include "extern.h"

#define	INF	1000

struct biltins bfunc[] =
{
    { "@", 		dolet, 		0, INF	},
    { "alias", 		doalias, 	0, INF	},
    { "bg", 		dobg, 		0, INF	},
    { "break", 		dobreak, 	0, 0	},
    { "breaksw", 	doswbrk, 	0, 0	},
    { "case", 		dozip, 		0, 1	},
    { "cd", 		dochngd, 	0, INF	},
    { "chdir", 		dochngd, 	0, INF	},
    { "continue", 	docontin, 	0, 0	},
    { "default", 	dozip, 		0, 0	},
    { "dirs", 		dodirs,		0, INF	},
    { "echo", 		doecho,		0, INF	},
    { "else", 		doelse,		0, INF	},
    { "end", 		doend, 		0, 0	},
    { "endif", 		dozip, 		0, 0	},
    { "endsw", 		dozip, 		0, 0	},
    { "eval", 		doeval, 	0, INF	},
    { "exec", 		execash, 	1, INF	},
    { "exit", 		doexit, 	0, INF	},
    { "fg", 		dofg, 		0, INF	},
    { "foreach", 	doforeach, 	3, INF	},
    { "glob", 		doglob, 	0, INF	},
    { "goto", 		dogoto, 	1, 1	},
    { "hashstat", 	hashstat, 	0, 0	},
    { "history", 	dohist, 	0, 2	},
    { "if", 		doif, 		1, INF	},
    { "jobs", 		dojobs, 	0, 1	},
    { "kill", 		dokill, 	1, INF	},
    { "limit", 		dolimit, 	0, 3	},
    { "linedit", 	doecho, 	0, INF	},
    { "login", 		dologin, 	0, 1	},
    { "logout", 	dologout, 	0, 0	},
    { "nice", 		donice, 	0, INF	},
    { "nohup", 		donohup, 	0, INF	},
    { "notify", 	donotify, 	0, INF	},
    { "onintr", 	doonintr, 	0, 2	},
    { "popd", 		dopopd, 	0, INF	},
    { "pushd", 		dopushd, 	0, INF	},
    { "rehash", 	dohash, 	0, 0	},
    { "repeat", 	dorepeat, 	2, INF	},
    { "set", 		doset, 		0, INF	},
    { "setenv", 	dosetenv, 	0, 2	},
    { "shift", 		shift, 		0, 1	},
    { "source", 	dosource, 	1, 2	},
    { "stop", 		dostop, 	1, INF	},
    { "suspend", 	dosuspend, 	0, 0	},
    { "switch", 	doswitch, 	1, INF	},
    { "time", 		dotime, 	0, INF	},
    { "umask", 		doumask, 	0, 1	},
    { "unalias", 	unalias, 	1, INF	},
    { "unhash", 	dounhash, 	0, 0	},
    { "unlimit", 	dounlimit, 	0, INF	},
    { "unset", 		unset, 		1, INF	},
    { "unsetenv", 	dounsetenv, 	1, INF	},
    { "wait",		dowait, 	0, 0	},
    { "which",		dowhich, 	1, INF	},
    { "while", 		dowhile, 	1, INF	}
};
int     nbfunc = sizeof bfunc / sizeof *bfunc;

struct srch srchn[] =
{
    { "@", 		T_LET		},
    { "break", 		T_BREAK		},
    { "breaksw", 	T_BRKSW		},
    { "case", 		T_CASE		},
    { "default", 	T_DEFAULT	},
    { "else", 		T_ELSE		},
    { "end", 		T_END		},
    { "endif", 		T_ENDIF		},
    { "endsw", 		T_ENDSW		},
    { "exit", 		T_EXIT		},
    { "foreach", 	T_FOREACH	},
    { "goto", 		T_GOTO		},
    { "if", 		T_IF		},
    { "label", 		T_LABEL		},
    { "set", 		T_SET		},
    { "switch", 	T_SWITCH	},
    { "while", 		T_WHILE		}
};
int     nsrchn = sizeof srchn / sizeof *srchn;

