/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define INCL_DOSEXCEPTIONS      /* for OS2 */
#include "apr_arch_threadproc.h"
#include "apr_private.h"
#include "apr_pools.h"
#include "apr_signal.h"
#include "apr_strings.h"

#include <assert.h>
#if APR_HAS_THREADS && APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif

#ifdef SIGWAIT_TAKES_ONE_ARG
#define apr_sigwait(a,b) ((*(b)=sigwait((a)))<0?-1:0)
#else
#define apr_sigwait(a,b) sigwait((a),(b))
#endif

APR_DECLARE(apr_status_t) apr_proc_kill(apr_proc_t *proc, int signum)
{
#ifdef OS2
    /* SIGTERM's don't work too well in OS/2 (only affects other EMX
     * programs). CGIs may not be, esp. REXX scripts, so use a native
     * call instead
     */
    if (signum == SIGTERM) {
        return APR_OS2_STATUS(DosSendSignalException(proc->pid,
                                                     XCPT_SIGNAL_BREAK));
    }
#endif /* OS2 */

    if (kill(proc->pid, signum) == -1) {
        return errno;
    }

    return APR_SUCCESS;
}


#if APR_HAVE_SIGACTION

#if defined(__NetBSD__) || defined(DARWIN)
static void avoid_zombies(int signo)
{
    int exit_status;

    while (waitpid(-1, &exit_status, WNOHANG) > 0) {
        /* do nothing */
    }
}
#endif /* DARWIN */

/*
 * Replace standard signal() with the more reliable sigaction equivalent
 * from W. Richard Stevens' "Advanced Programming in the UNIX Environment"
 * (the version that does not automatically restart system calls).
 */
APR_DECLARE(apr_sigfunc_t *) apr_signal(int signo, apr_sigfunc_t * func)
{
    struct sigaction act, oact;

    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
#ifdef SA_INTERRUPT             /* SunOS */
    act.sa_flags |= SA_INTERRUPT;
#endif
#if defined(__osf__) && defined(__alpha)
    /* XXX jeff thinks this should be enabled whenever SA_NOCLDWAIT is defined */

    /* this is required on Tru64 to cause child processes to
     * disappear gracefully - XPG4 compatible 
     */
    if ((signo == SIGCHLD) && (func == SIG_IGN)) {
        act.sa_flags |= SA_NOCLDWAIT;
    }
#endif
#if defined(__NetBSD__) || defined(DARWIN)
    /* ignoring SIGCHLD or leaving the default disposition doesn't avoid zombies,
     * and there is no SA_NOCLDWAIT flag, so catch the signal and reap status in 
     * the handler to avoid zombies
     */
    if ((signo == SIGCHLD) && (func == SIG_IGN)) {
        act.sa_handler = avoid_zombies;
    }
#endif
    if (sigaction(signo, &act, &oact) < 0)
        return SIG_ERR;
    return oact.sa_handler;
}

#endif /* HAVE_SIGACTION */

/* AC_DECL_SYS_SIGLIST defines either of these symbols depending
 * on the version of autoconf used. */
#if defined(SYS_SIGLIST_DECLARED) || HAVE_DECL_SYS_SIGLIST

void apr_signal_init(apr_pool_t *pglobal)
{
}
const char *apr_signal_description_get(int signum)
{
    return (signum >= 0) ? sys_siglist[signum] : "unknown signal (number)";
}

#else /* !(SYS_SIGLIST_DECLARED || HAVE_DECL_SYS_SIGLIST) */

/* we need to roll our own signal description stuff */

#if defined(NSIG)
#define APR_NUMSIG NSIG
#elif defined(_NSIG)
#define APR_NUMSIG _NSIG
#elif defined(__NSIG)
#define APR_NUMSIG __NSIG
#else
#define APR_NUMSIG 33   /* breaks on OS/390 with < 33; 32 is o.k. for most */
#endif

static const char *signal_description[APR_NUMSIG];

#define store_desc(index, string) \
        do { \
            if (index >= APR_NUMSIG) { \
                assert(index < APR_NUMSIG); \
            } \
            else { \
                signal_description[index] = string; \
            } \
        } while (0)

