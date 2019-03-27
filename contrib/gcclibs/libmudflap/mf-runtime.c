/* Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>
   Splay Tree code originally by Mark Mitchell <mark@markmitchell.com>,
   adapted from libiberty.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"

/* These attempt to coax various unix flavours to declare all our
   needed tidbits in the system headers.  */
#if !defined(__FreeBSD__) && !defined(__APPLE__)
#define _POSIX_SOURCE
#endif /* Some BSDs break <sys/socket.h> if this is defined. */
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define _BSD_TYPES
#define __EXTENSIONS__
#define _ALL_SOURCE
#define _LARGE_FILE_API
#define _XOPEN_SOURCE_EXTENDED 1

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#include <assert.h>

#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#include "mf-runtime.h"
#include "mf-impl.h"


/* ------------------------------------------------------------------------ */
/* Splay-tree implementation.  */

typedef uintptr_t mfsplay_tree_key;
typedef void *mfsplay_tree_value;

/* Forward declaration for a node in the tree.  */
typedef struct mfsplay_tree_node_s *mfsplay_tree_node;

/* The type of a function used to iterate over the tree.  */
typedef int (*mfsplay_tree_foreach_fn) (mfsplay_tree_node, void *);

/* The nodes in the splay tree.  */
struct mfsplay_tree_node_s
{
  /* Data.  */
  mfsplay_tree_key key;
  mfsplay_tree_value value;
  /* Children.  */
  mfsplay_tree_node left;
  mfsplay_tree_node right;
  /* XXX: The addition of a parent pointer may eliminate some recursion.  */
};

/* The splay tree itself.  */
struct mfsplay_tree_s
{
  /* The root of the tree.  */
  mfsplay_tree_node root;

  /* The last key value for which the tree has been splayed, but not
     since modified.  */
  mfsplay_tree_key last_splayed_key;
  int last_splayed_key_p;

  /* Statistics.  */
  unsigned num_keys;

  /* Traversal recursion control flags.  */
  unsigned max_depth;
  unsigned depth;
  unsigned rebalance_p;
};
typedef struct mfsplay_tree_s *mfsplay_tree;

static mfsplay_tree mfsplay_tree_new (void);
static mfsplay_tree_node mfsplay_tree_insert (mfsplay_tree, mfsplay_tree_key, mfsplay_tree_value);
static void mfsplay_tree_remove (mfsplay_tree, mfsplay_tree_key);
static mfsplay_tree_node mfsplay_tree_lookup (mfsplay_tree, mfsplay_tree_key);
static mfsplay_tree_node mfsplay_tree_predecessor (mfsplay_tree, mfsplay_tree_key);
static mfsplay_tree_node mfsplay_tree_successor (mfsplay_tree, mfsplay_tree_key);
static int mfsplay_tree_foreach (mfsplay_tree, mfsplay_tree_foreach_fn, void *);
static void mfsplay_tree_rebalance (mfsplay_tree sp);

/* ------------------------------------------------------------------------ */
/* Utility macros */

#define CTOR  __attribute__ ((constructor))
#define DTOR  __attribute__ ((destructor))


/* Codes to describe the context in which a violation occurs. */
#define __MF_VIOL_UNKNOWN 0
#define __MF_VIOL_READ 1
#define __MF_VIOL_WRITE 2
#define __MF_VIOL_REGISTER 3
#define __MF_VIOL_UNREGISTER 4
#define __MF_VIOL_WATCH 5

/* Protect against recursive calls. */

static void
begin_recursion_protect1 (const char *pf)
{
  if (__mf_get_state () == reentrant)
    {
      write (2, "mf: erroneous reentrancy detected in `", 38);
      write (2, pf, strlen(pf));
      write (2, "'\n", 2); \
      abort ();
    }
  __mf_set_state (reentrant);
}

#define BEGIN_RECURSION_PROTECT() \
  begin_recursion_protect1 (__PRETTY_FUNCTION__)

#define END_RECURSION_PROTECT() \
  __mf_set_state (active)

/* ------------------------------------------------------------------------ */
/* Required globals.  */

#define LOOKUP_CACHE_MASK_DFL 1023
#define LOOKUP_CACHE_SIZE_MAX 65536 /* Allows max CACHE_MASK 0xFFFF */
#define LOOKUP_CACHE_SHIFT_DFL 2

struct __mf_cache __mf_lookup_cache [LOOKUP_CACHE_SIZE_MAX];
uintptr_t __mf_lc_mask = LOOKUP_CACHE_MASK_DFL;
unsigned char __mf_lc_shift = LOOKUP_CACHE_SHIFT_DFL;
#define LOOKUP_CACHE_SIZE (__mf_lc_mask + 1)

struct __mf_options __mf_opts;
int __mf_starting_p = 1;

#ifdef LIBMUDFLAPTH
#ifdef HAVE_TLS
__thread enum __mf_state_enum __mf_state_1 = reentrant;
#endif
#else
enum __mf_state_enum __mf_state_1 = reentrant;
#endif

#ifdef LIBMUDFLAPTH
pthread_mutex_t __mf_biglock =
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
       PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
       PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

/* Use HAVE_PTHREAD_H here instead of LIBMUDFLAPTH, so that even
   the libmudflap.la (no threading support) can diagnose whether
   the application is linked with -lpthread.  See __mf_usage() below.  */
#if HAVE_PTHREAD_H
#ifdef _POSIX_THREADS
#pragma weak pthread_join
#else
#define pthread_join NULL
#endif
#endif


/* ------------------------------------------------------------------------ */
/* stats-related globals.  */

static unsigned long __mf_count_check;
static unsigned long __mf_lookup_cache_reusecount [LOOKUP_CACHE_SIZE_MAX];
static unsigned long __mf_count_register;
static unsigned long __mf_total_register_size [__MF_TYPE_MAX+1];
static unsigned long __mf_count_unregister;
static unsigned long __mf_total_unregister_size;
static unsigned long __mf_count_violation [__MF_VIOL_WATCH+1];
static unsigned long __mf_sigusr1_received;
static unsigned long __mf_sigusr1_handled;
/* not static */ unsigned long __mf_reentrancy;
#ifdef LIBMUDFLAPTH
/* not static */ unsigned long __mf_lock_contention;
#endif


/* ------------------------------------------------------------------------ */
/* mode-check-related globals.  */

typedef struct __mf_object
{
  uintptr_t low, high; /* __mf_register parameters */
  const char *name;
  char type; /* __MF_TYPE_something */
  char watching_p; /* Trigger a VIOL_WATCH on access? */
  unsigned read_count; /* Number of times __mf_check/read was called on this object.  */
  unsigned write_count; /* Likewise for __mf_check/write.  */
  unsigned liveness; /* A measure of recent checking activity.  */
  unsigned description_epoch; /* Last epoch __mf_describe_object printed this.  */

  uintptr_t alloc_pc;
  struct timeval alloc_time;
  char **alloc_backtrace;
  size_t alloc_backtrace_size;
#ifdef LIBMUDFLAPTH
  pthread_t alloc_thread;
#endif

  int deallocated_p;
  uintptr_t dealloc_pc;
  struct timeval dealloc_time;
  char **dealloc_backtrace;
  size_t dealloc_backtrace_size;
#ifdef LIBMUDFLAPTH
  pthread_t dealloc_thread;
#endif
} __mf_object_t;

/* Live objects: splay trees, separated by type, ordered on .low (base address).  */
/* Actually stored as static vars within lookup function below.  */

/* Dead objects: circular arrays; _MIN_CEM .. _MAX_CEM only */
static unsigned __mf_object_dead_head[__MF_TYPE_MAX_CEM+1]; /* next empty spot */
static __mf_object_t *__mf_object_cemetary[__MF_TYPE_MAX_CEM+1][__MF_PERSIST_MAX];


/* ------------------------------------------------------------------------ */
/* Forward function declarations */

void __mf_init () CTOR;
static void __mf_sigusr1_respond ();
static unsigned __mf_find_objects (uintptr_t ptr_low, uintptr_t ptr_high,
                                   __mf_object_t **objs, unsigned max_objs);
static unsigned __mf_find_objects2 (uintptr_t ptr_low, uintptr_t ptr_high,
                                    __mf_object_t **objs, unsigned max_objs, int type);
static unsigned __mf_find_dead_objects (uintptr_t ptr_low, uintptr_t ptr_high,
                                        __mf_object_t **objs, unsigned max_objs);
static void __mf_adapt_cache ();
static void __mf_describe_object (__mf_object_t *obj);
static unsigned __mf_watch_or_not (void *ptr, size_t sz, char flag);
static mfsplay_tree __mf_object_tree (int type);
static void __mf_link_object (__mf_object_t *node);
static void __mf_unlink_object (__mf_object_t *node);


/* ------------------------------------------------------------------------ */
/* Configuration engine */

static void
__mf_set_default_options ()
{
  memset (& __mf_opts, 0, sizeof (__mf_opts));

  __mf_opts.adapt_cache = 1000003;
  __mf_opts.abbreviate = 1;
  __mf_opts.verbose_violations = 1;
  __mf_opts.free_queue_length = 4;
  __mf_opts.persistent_count = 100;
  __mf_opts.crumple_zone = 32;
  __mf_opts.backtrace = 4;
  __mf_opts.timestamps = 1;
  __mf_opts.mudflap_mode = mode_check;
  __mf_opts.violation_mode = viol_nop;
  __mf_opts.heur_std_data = 1;
#ifdef LIBMUDFLAPTH
  __mf_opts.thread_stack = 0;
#endif
}

