//===-- GetOptInc.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/GetOptInc.h"

#if defined(REPLACE_GETOPT) || defined(REPLACE_GETOPT_LONG) ||                 \
    defined(REPLACE_GETOPT_LONG_ONLY)

// getopt.cpp
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(REPLACE_GETOPT)
int opterr = 1;   /* if error message should be printed */
int optind = 1;   /* index into parent argv vector */
int optopt = '?'; /* character checked for validity */
int optreset;     /* reset getopt */
char *optarg;     /* argument associated with option */
#endif

#define PRINT_ERROR ((opterr) && (*options != ':'))

#define FLAG_PERMUTE 0x01  /* permute non-options to the end of argv */
#define FLAG_ALLARGS 0x02  /* treat non-options as args to option "-1" */
#define FLAG_LONGONLY 0x04 /* operate as getopt_long_only */

/* return values */
#define BADCH (int)'?'
#define BADARG ((*options == ':') ? (int)':' : (int)'?')
#define INORDER (int)1

#define EMSG ""

static int getopt_internal(int, char *const *, const char *,
                           const struct option *, int *, int);
static int parse_long_options(char *const *, const char *,
                              const struct option *, int *, int);
static int gcd(int, int);
static void permute_args(int, int, int, char *const *);

static const char *place = EMSG; /* option letter processing */

/* XXX: set optreset to 1 rather than these two */
static int nonopt_start = -1; /* first non option argument (for permute) */
static int nonopt_end = -1;   /* first option after non options (for permute) */

/*
* Compute the greatest common divisor of a and b.
*/
static int gcd(int a, int b) {
  int c;

  c = a % b;
  while (c != 0) {
    a = b;
    b = c;
    c = a % b;
  }

  return (b);
}

static void pass() {}
#define warnx(a, ...) pass();

/*
* Exchange the block from nonopt_start to nonopt_end with the block
* from nonopt_end to opt_end (keeping the same order of arguments
* in each block).
*/
static void permute_args(int panonopt_start, int panonopt_end, int opt_end,
                         char *const *nargv) {
  int cstart, cyclelen, i, j, ncycle, nnonopts, nopts, pos;
  char *swap;

  /*
  * compute lengths of blocks and number and size of cycles
  */
  nnonopts = panonopt_end - panonopt_start;
  nopts = opt_end - panonopt_end;
  ncycle = gcd(nnonopts, nopts);
  cyclelen = (opt_end - panonopt_start) / ncycle;

  for (i = 0; i < ncycle; i++) {
    cstart = panonopt_end + i;
    pos = cstart;
    for (j = 0; j < cyclelen; j++) {
      if (pos >= panonopt_end)
        pos -= nnonopts;
      else
        pos += nopts;
      swap = nargv[pos];
      /* LINTED const cast */
      ((char **)nargv)[pos] = nargv[cstart];
      /* LINTED const cast */
      ((char **)nargv)[cstart] = swap;
    }
  }
}

