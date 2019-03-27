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
 * File: am-utils/fsinfo/fsi_analyze.c
 *
 */

/*
 * Analyze filesystem declarations
 *
 * Note: most of this is magic!
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>

char *disk_fs_strings[] =
{
  "fstype", "opts", "dumpset", "passno", "freq", "mount", "log", NULL,
};

char *mount_strings[] =
{
  "volname", "exportfs", NULL,
};

char *fsmount_strings[] =
{
  "as", "volname", "fstype", "opts", "from", NULL,
};

char *host_strings[] =
{
  "host", "netif", "config", "arch", "cluster", "os", NULL,
};

char *ether_if_strings[] =
{
  "inaddr", "netmask", "hwaddr", NULL,
};


/*
 * Strip off the trailing part of a domain
 * to produce a short-form domain relative
 * to the local host domain.
 * Note that this has no effect if the domain
 * names do not have the same number of
 * components.  If that restriction proves
 * to be a problem then the loop needs recoding
 * to skip from right to left and do partial
 * matches along the way -- ie more expensive.
 */
void
domain_strip(char *otherdom, char *localdom)
{
  char *p1, *p2;

  if ((p1 = strchr(otherdom, '.')) &&
      (p2 = strchr(localdom, '.')) &&
      STREQ(p1 + 1, p2 + 1))
    *p1 = '\0';
}


/*
 * Take a little-endian domain name and
 * transform into a big-endian Un*x pathname.
 * For example: kiska.doc.ic -> ic/doc/kiska
 */
static char *
compute_hostpath(char *hn)
{
  char *p = xmalloc(MAXPATHLEN);
  char *d;
  char path[MAXPATHLEN];

  xstrlcpy(p, hn, MAXPATHLEN);
  domain_strip(p, hostname);
  path[0] = '\0';

  do {
    d = strrchr(p, '.');
    if (d) {
      *d = '\0';
      xstrlcat(path, d + 1, sizeof(path));
      xstrlcat(path, "/", sizeof(path));
    } else {
      xstrlcat(path, p, sizeof(path));
    }
  } while (d);

  fsi_log("hostpath of '%s' is '%s'", hn, path);

  xstrlcpy(p, path, MAXPATHLEN);
  return p;
}


static dict_ent *
find_volname(char *nn)
{
  dict_ent *de;
  char *p = xstrdup(nn);
  char *q;

  do {
    fsi_log("Searching for volname %s", p);
    de = dict_locate(dict_of_volnames, p);
    q = strrchr(p, '/');
    if (q)
      *q = '\0';
  } while (!de && q);

  XFREE(p);
  return de;
}


static void
show_required(ioloc *l, int mask, char *info, char *hostname, char *strings[])
{
  int i;
  fsi_log("mask left for %s:%s is %#x", hostname, info, mask);

  for (i = 0; strings[i]; i++)
    if (ISSET(mask, i))
      lerror(l, "%s:%s needs field \"%s\"", hostname, info, strings[i]);
}


/*
 * Check and fill in "exportfs" details.
 * Make sure the m_exported field references
 * the most local node with an "exportfs" entry.
 */
static int
check_exportfs(qelem *q, fsi_mount *e)
{
  fsi_mount *mp;
  int errors = 0;

  ITER(mp, fsi_mount, q) {
    if (ISSET(mp->m_mask, DM_EXPORTFS)) {
      if (e)
	lwarning(mp->m_ioloc, "%s has duplicate exportfs data", mp->m_name);
      mp->m_exported = mp;
      if (!ISSET(mp->m_mask, DM_VOLNAME))
	set_mount(mp, DM_VOLNAME, xstrdup(mp->m_name));
    } else {
      mp->m_exported = e;
    }

    /*
     * Recursively descend the mount tree
     */
    if (mp->m_mount)
      errors += check_exportfs(mp->m_mount, mp->m_exported);

    /*
     * If a volume name has been specified, but this node and none
     * of its parents has been exported, report an error.
     */
    if (ISSET(mp->m_mask, DM_VOLNAME) && !mp->m_exported) {
      lerror(mp->m_ioloc, "%s has a volname but no exportfs data", mp->m_name);
      errors++;
    }
  }

  return errors;
}


