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
 * File: am-utils/amd/amfs_program.c
 *
 */

/*
 * Program file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward definitions */
static char *amfs_program_match(am_opts *fo);
static int amfs_program_mount(am_node *am, mntfs *mf);
static int amfs_program_umount(am_node *am, mntfs *mf);
static int amfs_program_init(mntfs *mf);

/*
 * Ops structure
 */
am_ops amfs_program_ops =
{
  "program",
  amfs_program_match,
  amfs_program_init,
  amfs_program_mount,
  amfs_program_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* amfs_program_readlink */
  0,				/* amfs_program_mounted */
  0,				/* amfs_program_umounted */
  amfs_generic_find_srvr,
  0,				/* amfs_program_get_wchan */
  FS_MKMNT | FS_BACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_PROGRAM_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Execute needs a mount and unmount command.
 */
static char *
amfs_program_match(am_opts *fo)
{
  char *prog;

  if (fo->opt_unmount && fo->opt_umount) {
    plog(XLOG_ERROR, "program: cannot specify both unmount and umount options");
    return 0;
  }
  if (!fo->opt_mount) {
    plog(XLOG_ERROR, "program: must specify mount command");
    return 0;
  }
  if (!fo->opt_unmount && !fo->opt_umount) {
    fo->opt_unmount = str3cat(NULL, UNMOUNT_PROGRAM, " umount ", fo->opt_fs);
    plog(XLOG_INFO, "program: un/umount not specified; using default \"%s\"",
	 fo->opt_unmount);
  }
  prog = strchr(fo->opt_mount, ' ');

  return xstrdup(prog ? prog + 1 : fo->opt_mount);
}


static int
amfs_program_init(mntfs *mf)
{
  /* check if already saved value */
  if (mf->mf_private != NULL)
    return 0;

  if (mf->mf_fo == NULL)
    return 0;

  /* save unmount (or umount) command */
  if (mf->mf_fo->opt_unmount != NULL)
    mf->mf_private = (opaque_t) xstrdup(mf->mf_fo->opt_unmount);
  else
    mf->mf_private = (opaque_t) xstrdup(mf->mf_fo->opt_umount);
  mf->mf_prfree = (void (*)(opaque_t)) free;

  return 0;
}


static int
amfs_program_exec(char *info)
{
  char **xivec;
  int error;

  /*
   * Split copy of command info string
   */
  info = xstrdup(info);
  xivec = strsplit(info, ' ', '\'');

  /*
   * Put stdout to stderr
   */
  (void) fclose(stdout);
  if (!logfp)
    logfp = stderr;		/* initialize before possible first use */
    if (dup(fileno(logfp)) == -1)
      goto out;
  if (fileno(logfp) != fileno(stderr)) {
    (void) fclose(stderr);
    if (dup(fileno(logfp)) == -1)
      goto out;
  }

  /*
   * Try the exec
   */
  if (amuDebug(D_FULL)) {
    char **cp = xivec;
    plog(XLOG_DEBUG, "executing (un)mount command...");
    while (*cp) {
      plog(XLOG_DEBUG, "arg[%ld] = '%s'", (long) (cp - xivec), *cp);
      cp++;
    }
  }

  if (xivec[0] == 0 || xivec[1] == 0) {
    errno = EINVAL;
    plog(XLOG_USER, "1st/2nd args missing to (un)mount program");
  } else {
    (void) execv(xivec[0], xivec + 1);
    error = errno;
    plog(XLOG_ERROR, "exec failed: %m");
    errno = error;
  }

out:
  /*
   * Save error number
   */
  error = errno;

  /*
   * Free allocate memory
   */
  XFREE(info);
  XFREE(xivec);

  /*
   * Return error
   */
  return error;
}


static int
amfs_program_mount(am_node *am, mntfs *mf)
{
  return amfs_program_exec(mf->mf_fo->opt_mount);
}


static int
amfs_program_umount(am_node *am, mntfs *mf)
{
  return amfs_program_exec((char *) mf->mf_private);
}
