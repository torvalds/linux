/* Dynamic architecture support for GDB, the GNU debugger.

   Copyright 1998, 1999, 2000, 2002, 2003 Free Software Foundation,
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

#ifndef GDBARCH_UTILS_H
#define GDBARCH_UTILS_H

struct gdbarch;
struct frame_info;
struct minimal_symbol;
struct type;
struct gdbarch_info;

/* gdbarch trace variable */
extern int gdbarch_debug;

/* Implementation of extract return value that grubs around in the
   register cache.  */
extern gdbarch_extract_return_value_ftype legacy_extract_return_value;

/* Implementation of store return value that grubs the register cache.  */
extern gdbarch_store_return_value_ftype legacy_store_return_value;

/* To return any structure or union type by value, store it at the
   address passed as an invisible first argument to the function.  */
extern gdbarch_use_struct_convention_ftype always_use_struct_convention;

/* Only structures, unions, and arrays are returned using the struct
   convention.  Note that arrays are never passed by value in the C
   language family, so that case is irrelevant for C.  */
extern gdbarch_return_value_on_stack_ftype generic_return_value_on_stack_not;

/* Backward compatible call_dummy_words. */
extern LONGEST legacy_call_dummy_words[];
extern int legacy_sizeof_call_dummy_words;

/* Typical remote_translate_xfer_address */
extern gdbarch_remote_translate_xfer_address_ftype generic_remote_translate_xfer_address;

/* The only possible cases for inner_than. */
extern int core_addr_lessthan (CORE_ADDR lhs, CORE_ADDR rhs);
extern int core_addr_greaterthan (CORE_ADDR lhs, CORE_ADDR rhs);

/* Floating point values. */
extern const struct floatformat *default_float_format (struct gdbarch *gdbarch);
extern const struct floatformat *default_double_format (struct gdbarch *gdbarch);

/* Identity functions on a CORE_ADDR.  Just return the "addr".  */

extern CORE_ADDR core_addr_identity (CORE_ADDR addr);
extern gdbarch_convert_from_func_ptr_addr_ftype convert_from_func_ptr_addr_identity;

/* No-op conversion of reg to regnum. */

extern int no_op_reg_to_regnum (int reg);

/* Versions of init_frame_pc().  Do nothing; do the default. */

extern CORE_ADDR deprecated_init_frame_pc_default (int fromleaf, struct frame_info *prev);

/* Do nothing version of elf_make_msymbol_special. */

void default_elf_make_msymbol_special (asymbol *sym, struct minimal_symbol *msym);

/* Do nothing version of coff_make_msymbol_special. */

void default_coff_make_msymbol_special (int val, struct minimal_symbol *msym);

/* Version of cannot_fetch_register() / cannot_store_register() that
   always fails. */

int cannot_register_not (int regnum);

/* Legacy version of target_virtual_frame_pointer().  Assumes that
   there is an DEPRECATED_FP_REGNUM and that it is the same, cooked or
   raw.  */

extern gdbarch_virtual_frame_pointer_ftype legacy_virtual_frame_pointer;

extern CORE_ADDR generic_skip_trampoline_code (CORE_ADDR pc);

extern CORE_ADDR generic_skip_solib_resolver (struct gdbarch *gdbarch,
					      CORE_ADDR pc);

extern int generic_in_solib_call_trampoline (CORE_ADDR pc, char *name);

extern int generic_in_solib_return_trampoline (CORE_ADDR pc, char *name);

extern int generic_in_function_epilogue_p (struct gdbarch *gdbarch, CORE_ADDR pc);

/* Assume that the world is sane, a registers raw and virtual size
   both match its type.  */

extern int generic_register_size (int regnum);

/* Assume that the world is sane, the registers are all adjacent.  */
extern int generic_register_byte (int regnum);

/* Prop up old targets that use various IN_SIGTRAMP() macros.  */
extern int legacy_pc_in_sigtramp (CORE_ADDR pc, char *name);

/* The orginal register_convert*() functions were overloaded.  They
   were used to both: convert between virtual and raw register formats
   (something that is discouraged); and to convert a register to the
   type of a corresponding variable.  These legacy functions preserve
   that overloaded behavour in existing targets.  */
extern int legacy_convert_register_p (int regnum, struct type *type);
extern void legacy_register_to_value (struct frame_info *frame, int regnum,
				      struct type *type, void *to);
extern void legacy_value_to_register (struct frame_info *frame, int regnum,
				      struct type *type, const void *from);

extern int default_stabs_argument_has_addr (struct gdbarch *gdbarch,
					    struct type *type);

/* For compatibility with older architectures, returns
   (LEGACY_SIM_REGNO_IGNORE) when the register doesn't have a valid
   name.  */

extern int legacy_register_sim_regno (int regnum);

/* Return the selected byte order, or BFD_ENDIAN_UNKNOWN if no byte
   order was explicitly selected.  */
extern enum bfd_endian selected_byte_order (void);

/* Return the selected architecture's name, or NULL if no architecture
   was explicitly selected.  */
extern const char *selected_architecture_name (void);

/* Initialize a ``struct info''.  Can't use memset(0) since some
   default values are not zero.  "fill" takes all available
   information and fills in any unspecified fields.  */

extern void gdbarch_info_init (struct gdbarch_info *info);
extern void gdbarch_info_fill (struct gdbarch *gdbarch,
			       struct gdbarch_info *info);

/* Similar to init, but this time fill in the blanks.  Information is
   obtained from the specified architecture, global "set ..." options,
   and explicitly initialized INFO fields.  */
extern void gdbarch_info_fill (struct gdbarch *gdbarch,
			       struct gdbarch_info *info);

/* Return the architecture for ABFD.  If no suitable architecture
   could be find, return NULL.  */

extern struct gdbarch *gdbarch_from_bfd (bfd *abfd);

#endif
