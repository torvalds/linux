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

#ifndef GDBARCH_H
#define GDBARCH_H

struct floatformat;
struct ui_file;
struct frame_info;
struct value;
struct objfile;
struct minimal_symbol;
struct regcache;
struct reggroup;
struct regset;
struct disassemble_info;
struct target_ops;

extern struct gdbarch *current_gdbarch;


/* If any of the following are defined, the target wasn't correctly
   converted. */

#if (GDB_MULTI_ARCH >= GDB_MULTI_ARCH_PURE) && defined (GDB_TM_FILE)
#error "GDB_TM_FILE: Pure multi-arch targets do not have a tm.h file."
#endif


/* The following are pre-initialized by GDBARCH. */

extern const struct bfd_arch_info * gdbarch_bfd_arch_info (struct gdbarch *gdbarch);
/* set_gdbarch_bfd_arch_info() - not applicable - pre-initialized. */
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_ARCHITECTURE)
#error "Non multi-arch definition of TARGET_ARCHITECTURE"
#endif
#if !defined (TARGET_ARCHITECTURE)
#define TARGET_ARCHITECTURE (gdbarch_bfd_arch_info (current_gdbarch))
#endif

extern int gdbarch_byte_order (struct gdbarch *gdbarch);
/* set_gdbarch_byte_order() - not applicable - pre-initialized. */
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_BYTE_ORDER)
#error "Non multi-arch definition of TARGET_BYTE_ORDER"
#endif
#if !defined (TARGET_BYTE_ORDER)
#define TARGET_BYTE_ORDER (gdbarch_byte_order (current_gdbarch))
#endif

extern enum gdb_osabi gdbarch_osabi (struct gdbarch *gdbarch);
/* set_gdbarch_osabi() - not applicable - pre-initialized. */
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_OSABI)
#error "Non multi-arch definition of TARGET_OSABI"
#endif
#if !defined (TARGET_OSABI)
#define TARGET_OSABI (gdbarch_osabi (current_gdbarch))
#endif


/* The following are initialized by the target dependent code. */

/* Number of bits in a char or unsigned char for the target machine.
   Just like CHAR_BIT in <limits.h> but describes the target machine.
   v:2:TARGET_CHAR_BIT:int:char_bit::::8 * sizeof (char):8::0:
  
   Number of bits in a short or unsigned short for the target machine. */

extern int gdbarch_short_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_short_bit (struct gdbarch *gdbarch, int short_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_SHORT_BIT)
#error "Non multi-arch definition of TARGET_SHORT_BIT"
#endif
#if !defined (TARGET_SHORT_BIT)
#define TARGET_SHORT_BIT (gdbarch_short_bit (current_gdbarch))
#endif

/* Number of bits in an int or unsigned int for the target machine. */

extern int gdbarch_int_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_int_bit (struct gdbarch *gdbarch, int int_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_INT_BIT)
#error "Non multi-arch definition of TARGET_INT_BIT"
#endif
#if !defined (TARGET_INT_BIT)
#define TARGET_INT_BIT (gdbarch_int_bit (current_gdbarch))
#endif

/* Number of bits in a long or unsigned long for the target machine. */

extern int gdbarch_long_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_long_bit (struct gdbarch *gdbarch, int long_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_LONG_BIT)
#error "Non multi-arch definition of TARGET_LONG_BIT"
#endif
#if !defined (TARGET_LONG_BIT)
#define TARGET_LONG_BIT (gdbarch_long_bit (current_gdbarch))
#endif

/* Number of bits in a long long or unsigned long long for the target
   machine. */

extern int gdbarch_long_long_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_long_long_bit (struct gdbarch *gdbarch, int long_long_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_LONG_LONG_BIT)
#error "Non multi-arch definition of TARGET_LONG_LONG_BIT"
#endif
#if !defined (TARGET_LONG_LONG_BIT)
#define TARGET_LONG_LONG_BIT (gdbarch_long_long_bit (current_gdbarch))
#endif

/* Number of bits in a float for the target machine. */

extern int gdbarch_float_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_float_bit (struct gdbarch *gdbarch, int float_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_FLOAT_BIT)
#error "Non multi-arch definition of TARGET_FLOAT_BIT"
#endif
#if !defined (TARGET_FLOAT_BIT)
#define TARGET_FLOAT_BIT (gdbarch_float_bit (current_gdbarch))
#endif

/* Number of bits in a double for the target machine. */

extern int gdbarch_double_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_double_bit (struct gdbarch *gdbarch, int double_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_DOUBLE_BIT)
#error "Non multi-arch definition of TARGET_DOUBLE_BIT"
#endif
#if !defined (TARGET_DOUBLE_BIT)
#define TARGET_DOUBLE_BIT (gdbarch_double_bit (current_gdbarch))
#endif

/* Number of bits in a long double for the target machine. */

extern int gdbarch_long_double_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_long_double_bit (struct gdbarch *gdbarch, int long_double_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_LONG_DOUBLE_BIT)
#error "Non multi-arch definition of TARGET_LONG_DOUBLE_BIT"
#endif
#if !defined (TARGET_LONG_DOUBLE_BIT)
#define TARGET_LONG_DOUBLE_BIT (gdbarch_long_double_bit (current_gdbarch))
#endif

/* For most targets, a pointer on the target and its representation as an
   address in GDB have the same size and "look the same".  For such a
   target, you need only set TARGET_PTR_BIT / ptr_bit and TARGET_ADDR_BIT
   / addr_bit will be set from it.
  
   If TARGET_PTR_BIT and TARGET_ADDR_BIT are different, you'll probably
   also need to set POINTER_TO_ADDRESS and ADDRESS_TO_POINTER as well.
  
   ptr_bit is the size of a pointer on the target */

extern int gdbarch_ptr_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_ptr_bit (struct gdbarch *gdbarch, int ptr_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_PTR_BIT)
#error "Non multi-arch definition of TARGET_PTR_BIT"
#endif
#if !defined (TARGET_PTR_BIT)
#define TARGET_PTR_BIT (gdbarch_ptr_bit (current_gdbarch))
#endif

/* addr_bit is the size of a target address as represented in gdb */

extern int gdbarch_addr_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_addr_bit (struct gdbarch *gdbarch, int addr_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_ADDR_BIT)
#error "Non multi-arch definition of TARGET_ADDR_BIT"
#endif
#if !defined (TARGET_ADDR_BIT)
#define TARGET_ADDR_BIT (gdbarch_addr_bit (current_gdbarch))
#endif

/* Number of bits in a BFD_VMA for the target object file format. */

extern int gdbarch_bfd_vma_bit (struct gdbarch *gdbarch);
extern void set_gdbarch_bfd_vma_bit (struct gdbarch *gdbarch, int bfd_vma_bit);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_BFD_VMA_BIT)
#error "Non multi-arch definition of TARGET_BFD_VMA_BIT"
#endif
#if !defined (TARGET_BFD_VMA_BIT)
#define TARGET_BFD_VMA_BIT (gdbarch_bfd_vma_bit (current_gdbarch))
#endif

/* One if `char' acts like `signed char', zero if `unsigned char'. */

extern int gdbarch_char_signed (struct gdbarch *gdbarch);
extern void set_gdbarch_char_signed (struct gdbarch *gdbarch, int char_signed);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_CHAR_SIGNED)
#error "Non multi-arch definition of TARGET_CHAR_SIGNED"
#endif
#if !defined (TARGET_CHAR_SIGNED)
#define TARGET_CHAR_SIGNED (gdbarch_char_signed (current_gdbarch))
#endif

#if defined (TARGET_READ_PC)
/* Legacy for systems yet to multi-arch TARGET_READ_PC */
#if !defined (TARGET_READ_PC_P)
#define TARGET_READ_PC_P() (1)
#endif
#endif

extern int gdbarch_read_pc_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_READ_PC_P)
#error "Non multi-arch definition of TARGET_READ_PC"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (TARGET_READ_PC_P)
#define TARGET_READ_PC_P() (gdbarch_read_pc_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_read_pc_ftype) (ptid_t ptid);
extern CORE_ADDR gdbarch_read_pc (struct gdbarch *gdbarch, ptid_t ptid);
extern void set_gdbarch_read_pc (struct gdbarch *gdbarch, gdbarch_read_pc_ftype *read_pc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_READ_PC)
#error "Non multi-arch definition of TARGET_READ_PC"
#endif
#if !defined (TARGET_READ_PC)
#define TARGET_READ_PC(ptid) (gdbarch_read_pc (current_gdbarch, ptid))
#endif

typedef void (gdbarch_write_pc_ftype) (CORE_ADDR val, ptid_t ptid);
extern void gdbarch_write_pc (struct gdbarch *gdbarch, CORE_ADDR val, ptid_t ptid);
extern void set_gdbarch_write_pc (struct gdbarch *gdbarch, gdbarch_write_pc_ftype *write_pc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_WRITE_PC)
#error "Non multi-arch definition of TARGET_WRITE_PC"
#endif
#if !defined (TARGET_WRITE_PC)
#define TARGET_WRITE_PC(val, ptid) (gdbarch_write_pc (current_gdbarch, val, ptid))
#endif

/* UNWIND_SP is a direct replacement for TARGET_READ_SP. */

#if defined (TARGET_READ_SP)
/* Legacy for systems yet to multi-arch TARGET_READ_SP */
#if !defined (TARGET_READ_SP_P)
#define TARGET_READ_SP_P() (1)
#endif
#endif

extern int gdbarch_read_sp_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_READ_SP_P)
#error "Non multi-arch definition of TARGET_READ_SP"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (TARGET_READ_SP_P)
#define TARGET_READ_SP_P() (gdbarch_read_sp_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_read_sp_ftype) (void);
extern CORE_ADDR gdbarch_read_sp (struct gdbarch *gdbarch);
extern void set_gdbarch_read_sp (struct gdbarch *gdbarch, gdbarch_read_sp_ftype *read_sp);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_READ_SP)
#error "Non multi-arch definition of TARGET_READ_SP"
#endif
#if !defined (TARGET_READ_SP)
#define TARGET_READ_SP() (gdbarch_read_sp (current_gdbarch))
#endif

/* Function for getting target's idea of a frame pointer.  FIXME: GDB's
   whole scheme for dealing with "frames" and "frame pointers" needs a
   serious shakedown. */

typedef void (gdbarch_virtual_frame_pointer_ftype) (CORE_ADDR pc, int *frame_regnum, LONGEST *frame_offset);
extern void gdbarch_virtual_frame_pointer (struct gdbarch *gdbarch, CORE_ADDR pc, int *frame_regnum, LONGEST *frame_offset);
extern void set_gdbarch_virtual_frame_pointer (struct gdbarch *gdbarch, gdbarch_virtual_frame_pointer_ftype *virtual_frame_pointer);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_VIRTUAL_FRAME_POINTER)
#error "Non multi-arch definition of TARGET_VIRTUAL_FRAME_POINTER"
#endif
#if !defined (TARGET_VIRTUAL_FRAME_POINTER)
#define TARGET_VIRTUAL_FRAME_POINTER(pc, frame_regnum, frame_offset) (gdbarch_virtual_frame_pointer (current_gdbarch, pc, frame_regnum, frame_offset))
#endif

extern int gdbarch_pseudo_register_read_p (struct gdbarch *gdbarch);

typedef void (gdbarch_pseudo_register_read_ftype) (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, void *buf);
extern void gdbarch_pseudo_register_read (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, void *buf);
extern void set_gdbarch_pseudo_register_read (struct gdbarch *gdbarch, gdbarch_pseudo_register_read_ftype *pseudo_register_read);

extern int gdbarch_pseudo_register_write_p (struct gdbarch *gdbarch);

typedef void (gdbarch_pseudo_register_write_ftype) (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, const void *buf);
extern void gdbarch_pseudo_register_write (struct gdbarch *gdbarch, struct regcache *regcache, int cookednum, const void *buf);
extern void set_gdbarch_pseudo_register_write (struct gdbarch *gdbarch, gdbarch_pseudo_register_write_ftype *pseudo_register_write);

extern int gdbarch_num_regs (struct gdbarch *gdbarch);
extern void set_gdbarch_num_regs (struct gdbarch *gdbarch, int num_regs);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (NUM_REGS)
#error "Non multi-arch definition of NUM_REGS"
#endif
#if !defined (NUM_REGS)
#define NUM_REGS (gdbarch_num_regs (current_gdbarch))
#endif

/* This macro gives the number of pseudo-registers that live in the
   register namespace but do not get fetched or stored on the target.
   These pseudo-registers may be aliases for other registers,
   combinations of other registers, or they may be computed by GDB. */

extern int gdbarch_num_pseudo_regs (struct gdbarch *gdbarch);
extern void set_gdbarch_num_pseudo_regs (struct gdbarch *gdbarch, int num_pseudo_regs);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (NUM_PSEUDO_REGS)
#error "Non multi-arch definition of NUM_PSEUDO_REGS"
#endif
#if !defined (NUM_PSEUDO_REGS)
#define NUM_PSEUDO_REGS (gdbarch_num_pseudo_regs (current_gdbarch))
#endif

/* GDB's standard (or well known) register numbers.  These can map onto
   a real register or a pseudo (computed) register or not be defined at
   all (-1).
   SP_REGNUM will hopefully be replaced by UNWIND_SP. */

extern int gdbarch_sp_regnum (struct gdbarch *gdbarch);
extern void set_gdbarch_sp_regnum (struct gdbarch *gdbarch, int sp_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SP_REGNUM)
#error "Non multi-arch definition of SP_REGNUM"
#endif
#if !defined (SP_REGNUM)
#define SP_REGNUM (gdbarch_sp_regnum (current_gdbarch))
#endif

