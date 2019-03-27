/* Frame unwinder for frames using the libunwind library.

   Copyright 2003 Free Software Foundation, Inc.

   Written by Jeff Johnston, contributed by Red Hat Inc.

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

#include "inferior.h"
#include "frame.h"
#include "frame-base.h"
#include "frame-unwind.h"
#include "gdbcore.h"
#include "gdbtypes.h"
#include "symtab.h"
#include "objfiles.h"
#include "regcache.h"

#include <dlfcn.h>

#include "gdb_assert.h"
#include "gdb_string.h"

#include "libunwind-frame.h"

#include "complaints.h"

static int libunwind_initialized;
static struct gdbarch_data *libunwind_descr_handle;

#ifndef LIBUNWIND_SO
#define LIBUNWIND_SO "libunwind.so"
#endif

/* Required function pointers from libunwind.  */
static int (*unw_get_reg_p) (unw_cursor_t *, unw_regnum_t, unw_word_t *);
static int (*unw_get_fpreg_p) (unw_cursor_t *, unw_regnum_t, unw_fpreg_t *);
static int (*unw_get_saveloc_p) (unw_cursor_t *, unw_regnum_t, unw_save_loc_t *);
static int (*unw_step_p) (unw_cursor_t *);
static int (*unw_init_remote_p) (unw_cursor_t *, unw_addr_space_t, void *);
static unw_addr_space_t (*unw_create_addr_space_p) (unw_accessors_t *, int);
static int (*unw_search_unwind_table_p) (unw_addr_space_t, unw_word_t, unw_dyn_info_t *,
					 unw_proc_info_t *, int, void *);
static unw_word_t (*unw_find_dyn_list_p) (unw_addr_space_t, unw_dyn_info_t *,
					  void *);


struct libunwind_frame_cache
{
  CORE_ADDR base;
  CORE_ADDR func_addr;
  unw_cursor_t cursor;
};

/* We need to qualify the function names with a platform-specific prefix to match 
   the names used by the libunwind library.  The UNW_OBJ macro is provided by the
   libunwind.h header file.  */
#define STRINGIFY2(name)	#name
#define STRINGIFY(name)		STRINGIFY2(name)

static char *get_reg_name = STRINGIFY(UNW_OBJ(get_reg));
static char *get_fpreg_name = STRINGIFY(UNW_OBJ(get_fpreg));
static char *get_saveloc_name = STRINGIFY(UNW_OBJ(get_save_loc));
static char *step_name = STRINGIFY(UNW_OBJ(step));
static char *init_remote_name = STRINGIFY(UNW_OBJ(init_remote));
static char *create_addr_space_name = STRINGIFY(UNW_OBJ(create_addr_space));
static char *search_unwind_table_name = STRINGIFY(UNW_OBJ(search_unwind_table));
static char *find_dyn_list_name = STRINGIFY(UNW_OBJ(find_dyn_list));

static struct libunwind_descr *
libunwind_descr (struct gdbarch *gdbarch)
{
  return gdbarch_data (gdbarch, libunwind_descr_handle);
}

static void *
libunwind_descr_init (struct gdbarch *gdbarch)
{
  struct libunwind_descr *descr = GDBARCH_OBSTACK_ZALLOC (gdbarch,
							  struct libunwind_descr);
  return descr;
}

void
libunwind_frame_set_descr (struct gdbarch *gdbarch, struct libunwind_descr *descr)
{
  struct libunwind_descr *arch_descr;

  gdb_assert (gdbarch != NULL);

  arch_descr = gdbarch_data (gdbarch, libunwind_descr_handle);

  if (arch_descr == NULL)
    {
      /* First time here.  Must initialize data area.  */
      arch_descr = libunwind_descr_init (gdbarch);
      set_gdbarch_data (gdbarch, libunwind_descr_handle, arch_descr);
    }

  /* Copy new descriptor info into arch descriptor.  */
  arch_descr->gdb2uw = descr->gdb2uw;
  arch_descr->uw2gdb = descr->uw2gdb;
  arch_descr->is_fpreg = descr->is_fpreg;
  arch_descr->accessors = descr->accessors;
}