static struct option
{
  char *name;
  char *description;
  enum
    {
      set_option,
      read_integer_option,
    } type;
  unsigned value;
  unsigned *target;
}
options [] =
  {
    {"mode-nop",
     "mudflaps do nothing",
     set_option, (unsigned)mode_nop, (unsigned *)&__mf_opts.mudflap_mode},
    {"mode-populate",
     "mudflaps populate object tree",
     set_option, (unsigned)mode_populate, (unsigned *)&__mf_opts.mudflap_mode},
    {"mode-check",
     "mudflaps check for memory violations",
     set_option, (unsigned)mode_check, (unsigned *)&__mf_opts.mudflap_mode},
    {"mode-violate",
     "mudflaps always cause violations (diagnostic)",
     set_option, (unsigned)mode_violate, (unsigned *)&__mf_opts.mudflap_mode},

    {"viol-nop",
     "violations do not change program execution",
     set_option, (unsigned)viol_nop, (unsigned *)&__mf_opts.violation_mode},
    {"viol-abort",
     "violations cause a call to abort()",
     set_option, (unsigned)viol_abort, (unsigned *)&__mf_opts.violation_mode},
    {"viol-segv",
     "violations are promoted to SIGSEGV signals",
     set_option, (unsigned)viol_segv, (unsigned *)&__mf_opts.violation_mode},
    {"viol-gdb",
     "violations fork a gdb process attached to current program",
     set_option, (unsigned)viol_gdb, (unsigned *)&__mf_opts.violation_mode},
    {"trace-calls",
     "trace calls to mudflap runtime library",
     set_option, 1, &__mf_opts.trace_mf_calls},
    {"verbose-trace",
     "trace internal events within mudflap runtime library",
     set_option, 1, &__mf_opts.verbose_trace},
    {"collect-stats",
     "collect statistics on mudflap's operation",
     set_option, 1, &__mf_opts.collect_stats},
#ifdef SIGUSR1
    {"sigusr1-report",
     "print report upon SIGUSR1",
     set_option, 1, &__mf_opts.sigusr1_report},
#endif
    {"internal-checking",
     "perform more expensive internal checking",
     set_option, 1, &__mf_opts.internal_checking},
    {"print-leaks",
     "print any memory leaks at program shutdown",
     set_option, 1, &__mf_opts.print_leaks},
    {"check-initialization",
     "detect uninitialized object reads",
     set_option, 1, &__mf_opts.check_initialization},
    {"verbose-violations",
     "print verbose messages when memory violations occur",
     set_option, 1, &__mf_opts.verbose_violations},
    {"abbreviate",
     "abbreviate repetitive listings",
     set_option, 1, &__mf_opts.abbreviate},
    {"timestamps",
     "track object lifetime timestamps",
     set_option, 1, &__mf_opts.timestamps},
    {"ignore-reads",
     "ignore read accesses - assume okay",
     set_option, 1, &__mf_opts.ignore_reads},
    {"wipe-stack",
     "wipe stack objects at unwind",
     set_option, 1, &__mf_opts.wipe_stack},
    {"wipe-heap",
     "wipe heap objects at free",
     set_option, 1, &__mf_opts.wipe_heap},
    {"heur-proc-map",
     "support /proc/self/map heuristics",
     set_option, 1, &__mf_opts.heur_proc_map},
    {"heur-stack-bound",
     "enable a simple upper stack bound heuristic",
     set_option, 1, &__mf_opts.heur_stack_bound},
    {"heur-start-end",
     "support _start.._end heuristics",
     set_option, 1, &__mf_opts.heur_start_end},
    {"heur-stdlib",
     "register standard library data (argv, errno, stdin, ...)",
     set_option, 1, &__mf_opts.heur_std_data},
    {"free-queue-length",
     "queue N deferred free() calls before performing them",
     read_integer_option, 0, &__mf_opts.free_queue_length},
    {"persistent-count",
     "keep a history of N unregistered regions",
     read_integer_option, 0, &__mf_opts.persistent_count},
    {"crumple-zone",
     "surround allocations with crumple zones of N bytes",
     read_integer_option, 0, &__mf_opts.crumple_zone},
    /* XXX: not type-safe.
    {"lc-mask",
     "set lookup cache size mask to N (2**M - 1)",
     read_integer_option, 0, (int *)(&__mf_lc_mask)},
    {"lc-shift",
     "set lookup cache pointer shift",
     read_integer_option, 0, (int *)(&__mf_lc_shift)},
    */
    {"lc-adapt",
     "adapt mask/shift parameters after N cache misses",
     read_integer_option, 1, &__mf_opts.adapt_cache},
    {"backtrace",
     "keep an N-level stack trace of each call context",
     read_integer_option, 0, &__mf_opts.backtrace},
#ifdef LIBMUDFLAPTH
    {"thread-stack",
     "override thread stacks allocation: N kB",
     read_integer_option, 0, &__mf_opts.thread_stack},
#endif
    {0, 0, set_option, 0, NULL}
  };

static void
__mf_usage ()
{
  struct option *opt;

  fprintf (stderr,
           "This is a %s%sGCC \"mudflap\" memory-checked binary.\n"
           "Mudflap is Copyright (C) 2002-2004 Free Software Foundation, Inc.\n"
           "\n"
           "The mudflap code can be controlled by an environment variable:\n"
           "\n"
           "$ export MUDFLAP_OPTIONS='<options>'\n"
           "$ <mudflapped_program>\n"
           "\n"
           "where <options> is a space-separated list of \n"
           "any of the following options.  Use `-no-OPTION' to disable options.\n"
           "\n",
#if HAVE_PTHREAD_H
           (pthread_join ? "multi-threaded " : "single-threaded "),
#else
           "",
#endif
#if LIBMUDFLAPTH
           "thread-aware "
#else
           "thread-unaware "
#endif
            );
  /* XXX: The multi-threaded thread-unaware combination is bad.  */

  for (opt = options; opt->name; opt++)
    {
      int default_p = (opt->value == * opt->target);

      switch (opt->type)
        {
          char buf[128];
        case set_option:
          fprintf (stderr, "-%-23.23s %s", opt->name, opt->description);
          if (default_p)
            fprintf (stderr, " [active]\n");
          else
            fprintf (stderr, "\n");
          break;
        case read_integer_option:
          strncpy (buf, opt->name, 128);
          strncpy (buf + strlen (opt->name), "=N", 2);
          fprintf (stderr, "-%-23.23s %s", buf, opt->description);
          fprintf (stderr, " [%d]\n", * opt->target);
          break;
        default: abort();
        }
    }

  fprintf (stderr, "\n");
}


int
__mf_set_options (const char *optstr)
{
  int rc;
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  rc = __mfu_set_options (optstr);
  /* XXX: It's not really that easy.  A change to a bunch of parameters
     can require updating auxiliary state or risk crashing:
     free_queue_length, crumple_zone ... */
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
  return rc;
}


int
__mfu_set_options (const char *optstr)
{
  struct option *opts = 0;
  char *nxt = 0;
  long tmp = 0;
  int rc = 0;
  const char *saved_optstr = optstr;

  /* XXX: bounds-check for optstr! */

  while (*optstr)
    {
      switch (*optstr) {
      case ' ':
      case '\t':
      case '\n':
        optstr++;
        break;

      case '-':
        if (*optstr+1)
          {
            int negate = 0;
            optstr++;

            if (*optstr == '?' ||
                strncmp (optstr, "help", 4) == 0)
              {
                /* Caller will print help and exit.  */
                return -1;
              }

            if (strncmp (optstr, "no-", 3) == 0)
              {
                negate = 1;
                optstr = & optstr[3];
              }

            for (opts = options; opts->name; opts++)
              {
                if (strncmp (optstr, opts->name, strlen (opts->name)) == 0)
                  {
                    optstr += strlen (opts->name);
                    assert (opts->target);
                    switch (opts->type)
                      {
                      case set_option:
                        if (negate)
                          *(opts->target) = 0;
                        else
                          *(opts->target) = opts->value;
                        break;
                      case read_integer_option:
                        if (! negate && (*optstr == '=' && *(optstr+1)))
                          {
                            optstr++;
                            tmp = strtol (optstr, &nxt, 10);
                            if ((optstr != nxt) && (tmp != LONG_MAX))
                              {
                                optstr = nxt;
                                *(opts->target) = (int)tmp;
                              }
                          }
                        else if (negate)
                          * opts->target = 0;
                        break;
                      }
                  }
              }
          }
        break;

      default:
        fprintf (stderr,
                 "warning: unrecognized string '%s' in mudflap options\n",
                 optstr);
        optstr += strlen (optstr);
        rc = -1;
        break;
      }
    }

  /* Special post-processing: bound __mf_lc_mask and free_queue_length for security. */
  __mf_lc_mask &= (LOOKUP_CACHE_SIZE_MAX - 1);
  __mf_opts.free_queue_length &= (__MF_FREEQ_MAX - 1);

  /* Clear the lookup cache, in case the parameters got changed.  */
  /* XXX: race */
  memset (__mf_lookup_cache, 0, sizeof(__mf_lookup_cache));
  /* void slot 0 */
  __mf_lookup_cache[0].low = MAXPTR;

  TRACE ("set options from `%s'\n", saved_optstr);

  /* Call this unconditionally, in case -sigusr1-report was toggled. */
  __mf_sigusr1_respond ();

  return rc;
}


#ifdef PIC

void
__mf_resolve_single_dynamic (struct __mf_dynamic_entry *e)
{
  char *err;

  assert (e);
  if (e->pointer) return;

#if HAVE_DLVSYM
  if (e->version != NULL && e->version[0] != '\0') /* non-null/empty */
    e->pointer = dlvsym (RTLD_NEXT, e->name, e->version);
  else
#endif
    e->pointer = dlsym (RTLD_NEXT, e->name);

  err = dlerror ();

  if (err)
    {
      fprintf (stderr, "mf: error in dlsym(\"%s\"): %s\n",
               e->name, err);
      abort ();
    }
  if (! e->pointer)
    {
      fprintf (stderr, "mf: dlsym(\"%s\") = NULL\n", e->name);
      abort ();
    }
}


static void
__mf_resolve_dynamics ()
{
  int i;
  for (i = 0; i < dyn_INITRESOLVE; i++)
    __mf_resolve_single_dynamic (& __mf_dynamic[i]);
}


/* NB: order must match enums in mf-impl.h */
struct __mf_dynamic_entry __mf_dynamic [] =
{
  {NULL, "calloc", NULL},
  {NULL, "free", NULL},
  {NULL, "malloc", NULL},
  {NULL, "mmap", NULL},
  {NULL, "munmap", NULL},
  {NULL, "realloc", NULL},
  {NULL, "DUMMY", NULL}, /* dyn_INITRESOLVE */
#ifdef LIBMUDFLAPTH
  {NULL, "pthread_create", PTHREAD_CREATE_VERSION},
  {NULL, "pthread_join", NULL},
  {NULL, "pthread_exit", NULL}
#endif
};

#endif /* PIC */



/* ------------------------------------------------------------------------ */

/* Lookup & manage automatic initialization of the five or so splay trees.  */
static mfsplay_tree
__mf_object_tree (int type)
{
  static mfsplay_tree trees [__MF_TYPE_MAX+1];
  assert (type >= 0 && type <= __MF_TYPE_MAX);
  if (UNLIKELY (trees[type] == NULL))
    trees[type] = mfsplay_tree_new ();
  return trees[type];
}


