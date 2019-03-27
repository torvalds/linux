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
 * File: am-utils/amd/readdir.c
 *
 */


#include <stdint.h>
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


/****************************************************************************
 *** MACROS                                                               ***
 ****************************************************************************/
#define DOT_DOT_COOKIE	(u_int) 1
#define MAX_CHAIN	2048


/****************************************************************************
 *** FORWARD DEFINITIONS                                                  ***
 ****************************************************************************/
static int key_already_in_chain(char *keyname, const nfsentry *chain);
static nfsentry *make_entry_chain(am_node *mp, const nfsentry *current_chain, int fully_browsable);
static int amfs_readdir_browsable(am_node *mp, nfscookie cookie, nfsdirlist *dp, nfsentry *ep, u_int count, int fully_browsable);

static const u_int dotdotcookie = DOT_DOT_COOKIE;

/****************************************************************************
 *** FUNCTIONS                                                             ***
 ****************************************************************************/
/*
 * Was: NEW_TOPLVL_READDIR
 * Search a chain for an entry with some name.
 * -Erez Zadok <ezk@cs.columbia.edu>
 */
static int
key_already_in_chain(char *keyname, const nfsentry *chain)
{
  const nfsentry *tmpchain = chain;

  while (tmpchain) {
    if (keyname && tmpchain->ne_name && STREQ(keyname, tmpchain->ne_name))
        return 1;
    tmpchain = tmpchain->ne_nextentry;
  }

  return 0;
}


/*
 * Create a chain of entries which are not linked.
 * -Erez Zadok <ezk@cs.columbia.edu>
 */
static nfsentry *
make_entry_chain(am_node *mp, const nfsentry *current_chain, int fully_browsable)
{
  static u_int last_cookie = (u_int) 2;	/* monotonically increasing */
  static nfsentry chain[MAX_CHAIN];
  static int max_entries = MAX_CHAIN;
  char *key;
  int num_entries = 0, i;
  u_int preflen = 0;
  nfsentry *retval = (nfsentry *) NULL;
  mntfs *mf;
  mnt_map *mmp;

  if (!mp) {
    plog(XLOG_DEBUG, "make_entry_chain: mp is (NULL)");
    return retval;
  }
  mf = mp->am_al->al_mnt;
  if (!mf) {
    plog(XLOG_DEBUG, "make_entry_chain: mp->am_al->al_mnt is (NULL)");
    return retval;
  }
  mmp = (mnt_map *) mf->mf_private;
  if (!mmp) {
    plog(XLOG_DEBUG, "make_entry_chain: mp->am_al->al_mnt->mf_private is (NULL)");
    return retval;
  }

  if (mp->am_pref)
    preflen = strlen(mp->am_pref);

  /* iterate over keys */
  for (i = 0; i < NKVHASH; i++) {
    kv *k;
    for (k = mmp->kvhash[i]; k ; k = k->next) {

      /*
       * Skip unwanted entries which are either not real entries or
       * very difficult to interpret (wildcards...)  This test needs
       * lots of improvement.  Any takers?
       */
      key = k->key;
      if (!key)
	continue;

      /* Skip '/defaults' */
      if (STREQ(key, "/defaults"))
	continue;

      /* Skip '*' */
      if (!fully_browsable && strchr(key, '*'))
	continue;

      /*
       * If the map has a prefix-string then check if the key starts with
       * this string, and if it does, skip over this prefix.  If it has a
       * prefix and it doesn't match the start of the key, skip it.
       */
      if (preflen) {
	if (preflen > strlen(key))
	  continue;
	if (!NSTREQ(key, mp->am_pref, preflen))
	  continue;
	key += preflen;
      }

      /* no more '/' are allowed, unless browsable_dirs=full was used */
      if (!fully_browsable && strchr(key, '/'))
	continue;

      /* no duplicates allowed */
      if (key_already_in_chain(key, current_chain))
	continue;

      /* fill in a cell and link the entry */
      if (num_entries >= max_entries) {
	/* out of space */
	plog(XLOG_DEBUG, "make_entry_chain: no more space in chain");
	if (num_entries > 0) {
	  chain[num_entries - 1].ne_nextentry = NULL;
	  retval = &chain[0];
	}
	return retval;
      }

      /* we have space.  put entry in next cell */
      ++last_cookie;
      chain[num_entries].ne_fileid = last_cookie;
      (void)memcpy(chain[num_entries].ne_cookie, &last_cookie,
	sizeof(last_cookie));
      chain[num_entries].ne_name = key;
      if (num_entries < max_entries - 1) {	/* link to next one */
	chain[num_entries].ne_nextentry = &chain[num_entries + 1];
      }
      ++num_entries;
    } /* end of "while (k)" */
  } /* end of "for (i ... NKVHASH ..." */

  /* terminate chain */
  if (num_entries > 0) {
    chain[num_entries - 1].ne_nextentry = NULL;
    retval = &chain[0];
  }

  return retval;
}



