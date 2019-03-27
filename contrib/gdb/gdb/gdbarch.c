/* *INDENT-OFF* */ /* THIS FILE IS GENERATED */

/* Dynamic architecture support for GDB, the GNU debugger.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free
   Software Foundation, Inc.

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

/* This file was created with the aid of ``gdbarch.sh''.

   The Bourne shell script ``gdbarch.sh'' creates the files
   ``new-gdbarch.c'' and ``new-gdbarch.h and then compares them
   against the existing ``gdbarch.[hc]''.  Any differences found
   being reported.

   If editing this file, please also run gdbarch.sh and merge any
   changes into that script. Conversely, when making sweeping changes
   to this file, modifying gdbarch.sh and using its output may prove
   easier. */


#include "defs.h"
#include "arch-utils.h"

#include "gdbcmd.h"
#include "inferior.h" /* enum CALL_DUMMY_LOCATION et.al. */
#include "symcat.h"

#include "floatformat.h"

#include "gdb_assert.h"
#include "gdb_string.h"
#include "gdb-events.h"
#include "reggroups.h"
#include "osabi.h"
#include "gdb_obstack.h"

/* Static function declarations */

static void alloc_gdbarch_data (struct gdbarch *);

/* Non-zero if we want to trace architecture code.  */

#ifndef GDBARCH_DEBUG
#define GDBARCH_DEBUG 0
#endif
int gdbarch_debug = GDBARCH_DEBUG;


/* Maintain the struct gdbarch object */

struct gdbarch
{
  /* Has this architecture been fully initialized?  */
  int initialized_p;

  /* An obstack bound to the lifetime of the architecture.  */
  struct obstack *obstack;

  /* basic architectural information */
  const struct bfd_arch_info * bfd_arch_info;
  int byte_order;
  enum gdb_osabi osabi;

  /* target specific vector. */
  struct gdbarch_tdep *tdep;
  gdbarch_dump_tdep_ftype *dump_tdep;

  /* per-architecture data-pointers */
  unsigned nr_data;
  void **data;

  /* per-architecture swap-regions */
  struct gdbarch_swap *swap;

  /* Multi-arch values.

     When extending this structure you must:

     Add the field below.

     Declare set/get functions and define the corresponding
     macro in gdbarch.h.

     gdbarch_alloc(): If zero/NULL is not a suitable default,
     initialize the new field.

     verify_gdbarch(): Confirm that the target updated the field
     correctly.

     gdbarch_dump(): Add a fprintf_unfiltered call so that the new
     field is dumped out

     ``startup_gdbarch()'': Append an initial value to the static
     variable (base values on the host's c-type system).

     get_gdbarch(): Implement the set/get functions (probably using
     the macro's as shortcuts).

     */

  int short_bit;
  int int_bit;
  int long_bit;
  int long_long_bit;
  int float_bit;
  int double_bit;
  int long_double_bit;
  int ptr_bit;
  int addr_bit;
  int bfd_vma_bit;
  int char_signed;
  gdbarch_read_pc_ftype *read_pc;
  gdbarch_write_pc_ftype *write_pc;
  gdbarch_read_sp_ftype *read_sp;
  gdbarch_virtual_frame_pointer_ftype *virtual_frame_pointer;
  gdbarch_pseudo_register_read_ftype *pseudo_register_read;
  gdbarch_pseudo_register_write_ftype *pseudo_register_write;
  int num_regs;
  int num_pseudo_regs;
  int sp_regnum;
  int pc_regnum;
  int ps_regnum;
  int fp0_regnum;
  gdbarch_stab_reg_to_regnum_ftype *stab_reg_to_regnum;
  gdbarch_ecoff_reg_to_regnum_ftype *ecoff_reg_to_regnum;
  gdbarch_dwarf_reg_to_regnum_ftype *dwarf_reg_to_regnum;
  gdbarch_sdb_reg_to_regnum_ftype *sdb_reg_to_regnum;
  gdbarch_dwarf2_reg_to_regnum_ftype *dwarf2_reg_to_regnum;
  gdbarch_register_name_ftype *register_name;
  gdbarch_register_type_ftype *register_type;
  gdbarch_deprecated_register_virtual_type_ftype *deprecated_register_virtual_type;
  int deprecated_register_bytes;
  gdbarch_deprecated_register_byte_ftype *deprecated_register_byte;
  gdbarch_deprecated_register_raw_size_ftype *deprecated_register_raw_size;
  gdbarch_deprecated_register_virtual_size_ftype *deprecated_register_virtual_size;
  int deprecated_max_register_raw_size;
  int deprecated_max_register_virtual_size;
  gdbarch_unwind_dummy_id_ftype *unwind_dummy_id;
  gdbarch_deprecated_save_dummy_frame_tos_ftype *deprecated_save_dummy_frame_tos;
  int deprecated_fp_regnum;
  gdbarch_deprecated_target_read_fp_ftype *deprecated_target_read_fp;
  gdbarch_push_dummy_call_ftype *push_dummy_call;
  gdbarch_deprecated_push_arguments_ftype *deprecated_push_arguments;
  int deprecated_use_generic_dummy_frames;
  gdbarch_deprecated_push_return_address_ftype *deprecated_push_return_address;
  gdbarch_deprecated_dummy_write_sp_ftype *deprecated_dummy_write_sp;
  int deprecated_register_size;
  int call_dummy_location;
  CORE_ADDR deprecated_call_dummy_start_offset;
  CORE_ADDR deprecated_call_dummy_breakpoint_offset;
  int deprecated_call_dummy_length;
  LONGEST * deprecated_call_dummy_words;
  int deprecated_sizeof_call_dummy_words;
  gdbarch_deprecated_fix_call_dummy_ftype *deprecated_fix_call_dummy;
  gdbarch_push_dummy_code_ftype *push_dummy_code;
  gdbarch_deprecated_push_dummy_frame_ftype *deprecated_push_dummy_frame;
  gdbarch_deprecated_do_registers_info_ftype *deprecated_do_registers_info;
  gdbarch_print_registers_info_ftype *print_registers_info;
  gdbarch_print_float_info_ftype *print_float_info;
  gdbarch_print_vector_info_ftype *print_vector_info;
  gdbarch_register_sim_regno_ftype *register_sim_regno;
  gdbarch_register_bytes_ok_ftype *register_bytes_ok;
  gdbarch_cannot_fetch_register_ftype *cannot_fetch_register;
  gdbarch_cannot_store_register_ftype *cannot_store_register;
  gdbarch_get_longjmp_target_ftype *get_longjmp_target;
  gdbarch_deprecated_pc_in_call_dummy_ftype *deprecated_pc_in_call_dummy;
  gdbarch_deprecated_init_frame_pc_first_ftype *deprecated_init_frame_pc_first;
  gdbarch_deprecated_init_frame_pc_ftype *deprecated_init_frame_pc;
  int believe_pcc_promotion;
  int believe_pcc_promotion_type;
  gdbarch_deprecated_get_saved_register_ftype *deprecated_get_saved_register;
  gdbarch_deprecated_register_convertible_ftype *deprecated_register_convertible;
  gdbarch_deprecated_register_convert_to_virtual_ftype *deprecated_register_convert_to_virtual;
  gdbarch_deprecated_register_convert_to_raw_ftype *deprecated_register_convert_to_raw;
  gdbarch_convert_register_p_ftype *convert_register_p;
  gdbarch_register_to_value_ftype *register_to_value;
  gdbarch_value_to_register_ftype *value_to_register;
  gdbarch_pointer_to_address_ftype *pointer_to_address;
  gdbarch_address_to_pointer_ftype *address_to_pointer;
  gdbarch_integer_to_address_ftype *integer_to_address;
  gdbarch_deprecated_pop_frame_ftype *deprecated_pop_frame;
  gdbarch_deprecated_store_struct_return_ftype *deprecated_store_struct_return;
  gdbarch_return_value_ftype *return_value;
  gdbarch_return_value_on_stack_ftype *return_value_on_stack;
  gdbarch_extract_return_value_ftype *extract_return_value;
  gdbarch_store_return_value_ftype *store_return_value;
  gdbarch_deprecated_extract_return_value_ftype *deprecated_extract_return_value;
  gdbarch_deprecated_store_return_value_ftype *deprecated_store_return_value;
  gdbarch_use_struct_convention_ftype *use_struct_convention;
  gdbarch_deprecated_extract_struct_value_address_ftype *deprecated_extract_struct_value_address;
  gdbarch_deprecated_frame_init_saved_regs_ftype *deprecated_frame_init_saved_regs;
  gdbarch_deprecated_init_extra_frame_info_ftype *deprecated_init_extra_frame_info;
  gdbarch_skip_prologue_ftype *skip_prologue;
  gdbarch_inner_than_ftype *inner_than;
  gdbarch_breakpoint_from_pc_ftype *breakpoint_from_pc;
  gdbarch_adjust_breakpoint_address_ftype *adjust_breakpoint_address;
  gdbarch_memory_insert_breakpoint_ftype *memory_insert_breakpoint;
  gdbarch_memory_remove_breakpoint_ftype *memory_remove_breakpoint;
  CORE_ADDR decr_pc_after_break;
  CORE_ADDR function_start_offset;
  gdbarch_remote_translate_xfer_address_ftype *remote_translate_xfer_address;
  CORE_ADDR frame_args_skip;
  gdbarch_deprecated_frameless_function_invocation_ftype *deprecated_frameless_function_invocation;
  gdbarch_deprecated_frame_chain_ftype *deprecated_frame_chain;
  gdbarch_deprecated_frame_chain_valid_ftype *deprecated_frame_chain_valid;
  gdbarch_deprecated_frame_saved_pc_ftype *deprecated_frame_saved_pc;
  gdbarch_unwind_pc_ftype *unwind_pc;
  gdbarch_unwind_sp_ftype *unwind_sp;
  gdbarch_deprecated_frame_args_address_ftype *deprecated_frame_args_address;
  gdbarch_deprecated_frame_locals_address_ftype *deprecated_frame_locals_address;
  gdbarch_deprecated_saved_pc_after_call_ftype *deprecated_saved_pc_after_call;
  gdbarch_frame_num_args_ftype *frame_num_args;
  gdbarch_deprecated_stack_align_ftype *deprecated_stack_align;
  gdbarch_frame_align_ftype *frame_align;
  gdbarch_deprecated_reg_struct_has_addr_ftype *deprecated_reg_struct_has_addr;
  gdbarch_stabs_argument_has_addr_ftype *stabs_argument_has_addr;
  int frame_red_zone_size;
  int parm_boundary;
  const struct floatformat * float_format;
  const struct floatformat * double_format;
  const struct floatformat * long_double_format;
  gdbarch_convert_from_func_ptr_addr_ftype *convert_from_func_ptr_addr;
  gdbarch_addr_bits_remove_ftype *addr_bits_remove;
  gdbarch_smash_text_address_ftype *smash_text_address;
  gdbarch_software_single_step_ftype *software_single_step;
  gdbarch_print_insn_ftype *print_insn;
  gdbarch_skip_trampoline_code_ftype *skip_trampoline_code;
  gdbarch_skip_solib_resolver_ftype *skip_solib_resolver;
  gdbarch_in_solib_call_trampoline_ftype *in_solib_call_trampoline;
  gdbarch_in_solib_return_trampoline_ftype *in_solib_return_trampoline;
  gdbarch_pc_in_sigtramp_ftype *pc_in_sigtramp;
  gdbarch_sigtramp_start_ftype *sigtramp_start;
  gdbarch_sigtramp_end_ftype *sigtramp_end;
  gdbarch_in_function_epilogue_p_ftype *in_function_epilogue_p;
  gdbarch_construct_inferior_arguments_ftype *construct_inferior_arguments;
  gdbarch_elf_make_msymbol_special_ftype *elf_make_msymbol_special;
  gdbarch_coff_make_msymbol_special_ftype *coff_make_msymbol_special;
  const char * name_of_malloc;
  int cannot_step_breakpoint;
  int have_nonsteppable_watchpoint;
  gdbarch_address_class_type_flags_ftype *address_class_type_flags;
  gdbarch_address_class_type_flags_to_name_ftype *address_class_type_flags_to_name;
  gdbarch_address_class_name_to_type_flags_ftype *address_class_name_to_type_flags;
  gdbarch_register_reggroup_p_ftype *register_reggroup_p;
  gdbarch_fetch_pointer_argument_ftype *fetch_pointer_argument;
  gdbarch_regset_from_core_section_ftype *regset_from_core_section;
};


/* The default architecture uses host values (for want of a better
   choice). */

extern const struct bfd_arch_info bfd_default_arch_struct;

struct gdbarch startup_gdbarch =
{
  1, /* Always initialized.  */
  NULL, /* The obstack.  */
  /* basic architecture information */
  &bfd_default_arch_struct,  /* bfd_arch_info */
  BFD_ENDIAN_BIG,  /* byte_order */
  GDB_OSABI_UNKNOWN,  /* osabi */
  /* target specific vector and its dump routine */
  NULL, NULL,
  /*per-architecture data-pointers and swap regions */
  0, NULL, NULL,
  /* Multi-arch values */
  8 * sizeof (short),  /* short_bit */
  8 * sizeof (int),  /* int_bit */
  8 * sizeof (long),  /* long_bit */
  8 * sizeof (LONGEST),  /* long_long_bit */
  8 * sizeof (float),  /* float_bit */
  8 * sizeof (double),  /* double_bit */
  8 * sizeof (long double),  /* long_double_bit */
  8 * sizeof (void*),  /* ptr_bit */
  8 * sizeof (void*),  /* addr_bit */
  8 * sizeof (void*),  /* bfd_vma_bit */
  1,  /* char_signed */
  0,  /* read_pc */
  0,  /* write_pc */
  0,  /* read_sp */
  0,  /* virtual_frame_pointer */
  0,  /* pseudo_register_read */
  0,  /* pseudo_register_write */
  0,  /* num_regs */
  0,  /* num_pseudo_regs */
  -1,  /* sp_regnum */
  -1,  /* pc_regnum */
  -1,  /* ps_regnum */
  0,  /* fp0_regnum */
  0,  /* stab_reg_to_regnum */
  0,  /* ecoff_reg_to_regnum */
  0,  /* dwarf_reg_to_regnum */
  0,  /* sdb_reg_to_regnum */
  0,  /* dwarf2_reg_to_regnum */
  0,  /* register_name */
  0,  /* register_type */
  0,  /* deprecated_register_virtual_type */
  0,  /* deprecated_register_bytes */
  generic_register_byte,  /* deprecated_register_byte */
  generic_register_size,  /* deprecated_register_raw_size */
  generic_register_size,  /* deprecated_register_virtual_size */
  0,  /* deprecated_max_register_raw_size */
  0,  /* deprecated_max_register_virtual_size */
  0,  /* unwind_dummy_id */
  0,  /* deprecated_save_dummy_frame_tos */
  -1,  /* deprecated_fp_regnum */
  0,  /* deprecated_target_read_fp */
  0,  /* push_dummy_call */
  0,  /* deprecated_push_arguments */
  0,  /* deprecated_use_generic_dummy_frames */
  0,  /* deprecated_push_return_address */
  0,  /* deprecated_dummy_write_sp */
  0,  /* deprecated_register_size */
  0,  /* call_dummy_location */
  0,  /* deprecated_call_dummy_start_offset */
  0,  /* deprecated_call_dummy_breakpoint_offset */
  0,  /* deprecated_call_dummy_length */
  0,  /* deprecated_call_dummy_words */
  0,  /* deprecated_sizeof_call_dummy_words */
  0,  /* deprecated_fix_call_dummy */
  0,  /* push_dummy_code */
  0,  /* deprecated_push_dummy_frame */
  0,  /* deprecated_do_registers_info */
  default_print_registers_info,  /* print_registers_info */
  0,  /* print_float_info */
  0,  /* print_vector_info */
  0,  /* register_sim_regno */
  0,  /* register_bytes_ok */
  0,  /* cannot_fetch_register */
  0,  /* cannot_store_register */
  0,  /* get_longjmp_target */
  generic_pc_in_call_dummy,  /* deprecated_pc_in_call_dummy */
  0,  /* deprecated_init_frame_pc_first */
  0,  /* deprecated_init_frame_pc */
  0,  /* believe_pcc_promotion */
  0,  /* believe_pcc_promotion_type */
  0,  /* deprecated_get_saved_register */
  0,  /* deprecated_register_convertible */
  0,  /* deprecated_register_convert_to_virtual */
  0,  /* deprecated_register_convert_to_raw */
  0,  /* convert_register_p */
  0,  /* register_to_value */
  0,  /* value_to_register */
  0,  /* pointer_to_address */
  0,  /* address_to_pointer */
  0,  /* integer_to_address */
  0,  /* deprecated_pop_frame */
  0,  /* deprecated_store_struct_return */
  0,  /* return_value */
  0,  /* return_value_on_stack */
  0,  /* extract_return_value */
  0,  /* store_return_value */
  0,  /* deprecated_extract_return_value */
  0,  /* deprecated_store_return_value */
  0,  /* use_struct_convention */
  0,  /* deprecated_extract_struct_value_address */
  0,  /* deprecated_frame_init_saved_regs */
  0,  /* deprecated_init_extra_frame_info */
  0,  /* skip_prologue */
  0,  /* inner_than */
  0,  /* breakpoint_from_pc */
  0,  /* adjust_breakpoint_address */
  0,  /* memory_insert_breakpoint */
  0,  /* memory_remove_breakpoint */
  0,  /* decr_pc_after_break */
  0,  /* function_start_offset */
  generic_remote_translate_xfer_address,  /* remote_translate_xfer_address */
  0,  /* frame_args_skip */
  0,  /* deprecated_frameless_function_invocation */
  0,  /* deprecated_frame_chain */
  0,  /* deprecated_frame_chain_valid */
  0,  /* deprecated_frame_saved_pc */
  0,  /* unwind_pc */
  0,  /* unwind_sp */
  get_frame_base,  /* deprecated_frame_args_address */
  get_frame_base,  /* deprecated_frame_locals_address */
  0,  /* deprecated_saved_pc_after_call */
  0,  /* frame_num_args */
  0,  /* deprecated_stack_align */
  0,  /* frame_align */
  0,  /* deprecated_reg_struct_has_addr */
  default_stabs_argument_has_addr,  /* stabs_argument_has_addr */
  0,  /* frame_red_zone_size */
  0,  /* parm_boundary */
  0,  /* float_format */
  0,  /* double_format */
  0,  /* long_double_format */
  convert_from_func_ptr_addr_identity,  /* convert_from_func_ptr_addr */
  0,  /* addr_bits_remove */
  0,  /* smash_text_address */
  0,  /* software_single_step */
  0,  /* print_insn */
  0,  /* skip_trampoline_code */
  generic_skip_solib_resolver,  /* skip_solib_resolver */
  0,  /* in_solib_call_trampoline */
  0,  /* in_solib_return_trampoline */
  0,  /* pc_in_sigtramp */
  0,  /* sigtramp_start */
  0,  /* sigtramp_end */
  generic_in_function_epilogue_p,  /* in_function_epilogue_p */
  construct_inferior_arguments,  /* construct_inferior_arguments */
  0,  /* elf_make_msymbol_special */
  0,  /* coff_make_msymbol_special */
  "malloc",  /* name_of_malloc */
  0,  /* cannot_step_breakpoint */
  0,  /* have_nonsteppable_watchpoint */
  0,  /* address_class_type_flags */
  0,  /* address_class_type_flags_to_name */
  0,  /* address_class_name_to_type_flags */
  default_register_reggroup_p,  /* register_reggroup_p */
  0,  /* fetch_pointer_argument */
  0,  /* regset_from_core_section */
  /* startup_gdbarch() */
};

struct gdbarch *current_gdbarch = &startup_gdbarch;

/* Create a new ``struct gdbarch'' based on information provided by
   ``struct gdbarch_info''. */

struct gdbarch *
gdbarch_alloc (const struct gdbarch_info *info,
               struct gdbarch_tdep *tdep)
{
  /* NOTE: The new architecture variable is named ``current_gdbarch''
     so that macros such as TARGET_DOUBLE_BIT, when expanded, refer to
     the current local architecture and not the previous global
     architecture.  This ensures that the new architectures initial
     values are not influenced by the previous architecture.  Once
     everything is parameterised with gdbarch, this will go away.  */
  struct gdbarch *current_gdbarch;

  /* Create an obstack for allocating all the per-architecture memory,
     then use that to allocate the architecture vector.  */
  struct obstack *obstack = XMALLOC (struct obstack);
  obstack_init (obstack);
  current_gdbarch = obstack_alloc (obstack, sizeof (*current_gdbarch));
  memset (current_gdbarch, 0, sizeof (*current_gdbarch));
  current_gdbarch->obstack = obstack;

  alloc_gdbarch_data (current_gdbarch);

  current_gdbarch->tdep = tdep;

  current_gdbarch->bfd_arch_info = info->bfd_arch_info;
  current_gdbarch->byte_order = info->byte_order;
  current_gdbarch->osabi = info->osabi;