extern int gdbarch_pc_regnum (struct gdbarch *gdbarch);
extern void set_gdbarch_pc_regnum (struct gdbarch *gdbarch, int pc_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (PC_REGNUM)
#error "Non multi-arch definition of PC_REGNUM"
#endif
#if !defined (PC_REGNUM)
#define PC_REGNUM (gdbarch_pc_regnum (current_gdbarch))
#endif

extern int gdbarch_ps_regnum (struct gdbarch *gdbarch);
extern void set_gdbarch_ps_regnum (struct gdbarch *gdbarch, int ps_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (PS_REGNUM)
#error "Non multi-arch definition of PS_REGNUM"
#endif
#if !defined (PS_REGNUM)
#define PS_REGNUM (gdbarch_ps_regnum (current_gdbarch))
#endif

extern int gdbarch_fp0_regnum (struct gdbarch *gdbarch);
extern void set_gdbarch_fp0_regnum (struct gdbarch *gdbarch, int fp0_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FP0_REGNUM)
#error "Non multi-arch definition of FP0_REGNUM"
#endif
#if !defined (FP0_REGNUM)
#define FP0_REGNUM (gdbarch_fp0_regnum (current_gdbarch))
#endif

/* Convert stab register number (from `r' declaration) to a gdb REGNUM. */

typedef int (gdbarch_stab_reg_to_regnum_ftype) (int stab_regnr);
extern int gdbarch_stab_reg_to_regnum (struct gdbarch *gdbarch, int stab_regnr);
extern void set_gdbarch_stab_reg_to_regnum (struct gdbarch *gdbarch, gdbarch_stab_reg_to_regnum_ftype *stab_reg_to_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (STAB_REG_TO_REGNUM)
#error "Non multi-arch definition of STAB_REG_TO_REGNUM"
#endif
#if !defined (STAB_REG_TO_REGNUM)
#define STAB_REG_TO_REGNUM(stab_regnr) (gdbarch_stab_reg_to_regnum (current_gdbarch, stab_regnr))
#endif

/* Provide a default mapping from a ecoff register number to a gdb REGNUM. */

typedef int (gdbarch_ecoff_reg_to_regnum_ftype) (int ecoff_regnr);
extern int gdbarch_ecoff_reg_to_regnum (struct gdbarch *gdbarch, int ecoff_regnr);
extern void set_gdbarch_ecoff_reg_to_regnum (struct gdbarch *gdbarch, gdbarch_ecoff_reg_to_regnum_ftype *ecoff_reg_to_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ECOFF_REG_TO_REGNUM)
#error "Non multi-arch definition of ECOFF_REG_TO_REGNUM"
#endif
#if !defined (ECOFF_REG_TO_REGNUM)
#define ECOFF_REG_TO_REGNUM(ecoff_regnr) (gdbarch_ecoff_reg_to_regnum (current_gdbarch, ecoff_regnr))
#endif

/* Provide a default mapping from a DWARF register number to a gdb REGNUM. */

typedef int (gdbarch_dwarf_reg_to_regnum_ftype) (int dwarf_regnr);
extern int gdbarch_dwarf_reg_to_regnum (struct gdbarch *gdbarch, int dwarf_regnr);
extern void set_gdbarch_dwarf_reg_to_regnum (struct gdbarch *gdbarch, gdbarch_dwarf_reg_to_regnum_ftype *dwarf_reg_to_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DWARF_REG_TO_REGNUM)
#error "Non multi-arch definition of DWARF_REG_TO_REGNUM"
#endif
#if !defined (DWARF_REG_TO_REGNUM)
#define DWARF_REG_TO_REGNUM(dwarf_regnr) (gdbarch_dwarf_reg_to_regnum (current_gdbarch, dwarf_regnr))
#endif

/* Convert from an sdb register number to an internal gdb register number. */

typedef int (gdbarch_sdb_reg_to_regnum_ftype) (int sdb_regnr);
extern int gdbarch_sdb_reg_to_regnum (struct gdbarch *gdbarch, int sdb_regnr);
extern void set_gdbarch_sdb_reg_to_regnum (struct gdbarch *gdbarch, gdbarch_sdb_reg_to_regnum_ftype *sdb_reg_to_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SDB_REG_TO_REGNUM)
#error "Non multi-arch definition of SDB_REG_TO_REGNUM"
#endif
#if !defined (SDB_REG_TO_REGNUM)
#define SDB_REG_TO_REGNUM(sdb_regnr) (gdbarch_sdb_reg_to_regnum (current_gdbarch, sdb_regnr))
#endif

typedef int (gdbarch_dwarf2_reg_to_regnum_ftype) (int dwarf2_regnr);
extern int gdbarch_dwarf2_reg_to_regnum (struct gdbarch *gdbarch, int dwarf2_regnr);
extern void set_gdbarch_dwarf2_reg_to_regnum (struct gdbarch *gdbarch, gdbarch_dwarf2_reg_to_regnum_ftype *dwarf2_reg_to_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DWARF2_REG_TO_REGNUM)
#error "Non multi-arch definition of DWARF2_REG_TO_REGNUM"
#endif
#if !defined (DWARF2_REG_TO_REGNUM)
#define DWARF2_REG_TO_REGNUM(dwarf2_regnr) (gdbarch_dwarf2_reg_to_regnum (current_gdbarch, dwarf2_regnr))
#endif

typedef const char * (gdbarch_register_name_ftype) (int regnr);
extern const char * gdbarch_register_name (struct gdbarch *gdbarch, int regnr);
extern void set_gdbarch_register_name (struct gdbarch *gdbarch, gdbarch_register_name_ftype *register_name);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (REGISTER_NAME)
#error "Non multi-arch definition of REGISTER_NAME"
#endif
#if !defined (REGISTER_NAME)
#define REGISTER_NAME(regnr) (gdbarch_register_name (current_gdbarch, regnr))
#endif

/* REGISTER_TYPE is a direct replacement for DEPRECATED_REGISTER_VIRTUAL_TYPE. */

extern int gdbarch_register_type_p (struct gdbarch *gdbarch);

typedef struct type * (gdbarch_register_type_ftype) (struct gdbarch *gdbarch, int reg_nr);
extern struct type * gdbarch_register_type (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_register_type (struct gdbarch *gdbarch, gdbarch_register_type_ftype *register_type);

/* REGISTER_TYPE is a direct replacement for DEPRECATED_REGISTER_VIRTUAL_TYPE. */

#if defined (DEPRECATED_REGISTER_VIRTUAL_TYPE)
/* Legacy for systems yet to multi-arch DEPRECATED_REGISTER_VIRTUAL_TYPE */
#if !defined (DEPRECATED_REGISTER_VIRTUAL_TYPE_P)
#define DEPRECATED_REGISTER_VIRTUAL_TYPE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_register_virtual_type_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_VIRTUAL_TYPE_P)
#error "Non multi-arch definition of DEPRECATED_REGISTER_VIRTUAL_TYPE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REGISTER_VIRTUAL_TYPE_P)
#define DEPRECATED_REGISTER_VIRTUAL_TYPE_P() (gdbarch_deprecated_register_virtual_type_p (current_gdbarch))
#endif

typedef struct type * (gdbarch_deprecated_register_virtual_type_ftype) (int reg_nr);
extern struct type * gdbarch_deprecated_register_virtual_type (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_deprecated_register_virtual_type (struct gdbarch *gdbarch, gdbarch_deprecated_register_virtual_type_ftype *deprecated_register_virtual_type);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_VIRTUAL_TYPE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_VIRTUAL_TYPE"
#endif
#if !defined (DEPRECATED_REGISTER_VIRTUAL_TYPE)
#define DEPRECATED_REGISTER_VIRTUAL_TYPE(reg_nr) (gdbarch_deprecated_register_virtual_type (current_gdbarch, reg_nr))
#endif

/* DEPRECATED_REGISTER_BYTES can be deleted.  The value is computed
   from REGISTER_TYPE. */

extern int gdbarch_deprecated_register_bytes (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_register_bytes (struct gdbarch *gdbarch, int deprecated_register_bytes);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_BYTES)
#error "Non multi-arch definition of DEPRECATED_REGISTER_BYTES"
#endif
#if !defined (DEPRECATED_REGISTER_BYTES)
#define DEPRECATED_REGISTER_BYTES (gdbarch_deprecated_register_bytes (current_gdbarch))
#endif

/* If the value returned by DEPRECATED_REGISTER_BYTE agrees with the
   register offsets computed using just REGISTER_TYPE, this can be
   deleted.  See: maint print registers.  NOTE: cagney/2002-05-02: This
   function with predicate has a valid (callable) initial value.  As a
   consequence, even when the predicate is false, the corresponding
   function works.  This simplifies the migration process - old code,
   calling DEPRECATED_REGISTER_BYTE, doesn't need to be modified. */

#if defined (DEPRECATED_REGISTER_BYTE)
/* Legacy for systems yet to multi-arch DEPRECATED_REGISTER_BYTE */
#if !defined (DEPRECATED_REGISTER_BYTE_P)
#define DEPRECATED_REGISTER_BYTE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_register_byte_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_BYTE_P)
#error "Non multi-arch definition of DEPRECATED_REGISTER_BYTE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REGISTER_BYTE_P)
#define DEPRECATED_REGISTER_BYTE_P() (gdbarch_deprecated_register_byte_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_register_byte_ftype) (int reg_nr);
extern int gdbarch_deprecated_register_byte (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_deprecated_register_byte (struct gdbarch *gdbarch, gdbarch_deprecated_register_byte_ftype *deprecated_register_byte);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_BYTE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_BYTE"
#endif
#if !defined (DEPRECATED_REGISTER_BYTE)
#define DEPRECATED_REGISTER_BYTE(reg_nr) (gdbarch_deprecated_register_byte (current_gdbarch, reg_nr))
#endif

/* If all registers have identical raw and virtual sizes and those
   sizes agree with the value computed from REGISTER_TYPE,
   DEPRECATED_REGISTER_RAW_SIZE can be deleted.  See: maint print
   registers. */

#if defined (DEPRECATED_REGISTER_RAW_SIZE)
/* Legacy for systems yet to multi-arch DEPRECATED_REGISTER_RAW_SIZE */
#if !defined (DEPRECATED_REGISTER_RAW_SIZE_P)
#define DEPRECATED_REGISTER_RAW_SIZE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_register_raw_size_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_RAW_SIZE_P)
#error "Non multi-arch definition of DEPRECATED_REGISTER_RAW_SIZE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REGISTER_RAW_SIZE_P)
#define DEPRECATED_REGISTER_RAW_SIZE_P() (gdbarch_deprecated_register_raw_size_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_register_raw_size_ftype) (int reg_nr);
extern int gdbarch_deprecated_register_raw_size (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_deprecated_register_raw_size (struct gdbarch *gdbarch, gdbarch_deprecated_register_raw_size_ftype *deprecated_register_raw_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_RAW_SIZE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_RAW_SIZE"
#endif
#if !defined (DEPRECATED_REGISTER_RAW_SIZE)
#define DEPRECATED_REGISTER_RAW_SIZE(reg_nr) (gdbarch_deprecated_register_raw_size (current_gdbarch, reg_nr))
#endif

/* If all registers have identical raw and virtual sizes and those
   sizes agree with the value computed from REGISTER_TYPE,
   DEPRECATED_REGISTER_VIRTUAL_SIZE can be deleted.  See: maint print
   registers. */

#if defined (DEPRECATED_REGISTER_VIRTUAL_SIZE)
/* Legacy for systems yet to multi-arch DEPRECATED_REGISTER_VIRTUAL_SIZE */
#if !defined (DEPRECATED_REGISTER_VIRTUAL_SIZE_P)
#define DEPRECATED_REGISTER_VIRTUAL_SIZE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_register_virtual_size_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_VIRTUAL_SIZE_P)
#error "Non multi-arch definition of DEPRECATED_REGISTER_VIRTUAL_SIZE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REGISTER_VIRTUAL_SIZE_P)
#define DEPRECATED_REGISTER_VIRTUAL_SIZE_P() (gdbarch_deprecated_register_virtual_size_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_register_virtual_size_ftype) (int reg_nr);
extern int gdbarch_deprecated_register_virtual_size (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_deprecated_register_virtual_size (struct gdbarch *gdbarch, gdbarch_deprecated_register_virtual_size_ftype *deprecated_register_virtual_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_VIRTUAL_SIZE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_VIRTUAL_SIZE"
#endif
#if !defined (DEPRECATED_REGISTER_VIRTUAL_SIZE)
#define DEPRECATED_REGISTER_VIRTUAL_SIZE(reg_nr) (gdbarch_deprecated_register_virtual_size (current_gdbarch, reg_nr))
#endif

/* DEPRECATED_MAX_REGISTER_RAW_SIZE can be deleted.  It has been
   replaced by the constant MAX_REGISTER_SIZE. */

#if defined (DEPRECATED_MAX_REGISTER_RAW_SIZE)
/* Legacy for systems yet to multi-arch DEPRECATED_MAX_REGISTER_RAW_SIZE */
#if !defined (DEPRECATED_MAX_REGISTER_RAW_SIZE_P)
#define DEPRECATED_MAX_REGISTER_RAW_SIZE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_max_register_raw_size_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_MAX_REGISTER_RAW_SIZE_P)
#error "Non multi-arch definition of DEPRECATED_MAX_REGISTER_RAW_SIZE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_MAX_REGISTER_RAW_SIZE_P)
#define DEPRECATED_MAX_REGISTER_RAW_SIZE_P() (gdbarch_deprecated_max_register_raw_size_p (current_gdbarch))
#endif

extern int gdbarch_deprecated_max_register_raw_size (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_max_register_raw_size (struct gdbarch *gdbarch, int deprecated_max_register_raw_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_MAX_REGISTER_RAW_SIZE)
#error "Non multi-arch definition of DEPRECATED_MAX_REGISTER_RAW_SIZE"
#endif
#if !defined (DEPRECATED_MAX_REGISTER_RAW_SIZE)
#define DEPRECATED_MAX_REGISTER_RAW_SIZE (gdbarch_deprecated_max_register_raw_size (current_gdbarch))
#endif

/* DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE can be deleted.  It has been
   replaced by the constant MAX_REGISTER_SIZE. */

#if defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE)
/* Legacy for systems yet to multi-arch DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE */
#if !defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P)
#define DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_max_register_virtual_size_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P)
#error "Non multi-arch definition of DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P)
#define DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE_P() (gdbarch_deprecated_max_register_virtual_size_p (current_gdbarch))
#endif

extern int gdbarch_deprecated_max_register_virtual_size (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_max_register_virtual_size (struct gdbarch *gdbarch, int deprecated_max_register_virtual_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE)
#error "Non multi-arch definition of DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE"
#endif
#if !defined (DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE)
#define DEPRECATED_MAX_REGISTER_VIRTUAL_SIZE (gdbarch_deprecated_max_register_virtual_size (current_gdbarch))
#endif

/* See gdbint.texinfo, and PUSH_DUMMY_CALL. */

extern int gdbarch_unwind_dummy_id_p (struct gdbarch *gdbarch);

typedef struct frame_id (gdbarch_unwind_dummy_id_ftype) (struct gdbarch *gdbarch, struct frame_info *info);
extern struct frame_id gdbarch_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *info);
extern void set_gdbarch_unwind_dummy_id (struct gdbarch *gdbarch, gdbarch_unwind_dummy_id_ftype *unwind_dummy_id);

/* Implement UNWIND_DUMMY_ID and PUSH_DUMMY_CALL, then delete
   SAVE_DUMMY_FRAME_TOS. */

#if defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS)
/* Legacy for systems yet to multi-arch DEPRECATED_SAVE_DUMMY_FRAME_TOS */
#if !defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS_P)
#define DEPRECATED_SAVE_DUMMY_FRAME_TOS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_save_dummy_frame_tos_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS_P)
#error "Non multi-arch definition of DEPRECATED_SAVE_DUMMY_FRAME_TOS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS_P)
#define DEPRECATED_SAVE_DUMMY_FRAME_TOS_P() (gdbarch_deprecated_save_dummy_frame_tos_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_save_dummy_frame_tos_ftype) (CORE_ADDR sp);
extern void gdbarch_deprecated_save_dummy_frame_tos (struct gdbarch *gdbarch, CORE_ADDR sp);
extern void set_gdbarch_deprecated_save_dummy_frame_tos (struct gdbarch *gdbarch, gdbarch_deprecated_save_dummy_frame_tos_ftype *deprecated_save_dummy_frame_tos);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS)
#error "Non multi-arch definition of DEPRECATED_SAVE_DUMMY_FRAME_TOS"
#endif
#if !defined (DEPRECATED_SAVE_DUMMY_FRAME_TOS)
#define DEPRECATED_SAVE_DUMMY_FRAME_TOS(sp) (gdbarch_deprecated_save_dummy_frame_tos (current_gdbarch, sp))
#endif

/* Implement UNWIND_DUMMY_ID and PUSH_DUMMY_CALL, then delete
   DEPRECATED_FP_REGNUM. */

extern int gdbarch_deprecated_fp_regnum (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_fp_regnum (struct gdbarch *gdbarch, int deprecated_fp_regnum);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FP_REGNUM)
#error "Non multi-arch definition of DEPRECATED_FP_REGNUM"
#endif
#if !defined (DEPRECATED_FP_REGNUM)
#define DEPRECATED_FP_REGNUM (gdbarch_deprecated_fp_regnum (current_gdbarch))
#endif

/* Implement UNWIND_DUMMY_ID and PUSH_DUMMY_CALL, then delete
   DEPRECATED_TARGET_READ_FP. */

#if defined (DEPRECATED_TARGET_READ_FP)
/* Legacy for systems yet to multi-arch DEPRECATED_TARGET_READ_FP */
#if !defined (DEPRECATED_TARGET_READ_FP_P)
#define DEPRECATED_TARGET_READ_FP_P() (1)
#endif
#endif

extern int gdbarch_deprecated_target_read_fp_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_TARGET_READ_FP_P)
#error "Non multi-arch definition of DEPRECATED_TARGET_READ_FP"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_TARGET_READ_FP_P)
#define DEPRECATED_TARGET_READ_FP_P() (gdbarch_deprecated_target_read_fp_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_target_read_fp_ftype) (void);
extern CORE_ADDR gdbarch_deprecated_target_read_fp (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_target_read_fp (struct gdbarch *gdbarch, gdbarch_deprecated_target_read_fp_ftype *deprecated_target_read_fp);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_TARGET_READ_FP)
#error "Non multi-arch definition of DEPRECATED_TARGET_READ_FP"
#endif
#if !defined (DEPRECATED_TARGET_READ_FP)
#define DEPRECATED_TARGET_READ_FP() (gdbarch_deprecated_target_read_fp (current_gdbarch))
#endif

/* See gdbint.texinfo.  See infcall.c.  New, all singing all dancing,
   replacement for DEPRECATED_PUSH_ARGUMENTS. */

extern int gdbarch_push_dummy_call_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_push_dummy_call_ftype) (struct gdbarch *gdbarch, CORE_ADDR func_addr, struct regcache *regcache, CORE_ADDR bp_addr, int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr);
extern CORE_ADDR gdbarch_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr, struct regcache *regcache, CORE_ADDR bp_addr, int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr);
extern void set_gdbarch_push_dummy_call (struct gdbarch *gdbarch, gdbarch_push_dummy_call_ftype *push_dummy_call);

/* PUSH_DUMMY_CALL is a direct replacement for DEPRECATED_PUSH_ARGUMENTS. */

#if defined (DEPRECATED_PUSH_ARGUMENTS)
/* Legacy for systems yet to multi-arch DEPRECATED_PUSH_ARGUMENTS */
#if !defined (DEPRECATED_PUSH_ARGUMENTS_P)
#define DEPRECATED_PUSH_ARGUMENTS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_push_arguments_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_ARGUMENTS_P)
#error "Non multi-arch definition of DEPRECATED_PUSH_ARGUMENTS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_PUSH_ARGUMENTS_P)
#define DEPRECATED_PUSH_ARGUMENTS_P() (gdbarch_deprecated_push_arguments_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_push_arguments_ftype) (int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr);
extern CORE_ADDR gdbarch_deprecated_push_arguments (struct gdbarch *gdbarch, int nargs, struct value **args, CORE_ADDR sp, int struct_return, CORE_ADDR struct_addr);
extern void set_gdbarch_deprecated_push_arguments (struct gdbarch *gdbarch, gdbarch_deprecated_push_arguments_ftype *deprecated_push_arguments);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_ARGUMENTS)
#error "Non multi-arch definition of DEPRECATED_PUSH_ARGUMENTS"
#endif
#if !defined (DEPRECATED_PUSH_ARGUMENTS)
#define DEPRECATED_PUSH_ARGUMENTS(nargs, args, sp, struct_return, struct_addr) (gdbarch_deprecated_push_arguments (current_gdbarch, nargs, args, sp, struct_return, struct_addr))
#endif

/* DEPRECATED_USE_GENERIC_DUMMY_FRAMES can be deleted.  Always true. */

extern int gdbarch_deprecated_use_generic_dummy_frames (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_use_generic_dummy_frames (struct gdbarch *gdbarch, int deprecated_use_generic_dummy_frames);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_USE_GENERIC_DUMMY_FRAMES)
#error "Non multi-arch definition of DEPRECATED_USE_GENERIC_DUMMY_FRAMES"
#endif
#if !defined (DEPRECATED_USE_GENERIC_DUMMY_FRAMES)
#define DEPRECATED_USE_GENERIC_DUMMY_FRAMES (gdbarch_deprecated_use_generic_dummy_frames (current_gdbarch))
#endif

/* Implement PUSH_RETURN_ADDRESS, and then merge in
   DEPRECATED_PUSH_RETURN_ADDRESS. */

#if defined (DEPRECATED_PUSH_RETURN_ADDRESS)
/* Legacy for systems yet to multi-arch DEPRECATED_PUSH_RETURN_ADDRESS */
#if !defined (DEPRECATED_PUSH_RETURN_ADDRESS_P)
#define DEPRECATED_PUSH_RETURN_ADDRESS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_push_return_address_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_RETURN_ADDRESS_P)
#error "Non multi-arch definition of DEPRECATED_PUSH_RETURN_ADDRESS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_PUSH_RETURN_ADDRESS_P)
#define DEPRECATED_PUSH_RETURN_ADDRESS_P() (gdbarch_deprecated_push_return_address_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_push_return_address_ftype) (CORE_ADDR pc, CORE_ADDR sp);
extern CORE_ADDR gdbarch_deprecated_push_return_address (struct gdbarch *gdbarch, CORE_ADDR pc, CORE_ADDR sp);
extern void set_gdbarch_deprecated_push_return_address (struct gdbarch *gdbarch, gdbarch_deprecated_push_return_address_ftype *deprecated_push_return_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_RETURN_ADDRESS)
#error "Non multi-arch definition of DEPRECATED_PUSH_RETURN_ADDRESS"
#endif
#if !defined (DEPRECATED_PUSH_RETURN_ADDRESS)
#define DEPRECATED_PUSH_RETURN_ADDRESS(pc, sp) (gdbarch_deprecated_push_return_address (current_gdbarch, pc, sp))
#endif

/* Implement PUSH_DUMMY_CALL, then merge in DEPRECATED_DUMMY_WRITE_SP. */

#if defined (DEPRECATED_DUMMY_WRITE_SP)
/* Legacy for systems yet to multi-arch DEPRECATED_DUMMY_WRITE_SP */
#if !defined (DEPRECATED_DUMMY_WRITE_SP_P)
#define DEPRECATED_DUMMY_WRITE_SP_P() (1)
#endif
#endif

extern int gdbarch_deprecated_dummy_write_sp_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_DUMMY_WRITE_SP_P)
#error "Non multi-arch definition of DEPRECATED_DUMMY_WRITE_SP"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_DUMMY_WRITE_SP_P)
#define DEPRECATED_DUMMY_WRITE_SP_P() (gdbarch_deprecated_dummy_write_sp_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_dummy_write_sp_ftype) (CORE_ADDR val);
extern void gdbarch_deprecated_dummy_write_sp (struct gdbarch *gdbarch, CORE_ADDR val);
extern void set_gdbarch_deprecated_dummy_write_sp (struct gdbarch *gdbarch, gdbarch_deprecated_dummy_write_sp_ftype *deprecated_dummy_write_sp);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_DUMMY_WRITE_SP)
#error "Non multi-arch definition of DEPRECATED_DUMMY_WRITE_SP"
#endif
#if !defined (DEPRECATED_DUMMY_WRITE_SP)
#define DEPRECATED_DUMMY_WRITE_SP(val) (gdbarch_deprecated_dummy_write_sp (current_gdbarch, val))
#endif

/* DEPRECATED_REGISTER_SIZE can be deleted. */

extern int gdbarch_deprecated_register_size (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_register_size (struct gdbarch *gdbarch, int deprecated_register_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_SIZE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_SIZE"
#endif
#if !defined (DEPRECATED_REGISTER_SIZE)
#define DEPRECATED_REGISTER_SIZE (gdbarch_deprecated_register_size (current_gdbarch))
#endif

extern int gdbarch_call_dummy_location (struct gdbarch *gdbarch);
extern void set_gdbarch_call_dummy_location (struct gdbarch *gdbarch, int call_dummy_location);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (CALL_DUMMY_LOCATION)
#error "Non multi-arch definition of CALL_DUMMY_LOCATION"
#endif
#if !defined (CALL_DUMMY_LOCATION)
#define CALL_DUMMY_LOCATION (gdbarch_call_dummy_location (current_gdbarch))
#endif

/* DEPRECATED_CALL_DUMMY_START_OFFSET can be deleted. */

extern CORE_ADDR gdbarch_deprecated_call_dummy_start_offset (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_call_dummy_start_offset (struct gdbarch *gdbarch, CORE_ADDR deprecated_call_dummy_start_offset);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_CALL_DUMMY_START_OFFSET)
#error "Non multi-arch definition of DEPRECATED_CALL_DUMMY_START_OFFSET"
#endif
#if !defined (DEPRECATED_CALL_DUMMY_START_OFFSET)
#define DEPRECATED_CALL_DUMMY_START_OFFSET (gdbarch_deprecated_call_dummy_start_offset (current_gdbarch))
#endif

/* DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET can be deleted. */

extern CORE_ADDR gdbarch_deprecated_call_dummy_breakpoint_offset (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_call_dummy_breakpoint_offset (struct gdbarch *gdbarch, CORE_ADDR deprecated_call_dummy_breakpoint_offset);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET)
#error "Non multi-arch definition of DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET"
#endif
#if !defined (DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET)
#define DEPRECATED_CALL_DUMMY_BREAKPOINT_OFFSET (gdbarch_deprecated_call_dummy_breakpoint_offset (current_gdbarch))
#endif

/* DEPRECATED_CALL_DUMMY_LENGTH can be deleted. */

extern int gdbarch_deprecated_call_dummy_length (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_call_dummy_length (struct gdbarch *gdbarch, int deprecated_call_dummy_length);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_CALL_DUMMY_LENGTH)
#error "Non multi-arch definition of DEPRECATED_CALL_DUMMY_LENGTH"
#endif
#if !defined (DEPRECATED_CALL_DUMMY_LENGTH)
#define DEPRECATED_CALL_DUMMY_LENGTH (gdbarch_deprecated_call_dummy_length (current_gdbarch))
#endif

/* DEPRECATED_CALL_DUMMY_WORDS can be deleted. */

extern LONGEST * gdbarch_deprecated_call_dummy_words (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_call_dummy_words (struct gdbarch *gdbarch, LONGEST * deprecated_call_dummy_words);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_CALL_DUMMY_WORDS)
#error "Non multi-arch definition of DEPRECATED_CALL_DUMMY_WORDS"
#endif
#if !defined (DEPRECATED_CALL_DUMMY_WORDS)
#define DEPRECATED_CALL_DUMMY_WORDS (gdbarch_deprecated_call_dummy_words (current_gdbarch))
#endif

/* Implement PUSH_DUMMY_CALL, then delete DEPRECATED_SIZEOF_CALL_DUMMY_WORDS. */

extern int gdbarch_deprecated_sizeof_call_dummy_words (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_sizeof_call_dummy_words (struct gdbarch *gdbarch, int deprecated_sizeof_call_dummy_words);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_SIZEOF_CALL_DUMMY_WORDS)
#error "Non multi-arch definition of DEPRECATED_SIZEOF_CALL_DUMMY_WORDS"
#endif
#if !defined (DEPRECATED_SIZEOF_CALL_DUMMY_WORDS)
#define DEPRECATED_SIZEOF_CALL_DUMMY_WORDS (gdbarch_deprecated_sizeof_call_dummy_words (current_gdbarch))
#endif

/* DEPRECATED_FIX_CALL_DUMMY can be deleted.  For the SPARC, implement
   PUSH_DUMMY_CODE and set CALL_DUMMY_LOCATION to ON_STACK. */

#if defined (DEPRECATED_FIX_CALL_DUMMY)
/* Legacy for systems yet to multi-arch DEPRECATED_FIX_CALL_DUMMY */
#if !defined (DEPRECATED_FIX_CALL_DUMMY_P)
#define DEPRECATED_FIX_CALL_DUMMY_P() (1)
#endif
#endif

extern int gdbarch_deprecated_fix_call_dummy_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FIX_CALL_DUMMY_P)
#error "Non multi-arch definition of DEPRECATED_FIX_CALL_DUMMY"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FIX_CALL_DUMMY_P)
#define DEPRECATED_FIX_CALL_DUMMY_P() (gdbarch_deprecated_fix_call_dummy_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_fix_call_dummy_ftype) (char *dummy, CORE_ADDR pc, CORE_ADDR fun, int nargs, struct value **args, struct type *type, int gcc_p);
extern void gdbarch_deprecated_fix_call_dummy (struct gdbarch *gdbarch, char *dummy, CORE_ADDR pc, CORE_ADDR fun, int nargs, struct value **args, struct type *type, int gcc_p);
extern void set_gdbarch_deprecated_fix_call_dummy (struct gdbarch *gdbarch, gdbarch_deprecated_fix_call_dummy_ftype *deprecated_fix_call_dummy);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FIX_CALL_DUMMY)
#error "Non multi-arch definition of DEPRECATED_FIX_CALL_DUMMY"
#endif
#if !defined (DEPRECATED_FIX_CALL_DUMMY)
#define DEPRECATED_FIX_CALL_DUMMY(dummy, pc, fun, nargs, args, type, gcc_p) (gdbarch_deprecated_fix_call_dummy (current_gdbarch, dummy, pc, fun, nargs, args, type, gcc_p))
#endif

/* This is a replacement for DEPRECATED_FIX_CALL_DUMMY et.al. */

extern int gdbarch_push_dummy_code_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_push_dummy_code_ftype) (struct gdbarch *gdbarch, CORE_ADDR sp, CORE_ADDR funaddr, int using_gcc, struct value **args, int nargs, struct type *value_type, CORE_ADDR *real_pc, CORE_ADDR *bp_addr);
extern CORE_ADDR gdbarch_push_dummy_code (struct gdbarch *gdbarch, CORE_ADDR sp, CORE_ADDR funaddr, int using_gcc, struct value **args, int nargs, struct type *value_type, CORE_ADDR *real_pc, CORE_ADDR *bp_addr);
extern void set_gdbarch_push_dummy_code (struct gdbarch *gdbarch, gdbarch_push_dummy_code_ftype *push_dummy_code);

/* Implement PUSH_DUMMY_CALL, then delete DEPRECATED_PUSH_DUMMY_FRAME. */

#if defined (DEPRECATED_PUSH_DUMMY_FRAME)
/* Legacy for systems yet to multi-arch DEPRECATED_PUSH_DUMMY_FRAME */
#if !defined (DEPRECATED_PUSH_DUMMY_FRAME_P)
#define DEPRECATED_PUSH_DUMMY_FRAME_P() (1)
#endif
#endif

extern int gdbarch_deprecated_push_dummy_frame_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_DUMMY_FRAME_P)
#error "Non multi-arch definition of DEPRECATED_PUSH_DUMMY_FRAME"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_PUSH_DUMMY_FRAME_P)
#define DEPRECATED_PUSH_DUMMY_FRAME_P() (gdbarch_deprecated_push_dummy_frame_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_push_dummy_frame_ftype) (void);
extern void gdbarch_deprecated_push_dummy_frame (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_push_dummy_frame (struct gdbarch *gdbarch, gdbarch_deprecated_push_dummy_frame_ftype *deprecated_push_dummy_frame);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PUSH_DUMMY_FRAME)
#error "Non multi-arch definition of DEPRECATED_PUSH_DUMMY_FRAME"
#endif
#if !defined (DEPRECATED_PUSH_DUMMY_FRAME)
#define DEPRECATED_PUSH_DUMMY_FRAME (gdbarch_deprecated_push_dummy_frame (current_gdbarch))
#endif

#if defined (DEPRECATED_DO_REGISTERS_INFO)
/* Legacy for systems yet to multi-arch DEPRECATED_DO_REGISTERS_INFO */
#if !defined (DEPRECATED_DO_REGISTERS_INFO_P)
#define DEPRECATED_DO_REGISTERS_INFO_P() (1)
#endif
#endif

extern int gdbarch_deprecated_do_registers_info_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_DO_REGISTERS_INFO_P)
#error "Non multi-arch definition of DEPRECATED_DO_REGISTERS_INFO"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_DO_REGISTERS_INFO_P)
#define DEPRECATED_DO_REGISTERS_INFO_P() (gdbarch_deprecated_do_registers_info_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_do_registers_info_ftype) (int reg_nr, int fpregs);
extern void gdbarch_deprecated_do_registers_info (struct gdbarch *gdbarch, int reg_nr, int fpregs);
extern void set_gdbarch_deprecated_do_registers_info (struct gdbarch *gdbarch, gdbarch_deprecated_do_registers_info_ftype *deprecated_do_registers_info);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_DO_REGISTERS_INFO)
#error "Non multi-arch definition of DEPRECATED_DO_REGISTERS_INFO"
#endif
#if !defined (DEPRECATED_DO_REGISTERS_INFO)
#define DEPRECATED_DO_REGISTERS_INFO(reg_nr, fpregs) (gdbarch_deprecated_do_registers_info (current_gdbarch, reg_nr, fpregs))
#endif