/* not static */void
__mf_init ()
{
  char *ov = 0;

  /* Return if initialization has already been done. */
  if (LIKELY (__mf_starting_p == 0))
    return;

  /* This initial bootstrap phase requires that __mf_starting_p = 1. */
#ifdef PIC
  __mf_resolve_dynamics ();
#endif
  __mf_starting_p = 0;

  __mf_set_state (active);

  __mf_set_default_options ();

  ov = getenv ("MUDFLAP_OPTIONS");
  if (ov)
    {
      int rc = __mfu_set_options (ov);
      if (rc < 0)
        {
          __mf_usage ();
          exit (1);
        }
    }

  /* Initialize to a non-zero description epoch. */
  __mf_describe_object (NULL);

#define REG_RESERVED(obj) \
  __mf_register (& obj, sizeof(obj), __MF_TYPE_NOACCESS, # obj)

  REG_RESERVED (__mf_lookup_cache);
  REG_RESERVED (__mf_lc_mask);
  REG_RESERVED (__mf_lc_shift);
  /* XXX: others of our statics?  */

  /* Prevent access to *NULL. */
  __mf_register (MINPTR, 1, __MF_TYPE_NOACCESS, "NULL");
  __mf_lookup_cache[0].low = (uintptr_t) -1;
}



int
__wrap_main (int argc, char* argv[])
{
  extern char **environ;
  extern int main ();
  extern int __real_main ();
  static int been_here = 0;

  if (__mf_opts.heur_std_data && ! been_here)
    {
      unsigned i;

      been_here = 1;
      __mf_register (argv, sizeof(char *)*(argc+1), __MF_TYPE_STATIC, "argv[]");
      for (i=0; i<argc; i++)
        {
          unsigned j = strlen (argv[i]);
          __mf_register (argv[i], j+1, __MF_TYPE_STATIC, "argv element");
        }

      for (i=0; ; i++)
        {
          char *e = environ[i];
          unsigned j;
          if (e == NULL) break;
          j = strlen (environ[i]);
          __mf_register (environ[i], j+1, __MF_TYPE_STATIC, "environ element");
        }
      __mf_register (environ, sizeof(char *)*(i+1), __MF_TYPE_STATIC, "environ[]");

      __mf_register (& errno, sizeof (errno), __MF_TYPE_STATIC, "errno area");

      __mf_register (stdin,  sizeof (*stdin),  __MF_TYPE_STATIC, "stdin");
      __mf_register (stdout, sizeof (*stdout), __MF_TYPE_STATIC, "stdout");
      __mf_register (stderr, sizeof (*stderr), __MF_TYPE_STATIC, "stderr");

      /* Make some effort to register ctype.h static arrays.  */
      /* XXX: e.g., on Solaris, may need to register __ctype, _ctype, __ctype_mask, __toupper, etc. */
      /* On modern Linux GLIBC, these are thread-specific and changeable, and are dealt
         with in mf-hooks2.c.  */
    }

#ifdef PIC
  return main (argc, argv, environ);
#else
  return __real_main (argc, argv, environ);
#endif
}



extern void __mf_fini () DTOR;
void __mf_fini ()
{
  TRACE ("__mf_fini\n");
  __mfu_report ();

#ifndef PIC
/* Since we didn't populate the tree for allocations in constructors
   before __mf_init, we cannot check destructors after __mf_fini.  */
  __mf_opts.mudflap_mode = mode_nop;
#endif
}



/* ------------------------------------------------------------------------ */
/* __mf_check */

void __mf_check (void *ptr, size_t sz, int type, const char *location)
{
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  __mfu_check (ptr, sz, type, location);
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
}


void __mfu_check (void *ptr, size_t sz, int type, const char *location)
{
  unsigned entry_idx = __MF_CACHE_INDEX (ptr);
  struct __mf_cache *entry = & __mf_lookup_cache [entry_idx];
  int judgement = 0; /* 0=undecided; <0=violation; >0=okay */
  uintptr_t ptr_low = (uintptr_t) ptr;
  uintptr_t ptr_high = CLAMPSZ (ptr, sz);
  struct __mf_cache old_entry = *entry;

  if (UNLIKELY (__mf_opts.sigusr1_report))
    __mf_sigusr1_respond ();
  if (UNLIKELY (__mf_opts.ignore_reads && type == 0))
    return;

  TRACE ("check ptr=%p b=%u size=%lu %s location=`%s'\n",
         ptr, entry_idx, (unsigned long)sz,
         (type == 0 ? "read" : "write"), location);

  switch (__mf_opts.mudflap_mode)
    {
    case mode_nop:
      /* It is tempting to poison the cache here similarly to
         mode_populate.  However that eliminates a valuable
         distinction between these two modes.  mode_nop is useful to
         let a user count & trace every single check / registration
         call.  mode_populate is useful to let a program run fast
         while unchecked.
      */
      judgement = 1;
      break;

    case mode_populate:
      entry->low = ptr_low;
      entry->high = ptr_high;
      judgement = 1;
      break;

    case mode_check:
      {
        unsigned heuristics = 0;

        /* Advance aging/adaptation counters.  */
        static unsigned adapt_count;
        adapt_count ++;
        if (UNLIKELY (__mf_opts.adapt_cache > 0 &&
                      adapt_count > __mf_opts.adapt_cache))
          {
            adapt_count = 0;
            __mf_adapt_cache ();
          }

        /* Looping only occurs if heuristics were triggered.  */
        while (judgement == 0)
          {
            DECLARE (void, free, void *p);
            __mf_object_t* ovr_obj[1];
            unsigned obj_count;
            __mf_object_t** all_ovr_obj = NULL;
            __mf_object_t** dealloc_me = NULL;
            unsigned i;

            /* Find all overlapping objects.  Be optimistic that there is just one.  */
            obj_count = __mf_find_objects (ptr_low, ptr_high, ovr_obj, 1);
            if (UNLIKELY (obj_count > 1))
              {
                /* Allocate a real buffer and do the search again.  */
                DECLARE (void *, malloc, size_t c);
                unsigned n;
                all_ovr_obj = CALL_REAL (malloc, (sizeof (__mf_object_t *) *
                                                   obj_count));
                if (all_ovr_obj == NULL) abort ();
                n = __mf_find_objects (ptr_low, ptr_high, all_ovr_obj, obj_count);
                assert (n == obj_count);
                dealloc_me = all_ovr_obj;
              }
            else
              {
                all_ovr_obj = ovr_obj;
                dealloc_me = NULL;
              }

            /* Update object statistics.  */
            for (i = 0; i < obj_count; i++)
              {
                __mf_object_t *obj = all_ovr_obj[i];
                assert (obj != NULL);
                if (type == __MF_CHECK_READ)
                  obj->read_count ++;
                else
                  obj->write_count ++;
                obj->liveness ++;
              }

            /* Iterate over the various objects.  There are a number of special cases.  */
            for (i = 0; i < obj_count; i++)
              {
                  __mf_object_t *obj = all_ovr_obj[i];

                /* Any __MF_TYPE_NOACCESS hit is bad.  */
                if (UNLIKELY (obj->type == __MF_TYPE_NOACCESS))
                  judgement = -1;

                /* Any object with a watch flag is bad.  */
                if (UNLIKELY (obj->watching_p))
                  judgement = -2; /* trigger VIOL_WATCH */

                /* A read from an uninitialized object is bad. */
                if (UNLIKELY (__mf_opts.check_initialization
                              /* reading */
                              && type == __MF_CHECK_READ
                              /* not written */
                              && obj->write_count == 0
                              /* uninitialized (heap) */
                              && obj->type == __MF_TYPE_HEAP))
                  judgement = -1;
              }

            /* We now know that the access spans no invalid objects.  */
            if (LIKELY (judgement >= 0))
              for (i = 0; i < obj_count; i++)
                {
                  __mf_object_t *obj = all_ovr_obj[i];

                  /* Is this access entirely contained within this object?  */
                  if (LIKELY (ptr_low >= obj->low && ptr_high <= obj->high))
                    {
                      /* Valid access.  */
                      entry->low = obj->low;
                      entry->high = obj->high;
                      judgement = 1;
                    }
                }

            /* This access runs off the end of one valid object.  That
                could be okay, if other valid objects fill in all the
                holes.  We allow this only for HEAP and GUESS type
                objects.  Accesses to STATIC and STACK variables
                should not be allowed to span.  */
            if (UNLIKELY ((judgement == 0) && (obj_count > 1)))
              {
                unsigned uncovered = 0;
                for (i = 0; i < obj_count; i++)
                  {
                    __mf_object_t *obj = all_ovr_obj[i];
                    int j, uncovered_low_p, uncovered_high_p;
                    uintptr_t ptr_lower, ptr_higher;

                    uncovered_low_p = ptr_low < obj->low;
                    ptr_lower = CLAMPSUB (obj->low, 1);
                    uncovered_high_p = ptr_high > obj->high;
                    ptr_higher = CLAMPADD (obj->high, 1);

                    for (j = 0; j < obj_count; j++)
                      {
                        __mf_object_t *obj2 = all_ovr_obj[j];

                        if (i == j) continue;

                        /* Filter out objects that cannot be spanned across.  */
                        if (obj2->type == __MF_TYPE_STACK
                            || obj2->type == __MF_TYPE_STATIC)
                          continue;

                          /* Consider a side "covered" if obj2 includes
                             the next byte on that side.  */
                          if (uncovered_low_p
                              && (ptr_lower >= obj2->low && ptr_lower <= obj2->high))
                            uncovered_low_p = 0;
                          if (uncovered_high_p
                              && (ptr_high >= obj2->low && ptr_higher <= obj2->high))
                            uncovered_high_p = 0;
                      }

                    if (uncovered_low_p || uncovered_high_p)
                      uncovered ++;
                  }

                /* Success if no overlapping objects are uncovered.  */
                if (uncovered == 0)
                  judgement = 1;
                }


            if (dealloc_me != NULL)
              CALL_REAL (free, dealloc_me);

            /* If the judgment is still unknown at this stage, loop
               around at most one more time.  */
            if (judgement == 0)
              {
                if (heuristics++ < 2) /* XXX parametrize this number? */
                  judgement = __mf_heuristic_check (ptr_low, ptr_high);
                else
                  judgement = -1;
              }
          }

      }
      break;

    case mode_violate:
      judgement = -1;
      break;
    }

  if (__mf_opts.collect_stats)
    {
      __mf_count_check ++;

      if (LIKELY (old_entry.low != entry->low || old_entry.high != entry->high))
        /* && (old_entry.low != 0) && (old_entry.high != 0)) */
        __mf_lookup_cache_reusecount [entry_idx] ++;
    }

  if (UNLIKELY (judgement < 0))
    __mf_violation (ptr, sz,
                    (uintptr_t) __builtin_return_address (0), location,
                    ((judgement == -1) ?
                     (type == __MF_CHECK_READ ? __MF_VIOL_READ : __MF_VIOL_WRITE) :
                     __MF_VIOL_WATCH));
}


static __mf_object_t *
__mf_insert_new_object (uintptr_t low, uintptr_t high, int type,
                        const char *name, uintptr_t pc)
{
  DECLARE (void *, calloc, size_t c, size_t n);

  __mf_object_t *new_obj;
  new_obj = CALL_REAL (calloc, 1, sizeof(__mf_object_t));
  new_obj->low = low;
  new_obj->high = high;
  new_obj->type = type;
  new_obj->name = name;
  new_obj->alloc_pc = pc;
#if HAVE_GETTIMEOFDAY
  if (__mf_opts.timestamps)
    gettimeofday (& new_obj->alloc_time, NULL);
#endif
#if LIBMUDFLAPTH
  new_obj->alloc_thread = pthread_self ();
#endif

  if (__mf_opts.backtrace > 0 && (type == __MF_TYPE_HEAP || type == __MF_TYPE_HEAP_I))
    new_obj->alloc_backtrace_size =
      __mf_backtrace (& new_obj->alloc_backtrace,
                      (void *) pc, 2);

  __mf_link_object (new_obj);
  return new_obj;
}


static void
__mf_uncache_object (__mf_object_t *old_obj)
{
  /* Remove any low/high pointers for this object from the lookup cache.  */

  /* Can it possibly exist in the cache?  */
  if (LIKELY (old_obj->read_count + old_obj->write_count))
    {
      /* As reported by Herman ten Brugge, we need to scan the entire
         cache for entries that may hit this object. */
      uintptr_t low = old_obj->low;
      uintptr_t high = old_obj->high;
      struct __mf_cache *entry = & __mf_lookup_cache [0];
      unsigned i;
      for (i = 0; i <= __mf_lc_mask; i++, entry++)
        {
          /* NB: the "||" in the following test permits this code to
             tolerate the situation introduced by __mf_check over
             contiguous objects, where a cache entry spans several
             objects.  */
          if (entry->low == low || entry->high == high)
            {
              entry->low = MAXPTR;
              entry->high = MINPTR;
            }
        }
    }
}


void
__mf_register (void *ptr, size_t sz, int type, const char *name)
{
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  __mfu_register (ptr, sz, type, name);
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
}


void
__mfu_register (void *ptr, size_t sz, int type, const char *name)
{
  TRACE ("register ptr=%p size=%lu type=%x name='%s'\n",
         ptr, (unsigned long) sz, type, name ? name : "");

  if (__mf_opts.collect_stats)
    {
      __mf_count_register ++;
      __mf_total_register_size [(type < 0) ? 0 :
                                (type > __MF_TYPE_MAX) ? 0 :
                                type] += sz;
    }

  if (UNLIKELY (__mf_opts.sigusr1_report))
    __mf_sigusr1_respond ();

  switch (__mf_opts.mudflap_mode)
    {
    case mode_nop:
      break;

    case mode_violate:
      __mf_violation (ptr, sz, (uintptr_t) __builtin_return_address (0), NULL,
                      __MF_VIOL_REGISTER);
      break;

    case mode_populate:
      /* Clear the cache.  */
      /* XXX: why the entire cache? */
      /* XXX: race */
      memset (__mf_lookup_cache, 0, sizeof(__mf_lookup_cache));
      /* void slot 0 */
      __mf_lookup_cache[0].low = MAXPTR;
      break;

    case mode_check:
      {
        __mf_object_t *ovr_objs [1];
        unsigned num_overlapping_objs;
        uintptr_t low = (uintptr_t) ptr;
        uintptr_t high = CLAMPSZ (ptr, sz);
        uintptr_t pc = (uintptr_t) __builtin_return_address (0);

        /* Treat unknown size indication as 1.  */
        if (UNLIKELY (sz == 0)) sz = 1;

        /* Look for objects only of the same type.  This will e.g. permit a registration
           of a STATIC overlapping with a GUESS, and a HEAP with a NOACCESS.  At
           __mf_check time however harmful overlaps will be detected. */
        num_overlapping_objs = __mf_find_objects2 (low, high, ovr_objs, 1, type);

        /* Handle overlaps.  */
        if (UNLIKELY (num_overlapping_objs > 0))
          {
            __mf_object_t *ovr_obj = ovr_objs[0];

            /* Accept certain specific duplication pairs.  */
            if (((type == __MF_TYPE_STATIC) || (type == __MF_TYPE_GUESS))
                && ovr_obj->low == low
                && ovr_obj->high == high
                && ovr_obj->type == type)
              {
                /* Duplicate registration for static objects may come
                   from distinct compilation units.  */
                VERBOSE_TRACE ("harmless duplicate reg %p-%p `%s'\n",
                               (void *) low, (void *) high,
                               (ovr_obj->name ? ovr_obj->name : ""));
                break;
              }

            /* Alas, a genuine violation.  */
            else
              {
                /* Two or more *real* mappings here. */
                __mf_violation ((void *) ptr, sz,
                                (uintptr_t) __builtin_return_address (0), NULL,
                                __MF_VIOL_REGISTER);
              }
          }
        else /* No overlapping objects: AOK.  */
          __mf_insert_new_object (low, high, type, name, pc);

        /* We could conceivably call __mf_check() here to prime the cache,
           but then the read_count/write_count field is not reliable.  */
        break;
      }
    } /* end switch (__mf_opts.mudflap_mode) */
}


void
__mf_unregister (void *ptr, size_t sz, int type)
{
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  __mfu_unregister (ptr, sz, type);
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
}


void
__mfu_unregister (void *ptr, size_t sz, int type)
{
  DECLARE (void, free, void *ptr);

  if (UNLIKELY (__mf_opts.sigusr1_report))
    __mf_sigusr1_respond ();

  TRACE ("unregister ptr=%p size=%lu type=%x\n", ptr, (unsigned long) sz, type);

  switch (__mf_opts.mudflap_mode)
    {
    case mode_nop:
      break;

    case mode_violate:
      __mf_violation (ptr, sz,
                      (uintptr_t) __builtin_return_address (0), NULL,
                      __MF_VIOL_UNREGISTER);
      break;

    case mode_populate:
      /* Clear the cache.  */
      /* XXX: race */
      memset (__mf_lookup_cache, 0, sizeof(__mf_lookup_cache));
      /* void slot 0 */
      __mf_lookup_cache[0].low = MAXPTR;
      break;

    case mode_check:
      {
        __mf_object_t *old_obj = NULL;
        __mf_object_t *del_obj = NULL;  /* Object to actually delete. */
        __mf_object_t *objs[1] = {NULL};
        unsigned num_overlapping_objs;

        num_overlapping_objs = __mf_find_objects2 ((uintptr_t) ptr,
                                                   CLAMPSZ (ptr, sz), objs, 1, type);

        /* Special case for HEAP_I - see free & realloc hook.  They don't
           know whether the input region was HEAP or HEAP_I before
           unmapping it.  Here we give HEAP a try in case HEAP_I
           failed.  */
        if ((type == __MF_TYPE_HEAP_I) && (num_overlapping_objs == 0))
          {
            num_overlapping_objs = __mf_find_objects2 ((uintptr_t) ptr,
                                                       CLAMPSZ (ptr, sz), objs, 1, __MF_TYPE_HEAP);
          }

        old_obj = objs[0];
        if (UNLIKELY ((num_overlapping_objs != 1) /* more than one overlap */
                      || ((sz == 0) ? 0 : (sz != (old_obj->high - old_obj->low + 1))) /* size mismatch */
                      || ((uintptr_t) ptr != old_obj->low))) /* base mismatch */
          {
            __mf_violation (ptr, sz,
                            (uintptr_t) __builtin_return_address (0), NULL,
                            __MF_VIOL_UNREGISTER);
            break;
          }

        __mf_unlink_object (old_obj);
        __mf_uncache_object (old_obj);

        /* Wipe buffer contents if desired.  */
        if ((__mf_opts.wipe_stack && old_obj->type == __MF_TYPE_STACK)
            || (__mf_opts.wipe_heap && (old_obj->type == __MF_TYPE_HEAP
                                        || old_obj->type == __MF_TYPE_HEAP_I)))
          {
            memset ((void *) old_obj->low,
                    0,
                    (size_t) (old_obj->high - old_obj->low + 1));
          }

        /* Manage the object cemetary.  */
        if (__mf_opts.persistent_count > 0
	    && (unsigned) old_obj->type <= __MF_TYPE_MAX_CEM)
          {
            old_obj->deallocated_p = 1;
            old_obj->dealloc_pc = (uintptr_t) __builtin_return_address (0);
#if HAVE_GETTIMEOFDAY
            if (__mf_opts.timestamps)
              gettimeofday (& old_obj->dealloc_time, NULL);
#endif
#ifdef LIBMUDFLAPTH
            old_obj->dealloc_thread = pthread_self ();
#endif

            if (__mf_opts.backtrace > 0 && old_obj->type == __MF_TYPE_HEAP)
              old_obj->dealloc_backtrace_size =
                __mf_backtrace (& old_obj->dealloc_backtrace,
                                NULL, 2);

            /* Encourage this object to be displayed again in current epoch.  */
            old_obj->description_epoch --;

            /* Put this object into the cemetary.  This may require this plot to
               be recycled, and the previous resident to be designated del_obj.  */
            {
              unsigned row = old_obj->type;
              unsigned plot = __mf_object_dead_head [row];

              del_obj = __mf_object_cemetary [row][plot];
              __mf_object_cemetary [row][plot] = old_obj;
              plot ++;
              if (plot == __mf_opts.persistent_count) plot = 0;
              __mf_object_dead_head [row] = plot;
            }
          }
        else
          del_obj = old_obj;

        if (__mf_opts.print_leaks)
          {
            if ((old_obj->read_count + old_obj->write_count) == 0 &&
                (old_obj->type == __MF_TYPE_HEAP
                 || old_obj->type == __MF_TYPE_HEAP_I))
              {
                fprintf (stderr,
                         "*******\n"
                         "mudflap warning: unaccessed registered object:\n");
                __mf_describe_object (old_obj);
              }
          }

        if (del_obj != NULL) /* May or may not equal old_obj.  */
          {
            if (__mf_opts.backtrace > 0)
              {
                CALL_REAL(free, del_obj->alloc_backtrace);
                if (__mf_opts.persistent_count > 0)
                  {
                    CALL_REAL(free, del_obj->dealloc_backtrace);
                  }
              }
            CALL_REAL(free, del_obj);
          }

        break;
      }
    } /* end switch (__mf_opts.mudflap_mode) */


  if (__mf_opts.collect_stats)
    {
      __mf_count_unregister ++;
      __mf_total_unregister_size += sz;
    }
}



struct tree_stats
{
  unsigned obj_count;
  unsigned long total_size;
  unsigned live_obj_count;
  double total_weight;
  double weighted_size;
  unsigned long weighted_address_bits [sizeof (uintptr_t) * 8][2];
};



static int
__mf_adapt_cache_fn (mfsplay_tree_node n, void *param)
{
  __mf_object_t *obj = (__mf_object_t *) n->value;
  struct tree_stats *s = (struct tree_stats *) param;

  assert (obj != NULL && s != NULL);

  /* Exclude never-accessed objects.  */
  if (obj->read_count + obj->write_count)
    {
      s->obj_count ++;
      s->total_size += (obj->high - obj->low + 1);

      if (obj->liveness)
        {
          unsigned i;
          uintptr_t addr;

          /* VERBOSE_TRACE ("analyze low=%p live=%u name=`%s'\n",
             (void *) obj->low, obj->liveness, obj->name); */

          s->live_obj_count ++;
          s->total_weight += (double) obj->liveness;
          s->weighted_size +=
            (double) (obj->high - obj->low + 1) *
            (double) obj->liveness;

          addr = obj->low;
          for (i=0; i<sizeof(uintptr_t) * 8; i++)
            {
              unsigned bit = addr & 1;
              s->weighted_address_bits[i][bit] += obj->liveness;
              addr = addr >> 1;
            }

          /* Age the liveness value.  */
          obj->liveness >>= 1;
        }
    }

  return 0;
}


static void
__mf_adapt_cache ()
{
  struct tree_stats s;
  uintptr_t new_mask = 0;
  unsigned char new_shift;
  float cache_utilization;
  float max_value;
  static float smoothed_new_shift = -1.0;
  unsigned i;

  memset (&s, 0, sizeof (s));

  mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_HEAP), __mf_adapt_cache_fn, (void *) & s);
  mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_HEAP_I), __mf_adapt_cache_fn, (void *) & s);
  mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_STACK), __mf_adapt_cache_fn, (void *) & s);
  mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_STATIC), __mf_adapt_cache_fn, (void *) & s);
  mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_GUESS), __mf_adapt_cache_fn, (void *) & s);

  /* Maybe we're dealing with funny aging/adaptation parameters, or an
     empty tree.  Just leave the cache alone in such cases, rather
     than risk dying by division-by-zero.  */
  if (! (s.obj_count > 0) && (s.live_obj_count > 0) && (s.total_weight > 0.0))
    return;

  /* Guess a good value for the shift parameter by finding an address bit that is a
     good discriminant of lively objects.  */
  max_value = 0.0;
  for (i=0; i<sizeof (uintptr_t)*8; i++)
    {
      float value = (float) s.weighted_address_bits[i][0] * (float) s.weighted_address_bits[i][1];
      if (max_value < value) max_value = value;
    }
  for (i=0; i<sizeof (uintptr_t)*8; i++)
    {
      float shoulder_factor = 0.7;  /* Include slightly less popular bits too.  */
      float value = (float) s.weighted_address_bits[i][0] * (float) s.weighted_address_bits[i][1];
      if (value >= max_value * shoulder_factor)
        break;
    }
  if (smoothed_new_shift < 0) smoothed_new_shift = __mf_lc_shift;
  /* Converge toward this slowly to reduce flapping. */
  smoothed_new_shift = 0.9*smoothed_new_shift + 0.1*i;
  new_shift = (unsigned) (smoothed_new_shift + 0.5);
  assert (new_shift < sizeof (uintptr_t)*8);

  /* Count number of used buckets.  */
  cache_utilization = 0.0;
  for (i = 0; i < (1 + __mf_lc_mask); i++)
    if (__mf_lookup_cache[i].low != 0 || __mf_lookup_cache[i].high != 0)
      cache_utilization += 1.0;
  cache_utilization /= (1 + __mf_lc_mask);

  new_mask |= 0xffff; /* XXX: force a large cache.  */
  new_mask &= (LOOKUP_CACHE_SIZE_MAX - 1);

  VERBOSE_TRACE ("adapt cache obj=%u/%u sizes=%lu/%.0f/%.0f => "
                 "util=%u%% m=%p s=%u\n",
                 s.obj_count, s.live_obj_count, s.total_size, s.total_weight, s.weighted_size,
                 (unsigned)(cache_utilization*100.0), (void *) new_mask, new_shift);

  /* We should reinitialize cache if its parameters have changed.  */
  if (new_mask != __mf_lc_mask ||
      new_shift != __mf_lc_shift)
    {
      __mf_lc_mask = new_mask;
      __mf_lc_shift = new_shift;
      /* XXX: race */
      memset (__mf_lookup_cache, 0, sizeof(__mf_lookup_cache));
      /* void slot 0 */
      __mf_lookup_cache[0].low = MAXPTR;
    }
}