  /* Force the explicit initialization of these. */
  current_gdbarch->short_bit = 2*TARGET_CHAR_BIT;
  current_gdbarch->int_bit = 4*TARGET_CHAR_BIT;
  current_gdbarch->long_bit = 4*TARGET_CHAR_BIT;
  current_gdbarch->long_long_bit = 2*TARGET_LONG_BIT;
  current_gdbarch->float_bit = 4*TARGET_CHAR_BIT;
  current_gdbarch->double_bit = 8*TARGET_CHAR_BIT;
  current_gdbarch->long_double_bit = 8*TARGET_CHAR_BIT;
  current_gdbarch->ptr_bit = TARGET_INT_BIT;
  current_gdbarch->bfd_vma_bit = TARGET_ARCHITECTURE->bits_per_address;
  current_gdbarch->char_signed = -1;
  current_gdbarch->write_pc = generic_target_write_pc;
  current_gdbarch->virtual_frame_pointer = legacy_virtual_frame_pointer;
  current_gdbarch->num_regs = -1;
  current_gdbarch->sp_regnum = -1;
  current_gdbarch->pc_regnum = -1;
  current_gdbarch->ps_regnum = -1;
  current_gdbarch->fp0_regnum = -1;
  current_gdbarch->stab_reg_to_regnum = no_op_reg_to_regnum;
  current_gdbarch->ecoff_reg_to_regnum = no_op_reg_to_regnum;
  current_gdbarch->dwarf_reg_to_regnum = no_op_reg_to_regnum;
  current_gdbarch->sdb_reg_to_regnum = no_op_reg_to_regnum;
  current_gdbarch->dwarf2_reg_to_regnum = no_op_reg_to_regnum;
  current_gdbarch->deprecated_register_byte = generic_register_byte;
  current_gdbarch->deprecated_register_raw_size = generic_register_size;
  current_gdbarch->deprecated_register_virtual_size = generic_register_size;
  current_gdbarch->deprecated_fp_regnum = -1;
  current_gdbarch->deprecated_use_generic_dummy_frames = 1;
  current_gdbarch->call_dummy_location = AT_ENTRY_POINT;
  current_gdbarch->deprecated_call_dummy_words = legacy_call_dummy_words;
  current_gdbarch->deprecated_sizeof_call_dummy_words = legacy_sizeof_call_dummy_words;
  current_gdbarch->print_registers_info = default_print_registers_info;
  current_gdbarch->register_sim_regno = legacy_register_sim_regno;
  current_gdbarch->cannot_fetch_register = cannot_register_not;
  current_gdbarch->cannot_store_register = cannot_register_not;
  current_gdbarch->deprecated_pc_in_call_dummy = generic_pc_in_call_dummy;
  current_gdbarch->convert_register_p = legacy_convert_register_p;
  current_gdbarch->register_to_value = legacy_register_to_value;
  current_gdbarch->value_to_register = legacy_value_to_register;
  current_gdbarch->pointer_to_address = unsigned_pointer_to_address;
  current_gdbarch->address_to_pointer = unsigned_address_to_pointer;
  current_gdbarch->return_value_on_stack = generic_return_value_on_stack_not;
  current_gdbarch->extract_return_value = legacy_extract_return_value;
  current_gdbarch->store_return_value = legacy_store_return_value;
  current_gdbarch->use_struct_convention = generic_use_struct_convention;
  current_gdbarch->memory_insert_breakpoint = default_memory_insert_breakpoint;
  current_gdbarch->memory_remove_breakpoint = default_memory_remove_breakpoint;
  current_gdbarch->remote_translate_xfer_address = generic_remote_translate_xfer_address;
  current_gdbarch->deprecated_frame_args_address = get_frame_base;
  current_gdbarch->deprecated_frame_locals_address = get_frame_base;
  current_gdbarch->stabs_argument_has_addr = default_stabs_argument_has_addr;
  current_gdbarch->convert_from_func_ptr_addr = convert_from_func_ptr_addr_identity;
  current_gdbarch->addr_bits_remove = core_addr_identity;
  current_gdbarch->smash_text_address = core_addr_identity;
  current_gdbarch->skip_trampoline_code = generic_skip_trampoline_code;
  current_gdbarch->skip_solib_resolver = generic_skip_solib_resolver;
  current_gdbarch->in_solib_call_trampoline = generic_in_solib_call_trampoline;
  current_gdbarch->in_solib_return_trampoline = generic_in_solib_return_trampoline;
  current_gdbarch->pc_in_sigtramp = legacy_pc_in_sigtramp;
  current_gdbarch->in_function_epilogue_p = generic_in_function_epilogue_p;
  current_gdbarch->construct_inferior_arguments = construct_inferior_arguments;
  current_gdbarch->elf_make_msymbol_special = default_elf_make_msymbol_special;
  current_gdbarch->coff_make_msymbol_special = default_coff_make_msymbol_special;
  current_gdbarch->name_of_malloc = "malloc";
  current_gdbarch->register_reggroup_p = default_register_reggroup_p;
  /* gdbarch_alloc() */

  return current_gdbarch;
}


/* Allocate extra space using the per-architecture obstack.  */

void *
gdbarch_obstack_zalloc (struct gdbarch *arch, long size)
{
  void *data = obstack_alloc (arch->obstack, size);
  memset (data, 0, size);
  return data;
}


/* Free a gdbarch struct.  This should never happen in normal
   operation --- once you've created a gdbarch, you keep it around.
   However, if an architecture's init function encounters an error
   building the structure, it may need to clean up a partially
   constructed gdbarch.  */

void
gdbarch_free (struct gdbarch *arch)
{
  struct obstack *obstack;
  gdb_assert (arch != NULL);
  gdb_assert (!arch->initialized_p);
  obstack = arch->obstack;
  obstack_free (obstack, 0); /* Includes the ARCH.  */
  xfree (obstack);
}


/* Ensure that all values in a GDBARCH are reasonable.  */

/* NOTE/WARNING: The parameter is called ``current_gdbarch'' so that it
   just happens to match the global variable ``current_gdbarch''.  That
   way macros refering to that variable get the local and not the global
   version - ulgh.  Once everything is parameterised with gdbarch, this
   will go away. */

static void
verify_gdbarch (struct gdbarch *current_gdbarch)
{
  struct ui_file *log;
  struct cleanup *cleanups;
  long dummy;
  char *buf;
  log = mem_fileopen ();
  cleanups = make_cleanup_ui_file_delete (log);
  /* fundamental */
  if (current_gdbarch->byte_order == BFD_ENDIAN_UNKNOWN)
    fprintf_unfiltered (log, "\n\tbyte-order");
  if (current_gdbarch->bfd_arch_info == NULL)
    fprintf_unfiltered (log, "\n\tbfd_arch_info");
  /* Check those that need to be defined for the given multi-arch level. */
  /* Skip verify of short_bit, invalid_p == 0 */
  /* Skip verify of int_bit, invalid_p == 0 */
  /* Skip verify of long_bit, invalid_p == 0 */
  /* Skip verify of long_long_bit, invalid_p == 0 */
  /* Skip verify of float_bit, invalid_p == 0 */
  /* Skip verify of double_bit, invalid_p == 0 */
  /* Skip verify of long_double_bit, invalid_p == 0 */
  /* Skip verify of ptr_bit, invalid_p == 0 */
  if (current_gdbarch->addr_bit == 0)
    current_gdbarch->addr_bit = TARGET_PTR_BIT;
  /* Skip verify of bfd_vma_bit, invalid_p == 0 */
  if (current_gdbarch->char_signed == -1)
    current_gdbarch->char_signed = 1;
  /* Skip verify of read_pc, has predicate */
  /* Skip verify of write_pc, invalid_p == 0 */
  /* Skip verify of read_sp, has predicate */
  /* Skip verify of virtual_frame_pointer, invalid_p == 0 */
  /* Skip verify of pseudo_register_read, has predicate */
  /* Skip verify of pseudo_register_write, has predicate */
  if ((GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL)
      && (current_gdbarch->num_regs == -1))
    fprintf_unfiltered (log, "\n\tnum_regs");
  /* Skip verify of num_pseudo_regs, invalid_p == 0 */
  /* Skip verify of sp_regnum, invalid_p == 0 */
  /* Skip verify of pc_regnum, invalid_p == 0 */
  /* Skip verify of ps_regnum, invalid_p == 0 */
  /* Skip verify of fp0_regnum, invalid_p == 0 */
  /* Skip verify of stab_reg_to_regnum, invalid_p == 0 */
  /* Skip verify of ecoff_reg_to_regnum, invalid_p == 0 */
  /* Skip verify of dwarf_reg_to_regnum, invalid_p == 0 */
  /* Skip verify of sdb_reg_to_regnum, invalid_p == 0 */
  /* Skip verify of dwarf2_reg_to_regnum, invalid_p == 0 */
  /* Skip verify of register_type, has predicate */
  /* Skip verify of deprecated_register_virtual_type, has predicate */
  /* Skip verify of deprecated_register_byte, has predicate */
  /* Skip verify of deprecated_register_raw_size, has predicate */
  /* Skip verify of deprecated_register_virtual_size, has predicate */
  /* Skip verify of deprecated_max_register_raw_size, has predicate */
  /* Skip verify of deprecated_max_register_virtual_size, has predicate */
  /* Skip verify of unwind_dummy_id, has predicate */
  /* Skip verify of deprecated_save_dummy_frame_tos, has predicate */
  /* Skip verify of deprecated_fp_regnum, invalid_p == 0 */
  /* Skip verify of deprecated_target_read_fp, has predicate */
  /* Skip verify of push_dummy_call, has predicate */
  /* Skip verify of deprecated_push_arguments, has predicate */
  /* Skip verify of deprecated_use_generic_dummy_frames, invalid_p == 0 */
  /* Skip verify of deprecated_push_return_address, has predicate */
  /* Skip verify of deprecated_dummy_write_sp, has predicate */
  /* Skip verify of call_dummy_location, invalid_p == 0 */
  /* Skip verify of deprecated_call_dummy_words, invalid_p == 0 */
  /* Skip verify of deprecated_sizeof_call_dummy_words, invalid_p == 0 */
  /* Skip verify of deprecated_fix_call_dummy, has predicate */
  /* Skip verify of push_dummy_code, has predicate */
  /* Skip verify of deprecated_push_dummy_frame, has predicate */
  /* Skip verify of deprecated_do_registers_info, has predicate */
  /* Skip verify of print_registers_info, invalid_p == 0 */
  /* Skip verify of print_float_info, has predicate */
  /* Skip verify of print_vector_info, has predicate */
  /* Skip verify of register_sim_regno, invalid_p == 0 */
  /* Skip verify of register_bytes_ok, has predicate */
  /* Skip verify of cannot_fetch_register, invalid_p == 0 */
  /* Skip verify of cannot_store_register, invalid_p == 0 */
  /* Skip verify of get_longjmp_target, has predicate */
  /* Skip verify of deprecated_pc_in_call_dummy, has predicate */
  /* Skip verify of deprecated_init_frame_pc_first, has predicate */
  /* Skip verify of deprecated_init_frame_pc, has predicate */
  /* Skip verify of deprecated_get_saved_register, has predicate */
  /* Skip verify of deprecated_register_convertible, has predicate */
  /* Skip verify of deprecated_register_convert_to_virtual, invalid_p == 0 */
  /* Skip verify of deprecated_register_convert_to_raw, invalid_p == 0 */
  /* Skip verify of convert_register_p, invalid_p == 0 */
  /* Skip verify of register_to_value, invalid_p == 0 */
  /* Skip verify of value_to_register, invalid_p == 0 */
  /* Skip verify of pointer_to_address, invalid_p == 0 */
  /* Skip verify of address_to_pointer, invalid_p == 0 */
  /* Skip verify of integer_to_address, has predicate */
  /* Skip verify of deprecated_pop_frame, has predicate */
  /* Skip verify of deprecated_store_struct_return, has predicate */
  /* Skip verify of return_value, has predicate */
  /* Skip verify of return_value_on_stack, invalid_p == 0 */
  /* Skip verify of extract_return_value, invalid_p == 0 */
  /* Skip verify of store_return_value, invalid_p == 0 */
  /* Skip verify of use_struct_convention, invalid_p == 0 */
  /* Skip verify of deprecated_extract_struct_value_address, has predicate */
  /* Skip verify of deprecated_frame_init_saved_regs, has predicate */
  /* Skip verify of deprecated_init_extra_frame_info, has predicate */
  if ((GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL)
      && (current_gdbarch->skip_prologue == 0))
    fprintf_unfiltered (log, "\n\tskip_prologue");
  if ((GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL)
      && (current_gdbarch->inner_than == 0))
    fprintf_unfiltered (log, "\n\tinner_than");
  if ((GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL)
      && (current_gdbarch->breakpoint_from_pc == 0))
    fprintf_unfiltered (log, "\n\tbreakpoint_from_pc");
  /* Skip verify of adjust_breakpoint_address, has predicate */
  /* Skip verify of memory_insert_breakpoint, invalid_p == 0 */
  /* Skip verify of memory_remove_breakpoint, invalid_p == 0 */
  /* Skip verify of decr_pc_after_break, invalid_p == 0 */
  /* Skip verify of function_start_offset, invalid_p == 0 */
  /* Skip verify of remote_translate_xfer_address, invalid_p == 0 */
  /* Skip verify of frame_args_skip, invalid_p == 0 */
  /* Skip verify of deprecated_frameless_function_invocation, has predicate */
  /* Skip verify of deprecated_frame_chain, has predicate */
  /* Skip verify of deprecated_frame_chain_valid, has predicate */
  /* Skip verify of deprecated_frame_saved_pc, has predicate */
  /* Skip verify of unwind_pc, has predicate */
  /* Skip verify of unwind_sp, has predicate */
  /* Skip verify of deprecated_frame_args_address, has predicate */
  /* Skip verify of deprecated_frame_locals_address, has predicate */
  /* Skip verify of deprecated_saved_pc_after_call, has predicate */
  /* Skip verify of frame_num_args, has predicate */
  /* Skip verify of deprecated_stack_align, has predicate */
  /* Skip verify of frame_align, has predicate */
  /* Skip verify of deprecated_reg_struct_has_addr, has predicate */
  /* Skip verify of stabs_argument_has_addr, invalid_p == 0 */
  if (current_gdbarch->float_format == 0)
    current_gdbarch->float_format = default_float_format (current_gdbarch);
  if (current_gdbarch->double_format == 0)
    current_gdbarch->double_format = default_double_format (current_gdbarch);
  if (current_gdbarch->long_double_format == 0)
    current_gdbarch->long_double_format = default_double_format (current_gdbarch);
  /* Skip verify of convert_from_func_ptr_addr, invalid_p == 0 */
  /* Skip verify of addr_bits_remove, invalid_p == 0 */
  /* Skip verify of smash_text_address, invalid_p == 0 */
  /* Skip verify of software_single_step, has predicate */
  if ((GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL)
      && (current_gdbarch->print_insn == 0))
    fprintf_unfiltered (log, "\n\tprint_insn");
  /* Skip verify of skip_trampoline_code, invalid_p == 0 */
  /* Skip verify of skip_solib_resolver, invalid_p == 0 */
  /* Skip verify of in_solib_call_trampoline, invalid_p == 0 */
  /* Skip verify of in_solib_return_trampoline, invalid_p == 0 */
  /* Skip verify of pc_in_sigtramp, invalid_p == 0 */
  /* Skip verify of sigtramp_start, has predicate */
  /* Skip verify of sigtramp_end, has predicate */
  /* Skip verify of in_function_epilogue_p, invalid_p == 0 */
  /* Skip verify of construct_inferior_arguments, invalid_p == 0 */
  /* Skip verify of elf_make_msymbol_special, invalid_p == 0 */
  /* Skip verify of coff_make_msymbol_special, invalid_p == 0 */
  /* Skip verify of name_of_malloc, invalid_p == 0 */
  /* Skip verify of cannot_step_breakpoint, invalid_p == 0 */
  /* Skip verify of have_nonsteppable_watchpoint, invalid_p == 0 */
  /* Skip verify of address_class_type_flags, has predicate */
  /* Skip verify of address_class_type_flags_to_name, has predicate */
  /* Skip verify of address_class_name_to_type_flags, has predicate */
  /* Skip verify of register_reggroup_p, invalid_p == 0 */
  /* Skip verify of fetch_pointer_argument, has predicate */
  /* Skip verify of regset_from_core_section, has predicate */
  buf = ui_file_xstrdup (log, &dummy);
  make_cleanup (xfree, buf);
  if (strlen (buf) > 0)
    internal_error (__FILE__, __LINE__,
                    "verify_gdbarch: the following are invalid ...%s",
                    buf);
  do_cleanups (cleanups);
}


/* Print out the details of the current architecture. */

/* NOTE/WARNING: The parameter is called ``current_gdbarch'' so that it
   just happens to match the global variable ``current_gdbarch''.  That
   way macros refering to that variable get the local and not the global
   version - ulgh.  Once everything is parameterised with gdbarch, this
   will go away. */

void
gdbarch_dump (struct gdbarch *current_gdbarch, struct ui_file *file)
{
  fprintf_unfiltered (file,
                      "gdbarch_dump: GDB_MULTI_ARCH = %d\n",
                      GDB_MULTI_ARCH);
  fprintf_unfiltered (file,
                      "gdbarch_dump: convert_from_func_ptr_addr = 0x%08lx\n",
                      (long) current_gdbarch->convert_from_func_ptr_addr);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_frame_align_p() = %d\n",
                      gdbarch_frame_align_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: frame_align = 0x%08lx\n",
                      (long) current_gdbarch->frame_align);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_regset_from_core_section_p() = %d\n",
                      gdbarch_regset_from_core_section_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: regset_from_core_section = 0x%08lx\n",
                      (long) current_gdbarch->regset_from_core_section);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_return_value_p() = %d\n",
                      gdbarch_return_value_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: return_value = 0x%08lx\n",
                      (long) current_gdbarch->return_value);
  fprintf_unfiltered (file,
                      "gdbarch_dump: in_function_epilogue_p = 0x%08lx\n",
                      (long) current_gdbarch->in_function_epilogue_p);
  fprintf_unfiltered (file,
                      "gdbarch_dump: register_reggroup_p = 0x%08lx\n",
                      (long) current_gdbarch->register_reggroup_p);
  fprintf_unfiltered (file,
                      "gdbarch_dump: stabs_argument_has_addr = 0x%08lx\n",
                      (long) current_gdbarch->stabs_argument_has_addr);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_pseudo_register_read_p() = %d\n",
                      gdbarch_pseudo_register_read_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: pseudo_register_read = 0x%08lx\n",
                      (long) current_gdbarch->pseudo_register_read);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_pseudo_register_write_p() = %d\n",
                      gdbarch_pseudo_register_write_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: pseudo_register_write = 0x%08lx\n",
                      (long) current_gdbarch->pseudo_register_write);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_address_class_name_to_type_flags_p() = %d\n",
                      gdbarch_address_class_name_to_type_flags_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: address_class_name_to_type_flags = 0x%08lx\n",
                      (long) current_gdbarch->address_class_name_to_type_flags);
#ifdef ADDRESS_CLASS_TYPE_FLAGS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ADDRESS_CLASS_TYPE_FLAGS_P()",
                      XSTRING (ADDRESS_CLASS_TYPE_FLAGS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ADDRESS_CLASS_TYPE_FLAGS_P() = %d\n",
                      ADDRESS_CLASS_TYPE_FLAGS_P ());
#endif
#ifdef ADDRESS_CLASS_TYPE_FLAGS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ADDRESS_CLASS_TYPE_FLAGS(byte_size, dwarf2_addr_class)",
                      XSTRING (ADDRESS_CLASS_TYPE_FLAGS (byte_size, dwarf2_addr_class)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ADDRESS_CLASS_TYPE_FLAGS = <0x%08lx>\n",
                      (long) current_gdbarch->address_class_type_flags
                      /*ADDRESS_CLASS_TYPE_FLAGS ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_address_class_type_flags_to_name_p() = %d\n",
                      gdbarch_address_class_type_flags_to_name_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: address_class_type_flags_to_name = 0x%08lx\n",
                      (long) current_gdbarch->address_class_type_flags_to_name);
#ifdef ADDRESS_TO_POINTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ADDRESS_TO_POINTER(type, buf, addr)",
                      XSTRING (ADDRESS_TO_POINTER (type, buf, addr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ADDRESS_TO_POINTER = <0x%08lx>\n",
                      (long) current_gdbarch->address_to_pointer
                      /*ADDRESS_TO_POINTER ()*/);
#endif
#ifdef ADDR_BITS_REMOVE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ADDR_BITS_REMOVE(addr)",
                      XSTRING (ADDR_BITS_REMOVE (addr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ADDR_BITS_REMOVE = <0x%08lx>\n",
                      (long) current_gdbarch->addr_bits_remove
                      /*ADDR_BITS_REMOVE ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_adjust_breakpoint_address_p() = %d\n",
                      gdbarch_adjust_breakpoint_address_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: adjust_breakpoint_address = 0x%08lx\n",
                      (long) current_gdbarch->adjust_breakpoint_address);
#ifdef BELIEVE_PCC_PROMOTION
  fprintf_unfiltered (file,
                      "gdbarch_dump: BELIEVE_PCC_PROMOTION # %s\n",
                      XSTRING (BELIEVE_PCC_PROMOTION));
  fprintf_unfiltered (file,
                      "gdbarch_dump: BELIEVE_PCC_PROMOTION = %d\n",
                      BELIEVE_PCC_PROMOTION);
#endif
#ifdef BELIEVE_PCC_PROMOTION_TYPE
  fprintf_unfiltered (file,
                      "gdbarch_dump: BELIEVE_PCC_PROMOTION_TYPE # %s\n",
                      XSTRING (BELIEVE_PCC_PROMOTION_TYPE));
  fprintf_unfiltered (file,
                      "gdbarch_dump: BELIEVE_PCC_PROMOTION_TYPE = %d\n",
                      BELIEVE_PCC_PROMOTION_TYPE);
#endif
#ifdef BREAKPOINT_FROM_PC
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "BREAKPOINT_FROM_PC(pcptr, lenptr)",
                      XSTRING (BREAKPOINT_FROM_PC (pcptr, lenptr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: BREAKPOINT_FROM_PC = <0x%08lx>\n",
                      (long) current_gdbarch->breakpoint_from_pc
                      /*BREAKPOINT_FROM_PC ()*/);
#endif
#ifdef CALL_DUMMY_LOCATION
  fprintf_unfiltered (file,
                      "gdbarch_dump: CALL_DUMMY_LOCATION # %s\n",
                      XSTRING (CALL_DUMMY_LOCATION));
  fprintf_unfiltered (file,
                      "gdbarch_dump: CALL_DUMMY_LOCATION = %d\n",
                      CALL_DUMMY_LOCATION);
#endif
#ifdef CANNOT_FETCH_REGISTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "CANNOT_FETCH_REGISTER(regnum)",
                      XSTRING (CANNOT_FETCH_REGISTER (regnum)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: CANNOT_FETCH_REGISTER = <0x%08lx>\n",
                      (long) current_gdbarch->cannot_fetch_register
                      /*CANNOT_FETCH_REGISTER ()*/);
#endif
#ifdef CANNOT_STEP_BREAKPOINT
  fprintf_unfiltered (file,
                      "gdbarch_dump: CANNOT_STEP_BREAKPOINT # %s\n",
                      XSTRING (CANNOT_STEP_BREAKPOINT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: CANNOT_STEP_BREAKPOINT = %d\n",
                      CANNOT_STEP_BREAKPOINT);
#endif
#ifdef CANNOT_STORE_REGISTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "CANNOT_STORE_REGISTER(regnum)",
                      XSTRING (CANNOT_STORE_REGISTER (regnum)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: CANNOT_STORE_REGISTER = <0x%08lx>\n",
                      (long) current_gdbarch->cannot_store_register
                      /*CANNOT_STORE_REGISTER ()*/);
#endif
#ifdef COFF_MAKE_MSYMBOL_SPECIAL
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "COFF_MAKE_MSYMBOL_SPECIAL(val, msym)",
                      XSTRING (COFF_MAKE_MSYMBOL_SPECIAL (val, msym)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: COFF_MAKE_MSYMBOL_SPECIAL = <0x%08lx>\n",
                      (long) current_gdbarch->coff_make_msymbol_special
                      /*COFF_MAKE_MSYMBOL_SPECIAL ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: construct_inferior_arguments = 0x%08lx\n",
                      (long) current_gdbarch->construct_inferior_arguments);
#ifdef CONVERT_REGISTER_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "CONVERT_REGISTER_P(regnum, type)",
                      XSTRING (CONVERT_REGISTER_P (regnum, type)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: CONVERT_REGISTER_P = <0x%08lx>\n",
                      (long) current_gdbarch->convert_register_p
                      /*CONVERT_REGISTER_P ()*/);
#endif
#ifdef DECR_PC_AFTER_BREAK
  fprintf_unfiltered (file,
                      "gdbarch_dump: DECR_PC_AFTER_BREAK # %s\n",
                      XSTRING (DECR_PC_AFTER_BREAK));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DECR_PC_AFTER_BREAK = %ld\n",
                      (long) DECR_PC_AFTER_BREAK);
#endif
#ifdef DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET # %s\n",
                      XSTRING (DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET = %ld\n",
                      (long) DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET);
#endif
#ifdef DEPRECATED_CALL_DUMMY_LENGTH
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_LENGTH # %s\n",
                      XSTRING (DEPRECATED_CALL_DUMMY_LENGTH));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_LENGTH = %d\n",
                      DEPRECATED_CALL_DUMMY_LENGTH);
