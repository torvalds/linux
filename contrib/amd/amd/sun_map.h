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
 * File: am-utils/amd/sun_map.h
 *
 */

#ifndef _SUN_MAP_H
#define _SUN_MAP_H

/* host */
struct sun_host {
  qelem head;     /* link-list header */
  char *name;     /* hostname */
  int weight;     /* weight given to the host */
};

/* location */
struct sun_location {
  qelem head;                 /* link-list header */
  char *path;                 /* server path */
  struct sun_host *host_list; /* list of hosts */
};

/* sun mount option */
struct sun_opt {
  qelem head;    /* link-list header */
  char *str;     /* option string */
};

/* mount point */
struct sun_mountpt {
  qelem head;                         /* link-list header */
  char *path;                         /* optional mount point path */
  char *fstype;                       /* filesystem type */
  struct sun_opt      *opt_list;      /* list of option strings */
  struct sun_location *location_list; /* list of 'struct s2a_location' */
};

/* automount entry */
struct sun_entry {
  qelem head;                         /* link-list header */
  char *key;                          /* auto map key */
  char *fstype;                       /* filesystem type */
  struct sun_opt      *opt_list;      /* list of mount options */
  struct sun_location *location_list; /* list of mount locations */
  struct sun_mountpt  *mountpt_list;  /* list of mount points */
};

/*
 * automount map file
 *
 * XXX: Only a place holder structure, not implemented yet.
 */
struct sun_map {
  qelem head;                     /* link-list header */
  char *path;                     /* directory path of the map file */
  char *mount_dir;                /* top level mount point for this map */
  int  lookup;                    /* lookup type i.e file, yp, program, etc. */
  int  direct_bool;               /* set true if this map is a direct map */
  struct sun_opt   *opt_list;     /* list of global map options */
  struct sun_opt   *include_list; /* list of included map files  */
  struct sun_entry *entry_list;   /* list of 'struct s2a_entry' */
};

/*
 * master map file
 *
 * XXX: Only a place holder structure, not implemented yet.
 */
struct sun_mmap {
  qelem head;                   /* link-list header */
  struct sun_opt *include_list; /* list of included master maps */
  struct sun_map *amap_list;    /* list of 'struct s2a_amap' */
};

struct sun_list {
  qelem *first;
  qelem *last;
};


/*
 * EXTERNS
 */
extern char *sun_entry2amd(const char *, const char *);
extern struct sun_entry *sun_map_parse_read(const char *);
extern void sun_list_add(struct sun_list *, qelem *);

#endif /* not _SUN_MAP_H */