typedef void (gdbarch_print_registers_info_ftype) (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, int regnum, int all);
extern void gdbarch_print_registers_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, int regnum, int all);
extern void set_gdbarch_print_registers_info (struct gdbarch *gdbarch, gdbarch_print_registers_info_ftype *print_registers_info);

extern int gdbarch_print_float_info_p (struct gdbarch *gdbarch);

typedef void (gdbarch_print_float_info_ftype) (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args);
extern void gdbarch_print_float_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args);
extern void set_gdbarch_print_float_info (struct gdbarch *gdbarch, gdbarch_print_float_info_ftype *print_float_info);

extern int gdbarch_print_vector_info_p (struct gdbarch *gdbarch);

typedef void (gdbarch_print_vector_info_ftype) (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args);
extern void gdbarch_print_vector_info (struct gdbarch *gdbarch, struct ui_file *file, struct frame_info *frame, const char *args);
extern void set_gdbarch_print_vector_info (struct gdbarch *gdbarch, gdbarch_print_vector_info_ftype *print_vector_info);

/* MAP a GDB RAW register number onto a simulator register number.  See
   also include/...-sim.h. */

typedef int (gdbarch_register_sim_regno_ftype) (int reg_nr);
extern int gdbarch_register_sim_regno (struct gdbarch *gdbarch, int reg_nr);
extern void set_gdbarch_register_sim_regno (struct gdbarch *gdbarch, gdbarch_register_sim_regno_ftype *register_sim_regno);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (REGISTER_SIM_REGNO)
#error "Non multi-arch definition of REGISTER_SIM_REGNO"
#endif
#if !defined (REGISTER_SIM_REGNO)
#define REGISTER_SIM_REGNO(reg_nr) (gdbarch_register_sim_regno (current_gdbarch, reg_nr))
#endif

#if defined (REGISTER_BYTES_OK)
/* Legacy for systems yet to multi-arch REGISTER_BYTES_OK */
#if !defined (REGISTER_BYTES_OK_P)
#define REGISTER_BYTES_OK_P() (1)
#endif
#endif

extern int gdbarch_register_bytes_ok_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (REGISTER_BYTES_OK_P)
#error "Non multi-arch definition of REGISTER_BYTES_OK"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (REGISTER_BYTES_OK_P)
#define REGISTER_BYTES_OK_P() (gdbarch_register_bytes_ok_p (current_gdbarch))
#endif

typedef int (gdbarch_register_bytes_ok_ftype) (long nr_bytes);
extern int gdbarch_register_bytes_ok (struct gdbarch *gdbarch, long nr_bytes);
extern void set_gdbarch_register_bytes_ok (struct gdbarch *gdbarch, gdbarch_register_bytes_ok_ftype *register_bytes_ok);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (REGISTER_BYTES_OK)
#error "Non multi-arch definition of REGISTER_BYTES_OK"
#endif
#if !defined (REGISTER_BYTES_OK)
#define REGISTER_BYTES_OK(nr_bytes) (gdbarch_register_bytes_ok (current_gdbarch, nr_bytes))
#endif

typedef int (gdbarch_cannot_fetch_register_ftype) (int regnum);
extern int gdbarch_cannot_fetch_register (struct gdbarch *gdbarch, int regnum);
extern void set_gdbarch_cannot_fetch_register (struct gdbarch *gdbarch, gdbarch_cannot_fetch_register_ftype *cannot_fetch_register);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (CANNOT_FETCH_REGISTER)
#error "Non multi-arch definition of CANNOT_FETCH_REGISTER"
#endif
#if !defined (CANNOT_FETCH_REGISTER)
#define CANNOT_FETCH_REGISTER(regnum) (gdbarch_cannot_fetch_register (current_gdbarch, regnum))
#endif

