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
 * File: am-utils/amd/opts.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/*
 * MACROS:
 */
#define	NLEN	16	/* Length of longest option name (conservative) */
#define S(x) (x) , (sizeof(x)-1)
/*
 * The BUFSPACE macros checks that there is enough space
 * left in the expansion buffer.  If there isn't then we
 * give up completely.  This is done to avoid crashing the
 * automounter itself (which would be a bad thing to do).
 */
#define BUFSPACE(ep, len) (((ep) + (len)) < expbuf+MAXPATHLEN)

/*
 * TYPEDEFS:
 */
typedef int (*IntFuncPtr) (char *);
typedef struct opt_apply opt_apply;
enum vs_opt { SelEQ, SelNE, VarAss };

/*
 * STRUCTURES
 */
struct opt {
  char *name;			/* Name of the option */
  int nlen;			/* Length of option name */
  char **optp;			/* Pointer to option value string */
  char **sel_p;			/* Pointer to selector value string */
  int (*fxn_p)(char *);		/* Pointer to boolean function */
  int case_insensitive;		/* How to do selector comparisons */
};

struct opt_apply {
  char **opt;
  char *val;
};

struct functable {
  char *name;
  IntFuncPtr func;
};

/*
 * FORWARD DEFINITION:
 */
static int f_in_network(char *);
static int f_xhost(char *);
static int f_netgrp(char *);
static int f_netgrpd(char *);
static int f_exists(char *);
static int f_false(char *);
static int f_true(char *);
static inline char *expand_options(char *key);

/*
 * STATICS:
 */
static char NullStr[] = "<NULL>";
static char nullstr[] = "";
static char *opt_dkey = NullStr;
static char *opt_host = nullstr; /* XXX: was the global hostname */
static char *opt_hostd = hostd;
static char *opt_key = nullstr;
static char *opt_keyd = nullstr;
static char *opt_map = nullstr;
static char *opt_path = nullstr;
char uid_str[SIZEOF_UID_STR], gid_str[SIZEOF_GID_STR];
char *opt_uid = uid_str;
char *opt_gid = gid_str;
static char *vars[8];
static char *literal_dollar = "$"; /* ${dollar}: a literal '$' in maps */

/*
 * GLOBALS
 */
static struct am_opts fs_static;      /* copy of the options to play with */


/*
 * Options in some order corresponding to frequency of use so that
 * first-match algorithm is sped up.
 */
static struct opt opt_fields[] = {
  /* Name and length.
	Option str.		Selector str.	boolean fxn.	case sensitive */
  { S("opts"),
       &fs_static.opt_opts,	0,		0, 		FALSE	},
  { S("host"),
	0,			&opt_host,	0,		TRUE	},
  { S("hostd"),
	0,			&opt_hostd,	0,		TRUE	},
  { S("type"),
	&fs_static.opt_type,	0,		0,		FALSE	},
  { S("rhost"),
	&fs_static.opt_rhost,	0,		0,		TRUE	},
  { S("rfs"),
	&fs_static.opt_rfs,	0,		0,		FALSE	},
  { S("fs"),
	&fs_static.opt_fs,	0,		0,		FALSE	},
  { S("key"),
	0,			&opt_key,	0,		FALSE	},
  { S("map"),
	0,			&opt_map,	0,		FALSE	},
  { S("sublink"),
	&fs_static.opt_sublink,	0,		0,		FALSE	},
  { S("arch"),
	0,			&gopt.arch,	0,		TRUE	},
  { S("dev"),
	&fs_static.opt_dev,	0,		0,		FALSE	},
  { S("pref"),
	&fs_static.opt_pref,	0,		0,		FALSE	},
  { S("path"),
	0,			&opt_path,	0,		FALSE	},
  { S("autodir"),
	0,			&gopt.auto_dir,	0,		FALSE	},
  { S("delay"),
	&fs_static.opt_delay,	0,		0,		FALSE	},
  { S("domain"),
	0,			&hostdomain,	0,		TRUE	},
  { S("karch"),
	0,			&gopt.karch,	0,		TRUE	},
  { S("cluster"),
	0,			&gopt.cluster,	0,		TRUE	},
  { S("wire"),
	0,			0,		f_in_network,	TRUE	},
  { S("network"),
	0,			0,		f_in_network,	TRUE	},
  { S("netnumber"),
	0,			0,		f_in_network,	TRUE	},
  { S("byte"),
	0,			&endian,	0,		TRUE	},
  { S("os"),
	0,			&gopt.op_sys,	0,		TRUE	},
  { S("osver"),
	0,			&gopt.op_sys_ver,	0,	TRUE	},
  { S("full_os"),
	0,			&gopt.op_sys_full,	0,	TRUE	},
  { S("vendor"),
	0,			&gopt.op_sys_vendor,	0,	TRUE	},
  { S("remopts"),
	&fs_static.opt_remopts,	0,		0,		FALSE	},
  { S("mount"),
	&fs_static.opt_mount,	0,		0,		FALSE	},
  { S("unmount"),
	&fs_static.opt_unmount,	0,		0,		FALSE	},
  { S("umount"),
	&fs_static.opt_umount,	0,		0,		FALSE	},
  { S("cache"),
	&fs_static.opt_cache,	0,		0,		FALSE	},
  { S("user"),
	&fs_static.opt_user,	0,		0,		FALSE	},
  { S("group"),
	&fs_static.opt_group,	0,		0,		FALSE	},
  { S(".key"),
	0,			&opt_dkey,	0,		FALSE	},
  { S("key."),
	0,			&opt_keyd,	0,		FALSE	},
  { S("maptype"),
	&fs_static.opt_maptype,	0,		0,		FALSE	},
  { S("cachedir"),
	&fs_static.opt_cachedir, 0,		0,		FALSE	},
  { S("addopts"),
	&fs_static.opt_addopts,	0,		0, 		FALSE	},
  { S("uid"),
	0,			&opt_uid,	0,		FALSE	},
  { S("gid"),
	0,			&opt_gid,	0, 		FALSE	},
  { S("mount_type"),
	&fs_static.opt_mount_type, 0,		0,		FALSE	},
  { S("dollar"),
	&literal_dollar,	0,		0,		FALSE	},
  { S("var0"),
	&vars[0],		0,		0,		FALSE	},
  { S("var1"),
	&vars[1],		0,		0,		FALSE	},
  { S("var2"),
	&vars[2],		0,		0,		FALSE	},
  { S("var3"),
	&vars[3],		0,		0,		FALSE	},
  { S("var4"),
	&vars[4],		0,		0,		FALSE	},
  { S("var5"),
	&vars[5],		0,		0,		FALSE	},
  { S("var6"),
	&vars[6],		0,		0,		FALSE	},
  { S("var7"),
	&vars[7],		0,		0,		FALSE	},
  { 0, 0, 0, 0, 0, FALSE },
};