/* This one is called only if map is browsable */
static int
amfs_readdir_browsable(am_node *mp, nfscookie cookie, nfsdirlist *dp, nfsentry *ep, u_int count, int fully_browsable)
{
  u_int gen = *(u_int *) (uintptr_t) cookie;
  int chain_length, i;
  static nfsentry *te, *te_next;
  static int j;

  dp->dl_eof = FALSE;		/* assume readdir not done */

  if (amuDebug(D_READDIR))
    plog(XLOG_DEBUG, "amfs_readdir_browsable gen=%u, count=%d",
	 gen, count);

  if (gen == 0) {
    /*
     * In the default instance (which is used to start a search) we return
     * "." and "..".
     *
     * This assumes that the count is big enough to allow both "." and ".."
     * to be returned in a single packet.  If it isn't (which would be
     * fairly unbelievable) then tough.
     */
    dlog("%s: default search", __func__);
    /*
     * Check for enough room.  This is extremely approximate but is more
     * than enough space.  Really need 2 times:
     *      4byte fileid
     *      4byte cookie
     *      4byte name length
     *      4byte name
     * plus the dirlist structure */
    if (count < (2 * (2 * (sizeof(*ep) + sizeof("..") + 4) + sizeof(*dp))))
      return EINVAL;

    /*
     * compute # of entries to send in this chain.
     * heuristics: 128 bytes per entry.
     * This is too much probably, but it seems to work better because
     * of the re-entrant nature of nfs_readdir, and esp. on systems
     * like OpenBSD 2.2.
     */
    chain_length = count / 128;

    /* reset static state counters */
    te = te_next = NULL;

    dp->dl_entries = ep;

    /* construct "." */
    ep[0].ne_fileid = mp->am_gen;
    ep[0].ne_name = ".";
    ep[0].ne_nextentry = &ep[1];
    (void)memset(ep[0].ne_cookie, 0, sizeof(u_int));

    /* construct ".." */
    if (mp->am_parent)
      ep[1].ne_fileid = mp->am_parent->am_gen;
    else
      ep[1].ne_fileid = mp->am_gen;

    ep[1].ne_name = "..";
    ep[1].ne_nextentry = NULL;
    (void)memcpy(ep[1].ne_cookie, &dotdotcookie, sizeof(dotdotcookie));

    /*
     * If map is browsable, call a function make_entry_chain() to construct
     * a linked list of unmounted keys, and return it.  Then link the chain
     * to the regular list.  Get the chain only once, but return
     * chunks of it each time.
     */
    te = make_entry_chain(mp, dp->dl_entries, fully_browsable);
    if (!te)
      return 0;
    if (amuDebug(D_READDIR)) {
      nfsentry *ne;
      for (j = 0, ne = te; ne; ne = ne->ne_nextentry)
	plog(XLOG_DEBUG, "gen1 key %4d \"%s\"", j++, ne->ne_name);
    }

    /* return only "chain_length" entries */
    te_next = te;
    for (i=1; i<chain_length; ++i) {
      te_next = te_next->ne_nextentry;
      if (!te_next)
	break;
    }
    if (te_next) {
      nfsentry *te_saved = te_next->ne_nextentry;
      te_next->ne_nextentry = NULL; /* terminate "te" chain */
      te_next = te_saved;	/* save rest of "te" for next iteration */
      dp->dl_eof = FALSE;	/* tell readdir there's more */
    } else {
      dp->dl_eof = TRUE;	/* tell readdir that's it */
    }
    ep[1].ne_nextentry = te;	/* append this chunk of "te" chain */
    if (amuDebug(D_READDIR)) {
      nfsentry *ne;
      for (j = 0, ne = te; ne; ne = ne->ne_nextentry)
	plog(XLOG_DEBUG, "gen2 key %4d \"%s\"", j++, ne->ne_name);
      for (j = 0, ne = ep; ne; ne = ne->ne_nextentry) {
	u_int cookie;
	(void)memcpy(&cookie, ne->ne_cookie, sizeof(cookie));
	plog(XLOG_DEBUG, "gen2+ key %4d \"%s\" fi=%d ck=%d",
	     j++, ne->ne_name, ne->ne_fileid, cookie);
      }
      plog(XLOG_DEBUG, "EOF is %d", dp->dl_eof);
    }
    return 0;
  } /* end of "if (gen == 0)" statement */

  dlog("%s: real child", __func__);

  if (gen == DOT_DOT_COOKIE) {
    dlog("%s: End of readdir in %s", __func__, mp->am_path);
    dp->dl_eof = TRUE;
    dp->dl_entries = NULL;
    return 0;
  }

  /*
   * If browsable directories, then continue serving readdir() with another
   * chunk of entries, starting from where we left off (when gen was equal
   * to 0).  Once again, assume last chunk served to readdir.
   */
  dp->dl_eof = TRUE;
  dp->dl_entries = ep;

  te = te_next;			/* reset 'te' from last saved te_next */
  if (!te) {			/* another indicator of end of readdir */
    dp->dl_entries = NULL;
    return 0;
  }
  /*
   * compute # of entries to send in this chain.
   * heuristics: 128 bytes per entry.
   */
  chain_length = count / 128;

  /* return only "chain_length" entries */
  for (i = 1; i < chain_length; ++i) {
    te_next = te_next->ne_nextentry;
    if (!te_next)
      break;
  }
  if (te_next) {
    nfsentry *te_saved = te_next->ne_nextentry;
    te_next->ne_nextentry = NULL; /* terminate "te" chain */
    te_next = te_saved;		/* save rest of "te" for next iteration */
    dp->dl_eof = FALSE;		/* tell readdir there's more */
  }
  ep = te;			/* send next chunk of "te" chain */
  dp->dl_entries = ep;
  if (amuDebug(D_READDIR)) {
    nfsentry *ne;
    plog(XLOG_DEBUG, "dl_entries=%p, te_next=%p, dl_eof=%d",
	 dp->dl_entries, te_next, dp->dl_eof);
    for (ne = te; ne; ne = ne->ne_nextentry)
      plog(XLOG_DEBUG, "gen3 key %4d \"%s\"", j++, ne->ne_name);
  }
  return 0;
}