typedef int (gdbarch_cannot_store_register_ftype) (int regnum);
extern int gdbarch_cannot_store_register (struct gdbarch *gdbarch, int regnum);
extern void set_gdbarch_cannot_store_register (struct gdbarch *gdbarch, gdbarch_cannot_store_register_ftype *cannot_store_register);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (CANNOT_STORE_REGISTER)
#error "Non multi-arch definition of CANNOT_STORE_REGISTER"
#endif
#if !defined (CANNOT_STORE_REGISTER)
#define CANNOT_STORE_REGISTER(regnum) (gdbarch_cannot_store_register (current_gdbarch, regnum))
#endif

/* setjmp/longjmp support. */

#if defined (GET_LONGJMP_TARGET)
/* Legacy for systems yet to multi-arch GET_LONGJMP_TARGET */
#if !defined (GET_LONGJMP_TARGET_P)
#define GET_LONGJMP_TARGET_P() (1)
#endif
#endif

extern int gdbarch_get_longjmp_target_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (GET_LONGJMP_TARGET_P)
#error "Non multi-arch definition of GET_LONGJMP_TARGET"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (GET_LONGJMP_TARGET_P)
#define GET_LONGJMP_TARGET_P() (gdbarch_get_longjmp_target_p (current_gdbarch))
#endif

typedef int (gdbarch_get_longjmp_target_ftype) (CORE_ADDR *pc);
extern int gdbarch_get_longjmp_target (struct gdbarch *gdbarch, CORE_ADDR *pc);
extern void set_gdbarch_get_longjmp_target (struct gdbarch *gdbarch, gdbarch_get_longjmp_target_ftype *get_longjmp_target);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (GET_LONGJMP_TARGET)
#error "Non multi-arch definition of GET_LONGJMP_TARGET"
#endif
#if !defined (GET_LONGJMP_TARGET)
#define GET_LONGJMP_TARGET(pc) (gdbarch_get_longjmp_target (current_gdbarch, pc))
#endif

/* NOTE: cagney/2002-11-24: This function with predicate has a valid
   (callable) initial value.  As a consequence, even when the predicate
   is false, the corresponding function works.  This simplifies the
   migration process - old code, calling DEPRECATED_PC_IN_CALL_DUMMY(),
   doesn't need to be modified. */

#if defined (DEPRECATED_PC_IN_CALL_DUMMY)
/* Legacy for systems yet to multi-arch DEPRECATED_PC_IN_CALL_DUMMY */
#if !defined (DEPRECATED_PC_IN_CALL_DUMMY_P)
#define DEPRECATED_PC_IN_CALL_DUMMY_P() (1)
#endif
#endif

extern int gdbarch_deprecated_pc_in_call_dummy_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PC_IN_CALL_DUMMY_P)
#error "Non multi-arch definition of DEPRECATED_PC_IN_CALL_DUMMY"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_PC_IN_CALL_DUMMY_P)
#define DEPRECATED_PC_IN_CALL_DUMMY_P() (gdbarch_deprecated_pc_in_call_dummy_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_pc_in_call_dummy_ftype) (CORE_ADDR pc, CORE_ADDR sp, CORE_ADDR frame_address);
extern int gdbarch_deprecated_pc_in_call_dummy (struct gdbarch *gdbarch, CORE_ADDR pc, CORE_ADDR sp, CORE_ADDR frame_address);
extern void set_gdbarch_deprecated_pc_in_call_dummy (struct gdbarch *gdbarch, gdbarch_deprecated_pc_in_call_dummy_ftype *deprecated_pc_in_call_dummy);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_PC_IN_CALL_DUMMY)
#error "Non multi-arch definition of DEPRECATED_PC_IN_CALL_DUMMY"
#endif
#if !defined (DEPRECATED_PC_IN_CALL_DUMMY)
#define DEPRECATED_PC_IN_CALL_DUMMY(pc, sp, frame_address) (gdbarch_deprecated_pc_in_call_dummy (current_gdbarch, pc, sp, frame_address))
#endif

#if defined (DEPRECATED_INIT_FRAME_PC_FIRST)
/* Legacy for systems yet to multi-arch DEPRECATED_INIT_FRAME_PC_FIRST */
#if !defined (DEPRECATED_INIT_FRAME_PC_FIRST_P)
#define DEPRECATED_INIT_FRAME_PC_FIRST_P() (1)
#endif
#endif

extern int gdbarch_deprecated_init_frame_pc_first_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_FRAME_PC_FIRST_P)
#error "Non multi-arch definition of DEPRECATED_INIT_FRAME_PC_FIRST"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_INIT_FRAME_PC_FIRST_P)
#define DEPRECATED_INIT_FRAME_PC_FIRST_P() (gdbarch_deprecated_init_frame_pc_first_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_init_frame_pc_first_ftype) (int fromleaf, struct frame_info *prev);
extern CORE_ADDR gdbarch_deprecated_init_frame_pc_first (struct gdbarch *gdbarch, int fromleaf, struct frame_info *prev);
extern void set_gdbarch_deprecated_init_frame_pc_first (struct gdbarch *gdbarch, gdbarch_deprecated_init_frame_pc_first_ftype *deprecated_init_frame_pc_first);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_FRAME_PC_FIRST)
#error "Non multi-arch definition of DEPRECATED_INIT_FRAME_PC_FIRST"
#endif
#if !defined (DEPRECATED_INIT_FRAME_PC_FIRST)
#define DEPRECATED_INIT_FRAME_PC_FIRST(fromleaf, prev) (gdbarch_deprecated_init_frame_pc_first (current_gdbarch, fromleaf, prev))
#endif

#if defined (DEPRECATED_INIT_FRAME_PC)
/* Legacy for systems yet to multi-arch DEPRECATED_INIT_FRAME_PC */
#if !defined (DEPRECATED_INIT_FRAME_PC_P)
#define DEPRECATED_INIT_FRAME_PC_P() (1)
#endif
#endif

extern int gdbarch_deprecated_init_frame_pc_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_FRAME_PC_P)
#error "Non multi-arch definition of DEPRECATED_INIT_FRAME_PC"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_INIT_FRAME_PC_P)
#define DEPRECATED_INIT_FRAME_PC_P() (gdbarch_deprecated_init_frame_pc_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_init_frame_pc_ftype) (int fromleaf, struct frame_info *prev);
extern CORE_ADDR gdbarch_deprecated_init_frame_pc (struct gdbarch *gdbarch, int fromleaf, struct frame_info *prev);
extern void set_gdbarch_deprecated_init_frame_pc (struct gdbarch *gdbarch, gdbarch_deprecated_init_frame_pc_ftype *deprecated_init_frame_pc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_FRAME_PC)
#error "Non multi-arch definition of DEPRECATED_INIT_FRAME_PC"
#endif
#if !defined (DEPRECATED_INIT_FRAME_PC)
#define DEPRECATED_INIT_FRAME_PC(fromleaf, prev) (gdbarch_deprecated_init_frame_pc (current_gdbarch, fromleaf, prev))
#endif

extern int gdbarch_believe_pcc_promotion (struct gdbarch *gdbarch);
extern void set_gdbarch_believe_pcc_promotion (struct gdbarch *gdbarch, int believe_pcc_promotion);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (BELIEVE_PCC_PROMOTION)
#error "Non multi-arch definition of BELIEVE_PCC_PROMOTION"
#endif
#if !defined (BELIEVE_PCC_PROMOTION)
#define BELIEVE_PCC_PROMOTION (gdbarch_believe_pcc_promotion (current_gdbarch))
#endif

extern int gdbarch_believe_pcc_promotion_type (struct gdbarch *gdbarch);
extern void set_gdbarch_believe_pcc_promotion_type (struct gdbarch *gdbarch, int believe_pcc_promotion_type);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (BELIEVE_PCC_PROMOTION_TYPE)
#error "Non multi-arch definition of BELIEVE_PCC_PROMOTION_TYPE"
#endif
#if !defined (BELIEVE_PCC_PROMOTION_TYPE)
#define BELIEVE_PCC_PROMOTION_TYPE (gdbarch_believe_pcc_promotion_type (current_gdbarch))
#endif

#if defined (DEPRECATED_GET_SAVED_REGISTER)
/* Legacy for systems yet to multi-arch DEPRECATED_GET_SAVED_REGISTER */
#if !defined (DEPRECATED_GET_SAVED_REGISTER_P)
#define DEPRECATED_GET_SAVED_REGISTER_P() (1)
#endif
#endif

extern int gdbarch_deprecated_get_saved_register_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_GET_SAVED_REGISTER_P)
#error "Non multi-arch definition of DEPRECATED_GET_SAVED_REGISTER"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_GET_SAVED_REGISTER_P)
#define DEPRECATED_GET_SAVED_REGISTER_P() (gdbarch_deprecated_get_saved_register_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_get_saved_register_ftype) (char *raw_buffer, int *optimized, CORE_ADDR *addrp, struct frame_info *frame, int regnum, enum lval_type *lval);
extern void gdbarch_deprecated_get_saved_register (struct gdbarch *gdbarch, char *raw_buffer, int *optimized, CORE_ADDR *addrp, struct frame_info *frame, int regnum, enum lval_type *lval);
extern void set_gdbarch_deprecated_get_saved_register (struct gdbarch *gdbarch, gdbarch_deprecated_get_saved_register_ftype *deprecated_get_saved_register);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_GET_SAVED_REGISTER)
#error "Non multi-arch definition of DEPRECATED_GET_SAVED_REGISTER"
#endif
#if !defined (DEPRECATED_GET_SAVED_REGISTER)
#define DEPRECATED_GET_SAVED_REGISTER(raw_buffer, optimized, addrp, frame, regnum, lval) (gdbarch_deprecated_get_saved_register (current_gdbarch, raw_buffer, optimized, addrp, frame, regnum, lval))
#endif

/* For register <-> value conversions, replaced by CONVERT_REGISTER_P et.al.
   For raw <-> cooked register conversions, replaced by pseudo registers. */

#if defined (DEPRECATED_REGISTER_CONVERTIBLE)
/* Legacy for systems yet to multi-arch DEPRECATED_REGISTER_CONVERTIBLE */
#if !defined (DEPRECATED_REGISTER_CONVERTIBLE_P)
#define DEPRECATED_REGISTER_CONVERTIBLE_P() (1)
#endif
#endif

extern int gdbarch_deprecated_register_convertible_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_CONVERTIBLE_P)
#error "Non multi-arch definition of DEPRECATED_REGISTER_CONVERTIBLE"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REGISTER_CONVERTIBLE_P)
#define DEPRECATED_REGISTER_CONVERTIBLE_P() (gdbarch_deprecated_register_convertible_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_register_convertible_ftype) (int nr);
extern int gdbarch_deprecated_register_convertible (struct gdbarch *gdbarch, int nr);
extern void set_gdbarch_deprecated_register_convertible (struct gdbarch *gdbarch, gdbarch_deprecated_register_convertible_ftype *deprecated_register_convertible);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_CONVERTIBLE)
#error "Non multi-arch definition of DEPRECATED_REGISTER_CONVERTIBLE"
#endif
#if !defined (DEPRECATED_REGISTER_CONVERTIBLE)
#define DEPRECATED_REGISTER_CONVERTIBLE(nr) (gdbarch_deprecated_register_convertible (current_gdbarch, nr))
#endif

/* For register <-> value conversions, replaced by CONVERT_REGISTER_P et.al.
   For raw <-> cooked register conversions, replaced by pseudo registers. */

typedef void (gdbarch_deprecated_register_convert_to_virtual_ftype) (int regnum, struct type *type, char *from, char *to);
extern void gdbarch_deprecated_register_convert_to_virtual (struct gdbarch *gdbarch, int regnum, struct type *type, char *from, char *to);
extern void set_gdbarch_deprecated_register_convert_to_virtual (struct gdbarch *gdbarch, gdbarch_deprecated_register_convert_to_virtual_ftype *deprecated_register_convert_to_virtual);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL)
#error "Non multi-arch definition of DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL"
#endif
#if !defined (DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL)
#define DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL(regnum, type, from, to) (gdbarch_deprecated_register_convert_to_virtual (current_gdbarch, regnum, type, from, to))
#endif

/* For register <-> value conversions, replaced by CONVERT_REGISTER_P et.al.
   For raw <-> cooked register conversions, replaced by pseudo registers. */

typedef void (gdbarch_deprecated_register_convert_to_raw_ftype) (struct type *type, int regnum, const char *from, char *to);
extern void gdbarch_deprecated_register_convert_to_raw (struct gdbarch *gdbarch, struct type *type, int regnum, const char *from, char *to);
extern void set_gdbarch_deprecated_register_convert_to_raw (struct gdbarch *gdbarch, gdbarch_deprecated_register_convert_to_raw_ftype *deprecated_register_convert_to_raw);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REGISTER_CONVERT_TO_RAW)
#error "Non multi-arch definition of DEPRECATED_REGISTER_CONVERT_TO_RAW"
#endif
#if !defined (DEPRECATED_REGISTER_CONVERT_TO_RAW)
#define DEPRECATED_REGISTER_CONVERT_TO_RAW(type, regnum, from, to) (gdbarch_deprecated_register_convert_to_raw (current_gdbarch, type, regnum, from, to))
#endif

typedef int (gdbarch_convert_register_p_ftype) (int regnum, struct type *type);
extern int gdbarch_convert_register_p (struct gdbarch *gdbarch, int regnum, struct type *type);
extern void set_gdbarch_convert_register_p (struct gdbarch *gdbarch, gdbarch_convert_register_p_ftype *convert_register_p);
#if (GDB_MULTI_ARCH >= GDB_MULTI_ARCH_PARTIAL) && defined (CONVERT_REGISTER_P)
#error "Non multi-arch definition of CONVERT_REGISTER_P"
#endif
#if !defined (CONVERT_REGISTER_P)
#define CONVERT_REGISTER_P(regnum, type) (gdbarch_convert_register_p (current_gdbarch, regnum, type))
#endif

typedef void (gdbarch_register_to_value_ftype) (struct frame_info *frame, int regnum, struct type *type, void *buf);
extern void gdbarch_register_to_value (struct gdbarch *gdbarch, struct frame_info *frame, int regnum, struct type *type, void *buf);
extern void set_gdbarch_register_to_value (struct gdbarch *gdbarch, gdbarch_register_to_value_ftype *register_to_value);
#if (GDB_MULTI_ARCH >= GDB_MULTI_ARCH_PARTIAL) && defined (REGISTER_TO_VALUE)
#error "Non multi-arch definition of REGISTER_TO_VALUE"
#endif
#if !defined (REGISTER_TO_VALUE)
#define REGISTER_TO_VALUE(frame, regnum, type, buf) (gdbarch_register_to_value (current_gdbarch, frame, regnum, type, buf))
#endif

typedef void (gdbarch_value_to_register_ftype) (struct frame_info *frame, int regnum, struct type *type, const void *buf);
extern void gdbarch_value_to_register (struct gdbarch *gdbarch, struct frame_info *frame, int regnum, struct type *type, const void *buf);
extern void set_gdbarch_value_to_register (struct gdbarch *gdbarch, gdbarch_value_to_register_ftype *value_to_register);
#if (GDB_MULTI_ARCH >= GDB_MULTI_ARCH_PARTIAL) && defined (VALUE_TO_REGISTER)
#error "Non multi-arch definition of VALUE_TO_REGISTER"
#endif
#if !defined (VALUE_TO_REGISTER)
#define VALUE_TO_REGISTER(frame, regnum, type, buf) (gdbarch_value_to_register (current_gdbarch, frame, regnum, type, buf))
#endif

typedef CORE_ADDR (gdbarch_pointer_to_address_ftype) (struct type *type, const void *buf);
extern CORE_ADDR gdbarch_pointer_to_address (struct gdbarch *gdbarch, struct type *type, const void *buf);
extern void set_gdbarch_pointer_to_address (struct gdbarch *gdbarch, gdbarch_pointer_to_address_ftype *pointer_to_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (POINTER_TO_ADDRESS)
#error "Non multi-arch definition of POINTER_TO_ADDRESS"
#endif
#if !defined (POINTER_TO_ADDRESS)
#define POINTER_TO_ADDRESS(type, buf) (gdbarch_pointer_to_address (current_gdbarch, type, buf))
#endif

typedef void (gdbarch_address_to_pointer_ftype) (struct type *type, void *buf, CORE_ADDR addr);
extern void gdbarch_address_to_pointer (struct gdbarch *gdbarch, struct type *type, void *buf, CORE_ADDR addr);
extern void set_gdbarch_address_to_pointer (struct gdbarch *gdbarch, gdbarch_address_to_pointer_ftype *address_to_pointer);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ADDRESS_TO_POINTER)
#error "Non multi-arch definition of ADDRESS_TO_POINTER"
#endif
#if !defined (ADDRESS_TO_POINTER)
#define ADDRESS_TO_POINTER(type, buf, addr) (gdbarch_address_to_pointer (current_gdbarch, type, buf, addr))
#endif

#if defined (INTEGER_TO_ADDRESS)
/* Legacy for systems yet to multi-arch INTEGER_TO_ADDRESS */
#if !defined (INTEGER_TO_ADDRESS_P)
#define INTEGER_TO_ADDRESS_P() (1)
#endif
#endif