static struct functable functable[] = {
  { "in_network",	f_in_network },
  { "xhost",		f_xhost },
  { "netgrp",		f_netgrp },
  { "netgrpd",		f_netgrpd },
  { "exists",		f_exists },
  { "false",		f_false },
  { "true",		f_true },
  { 0, 0 },
};

/*
 * Specially expand the remote host name first
 */
static opt_apply rhost_expansion[] =
{
  {&fs_static.opt_rhost, "${host}"},
  {0, 0},
};

/*
 * List of options which need to be expanded
 * Note that the order here _may_ be important.
 */
static opt_apply expansions[] =
{
  {&fs_static.opt_sublink, 0},
  {&fs_static.opt_rfs, "${path}"},
  {&fs_static.opt_fs, "${autodir}/${rhost}${rfs}"},
  {&fs_static.opt_opts, "rw"},
  {&fs_static.opt_remopts, "${opts}"},
  {&fs_static.opt_mount, 0},
  {&fs_static.opt_unmount, 0},
  {&fs_static.opt_umount, 0},
  {&fs_static.opt_cachedir, 0},
  {&fs_static.opt_addopts, 0},
  {0, 0},
};

/*
 * List of options which need to be free'ed before re-use
 */
static opt_apply to_free[] =
{
  {&fs_static.fs_glob, 0},
  {&fs_static.fs_local, 0},
  {&fs_static.fs_mtab, 0},
  {&fs_static.opt_sublink, 0},
  {&fs_static.opt_rfs, 0},
  {&fs_static.opt_fs, 0},
  {&fs_static.opt_rhost, 0},
  {&fs_static.opt_opts, 0},
  {&fs_static.opt_remopts, 0},
  {&fs_static.opt_mount, 0},
  {&fs_static.opt_unmount, 0},
  {&fs_static.opt_umount, 0},
  {&fs_static.opt_cachedir, 0},
  {&fs_static.opt_addopts, 0},
  {&vars[0], 0},
  {&vars[1], 0},
  {&vars[2], 0},
  {&vars[3], 0},
  {&vars[4], 0},
  {&vars[5], 0},
  {&vars[6], 0},
  {&vars[7], 0},
  {0, 0},
};


/*
 * expand backslash escape sequences
 * (escaped slash is handled separately in normalize_slash)
 */
static char
backslash(char **p)
{
  char c;

  if ((*p)[1] == '\0') {
    plog(XLOG_USER, "Empty backslash escape");
    return **p;
  }

  if (**p == '\\') {
    (*p)++;
    switch (**p) {
    case 'g':
      c = '\007';		/* Bell */
      break;
    case 'b':
      c = '\010';		/* Backspace */
      break;
    case 't':
      c = '\011';		/* Horizontal Tab */
      break;
    case 'n':
      c = '\012';		/* New Line */
      break;
    case 'v':
      c = '\013';		/* Vertical Tab */
      break;
    case 'f':
      c = '\014';		/* Form Feed */
      break;
    case 'r':
      c = '\015';		/* Carriage Return */
      break;
    case 'e':
      c = '\033';		/* Escape */
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      {
	int cnt, val, ch;

	for (cnt = 0, val = 0; cnt < 3; cnt++) {
	  ch = *(*p)++;
	  if (ch < '0' || ch > '7') {
	    (*p)--;
	    break;
	  }
	  val = (val << 3) | (ch - '0');
	}

	if ((val & 0xffffff00) != 0)
	  plog(XLOG_USER,
	       "Too large character constant %u\n",
	       val);
	c = (char) val;
	--(*p);
      }
      break;

    default:
      c = **p;
      break;
    }
  } else
    c = **p;

  return c;
}


