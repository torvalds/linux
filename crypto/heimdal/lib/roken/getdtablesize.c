/*
 * Copyright (c) 1995-2001 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include "roken.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#elif defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
getdtablesize(void)
{
  int files = -1;
#if defined(HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
  files = sysconf(_SC_OPEN_MAX);
#else /* !defined(HAVE_SYSCONF) */
#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)
  struct rlimit res;
  if (getrlimit(RLIMIT_NOFILE, &res) == 0)
    files = res.rlim_cur;
#else /* !definded(HAVE_GETRLIMIT) */
#if defined(HAVE_SYSCTL) && defined(CTL_KERN) && defined(KERN_MAXFILES)
  int mib[2];
  size_t len;

  mib[0] = CTL_KERN;
  mib[1] = KERN_MAXFILES;
  len = sizeof(files);
  sysctl(&mib, 2, &files, sizeof(files), NULL, 0);
#endif /* defined(HAVE_SYSCTL) */
#endif /* !definded(HAVE_GETRLIMIT) */
#endif /* !defined(HAVE_SYSCONF) */

#ifdef OPEN_MAX
  if (files < 0)
    files = OPEN_MAX;
#endif

#ifdef NOFILE
  if (files < 0)
    files = NOFILE;
#endif

  return files;
}