extern int gdbarch_integer_to_address_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (INTEGER_TO_ADDRESS_P)
#error "Non multi-arch definition of INTEGER_TO_ADDRESS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (INTEGER_TO_ADDRESS_P)
#define INTEGER_TO_ADDRESS_P() (gdbarch_integer_to_address_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_integer_to_address_ftype) (struct type *type, void *buf);
extern CORE_ADDR gdbarch_integer_to_address (struct gdbarch *gdbarch, struct type *type, void *buf);
extern void set_gdbarch_integer_to_address (struct gdbarch *gdbarch, gdbarch_integer_to_address_ftype *integer_to_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (INTEGER_TO_ADDRESS)
#error "Non multi-arch definition of INTEGER_TO_ADDRESS"
#endif
#if !defined (INTEGER_TO_ADDRESS)
#define INTEGER_TO_ADDRESS(type, buf) (gdbarch_integer_to_address (current_gdbarch, type, buf))
#endif

#if defined (DEPRECATED_POP_FRAME)
/* Legacy for systems yet to multi-arch DEPRECATED_POP_FRAME */
#if !defined (DEPRECATED_POP_FRAME_P)
#define DEPRECATED_POP_FRAME_P() (1)
#endif
#endif

extern int gdbarch_deprecated_pop_frame_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_POP_FRAME_P)
#error "Non multi-arch definition of DEPRECATED_POP_FRAME"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_POP_FRAME_P)
#define DEPRECATED_POP_FRAME_P() (gdbarch_deprecated_pop_frame_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_pop_frame_ftype) (void);
extern void gdbarch_deprecated_pop_frame (struct gdbarch *gdbarch);
extern void set_gdbarch_deprecated_pop_frame (struct gdbarch *gdbarch, gdbarch_deprecated_pop_frame_ftype *deprecated_pop_frame);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_POP_FRAME)
#error "Non multi-arch definition of DEPRECATED_POP_FRAME"
#endif
#if !defined (DEPRECATED_POP_FRAME)
#define DEPRECATED_POP_FRAME (gdbarch_deprecated_pop_frame (current_gdbarch))
#endif

/* NOTE: cagney/2003-03-24: Replaced by PUSH_ARGUMENTS. */

#if defined (DEPRECATED_STORE_STRUCT_RETURN)
/* Legacy for systems yet to multi-arch DEPRECATED_STORE_STRUCT_RETURN */
#if !defined (DEPRECATED_STORE_STRUCT_RETURN_P)
#define DEPRECATED_STORE_STRUCT_RETURN_P() (1)
#endif
#endif

extern int gdbarch_deprecated_store_struct_return_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_STORE_STRUCT_RETURN_P)
#error "Non multi-arch definition of DEPRECATED_STORE_STRUCT_RETURN"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_STORE_STRUCT_RETURN_P)
#define DEPRECATED_STORE_STRUCT_RETURN_P() (gdbarch_deprecated_store_struct_return_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_store_struct_return_ftype) (CORE_ADDR addr, CORE_ADDR sp);
extern void gdbarch_deprecated_store_struct_return (struct gdbarch *gdbarch, CORE_ADDR addr, CORE_ADDR sp);
extern void set_gdbarch_deprecated_store_struct_return (struct gdbarch *gdbarch, gdbarch_deprecated_store_struct_return_ftype *deprecated_store_struct_return);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_STORE_STRUCT_RETURN)
#error "Non multi-arch definition of DEPRECATED_STORE_STRUCT_RETURN"
#endif
#if !defined (DEPRECATED_STORE_STRUCT_RETURN)
#define DEPRECATED_STORE_STRUCT_RETURN(addr, sp) (gdbarch_deprecated_store_struct_return (current_gdbarch, addr, sp))
#endif

/* It has been suggested that this, well actually its predecessor,
   should take the type/value of the function to be called and not the
   return type.  This is left as an exercise for the reader. */

extern int gdbarch_return_value_p (struct gdbarch *gdbarch);

typedef enum return_value_convention (gdbarch_return_value_ftype) (struct gdbarch *gdbarch, struct type *valtype, struct regcache *regcache, void *readbuf, const void *writebuf);
extern enum return_value_convention gdbarch_return_value (struct gdbarch *gdbarch, struct type *valtype, struct regcache *regcache, void *readbuf, const void *writebuf);
extern void set_gdbarch_return_value (struct gdbarch *gdbarch, gdbarch_return_value_ftype *return_value);

/* The deprecated methods RETURN_VALUE_ON_STACK, EXTRACT_RETURN_VALUE,
   STORE_RETURN_VALUE and USE_STRUCT_CONVENTION have all been folded
   into RETURN_VALUE. */

typedef int (gdbarch_return_value_on_stack_ftype) (struct type *type);
extern int gdbarch_return_value_on_stack (struct gdbarch *gdbarch, struct type *type);
extern void set_gdbarch_return_value_on_stack (struct gdbarch *gdbarch, gdbarch_return_value_on_stack_ftype *return_value_on_stack);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (RETURN_VALUE_ON_STACK)
#error "Non multi-arch definition of RETURN_VALUE_ON_STACK"
#endif
#if !defined (RETURN_VALUE_ON_STACK)
#define RETURN_VALUE_ON_STACK(type) (gdbarch_return_value_on_stack (current_gdbarch, type))
#endif

typedef void (gdbarch_extract_return_value_ftype) (struct type *type, struct regcache *regcache, void *valbuf);
extern void gdbarch_extract_return_value (struct gdbarch *gdbarch, struct type *type, struct regcache *regcache, void *valbuf);
extern void set_gdbarch_extract_return_value (struct gdbarch *gdbarch, gdbarch_extract_return_value_ftype *extract_return_value);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (EXTRACT_RETURN_VALUE)
#error "Non multi-arch definition of EXTRACT_RETURN_VALUE"
#endif
#if !defined (EXTRACT_RETURN_VALUE)
#define EXTRACT_RETURN_VALUE(type, regcache, valbuf) (gdbarch_extract_return_value (current_gdbarch, type, regcache, valbuf))
#endif

typedef void (gdbarch_store_return_value_ftype) (struct type *type, struct regcache *regcache, const void *valbuf);
extern void gdbarch_store_return_value (struct gdbarch *gdbarch, struct type *type, struct regcache *regcache, const void *valbuf);
extern void set_gdbarch_store_return_value (struct gdbarch *gdbarch, gdbarch_store_return_value_ftype *store_return_value);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (STORE_RETURN_VALUE)
#error "Non multi-arch definition of STORE_RETURN_VALUE"
#endif
#if !defined (STORE_RETURN_VALUE)
#define STORE_RETURN_VALUE(type, regcache, valbuf) (gdbarch_store_return_value (current_gdbarch, type, regcache, valbuf))
#endif

typedef void (gdbarch_deprecated_extract_return_value_ftype) (struct type *type, char *regbuf, char *valbuf);
extern void gdbarch_deprecated_extract_return_value (struct gdbarch *gdbarch, struct type *type, char *regbuf, char *valbuf);
extern void set_gdbarch_deprecated_extract_return_value (struct gdbarch *gdbarch, gdbarch_deprecated_extract_return_value_ftype *deprecated_extract_return_value);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_EXTRACT_RETURN_VALUE)
#error "Non multi-arch definition of DEPRECATED_EXTRACT_RETURN_VALUE"
#endif
#if !defined (DEPRECATED_EXTRACT_RETURN_VALUE)
#define DEPRECATED_EXTRACT_RETURN_VALUE(type, regbuf, valbuf) (gdbarch_deprecated_extract_return_value (current_gdbarch, type, regbuf, valbuf))
#endif

typedef void (gdbarch_deprecated_store_return_value_ftype) (struct type *type, char *valbuf);
extern void gdbarch_deprecated_store_return_value (struct gdbarch *gdbarch, struct type *type, char *valbuf);
extern void set_gdbarch_deprecated_store_return_value (struct gdbarch *gdbarch, gdbarch_deprecated_store_return_value_ftype *deprecated_store_return_value);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_STORE_RETURN_VALUE)
#error "Non multi-arch definition of DEPRECATED_STORE_RETURN_VALUE"
#endif
#if !defined (DEPRECATED_STORE_RETURN_VALUE)
#define DEPRECATED_STORE_RETURN_VALUE(type, valbuf) (gdbarch_deprecated_store_return_value (current_gdbarch, type, valbuf))
#endif

typedef int (gdbarch_use_struct_convention_ftype) (int gcc_p, struct type *value_type);
extern int gdbarch_use_struct_convention (struct gdbarch *gdbarch, int gcc_p, struct type *value_type);
extern void set_gdbarch_use_struct_convention (struct gdbarch *gdbarch, gdbarch_use_struct_convention_ftype *use_struct_convention);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (USE_STRUCT_CONVENTION)
#error "Non multi-arch definition of USE_STRUCT_CONVENTION"
#endif
#if !defined (USE_STRUCT_CONVENTION)
#define USE_STRUCT_CONVENTION(gcc_p, value_type) (gdbarch_use_struct_convention (current_gdbarch, gcc_p, value_type))
#endif

/* As of 2004-01-17 only the 32-bit SPARC ABI has been identified as an
   ABI suitable for the implementation of a robust extract
   struct-convention return-value address method (the sparc saves the
   address in the callers frame).  All the other cases so far examined,
   the DEPRECATED_EXTRACT_STRUCT_VALUE implementation has been
   erreneous - the code was incorrectly assuming that the return-value
   address, stored in a register, was preserved across the entire
   function call.
   For the moment retain DEPRECATED_EXTRACT_STRUCT_VALUE as a marker of
   the ABIs that are still to be analyzed - perhaps this should simply
   be deleted.  The commented out extract_returned_value_address method
   is provided as a starting point for the 32-bit SPARC.  It, or
   something like it, along with changes to both infcmd.c and stack.c
   will be needed for that case to work.  NB: It is passed the callers
   frame since it is only after the callee has returned that this
   function is used.
  M:::CORE_ADDR:extract_returned_value_address:struct frame_info *caller_frame:caller_frame */

#if defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS)
/* Legacy for systems yet to multi-arch DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS */
#if !defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P)
#define DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_extract_struct_value_address_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P)
#error "Non multi-arch definition of DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P)
#define DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P() (gdbarch_deprecated_extract_struct_value_address_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_extract_struct_value_address_ftype) (struct regcache *regcache);
extern CORE_ADDR gdbarch_deprecated_extract_struct_value_address (struct gdbarch *gdbarch, struct regcache *regcache);
extern void set_gdbarch_deprecated_extract_struct_value_address (struct gdbarch *gdbarch, gdbarch_deprecated_extract_struct_value_address_ftype *deprecated_extract_struct_value_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS)
#error "Non multi-arch definition of DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS"
#endif
#if !defined (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS)
#define DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS(regcache) (gdbarch_deprecated_extract_struct_value_address (current_gdbarch, regcache))
#endif

#if defined (DEPRECATED_FRAME_INIT_SAVED_REGS)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_INIT_SAVED_REGS */
#if !defined (DEPRECATED_FRAME_INIT_SAVED_REGS_P)
#define DEPRECATED_FRAME_INIT_SAVED_REGS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_init_saved_regs_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_INIT_SAVED_REGS_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_INIT_SAVED_REGS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_INIT_SAVED_REGS_P)
#define DEPRECATED_FRAME_INIT_SAVED_REGS_P() (gdbarch_deprecated_frame_init_saved_regs_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_frame_init_saved_regs_ftype) (struct frame_info *frame);
extern void gdbarch_deprecated_frame_init_saved_regs (struct gdbarch *gdbarch, struct frame_info *frame);
extern void set_gdbarch_deprecated_frame_init_saved_regs (struct gdbarch *gdbarch, gdbarch_deprecated_frame_init_saved_regs_ftype *deprecated_frame_init_saved_regs);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_INIT_SAVED_REGS)
#error "Non multi-arch definition of DEPRECATED_FRAME_INIT_SAVED_REGS"
#endif
#if !defined (DEPRECATED_FRAME_INIT_SAVED_REGS)
#define DEPRECATED_FRAME_INIT_SAVED_REGS(frame) (gdbarch_deprecated_frame_init_saved_regs (current_gdbarch, frame))
#endif

#if defined (DEPRECATED_INIT_EXTRA_FRAME_INFO)
/* Legacy for systems yet to multi-arch DEPRECATED_INIT_EXTRA_FRAME_INFO */
#if !defined (DEPRECATED_INIT_EXTRA_FRAME_INFO_P)
#define DEPRECATED_INIT_EXTRA_FRAME_INFO_P() (1)
#endif
#endif

extern int gdbarch_deprecated_init_extra_frame_info_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_EXTRA_FRAME_INFO_P)
#error "Non multi-arch definition of DEPRECATED_INIT_EXTRA_FRAME_INFO"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_INIT_EXTRA_FRAME_INFO_P)
#define DEPRECATED_INIT_EXTRA_FRAME_INFO_P() (gdbarch_deprecated_init_extra_frame_info_p (current_gdbarch))
#endif

typedef void (gdbarch_deprecated_init_extra_frame_info_ftype) (int fromleaf, struct frame_info *frame);
extern void gdbarch_deprecated_init_extra_frame_info (struct gdbarch *gdbarch, int fromleaf, struct frame_info *frame);
extern void set_gdbarch_deprecated_init_extra_frame_info (struct gdbarch *gdbarch, gdbarch_deprecated_init_extra_frame_info_ftype *deprecated_init_extra_frame_info);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_INIT_EXTRA_FRAME_INFO)
#error "Non multi-arch definition of DEPRECATED_INIT_EXTRA_FRAME_INFO"
#endif
#if !defined (DEPRECATED_INIT_EXTRA_FRAME_INFO)
#define DEPRECATED_INIT_EXTRA_FRAME_INFO(fromleaf, frame) (gdbarch_deprecated_init_extra_frame_info (current_gdbarch, fromleaf, frame))
#endif

typedef CORE_ADDR (gdbarch_skip_prologue_ftype) (CORE_ADDR ip);
extern CORE_ADDR gdbarch_skip_prologue (struct gdbarch *gdbarch, CORE_ADDR ip);
extern void set_gdbarch_skip_prologue (struct gdbarch *gdbarch, gdbarch_skip_prologue_ftype *skip_prologue);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SKIP_PROLOGUE)
#error "Non multi-arch definition of SKIP_PROLOGUE"
#endif
#if !defined (SKIP_PROLOGUE)
#define SKIP_PROLOGUE(ip) (gdbarch_skip_prologue (current_gdbarch, ip))
#endif

typedef int (gdbarch_inner_than_ftype) (CORE_ADDR lhs, CORE_ADDR rhs);
extern int gdbarch_inner_than (struct gdbarch *gdbarch, CORE_ADDR lhs, CORE_ADDR rhs);
extern void set_gdbarch_inner_than (struct gdbarch *gdbarch, gdbarch_inner_than_ftype *inner_than);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (INNER_THAN)
#error "Non multi-arch definition of INNER_THAN"
#endif
#if !defined (INNER_THAN)
#define INNER_THAN(lhs, rhs) (gdbarch_inner_than (current_gdbarch, lhs, rhs))
#endif

typedef const unsigned char * (gdbarch_breakpoint_from_pc_ftype) (CORE_ADDR *pcptr, int *lenptr);
extern const unsigned char * gdbarch_breakpoint_from_pc (struct gdbarch *gdbarch, CORE_ADDR *pcptr, int *lenptr);
extern void set_gdbarch_breakpoint_from_pc (struct gdbarch *gdbarch, gdbarch_breakpoint_from_pc_ftype *breakpoint_from_pc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (BREAKPOINT_FROM_PC)
#error "Non multi-arch definition of BREAKPOINT_FROM_PC"
#endif
#if !defined (BREAKPOINT_FROM_PC)
#define BREAKPOINT_FROM_PC(pcptr, lenptr) (gdbarch_breakpoint_from_pc (current_gdbarch, pcptr, lenptr))
#endif

extern int gdbarch_adjust_breakpoint_address_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_adjust_breakpoint_address_ftype) (struct gdbarch *gdbarch, CORE_ADDR bpaddr);
extern CORE_ADDR gdbarch_adjust_breakpoint_address (struct gdbarch *gdbarch, CORE_ADDR bpaddr);
extern void set_gdbarch_adjust_breakpoint_address (struct gdbarch *gdbarch, gdbarch_adjust_breakpoint_address_ftype *adjust_breakpoint_address);

typedef int (gdbarch_memory_insert_breakpoint_ftype) (CORE_ADDR addr, char *contents_cache);
extern int gdbarch_memory_insert_breakpoint (struct gdbarch *gdbarch, CORE_ADDR addr, char *contents_cache);
extern void set_gdbarch_memory_insert_breakpoint (struct gdbarch *gdbarch, gdbarch_memory_insert_breakpoint_ftype *memory_insert_breakpoint);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (MEMORY_INSERT_BREAKPOINT)
#error "Non multi-arch definition of MEMORY_INSERT_BREAKPOINT"
#endif
#if !defined (MEMORY_INSERT_BREAKPOINT)
#define MEMORY_INSERT_BREAKPOINT(addr, contents_cache) (gdbarch_memory_insert_breakpoint (current_gdbarch, addr, contents_cache))
#endif