/*
 * Skip to next option in the string
 */
static char *
opt(char **p)
{
  char *cp = *p;
  char *dp = cp;
  char *s = cp;

top:
  while (*cp && *cp != ';') {
    if (*cp == '"') {
      /*
       * Skip past string
       */
      for (cp++; *cp && *cp != '"'; cp++)
	if (*cp == '\\')
	  *dp++ = backslash(&cp);
	else
	  *dp++ = *cp;
      if (*cp)
	cp++;
    } else {
      *dp++ = *cp++;
    }
  }

  /*
   * Skip past any remaining ';'s
   */
  while (*cp == ';')
    cp++;

  /*
   * If we have a zero length string
   * and there are more fields, then
   * parse the next one.  This allows
   * sequences of empty fields.
   */
  if (*cp && dp == s)
    goto top;

  *dp = '\0';

  *p = cp;
  return s;
}


/*
 * These routines add a new style of selector; function-style boolean
 * operators.  To add new ones, just define functions as in true, false,
 * exists (below) and add them to the functable, above.
 *
 * Usage example: Some people have X11R5 local, some go to a server. I do
 * this:
 *
 *    *       exists(/usr/pkg/${key});type:=link;fs:=/usr/pkg/${key} || \
 *            -type:=nfs;rfs=/usr/pkg/${key} \
 *            rhost:=server1 \
 *            rhost:=server2
 *
 * -Rens Troost <rens@imsi.com>
 */
static IntFuncPtr
functable_lookup(char *key)
{
  struct functable *fp;

  for (fp = functable; fp->name; fp++)
    if (FSTREQ(fp->name, key))
        return (fp->func);
  return (IntFuncPtr) NULL;
}


/*
 * Fill in the global structure fs_static by
 * cracking the string opts.  opts may be
 * scribbled on at will.  Does NOT evaluate options.
 * Returns 0 on error, 1 if no syntax errors were discovered.
 */
static int
split_opts(char *opts, char *mapkey)
{
  char *o = opts;
  char *f;

  /*
   * For each user-specified option
   */
  for (f = opt(&o); *f; f = opt(&o)) {
    struct opt *op;
    char *eq = strchr(f, '=');
    char *opt = NULL;

    if (!eq)
      continue;

    if (*(eq-1) == '!' ||
	eq[1] == '=' ||
	eq[1] == '!') {	/* != or == or =! */
      continue;			/* we don't care about selectors */
    }

    if (*(eq-1) == ':') {	/* := */
      *(eq-1) = '\0';
    } else {
      /* old style assignment */
      eq[0] = '\0';
    }
    opt = eq + 1;

    /*
     * For each recognized option
     */
    for (op = opt_fields; op->name; op++) {
      /*
       * Check whether they match
       */
      if (FSTREQ(op->name, f)) {
	if (op->sel_p) {
	  plog(XLOG_USER, "key %s: Can't assign to a selector (%s)",
	       mapkey, op->name);
	  return 0;
	}
	*op->optp = opt;	/* actual assignment into fs_static */
	break;			/* break out of for loop */
      }	/* end of "if (FSTREQ(op->name, f))" statement  */
    } /* end of "for (op = opt_fields..." statement  */

    if (!op->name)
      plog(XLOG_USER, "key %s: Unrecognized key/option \"%s\"", mapkey, f);
  }

  return 1;
}


/*
 * Just evaluate selectors, which were split by split_opts.
 * Returns 0 on error or no match, 1 if matched.
 */