void apr_signal_init(apr_pool_t *pglobal)
{
    int sig;

    store_desc(0, "Signal 0");

#ifdef SIGHUP
    store_desc(SIGHUP, "Hangup");
#endif
#ifdef SIGINT
    store_desc(SIGINT, "Interrupt");
#endif
#ifdef SIGQUIT
    store_desc(SIGQUIT, "Quit");
#endif
#ifdef SIGILL
    store_desc(SIGILL, "Illegal instruction");
#endif
#ifdef SIGTRAP
    store_desc(SIGTRAP, "Trace/BPT trap");
#endif
#ifdef SIGIOT
    store_desc(SIGIOT, "IOT instruction");
#endif
#ifdef SIGABRT
    store_desc(SIGABRT, "Abort");
#endif
#ifdef SIGEMT
    store_desc(SIGEMT, "Emulator trap");
#endif
#ifdef SIGFPE
    store_desc(SIGFPE, "Arithmetic exception");
#endif
#ifdef SIGKILL
    store_desc(SIGKILL, "Killed");
#endif
#ifdef SIGBUS
    store_desc(SIGBUS, "Bus error");
#endif
#ifdef SIGSEGV
    store_desc(SIGSEGV, "Segmentation fault");
#endif
#ifdef SIGSYS
    store_desc(SIGSYS, "Bad system call");
#endif
#ifdef SIGPIPE
    store_desc(SIGPIPE, "Broken pipe");
#endif
#ifdef SIGALRM
    store_desc(SIGALRM, "Alarm clock");
#endif
#ifdef SIGTERM
    store_desc(SIGTERM, "Terminated");
#endif
#ifdef SIGUSR1
    store_desc(SIGUSR1, "User defined signal 1");
#endif
#ifdef SIGUSR2
    store_desc(SIGUSR2, "User defined signal 2");
#endif
#ifdef SIGCLD
    store_desc(SIGCLD, "Child status change");
#endif
#ifdef SIGCHLD
    store_desc(SIGCHLD, "Child status change");
#endif
#ifdef SIGPWR
    store_desc(SIGPWR, "Power-fail restart");
#endif
#ifdef SIGWINCH
    store_desc(SIGWINCH, "Window changed");
#endif
#ifdef SIGURG
    store_desc(SIGURG, "urgent socket condition");
#endif
#ifdef SIGPOLL
    store_desc(SIGPOLL, "Pollable event occurred");
#endif
#ifdef SIGIO
    store_desc(SIGIO, "socket I/O possible");
#endif
#ifdef SIGSTOP
    store_desc(SIGSTOP, "Stopped (signal)");
#endif
#ifdef SIGTSTP
    store_desc(SIGTSTP, "Stopped");
#endif
#ifdef SIGCONT
    store_desc(SIGCONT, "Continued");
#endif
#ifdef SIGTTIN
    store_desc(SIGTTIN, "Stopped (tty input)");
#endif
#ifdef SIGTTOU
    store_desc(SIGTTOU, "Stopped (tty output)");
#endif
#ifdef SIGVTALRM
    store_desc(SIGVTALRM, "virtual timer expired");
#endif
#ifdef SIGPROF
    store_desc(SIGPROF, "profiling timer expired");
#endif
#ifdef SIGXCPU
    store_desc(SIGXCPU, "exceeded cpu limit");
#endif
#ifdef SIGXFSZ
    store_desc(SIGXFSZ, "exceeded file size limit");
#endif

    for (sig = 0; sig < APR_NUMSIG; ++sig)
        if (signal_description[sig] == NULL)
            signal_description[sig] = apr_psprintf(pglobal, "signal #%d", sig);
}

const char *apr_signal_description_get(int signum)
{
    return
        (signum >= 0 && signum < APR_NUMSIG)
        ? signal_description[signum]
        : "unknown signal (number)";
}

#endif /* SYS_SIGLIST_DECLARED || HAVE_DECL_SYS_SIGLIST */

#if APR_HAS_THREADS && (HAVE_SIGSUSPEND || APR_HAVE_SIGWAIT) && !defined(OS2)

static void remove_sync_sigs(sigset_t *sig_mask)
{
#ifdef SIGABRT
    sigdelset(sig_mask, SIGABRT);
#endif
#ifdef SIGBUS
    sigdelset(sig_mask, SIGBUS);
#endif
#ifdef SIGEMT
    sigdelset(sig_mask, SIGEMT);
#endif
#ifdef SIGFPE
    sigdelset(sig_mask, SIGFPE);
#endif
#ifdef SIGILL
    sigdelset(sig_mask, SIGILL);
#endif
#ifdef SIGIOT
    sigdelset(sig_mask, SIGIOT);
#endif
#ifdef SIGPIPE
    sigdelset(sig_mask, SIGPIPE);
#endif
#ifdef SIGSEGV
    sigdelset(sig_mask, SIGSEGV);
#endif
#ifdef SIGSYS
    sigdelset(sig_mask, SIGSYS);
#endif
#ifdef SIGTRAP
    sigdelset(sig_mask, SIGTRAP);
#endif

/* the rest of the signals removed from the mask in this function
 * absolutely must be removed; you cannot block synchronous signals
 * (requirement of pthreads API)
 *
 * SIGUSR2 is being removed from the mask for the convenience of
 * Purify users (Solaris, HP-UX, SGI) since Purify uses SIGUSR2
 */
#ifdef SIGUSR2
    sigdelset(sig_mask, SIGUSR2);
#endif
}