static int
amfs_readdir(am_node *mp, nfscookie cookie, nfsdirlist *dp, nfsentry *ep, u_int count)
{
  u_int gen = *(u_int *) (uintptr_t) cookie;
  am_node *xp;

  dp->dl_eof = FALSE;		/* assume readdir not done */

  /* when gen is 0, we start reading from the beginning of the directory */
  if (gen == 0) {
    /*
     * In the default instance (which is used to start a search) we return
     * "." and "..".
     *
     * This assumes that the count is big enough to allow both "." and ".."
     * to be returned in a single packet.  If it isn't (which would be
     * fairly unbelievable) then tough.
     */
    dlog("%s: default search", __func__);
    /*
     * Check for enough room.  This is extremely approximate but is more
     * than enough space.  Really need 2 times:
     *      4byte fileid
     *      4byte cookie
     *      4byte name length
     *      4byte name
     * plus the dirlist structure */
#define NEEDROOM (2 * (2 * (sizeof(*ep) + sizeof("..") + 4) + sizeof(*dp)))
    if (count < NEEDROOM) {
      dlog("%s: not enough room %u < %zu", __func__, count, NEEDROOM);
      return EINVAL;
    }

    xp = next_nonerror_node(mp->am_child);
    dp->dl_entries = ep;

    /* construct "." */
    ep[0].ne_fileid = mp->am_gen;
    ep[0].ne_name = ".";
    ep[0].ne_nextentry = &ep[1];
    (void)memset(ep[0].ne_cookie, 0, sizeof(u_int));

    /* construct ".." */
    if (mp->am_parent)
      ep[1].ne_fileid = mp->am_parent->am_gen;
    else
      ep[1].ne_fileid = mp->am_gen;
    ep[1].ne_name = "..";
    ep[1].ne_nextentry = NULL;
    (void)memcpy(ep[1].ne_cookie, (xp ? &xp->am_gen : &dotdotcookie),
      sizeof(dotdotcookie));

    if (!xp)
      dp->dl_eof = TRUE;	/* by default assume readdir done */

    if (amuDebug(D_READDIR)) {
      nfsentry *ne;
      int j;
      for (j = 0, ne = ep; ne; ne = ne->ne_nextentry) {
	u_int cookie;
	(void)memcpy(&cookie, ne->ne_cookie, sizeof(cookie));
	plog(XLOG_DEBUG, "gen1 key %4d \"%s\" fi=%d ck=%d",
	     j++, ne->ne_name, ne->ne_fileid, cookie);
      }
    }
    return 0;
  }
  dlog("%s: real child", __func__);

  if (gen == DOT_DOT_COOKIE) {
    dlog("%s: End of readdir in %s", __func__, mp->am_path);
    dp->dl_eof = TRUE;
    dp->dl_entries = NULL;
    if (amuDebug(D_READDIR))
      plog(XLOG_DEBUG, "end of readdir eof=TRUE, dl_entries=0\n");
    return 0;
  }

  /* non-browsable directories code */
  xp = mp->am_child;
  while (xp && xp->am_gen != gen)
    xp = xp->am_osib;

  if (xp) {
    int nbytes = count / 2;	/* conservative */
    int todo = MAX_READDIR_ENTRIES;

    dp->dl_entries = ep;
    do {
      am_node *xp_next = next_nonerror_node(xp->am_osib);

      if (xp_next) {
	(void)memcpy(ep->ne_cookie, &xp_next->am_gen, sizeof(xp_next->am_gen));
      } else {
	(void)memcpy(ep->ne_cookie, &dotdotcookie, sizeof(dotdotcookie));
	dp->dl_eof = TRUE;
      }

      ep->ne_fileid = xp->am_gen;
      ep->ne_name = xp->am_name;
      nbytes -= sizeof(*ep) + 1;
      if (xp->am_name)
	nbytes -= strlen(xp->am_name);

      xp = xp_next;

      if (nbytes > 0 && !dp->dl_eof && todo > 1) {
	ep->ne_nextentry = ep + 1;
	ep++;
	--todo;
      } else {
	todo = 0;
      }
    } while (todo > 0);

    ep->ne_nextentry = NULL;

    if (amuDebug(D_READDIR)) {
      nfsentry *ne;
      int j;
      for (j=0,ne=ep; ne; ne=ne->ne_nextentry) {
	u_int cookie;
	(void)memcpy(&cookie, ne->ne_cookie, sizeof(cookie));
	plog(XLOG_DEBUG, "gen2 key %4d \"%s\" fi=%d ck=%d",
	     j++, ne->ne_name, ne->ne_fileid, cookie);
      }
    }
    return 0;
  }
  return ESTALE;
}