static int
analyze_dkmount_tree(qelem *q, fsi_mount *parent, disk_fs *dk)
{
  fsi_mount *mp;
  int errors = 0;

  ITER(mp, fsi_mount, q) {
    fsi_log("Mount %s:", mp->m_name);
    if (parent) {
      char n[MAXPATHLEN];
      xsnprintf(n, sizeof(n), "%s/%s", parent->m_name, mp->m_name);
      if (*mp->m_name == '/')
	lerror(mp->m_ioloc, "sub-directory %s of %s starts with '/'", mp->m_name, parent->m_name);
      else if (STREQ(mp->m_name, "default"))
	lwarning(mp->m_ioloc, "sub-directory of %s is named \"default\"", parent->m_name);
      fsi_log("Changing name %s to %s", mp->m_name, n);
      XFREE(mp->m_name);
      mp->m_name = xstrdup(n);
    }

    mp->m_name_len = strlen(mp->m_name);
    mp->m_parent = parent;
    mp->m_dk = dk;
    if (mp->m_mount)
      analyze_dkmount_tree(mp->m_mount, mp, dk);
  }

  return errors;
}


/*
 * The mount tree is a singleton list
 * containing the top-level mount
 * point for a disk.
 */
static int
analyze_dkmounts(disk_fs *dk, qelem *q)
{
  int errors = 0;
  fsi_mount *mp, *mp2 = NULL;
  int i = 0;

  /*
   * First scan the list of subdirs to make
   * sure there is only one - and remember it
   */
  if (q) {
    ITER(mp, fsi_mount, q) {
      mp2 = mp;
      i++;
    }
  }

  /*
   * Check...
   */
  if (i < 1) {
    lerror(dk->d_ioloc, "%s:%s has no mount point", dk->d_host->h_hostname, dk->d_dev);
    return 1;
  }

  if (i > 1) {
    lerror(dk->d_ioloc, "%s:%s has more than one mount point", dk->d_host->h_hostname, dk->d_dev);
    errors++;
  }

  /*
   * Now see if a default mount point is required
   */
  if (mp2 && STREQ(mp2->m_name, "default")) {
    if (ISSET(mp2->m_mask, DM_VOLNAME)) {
      char nbuf[1024];
      compute_automount_point(nbuf, sizeof(nbuf), dk->d_host, mp2->m_volname);
      XFREE(mp2->m_name);
      mp2->m_name = xstrdup(nbuf);
      fsi_log("%s:%s has default mount on %s", dk->d_host->h_hostname, dk->d_dev, mp2->m_name);
    } else {
      lerror(dk->d_ioloc, "no volname given for %s:%s", dk->d_host->h_hostname, dk->d_dev);
      errors++;
    }
  }

  /*
   * Fill in the disk mount point
   */
  if (!errors && mp2 && mp2->m_name)
    dk->d_mountpt = xstrdup(mp2->m_name);
  else
    dk->d_mountpt = xstrdup("error");

  /*
   * Analyze the mount tree
   */
  errors += analyze_dkmount_tree(q, NULL, dk);

  /*
   * Analyze the export tree
   */
  errors += check_exportfs(q, NULL);

  return errors;
}


static void
fixup_required_disk_info(disk_fs *dp)
{
  /*
   * "fstype"
   */
  if (ISSET(dp->d_mask, DF_FSTYPE)) {
    if (STREQ(dp->d_fstype, "swap")) {

      /*
       * Fixup for a swap device
       */
      if (!ISSET(dp->d_mask, DF_PASSNO)) {
	dp->d_passno = 0;
	BITSET(dp->d_mask, DF_PASSNO);
      } else if (dp->d_freq != 0) {
	lwarning(dp->d_ioloc,
		 "Pass number for %s:%s is non-zero",
		 dp->d_host->h_hostname, dp->d_dev);
      }

      /*
       * "freq"
       */
      if (!ISSET(dp->d_mask, DF_FREQ)) {
	dp->d_freq = 0;
	BITSET(dp->d_mask, DF_FREQ);
      } else if (dp->d_freq != 0) {
	lwarning(dp->d_ioloc,
		 "dump frequency for %s:%s is non-zero",
		 dp->d_host->h_hostname, dp->d_dev);
      }

      /*
       * "opts"
       */
      if (!ISSET(dp->d_mask, DF_OPTS))
	set_disk_fs(dp, DF_OPTS, xstrdup("swap"));

      /*
       * "mount"
       */
      if (!ISSET(dp->d_mask, DF_MOUNT)) {
	qelem *q = new_que();
	fsi_mount *m = new_mount();

	m->m_name = xstrdup("swap");
	m->m_mount = new_que();
	ins_que(&m->m_q, q->q_back);
	dp->d_mount = q;
	BITSET(dp->d_mask, DF_MOUNT);
      } else {
	lerror(dp->d_ioloc, "%s: mount field specified for swap partition", dp->d_host->h_hostname);
      }
    } else if (STREQ(dp->d_fstype, "export")) {

      /*
       * "passno"
       */
      if (!ISSET(dp->d_mask, DF_PASSNO)) {
	dp->d_passno = 0;
	BITSET(dp->d_mask, DF_PASSNO);
      } else if (dp->d_passno != 0) {
	lwarning(dp->d_ioloc,
		 "pass number for %s:%s is non-zero",
		 dp->d_host->h_hostname, dp->d_dev);
      }

      /*
       * "freq"
       */
      if (!ISSET(dp->d_mask, DF_FREQ)) {
	dp->d_freq = 0;
	BITSET(dp->d_mask, DF_FREQ);
      } else if (dp->d_freq != 0) {
	lwarning(dp->d_ioloc,
		 "dump frequency for %s:%s is non-zero",
		 dp->d_host->h_hostname, dp->d_dev);
      }

      /*
       * "opts"
       */
      if (!ISSET(dp->d_mask, DF_OPTS))
	set_disk_fs(dp, DF_OPTS, xstrdup("rw,defaults"));

    }
  }
}


