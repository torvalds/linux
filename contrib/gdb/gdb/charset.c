/* Character set conversion support for GDB.

   Copyright 2001, 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "charset.h"
#include "gdbcmd.h"
#include "gdb_assert.h"

#include <stddef.h>
#include "gdb_string.h"
#include <ctype.h>

#ifdef HAVE_ICONV
#include <iconv.h>
#endif


/* How GDB's character set support works

   GDB has two global settings:

   - The `current host character set' is the character set GDB should
     use in talking to the user, and which (hopefully) the user's
     terminal knows how to display properly.

   - The `current target character set' is the character set the
     program being debugged uses.

   There are commands to set each of these, and mechanisms for
   choosing reasonable default values.  GDB has a global list of
   character sets that it can use as its host or target character
   sets.

   The header file `charset.h' declares various functions that
   different pieces of GDB need to perform tasks like:

   - printing target strings and characters to the user's terminal
     (mostly target->host conversions),

   - building target-appropriate representations of strings and
     characters the user enters in expressions (mostly host->target
     conversions),

   and so on.

   Now, many of these operations are specific to a particular
   host/target character set pair.  If GDB supports N character sets,
   there are N^2 possible pairs.  This means that, the larger GDB's
   repertoire of character sets gets, the more expensive it gets to add
   new character sets.

   To make sure that GDB can do the right thing for every possible
   pairing of host and target character set, while still allowing
   GDB's repertoire to scale, we use a two-tiered approach:

   - We maintain a global table of "translations" --- groups of
     functions specific to a particular pair of character sets.

   - However, a translation can be incomplete: some functions can be
     omitted.  Where there is not a translation to specify exactly
     what function to use, we provide reasonable defaults.  The
     default behaviors try to use the "iconv" library functions, which
     support a wide range of character sets.  However, even if iconv
     is not available, there are fallbacks to support trivial
     translations: when the host and target character sets are the
     same.  */


/* The character set and translation structures.  */


/* A character set GDB knows about.  GDB only supports character sets
   with stateless encodings, in which every character is one byte
   long.  */
struct charset {

  /* A singly-linked list of all known charsets.  */
  struct charset *next;

  /* The name of the character set.  Comparisons on character set
     names are case-sensitive.  */
  const char *name;

  /* Non-zero iff this character set can be used as a host character
     set.  At present, GDB basically assumes that the host character
     set is a superset of ASCII.  */
  int valid_host_charset;

  /* Pointers to charset-specific functions that depend only on a
     single character set, and data pointers to pass to them.  */
  int (*host_char_print_literally) (void *baton,
                                    int host_char);
  void *host_char_print_literally_baton;

  int (*target_char_to_control_char) (void *baton,
                                      int target_char,
                                      int *target_ctrl_char);
  void *target_char_to_control_char_baton;
};


/* A translation from one character set to another.  */
struct translation {

  /* A singly-linked list of all known translations.  */
  struct translation *next;

  /* This structure describes functions going from the FROM character
     set to the TO character set.  Comparisons on character set names
     are case-sensitive.  */
  const char *from, *to;

  /* Pointers to translation-specific functions, and data pointers to
     pass to them.  These pointers can be zero, indicating that GDB
     should fall back on the default behavior.  We hope the default
     behavior will be correct for many from/to pairs, reducing the
     number of translations that need to be registered explicitly.  */
  
  /* TARGET_CHAR is in the `from' charset.
     Returns a string in the `to' charset.  */
  const char *(*c_target_char_has_backslash_escape) (void *baton,
                                                     int target_char);
  void *c_target_char_has_backslash_escape_baton;

  /* HOST_CHAR is in the `from' charset.
     TARGET_CHAR points to a char in the `to' charset.  */
  int (*c_parse_backslash) (void *baton, int host_char, int *target_char);
  void *c_parse_backslash_baton;

  /* This is used for the host_char_to_target and target_char_to_host
     functions.  */
  int (*convert_char) (void *baton, int from, int *to);
  void *convert_char_baton;
};



/* The global lists of character sets and translations.  */


#ifndef GDB_DEFAULT_HOST_CHARSET
#define GDB_DEFAULT_HOST_CHARSET "ISO-8859-1"
#endif

#ifndef GDB_DEFAULT_TARGET_CHARSET
#define GDB_DEFAULT_TARGET_CHARSET "ISO-8859-1"
#endif

static const char *host_charset_name = GDB_DEFAULT_HOST_CHARSET;
static const char *target_charset_name = GDB_DEFAULT_TARGET_CHARSET;

static const char *host_charset_enum[] = 
{
  "ASCII",
  "ISO-8859-1",
  0
};

static const char *target_charset_enum[] = 
{
  "ASCII",
  "ISO-8859-1",
  "EBCDIC-US",
  "IBM1047",
  0
};

/* The global list of all the charsets GDB knows about.  */
static struct charset *all_charsets;


static void
register_charset (struct charset *cs)
{
  struct charset **ptr;

  /* Put the new charset on the end, so that the list ends up in the
     same order as the registrations in the _initialize function.  */
  for (ptr = &all_charsets; *ptr; ptr = &(*ptr)->next)
    ;

  cs->next = 0;
  *ptr = cs;
}


static struct charset *
lookup_charset (const char *name)
{
  struct charset *cs;

  for (cs = all_charsets; cs; cs = cs->next)
    if (! strcmp (name, cs->name))
      return cs;

  return NULL;
}


