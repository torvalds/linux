/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 2005 Daniel P. Ottavio
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
 * File: am-utils/amd/sun_map.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>



/*
 * Add a data pointer to the end of the list.
 */
void
sun_list_add(struct sun_list *list, qelem *item)
{
  if (list->last == NULL) {
    list->last = item;
    list->first = item;
    item->q_back = NULL;
  }
  else {
    list->last->q_forw = item;
    item->q_back = list->last;
    list->last = item;
  }

  item->q_forw = NULL;
}


/*
 * Sun2Amd conversion routines
 */

/*
 * AMD entry keywords
 */
#define AMD_OPTS_KW      "addopts:="     /* add entry options */
#define AMD_RHOST_KW     "rhost:="       /* remote host */
#define AMD_RFS_KW       "rfs:="         /* remote file system */
#define AMD_FS_KW        "fs:="          /* local file system */
#define AMD_DEV_KW       "dev:="         /* device */
#define AMD_TYPE_NFS_KW  "type:=nfs;"    /* fs type nfs */
#define AMD_TYPE_AUTO_KW "type:=auto;"   /* fs type auto */
#define AMD_TYPE_CDFS_KW "type:=cdfs;"   /* fs type cd */
#define AMD_MAP_FS_KW    "fs:=${map};"   /* set the mount map as current map */
#define AMD_MAP_PREF_KW  "pref:=${key}/" /* set the mount map as current map */

/*
 * A set of string Sun fstypes.
 */
#define SUN_NFS_TYPE     "nfs"
#define SUN_HSFS_TYPE    "hsfs" /* CD fs */
#define SUN_AUTOFS_TYPE  "autofs"
#define SUN_CACHEFS_TYPE "cachefs"

#define SUN_KEY_SUB      "&"         /* Sun key substitution */

/* a set a Sun variable substitutions for map entries */
#define SUN_ARCH         "$ARCH"     /* host architecture */
#define SUN_CPU          "$CPU"      /* processor type */
#define SUN_HOST         "$HOST"     /* host name */
#define SUN_OSNAME       "$OSNAME"   /* OS name */
#define SUN_OSREL        "$OSREL"    /* OS release */
#define SUN_OSVERS       "$OSVERS"   /* OS version */
#define SUN_NATISA       "$NATISA"   /* native instruction set */

/* a set of Amd variable substitutions */
#define AMD_ARCH         "${arch}"   /* host architecture */
#define AMD_HOST         "${host}"   /* host name */
#define AMD_OSNAME       "${os}"     /* OS name */
#define AMD_OSVER        "${osver}"  /* OS version */


/*
 * Return a copy of src that has all occurrences of 'str' replaced
 * with sub.
 *
 * param src - the original string
 * param str - string that is the replaced with str
 * param sub - string that replaces an occurrences of 'delim'
 *
 * return - new string with str substitutions, NULL on error
 */
static char *
sun_strsub(const char *src, const char *str, const char *sub)
{

  char *retval = NULL, *str_start, *str_end, *src_end;
  size_t total_size, first_half, second_half, sub_size;

  /* assign pointers to the start and end of str */
  if ((str_start = strstr(src, str)) == NULL) {
    return retval;
  }
  str_end = (strlen(str) - 1) + str_start;

  /* assign to the end of the src. */
  src_end = (strlen(src) - 1) + (char*)src;

  /* size from the beginning of src to the start of str */
  first_half = (size_t)(str_start - src);

  /* size from the end of str to the end of src */
  second_half = (size_t)(src_end - str_end);

  sub_size = strlen(sub);

  total_size = (first_half + sub_size + second_half + 1);

  retval = (char*)xmalloc(total_size);
  memset(retval, 0, total_size);

  /*
   * Put together the string such that the first half is copied
   * followed the sub and second half.
   *
   * We use strncpy instead of xstrlcpy because we are intentionally
   * causing truncation and we don't want this to cause errors in the
   * log.
   */
  (void)strncpy(retval, src, first_half);
  (void)strncat(retval, sub, sub_size);
  (void)strncat(retval, str_end + 1, second_half);

  if (strstr(retval, str) != NULL) {
    /*
     * If there is another occurrences of str call this function
     * recursively.
     */
    char* tmp;
    if ((tmp = sun_strsub(retval, str, sub)) != NULL) {
      XFREE(retval);
      retval = tmp;
    }
  }
  return retval;
}