#endif
#ifdef DEPRECATED_CALL_DUMMY_START_OFFSET
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_START_OFFSET # %s\n",
                      XSTRING (DEPRECATED_CALL_DUMMY_START_OFFSET));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_START_OFFSET = %ld\n",
                      (long) DEPRECATED_CALL_DUMMY_START_OFFSET);
#endif
#ifdef DEPRECATED_CALL_DUMMY_WORDS
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_WORDS # %s\n",
                      XSTRING (DEPRECATED_CALL_DUMMY_WORDS));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_CALL_DUMMY_WORDS = 0x%08lx\n",
                      (long) DEPRECATED_CALL_DUMMY_WORDS);
#endif
#ifdef DEPRECATED_DO_REGISTERS_INFO_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_DO_REGISTERS_INFO_P()",
                      XSTRING (DEPRECATED_DO_REGISTERS_INFO_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_DO_REGISTERS_INFO_P() = %d\n",
                      DEPRECATED_DO_REGISTERS_INFO_P ());
#endif
#ifdef DEPRECATED_DO_REGISTERS_INFO
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_DO_REGISTERS_INFO(reg_nr, fpregs)",
                      XSTRING (DEPRECATED_DO_REGISTERS_INFO (reg_nr, fpregs)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_DO_REGISTERS_INFO = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_do_registers_info
                      /*DEPRECATED_DO_REGISTERS_INFO ()*/);
#endif
#ifdef DEPRECATED_DUMMY_WRITE_SP_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_DUMMY_WRITE_SP_P()",
                      XSTRING (DEPRECATED_DUMMY_WRITE_SP_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_DUMMY_WRITE_SP_P() = %d\n",
                      DEPRECATED_DUMMY_WRITE_SP_P ());
#endif
#ifdef DEPRECATED_DUMMY_WRITE_SP
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_DUMMY_WRITE_SP(val)",
                      XSTRING (DEPRECATED_DUMMY_WRITE_SP (val)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_DUMMY_WRITE_SP = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_dummy_write_sp
                      /*DEPRECATED_DUMMY_WRITE_SP ()*/);
#endif
#ifdef DEPRECATED_EXTRACT_RETURN_VALUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_EXTRACT_RETURN_VALUE(type, regbuf, valbuf)",
                      XSTRING (DEPRECATED_EXTRACT_RETURN_VALUE (type, regbuf, valbuf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_EXTRACT_RETURN_VALUE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_extract_return_value
                      /*DEPRECATED_EXTRACT_RETURN_VALUE ()*/);
#endif
#ifdef DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P()",
                      XSTRING (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P() = %d\n",
                      DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P ());
#endif
#ifdef DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS(regcache)",
                      XSTRING (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS (regcache)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_extract_struct_value_address
                      /*DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS ()*/);
#endif
#ifdef DEPRECATED_FIX_CALL_DUMMY_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FIX_CALL_DUMMY_P()",
                      XSTRING (DEPRECATED_FIX_CALL_DUMMY_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FIX_CALL_DUMMY_P() = %d\n",
                      DEPRECATED_FIX_CALL_DUMMY_P ());
#endif
#ifdef DEPRECATED_FIX_CALL_DUMMY
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FIX_CALL_DUMMY(dummy, pc, fun, nargs, args, type, gcc_p)",
                      XSTRING (DEPRECATED_FIX_CALL_DUMMY (dummy, pc, fun, nargs, args, type, gcc_p)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FIX_CALL_DUMMY = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_fix_call_dummy
                      /*DEPRECATED_FIX_CALL_DUMMY ()*/);
#endif
#ifdef DEPRECATED_FP_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FP_REGNUM # %s\n",
                      XSTRING (DEPRECATED_FP_REGNUM));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FP_REGNUM = %d\n",
                      DEPRECATED_FP_REGNUM);
#endif
#ifdef DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P()",
                      XSTRING (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P() = %d\n",
                      DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P ());
#endif
#ifdef DEPRECATED_FRAMELESS_FUNCTION_INVOCATION
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAMELESS_FUNCTION_INVOCATION(fi)",
                      XSTRING (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION (fi)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAMELESS_FUNCTION_INVOCATION = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frameless_function_invocation
                      /*DEPRECATED_FRAMELESS_FUNCTION_INVOCATION ()*/);
#endif
#ifdef DEPRECATED_FRAME_ARGS_ADDRESS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_ARGS_ADDRESS_P()",
                      XSTRING (DEPRECATED_FRAME_ARGS_ADDRESS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_ARGS_ADDRESS_P() = %d\n",
                      DEPRECATED_FRAME_ARGS_ADDRESS_P ());
#endif
#ifdef DEPRECATED_FRAME_ARGS_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_ARGS_ADDRESS(fi)",
                      XSTRING (DEPRECATED_FRAME_ARGS_ADDRESS (fi)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_ARGS_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_args_address
                      /*DEPRECATED_FRAME_ARGS_ADDRESS ()*/);
#endif
#ifdef DEPRECATED_FRAME_CHAIN_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_CHAIN_P()",
                      XSTRING (DEPRECATED_FRAME_CHAIN_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_CHAIN_P() = %d\n",
                      DEPRECATED_FRAME_CHAIN_P ());
#endif
#ifdef DEPRECATED_FRAME_CHAIN
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_CHAIN(frame)",
                      XSTRING (DEPRECATED_FRAME_CHAIN (frame)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_CHAIN = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_chain
                      /*DEPRECATED_FRAME_CHAIN ()*/);
#endif
#ifdef DEPRECATED_FRAME_CHAIN_VALID_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_CHAIN_VALID_P()",
                      XSTRING (DEPRECATED_FRAME_CHAIN_VALID_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_CHAIN_VALID_P() = %d\n",
                      DEPRECATED_FRAME_CHAIN_VALID_P ());
#endif
#ifdef DEPRECATED_FRAME_CHAIN_VALID
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_CHAIN_VALID(chain, thisframe)",
                      XSTRING (DEPRECATED_FRAME_CHAIN_VALID (chain, thisframe)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_CHAIN_VALID = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_chain_valid
                      /*DEPRECATED_FRAME_CHAIN_VALID ()*/);
#endif
#ifdef DEPRECATED_FRAME_INIT_SAVED_REGS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_INIT_SAVED_REGS_P()",
                      XSTRING (DEPRECATED_FRAME_INIT_SAVED_REGS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_INIT_SAVED_REGS_P() = %d\n",
                      DEPRECATED_FRAME_INIT_SAVED_REGS_P ());
#endif
#ifdef DEPRECATED_FRAME_INIT_SAVED_REGS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_INIT_SAVED_REGS(frame)",
                      XSTRING (DEPRECATED_FRAME_INIT_SAVED_REGS (frame)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_INIT_SAVED_REGS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_init_saved_regs
                      /*DEPRECATED_FRAME_INIT_SAVED_REGS ()*/);
#endif
#ifdef DEPRECATED_FRAME_LOCALS_ADDRESS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_LOCALS_ADDRESS_P()",
                      XSTRING (DEPRECATED_FRAME_LOCALS_ADDRESS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_LOCALS_ADDRESS_P() = %d\n",
                      DEPRECATED_FRAME_LOCALS_ADDRESS_P ());
#endif
#ifdef DEPRECATED_FRAME_LOCALS_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_LOCALS_ADDRESS(fi)",
                      XSTRING (DEPRECATED_FRAME_LOCALS_ADDRESS (fi)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_LOCALS_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_locals_address
                      /*DEPRECATED_FRAME_LOCALS_ADDRESS ()*/);
#endif
#ifdef DEPRECATED_FRAME_SAVED_PC_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_SAVED_PC_P()",
                      XSTRING (DEPRECATED_FRAME_SAVED_PC_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_SAVED_PC_P() = %d\n",
                      DEPRECATED_FRAME_SAVED_PC_P ());
#endif
#ifdef DEPRECATED_FRAME_SAVED_PC
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_FRAME_SAVED_PC(fi)",
                      XSTRING (DEPRECATED_FRAME_SAVED_PC (fi)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_FRAME_SAVED_PC = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_frame_saved_pc
                      /*DEPRECATED_FRAME_SAVED_PC ()*/);
#endif
#ifdef DEPRECATED_GET_SAVED_REGISTER_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_GET_SAVED_REGISTER_P()",
                      XSTRING (DEPRECATED_GET_SAVED_REGISTER_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_GET_SAVED_REGISTER_P() = %d\n",
                      DEPRECATED_GET_SAVED_REGISTER_P ());
#endif
#ifdef DEPRECATED_GET_SAVED_REGISTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_GET_SAVED_REGISTER(raw_buffer, optimized, addrp, frame, regnum, lval)",
                      XSTRING (DEPRECATED_GET_SAVED_REGISTER (raw_buffer, optimized, addrp, frame, regnum, lval)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_GET_SAVED_REGISTER = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_get_saved_register
                      /*DEPRECATED_GET_SAVED_REGISTER ()*/);
#endif
#ifdef DEPRECATED_INIT_EXTRA_FRAME_INFO_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_EXTRA_FRAME_INFO_P()",
                      XSTRING (DEPRECATED_INIT_EXTRA_FRAME_INFO_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_EXTRA_FRAME_INFO_P() = %d\n",
                      DEPRECATED_INIT_EXTRA_FRAME_INFO_P ());
#endif
#ifdef DEPRECATED_INIT_EXTRA_FRAME_INFO
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_EXTRA_FRAME_INFO(fromleaf, frame)",
                      XSTRING (DEPRECATED_INIT_EXTRA_FRAME_INFO (fromleaf, frame)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_EXTRA_FRAME_INFO = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_init_extra_frame_info
                      /*DEPRECATED_INIT_EXTRA_FRAME_INFO ()*/);
#endif
#ifdef DEPRECATED_INIT_FRAME_PC_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_FRAME_PC_P()",
                      XSTRING (DEPRECATED_INIT_FRAME_PC_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_FRAME_PC_P() = %d\n",
                      DEPRECATED_INIT_FRAME_PC_P ());
#endif
#ifdef DEPRECATED_INIT_FRAME_PC
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_FRAME_PC(fromleaf, prev)",
                      XSTRING (DEPRECATED_INIT_FRAME_PC (fromleaf, prev)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_FRAME_PC = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_init_frame_pc
                      /*DEPRECATED_INIT_FRAME_PC ()*/);
#endif
#ifdef DEPRECATED_INIT_FRAME_PC_FIRST_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_FRAME_PC_FIRST_P()",
                      XSTRING (DEPRECATED_INIT_FRAME_PC_FIRST_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_FRAME_PC_FIRST_P() = %d\n",
                      DEPRECATED_INIT_FRAME_PC_FIRST_P ());
#endif
#ifdef DEPRECATED_INIT_FRAME_PC_FIRST
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_INIT_FRAME_PC_FIRST(fromleaf, prev)",
                      XSTRING (DEPRECATED_INIT_FRAME_PC_FIRST (fromleaf, prev)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_INIT_FRAME_PC_FIRST = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_init_frame_pc_first
                      /*DEPRECATED_INIT_FRAME_PC_FIRST ()*/);
#endif
#ifdef DEPRECATED_MAX_REGISTER_RAW_SIZE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_MAX_REGISTER_RAW_SIZE_P()",
                      XSTRING (DEPRECATED_MAX_REGISTER_RAW_SIZE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_RAW_SIZE_P() = %d\n",
                      DEPRECATED_MAX_REGISTER_RAW_SIZE_P ());
#endif
#ifdef DEPRECATED_MAX_REGISTER_RAW_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_RAW_SIZE # %s\n",
                      XSTRING (DEPRECATED_MAX_REGISTER_RAW_SIZE));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_RAW_SIZE = %d\n",
                      DEPRECATED_MAX_REGISTER_RAW_SIZE);
#endif
#ifdef DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P()",
                      XSTRING (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P() = %d\n",
                      DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P ());
#endif
#ifdef DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE # %s\n",
                      XSTRING (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE = %d\n",
                      DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE);
#endif
#ifdef DEPRECATED_PC_IN_CALL_DUMMY_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PC_IN_CALL_DUMMY_P()",
                      XSTRING (DEPRECATED_PC_IN_CALL_DUMMY_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PC_IN_CALL_DUMMY_P() = %d\n",
                      DEPRECATED_PC_IN_CALL_DUMMY_P ());
#endif
#ifdef DEPRECATED_PC_IN_CALL_DUMMY
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PC_IN_CALL_DUMMY(pc, sp, frame_address)",
                      XSTRING (DEPRECATED_PC_IN_CALL_DUMMY (pc, sp, frame_address)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PC_IN_CALL_DUMMY = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_pc_in_call_dummy
                      /*DEPRECATED_PC_IN_CALL_DUMMY ()*/);
#endif
#ifdef DEPRECATED_POP_FRAME_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_POP_FRAME_P()",
                      XSTRING (DEPRECATED_POP_FRAME_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_POP_FRAME_P() = %d\n",
                      DEPRECATED_POP_FRAME_P ());
#endif
#ifdef DEPRECATED_POP_FRAME
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_POP_FRAME(-)",
                      XSTRING (DEPRECATED_POP_FRAME (-)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_POP_FRAME = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_pop_frame
                      /*DEPRECATED_POP_FRAME ()*/);
#endif
#ifdef DEPRECATED_PUSH_ARGUMENTS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_ARGUMENTS_P()",
                      XSTRING (DEPRECATED_PUSH_ARGUMENTS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_ARGUMENTS_P() = %d\n",
                      DEPRECATED_PUSH_ARGUMENTS_P ());
#endif
#ifdef DEPRECATED_PUSH_ARGUMENTS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_ARGUMENTS(nargs, args, sp, struct_return, struct_addr)",
                      XSTRING (DEPRECATED_PUSH_ARGUMENTS (nargs, args, sp, struct_return, struct_addr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_ARGUMENTS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_push_arguments
                      /*DEPRECATED_PUSH_ARGUMENTS ()*/);
#endif
#ifdef DEPRECATED_PUSH_DUMMY_FRAME_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_DUMMY_FRAME_P()",
                      XSTRING (DEPRECATED_PUSH_DUMMY_FRAME_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_DUMMY_FRAME_P() = %d\n",
                      DEPRECATED_PUSH_DUMMY_FRAME_P ());
#endif
#ifdef DEPRECATED_PUSH_DUMMY_FRAME
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_DUMMY_FRAME(-)",
                      XSTRING (DEPRECATED_PUSH_DUMMY_FRAME (-)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_DUMMY_FRAME = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_push_dummy_frame
                      /*DEPRECATED_PUSH_DUMMY_FRAME ()*/);
#endif
#ifdef DEPRECATED_PUSH_RETURN_ADDRESS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_RETURN_ADDRESS_P()",
                      XSTRING (DEPRECATED_PUSH_RETURN_ADDRESS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_RETURN_ADDRESS_P() = %d\n",
                      DEPRECATED_PUSH_RETURN_ADDRESS_P ());
#endif
#ifdef DEPRECATED_PUSH_RETURN_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_PUSH_RETURN_ADDRESS(pc, sp)",
                      XSTRING (DEPRECATED_PUSH_RETURN_ADDRESS (pc, sp)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_PUSH_RETURN_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_push_return_address
                      /*DEPRECATED_PUSH_RETURN_ADDRESS ()*/);
#endif
#ifdef DEPRECATED_REGISTER_BYTE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_BYTE_P()",
                      XSTRING (DEPRECATED_REGISTER_BYTE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_BYTE_P() = %d\n",
                      DEPRECATED_REGISTER_BYTE_P ());
#endif
#ifdef DEPRECATED_REGISTER_BYTE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_BYTE(reg_nr)",
                      XSTRING (DEPRECATED_REGISTER_BYTE (reg_nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_BYTE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_byte
                      /*DEPRECATED_REGISTER_BYTE ()*/);
#endif
#ifdef DEPRECATED_REGISTER_BYTES
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_BYTES # %s\n",
                      XSTRING (DEPRECATED_REGISTER_BYTES));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_BYTES = %d\n",
                      DEPRECATED_REGISTER_BYTES);
#endif
#ifdef DEPRECATED_REGISTER_CONVERTIBLE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_CONVERTIBLE_P()",
                      XSTRING (DEPRECATED_REGISTER_CONVERTIBLE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_CONVERTIBLE_P() = %d\n",
                      DEPRECATED_REGISTER_CONVERTIBLE_P ());
#endif
#ifdef DEPRECATED_REGISTER_CONVERTIBLE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_CONVERTIBLE(nr)",
                      XSTRING (DEPRECATED_REGISTER_CONVERTIBLE (nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_CONVERTIBLE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_convertible
                      /*DEPRECATED_REGISTER_CONVERTIBLE ()*/);
#endif
#ifdef DEPRECATED_REGISTER_CONVERT_TO_RAW
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_CONVERT_TO_RAW(type, regnum, from, to)",
                      XSTRING (DEPRECATED_REGISTER_CONVERT_TO_RAW (type, regnum, from, to)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_CONVERT_TO_RAW = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_convert_to_raw
                      /*DEPRECATED_REGISTER_CONVERT_TO_RAW ()*/);
#endif
#ifdef DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL(regnum, type, from, to)",
                      XSTRING (DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL (regnum, type, from, to)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_convert_to_virtual
                      /*DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL ()*/);
#endif
#ifdef DEPRECATED_REGISTER_RAW_SIZE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_RAW_SIZE_P()",
                      XSTRING (DEPRECATED_REGISTER_RAW_SIZE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_RAW_SIZE_P() = %d\n",
                      DEPRECATED_REGISTER_RAW_SIZE_P ());
#endif
#ifdef DEPRECATED_REGISTER_RAW_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_RAW_SIZE(reg_nr)",
                      XSTRING (DEPRECATED_REGISTER_RAW_SIZE (reg_nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_RAW_SIZE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_raw_size
                      /*DEPRECATED_REGISTER_RAW_SIZE ()*/);
#endif
#ifdef DEPRECATED_REGISTER_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_SIZE # %s\n",
                      XSTRING (DEPRECATED_REGISTER_SIZE));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_SIZE = %d\n",
                      DEPRECATED_REGISTER_SIZE);
#endif
#ifdef DEPRECATED_REGISTER_VIRTUAL_SIZE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_VIRTUAL_SIZE_P()",
                      XSTRING (DEPRECATED_REGISTER_VIRTUAL_SIZE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_VIRTUAL_SIZE_P() = %d\n",
                      DEPRECATED_REGISTER_VIRTUAL_SIZE_P ());
#endif
#ifdef DEPRECATED_REGISTER_VIRTUAL_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_VIRTUAL_SIZE(reg_nr)",
                      XSTRING (DEPRECATED_REGISTER_VIRTUAL_SIZE (reg_nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_VIRTUAL_SIZE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_virtual_size
                      /*DEPRECATED_REGISTER_VIRTUAL_SIZE ()*/);
#endif
#ifdef DEPRECATED_REGISTER_VIRTUAL_TYPE_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_VIRTUAL_TYPE_P()",
                      XSTRING (DEPRECATED_REGISTER_VIRTUAL_TYPE_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_VIRTUAL_TYPE_P() = %d\n",
                      DEPRECATED_REGISTER_VIRTUAL_TYPE_P ());
#endif
#ifdef DEPRECATED_REGISTER_VIRTUAL_TYPE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REGISTER_VIRTUAL_TYPE(reg_nr)",
                      XSTRING (DEPRECATED_REGISTER_VIRTUAL_TYPE (reg_nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REGISTER_VIRTUAL_TYPE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_register_virtual_type
                      /*DEPRECATED_REGISTER_VIRTUAL_TYPE ()*/);
#endif
#ifdef DEPRECATED_REG_STRUCT_HAS_ADDR_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REG_STRUCT_HAS_ADDR_P()",
                      XSTRING (DEPRECATED_REG_STRUCT_HAS_ADDR_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REG_STRUCT_HAS_ADDR_P() = %d\n",
                      DEPRECATED_REG_STRUCT_HAS_ADDR_P ());
#endif
#ifdef DEPRECATED_REG_STRUCT_HAS_ADDR
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_REG_STRUCT_HAS_ADDR(gcc_p, type)",
                      XSTRING (DEPRECATED_REG_STRUCT_HAS_ADDR (gcc_p, type)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_REG_STRUCT_HAS_ADDR = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_reg_struct_has_addr
                      /*DEPRECATED_REG_STRUCT_HAS_ADDR ()*/);
#endif
#ifdef DEPRECATED_SAVED_PC_AFTER_CALL_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_SAVED_PC_AFTER_CALL_P()",
                      XSTRING (DEPRECATED_SAVED_PC_AFTER_CALL_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SAVED_PC_AFTER_CALL_P() = %d\n",
                      DEPRECATED_SAVED_PC_AFTER_CALL_P ());
#endif
#ifdef DEPRECATED_SAVED_PC_AFTER_CALL
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_SAVED_PC_AFTER_CALL(frame)",
                      XSTRING (DEPRECATED_SAVED_PC_AFTER_CALL (frame)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SAVED_PC_AFTER_CALL = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_saved_pc_after_call
                      /*DEPRECATED_SAVED_PC_AFTER_CALL ()*/);
#endif
#ifdef DEPRECATED_SAVE_DUMMY_FRAME_TOS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_SAVE_DUMMY_FRAME_TOS_P()",
                      XSTRING (DEPRECATED_SAVE_DUMMY_FRAME_TOS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SAVE_DUMMY_FRAME_TOS_P() = %d\n",
                      DEPRECATED_SAVE_DUMMY_FRAME_TOS_P ());
#endif
#ifdef DEPRECATED_SAVE_DUMMY_FRAME_TOS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_SAVE_DUMMY_FRAME_TOS(sp)",
                      XSTRING (DEPRECATED_SAVE_DUMMY_FRAME_TOS (sp)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SAVE_DUMMY_FRAME_TOS = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_save_dummy_frame_tos
                      /*DEPRECATED_SAVE_DUMMY_FRAME_TOS ()*/);
#endif
#ifdef DEPRECATED_SIZEOF_CALL_DUMMY_WORDS
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SIZEOF_CALL_DUMMY_WORDS # %s\n",
                      XSTRING (DEPRECATED_SIZEOF_CALL_DUMMY_WORDS));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_SIZEOF_CALL_DUMMY_WORDS = %d\n",
                      DEPRECATED_SIZEOF_CALL_DUMMY_WORDS);
#endif
#ifdef DEPRECATED_STACK_ALIGN_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_STACK_ALIGN_P()",
                      XSTRING (DEPRECATED_STACK_ALIGN_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_STACK_ALIGN_P() = %d\n",
                      DEPRECATED_STACK_ALIGN_P ());
#endif
#ifdef DEPRECATED_STACK_ALIGN
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_STACK_ALIGN(sp)",
                      XSTRING (DEPRECATED_STACK_ALIGN (sp)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_STACK_ALIGN = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_stack_align
                      /*DEPRECATED_STACK_ALIGN ()*/);
