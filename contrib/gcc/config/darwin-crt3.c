/* __cxa_atexit backwards-compatibility support for Darwin.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

/* Don't do anything if we are compiling for a kext multilib. */
#ifdef __PIC__

/* It is incorrect to include config.h here, because this file is being
   compiled for the target, and hence definitions concerning only the host
   do not apply.  */

#include "tconfig.h"
#include "tsystem.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* This file works around two different problems.

   The first problem is that there is no __cxa_atexit on Mac OS versions
   before 10.4.  It fixes this by providing a complete atexit and
   __cxa_atexit emulation called from the regular atexit.

   The second problem is that on all shipping versions of Mac OS,
   __cxa_finalize and exit() don't work right: they don't run routines
   that were registered while other atexit routines are running.  This
   is worked around by wrapping each atexit/__cxa_atexit routine with
   our own routine which ensures that any __cxa_atexit calls while it
   is running are honoured.

   There are still problems which this does not solve.  Before 10.4,
   shared objects linked with previous compilers won't have their
   atexit calls properly interleaved with code compiled with newer
   compilers.  Also, atexit routines registered from shared objects
   linked with previous compilers won't get the bug fix.  */

typedef int (*cxa_atexit_p)(void (*func) (void*), void* arg, const void* dso);
typedef void (*cxa_finalize_p)(const void *dso);
typedef int (*atexit_p)(void (*func)(void));

/* These are from "keymgr.h".  */
extern void *_keymgr_get_and_lock_processwide_ptr (unsigned key);
extern int _keymgr_get_and_lock_processwide_ptr_2 (unsigned, void **);
extern int _keymgr_set_and_unlock_processwide_ptr (unsigned key, void *ptr);

extern void *__keymgr_global[];
typedef struct _Sinfo_Node {
        unsigned int size ;             /*size of this node*/
        unsigned short major_version ;  /*API major version.*/
        unsigned short minor_version ;  /*API minor version.*/
        } _Tinfo_Node ;

#ifdef __ppc__
#define CHECK_KEYMGR_ERROR(e) \
  (((_Tinfo_Node *)__keymgr_global[2])->major_version >= 4 ? (e) : 0)
#else
#define CHECK_KEYMGR_ERROR(e) (e)
#endif

/* Our globals are stored under this keymgr index.  */
#define KEYMGR_ATEXIT_LIST	14

/* The different kinds of callback routines.  */
typedef void (*atexit_callback)(void);
typedef void (*cxa_atexit_callback)(void *);

/* This structure holds a routine to call.  There may be extra fields
   at the end of the structure that this code doesn't know about.  */
struct one_atexit_routine 
{
  union {
    atexit_callback ac;
    cxa_atexit_callback cac;
  } callback;
  /* has_arg is 0/2/4 if 'ac' is live, 1/3/5 if 'cac' is live.  
     Higher numbers indicate a later version of the structure that this
     code doesn't understand and will ignore.  */
  int has_arg;
  void * arg;
};

struct atexit_routine_list
{
  struct atexit_routine_list * next;
  struct one_atexit_routine r;
};

/* The various possibilities for status of atexit().  */
enum atexit_status {
  atexit_status_unknown = 0,
  atexit_status_missing = 1,
  atexit_status_broken = 2,
  atexit_status_working = 16
};

struct keymgr_atexit_list
{
  /* Version of this list.  This code knows only about version 0.
     If the version is higher than 0, this code may add new atexit routines
     but should not attempt to run the list.  */
  short version;
  /* 1 if an atexit routine is currently being run by this code, 0
     otherwise.  */
  char running_routines;
  /* Holds a value from 'enum atexit_status'.  */
  unsigned char atexit_status;
  /* The list of atexit and cxa_atexit routines registered.  If
   atexit_status_missing it contains all routines registered while
   linked with this code.  If atexit_status_broken it contains all
   routines registered during cxa_finalize while linked with this
   code.  */
  struct atexit_routine_list *l;
  /* &__cxa_atexit; set if atexit_status >= atexit_status_broken.  */
  cxa_atexit_p cxa_atexit_f;
  /* &__cxa_finalize; set if atexit_status >= atexit_status_broken.  */
  cxa_finalize_p cxa_finalize_f;
  /* &atexit; set if atexit_status >= atexit_status_working
     or atexit_status == atexit_status_missing.  */
  atexit_p atexit_f;
};