/* __mf_find_object[s] */

/* Find overlapping live objecs between [low,high].  Return up to
   max_objs of their pointers in objs[].  Return total count of
   overlaps (may exceed max_objs). */

unsigned
__mf_find_objects2 (uintptr_t ptr_low, uintptr_t ptr_high,
                    __mf_object_t **objs, unsigned max_objs, int type)
{
  unsigned count = 0;
  mfsplay_tree t = __mf_object_tree (type);
  mfsplay_tree_key k = (mfsplay_tree_key) ptr_low;
  int direction;

  mfsplay_tree_node n = mfsplay_tree_lookup (t, k);
  /* An exact match for base address implies a hit.  */
  if (n != NULL)
    {
      if (count < max_objs)
        objs[count] = (__mf_object_t *) n->value;
      count ++;
    }

  /* Iterate left then right near this key value to find all overlapping objects. */
  for (direction = 0; direction < 2; direction ++)
    {
      /* Reset search origin.  */
      k = (mfsplay_tree_key) ptr_low;

      while (1)
        {
          __mf_object_t *obj;

          n = (direction == 0 ? mfsplay_tree_successor (t, k) : mfsplay_tree_predecessor (t, k));
          if (n == NULL) break;
          obj = (__mf_object_t *) n->value;

          if (! (obj->low <= ptr_high && obj->high >= ptr_low)) /* No overlap? */
            break;

          if (count < max_objs)
            objs[count] = (__mf_object_t *) n->value;
          count ++;

          k = (mfsplay_tree_key) obj->low;
        }
    }

  return count;
}


