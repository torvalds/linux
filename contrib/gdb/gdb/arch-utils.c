/* Dynamic architecture support for GDB, the GNU debugger.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003 Free Software Foundation,
   Inc.

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

#include "arch-utils.h"
#include "buildsym.h"
#include "gdbcmd.h"
#include "inferior.h"		/* enum CALL_DUMMY_LOCATION et.al. */
#include "gdb_string.h"
#include "regcache.h"
#include "gdb_assert.h"
#include "sim-regno.h"

#include "osabi.h"

#include "version.h"

#include "floatformat.h"

/* Implementation of extract return value that grubs around in the
   register cache.  */
void
legacy_extract_return_value (struct type *type, struct regcache *regcache,
			     void *valbuf)
{
  char *registers = deprecated_grub_regcache_for_registers (regcache);
  bfd_byte *buf = valbuf;
  DEPRECATED_EXTRACT_RETURN_VALUE (type, registers, buf); /* OK */
}

/* Implementation of store return value that grubs the register cache.
   Takes a local copy of the buffer to avoid const problems.  */
void
legacy_store_return_value (struct type *type, struct regcache *regcache,
			   const void *buf)
{
  bfd_byte *b = alloca (TYPE_LENGTH (type));
  gdb_assert (regcache == current_regcache);
  memcpy (b, buf, TYPE_LENGTH (type));
  DEPRECATED_STORE_RETURN_VALUE (type, b);
}


int
always_use_struct_convention (int gcc_p, struct type *value_type)
{
  return 1;
}


int
legacy_register_sim_regno (int regnum)
{
  /* Only makes sense to supply raw registers.  */
  gdb_assert (regnum >= 0 && regnum < NUM_REGS);
  /* NOTE: cagney/2002-05-13: The old code did it this way and it is
     suspected that some GDB/SIM combinations may rely on this
     behavour.  The default should be one2one_register_sim_regno
     (below).  */
  if (REGISTER_NAME (regnum) != NULL
      && REGISTER_NAME (regnum)[0] != '\0')
    return regnum;
  else
    return LEGACY_SIM_REGNO_IGNORE;
}

int
generic_return_value_on_stack_not (struct type *type)
{
  return 0;
}

CORE_ADDR
generic_skip_trampoline_code (CORE_ADDR pc)
{
  return 0;
}

CORE_ADDR
generic_skip_solib_resolver (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  return 0;
}

int
generic_in_solib_call_trampoline (CORE_ADDR pc, char *name)
{
  return 0;
}

int
generic_in_solib_return_trampoline (CORE_ADDR pc, char *name)
{
  return 0;
}

int
generic_in_function_epilogue_p (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  return 0;
}

#if defined (CALL_DUMMY)
LONGEST legacy_call_dummy_words[] = CALL_DUMMY;
#else
LONGEST legacy_call_dummy_words[1];
#endif
int legacy_sizeof_call_dummy_words = sizeof (legacy_call_dummy_words);

void
generic_remote_translate_xfer_address (struct gdbarch *gdbarch,
				       struct regcache *regcache,
				       CORE_ADDR gdb_addr, int gdb_len,
				       CORE_ADDR * rem_addr, int *rem_len)
{
  *rem_addr = gdb_addr;
  *rem_len = gdb_len;
}

/* Helper functions for INNER_THAN */

int
core_addr_lessthan (CORE_ADDR lhs, CORE_ADDR rhs)
{
  return (lhs < rhs);
}

int
core_addr_greaterthan (CORE_ADDR lhs, CORE_ADDR rhs)
{
  return (lhs > rhs);
}


/* Helper functions for TARGET_{FLOAT,DOUBLE}_FORMAT */

const struct floatformat *
default_float_format (struct gdbarch *gdbarch)
{
  int byte_order = gdbarch_byte_order (gdbarch);
  switch (byte_order)
    {
    case BFD_ENDIAN_BIG:
      return &floatformat_ieee_single_big;
    case BFD_ENDIAN_LITTLE:
      return &floatformat_ieee_single_little;
    default:
      internal_error (__FILE__, __LINE__,
		      "default_float_format: bad byte order");
    }
}


