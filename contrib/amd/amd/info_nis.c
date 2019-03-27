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
 * File: am-utils/amd/info_nis.c
 *
 */

/*
 * Get info from NIS map
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>


/*
 * NIS+ servers in NIS compat mode don't have yp_order()
 *
 *	has_yp_order = 1	NIS server
 *		     = 0	NIS+ server
 *		     = -1	server is down
 */
static int has_yp_order = -1;

/* forward declarations */
int nis_reload(mnt_map *m, char *map, void (*fn) (mnt_map *, char *, char *));
int nis_search(mnt_map *m, char *map, char *key, char **val, time_t *tp);
int nis_init(mnt_map *m, char *map, time_t *tp);
int nis_isup(mnt_map *m, char *map);
int nis_mtime(mnt_map *m, char *map, time_t *tp);

/* typedefs */
typedef void (*nis_callback_fxn_t)(mnt_map *, char *, char *);
#ifndef DEFINED_YPALL_CALLBACK_FXN_T
typedef int (*ypall_callback_fxn_t)();
#endif /* DEFINED_YPALL_CALLBACK_FXN_T */

struct nis_callback_data {
  mnt_map *ncd_m;
  char *ncd_map;
  nis_callback_fxn_t ncd_fn;
};

/* Map to the right version of yp_all */
#ifdef HAVE_BAD_YP_ALL
# define yp_all am_yp_all
static int am_yp_all(char *indomain, char *inmap, struct ypall_callback *incallback);
#endif /* HAVE_BAD_YP_ALL */


/*
 * Figure out the nis domain name
 */
static int
determine_nis_domain(void)
{
  static int nis_not_running = 0;
  char default_domain[YPMAXDOMAIN];

  if (nis_not_running)
    return ENOENT;

  if (getdomainname(default_domain, sizeof(default_domain)) < 0) {
    nis_not_running = 1;
    plog(XLOG_ERROR, "getdomainname: %m");
    return EIO;
  }
  if (!*default_domain) {
    nis_not_running = 1;
    plog(XLOG_WARNING, "NIS domain name is not set.  NIS ignored.");
    return ENOENT;
  }
  gopt.nis_domain = xstrdup(default_domain);

  return 0;
}


/*
 * Callback from yp_all
 */
static int
callback(int status, char *key, int kl, char *val, int vl, char *data)
{
  struct nis_callback_data *ncdp = (struct nis_callback_data *) data;

  if (status == YP_TRUE) {

    /* add to list of maps */
    char *kp = strnsave(key, kl);
    char *vp = strnsave(val, vl);

    (*ncdp->ncd_fn) (ncdp->ncd_m, kp, vp);

    /* we want more ... */
    return FALSE;

  } else {

    /* NOMORE means end of map - otherwise log error */
    if (status != YP_NOMORE) {
      /* check what went wrong */
      int e = ypprot_err(status);

      plog(XLOG_ERROR, "yp enumeration of %s: %s, status=%d, e=%d",
	   ncdp->ncd_map, yperr_string(e), status, e);
    }
    return TRUE;
  }
}


int
nis_reload(mnt_map *m, char *map, void (*fn) (mnt_map *, char *, char *))
{
  int error;
  struct nis_callback_data data;
  struct ypall_callback cbinfo;

  if (!gopt.nis_domain) {
    error = determine_nis_domain();
    if (error)
      return error;
  }
  data.ncd_m = m;
  data.ncd_map = map;
  data.ncd_fn = fn;
  cbinfo.data = (voidp) &data;
  cbinfo.foreach = (ypall_callback_fxn_t) callback;

  plog(XLOG_INFO, "NIS map %s reloading using yp_all", map);
  /*
   * If you are using NIS and your yp_all function is "broken", you have to
   * get it fixed.  The bug in yp_all() is that it does not close a TCP
   * connection to ypserv, and this ypserv runs out of open file descriptors,
   * getting into an infinite loop, thus all YP clients eventually unbind
   * and hang too.
   */
  error = yp_all(gopt.nis_domain, map, &cbinfo);

  if (error)
    plog(XLOG_ERROR, "error grabbing nis map of %s: %s", map, yperr_string(ypprot_err(error)));
  return error;
}


/*
 * Check if NIS is up, so we can determine if to clear the map or not.
 * Test it by checking the yp order.
 * Returns: 0 if NIS is down, 1 if it is up.
 */
int
nis_isup(mnt_map *m, char *map)
{
  YP_ORDER_OUTORDER_TYPE order;
  int error;
  char *master;
  static int last_status = 1;	/* assume up by default */

  switch (has_yp_order) {
  case 1:
    /*
     * NIS server with yp_order
     */
    error = yp_order(gopt.nis_domain, map, &order);
    if (error != 0) {
      plog(XLOG_ERROR,
	   "nis_isup: error getting the order of map %s: %s",
	   map, yperr_string(ypprot_err(error)));
      last_status = 0;
      return 0;			/* NIS is down */
    }
    break;

  case 0:
    /*
     * NIS+ server without yp_order
     */
    error = yp_master(gopt.nis_domain, map, &master);
    if (error != 0) {
      plog(XLOG_ERROR,
	   "nis_isup: error getting the master of map %s: %s",
	   map, yperr_string(ypprot_err(error)));
      last_status = 0;
      return 0;			/* NIS+ is down */
    }
    break;

  default:
    /*
     * server was down
     */
    last_status = 0;
  }

  if (last_status == 0) {	/* reinitialize if was down before */
    time_t dummy;
    error = nis_init(m, map, &dummy);
    if (error)
      return 0;			/* still down */
    plog(XLOG_INFO, "nis_isup: NIS came back up for map %s", map);
    last_status = 1;
  }
  return 1;			/* NIS is up */
}


