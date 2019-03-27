/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
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
 * File: am-utils/fsinfo/fsi_util.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>

/* static variables */
static int show_range = 10;
static int col = 0;
static int total_shown = 0;
static int total_mmm = 8;


static int
col_output(int len)
{
  int wrapped = 0;

  col += len;
  if (col > 77) {
    fputc('\n', stdout);
    col = len;
    wrapped = 1;
  }
  return wrapped;
}


static void
show_total(void)
{
  if (total_mmm != -show_range + 1) {
    char n[8];
    int len;

    if (total_mmm < 0)
      fputc('*', stdout);
    xsnprintf(n, sizeof(n), "%d", total_shown);
    len = strlen(n);
    if (col_output(len))
      fputc(' ', stdout);
    fputs(n, stdout);
    fflush(stdout);
    total_mmm = -show_range;
  }
}


void
col_cleanup(int eoj)
{
  if (verbose < 0)
    return;
  if (eoj) {
    show_total();
    fputs(")]", stdout);
  }
  if (col) {
    fputc('\n', stdout);
    col = 0;
  }
}


/*
 * Lots of ways of reporting errors...
 */
void
error(char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  col_cleanup(0);
  fprintf(stderr, "%s: Error, ", progname);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  errors++;
  va_end(ap);
}


void
lerror(ioloc *l, char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  col_cleanup(0);
  fprintf(stderr, "%s:%d: ", l->i_file, l->i_line);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  errors++;
  va_end(ap);
}


void
lwarning(ioloc *l, char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  col_cleanup(0);
  fprintf(stderr, "%s:%d: ", l->i_file, l->i_line);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}


void
fatal(char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  col_cleanup(1);
  fprintf(stderr, "%s: Fatal, ", progname);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  exit(1);
}


/*
 * Debug log
 */
void
fsi_log(char *fmt, ...)
{
  va_list ap;

  if (verbose > 0) {
    va_start(ap, fmt);
    fputc('#', stdout);
    fprintf(stdout, "%s: ", progname);
    vfprintf(stdout, fmt, ap);
    putc('\n', stdout);
    va_end(ap);
  }
}


void
info_hdr(FILE *ef, char *info)
{
  fprintf(ef, "# *** NOTE: This file contains %s info\n", info);
}


void
gen_hdr(FILE *ef, char *hn)
{
  fprintf(ef, "# *** NOTE: Only for use on %s\n", hn);
}


static void
make_banner(FILE *fp)
{
  time_t t = time((time_t *) NULL);
  char *cp = ctime(&t);

  fprintf(fp,
	  "\
# *** This file was automatically generated -- DO NOT EDIT HERE ***\n\
# \"%s\" run by %s@%s on %s\
#\n\
",
	  progname, username, hostname, cp);
}


void
show_new(char *msg)
{
  if (verbose < 0)
    return;

  total_shown++;
  if (total_mmm > show_range) {
    show_total();
  } else if (total_mmm == 0) {
    fputc('*', stdout);
    fflush(stdout);
    col += 1;
  }
  total_mmm++;
}


void
show_area_being_processed(char *area, int n)
{
  static char *last_area = NULL;

  if (verbose < 0)
    return;
  if (last_area) {
    if (total_shown)
      show_total();
    fputs(")", stdout);
    col += 1;
  }

  if (!last_area || !STREQ(area, last_area)) {
    if (last_area) {
      col_cleanup(0);
      total_shown = 0;
      total_mmm = show_range + 1;
    }
    (void) col_output(strlen(area) + 2);
    fprintf(stdout, "[%s", area);
    last_area = area;
  }

  fputs(" (", stdout);
  col += 2;
  show_range = n;
  total_mmm = n + 1;

  fflush(stdout);
}


/*
 * Open a file with the given prefix and name
 */