/* The global list of translations.  */
static struct translation *all_translations;


static void
register_translation (struct translation *t)
{
  t->next = all_translations;
  all_translations = t;
}


static struct translation *
lookup_translation (const char *from, const char *to)
{
  struct translation *t;

  for (t = all_translations; t; t = t->next)
    if (! strcmp (from, t->from)
        && ! strcmp (to, t->to))
      return t;

  return 0;
}



/* Constructing charsets.  */

/* Allocate, initialize and return a straightforward charset.
   Use this function, rather than creating the structures yourself,
   so that we can add new fields to the structure in the future without
   having to tweak all the old charset descriptions.  */
static struct charset *
simple_charset (const char *name,
                int valid_host_charset,
                int (*host_char_print_literally) (void *baton, int host_char),
                void *host_char_print_literally_baton,
                int (*target_char_to_control_char) (void *baton,
                                                    int target_char,
                                                    int *target_ctrl_char),
                void *target_char_to_control_char_baton)
{
  struct charset *cs = xmalloc (sizeof (*cs));

  memset (cs, 0, sizeof (*cs));
  cs->name = name;
  cs->valid_host_charset = valid_host_charset;
  cs->host_char_print_literally = host_char_print_literally;
  cs->host_char_print_literally_baton = host_char_print_literally_baton;
  cs->target_char_to_control_char = target_char_to_control_char;
  cs->target_char_to_control_char_baton = target_char_to_control_char_baton;

  return cs;
}



/* ASCII functions.  */

static int
ascii_print_literally (void *baton, int c)
{
  c &= 0xff;

  return (0x20 <= c && c <= 0x7e);
}


static int
ascii_to_control (void *baton, int c, int *ctrl_char)
{
  *ctrl_char = (c & 037);
  return 1;
}


/* ISO-8859 family functions.  */


static int
iso_8859_print_literally (void *baton, int c)
{
  c &= 0xff;

  return ((0x20 <= c && c <= 0x7e) /* ascii printables */
          || (! sevenbit_strings && 0xA0 <= c)); /* iso 8859 printables */
}


static int
iso_8859_to_control (void *baton, int c, int *ctrl_char)
{
  *ctrl_char = (c & 0200) | (c & 037);
  return 1;
}


/* Construct an ISO-8859-like character set.  */
static struct charset *
iso_8859_family_charset (const char *name)
{
  return simple_charset (name, 1,
                         iso_8859_print_literally, 0,
                         iso_8859_to_control, 0);
}



/* EBCDIC family functions.  */


static int
ebcdic_print_literally (void *baton, int c)
{
  c &= 0xff;

  return (64 <= c && c <= 254);
}


static int
ebcdic_to_control (void *baton, int c, int *ctrl_char)
{
  /* There are no control character equivalents in EBCDIC.  Use
     numeric escapes.  */
  return 0;
}


/* Construct an EBCDIC-like character set.  */
static struct charset *
ebcdic_family_charset (const char *name)
{
  return simple_charset (name, 0,
                         ebcdic_print_literally, 0,
                         ebcdic_to_control, 0);
}
                




/* Fallback functions using iconv.  */

#if defined(HAVE_ICONV)

struct cached_iconv {
  struct charset *from, *to;
  iconv_t i;
};


/* Make sure the iconv cache *CI contains an iconv descriptor
   translating from FROM to TO.  If it already does, fine; otherwise,
   close any existing descriptor, and open up a new one.  On success,
   return zero; on failure, return -1 and set errno.  */
static int
check_iconv_cache (struct cached_iconv *ci,
                   struct charset *from,
                   struct charset *to)
{
  iconv_t i;

  /* Does the cached iconv descriptor match the conversion we're trying
     to do now?  */
  if (ci->from == from
      && ci->to == to
      && ci->i != (iconv_t) 0)
    return 0;

  /* It doesn't.  If we actually had any iconv descriptor open at
     all, close it now.  */
  if (ci->i != (iconv_t) 0)
    {
      i = ci->i;
      ci->i = (iconv_t) 0;
      
      if (iconv_close (i) == -1)
        error ("Error closing `iconv' descriptor for "
               "`%s'-to-`%s' character conversion: %s",
               ci->from->name, ci->to->name, safe_strerror (errno));
    }

  /* Open a new iconv descriptor for the required conversion.  */
  i = iconv_open (to->name, from->name);
  if (i == (iconv_t) -1)
    return -1;

  ci->i = i;
  ci->from = from;
  ci->to = to;

  return 0;
}


/* Convert FROM_CHAR using the cached iconv conversion *CI.  Return
   non-zero if the conversion was successful, zero otherwise.  */