static struct libunwind_frame_cache *
libunwind_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  unw_accessors_t *acc;
  unw_addr_space_t as;
  unw_word_t fp;
  unw_regnum_t uw_sp_regnum;
  struct libunwind_frame_cache *cache;
  struct libunwind_descr *descr;
  int i, ret;

  if (*this_cache)
    return *this_cache;

  /* Allocate a new cache.  */
  cache = FRAME_OBSTACK_ZALLOC (struct libunwind_frame_cache);

  cache->func_addr = frame_func_unwind (next_frame);

  /* Get a libunwind cursor to the previous frame.  We do this by initializing
     a cursor.  Libunwind treats a new cursor as the top of stack and will get
     the current register set via the libunwind register accessor.  Now, we
     provide the platform-specific accessors and we set up the register accessor to use
     the frame register unwinding interfaces so that we properly get the registers for
     the current frame rather than the top.  We then use the  unw_step function to 
     move the libunwind cursor back one frame.  We can later use this cursor to find previous 
     registers via the unw_get_reg interface which will invoke libunwind's special logic.  */
  descr = libunwind_descr (get_frame_arch (next_frame));
  acc = descr->accessors;
  as =  unw_create_addr_space_p (acc,
				 TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
				 ? __BIG_ENDIAN
				 : __LITTLE_ENDIAN);

  unw_init_remote_p (&cache->cursor, as, next_frame);
  unw_step_p (&cache->cursor);

  /* To get base address, get sp from previous frame.  */
  uw_sp_regnum = descr->gdb2uw (SP_REGNUM);
  ret = unw_get_reg_p (&cache->cursor, uw_sp_regnum, &fp);
  if (ret < 0)
    error ("Can't get libunwind sp register.");

  cache->base = (CORE_ADDR)fp;

  *this_cache = cache;
  return cache;
}

unw_word_t
libunwind_find_dyn_list (unw_addr_space_t as, unw_dyn_info_t *di, void *arg)
{
  return unw_find_dyn_list_p (as, di, arg);
}

static const struct frame_unwind libunwind_frame_unwind =
{
  NORMAL_FRAME,
  libunwind_frame_this_id,
  libunwind_frame_prev_register
};

/* Verify if there is sufficient libunwind information for the frame to use
   libunwind frame unwinding.  */
const struct frame_unwind *
libunwind_frame_sniffer (struct frame_info *next_frame)
{
  unw_cursor_t cursor;
  unw_accessors_t *acc;
  unw_addr_space_t as;
  struct libunwind_descr *descr;
  int i, ret;

  /* To test for libunwind unwind support, initialize a cursor to the current frame and try to back
     up.  We use this same method when setting up the frame cache (see libunwind_frame_cache()).
     If libunwind returns success for this operation, it means that it has found sufficient
     libunwind unwinding information to do so.  */

  descr = libunwind_descr (get_frame_arch (next_frame));
  acc = descr->accessors;
  as =  unw_create_addr_space_p (acc,
				 TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
				 ? __BIG_ENDIAN
				 : __LITTLE_ENDIAN);

  ret = unw_init_remote_p (&cursor, as, next_frame);

  if (ret >= 0)
    ret = unw_step_p (&cursor);

  if (ret < 0)
    return NULL;

  return &libunwind_frame_unwind;
}

void
libunwind_frame_this_id (struct frame_info *next_frame, void **this_cache,
		      struct frame_id *this_id)
{
  struct libunwind_frame_cache *cache =
    libunwind_frame_cache (next_frame, this_cache);

  (*this_id) = frame_id_build (cache->base, cache->func_addr);
}