FILE *
pref_open(char *pref, char *hn, void (*hdr) (FILE *, char *), char *arg)
{
  char p[MAXPATHLEN];
  FILE *ef;

  xsnprintf(p, sizeof(p), "%s%s", pref, hn);
  fsi_log("Writing %s info for %s to %s", pref, hn, p);
  ef = fopen(p, "w");
  if (ef) {
    (*hdr) (ef, arg);
    make_banner(ef);
  } else {
    error("can't open %s for writing", p);
  }

  return ef;
}


int
pref_close(FILE *fp)
{
  return fclose(fp) == 0;
}


/*
 * Determine where Amd would automount the host/volname pair
 */
void
compute_automount_point(char *buf, size_t l, host *hp, char *vn)
{
  xsnprintf(buf, l, "%s/%s%s", autodir, hp->h_lochost, vn);
}


/*
 * Data constructors..
 */
automount *
new_automount(char *name)
{
  automount *ap = CALLOC(struct automount);

  ap->a_ioloc = current_location();
  ap->a_name = name;
  ap->a_volname = NULL;
  ap->a_mount = NULL;
  ap->a_opts = NULL;
  show_new("automount");
  return ap;
}


auto_tree *
new_auto_tree(char *def, qelem *ap)
{
  auto_tree *tp = CALLOC(struct auto_tree);

  tp->t_ioloc = current_location();
  tp->t_defaults = def;
  tp->t_mount = ap;
  show_new("auto_tree");
  return tp;
}


host *
new_host(void)
{
  host *hp = CALLOC(struct host);

  hp->h_ioloc = current_location();
  hp->h_mask = 0;
  show_new("host");
  return hp;
}


void
set_host(host *hp, int k, char *v)
{
  int m = 1 << k;

  if (hp->h_mask & m) {
    fsi_error("host field \"%s\" already set", host_strings[k]);
    return;
  }
  hp->h_mask |= m;

  switch (k) {

  case HF_HOST:{
      char *p = xstrdup(v);
      dict_ent *de = dict_locate(dict_of_hosts, v);

      if (de)
	fsi_error("duplicate host %s!", v);
      else
	dict_add(dict_of_hosts, v, (char *) hp);
      hp->h_hostname = v;
      domain_strip(p, hostname);
      if (strchr(p, '.') != 0)
	XFREE(p);
      else
	hp->h_lochost = p;
    }
    break;

  case HF_CONFIG:{
      qelem *q;
      qelem *vq = (qelem *) v;

      hp->h_mask &= ~m;
      if (hp->h_config)
	q = hp->h_config;
      else
	q = hp->h_config = new_que();
      ins_que(vq, q->q_back);
    }
    break;

  case HF_ETHER:{
      qelem *q;
      qelem *vq = (qelem *) v;

      hp->h_mask &= ~m;
      if (hp->h_ether)
	q = hp->h_ether;
      else
	q = hp->h_ether = new_que();
      ins_que(vq, q->q_back);
    }
    break;

  case HF_ARCH:
    hp->h_arch = v;
    break;

  case HF_OS:
    hp->h_os = v;
    break;

  case HF_CLUSTER:
    hp->h_cluster = v;
    break;

  default:
    abort();
    break;
  }
}


ether_if *
new_ether_if(void)
{
  ether_if *ep = CALLOC(struct ether_if);

  ep->e_mask = 0;
  ep->e_ioloc = current_location();
  show_new("ether_if");
  return ep;
}


void
set_ether_if(ether_if *ep, int k, char *v)
{
  int m = 1 << k;

  if (ep->e_mask & m) {
    fsi_error("netif field \"%s\" already set", ether_if_strings[k]);
    return;
  }
  ep->e_mask |= m;

  switch (k) {

  case EF_INADDR:{
      ep->e_inaddr.s_addr = inet_addr(v);
      if ((int) ep->e_inaddr.s_addr == (int) INADDR_NONE)
	fsi_error("malformed IP dotted quad: %s", v);
      XFREE(v);
    }
    break;

  case EF_NETMASK:{
      u_long nm = 0;

      if ((sscanf(v, "0x%lx", &nm) == 1 || sscanf(v, "%lx", &nm) == 1) && nm != 0)
	ep->e_netmask = htonl(nm);
      else
	fsi_error("malformed netmask: %s", v);
      XFREE(v);
    }
    break;

  case EF_HWADDR:
    ep->e_hwaddr = v;
    break;

  default:
    abort();
    break;
  }
}


