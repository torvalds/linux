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
 * File: am-utils/amd/amq_svc.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* typedefs */
typedef char *(*amqsvcproc_t)(voidp, struct svc_req *);

#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
# ifdef NEED_LIBWRAP_SEVERITY_VARIABLES
/*
 * Some systems that define libwrap already define these two variables
 * in libwrap, while others don't: so I need to know precisely iff
 * to define these two severity variables.
 */
int allow_severity=0, deny_severity=0, rfc931_timeout=0;
# endif /* NEED_LIBWRAP_SEVERITY_VARIABLES */

/*
 * check if remote amq is authorized to access this amd.
 * Returns: 1=allowed, 0=denied.
 */
static int
amqsvc_is_client_allowed(const struct sockaddr_in *addr)
{
  struct request_info req;

  request_init(&req, RQ_DAEMON, AMD_SERVICE_NAME, RQ_CLIENT_SIN, addr, 0);
  sock_methods(&req);

  if (hosts_access(&req))
         return 1;

  return 0;
}
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */


/*
 * Prepare the parent and child:
 * 1) Setup IPC pipe.
 * 2) Set signal masks.
 * 3) Fork by calling background() so that NumChildren is updated.
 */
static int
amq_fork(opaque_t argp)
{
#ifdef HAVE_SIGACTION
  sigset_t new, mask;
#else /* not HAVE_SIGACTION */
  int mask;
#endif /* not HAVE_SIGACTION */
  am_node *mp;
  pid_t pid;

  mp = find_ap(*(char **) argp);
  if (mp == NULL) {
    errno = 0;
    return -1;
  }

  if (pipe(mp->am_fd) == -1) {
    mp->am_fd[0] = -1;
    mp->am_fd[1] = -1;
    return -1;
  }

#ifdef HAVE_SIGACTION
  sigemptyset(&new);		/* initialize signal set we wish to block */
  sigaddset(&new, SIGHUP);
  sigaddset(&new, SIGINT);
  sigaddset(&new, SIGQUIT);
  sigaddset(&new, SIGCHLD);
  sigprocmask(SIG_BLOCK, &new, &mask);
#else /* not HAVE_SIGACTION */
  mask =
      sigmask(SIGHUP) |
      sigmask(SIGINT) |
      sigmask(SIGQUIT) |
      sigmask(SIGCHLD);
  mask = sigblock(mask);
#endif /* not HAVE_SIGACTION */

  switch ((pid = background())) {
  case -1:	/* error */
    dlog("amq_fork failed");
    return -1;

  case 0:	/* child */
    close(mp->am_fd[1]);	/* close output end of pipe */
    mp->am_fd[1] = -1;
    return 0;

  default:	/* parent */
    close(mp->am_fd[0]);	/* close input end of pipe */
    mp->am_fd[0] = -1;

#ifdef HAVE_SIGACTION
    sigprocmask(SIG_SETMASK, &mask, NULL);
#else /* not HAVE_SIGACTION */
    sigsetmask(mask);
#endif /* not HAVE_SIGACTION */
    return pid;
  }
}