/* Return 0 if __cxa_atexit has the bug it has in Mac OS 10.4: it
   fails to call routines registered while an atexit routine is
   running.  Return 1 if it works properly, and -1 if an error occurred.  */

struct atexit_data 
{
  int result;
  cxa_atexit_p cxa_atexit;
};

static void cxa_atexit_check_2 (void *arg)
{
  ((struct atexit_data *)arg)->result = 1;
}

static void cxa_atexit_check_1 (void *arg)
{
  struct atexit_data * aed = arg;
  if (aed->cxa_atexit (cxa_atexit_check_2, arg, arg) != 0)
    aed->result = -1;
}

static int
check_cxa_atexit (cxa_atexit_p cxa_atexit, cxa_finalize_p cxa_finalize)
{
  struct atexit_data aed = { 0, cxa_atexit };

  /* We re-use &aed as the 'dso' parameter, since it's a unique address.  */
  if (cxa_atexit (cxa_atexit_check_1, &aed, &aed) != 0)
    return -1;
  cxa_finalize (&aed);
  if (aed.result == 0)
    {
      /* Call __cxa_finalize again to make sure that cxa_atexit_check_2
	 is removed from the list before AED goes out of scope.  */
      cxa_finalize (&aed);
      aed.result = 0;
    }
  return aed.result;
}

#ifdef __ppc__
/* This comes from Csu.  It works only before 10.4.  The prototype has
   been altered a bit to avoid casting.  */
extern int _dyld_func_lookup(const char *dyld_func_name,
     void *address) __attribute__((visibility("hidden")));

static void our_atexit (void);

/* We're running on 10.3.9.  Find the address of the system atexit()
   function.  So easy to say, so hard to do.  */
static atexit_p
find_atexit_10_3 (void)
{
  unsigned int (*dyld_image_count_fn)(void);
  const char *(*dyld_get_image_name_fn)(unsigned int image_index);
  const void *(*dyld_get_image_header_fn)(unsigned int image_index);
  const void *(*NSLookupSymbolInImage_fn)(const void *image, 
					  const char *symbolName,
					  unsigned int options);
  void *(*NSAddressOfSymbol_fn)(const void *symbol);
  unsigned i, count;
  
  /* Find some dyld functions.  */
  _dyld_func_lookup("__dyld_image_count", &dyld_image_count_fn);
  _dyld_func_lookup("__dyld_get_image_name", &dyld_get_image_name_fn);
  _dyld_func_lookup("__dyld_get_image_header", &dyld_get_image_header_fn);
  _dyld_func_lookup("__dyld_NSLookupSymbolInImage", &NSLookupSymbolInImage_fn);
  _dyld_func_lookup("__dyld_NSAddressOfSymbol", &NSAddressOfSymbol_fn);

  /* If any of these don't exist, that's an error.  */
  if (! dyld_image_count_fn || ! dyld_get_image_name_fn
      || ! dyld_get_image_header_fn || ! NSLookupSymbolInImage_fn
      || ! NSAddressOfSymbol_fn)
    return NULL;
  
  count = dyld_image_count_fn ();
  for (i = 0; i < count; i++)
    {
      const char * path = dyld_get_image_name_fn (i);
      const void * image;
      const void * symbol;
      
      if (strcmp (path, "/usr/lib/libSystem.B.dylib") != 0)
	continue;
      image = dyld_get_image_header_fn (i);
      if (! image)
	return NULL;
      /* '4' is NSLOOKUPSYMBOLINIMAGE_OPTION_RETURN_ON_ERROR.  */
      symbol = NSLookupSymbolInImage_fn (image, "_atexit", 4);
      if (! symbol)
	return NULL;
      return NSAddressOfSymbol_fn (symbol);
    }
  return NULL;
}
#endif

/* Create (if necessary), find, lock, fill in, and return our globals.  
   Return NULL on error, in which case the globals will not be locked.  
   The caller should call keymgr_set_and_unlock.  */
