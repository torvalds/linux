/* Darwin support needed only by C/C++ frontends.
   Copyright (C) 2001, 2003, 2004, 2005  Free Software Foundation, Inc.
   Contributed by Apple Computer Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "cpplib.h"
#include "tree.h"
#include "c-pragma.h"
#include "c-tree.h"
#include "c-incpath.h"
#include "c-common.h"
#include "toplev.h"
#include "flags.h"
#include "tm_p.h"
#include "cppdefault.h"
#include "prefix.h"

/* Pragmas.  */

#define BAD(gmsgid) do { warning (OPT_Wpragmas, gmsgid); return; } while (0)
#define BAD2(msgid, arg) do { warning (OPT_Wpragmas, msgid, arg); return; } while (0)

static bool using_frameworks = false;

/* Maintain a small stack of alignments.  This is similar to pragma
   pack's stack, but simpler.  */

static void push_field_alignment (int);
static void pop_field_alignment (void);
static const char *find_subframework_file (const char *, const char *);
static void add_system_framework_path (char *);
static const char *find_subframework_header (cpp_reader *pfile, const char *header,
					     cpp_dir **dirp);

typedef struct align_stack
{
  int alignment;
  struct align_stack * prev;
} align_stack;

static struct align_stack * field_align_stack = NULL;

static void
push_field_alignment (int bit_alignment)
{
  align_stack *entry = XNEW (align_stack);

  entry->alignment = maximum_field_alignment;
  entry->prev = field_align_stack;
  field_align_stack = entry;

  maximum_field_alignment = bit_alignment;
}

static void
pop_field_alignment (void)
{
  if (field_align_stack)
    {
      align_stack *entry = field_align_stack;

      maximum_field_alignment = entry->alignment;
      field_align_stack = entry->prev;
      free (entry);
    }
  else
    error ("too many #pragma options align=reset");
}

/* Handlers for Darwin-specific pragmas.  */

void
darwin_pragma_ignore (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  /* Do nothing.  */
}

/* #pragma options align={mac68k|power|reset} */