void
set_disk_fs(disk_fs *dp, int k, char *v)
{
  int m = 1 << k;

  if (dp->d_mask & m) {
    fsi_error("fs field \"%s\" already set", disk_fs_strings[k]);
    return;
  }
  dp->d_mask |= m;

  switch (k) {

  case DF_FSTYPE:
    dp->d_fstype = v;
    break;

  case DF_OPTS:
    dp->d_opts = v;
    break;

  case DF_DUMPSET:
    dp->d_dumpset = v;
    break;

  case DF_LOG:
    dp->d_log = v;
    break;

  case DF_PASSNO:
    dp->d_passno = atoi(v);
    XFREE(v);
    break;

  case DF_FREQ:
    dp->d_freq = atoi(v);
    XFREE(v);
    break;

  case DF_MOUNT:
    dp->d_mount = &((fsi_mount *) v)->m_q;
    break;

  default:
    abort();
    break;
  }
}


disk_fs *
new_disk_fs(void)
{
  disk_fs *dp = CALLOC(struct disk_fs);

  dp->d_ioloc = current_location();
  show_new("disk_fs");
  return dp;
}


void
set_mount(fsi_mount *mp, int k, char *v)
{
  int m = 1 << k;

  if (mp->m_mask & m) {
    fsi_error("mount tree field \"%s\" already set", mount_strings[k]);
    return;
  }
  mp->m_mask |= m;

  switch (k) {

  case DM_VOLNAME:
    dict_add(dict_of_volnames, v, (char *) mp);
    mp->m_volname = v;
    break;

  case DM_EXPORTFS:
    mp->m_exportfs = v;
    break;

  case DM_SEL:
    mp->m_sel = v;
    break;

  default:
    abort();
    break;
  }
}


fsi_mount *
new_mount(void)
{
  fsi_mount *fp = CALLOC(struct fsi_mount);

  fp->m_ioloc = current_location();
  show_new("mount");
  return fp;
}


void
set_fsmount(fsmount *fp, int k, char *v)
{
  int m = 1 << k;

  if (fp->f_mask & m) {
    fsi_error("mount field \"%s\" already set", fsmount_strings[k]);
    return;
  }
  fp->f_mask |= m;

  switch (k) {

  case FM_LOCALNAME:
    fp->f_localname = v;
    break;

  case FM_VOLNAME:
    fp->f_volname = v;
    break;

  case FM_FSTYPE:
    fp->f_fstype = v;
    break;

  case FM_OPTS:
    fp->f_opts = v;
    break;

  case FM_FROM:
    fp->f_from = v;
    break;

  case FM_DIRECT:
    break;

  default:
    abort();
    break;
  }
}


fsmount *
new_fsmount(void)
{
  fsmount *fp = CALLOC(struct fsmount);

  fp->f_ioloc = current_location();
  show_new("fsmount");
  return fp;
}


void
init_que(qelem *q)
{
  q->q_forw = q->q_back = q;
}


qelem *
new_que(void)
{
  qelem *q = CALLOC(qelem);

  init_que(q);
  return q;
}


void
ins_que(qelem *elem, qelem *pred)
{
  qelem *p;

  p = pred->q_forw;
  elem->q_back = pred;
  elem->q_forw = p;
  pred->q_forw = elem;
  p->q_back = elem;
}


void
rem_que(qelem *elem)
{
  qelem *p, *p2;

  p = elem->q_forw;
  p2 = elem->q_back;

  p2->q_forw = p;
  p->q_back = p2;
}
