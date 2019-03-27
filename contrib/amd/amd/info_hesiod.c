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
 * File: am-utils/amd/info_hesiod.c
 *
 */

/*
 * Get info from Hesiod
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>

#define	HES_PREFIX	"hesiod."
#define	HES_PREFLEN	7

#ifdef HAVE_HESIOD_INIT
/* bsdi3 does not define this extern in any header file */
extern char **hesiod_resolve(void *context, const char *name, const char *type);
extern int hesiod_init(void **context);
static voidp hesiod_context;
#endif /* HAVE_HESIOD_INIT */

/* forward declarations */
int amu_hesiod_init(mnt_map *m, char *map, time_t *tp);
int hesiod_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp);
int hesiod_isup(mnt_map *m, char *map);

/*
 * No easy way to probe the server - check the map name begins with "hesiod."
 * Note: this name includes 'amu_' so as to not conflict with libhesiod's
 * hesiod_init() function.
 */
int
amu_hesiod_init(mnt_map *m, char *map, time_t *tp)
{
  dlog("amu_hesiod_init(%s)", map);
  *tp = 0;

#ifdef HAVE_HESIOD_INIT
  if (!hesiod_context && hesiod_init(&hesiod_context) != 0)
    return ENOENT;
#endif /* HAVE_HESIOD_INIT */

  return NSTREQ(map, HES_PREFIX, HES_PREFLEN) ? 0 : ENOENT;
}


/*
 * Do a Hesiod nameserver call.
 * Modify time is ignored by Hesiod - XXX
 */
int
hesiod_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
  char hes_key[MAXPATHLEN];
  char **rvec;
#ifndef HAVE_HESIOD_INIT
  int error;
#endif /* not HAVE_HESIOD_INIT */

  dlog("hesiod_search(m=%lx, map=%s, key=%s, pval=%lx tp=%lx)",
       (unsigned long) m, map, key, (unsigned long) pval, (unsigned long) tp);

  if (key[0] == '.')
    return ENOENT;

  xsnprintf(hes_key, sizeof(hes_key), "%s.%s", key, map + HES_PREFLEN);

  /*
   * Call the resolver
   */
  dlog("Hesiod base is: %s\n", gopt.hesiod_base);
  dlog("hesiod_search: hes_resolve(%s, %s)", hes_key, gopt.hesiod_base);
  if (amuDebug(D_INFO))
    _res.options |= RES_DEBUG;

#ifdef HAVE_HESIOD_INIT
  /* new style hesiod */
  rvec = hesiod_resolve(hesiod_context, hes_key, gopt.hesiod_base);
#else /* not HAVE_HESIOD_INIT */
  rvec = hes_resolve(hes_key, gopt.hesiod_base);
#endif /* not HAVE_HESIOD_INIT */

  /*
   * If a reply was forthcoming then return
   * it (and free subsequent replies)
   */
  if (rvec && *rvec) {
    if (m->cfm && (m->cfm->cfm_flags & CFM_SUN_MAP_SYNTAX)) {
      *pval = sun_entry2amd(key, *rvec);
      XFREE(*rvec);
    } else
      *pval = *rvec;
    while (*++rvec)
      XFREE(*rvec);
    return 0;
  }

#ifdef HAVE_HESIOD_INIT
  /* new style hesiod */
  return errno;
#else /* not HAVE_HESIOD_INIT */
  /*
   * Otherwise reflect the hesiod error into a Un*x error
   */
  dlog("hesiod_search: Error: %d", hes_error());
  switch (hes_error()) {
  case HES_ER_NOTFOUND:
    error = ENOENT;
    break;
  case HES_ER_CONFIG:
    error = EIO;
    break;
  case HES_ER_NET:
    error = ETIMEDOUT;
    break;
  default:
    error = EINVAL;
    break;
  }
  dlog("hesiod_search: Returning: %d", error);
  return error;
#endif /* not HAVE_HESIOD_INIT */
}


/*
 * Check if Hesiod is up, so we can determine if to clear the map or not.
 * Test it by querying for /defaults.
 * Returns: 0 if Hesiod is down, 1 if it is up.
 */
int
hesiod_isup(mnt_map *m, char *map)
{
  int error;
  char *val;
  time_t mtime;
  static int last_status = 1;	/* assume up by default */

  error = hesiod_search(m, map, "/defaults", &val, &mtime);
  dlog("hesiod_isup(%s): %s", map, strerror(error));
  if (error != 0 && error != ENOENT) {
    plog(XLOG_ERROR,
	 "hesiod_isup: error getting `/defaults' entry in map %s: %m", map);
    last_status = 0;
    return 0;			/* Hesiod is down */
  }
  if (last_status == 0) {	/* if was down before */
    plog(XLOG_INFO, "hesiod_isup: Hesiod came back up for map %s", map);
    last_status = 1;
  }
  return 1;			/* Hesiod is up */
}