typedef int (gdbarch_memory_remove_breakpoint_ftype) (CORE_ADDR addr, char *contents_cache);
extern int gdbarch_memory_remove_breakpoint (struct gdbarch *gdbarch, CORE_ADDR addr, char *contents_cache);
extern void set_gdbarch_memory_remove_breakpoint (struct gdbarch *gdbarch, gdbarch_memory_remove_breakpoint_ftype *memory_remove_breakpoint);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (MEMORY_REMOVE_BREAKPOINT)
#error "Non multi-arch definition of MEMORY_REMOVE_BREAKPOINT"
#endif
#if !defined (MEMORY_REMOVE_BREAKPOINT)
#define MEMORY_REMOVE_BREAKPOINT(addr, contents_cache) (gdbarch_memory_remove_breakpoint (current_gdbarch, addr, contents_cache))
#endif

extern CORE_ADDR gdbarch_decr_pc_after_break (struct gdbarch *gdbarch);
extern void set_gdbarch_decr_pc_after_break (struct gdbarch *gdbarch, CORE_ADDR decr_pc_after_break);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DECR_PC_AFTER_BREAK)
#error "Non multi-arch definition of DECR_PC_AFTER_BREAK"
#endif
#if !defined (DECR_PC_AFTER_BREAK)
#define DECR_PC_AFTER_BREAK (gdbarch_decr_pc_after_break (current_gdbarch))
#endif

extern CORE_ADDR gdbarch_function_start_offset (struct gdbarch *gdbarch);
extern void set_gdbarch_function_start_offset (struct gdbarch *gdbarch, CORE_ADDR function_start_offset);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FUNCTION_START_OFFSET)
#error "Non multi-arch definition of FUNCTION_START_OFFSET"
#endif
#if !defined (FUNCTION_START_OFFSET)
#define FUNCTION_START_OFFSET (gdbarch_function_start_offset (current_gdbarch))
#endif

typedef void (gdbarch_remote_translate_xfer_address_ftype) (struct gdbarch *gdbarch, struct regcache *regcache, CORE_ADDR gdb_addr, int gdb_len, CORE_ADDR *rem_addr, int *rem_len);
extern void gdbarch_remote_translate_xfer_address (struct gdbarch *gdbarch, struct regcache *regcache, CORE_ADDR gdb_addr, int gdb_len, CORE_ADDR *rem_addr, int *rem_len);
extern void set_gdbarch_remote_translate_xfer_address (struct gdbarch *gdbarch, gdbarch_remote_translate_xfer_address_ftype *remote_translate_xfer_address);

extern CORE_ADDR gdbarch_frame_args_skip (struct gdbarch *gdbarch);
extern void set_gdbarch_frame_args_skip (struct gdbarch *gdbarch, CORE_ADDR frame_args_skip);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FRAME_ARGS_SKIP)
#error "Non multi-arch definition of FRAME_ARGS_SKIP"
#endif
#if !defined (FRAME_ARGS_SKIP)
#define FRAME_ARGS_SKIP (gdbarch_frame_args_skip (current_gdbarch))
#endif

/* DEPRECATED_FRAMELESS_FUNCTION_INVOCATION is not needed.  The new
   frame code works regardless of the type of frame - frameless,
   stackless, or normal. */

#if defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAMELESS_FUNCTION_INVOCATION */
#if !defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P)
#define DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frameless_function_invocation_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P)
#error "Non multi-arch definition of DEPRECATED_FRAMELESS_FUNCTION_INVOCATION"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P)
#define DEPRECATED_FRAMELESS_FUNCTION_INVOCATION_P() (gdbarch_deprecated_frameless_function_invocation_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_frameless_function_invocation_ftype) (struct frame_info *fi);
extern int gdbarch_deprecated_frameless_function_invocation (struct gdbarch *gdbarch, struct frame_info *fi);
extern void set_gdbarch_deprecated_frameless_function_invocation (struct gdbarch *gdbarch, gdbarch_deprecated_frameless_function_invocation_ftype *deprecated_frameless_function_invocation);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION)
#error "Non multi-arch definition of DEPRECATED_FRAMELESS_FUNCTION_INVOCATION"
#endif
#if !defined (DEPRECATED_FRAMELESS_FUNCTION_INVOCATION)
#define DEPRECATED_FRAMELESS_FUNCTION_INVOCATION(fi) (gdbarch_deprecated_frameless_function_invocation (current_gdbarch, fi))
#endif

#if defined (DEPRECATED_FRAME_CHAIN)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_CHAIN */
#if !defined (DEPRECATED_FRAME_CHAIN_P)
#define DEPRECATED_FRAME_CHAIN_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_chain_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_CHAIN_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_CHAIN"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_CHAIN_P)
#define DEPRECATED_FRAME_CHAIN_P() (gdbarch_deprecated_frame_chain_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_frame_chain_ftype) (struct frame_info *frame);
extern CORE_ADDR gdbarch_deprecated_frame_chain (struct gdbarch *gdbarch, struct frame_info *frame);
extern void set_gdbarch_deprecated_frame_chain (struct gdbarch *gdbarch, gdbarch_deprecated_frame_chain_ftype *deprecated_frame_chain);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_CHAIN)
#error "Non multi-arch definition of DEPRECATED_FRAME_CHAIN"
#endif
#if !defined (DEPRECATED_FRAME_CHAIN)
#define DEPRECATED_FRAME_CHAIN(frame) (gdbarch_deprecated_frame_chain (current_gdbarch, frame))
#endif

#if defined (DEPRECATED_FRAME_CHAIN_VALID)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_CHAIN_VALID */
#if !defined (DEPRECATED_FRAME_CHAIN_VALID_P)
#define DEPRECATED_FRAME_CHAIN_VALID_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_chain_valid_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_CHAIN_VALID_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_CHAIN_VALID"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_CHAIN_VALID_P)
#define DEPRECATED_FRAME_CHAIN_VALID_P() (gdbarch_deprecated_frame_chain_valid_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_frame_chain_valid_ftype) (CORE_ADDR chain, struct frame_info *thisframe);
extern int gdbarch_deprecated_frame_chain_valid (struct gdbarch *gdbarch, CORE_ADDR chain, struct frame_info *thisframe);
extern void set_gdbarch_deprecated_frame_chain_valid (struct gdbarch *gdbarch, gdbarch_deprecated_frame_chain_valid_ftype *deprecated_frame_chain_valid);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_CHAIN_VALID)
#error "Non multi-arch definition of DEPRECATED_FRAME_CHAIN_VALID"
#endif
#if !defined (DEPRECATED_FRAME_CHAIN_VALID)
#define DEPRECATED_FRAME_CHAIN_VALID(chain, thisframe) (gdbarch_deprecated_frame_chain_valid (current_gdbarch, chain, thisframe))
#endif

/* DEPRECATED_FRAME_SAVED_PC has been replaced by UNWIND_PC.  Please
   note, per UNWIND_PC's doco, that while the two have similar
   interfaces they have very different underlying implementations. */

#if defined (DEPRECATED_FRAME_SAVED_PC)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_SAVED_PC */
#if !defined (DEPRECATED_FRAME_SAVED_PC_P)
#define DEPRECATED_FRAME_SAVED_PC_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_saved_pc_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_SAVED_PC_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_SAVED_PC"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_SAVED_PC_P)
#define DEPRECATED_FRAME_SAVED_PC_P() (gdbarch_deprecated_frame_saved_pc_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_frame_saved_pc_ftype) (struct frame_info *fi);
extern CORE_ADDR gdbarch_deprecated_frame_saved_pc (struct gdbarch *gdbarch, struct frame_info *fi);
extern void set_gdbarch_deprecated_frame_saved_pc (struct gdbarch *gdbarch, gdbarch_deprecated_frame_saved_pc_ftype *deprecated_frame_saved_pc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_SAVED_PC)
#error "Non multi-arch definition of DEPRECATED_FRAME_SAVED_PC"
#endif
#if !defined (DEPRECATED_FRAME_SAVED_PC)
#define DEPRECATED_FRAME_SAVED_PC(fi) (gdbarch_deprecated_frame_saved_pc (current_gdbarch, fi))
#endif

extern int gdbarch_unwind_pc_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_unwind_pc_ftype) (struct gdbarch *gdbarch, struct frame_info *next_frame);
extern CORE_ADDR gdbarch_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame);
extern void set_gdbarch_unwind_pc (struct gdbarch *gdbarch, gdbarch_unwind_pc_ftype *unwind_pc);

extern int gdbarch_unwind_sp_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_unwind_sp_ftype) (struct gdbarch *gdbarch, struct frame_info *next_frame);
extern CORE_ADDR gdbarch_unwind_sp (struct gdbarch *gdbarch, struct frame_info *next_frame);
extern void set_gdbarch_unwind_sp (struct gdbarch *gdbarch, gdbarch_unwind_sp_ftype *unwind_sp);

/* DEPRECATED_FRAME_ARGS_ADDRESS as been replaced by the per-frame
   frame-base.  Enable frame-base before frame-unwind. */

#if defined (DEPRECATED_FRAME_ARGS_ADDRESS)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_ARGS_ADDRESS */
#if !defined (DEPRECATED_FRAME_ARGS_ADDRESS_P)
#define DEPRECATED_FRAME_ARGS_ADDRESS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_args_address_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_ARGS_ADDRESS_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_ARGS_ADDRESS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_ARGS_ADDRESS_P)
#define DEPRECATED_FRAME_ARGS_ADDRESS_P() (gdbarch_deprecated_frame_args_address_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_frame_args_address_ftype) (struct frame_info *fi);
extern CORE_ADDR gdbarch_deprecated_frame_args_address (struct gdbarch *gdbarch, struct frame_info *fi);
extern void set_gdbarch_deprecated_frame_args_address (struct gdbarch *gdbarch, gdbarch_deprecated_frame_args_address_ftype *deprecated_frame_args_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_ARGS_ADDRESS)
#error "Non multi-arch definition of DEPRECATED_FRAME_ARGS_ADDRESS"
#endif
#if !defined (DEPRECATED_FRAME_ARGS_ADDRESS)
#define DEPRECATED_FRAME_ARGS_ADDRESS(fi) (gdbarch_deprecated_frame_args_address (current_gdbarch, fi))
#endif

/* DEPRECATED_FRAME_LOCALS_ADDRESS as been replaced by the per-frame
   frame-base.  Enable frame-base before frame-unwind. */

#if defined (DEPRECATED_FRAME_LOCALS_ADDRESS)
/* Legacy for systems yet to multi-arch DEPRECATED_FRAME_LOCALS_ADDRESS */
#if !defined (DEPRECATED_FRAME_LOCALS_ADDRESS_P)
#define DEPRECATED_FRAME_LOCALS_ADDRESS_P() (1)
#endif
#endif

extern int gdbarch_deprecated_frame_locals_address_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_LOCALS_ADDRESS_P)
#error "Non multi-arch definition of DEPRECATED_FRAME_LOCALS_ADDRESS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_FRAME_LOCALS_ADDRESS_P)
#define DEPRECATED_FRAME_LOCALS_ADDRESS_P() (gdbarch_deprecated_frame_locals_address_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_frame_locals_address_ftype) (struct frame_info *fi);
extern CORE_ADDR gdbarch_deprecated_frame_locals_address (struct gdbarch *gdbarch, struct frame_info *fi);
extern void set_gdbarch_deprecated_frame_locals_address (struct gdbarch *gdbarch, gdbarch_deprecated_frame_locals_address_ftype *deprecated_frame_locals_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_FRAME_LOCALS_ADDRESS)
#error "Non multi-arch definition of DEPRECATED_FRAME_LOCALS_ADDRESS"
#endif
#if !defined (DEPRECATED_FRAME_LOCALS_ADDRESS)
#define DEPRECATED_FRAME_LOCALS_ADDRESS(fi) (gdbarch_deprecated_frame_locals_address (current_gdbarch, fi))
#endif

#if defined (DEPRECATED_SAVED_PC_AFTER_CALL)
/* Legacy for systems yet to multi-arch DEPRECATED_SAVED_PC_AFTER_CALL */
#if !defined (DEPRECATED_SAVED_PC_AFTER_CALL_P)
#define DEPRECATED_SAVED_PC_AFTER_CALL_P() (1)
#endif
#endif

extern int gdbarch_deprecated_saved_pc_after_call_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_SAVED_PC_AFTER_CALL_P)
#error "Non multi-arch definition of DEPRECATED_SAVED_PC_AFTER_CALL"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_SAVED_PC_AFTER_CALL_P)
#define DEPRECATED_SAVED_PC_AFTER_CALL_P() (gdbarch_deprecated_saved_pc_after_call_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_saved_pc_after_call_ftype) (struct frame_info *frame);
extern CORE_ADDR gdbarch_deprecated_saved_pc_after_call (struct gdbarch *gdbarch, struct frame_info *frame);
extern void set_gdbarch_deprecated_saved_pc_after_call (struct gdbarch *gdbarch, gdbarch_deprecated_saved_pc_after_call_ftype *deprecated_saved_pc_after_call);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_SAVED_PC_AFTER_CALL)
#error "Non multi-arch definition of DEPRECATED_SAVED_PC_AFTER_CALL"
#endif
#if !defined (DEPRECATED_SAVED_PC_AFTER_CALL)
#define DEPRECATED_SAVED_PC_AFTER_CALL(frame) (gdbarch_deprecated_saved_pc_after_call (current_gdbarch, frame))
#endif

#if defined (FRAME_NUM_ARGS)
/* Legacy for systems yet to multi-arch FRAME_NUM_ARGS */
#if !defined (FRAME_NUM_ARGS_P)
#define FRAME_NUM_ARGS_P() (1)
#endif
#endif

extern int gdbarch_frame_num_args_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FRAME_NUM_ARGS_P)
#error "Non multi-arch definition of FRAME_NUM_ARGS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (FRAME_NUM_ARGS_P)
#define FRAME_NUM_ARGS_P() (gdbarch_frame_num_args_p (current_gdbarch))
#endif

typedef int (gdbarch_frame_num_args_ftype) (struct frame_info *frame);
extern int gdbarch_frame_num_args (struct gdbarch *gdbarch, struct frame_info *frame);
extern void set_gdbarch_frame_num_args (struct gdbarch *gdbarch, gdbarch_frame_num_args_ftype *frame_num_args);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FRAME_NUM_ARGS)
#error "Non multi-arch definition of FRAME_NUM_ARGS"
#endif
#if !defined (FRAME_NUM_ARGS)
#define FRAME_NUM_ARGS(frame) (gdbarch_frame_num_args (current_gdbarch, frame))
#endif

/* DEPRECATED_STACK_ALIGN has been replaced by an initial aligning call
   to frame_align and the requirement that methods such as
   push_dummy_call and frame_red_zone_size maintain correct stack/frame
   alignment. */

#if defined (DEPRECATED_STACK_ALIGN)
/* Legacy for systems yet to multi-arch DEPRECATED_STACK_ALIGN */
#if !defined (DEPRECATED_STACK_ALIGN_P)
#define DEPRECATED_STACK_ALIGN_P() (1)
#endif
#endif

extern int gdbarch_deprecated_stack_align_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_STACK_ALIGN_P)
#error "Non multi-arch definition of DEPRECATED_STACK_ALIGN"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_STACK_ALIGN_P)
#define DEPRECATED_STACK_ALIGN_P() (gdbarch_deprecated_stack_align_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_deprecated_stack_align_ftype) (CORE_ADDR sp);
extern CORE_ADDR gdbarch_deprecated_stack_align (struct gdbarch *gdbarch, CORE_ADDR sp);
extern void set_gdbarch_deprecated_stack_align (struct gdbarch *gdbarch, gdbarch_deprecated_stack_align_ftype *deprecated_stack_align);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_STACK_ALIGN)
#error "Non multi-arch definition of DEPRECATED_STACK_ALIGN"
#endif
#if !defined (DEPRECATED_STACK_ALIGN)
#define DEPRECATED_STACK_ALIGN(sp) (gdbarch_deprecated_stack_align (current_gdbarch, sp))
#endif

extern int gdbarch_frame_align_p (struct gdbarch *gdbarch);

typedef CORE_ADDR (gdbarch_frame_align_ftype) (struct gdbarch *gdbarch, CORE_ADDR address);
extern CORE_ADDR gdbarch_frame_align (struct gdbarch *gdbarch, CORE_ADDR address);
extern void set_gdbarch_frame_align (struct gdbarch *gdbarch, gdbarch_frame_align_ftype *frame_align);

/* DEPRECATED_REG_STRUCT_HAS_ADDR has been replaced by
   stabs_argument_has_addr. */

#if defined (DEPRECATED_REG_STRUCT_HAS_ADDR)
/* Legacy for systems yet to multi-arch DEPRECATED_REG_STRUCT_HAS_ADDR */
#if !defined (DEPRECATED_REG_STRUCT_HAS_ADDR_P)
#define DEPRECATED_REG_STRUCT_HAS_ADDR_P() (1)
#endif
#endif

extern int gdbarch_deprecated_reg_struct_has_addr_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REG_STRUCT_HAS_ADDR_P)
#error "Non multi-arch definition of DEPRECATED_REG_STRUCT_HAS_ADDR"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (DEPRECATED_REG_STRUCT_HAS_ADDR_P)
#define DEPRECATED_REG_STRUCT_HAS_ADDR_P() (gdbarch_deprecated_reg_struct_has_addr_p (current_gdbarch))
#endif