/*
 * Search a chain for an entry with some name.
 */
static int
key_already_in_chain3(char *keyname, const am_entry3 *chain)
{
  const am_entry3 *tmpchain = chain;

  while (tmpchain) {
    if (keyname && tmpchain->name && STREQ(keyname, tmpchain->name))
        return 1;
    tmpchain = tmpchain->nextentry;
  }

  return 0;
}

/*
 * Create a chain of entries which are not linked.
 */
static am_entry3 *
make_entry_chain3(am_node *mp, const am_entry3 *current_chain, int fully_browsable)
{
  static uint64 last_cookie = (uint64) 2;	/* monotonically increasing */
  static am_entry3 chain[MAX_CHAIN];
  static int max_entries = MAX_CHAIN;
  char *key;
  int num_entries = 0, i;
  u_int preflen = 0;
  am_entry3 *retval = (am_entry3 *) NULL;
  mntfs *mf;
  mnt_map *mmp;

  if (!mp) {
    plog(XLOG_DEBUG, "make_entry_chain3: mp is (NULL)");
    return retval;
  }
  mf = mp->am_al->al_mnt;
  if (!mf) {
    plog(XLOG_DEBUG, "make_entry_chain3: mp->am_al->al_mnt is (NULL)");
    return retval;
  }
  mmp = (mnt_map *) mf->mf_private;
  if (!mmp) {
    plog(XLOG_DEBUG, "make_entry_chain3: mp->am_al->al_mnt->mf_private is (NULL)");
    return retval;
  }

  if (mp->am_pref)
    preflen = strlen(mp->am_pref);

  /* iterate over keys */
  for (i = 0; i < NKVHASH; i++) {
    kv *k;
    for (k = mmp->kvhash[i]; k ; k = k->next) {

      /*
       * Skip unwanted entries which are either not real entries or
       * very difficult to interpret (wildcards...)  This test needs
       * lots of improvement.  Any takers?
       */
      key = k->key;
      if (!key)
	continue;

      /* Skip '/defaults' */
      if (STREQ(key, "/defaults"))
	continue;

      /* Skip '*' */
      if (!fully_browsable && strchr(key, '*'))
	continue;

      /*
       * If the map has a prefix-string then check if the key starts with
       * this string, and if it does, skip over this prefix.  If it has a
       * prefix and it doesn't match the start of the key, skip it.
       */
      if (preflen) {
	if (preflen > strlen(key))
	  continue;
	if (!NSTREQ(key, mp->am_pref, preflen))
	  continue;
	key += preflen;
      }

      /* no more '/' are allowed, unless browsable_dirs=full was used */
      if (!fully_browsable && strchr(key, '/'))
	continue;

      /* no duplicates allowed */
      if (key_already_in_chain3(key, current_chain))
	continue;

      /* fill in a cell and link the entry */
      if (num_entries >= max_entries) {
	/* out of space */
	plog(XLOG_DEBUG, "make_entry_chain3: no more space in chain");
	if (num_entries > 0) {
	  chain[num_entries - 1].nextentry = NULL;
	  retval = &chain[0];
	}
	return retval;
      }

      /* we have space.  put entry in next cell */
      ++last_cookie;
      chain[num_entries].fileid = last_cookie;
      chain[num_entries].cookie = last_cookie;
      chain[num_entries].name = key;
      if (num_entries < max_entries - 1) {	/* link to next one */
	chain[num_entries].nextentry = &chain[num_entries + 1];
      }
      ++num_entries;
    } /* end of "while (k)" */
  } /* end of "for (i ... NKVHASH ..." */

  /* terminate chain */
  if (num_entries > 0) {
    chain[num_entries - 1].nextentry = NULL;
    retval = &chain[0];
  }

  return retval;
}

