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
 * File: am-utils/amd/amfs_union.c
 *
 */

/*
 * Union automounter file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/****************************************************************************
 *** FORWARD DEFINITIONS                                                  ***
 ****************************************************************************/
static int create_amfs_union_node(char *dir, opaque_t arg);
static void amfs_union_mounted(mntfs *mf);


/****************************************************************************
 *** OPS STRUCTURES                                                       ***
 ****************************************************************************/
am_ops amfs_union_ops =
{
  "union",
  amfs_generic_match,
  0,				/* amfs_union_init */
  amfs_toplvl_mount,
  amfs_toplvl_umount,
  amfs_generic_lookup_child,
  amfs_generic_mount_child,
  amfs_generic_readdir,
  0,				/* amfs_union_readlink */
  amfs_union_mounted,
  0,				/* amfs_union_umounted */
  amfs_generic_find_srvr,
  0,				/* amfs_union_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_BACKGROUND | FS_AMQINFO | FS_DIRECTORY,
#ifdef HAVE_FS_AUTOFS
  AUTOFS_UNION_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Create a reference to a union'ed entry
 * XXX: this function may not be used anywhere...
 */
static int
create_amfs_union_node(char *dir, opaque_t arg)
{
  if (!STREQ(dir, "/defaults")) {
    int error = 0;
    am_node *am;
    am = amfs_generic_lookup_child(arg, dir, &error, VLOOK_CREATE);
    if (am && error < 0)
      (void)amfs_generic_mount_child(am, &error);
    if (error > 0) {
      errno = error;		/* XXX */
      plog(XLOG_ERROR, "unionfs: could not mount %s: %m", dir);
    }
    return error;
  }
  return 0;
}


static void
amfs_union_mounted(mntfs *mf)
{
  int index;
  am_node *mp;

  amfs_mkcacheref(mf);

  /*
   * Having made the union mount point,
   * populate all the entries...
   */
  for (mp = get_first_exported_ap(&index);
       mp;
       mp = get_next_exported_ap(&index)) {
    if (mp->am_al->al_mnt == mf) {
      /* return value from create_amfs_union_node is ignored by mapc_keyiter */
      (void) mapc_keyiter((mnt_map *) mp->am_al->al_mnt->mf_private,
			  create_amfs_union_node,
			  mp);
      break;
    }
  }
}