typedef int (gdbarch_deprecated_reg_struct_has_addr_ftype) (int gcc_p, struct type *type);
extern int gdbarch_deprecated_reg_struct_has_addr (struct gdbarch *gdbarch, int gcc_p, struct type *type);
extern void set_gdbarch_deprecated_reg_struct_has_addr (struct gdbarch *gdbarch, gdbarch_deprecated_reg_struct_has_addr_ftype *deprecated_reg_struct_has_addr);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (DEPRECATED_REG_STRUCT_HAS_ADDR)
#error "Non multi-arch definition of DEPRECATED_REG_STRUCT_HAS_ADDR"
#endif
#if !defined (DEPRECATED_REG_STRUCT_HAS_ADDR)
#define DEPRECATED_REG_STRUCT_HAS_ADDR(gcc_p, type) (gdbarch_deprecated_reg_struct_has_addr (current_gdbarch, gcc_p, type))
#endif

typedef int (gdbarch_stabs_argument_has_addr_ftype) (struct gdbarch *gdbarch, struct type *type);
extern int gdbarch_stabs_argument_has_addr (struct gdbarch *gdbarch, struct type *type);
extern void set_gdbarch_stabs_argument_has_addr (struct gdbarch *gdbarch, gdbarch_stabs_argument_has_addr_ftype *stabs_argument_has_addr);

extern int gdbarch_frame_red_zone_size (struct gdbarch *gdbarch);
extern void set_gdbarch_frame_red_zone_size (struct gdbarch *gdbarch, int frame_red_zone_size);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FRAME_RED_ZONE_SIZE)
#error "Non multi-arch definition of FRAME_RED_ZONE_SIZE"
#endif
#if !defined (FRAME_RED_ZONE_SIZE)
#define FRAME_RED_ZONE_SIZE (gdbarch_frame_red_zone_size (current_gdbarch))
#endif

extern int gdbarch_parm_boundary (struct gdbarch *gdbarch);
extern void set_gdbarch_parm_boundary (struct gdbarch *gdbarch, int parm_boundary);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (PARM_BOUNDARY)
#error "Non multi-arch definition of PARM_BOUNDARY"
#endif
#if !defined (PARM_BOUNDARY)
#define PARM_BOUNDARY (gdbarch_parm_boundary (current_gdbarch))
#endif

extern const struct floatformat * gdbarch_float_format (struct gdbarch *gdbarch);
extern void set_gdbarch_float_format (struct gdbarch *gdbarch, const struct floatformat * float_format);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_FLOAT_FORMAT)
#error "Non multi-arch definition of TARGET_FLOAT_FORMAT"
#endif
#if !defined (TARGET_FLOAT_FORMAT)
#define TARGET_FLOAT_FORMAT (gdbarch_float_format (current_gdbarch))
#endif

extern const struct floatformat * gdbarch_double_format (struct gdbarch *gdbarch);
extern void set_gdbarch_double_format (struct gdbarch *gdbarch, const struct floatformat * double_format);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_DOUBLE_FORMAT)
#error "Non multi-arch definition of TARGET_DOUBLE_FORMAT"
#endif
#if !defined (TARGET_DOUBLE_FORMAT)
#define TARGET_DOUBLE_FORMAT (gdbarch_double_format (current_gdbarch))
#endif

extern const struct floatformat * gdbarch_long_double_format (struct gdbarch *gdbarch);
extern void set_gdbarch_long_double_format (struct gdbarch *gdbarch, const struct floatformat * long_double_format);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_LONG_DOUBLE_FORMAT)
#error "Non multi-arch definition of TARGET_LONG_DOUBLE_FORMAT"
#endif
#if !defined (TARGET_LONG_DOUBLE_FORMAT)
#define TARGET_LONG_DOUBLE_FORMAT (gdbarch_long_double_format (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_convert_from_func_ptr_addr_ftype) (struct gdbarch *gdbarch, CORE_ADDR addr, struct target_ops *targ);
extern CORE_ADDR gdbarch_convert_from_func_ptr_addr (struct gdbarch *gdbarch, CORE_ADDR addr, struct target_ops *targ);
extern void set_gdbarch_convert_from_func_ptr_addr (struct gdbarch *gdbarch, gdbarch_convert_from_func_ptr_addr_ftype *convert_from_func_ptr_addr);

/* On some machines there are bits in addresses which are not really
   part of the address, but are used by the kernel, the hardware, etc.
   for special purposes.  ADDR_BITS_REMOVE takes out any such bits so
   we get a "real" address such as one would find in a symbol table.
   This is used only for addresses of instructions, and even then I'm
   not sure it's used in all contexts.  It exists to deal with there
   being a few stray bits in the PC which would mislead us, not as some
   sort of generic thing to handle alignment or segmentation (it's
   possible it should be in TARGET_READ_PC instead). */

typedef CORE_ADDR (gdbarch_addr_bits_remove_ftype) (CORE_ADDR addr);
extern CORE_ADDR gdbarch_addr_bits_remove (struct gdbarch *gdbarch, CORE_ADDR addr);
extern void set_gdbarch_addr_bits_remove (struct gdbarch *gdbarch, gdbarch_addr_bits_remove_ftype *addr_bits_remove);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ADDR_BITS_REMOVE)
#error "Non multi-arch definition of ADDR_BITS_REMOVE"
#endif
#if !defined (ADDR_BITS_REMOVE)
#define ADDR_BITS_REMOVE(addr) (gdbarch_addr_bits_remove (current_gdbarch, addr))
#endif

/* It is not at all clear why SMASH_TEXT_ADDRESS is not folded into
   ADDR_BITS_REMOVE. */

typedef CORE_ADDR (gdbarch_smash_text_address_ftype) (CORE_ADDR addr);
extern CORE_ADDR gdbarch_smash_text_address (struct gdbarch *gdbarch, CORE_ADDR addr);
extern void set_gdbarch_smash_text_address (struct gdbarch *gdbarch, gdbarch_smash_text_address_ftype *smash_text_address);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SMASH_TEXT_ADDRESS)
#error "Non multi-arch definition of SMASH_TEXT_ADDRESS"
#endif
#if !defined (SMASH_TEXT_ADDRESS)
#define SMASH_TEXT_ADDRESS(addr) (gdbarch_smash_text_address (current_gdbarch, addr))
#endif

/* FIXME/cagney/2001-01-18: This should be split in two.  A target method that indicates if
   the target needs software single step.  An ISA method to implement it.
  
   FIXME/cagney/2001-01-18: This should be replaced with something that inserts breakpoints
   using the breakpoint system instead of blatting memory directly (as with rs6000).
  
   FIXME/cagney/2001-01-18: The logic is backwards.  It should be asking if the target can
   single step.  If not, then implement single step using breakpoints. */

#if defined (SOFTWARE_SINGLE_STEP)
/* Legacy for systems yet to multi-arch SOFTWARE_SINGLE_STEP */
#if !defined (SOFTWARE_SINGLE_STEP_P)
#define SOFTWARE_SINGLE_STEP_P() (1)
#endif
#endif

extern int gdbarch_software_single_step_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SOFTWARE_SINGLE_STEP_P)
#error "Non multi-arch definition of SOFTWARE_SINGLE_STEP"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (SOFTWARE_SINGLE_STEP_P)
#define SOFTWARE_SINGLE_STEP_P() (gdbarch_software_single_step_p (current_gdbarch))
#endif

typedef void (gdbarch_software_single_step_ftype) (enum target_signal sig, int insert_breakpoints_p);
extern void gdbarch_software_single_step (struct gdbarch *gdbarch, enum target_signal sig, int insert_breakpoints_p);
extern void set_gdbarch_software_single_step (struct gdbarch *gdbarch, gdbarch_software_single_step_ftype *software_single_step);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SOFTWARE_SINGLE_STEP)
#error "Non multi-arch definition of SOFTWARE_SINGLE_STEP"
#endif
#if !defined (SOFTWARE_SINGLE_STEP)
#define SOFTWARE_SINGLE_STEP(sig, insert_breakpoints_p) (gdbarch_software_single_step (current_gdbarch, sig, insert_breakpoints_p))
#endif

/* FIXME: cagney/2003-08-28: Need to find a better way of selecting the
   disassembler.  Perhaphs objdump can handle it? */

typedef int (gdbarch_print_insn_ftype) (bfd_vma vma, struct disassemble_info *info);
extern int gdbarch_print_insn (struct gdbarch *gdbarch, bfd_vma vma, struct disassemble_info *info);
extern void set_gdbarch_print_insn (struct gdbarch *gdbarch, gdbarch_print_insn_ftype *print_insn);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (TARGET_PRINT_INSN)
#error "Non multi-arch definition of TARGET_PRINT_INSN"
#endif
#if !defined (TARGET_PRINT_INSN)
#define TARGET_PRINT_INSN(vma, info) (gdbarch_print_insn (current_gdbarch, vma, info))
#endif

typedef CORE_ADDR (gdbarch_skip_trampoline_code_ftype) (CORE_ADDR pc);
extern CORE_ADDR gdbarch_skip_trampoline_code (struct gdbarch *gdbarch, CORE_ADDR pc);
extern void set_gdbarch_skip_trampoline_code (struct gdbarch *gdbarch, gdbarch_skip_trampoline_code_ftype *skip_trampoline_code);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SKIP_TRAMPOLINE_CODE)
#error "Non multi-arch definition of SKIP_TRAMPOLINE_CODE"
#endif
#if !defined (SKIP_TRAMPOLINE_CODE)
#define SKIP_TRAMPOLINE_CODE(pc) (gdbarch_skip_trampoline_code (current_gdbarch, pc))
#endif

/* If IN_SOLIB_DYNSYM_RESOLVE_CODE returns true, and SKIP_SOLIB_RESOLVER
   evaluates non-zero, this is the address where the debugger will place
   a step-resume breakpoint to get us past the dynamic linker. */

typedef CORE_ADDR (gdbarch_skip_solib_resolver_ftype) (struct gdbarch *gdbarch, CORE_ADDR pc);
extern CORE_ADDR gdbarch_skip_solib_resolver (struct gdbarch *gdbarch, CORE_ADDR pc);
extern void set_gdbarch_skip_solib_resolver (struct gdbarch *gdbarch, gdbarch_skip_solib_resolver_ftype *skip_solib_resolver);

/* For SVR4 shared libraries, each call goes through a small piece of
   trampoline code in the ".plt" section.  IN_SOLIB_CALL_TRAMPOLINE evaluates
   to nonzero if we are currently stopped in one of these. */

typedef int (gdbarch_in_solib_call_trampoline_ftype) (CORE_ADDR pc, char *name);
extern int gdbarch_in_solib_call_trampoline (struct gdbarch *gdbarch, CORE_ADDR pc, char *name);
extern void set_gdbarch_in_solib_call_trampoline (struct gdbarch *gdbarch, gdbarch_in_solib_call_trampoline_ftype *in_solib_call_trampoline);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (IN_SOLIB_CALL_TRAMPOLINE)
#error "Non multi-arch definition of IN_SOLIB_CALL_TRAMPOLINE"
#endif
#if !defined (IN_SOLIB_CALL_TRAMPOLINE)
#define IN_SOLIB_CALL_TRAMPOLINE(pc, name) (gdbarch_in_solib_call_trampoline (current_gdbarch, pc, name))
#endif

/* Some systems also have trampoline code for returning from shared libs. */

typedef int (gdbarch_in_solib_return_trampoline_ftype) (CORE_ADDR pc, char *name);
extern int gdbarch_in_solib_return_trampoline (struct gdbarch *gdbarch, CORE_ADDR pc, char *name);
extern void set_gdbarch_in_solib_return_trampoline (struct gdbarch *gdbarch, gdbarch_in_solib_return_trampoline_ftype *in_solib_return_trampoline);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (IN_SOLIB_RETURN_TRAMPOLINE)
#error "Non multi-arch definition of IN_SOLIB_RETURN_TRAMPOLINE"
#endif
#if !defined (IN_SOLIB_RETURN_TRAMPOLINE)
#define IN_SOLIB_RETURN_TRAMPOLINE(pc, name) (gdbarch_in_solib_return_trampoline (current_gdbarch, pc, name))
#endif

/* Sigtramp is a routine that the kernel calls (which then calls the
   signal handler).  On most machines it is a library routine that is
   linked into the executable.
  
   This macro, given a program counter value and the name of the
   function in which that PC resides (which can be null if the name is
   not known), returns nonzero if the PC and name show that we are in
   sigtramp.
  
   On most machines just see if the name is sigtramp (and if we have
   no name, assume we are not in sigtramp).
  
   FIXME: cagney/2002-04-21: The function find_pc_partial_function
   calls find_pc_sect_partial_function() which calls PC_IN_SIGTRAMP.
   This means PC_IN_SIGTRAMP function can't be implemented by doing its
   own local NAME lookup.
  
   FIXME: cagney/2002-04-21: PC_IN_SIGTRAMP is something of a mess.
   Some code also depends on SIGTRAMP_START and SIGTRAMP_END but other
   does not. */

typedef int (gdbarch_pc_in_sigtramp_ftype) (CORE_ADDR pc, char *name);
extern int gdbarch_pc_in_sigtramp (struct gdbarch *gdbarch, CORE_ADDR pc, char *name);
extern void set_gdbarch_pc_in_sigtramp (struct gdbarch *gdbarch, gdbarch_pc_in_sigtramp_ftype *pc_in_sigtramp);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (PC_IN_SIGTRAMP)
#error "Non multi-arch definition of PC_IN_SIGTRAMP"
#endif
#if !defined (PC_IN_SIGTRAMP)
#define PC_IN_SIGTRAMP(pc, name) (gdbarch_pc_in_sigtramp (current_gdbarch, pc, name))
#endif

#if defined (SIGTRAMP_START)
/* Legacy for systems yet to multi-arch SIGTRAMP_START */
#if !defined (SIGTRAMP_START_P)
#define SIGTRAMP_START_P() (1)
#endif
#endif

extern int gdbarch_sigtramp_start_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SIGTRAMP_START_P)
#error "Non multi-arch definition of SIGTRAMP_START"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (SIGTRAMP_START_P)
#define SIGTRAMP_START_P() (gdbarch_sigtramp_start_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_sigtramp_start_ftype) (CORE_ADDR pc);
extern CORE_ADDR gdbarch_sigtramp_start (struct gdbarch *gdbarch, CORE_ADDR pc);
extern void set_gdbarch_sigtramp_start (struct gdbarch *gdbarch, gdbarch_sigtramp_start_ftype *sigtramp_start);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SIGTRAMP_START)
#error "Non multi-arch definition of SIGTRAMP_START"
#endif
#if !defined (SIGTRAMP_START)
#define SIGTRAMP_START(pc) (gdbarch_sigtramp_start (current_gdbarch, pc))
#endif

#if defined (SIGTRAMP_END)
/* Legacy for systems yet to multi-arch SIGTRAMP_END */
#if !defined (SIGTRAMP_END_P)
#define SIGTRAMP_END_P() (1)
#endif
#endif

extern int gdbarch_sigtramp_end_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SIGTRAMP_END_P)
#error "Non multi-arch definition of SIGTRAMP_END"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (SIGTRAMP_END_P)
#define SIGTRAMP_END_P() (gdbarch_sigtramp_end_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_sigtramp_end_ftype) (CORE_ADDR pc);
extern CORE_ADDR gdbarch_sigtramp_end (struct gdbarch *gdbarch, CORE_ADDR pc);
extern void set_gdbarch_sigtramp_end (struct gdbarch *gdbarch, gdbarch_sigtramp_end_ftype *sigtramp_end);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (SIGTRAMP_END)
#error "Non multi-arch definition of SIGTRAMP_END"
#endif
#if !defined (SIGTRAMP_END)
#define SIGTRAMP_END(pc) (gdbarch_sigtramp_end (current_gdbarch, pc))
#endif

/* A target might have problems with watchpoints as soon as the stack
   frame of the current function has been destroyed.  This mostly happens
   as the first action in a funtion's epilogue.  in_function_epilogue_p()
   is defined to return a non-zero value if either the given addr is one
   instruction after the stack destroying instruction up to the trailing
   return instruction or if we can figure out that the stack frame has
   already been invalidated regardless of the value of addr.  Targets
   which don't suffer from that problem could just let this functionality
   untouched. */

typedef int (gdbarch_in_function_epilogue_p_ftype) (struct gdbarch *gdbarch, CORE_ADDR addr);
extern int gdbarch_in_function_epilogue_p (struct gdbarch *gdbarch, CORE_ADDR addr);
extern void set_gdbarch_in_function_epilogue_p (struct gdbarch *gdbarch, gdbarch_in_function_epilogue_p_ftype *in_function_epilogue_p);

/* Given a vector of command-line arguments, return a newly allocated
   string which, when passed to the create_inferior function, will be
   parsed (on Unix systems, by the shell) to yield the same vector.
   This function should call error() if the argument vector is not
   representable for this target or if this target does not support
   command-line arguments.
   ARGC is the number of elements in the vector.
   ARGV is an array of strings, one per argument. */

