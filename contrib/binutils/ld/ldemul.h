/* ld-emul.h - Linker emulation header file
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2000, 2001,
   2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

#ifndef LDEMUL_H
#define LDEMUL_H

/* Forward declaration for ldemul_add_options() and others.  */
struct option;

extern void ldemul_hll
  (char *);
extern void ldemul_syslib
  (char *);
extern void ldemul_after_parse
  (void);
extern void ldemul_before_parse
  (void);
extern void ldemul_after_open
  (void);
extern void ldemul_after_allocation
  (void);
extern void ldemul_before_allocation
  (void);
extern void ldemul_set_output_arch
  (void);
extern char *ldemul_choose_target
  (int, char**);
extern void ldemul_choose_mode
  (char *);
extern void ldemul_list_emulations
  (FILE *);
extern void ldemul_list_emulation_options
  (FILE *);
extern char *ldemul_get_script
  (int *isfile);
extern void ldemul_finish
  (void);
extern void ldemul_set_symbols
  (void);
extern void ldemul_create_output_section_statements
  (void);
extern bfd_boolean ldemul_place_orphan
  (asection *);
extern bfd_boolean ldemul_parse_args
  (int, char **);
extern void ldemul_add_options
  (int, char **, int, struct option **, int, struct option **);
extern bfd_boolean ldemul_handle_option
  (int);
extern bfd_boolean ldemul_unrecognized_file
  (struct lang_input_statement_struct *);
extern bfd_boolean ldemul_recognized_file
  (struct lang_input_statement_struct *);
extern bfd_boolean ldemul_open_dynamic_archive
  (const char *, struct search_dirs *, struct lang_input_statement_struct *);
extern char *ldemul_default_target
  (int, char**);
extern void after_parse_default
  (void);
extern void after_open_default
  (void);
extern void after_allocation_default
  (void);
extern void before_allocation_default
  (void);
extern void finish_default
  (void);
extern void finish_default
  (void);
extern void set_output_arch_default
  (void);
extern void syslib_default
  (char*);
extern void hll_default
  (char*);
extern int  ldemul_find_potential_libraries
  (char *, struct lang_input_statement_struct *);
extern struct bfd_elf_version_expr *ldemul_new_vers_pattern
  (struct bfd_elf_version_expr *);

typedef struct ld_emulation_xfer_struct {
  /* Run before parsing the command line and script file.
     Set the architecture, maybe other things.  */
  void   (*before_parse) (void);

  /* Handle the SYSLIB (low level library) script command.  */
  void   (*syslib) (char *);

  /* Handle the HLL (high level library) script command.  */
  void   (*hll) (char *);

  /* Run after parsing the command line and script file.  */
  void   (*after_parse) (void);

  /* Run after opening all input files, and loading the symbols.  */
  void   (*after_open) (void);

  /* Run after allocating output sections.  */
  void   (*after_allocation)  (void);

  /* Set the output architecture and machine if possible.  */
  void   (*set_output_arch) (void);

  /* Decide which target name to use.  */
  char * (*choose_target) (int, char**);

  /* Run before allocating output sections.  */
  void   (*before_allocation) (void);

  /* Return the appropriate linker script.  */
  char * (*get_script) (int *isfile);

  /* The name of this emulation.  */
  char *emulation_name;

  /* The output format.  */
  char *target_name;

  /* Run after assigning values from the script.  */
  void	(*finish) (void);

  /* Create any output sections needed by the target.  */
  void	(*create_output_section_statements) (void);

  /* Try to open a dynamic library.  ARCH is an architecture name, and
     is normally the empty string.  ENTRY is the lang_input_statement
     that should be opened.  */
  bfd_boolean (*open_dynamic_archive)
    (const char *arch, struct search_dirs *,
     struct lang_input_statement_struct *entry);

  /* Place an orphan section.  Return TRUE if it was placed, FALSE if
     the default action should be taken.  This field may be NULL, in
     which case the default action will always be taken.  */
  bfd_boolean (*place_orphan)
    (asection *);

  /* Run after assigning parsing with the args, but before
     reading the script.  Used to initialize symbols used in the script.  */
  void	(*set_symbols) (void);

  /* Parse args which the base linker doesn't understand.
     Return TRUE if the arg needs no further processing.  */
  bfd_boolean (*parse_args) (int, char **);

  /* Hook to add options to parameters passed by the base linker to
     getopt_long and getopt_long_only calls.  */
  void (*add_options)
    (int, char **, int, struct option **, int, struct option **);

  /* Companion to the above to handle an option.  Returns TRUE if it is
     one of our options.  */
  bfd_boolean (*handle_option) (int);

  /* Run to handle files which are not recognized as object files or
     archives.  Return TRUE if the file was handled.  */
  bfd_boolean (*unrecognized_file)
    (struct lang_input_statement_struct *);

  /* Run to list the command line options which parse_args handles.  */
  void (* list_options) (FILE *);

  /* Run to specially handle files which *are* recognized as object
     files or archives.  Return TRUE if the file was handled.  */
  bfd_boolean (*recognized_file)
    (struct lang_input_statement_struct *);

  /* Called when looking for libraries in a directory specified
     via a linker command line option or linker script option.
     Files that match the pattern "lib*.a" have already been scanned.
     (For VMS files matching ":lib*.a" have also been scanned).  */
  int (* find_potential_libraries)
    (char *, struct lang_input_statement_struct *);

  /* Called when adding a new version pattern.  PowerPC64-ELF uses
     this hook to add a pattern matching ".foo" for every "foo".  */
  struct bfd_elf_version_expr * (*new_vers_pattern)
    (struct bfd_elf_version_expr *);

} ld_emulation_xfer_type;

typedef enum {
  intel_ic960_ld_mode_enum,
  default_mode_enum,
  intel_gld960_ld_mode_enum
} lang_emulation_mode_enum_type;

extern ld_emulation_xfer_type *ld_emulations[];

#endif