/*
* parse_long_options --
*  Parse long options in argc/argv argument vector.
* Returns -1 if short_too is set and the option does not match long_options.
*/
static int parse_long_options(char *const *nargv, const char *options,
                              const struct option *long_options, int *idx,
                              int short_too) {
  char *current_argv, *has_equal;
  size_t current_argv_len;
  int i, match;

  current_argv = const_cast<char *>(place);
  match = -1;

  optind++;

  if ((has_equal = strchr(current_argv, '=')) != NULL) {
    /* argument found (--option=arg) */
    current_argv_len = has_equal - current_argv;
    has_equal++;
  } else
    current_argv_len = strlen(current_argv);

  for (i = 0; long_options[i].name; i++) {
    /* find matching long option */
    if (strncmp(current_argv, long_options[i].name, current_argv_len))
      continue;

    if (strlen(long_options[i].name) == current_argv_len) {
      /* exact match */
      match = i;
      break;
    }
    /*
    * If this is a known short option, don't allow
    * a partial match of a single character.
    */
    if (short_too && current_argv_len == 1)
      continue;

    if (match == -1) /* partial match */
      match = i;
    else {
      /* ambiguous abbreviation */
      if (PRINT_ERROR)
        warnx(ambig, (int)current_argv_len, current_argv);
      optopt = 0;
      return (BADCH);
    }
  }
  if (match != -1) { /* option found */
    if (long_options[match].has_arg == no_argument && has_equal) {
      if (PRINT_ERROR)
        warnx(noarg, (int)current_argv_len, current_argv);
      /*
      * XXX: GNU sets optopt to val regardless of flag
      */
      if (long_options[match].flag == NULL)
        optopt = long_options[match].val;
      else
        optopt = 0;
      return (BADARG);
    }
    if (long_options[match].has_arg == required_argument ||
        long_options[match].has_arg == optional_argument) {
      if (has_equal)
        optarg = has_equal;
      else if (long_options[match].has_arg == required_argument) {
        /*
        * optional argument doesn't use next nargv
        */
        optarg = nargv[optind++];
      }
    }
    if ((long_options[match].has_arg == required_argument) &&
        (optarg == NULL)) {
      /*
      * Missing argument; leading ':' indicates no error
      * should be generated.
      */
      if (PRINT_ERROR)
        warnx(recargstring, current_argv);
      /*
      * XXX: GNU sets optopt to val regardless of flag
      */
      if (long_options[match].flag == NULL)
        optopt = long_options[match].val;
      else
        optopt = 0;
      --optind;
      return (BADARG);
    }
  } else { /* unknown option */
    if (short_too) {
      --optind;
      return (-1);
    }
    if (PRINT_ERROR)
      warnx(illoptstring, current_argv);
    optopt = 0;
    return (BADCH);
  }
  if (idx)
    *idx = match;
  if (long_options[match].flag) {
    *long_options[match].flag = long_options[match].val;
    return (0);
  } else
    return (long_options[match].val);
}