void
darwin_pragma_options (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  const char *arg;
  tree t, x;

  if (pragma_lex (&t) != CPP_NAME)
    BAD ("malformed '#pragma options', ignoring");
  arg = IDENTIFIER_POINTER (t);
  if (strcmp (arg, "align"))
    BAD ("malformed '#pragma options', ignoring");
  if (pragma_lex (&t) != CPP_EQ)
    BAD ("malformed '#pragma options', ignoring");
  if (pragma_lex (&t) != CPP_NAME)
    BAD ("malformed '#pragma options', ignoring");

  if (pragma_lex (&x) != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of '#pragma options'");

  arg = IDENTIFIER_POINTER (t);
  if (!strcmp (arg, "mac68k"))
    push_field_alignment (16);
  else if (!strcmp (arg, "power"))
    push_field_alignment (0);
  else if (!strcmp (arg, "reset"))
    pop_field_alignment ();
  else
    BAD ("malformed '#pragma options align={mac68k|power|reset}', ignoring");
}

/* #pragma unused ([var {, var}*]) */

void
darwin_pragma_unused (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  tree decl, x;
  int tok;

  if (pragma_lex (&x) != CPP_OPEN_PAREN)
    BAD ("missing '(' after '#pragma unused', ignoring");

  while (1)
    {
      tok = pragma_lex (&decl);
      if (tok == CPP_NAME && decl)
	{
	  tree local = lookup_name (decl);
	  if (local && (TREE_CODE (local) == PARM_DECL
			|| TREE_CODE (local) == VAR_DECL))
	    TREE_USED (local) = 1;
	  tok = pragma_lex (&x);
	  if (tok != CPP_COMMA)
	    break;
	}
    }

  if (tok != CPP_CLOSE_PAREN)
    BAD ("missing ')' after '#pragma unused', ignoring");

  if (pragma_lex (&x) != CPP_EOF)
    BAD ("junk at end of '#pragma unused'");
}

/* Parse the ms_struct pragma.  */
void
darwin_pragma_ms_struct (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  const char *arg;
  tree t;

  if (pragma_lex (&t) != CPP_NAME)
    BAD ("malformed '#pragma ms_struct', ignoring");
  arg = IDENTIFIER_POINTER (t);

  if (!strcmp (arg, "on"))
    darwin_ms_struct = true;
  else if (!strcmp (arg, "off") || !strcmp (arg, "reset"))
    darwin_ms_struct = false;
  else
    BAD ("malformed '#pragma ms_struct {on|off|reset}', ignoring");

  if (pragma_lex (&t) != CPP_EOF)
    BAD ("junk at end of '#pragma ms_struct'");
}

static struct {
  size_t len;
  const char *name;
  cpp_dir* dir;
} *frameworks_in_use;
static int num_frameworks = 0;
static int max_frameworks = 0;


/* Remember which frameworks have been seen, so that we can ensure
   that all uses of that framework come from the same framework.  DIR
   is the place where the named framework NAME, which is of length
   LEN, was found.  We copy the directory name from NAME, as it will be
   freed by others.  */

static void
add_framework (const char *name, size_t len, cpp_dir *dir)
{
  char *dir_name;
  int i;
  for (i = 0; i < num_frameworks; ++i)
    {
      if (len == frameworks_in_use[i].len
	  && strncmp (name, frameworks_in_use[i].name, len) == 0)
	{
	  return;
	}
    }
  if (i >= max_frameworks)
    {
      max_frameworks = i*2;
      max_frameworks += i == 0;
      frameworks_in_use = xrealloc (frameworks_in_use,
				    max_frameworks*sizeof(*frameworks_in_use));
    }
  dir_name = XNEWVEC (char, len + 1);
  memcpy (dir_name, name, len);
  dir_name[len] = '\0';
  frameworks_in_use[num_frameworks].name = dir_name;
  frameworks_in_use[num_frameworks].len = len;
  frameworks_in_use[num_frameworks].dir = dir;
  ++num_frameworks;
}

/* Recall if we have seen the named framework NAME, before, and where
   we saw it.  NAME is LEN bytes long.  The return value is the place
   where it was seen before.  */

static struct cpp_dir*
find_framework (const char *name, size_t len)
{
  int i;
  for (i = 0; i < num_frameworks; ++i)
    {
      if (len == frameworks_in_use[i].len
	  && strncmp (name, frameworks_in_use[i].name, len) == 0)
	{
	  return frameworks_in_use[i].dir;
	}
    }
  return 0;
}

/* There are two directories in a framework that contain header files,
   Headers and PrivateHeaders.  We search Headers first as it is more
   common to upgrade a header from PrivateHeaders to Headers and when
   that is done, the old one might hang around and be out of data,
   causing grief.  */

struct framework_header {const char * dirName; int dirNameLen; };
static struct framework_header framework_header_dirs[] = {
  { "Headers", 7 },
  { "PrivateHeaders", 14 },
  { NULL, 0 }
};

/* Returns a pointer to a malloced string that contains the real pathname
   to the file, given the base name and the name.  */

static char *
framework_construct_pathname (const char *fname, cpp_dir *dir)
{
  char *buf;
  size_t fname_len, frname_len;
  cpp_dir *fast_dir;
  char *frname;
  struct stat st;
  int i;

  /* Framework names must have a / in them.  */
  buf = strchr (fname, '/');
  if (buf)
    fname_len = buf - fname;
  else
    return 0;

  fast_dir = find_framework (fname, fname_len);

  /* Framework includes must all come from one framework.  */
  if (fast_dir && dir != fast_dir)
    return 0;

  frname = XNEWVEC (char, strlen (fname) + dir->len + 2
		    + strlen(".framework/") + strlen("PrivateHeaders"));
  strncpy (&frname[0], dir->name, dir->len);
  frname_len = dir->len;
  if (frname_len && frname[frname_len-1] != '/')
    frname[frname_len++] = '/';
  strncpy (&frname[frname_len], fname, fname_len);
  frname_len += fname_len;
  strncpy (&frname[frname_len], ".framework/", strlen (".framework/"));
  frname_len += strlen (".framework/");

  if (fast_dir == 0)
    {
      frname[frname_len-1] = 0;
      if (stat (frname, &st) == 0)
	{
	  /* As soon as we find the first instance of the framework,
	     we stop and never use any later instance of that
	     framework.  */
	  add_framework (fname, fname_len, dir);
	}
      else
	{
	  /* If we can't find the parent directory, no point looking
	     further.  */
	  free (frname);
	  return 0;
	}
      frname[frname_len-1] = '/';
    }

  /* Append framework_header_dirs and header file name */
  for (i = 0; framework_header_dirs[i].dirName; i++)
    {
      strncpy (&frname[frname_len],
	       framework_header_dirs[i].dirName,
	       framework_header_dirs[i].dirNameLen);
      strcpy (&frname[frname_len + framework_header_dirs[i].dirNameLen],
	      &fname[fname_len]);

      if (stat (frname, &st) == 0)
	return frname;
    }

  free (frname);
  return 0;
}

/* Search for FNAME in sub-frameworks.  pname is the context that we
   wish to search in.  Return the path the file was found at,
   otherwise return 0.  */

static const char*
find_subframework_file (const char *fname, const char *pname)
{
  char *sfrname;
  const char *dot_framework = ".framework/";
  char *bufptr;
  int sfrname_len, i, fname_len;
  struct cpp_dir *fast_dir;
  static struct cpp_dir subframe_dir;
  struct stat st;

  bufptr = strchr (fname, '/');

  /* Subframework files must have / in the name.  */
  if (bufptr == 0)
    return 0;

  fname_len = bufptr - fname;
  fast_dir = find_framework (fname, fname_len);

  /* Sub framework header filename includes parent framework name and
     header name in the "CarbonCore/OSUtils.h" form. If it does not
     include slash it is not a sub framework include.  */
  bufptr = strstr (pname, dot_framework);

  /* If the parent header is not of any framework, then this header
     cannot be part of any subframework.  */
  if (!bufptr)
    return 0;

  /* Now translate. For example,                  +- bufptr
     fname = CarbonCore/OSUtils.h                 |
     pname = /System/Library/Frameworks/Foundation.framework/Headers/Foundation.h
     into
     sfrname = /System/Library/Frameworks/Foundation.framework/Frameworks/CarbonCore.framework/Headers/OSUtils.h */

  sfrname = XNEWVEC (char, strlen (pname) + strlen (fname) + 2 +
			      strlen ("Frameworks/") + strlen (".framework/")
			      + strlen ("PrivateHeaders"));

  bufptr += strlen (dot_framework);

  sfrname_len = bufptr - pname;

  strncpy (&sfrname[0], pname, sfrname_len);

  strncpy (&sfrname[sfrname_len], "Frameworks/", strlen ("Frameworks/"));
  sfrname_len += strlen("Frameworks/");

  strncpy (&sfrname[sfrname_len], fname, fname_len);
  sfrname_len += fname_len;

  strncpy (&sfrname[sfrname_len], ".framework/", strlen (".framework/"));
  sfrname_len += strlen (".framework/");

  /* Append framework_header_dirs and header file name */
  for (i = 0; framework_header_dirs[i].dirName; i++)
    {
      strncpy (&sfrname[sfrname_len],
	       framework_header_dirs[i].dirName,
	       framework_header_dirs[i].dirNameLen);
      strcpy (&sfrname[sfrname_len + framework_header_dirs[i].dirNameLen],
	      &fname[fname_len]);

      if (stat (sfrname, &st) == 0)
	{
	  if (fast_dir != &subframe_dir)
	    {
	      if (fast_dir)
		warning (0, "subframework include %s conflicts with framework include",
			 fname);
	      else
		add_framework (fname, fname_len, &subframe_dir);
	    }

	  return sfrname;
	}
    }
  free (sfrname);

  return 0;
}

/* Add PATH to the system includes. PATH must be malloc-ed and
   NUL-terminated.  System framework paths are C++ aware.  */

static void
add_system_framework_path (char *path)
{
  int cxx_aware = 1;
  cpp_dir *p;

  p = XNEW (cpp_dir);
  p->next = NULL;
  p->name = path;
  p->sysp = 1 + !cxx_aware;
  p->construct = framework_construct_pathname;
  using_frameworks = 1;

  add_cpp_dir_path (p, SYSTEM);
}

/* Add PATH to the bracket includes. PATH must be malloc-ed and
   NUL-terminated.  */

void
add_framework_path (char *path)
{
  cpp_dir *p;

  p = XNEW (cpp_dir);
  p->next = NULL;
  p->name = path;
  p->sysp = 0;
  p->construct = framework_construct_pathname;
  using_frameworks = 1;

  add_cpp_dir_path (p, BRACKET);
}

static const char *framework_defaults [] =
  {
    "/System/Library/Frameworks",
    "/Library/Frameworks",
  };

/* Register the GNU objective-C runtime include path if STDINC.  */

void
darwin_register_objc_includes (const char *sysroot, const char *iprefix,
			       int stdinc)
{
  const char *fname;
  size_t len;
  /* We do not do anything if we do not want the standard includes. */
  if (!stdinc)
    return;

  fname = GCC_INCLUDE_DIR "-gnu-runtime";

  /* Register the GNU OBJC runtime include path if we are compiling  OBJC
    with GNU-runtime.  */

  if (c_dialect_objc () && !flag_next_runtime)
    {
      char *str;
      /* See if our directory starts with the standard prefix.
	 "Translate" them, i.e. replace /usr/local/lib/gcc... with
	 IPREFIX and search them first.  */
      if (iprefix && (len = cpp_GCC_INCLUDE_DIR_len) != 0 && !sysroot
	  && !strncmp (fname, cpp_GCC_INCLUDE_DIR, len))
	{
	  str = concat (iprefix, fname + len, NULL);
          /* FIXME: wrap the headers for C++awareness.  */
	  add_path (str, SYSTEM, /*c++aware=*/false, false);
	}

      /* Should this directory start with the sysroot?  */
      if (sysroot)
	str = concat (sysroot, fname, NULL);
      else
	str = update_path (fname, "");

      add_path (str, SYSTEM, /*c++aware=*/false, false);
    }
}


/* Register all the system framework paths if STDINC is true and setup
   the missing_header callback for subframework searching if any
   frameworks had been registered.  */

void
darwin_register_frameworks (const char *sysroot,
			    const char *iprefix ATTRIBUTE_UNUSED, int stdinc)
{
  if (stdinc)
    {
      size_t i;

      /* Setup default search path for frameworks.  */
      for (i=0; i<sizeof (framework_defaults)/sizeof(const char *); ++i)
	{
	  char *str;
	  if (sysroot)
	    str = concat (sysroot, xstrdup (framework_defaults [i]), NULL);
	  else
	    str = xstrdup (framework_defaults[i]);
	  /* System Framework headers are cxx aware.  */
	  add_system_framework_path (str);
	}
    }

  if (using_frameworks)
    cpp_get_callbacks (parse_in)->missing_header = find_subframework_header;
}

/* Search for HEADER in context dependent way.  The return value is
   the malloced name of a header to try and open, if any, or NULL
   otherwise.  This is called after normal header lookup processing
   fails to find a header.  We search each file in the include stack,
   using FUNC, starting from the most deeply nested include and
   finishing with the main input file.  We stop searching when FUNC
   returns nonzero.  */

static const char*
find_subframework_header (cpp_reader *pfile, const char *header, cpp_dir **dirp)
{
  const char *fname = header;
  struct cpp_buffer *b;
  const char *n;

  for (b = cpp_get_buffer (pfile);
       b && cpp_get_file (b) && cpp_get_path (cpp_get_file (b));
       b = cpp_get_prev (b))
    {
      n = find_subframework_file (fname, cpp_get_path (cpp_get_file (b)));
      if (n)
	{
	  /* Logically, the place where we found the subframework is
	     the place where we found the Framework that contains the
	     subframework.  This is useful for tracking wether or not
	     we are in a system header.  */
	  *dirp = cpp_get_dir (cpp_get_file (b));
	  return n;
	}
    }

  return 0;
}

/* Return the value of darwin_macosx_version_min suitable for the
   __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ macro,
   so '10.4.2' becomes 1042.
   Print a warning if the version number is not known.  */
static const char *
version_as_macro (void)
{
  static char result[] = "1000";

  if (strncmp (darwin_macosx_version_min, "10.", 3) != 0)
    goto fail;
  if (! ISDIGIT (darwin_macosx_version_min[3]))
    goto fail;
  result[2] = darwin_macosx_version_min[3];
  if (darwin_macosx_version_min[4] != '\0')
    {
      if (darwin_macosx_version_min[4] != '.')
	goto fail;
      if (! ISDIGIT (darwin_macosx_version_min[5]))
	goto fail;
      if (darwin_macosx_version_min[6] != '\0')
	goto fail;
      result[3] = darwin_macosx_version_min[5];
    }
  else
    result[3] = '0';

  return result;

 fail:
  error ("Unknown value %qs of -mmacosx-version-min",
	 darwin_macosx_version_min);
  return "1000";
}

/* Define additional CPP flags for Darwin.   */

#define builtin_define(TXT) cpp_define (pfile, TXT)

void
darwin_cpp_builtins (cpp_reader *pfile)
{
  builtin_define ("__MACH__");
  builtin_define ("__APPLE__");

  /* __APPLE_CC__ is defined as some old Apple include files expect it
     to be defined and won't work if it isn't.  */
  builtin_define_with_value ("__APPLE_CC__", "1", false);

  if (darwin_macosx_version_min)
    builtin_define_with_value ("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__",
			       version_as_macro(), false);
}