static void
fixup_required_mount_info(fsmount *fp, dict_ent *de)
{
  if (!ISSET(fp->f_mask, FM_FROM)) {
    if (de->de_count != 1) {
      lerror(fp->f_ioloc, "ambiguous mount: %s is a replicated filesystem", fp->f_volname);
    } else {
      dict_data *dd;
      fsi_mount *mp = NULL;
      dd = AM_FIRST(dict_data, &de->de_q);
      mp = (fsi_mount *) dd->dd_data;
      if (!mp)
	abort();
      fp->f_ref = mp;
      set_fsmount(fp, FM_FROM, mp->m_dk->d_host->h_hostname);
      fsi_log("set: %s comes from %s", fp->f_volname, fp->f_from);
    }
  }

  if (!ISSET(fp->f_mask, FM_FSTYPE)) {
    set_fsmount(fp, FM_FSTYPE, xstrdup("nfs"));
    fsi_log("set: fstype is %s", fp->f_fstype);
  }

  if (!ISSET(fp->f_mask, FM_OPTS)) {
    set_fsmount(fp, FM_OPTS, xstrdup("rw,nosuid,grpid,defaults"));
    fsi_log("set: opts are %s", fp->f_opts);
  }

  if (!ISSET(fp->f_mask, FM_LOCALNAME)) {
    if (fp->f_ref) {
      set_fsmount(fp, FM_LOCALNAME, xstrdup(fp->f_volname));
      fsi_log("set: localname is %s", fp->f_localname);
    } else {
      lerror(fp->f_ioloc, "cannot determine localname since volname %s is not uniquely defined", fp->f_volname);
    }
  }
}


/*
 * For each disk on a host
 * analyze the mount information
 * and fill in any derivable
 * details.
 */
static void
analyze_drives(host *hp)
{
  qelem *q = hp->h_disk_fs;
  disk_fs *dp;

  ITER(dp, disk_fs, q) {
    int req;
    fsi_log("Disk %s:", dp->d_dev);
    dp->d_host = hp;
    fixup_required_disk_info(dp);
    req = ~dp->d_mask & DF_REQUIRED;
    if (req)
      show_required(dp->d_ioloc, req, dp->d_dev, hp->h_hostname, disk_fs_strings);
    analyze_dkmounts(dp, dp->d_mount);
  }
}


/*
 * Check that all static mounts make sense and
 * that the source volumes exist.
 */