static struct keymgr_atexit_list *
get_globals (void)
{
  struct keymgr_atexit_list * r;
  
#ifdef __ppc__
  /* 10.3.9 doesn't have _keymgr_get_and_lock_processwide_ptr_2 so the
     PPC side can't use it.  On 10.4 this just means the error gets
     reported a little later when
     _keymgr_set_and_unlock_processwide_ptr finds that the key was
     never locked.  */
  r = _keymgr_get_and_lock_processwide_ptr (KEYMGR_ATEXIT_LIST);
#else
  void * rr;
  if (_keymgr_get_and_lock_processwide_ptr_2 (KEYMGR_ATEXIT_LIST, &rr))
    return NULL;
  r = rr;
#endif
  
  if (r == NULL)
    {
      r = calloc (sizeof (struct keymgr_atexit_list), 1);
      if (! r)
	return NULL;
    }

  if (r->atexit_status == atexit_status_unknown)
    {
      void *handle;

      handle = dlopen ("/usr/lib/libSystem.B.dylib", RTLD_NOLOAD);
      if (!handle)
	{
#ifdef __ppc__
	  r->atexit_status = atexit_status_missing;
	  r->atexit_f = find_atexit_10_3 ();
	  if (! r->atexit_f)
	    goto error;
	  if (r->atexit_f (our_atexit))
	    goto error;
#else
	  goto error;
#endif
	}
      else
	{
	  int chk_result;

	  r->cxa_atexit_f = (cxa_atexit_p)dlsym (handle, "__cxa_atexit");
	  r->cxa_finalize_f = (cxa_finalize_p)dlsym (handle, "__cxa_finalize");
	  if (! r->cxa_atexit_f || ! r->cxa_finalize_f)
	    goto error;

	  chk_result = check_cxa_atexit (r->cxa_atexit_f, r->cxa_finalize_f);
	  if (chk_result == -1)
	    goto error;
	  else if (chk_result == 0)
	    r->atexit_status = atexit_status_broken;
	  else
	    {
	      r->atexit_f = (atexit_p)dlsym (handle, "atexit");
	      if (! r->atexit_f)
		goto error;
	      r->atexit_status = atexit_status_working;
	    }
	}
    }

  return r;
  
 error:
  _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, r);
  return NULL;
}

/* Add TO_ADD to ATEXIT_LIST.  ATEXIT_LIST may be NULL but is
   always the result of calling _keymgr_get_and_lock_processwide_ptr and
   so KEYMGR_ATEXIT_LIST is known to be locked; this routine is responsible
   for unlocking it.  */

static int
add_routine (struct keymgr_atexit_list * g,
	     const struct one_atexit_routine * to_add)
{
  struct atexit_routine_list * s
    = malloc (sizeof (struct atexit_routine_list));
  int result;
  
  if (!s)
    {
      _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
      return -1;
    }
  s->r = *to_add;
  s->next = g->l;
  g->l = s;
  result = _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
  return CHECK_KEYMGR_ERROR (result) == 0 ? 0 : -1;
}

/* This runs the routines in G->L up to STOP.  */
static struct keymgr_atexit_list *
run_routines (struct keymgr_atexit_list *g,
	      struct atexit_routine_list *stop)
{
  for (;;)
    {
      struct atexit_routine_list * cur = g->l;
      if (! cur || cur == stop)
	break;
      g->l = cur->next;
      _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);

      switch (cur->r.has_arg) {
      case 0: case 2: case 4:
	cur->r.callback.ac ();
	break;
      case 1: case 3: case 5:
	cur->r.callback.cac (cur->r.arg);
	break;
      default:
	/* Don't understand, so don't call it.  */
	break;
      }
      free (cur);

      g = _keymgr_get_and_lock_processwide_ptr (KEYMGR_ATEXIT_LIST);
      if (! g)
	break;
    }
  return g;
}

/* Call the routine described by ROUTINE_PARAM and then call any
   routines added to KEYMGR_ATEXIT_LIST while that routine was
   running, all with in_cxa_finalize set.  */