const struct floatformat *
default_double_format (struct gdbarch *gdbarch)
{
  int byte_order = gdbarch_byte_order (gdbarch);
  switch (byte_order)
    {
    case BFD_ENDIAN_BIG:
      return &floatformat_ieee_double_big;
    case BFD_ENDIAN_LITTLE:
      return &floatformat_ieee_double_little;
    default:
      internal_error (__FILE__, __LINE__,
		      "default_double_format: bad byte order");
    }
}

/* Misc helper functions for targets. */

CORE_ADDR
core_addr_identity (CORE_ADDR addr)
{
  return addr;
}

CORE_ADDR
convert_from_func_ptr_addr_identity (struct gdbarch *gdbarch, CORE_ADDR addr,
				     struct target_ops *targ)
{
  return addr;
}

int
no_op_reg_to_regnum (int reg)
{
  return reg;
}

CORE_ADDR
deprecated_init_frame_pc_default (int fromleaf, struct frame_info *prev)
{
  if (fromleaf && DEPRECATED_SAVED_PC_AFTER_CALL_P ())
    return DEPRECATED_SAVED_PC_AFTER_CALL (get_next_frame (prev));
  else if (get_next_frame (prev) != NULL)
    return DEPRECATED_FRAME_SAVED_PC (get_next_frame (prev));
  else
    return read_pc ();
}

void
default_elf_make_msymbol_special (asymbol *sym, struct minimal_symbol *msym)
{
  return;
}

void
default_coff_make_msymbol_special (int val, struct minimal_symbol *msym)
{
  return;
}

int
cannot_register_not (int regnum)
{
  return 0;
}

/* Legacy version of target_virtual_frame_pointer().  Assumes that
   there is an DEPRECATED_FP_REGNUM and that it is the same, cooked or
   raw.  */

void
legacy_virtual_frame_pointer (CORE_ADDR pc,
			      int *frame_regnum,
			      LONGEST *frame_offset)
{
  /* FIXME: cagney/2002-09-13: This code is used when identifying the
     frame pointer of the current PC.  It is assuming that a single
     register and an offset can determine this.  I think it should
     instead generate a byte code expression as that would work better
     with things like Dwarf2's CFI.  */
  if (DEPRECATED_FP_REGNUM >= 0 && DEPRECATED_FP_REGNUM < NUM_REGS)
    *frame_regnum = DEPRECATED_FP_REGNUM;
  else if (SP_REGNUM >= 0 && SP_REGNUM < NUM_REGS)
    *frame_regnum = SP_REGNUM;
  else
    /* Should this be an internal error?  I guess so, it is reflecting
       an architectural limitation in the current design.  */
    internal_error (__FILE__, __LINE__, "No virtual frame pointer available");
  *frame_offset = 0;
}

/* Assume the world is sane, every register's virtual and real size
   is identical.  */

int
generic_register_size (int regnum)
{
  gdb_assert (regnum >= 0 && regnum < NUM_REGS + NUM_PSEUDO_REGS);
  if (gdbarch_register_type_p (current_gdbarch))
    return TYPE_LENGTH (gdbarch_register_type (current_gdbarch, regnum));
  else
    /* FIXME: cagney/2003-03-01: Once all architectures implement
       gdbarch_register_type(), this entire function can go away.  It
       is made obsolete by register_size().  */
    return TYPE_LENGTH (DEPRECATED_REGISTER_VIRTUAL_TYPE (regnum)); /* OK */
}

/* Assume all registers are adjacent.  */

int
generic_register_byte (int regnum)
{
  int byte;
  int i;
  gdb_assert (regnum >= 0 && regnum < NUM_REGS + NUM_PSEUDO_REGS);
  byte = 0;
  for (i = 0; i < regnum; i++)
    {
      byte += generic_register_size (i);
    }
  return byte;
}