static int
cached_iconv_convert (struct cached_iconv *ci, int from_char, int *to_char)
{
  char from;
  ICONV_CONST char *from_ptr = &from;
  char to, *to_ptr = &to;
  size_t from_left = sizeof (from), to_left = sizeof (to);

  gdb_assert (ci->i != (iconv_t) 0);

  from = from_char;
  if (iconv (ci->i, &from_ptr, &from_left, &to_ptr, &to_left)
      == (size_t) -1)
    {
      /* These all suggest that the input or output character sets
         have multi-byte encodings of some characters, which means
         it's unsuitable for use as a GDB character set.  We should
         never have selected it.  */
      gdb_assert (errno != E2BIG && errno != EINVAL);

      /* This suggests a bug in the code managing *CI.  */
      gdb_assert (errno != EBADF);

      /* This seems to mean that there is no equivalent character in
         the `to' character set.  */
      if (errno == EILSEQ)
        return 0;

      /* Anything else is mysterious.  */
      internal_error (__FILE__, __LINE__,
		      "Error converting character `%d' from `%s' to `%s' "
                      "character set: %s",
                      from_char, ci->from->name, ci->to->name,
                      safe_strerror (errno));
    }

  /* If the pointers weren't advanced across the input, that also
     suggests something was wrong.  */
  gdb_assert (from_left == 0 && to_left == 0);

  *to_char = (unsigned char) to;
  return 1;
}


static void
register_iconv_charsets (void)
{
  /* Here we should check whether various character sets were
     recognized by the local iconv implementation.

     The first implementation registered a bunch of character sets
     recognized by iconv, but then we discovered that iconv on Solaris
     and iconv on GNU/Linux had no character sets in common.  So we
     replaced them with the hard-coded tables that appear later in the
     file.  */
}

#endif /* defined (HAVE_ICONV) */


/* Fallback routines for systems without iconv.  */

#if ! defined (HAVE_ICONV) 
struct cached_iconv { char nothing; };

static int
check_iconv_cache (struct cached_iconv *ci,
                   struct charset *from,
                   struct charset *to)
{
  errno = EINVAL;
  return -1;
}

static int
cached_iconv_convert (struct cached_iconv *ci, int from_char, int *to_char)
{
  /* This function should never be called.  */
  gdb_assert (0);
}

static void
register_iconv_charsets (void)
{
}

#endif /* ! defined(HAVE_ICONV) */


/* Default trivial conversion functions.  */

static int
identity_either_char_to_other (void *baton, int either_char, int *other_char)
{
  *other_char = either_char;
  return 1;
}



/* Default non-trivial conversion functions.  */


static char backslashable[] = "abfnrtv";
static char *backslashed[] = {"a", "b", "f", "n", "r", "t", "v", "0"};
static char represented[] = "\a\b\f\n\r\t\v";


/* Translate TARGET_CHAR into the host character set, and see if it
   matches any of our standard escape sequences.  */
static const char *
default_c_target_char_has_backslash_escape (void *baton, int target_char)
{
  int host_char;
  const char *ix;

  /* If target_char has no equivalent in the host character set,
     assume it doesn't have a backslashed form.  */
  if (! target_char_to_host (target_char, &host_char))
    return NULL;

  ix = strchr (represented, host_char);
  if (ix)
    return backslashed[ix - represented];
  else
    return NULL;
}


/* Translate the backslash the way we would in the host character set,
   and then try to translate that into the target character set.  */
static int
default_c_parse_backslash (void *baton, int host_char, int *target_char)
{
  const char *ix;

  ix = strchr (backslashable, host_char);

  if (! ix)
    return 0;
  else
    return host_char_to_target (represented[ix - backslashable],
                                target_char);
}


/* Convert using a cached iconv descriptor.  */
static int
iconv_convert (void *baton, int from_char, int *to_char)
{
  struct cached_iconv *ci = baton;
  return cached_iconv_convert (ci, from_char, to_char);
}



/* Conversion tables.  */


/* I'd much rather fall back on iconv whenever possible.  But the
   character set names you use with iconv aren't standardized at all,
   a lot of platforms have really meager character set coverage, etc.
   I wanted to have at least something we could use to exercise the
   test suite on all platforms.

   In the long run, we should have a configure-time process explore
   somehow which character sets the host platform supports, and some
   arrangement that allows GDB users to use platform-indepedent names
   for character sets.  */


/* We generated these tables using iconv on a GNU/Linux machine.  */