static void
cxa_atexit_wrapper (void* routine_param)
{
  struct one_atexit_routine * routine = routine_param;
  struct keymgr_atexit_list *g;
  struct atexit_routine_list * base = NULL;
  char prev_running = 0;
  
  g = _keymgr_get_and_lock_processwide_ptr (KEYMGR_ATEXIT_LIST);
  if (g)
    {
      prev_running = g->running_routines;
      g->running_routines = 1;
      base = g->l;
      _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
    }

  if (routine->has_arg)
    routine->callback.cac (routine->arg);
  else
    routine->callback.ac ();

  if (g)
    g = _keymgr_get_and_lock_processwide_ptr (KEYMGR_ATEXIT_LIST);
  if (g)
    g = run_routines (g, base);
  if (g)
    {
      g->running_routines = prev_running;
      _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
    }
}

#ifdef __ppc__
/* This code is used while running on 10.3.9, when __cxa_atexit doesn't
   exist in the system library.  10.3.9 only supported regular PowerPC,
   so this code isn't necessary on x86 or ppc64.  */

/* This routine is called from the system atexit(); it runs everything
   registered on the KEYMGR_ATEXIT_LIST.  */

static void
our_atexit (void)
{
  struct keymgr_atexit_list *g;
  char prev_running;

  g = _keymgr_get_and_lock_processwide_ptr (KEYMGR_ATEXIT_LIST);
  if (! g || g->version != 0 || g->atexit_status != atexit_status_missing)
    return;
  
  prev_running = g->running_routines;
  g->running_routines = 1;
  g = run_routines (g, NULL);
  if (! g)
    return;
  g->running_routines = prev_running;
  _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
}
#endif

/* This is our wrapper around atexit and __cxa_atexit.  It will return
   nonzero if an error occurs, and otherwise:
   - if in_cxa_finalize is set, or running on 10.3.9, add R to
     KEYMGR_ATEXIT_LIST; or
   - call the system __cxa_atexit to add cxa_atexit_wrapper with an argument
     that indicates how cxa_atexit_wrapper should call R.  */

static int
atexit_common (const struct one_atexit_routine *r, const void *dso)
{
  struct keymgr_atexit_list *g = get_globals ();

  if (! g)
    return -1;
  
  if (g->running_routines || g->atexit_status == atexit_status_missing)
    return add_routine (g, r);

  if (g->atexit_status >= atexit_status_working)
    {
      int result;
      if (r->has_arg)
	{
	  cxa_atexit_p cxa_atexit = g->cxa_atexit_f;
	  result = _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST,
							   g);
	  if (CHECK_KEYMGR_ERROR (result))
	    return -1;
	  return cxa_atexit (r->callback.cac, r->arg, dso);
	}
      else
	{
	  atexit_p atexit_f = g->atexit_f;
	  result = _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST,
							   g);
	  if (CHECK_KEYMGR_ERROR (result))
	    return -1;
	  return atexit_f (r->callback.ac);
	}
    }
  else
    {
      cxa_atexit_p cxa_atexit = g->cxa_atexit_f;
      struct one_atexit_routine *alloced;
      int result;

      result = _keymgr_set_and_unlock_processwide_ptr (KEYMGR_ATEXIT_LIST, g);
      if (CHECK_KEYMGR_ERROR (result))
	return -1;

      alloced = malloc (sizeof (struct one_atexit_routine));
      if (! alloced)
	return -1;
      *alloced = *r;
      return cxa_atexit (cxa_atexit_wrapper, alloced, dso);
    }
}

/* These are the actual replacement routines; they just funnel into
   atexit_common.  */

int __cxa_atexit (cxa_atexit_callback func, void* arg, 
		  const void* dso) __attribute__((visibility("hidden")));

int
__cxa_atexit (cxa_atexit_callback func, void* arg, const void* dso)
{
  struct one_atexit_routine r;
  r.callback.cac = func;
  r.has_arg = 1;
  r.arg = arg;
  return atexit_common (&r, dso);
}

int atexit (atexit_callback func) __attribute__((visibility("hidden")));

/* Use __dso_handle to allow even bundles that call atexit() to be unloaded
   on 10.4.  */
extern void __dso_handle;

int
atexit (atexit_callback func)
{
  struct one_atexit_routine r;
  r.callback.ac = func;
  r.has_arg = 0;
  return atexit_common (&r, &__dso_handle);
}

#endif /* __PIC__ */