int
legacy_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
#if !defined (IN_SIGTRAMP)
  if (SIGTRAMP_START_P ())
    return (pc) >= SIGTRAMP_START (pc) && (pc) < SIGTRAMP_END (pc);
  else
    return name && strcmp ("_sigtramp", name) == 0;
#else
  return IN_SIGTRAMP (pc, name);
#endif
}

int
legacy_convert_register_p (int regnum, struct type *type)
{
  return (DEPRECATED_REGISTER_CONVERTIBLE_P ()
	  && DEPRECATED_REGISTER_CONVERTIBLE (regnum));
}

void
legacy_register_to_value (struct frame_info *frame, int regnum,
			  struct type *type, void *to)
{
  char from[MAX_REGISTER_SIZE];
  get_frame_register (frame, regnum, from);
  DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL (regnum, type, from, to);
}

void
legacy_value_to_register (struct frame_info *frame, int regnum,
			  struct type *type, const void *tmp)
{
  char to[MAX_REGISTER_SIZE];
  char *from = alloca (TYPE_LENGTH (type));
  memcpy (from, from, TYPE_LENGTH (type));
  DEPRECATED_REGISTER_CONVERT_TO_RAW (type, regnum, from, to);
  put_frame_register (frame, regnum, to);
}

int
default_stabs_argument_has_addr (struct gdbarch *gdbarch, struct type *type)
{
  if (DEPRECATED_REG_STRUCT_HAS_ADDR_P ()
      && DEPRECATED_REG_STRUCT_HAS_ADDR (processing_gcc_compilation, type))
    {
      CHECK_TYPEDEF (type);

      return (TYPE_CODE (type) == TYPE_CODE_STRUCT
	      || TYPE_CODE (type) == TYPE_CODE_UNION
	      || TYPE_CODE (type) == TYPE_CODE_SET
	      || TYPE_CODE (type) == TYPE_CODE_BITSTRING);
    }

  return 0;
}


/* Functions to manipulate the endianness of the target.  */

/* ``target_byte_order'' is only used when non- multi-arch.
   Multi-arch targets obtain the current byte order using the
   TARGET_BYTE_ORDER gdbarch method.

   The choice of initial value is entirely arbitrary.  During startup,
   the function initialize_current_architecture() updates this value
   based on default byte-order information extracted from BFD.  */
static int target_byte_order = BFD_ENDIAN_BIG;
static int target_byte_order_auto = 1;

enum bfd_endian
selected_byte_order (void)
{
  if (target_byte_order_auto)
    return BFD_ENDIAN_UNKNOWN;
  else
    return target_byte_order;
}

static const char endian_big[] = "big";
static const char endian_little[] = "little";
static const char endian_auto[] = "auto";
static const char *endian_enum[] =
{
  endian_big,
  endian_little,
  endian_auto,
  NULL,
};
static const char *set_endian_string;

/* Called by ``show endian''.  */

static void
show_endian (char *args, int from_tty)
{
  if (target_byte_order_auto)
    printf_unfiltered ("The target endianness is set automatically (currently %s endian)\n",
		       (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "big" : "little"));
  else
    printf_unfiltered ("The target is assumed to be %s endian\n",
		       (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "big" : "little"));
}

static void
set_endian (char *ignore_args, int from_tty, struct cmd_list_element *c)
{
  if (set_endian_string == endian_auto)
    {
      target_byte_order_auto = 1;
    }
  else if (set_endian_string == endian_little)
    {
      struct gdbarch_info info;
      target_byte_order_auto = 0;
      gdbarch_info_init (&info);
      info.byte_order = BFD_ENDIAN_LITTLE;
      if (! gdbarch_update_p (info))
	printf_unfiltered ("Little endian target not supported by GDB\n");
    }
  else if (set_endian_string == endian_big)
    {
      struct gdbarch_info info;
      target_byte_order_auto = 0;
      gdbarch_info_init (&info);
      info.byte_order = BFD_ENDIAN_BIG;
      if (! gdbarch_update_p (info))
	printf_unfiltered ("Big endian target not supported by GDB\n");
    }
  else
    internal_error (__FILE__, __LINE__,
		    "set_endian: bad value");
  show_endian (NULL, from_tty);
}

