/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *
 *
 * File: am-utils/amd/sched.c
 *
 */

/*
 * Process scheduler
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


typedef struct pjob pjob;

struct pjob {
  qelem hdr;		/* Linked list */
  int pid;		/* Process ID of job */
  cb_fun *cb_fun;	/* Callback function */
  opaque_t cb_arg;	/* Argument for callback */
  int w;		/* everyone these days uses int, not a "union wait" */
  wchan_t wchan;	/* Wait channel */
};

/* globals */
qelem proc_list_head = {&proc_list_head, &proc_list_head};
qelem proc_wait_list = {&proc_wait_list, &proc_wait_list};
int task_notify_todo;


void
ins_que(qelem *elem, qelem *pred)
{
  qelem *p = pred->q_forw;

  elem->q_back = pred;
  elem->q_forw = p;
  pred->q_forw = elem;
  p->q_back = elem;
}


void
rem_que(qelem *elem)
{
  qelem *p = elem->q_forw;
  qelem *p2 = elem->q_back;

  p2->q_forw = p;
  p->q_back = p2;
}


static pjob *
sched_job(cb_fun *cf, opaque_t ca)
{
  pjob *p = ALLOC(struct pjob);

  p->cb_fun = cf;
  p->cb_arg = ca;

  /*
   * Now place on wait queue
   */
  ins_que(&p->hdr, &proc_wait_list);

  return p;
}


/*
 * tf: The task to execute (ta is its arguments)
 * cf: Continuation function (ca is its arguments)
 */
void
run_task(task_fun *tf, opaque_t ta, cb_fun *cf, opaque_t ca)
{
  pjob *p = sched_job(cf, ca);
#ifdef HAVE_SIGACTION
  sigset_t new, mask;
#else /* not HAVE_SIGACTION */
  int mask;
#endif /* not HAVE_SIGACTION */

  p->wchan = (wchan_t) p;

#ifdef HAVE_SIGACTION
  sigemptyset(&new);		/* initialize signal set we wish to block */
  sigaddset(&new, SIGCHLD);	/* only block on SIGCHLD */
  sigprocmask(SIG_BLOCK, &new, &mask);
#else /* not HAVE_SIGACTION */
  mask = sigblock(sigmask(SIGCHLD));
#endif /* not HAVE_SIGACTION */

  if ((p->pid = background())) {
#ifdef HAVE_SIGACTION
    sigprocmask(SIG_SETMASK, &mask, NULL);
#else /* not HAVE_SIGACTION */
    sigsetmask(mask);
#endif /* not HAVE_SIGACTION */
    return;
  }

  /* child code runs here, parent has returned to caller */

  exit((*tf) (ta));
  /* firewall... */
  abort();
}


/*
 * Schedule a task to be run when woken up
 */
void
sched_task(cb_fun *cf, opaque_t ca, wchan_t wchan)
{
  /*
   * Allocate a new task
   */
  pjob *p = sched_job(cf, ca);

  dlog("SLEEP on %p", wchan);
  p->wchan = wchan;
  p->pid = 0;
  p->w = 0;			/* was memset (when ->w was union) */
}


static void
wakeupjob(pjob *p)
{
  rem_que(&p->hdr);
  ins_que(&p->hdr, &proc_list_head);
  task_notify_todo++;
}


void
wakeup(wchan_t wchan)
{
  pjob *p, *p2;

  if (!foreground)
    return;

  /*
   * Can't use ITER() here because
   * wakeupjob() juggles the list.
   */
  for (p = AM_FIRST(pjob, &proc_wait_list);
       p2 = NEXT(pjob, p), p != HEAD(pjob, &proc_wait_list);
       p = p2) {
    if (p->wchan == wchan) {
      wakeupjob(p);
    }
  }
}


void
wakeup_task(int rc, int term, wchan_t wchan)
{
  wakeup(wchan);
}


wchan_t
get_mntfs_wchan(mntfs *mf)
{
  if (mf &&
      mf->mf_ops &&
      mf->mf_ops->get_wchan)
    return mf->mf_ops->get_wchan(mf);
  return mf;
}


/*
 * Run any pending tasks.
 * This must be called with SIGCHLD disabled
 */
void
do_task_notify(void)
{
  /*
   * Keep taking the first item off the list and processing it.
   *
   * Done this way because the callback can, quite reasonably,
   * queue a new task, so no local reference into the list can be
   * held here.
   */
  while (AM_FIRST(pjob, &proc_list_head) != HEAD(pjob, &proc_list_head)) {
    pjob *p = AM_FIRST(pjob, &proc_list_head);
    rem_que(&p->hdr);
    /*
     * This job has completed
     */
    --task_notify_todo;

    /*
     * Do callback if it exists
     */
    if (p->cb_fun) {
      /* these two trigraphs will ensure compatibility with strict POSIX.1 */
      p->cb_fun(WIFEXITED(p->w)   ? WEXITSTATUS(p->w) : 0,
		WIFSIGNALED(p->w) ? WTERMSIG(p->w)    : 0,
		p->cb_arg);
    }
    XFREE(p);
  }
}


RETSIGTYPE
sigchld(int sig)
{
  int w;	/* everyone these days uses int, not a "union wait" */
  int pid;

#ifdef HAVE_WAITPID
  while ((pid = waitpid((pid_t) -1,  &w, WNOHANG)) > 0) {
#else /* not HAVE_WAITPID */
  while ((pid = wait3( &w, WNOHANG, (struct rusage *) NULL)) > 0) {
#endif /* not HAVE_WAITPID */
    pjob *p, *p2;

    if (WIFSIGNALED(w))
      plog(XLOG_ERROR, "Process %d exited with signal %d",
	   pid, WTERMSIG(w));
    else
      dlog("Process %d exited with status %d",
	   pid, WEXITSTATUS(w));

    for (p = AM_FIRST(pjob, &proc_wait_list);
	 p2 = NEXT(pjob, p), p != HEAD(pjob, &proc_wait_list);
	 p = p2) {
      if (p->pid == pid) {
	p->w = w;
	wakeupjob(p);
	break;
      }
    } /* end of for loop */

    if (p == HEAD(pjob, &proc_wait_list))
      dlog("can't locate task block for pid %d", pid);

    /*
     * Must count down children inside the while loop, otherwise we won't
     * count them all, and NumChildren (and later backoff) will be set
     * incorrectly. SH/RUNIT 940519.
     */
    if (--NumChildren < 0)
      NumChildren = 0;
  } /* end of "while wait..." loop */

#ifdef REINSTALL_SIGNAL_HANDLER
  signal(sig, sigchld);
#endif /* REINSTALL_SIGNAL_HANDLER */

  if (select_intr_valid)
    longjmp(select_intr, sig);
}