/*
 * Return a new string that is a copy of str, all occurrences of a Sun
 * variable substitutions are replaced by there equivalent Amd
 * substitutions.
 *
 * param str - source string
 *
 * return - A new string with the expansions, NULL if str does not
 * exist in src or error.
 */
static char *
sun_expand2amd(const char *str)
{

  char *retval = NULL, *tmp = NULL, *tmp2 = NULL;
  const char *pos;

  /*
   * Iterator through the string looking for '$' chars.  For each '$'
   * found try to replace it with Sun variable substitutions.  If we
   * find a '$' that is not a substation each of the i.e $blah than
   * each of the replace attempt will fail and we'll move on to the
   * next char.
   */
  tmp = xstrdup(str);
  for (pos = str; *pos != '\0'; pos++) {
    if (*pos != '$') {
      continue;
    }
    if (tmp2 != NULL) {
      XFREE(tmp);
      tmp = tmp2;
    }

    /*
     * If a 'replace' does not return NULL than a variable was
     * successfully substituted.
     */

    /* architecture */
    if ((tmp2 = sun_strsub(tmp, SUN_ARCH, AMD_ARCH)) != NULL) {
      continue;
    }
    /* cpu - there is not POSIX uname for cpu so just use machine */
    if ((tmp2 = sun_strsub(tmp, SUN_CPU, AMD_ARCH)) != NULL) {
      continue;
    }
    /* hostname */
    if ((tmp2 = sun_strsub(tmp, SUN_HOST, AMD_HOST)) != NULL) {
      continue;
    }
    /* os name */
    if ((tmp2 = sun_strsub(tmp, SUN_OSNAME, AMD_OSNAME)) != NULL) {
      continue;
    }
    /*
     * os release - Amd doesn't hava a OS release var just usr os
     * version or now.
     */
    if ((tmp2 = sun_strsub(tmp, SUN_OSREL, AMD_OSVER)) != NULL) {
      continue;
    }
    /* os version */
    if ((tmp2 = sun_strsub(tmp, SUN_OSVERS, AMD_OSVER)) != NULL) {
      continue;
    }
    /* native instruction set - there is no POSIX natisa so just use system */
    if ((tmp2 = sun_strsub(tmp, SUN_NATISA, AMD_ARCH)) != NULL) {
      continue;
    }
  }
  if (tmp2 == NULL) {
    retval = tmp;
  }
  else {
    retval = tmp2;
    XFREE(tmp);
  }

  return retval;
}


/*
 * This is a wrapper function for appending Amd entry information to a
 * buffer.  Any Sun variable substitutions will be converted into Amd
 * equivalents.
 *
 * param dest   - destination buffer
 * param deslen - destination buffer length
 * param key    - entry key, this might be needed for key substitutions
 * param str    - string to append
 */
static void
sun_append_str(char *dest,
	       size_t destlen,
	       const char *key,
	       const char *str)
{
  char *sub = NULL, *sub2 = NULL, *out = NULL;

  /* By default we are going to just write the original string. */
  out = (char*)str;

  /*
   * Resolve variable substitutions in two steps; 1) replace any key
   * map substitutions with the entry key 2) expand any variable
   * substitutions i.e $HOST.
   *
   * Try to replace the key substitution '&'. If this function returns
   * with a new string, one or more key subs. where replaced with the
   * entry key.
   */
  if ((sub = sun_strsub(str, SUN_KEY_SUB, "${key}")) != NULL) {
    out = sub;
    /*
     * Try to convert any variable substitutions. If this function
     * returns a new string one or more var subs where expanded.
     */
    if ((sub2 = sun_expand2amd(sub)) != NULL) {
      out = sub2;
    }
  }
  /*
   * Try to convert any variable substitutions. If this function
   * returns a new string one or more var subs where expanded.
   */
  else if (out != NULL && (sub = sun_expand2amd(out)) != NULL) {
    out = sub;
  }

  if (out != NULL) {
    xstrlcat(dest, out, destlen);
  }
  XFREE(sub);
  XFREE(sub2);
}


/*
 * Convert the list of Sun mount options to Amd mount options.  The
 * result is concatenated to dest.
 *
 * param dest     - destination buffer
 * param destlen  - destination buffer length
 * param key      - automount key
 * param opt_list - list of Sun mount options
 */