/* Functions to manipulate the architecture of the target */

enum set_arch { set_arch_auto, set_arch_manual };

static int target_architecture_auto = 1;

static const char *set_architecture_string;

const char *
selected_architecture_name (void)
{
  if (target_architecture_auto)
    return NULL;
  else
    return set_architecture_string;
}

/* Called if the user enters ``show architecture'' without an
   argument. */

static void
show_architecture (char *args, int from_tty)
{
  const char *arch;
  arch = TARGET_ARCHITECTURE->printable_name;
  if (target_architecture_auto)
    printf_filtered ("The target architecture is set automatically (currently %s)\n", arch);
  else
    printf_filtered ("The target architecture is assumed to be %s\n", arch);
}


/* Called if the user enters ``set architecture'' with or without an
   argument. */

static void
set_architecture (char *ignore_args, int from_tty, struct cmd_list_element *c)
{
  if (strcmp (set_architecture_string, "auto") == 0)
    {
      target_architecture_auto = 1;
    }
  else
    {
      struct gdbarch_info info;
      gdbarch_info_init (&info);
      info.bfd_arch_info = bfd_scan_arch (set_architecture_string);
      if (info.bfd_arch_info == NULL)
	internal_error (__FILE__, __LINE__,
			"set_architecture: bfd_scan_arch failed");
      if (gdbarch_update_p (info))
	target_architecture_auto = 0;
      else
	printf_unfiltered ("Architecture `%s' not recognized.\n",
			   set_architecture_string);
    }
  show_architecture (NULL, from_tty);
}

/* Try to select a global architecture that matches "info".  Return
   non-zero if the attempt succeds.  */
int
gdbarch_update_p (struct gdbarch_info info)
{
  struct gdbarch *new_gdbarch = gdbarch_find_by_info (info);

  /* If there no architecture by that name, reject the request.  */
  if (new_gdbarch == NULL)
    {
      if (gdbarch_debug)
	fprintf_unfiltered (gdb_stdlog, "gdbarch_update_p: "
			    "Architecture not found\n");
      return 0;
    }

  /* If it is the same old architecture, accept the request (but don't
     swap anything).  */
  if (new_gdbarch == current_gdbarch)
    {
      if (gdbarch_debug)
	fprintf_unfiltered (gdb_stdlog, "gdbarch_update_p: "
			    "Architecture 0x%08lx (%s) unchanged\n",
			    (long) new_gdbarch,
			    gdbarch_bfd_arch_info (new_gdbarch)->printable_name);
      return 1;
    }

  /* It's a new architecture, swap it in.  */
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_update_p: "
			"New architecture 0x%08lx (%s) selected\n",
			(long) new_gdbarch,
			gdbarch_bfd_arch_info (new_gdbarch)->printable_name);
  deprecated_current_gdbarch_select_hack (new_gdbarch);

  return 1;
}

/* Return the architecture for ABFD.  If no suitable architecture
   could be find, return NULL.  */

struct gdbarch *
gdbarch_from_bfd (bfd *abfd)
{
  struct gdbarch *old_gdbarch = current_gdbarch;
  struct gdbarch *new_gdbarch;
  struct gdbarch_info info;

  gdbarch_info_init (&info);
  info.abfd = abfd;
  return gdbarch_find_by_info (info);
}

/* Set the dynamic target-system-dependent parameters (architecture,
   byte-order) using information found in the BFD */

