/*
 * Copyright (c) 2011 Christos Zoulas
 * All rights reserved.
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
 * File: am-utils/include/nfs_common.c
 *
 */
struct nfs_common_args {
  u_long flags;
  u_long acdirmin;
  u_long acdirmax;
  u_long acregmin;
  u_long acregmax;
  u_long timeo;
  u_long retrans;
  u_long rsize;
  u_long wsize;
};

#ifdef HAVE_NFS_ARGS_T_ACREGMIN
#define GET_ACREGMIN(nap, a) nap->acregmin = a.acregmin;
#define PUT_ACREGMIN(nap, a) a.acregmin = nap->acregmin;
#else
#define GET_ACREGMIN(nap, a)
#define PUT_ACREGMIN(nap, a)
#endif
#ifdef HAVE_NFS_ARGS_T_ACREGMAX
#define GET_ACREGMAX(nap, a) nap->acregmax = a.acregmax;
#define PUT_ACREGMAX(nap, a) a.acregmax = nap->acregmax;
#else
#define GET_ACREGMAX(nap, a)
#define PUT_ACREGMAX(nap, a)
#endif

#ifdef HAVE_NFS_ARGS_T_ACDIRMIN
#define GET_ACDIRMIN(nap, a) nap->acdirmin = a.acdirmin;
#define PUT_ACDIRMIN(nap, a) a.acdirmin = nap->acdirmin;
#else
#define GET_ACDIRMIN(nap, a)
#define PUT_ACDIRMIN(nap, a)
#endif
#ifdef HAVE_NFS_ARGS_T_ACDIRMAX
#define GET_ACDIRMAX(nap, a) nap->acdirmax = a.acdirmax;
#define PUT_ACDIRMAX(nap, a) a.acdirmax = nap->acdirmax;
#else
#define GET_ACDIRMAX(nap, a)
#define PUT_ACDIRMAX(nap, a)
#endif

#define get_nfs_common_args(nap, a) \
  do { \
    nap->flags = a.flags; \
    GET_ACREGMIN(nap, a) \
    GET_ACREGMAX(nap, a) \
    GET_ACDIRMIN(nap, a) \
    GET_ACDIRMAX(nap, a) \
    nap->timeo = a.timeo; \
    nap->retrans = a.retrans; \
    nap->rsize = a.rsize; \
    nap->wsize = a.wsize; \
  } while (/*CONSTCOND*/0)

#define put_nfs_common_args(nap, a) \
  do { \
    a.flags = nap->flags; \
    PUT_ACREGMIN(nap, a) \
    PUT_ACREGMAX(nap, a) \
    PUT_ACDIRMIN(nap, a) \
    PUT_ACDIRMAX(nap, a) \
    a.timeo = nap->timeo; \
    a.retrans = nap->retrans; \
    a.rsize = nap->rsize; \
    a.wsize = nap->wsize; \
  } while (/*CONSTCOND*/0)
