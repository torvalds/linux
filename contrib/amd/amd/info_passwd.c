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
 * File: am-utils/amd/info_passwd.c
 *
 */

/*
 * Get info from password "file"
 *
 * This is experimental and probably doesn't do what you expect.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

#define	PASSWD_MAP	"/etc/passwd"

/* forward declarations */
int passwd_init(mnt_map *m, char *map, time_t *tp);
int passwd_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp);


/*
 * Nothing to probe - check the map name is PASSWD_MAP.
 */
int
passwd_init(mnt_map *m, char *map, time_t *tp)
{
  *tp = 0;

  /*
   * Recognize the old format "PASSWD_MAP"
   * Uses default return string
   * "type:=nfs;rfs:=/${var0}/${var1};rhost:=${var1};sublink:=${var2};fs:=${autodir}${var3}"
   */
  if (STREQ(map, PASSWD_MAP))
    return 0;
  /*
   * Recognize the new format "PASSWD_MAP:pval-format"
   */
  if (!NSTREQ(map, PASSWD_MAP, sizeof(PASSWD_MAP) - 1))
    return ENOENT;
  if (map[sizeof(PASSWD_MAP)-1] != ':')
    return ENOENT;

  return 0;
}


/*
 * Grab the entry via the getpwname routine
 * Modify time is ignored by passwd - XXX
 */
int
passwd_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
  char *dir = NULL;
  struct passwd *pw;

  if (STREQ(key, "/defaults")) {
    *pval = xstrdup("type:=nfs");
    return 0;
  }
  pw = getpwnam(key);

  if (pw) {
    /*
     * We chop the home directory up as follows:
     * /anydir/dom1/dom2/dom3/user
     *
     * and return
     * rfs:=/anydir/dom3;rhost:=dom3.dom2.dom1;sublink:=user
     * and now have
     * var0:=pw-prefix:=anydir
     * var1:=pw-rhost:=dom3.dom2.dom1
     * var2:=pw-user:=user
     * var3:=pw-home:=/anydir/dom1/dom2/dom3/user
     *
     * This allows cross-domain entries in your passwd file.
     * ... but forget about security!
     */
    char *user;
    char *p, *q;
    char val[MAXPATHLEN];
    char rhost[MAXHOSTNAMELEN];
    dir = xstrdup(pw->pw_dir);

    /*
     * Find user name.  If no / then Invalid...
     */
    user = strrchr(dir, '/');
    if (!user)
      goto enoent;
    *user++ = '\0';

    /*
     * Find start of host "path".  If no / then Invalid...
     */
    p = strchr(dir + 1, '/');
    if (!p)
      goto enoent;
    *p++ = '\0';

    /*
     * At this point, p is dom1/dom2/dom3
     * Copy, backwards, into rhost replacing
     * / with .
     */
    rhost[0] = '\0';
    do {
      q = strrchr(p, '/');
      if (q) {
	xstrlcat(rhost, q + 1, sizeof(rhost));
	xstrlcat(rhost, ".", sizeof(rhost));
	*q = '\0';
      } else {
	xstrlcat(rhost, p, sizeof(rhost));
      }
    } while (q);

    /*
     * Sanity check
     */
    if (*rhost == '\0' || *user == '\0' || *dir == '\0')
      goto enoent;

    /*
     * Make up return string
     */
    q = strchr(rhost, '.');
    if (q)
      *q = '\0';
    p = strchr(map, ':');
    if (p)
      p++;
    else
      p = "type:=nfs;rfs:=/${var0}/${var1};rhost:=${var1};sublink:=${var2};fs:=${autodir}${var3}";
    xsnprintf(val, sizeof(val), "var0:=%s;var1:=%s;var2:=%s;var3:=%s;%s",
	      dir+1, rhost, user, pw->pw_dir, p);
    dlog("passwd_search: map=%s key=%s -> %s", map, key, val);
    if (q)
      *q = '.';
    *pval = xstrdup(val);
    return 0;
  }

enoent:
  XFREE(dir);

  return ENOENT;
}
