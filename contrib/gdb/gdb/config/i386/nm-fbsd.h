/* Native-dependent definitions for FreeBSD/i386.

   Copyright 1986, 1987, 1989, 1992, 1994, 1996, 1997, 2000, 2001, 2004
   Free Software Foundation, Inc.

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

#ifndef NM_FBSD_H
#define NM_FBSD_H

#ifdef HAVE_PT_GETDBREGS
#define I386_USE_GENERIC_WATCHPOINTS
#endif

#include "i386/nm-i386.h"

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

/* Provide access to the i386 hardware debugging registers.  */

#define I386_DR_LOW_SET_CONTROL(control) \
  i386bsd_dr_set_control (control)
extern void i386bsd_dr_set_control (unsigned long control);

#define I386_DR_LOW_SET_ADDR(regnum, addr) \
  i386bsd_dr_set_addr (regnum, addr)
extern void i386bsd_dr_set_addr (int regnum, CORE_ADDR addr);

#define I386_DR_LOW_RESET_ADDR(regnum) \
  i386bsd_dr_reset_addr (regnum)
extern void i386bsd_dr_reset_addr (int regnum);

#define I386_DR_LOW_GET_STATUS() \
  i386bsd_dr_get_status ()
extern unsigned long i386bsd_dr_get_status (void);


/* Type of the third argument to the `ptrace' system call.  */
#define PTRACE_ARG3_TYPE caddr_t

/* Override copies of {fetch,store}_inferior_registers in `infptrace.c'.  */
#define FETCH_INFERIOR_REGISTERS

/* Override child_resume in `infptrace.c' to work around a kernel bug.  */
#define CHILD_RESUME

/* Override child_pid_to_exec_file in 'inftarg.c'.  */
#define CHILD_PID_TO_EXEC_FILE

/* We can attach and detach.  */
#define ATTACH_DETACH


/* Support for the user struct.  */

/* Return the size of the user struct.  */

#define KERNEL_U_SIZE kernel_u_size ()
extern int kernel_u_size (void);

/* This is the amount to subtract from u.u_ar0
   to get the offset in the core file of the register values.  */

#include <machine/vmparam.h>
#define KERNEL_U_ADDR USRSTACK

#define REGISTER_U_ADDR(addr, blockend, regno) \
  (addr) = register_u_addr ((blockend), (regno))
extern CORE_ADDR register_u_addr (CORE_ADDR blockend, int regno);


/* Shared library support.  */

#include "solib.h"

/* Make structure definitions match up with those expected in
   `solib-sunos.c'.  */

#define link_object	sod
#define lo_name		sod_name
#define lo_library	sod_library
#define lo_unused	sod_reserved
#define lo_major	sod_major
#define lo_minor	sod_minor
#define lo_next		sod_next

#define link_map	so_map
#define lm_addr		som_addr
#define lm_name		som_path
#define lm_next		som_next
#define lm_lop		som_sod
#define lm_lob		som_sodbase
#define lm_rwt		som_write
#define lm_ld		som_dynamic
#define lm_lpd		som_spd

#define link_dynamic_2	section_dispatch_table
#define ld_loaded	sdt_loaded
#define ld_need		sdt_sods
#define ld_rules	sdt_filler1
#define ld_got		sdt_got
#define ld_plt		sdt_plt
#define ld_rel		sdt_rel
#define ld_hash		sdt_hash
#define ld_stab		sdt_nzlist
#define ld_stab_hash	sdt_filler2
#define ld_buckets	sdt_buckets
#define ld_symbols	sdt_strings
#define ld_symb_size	sdt_str_sz
#define ld_text		sdt_text_sz
#define ld_plt_sz	sdt_plt_sz

#define rtc_symb	rt_symbol
#define rtc_sp		rt_sp
#define rtc_next	rt_next

#define ld_debug	so_debug
#define ldd_version	dd_version
#define ldd_in_debugger	dd_in_debugger
#define ldd_sym_loaded	dd_sym_loaded
#define ldd_bp_addr	dd_bpt_addr
#define ldd_bp_inst	dd_bpt_shadow
#define ldd_cp		dd_cc

#define link_dynamic	_dynamic
#define ld_version	d_version
#define ldd		d_debug
#define ld_un		d_un
#define ld_2		d_sdt

#endif /* nm-fbsd.h */
