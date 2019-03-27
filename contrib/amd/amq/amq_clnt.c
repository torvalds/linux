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
 * File: am-utils/amq/amq_clnt.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amq.h>


static struct timeval TIMEOUT = {ALLOWED_MOUNT_TIME, 0};


voidp
amqproc_null_1(voidp argp, CLIENT *clnt)
{
  static char res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_NULL,
		(XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_void, &res, TIMEOUT)
      != RPC_SUCCESS) {
    return (NULL);
  }
  return ((voidp) &res);
}


amq_mount_tree_p *
amqproc_mnttree_1(amq_string *argp, CLIENT *clnt)
{
  static amq_mount_tree_p res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_MNTTREE,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) argp,
		(XDRPROC_T_TYPE) xdr_amq_mount_tree_p, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


voidp
amqproc_umnt_1(amq_string *argp, CLIENT *clnt)
{
  static char res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_UMNT,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) argp,
		(XDRPROC_T_TYPE) xdr_void, &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return ((voidp) &res);
}


amq_sync_umnt *
amqproc_sync_umnt_1(amq_string *argp, CLIENT *clnt)
{
  static amq_sync_umnt res;
  enum clnt_stat rv;

  memset((char *) &res, 0, sizeof(res));
  if ((rv = clnt_call(clnt, AMQPROC_SYNC_UMNT,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) argp,
		(XDRPROC_T_TYPE) xdr_amq_sync_umnt, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT)) != RPC_SUCCESS) {
    return (NULL);
  }
  return &res;
}


amq_mount_stats *
amqproc_stats_1(voidp argp, CLIENT *clnt)
{
  static amq_mount_stats res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_STATS,
		(XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_amq_mount_stats,
		(SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


amq_mount_tree_list *
amqproc_export_1(voidp argp, CLIENT *clnt)
{
  static amq_mount_tree_list res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_EXPORT,
		(XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_amq_mount_tree_list,
		(SVC_IN_ARG_TYPE) &res, TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


int *
amqproc_setopt_1(amq_setopt *argp, CLIENT *clnt)
{
  static int res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_SETOPT, (XDRPROC_T_TYPE) xdr_amq_setopt,
		(SVC_IN_ARG_TYPE) argp, (XDRPROC_T_TYPE) xdr_int,
		(SVC_IN_ARG_TYPE) &res, TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


amq_mount_info_list *
amqproc_getmntfs_1(voidp argp, CLIENT *clnt)
{
  static amq_mount_info_list res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_GETMNTFS, (XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_amq_mount_info_list,
		(SVC_IN_ARG_TYPE) &res, TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}

amq_map_info_list *
amqproc_getmapinfo_1(voidp argp, CLIENT *clnt)
{
  static amq_map_info_list res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_GETMAPINFO, (XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_amq_map_info_list,
		(SVC_IN_ARG_TYPE) &res, TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


int *
amqproc_mount_1(voidp argp, CLIENT *clnt)
{
  static int res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_MOUNT, (XDRPROC_T_TYPE) xdr_amq_string, argp,
		(XDRPROC_T_TYPE) xdr_int, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


amq_string *
amqproc_getvers_1(voidp argp, CLIENT *clnt)
{
  static amq_string res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_GETVERS, (XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


int *
amqproc_getpid_1(voidp argp, CLIENT *clnt)
{
  static int res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_GETPID, (XDRPROC_T_TYPE) xdr_void, argp,
		(XDRPROC_T_TYPE) xdr_int, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}


amq_string *
amqproc_pawd_1(amq_string *argp, CLIENT *clnt)
{
  static amq_string res;

  memset((char *) &res, 0, sizeof(res));
  if (clnt_call(clnt, AMQPROC_PAWD,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) argp,
		(XDRPROC_T_TYPE) xdr_amq_string, (SVC_IN_ARG_TYPE) &res,
		TIMEOUT) != RPC_SUCCESS) {
    return (NULL);
  }
  return (&res);
}
