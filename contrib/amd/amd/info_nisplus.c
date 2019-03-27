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
 * File: am-utils/amd/info_nisplus.c
 *
 */

/*
 * Get info from NIS+ (version 3) map
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>

#define NISPLUS_KEY "key="
#define NISPLUS_ORGDIR ".org_dir"

struct nis_callback_data {
  mnt_map *ncd_m;
  char *ncd_map;
  void (*ncd_fn)();
};

struct nisplus_search_callback_data {
  nis_name key;
  char *value;
};


static int
nisplus_callback(const nis_name key, const nis_object *value, voidp opaquedata)
{
  char *kp = strnsave(ENTRY_VAL(value, 0), ENTRY_LEN(value, 0));
  char *vp = strnsave(ENTRY_VAL(value, 1), ENTRY_LEN(value, 1));
  struct nis_callback_data *data = (struct nis_callback_data *) opaquedata;

  dlog("NISplus callback for <%s,%s>", kp, vp);

  (*data->ncd_fn) (data->ncd_m, kp, vp);

  /*
   * We want more ...
   */
  return FALSE;
}


int
nisplus_reload(mnt_map *m, char *map, void (*fn) ())
{
  int error = 0;
  struct nis_callback_data data;
  nis_result *result;
  char *org;		/* if map does not have ".org_dir" then append it */
  nis_name map_name;
  size_t l;

  org = strstr(map, NISPLUS_ORGDIR);
  if (org == NULL)
    org = NISPLUS_ORGDIR;
  else
    org = "";

  /* make some room for the NIS map_name */
  l = strlen(map) + sizeof(NISPLUS_ORGDIR);
  map_name = xmalloc(l);
  if (map_name == NULL) {
    plog(XLOG_ERROR, "Unable to create map_name %s: %s",
	 map, strerror(ENOMEM));
    return ENOMEM;
  }
  xsnprintf(map_name, l, "%s%s", map, org);

  data.ncd_m = m;
  data.ncd_map = map_name;
  data.ncd_fn = fn;

  dlog("NISplus reload for %s", map);

  result = nis_list(map_name,
		    EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH,
		    (int (*)()) nisplus_callback,
		    &data);

  /* free off the NIS map_name */
  XFREE(map_name);

  if (result->status != NIS_SUCCESS && result->status != NIS_CBRESULTS)
    error = 1;

  if (error)
    plog(XLOG_ERROR, "error grabbing nisplus map of %s: %s",
	 map,
	 nis_sperrno(result->status));

  nis_freeresult(result);
  return error;
}


static int
nisplus_search_callback(const nis_name key, const nis_object *value, voidp opaquedata)
{
  struct nisplus_search_callback_data *data = (struct nisplus_search_callback_data *) opaquedata;

  dlog("NISplus search callback for <%s>", ENTRY_VAL(value, 0));
  dlog("NISplus search callback value <%s>", ENTRY_VAL(value, 1));

  data->value = strnsave(ENTRY_VAL(value, 1), ENTRY_LEN(value, 1));
  return TRUE;
}


/*
 * Try to locate a key using NIS+.
 */
int
nisplus_search(mnt_map *m, char *map, char *key, char **val, time_t *tp)
{
  nis_result *result;
  int error = 0;
  struct nisplus_search_callback_data data;
  nis_name index;
  char *org;		/* if map does not have ".org_dir" then append it */
  size_t l;

  org = strstr(map, NISPLUS_ORGDIR);
  if (org == NULL)
    org = NISPLUS_ORGDIR;
  else
    org = "";

  /* make some room for the NIS index */
  l = sizeof('[')		/* for opening selection criteria */
    + sizeof(NISPLUS_KEY)
    + strlen(key)
    + sizeof(']')		/* for closing selection criteria */
    + sizeof(',')		/* + 1 for , separator */
    + strlen(map)
    + sizeof(NISPLUS_ORGDIR);
  index = xmalloc(l);
  if (index == NULL) {
    plog(XLOG_ERROR,
	 "Unable to create index %s: %s",
	 map,
	 strerror(ENOMEM));
    return ENOMEM;
  }
  xsnprintf(index, l, "[%s%s],%s%s", NISPLUS_KEY, key, map, org);

  data.key = key;
  data.value = NULL;

  dlog("NISplus search for %s", index);

  result = nis_list(index,
		    EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH,
		    (int (*)()) nisplus_search_callback,
		    &data);

  /* free off the NIS index */
  XFREE(index);

  if (result == NULL) {
    plog(XLOG_ERROR, "nisplus_search: %s: %s", map, strerror(ENOMEM));
    return ENOMEM;
  }

  /*
   * Do something interesting with the return code
   */
  switch (result->status) {
  case NIS_SUCCESS:
  case NIS_CBRESULTS:

    if (data.value == NULL) {
      nis_object *value = result->objects.objects_val;
      dlog("NISplus search found <nothing>");
      dlog("NISplus search for %s: %s(%d)",
	   map, nis_sperrno(result->status), result->status);

      if (value != NULL)
	data.value = strnsave(ENTRY_VAL(value, 1), ENTRY_LEN(value, 1));
    }

    if (m->cfm && (m->cfm->cfm_flags & CFM_SUN_MAP_SYNTAX)) {
      *val = sun_entry2amd(key, data.value);
      XFREE(data.value);	/* strnsave malloc'ed it above */
    } else
      *val = data.value;

    if (*val) {
      error = 0;
      dlog("NISplus search found %s", *val);
    } else {
      error = ENOENT;
      dlog("NISplus search found nothing");
    }

    *tp = 0;
    break;

  case NIS_NOSUCHNAME:
    dlog("NISplus search returned %d", result->status);
    error = ENOENT;
    break;

  default:
    plog(XLOG_ERROR, "nisplus_search: %s: %s", map, nis_sperrno(result->status));
    error = EIO;
    break;
  }
  nis_freeresult(result);

  return error;
}


int
nisplus_init(mnt_map *m, char *map, time_t *tp)
{
  nis_result *result;
  char *org;		/* if map does not have ".org_dir" then append it */
  nis_name map_name;
  int error = 0;
  size_t l;

  org = strstr(map, NISPLUS_ORGDIR);
  if (org == NULL)
    org = NISPLUS_ORGDIR;
  else
    org = "";

  /* make some room for the NIS map_name */
  l = strlen(map) + sizeof(NISPLUS_ORGDIR);
  map_name = xmalloc(l);
  if (map_name == NULL) {
    plog(XLOG_ERROR,
	 "Unable to create map_name %s: %s",
	 map,
	 strerror(ENOMEM));
    return ENOMEM;
  }
  xsnprintf(map_name, l, "%s%s", map, org);

  result = nis_lookup(map_name, (EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH));

  /* free off the NIS map_name */
  XFREE(map_name);

  if (result == NULL) {
    plog(XLOG_ERROR, "NISplus init <%s>: %s", map, strerror(ENOMEM));
    return ENOMEM;
  }

  if (result->status != NIS_SUCCESS) {
    dlog("NISplus init <%s>: %s (%d)",
	 map, nis_sperrno(result->status), result->status);

    error = ENOENT;
  }

  *tp = 0;			/* no time */
  nis_freeresult(result);
  return error;
}


int
nisplus_mtime(mnt_map *m, char *map, time_t *tp)
{
  return nisplus_init(m,map, tp);
}
