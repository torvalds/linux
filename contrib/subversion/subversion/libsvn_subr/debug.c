/*
 * debug.c :  small functions to help SVN developers
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* These functions are only available to SVN developers and should never
   be used in release code. One of the reasons to avoid this code in release
   builds is that this code is not thread-safe. */
#include <stdarg.h>
#include <assert.h>

#include <apr_pools.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_string.h"

#ifndef SVN_DBG__PROTOTYPES
#define SVN_DBG__PROTOTYPES
#endif
#include "private/svn_debug.h"


#define DBG_FLAG "DBG: "

/* This will be tweaked by the preamble code.  */
static const char *debug_file = NULL;
static long debug_line = 0;
static FILE * volatile debug_output = NULL;


static svn_boolean_t
quiet_mode(void)
{
  return getenv("SVN_DBG_QUIET") != NULL;
}


void
svn_dbg__preamble(const char *file, long line, FILE *output)
{
  debug_output = output;

  if (output != NULL && !quiet_mode())
    {
      /* Quick and dirty basename() code.  */
      const char *slash = strrchr(file, '/');

      if (slash == NULL)
        slash = strrchr(file, '\\');
      if (slash)
        debug_file = slash + 1;
      else
        debug_file = file;
    }
  debug_line = line;
}


/* Print a formatted string using format FMT and argument-list AP,
 * prefixing each line of output with a debug header. */
static void
debug_vprintf(const char *fmt, va_list ap)
{
  FILE *output = debug_output;
  char prefix[80], buffer[4096];
  char *s = buffer;
  int n;

  if (output == NULL || quiet_mode())
    return;

  n = apr_snprintf(prefix, sizeof(prefix), DBG_FLAG "%s:%4ld: ",
                   debug_file, debug_line);
  assert(n < sizeof(prefix) - 1);
  n = apr_vsnprintf(buffer, sizeof(buffer), fmt, ap);
  assert(n < sizeof(buffer) - 1);
  do
    {
      char *newline = strchr(s, '\n');
      if (newline)
        *newline = '\0';

      fputs(prefix, output);
      fputs(s, output);
      fputc('\n', output);

      if (! newline)
        break;
      s = newline + 1;
    }
  while (*s);  /* print another line, except after a final newline */
}


void
svn_dbg__printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  debug_vprintf(fmt, ap);
  va_end(ap);
}


void
svn_dbg__print_props(apr_hash_t *props,
                     const char *header_fmt,
                     ...)
{
/* We only build this code if SVN_DEBUG is defined. */
#ifdef SVN_DEBUG

  apr_hash_index_t *hi;
  va_list ap;

  va_start(ap, header_fmt);
  debug_vprintf(header_fmt, ap);
  va_end(ap);

  if (props == NULL)
    {
      svn_dbg__printf("    (null)\n");
      return;
    }

  for (hi = apr_hash_first(apr_hash_pool_get(props), props); hi;
        hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      svn_string_t *val = apr_hash_this_val(hi);

      svn_dbg__printf("    '%s' -> '%s'\n", name, val->data);
    }
#endif /* SVN_DEBUG */
}