unsigned
__mf_find_objects (uintptr_t ptr_low, uintptr_t ptr_high,
                   __mf_object_t **objs, unsigned max_objs)
{
  int type;
  unsigned count = 0;

  /* Search each splay tree for overlaps.  */
  for (type = __MF_TYPE_NOACCESS; type <= __MF_TYPE_GUESS; type++)
    {
      unsigned c = __mf_find_objects2 (ptr_low, ptr_high, objs, max_objs, type);
      if (c > max_objs)
        {
          max_objs = 0;
          objs = NULL;
        }
      else /* NB: C may equal 0 */
        {
          max_objs -= c;
          objs += c;
        }
      count += c;
    }

  return count;
}



/* __mf_link_object */

static void
__mf_link_object (__mf_object_t *node)
{
  mfsplay_tree t = __mf_object_tree (node->type);
  mfsplay_tree_insert (t, (mfsplay_tree_key) node->low, (mfsplay_tree_value) node);
}

/* __mf_unlink_object */

static void
__mf_unlink_object (__mf_object_t *node)
{
  mfsplay_tree t = __mf_object_tree (node->type);
  mfsplay_tree_remove (t, (mfsplay_tree_key) node->low);
}

/* __mf_find_dead_objects */

/* Find overlapping dead objecs between [low,high].  Return up to
   max_objs of their pointers in objs[].  Return total count of
   overlaps (may exceed max_objs).  */

static unsigned
__mf_find_dead_objects (uintptr_t low, uintptr_t high,
                        __mf_object_t **objs, unsigned max_objs)
{
  if (__mf_opts.persistent_count > 0)
    {
      unsigned count = 0;
      unsigned recollection = 0;
      unsigned row = 0;

      assert (low <= high);
      assert (max_objs == 0 || objs != NULL);

      /* Widen the search from the most recent plots in each row, looking
         backward in time.  */
      recollection = 0;
      while (recollection < __mf_opts.persistent_count)
        {
          count = 0;

          for (row = 0; row <= __MF_TYPE_MAX_CEM; row ++)
            {
              unsigned plot;
              unsigned i;

              plot = __mf_object_dead_head [row];
              for (i = 0; i <= recollection; i ++)
                {
                  __mf_object_t *obj;

                  /* Look backward through row: it's a circular buffer.  */
                  if (plot > 0) plot --;
                  else plot = __mf_opts.persistent_count - 1;

                  obj = __mf_object_cemetary [row][plot];
                  if (obj && obj->low <= high && obj->high >= low)
                    {
                      /* Found an overlapping dead object!  */
                      if (count < max_objs)
                        objs [count] = obj;
                      count ++;
                    }
                }
            }

          if (count)
            break;

          /* Look farther back in time.  */
          recollection = (recollection * 2) + 1;
        }

      return count;
    } else {
      return 0;
    }
}

/* __mf_describe_object */

static void
__mf_describe_object (__mf_object_t *obj)
{
  static unsigned epoch = 0;
  if (obj == NULL)
    {
      epoch ++;
      return;
    }

  if (__mf_opts.abbreviate && obj->description_epoch == epoch)
    {
      fprintf (stderr,
               "mudflap %sobject %p: name=`%s'\n",
               (obj->deallocated_p ? "dead " : ""),
               (void *) obj, (obj->name ? obj->name : ""));
      return;
    }
  else
    obj->description_epoch = epoch;

  fprintf (stderr,
           "mudflap %sobject %p: name=`%s'\n"
           "bounds=[%p,%p] size=%lu area=%s check=%ur/%uw liveness=%u%s\n"
           "alloc time=%lu.%06lu pc=%p"
#ifdef LIBMUDFLAPTH
           " thread=%u"
#endif
           "\n",
           (obj->deallocated_p ? "dead " : ""),
           (void *) obj, (obj->name ? obj->name : ""),
           (void *) obj->low, (void *) obj->high,
           (unsigned long) (obj->high - obj->low + 1),
           (obj->type == __MF_TYPE_NOACCESS ? "no-access" :
            obj->type == __MF_TYPE_HEAP ? "heap" :
            obj->type == __MF_TYPE_HEAP_I ? "heap-init" :
            obj->type == __MF_TYPE_STACK ? "stack" :
            obj->type == __MF_TYPE_STATIC ? "static" :
            obj->type == __MF_TYPE_GUESS ? "guess" :
            "unknown"),
           obj->read_count, obj->write_count, obj->liveness,
           obj->watching_p ? " watching" : "",
           obj->alloc_time.tv_sec, obj->alloc_time.tv_usec,
           (void *) obj->alloc_pc
#ifdef LIBMUDFLAPTH
           , (unsigned) obj->alloc_thread
#endif
           );

  if (__mf_opts.backtrace > 0)
  {
    unsigned i;
    for (i=0; i<obj->alloc_backtrace_size; i++)
      fprintf (stderr, "      %s\n", obj->alloc_backtrace[i]);
  }

  if (__mf_opts.persistent_count > 0)
    {
      if (obj->deallocated_p)
        {
          fprintf (stderr, "dealloc time=%lu.%06lu pc=%p"
#ifdef LIBMUDFLAPTH
                   " thread=%u"
#endif
                   "\n",
                   obj->dealloc_time.tv_sec, obj->dealloc_time.tv_usec,
                   (void *) obj->dealloc_pc
#ifdef LIBMUDFLAPTH
                   , (unsigned) obj->dealloc_thread
#endif
                   );


          if (__mf_opts.backtrace > 0)
          {
            unsigned i;
            for (i=0; i<obj->dealloc_backtrace_size; i++)
              fprintf (stderr, "      %s\n", obj->dealloc_backtrace[i]);
          }
        }
    }
}