#endif
#ifdef DEPRECATED_STORE_RETURN_VALUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_STORE_RETURN_VALUE(type, valbuf)",
                      XSTRING (DEPRECATED_STORE_RETURN_VALUE (type, valbuf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_STORE_RETURN_VALUE = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_store_return_value
                      /*DEPRECATED_STORE_RETURN_VALUE ()*/);
#endif
#ifdef DEPRECATED_STORE_STRUCT_RETURN_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_STORE_STRUCT_RETURN_P()",
                      XSTRING (DEPRECATED_STORE_STRUCT_RETURN_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_STORE_STRUCT_RETURN_P() = %d\n",
                      DEPRECATED_STORE_STRUCT_RETURN_P ());
#endif
#ifdef DEPRECATED_STORE_STRUCT_RETURN
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_STORE_STRUCT_RETURN(addr, sp)",
                      XSTRING (DEPRECATED_STORE_STRUCT_RETURN (addr, sp)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_STORE_STRUCT_RETURN = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_store_struct_return
                      /*DEPRECATED_STORE_STRUCT_RETURN ()*/);
#endif
#ifdef DEPRECATED_TARGET_READ_FP_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_TARGET_READ_FP_P()",
                      XSTRING (DEPRECATED_TARGET_READ_FP_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_TARGET_READ_FP_P() = %d\n",
                      DEPRECATED_TARGET_READ_FP_P ());
#endif
#ifdef DEPRECATED_TARGET_READ_FP
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DEPRECATED_TARGET_READ_FP()",
                      XSTRING (DEPRECATED_TARGET_READ_FP ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_TARGET_READ_FP = <0x%08lx>\n",
                      (long) current_gdbarch->deprecated_target_read_fp
                      /*DEPRECATED_TARGET_READ_FP ()*/);
#endif
#ifdef DEPRECATED_USE_GENERIC_DUMMY_FRAMES
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_USE_GENERIC_DUMMY_FRAMES # %s\n",
                      XSTRING (DEPRECATED_USE_GENERIC_DUMMY_FRAMES));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DEPRECATED_USE_GENERIC_DUMMY_FRAMES = %d\n",
                      DEPRECATED_USE_GENERIC_DUMMY_FRAMES);
#endif
#ifdef DWARF2_REG_TO_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DWARF2_REG_TO_REGNUM(dwarf2_regnr)",
                      XSTRING (DWARF2_REG_TO_REGNUM (dwarf2_regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DWARF2_REG_TO_REGNUM = <0x%08lx>\n",
                      (long) current_gdbarch->dwarf2_reg_to_regnum
                      /*DWARF2_REG_TO_REGNUM ()*/);
#endif
#ifdef DWARF_REG_TO_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "DWARF_REG_TO_REGNUM(dwarf_regnr)",
                      XSTRING (DWARF_REG_TO_REGNUM (dwarf_regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: DWARF_REG_TO_REGNUM = <0x%08lx>\n",
                      (long) current_gdbarch->dwarf_reg_to_regnum
                      /*DWARF_REG_TO_REGNUM ()*/);
#endif
#ifdef ECOFF_REG_TO_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ECOFF_REG_TO_REGNUM(ecoff_regnr)",
                      XSTRING (ECOFF_REG_TO_REGNUM (ecoff_regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ECOFF_REG_TO_REGNUM = <0x%08lx>\n",
                      (long) current_gdbarch->ecoff_reg_to_regnum
                      /*ECOFF_REG_TO_REGNUM ()*/);
#endif
#ifdef ELF_MAKE_MSYMBOL_SPECIAL
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "ELF_MAKE_MSYMBOL_SPECIAL(sym, msym)",
                      XSTRING (ELF_MAKE_MSYMBOL_SPECIAL (sym, msym)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: ELF_MAKE_MSYMBOL_SPECIAL = <0x%08lx>\n",
                      (long) current_gdbarch->elf_make_msymbol_special
                      /*ELF_MAKE_MSYMBOL_SPECIAL ()*/);
#endif
#ifdef EXTRACT_RETURN_VALUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "EXTRACT_RETURN_VALUE(type, regcache, valbuf)",
                      XSTRING (EXTRACT_RETURN_VALUE (type, regcache, valbuf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: EXTRACT_RETURN_VALUE = <0x%08lx>\n",
                      (long) current_gdbarch->extract_return_value
                      /*EXTRACT_RETURN_VALUE ()*/);
#endif
#ifdef FETCH_POINTER_ARGUMENT_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "FETCH_POINTER_ARGUMENT_P()",
                      XSTRING (FETCH_POINTER_ARGUMENT_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FETCH_POINTER_ARGUMENT_P() = %d\n",
                      FETCH_POINTER_ARGUMENT_P ());
#endif
#ifdef FETCH_POINTER_ARGUMENT
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "FETCH_POINTER_ARGUMENT(frame, argi, type)",
                      XSTRING (FETCH_POINTER_ARGUMENT (frame, argi, type)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FETCH_POINTER_ARGUMENT = <0x%08lx>\n",
                      (long) current_gdbarch->fetch_pointer_argument
                      /*FETCH_POINTER_ARGUMENT ()*/);
#endif
#ifdef FP0_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: FP0_REGNUM # %s\n",
                      XSTRING (FP0_REGNUM));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FP0_REGNUM = %d\n",
                      FP0_REGNUM);
#endif
#ifdef FRAME_ARGS_SKIP
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_ARGS_SKIP # %s\n",
                      XSTRING (FRAME_ARGS_SKIP));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_ARGS_SKIP = %ld\n",
                      (long) FRAME_ARGS_SKIP);
#endif
#ifdef FRAME_NUM_ARGS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "FRAME_NUM_ARGS_P()",
                      XSTRING (FRAME_NUM_ARGS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_NUM_ARGS_P() = %d\n",
                      FRAME_NUM_ARGS_P ());
#endif
#ifdef FRAME_NUM_ARGS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "FRAME_NUM_ARGS(frame)",
                      XSTRING (FRAME_NUM_ARGS (frame)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_NUM_ARGS = <0x%08lx>\n",
                      (long) current_gdbarch->frame_num_args
                      /*FRAME_NUM_ARGS ()*/);
#endif
#ifdef FRAME_RED_ZONE_SIZE
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_RED_ZONE_SIZE # %s\n",
                      XSTRING (FRAME_RED_ZONE_SIZE));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FRAME_RED_ZONE_SIZE = %d\n",
                      FRAME_RED_ZONE_SIZE);
#endif
#ifdef FUNCTION_START_OFFSET
  fprintf_unfiltered (file,
                      "gdbarch_dump: FUNCTION_START_OFFSET # %s\n",
                      XSTRING (FUNCTION_START_OFFSET));
  fprintf_unfiltered (file,
                      "gdbarch_dump: FUNCTION_START_OFFSET = %ld\n",
                      (long) FUNCTION_START_OFFSET);
#endif
#ifdef GET_LONGJMP_TARGET_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "GET_LONGJMP_TARGET_P()",
                      XSTRING (GET_LONGJMP_TARGET_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: GET_LONGJMP_TARGET_P() = %d\n",
                      GET_LONGJMP_TARGET_P ());
#endif
#ifdef GET_LONGJMP_TARGET
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "GET_LONGJMP_TARGET(pc)",
                      XSTRING (GET_LONGJMP_TARGET (pc)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: GET_LONGJMP_TARGET = <0x%08lx>\n",
                      (long) current_gdbarch->get_longjmp_target
                      /*GET_LONGJMP_TARGET ()*/);
#endif
#ifdef HAVE_NONSTEPPABLE_WATCHPOINT
  fprintf_unfiltered (file,
                      "gdbarch_dump: HAVE_NONSTEPPABLE_WATCHPOINT # %s\n",
                      XSTRING (HAVE_NONSTEPPABLE_WATCHPOINT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: HAVE_NONSTEPPABLE_WATCHPOINT = %d\n",
                      HAVE_NONSTEPPABLE_WATCHPOINT);
#endif
#ifdef INNER_THAN
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "INNER_THAN(lhs, rhs)",
                      XSTRING (INNER_THAN (lhs, rhs)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: INNER_THAN = <0x%08lx>\n",
                      (long) current_gdbarch->inner_than
                      /*INNER_THAN ()*/);
#endif
#ifdef INTEGER_TO_ADDRESS_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "INTEGER_TO_ADDRESS_P()",
                      XSTRING (INTEGER_TO_ADDRESS_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: INTEGER_TO_ADDRESS_P() = %d\n",
                      INTEGER_TO_ADDRESS_P ());
#endif
#ifdef INTEGER_TO_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "INTEGER_TO_ADDRESS(type, buf)",
                      XSTRING (INTEGER_TO_ADDRESS (type, buf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: INTEGER_TO_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->integer_to_address
                      /*INTEGER_TO_ADDRESS ()*/);
#endif
#ifdef IN_SOLIB_CALL_TRAMPOLINE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "IN_SOLIB_CALL_TRAMPOLINE(pc, name)",
                      XSTRING (IN_SOLIB_CALL_TRAMPOLINE (pc, name)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: IN_SOLIB_CALL_TRAMPOLINE = <0x%08lx>\n",
                      (long) current_gdbarch->in_solib_call_trampoline
                      /*IN_SOLIB_CALL_TRAMPOLINE ()*/);
#endif
#ifdef IN_SOLIB_RETURN_TRAMPOLINE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "IN_SOLIB_RETURN_TRAMPOLINE(pc, name)",
                      XSTRING (IN_SOLIB_RETURN_TRAMPOLINE (pc, name)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: IN_SOLIB_RETURN_TRAMPOLINE = <0x%08lx>\n",
                      (long) current_gdbarch->in_solib_return_trampoline
                      /*IN_SOLIB_RETURN_TRAMPOLINE ()*/);
#endif
#ifdef MEMORY_INSERT_BREAKPOINT
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "MEMORY_INSERT_BREAKPOINT(addr, contents_cache)",
                      XSTRING (MEMORY_INSERT_BREAKPOINT (addr, contents_cache)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: MEMORY_INSERT_BREAKPOINT = <0x%08lx>\n",
                      (long) current_gdbarch->memory_insert_breakpoint
                      /*MEMORY_INSERT_BREAKPOINT ()*/);
#endif
#ifdef MEMORY_REMOVE_BREAKPOINT
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "MEMORY_REMOVE_BREAKPOINT(addr, contents_cache)",
                      XSTRING (MEMORY_REMOVE_BREAKPOINT (addr, contents_cache)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: MEMORY_REMOVE_BREAKPOINT = <0x%08lx>\n",
                      (long) current_gdbarch->memory_remove_breakpoint
                      /*MEMORY_REMOVE_BREAKPOINT ()*/);
#endif
#ifdef NAME_OF_MALLOC
  fprintf_unfiltered (file,
                      "gdbarch_dump: NAME_OF_MALLOC # %s\n",
                      XSTRING (NAME_OF_MALLOC));
  fprintf_unfiltered (file,
                      "gdbarch_dump: NAME_OF_MALLOC = %s\n",
                      NAME_OF_MALLOC);
#endif
#ifdef NUM_PSEUDO_REGS
  fprintf_unfiltered (file,
                      "gdbarch_dump: NUM_PSEUDO_REGS # %s\n",
                      XSTRING (NUM_PSEUDO_REGS));
  fprintf_unfiltered (file,
                      "gdbarch_dump: NUM_PSEUDO_REGS = %d\n",
                      NUM_PSEUDO_REGS);
#endif
#ifdef NUM_REGS
  fprintf_unfiltered (file,
                      "gdbarch_dump: NUM_REGS # %s\n",
                      XSTRING (NUM_REGS));
  fprintf_unfiltered (file,
                      "gdbarch_dump: NUM_REGS = %d\n",
                      NUM_REGS);
#endif
#ifdef PARM_BOUNDARY
  fprintf_unfiltered (file,
                      "gdbarch_dump: PARM_BOUNDARY # %s\n",
                      XSTRING (PARM_BOUNDARY));
  fprintf_unfiltered (file,
                      "gdbarch_dump: PARM_BOUNDARY = %d\n",
                      PARM_BOUNDARY);
#endif
#ifdef PC_IN_SIGTRAMP
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "PC_IN_SIGTRAMP(pc, name)",
                      XSTRING (PC_IN_SIGTRAMP (pc, name)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: PC_IN_SIGTRAMP = <0x%08lx>\n",
                      (long) current_gdbarch->pc_in_sigtramp
                      /*PC_IN_SIGTRAMP ()*/);
#endif
#ifdef PC_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: PC_REGNUM # %s\n",
                      XSTRING (PC_REGNUM));
  fprintf_unfiltered (file,
                      "gdbarch_dump: PC_REGNUM = %d\n",
                      PC_REGNUM);
#endif
#ifdef POINTER_TO_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "POINTER_TO_ADDRESS(type, buf)",
                      XSTRING (POINTER_TO_ADDRESS (type, buf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: POINTER_TO_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->pointer_to_address
                      /*POINTER_TO_ADDRESS ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_print_float_info_p() = %d\n",
                      gdbarch_print_float_info_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: print_float_info = 0x%08lx\n",
                      (long) current_gdbarch->print_float_info);
  fprintf_unfiltered (file,
                      "gdbarch_dump: print_registers_info = 0x%08lx\n",
                      (long) current_gdbarch->print_registers_info);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_print_vector_info_p() = %d\n",
                      gdbarch_print_vector_info_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: print_vector_info = 0x%08lx\n",
                      (long) current_gdbarch->print_vector_info);
#ifdef PS_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: PS_REGNUM # %s\n",
                      XSTRING (PS_REGNUM));
  fprintf_unfiltered (file,
                      "gdbarch_dump: PS_REGNUM = %d\n",
                      PS_REGNUM);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_push_dummy_call_p() = %d\n",
                      gdbarch_push_dummy_call_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: push_dummy_call = 0x%08lx\n",
                      (long) current_gdbarch->push_dummy_call);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_push_dummy_code_p() = %d\n",
                      gdbarch_push_dummy_code_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: push_dummy_code = 0x%08lx\n",
                      (long) current_gdbarch->push_dummy_code);
#ifdef REGISTER_BYTES_OK_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "REGISTER_BYTES_OK_P()",
                      XSTRING (REGISTER_BYTES_OK_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: REGISTER_BYTES_OK_P() = %d\n",
                      REGISTER_BYTES_OK_P ());
#endif
#ifdef REGISTER_BYTES_OK
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "REGISTER_BYTES_OK(nr_bytes)",
                      XSTRING (REGISTER_BYTES_OK (nr_bytes)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: REGISTER_BYTES_OK = <0x%08lx>\n",
                      (long) current_gdbarch->register_bytes_ok
                      /*REGISTER_BYTES_OK ()*/);
#endif
#ifdef REGISTER_NAME
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "REGISTER_NAME(regnr)",
                      XSTRING (REGISTER_NAME (regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: REGISTER_NAME = <0x%08lx>\n",
                      (long) current_gdbarch->register_name
                      /*REGISTER_NAME ()*/);
#endif
#ifdef REGISTER_SIM_REGNO
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "REGISTER_SIM_REGNO(reg_nr)",
                      XSTRING (REGISTER_SIM_REGNO (reg_nr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: REGISTER_SIM_REGNO = <0x%08lx>\n",
                      (long) current_gdbarch->register_sim_regno
                      /*REGISTER_SIM_REGNO ()*/);
#endif
#ifdef REGISTER_TO_VALUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "REGISTER_TO_VALUE(frame, regnum, type, buf)",
                      XSTRING (REGISTER_TO_VALUE (frame, regnum, type, buf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: REGISTER_TO_VALUE = <0x%08lx>\n",
                      (long) current_gdbarch->register_to_value
                      /*REGISTER_TO_VALUE ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_register_type_p() = %d\n",
                      gdbarch_register_type_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: register_type = 0x%08lx\n",
                      (long) current_gdbarch->register_type);
  fprintf_unfiltered (file,
                      "gdbarch_dump: remote_translate_xfer_address = 0x%08lx\n",
                      (long) current_gdbarch->remote_translate_xfer_address);
#ifdef RETURN_VALUE_ON_STACK
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "RETURN_VALUE_ON_STACK(type)",
                      XSTRING (RETURN_VALUE_ON_STACK (type)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: RETURN_VALUE_ON_STACK = <0x%08lx>\n",
                      (long) current_gdbarch->return_value_on_stack
                      /*RETURN_VALUE_ON_STACK ()*/);
#endif
#ifdef SDB_REG_TO_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SDB_REG_TO_REGNUM(sdb_regnr)",
                      XSTRING (SDB_REG_TO_REGNUM (sdb_regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SDB_REG_TO_REGNUM = <0x%08lx>\n",
                      (long) current_gdbarch->sdb_reg_to_regnum
                      /*SDB_REG_TO_REGNUM ()*/);
#endif
#ifdef SIGTRAMP_END_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SIGTRAMP_END_P()",
                      XSTRING (SIGTRAMP_END_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SIGTRAMP_END_P() = %d\n",
                      SIGTRAMP_END_P ());
#endif
#ifdef SIGTRAMP_END
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SIGTRAMP_END(pc)",
                      XSTRING (SIGTRAMP_END (pc)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SIGTRAMP_END = <0x%08lx>\n",
                      (long) current_gdbarch->sigtramp_end
                      /*SIGTRAMP_END ()*/);
#endif
#ifdef SIGTRAMP_START_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SIGTRAMP_START_P()",
                      XSTRING (SIGTRAMP_START_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SIGTRAMP_START_P() = %d\n",
                      SIGTRAMP_START_P ());
#endif
#ifdef SIGTRAMP_START
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SIGTRAMP_START(pc)",
                      XSTRING (SIGTRAMP_START (pc)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SIGTRAMP_START = <0x%08lx>\n",
                      (long) current_gdbarch->sigtramp_start
                      /*SIGTRAMP_START ()*/);
#endif
#ifdef SKIP_PROLOGUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SKIP_PROLOGUE(ip)",
                      XSTRING (SKIP_PROLOGUE (ip)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SKIP_PROLOGUE = <0x%08lx>\n",
                      (long) current_gdbarch->skip_prologue
                      /*SKIP_PROLOGUE ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: skip_solib_resolver = 0x%08lx\n",
                      (long) current_gdbarch->skip_solib_resolver);
#ifdef SKIP_TRAMPOLINE_CODE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SKIP_TRAMPOLINE_CODE(pc)",
                      XSTRING (SKIP_TRAMPOLINE_CODE (pc)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SKIP_TRAMPOLINE_CODE = <0x%08lx>\n",
                      (long) current_gdbarch->skip_trampoline_code
                      /*SKIP_TRAMPOLINE_CODE ()*/);
#endif
#ifdef SMASH_TEXT_ADDRESS
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SMASH_TEXT_ADDRESS(addr)",
                      XSTRING (SMASH_TEXT_ADDRESS (addr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SMASH_TEXT_ADDRESS = <0x%08lx>\n",
                      (long) current_gdbarch->smash_text_address
                      /*SMASH_TEXT_ADDRESS ()*/);
#endif
#ifdef SOFTWARE_SINGLE_STEP_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SOFTWARE_SINGLE_STEP_P()",
                      XSTRING (SOFTWARE_SINGLE_STEP_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SOFTWARE_SINGLE_STEP_P() = %d\n",
                      SOFTWARE_SINGLE_STEP_P ());
#endif
#ifdef SOFTWARE_SINGLE_STEP
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "SOFTWARE_SINGLE_STEP(sig, insert_breakpoints_p)",
                      XSTRING (SOFTWARE_SINGLE_STEP (sig, insert_breakpoints_p)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SOFTWARE_SINGLE_STEP = <0x%08lx>\n",
                      (long) current_gdbarch->software_single_step
                      /*SOFTWARE_SINGLE_STEP ()*/);
#endif
#ifdef SP_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: SP_REGNUM # %s\n",
                      XSTRING (SP_REGNUM));
  fprintf_unfiltered (file,
                      "gdbarch_dump: SP_REGNUM = %d\n",
                      SP_REGNUM);
#endif
#ifdef STAB_REG_TO_REGNUM
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "STAB_REG_TO_REGNUM(stab_regnr)",
                      XSTRING (STAB_REG_TO_REGNUM (stab_regnr)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: STAB_REG_TO_REGNUM = <0x%08lx>\n",
                      (long) current_gdbarch->stab_reg_to_regnum
                      /*STAB_REG_TO_REGNUM ()*/);
#endif
#ifdef STORE_RETURN_VALUE
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "STORE_RETURN_VALUE(type, regcache, valbuf)",
                      XSTRING (STORE_RETURN_VALUE (type, regcache, valbuf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: STORE_RETURN_VALUE = <0x%08lx>\n",
                      (long) current_gdbarch->store_return_value
                      /*STORE_RETURN_VALUE ()*/);
#endif
#ifdef TARGET_ADDR_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_ADDR_BIT # %s\n",
                      XSTRING (TARGET_ADDR_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_ADDR_BIT = %d\n",
                      TARGET_ADDR_BIT);
#endif
#ifdef TARGET_ARCHITECTURE
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_ARCHITECTURE # %s\n",
                      XSTRING (TARGET_ARCHITECTURE));
  if (TARGET_ARCHITECTURE != NULL)
    fprintf_unfiltered (file,
                        "gdbarch_dump: TARGET_ARCHITECTURE = %s\n",
                        TARGET_ARCHITECTURE->printable_name);
#endif
#ifdef TARGET_BFD_VMA_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_BFD_VMA_BIT # %s\n",
                      XSTRING (TARGET_BFD_VMA_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_BFD_VMA_BIT = %d\n",
                      TARGET_BFD_VMA_BIT);
#endif
#ifdef TARGET_BYTE_ORDER
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_BYTE_ORDER # %s\n",
                      XSTRING (TARGET_BYTE_ORDER));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_BYTE_ORDER = %ld\n",
                      (long) TARGET_BYTE_ORDER);
#endif
#ifdef TARGET_CHAR_SIGNED
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_CHAR_SIGNED # %s\n",
                      XSTRING (TARGET_CHAR_SIGNED));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_CHAR_SIGNED = %d\n",
                      TARGET_CHAR_SIGNED);
#endif
#ifdef TARGET_DOUBLE_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_DOUBLE_BIT # %s\n",
                      XSTRING (TARGET_DOUBLE_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_DOUBLE_BIT = %d\n",
                      TARGET_DOUBLE_BIT);
#endif
#ifdef TARGET_DOUBLE_FORMAT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_DOUBLE_FORMAT # %s\n",
                      XSTRING (TARGET_DOUBLE_FORMAT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_DOUBLE_FORMAT = %s\n",
                      (TARGET_DOUBLE_FORMAT)->name);
#endif
#ifdef TARGET_FLOAT_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_FLOAT_BIT # %s\n",
                      XSTRING (TARGET_FLOAT_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_FLOAT_BIT = %d\n",
                      TARGET_FLOAT_BIT);
#endif
#ifdef TARGET_FLOAT_FORMAT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_FLOAT_FORMAT # %s\n",
                      XSTRING (TARGET_FLOAT_FORMAT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_FLOAT_FORMAT = %s\n",
                      (TARGET_FLOAT_FORMAT)->name);
#endif
#ifdef TARGET_INT_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_INT_BIT # %s\n",
                      XSTRING (TARGET_INT_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_INT_BIT = %d\n",
                      TARGET_INT_BIT);
#endif
#ifdef TARGET_LONG_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_BIT # %s\n",
                      XSTRING (TARGET_LONG_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_BIT = %d\n",
                      TARGET_LONG_BIT);
#endif
#ifdef TARGET_LONG_DOUBLE_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_DOUBLE_BIT # %s\n",
                      XSTRING (TARGET_LONG_DOUBLE_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_DOUBLE_BIT = %d\n",
                      TARGET_LONG_DOUBLE_BIT);
#endif
#ifdef TARGET_LONG_DOUBLE_FORMAT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_DOUBLE_FORMAT # %s\n",
                      XSTRING (TARGET_LONG_DOUBLE_FORMAT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_DOUBLE_FORMAT = %s\n",
                      (TARGET_LONG_DOUBLE_FORMAT)->name);
#endif
#ifdef TARGET_LONG_LONG_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_LONG_BIT # %s\n",
                      XSTRING (TARGET_LONG_LONG_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_LONG_LONG_BIT = %d\n",
                      TARGET_LONG_LONG_BIT);
#endif
#ifdef TARGET_OSABI
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_OSABI # %s\n",
                      XSTRING (TARGET_OSABI));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_OSABI = %ld\n",
                      (long) TARGET_OSABI);
