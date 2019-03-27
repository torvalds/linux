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
 * File: am-utils/fsinfo/wr_exportfs.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>


static int
write_export_info(FILE *ef, qelem *q, int errors)
{
  fsi_mount *mp;

  ITER(mp, fsi_mount, q) {
    if (mp->m_mask & (1 << DM_EXPORTFS))
      fprintf(ef, "%s\t%s\n", mp->m_volname, mp->m_exportfs);
    if (mp->m_mount)
      errors += write_export_info(ef, mp->m_mount, 0);
  }

  return errors;
}


static int
write_dkexports(FILE *ef, qelem *q)
{
  int errors = 0;
  disk_fs *dp;

  ITER(dp, disk_fs, q) {
    if (dp->d_mount)
      errors += write_export_info(ef, dp->d_mount, 0);
  }

  return errors;
}


int
write_exportfs(qelem *q)
{
  int errors = 0;

  if (exportfs_pref) {
    host *hp;

    show_area_being_processed("write exportfs", 5);
    ITER(hp, host, q) {
      if (hp->h_disk_fs) {
	FILE *ef = pref_open(exportfs_pref, hp->h_hostname, gen_hdr, hp->h_hostname);
	if (ef) {
	  show_new(hp->h_hostname);
	  errors += write_dkexports(ef, hp->h_disk_fs);
	  errors += pref_close(ef);
	} else {
	  errors++;
	}
      }
    }
  }

  return errors;
}