static int
__mf_report_leaks_fn (mfsplay_tree_node n, void *param)
{
  __mf_object_t *node = (__mf_object_t *) n->value;
  unsigned *count = (unsigned *) param;

  if (count != NULL)
    (*count) ++;

  fprintf (stderr, "Leaked object %u:\n", (*count));
  __mf_describe_object (node);

  return 0;
}


static unsigned
__mf_report_leaks ()
{
  unsigned count = 0;

  (void) mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_HEAP),
                             __mf_report_leaks_fn, & count);
  (void) mfsplay_tree_foreach (__mf_object_tree (__MF_TYPE_HEAP_I),
                             __mf_report_leaks_fn, & count);

  return count;
}

/* ------------------------------------------------------------------------ */
/* __mf_report */

void
__mf_report ()
{
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  __mfu_report ();
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
}

void
__mfu_report ()
{
  if (__mf_opts.collect_stats)
    {
      fprintf (stderr,
               "*******\n"
               "mudflap stats:\n"
               "calls to __mf_check: %lu\n"
               "         __mf_register: %lu [%luB, %luB, %luB, %luB, %luB]\n"
               "         __mf_unregister: %lu [%luB]\n"
               "         __mf_violation: [%lu, %lu, %lu, %lu, %lu]\n",
               __mf_count_check,
               __mf_count_register,
               __mf_total_register_size[0], __mf_total_register_size[1],
               __mf_total_register_size[2], __mf_total_register_size[3],
               __mf_total_register_size[4], /* XXX */
               __mf_count_unregister, __mf_total_unregister_size,
               __mf_count_violation[0], __mf_count_violation[1],
               __mf_count_violation[2], __mf_count_violation[3],
               __mf_count_violation[4]);

      fprintf (stderr,
               "calls with reentrancy: %lu\n", __mf_reentrancy);
#ifdef LIBMUDFLAPTH
      fprintf (stderr,
               "           lock contention: %lu\n", __mf_lock_contention);
#endif

      /* Lookup cache stats.  */
      {
        unsigned i;
        unsigned max_reuse = 0;
        unsigned num_used = 0;
        unsigned num_unused = 0;

        for (i = 0; i < LOOKUP_CACHE_SIZE; i++)
          {
            if (__mf_lookup_cache_reusecount[i])
              num_used ++;
            else
              num_unused ++;
            if (max_reuse < __mf_lookup_cache_reusecount[i])
              max_reuse = __mf_lookup_cache_reusecount[i];
          }
        fprintf (stderr, "lookup cache slots used: %u  unused: %u  peak-reuse: %u\n",
                 num_used, num_unused, max_reuse);
      }

      {
        unsigned live_count;
        live_count = __mf_find_objects (MINPTR, MAXPTR, NULL, 0);
        fprintf (stderr, "number of live objects: %u\n", live_count);
      }

      if (__mf_opts.persistent_count > 0)
        {
          unsigned dead_count = 0;
          unsigned row, plot;
          for (row = 0; row <= __MF_TYPE_MAX_CEM; row ++)
            for (plot = 0 ; plot < __mf_opts.persistent_count; plot ++)
              if (__mf_object_cemetary [row][plot] != 0)
                dead_count ++;
          fprintf (stderr, "          zombie objects: %u\n", dead_count);
        }
    }
  if (__mf_opts.print_leaks && (__mf_opts.mudflap_mode == mode_check))
    {
      unsigned l;
      extern void * __mf_wrap_alloca_indirect (size_t c);

      /* Free up any remaining alloca()'d blocks.  */
      __mf_wrap_alloca_indirect (0);
      __mf_describe_object (NULL); /* Reset description epoch.  */
      l = __mf_report_leaks ();
      fprintf (stderr, "number of leaked objects: %u\n", l);
    }
}

/* __mf_backtrace */

size_t
__mf_backtrace (char ***symbols, void *guess_pc, unsigned guess_omit_levels)
{
  void ** pc_array;
  unsigned pc_array_size = __mf_opts.backtrace + guess_omit_levels;
  unsigned remaining_size;
  unsigned omitted_size = 0;
  unsigned i;
  DECLARE (void, free, void *ptr);
  DECLARE (void *, calloc, size_t c, size_t n);
  DECLARE (void *, malloc, size_t n);

  pc_array = CALL_REAL (calloc, pc_array_size, sizeof (void *) );
#ifdef HAVE_BACKTRACE
  pc_array_size = backtrace (pc_array, pc_array_size);
#else
#define FETCH(n) do { if (pc_array_size >= n) { \
                 pc_array[n] = __builtin_return_address(n); \
                 if (pc_array[n] == 0) pc_array_size = n; } } while (0)

  /* Unroll some calls __builtin_return_address because this function
     only takes a literal integer parameter.  */
  FETCH (0);
#if 0
  /* XXX: __builtin_return_address sometimes crashes (!) on >0 arguments,
     rather than simply returning 0.  :-(  */
  FETCH (1);
  FETCH (2);
  FETCH (3);
  FETCH (4);
  FETCH (5);
  FETCH (6);
  FETCH (7);
  FETCH (8);
  if (pc_array_size > 8) pc_array_size = 9;
#else
  if (pc_array_size > 0) pc_array_size = 1;
#endif

#undef FETCH
#endif

  /* We want to trim the first few levels of the stack traceback,
     since they contain libmudflap wrappers and junk.  If pc_array[]
     ends up containing a non-NULL guess_pc, then trim everything
     before that.  Otherwise, omit the first guess_omit_levels
     entries. */

  if (guess_pc != NULL)
    for (i=0; i<pc_array_size; i++)
      if (pc_array [i] == guess_pc)
        omitted_size = i;

  if (omitted_size == 0) /* No match? */
    if (pc_array_size > guess_omit_levels)
      omitted_size = guess_omit_levels;

  remaining_size = pc_array_size - omitted_size;

#ifdef HAVE_BACKTRACE_SYMBOLS
  *symbols = backtrace_symbols (pc_array + omitted_size, remaining_size);
#else
  {
    /* Let's construct a buffer by hand.  It will have <remaining_size>
       char*'s at the front, pointing at individual strings immediately
       afterwards.  */
    void *buffer;
    char *chars;
    char **pointers;
    enum { perline = 30 };
    buffer = CALL_REAL (malloc, remaining_size * (perline + sizeof(char *)));
    pointers = (char **) buffer;
    chars = (char *)buffer + (remaining_size * sizeof (char *));
    for (i = 0; i < remaining_size; i++)
      {
        pointers[i] = chars;
        sprintf (chars, "[0x%p]", pc_array [omitted_size + i]);
        chars = chars + perline;
      }
    *symbols = pointers;
  }
#endif
  CALL_REAL (free, pc_array);

  return remaining_size;
}

/* ------------------------------------------------------------------------ */
/* __mf_violation */