/*
 * Try to locate a key using NIS.
 */
int
nis_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
  int outlen;
  int res;
  YP_ORDER_OUTORDER_TYPE order;

  /*
   * Make sure domain initialized
   */
  if (!gopt.nis_domain) {
    int error = determine_nis_domain();
    if (error)
      return error;
  }


  switch (has_yp_order) {
  case 1:
    /*
     * NIS server with yp_order
     * Check if map has changed
     */
    if (yp_order(gopt.nis_domain, map, &order))
      return EIO;
    if ((time_t) order > *tp) {
      *tp = (time_t) order;
      return -1;
    }
    break;

  case 0:
    /*
     * NIS+ server without yp_order
     * Check if timeout has expired to invalidate the cache
     */
    order = time(NULL);
    if ((time_t)order - *tp > gopt.am_timeo) {
      *tp = (time_t)order;
      return(-1);
    }
    break;

  default:
    /*
     * server was down
     */
     if (nis_isup(m, map))
       return -1;
     return EIO;
  }

  /*
   * Lookup key
   */
  res = yp_match(gopt.nis_domain, map, key, strlen(key), pval, &outlen);
  if (m->cfm && (m->cfm->cfm_flags & CFM_SUN_MAP_SYNTAX) && res == 0) {
    char *oldval = *pval;
    *pval = sun_entry2amd(key, oldval);
    /* We always need to free the output of the yp_match call. */
    XFREE(oldval);
    if (*pval == NULL)
      return -1;		/* sun2amd parser error */
  }

  /*
   * Do something interesting with the return code
   */
  switch (res) {
  case 0:
    return 0;

  case YPERR_KEY:
    return ENOENT;

  default:
    plog(XLOG_ERROR, "nis_search: %s: %s", map, yperr_string(res));
    return EIO;
  }
}


int
nis_init(mnt_map *m, char *map, time_t *tp)
{
  YP_ORDER_OUTORDER_TYPE order;
  int yp_order_result;
  char *master;

  if (!gopt.nis_domain) {
    int error = determine_nis_domain();
    if (error)
      return error;
  }

  /*
   * To see if the map exists, try to find
   * a master for it.
   */
  yp_order_result = yp_order(gopt.nis_domain, map, &order);
  switch (yp_order_result) {
  case 0:
    /* NIS server found */
    has_yp_order = 1;
    *tp = (time_t) order;
    dlog("NIS master for %s@%s has order %lu", map, gopt.nis_domain, (unsigned long) order);
    break;
  case YPERR_YPERR:
    /* NIS+ server found ! */
    has_yp_order = 0;
    /* try yp_master() instead */
    if (yp_master(gopt.nis_domain, map, &master)) {
      return ENOENT;
    } else {
      dlog("NIS master for %s@%s is a NIS+ server", map, gopt.nis_domain);
      /* Use fake timestamps */
      *tp = time(NULL);
    }
    break;
  default:
    /* server is down */
    has_yp_order = -1;
    return ENOENT;
  }
  return 0;
}


int
nis_mtime(mnt_map *m, char *map, time_t *tp)
{
  return nis_init(m, map, tp);
}


#ifdef HAVE_BAD_YP_ALL
/*
 * If you are using NIS and your yp_all function is "broken", use an
 * alternate code which avoids a bug in yp_all().  The bug in yp_all() is
 * that it does not close a TCP connection to ypserv, and this ypserv runs
 * out of open filedescriptors, getting into an infinite loop, thus all YP
 * clients eventually unbind and hang too.
 *
 * Systems known to be plagued with this bug:
 *	earlier SunOS 4.x
 *	all irix systems (at this time, up to 6.4 was checked)
 *
 * -Erez Zadok <ezk@cs.columbia.edu>
 * -James Tanis <jtt@cs.columbia.edu> */
static int
am_yp_all(char *indomain, char *inmap, struct ypall_callback *incallback)
{
  int i, j;
  char *outkey, *outval;
  int outkeylen, outvallen;
  char *outkey_old;
  int outkeylen_old;

  plog(XLOG_INFO, "NIS map %s reloading using am_yp_all", inmap);

  i = yp_first(indomain, inmap, &outkey, &outkeylen, &outval, &outvallen);
  if (i) {
    plog(XLOG_ERROR, "yp_first() returned error: %s\n", yperr_string(i));
  }
  do {
    j = (incallback->foreach)(YP_TRUE,
			      outkey,
			      outkeylen,
			      outval,
			      outvallen,
			      incallback->data);
    if (j != FALSE)		/* terminate loop */
      break;

    /*
     * We have to manually free all char ** arguments to yp_first/yp_next
     * outval must be freed *before* calling yp_next again, outkey can be
     * freed as outkey_old *after* the call (this saves one call to
     * strnsave).
     */
    XFREE(outval);
    outkey_old = outkey;
    outkeylen_old = outkeylen;
    i = yp_next(indomain,
		inmap,
		outkey_old,
		outkeylen_old,
		&outkey,
		&outkeylen,
		&outval,
		&outvallen);
    XFREE(outkey_old);
  } while (!i);
  if (i) {
    dlog("yp_next() returned error: %s\n", yperr_string(i));
  }
  if (i == YPERR_NOMORE)
    return 0;
  return i;
}
#endif /* HAVE_BAD_YP_ALL */