void
amq_program_1(struct svc_req *rqstp, SVCXPRT *transp)
{
  union {
    amq_string amqproc_mnttree_1_arg;
    amq_string amqproc_umnt_1_arg;
    amq_setopt amqproc_setopt_1_arg;
  } argument;
  char *result;
  xdrproc_t xdr_argument, xdr_result;
  amqsvcproc_t local;
  amqsvcproc_t child;
  amqsvcproc_t parent;
  pid_t pid;

#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
  if (gopt.flags & CFM_USE_TCPWRAPPERS) {
    struct sockaddr_in *remote_addr = svc_getcaller(rqstp->rq_xprt);
    char *remote_hostname = inet_ntoa(remote_addr->sin_addr);

    if (!amqsvc_is_client_allowed(remote_addr)) {
      plog(XLOG_WARNING, "Amd denied remote amq service to %s", remote_hostname);
      svcerr_auth(transp, AUTH_FAILED);
      return;
    } else {
      dlog("Amd allowed remote amq service to %s", remote_hostname);
    }
  }
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */

  local = NULL;
  child = NULL;
  parent = NULL;

  switch (rqstp->rq_proc) {

  case AMQPROC_NULL:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_void;
    local = (amqsvcproc_t) amqproc_null_1_svc;
    break;

  case AMQPROC_MNTTREE:
    xdr_argument = (xdrproc_t) xdr_amq_string;
    xdr_result = (xdrproc_t) xdr_amq_mount_tree_p;
    local = (amqsvcproc_t) amqproc_mnttree_1_svc;
    break;

  case AMQPROC_UMNT:
    xdr_argument = (xdrproc_t) xdr_amq_string;
    xdr_result = (xdrproc_t) xdr_void;
    local = (amqsvcproc_t) amqproc_umnt_1_svc;
    break;

  case AMQPROC_STATS:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_amq_mount_stats;
    local = (amqsvcproc_t) amqproc_stats_1_svc;
    break;

  case AMQPROC_EXPORT:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_amq_mount_tree_list;
    local = (amqsvcproc_t) amqproc_export_1_svc;
    break;

  case AMQPROC_SETOPT:
    xdr_argument = (xdrproc_t) xdr_amq_setopt;
    xdr_result = (xdrproc_t) xdr_int;
    local = (amqsvcproc_t) amqproc_setopt_1_svc;
    break;

  case AMQPROC_GETMNTFS:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_amq_mount_info_qelem;
    local = (amqsvcproc_t) amqproc_getmntfs_1_svc;
    break;

  case AMQPROC_GETVERS:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_amq_string;
    local = (amqsvcproc_t) amqproc_getvers_1_svc;
    break;

  case AMQPROC_GETPID:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_int;
    local = (amqsvcproc_t) amqproc_getpid_1_svc;
    break;

  case AMQPROC_PAWD:
    xdr_argument = (xdrproc_t) xdr_amq_string;
    xdr_result = (xdrproc_t) xdr_amq_string;
    local = (amqsvcproc_t) amqproc_pawd_1_svc;
    break;

  case AMQPROC_SYNC_UMNT:
    xdr_argument = (xdrproc_t) xdr_amq_string;
    xdr_result = (xdrproc_t) xdr_amq_sync_umnt;
    parent = (amqsvcproc_t) amqproc_sync_umnt_1_svc_parent;
    child = (amqsvcproc_t) amqproc_sync_umnt_1_svc_child;
    /* used if fork fails */
    local = (amqsvcproc_t) amqproc_sync_umnt_1_svc_async;
    break;

  case AMQPROC_GETMAPINFO:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_amq_map_info_qelem;
    local = (amqsvcproc_t) amqproc_getmapinfo_1_svc;
    break;

  default:
    svcerr_noproc(transp);
    return;
  }

  memset((char *) &argument, 0, sizeof(argument));
  if (!svc_getargs(transp,
		   (XDRPROC_T_TYPE) xdr_argument,
		   (SVC_IN_ARG_TYPE) & argument)) {
    svcerr_decode(transp);
    return;
  }

  pid = -1;
  result = NULL;

  if (child) {
    switch ((pid = amq_fork(&argument))) {
    case -1:	/* error */
      break;

    case 0:	/* child */
      result = (*child) (&argument, rqstp);
      local = NULL;
      break;

    default:	/* parent */
      result = (*parent) (&argument, rqstp);
      local = NULL;
      break;
    }
  }

  if (local)
    result = (*local) (&argument, rqstp);

  if (result != NULL && !svc_sendreply(transp,
				       (XDRPROC_T_TYPE) xdr_result,
				       result)) {
    svcerr_systemerr(transp);
  }

  if (!svc_freeargs(transp,
		    (XDRPROC_T_TYPE) xdr_argument,
		    (SVC_IN_ARG_TYPE) & argument)) {
    plog(XLOG_FATAL, "unable to free rpc arguments in amqprog_1");
    going_down(1);
  }

  if (pid == 0)
    exit(0);	/* the child is done! */
}
