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
 * File: am-utils/conf/umount/umount_bsd44.c
 *
 */

/*
 * Unmounting filesystems under BSD 4.4.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>


int
umount_fs(char *mntdir, const char *mnttabname, u_int unmount_flags)
{
  int error;

eintr:
  error = unmount(mntdir, 0);
  if (error < 0)
    error = errno;

  switch (error) {
  case EINVAL:
  case ENOTBLK:
  case ENOENT:
    plog(XLOG_WARNING, "unmount: %s is not mounted", mntdir);
    error = 0;			/* Not really an error */
    break;

  case EINTR:
    /* not sure why this happens, but it does.  ask kirk one day... */
    dlog("%s: unmount: %m", mntdir);
    goto eintr;

#ifdef MNT2_GEN_OPT_FORCE
  case EBUSY:
  case EIO:
  case ESTALE:
    /* caller determines if forced unmounts should be used */
    if (unmount_flags & AMU_UMOUNT_FORCE) {
      error = umount2_fs(mntdir, unmount_flags);
      if (error < 0)
	error = errno;
      else
	return error;
    }
    /* fallthrough */
#endif /* MNT2_GEN_OPT_FORCE */

  default:
    dlog("%s: unmount: %m", mntdir);
    break;
  }

  return error;
}


#ifdef MNT2_GEN_OPT_FORCE
/* force unmount, no questions asked, without touching mnttab file */
int
umount2_fs(const char *mntdir, u_int unmount_flags)
{
  int error = 0;

  if (unmount_flags & AMU_UMOUNT_FORCE) {
    plog(XLOG_INFO, "umount2_fs: trying unmount/forced on %s", mntdir);
    error = unmount(mntdir, MNT2_GEN_OPT_FORCE);
    if (error < 0 && (errno == EINVAL || errno == ENOENT))
      error = 0;		/* ignore EINVAL/ENOENT */
    if (error < 0)
      plog(XLOG_WARNING, "%s: unmount/force: %m", mntdir);
    else
      dlog("%s: unmount/force: OK", mntdir);
  }
  return error;
}
#endif /* MNT2_GEN_OPT_FORCE */
