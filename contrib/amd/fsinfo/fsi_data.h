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
 * File: am-utils/fsinfo/fsi_data.h
 *
 */

#ifndef _FSI_DATA_H
#define _FSI_DATA_H

typedef struct auto_tree auto_tree;
typedef struct automount automount;
typedef struct dict dict;
typedef struct dict_data dict_data;
typedef struct dict_ent dict_ent;
typedef struct disk_fs disk_fs;
typedef struct ether_if ether_if;
typedef struct fsmount fsmount;
typedef struct host host;
typedef struct ioloc ioloc;
typedef struct fsi_mount fsi_mount;


/*
 * Automount tree
 */
struct automount {
  qelem a_q;
  ioloc *a_ioloc;
  char *a_name;			/* Automount key */
  char *a_volname;		/* Equivalent volume to be referenced */
  char *a_symlink;		/* Symlink representation */
  char *a_opts;			/* opts for mounting */
  char *a_hardwiredfs;		/* hack to bypass bogus fs definitions */
  qelem *a_mount;		/* Tree representation */
  dict_ent *a_mounted;
};

/*
 * List of automount trees
 */
struct auto_tree {
  qelem t_q;
  ioloc *t_ioloc;
  char *t_defaults;
  qelem *t_mount;
};

/*
 * A host
 */
struct host {
  qelem q;
  int h_mask;
  ioloc *h_ioloc;
  fsmount *h_netroot, *h_netswap;
#define HF_HOST	0
  char *h_hostname;	/* The full name of the host */
  char *h_lochost;	/* The name of the host with local domains stripped */
  char *h_hostpath;	/* The filesystem path to the host (cf
			   compute_hostpath) */
#define	HF_ETHER 1
  qelem *h_ether;
#define	HF_CONFIG 2
  qelem *h_config;
#define	HF_ARCH 3
  char *h_arch;
#define	HF_CLUSTER 4
  char *h_cluster;
#define	HF_OS 5
  char *h_os;
  qelem *h_disk_fs;
  qelem *h_mount;
};

/*
 * An ethernet interface
 */
struct ether_if {
  qelem e_q;
  int e_mask;
  ioloc *e_ioloc;
  char *e_if;
#define	EF_INADDR 0
  struct in_addr e_inaddr;
#define	EF_NETMASK 1
  u_long e_netmask;
#define	EF_HWADDR 2
  char *e_hwaddr;
};

/*
 * Disk filesystem structure.
 *
 * If the DF_* numbers are changed
 * disk_fs_strings in analyze.c will
 * need updating.
 */
struct disk_fs {
  qelem d_q;
  int d_mask;
  ioloc *d_ioloc;
  host *d_host;
  char *d_mountpt;
  char *d_dev;
#define	DF_FSTYPE	0
  char *d_fstype;
#define	DF_OPTS		1
  char *d_opts;
#define	DF_DUMPSET	2
  char *d_dumpset;
#define	DF_PASSNO	3
  int d_passno;
#define	DF_FREQ		4
  int d_freq;
#define	DF_MOUNT	5
  qelem *d_mount;
#define	DF_LOG		6
  char *d_log;
};

#define	DF_REQUIRED	((1<<DF_FSTYPE)|(1<<DF_OPTS)|(1<<DF_PASSNO)|(1<<DF_MOUNT))

/*
 * A mount tree
 */
struct fsi_mount {
  qelem m_q;
  ioloc *m_ioloc;
  int m_mask;
#define	DM_VOLNAME	0
  char *m_volname;
#define	DM_EXPORTFS	1
  char *m_exportfs;
#define	DM_SEL		2
  char *m_sel;
  char *m_name;
  int m_name_len;
  fsi_mount *m_parent;
  disk_fs *m_dk;
  fsi_mount *m_exported;
  qelem *m_mount;
};

/*
 * Additional filesystem mounts
 *
 * If the FM_* numbers are changed
 * disk_fs_strings in analyze.c will
 * need updating.
 */
struct fsmount {
  qelem f_q;
  fsi_mount *f_ref;
  ioloc *f_ioloc;
  int f_mask;
#define	FM_LOCALNAME	0
  char *f_localname;
#define	FM_VOLNAME	1
  char *f_volname;
#define	FM_FSTYPE	2
  char *f_fstype;
#define	FM_OPTS		3
  char *f_opts;
#define	FM_FROM		4
  char *f_from;
#define FM_DIRECT	5
};

#define	FM_REQUIRED	((1<<FM_VOLNAME)|(1<<FM_FSTYPE)|(1<<FM_OPTS)|(1<<FM_FROM)|(1<<FM_LOCALNAME))
#define	FM_NETROOT	0x01
#define	FM_NETSWAP	0x02
#define	FM_NETBOOT	(FM_NETROOT|FM_NETSWAP)

#define	DICTHASH	5
struct dict_ent {
  dict_ent *de_next;
  char *de_key;
  int de_count;
  qelem de_q;
};

/*
 * Dictionaries ...
 */
struct dict_data {
  qelem dd_q;
  char *dd_data;
};

struct dict {
  dict_ent *de[DICTHASH];
};

/*
 * Source text location for error reports
 */
struct ioloc {
  int i_line;
  char *i_file;
};
#endif /* not _FSI_DATA_H */