typedef char * (gdbarch_construct_inferior_arguments_ftype) (struct gdbarch *gdbarch, int argc, char **argv);
extern char * gdbarch_construct_inferior_arguments (struct gdbarch *gdbarch, int argc, char **argv);
extern void set_gdbarch_construct_inferior_arguments (struct gdbarch *gdbarch, gdbarch_construct_inferior_arguments_ftype *construct_inferior_arguments);

typedef void (gdbarch_elf_make_msymbol_special_ftype) (asymbol *sym, struct minimal_symbol *msym);
extern void gdbarch_elf_make_msymbol_special (struct gdbarch *gdbarch, asymbol *sym, struct minimal_symbol *msym);
extern void set_gdbarch_elf_make_msymbol_special (struct gdbarch *gdbarch, gdbarch_elf_make_msymbol_special_ftype *elf_make_msymbol_special);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ELF_MAKE_MSYMBOL_SPECIAL)
#error "Non multi-arch definition of ELF_MAKE_MSYMBOL_SPECIAL"
#endif
#if !defined (ELF_MAKE_MSYMBOL_SPECIAL)
#define ELF_MAKE_MSYMBOL_SPECIAL(sym, msym) (gdbarch_elf_make_msymbol_special (current_gdbarch, sym, msym))
#endif

typedef void (gdbarch_coff_make_msymbol_special_ftype) (int val, struct minimal_symbol *msym);
extern void gdbarch_coff_make_msymbol_special (struct gdbarch *gdbarch, int val, struct minimal_symbol *msym);
extern void set_gdbarch_coff_make_msymbol_special (struct gdbarch *gdbarch, gdbarch_coff_make_msymbol_special_ftype *coff_make_msymbol_special);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (COFF_MAKE_MSYMBOL_SPECIAL)
#error "Non multi-arch definition of COFF_MAKE_MSYMBOL_SPECIAL"
#endif
#if !defined (COFF_MAKE_MSYMBOL_SPECIAL)
#define COFF_MAKE_MSYMBOL_SPECIAL(val, msym) (gdbarch_coff_make_msymbol_special (current_gdbarch, val, msym))
#endif

extern const char * gdbarch_name_of_malloc (struct gdbarch *gdbarch);
extern void set_gdbarch_name_of_malloc (struct gdbarch *gdbarch, const char * name_of_malloc);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (NAME_OF_MALLOC)
#error "Non multi-arch definition of NAME_OF_MALLOC"
#endif
#if !defined (NAME_OF_MALLOC)
#define NAME_OF_MALLOC (gdbarch_name_of_malloc (current_gdbarch))
#endif

extern int gdbarch_cannot_step_breakpoint (struct gdbarch *gdbarch);
extern void set_gdbarch_cannot_step_breakpoint (struct gdbarch *gdbarch, int cannot_step_breakpoint);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (CANNOT_STEP_BREAKPOINT)
#error "Non multi-arch definition of CANNOT_STEP_BREAKPOINT"
#endif
#if !defined (CANNOT_STEP_BREAKPOINT)
#define CANNOT_STEP_BREAKPOINT (gdbarch_cannot_step_breakpoint (current_gdbarch))
#endif

extern int gdbarch_have_nonsteppable_watchpoint (struct gdbarch *gdbarch);
extern void set_gdbarch_have_nonsteppable_watchpoint (struct gdbarch *gdbarch, int have_nonsteppable_watchpoint);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (HAVE_NONSTEPPABLE_WATCHPOINT)
#error "Non multi-arch definition of HAVE_NONSTEPPABLE_WATCHPOINT"
#endif
#if !defined (HAVE_NONSTEPPABLE_WATCHPOINT)
#define HAVE_NONSTEPPABLE_WATCHPOINT (gdbarch_have_nonsteppable_watchpoint (current_gdbarch))
#endif

#if defined (ADDRESS_CLASS_TYPE_FLAGS)
/* Legacy for systems yet to multi-arch ADDRESS_CLASS_TYPE_FLAGS */
#if !defined (ADDRESS_CLASS_TYPE_FLAGS_P)
#define ADDRESS_CLASS_TYPE_FLAGS_P() (1)
#endif
#endif

extern int gdbarch_address_class_type_flags_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ADDRESS_CLASS_TYPE_FLAGS_P)
#error "Non multi-arch definition of ADDRESS_CLASS_TYPE_FLAGS"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (ADDRESS_CLASS_TYPE_FLAGS_P)
#define ADDRESS_CLASS_TYPE_FLAGS_P() (gdbarch_address_class_type_flags_p (current_gdbarch))
#endif

typedef int (gdbarch_address_class_type_flags_ftype) (int byte_size, int dwarf2_addr_class);
extern int gdbarch_address_class_type_flags (struct gdbarch *gdbarch, int byte_size, int dwarf2_addr_class);
extern void set_gdbarch_address_class_type_flags (struct gdbarch *gdbarch, gdbarch_address_class_type_flags_ftype *address_class_type_flags);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (ADDRESS_CLASS_TYPE_FLAGS)
#error "Non multi-arch definition of ADDRESS_CLASS_TYPE_FLAGS"
#endif
#if !defined (ADDRESS_CLASS_TYPE_FLAGS)
#define ADDRESS_CLASS_TYPE_FLAGS(byte_size, dwarf2_addr_class) (gdbarch_address_class_type_flags (current_gdbarch, byte_size, dwarf2_addr_class))
#endif

extern int gdbarch_address_class_type_flags_to_name_p (struct gdbarch *gdbarch);

typedef const char * (gdbarch_address_class_type_flags_to_name_ftype) (struct gdbarch *gdbarch, int type_flags);
extern const char * gdbarch_address_class_type_flags_to_name (struct gdbarch *gdbarch, int type_flags);
extern void set_gdbarch_address_class_type_flags_to_name (struct gdbarch *gdbarch, gdbarch_address_class_type_flags_to_name_ftype *address_class_type_flags_to_name);

extern int gdbarch_address_class_name_to_type_flags_p (struct gdbarch *gdbarch);

typedef int (gdbarch_address_class_name_to_type_flags_ftype) (struct gdbarch *gdbarch, const char *name, int *type_flags_ptr);
extern int gdbarch_address_class_name_to_type_flags (struct gdbarch *gdbarch, const char *name, int *type_flags_ptr);
extern void set_gdbarch_address_class_name_to_type_flags (struct gdbarch *gdbarch, gdbarch_address_class_name_to_type_flags_ftype *address_class_name_to_type_flags);

/* Is a register in a group */

typedef int (gdbarch_register_reggroup_p_ftype) (struct gdbarch *gdbarch, int regnum, struct reggroup *reggroup);
extern int gdbarch_register_reggroup_p (struct gdbarch *gdbarch, int regnum, struct reggroup *reggroup);
extern void set_gdbarch_register_reggroup_p (struct gdbarch *gdbarch, gdbarch_register_reggroup_p_ftype *register_reggroup_p);

/* Fetch the pointer to the ith function argument. */

#if defined (FETCH_POINTER_ARGUMENT)
/* Legacy for systems yet to multi-arch FETCH_POINTER_ARGUMENT */
#if !defined (FETCH_POINTER_ARGUMENT_P)
#define FETCH_POINTER_ARGUMENT_P() (1)
#endif
#endif

extern int gdbarch_fetch_pointer_argument_p (struct gdbarch *gdbarch);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FETCH_POINTER_ARGUMENT_P)
#error "Non multi-arch definition of FETCH_POINTER_ARGUMENT"
#endif
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) || !defined (FETCH_POINTER_ARGUMENT_P)
#define FETCH_POINTER_ARGUMENT_P() (gdbarch_fetch_pointer_argument_p (current_gdbarch))
#endif

typedef CORE_ADDR (gdbarch_fetch_pointer_argument_ftype) (struct frame_info *frame, int argi, struct type *type);
extern CORE_ADDR gdbarch_fetch_pointer_argument (struct gdbarch *gdbarch, struct frame_info *frame, int argi, struct type *type);
extern void set_gdbarch_fetch_pointer_argument (struct gdbarch *gdbarch, gdbarch_fetch_pointer_argument_ftype *fetch_pointer_argument);
#if (GDB_MULTI_ARCH > GDB_MULTI_ARCH_PARTIAL) && defined (FETCH_POINTER_ARGUMENT)
#error "Non multi-arch definition of FETCH_POINTER_ARGUMENT"
#endif
#if !defined (FETCH_POINTER_ARGUMENT)
#define FETCH_POINTER_ARGUMENT(frame, argi, type) (gdbarch_fetch_pointer_argument (current_gdbarch, frame, argi, type))
#endif

/* Return the appropriate register set for a core file section with
   name SECT_NAME and size SECT_SIZE. */

extern int gdbarch_regset_from_core_section_p (struct gdbarch *gdbarch);

typedef const struct regset * (gdbarch_regset_from_core_section_ftype) (struct gdbarch *gdbarch, const char *sect_name, size_t sect_size);
extern const struct regset * gdbarch_regset_from_core_section (struct gdbarch *gdbarch, const char *sect_name, size_t sect_size);
extern void set_gdbarch_regset_from_core_section (struct gdbarch *gdbarch, gdbarch_regset_from_core_section_ftype *regset_from_core_section);

extern struct gdbarch_tdep *gdbarch_tdep (struct gdbarch *gdbarch);


/* Mechanism for co-ordinating the selection of a specific
   architecture.

   GDB targets (*-tdep.c) can register an interest in a specific
   architecture.  Other GDB components can register a need to maintain
   per-architecture data.

   The mechanisms below ensures that there is only a loose connection
   between the set-architecture command and the various GDB
   components.  Each component can independently register their need
   to maintain architecture specific data with gdbarch.

   Pragmatics:

   Previously, a single TARGET_ARCHITECTURE_HOOK was provided.  It
   didn't scale.

   The more traditional mega-struct containing architecture specific
   data for all the various GDB components was also considered.  Since
   GDB is built from a variable number of (fairly independent)
   components it was determined that the global aproach was not
   applicable. */


/* Register a new architectural family with GDB.

   Register support for the specified ARCHITECTURE with GDB.  When
   gdbarch determines that the specified architecture has been
   selected, the corresponding INIT function is called.

   --

   The INIT function takes two parameters: INFO which contains the
   information available to gdbarch about the (possibly new)
   architecture; ARCHES which is a list of the previously created
   ``struct gdbarch'' for this architecture.

   The INFO parameter is, as far as possible, be pre-initialized with
   information obtained from INFO.ABFD or the previously selected
   architecture.

   The ARCHES parameter is a linked list (sorted most recently used)
   of all the previously created architures for this architecture
   family.  The (possibly NULL) ARCHES->gdbarch can used to access
   values from the previously selected architecture for this
   architecture family.  The global ``current_gdbarch'' shall not be
   used.

   The INIT function shall return any of: NULL - indicating that it
   doesn't recognize the selected architecture; an existing ``struct
   gdbarch'' from the ARCHES list - indicating that the new
   architecture is just a synonym for an earlier architecture (see
   gdbarch_list_lookup_by_info()); a newly created ``struct gdbarch''
   - that describes the selected architecture (see gdbarch_alloc()).

   The DUMP_TDEP function shall print out all target specific values.
   Care should be taken to ensure that the function works in both the
   multi-arch and non- multi-arch cases. */

struct gdbarch_list
{
  struct gdbarch *gdbarch;
  struct gdbarch_list *next;
};

struct gdbarch_info
{
  /* Use default: NULL (ZERO). */
  const struct bfd_arch_info *bfd_arch_info;

  /* Use default: BFD_ENDIAN_UNKNOWN (NB: is not ZERO).  */
  int byte_order;

  /* Use default: NULL (ZERO). */
  bfd *abfd;

  /* Use default: NULL (ZERO). */
  struct gdbarch_tdep_info *tdep_info;

  /* Use default: GDB_OSABI_UNINITIALIZED (-1).  */
  enum gdb_osabi osabi;
};

typedef struct gdbarch *(gdbarch_init_ftype) (struct gdbarch_info info, struct gdbarch_list *arches);
typedef void (gdbarch_dump_tdep_ftype) (struct gdbarch *gdbarch, struct ui_file *file);

/* DEPRECATED - use gdbarch_register() */
extern void register_gdbarch_init (enum bfd_architecture architecture, gdbarch_init_ftype *);

extern void gdbarch_register (enum bfd_architecture architecture,
                              gdbarch_init_ftype *,
                              gdbarch_dump_tdep_ftype *);


/* Return a freshly allocated, NULL terminated, array of the valid
   architecture names.  Since architectures are registered during the
   _initialize phase this function only returns useful information
   once initialization has been completed. */

extern const char **gdbarch_printable_names (void);


/* Helper function.  Search the list of ARCHES for a GDBARCH that
   matches the information provided by INFO. */

extern struct gdbarch_list *gdbarch_list_lookup_by_info (struct gdbarch_list *arches,  const struct gdbarch_info *info);


/* Helper function.  Create a preliminary ``struct gdbarch''.  Perform
   basic initialization using values obtained from the INFO andTDEP
   parameters.  set_gdbarch_*() functions are called to complete the
   initialization of the object. */

extern struct gdbarch *gdbarch_alloc (const struct gdbarch_info *info, struct gdbarch_tdep *tdep);


/* Helper function.  Free a partially-constructed ``struct gdbarch''.
   It is assumed that the caller freeds the ``struct
   gdbarch_tdep''. */

extern void gdbarch_free (struct gdbarch *);


/* Helper function.  Allocate memory from the ``struct gdbarch''
   obstack.  The memory is freed when the corresponding architecture
   is also freed.  */

extern void *gdbarch_obstack_zalloc (struct gdbarch *gdbarch, long size);
#define GDBARCH_OBSTACK_CALLOC(GDBARCH, NR, TYPE) ((TYPE *) gdbarch_obstack_zalloc ((GDBARCH), (NR) * sizeof (TYPE)))
#define GDBARCH_OBSTACK_ZALLOC(GDBARCH, TYPE) ((TYPE *) gdbarch_obstack_zalloc ((GDBARCH), sizeof (TYPE)))


/* Helper function. Force an update of the current architecture.

   The actual architecture selected is determined by INFO, ``(gdb) set
   architecture'' et.al., the existing architecture and BFD's default
   architecture.  INFO should be initialized to zero and then selected
   fields should be updated.

   Returns non-zero if the update succeeds */

extern int gdbarch_update_p (struct gdbarch_info info);


/* Helper function.  Find an architecture matching info.

   INFO should be initialized using gdbarch_info_init, relevant fields
   set, and then finished using gdbarch_info_fill.

   Returns the corresponding architecture, or NULL if no matching
   architecture was found.  "current_gdbarch" is not updated.  */

extern struct gdbarch *gdbarch_find_by_info (struct gdbarch_info info);


/* Helper function.  Set the global "current_gdbarch" to "gdbarch".

   FIXME: kettenis/20031124: Of the functions that follow, only
   gdbarch_from_bfd is supposed to survive.  The others will
   dissappear since in the future GDB will (hopefully) be truly
   multi-arch.  However, for now we're still stuck with the concept of
   a single active architecture.  */

extern void deprecated_current_gdbarch_select_hack (struct gdbarch *gdbarch);


/* Register per-architecture data-pointer.

   Reserve space for a per-architecture data-pointer.  An identifier
   for the reserved data-pointer is returned.  That identifer should
   be saved in a local static variable.

   The per-architecture data-pointer is either initialized explicitly
   (set_gdbarch_data()) or implicitly (by INIT() via a call to
   gdbarch_data()).

   Memory for the per-architecture data shall be allocated using
   gdbarch_obstack_zalloc.  That memory will be deleted when the
   corresponding architecture object is deleted.

   When a previously created architecture is re-selected, the
   per-architecture data-pointer for that previous architecture is
   restored.  INIT() is not re-called.

   Multiple registrarants for any architecture are allowed (and
   strongly encouraged).  */

struct gdbarch_data;

typedef void *(gdbarch_data_init_ftype) (struct gdbarch *gdbarch);
extern struct gdbarch_data *register_gdbarch_data (gdbarch_data_init_ftype *init);
extern void set_gdbarch_data (struct gdbarch *gdbarch,
			      struct gdbarch_data *data,
			      void *pointer);

extern void *gdbarch_data (struct gdbarch *gdbarch, struct gdbarch_data *);



/* Register per-architecture memory region.

   Provide a memory-region swap mechanism.  Per-architecture memory
   region are created.  These memory regions are swapped whenever the
   architecture is changed.  For a new architecture, the memory region
   is initialized with zero (0) and the INIT function is called.

   Memory regions are swapped / initialized in the order that they are
   registered.  NULL DATA and/or INIT values can be specified.

   New code should use register_gdbarch_data(). */

typedef void (gdbarch_swap_ftype) (void);
extern void deprecated_register_gdbarch_swap (void *data, unsigned long size, gdbarch_swap_ftype *init);
#define DEPRECATED_REGISTER_GDBARCH_SWAP(VAR) deprecated_register_gdbarch_swap (&(VAR), sizeof ((VAR)), NULL)



/* Set the dynamic target-system-dependent parameters (architecture,
   byte-order, ...) using information found in the BFD */

extern void set_gdbarch_from_file (bfd *);


/* Initialize the current architecture to the "first" one we find on
   our list.  */

extern void initialize_current_architecture (void);

/* gdbarch trace variable */
extern int gdbarch_debug;

extern void gdbarch_dump (struct gdbarch *gdbarch, struct ui_file *file);

#endif