static int
eval_selectors(char *opts, char *mapkey)
{
  char *o, *old_o;
  char *f;
  int ret = 0;

  o = old_o = xstrdup(opts);

  /*
   * For each user-specified option
   */
  for (f = opt(&o); *f; f = opt(&o)) {
    struct opt *op;
    enum vs_opt vs_opt;
    char *eq = strchr(f, '=');
    char *fx;
    IntFuncPtr func;
    char *opt = NULL;
    char *arg;

    if (!eq) {
      /*
       * No value, is it a function call?
       */
      arg = strchr(f, '(');

      if (!arg || arg[1] == '\0' || arg == f) {
	/*
	 * No, just continue
	 */
	plog(XLOG_USER, "key %s: No value component in \"%s\"", mapkey, f);
	continue;
      }

      /* null-terminate the argument  */
      *arg++ = '\0';
      fx = strchr(arg, ')');
      if (fx == NULL || fx == arg) {
	plog(XLOG_USER, "key %s: Malformed function in \"%s\"", mapkey, f);
	continue;
      }
      *fx = '\0';

      if (f[0] == '!') {
	vs_opt = SelNE;
	f++;
      } else {
	vs_opt = SelEQ;
      }
      /*
       * look up f in functable and pass it arg.
       * func must return 0 on failure, and 1 on success.
       */
      if ((func = functable_lookup(f))) {
	int funok;

	/* this allocates memory, don't forget to free */
	arg = expand_options(arg);
	funok = func(arg);
	XFREE(arg);

	if (vs_opt == SelNE)
	  funok = !funok;
	if (!funok)
	  goto out;

	continue;
      } else {
	plog(XLOG_USER, "key %s: unknown function \"%s\"", mapkey, f);
	goto out;
      }
    } else {
      if (eq[1] == '\0' || eq == f) {
#ifdef notdef
	/* We allow empty assignments */
	plog(XLOG_USER, "key %s: Bad selector \"%s\"", mapkey, f);
#endif
	continue;
      }
    }

    /*
     * Check what type of operation is happening
     * !=, =!  is SelNE
     * == is SelEQ
     * =, := is VarAss
     */
    if (*(eq-1) == '!') {	/* != */
      vs_opt = SelNE;
      *(eq-1) = '\0';
      opt = eq + 1;
    } else if (*(eq-1) == ':') {	/* := */
      continue;
    } else if (eq[1] == '=') {	/* == */
      vs_opt = SelEQ;
      eq[0] = '\0';
      opt = eq + 2;
    } else if (eq[1] == '!') {	/* =! */
      vs_opt = SelNE;
      eq[0] = '\0';
      opt = eq + 2;
    } else {
      /* old style assignment */
      continue;
    }

    /*
     * For each recognized option
     */
    for (op = opt_fields; op->name; op++) {
      /*
       * Check whether they match
       */
      if (FSTREQ(op->name, f)) {
	opt = expand_options(opt);

	if (op->sel_p != NULL) {
	  int selok;
	  if (op->case_insensitive) {
	    selok = STRCEQ(*op->sel_p, opt);
	  } else {
	    selok = STREQ(*op->sel_p, opt);
	  }
	  if (vs_opt == SelNE)
	    selok = !selok;
	  if (!selok) {
	    plog(XLOG_MAP, "key %s: map selector %s (=%s) did not %smatch %s",
		 mapkey,
		 op->name,
		 *op->sel_p,
		 vs_opt == SelNE ? "mis" : "",
		 opt);
	    XFREE(opt);
	    goto out;
	  }
	  XFREE(opt);
	}
	/* check if to apply a function */
	if (op->fxn_p) {
	  int funok;

	  funok = op->fxn_p(opt);
	  if (vs_opt == SelNE)
	    funok = !funok;
	  if (!funok) {
	    plog(XLOG_MAP, "key %s: map function %s did not %smatch %s",
		 mapkey,
		 op->name,
		 vs_opt == SelNE ? "mis" : "",
		 opt);
	    XFREE(opt);
	    goto out;
	  }
	  XFREE(opt);
	}
	break;			/* break out of for loop */
      }
    }

    if (!op->name)
      plog(XLOG_USER, "key %s: Unrecognized key/option \"%s\"", mapkey, f);
  }

  /* all is ok */
  ret = 1;

 out:
  free(old_o);
  return ret;
}


/*
 * Skip to next option in the string, but don't scribble over the string.
 * However, *p gets repointed to the start of the next string past ';'.
 */
static char *
opt_no_scribble(char **p)
{
  char *cp = *p;
  char *dp = cp;
  char *s = cp;

top:
  while (*cp && *cp != ';') {
    if (*cp == '\"') {
      /*
       * Skip past string
       */
      cp++;
      while (*cp && *cp != '\"')
	*dp++ = *cp++;
      if (*cp)
	cp++;
    } else {
      *dp++ = *cp++;
    }
  }

  /*
   * Skip past any remaining ';'s
   */
  while (*cp == ';')
    cp++;

  /*
   * If we have a zero length string
   * and there are more fields, then
   * parse the next one.  This allows
   * sequences of empty fields.
   */
  if (*cp && dp == s)
    goto top;

  *p = cp;
  return s;
}


/*
 * Strip any selectors from a string.  Selectors are all assumed to be
 * first in the string.  This is used for the new /defaults method which will
 * use selectors as well.
 */