static void
sun_opts2amd(char *dest,
	     size_t destlen,
	     const char *key,
	     const struct sun_opt *opt_list)
{
  const struct sun_opt *opt;

  xstrlcat(dest, AMD_OPTS_KW, destlen);

  /* Iterate through each option and append it to the buffer. */
  for(opt = opt_list; opt != NULL; opt = NEXT(struct sun_opt, opt)) {
    sun_append_str(dest, destlen, key, opt->str);
    /* If there are more options add some commas. */
    if (NEXT(struct sun_opt, opt) != NULL) {
      xstrlcat(dest, ",", destlen);
    }
  }
  xstrlcat(dest, ";", destlen);
}


/*
 * Convert the list of Sun mount locations to a list of Amd mount
 * locations.  The result is concatenated to dest.
 *
 * param dest       - destination buffer
 * param destlen    - destination buffer length
 * param key        - automount key
 * param local_list - list of Sun mount locations
 */
static void
sun_locations2amd(char *dest,
		  size_t destlen,
		  const char *key,
		  const struct sun_location *local_list)
{
  const struct sun_location *local;
  const struct sun_host *host;

  for (local = local_list;
       local != NULL;
       local = NEXT(struct sun_location,local)) {
    /*
     * Check to see if the list of hosts is empty.  Some mount types
     * i.e cd-rom may have mount location with no host.
     */
    if (local->host_list != NULL) {
      /* Write each host that belongs to this location. */
      for (host = local->host_list;
	   host != NULL;
	   host = NEXT(struct sun_host, host)) {
	/* set fstype NFS */
	xstrlcat(dest, AMD_TYPE_NFS_KW, destlen);
	/* add rhost key word */
	xstrlcat(dest, AMD_RHOST_KW, destlen);
	/* add host name */
	sun_append_str(dest, destlen, key, host->name);
	xstrlcat(dest, ";", destlen);
	/* add remote fs key word */
	xstrlcat(dest, AMD_RFS_KW, destlen);
	/* add local path */
	sun_append_str(dest, destlen, key, local->path);
	if (NEXT(struct sun_host, host) != NULL) {
	  xstrlcat(dest, ";", destlen);
	  xstrlcat(dest, " ", destlen);
	}
      }
    }
    else {
      /* no host location */
      xstrlcat(dest, AMD_FS_KW, destlen);
      sun_append_str(dest, destlen, key, local->path);
    }
    if (NEXT(struct sun_location, local) != NULL) {
      /* add a space to separate each location */
      xstrlcat(dest, " ", destlen);
    }
  }
}


/*
 * Convert a Sun HSFS mount point to an Amd.  The result is
 * concatenated intp dest.
 *
 * param dest    - destination buffer
 * param destlen - destination buffer length
 * param key     - automount key
 * param s_entry - Sun entry
 */
static void
sun_hsfs2amd(char *dest,
	     size_t destlen,
	     const char *key,
	     const struct sun_entry *s_entry)
{
  /* set fstype CDFS */
  xstrlcat(dest, AMD_TYPE_CDFS_KW, destlen);
  /* set the cdrom device */
  xstrlcat(dest, AMD_DEV_KW, destlen);
  /* XXX: For now just assume that there is only one device. */
  xstrlcat(dest, s_entry->location_list->path, destlen);
}


/*
 * Convert a Sun NFS automount entry to an Amd.  The result is concatenated
 * into dest.
 *
 * param dest    - destination buffer
 * param destlen - destination buffer length
 * param key     - automount key
 * param s_entry - Sun entry
 */
static void
sun_nfs2amd(char *dest,
	    size_t destlen,
	    const char *key,
	    const struct sun_entry *s_entry)
{
  if (s_entry->location_list != NULL) {
    /* write out the list of mountpoint locations */
    sun_locations2amd(dest, destlen, key, s_entry->location_list);
  }
}


/*
 * Convert a Sun multi-mount point entry to an Amd.  This is done
 * using the Amd type auto.  Each auto entry is separated with a \n.
 *
 * param dest    - destination buffer
 * param destlen - destination buffer length
 * param key     - automount key
 * param s_entry - Sun entry
 */