#endif
#ifdef TARGET_PRINT_INSN
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_PRINT_INSN(vma, info)",
                      XSTRING (TARGET_PRINT_INSN (vma, info)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_PRINT_INSN = <0x%08lx>\n",
                      (long) current_gdbarch->print_insn
                      /*TARGET_PRINT_INSN ()*/);
#endif
#ifdef TARGET_PTR_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_PTR_BIT # %s\n",
                      XSTRING (TARGET_PTR_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_PTR_BIT = %d\n",
                      TARGET_PTR_BIT);
#endif
#ifdef TARGET_READ_PC_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_READ_PC_P()",
                      XSTRING (TARGET_READ_PC_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_READ_PC_P() = %d\n",
                      TARGET_READ_PC_P ());
#endif
#ifdef TARGET_READ_PC
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_READ_PC(ptid)",
                      XSTRING (TARGET_READ_PC (ptid)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_READ_PC = <0x%08lx>\n",
                      (long) current_gdbarch->read_pc
                      /*TARGET_READ_PC ()*/);
#endif
#ifdef TARGET_READ_SP_P
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_READ_SP_P()",
                      XSTRING (TARGET_READ_SP_P ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_READ_SP_P() = %d\n",
                      TARGET_READ_SP_P ());
#endif
#ifdef TARGET_READ_SP
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_READ_SP()",
                      XSTRING (TARGET_READ_SP ()));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_READ_SP = <0x%08lx>\n",
                      (long) current_gdbarch->read_sp
                      /*TARGET_READ_SP ()*/);
#endif
#ifdef TARGET_SHORT_BIT
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_SHORT_BIT # %s\n",
                      XSTRING (TARGET_SHORT_BIT));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_SHORT_BIT = %d\n",
                      TARGET_SHORT_BIT);
#endif
#ifdef TARGET_VIRTUAL_FRAME_POINTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_VIRTUAL_FRAME_POINTER(pc, frame_regnum, frame_offset)",
                      XSTRING (TARGET_VIRTUAL_FRAME_POINTER (pc, frame_regnum, frame_offset)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_VIRTUAL_FRAME_POINTER = <0x%08lx>\n",
                      (long) current_gdbarch->virtual_frame_pointer
                      /*TARGET_VIRTUAL_FRAME_POINTER ()*/);
#endif
#ifdef TARGET_WRITE_PC
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "TARGET_WRITE_PC(val, ptid)",
                      XSTRING (TARGET_WRITE_PC (val, ptid)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: TARGET_WRITE_PC = <0x%08lx>\n",
                      (long) current_gdbarch->write_pc
                      /*TARGET_WRITE_PC ()*/);
#endif
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_unwind_dummy_id_p() = %d\n",
                      gdbarch_unwind_dummy_id_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: unwind_dummy_id = 0x%08lx\n",
                      (long) current_gdbarch->unwind_dummy_id);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_unwind_pc_p() = %d\n",
                      gdbarch_unwind_pc_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: unwind_pc = 0x%08lx\n",
                      (long) current_gdbarch->unwind_pc);
  fprintf_unfiltered (file,
                      "gdbarch_dump: gdbarch_unwind_sp_p() = %d\n",
                      gdbarch_unwind_sp_p (current_gdbarch));
  fprintf_unfiltered (file,
                      "gdbarch_dump: unwind_sp = 0x%08lx\n",
                      (long) current_gdbarch->unwind_sp);
#ifdef USE_STRUCT_CONVENTION
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "USE_STRUCT_CONVENTION(gcc_p, value_type)",
                      XSTRING (USE_STRUCT_CONVENTION (gcc_p, value_type)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: USE_STRUCT_CONVENTION = <0x%08lx>\n",
                      (long) current_gdbarch->use_struct_convention
                      /*USE_STRUCT_CONVENTION ()*/);
#endif
#ifdef VALUE_TO_REGISTER
  fprintf_unfiltered (file,
                      "gdbarch_dump: %s # %s\n",
                      "VALUE_TO_REGISTER(frame, regnum, type, buf)",
                      XSTRING (VALUE_TO_REGISTER (frame, regnum, type, buf)));
  fprintf_unfiltered (file,
                      "gdbarch_dump: VALUE_TO_REGISTER = <0x%08lx>\n",
                      (long) current_gdbarch->value_to_register
                      /*VALUE_TO_REGISTER ()*/);
#endif
  if (current_gdbarch->dump_tdep != NULL)
    current_gdbarch->dump_tdep (current_gdbarch, file);
}

struct gdbarch_tdep *
gdbarch_tdep (struct gdbarch *gdbarch)
{
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_tdep called\n");
  return gdbarch->tdep;
}


const struct bfd_arch_info *
gdbarch_bfd_arch_info (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_bfd_arch_info called\n");
  return gdbarch->bfd_arch_info;
}

int
gdbarch_byte_order (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_byte_order called\n");
  return gdbarch->byte_order;
}

enum gdb_osabi
gdbarch_osabi (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_osabi called\n");
  return gdbarch->osabi;
}

int
gdbarch_short_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of short_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_short_bit called\n");
  return gdbarch->short_bit;
}

void
set_gdbarch_short_bit (struct gdbarch *gdbarch,
                       int short_bit)
{
  gdbarch->short_bit = short_bit;
}

int
gdbarch_int_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of int_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_int_bit called\n");
  return gdbarch->int_bit;
}

void
set_gdbarch_int_bit (struct gdbarch *gdbarch,
                     int int_bit)
{
  gdbarch->int_bit = int_bit;
}

int
gdbarch_long_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of long_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_long_bit called\n");
  return gdbarch->long_bit;
}

void
set_gdbarch_long_bit (struct gdbarch *gdbarch,
                      int long_bit)
{
  gdbarch->long_bit = long_bit;
}

int
gdbarch_long_long_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of long_long_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_long_long_bit called\n");
  return gdbarch->long_long_bit;
}

void
set_gdbarch_long_long_bit (struct gdbarch *gdbarch,
                           int long_long_bit)
{
  gdbarch->long_long_bit = long_long_bit;
}

int
gdbarch_float_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of float_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_float_bit called\n");
  return gdbarch->float_bit;
}

void
set_gdbarch_float_bit (struct gdbarch *gdbarch,
                       int float_bit)
{
  gdbarch->float_bit = float_bit;
}

int
gdbarch_double_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of double_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_double_bit called\n");
  return gdbarch->double_bit;
}

void
set_gdbarch_double_bit (struct gdbarch *gdbarch,
                        int double_bit)
{
  gdbarch->double_bit = double_bit;
}

int
gdbarch_long_double_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of long_double_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_long_double_bit called\n");
  return gdbarch->long_double_bit;
}

void
set_gdbarch_long_double_bit (struct gdbarch *gdbarch,
                             int long_double_bit)
{
  gdbarch->long_double_bit = long_double_bit;
}

int
gdbarch_ptr_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of ptr_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_ptr_bit called\n");
  return gdbarch->ptr_bit;
}

void
set_gdbarch_ptr_bit (struct gdbarch *gdbarch,
                     int ptr_bit)
{
  gdbarch->ptr_bit = ptr_bit;
}

int
gdbarch_addr_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Check variable changed from pre-default.  */
  gdb_assert (gdbarch->addr_bit != 0);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_addr_bit called\n");
  return gdbarch->addr_bit;
}

void
set_gdbarch_addr_bit (struct gdbarch *gdbarch,
                      int addr_bit)
{
  gdbarch->addr_bit = addr_bit;
}

int
gdbarch_bfd_vma_bit (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of bfd_vma_bit, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_bfd_vma_bit called\n");
  return gdbarch->bfd_vma_bit;
}

void
set_gdbarch_bfd_vma_bit (struct gdbarch *gdbarch,
                         int bfd_vma_bit)
{
  gdbarch->bfd_vma_bit = bfd_vma_bit;
}

int
gdbarch_char_signed (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Check variable changed from pre-default.  */
  gdb_assert (gdbarch->char_signed != -1);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_char_signed called\n");
  return gdbarch->char_signed;
}

void
set_gdbarch_char_signed (struct gdbarch *gdbarch,
                         int char_signed)
{
  gdbarch->char_signed = char_signed;
}

int
gdbarch_read_pc_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->read_pc != NULL;
}

CORE_ADDR
gdbarch_read_pc (struct gdbarch *gdbarch, ptid_t ptid)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->read_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_read_pc called\n");
  return gdbarch->read_pc (ptid);
}

void
set_gdbarch_read_pc (struct gdbarch *gdbarch,
                     gdbarch_read_pc_ftype read_pc)
{
  gdbarch->read_pc = read_pc;
}

void
gdbarch_write_pc (struct gdbarch *gdbarch, CORE_ADDR val, ptid_t ptid)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->write_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_write_pc called\n");
  gdbarch->write_pc (val, ptid);
}

void
set_gdbarch_write_pc (struct gdbarch *gdbarch,
                      gdbarch_write_pc_ftype write_pc)
{
  gdbarch->write_pc = write_pc;
}

int
gdbarch_read_sp_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->read_sp != NULL;
}

CORE_ADDR
gdbarch_read_sp (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->read_sp != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_read_sp called\n");
  return gdbarch->read_sp ();
}

void
set_gdbarch_read_sp (struct gdbarch *gdbarch,
                     gdbarch_read_sp_ftype read_sp)
{
  gdbarch->read_sp = read_sp;
}

void
gdbarch_virtual_frame_pointer (struct gdbarch *gdbarch, CORE_ADDR pc, int *frame_regnum, LONGEST *frame_offset)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->virtual_frame_pointer != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_virtual_frame_pointer called\n");
  gdbarch->virtual_frame_pointer (pc, frame_regnum, frame_offset);
}

void
set_gdbarch_virtual_frame_pointer (struct gdbarch *gdbarch,
                                   gdbarch_virtual_frame_pointer_ftype virtual_frame_pointer)
{
  gdbarch->virtual_frame_pointer = virtual_frame_pointer;
}

int
gdbarch_pseudo_register_read_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->pseudo_register_read != NULL;
}

void
gdbarch_pseudo_register_read (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->pseudo_register_read != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_pseudo_register_read called\n");
  gdbarch->pseudo_register_read (gdbarch, regcache, cookednum, buf);
}

void
set_gdbarch_pseudo_register_read (struct gdbarch *gdbarch,
                                  gdbarch_pseudo_register_read_ftype pseudo_register_read)
{
  gdbarch->pseudo_register_read = pseudo_register_read;
}

int
gdbarch_pseudo_register_write_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->pseudo_register_write != NULL;
}

void
gdbarch_pseudo_register_write (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, const void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->pseudo_register_write != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_pseudo_register_write called\n");
  gdbarch->pseudo_register_write (gdbarch, regcache, cookednum, buf);
}

void
set_gdbarch_pseudo_register_write (struct gdbarch *gdbarch,
                                   gdbarch_pseudo_register_write_ftype pseudo_register_write)
{
  gdbarch->pseudo_register_write = pseudo_register_write;
}

int
gdbarch_num_regs (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Check variable changed from pre-default.  */
  gdb_assert (gdbarch->num_regs != -1);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_num_regs called\n");
  return gdbarch->num_regs;
}

void
set_gdbarch_num_regs (struct gdbarch *gdbarch,
                      int num_regs)
{
  gdbarch->num_regs = num_regs;
}

int
gdbarch_num_pseudo_regs (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of num_pseudo_regs, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_num_pseudo_regs called\n");
  return gdbarch->num_pseudo_regs;
}

void
set_gdbarch_num_pseudo_regs (struct gdbarch *gdbarch,
                             int num_pseudo_regs)
{
  gdbarch->num_pseudo_regs = num_pseudo_regs;
}

int
gdbarch_sp_regnum (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of sp_regnum, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_sp_regnum called\n");
  return gdbarch->sp_regnum;
}

void
set_gdbarch_sp_regnum (struct gdbarch *gdbarch,
                       int sp_regnum)
{
  gdbarch->sp_regnum = sp_regnum;
}

int
gdbarch_pc_regnum (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of pc_regnum, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_pc_regnum called\n");
  return gdbarch->pc_regnum;
}

void
set_gdbarch_pc_regnum (struct gdbarch *gdbarch,
                       int pc_regnum)
{
  gdbarch->pc_regnum = pc_regnum;
}

int
gdbarch_ps_regnum (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of ps_regnum, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_ps_regnum called\n");
  return gdbarch->ps_regnum;
}

void
set_gdbarch_ps_regnum (struct gdbarch *gdbarch,
                       int ps_regnum)
{
  gdbarch->ps_regnum = ps_regnum;
}

int
gdbarch_fp0_regnum (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of fp0_regnum, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_fp0_regnum called\n");
  return gdbarch->fp0_regnum;
}

void
set_gdbarch_fp0_regnum (struct gdbarch *gdbarch,
                        int fp0_regnum)
{
  gdbarch->fp0_regnum = fp0_regnum;
}

int
gdbarch_stab_reg_to_regnum (struct gdbarch *gdbarch, int stab_regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->stab_reg_to_regnum != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_stab_reg_to_regnum called\n");
  return gdbarch->stab_reg_to_regnum (stab_regnr);
}

void
set_gdbarch_stab_reg_to_regnum (struct gdbarch *gdbarch,
                                gdbarch_stab_reg_to_regnum_ftype stab_reg_to_regnum)
{
  gdbarch->stab_reg_to_regnum = stab_reg_to_regnum;
}

int
gdbarch_ecoff_reg_to_regnum (struct gdbarch *gdbarch, int ecoff_regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->ecoff_reg_to_regnum != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_ecoff_reg_to_regnum called\n");
  return gdbarch->ecoff_reg_to_regnum (ecoff_regnr);
}

void
set_gdbarch_ecoff_reg_to_regnum (struct gdbarch *gdbarch,
                                 gdbarch_ecoff_reg_to_regnum_ftype ecoff_reg_to_regnum)
{
  gdbarch->ecoff_reg_to_regnum = ecoff_reg_to_regnum;
}

int
gdbarch_dwarf_reg_to_regnum (struct gdbarch *gdbarch, int dwarf_regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->dwarf_reg_to_regnum != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_dwarf_reg_to_regnum called\n");
  return gdbarch->dwarf_reg_to_regnum (dwarf_regnr);
}

void
set_gdbarch_dwarf_reg_to_regnum (struct gdbarch *gdbarch,
                                 gdbarch_dwarf_reg_to_regnum_ftype dwarf_reg_to_regnum)
{
  gdbarch->dwarf_reg_to_regnum = dwarf_reg_to_regnum;
}

int
gdbarch_sdb_reg_to_regnum (struct gdbarch *gdbarch, int sdb_regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->sdb_reg_to_regnum != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_sdb_reg_to_regnum called\n");
  return gdbarch->sdb_reg_to_regnum (sdb_regnr);
}

void
set_gdbarch_sdb_reg_to_regnum (struct gdbarch *gdbarch,
                               gdbarch_sdb_reg_to_regnum_ftype sdb_reg_to_regnum)
{
  gdbarch->sdb_reg_to_regnum = sdb_reg_to_regnum;
}

int
gdbarch_dwarf2_reg_to_regnum (struct gdbarch *gdbarch, int dwarf2_regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->dwarf2_reg_to_regnum != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_dwarf2_reg_to_regnum called\n");
  return gdbarch->dwarf2_reg_to_regnum (dwarf2_regnr);
}

void
set_gdbarch_dwarf2_reg_to_regnum (struct gdbarch *gdbarch,
                                  gdbarch_dwarf2_reg_to_regnum_ftype dwarf2_reg_to_regnum)
{
  gdbarch->dwarf2_reg_to_regnum = dwarf2_reg_to_regnum;
}

const char *
gdbarch_register_name (struct gdbarch *gdbarch, int regnr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_name != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_name called\n");
  return gdbarch->register_name (regnr);
}

void
set_gdbarch_register_name (struct gdbarch *gdbarch,
                           gdbarch_register_name_ftype register_name)
{
  gdbarch->register_name = register_name;
}

int
gdbarch_register_type_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->register_type != NULL;
}

struct type *
gdbarch_register_type (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_type != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_type called\n");
  return gdbarch->register_type (gdbarch, reg_nr);
}

void
set_gdbarch_register_type (struct gdbarch *gdbarch,
                           gdbarch_register_type_ftype register_type)
{
  gdbarch->register_type = register_type;
}

int
gdbarch_deprecated_register_virtual_type_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_register_virtual_type != NULL;
}

struct type *
gdbarch_deprecated_register_virtual_type (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_virtual_type != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_virtual_type called\n");
  return gdbarch->deprecated_register_virtual_type (reg_nr);
}

void
set_gdbarch_deprecated_register_virtual_type (struct gdbarch *gdbarch,
                                              gdbarch_deprecated_register_virtual_type_ftype deprecated_register_virtual_type)
{
  gdbarch->deprecated_register_virtual_type = deprecated_register_virtual_type;
}

int
gdbarch_deprecated_register_bytes (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_bytes called\n");
  return gdbarch->deprecated_register_bytes;
}

void
set_gdbarch_deprecated_register_bytes (struct gdbarch *gdbarch,
                                       int deprecated_register_bytes)
{
  gdbarch->deprecated_register_bytes = deprecated_register_bytes;
}

int
gdbarch_deprecated_register_byte_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_register_byte != generic_register_byte;
}