char *
strip_selectors(char *opts, char *mapkey)
{
  /*
   * Fill in the global structure fs_static by
   * cracking the string opts.  opts may be
   * scribbled on at will.
   */
  char *o = opts;
  char *oo = opts;
  char *f;

  /*
   * Scan options.  Note that the opt() function scribbles on the opt string.
   */
  while (*(f = opt_no_scribble(&o))) {
    enum vs_opt vs_opt = VarAss;
    char *eq = strchr(f, '=');

    if (!eq || eq[1] == '\0' || eq == f) {
      /*
       * No option or assignment?  Return as is.
       */
      plog(XLOG_USER, "key %s: No option or assignment in \"%s\"", mapkey, f);
      return o;
    }
    /*
     * Check what type of operation is happening
     * !=, =!  is SelNE
     * == is SelEQ
     * := is VarAss
     */
    if (*(eq-1) == '!') {	/* != */
      vs_opt = SelNE;
    } else if (*(eq-1) == ':') {	/* := */
      vs_opt = VarAss;
    } else if (eq[1] == '=') {	/* == */
      vs_opt = SelEQ;
    } else if (eq[1] == '!') {	/* =! */
      vs_opt = SelNE;
    }
    switch (vs_opt) {
    case SelEQ:
    case SelNE:
      /* Skip this selector, maybe there's another one following it */
      plog(XLOG_USER, "skipping selector to \"%s\"", o);
      /* store previous match. it may have been the first assignment */
      oo = o;
      break;

    case VarAss:
      /* found the first assignment, return the string starting with it */
      dlog("found first assignment past selectors \"%s\"", o);
      return oo;
    }
  }

  /* return the same string by default. should not happen. */
  return oo;
}


/*****************************************************************************
 *** BOOLEAN FUNCTIONS (return 0 if false, 1 if true):                     ***
 *****************************************************************************/

/* test if arg is any of this host's network names or numbers */
static int
f_in_network(char *arg)
{
  int status;

  if (!arg)
    return 0;

  status = is_network_member(arg);
  dlog("%s is %son a local network", arg, (status ? "" : "not "));
  return status;
}


/*
 * Test if arg is any of this host's names or aliases (CNAMES).
 * Note: this function compares against the fully expanded host name (hostd).
 * XXX: maybe we also need to compare against the stripped host name?
 */
static int
f_xhost(char *arg)
{
  struct hostent *hp;
  char **cp;

  if (!arg)
    return 0;

  /* simple test: does it match main host name? */
  if (STREQ(arg, opt_hostd))
    return 1;

  /* now find all of the names of "arg" and compare against opt_hostd */
  hp = gethostbyname(arg);
  if (hp == NULL) {
#ifdef HAVE_HSTRERROR
    plog(XLOG_ERROR, "gethostbyname xhost(%s): %s", arg, hstrerror(h_errno));
#else /* not HAVE_HSTRERROR */
    plog(XLOG_ERROR, "gethostbyname xhost(%s): h_errno %d", arg, h_errno);
#endif /* not HAVE_HSTRERROR */
    return 0;
  }
  /* check primary name */
  if (hp->h_name) {
    dlog("xhost: compare %s==%s", hp->h_name, opt_hostd);
    if (STREQ(hp->h_name, opt_hostd)) {
      plog(XLOG_INFO, "xhost(%s): matched h_name %s", arg, hp->h_name);
      return 1;
    }
  }
  /* check all aliases, if any */
  if (hp->h_aliases == NULL) {
    dlog("gethostbyname(%s) has no aliases", arg);
    return 0;
  }
  cp = hp->h_aliases;
  while (*cp) {
    dlog("xhost: compare alias %s==%s", *cp, opt_hostd);
    if (STREQ(*cp, opt_hostd)) {
      plog(XLOG_INFO, "xhost(%s): matched alias %s", arg, *cp);
      return 1;
    }
    cp++;
  }
  /* nothing matched */
  return 0;
}


/* test if this host (short hostname form) is in netgroup (arg) */
static int
f_netgrp(char *arg)
{
  int status;
  char *ptr, *nhost;

  if ((ptr = strchr(arg, ',')) != NULL) {
    *ptr = '\0';
    nhost = ptr + 1;
  } else {
    nhost = opt_host;
  }
  status = innetgr(arg, nhost, NULL, NULL);
  dlog("netgrp = %s status = %d host = %s", arg, status, nhost);
  if (ptr)
    *ptr = ',';
  return status;
}


/* test if this host (fully-qualified name) is in netgroup (arg) */
static int
f_netgrpd(char *arg)
{
  int status;
  char *ptr, *nhost;

  if ((ptr = strchr(arg, ',')) != NULL) {
    *ptr = '\0';
    nhost = ptr + 1;
  } else {
    nhost = opt_hostd;
  }
  status = innetgr(arg, nhost, NULL, NULL);
  dlog("netgrp = %s status = %d hostd = %s", arg, status, nhost);
  if (ptr)
    *ptr = ',';
  return status;
}


/* test if file (arg) exists via lstat */
static int
f_exists(char *arg)
{
  struct stat buf;

  if (lstat(arg, &buf) < 0)
    return (0);
  else
    return (1);
}


/* always false */
static int
f_false(char *arg)
{
  return (0);
}


/* always true */
static int
f_true(char *arg)
{
  return (1);
}


/*
 * Free an option
 */