void
libunwind_frame_prev_register (struct frame_info *next_frame, void **this_cache,
			       int regnum, int *optimizedp,
			       enum lval_type *lvalp, CORE_ADDR *addrp,
			       int *realnump, void *valuep)
{
  struct libunwind_frame_cache *cache =
    libunwind_frame_cache (next_frame, this_cache);

  void *ptr;
  unw_cursor_t *c;
  unw_save_loc_t sl;
  int i, ret;
  unw_word_t intval;
  unw_fpreg_t fpval;
  unw_regnum_t uw_regnum;
  struct libunwind_descr *descr;

  /* Convert from gdb register number to libunwind register number.  */
  descr = libunwind_descr (get_frame_arch (next_frame));
  uw_regnum = descr->gdb2uw (regnum);

  gdb_assert (regnum >= 0);

  if (!target_has_registers)
    error ("No registers.");

  *optimizedp = 0;
  *addrp = 0;
  *lvalp = not_lval;
  *realnump = -1;

  memset (valuep, 0, register_size (current_gdbarch, regnum));

  if (uw_regnum < 0)
    return;

  /* To get the previous register, we use the libunwind register APIs with
     the cursor we have already pushed back to the previous frame.  */

  if (descr->is_fpreg (uw_regnum))
    {
      ret = unw_get_fpreg_p (&cache->cursor, uw_regnum, &fpval);
      ptr = &fpval;
    }
  else
    {
      ret = unw_get_reg_p (&cache->cursor, uw_regnum, &intval);
      ptr = &intval;
    }

  if (ret < 0)
    return;

  memcpy (valuep, ptr, register_size (current_gdbarch, regnum));

  if (unw_get_saveloc_p (&cache->cursor, uw_regnum, &sl) < 0)
    return;

  switch (sl.type)
    {
    case UNW_SLT_NONE:
      *optimizedp = 1;
      break;

    case UNW_SLT_MEMORY:
      *lvalp = lval_memory;
      *addrp = sl.u.addr;
      break;

    case UNW_SLT_REG:
      *lvalp = lval_register;
      *realnump = regnum;
      break;
    }
} 

CORE_ADDR
libunwind_frame_base_address (struct frame_info *next_frame, void **this_cache)
{
  struct libunwind_frame_cache *cache =
    libunwind_frame_cache (next_frame, this_cache);

  return cache->base;
}

/* The following is a glue routine to call the libunwind unwind table
   search function to get unwind information for a specified ip address.  */ 
int
libunwind_search_unwind_table (void *as, long ip, void *di,
			       void *pi, int need_unwind_info, void *args)
{
  return unw_search_unwind_table_p (*(unw_addr_space_t *)as, (unw_word_t )ip, 
				    di, pi, need_unwind_info, args);
}

static int
libunwind_load (void)
{
  void *handle;

  handle = dlopen (LIBUNWIND_SO, RTLD_NOW);
  if (handle == NULL)
    return 0;

  /* Initialize pointers to the dynamic library functions we will use.  */

  unw_get_reg_p = dlsym (handle, get_reg_name);
  if (unw_get_reg_p == NULL)
    return 0;

  unw_get_fpreg_p = dlsym (handle, get_fpreg_name);
  if (unw_get_fpreg_p == NULL)
    return 0;

  unw_get_saveloc_p = dlsym (handle, get_saveloc_name);
  if (unw_get_saveloc_p == NULL)
    return 0;

  unw_step_p = dlsym (handle, step_name);
  if (unw_step_p == NULL)
    return 0;

  unw_init_remote_p = dlsym (handle, init_remote_name);
  if (unw_init_remote_p == NULL)
    return 0;

  unw_create_addr_space_p = dlsym (handle, create_addr_space_name);
  if (unw_create_addr_space_p == NULL)
    return 0;

  unw_search_unwind_table_p = dlsym (handle, search_unwind_table_name);
  if (unw_search_unwind_table_p == NULL)
    return 0;

  unw_find_dyn_list_p = dlsym (handle, find_dyn_list_name);
  if (unw_find_dyn_list_p == NULL)
    return 0;
   
  return 1;
}

int
libunwind_is_initialized (void)
{
  return libunwind_initialized;
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_libunwind_frame (void);

void
_initialize_libunwind_frame (void)
{
  libunwind_descr_handle = register_gdbarch_data (libunwind_descr_init);

  libunwind_initialized = libunwind_load ();
}