/*
* getopt_internal --
*  Parse argc/argv argument vector.  Called by user level routines.
*/
static int getopt_internal(int nargc, char *const *nargv, const char *options,
                           const struct option *long_options, int *idx,
                           int flags) {
  const char *oli; /* option letter list index */
  int optchar, short_too;
  static int posixly_correct = -1;

  if (options == NULL)
    return (-1);

  /*
  * XXX Some GNU programs (like cvs) set optind to 0 instead of
  * XXX using optreset.  Work around this braindamage.
  */
  if (optind == 0)
    optind = optreset = 1;

  /*
  * Disable GNU extensions if POSIXLY_CORRECT is set or options
  * string begins with a '+'.
  */
  if (posixly_correct == -1 || optreset)
    posixly_correct = (getenv("POSIXLY_CORRECT") != NULL);
  if (*options == '-')
    flags |= FLAG_ALLARGS;
  else if (posixly_correct || *options == '+')
    flags &= ~FLAG_PERMUTE;
  if (*options == '+' || *options == '-')
    options++;

  optarg = NULL;
  if (optreset)
    nonopt_start = nonopt_end = -1;
start:
  if (optreset || !*place) { /* update scanning pointer */
    optreset = 0;
    if (optind >= nargc) { /* end of argument vector */
      place = EMSG;
      if (nonopt_end != -1) {
        /* do permutation, if we have to */
        permute_args(nonopt_start, nonopt_end, optind, nargv);
        optind -= nonopt_end - nonopt_start;
      } else if (nonopt_start != -1) {
        /*
        * If we skipped non-options, set optind
        * to the first of them.
        */
        optind = nonopt_start;
      }
      nonopt_start = nonopt_end = -1;
      return (-1);
    }
    if (*(place = nargv[optind]) != '-' ||
        (place[1] == '\0' && strchr(options, '-') == NULL)) {
      place = EMSG; /* found non-option */
      if (flags & FLAG_ALLARGS) {
        /*
        * GNU extension:
        * return non-option as argument to option 1
        */
        optarg = nargv[optind++];
        return (INORDER);
      }
      if (!(flags & FLAG_PERMUTE)) {
        /*
        * If no permutation wanted, stop parsing
        * at first non-option.
        */
        return (-1);
      }
      /* do permutation */
      if (nonopt_start == -1)
        nonopt_start = optind;
      else if (nonopt_end != -1) {
        permute_args(nonopt_start, nonopt_end, optind, nargv);
        nonopt_start = optind - (nonopt_end - nonopt_start);
        nonopt_end = -1;
      }
      optind++;
      /* process next argument */
      goto start;
    }
    if (nonopt_start != -1 && nonopt_end == -1)
      nonopt_end = optind;

    /*
    * If we have "-" do nothing, if "--" we are done.
    */
    if (place[1] != '\0' && *++place == '-' && place[1] == '\0') {
      optind++;
      place = EMSG;
      /*
      * We found an option (--), so if we skipped
      * non-options, we have to permute.
      */
      if (nonopt_end != -1) {
        permute_args(nonopt_start, nonopt_end, optind, nargv);
        optind -= nonopt_end - nonopt_start;
      }
      nonopt_start = nonopt_end = -1;
      return (-1);
    }
  }

  /*
  * Check long options if:
  *  1) we were passed some
  *  2) the arg is not just "-"
  *  3) either the arg starts with -- we are getopt_long_only()
  */
  if (long_options != NULL && place != nargv[optind] &&
      (*place == '-' || (flags & FLAG_LONGONLY))) {
    short_too = 0;
    if (*place == '-')
      place++; /* --foo long option */
    else if (*place != ':' && strchr(options, *place) != NULL)
      short_too = 1; /* could be short option too */

    optchar = parse_long_options(nargv, options, long_options, idx, short_too);
    if (optchar != -1) {
      place = EMSG;
      return (optchar);
    }
  }

  if ((optchar = (int)*place++) == (int)':' ||
      (optchar == (int)'-' && *place != '\0') ||
      (oli = strchr(options, optchar)) == NULL) {
    /*
    * If the user specified "-" and  '-' isn't listed in
    * options, return -1 (non-option) as per POSIX.
    * Otherwise, it is an unknown option character (or ':').
    */
    if (optchar == (int)'-' && *place == '\0')
      return (-1);
    if (!*place)
      ++optind;
    if (PRINT_ERROR)
      warnx(illoptchar, optchar);
    optopt = optchar;
    return (BADCH);
  }
  if (long_options != NULL && optchar == 'W' && oli[1] == ';') {
    /* -W long-option */
    if (*place) /* no space */
      /* NOTHING */;
    else if (++optind >= nargc) { /* no arg */
      place = EMSG;
      if (PRINT_ERROR)
        warnx(recargchar, optchar);
      optopt = optchar;
      return (BADARG);
    } else /* white space */
      place = nargv[optind];
    optchar = parse_long_options(nargv, options, long_options, idx, 0);
    place = EMSG;
    return (optchar);
  }
  if (*++oli != ':') { /* doesn't take argument */
    if (!*place)
      ++optind;
  } else { /* takes (optional) argument */
    optarg = NULL;
    if (*place) /* no white space */
      optarg = const_cast<char *>(place);
    else if (oli[1] != ':') {  /* arg not optional */
      if (++optind >= nargc) { /* no arg */
        place = EMSG;
        if (PRINT_ERROR)
          warnx(recargchar, optchar);
        optopt = optchar;
        return (BADARG);
      } else
        optarg = nargv[optind];
    }
    place = EMSG;
    ++optind;
  }
  /* dump back option letter */
  return (optchar);
}

/*
* getopt --
*  Parse argc/argv argument vector.
*
* [eventually this will replace the BSD getopt]
*/
#if defined(REPLACE_GETOPT)
int getopt(int nargc, char *const *nargv, const char *options) {

  /*
  * We don't pass FLAG_PERMUTE to getopt_internal() since
  * the BSD getopt(3) (unlike GNU) has never done this.
  *
  * Furthermore, since many privileged programs call getopt()
  * before dropping privileges it makes sense to keep things
  * as simple (and bug-free) as possible.
  */
  return (getopt_internal(nargc, nargv, options, NULL, NULL, 0));
}
#endif

/*
* getopt_long --
*  Parse argc/argv argument vector.
*/
#if defined(REPLACE_GETOPT_LONG)
int getopt_long(int nargc, char *const *nargv, const char *options,
                const struct option *long_options, int *idx) {
  return (
      getopt_internal(nargc, nargv, options, long_options, idx, FLAG_PERMUTE));
}
#endif

/*
* getopt_long_only --
*  Parse argc/argv argument vector.
*/
#if defined(REPLACE_GETOPT_LONG_ONLY)
int getopt_long_only(int nargc, char *const *nargv, const char *options,
                     const struct option *long_options, int *idx) {

  return (getopt_internal(nargc, nargv, options, long_options, idx,
                          FLAG_PERMUTE | FLAG_LONGONLY));
}
#endif

#endif