static void
free_op(opt_apply *p, int b)
{
  XFREE(*p->opt);
}


/*
 * Normalize slashes in the string.
 */
void
normalize_slash(char *p)
{
  char *f, *f0;

  if (!(gopt.flags & CFM_NORMALIZE_SLASHES))
    return;

  f0 = f = strchr(p, '/');
  if (f) {
    char *t = f;
    do {
      /* assert(*f == '/'); */
      if (f == f0 && f[0] == '/' && f[1] == '/') {
	/* copy double slash iff first */
	*t++ = *f++;
	*t++ = *f++;
      } else {
	/* copy a single / across */
	*t++ = *f++;
      }

      /* assert(f[-1] == '/'); */
      /* skip past more /'s */
      while (*f == '/')
	f++;

      /* assert(*f != '/'); */
      /* keep copying up to next / */
      while (*f && *f != '/') {
	/* support escaped slashes '\/' */
	if (f[0] == '\\' && f[1] == '/')
	  f++;			/* skip backslash */
	*t++ = *f++;
      }

      /* assert(*f == 0 || *f == '/'); */

    } while (*f);
    *t = '\0';			/* derived from fix by Steven Glassman */
  }
}


/*
 * Macro-expand an option.  Note that this does not
 * handle recursive expansions.  They will go badly wrong.
 * If sel_p is true then old expand selectors, otherwise
 * don't expand selectors.
 */
