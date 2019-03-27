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
 * File: am-utils/amd/info_ndbm.c
 *
 */

/*
 * Get info from NDBM map
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>

/* forward declarations */
int ndbm_init(mnt_map *m, char *map, time_t *tp);
int ndbm_mtime(mnt_map *m, char *map, time_t *tp);
int ndbm_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp);


static int
search_ndbm(mnt_map *m, DBM *db, char *key, char **val)
{
  datum k, v;

  k.dptr = key;
  k.dsize = strlen(key) + 1;
  v = dbm_fetch(db, k);
  if (v.dptr) {
    if (m->cfm && (m->cfm->cfm_flags & CFM_SUN_MAP_SYNTAX))
      *val = sun_entry2amd(key, v.dptr);
    else
      *val = xstrdup(v.dptr);
    return 0;
  }
  return ENOENT;
}


int
ndbm_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
  DBM *db;

  db = dbm_open(map, O_RDONLY, 0);
  if (db) {
    struct stat stb;
    int error;
#ifdef DBM_SUFFIX
    char dbfilename[256];

    xstrlcpy(dbfilename, map, sizeof(dbfilename));
    xstrlcat(dbfilename, DBM_SUFFIX, sizeof(dbfilename));
    error = stat(dbfilename, &stb);
#else /* not DBM_SUFFIX */
    error = fstat(dbm_pagfno(db), &stb);
#endif /* not DBM_SUFFIX */
    if (!error && *tp < stb.st_mtime) {
      *tp = stb.st_mtime;
      error = -1;
    } else {
      error = search_ndbm(m, db, key, pval);
    }
    (void) dbm_close(db);
    return error;
  }
  return errno;
}


int
ndbm_init(mnt_map *m, char *map, time_t *tp)
{
  DBM *db;

  db = dbm_open(map, O_RDONLY, 0);
  if (db) {
    struct stat stb;
    int error;
#ifdef DBM_SUFFIX
    char dbfilename[256];

    xstrlcpy(dbfilename, map, sizeof(dbfilename));
    xstrlcat(dbfilename, DBM_SUFFIX, sizeof(dbfilename));
    error = stat(dbfilename, &stb);
#else /* not DBM_SUFFIX */
    error = fstat(dbm_pagfno(db), &stb);
#endif /* not DBM_SUFFIX */
    if (error < 0)
      *tp = clocktime(NULL);
    else
      *tp = stb.st_mtime;
    dbm_close(db);
    return 0;
  }
  return errno;
}


int
ndbm_mtime(mnt_map *m, char *map, time_t *tp)
{
  return ndbm_init(m,map, tp);
}
