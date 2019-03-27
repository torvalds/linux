/* NAME:
 *	wait.h - compensate for what vendors leave out
 *
 * AUTHOR:
 *	Simon J. Gerraty <sjg@crufty.net>
 */
/*
 * RCSid:
 *	$Id: wait.h,v 1.6 2002/11/26 07:53:06 sjg Exp $
 *
 *      @(#)Copyright (c) 1994, Simon J. Gerraty.
 *      
 *      This is free software.  It comes with NO WARRANTY.
 *      Permission to use, modify and distribute this source code 
 *      is granted subject to the following conditions.
 *      1/ that the above copyright notice and this notice 
 *      are preserved in all copies and that due credit be given 
 *      to the author.  
 *      2/ that any changes to this code are clearly commented 
 *      as such so that the author does not get blamed for bugs 
 *      other than his own.
 *      
 *      Please send copies of changes and bug-fixes to:
 *      sjg@crufty.net
 */

#include <sys/wait.h>

#ifdef sun386
# define UNION_WAIT
# define WEXITSTATUS(x) ((&x)->w_retcode)
# define WTERMSIG(x) ((&x)->w_termsig)
# define WSTOPSIG(x) ((&x)->w_stopsig)
# define HAVE_WAIT4
#endif

#ifndef WAIT_T
# ifdef UNION_WAIT
#   define WAIT_T union wait
#   define WAIT_STATUS(x) (x).w_status
# else
#   define WAIT_T int
#   define WAIT_STATUS(x) x
# endif
#endif

#ifndef WEXITSTATUS
# define WEXITSTATUS(_X)       (((int)(_X)>>8)&0377)
#endif
#ifndef WSTOPPED
# define WSTOPPED 0177
#endif
#ifndef WSTOPSIG
# define WSTOPSIG(x) WSTOPPED
#endif

#ifdef UNION_WAIT
#ifndef WSET_STOPCODE
#define WSET_STOPCODE(x, sig) ((&x)->w_stopsig = (sig))
#endif
#ifndef WSET_EXITCODE
#define WSET_EXITCODE(x, ret, sig) ((&x)->w_termsig = (sig), (&x)->w_retcode = (ret))
#endif 
#else
#ifndef WSET_STOPCODE
#define WSET_STOPCODE(x, sig) ((x) = ((sig) << 8) | 0177)
#endif
#ifndef WSET_EXITCODE
#define WSET_EXITCODE(x, ret, sig) ((x) = (ret << 8) | (sig))
#endif 
#endif

#ifndef HAVE_WAITPID
# ifdef HAVE_WAIT4
#   define waitpid(pid, statusp, flags)	 wait4(pid, statusp, flags, (char *)0)
# else
#   ifdef HAVE_WAIT3
#     define waitpid(pid, statusp, flags) wait3(statusp, flags, (char *)0)
#   endif
# endif
#endif
