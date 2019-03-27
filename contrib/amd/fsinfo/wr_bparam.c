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
 * File: am-utils/fsinfo/wr_bparam.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>


/*
 * Write a host/path in NFS format
 */
static int
write_nfsname(FILE *ef, fsmount *fp, char *hn)
{
  int errors = 0;
  char *h = xstrdup(fp->f_ref->m_dk->d_host->h_hostname);

 domain_strip(h, hn);
  fprintf(ef, "%s:%s", h, fp->f_volname);
  XFREE(h);
  return errors;
}


/*
 * Write a bootparams entry for a host
 */
static int
write_boot_info(FILE *ef, host *hp)
{
  int errors = 0;

  fprintf(ef, "%s\troot=", hp->h_hostname);
  errors += write_nfsname(ef, hp->h_netroot, hp->h_hostname);
  fputs(" swap=", ef);
  errors += write_nfsname(ef, hp->h_netswap, hp->h_hostname);
  fputs("\n", ef);

  return 0;
}


/*
 * Output a bootparams file
 */
int
write_bootparams(qelem *q)
{
  int errors = 0;

  if (bootparams_pref) {
    FILE *ef = pref_open(bootparams_pref, "bootparams", info_hdr, "bootparams");
    if (ef) {
      host *hp;
      ITER(hp, host, q)
      if (hp->h_netroot && hp->h_netswap)
	  errors += write_boot_info(ef, hp);
      errors += pref_close(ef);
    } else {
      errors++;
    }
  }

  return errors;
}