int
gdbarch_deprecated_register_byte (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_byte != NULL);
  /* Do not check predicate: gdbarch->deprecated_register_byte != generic_register_byte, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_byte called\n");
  return gdbarch->deprecated_register_byte (reg_nr);
}

void
set_gdbarch_deprecated_register_byte (struct gdbarch *gdbarch,
                                      gdbarch_deprecated_register_byte_ftype deprecated_register_byte)
{
  gdbarch->deprecated_register_byte = deprecated_register_byte;
}

int
gdbarch_deprecated_register_raw_size_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_register_raw_size != generic_register_size;
}

int
gdbarch_deprecated_register_raw_size (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_raw_size != NULL);
  /* Do not check predicate: gdbarch->deprecated_register_raw_size != generic_register_size, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_raw_size called\n");
  return gdbarch->deprecated_register_raw_size (reg_nr);
}

void
set_gdbarch_deprecated_register_raw_size (struct gdbarch *gdbarch,
                                          gdbarch_deprecated_register_raw_size_ftype deprecated_register_raw_size)
{
  gdbarch->deprecated_register_raw_size = deprecated_register_raw_size;
}

int
gdbarch_deprecated_register_virtual_size_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_register_virtual_size != generic_register_size;
}

int
gdbarch_deprecated_register_virtual_size (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_virtual_size != NULL);
  /* Do not check predicate: gdbarch->deprecated_register_virtual_size != generic_register_size, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_virtual_size called\n");
  return gdbarch->deprecated_register_virtual_size (reg_nr);
}

void
set_gdbarch_deprecated_register_virtual_size (struct gdbarch *gdbarch,
                                              gdbarch_deprecated_register_virtual_size_ftype deprecated_register_virtual_size)
{
  gdbarch->deprecated_register_virtual_size = deprecated_register_virtual_size;
}

int
gdbarch_deprecated_max_register_raw_size_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_max_register_raw_size != 0;
}

int
gdbarch_deprecated_max_register_raw_size (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_max_register_raw_size called\n");
  return gdbarch->deprecated_max_register_raw_size;
}

void
set_gdbarch_deprecated_max_register_raw_size (struct gdbarch *gdbarch,
                                              int deprecated_max_register_raw_size)
{
  gdbarch->deprecated_max_register_raw_size = deprecated_max_register_raw_size;
}

int
gdbarch_deprecated_max_register_virtual_size_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_max_register_virtual_size != 0;
}

int
gdbarch_deprecated_max_register_virtual_size (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_max_register_virtual_size called\n");
  return gdbarch->deprecated_max_register_virtual_size;
}

void
set_gdbarch_deprecated_max_register_virtual_size (struct gdbarch *gdbarch,
                                                  int deprecated_max_register_virtual_size)
{
  gdbarch->deprecated_max_register_virtual_size = deprecated_max_register_virtual_size;
}

int
gdbarch_unwind_dummy_id_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->unwind_dummy_id != NULL;
}

struct frame_id
gdbarch_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *info)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->unwind_dummy_id != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_unwind_dummy_id called\n");
  return gdbarch->unwind_dummy_id (gdbarch, info);
}

void
set_gdbarch_unwind_dummy_id (struct gdbarch *gdbarch,
                             gdbarch_unwind_dummy_id_ftype unwind_dummy_id)
{
  gdbarch->unwind_dummy_id = unwind_dummy_id;
}

int
gdbarch_deprecated_save_dummy_frame_tos_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_save_dummy_frame_tos != NULL;
}

void
gdbarch_deprecated_save_dummy_frame_tos (struct gdbarch *gdbarch, CORE_ADDR sp)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_save_dummy_frame_tos != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_save_dummy_frame_tos called\n");
  gdbarch->deprecated_save_dummy_frame_tos (sp);
}

void
set_gdbarch_deprecated_save_dummy_frame_tos (struct gdbarch *gdbarch,
                                             gdbarch_deprecated_save_dummy_frame_tos_ftype deprecated_save_dummy_frame_tos)
{
  gdbarch->deprecated_save_dummy_frame_tos = deprecated_save_dummy_frame_tos;
}

int
gdbarch_deprecated_fp_regnum (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of deprecated_fp_regnum, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_fp_regnum called\n");
  return gdbarch->deprecated_fp_regnum;
}

void
set_gdbarch_deprecated_fp_regnum (struct gdbarch *gdbarch,
                                  int deprecated_fp_regnum)
{
  gdbarch->deprecated_fp_regnum = deprecated_fp_regnum;
}

int
gdbarch_deprecated_target_read_fp_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_target_read_fp != NULL;
}

CORE_ADDR
gdbarch_deprecated_target_read_fp (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_target_read_fp != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_target_read_fp called\n");
  return gdbarch->deprecated_target_read_fp ();
}

void
set_gdbarch_deprecated_target_read_fp (struct gdbarch *gdbarch,
                                       gdbarch_deprecated_target_read_fp_ftype deprecated_target_read_fp)
{
  gdbarch->deprecated_target_read_fp = deprecated_target_read_fp;
}

int
gdbarch_push_dummy_call_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->push_dummy_call != NULL;
}

CORE_ADDR
gdbarch_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr, struct regcache *regcache, CORE_ADDR bp_addr, int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->push_dummy_call != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_push_dummy_call called\n");
  return gdbarch->push_dummy_call (gdbarch, func_addr, regcache, bp_addr, nargs, args, sp, struct_return, struct_addr);
}

void
set_gdbarch_push_dummy_call (struct gdbarch *gdbarch,
                             gdbarch_push_dummy_call_ftype push_dummy_call)
{
  gdbarch->push_dummy_call = push_dummy_call;
}

int
gdbarch_deprecated_push_arguments_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_push_arguments != NULL;
}

CORE_ADDR
gdbarch_deprecated_push_arguments (struct gdbarch *gdbarch, int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_push_arguments != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_push_arguments called\n");
  return gdbarch->deprecated_push_arguments (nargs, args, sp, struct_return, struct_addr);
}

void
set_gdbarch_deprecated_push_arguments (struct gdbarch *gdbarch,
                                       gdbarch_deprecated_push_arguments_ftype deprecated_push_arguments)
{
  gdbarch->deprecated_push_arguments = deprecated_push_arguments;
}

int
gdbarch_deprecated_use_generic_dummy_frames (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of deprecated_use_generic_dummy_frames, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_use_generic_dummy_frames called\n");
  return gdbarch->deprecated_use_generic_dummy_frames;
}

void
set_gdbarch_deprecated_use_generic_dummy_frames (struct gdbarch *gdbarch,
                                                 int deprecated_use_generic_dummy_frames)
{
  gdbarch->deprecated_use_generic_dummy_frames = deprecated_use_generic_dummy_frames;
}

int
gdbarch_deprecated_push_return_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_push_return_address != NULL;
}

CORE_ADDR
gdbarch_deprecated_push_return_address (struct gdbarch *gdbarch, CORE_ADDR pc, CORE_ADDR sp)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_push_return_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_push_return_address called\n");
  return gdbarch->deprecated_push_return_address (pc, sp);
}

void
set_gdbarch_deprecated_push_return_address (struct gdbarch *gdbarch,
                                            gdbarch_deprecated_push_return_address_ftype deprecated_push_return_address)
{
  gdbarch->deprecated_push_return_address = deprecated_push_return_address;
}

int
gdbarch_deprecated_dummy_write_sp_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_dummy_write_sp != NULL;
}

void
gdbarch_deprecated_dummy_write_sp (struct gdbarch *gdbarch, CORE_ADDR val)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_dummy_write_sp != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_dummy_write_sp called\n");
  gdbarch->deprecated_dummy_write_sp (val);
}

void
set_gdbarch_deprecated_dummy_write_sp (struct gdbarch *gdbarch,
                                       gdbarch_deprecated_dummy_write_sp_ftype deprecated_dummy_write_sp)
{
  gdbarch->deprecated_dummy_write_sp = deprecated_dummy_write_sp;
}

int
gdbarch_deprecated_register_size (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_size called\n");
  return gdbarch->deprecated_register_size;
}

void
set_gdbarch_deprecated_register_size (struct gdbarch *gdbarch,
                                      int deprecated_register_size)
{
  gdbarch->deprecated_register_size = deprecated_register_size;
}

int
gdbarch_call_dummy_location (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of call_dummy_location, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_call_dummy_location called\n");
  return gdbarch->call_dummy_location;
}

void
set_gdbarch_call_dummy_location (struct gdbarch *gdbarch,
                                 int call_dummy_location)
{
  gdbarch->call_dummy_location = call_dummy_location;
}

CORE_ADDR
gdbarch_deprecated_call_dummy_start_offset (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_call_dummy_start_offset called\n");
  return gdbarch->deprecated_call_dummy_start_offset;
}

void
set_gdbarch_deprecated_call_dummy_start_offset (struct gdbarch *gdbarch,
                                                CORE_ADDR deprecated_call_dummy_start_offset)
{
  gdbarch->deprecated_call_dummy_start_offset = deprecated_call_dummy_start_offset;
}

CORE_ADDR
gdbarch_deprecated_call_dummy_breakpoint_offset (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_call_dummy_breakpoint_offset called\n");
  return gdbarch->deprecated_call_dummy_breakpoint_offset;
}

void
set_gdbarch_deprecated_call_dummy_breakpoint_offset (struct gdbarch *gdbarch,
                                                     CORE_ADDR deprecated_call_dummy_breakpoint_offset)
{
  gdbarch->deprecated_call_dummy_breakpoint_offset = deprecated_call_dummy_breakpoint_offset;
}

int
gdbarch_deprecated_call_dummy_length (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_call_dummy_length called\n");
  return gdbarch->deprecated_call_dummy_length;
}

void
set_gdbarch_deprecated_call_dummy_length (struct gdbarch *gdbarch,
                                          int deprecated_call_dummy_length)
{
  gdbarch->deprecated_call_dummy_length = deprecated_call_dummy_length;
}

LONGEST *
gdbarch_deprecated_call_dummy_words (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of deprecated_call_dummy_words, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_call_dummy_words called\n");
  return gdbarch->deprecated_call_dummy_words;
}

void
set_gdbarch_deprecated_call_dummy_words (struct gdbarch *gdbarch,
                                         LONGEST * deprecated_call_dummy_words)
{
  gdbarch->deprecated_call_dummy_words = deprecated_call_dummy_words;
}

int
gdbarch_deprecated_sizeof_call_dummy_words (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of deprecated_sizeof_call_dummy_words, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_sizeof_call_dummy_words called\n");
  return gdbarch->deprecated_sizeof_call_dummy_words;
}

void
set_gdbarch_deprecated_sizeof_call_dummy_words (struct gdbarch *gdbarch,
                                                int deprecated_sizeof_call_dummy_words)
{
  gdbarch->deprecated_sizeof_call_dummy_words = deprecated_sizeof_call_dummy_words;
}

int
gdbarch_deprecated_fix_call_dummy_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_fix_call_dummy != NULL;
}

void
gdbarch_deprecated_fix_call_dummy (struct gdbarch *gdbarch, char *dummy, CORE_ADDR pc, CORE_ADDR fun, int nargs, struct value **args, struct type *type, int gcc_p)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_fix_call_dummy != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_fix_call_dummy called\n");
  gdbarch->deprecated_fix_call_dummy (dummy, pc, fun, nargs, args, type, gcc_p);
}

void
set_gdbarch_deprecated_fix_call_dummy (struct gdbarch *gdbarch,
                                       gdbarch_deprecated_fix_call_dummy_ftype deprecated_fix_call_dummy)
{
  gdbarch->deprecated_fix_call_dummy = deprecated_fix_call_dummy;
}

int
gdbarch_push_dummy_code_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->push_dummy_code != NULL;
}

CORE_ADDR
gdbarch_push_dummy_code (struct gdbarch *gdbarch, CORE_ADDR sp, CORE_ADDR funaddr, int using_gcc, struct value **args, int nargs, struct type *value_type, CORE_ADDR *real_pc, CORE_ADDR *bp_addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->push_dummy_code != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_push_dummy_code called\n");
  return gdbarch->push_dummy_code (gdbarch, sp, funaddr, using_gcc, args, nargs, value_type, real_pc, bp_addr);
}

void
set_gdbarch_push_dummy_code (struct gdbarch *gdbarch,
                             gdbarch_push_dummy_code_ftype push_dummy_code)
{
  gdbarch->push_dummy_code = push_dummy_code;
}

int
gdbarch_deprecated_push_dummy_frame_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_push_dummy_frame != NULL;
}

void
gdbarch_deprecated_push_dummy_frame (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_push_dummy_frame != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_push_dummy_frame called\n");
  gdbarch->deprecated_push_dummy_frame ();
}

void
set_gdbarch_deprecated_push_dummy_frame (struct gdbarch *gdbarch,
                                         gdbarch_deprecated_push_dummy_frame_ftype deprecated_push_dummy_frame)
{
  gdbarch->deprecated_push_dummy_frame = deprecated_push_dummy_frame;
}

int
gdbarch_deprecated_do_registers_info_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_do_registers_info != NULL;
}

void
gdbarch_deprecated_do_registers_info (struct gdbarch *gdbarch, int reg_nr, int fpregs)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_do_registers_info != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_do_registers_info called\n");
  gdbarch->deprecated_do_registers_info (reg_nr, fpregs);
}

void
set_gdbarch_deprecated_do_registers_info (struct gdbarch *gdbarch,
                                          gdbarch_deprecated_do_registers_info_ftype deprecated_do_registers_info)
{
  gdbarch->deprecated_do_registers_info = deprecated_do_registers_info;
}

void
gdbarch_print_registers_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, int regnum, int all)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->print_registers_info != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_print_registers_info called\n");
  gdbarch->print_registers_info (gdbarch, file, frame, regnum, all);
}

void
set_gdbarch_print_registers_info (struct gdbarch *gdbarch,
                                  gdbarch_print_registers_info_ftype print_registers_info)
{
  gdbarch->print_registers_info = print_registers_info;
}

int
gdbarch_print_float_info_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->print_float_info != NULL;
}

void
gdbarch_print_float_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->print_float_info != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_print_float_info called\n");
  gdbarch->print_float_info (gdbarch, file, frame, args);
}

void
set_gdbarch_print_float_info (struct gdbarch *gdbarch,
                              gdbarch_print_float_info_ftype print_float_info)
{
  gdbarch->print_float_info = print_float_info;
}

int
gdbarch_print_vector_info_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->print_vector_info != NULL;
}

void
gdbarch_print_vector_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->print_vector_info != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_print_vector_info called\n");
  gdbarch->print_vector_info (gdbarch, file, frame, args);
}

void
set_gdbarch_print_vector_info (struct gdbarch *gdbarch,
                               gdbarch_print_vector_info_ftype print_vector_info)
{
  gdbarch->print_vector_info = print_vector_info;
}

int
gdbarch_register_sim_regno (struct gdbarch *gdbarch, int reg_nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_sim_regno != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_sim_regno called\n");
  return gdbarch->register_sim_regno (reg_nr);
}

void
set_gdbarch_register_sim_regno (struct gdbarch *gdbarch,
                                gdbarch_register_sim_regno_ftype register_sim_regno)
{
  gdbarch->register_sim_regno = register_sim_regno;
}

int
gdbarch_register_bytes_ok_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->register_bytes_ok != NULL;
}

int
gdbarch_register_bytes_ok (struct gdbarch *gdbarch, long nr_bytes)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_bytes_ok != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_bytes_ok called\n");
  return gdbarch->register_bytes_ok (nr_bytes);
}

void
set_gdbarch_register_bytes_ok (struct gdbarch *gdbarch,
                               gdbarch_register_bytes_ok_ftype register_bytes_ok)
{
  gdbarch->register_bytes_ok = register_bytes_ok;
}

int
gdbarch_cannot_fetch_register (struct gdbarch *gdbarch, int regnum)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->cannot_fetch_register != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_cannot_fetch_register called\n");
  return gdbarch->cannot_fetch_register (regnum);
}

void
set_gdbarch_cannot_fetch_register (struct gdbarch *gdbarch,
                                   gdbarch_cannot_fetch_register_ftype cannot_fetch_register)
{
  gdbarch->cannot_fetch_register = cannot_fetch_register;
}

int
gdbarch_cannot_store_register (struct gdbarch *gdbarch, int regnum)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->cannot_store_register != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_cannot_store_register called\n");
  return gdbarch->cannot_store_register (regnum);
}

void
set_gdbarch_cannot_store_register (struct gdbarch *gdbarch,
                                   gdbarch_cannot_store_register_ftype cannot_store_register)
{
  gdbarch->cannot_store_register = cannot_store_register;
}

int
gdbarch_get_longjmp_target_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->get_longjmp_target != NULL;
}

int
gdbarch_get_longjmp_target (struct gdbarch *gdbarch, CORE_ADDR *pc)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->get_longjmp_target != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_get_longjmp_target called\n");
  return gdbarch->get_longjmp_target (pc);
}

void
set_gdbarch_get_longjmp_target (struct gdbarch *gdbarch,
                                gdbarch_get_longjmp_target_ftype get_longjmp_target)
{
  gdbarch->get_longjmp_target = get_longjmp_target;
}

int
gdbarch_deprecated_pc_in_call_dummy_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_pc_in_call_dummy != generic_pc_in_call_dummy;
}

int
gdbarch_deprecated_pc_in_call_dummy (struct gdbarch *gdbarch, CORE_ADDR pc, CORE_ADDR sp, CORE_ADDR frame_address)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_pc_in_call_dummy != NULL);
  /* Do not check predicate: gdbarch->deprecated_pc_in_call_dummy != generic_pc_in_call_dummy, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_pc_in_call_dummy called\n");
  return gdbarch->deprecated_pc_in_call_dummy (pc, sp, frame_address);
}

void
set_gdbarch_deprecated_pc_in_call_dummy (struct gdbarch *gdbarch,
                                         gdbarch_deprecated_pc_in_call_dummy_ftype deprecated_pc_in_call_dummy)
{
  gdbarch->deprecated_pc_in_call_dummy = deprecated_pc_in_call_dummy;
}

int
gdbarch_deprecated_init_frame_pc_first_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_init_frame_pc_first != NULL;
}

CORE_ADDR
gdbarch_deprecated_init_frame_pc_first (struct gdbarch *gdbarch, int fromleaf, struct frame_info *prev)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_init_frame_pc_first != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_init_frame_pc_first called\n");
  return gdbarch->deprecated_init_frame_pc_first (fromleaf, prev);
}

void
set_gdbarch_deprecated_init_frame_pc_first (struct gdbarch *gdbarch,
                                            gdbarch_deprecated_init_frame_pc_first_ftype deprecated_init_frame_pc_first)
{
  gdbarch->deprecated_init_frame_pc_first = deprecated_init_frame_pc_first;
}

int
gdbarch_deprecated_init_frame_pc_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_init_frame_pc != NULL;
}

CORE_ADDR
gdbarch_deprecated_init_frame_pc (struct gdbarch *gdbarch, int fromleaf, struct frame_info *prev)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_init_frame_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_init_frame_pc called\n");
  return gdbarch->deprecated_init_frame_pc (fromleaf, prev);
}

void
set_gdbarch_deprecated_init_frame_pc (struct gdbarch *gdbarch,
                                      gdbarch_deprecated_init_frame_pc_ftype deprecated_init_frame_pc)
{
  gdbarch->deprecated_init_frame_pc = deprecated_init_frame_pc;
}

int
gdbarch_believe_pcc_promotion (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_believe_pcc_promotion called\n");
  return gdbarch->believe_pcc_promotion;
}

void
set_gdbarch_believe_pcc_promotion (struct gdbarch *gdbarch,
                                   int believe_pcc_promotion)
{
  gdbarch->believe_pcc_promotion = believe_pcc_promotion;
}

int
gdbarch_believe_pcc_promotion_type (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_believe_pcc_promotion_type called\n");
  return gdbarch->believe_pcc_promotion_type;
}

void
set_gdbarch_believe_pcc_promotion_type (struct gdbarch *gdbarch,
                                        int believe_pcc_promotion_type)
{
  gdbarch->believe_pcc_promotion_type = believe_pcc_promotion_type;
}

int
gdbarch_deprecated_get_saved_register_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_get_saved_register != NULL;
}

void
gdbarch_deprecated_get_saved_register (struct gdbarch *gdbarch, char *raw_buffer, int *optimized, CORE_ADDR *addrp, struct frame_info *frame, int regnum, enum lval_type *lval)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_get_saved_register != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_get_saved_register called\n");
  gdbarch->deprecated_get_saved_register (raw_buffer, optimized, addrp, frame, regnum, lval);
}

void
set_gdbarch_deprecated_get_saved_register (struct gdbarch *gdbarch,
                                           gdbarch_deprecated_get_saved_register_ftype deprecated_get_saved_register)
{
  gdbarch->deprecated_get_saved_register = deprecated_get_saved_register;
}

int
gdbarch_deprecated_register_convertible_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_register_convertible != NULL;
}

int
gdbarch_deprecated_register_convertible (struct gdbarch *gdbarch, int nr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_convertible != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_convertible called\n");
  return gdbarch->deprecated_register_convertible (nr);
}

void
set_gdbarch_deprecated_register_convertible (struct gdbarch *gdbarch,
                                             gdbarch_deprecated_register_convertible_ftype deprecated_register_convertible)
{
  gdbarch->deprecated_register_convertible = deprecated_register_convertible;
}

void
gdbarch_deprecated_register_convert_to_virtual (struct gdbarch *gdbarch, int regnum, struct type *type, char *from, char *to)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_convert_to_virtual != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_convert_to_virtual called\n");
  gdbarch->deprecated_register_convert_to_virtual (regnum, type, from, to);
}

void
set_gdbarch_deprecated_register_convert_to_virtual (struct gdbarch *gdbarch,
                                                    gdbarch_deprecated_register_convert_to_virtual_ftype deprecated_register_convert_to_virtual)
{
  gdbarch->deprecated_register_convert_to_virtual = deprecated_register_convert_to_virtual;
}

void
gdbarch_deprecated_register_convert_to_raw (struct gdbarch *gdbarch, struct type *type, int regnum, const char *from, char *to)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_register_convert_to_raw != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_register_convert_to_raw called\n");
  gdbarch->deprecated_register_convert_to_raw (type, regnum, from, to);
}

void
set_gdbarch_deprecated_register_convert_to_raw (struct gdbarch *gdbarch,
                                                gdbarch_deprecated_register_convert_to_raw_ftype deprecated_register_convert_to_raw)
{
  gdbarch->deprecated_register_convert_to_raw = deprecated_register_convert_to_raw;
}

int
gdbarch_convert_register_p (struct gdbarch *gdbarch, int regnum, struct type *type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->convert_register_p != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_convert_register_p called\n");
  return gdbarch->convert_register_p (regnum, type);
}

void
set_gdbarch_convert_register_p (struct gdbarch *gdbarch,
                                gdbarch_convert_register_p_ftype convert_register_p)
{
  gdbarch->convert_register_p = convert_register_p;
}

void
gdbarch_register_to_value (struct gdbarch *gdbarch, struct frame_info *frame, int regnum, struct type *type, void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_to_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_to_value called\n");
  gdbarch->register_to_value (frame, regnum, type, buf);
}

void
set_gdbarch_register_to_value (struct gdbarch *gdbarch,
                               gdbarch_register_to_value_ftype register_to_value)
{
  gdbarch->register_to_value = register_to_value;
}

void
gdbarch_value_to_register (struct gdbarch *gdbarch, struct frame_info *frame, int regnum, struct type *type, const void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->value_to_register != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_value_to_register called\n");
  gdbarch->value_to_register (frame, regnum, type, buf);
}

void
set_gdbarch_value_to_register (struct gdbarch *gdbarch,
                               gdbarch_value_to_register_ftype value_to_register)
{
  gdbarch->value_to_register = value_to_register;
}

CORE_ADDR
gdbarch_pointer_to_address (struct gdbarch *gdbarch, struct type *type, const void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->pointer_to_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_pointer_to_address called\n");
  return gdbarch->pointer_to_address (type, buf);
}

void
set_gdbarch_pointer_to_address (struct gdbarch *gdbarch,
                                gdbarch_pointer_to_address_ftype pointer_to_address)
{
  gdbarch->pointer_to_address = pointer_to_address;
}

void
gdbarch_address_to_pointer (struct gdbarch *gdbarch, struct type *type, void *buf, CORE_ADDR addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->address_to_pointer != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_address_to_pointer called\n");
  gdbarch->address_to_pointer (type, buf, addr);
}

void
set_gdbarch_address_to_pointer (struct gdbarch *gdbarch,
                                gdbarch_address_to_pointer_ftype address_to_pointer)
{
  gdbarch->address_to_pointer = address_to_pointer;
}

int
gdbarch_integer_to_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->integer_to_address != NULL;
}

CORE_ADDR
gdbarch_integer_to_address (struct gdbarch *gdbarch, struct type *type, void *buf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->integer_to_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_integer_to_address called\n");
  return gdbarch->integer_to_address (type, buf);
}

void
set_gdbarch_integer_to_address (struct gdbarch *gdbarch,
                                gdbarch_integer_to_address_ftype integer_to_address)
{
  gdbarch->integer_to_address = integer_to_address;
}

int
gdbarch_deprecated_pop_frame_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_pop_frame != NULL;
}

void
gdbarch_deprecated_pop_frame (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_pop_frame != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_pop_frame called\n");
  gdbarch->deprecated_pop_frame ();
}

void
set_gdbarch_deprecated_pop_frame (struct gdbarch *gdbarch,
                                  gdbarch_deprecated_pop_frame_ftype deprecated_pop_frame)
{
  gdbarch->deprecated_pop_frame = deprecated_pop_frame;
}

int
gdbarch_deprecated_store_struct_return_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_store_struct_return != NULL;
}

void
gdbarch_deprecated_store_struct_return (struct gdbarch *gdbarch, CORE_ADDR addr, CORE_ADDR sp)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_store_struct_return != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_store_struct_return called\n");
  gdbarch->deprecated_store_struct_return (addr, sp);
}

void
set_gdbarch_deprecated_store_struct_return (struct gdbarch *gdbarch,
                                            gdbarch_deprecated_store_struct_return_ftype deprecated_store_struct_return)
{
  gdbarch->deprecated_store_struct_return = deprecated_store_struct_return;
}

int
gdbarch_return_value_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->return_value != NULL;
}

enum return_value_convention
gdbarch_return_value (struct gdbarch *gdbarch, struct type *valtype, struct regcache *regcache, void *readbuf, const void *writebuf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->return_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_return_value called\n");
  return gdbarch->return_value (gdbarch, valtype, regcache, readbuf, writebuf);
}

void
set_gdbarch_return_value (struct gdbarch *gdbarch,
                          gdbarch_return_value_ftype return_value)
{
  gdbarch->return_value = return_value;
}

int
gdbarch_return_value_on_stack (struct gdbarch *gdbarch, struct type *type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->return_value_on_stack != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_return_value_on_stack called\n");
  return gdbarch->return_value_on_stack (type);
}

void
set_gdbarch_return_value_on_stack (struct gdbarch *gdbarch,
                                   gdbarch_return_value_on_stack_ftype return_value_on_stack)
{
  gdbarch->return_value_on_stack = return_value_on_stack;
}

void
gdbarch_extract_return_value (struct gdbarch *gdbarch, struct type *type, struct regcache *regcache, void *valbuf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->extract_return_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_extract_return_value called\n");
  gdbarch->extract_return_value (type, regcache, valbuf);
}

void
set_gdbarch_extract_return_value (struct gdbarch *gdbarch,
                                  gdbarch_extract_return_value_ftype extract_return_value)
{
  gdbarch->extract_return_value = extract_return_value;
}

void
gdbarch_store_return_value (struct gdbarch *gdbarch, struct type *type, struct regcache *regcache, const void *valbuf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->store_return_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_store_return_value called\n");
  gdbarch->store_return_value (type, regcache, valbuf);
}

void
set_gdbarch_store_return_value (struct gdbarch *gdbarch,
                                gdbarch_store_return_value_ftype store_return_value)
{
  gdbarch->store_return_value = store_return_value;
}

void
gdbarch_deprecated_extract_return_value (struct gdbarch *gdbarch, struct type *type, char *regbuf, char *valbuf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_extract_return_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_extract_return_value called\n");
  gdbarch->deprecated_extract_return_value (type, regbuf, valbuf);
}

void
set_gdbarch_deprecated_extract_return_value (struct gdbarch *gdbarch,
                                             gdbarch_deprecated_extract_return_value_ftype deprecated_extract_return_value)
{
  gdbarch->deprecated_extract_return_value = deprecated_extract_return_value;
}

void
gdbarch_deprecated_store_return_value (struct gdbarch *gdbarch, struct type *type, char *valbuf)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_store_return_value != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_store_return_value called\n");
  gdbarch->deprecated_store_return_value (type, valbuf);
}

void
set_gdbarch_deprecated_store_return_value (struct gdbarch *gdbarch,
                                           gdbarch_deprecated_store_return_value_ftype deprecated_store_return_value)
{
  gdbarch->deprecated_store_return_value = deprecated_store_return_value;
}

int
gdbarch_use_struct_convention (struct gdbarch *gdbarch, int gcc_p, struct type *value_type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->use_struct_convention != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_use_struct_convention called\n");
  return gdbarch->use_struct_convention (gcc_p, value_type);
}

void
set_gdbarch_use_struct_convention (struct gdbarch *gdbarch,
                                   gdbarch_use_struct_convention_ftype use_struct_convention)
{
  gdbarch->use_struct_convention = use_struct_convention;
}

int
gdbarch_deprecated_extract_struct_value_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_extract_struct_value_address != NULL;
}

CORE_ADDR
gdbarch_deprecated_extract_struct_value_address (struct gdbarch *gdbarch, struct regcache *regcache)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_extract_struct_value_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_extract_struct_value_address called\n");
  return gdbarch->deprecated_extract_struct_value_address (regcache);
}

void
set_gdbarch_deprecated_extract_struct_value_address (struct gdbarch *gdbarch,
                                                     gdbarch_deprecated_extract_struct_value_address_ftype deprecated_extract_struct_value_address)
{
  gdbarch->deprecated_extract_struct_value_address = deprecated_extract_struct_value_address;
}

int
gdbarch_deprecated_frame_init_saved_regs_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_init_saved_regs != NULL;
}

void
gdbarch_deprecated_frame_init_saved_regs (struct gdbarch *gdbarch, struct frame_info *frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_init_saved_regs != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_init_saved_regs called\n");
  gdbarch->deprecated_frame_init_saved_regs (frame);
}

void
set_gdbarch_deprecated_frame_init_saved_regs (struct gdbarch *gdbarch,
                                              gdbarch_deprecated_frame_init_saved_regs_ftype deprecated_frame_init_saved_regs)
{
  gdbarch->deprecated_frame_init_saved_regs = deprecated_frame_init_saved_regs;
}

int
gdbarch_deprecated_init_extra_frame_info_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_init_extra_frame_info != NULL;
}

void
gdbarch_deprecated_init_extra_frame_info (struct gdbarch *gdbarch, int fromleaf, struct frame_info *frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_init_extra_frame_info != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_init_extra_frame_info called\n");
  gdbarch->deprecated_init_extra_frame_info (fromleaf, frame);
}

void
set_gdbarch_deprecated_init_extra_frame_info (struct gdbarch *gdbarch,
                                              gdbarch_deprecated_init_extra_frame_info_ftype deprecated_init_extra_frame_info)
{
  gdbarch->deprecated_init_extra_frame_info = deprecated_init_extra_frame_info;
}

CORE_ADDR
gdbarch_skip_prologue (struct gdbarch *gdbarch, CORE_ADDR ip)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->skip_prologue != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_skip_prologue called\n");
  return gdbarch->skip_prologue (ip);
}

void
set_gdbarch_skip_prologue (struct gdbarch *gdbarch,
                           gdbarch_skip_prologue_ftype skip_prologue)
{
  gdbarch->skip_prologue = skip_prologue;
}

int
gdbarch_inner_than (struct gdbarch *gdbarch, CORE_ADDR lhs, CORE_ADDR rhs)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->inner_than != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_inner_than called\n");
  return gdbarch->inner_than (lhs, rhs);
}

void
set_gdbarch_inner_than (struct gdbarch *gdbarch,
                        gdbarch_inner_than_ftype inner_than)
{
  gdbarch->inner_than = inner_than;
}

const unsigned char *
gdbarch_breakpoint_from_pc (struct gdbarch *gdbarch, CORE_ADDR *pcptr, int *lenptr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->breakpoint_from_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_breakpoint_from_pc called\n");
  return gdbarch->breakpoint_from_pc (pcptr, lenptr);
}

void
set_gdbarch_breakpoint_from_pc (struct gdbarch *gdbarch,
                                gdbarch_breakpoint_from_pc_ftype breakpoint_from_pc)
{
  gdbarch->breakpoint_from_pc = breakpoint_from_pc;
}

int
gdbarch_adjust_breakpoint_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->adjust_breakpoint_address != NULL;
}

CORE_ADDR
gdbarch_adjust_breakpoint_address (struct gdbarch *gdbarch, CORE_ADDR bpaddr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->adjust_breakpoint_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_adjust_breakpoint_address called\n");
  return gdbarch->adjust_breakpoint_address (gdbarch, bpaddr);
}

void
set_gdbarch_adjust_breakpoint_address (struct gdbarch *gdbarch,
                                       gdbarch_adjust_breakpoint_address_ftype adjust_breakpoint_address)
{
  gdbarch->adjust_breakpoint_address = adjust_breakpoint_address;
}

int
gdbarch_memory_insert_breakpoint (struct gdbarch *gdbarch, CORE_ADDR addr, char *contents_cache)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->memory_insert_breakpoint != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_memory_insert_breakpoint called\n");
  return gdbarch->memory_insert_breakpoint (addr, contents_cache);
}

void
set_gdbarch_memory_insert_breakpoint (struct gdbarch *gdbarch,
                                      gdbarch_memory_insert_breakpoint_ftype memory_insert_breakpoint)
{
  gdbarch->memory_insert_breakpoint = memory_insert_breakpoint;
}

int
gdbarch_memory_remove_breakpoint (struct gdbarch *gdbarch, CORE_ADDR addr, char *contents_cache)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->memory_remove_breakpoint != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_memory_remove_breakpoint called\n");
  return gdbarch->memory_remove_breakpoint (addr, contents_cache);
}

void
set_gdbarch_memory_remove_breakpoint (struct gdbarch *gdbarch,
                                      gdbarch_memory_remove_breakpoint_ftype memory_remove_breakpoint)
{
  gdbarch->memory_remove_breakpoint = memory_remove_breakpoint;
}

CORE_ADDR
gdbarch_decr_pc_after_break (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of decr_pc_after_break, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_decr_pc_after_break called\n");
  return gdbarch->decr_pc_after_break;
}

void
set_gdbarch_decr_pc_after_break (struct gdbarch *gdbarch,
                                 CORE_ADDR decr_pc_after_break)
{
  gdbarch->decr_pc_after_break = decr_pc_after_break;
}

CORE_ADDR
gdbarch_function_start_offset (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of function_start_offset, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_function_start_offset called\n");
  return gdbarch->function_start_offset;
}

void
set_gdbarch_function_start_offset (struct gdbarch *gdbarch,
                                   CORE_ADDR function_start_offset)
{
  gdbarch->function_start_offset = function_start_offset;
}

void
gdbarch_remote_translate_xfer_address (struct gdbarch *gdbarch, struct regcache *regcache, CORE_ADDR gdb_addr, int gdb_len, CORE_ADDR *rem_addr, int *rem_len)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->remote_translate_xfer_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_remote_translate_xfer_address called\n");
  gdbarch->remote_translate_xfer_address (gdbarch, regcache, gdb_addr, gdb_len, rem_addr, rem_len);
}

void
set_gdbarch_remote_translate_xfer_address (struct gdbarch *gdbarch,
                                           gdbarch_remote_translate_xfer_address_ftype remote_translate_xfer_address)
{
  gdbarch->remote_translate_xfer_address = remote_translate_xfer_address;
}

CORE_ADDR
gdbarch_frame_args_skip (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of frame_args_skip, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_frame_args_skip called\n");
  return gdbarch->frame_args_skip;
}

void
set_gdbarch_frame_args_skip (struct gdbarch *gdbarch,
                             CORE_ADDR frame_args_skip)
{
  gdbarch->frame_args_skip = frame_args_skip;
}

int
gdbarch_deprecated_frameless_function_invocation_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frameless_function_invocation != NULL;
}

int
gdbarch_deprecated_frameless_function_invocation (struct gdbarch *gdbarch, struct frame_info *fi)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frameless_function_invocation != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frameless_function_invocation called\n");
  return gdbarch->deprecated_frameless_function_invocation (fi);
}

void
set_gdbarch_deprecated_frameless_function_invocation (struct gdbarch *gdbarch,
                                                      gdbarch_deprecated_frameless_function_invocation_ftype deprecated_frameless_function_invocation)
{
  gdbarch->deprecated_frameless_function_invocation = deprecated_frameless_function_invocation;
}

int
gdbarch_deprecated_frame_chain_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_chain != NULL;
}

CORE_ADDR
gdbarch_deprecated_frame_chain (struct gdbarch *gdbarch, struct frame_info *frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_chain != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_chain called\n");
  return gdbarch->deprecated_frame_chain (frame);
}

void
set_gdbarch_deprecated_frame_chain (struct gdbarch *gdbarch,
                                    gdbarch_deprecated_frame_chain_ftype deprecated_frame_chain)
{
  gdbarch->deprecated_frame_chain = deprecated_frame_chain;
}

int
gdbarch_deprecated_frame_chain_valid_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_chain_valid != NULL;
}

int
gdbarch_deprecated_frame_chain_valid (struct gdbarch *gdbarch, CORE_ADDR chain, struct frame_info *thisframe)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_chain_valid != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_chain_valid called\n");
  return gdbarch->deprecated_frame_chain_valid (chain, thisframe);
}

void
set_gdbarch_deprecated_frame_chain_valid (struct gdbarch *gdbarch,
                                          gdbarch_deprecated_frame_chain_valid_ftype deprecated_frame_chain_valid)
{
  gdbarch->deprecated_frame_chain_valid = deprecated_frame_chain_valid;
}

int
gdbarch_deprecated_frame_saved_pc_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_saved_pc != NULL;
}

CORE_ADDR
gdbarch_deprecated_frame_saved_pc (struct gdbarch *gdbarch, struct frame_info *fi)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_saved_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_saved_pc called\n");
  return gdbarch->deprecated_frame_saved_pc (fi);
}

void
set_gdbarch_deprecated_frame_saved_pc (struct gdbarch *gdbarch,
                                       gdbarch_deprecated_frame_saved_pc_ftype deprecated_frame_saved_pc)
{
  gdbarch->deprecated_frame_saved_pc = deprecated_frame_saved_pc;
}

int
gdbarch_unwind_pc_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->unwind_pc != NULL;
}

CORE_ADDR
gdbarch_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->unwind_pc != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_unwind_pc called\n");
  return gdbarch->unwind_pc (gdbarch, next_frame);
}

void
set_gdbarch_unwind_pc (struct gdbarch *gdbarch,
                       gdbarch_unwind_pc_ftype unwind_pc)
{
  gdbarch->unwind_pc = unwind_pc;
}

int
gdbarch_unwind_sp_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->unwind_sp != NULL;
}

CORE_ADDR
gdbarch_unwind_sp (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->unwind_sp != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_unwind_sp called\n");
  return gdbarch->unwind_sp (gdbarch, next_frame);
}

void
set_gdbarch_unwind_sp (struct gdbarch *gdbarch,
                       gdbarch_unwind_sp_ftype unwind_sp)
{
  gdbarch->unwind_sp = unwind_sp;
}

int
gdbarch_deprecated_frame_args_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_args_address != get_frame_base;
}

CORE_ADDR
gdbarch_deprecated_frame_args_address (struct gdbarch *gdbarch, struct frame_info *fi)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_args_address != NULL);
  /* Do not check predicate: gdbarch->deprecated_frame_args_address != get_frame_base, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_args_address called\n");
  return gdbarch->deprecated_frame_args_address (fi);
}

void
set_gdbarch_deprecated_frame_args_address (struct gdbarch *gdbarch,
                                           gdbarch_deprecated_frame_args_address_ftype deprecated_frame_args_address)
{
  gdbarch->deprecated_frame_args_address = deprecated_frame_args_address;
}

int
gdbarch_deprecated_frame_locals_address_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_frame_locals_address != get_frame_base;
}

CORE_ADDR
gdbarch_deprecated_frame_locals_address (struct gdbarch *gdbarch, struct frame_info *fi)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_frame_locals_address != NULL);
  /* Do not check predicate: gdbarch->deprecated_frame_locals_address != get_frame_base, allow call.  */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_frame_locals_address called\n");
  return gdbarch->deprecated_frame_locals_address (fi);
}