static void
analyze_mounts(host *hp)
{
  qelem *q = hp->h_mount;
  fsmount *fp;
  int netbootp = 0;

  ITER(fp, fsmount, q) {
    char *p;
    char *nn = xstrdup(fp->f_volname);
    int req;
    dict_ent *de = (dict_ent *) NULL;
    int found = 0;
    int matched = 0;

    if (ISSET(fp->f_mask, FM_DIRECT)) {
      found = 1;
      matched = 1;
    } else
      do {
	p = NULL;
	de = find_volname(nn);
	fsi_log("Mount: %s (trying %s)", fp->f_volname, nn);

	if (de) {
	  found = 1;

	  /*
	   * Check that the from field is really exporting
	   * the filesystem requested.
	   * LBL: If fake mount, then don't care about
	   *      consistency check.
	   */
	  if (ISSET(fp->f_mask, FM_FROM) && !ISSET(fp->f_mask, FM_DIRECT)) {
	    dict_data *dd;
	    fsi_mount *mp2 = NULL;

	    ITER(dd, dict_data, &de->de_q) {
	      fsi_mount *mp = (fsi_mount *) dd->dd_data;

	      if (fp->f_from &&
		  STREQ(mp->m_dk->d_host->h_hostname, fp->f_from)) {
		mp2 = mp;
		break;
	      }
	    }

	    if (mp2) {
	      fp->f_ref = mp2;
	      matched = 1;
	      break;
	    }
	  } else {
	    matched = 1;
	    break;
	  }
	}
	p = strrchr(nn, '/');
	if (p)
	  *p = '\0';
      } while (de && p);
    XFREE(nn);

    if (!found) {
      lerror(fp->f_ioloc, "volname %s unknown", fp->f_volname);
    } else if (matched) {

      if (de)
	fixup_required_mount_info(fp, de);
      req = ~fp->f_mask & FM_REQUIRED;
      if (req) {
	show_required(fp->f_ioloc, req, fp->f_volname, hp->h_hostname,
		      fsmount_strings);
      } else if (STREQ(fp->f_localname, "/")) {
	hp->h_netroot = fp;
	netbootp |= FM_NETROOT;
      } else if (STREQ(fp->f_localname, "swap")) {
	hp->h_netswap = fp;
	netbootp |= FM_NETSWAP;
      }

    } else {
      lerror(fp->f_ioloc, "volname %s not exported from %s", fp->f_volname,
	     fp->f_from ? fp->f_from : "anywhere");
    }
  }

  if (netbootp && (netbootp != FM_NETBOOT))
    lerror(hp->h_ioloc, "network booting requires both root and swap areas");
}


void
analyze_hosts(qelem *q)
{
  host *hp;

  show_area_being_processed("analyze hosts", 5);

  /*
   * Check all drives
   */
  ITER(hp, host, q) {
    fsi_log("disks on host %s", hp->h_hostname);
    show_new("ana-host");
    hp->h_hostpath = compute_hostpath(hp->h_hostname);

    if (hp->h_disk_fs)
      analyze_drives(hp);

  }

  show_area_being_processed("analyze mounts", 5);

  /*
   * Check static mounts
   */
  ITER(hp, host, q) {
    fsi_log("mounts on host %s", hp->h_hostname);
    show_new("ana-mount");
    if (hp->h_mount)
      analyze_mounts(hp);

  }
}


/*
 * Check an automount request
 */
static void
analyze_automount(automount *ap)
{
  dict_ent *de = find_volname(ap->a_volname);

  if (de) {
    ap->a_mounted = de;
  } else {
    if (STREQ(ap->a_volname, ap->a_name))
      lerror(ap->a_ioloc, "unknown volname %s automounted", ap->a_volname);
    else
      lerror(ap->a_ioloc, "unknown volname %s automounted on %s", ap->a_volname, ap->a_name);
  }
}


static void
analyze_automount_tree(qelem *q, char *pref, int lvl)
{
  automount *ap;

  ITER(ap, automount, q) {
    char nname[1024];

    if (lvl > 0 || ap->a_mount)
      if (ap->a_name[1] && strchr(ap->a_name + 1, '/'))
	lerror(ap->a_ioloc, "not allowed '/' in a directory name");
    xsnprintf(nname, sizeof(nname), "%s/%s", pref, ap->a_name);
    XFREE(ap->a_name);
    ap->a_name = xstrdup(nname[1] == '/' ? nname + 1 : nname);
    fsi_log("automount point %s:", ap->a_name);
    show_new("ana-automount");

    if (ap->a_mount) {
      analyze_automount_tree(ap->a_mount, ap->a_name, lvl + 1);
    } else if (ap->a_hardwiredfs) {
      fsi_log("\thardwired from %s to %s", ap->a_volname, ap->a_hardwiredfs);
    } else if (ap->a_volname) {
      fsi_log("\tautomount from %s", ap->a_volname);
      analyze_automount(ap);
    } else if (ap->a_symlink) {
      fsi_log("\tsymlink to %s", ap->a_symlink);
    } else {
      ap->a_volname = xstrdup(ap->a_name);
      fsi_log("\timplicit automount from %s", ap->a_volname);
      analyze_automount(ap);
    }
  }
}


void
analyze_automounts(qelem *q)
{
  auto_tree *tp;

  show_area_being_processed("analyze automount", 5);

  /*
   * q is a list of automounts
   */
  ITER(tp, auto_tree, q)
    analyze_automount_tree(tp->t_mount, "", 0);
}