void
__mf_violation (void *ptr, size_t sz, uintptr_t pc,
                const char *location, int type)
{
  char buf [128];
  static unsigned violation_number;
  DECLARE(void, free, void *ptr);

  TRACE ("violation pc=%p location=%s type=%d ptr=%p size=%lu\n",
         (void *) pc,
         (location != NULL ? location : ""), type, ptr, (unsigned long) sz);

  if (__mf_opts.collect_stats)
    __mf_count_violation [(type < 0) ? 0 :
                          (type > __MF_VIOL_WATCH) ? 0 :
                          type] ++;

  /* Print out a basic warning message.  */
  if (__mf_opts.verbose_violations)
  {
    unsigned dead_p;
    unsigned num_helpful = 0;
    struct timeval now = { 0, 0 };
#if HAVE_GETTIMEOFDAY
    gettimeofday (& now, NULL);
#endif

    violation_number ++;
    fprintf (stderr,
             "*******\n"
             "mudflap violation %u (%s): time=%lu.%06lu "
             "ptr=%p size=%lu\npc=%p%s%s%s\n",
             violation_number,
             ((type == __MF_VIOL_READ) ? "check/read" :
              (type == __MF_VIOL_WRITE) ? "check/write" :
              (type == __MF_VIOL_REGISTER) ? "register" :
              (type == __MF_VIOL_UNREGISTER) ? "unregister" :
              (type == __MF_VIOL_WATCH) ? "watch" : "unknown"),
             now.tv_sec, now.tv_usec,
             (void *) ptr, (unsigned long)sz, (void *) pc,
             (location != NULL ? " location=`" : ""),
             (location != NULL ? location : ""),
             (location != NULL ? "'" : ""));

    if (__mf_opts.backtrace > 0)
      {
        char ** symbols;
        unsigned i, num;

        num = __mf_backtrace (& symbols, (void *) pc, 2);
        /* Note: backtrace_symbols calls malloc().  But since we're in
           __mf_violation and presumably __mf_check, it'll detect
           recursion, and not put the new string into the database.  */

        for (i=0; i<num; i++)
          fprintf (stderr, "      %s\n", symbols[i]);

        /* Calling free() here would trigger a violation.  */
        CALL_REAL(free, symbols);
      }


    /* Look for nearby objects.  For this, we start with s_low/s_high
       pointing to the given area, looking for overlapping objects.
       If none show up, widen the search area and keep looking. */

    if (sz == 0) sz = 1;

    for (dead_p = 0; dead_p <= 1; dead_p ++) /* for dead_p in 0 1 */
      {
        enum {max_objs = 3}; /* magic */
        __mf_object_t *objs[max_objs];
        unsigned num_objs = 0;
        uintptr_t s_low, s_high;
        unsigned tries = 0;
        unsigned i;

        s_low = (uintptr_t) ptr;
        s_high = CLAMPSZ (ptr, sz);

        while (tries < 16) /* magic */
          {
            if (dead_p)
              num_objs = __mf_find_dead_objects (s_low, s_high, objs, max_objs);
            else
              num_objs = __mf_find_objects (s_low, s_high, objs, max_objs);

            if (num_objs) /* good enough */
              break;

            tries ++;

            /* XXX: tune this search strategy.  It's too dependent on
             sz, which can vary from 1 to very big (when array index
             checking) numbers. */
            s_low = CLAMPSUB (s_low, (sz * tries * tries));
            s_high = CLAMPADD (s_high, (sz * tries * tries));
          }

        for (i = 0; i < min (num_objs, max_objs); i++)
          {
            __mf_object_t *obj = objs[i];
            uintptr_t low = (uintptr_t) ptr;
            uintptr_t high = CLAMPSZ (ptr, sz);
            unsigned before1 = (low < obj->low) ? obj->low - low : 0;
            unsigned after1 = (low > obj->high) ? low - obj->high : 0;
            unsigned into1 = (high >= obj->low && low <= obj->high) ? low - obj->low : 0;
            unsigned before2 = (high < obj->low) ? obj->low - high : 0;
            unsigned after2 = (high > obj->high) ? high - obj->high : 0;
            unsigned into2 = (high >= obj->low && low <= obj->high) ? high - obj->low : 0;

            fprintf (stderr, "Nearby object %u: checked region begins %uB %s and ends %uB %s\n",
                     num_helpful + i + 1,
                     (before1 ? before1 : after1 ? after1 : into1),
                     (before1 ? "before" : after1 ? "after" : "into"),
                     (before2 ? before2 : after2 ? after2 : into2),
                     (before2 ? "before" : after2 ? "after" : "into"));
            __mf_describe_object (obj);
          }
        num_helpful += num_objs;
      }

    fprintf (stderr, "number of nearby objects: %u\n", num_helpful);
  }

  /* How to finally handle this violation?  */
  switch (__mf_opts.violation_mode)
    {
    case viol_nop:
      break;
    case viol_segv:
      kill (getpid(), SIGSEGV);
      break;
    case viol_abort:
      abort ();
      break;
    case viol_gdb:

      snprintf (buf, 128, "gdb --pid=%u", (unsigned) getpid ());
      system (buf);
      /* XXX: should probably fork() && sleep(GDB_WAIT_PARAMETER)
      instead, and let the forked child execlp() gdb.  That way, this
      subject process can be resumed under the supervision of gdb.
      This can't happen now, since system() only returns when gdb
      dies.  In that case, we need to beware of starting a second
      concurrent gdb child upon the next violation.  (But if the first
      gdb dies, then starting a new one is appropriate.)  */
      break;
    }
}

/* ------------------------------------------------------------------------ */


unsigned __mf_watch (void *ptr, size_t sz)
{
  unsigned rc;
  LOCKTH ();
  BEGIN_RECURSION_PROTECT ();
  rc = __mf_watch_or_not (ptr, sz, 1);
  END_RECURSION_PROTECT ();
  UNLOCKTH ();
  return rc;
}

unsigned __mf_unwatch (void *ptr, size_t sz)
{
  unsigned rc;
  LOCKTH ();
  rc = __mf_watch_or_not (ptr, sz, 0);
  UNLOCKTH ();
  return rc;
}


static unsigned
__mf_watch_or_not (void *ptr, size_t sz, char flag)
{
  uintptr_t ptr_high = CLAMPSZ (ptr, sz);
  uintptr_t ptr_low = (uintptr_t) ptr;
  unsigned count = 0;

  TRACE ("%s ptr=%p size=%lu\n",
         (flag ? "watch" : "unwatch"), ptr, (unsigned long) sz);

  switch (__mf_opts.mudflap_mode)
    {
    case mode_nop:
    case mode_populate:
    case mode_violate:
      count = 0;
      break;

    case mode_check:
      {
        __mf_object_t **all_ovr_objs;
        unsigned obj_count;
        unsigned n;
        DECLARE (void *, malloc, size_t c);
        DECLARE (void, free, void *p);

        obj_count = __mf_find_objects (ptr_low, ptr_high, NULL, 0);
        VERBOSE_TRACE (" %u:", obj_count);

        all_ovr_objs = CALL_REAL (malloc, (sizeof (__mf_object_t *) * obj_count));
        if (all_ovr_objs == NULL) abort ();
        n = __mf_find_objects (ptr_low, ptr_high, all_ovr_objs, obj_count);
        assert (n == obj_count);

        for (n = 0; n < obj_count; n ++)
          {
            __mf_object_t *obj = all_ovr_objs[n];

            VERBOSE_TRACE (" [%p]", (void *) obj);
            if (obj->watching_p != flag)
              {
                obj->watching_p = flag;
                count ++;

                /* Remove object from cache, to ensure next access
                   goes through __mf_check().  */
                if (flag)
                  __mf_uncache_object (obj);
              }
          }
        CALL_REAL (free, all_ovr_objs);
      }
      break;
    }

  return count;
}


void
__mf_sigusr1_handler (int num)
{
  __mf_sigusr1_received ++;
}

/* Install or remove SIGUSR1 handler as necessary.
   Also, respond to a received pending SIGUSR1.  */
void
__mf_sigusr1_respond ()
{
  static int handler_installed;

#ifdef SIGUSR1
  /* Manage handler */
  if (__mf_opts.sigusr1_report && ! handler_installed)
    {
      signal (SIGUSR1, __mf_sigusr1_handler);
      handler_installed = 1;
    }
  else if(! __mf_opts.sigusr1_report && handler_installed)
    {
      signal (SIGUSR1, SIG_DFL);
      handler_installed = 0;
    }
#endif

  /* Manage enqueued signals */
  if (__mf_sigusr1_received > __mf_sigusr1_handled)
    {
      __mf_sigusr1_handled ++;
      assert (__mf_get_state () == reentrant);
      __mfu_report ();
      handler_installed = 0; /* We may need to re-enable signal; this might be a SysV library. */
    }
}


/* XXX: provide an alternative __assert_fail function that cannot
   fail due to libmudflap infinite recursion.  */
#ifndef NDEBUG

static void
write_itoa (int fd, unsigned n)
{
  enum x { bufsize = sizeof(n)*4 };
  char buf [bufsize];
  unsigned i;

  for (i=0; i<bufsize-1; i++)
    {
      unsigned digit = n % 10;
      buf[bufsize-2-i] = digit + '0';
      n /= 10;
      if (n == 0)
        {
          char *m = & buf [bufsize-2-i];
          buf[bufsize-1] = '\0';
          write (fd, m, strlen(m));
          break;
        }
    }
}


void
__assert_fail (const char *msg, const char *file, unsigned line, const char *func)
{
#define write2(string) write (2, (string), strlen ((string)));
  write2("mf");
#ifdef LIBMUDFLAPTH
  write2("(");
  write_itoa (2, (unsigned) pthread_self ());
  write2(")");
#endif
  write2(": assertion failure: `");
  write (2, msg, strlen (msg));
  write2("' in ");
  write (2, func, strlen (func));
  write2(" at ");
  write (2, file, strlen (file));
  write2(":");
  write_itoa (2, line);
  write2("\n");
#undef write2
  abort ();
}


#endif



/* Adapted splay tree code, originally from libiberty.  It has been
   specialized for libmudflap as requested by RMS.  */

static void
mfsplay_tree_free (void *p)
{
  DECLARE (void, free, void *p);
  CALL_REAL (free, p);
}

static void *
mfsplay_tree_xmalloc (size_t s)
{
  DECLARE (void *, malloc, size_t s);
  return CALL_REAL (malloc, s);
}


static void mfsplay_tree_splay (mfsplay_tree, mfsplay_tree_key);
static mfsplay_tree_node mfsplay_tree_splay_helper (mfsplay_tree,
                                                mfsplay_tree_key,
                                                mfsplay_tree_node *,
                                                mfsplay_tree_node *,
                                                mfsplay_tree_node *);


/* Help splay SP around KEY.  PARENT and GRANDPARENT are the parent
   and grandparent, respectively, of NODE.  */

static mfsplay_tree_node
mfsplay_tree_splay_helper (mfsplay_tree sp,
                         mfsplay_tree_key key,
                         mfsplay_tree_node * node,
                         mfsplay_tree_node * parent,
                         mfsplay_tree_node * grandparent)
{
  mfsplay_tree_node *next;
  mfsplay_tree_node n;
  int comparison;

  n = *node;

  if (!n)
    return *parent;

  comparison = ((key > n->key) ? 1 : ((key < n->key) ? -1 : 0));

  if (comparison == 0)
    /* We've found the target.  */
    next = 0;
  else if (comparison < 0)
    /* The target is to the left.  */
    next = &n->left;
  else
    /* The target is to the right.  */
    next = &n->right;

  if (next)
    {
      /* Check whether our recursion depth is too high.  Abort this search,
         and signal that a rebalance is required to continue.  */
      if (sp->depth > sp->max_depth)
        {
          sp->rebalance_p = 1;
          return n;
         }

      /* Continue down the tree.  */
      sp->depth ++;
      n = mfsplay_tree_splay_helper (sp, key, next, node, parent);
      sp->depth --;

      /* The recursive call will change the place to which NODE
         points.  */
      if (*node != n || sp->rebalance_p)
        return n;
    }

  if (!parent)
    /* NODE is the root.  We are done.  */
    return n;

  /* First, handle the case where there is no grandparent (i.e.,
   *PARENT is the root of the tree.)  */
  if (!grandparent)
    {
      if (n == (*parent)->left)
        {
          *node = n->right;
          n->right = *parent;
        }
      else
        {
          *node = n->left;
          n->left = *parent;
        }
      *parent = n;
      return n;
    }

  /* Next handle the cases where both N and *PARENT are left children,
     or where both are right children.  */
  if (n == (*parent)->left && *parent == (*grandparent)->left)
    {
      mfsplay_tree_node p = *parent;

      (*grandparent)->left = p->right;
      p->right = *grandparent;
      p->left = n->right;
      n->right = p;
      *grandparent = n;
      return n;
    }
  else if (n == (*parent)->right && *parent == (*grandparent)->right)
    {
      mfsplay_tree_node p = *parent;

      (*grandparent)->right = p->left;
      p->left = *grandparent;
      p->right = n->left;
      n->left = p;
      *grandparent = n;
      return n;
    }

  /* Finally, deal with the case where N is a left child, but *PARENT
     is a right child, or vice versa.  */
  if (n == (*parent)->left)
    {
      (*parent)->left = n->right;
      n->right = *parent;
      (*grandparent)->right = n->left;
      n->left = *grandparent;
      *grandparent = n;
      return n;
    }
  else
    {
      (*parent)->right = n->left;
      n->left = *parent;
      (*grandparent)->left = n->right;
      n->right = *grandparent;
      *grandparent = n;
      return n;
    }
}



