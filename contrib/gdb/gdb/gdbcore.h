/* Machine independent variables that describe the core file under GDB.
   Copyright 1986, 1987, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.

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

/* Interface routines for core, executable, etc.  */

#if !defined (GDBCORE_H)
#define GDBCORE_H 1

struct type;

#include "bfd.h"

/* Return the name of the executable file as a string.
   ERR nonzero means get error if there is none specified;
   otherwise return 0 in that case.  */

extern char *get_exec_file (int err);

/* Nonzero if there is a core file.  */

extern int have_core_file_p (void);

/* Read "memory data" from whatever target or inferior we have.
   Returns zero if successful, errno value if not.  EIO is used for
   address out of bounds.  If breakpoints are inserted, returns shadow
   contents, not the breakpoints themselves.  From breakpoint.c.  */

extern int read_memory_nobpt (CORE_ADDR memaddr, char *myaddr, unsigned len);

/* Report a memory error with error().  */

extern void memory_error (int status, CORE_ADDR memaddr);

/* Like target_read_memory, but report an error if can't read.  */

extern void read_memory (CORE_ADDR memaddr, char *myaddr, int len);

/* Read an integer from debugged memory, given address and number of
   bytes.  */

extern LONGEST read_memory_integer (CORE_ADDR memaddr, int len);
extern int safe_read_memory_integer (CORE_ADDR memaddr, int len, LONGEST *return_value);

/* Read an unsigned integer from debugged memory, given address and
   number of bytes.  */

extern ULONGEST read_memory_unsigned_integer (CORE_ADDR memaddr, int len);

/* Read a null-terminated string from the debuggee's memory, given address,
 * a buffer into which to place the string, and the maximum available space */

extern void read_memory_string (CORE_ADDR, char *, int);

/* Read the pointer of type TYPE at ADDR, and return the address it
   represents. */

CORE_ADDR read_memory_typed_address (CORE_ADDR addr, struct type *type);

/* This takes a char *, not void *.  This is probably right, because
   passing in an int * or whatever is wrong with respect to
   byteswapping, alignment, different sizes for host vs. target types,
   etc.  */

extern void write_memory (CORE_ADDR memaddr, char *myaddr, int len);

/* Store VALUE at ADDR in the inferior as a LEN-byte unsigned integer.  */
extern void write_memory_unsigned_integer (CORE_ADDR addr, int len,
                                           ULONGEST value);

/* Store VALUE at ADDR in the inferior as a LEN-byte unsigned integer.  */
extern void write_memory_signed_integer (CORE_ADDR addr, int len,
                                         LONGEST value);

extern void generic_search (int len, char *data, char *mask,
			    CORE_ADDR startaddr, int increment,
			    CORE_ADDR lorange, CORE_ADDR hirange,
			    CORE_ADDR * addr_found, char *data_found);

/* Hook for `exec_file_command' command to call.  */

extern void (*exec_file_display_hook) (char *filename);

/* Hook for "file_command", which is more useful than above
   (because it is invoked AFTER symbols are read, not before) */

extern void (*file_changed_hook) (char *filename);

extern void specify_exec_file_hook (void (*hook) (char *filename));

/* Binary File Diddlers for the exec and core files */

extern bfd *core_bfd;
extern bfd *exec_bfd;

/* Whether to open exec and core files read-only or read-write.  */

extern int write_files;

extern void core_file_command (char *filename, int from_tty);

extern void exec_open (char *filename, int from_tty);

extern void exec_file_attach (char *filename, int from_tty);

extern void exec_file_clear (int from_tty);

extern void validate_files (void);

extern CORE_ADDR register_addr (int regno, CORE_ADDR blockend);

#if !defined (KERNEL_U_ADDR)
extern CORE_ADDR kernel_u_addr;
#define KERNEL_U_ADDR kernel_u_addr
#endif

/* The target vector for core files. */

extern struct target_ops core_ops;

/* The current default bfd target.  */

extern char *gnutarget;

extern void set_gnutarget (char *);

/* Structure to keep track of core register reading functions for
   various core file types.  */

struct core_fns
  {

    /* BFD flavour that a core file handler is prepared to read.  This
       can be used by the handler's core tasting function as a first
       level filter to reject BFD's that don't have the right
       flavour. */

    enum bfd_flavour core_flavour;

    /* Core file handler function to call to recognize corefile
       formats that BFD rejects.  Some core file format just don't fit
       into the BFD model, or may require other resources to identify
       them, that simply aren't available to BFD (such as symbols from
       another file).  Returns nonzero if the handler recognizes the
       format, zero otherwise. */

    int (*check_format) (bfd *);

    /* Core file handler function to call to ask if it can handle a
       given core file format or not.  Returns zero if it can't,
       nonzero otherwise. */

    int (*core_sniffer) (struct core_fns *, bfd *);

    /* Extract the register values out of the core file and store them where
       `read_register' will find them.

       CORE_REG_SECT points to the register values themselves, read into
       memory.

       CORE_REG_SIZE is the size of that area.

       WHICH says which set of registers we are handling:
         0 --- integer registers
         2 --- floating-point registers, on machines where they are
               discontiguous
         3 --- extended floating-point registers, on machines where
               these are present in yet a third area.  (GNU/Linux uses
               this to get at the SSE registers.)

       REG_ADDR is the offset from u.u_ar0 to the register values relative to
       core_reg_sect.  This is used with old-fashioned core files to locate the
       registers in a large upage-plus-stack ".reg" section.  Original upage
       address X is at location core_reg_sect+x+reg_addr. */

    void (*core_read_registers) (char *core_reg_sect,
				 unsigned core_reg_size,
				 int which, CORE_ADDR reg_addr);

    /* Finds the next struct core_fns.  They are allocated and initialized
       in whatever module implements the functions pointed to; an 
       initializer calls add_core_fns to add them to the global chain.  */

    struct core_fns *next;

  };

extern void add_core_fns (struct core_fns *cf);
extern int default_core_sniffer (struct core_fns *cf, bfd * abfd);
extern int default_check_format (bfd * abfd);

#endif /* !defined (GDBCORE_H) */