static char *
expand_op(char *opt, int sel_p)
{
#define EXPAND_ERROR "No space to expand \"%s\""
  char expbuf[MAXPATHLEN + 1];
  char nbuf[NLEN + 1];
  char *ep = expbuf;
  char *cp = opt;
  char *dp;
  struct opt *op;
  char *cp_orig = opt;

  while ((dp = strchr(cp, '$'))) {
    char ch;
    /*
     * First copy up to the $
     */
    {
      int len = dp - cp;

      if (len > 0) {
	if (BUFSPACE(ep, len)) {
	  /*
	   * We use strncpy (not xstrlcpy) because 'ep' relies on its
	   * semantics.  BUFSPACE guarantees that ep can hold len.
	   */
	  strncpy(ep, cp, len);
	  ep += len;
	} else {
	  plog(XLOG_ERROR, EXPAND_ERROR, opt);
	  goto out;
	}
      }
    }

    cp = dp + 1;
    ch = *cp++;
    if (ch == '$') {
      if (BUFSPACE(ep, 1)) {
	*ep++ = '$';
      } else {
	plog(XLOG_ERROR, EXPAND_ERROR, opt);
	goto out;
      }
    } else if (ch == '{') {
      /* Expansion... */
      enum {
	E_All, E_Dir, E_File, E_Domain, E_Host
      } todo;
      /*
       * Find closing brace
       */
      char *br_p = strchr(cp, '}');
      int len;

      /*
       * Check we found it
       */
      if (!br_p) {
	/*
	 * Just give up
	 */
	plog(XLOG_USER, "No closing '}' in \"%s\"", opt);
	goto out;
      }
      len = br_p - cp;

      /*
       * Figure out which part of the variable to grab.
       */
      if (*cp == '/') {
	/*
	 * Just take the last component
	 */
	todo = E_File;
	cp++;
	--len;
      } else if (*(br_p-1) == '/') {
	/*
	 * Take all but the last component
	 */
	todo = E_Dir;
	--len;
      } else if (*cp == '.') {
	/*
	 * Take domain name
	 */
	todo = E_Domain;
	cp++;
	--len;
      } else if (*(br_p-1) == '.') {
	/*
	 * Take host name
	 */
	todo = E_Host;
	--len;
      } else {
	/*
	 * Take the whole lot
	 */
	todo = E_All;
      }

      /*
       * Truncate if too long.  Since it won't
       * match anyway it doesn't matter that
       * it has been cut short.
       */
      if (len > NLEN)
	len = NLEN;

      /*
       * Put the string into another buffer so
       * we can do comparisons.
       *
       * We use strncpy here (not xstrlcpy) because the dest is meant
       * to be truncated and we don't want to log it as an error.  The
       * use of the BUFSPACE macro above guarantees the safe use of
       * strncpy with nbuf.
       */
      strncpy(nbuf, cp, len);
      nbuf[len] = '\0';

      /*
       * Advance cp
       */
      cp = br_p + 1;

      /*
       * Search the option array
       */
      for (op = opt_fields; op->name; op++) {
	/*
	 * Check for match
	 */
	if (len == op->nlen && STREQ(op->name, nbuf)) {
	  char xbuf[NLEN + 3];
	  char *val;
	  /*
	   * Found expansion.  Copy
	   * the correct value field.
	   */
	  if (!(!op->sel_p == !sel_p)) {
	    /*
	     * Copy the string across unexpanded
	     */
	    xsnprintf(xbuf, sizeof(xbuf), "${%s%s%s}",
		      todo == E_File ? "/" :
		      todo == E_Domain ? "." : "",
		      nbuf,
		      todo == E_Dir ? "/" :
		      todo == E_Host ? "." : "");
	    val = xbuf;
	    /*
	     * Make sure expansion doesn't
	     * munge the value!
	     */
	    todo = E_All;
	  } else if (op->sel_p) {
	    val = *op->sel_p;
	  } else {
	    val = *op->optp;
	  }

	  if (val) {
	    /*
	     * Do expansion:
	     * ${/var} means take just the last part
	     * ${var/} means take all but the last part
	     * ${.var} means take all but first part
	     * ${var.} means take just the first part
	     * ${var} means take the whole lot
	     */
	    int vlen = strlen(val);
	    char *vptr = val;
	    switch (todo) {
	    case E_Dir:
	      vptr = strrchr(val, '/');
	      if (vptr)
		vlen = vptr - val;
	      vptr = val;
	      break;
	    case E_File:
	      vptr = strrchr(val, '/');
	      if (vptr) {
		vptr++;
		vlen = strlen(vptr);
	      } else
		vptr = val;
	      break;
	    case E_Domain:
	      vptr = strchr(val, '.');
	      if (vptr) {
		vptr++;
		vlen = strlen(vptr);
	      } else {
		vptr = "";
		vlen = 0;
	      }
	      break;
	    case E_Host:
	      vptr = strchr(val, '.');
	      if (vptr)
		vlen = vptr - val;
	      vptr = val;
	      break;
	    case E_All:
	      break;
	    }

	    if (BUFSPACE(ep, vlen+1)) {
	      /*
	       * Don't call xstrlcpy() to truncate a string here.  It causes
	       * spurious xstrlcpy() syslog() errors.  Use memcpy() and
	       * explicitly terminate the string.
	       */
	      memcpy(ep, vptr, vlen+1);
	      ep += vlen;
	      *ep = '\0';
	    } else {
	      plog(XLOG_ERROR, EXPAND_ERROR, opt);
	      goto out;
	    }
	  }
	  /*
	   * Done with this variable
	   */
	  break;
	}
      }

      /*
       * Check that the search was successful
       */
      if (!op->name) {
	/*
	 * If it wasn't then scan the
	 * environment for that name
	 * and use any value found
	 */
	char *env = getenv(nbuf);

	if (env) {
	  int vlen = strlen(env);

	  if (BUFSPACE(ep, vlen+1)) {
	    xstrlcpy(ep, env, vlen+1);
	    ep += vlen;
	  } else {
	    plog(XLOG_ERROR, EXPAND_ERROR, opt);
	    goto out;
	  }
	  if (amuDebug(D_STR))
	    plog(XLOG_DEBUG, "Environment gave \"%s\" -> \"%s\"", nbuf, env);
	} else {
	  plog(XLOG_USER, "Unknown sequence \"${%s}\"", nbuf);
	}
      }
    } else {
      /*
       * Error, error
       */
      plog(XLOG_USER, "Unknown $ sequence in \"%s\"", opt);
    }
  }

out:
  /*
   * Handle common case - no expansion
   */
  if (cp == opt) {
    opt = xstrdup(cp);
  } else {
    /*
     * Finish off the expansion
     */
    int vlen = strlen(cp);
    if (BUFSPACE(ep, vlen+1)) {
      xstrlcpy(ep, cp, vlen+1);
      /* ep += vlen; */
    } else {
      plog(XLOG_ERROR, EXPAND_ERROR, opt);
    }

    /*
     * Save the expansion
     */
    opt = xstrdup(expbuf);
  }

  normalize_slash(opt);

  if (amuDebug(D_STR)) {
    plog(XLOG_DEBUG, "Expansion of \"%s\"...", cp_orig);
    plog(XLOG_DEBUG, "......... is \"%s\"", opt);
  }
  return opt;
}


/*
 * Wrapper for expand_op
 */
static void
expand_opts(opt_apply *p, int sel_p)
{
  if (*p->opt) {
    *p->opt = expand_op(*p->opt, sel_p);
  } else if (p->val) {
    /*
     * Do double expansion, remembering
     * to free the string from the first
     * expansion...
     */
    char *s = expand_op(p->val, TRUE);
    *p->opt = expand_op(s, sel_p);
    XFREE(s);
  }
}


/*
 * Apply a function to a list of options
 */
static void
apply_opts(void (*op) (opt_apply *, int), opt_apply ppp[], int b)
{
  opt_apply *pp;

  for (pp = ppp; pp->opt; pp++)
    (*op) (pp, b);
}


/*
 * Free the option table
 */
void
free_opts(am_opts *fo)
{
  /*
   * Copy in the structure we are playing with
   */
  fs_static = *fo;

  /*
   * Free previously allocated memory
   */
  apply_opts(free_op, to_free, FALSE);
}