static size_t needroom3(void)
{
  /*
   * Check for enough room.  This is extremely approximate but should
   * be enough space.  Really need 2 times:
   *      (8byte fileid
   *      8byte cookie
   *      8byte name pointer
   *      8byte next entry addres) = sizeof(am_entry3)
   *      2byte name + 1byte terminator
   * plus the size of the am_dirlist3 structure */
  return ((2 * ((sizeof(am_entry3) + sizeof("..") + 1))) + sizeof(am_dirlist3));
}

/* This one is called only if map is browsable */
static int
amfs_readdir3_browsable(am_node *mp, am_cookie3 cookie,
			am_dirlist3 *dp, am_entry3 *ep, u_int count,
			int fully_browsable)
{
  uint64 gen = *(uint64 *) (uintptr_t) cookie;
  int chain_length, i;
  static am_entry3 *te, *te_next;
  static int j;

  dp->eof = FALSE;		/* assume readdir not done */

  if (amuDebug(D_READDIR))
    plog(XLOG_DEBUG, "amfs_readdir3_browsable gen=%lu, count=%d", (long unsigned) gen, count);

  if (gen == 0) {
    size_t needed = needroom3();
    /*
     * In the default instance (which is used to start a search) we return
     * "." and "..".
     *
     * This assumes that the count is big enough to allow both "." and ".."
     * to be returned in a single packet.  If it isn't (which would be
     * fairly unbelievable) then tough.
     */
    dlog("%s: default search", __func__);

    if (count < needed) {
      dlog("%s: not enough room %u < %zu", __func__, count, needed);
      return EINVAL;
    }

    /*
     * compute # of entries to send in this chain.
     * heuristics: 128 bytes per entry.
     * This is too much probably, but it seems to work better because
     * of the re-entrant nature of nfs_readdir, and esp. on systems
     * like OpenBSD 2.2.
     */
    chain_length = count / 128;

    /* reset static state counters */
    te = te_next = NULL;

    dp->entries = ep;

    /* construct "." */
    ep[0].fileid = mp->am_gen;
    ep[0].name = ".";
    ep[0].nextentry = &ep[1];
    ep[0].cookie = 0;

    /* construct ".." */
    if (mp->am_parent)
      ep[1].fileid = mp->am_parent->am_gen;
    else
      ep[1].fileid = mp->am_gen;

    ep[1].name = "..";
    ep[1].nextentry = NULL;
    ep[1].cookie = dotdotcookie;

    /*
     * If map is browsable, call a function make_entry_chain() to construct
     * a linked list of unmounted keys, and return it.  Then link the chain
     * to the regular list.  Get the chain only once, but return
     * chunks of it each time.
     */
    te = make_entry_chain3(mp, dp->entries, fully_browsable);
    if (!te)
      return 0;
    if (amuDebug(D_READDIR)) {
      am_entry3 *ne;
      for (j = 0, ne = te; ne; ne = ne->ne_nextentry)
	plog(XLOG_DEBUG, "gen1 key %4d \"%s\"", j++, ne->ne_name);
    }

    /* return only "chain_length" entries */
    te_next = te;
    for (i=1; i<chain_length; ++i) {
      te_next = te_next->nextentry;
      if (!te_next)
	break;
    }
    if (te_next) {
      am_entry3 *te_saved = te_next->nextentry;
      te_next->nextentry = NULL; /* terminate "te" chain */
      te_next = te_saved;	 /* save rest of "te" for next iteration */
      dp->eof = FALSE;		 /* tell readdir there's more */
    } else {
      dp->eof = TRUE;		 /* tell readdir that's it */
    }
    ep[1].nextentry = te;	 /* append this chunk of "te" chain */
    if (amuDebug(D_READDIR)) {
      am_entry3 *ne;
      for (j = 0, ne = te; ne; ne = ne->ne_nextentry)
	plog(XLOG_DEBUG, "gen2 key %4d \"%s\"", j++, ne->name);
      for (j = 0, ne = ep; ne; ne = ne->ne_nextentry) {
	plog(XLOG_DEBUG, "gen2+ key %4d \"%s\" fi=%lu ck=%lu",
	     j++, ne->name, (long unsigned) ne->fileid, (long unsigned) ne->cookie);
      }
      plog(XLOG_DEBUG, "EOF is %d", dp->eof);
    }
    return 0;
  } /* end of "if (gen == 0)" statement */

  dlog("%s: real child", __func__);

  if (gen == DOT_DOT_COOKIE) {
    dlog("%s: End of readdir in %s", __func__, mp->am_path);
    dp->eof = TRUE;
    dp->entries = NULL;
    return 0;
  }

  /*
   * If browsable directories, then continue serving readdir() with another
   * chunk of entries, starting from where we left off (when gen was equal
   * to 0).  Once again, assume last chunk served to readdir.
   */
  dp->eof = TRUE;
  dp->entries = ep;

  te = te_next;			/* reset 'te' from last saved te_next */
  if (!te) {			/* another indicator of end of readdir */
    dp->entries = NULL;
    return 0;
  }
  /*
   * compute # of entries to send in this chain.
   * heuristics: 128 bytes per entry.
   */
  chain_length = count / 128;

  /* return only "chain_length" entries */
  for (i = 1; i < chain_length; ++i) {
    te_next = te_next->nextentry;
    if (!te_next)
      break;
  }
  if (te_next) {
    am_entry3 *te_saved = te_next->nextentry;
    te_next->nextentry = NULL; /* terminate "te" chain */
    te_next = te_saved;		/* save rest of "te" for next iteration */
    dp->eof = FALSE;		/* tell readdir there's more */
  }
  ep = te;			/* send next chunk of "te" chain */
  dp->entries = ep;
  if (amuDebug(D_READDIR)) {
    am_entry3 *ne;
    plog(XLOG_DEBUG,
	 "entries=%p, te_next=%p, eof=%d", dp->entries, te_next, dp->eof);
    for (ne = te; ne; ne = ne->nextentry)
      plog(XLOG_DEBUG, "gen3 key %4d \"%s\"", j++, ne->name);
  }
  return 0;
}