APR_DECLARE(apr_status_t) apr_signal_thread(int(*signal_handler)(int signum))
{
    sigset_t sig_mask;
#if APR_HAVE_SIGWAIT
    int (*sig_func)(int signum) = (int (*)(int))signal_handler;
#endif

    /* This thread will be the one responsible for handling signals */
    sigfillset(&sig_mask);

    /* On certain platforms, sigwait() returns EINVAL if any of various
     * unblockable signals are included in the mask.  This was first 
     * observed on AIX and Tru64.
     */
#ifdef SIGKILL
    sigdelset(&sig_mask, SIGKILL);
#endif
#ifdef SIGSTOP
    sigdelset(&sig_mask, SIGSTOP);
#endif
#ifdef SIGCONT
    sigdelset(&sig_mask, SIGCONT);
#endif
#ifdef SIGWAITING
    sigdelset(&sig_mask, SIGWAITING);
#endif

    /* no synchronous signals should be in the mask passed to sigwait() */
    remove_sync_sigs(&sig_mask);

    /* On AIX (4.3.3, at least), sigwait() won't wake up if the high-
     * order bit of the second word of flags is turned on.  sigdelset()
     * returns an error when trying to turn this off, so we'll turn it
     * off manually.
     *
     * Note that the private fields differ between 32-bit and 64-bit
     * and even between _ALL_SOURCE and !_ALL_SOURCE.  Except that on
     * AIX 4.3 32-bit builds and 64-bit builds use the same definition.
     *
     * Applicable AIX fixes such that this is no longer needed:
     *
     * APAR IY23096 for AIX 51B, fix included in AIX 51C, and
     * APAR IY24162 for 43X.
     */
#if defined(_AIX)
#if defined(__64BIT__) && defined(_AIXVERSION_510)
#ifdef _ALL_SOURCE
        sig_mask.ss_set[3] &= 0x7FFFFFFF;
#else /* not _ALL_SOURCE */
        sig_mask.__ss_set[3] &= 0x7FFFFFFF;
#endif
#else /* not 64-bit build, or 64-bit build on 4.3 */
#ifdef _ALL_SOURCE
        sig_mask.hisigs &= 0x7FFFFFFF;
#else /* not _ALL_SOURCE */
        sig_mask.__hisigs &= 0x7FFFFFFF;
#endif
#endif
#endif /* _AIX */

    while (1) {
#if APR_HAVE_SIGWAIT
        int signal_received;

        if (apr_sigwait(&sig_mask, &signal_received) != 0)
        {
            /* handle sigwait() error here */
        }
        
        if (sig_func(signal_received) == 1) {
            return APR_SUCCESS;
        }
#elif HAVE_SIGSUSPEND
	sigsuspend(&sig_mask);
#else
#error No apr_sigwait() and no sigsuspend()
#endif
    }
}

APR_DECLARE(apr_status_t) apr_setup_signal_thread(void)
{
    sigset_t sig_mask;
    int rv;

    /* All threads should mask out signals to be handled by
     * the thread doing sigwait().
     *
     * No thread should ever block synchronous signals.
     * See the Solaris man page for pthread_sigmask() for
     * some information.  Solaris chooses to knock out such
     * processes when a blocked synchronous signal is 
     * delivered, skipping any registered signal handler.
     * AIX doesn't call a signal handler either.  At least
     * one level of linux+glibc does call the handler even
     * when the synchronous signal is blocked.
     */
    sigfillset(&sig_mask);
    remove_sync_sigs(&sig_mask);

#if defined(SIGPROCMASK_SETS_THREAD_MASK) || ! APR_HAS_THREADS
    if ((rv = sigprocmask(SIG_SETMASK, &sig_mask, NULL)) != 0) {
        rv = errno;
    }
#else
    if ((rv = pthread_sigmask(SIG_SETMASK, &sig_mask, NULL)) != 0) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
    }
#endif
    return rv;
}

#endif /* APR_HAS_THREADS && ... */

APR_DECLARE(apr_status_t) apr_signal_block(int signum)
{
#if APR_HAVE_SIGACTION
    sigset_t sig_mask;
    int rv;

    sigemptyset(&sig_mask);

    sigaddset(&sig_mask, signum);

#if defined(SIGPROCMASK_SETS_THREAD_MASK) || ! APR_HAS_THREADS
    if ((rv = sigprocmask(SIG_BLOCK, &sig_mask, NULL)) != 0) {
        rv = errno;
    }
#else
    if ((rv = pthread_sigmask(SIG_BLOCK, &sig_mask, NULL)) != 0) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
    }
#endif
    return rv;
#else
    return APR_ENOTIMPL;
#endif
}

APR_DECLARE(apr_status_t) apr_signal_unblock(int signum)
{
#if APR_HAVE_SIGACTION
    sigset_t sig_mask;
    int rv;

    sigemptyset(&sig_mask);

    sigaddset(&sig_mask, signum);

#if defined(SIGPROCMASK_SETS_THREAD_MASK) || ! APR_HAS_THREADS
    if ((rv = sigprocmask(SIG_UNBLOCK, &sig_mask, NULL)) != 0) {
        rv = errno;
    }
#else
    if ((rv = pthread_sigmask(SIG_UNBLOCK, &sig_mask, NULL)) != 0) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
    }
#endif
    return rv;
#else
    return APR_ENOTIMPL;
#endif
}