static void
sun_multi2amd(char *dest,
	      size_t destlen,
	      const char *key,
	      const struct sun_entry *s_entry)
{
  const struct sun_mountpt *mountpt;

  /* We need to setup a auto fs Amd automount point. */
  xstrlcat(dest, AMD_TYPE_AUTO_KW, destlen);
  xstrlcat(dest, AMD_MAP_FS_KW, destlen);
  xstrlcat(dest, AMD_MAP_PREF_KW, destlen);

  /* write the mountpts to dest */
  for (mountpt = s_entry->mountpt_list;
       mountpt != NULL;
       mountpt = NEXT(struct sun_mountpt, mountpt)) {
    xstrlcat(dest, "\n", destlen);
    /* write the key */
    xstrlcat(dest, key, destlen);
    /* write the mount path */
    sun_append_str(dest, destlen, key, mountpt->path);
    /* space */
    xstrlcat(dest, " ", destlen);
    /* Write all the host locations for this mount point. */
    sun_locations2amd(dest, destlen, key, mountpt->location_list);
  }
}


/*
 * Convert the sun_entry into an Amd equivalent string.
 *
 * param key     - automount key
 * param s_entry - Sun style automap entry
 *
 * return - Amd entry on succes, NULL on error
 */
char *
sun_entry2amd(const char *key, const char *s_entry_str)
{
  char *retval = NULL;
  char line_buff[INFO_MAX_LINE_LEN];
  int ws;
  struct sun_entry *s_entry = NULL;

  /* The key should not be NULL. */
  if (key == NULL) {
    plog(XLOG_ERROR,"Sun key value was null");
    goto err;
  }
  /* The Sun entry string should never be NULL. */
  if (s_entry_str == NULL) {
    plog(XLOG_ERROR,"Sun entry value was null");
    goto err;
  }

  /* Make sure there are no trailing white spaces or '\n'. */
  xstrlcpy(line_buff, s_entry_str, sizeof(line_buff));
  ws = strlen(line_buff) - 1;
  while (ws >= 0 && (isspace((unsigned char)line_buff[ws]) || line_buff[ws] == '\n')) {
    line_buff[ws--] = '\0';
  }

  /* Parse the sun entry line. */
  s_entry = sun_map_parse_read(line_buff);
  if (s_entry == NULL) {
    plog(XLOG_ERROR,"could not parse Sun style map");
    goto err;
  }

  memset(line_buff, 0, sizeof(line_buff));

  if (s_entry->opt_list != NULL) {
    /* write the mount options to the buffer  */
    sun_opts2amd(line_buff, sizeof(line_buff), key, s_entry->opt_list);
  }

  /* Check if this is a multi-mount entry. */
  if (s_entry->mountpt_list != NULL) {
    /* multi-mount point */
    sun_multi2amd(line_buff, sizeof(line_buff), key, s_entry);
    retval = xstrdup(line_buff);
  }
  else {
    /* single mount point */
    if (s_entry->fstype != NULL) {
      if (NSTREQ(s_entry->fstype, SUN_NFS_TYPE, strlen(SUN_NFS_TYPE))) {
	/* NFS Type */
	sun_nfs2amd(line_buff, sizeof(line_buff), key, s_entry);
	retval = xstrdup(line_buff);
      }
      else if (NSTREQ(s_entry->fstype, SUN_HSFS_TYPE, strlen(SUN_HSFS_TYPE))) {
	/* HSFS Type (CD fs) */
	sun_hsfs2amd(line_buff, sizeof(line_buff), key, s_entry);
	retval = xstrdup(line_buff);
      }
      /*
       * XXX: The following fstypes are not yet supported.
       */
      else if (NSTREQ(s_entry->fstype, SUN_AUTOFS_TYPE, strlen(SUN_AUTOFS_TYPE))) {
	/* AutoFS Type */
	plog(XLOG_ERROR, "Sun fstype %s is currently not supported by Amd.",
	     s_entry->fstype);
	goto err;

      }
      else if (NSTREQ(s_entry->fstype, SUN_CACHEFS_TYPE, strlen(SUN_CACHEFS_TYPE))) {
	/* CacheFS Type */
	plog(XLOG_ERROR, "Sun fstype %s is currently not supported by Amd.",
	     s_entry->fstype);
	goto err;
      }
      else {
	plog(XLOG_ERROR, "Sun fstype %s is currently not supported by Amd.",
	     s_entry->fstype);
	goto err;
      }
    }
    else {
      plog(XLOG_INFO, "No SUN fstype specified defaulting to NFS.");
      sun_nfs2amd(line_buff, sizeof(line_buff), key, s_entry);
      retval = xstrdup(line_buff);
    }
  }

 err:
  XFREE(s_entry);
  return retval;
}