static int
amfs_readdir3(am_node *mp, am_cookie3 cookie,
	      am_dirlist3 *dp, am_entry3 *ep, u_int count)
{
  uint64 gen = *(uint64 *) (uintptr_t) cookie;
  am_node *xp;

  if (amuDebug(D_READDIR))
    plog(XLOG_DEBUG, "amfs_readdir3 gen=%lu, count=%d", (long unsigned) gen, count);

  dp->eof = FALSE;		/* assume readdir not done */

  /* when gen is 0, we start reading from the beginning of the directory */
  if (gen == 0) {
    size_t needed = needroom3();
    /*
     * In the default instance (which is used to start a search) we return
     * "." and "..".
     *
     * This assumes that the count is big enough to allow both "." and ".."
     * to be returned in a single packet.  If it isn't (which would be
     * fairly unbelievable) then tough.
     */
    dlog("%s: default search", __func__);

    if (count < needed) {
      dlog("%s: not enough room %u < %zu", __func__, count, needed);
      return EINVAL;
    }

    xp = next_nonerror_node(mp->am_child);
    dp->entries = ep;

    /* construct "." */
    ep[0].fileid = mp->am_gen;
    ep[0].name = ".";
    ep[0].cookie = 0;
    ep[0].nextentry = &ep[1];

    /* construct ".." */
    if (mp->am_parent)
      ep[1].fileid = mp->am_parent->am_gen;
    else
      ep[1].fileid = mp->am_gen;
    ep[1].name = "..";
    ep[1].nextentry = NULL;
    ep[1].cookie = (xp ? xp->am_gen : dotdotcookie);

    if (!xp)
      dp->eof = TRUE;	/* by default assume readdir done */

    if (amuDebug(D_READDIR)) {
      am_entry3 *ne;
      int j;
      for (j = 0, ne = ep; ne; ne = ne->nextentry) {
	plog(XLOG_DEBUG, "gen1 key %4d \"%s\" fi=%lu ck=%lu",
	     j++, ne->name, (long unsigned) ne->fileid, (long unsigned) ne->cookie);
      }
    }
    return 0;
  }
  dlog("%s: real child", __func__);

  if (gen == (uint64) DOT_DOT_COOKIE) {
    dlog("%s: End of readdir in %s", __func__, mp->am_path);
    dp->eof = TRUE;
    dp->entries = NULL;
    if (amuDebug(D_READDIR))
      plog(XLOG_DEBUG, "end of readdir eof=TRUE, dl_entries=0\n");
    return 0;
  }

  /* non-browsable directories code */
  xp = mp->am_child;
  while (xp && xp->am_gen != gen)
    xp = xp->am_osib;

  if (xp) {
    int nbytes = count / 2;	/* conservative */
    int todo = MAX_READDIR_ENTRIES;

    dp->entries = ep;
    do {
      am_node *xp_next = next_nonerror_node(xp->am_osib);

      if (xp_next) {
        ep->cookie = xp_next->am_gen;
      } else {
	ep->cookie = (uint64) dotdotcookie;
	dp->eof = TRUE;
      }

      ep->fileid = xp->am_gen;
      ep->name = xp->am_name;
      nbytes -= sizeof(*ep) + 1;
      if (xp->am_name)
	nbytes -= strlen(xp->am_name);

      xp = xp_next;

      if (nbytes > 0 && !dp->dl_eof && todo > 1) {
	ep->nextentry = ep + 1;
	ep++;
	--todo;
      } else {
	todo = 0;
      }
    } while (todo > 0);

    ep->nextentry = NULL;

    if (amuDebug(D_READDIR)) {
      am_entry3 *ne;
      int j;
      for (j = 0, ne = ep; ne; ne = ne->nextentry) {
	plog(XLOG_DEBUG, "gen2 key %4d \"%s\" fi=%lu ck=%lu",
	     j++, ne->name, (long unsigned) ne->fileid, (long unsigned) ne->cookie);
      }
    }
    return 0;
  }
  return ESTALE;
}

/*
 * This readdir function which call a special version of it that allows
 * browsing if browsable_dirs=yes was set on the map.
 */
int
amfs_generic_readdir(am_node *mp, voidp cookie, voidp dp, voidp ep, u_int count)
{
  int browsable, full;

  /* check if map is browsable */
  browsable = 0;
  if (mp->am_al->al_mnt && mp->am_al->al_mnt->mf_mopts) {
    mntent_t mnt;
    mnt.mnt_opts = mp->am_al->al_mnt->mf_mopts;
    if (amu_hasmntopt(&mnt, "fullybrowsable"))
      browsable = 2;
    else if (amu_hasmntopt(&mnt, "browsable"))
      browsable = 1;
  }
  full = (browsable == 2);

  if (nfs_dispatcher == nfs_program_2) {
    if (browsable)
      return amfs_readdir_browsable(mp, cookie, dp, ep, count, full);
    else
      return amfs_readdir(mp, cookie, dp, ep, count);
  } else {
    if (browsable)
      return amfs_readdir3_browsable(mp, (am_cookie3) (uintptr_t) cookie, dp, ep, count, full);
    else
      return amfs_readdir3(mp, (am_cookie3) (uintptr_t) cookie, dp, ep, count);
  }
}
