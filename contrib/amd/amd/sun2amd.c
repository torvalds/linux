/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 2005 Daniel P. Ottavio
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
 * File: am-utils/amd/sun2amd.c
 *
 */

/*
 * Translate Sun-syntax maps to Amd maps
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>


/* dummies to make the program compile and link */
struct amu_global_options gopt;
#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
# ifdef NEED_LIBWRAP_SEVERITY_VARIABLES
/*
 * Some systems that define libwrap already define these two variables
 * in libwrap, while others don't: so I need to know precisely iff
 * to define these two severity variables.
 */
int allow_severity=0, deny_severity=0, rfc931_timeout=0;
# endif /* NEED_LIBWRAP_SEVERITY_VARIABLES */
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */


/*
 * Parse the stream sun_in, convert the map information to amd, write
 * the results to amd_out.
 */
static int
sun2amd_convert(FILE *sun_in, FILE *amd_out)
{
  char line_buff[INFO_MAX_LINE_LEN], *tmp, *key, *entry;
  int pos, line = 0, retval = 1;

  /* just to be safe */
  memset(line_buff, 0, sizeof(line_buff));

  /* Read the input line by line and do the conversion. */
  while ((pos = file_read_line(line_buff, sizeof(line_buff), sun_in))) {
    line++;
    line_buff[pos - 1] = '\0';

    /* remove comments */
    if ((tmp = strchr(line_buff, '#')) != NULL) {
      *tmp = '\0';
    }

    /* find start of key */
    key = line_buff;
    while (*key != '\0' && isspace((unsigned char)*key)) {
      key++;
    }

    /* ignore blank lines */
    if (*key == '\0') {
      continue;
    }

    /* find the end of the key and NULL terminate */
    tmp = key;
    while (*tmp != '\0' && isspace((unsigned char)*tmp) == 0) {
      tmp++;
    }
    if (*tmp == '\0') {
      plog(XLOG_ERROR, "map line %d has no entry", line);
      goto err;
    }
    *tmp++ = '\0';
    if (*tmp == '\0') {
      plog(XLOG_ERROR, "map line %d has no entry", line);
      goto err;
    }
    entry = tmp;

    /* convert the sun entry to an amd entry */
    if ((tmp = sun_entry2amd(key, entry)) == NULL) {
      plog(XLOG_ERROR, "parse error on line %d", line);
      goto err;
    }

    if (fprintf(amd_out, "%s %s\n", key, tmp) < 0) {
      plog(XLOG_ERROR, "can't write to output stream: %s", strerror(errno));
      goto err;
    }

    /* just to be safe */
    memset(line_buff, 0, sizeof(line_buff));
  }

  /* success */
  retval = 0;

 err:
  return retval;
}


/*
 * wrapper open function
 */
static FILE *
sun2amd_open(const char *path, const char *mode)
{
  FILE *retval = NULL;

  if ((retval = fopen(path,mode)) == NULL) {
    plog(XLOG_ERROR,"could not open file %s",path);
  }

  return retval;
}


/*
 * echo the usage and exit
 */
static void
sun2amd_usage(void)
{
  fprintf(stderr,
	  "usage : sun2amd [-hH] [-i infile] [-o outfile]\n"
	  "-h\thelp\n"
	  "-i\tspecify an infile (defaults to stdin)\n"
	  "-o\tspecify an outfile (defaults to stdout)\n");
}


int
main(int argc, char **argv)
{
  /* default in/out to stdin/stdout */
  FILE *sun_in = stdin, *amd_out = stdout;
  int opt, retval = 1;

  while ((opt = getopt(argc, argv , "i:o:hH")) != -1) {
    switch (opt) {

    case 'i':
      if ((sun_in = sun2amd_open(optarg,"r")) == NULL) {
	goto err;
      }
      break;

    case 'o':
      if ((amd_out = sun2amd_open(optarg,"w")) == NULL) {
	goto err;
      }
      break;

    case 'h':
    case 'H':
      sun2amd_usage();
      goto err;
    }
  }

  retval = sun2amd_convert(sun_in,amd_out);

 err:
  exit(retval);
}