void
set_gdbarch_from_file (bfd *abfd)
{
  struct gdbarch *gdbarch;

  gdbarch = gdbarch_from_bfd (abfd);
  if (gdbarch == NULL)
    error ("Architecture of file not recognized.\n");
  deprecated_current_gdbarch_select_hack (gdbarch);
}

/* Initialize the current architecture.  Update the ``set
   architecture'' command so that it specifies a list of valid
   architectures.  */

#ifdef DEFAULT_BFD_ARCH
extern const bfd_arch_info_type DEFAULT_BFD_ARCH;
static const bfd_arch_info_type *default_bfd_arch = &DEFAULT_BFD_ARCH;
#else
static const bfd_arch_info_type *default_bfd_arch;
#endif

#ifdef DEFAULT_BFD_VEC
extern const bfd_target DEFAULT_BFD_VEC;
static const bfd_target *default_bfd_vec = &DEFAULT_BFD_VEC;
#else
static const bfd_target *default_bfd_vec;
#endif

void
initialize_current_architecture (void)
{
  const char **arches = gdbarch_printable_names ();

  /* determine a default architecture and byte order. */
  struct gdbarch_info info;
  gdbarch_info_init (&info);
  
  /* Find a default architecture. */
  if (info.bfd_arch_info == NULL
      && default_bfd_arch != NULL)
    info.bfd_arch_info = default_bfd_arch;
  if (info.bfd_arch_info == NULL)
    {
      /* Choose the architecture by taking the first one
	 alphabetically. */
      const char *chosen = arches[0];
      const char **arch;
      for (arch = arches; *arch != NULL; arch++)
	{
	  if (strcmp (*arch, chosen) < 0)
	    chosen = *arch;
	}
      if (chosen == NULL)
	internal_error (__FILE__, __LINE__,
			"initialize_current_architecture: No arch");
      info.bfd_arch_info = bfd_scan_arch (chosen);
      if (info.bfd_arch_info == NULL)
	internal_error (__FILE__, __LINE__,
			"initialize_current_architecture: Arch not found");
    }

  /* Take several guesses at a byte order.  */
  if (info.byte_order == BFD_ENDIAN_UNKNOWN
      && default_bfd_vec != NULL)
    {
      /* Extract BFD's default vector's byte order. */
      switch (default_bfd_vec->byteorder)
	{
	case BFD_ENDIAN_BIG:
	  info.byte_order = BFD_ENDIAN_BIG;
	  break;
	case BFD_ENDIAN_LITTLE:
	  info.byte_order = BFD_ENDIAN_LITTLE;
	  break;
	default:
	  break;
	}
    }
  if (info.byte_order == BFD_ENDIAN_UNKNOWN)
    {
      /* look for ``*el-*'' in the target name. */
      const char *chp;
      chp = strchr (target_name, '-');
      if (chp != NULL
	  && chp - 2 >= target_name
	  && strncmp (chp - 2, "el", 2) == 0)
	info.byte_order = BFD_ENDIAN_LITTLE;
    }
  if (info.byte_order == BFD_ENDIAN_UNKNOWN)
    {
      /* Wire it to big-endian!!! */
      info.byte_order = BFD_ENDIAN_BIG;
    }

  if (! gdbarch_update_p (info))
    internal_error (__FILE__, __LINE__,
		    "initialize_current_architecture: Selection of initial architecture failed");

  /* Create the ``set architecture'' command appending ``auto'' to the
     list of architectures. */
  {
    struct cmd_list_element *c;
    /* Append ``auto''. */
    int nr;
    for (nr = 0; arches[nr] != NULL; nr++);
    arches = xrealloc (arches, sizeof (char*) * (nr + 2));
    arches[nr + 0] = "auto";
    arches[nr + 1] = NULL;
    /* FIXME: add_set_enum_cmd() uses an array of ``char *'' instead
       of ``const char *''.  We just happen to know that the casts are
       safe. */
    c = add_set_enum_cmd ("architecture", class_support,
			  arches, &set_architecture_string,
			  "Set architecture of target.",
			  &setlist);
    set_cmd_sfunc (c, set_architecture);
    add_alias_cmd ("processor", "architecture", class_support, 1, &setlist);
    /* Don't use set_from_show - need to print both auto/manual and
       current setting. */
    add_cmd ("architecture", class_support, show_architecture,
	     "Show the current target architecture", &showlist);
  }
}