static int ascii_to_iso_8859_1_table[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, /* 32 */
   32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, /* 48 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, /* 64 */
   64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, /* 80 */
   80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, /* 96 */
   96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111, /* 112 */
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127, /* 128 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 144 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int ascii_to_ebcdic_us_table[] = {
    0,  1,  2,  3, 55, 45, 46, 47, 22,  5, 37, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 60, 61, 50, 38, 24, 25, 63, 39, 28, 29, 30, 31, /* 32 */
   64, 90,127,123, 91,108, 80,125, 77, 93, 92, 78,107, 96, 75, 97, /* 48 */
  240,241,242,243,244,245,246,247,248,249,122, 94, 76,126,110,111, /* 64 */
  124,193,194,195,196,197,198,199,200,201,209,210,211,212,213,214, /* 80 */
  215,216,217,226,227,228,229,230,231,232,233, -1,224, -1, -1,109, /* 96 */
  121,129,130,131,132,133,134,135,136,137,145,146,147,148,149,150, /* 112 */
  151,152,153,162,163,164,165,166,167,168,169,192, 79,208,161,  7, /* 128 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 144 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int ascii_to_ibm1047_table[] = {
    0,  1,  2,  3, 55, 45, 46, 47, 22,  5, 37, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 60, 61, 50, 38, 24, 25, 63, 39, 28, 29, 30, 31, /* 32 */
   64, 90,127,123, 91,108, 80,125, 77, 93, 92, 78,107, 96, 75, 97, /* 48 */
  240,241,242,243,244,245,246,247,248,249,122, 94, 76,126,110,111, /* 64 */
  124,193,194,195,196,197,198,199,200,201,209,210,211,212,213,214, /* 80 */
  215,216,217,226,227,228,229,230,231,232,233,173,224,189, 95,109, /* 96 */
  121,129,130,131,132,133,134,135,136,137,145,146,147,148,149,150, /* 112 */
  151,152,153,162,163,164,165,166,167,168,169,192, 79,208,161,  7, /* 128 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 144 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int iso_8859_1_to_ascii_table[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, /* 32 */
   32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, /* 48 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, /* 64 */
   64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, /* 80 */
   80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, /* 96 */
   96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111, /* 112 */
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127, /* 128 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 144 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int iso_8859_1_to_ebcdic_us_table[] = {
    0,  1,  2,  3, 55, 45, 46, 47, 22,  5, 37, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 60, 61, 50, 38, 24, 25, 63, 39, 28, 29, 30, 31, /* 32 */
   64, 90,127,123, 91,108, 80,125, 77, 93, 92, 78,107, 96, 75, 97, /* 48 */
  240,241,242,243,244,245,246,247,248,249,122, 94, 76,126,110,111, /* 64 */
  124,193,194,195,196,197,198,199,200,201,209,210,211,212,213,214, /* 80 */
  215,216,217,226,227,228,229,230,231,232,233, -1,224, -1, -1,109, /* 96 */
  121,129,130,131,132,133,134,135,136,137,145,146,147,148,149,150, /* 112 */
  151,152,153,162,163,164,165,166,167,168,169,192, 79,208,161,  7, /* 128 */
   32, 33, 34, 35, 36, 21,  6, 23, 40, 41, 42, 43, 44,  9, 10, 27, /* 144 */
   48, 49, 26, 51, 52, 53, 54,  8, 56, 57, 58, 59,  4, 20, 62,255, /* 160 */
   -1, -1, 74, -1, -1, -1,106, -1, -1, -1, -1, -1, 95, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int iso_8859_1_to_ibm1047_table[] = {
    0,  1,  2,  3, 55, 45, 46, 47, 22,  5, 37, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 60, 61, 50, 38, 24, 25, 63, 39, 28, 29, 30, 31, /* 32 */
   64, 90,127,123, 91,108, 80,125, 77, 93, 92, 78,107, 96, 75, 97, /* 48 */
  240,241,242,243,244,245,246,247,248,249,122, 94, 76,126,110,111, /* 64 */
  124,193,194,195,196,197,198,199,200,201,209,210,211,212,213,214, /* 80 */
  215,216,217,226,227,228,229,230,231,232,233,173,224,189, 95,109, /* 96 */
  121,129,130,131,132,133,134,135,136,137,145,146,147,148,149,150, /* 112 */
  151,152,153,162,163,164,165,166,167,168,169,192, 79,208,161,  7, /* 128 */
   32, 33, 34, 35, 36, 21,  6, 23, 40, 41, 42, 43, 44,  9, 10, 27, /* 144 */
   48, 49, 26, 51, 52, 53, 54,  8, 56, 57, 58, 59,  4, 20, 62,255, /* 160 */
   65,170, 74,177,159,178,106,181,187,180,154,138,176,202,175,188, /* 176 */
  144,143,234,250,190,160,182,179,157,218,155,139,183,184,185,171, /* 192 */
  100,101, 98,102, 99,103,158,104,116,113,114,115,120,117,118,119, /* 208 */
  172,105,237,238,235,239,236,191,128,253,254,251,252,186,174, 89, /* 224 */
   68, 69, 66, 70, 67, 71,156, 72, 84, 81, 82, 83, 88, 85, 86, 87, /* 240 */
  140, 73,205,206,203,207,204,225,112,221,222,219,220,141,142,223  /* 256 */
};


static int ebcdic_us_to_ascii_table[] = {
    0,  1,  2,  3, -1,  9, -1,127, -1, -1, -1, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, -1, -1,  8, -1, 24, 25, -1, -1, 28, 29, 30, 31, /* 32 */
   -1, -1, -1, -1, -1, 10, 23, 27, -1, -1, -1, -1, -1,  5,  6,  7, /* 48 */
   -1, -1, 22, -1, -1, -1, -1,  4, -1, -1, -1, -1, 20, 21, -1, 26, /* 64 */
   32, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 46, 60, 40, 43,124, /* 80 */
   38, -1, -1, -1, -1, -1, -1, -1, -1, -1, 33, 36, 42, 41, 59, -1, /* 96 */
   45, 47, -1, -1, -1, -1, -1, -1, -1, -1, -1, 44, 37, 95, 62, 63, /* 112 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, 96, 58, 35, 64, 39, 61, 34, /* 128 */
   -1, 97, 98, 99,100,101,102,103,104,105, -1, -1, -1, -1, -1, -1, /* 144 */
   -1,106,107,108,109,110,111,112,113,114, -1, -1, -1, -1, -1, -1, /* 160 */
   -1,126,115,116,117,118,119,120,121,122, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
  123, 65, 66, 67, 68, 69, 70, 71, 72, 73, -1, -1, -1, -1, -1, -1, /* 208 */
  125, 74, 75, 76, 77, 78, 79, 80, 81, 82, -1, -1, -1, -1, -1, -1, /* 224 */
   92, -1, 83, 84, 85, 86, 87, 88, 89, 90, -1, -1, -1, -1, -1, -1, /* 240 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int ebcdic_us_to_iso_8859_1_table[] = {
    0,  1,  2,  3,156,  9,134,127,151,141,142, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19,157,133,  8,135, 24, 25,146,143, 28, 29, 30, 31, /* 32 */
  128,129,130,131,132, 10, 23, 27,136,137,138,139,140,  5,  6,  7, /* 48 */
  144,145, 22,147,148,149,150,  4,152,153,154,155, 20, 21,158, 26, /* 64 */
   32, -1, -1, -1, -1, -1, -1, -1, -1, -1,162, 46, 60, 40, 43,124, /* 80 */
   38, -1, -1, -1, -1, -1, -1, -1, -1, -1, 33, 36, 42, 41, 59,172, /* 96 */
   45, 47, -1, -1, -1, -1, -1, -1, -1, -1,166, 44, 37, 95, 62, 63, /* 112 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, 96, 58, 35, 64, 39, 61, 34, /* 128 */
   -1, 97, 98, 99,100,101,102,103,104,105, -1, -1, -1, -1, -1, -1, /* 144 */
   -1,106,107,108,109,110,111,112,113,114, -1, -1, -1, -1, -1, -1, /* 160 */
   -1,126,115,116,117,118,119,120,121,122, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
  123, 65, 66, 67, 68, 69, 70, 71, 72, 73, -1, -1, -1, -1, -1, -1, /* 208 */
  125, 74, 75, 76, 77, 78, 79, 80, 81, 82, -1, -1, -1, -1, -1, -1, /* 224 */
   92, -1, 83, 84, 85, 86, 87, 88, 89, 90, -1, -1, -1, -1, -1, -1, /* 240 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1,159  /* 256 */
};


static int ebcdic_us_to_ibm1047_table[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, /* 32 */
   32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, /* 48 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, /* 64 */
   64, -1, -1, -1, -1, -1, -1, -1, -1, -1, 74, 75, 76, 77, 78, 79, /* 80 */
   80, -1, -1, -1, -1, -1, -1, -1, -1, -1, 90, 91, 92, 93, 94,176, /* 96 */
   96, 97, -1, -1, -1, -1, -1, -1, -1, -1,106,107,108,109,110,111, /* 112 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1,121,122,123,124,125,126,127, /* 128 */
   -1,129,130,131,132,133,134,135,136,137, -1, -1, -1, -1, -1, -1, /* 144 */
   -1,145,146,147,148,149,150,151,152,153, -1, -1, -1, -1, -1, -1, /* 160 */
   -1,161,162,163,164,165,166,167,168,169, -1, -1, -1, -1, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
  192,193,194,195,196,197,198,199,200,201, -1, -1, -1, -1, -1, -1, /* 208 */
  208,209,210,211,212,213,214,215,216,217, -1, -1, -1, -1, -1, -1, /* 224 */
  224, -1,226,227,228,229,230,231,232,233, -1, -1, -1, -1, -1, -1, /* 240 */
  240,241,242,243,244,245,246,247,248,249, -1, -1, -1, -1, -1,255  /* 256 */
};


static int ibm1047_to_ascii_table[] = {
    0,  1,  2,  3, -1,  9, -1,127, -1, -1, -1, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, -1, -1,  8, -1, 24, 25, -1, -1, 28, 29, 30, 31, /* 32 */
   -1, -1, -1, -1, -1, 10, 23, 27, -1, -1, -1, -1, -1,  5,  6,  7, /* 48 */
   -1, -1, 22, -1, -1, -1, -1,  4, -1, -1, -1, -1, 20, 21, -1, 26, /* 64 */
   32, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 46, 60, 40, 43,124, /* 80 */
   38, -1, -1, -1, -1, -1, -1, -1, -1, -1, 33, 36, 42, 41, 59, 94, /* 96 */
   45, 47, -1, -1, -1, -1, -1, -1, -1, -1, -1, 44, 37, 95, 62, 63, /* 112 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, 96, 58, 35, 64, 39, 61, 34, /* 128 */
   -1, 97, 98, 99,100,101,102,103,104,105, -1, -1, -1, -1, -1, -1, /* 144 */
   -1,106,107,108,109,110,111,112,113,114, -1, -1, -1, -1, -1, -1, /* 160 */
   -1,126,115,116,117,118,119,120,121,122, -1, -1, -1, 91, -1, -1, /* 176 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 93, -1, -1, /* 192 */
  123, 65, 66, 67, 68, 69, 70, 71, 72, 73, -1, -1, -1, -1, -1, -1, /* 208 */
  125, 74, 75, 76, 77, 78, 79, 80, 81, 82, -1, -1, -1, -1, -1, -1, /* 224 */
   92, -1, 83, 84, 85, 86, 87, 88, 89, 90, -1, -1, -1, -1, -1, -1, /* 240 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1, -1  /* 256 */
};


static int ibm1047_to_iso_8859_1_table[] = {
    0,  1,  2,  3,156,  9,134,127,151,141,142, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19,157,133,  8,135, 24, 25,146,143, 28, 29, 30, 31, /* 32 */
  128,129,130,131,132, 10, 23, 27,136,137,138,139,140,  5,  6,  7, /* 48 */
  144,145, 22,147,148,149,150,  4,152,153,154,155, 20, 21,158, 26, /* 64 */
   32,160,226,228,224,225,227,229,231,241,162, 46, 60, 40, 43,124, /* 80 */
   38,233,234,235,232,237,238,239,236,223, 33, 36, 42, 41, 59, 94, /* 96 */
   45, 47,194,196,192,193,195,197,199,209,166, 44, 37, 95, 62, 63, /* 112 */
  248,201,202,203,200,205,206,207,204, 96, 58, 35, 64, 39, 61, 34, /* 128 */
  216, 97, 98, 99,100,101,102,103,104,105,171,187,240,253,254,177, /* 144 */
  176,106,107,108,109,110,111,112,113,114,170,186,230,184,198,164, /* 160 */
  181,126,115,116,117,118,119,120,121,122,161,191,208, 91,222,174, /* 176 */
  172,163,165,183,169,167,182,188,189,190,221,168,175, 93,180,215, /* 192 */
  123, 65, 66, 67, 68, 69, 70, 71, 72, 73,173,244,246,242,243,245, /* 208 */
  125, 74, 75, 76, 77, 78, 79, 80, 81, 82,185,251,252,249,250,255, /* 224 */
   92,247, 83, 84, 85, 86, 87, 88, 89, 90,178,212,214,210,211,213, /* 240 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57,179,219,220,217,218,159  /* 256 */
};


static int ibm1047_to_ebcdic_us_table[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, /* 16 */
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, /* 32 */
   32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, /* 48 */
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, /* 64 */
   64, -1, -1, -1, -1, -1, -1, -1, -1, -1, 74, 75, 76, 77, 78, 79, /* 80 */
   80, -1, -1, -1, -1, -1, -1, -1, -1, -1, 90, 91, 92, 93, 94, -1, /* 96 */
   96, 97, -1, -1, -1, -1, -1, -1, -1, -1,106,107,108,109,110,111, /* 112 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1,121,122,123,124,125,126,127, /* 128 */
   -1,129,130,131,132,133,134,135,136,137, -1, -1, -1, -1, -1, -1, /* 144 */
   -1,145,146,147,148,149,150,151,152,153, -1, -1, -1, -1, -1, -1, /* 160 */
   -1,161,162,163,164,165,166,167,168,169, -1, -1, -1, -1, -1, -1, /* 176 */
   95, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192 */
  192,193,194,195,196,197,198,199,200,201, -1, -1, -1, -1, -1, -1, /* 208 */
  208,209,210,211,212,213,214,215,216,217, -1, -1, -1, -1, -1, -1, /* 224 */
  224, -1,226,227,228,229,230,231,232,233, -1, -1, -1, -1, -1, -1, /* 240 */
  240,241,242,243,244,245,246,247,248,249, -1, -1, -1, -1, -1,255  /* 256 */
};


static int
table_convert_char (void *baton, int from, int *to)
{
  int *table = (int *) baton;

  if (0 <= from && from <= 255
      && table[from] != -1)
    {
      *to = table[from];
      return 1;
    }
  else
    return 0;
}


static struct translation *
table_translation (const char *from, const char *to, int *table,
                   const char *(*c_target_char_has_backslash_escape)
                   (void *baton, int target_char),
                   void *c_target_char_has_backslash_escape_baton,
                   int (*c_parse_backslash) (void *baton,
                                             int host_char,
                                             int *target_char),
                   void *c_parse_backslash_baton)
{
  struct translation *t = xmalloc (sizeof (*t));

  memset (t, 0, sizeof (*t));
  t->from = from;
  t->to = to;
  t->c_target_char_has_backslash_escape = c_target_char_has_backslash_escape;
  t->c_target_char_has_backslash_escape_baton
    = c_target_char_has_backslash_escape_baton;
  t->c_parse_backslash = c_parse_backslash;
  t->c_parse_backslash_baton = c_parse_backslash_baton;
  t->convert_char = table_convert_char;
  t->convert_char_baton = (void *) table;

  return t;
}


static struct translation *
simple_table_translation (const char *from, const char *to, int *table)
{
  return table_translation (from, to, table, 0, 0, 0, 0);
}



/* Setting and retrieving the host and target charsets.  */


/* The current host and target character sets.  */
static struct charset *current_host_charset, *current_target_charset;

/* The current functions and batons we should use for the functions in
   charset.h.  */

static const char *(*c_target_char_has_backslash_escape_func)
     (void *baton, int target_char);
static void *c_target_char_has_backslash_escape_baton;

static int (*c_parse_backslash_func) (void *baton,
                                      int host_char,
                                      int *target_char);
static void *c_parse_backslash_baton;

static int (*host_char_to_target_func) (void *baton,
                                        int host_char,
                                        int *target_char);
static void *host_char_to_target_baton;

static int (*target_char_to_host_func) (void *baton,
                                        int target_char,
                                        int *host_char);
static void *target_char_to_host_baton;


/* Cached iconv conversions, that might be useful to fallback
   routines.  */
static struct cached_iconv cached_iconv_host_to_target;
static struct cached_iconv cached_iconv_target_to_host;


/* Charset structures manipulation functions.  */

static struct charset *
lookup_charset_or_error (const char *name)
{
  struct charset *cs = lookup_charset (name);

  if (! cs)
    error ("GDB doesn't know of any character set named `%s'.", name);

  return cs;
}

static void
check_valid_host_charset (struct charset *cs)
{
  if (! cs->valid_host_charset)
    error ("GDB can't use `%s' as its host character set.", cs->name);
}

/* Set the host and target character sets to HOST and TARGET.  */
static void
set_host_and_target_charsets (struct charset *host, struct charset *target)
{
  struct translation *h2t, *t2h;

  /* If they're not both initialized yet, then just do nothing for
     now.  As soon as we're done running our initialize function,
     everything will be initialized.  */
  if (! host || ! target)
    {
      current_host_charset = host;
      current_target_charset = target;
      return;
    }

  h2t = lookup_translation (host->name, target->name);
  t2h = lookup_translation (target->name, host->name);

  /* If the translations don't provide conversion functions, make sure
     iconv can back them up.  Do this *before* modifying any state.  */
  if (host != target)
    {
      if (! h2t || ! h2t->convert_char)
        {
          if (check_iconv_cache (&cached_iconv_host_to_target, host, target)
              < 0)
            error ("GDB can't convert from the `%s' character set to `%s'.",
                   host->name, target->name);
        }
      if (! t2h || ! t2h->convert_char)
        {
          if (check_iconv_cache (&cached_iconv_target_to_host, target, host)
              < 0)
            error ("GDB can't convert from the `%s' character set to `%s'.",
                   target->name, host->name);
        }
    }

  if (t2h && t2h->c_target_char_has_backslash_escape)
    {
      c_target_char_has_backslash_escape_func
        = t2h->c_target_char_has_backslash_escape;
      c_target_char_has_backslash_escape_baton
        = t2h->c_target_char_has_backslash_escape_baton;
    }
  else
    c_target_char_has_backslash_escape_func
      = default_c_target_char_has_backslash_escape;

  if (h2t && h2t->c_parse_backslash)
    {
      c_parse_backslash_func = h2t->c_parse_backslash;
      c_parse_backslash_baton = h2t->c_parse_backslash_baton;
    }
  else
    c_parse_backslash_func = default_c_parse_backslash;

  if (h2t && h2t->convert_char)
    {
      host_char_to_target_func = h2t->convert_char;
      host_char_to_target_baton = h2t->convert_char_baton;
    }
  else if (host == target)
    host_char_to_target_func = identity_either_char_to_other;
  else
    {
      host_char_to_target_func = iconv_convert;
      host_char_to_target_baton = &cached_iconv_host_to_target;
    }

  if (t2h && t2h->convert_char)
    {
      target_char_to_host_func = t2h->convert_char;
      target_char_to_host_baton = t2h->convert_char_baton;
    }
  else if (host == target)
    target_char_to_host_func = identity_either_char_to_other;
  else
    {
      target_char_to_host_func = iconv_convert;
      target_char_to_host_baton = &cached_iconv_target_to_host;
    }

  current_host_charset = host;
  current_target_charset = target;
}

/* Do the real work of setting the host charset.  */
static void
set_host_charset (const char *charset)
{
  struct charset *cs = lookup_charset_or_error (charset);
  check_valid_host_charset (cs);
  set_host_and_target_charsets (cs, current_target_charset);
}

/* Do the real work of setting the target charset.  */
static void
set_target_charset (const char *charset)
{
  struct charset *cs = lookup_charset_or_error (charset);

  set_host_and_target_charsets (current_host_charset, cs);
}


/* 'Set charset', 'set host-charset', 'set target-charset', 'show
   charset' sfunc's.  */

/* This is the sfunc for the 'set charset' command.  */
static void
set_charset_sfunc (char *charset, int from_tty, struct cmd_list_element *c)
{
  struct charset *cs = lookup_charset_or_error (host_charset_name);
  check_valid_host_charset (cs);
  /* CAREFUL: set the target charset here as well. */
  target_charset_name = host_charset_name;
  set_host_and_target_charsets (cs, cs);
}

/* 'set host-charset' command sfunc.  We need a wrapper here because
   the function needs to have a specific signature.  */
static void
set_host_charset_sfunc (char *charset, int from_tty,
			  struct cmd_list_element *c)
{
  set_host_charset (host_charset_name);
}

/* Wrapper for the 'set target-charset' command.  */
static void
set_target_charset_sfunc (char *charset, int from_tty,
			    struct cmd_list_element *c)
{
  set_target_charset (target_charset_name);
}

/* sfunc for the 'show charset' command.  */
static void
show_charset (char *arg, int from_tty)
{
  if (current_host_charset == current_target_charset)
    {
      printf_filtered ("The current host and target character set is `%s'.\n",
                       host_charset ());
    }
  else
    {
      printf_filtered ("The current host character set is `%s'.\n",
                       host_charset ());
      printf_filtered ("The current target character set is `%s'.\n",
                       target_charset ());
    }
}


/* Accessor functions.  */

const char *
host_charset (void)
{
  return current_host_charset->name;
}

const char *
target_charset (void)
{
  return current_target_charset->name;
}



/* Public character management functions.  */


const char *
c_target_char_has_backslash_escape (int target_char)
{
  return ((*c_target_char_has_backslash_escape_func)
          (c_target_char_has_backslash_escape_baton, target_char));
}


int
c_parse_backslash (int host_char, int *target_char)
{
  return (*c_parse_backslash_func) (c_parse_backslash_baton,
                                    host_char, target_char);
}


int
host_char_print_literally (int host_char)
{
  return ((*current_host_charset->host_char_print_literally)
          (current_host_charset->host_char_print_literally_baton,
           host_char));
}


int
target_char_to_control_char (int target_char, int *target_ctrl_char)
{
  return ((*current_target_charset->target_char_to_control_char)
          (current_target_charset->target_char_to_control_char_baton,
           target_char, target_ctrl_char));
}


int
host_char_to_target (int host_char, int *target_char)
{
  return ((*host_char_to_target_func)
          (host_char_to_target_baton, host_char, target_char));
}


int
target_char_to_host (int target_char, int *host_char)
{
  return ((*target_char_to_host_func)
          (target_char_to_host_baton, target_char, host_char));
}



/* The charset.c module initialization function.  */

extern initialize_file_ftype _initialize_charset; /* -Wmissing-prototype */

void
_initialize_charset (void)
{
  struct cmd_list_element *new_cmd;

  /* Register all the character set GDB knows about.

     You should use the same names that iconv does, where possible, to
     take advantage of the iconv-based default behaviors.

     CAUTION: if you register a character set, you must also register
     as many translations as are necessary to make that character set
     interoperate correctly with all the other character sets.  We do
     provide default behaviors when no translation is available, or
     when a translation's function pointer for a particular operation
     is zero.  Hopefully, these defaults will be correct often enough
     that we won't need to provide too many translations.  */
  register_charset (simple_charset ("ASCII", 1,
                                    ascii_print_literally, 0,
                                    ascii_to_control, 0));
  register_charset (iso_8859_family_charset ("ISO-8859-1"));
  register_charset (ebcdic_family_charset ("EBCDIC-US"));
  register_charset (ebcdic_family_charset ("IBM1047"));
  register_iconv_charsets ();

  {
    struct { char *from; char *to; int *table; } tlist[] = {
      { "ASCII",      "ISO-8859-1", ascii_to_iso_8859_1_table },
      { "ASCII",      "EBCDIC-US",  ascii_to_ebcdic_us_table },
      { "ASCII",      "IBM1047",    ascii_to_ibm1047_table },
      { "ISO-8859-1", "ASCII",      iso_8859_1_to_ascii_table },
      { "ISO-8859-1", "EBCDIC-US",  iso_8859_1_to_ebcdic_us_table },
      { "ISO-8859-1", "IBM1047",    iso_8859_1_to_ibm1047_table },
      { "EBCDIC-US",  "ASCII",      ebcdic_us_to_ascii_table },
      { "EBCDIC-US",  "ISO-8859-1", ebcdic_us_to_iso_8859_1_table },
      { "EBCDIC-US",  "IBM1047",    ebcdic_us_to_ibm1047_table },
      { "IBM1047",    "ASCII",      ibm1047_to_ascii_table },
      { "IBM1047",    "ISO-8859-1", ibm1047_to_iso_8859_1_table },
      { "IBM1047",    "EBCDIC-US",  ibm1047_to_ebcdic_us_table }
    };

    int i;

    for (i = 0; i < (sizeof (tlist) / sizeof (tlist[0])); i++)
      register_translation (simple_table_translation (tlist[i].from,
                                                      tlist[i].to,
                                                      tlist[i].table));
  }

  set_host_charset (host_charset_name);
  set_target_charset (target_charset_name);

  new_cmd = add_set_enum_cmd ("charset",
			      class_support,
			      host_charset_enum,
			      &host_charset_name,
                              "Set the host and target character sets.\n"
                              "The `host character set' is the one used by the system GDB is running on.\n"
                              "The `target character set' is the one used by the program being debugged.\n"
                              "You may only use supersets of ASCII for your host character set; GDB does\n"
                              "not support any others.\n"
                              "To see a list of the character sets GDB supports, type `set charset <TAB>'.",
			      &setlist);

  /* Note that the sfunc below needs to set target_charset_name, because 
     the 'set charset' command sets two variables.  */
  set_cmd_sfunc (new_cmd, set_charset_sfunc);
  /* Don't use set_from_show - need to print some extra info. */
  add_cmd ("charset", class_support, show_charset,
	   "Show the host and target character sets.\n"
	   "The `host character set' is the one used by the system GDB is running on.\n"
	   "The `target character set' is the one used by the program being debugged.\n"
	   "You may only use supersets of ASCII for your host character set; GDB does\n"
	   "not support any others.\n"
	   "To see a list of the character sets GDB supports, type `set charset <TAB>'.", 
	   &showlist);


  new_cmd = add_set_enum_cmd ("host-charset",
			      class_support,
			      host_charset_enum,
			      &host_charset_name,
			      "Set the host character set.\n"
			      "The `host character set' is the one used by the system GDB is running on.\n"
			      "You may only use supersets of ASCII for your host character set; GDB does\n"
			      "not support any others.\n"
			      "To see a list of the character sets GDB supports, type `set host-charset <TAB>'.",
			      &setlist);

  set_cmd_sfunc (new_cmd, set_host_charset_sfunc);

  add_show_from_set (new_cmd, &showlist);



  new_cmd = add_set_enum_cmd ("target-charset",
			      class_support,
			      target_charset_enum,
			      &target_charset_name,
			      "Set the target character set.\n"
			      "The `target character set' is the one used by the program being debugged.\n"
			      "GDB translates characters and strings between the host and target\n"
			      "character sets as needed.\n"
			      "To see a list of the character sets GDB supports, type `set target-charset'<TAB>",
			      &setlist);

  set_cmd_sfunc (new_cmd, set_target_charset_sfunc);
  add_show_from_set (new_cmd, &showlist);
}
