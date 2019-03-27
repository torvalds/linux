/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
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
 * File: am-utils/amd/srvr_amfs_auto.c
 *
 */

/*
 * Automount FS server ("localhost") modeling
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* globals */

/* statics */
static qelem amfs_auto_srvr_list = {&amfs_auto_srvr_list, &amfs_auto_srvr_list};
static fserver *localhost;


/*
 * Find an nfs server for the local host
 */
fserver *
amfs_generic_find_srvr(mntfs *mf)
{
  fserver *fs = localhost;

  if (!fs) {
    fs = ALLOC(struct fserver);
    fs->fs_refc = 0;
    fs->fs_host = xstrdup("localhost");
    fs->fs_ip = NULL;
    fs->fs_cid = 0;
    fs->fs_pinger = AM_PINGER;
    fs->fs_flags = FSF_VALID | FSF_PING_UNINIT;
    fs->fs_type = "local";
    fs->fs_private = NULL;
    fs->fs_prfree = NULL;

    ins_que(&fs->fs_q, &amfs_auto_srvr_list);

    srvrlog(fs, "starts up");

    localhost = fs;
  }
  fs->fs_refc++;

  return fs;
}


/*****************************************************************************
 *** GENERIC ROUTINES FOLLOW
 *****************************************************************************/

/*
 * Wakeup anything waiting for this server
 */
void
wakeup_srvr(fserver *fs)
{
  fs->fs_flags &= ~FSF_WANT;
  wakeup((voidp) fs);
}


/*
 * Called when final ttl of server has expired
 */
static void
timeout_srvr(voidp v)
{
  fserver *fs = v;

  /*
   * If the reference count is still zero then
   * we are free to remove this node
   */
  if (fs->fs_refc == 0) {
    dlog("Deleting file server %s", fs->fs_host);
    if (fs->fs_flags & FSF_WANT)
      wakeup_srvr(fs);

    /*
     * Remove from queue.
     */
    rem_que(&fs->fs_q);
    /*
     * (Possibly) call the private free routine.
     */
    if (fs->fs_private && fs->fs_prfree)
      (*fs->fs_prfree) (fs->fs_private);

    /*
     * Free the net address
     */
    XFREE(fs->fs_ip);

    /*
     * Free the host name.
     */
    XFREE(fs->fs_host);

    /*
     * Discard the fserver object.
     */
    XFREE(fs);
  }
}


/*
 * Free a file server
 */
void
free_srvr(fserver *fs)
{
  if (--fs->fs_refc == 0) {
    /*
     * The reference count is now zero,
     * so arrange for this node to be
     * removed in AM_TTL seconds if no
     * other mntfs is referencing it.
     */
    int ttl = (FSRV_ERROR(fs) || FSRV_ISDOWN(fs)) ? 19 : AM_TTL;

    dlog("Last hard reference to file server %s - will timeout in %ds", fs->fs_host, ttl);
    if (fs->fs_cid) {
      untimeout(fs->fs_cid);
      /*
       * Turn off pinging - XXX
       */
      fs->fs_flags &= ~FSF_PINGING;
    }

    /*
     * Keep structure lying around for a while
     */
    fs->fs_cid = timeout(ttl, timeout_srvr, (voidp) fs);

    /*
     * Mark the fileserver down and invalid again
     */
    fs->fs_flags &= ~FSF_VALID;
    fs->fs_flags |= FSF_DOWN;
  }
}


/*
 * Make a duplicate fserver reference
 */
fserver *
dup_srvr(fserver *fs)
{
  fs->fs_refc++;
  return fs;
}


/*
 * Log state change
 */
void
srvrlog(fserver *fs, char *state)
{
  plog(XLOG_INFO, "file server %s, type %s, state %s", fs->fs_host, fs->fs_type, state);
}