/* Initialize a gdbarch info to values that will be automatically
   overridden.  Note: Originally, this ``struct info'' was initialized
   using memset(0).  Unfortunately, that ran into problems, namely
   BFD_ENDIAN_BIG is zero.  An explicit initialization function that
   can explicitly set each field to a well defined value is used.  */

void
gdbarch_info_init (struct gdbarch_info *info)
{
  memset (info, 0, sizeof (struct gdbarch_info));
  info->byte_order = BFD_ENDIAN_UNKNOWN;
  info->osabi = GDB_OSABI_UNINITIALIZED;
}

/* Similar to init, but this time fill in the blanks.  Information is
   obtained from the specified architecture, global "set ..." options,
   and explicitly initialized INFO fields.  */

void
gdbarch_info_fill (struct gdbarch *gdbarch, struct gdbarch_info *info)
{
  /* "(gdb) set architecture ...".  */
  if (info->bfd_arch_info == NULL
      && !target_architecture_auto
      && gdbarch != NULL)
    info->bfd_arch_info = gdbarch_bfd_arch_info (gdbarch);
  if (info->bfd_arch_info == NULL
      && info->abfd != NULL
      && bfd_get_arch (info->abfd) != bfd_arch_unknown
      && bfd_get_arch (info->abfd) != bfd_arch_obscure)
    info->bfd_arch_info = bfd_get_arch_info (info->abfd);
  if (info->bfd_arch_info == NULL
      && gdbarch != NULL)
    info->bfd_arch_info = gdbarch_bfd_arch_info (gdbarch);

  /* "(gdb) set byte-order ...".  */
  if (info->byte_order == BFD_ENDIAN_UNKNOWN
      && !target_byte_order_auto
      && gdbarch != NULL)
    info->byte_order = gdbarch_byte_order (gdbarch);
  /* From the INFO struct.  */
  if (info->byte_order == BFD_ENDIAN_UNKNOWN
      && info->abfd != NULL)
    info->byte_order = (bfd_big_endian (info->abfd) ? BFD_ENDIAN_BIG
		       : bfd_little_endian (info->abfd) ? BFD_ENDIAN_LITTLE
		       : BFD_ENDIAN_UNKNOWN);
  /* From the current target.  */
  if (info->byte_order == BFD_ENDIAN_UNKNOWN
      && gdbarch != NULL)
    info->byte_order = gdbarch_byte_order (gdbarch);

  /* "(gdb) set osabi ...".  Handled by gdbarch_lookup_osabi.  */
  if (info->osabi == GDB_OSABI_UNINITIALIZED)
    info->osabi = gdbarch_lookup_osabi (info->abfd);
  if (info->osabi == GDB_OSABI_UNINITIALIZED
      && gdbarch != NULL)
    info->osabi = gdbarch_osabi (gdbarch);

  /* Must have at least filled in the architecture.  */
  gdb_assert (info->bfd_arch_info != NULL);
}

/* */

extern initialize_file_ftype _initialize_gdbarch_utils; /* -Wmissing-prototypes */

void
_initialize_gdbarch_utils (void)
{
  struct cmd_list_element *c;
  c = add_set_enum_cmd ("endian", class_support,
			endian_enum, &set_endian_string,
			"Set endianness of target.",
			&setlist);
  set_cmd_sfunc (c, set_endian);
  /* Don't use set_from_show - need to print both auto/manual and
     current setting. */
  add_cmd ("endian", class_support, show_endian,
	   "Show the current byte-order", &showlist);
}
