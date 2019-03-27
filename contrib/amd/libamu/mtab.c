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
 * File: am-utils/libamu/mtab.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>


/*
 * Firewall /etc/mtab entries
 */
void
mnt_free(mntent_t *mp)
{
  XFREE(mp->mnt_fsname);
  XFREE(mp->mnt_dir);
  XFREE(mp->mnt_type);
  XFREE(mp->mnt_opts);

#ifdef HAVE_MNTENT_T_MNT_TIME
# ifdef HAVE_MNTENT_T_MNT_TIME_STRING
  XFREE(mp->mnt_time);
# endif /* HAVE_MNTENT_T_MNT_TIME_STRING */
#endif /* HAVE_MNTENT_T_MNT_TIME */

  XFREE(mp);
}


/*
 * Discard memory allocated for mount list
 */
void
discard_mntlist(mntlist *mp)
{
  mntlist *mp2;

  while ((mp2 = mp)) {
    mp = mp->mnext;
    if (mp2->mnt)
      mnt_free(mp2->mnt);
    XFREE(mp2);
  }
}


/*
 * Throw away a mount list
 */
void
free_mntlist(mntlist *mp)
{
  discard_mntlist(mp);
#ifdef MOUNT_TABLE_ON_FILE
  unlock_mntlist();
#endif /* MOUNT_TABLE_ON_FILE */
}


/*
 * Utility routine which returns a pointer to whatever follows an = in a
 * string.  Returns null if = is not found in the string.
 */
char *
haseq(char *instr)
{
  if (instr) {
    char *eq = strchr(instr, '=');
    if (eq) return ++eq;
  }
  return NULL;
}


/*
 * Utility routine which returns a pointer to whatever
 * follows an = in a mount option.  Returns null if option
 * doesn't exist or doesn't have an '='.  Won't fail for opt,foo=.
 */
char *
hasmnteq(mntent_t *mnt, char *opt)
{
  if (mnt && opt) {		/* disallow null input pointers */
    if ( *opt ) {		/* disallow the null string as an opt */
      char *str = amu_hasmntopt(mnt, opt);
      if ( str ) {		/* option was there */
	char *eq = str + strlen(opt); /* Look at char just after option */
	if (*eq == '=')		/* Is it '=' ? */
	  return ++eq;		/* If so, return pointer to remaining str */
      }
    }
  }
  return NULL;
}


/*
 * Wrapper around hasmntvalerr(), which retains backwards compatibiliy with
 * older use of hasmntval().
 *
 * XXX: eventually, all use of hasmntval() should be replaced with
 * hasmntvalerr().
 */
int
hasmntval(mntent_t *mnt, char *opt)
{
  int err, val = 0;

  err = hasmntvalerr(mnt, opt, &val);
  if (err)	   /* if there was an error (hasmntvalerr returned 1) */
    return 0;	   /* redundant: val==0 above, but leave here for clarity */
  /* otherwise there was no error */
  return val;
}


/*
 * Utility routine which determines the value of a numeric option in the
 * mount options (such as port=%d), and fills in the value in the argument
 * valp (argument won't be touched if no value is set, for example due to an
 * error).
 *
 * Returns non-zero (1) on error; returns 0 on success.
 *
 * XXX: eventually, all use of hasmntval() should be replaced with
 * hasmntvalerr().
 */
unsigned int
hasmntvalerr(mntent_t *mnt, char *opt, int *valp)
{
  char *str = amu_hasmntopt(mnt, opt);
  int err = 1;		     /* 1 means no good value was set (an error) */
  char *eq, *endptr;
  long int i;

  /* exit if no option specificed */
  if (!str) {
    goto out;
  }

  eq = hasmnteq(mnt, opt);

  if (!eq) {		  /* no argument to option ('=' sign was missing) */
    plog(XLOG_MAP, "numeric option to \"%s\" missing", opt);
    goto out;
  }

  /* if got here, then we had an '=' after option name */
  endptr = NULL;
  i = strtol(eq, &endptr, 0); /* hex and octal allowed ;-) */
  if (!endptr ||
      (endptr != eq && (*endptr == ',' || *endptr == '\0'))) {
      /*
       * endptr set means strtol saw a non-digit.  If the non-digit is a
       * comma, it's probably the start of the next option.  If the comma is
       * the first char though, complain about it (foo=,bar is made
       * noticeable by this).
       *
       * Similar reasoning for '\0' instead of comma, it's the end of the
       * string.
       */
    *valp = (int) i;		/* set good value */
    err = 0;			/* no error */
  } else {
    /* whatever was after the '=' sign wasn't a number */
    plog(XLOG_MAP, "invalid numeric option in \"%s\": \"%s\"", opt, str);
    /* fall through to error/exit processing */
  }

 out:
  return err;
}


/*
 * Utility routine which returns the string value of
 * an option in the mount options (such as proto=udp).
 * Returns NULL if the option is not specified.
 * Returns malloc'ed string (caller must free!)
 */
char *
hasmntstr(mntent_t *mnt, char *opt)
{
  char *str = amu_hasmntopt(mnt, opt);

  if (str) { /* The option was there */

    char *eq = hasmnteq(mnt, opt);

    if (eq) { /* and had an = after it */

      char *endptr = strchr(eq, ',');

      /* if saw no comma, return xstrdup'd string */
      if (!endptr)
	return xstrdup(eq);
      else {
	/* else we need to copy only the chars needed */
	int len = endptr - eq;
	char *buf = xmalloc(len + 1);
	strncpy(buf, eq, len);
	buf[len] = '\0';
	return buf;
      }
    }
  }
  return NULL;
}