void
set_gdbarch_deprecated_frame_locals_address (struct gdbarch *gdbarch,
                                             gdbarch_deprecated_frame_locals_address_ftype deprecated_frame_locals_address)
{
  gdbarch->deprecated_frame_locals_address = deprecated_frame_locals_address;
}

int
gdbarch_deprecated_saved_pc_after_call_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_saved_pc_after_call != NULL;
}

CORE_ADDR
gdbarch_deprecated_saved_pc_after_call (struct gdbarch *gdbarch, struct frame_info *frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_saved_pc_after_call != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_saved_pc_after_call called\n");
  return gdbarch->deprecated_saved_pc_after_call (frame);
}

void
set_gdbarch_deprecated_saved_pc_after_call (struct gdbarch *gdbarch,
                                            gdbarch_deprecated_saved_pc_after_call_ftype deprecated_saved_pc_after_call)
{
  gdbarch->deprecated_saved_pc_after_call = deprecated_saved_pc_after_call;
}

int
gdbarch_frame_num_args_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->frame_num_args != NULL;
}

int
gdbarch_frame_num_args (struct gdbarch *gdbarch, struct frame_info *frame)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->frame_num_args != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_frame_num_args called\n");
  return gdbarch->frame_num_args (frame);
}

void
set_gdbarch_frame_num_args (struct gdbarch *gdbarch,
                            gdbarch_frame_num_args_ftype frame_num_args)
{
  gdbarch->frame_num_args = frame_num_args;
}

int
gdbarch_deprecated_stack_align_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_stack_align != NULL;
}

CORE_ADDR
gdbarch_deprecated_stack_align (struct gdbarch *gdbarch, CORE_ADDR sp)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_stack_align != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_stack_align called\n");
  return gdbarch->deprecated_stack_align (sp);
}

void
set_gdbarch_deprecated_stack_align (struct gdbarch *gdbarch,
                                    gdbarch_deprecated_stack_align_ftype deprecated_stack_align)
{
  gdbarch->deprecated_stack_align = deprecated_stack_align;
}

int
gdbarch_frame_align_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->frame_align != NULL;
}

CORE_ADDR
gdbarch_frame_align (struct gdbarch *gdbarch, CORE_ADDR address)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->frame_align != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_frame_align called\n");
  return gdbarch->frame_align (gdbarch, address);
}

void
set_gdbarch_frame_align (struct gdbarch *gdbarch,
                         gdbarch_frame_align_ftype frame_align)
{
  gdbarch->frame_align = frame_align;
}

int
gdbarch_deprecated_reg_struct_has_addr_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->deprecated_reg_struct_has_addr != NULL;
}

int
gdbarch_deprecated_reg_struct_has_addr (struct gdbarch *gdbarch, int gcc_p, struct type *type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->deprecated_reg_struct_has_addr != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_deprecated_reg_struct_has_addr called\n");
  return gdbarch->deprecated_reg_struct_has_addr (gcc_p, type);
}

void
set_gdbarch_deprecated_reg_struct_has_addr (struct gdbarch *gdbarch,
                                            gdbarch_deprecated_reg_struct_has_addr_ftype deprecated_reg_struct_has_addr)
{
  gdbarch->deprecated_reg_struct_has_addr = deprecated_reg_struct_has_addr;
}

int
gdbarch_stabs_argument_has_addr (struct gdbarch *gdbarch, struct type *type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->stabs_argument_has_addr != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_stabs_argument_has_addr called\n");
  return gdbarch->stabs_argument_has_addr (gdbarch, type);
}

void
set_gdbarch_stabs_argument_has_addr (struct gdbarch *gdbarch,
                                     gdbarch_stabs_argument_has_addr_ftype stabs_argument_has_addr)
{
  gdbarch->stabs_argument_has_addr = stabs_argument_has_addr;
}

int
gdbarch_frame_red_zone_size (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_frame_red_zone_size called\n");
  return gdbarch->frame_red_zone_size;
}

void
set_gdbarch_frame_red_zone_size (struct gdbarch *gdbarch,
                                 int frame_red_zone_size)
{
  gdbarch->frame_red_zone_size = frame_red_zone_size;
}

int
gdbarch_parm_boundary (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_parm_boundary called\n");
  return gdbarch->parm_boundary;
}

void
set_gdbarch_parm_boundary (struct gdbarch *gdbarch,
                           int parm_boundary)
{
  gdbarch->parm_boundary = parm_boundary;
}

const struct floatformat *
gdbarch_float_format (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_float_format called\n");
  return gdbarch->float_format;
}

void
set_gdbarch_float_format (struct gdbarch *gdbarch,
                          const struct floatformat * float_format)
{
  gdbarch->float_format = float_format;
}

const struct floatformat *
gdbarch_double_format (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_double_format called\n");
  return gdbarch->double_format;
}

void
set_gdbarch_double_format (struct gdbarch *gdbarch,
                           const struct floatformat * double_format)
{
  gdbarch->double_format = double_format;
}

const struct floatformat *
gdbarch_long_double_format (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_long_double_format called\n");
  return gdbarch->long_double_format;
}

void
set_gdbarch_long_double_format (struct gdbarch *gdbarch,
                                const struct floatformat * long_double_format)
{
  gdbarch->long_double_format = long_double_format;
}

CORE_ADDR
gdbarch_convert_from_func_ptr_addr (struct gdbarch *gdbarch, CORE_ADDR addr, struct target_ops *targ)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->convert_from_func_ptr_addr != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_convert_from_func_ptr_addr called\n");
  return gdbarch->convert_from_func_ptr_addr (gdbarch, addr, targ);
}

void
set_gdbarch_convert_from_func_ptr_addr (struct gdbarch *gdbarch,
                                        gdbarch_convert_from_func_ptr_addr_ftype convert_from_func_ptr_addr)
{
  gdbarch->convert_from_func_ptr_addr = convert_from_func_ptr_addr;
}

CORE_ADDR
gdbarch_addr_bits_remove (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->addr_bits_remove != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_addr_bits_remove called\n");
  return gdbarch->addr_bits_remove (addr);
}

void
set_gdbarch_addr_bits_remove (struct gdbarch *gdbarch,
                              gdbarch_addr_bits_remove_ftype addr_bits_remove)
{
  gdbarch->addr_bits_remove = addr_bits_remove;
}

CORE_ADDR
gdbarch_smash_text_address (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->smash_text_address != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_smash_text_address called\n");
  return gdbarch->smash_text_address (addr);
}

void
set_gdbarch_smash_text_address (struct gdbarch *gdbarch,
                                gdbarch_smash_text_address_ftype smash_text_address)
{
  gdbarch->smash_text_address = smash_text_address;
}

int
gdbarch_software_single_step_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->software_single_step != NULL;
}

void
gdbarch_software_single_step (struct gdbarch *gdbarch, enum target_signal sig, int insert_breakpoints_p)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->software_single_step != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_software_single_step called\n");
  gdbarch->software_single_step (sig, insert_breakpoints_p);
}

void
set_gdbarch_software_single_step (struct gdbarch *gdbarch,
                                  gdbarch_software_single_step_ftype software_single_step)
{
  gdbarch->software_single_step = software_single_step;
}

int
gdbarch_print_insn (struct gdbarch *gdbarch, bfd_vma vma, struct disassemble_info *info)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->print_insn != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_print_insn called\n");
  return gdbarch->print_insn (vma, info);
}

void
set_gdbarch_print_insn (struct gdbarch *gdbarch,
                        gdbarch_print_insn_ftype print_insn)
{
  gdbarch->print_insn = print_insn;
}

CORE_ADDR
gdbarch_skip_trampoline_code (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->skip_trampoline_code != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_skip_trampoline_code called\n");
  return gdbarch->skip_trampoline_code (pc);
}

void
set_gdbarch_skip_trampoline_code (struct gdbarch *gdbarch,
                                  gdbarch_skip_trampoline_code_ftype skip_trampoline_code)
{
  gdbarch->skip_trampoline_code = skip_trampoline_code;
}

CORE_ADDR
gdbarch_skip_solib_resolver (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->skip_solib_resolver != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_skip_solib_resolver called\n");
  return gdbarch->skip_solib_resolver (gdbarch, pc);
}

void
set_gdbarch_skip_solib_resolver (struct gdbarch *gdbarch,
                                 gdbarch_skip_solib_resolver_ftype skip_solib_resolver)
{
  gdbarch->skip_solib_resolver = skip_solib_resolver;
}

int
gdbarch_in_solib_call_trampoline (struct gdbarch *gdbarch, CORE_ADDR pc, char *name)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->in_solib_call_trampoline != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_in_solib_call_trampoline called\n");
  return gdbarch->in_solib_call_trampoline (pc, name);
}

void
set_gdbarch_in_solib_call_trampoline (struct gdbarch *gdbarch,
                                      gdbarch_in_solib_call_trampoline_ftype in_solib_call_trampoline)
{
  gdbarch->in_solib_call_trampoline = in_solib_call_trampoline;
}

int
gdbarch_in_solib_return_trampoline (struct gdbarch *gdbarch, CORE_ADDR pc, char *name)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->in_solib_return_trampoline != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_in_solib_return_trampoline called\n");
  return gdbarch->in_solib_return_trampoline (pc, name);
}

void
set_gdbarch_in_solib_return_trampoline (struct gdbarch *gdbarch,
                                        gdbarch_in_solib_return_trampoline_ftype in_solib_return_trampoline)
{
  gdbarch->in_solib_return_trampoline = in_solib_return_trampoline;
}

int
gdbarch_pc_in_sigtramp (struct gdbarch *gdbarch, CORE_ADDR pc, char *name)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->pc_in_sigtramp != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_pc_in_sigtramp called\n");
  return gdbarch->pc_in_sigtramp (pc, name);
}

void
set_gdbarch_pc_in_sigtramp (struct gdbarch *gdbarch,
                            gdbarch_pc_in_sigtramp_ftype pc_in_sigtramp)
{
  gdbarch->pc_in_sigtramp = pc_in_sigtramp;
}

int
gdbarch_sigtramp_start_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->sigtramp_start != NULL;
}

CORE_ADDR
gdbarch_sigtramp_start (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->sigtramp_start != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_sigtramp_start called\n");
  return gdbarch->sigtramp_start (pc);
}

void
set_gdbarch_sigtramp_start (struct gdbarch *gdbarch,
                            gdbarch_sigtramp_start_ftype sigtramp_start)
{
  gdbarch->sigtramp_start = sigtramp_start;
}

int
gdbarch_sigtramp_end_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->sigtramp_end != NULL;
}

CORE_ADDR
gdbarch_sigtramp_end (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->sigtramp_end != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_sigtramp_end called\n");
  return gdbarch->sigtramp_end (pc);
}

void
set_gdbarch_sigtramp_end (struct gdbarch *gdbarch,
                          gdbarch_sigtramp_end_ftype sigtramp_end)
{
  gdbarch->sigtramp_end = sigtramp_end;
}

