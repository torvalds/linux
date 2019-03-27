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
 * File: am-utils/amd/info_union.c
 *
 */

/*
 * Get info from the system namespace
 *
 * NOTE: Cannot handle reads back through the automounter.
 * THIS WILL CAUSE A DEADLOCK!
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

#define	UNION_PREFIX	"union:"
#define	UNION_PREFLEN	6

/* forward declarations */
int union_init(mnt_map *m, char *map, time_t *tp);
int union_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp);
int union_reload(mnt_map *m, char *map, void (*fn) (mnt_map *, char *, char *));


/*
 * No way to probe - check the map name begins with "union:"
 */
int
union_init(mnt_map *m, char *map, time_t *tp)
{
  *tp = 0;
  return NSTREQ(map, UNION_PREFIX, UNION_PREFLEN) ? 0 : ENOENT;
}


int
union_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
  char *mapd = xstrdup(map + UNION_PREFLEN);
  char **v = strsplit(mapd, ':', '\"');
  char **p;
  size_t l;

  for (p = v; p[1]; p++) ;
  l = strlen(*p) + 5;
  *pval = xmalloc(l);
  xsnprintf(*pval, l, "fs:=%s", *p);
  XFREE(mapd);
  XFREE(v);
  return 0;
}


int
union_reload(mnt_map *m, char *map, void (*fn) (mnt_map *, char *, char *))
{
  static const char fseq[] = "fs:=";
  char *mapd = xstrdup(map + UNION_PREFLEN);
  char **v = strsplit(mapd, ':', '\"');
  char **dir;

  /*
   * Add fake /defaults entry
   */
  (*fn) (m, xstrdup("/defaults"), xstrdup("type:=link;opts:=nounmount;sublink:=${key}"));

  for (dir = v; *dir; dir++) {
    size_t l;
    struct dirent *dp;

    DIR *dirp = opendir(*dir);
    if (!dirp) {
      plog(XLOG_USER, "Cannot read directory %s: %m", *dir);
      continue;
    }
    l = strlen(*dir) + sizeof(fseq);

    dlog("Reading directory %s...", *dir);
    while ((dp = readdir(dirp))) {
      char *val, *dpname = &dp->d_name[0];
      if (dpname[0] == '.' &&
	  (dpname[1] == '\0' ||
	   (dpname[1] == '.' && dpname[2] == '\0')))
	continue;

      dlog("... gives %s", dp->d_name);
      val = xmalloc(l);
      xsnprintf(val, l, "%s%s", fseq, *dir);
      (*fn) (m, xstrdup(dp->d_name), val);
    }
    closedir(dirp);
  }

  /*
   * Add wildcard entry
   */
  {
    size_t l = strlen(*(dir-1)) + sizeof(fseq);
    char *val = xmalloc(l);

    xsnprintf(val, l, "%s%s", fseq, *(dir-1));
    (*fn) (m, xstrdup("*"), val);
  }
  XFREE(mapd);
  XFREE(v);
  return 0;
}
