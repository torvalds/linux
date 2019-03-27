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
 * File: am-utils/amd/ops_lustre.c
 *
 */

/*
 * Lustre file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#ifdef HAVE_FS_LUSTRE
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static char *lustre_match(am_opts *fo);
static int lustre_mount(am_node *am, mntfs *mf);
static int lustre_umount(am_node *am, mntfs *mf);

/*
 * Ops structure
 */
am_ops lustre_ops =
{
  "lustre",
  lustre_match,
  0,				/* lustre_init */
  lustre_mount,
  lustre_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* lustre_readlink */
  0,				/* lustre_mounted */
  0,				/* lustre_umounted */
  amfs_generic_find_srvr,
  0,				/* lustre_get_wchan */
  FS_MKMNT | FS_UBACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_LUSTRE_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Lustre needs remote filesystem and host.
 */
static char *
lustre_match(am_opts *fo)
{
  char *xmtab, *cp;
  size_t l;
  char *rhost, *ptr, *remhost;
  struct in_addr addr;

  if (fo->opt_fs && !fo->opt_rfs)
    fo->opt_rfs = fo->opt_fs;
  if (!fo->opt_rfs) {
    plog(XLOG_USER, "lustre: no remote filesystem specified");
    return NULL;
  }
  if (!fo->opt_rhost) {
    plog(XLOG_USER, "lustre: no remote host specified");
    return NULL;
  }

  /*
   * Determine magic cookie to put in mtab
   */
  rhost = xstrdup(fo->opt_rhost);
  remhost = NULL;
  for (ptr = strtok(rhost, ":"); ptr; ptr = strtok(NULL, ":")) {
    char *at = strchr(ptr, '@');
    if (at == NULL) {
      plog(XLOG_USER, "lustre: missing protocol in host `%s'", ptr);
      XFREE(rhost);
      return NULL;
    }
    *at = '\0';
    /*
     * Convert symbolic addresses to numbers that the kernel likes
     */
    if (inet_aton(ptr, &addr) == 0) {
      struct hostent *hp;
      if ((hp = gethostbyname(ptr)) == NULL) {
	plog(XLOG_USER, "lustre: unknown host `%s'", ptr);
	XFREE(rhost);
	return NULL;
      }
      if (hp->h_length != sizeof(addr.s_addr)) {
	plog(XLOG_USER, "lustre: bad address length %zu != %d for %s",
	  sizeof(addr), hp->h_length, ptr);
	XFREE(rhost);
	return NULL;
      }
      memcpy(&addr.s_addr, hp->h_addr, sizeof(addr));
    }
    *at = '@';

    cp = remhost;
    if (remhost)
      remhost = strvcat(cp, ":", inet_ntoa(addr), at, NULL);
    else
      remhost = strvcat(inet_ntoa(addr), at, NULL);
    XFREE(cp);
  }
  if (remhost == NULL) {
    plog(XLOG_USER, "lustre: empty host");
    XFREE(rhost);
    return NULL;
  }

  XFREE(rhost);
  XFREE(fo->opt_rhost);
  fo->opt_rhost = remhost;

  l = strlen(fo->opt_rhost) + strlen(fo->opt_rfs) + 2;
  xmtab = xmalloc(l);
  xsnprintf(xmtab, l, "%s:%s", fo->opt_rhost, fo->opt_rfs);
  dlog("lustre: mounting remote server \"%s\", remote fs \"%s\" on \"%s\"",
       fo->opt_rhost, fo->opt_rfs, fo->opt_fs);


  return xmtab;
}

static int
lustre_mount(am_node *am, mntfs *mf)
{
  mntent_t mnt;
  int genflags, error;
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;

  /*
   * Figure out the name of the file system type.
   */
  MTYPE_TYPE type = MOUNT_TYPE_LUSTRE;

  /*
   * Fill in the mount structure
   */
  memset(&mnt, 0, sizeof(mnt));
  mnt.mnt_dir = mf->mf_mount;
  mnt.mnt_fsname = mf->mf_info;
  mnt.mnt_type = MNTTAB_TYPE_LUSTRE;
  mnt.mnt_opts = mf->mf_mopts;

  genflags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    genflags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */

  /*
   * Call generic mount routine
   */
  error = mount_fs(&mnt, genflags, NULL, 0, type, 0,
      NULL, mnttab_file_name, on_autofs);
  if (error) {
    errno = error;
    plog(XLOG_ERROR, "mount_lustre: %m");
    return error;
  }

  return 0;
}


static int
lustre_umount(am_node *am, mntfs *mf)
{
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;

  return UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
}
#endif