int
gdbarch_in_function_epilogue_p (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->in_function_epilogue_p != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_in_function_epilogue_p called\n");
  return gdbarch->in_function_epilogue_p (gdbarch, addr);
}

void
set_gdbarch_in_function_epilogue_p (struct gdbarch *gdbarch,
                                    gdbarch_in_function_epilogue_p_ftype in_function_epilogue_p)
{
  gdbarch->in_function_epilogue_p = in_function_epilogue_p;
}

char *
gdbarch_construct_inferior_arguments (struct gdbarch *gdbarch, int argc, char **argv)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->construct_inferior_arguments != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_construct_inferior_arguments called\n");
  return gdbarch->construct_inferior_arguments (gdbarch, argc, argv);
}

void
set_gdbarch_construct_inferior_arguments (struct gdbarch *gdbarch,
                                          gdbarch_construct_inferior_arguments_ftype construct_inferior_arguments)
{
  gdbarch->construct_inferior_arguments = construct_inferior_arguments;
}

void
gdbarch_elf_make_msymbol_special (struct gdbarch *gdbarch, asymbol *sym, struct minimal_symbol *msym)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->elf_make_msymbol_special != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_elf_make_msymbol_special called\n");
  gdbarch->elf_make_msymbol_special (sym, msym);
}

void
set_gdbarch_elf_make_msymbol_special (struct gdbarch *gdbarch,
                                      gdbarch_elf_make_msymbol_special_ftype elf_make_msymbol_special)
{
  gdbarch->elf_make_msymbol_special = elf_make_msymbol_special;
}

void
gdbarch_coff_make_msymbol_special (struct gdbarch *gdbarch, int val, struct minimal_symbol *msym)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->coff_make_msymbol_special != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_coff_make_msymbol_special called\n");
  gdbarch->coff_make_msymbol_special (val, msym);
}

void
set_gdbarch_coff_make_msymbol_special (struct gdbarch *gdbarch,
                                       gdbarch_coff_make_msymbol_special_ftype coff_make_msymbol_special)
{
  gdbarch->coff_make_msymbol_special = coff_make_msymbol_special;
}

const char *
gdbarch_name_of_malloc (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of name_of_malloc, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_name_of_malloc called\n");
  return gdbarch->name_of_malloc;
}

void
set_gdbarch_name_of_malloc (struct gdbarch *gdbarch,
                            const char * name_of_malloc)
{
  gdbarch->name_of_malloc = name_of_malloc;
}

int
gdbarch_cannot_step_breakpoint (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of cannot_step_breakpoint, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_cannot_step_breakpoint called\n");
  return gdbarch->cannot_step_breakpoint;
}

void
set_gdbarch_cannot_step_breakpoint (struct gdbarch *gdbarch,
                                    int cannot_step_breakpoint)
{
  gdbarch->cannot_step_breakpoint = cannot_step_breakpoint;
}

int
gdbarch_have_nonsteppable_watchpoint (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  /* Skip verify of have_nonsteppable_watchpoint, invalid_p == 0 */
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_have_nonsteppable_watchpoint called\n");
  return gdbarch->have_nonsteppable_watchpoint;
}

void
set_gdbarch_have_nonsteppable_watchpoint (struct gdbarch *gdbarch,
                                          int have_nonsteppable_watchpoint)
{
  gdbarch->have_nonsteppable_watchpoint = have_nonsteppable_watchpoint;
}

int
gdbarch_address_class_type_flags_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->address_class_type_flags != NULL;
}

int
gdbarch_address_class_type_flags (struct gdbarch *gdbarch, int byte_size, int dwarf2_addr_class)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->address_class_type_flags != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_address_class_type_flags called\n");
  return gdbarch->address_class_type_flags (byte_size, dwarf2_addr_class);
}

void
set_gdbarch_address_class_type_flags (struct gdbarch *gdbarch,
                                      gdbarch_address_class_type_flags_ftype address_class_type_flags)
{
  gdbarch->address_class_type_flags = address_class_type_flags;
}

int
gdbarch_address_class_type_flags_to_name_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->address_class_type_flags_to_name != NULL;
}

const char *
gdbarch_address_class_type_flags_to_name (struct gdbarch *gdbarch, int type_flags)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->address_class_type_flags_to_name != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_address_class_type_flags_to_name called\n");
  return gdbarch->address_class_type_flags_to_name (gdbarch, type_flags);
}

void
set_gdbarch_address_class_type_flags_to_name (struct gdbarch *gdbarch,
                                              gdbarch_address_class_type_flags_to_name_ftype address_class_type_flags_to_name)
{
  gdbarch->address_class_type_flags_to_name = address_class_type_flags_to_name;
}

int
gdbarch_address_class_name_to_type_flags_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->address_class_name_to_type_flags != NULL;
}

int
gdbarch_address_class_name_to_type_flags (struct gdbarch *gdbarch, const char *name, int *type_flags_ptr)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->address_class_name_to_type_flags != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_address_class_name_to_type_flags called\n");
  return gdbarch->address_class_name_to_type_flags (gdbarch, name, type_flags_ptr);
}

void
set_gdbarch_address_class_name_to_type_flags (struct gdbarch *gdbarch,
                                              gdbarch_address_class_name_to_type_flags_ftype address_class_name_to_type_flags)
{
  gdbarch->address_class_name_to_type_flags = address_class_name_to_type_flags;
}

int
gdbarch_register_reggroup_p (struct gdbarch *gdbarch, int regnum, struct reggroup *reggroup)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->register_reggroup_p != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_register_reggroup_p called\n");
  return gdbarch->register_reggroup_p (gdbarch, regnum, reggroup);
}

void
set_gdbarch_register_reggroup_p (struct gdbarch *gdbarch,
                                 gdbarch_register_reggroup_p_ftype register_reggroup_p)
{
  gdbarch->register_reggroup_p = register_reggroup_p;
}

int
gdbarch_fetch_pointer_argument_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->fetch_pointer_argument != NULL;
}

CORE_ADDR
gdbarch_fetch_pointer_argument (struct gdbarch *gdbarch, struct frame_info *frame, int argi, struct type *type)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->fetch_pointer_argument != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_fetch_pointer_argument called\n");
  return gdbarch->fetch_pointer_argument (frame, argi, type);
}

void
set_gdbarch_fetch_pointer_argument (struct gdbarch *gdbarch,
                                    gdbarch_fetch_pointer_argument_ftype fetch_pointer_argument)
{
  gdbarch->fetch_pointer_argument = fetch_pointer_argument;
}

int
gdbarch_regset_from_core_section_p (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch != NULL);
  return gdbarch->regset_from_core_section != NULL;
}

const struct regset *
gdbarch_regset_from_core_section (struct gdbarch *gdbarch, const char *sect_name, size_t sect_size)
{
  gdb_assert (gdbarch != NULL);
  gdb_assert (gdbarch->regset_from_core_section != NULL);
  if (gdbarch_debug >= 2)
    fprintf_unfiltered (gdb_stdlog, "gdbarch_regset_from_core_section called\n");
  return gdbarch->regset_from_core_section (gdbarch, sect_name, sect_size);
}

void
set_gdbarch_regset_from_core_section (struct gdbarch *gdbarch,
                                      gdbarch_regset_from_core_section_ftype regset_from_core_section)
{
  gdbarch->regset_from_core_section = regset_from_core_section;
}


/* Keep a registry of per-architecture data-pointers required by GDB
   modules. */

struct gdbarch_data
{
  unsigned index;
  int init_p;
  gdbarch_data_init_ftype *init;
};

struct gdbarch_data_registration
{
  struct gdbarch_data *data;
  struct gdbarch_data_registration *next;
};

struct gdbarch_data_registry
{
  unsigned nr;
  struct gdbarch_data_registration *registrations;
};

struct gdbarch_data_registry gdbarch_data_registry =
{
  0, NULL,
};

struct gdbarch_data *
register_gdbarch_data (gdbarch_data_init_ftype *init)
{
  struct gdbarch_data_registration **curr;
  /* Append the new registraration.  */
  for (curr = &gdbarch_data_registry.registrations;
       (*curr) != NULL;
       curr = &(*curr)->next);
  (*curr) = XMALLOC (struct gdbarch_data_registration);
  (*curr)->next = NULL;
  (*curr)->data = XMALLOC (struct gdbarch_data);
  (*curr)->data->index = gdbarch_data_registry.nr++;
  (*curr)->data->init = init;
  (*curr)->data->init_p = 1;
  return (*curr)->data;
}


/* Create/delete the gdbarch data vector. */

static void
alloc_gdbarch_data (struct gdbarch *gdbarch)
{
  gdb_assert (gdbarch->data == NULL);
  gdbarch->nr_data = gdbarch_data_registry.nr;
  gdbarch->data = GDBARCH_OBSTACK_CALLOC (gdbarch, gdbarch->nr_data, void *);
}

/* Initialize the current value of the specified per-architecture
   data-pointer. */

void
set_gdbarch_data (struct gdbarch *gdbarch,
                  struct gdbarch_data *data,
                  void *pointer)
{
  gdb_assert (data->index < gdbarch->nr_data);
  gdb_assert (gdbarch->data[data->index] == NULL);
  gdbarch->data[data->index] = pointer;
}

/* Return the current value of the specified per-architecture
   data-pointer. */

void *
gdbarch_data (struct gdbarch *gdbarch, struct gdbarch_data *data)
{
  gdb_assert (data->index < gdbarch->nr_data);
  /* The data-pointer isn't initialized, call init() to get a value but
     only if the architecture initializaiton has completed.  Otherwise
     punt - hope that the caller knows what they are doing.  */
  if (gdbarch->data[data->index] == NULL
      && gdbarch->initialized_p)
    {
      /* Be careful to detect an initialization cycle.  */
      gdb_assert (data->init_p);
      data->init_p = 0;
      gdb_assert (data->init != NULL);
      gdbarch->data[data->index] = data->init (gdbarch);
      data->init_p = 1;
      gdb_assert (gdbarch->data[data->index] != NULL);
    }
  return gdbarch->data[data->index];
}



/* Keep a registry of swapped data required by GDB modules. */

struct gdbarch_swap
{
  void *swap;
  struct gdbarch_swap_registration *source;
  struct gdbarch_swap *next;
};

struct gdbarch_swap_registration
{
  void *data;
  unsigned long sizeof_data;
  gdbarch_swap_ftype *init;
  struct gdbarch_swap_registration *next;
};

struct gdbarch_swap_registry
{
  int nr;
  struct gdbarch_swap_registration *registrations;
};

struct gdbarch_swap_registry gdbarch_swap_registry = 
{
  0, NULL,
};

void
deprecated_register_gdbarch_swap (void *data,
		                  unsigned long sizeof_data,
		                  gdbarch_swap_ftype *init)
{
  struct gdbarch_swap_registration **rego;
  for (rego = &gdbarch_swap_registry.registrations;
       (*rego) != NULL;
       rego = &(*rego)->next);
  (*rego) = XMALLOC (struct gdbarch_swap_registration);
  (*rego)->next = NULL;
  (*rego)->init = init;
  (*rego)->data = data;
  (*rego)->sizeof_data = sizeof_data;
}

static void
current_gdbarch_swap_init_hack (void)
{
  struct gdbarch_swap_registration *rego;
  struct gdbarch_swap **curr = &current_gdbarch->swap;
  for (rego = gdbarch_swap_registry.registrations;
       rego != NULL;
       rego = rego->next)
    {
      if (rego->data != NULL)
	{
	  (*curr) = GDBARCH_OBSTACK_ZALLOC (current_gdbarch,
					    struct gdbarch_swap);
	  (*curr)->source = rego;
	  (*curr)->swap = gdbarch_obstack_zalloc (current_gdbarch,
						  rego->sizeof_data);
	  (*curr)->next = NULL;
	  curr = &(*curr)->next;
	}
      if (rego->init != NULL)
	rego->init ();
    }
}

static struct gdbarch *
current_gdbarch_swap_out_hack (void)
{
  struct gdbarch *old_gdbarch = current_gdbarch;
  struct gdbarch_swap *curr;

  gdb_assert (old_gdbarch != NULL);
  for (curr = old_gdbarch->swap;
       curr != NULL;
       curr = curr->next)
    {
      memcpy (curr->swap, curr->source->data, curr->source->sizeof_data);
      memset (curr->source->data, 0, curr->source->sizeof_data);
    }
  current_gdbarch = NULL;
  return old_gdbarch;
}

static void
current_gdbarch_swap_in_hack (struct gdbarch *new_gdbarch)
{
  struct gdbarch_swap *curr;

  gdb_assert (current_gdbarch == NULL);
  for (curr = new_gdbarch->swap;
       curr != NULL;
       curr = curr->next)
    memcpy (curr->source->data, curr->swap, curr->source->sizeof_data);
  current_gdbarch = new_gdbarch;
}


/* Keep a registry of the architectures known by GDB. */

struct gdbarch_registration
{
  enum bfd_architecture bfd_architecture;
  gdbarch_init_ftype *init;
  gdbarch_dump_tdep_ftype *dump_tdep;
  struct gdbarch_list *arches;
  struct gdbarch_registration *next;
};

static struct gdbarch_registration *gdbarch_registry = NULL;

static void
append_name (const char ***buf, int *nr, const char *name)
{
  *buf = xrealloc (*buf, sizeof (char**) * (*nr + 1));
  (*buf)[*nr] = name;
  *nr += 1;
}

const char **
gdbarch_printable_names (void)
{
  /* Accumulate a list of names based on the registed list of
     architectures. */
  enum bfd_architecture a;
  int nr_arches = 0;
  const char **arches = NULL;
  struct gdbarch_registration *rego;
  for (rego = gdbarch_registry;
       rego != NULL;
       rego = rego->next)
    {
      const struct bfd_arch_info *ap;
      ap = bfd_lookup_arch (rego->bfd_architecture, 0);
      if (ap == NULL)
        internal_error (__FILE__, __LINE__,
                        "gdbarch_architecture_names: multi-arch unknown");
      do
        {
          append_name (&arches, &nr_arches, ap->printable_name);
          ap = ap->next;
        }
      while (ap != NULL);
    }
  append_name (&arches, &nr_arches, NULL);
  return arches;
}


void
gdbarch_register (enum bfd_architecture bfd_architecture,
                  gdbarch_init_ftype *init,
		  gdbarch_dump_tdep_ftype *dump_tdep)
{
  struct gdbarch_registration **curr;
  const struct bfd_arch_info *bfd_arch_info;
  /* Check that BFD recognizes this architecture */
  bfd_arch_info = bfd_lookup_arch (bfd_architecture, 0);
  if (bfd_arch_info == NULL)
    {
      internal_error (__FILE__, __LINE__,
                      "gdbarch: Attempt to register unknown architecture (%d)",
                      bfd_architecture);
    }
  /* Check that we haven't seen this architecture before */
  for (curr = &gdbarch_registry;
       (*curr) != NULL;
       curr = &(*curr)->next)
    {
      if (bfd_architecture == (*curr)->bfd_architecture)
	internal_error (__FILE__, __LINE__,
                        "gdbarch: Duplicate registraration of architecture (%s)",
	                bfd_arch_info->printable_name);
    }
  /* log it */
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "register_gdbarch_init (%s, 0x%08lx)\n",
			bfd_arch_info->printable_name,
			(long) init);
  /* Append it */
  (*curr) = XMALLOC (struct gdbarch_registration);
  (*curr)->bfd_architecture = bfd_architecture;
  (*curr)->init = init;
  (*curr)->dump_tdep = dump_tdep;
  (*curr)->arches = NULL;
  (*curr)->next = NULL;
}

void
register_gdbarch_init (enum bfd_architecture bfd_architecture,
		       gdbarch_init_ftype *init)
{
  gdbarch_register (bfd_architecture, init, NULL);
}


/* Look for an architecture using gdbarch_info.  Base search on only
   BFD_ARCH_INFO and BYTE_ORDER. */

struct gdbarch_list *
gdbarch_list_lookup_by_info (struct gdbarch_list *arches,
                             const struct gdbarch_info *info)
{
  for (; arches != NULL; arches = arches->next)
    {
      if (info->bfd_arch_info != arches->gdbarch->bfd_arch_info)
	continue;
      if (info->byte_order != arches->gdbarch->byte_order)
	continue;
      if (info->osabi != arches->gdbarch->osabi)
	continue;
      return arches;
    }
  return NULL;
}


/* Find an architecture that matches the specified INFO.  Create a new
   architecture if needed.  Return that new architecture.  Assumes
   that there is no current architecture.  */

static struct gdbarch *
find_arch_by_info (struct gdbarch *old_gdbarch, struct gdbarch_info info)
{
  struct gdbarch *new_gdbarch;
  struct gdbarch_registration *rego;

  /* The existing architecture has been swapped out - all this code
     works from a clean slate.  */
  gdb_assert (current_gdbarch == NULL);

  /* Fill in missing parts of the INFO struct using a number of
     sources: "set ..."; INFOabfd supplied; and the existing
     architecture.  */
  gdbarch_info_fill (old_gdbarch, &info);

  /* Must have found some sort of architecture. */
  gdb_assert (info.bfd_arch_info != NULL);

  if (gdbarch_debug)
    {
      fprintf_unfiltered (gdb_stdlog,
			  "find_arch_by_info: info.bfd_arch_info %s\n",
			  (info.bfd_arch_info != NULL
			   ? info.bfd_arch_info->printable_name
			   : "(null)"));
      fprintf_unfiltered (gdb_stdlog,
			  "find_arch_by_info: info.byte_order %d (%s)\n",
			  info.byte_order,
			  (info.byte_order == BFD_ENDIAN_BIG ? "big"
			   : info.byte_order == BFD_ENDIAN_LITTLE ? "little"
			   : "default"));
      fprintf_unfiltered (gdb_stdlog,
			  "find_arch_by_info: info.osabi %d (%s)\n",
			  info.osabi, gdbarch_osabi_name (info.osabi));
      fprintf_unfiltered (gdb_stdlog,
			  "find_arch_by_info: info.abfd 0x%lx\n",
			  (long) info.abfd);
      fprintf_unfiltered (gdb_stdlog,
			  "find_arch_by_info: info.tdep_info 0x%lx\n",
			  (long) info.tdep_info);
    }

  /* Find the tdep code that knows about this architecture.  */
  for (rego = gdbarch_registry;
       rego != NULL;
       rego = rego->next)
    if (rego->bfd_architecture == info.bfd_arch_info->arch)
      break;
  if (rego == NULL)
    {
      if (gdbarch_debug)
	fprintf_unfiltered (gdb_stdlog, "find_arch_by_info: "
			    "No matching architecture\n");
      return 0;
    }

  /* Ask the tdep code for an architecture that matches "info".  */
  new_gdbarch = rego->init (info, rego->arches);

  /* Did the tdep code like it?  No.  Reject the change and revert to
     the old architecture.  */
  if (new_gdbarch == NULL)
    {
      if (gdbarch_debug)
	fprintf_unfiltered (gdb_stdlog, "find_arch_by_info: "
			    "Target rejected architecture\n");
      return NULL;
    }

  /* Is this a pre-existing architecture (as determined by already
     being initialized)?  Move it to the front of the architecture
     list (keeping the list sorted Most Recently Used).  */
  if (new_gdbarch->initialized_p)
    {
      struct gdbarch_list **list;
      struct gdbarch_list *this;
      if (gdbarch_debug)
	fprintf_unfiltered (gdb_stdlog, "find_arch_by_info: "
			    "Previous architecture 0x%08lx (%s) selected\n",
			    (long) new_gdbarch,
			    new_gdbarch->bfd_arch_info->printable_name);
      /* Find the existing arch in the list.  */
      for (list = &rego->arches;
	   (*list) != NULL && (*list)->gdbarch != new_gdbarch;
	   list = &(*list)->next);
      /* It had better be in the list of architectures.  */
      gdb_assert ((*list) != NULL && (*list)->gdbarch == new_gdbarch);
      /* Unlink THIS.  */
      this = (*list);
      (*list) = this->next;
      /* Insert THIS at the front.  */
      this->next = rego->arches;
      rego->arches = this;
      /* Return it.  */
      return new_gdbarch;
    }

  /* It's a new architecture.  */
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "find_arch_by_info: "
			"New architecture 0x%08lx (%s) selected\n",
			(long) new_gdbarch,
			new_gdbarch->bfd_arch_info->printable_name);
  
  /* Insert the new architecture into the front of the architecture
     list (keep the list sorted Most Recently Used).  */
  {
    struct gdbarch_list *this = XMALLOC (struct gdbarch_list);
    this->next = rego->arches;
    this->gdbarch = new_gdbarch;
    rego->arches = this;
  }    

  /* Check that the newly installed architecture is valid.  Plug in
     any post init values.  */
  new_gdbarch->dump_tdep = rego->dump_tdep;
  verify_gdbarch (new_gdbarch);
  new_gdbarch->initialized_p = 1;

  /* Initialize any per-architecture swap areas.  This phase requires
     a valid global CURRENT_GDBARCH.  Set it momentarially, and then
     swap the entire architecture out.  */
  current_gdbarch = new_gdbarch;
  current_gdbarch_swap_init_hack ();
  current_gdbarch_swap_out_hack ();

  if (gdbarch_debug)
    gdbarch_dump (new_gdbarch, gdb_stdlog);

  return new_gdbarch;
}

struct gdbarch *
gdbarch_find_by_info (struct gdbarch_info info)
{
  /* Save the previously selected architecture, setting the global to
     NULL.  This stops things like gdbarch->init() trying to use the
     previous architecture's configuration.  The previous architecture
     may not even be of the same architecture family.  The most recent
     architecture of the same family is found at the head of the
     rego->arches list.  */
  struct gdbarch *old_gdbarch = current_gdbarch_swap_out_hack ();

  /* Find the specified architecture.  */
  struct gdbarch *new_gdbarch = find_arch_by_info (old_gdbarch, info);

  /* Restore the existing architecture.  */
  gdb_assert (current_gdbarch == NULL);
  current_gdbarch_swap_in_hack (old_gdbarch);

  return new_gdbarch;
}

/* Make the specified architecture current, swapping the existing one
   out.  */

void
deprecated_current_gdbarch_select_hack (struct gdbarch *new_gdbarch)
{
  gdb_assert (new_gdbarch != NULL);
  gdb_assert (current_gdbarch != NULL);
  gdb_assert (new_gdbarch->initialized_p);
  current_gdbarch_swap_out_hack ();
  current_gdbarch_swap_in_hack (new_gdbarch);
  architecture_changed_event ();
}

extern void _initialize_gdbarch (void);

void
_initialize_gdbarch (void)
{
  struct cmd_list_element *c;

  add_show_from_set (add_set_cmd ("arch",
				  class_maintenance,
				  var_zinteger,
				  (char *)&gdbarch_debug,
				  "Set architecture debugging.\n\
When non-zero, architecture debugging is enabled.", &setdebuglist),
		     &showdebuglist);
  c = add_set_cmd ("archdebug",
		   class_maintenance,
		   var_zinteger,
		   (char *)&gdbarch_debug,
		   "Set architecture debugging.\n\
When non-zero, architecture debugging is enabled.", &setlist);

  deprecate_cmd (c, "set debug arch");
  deprecate_cmd (add_show_from_set (c, &showlist), "show debug arch");
}