am_opts *
copy_opts(am_opts *old)
{
  am_opts *newopts;
  newopts = CALLOC(struct am_opts);

#define _AM_OPT_COPY(field) do { \
    if (old->field) \
      newopts->field = xstrdup(old->field); \
  } while (0)

  _AM_OPT_COPY(fs_glob);
  _AM_OPT_COPY(fs_local);
  _AM_OPT_COPY(fs_mtab);
  _AM_OPT_COPY(opt_dev);
  _AM_OPT_COPY(opt_delay);
  _AM_OPT_COPY(opt_dir);
  _AM_OPT_COPY(opt_fs);
  _AM_OPT_COPY(opt_group);
  _AM_OPT_COPY(opt_mount);
  _AM_OPT_COPY(opt_opts);
  _AM_OPT_COPY(opt_remopts);
  _AM_OPT_COPY(opt_pref);
  _AM_OPT_COPY(opt_cache);
  _AM_OPT_COPY(opt_rfs);
  _AM_OPT_COPY(opt_rhost);
  _AM_OPT_COPY(opt_sublink);
  _AM_OPT_COPY(opt_type);
  _AM_OPT_COPY(opt_mount_type);
  _AM_OPT_COPY(opt_unmount);
  _AM_OPT_COPY(opt_umount);
  _AM_OPT_COPY(opt_user);
  _AM_OPT_COPY(opt_maptype);
  _AM_OPT_COPY(opt_cachedir);
  _AM_OPT_COPY(opt_addopts);

  return newopts;
}


/*
 * Expand selectors (variables that cannot be assigned to or overridden)
 */
char *
expand_selectors(char *key)
{
  return expand_op(key, TRUE);
}


/*
 * Expand options (i.e. non-selectors, see above for definition)
 */
static inline char *
expand_options(char *key)
{
  return expand_op(key, FALSE);
}


/*
 * Remove trailing /'s from a string
 * unless the string is a single / (Steven Glassman)
 * or unless it is two slashes // (Kevin D. Bond)
 * or unless amd.conf says not to touch slashes.
 */
void
deslashify(char *s)
{
  if (!(gopt.flags & CFM_NORMALIZE_SLASHES))
    return;

  if (s && *s) {
    char *sl = s + strlen(s);

    while (*--sl == '/' && sl > s)
      *sl = '\0';
  }
}


int
eval_fs_opts(am_opts *fo, char *opts, char *g_opts, char *path, char *key, char *map)
{
  int ok = TRUE;

  free_opts(fo);

  /*
   * Clear out the option table
   */
  memset((voidp) &fs_static, 0, sizeof(fs_static));
  memset((voidp) vars, 0, sizeof(vars));
  memset((voidp) fo, 0, sizeof(*fo));

  /* set hostname */
  opt_host = (char *) am_get_hostname();

  /*
   * Set key, map & path before expansion
   */
  opt_key = key;
  opt_map = map;
  opt_path = path;

  opt_dkey = strchr(key, '.');
  if (!opt_dkey) {
    opt_dkey = NullStr;
    opt_keyd = key;
  } else {
    opt_keyd = strnsave(key, opt_dkey - key);
    opt_dkey++;
    if (*opt_dkey == '\0')	/* check for 'host.' */
      opt_dkey = NullStr;
  }

  /*
   * Expand global options
   */
  fs_static.fs_glob = expand_selectors(g_opts);

  /*
   * Expand local options
   */
  fs_static.fs_local = expand_selectors(opts);

  /* break global options into fs_static fields */
  if ((ok = split_opts(fs_static.fs_glob, key))) {
    dlog("global split_opts ok");
    /*
     * evaluate local selectors
     */
    if ((ok = eval_selectors(fs_static.fs_local, key))) {
      dlog("local eval_selectors ok");
      /* if the local selectors matched, then do the local overrides */
      ok = split_opts(fs_static.fs_local, key);
      if (ok)
	dlog("local split_opts ok");
    }
  }

  /*
   * Normalize remote host name.
   * 1.  Expand variables
   * 2.  Normalize relative to host tables
   * 3.  Strip local domains from the remote host
   *     name before using it in other expansions.
   *     This makes mount point names and other things
   *     much shorter, while allowing cross domain
   *     sharing of mount maps.
   */
  apply_opts(expand_opts, rhost_expansion, FALSE);
  if (ok && fs_static.opt_rhost && *fs_static.opt_rhost)
    host_normalize(&fs_static.opt_rhost);

  /*
   * Macro expand the options.
   * Do this regardless of whether we are accepting
   * this mount - otherwise nasty things happen
   * with memory allocation.
   */
  apply_opts(expand_opts, expansions, FALSE);

  /*
   * Strip trailing slashes from local pathname...
   */
  deslashify(fs_static.opt_fs);

  /*
   * ok... copy the data back out.
   */
  *fo = fs_static;

  /*
   * Clear defined options
   */
  if (opt_keyd != key && opt_keyd != nullstr)
    XFREE(opt_keyd);
  opt_keyd = nullstr;
  opt_dkey = NullStr;
  opt_key = opt_map = opt_path = nullstr;

  return ok;
}