static int
mfsplay_tree_rebalance_helper1 (mfsplay_tree_node n, void *array_ptr)
{
  mfsplay_tree_node **p = array_ptr;
  *(*p) = n;
  (*p)++;
  return 0;
}


static mfsplay_tree_node
mfsplay_tree_rebalance_helper2 (mfsplay_tree_node * array, unsigned low,
                              unsigned high)
{
  unsigned middle = low + (high - low) / 2;
  mfsplay_tree_node n = array[middle];

  /* Note that since we're producing a balanced binary tree, it is not a problem
     that this function is recursive.  */
  if (low + 1 <= middle)
    n->left = mfsplay_tree_rebalance_helper2 (array, low, middle - 1);
  else
    n->left = NULL;

  if (middle + 1 <= high)
    n->right = mfsplay_tree_rebalance_helper2 (array, middle + 1, high);
  else
    n->right = NULL;

  return n;
}


/* Rebalance the entire tree.  Do this by copying all the node
   pointers into an array, then cleverly re-linking them.  */
static void
mfsplay_tree_rebalance (mfsplay_tree sp)
{
  mfsplay_tree_node *all_nodes, *all_nodes_1;

  if (sp->num_keys <= 2)
    return;

  all_nodes = mfsplay_tree_xmalloc (sizeof (mfsplay_tree_node) * sp->num_keys);

  /* Traverse all nodes to copy their addresses into this array.  */
  all_nodes_1 = all_nodes;
  mfsplay_tree_foreach (sp, mfsplay_tree_rebalance_helper1,
                      (void *) &all_nodes_1);

  /* Relink all the nodes.  */
  sp->root = mfsplay_tree_rebalance_helper2 (all_nodes, 0, sp->num_keys - 1);

  mfsplay_tree_free (all_nodes);
}


/* Splay SP around KEY.  */
static void
mfsplay_tree_splay (mfsplay_tree sp, mfsplay_tree_key key)
{
  if (sp->root == 0)
    return;

  /* If we just splayed the tree with the same key, do nothing.  */
  if (sp->last_splayed_key_p &&
      (sp->last_splayed_key == key))
    return;

  /* Compute a maximum recursion depth for a splay tree with NUM nodes.
     The idea is to limit excessive stack usage if we're facing
     degenerate access patterns.  Unfortunately such patterns can occur
     e.g. during static initialization, where many static objects might
     be registered in increasing address sequence, or during a case where
     large tree-like heap data structures are allocated quickly.

     On x86, this corresponds to roughly 200K of stack usage.
     XXX: For libmudflapth, this could be a function of __mf_opts.thread_stack.  */
  sp->max_depth = 2500;
  sp->rebalance_p = sp->depth = 0;

  mfsplay_tree_splay_helper (sp, key, &sp->root, NULL, NULL);
  if (sp->rebalance_p)
    {
      mfsplay_tree_rebalance (sp);

      sp->rebalance_p = sp->depth = 0;
      mfsplay_tree_splay_helper (sp, key, &sp->root, NULL, NULL);

      if (sp->rebalance_p)
        abort ();
    }


  /* Cache this splay key. */
  sp->last_splayed_key = key;
  sp->last_splayed_key_p = 1;
}



/* Allocate a new splay tree.  */
static mfsplay_tree
mfsplay_tree_new ()
{
  mfsplay_tree sp = mfsplay_tree_xmalloc (sizeof (struct mfsplay_tree_s));
  sp->root = NULL;
  sp->last_splayed_key_p = 0;
  sp->num_keys = 0;

  return sp;
}



/* Insert a new node (associating KEY with DATA) into SP.  If a
   previous node with the indicated KEY exists, its data is replaced
   with the new value.  Returns the new node.  */
static mfsplay_tree_node
mfsplay_tree_insert (mfsplay_tree sp, mfsplay_tree_key key, mfsplay_tree_value value)
{
  int comparison = 0;

  mfsplay_tree_splay (sp, key);

  if (sp->root)
    comparison = ((sp->root->key > key) ? 1 :
                  ((sp->root->key < key) ? -1 : 0));

  if (sp->root && comparison == 0)
    {
      /* If the root of the tree already has the indicated KEY, just
         replace the value with VALUE.  */
      sp->root->value = value;
    }
  else
    {
      /* Create a new node, and insert it at the root.  */
      mfsplay_tree_node node;

      node = mfsplay_tree_xmalloc (sizeof (struct mfsplay_tree_node_s));
      node->key = key;
      node->value = value;
      sp->num_keys++;
      if (!sp->root)
        node->left = node->right = 0;
      else if (comparison < 0)
        {
          node->left = sp->root;
          node->right = node->left->right;
          node->left->right = 0;
        }
      else
        {
          node->right = sp->root;
          node->left = node->right->left;
          node->right->left = 0;
        }

      sp->root = node;
      sp->last_splayed_key_p = 0;
    }

  return sp->root;
}

/* Remove KEY from SP.  It is not an error if it did not exist.  */

static void
mfsplay_tree_remove (mfsplay_tree sp, mfsplay_tree_key key)
{
  mfsplay_tree_splay (sp, key);
  sp->last_splayed_key_p = 0;
  if (sp->root && (sp->root->key == key))
    {
      mfsplay_tree_node left, right;
      left = sp->root->left;
      right = sp->root->right;
      /* Delete the root node itself.  */
      mfsplay_tree_free (sp->root);
      sp->num_keys--;
      /* One of the children is now the root.  Doesn't matter much
         which, so long as we preserve the properties of the tree.  */
      if (left)
        {
          sp->root = left;
          /* If there was a right child as well, hang it off the
             right-most leaf of the left child.  */
          if (right)
            {
              while (left->right)
                left = left->right;
              left->right = right;
            }
        }
      else
        sp->root = right;
    }
}

/* Lookup KEY in SP, returning VALUE if present, and NULL
   otherwise.  */

static mfsplay_tree_node
mfsplay_tree_lookup (mfsplay_tree sp, mfsplay_tree_key key)
{
  mfsplay_tree_splay (sp, key);
  if (sp->root && (sp->root->key == key))
    return sp->root;
  else
    return 0;
}


/* Return the immediate predecessor KEY, or NULL if there is no
   predecessor.  KEY need not be present in the tree.  */

static mfsplay_tree_node
mfsplay_tree_predecessor (mfsplay_tree sp, mfsplay_tree_key key)
{
  int comparison;
  mfsplay_tree_node node;
  /* If the tree is empty, there is certainly no predecessor.  */
  if (!sp->root)
    return NULL;
  /* Splay the tree around KEY.  That will leave either the KEY
     itself, its predecessor, or its successor at the root.  */
  mfsplay_tree_splay (sp, key);
  comparison = ((sp->root->key > key) ? 1 :
                ((sp->root->key < key) ? -1 : 0));

  /* If the predecessor is at the root, just return it.  */
  if (comparison < 0)
    return sp->root;
  /* Otherwise, find the rightmost element of the left subtree.  */
  node = sp->root->left;
  if (node)
    while (node->right)
      node = node->right;
  return node;
}

/* Return the immediate successor KEY, or NULL if there is no
   successor.  KEY need not be present in the tree.  */

static mfsplay_tree_node
mfsplay_tree_successor (mfsplay_tree sp, mfsplay_tree_key key)
{
  int comparison;
  mfsplay_tree_node node;
  /* If the tree is empty, there is certainly no successor.  */
  if (!sp->root)
    return NULL;
  /* Splay the tree around KEY.  That will leave either the KEY
     itself, its predecessor, or its successor at the root.  */
  mfsplay_tree_splay (sp, key);
  comparison = ((sp->root->key > key) ? 1 :
                ((sp->root->key < key) ? -1 : 0));
  /* If the successor is at the root, just return it.  */
  if (comparison > 0)
    return sp->root;
  /* Otherwise, find the leftmost element of the right subtree.  */
  node = sp->root->right;
  if (node)
    while (node->left)
      node = node->left;
  return node;
}

/* Call FN, passing it the DATA, for every node in SP, following an
   in-order traversal.  If FN every returns a non-zero value, the
   iteration ceases immediately, and the value is returned.
   Otherwise, this function returns 0.

   This function simulates recursion using dynamically allocated
   arrays, since it may be called from mfsplay_tree_rebalance(), which
   in turn means that the tree is already uncomfortably deep for stack
   space limits.  */
static int
mfsplay_tree_foreach (mfsplay_tree st, mfsplay_tree_foreach_fn fn, void *data)
{
  mfsplay_tree_node *stack1;
  char *stack2;
  unsigned sp;
  int val = 0;
  enum s { s_left, s_here, s_right, s_up };

  if (st->root == NULL) /* => num_keys == 0 */
    return 0;

  stack1 = mfsplay_tree_xmalloc (sizeof (mfsplay_tree_node) * st->num_keys);
  stack2 = mfsplay_tree_xmalloc (sizeof (char) * st->num_keys);

  sp = 0;
  stack1 [sp] = st->root;
  stack2 [sp] = s_left;

  while (1)
    {
      mfsplay_tree_node n;
      enum s s;

      n = stack1 [sp];
      s = stack2 [sp];

      /* Handle each of the four possible states separately.  */

      /* 1: We're here to traverse the left subtree (if any).  */
      if (s == s_left)
        {
          stack2 [sp] = s_here;
          if (n->left != NULL)
            {
              sp ++;
              stack1 [sp] = n->left;
              stack2 [sp] = s_left;
            }
        }

      /* 2: We're here to traverse this node.  */
      else if (s == s_here)
        {
          stack2 [sp] = s_right;
          val = (*fn) (n, data);
          if (val) break;
        }

      /* 3: We're here to traverse the right subtree (if any).  */
      else if (s == s_right)
        {
          stack2 [sp] = s_up;
          if (n->right != NULL)
            {
              sp ++;
              stack1 [sp] = n->right;
              stack2 [sp] = s_left;
            }
        }

      /* 4: We're here after both subtrees (if any) have been traversed.  */
      else if (s == s_up)
        {
          /* Pop the stack.  */
          if (sp == 0) break; /* Popping off the root note: we're finished!  */
          sp --;
        }

      else
        abort ();
    }

  mfsplay_tree_free (stack1);
  mfsplay_tree_free (stack2);
  return val;
}
