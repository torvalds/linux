/* Annotation routines for GDB.
   Copyright 1986, 1989, 1990, 1991, 1992, 1994, 1995, 1996, 1998, 1999,
   2000 Free Software Foundation, Inc.

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
#include "annotate.h"
#include "value.h"
#include "target.h"
#include "gdbtypes.h"
#include "breakpoint.h"


/* Prototypes for local functions. */

extern void _initialize_annotate (void);

static void print_value_flags (struct type *);

static void breakpoint_changed (struct breakpoint *);

void (*annotate_starting_hook) (void);
void (*annotate_stopped_hook) (void);
void (*annotate_signalled_hook) (void);
void (*annotate_signal_hook) (void);
void (*annotate_exited_hook) (void);

static int ignore_count_changed = 0;

static void
print_value_flags (struct type *t)
{
  if (can_dereference (t))
    printf_filtered ("*");
  else
    printf_filtered ("-");
}

void
breakpoints_changed (void)
{
  if (annotation_level > 1)
    {
      target_terminal_ours ();
      printf_unfiltered ("\n\032\032breakpoints-invalid\n");
      if (ignore_count_changed)
	ignore_count_changed = 0;	/* Avoid multiple break annotations. */
    }
}

/* The GUI needs to be informed of ignore_count changes, but we don't
   want to provide successive multiple breakpoints-invalid messages
   that are all caused by the fact that the ignore count is changing
   (which could keep the GUI very busy).  One is enough, after the
   target actually "stops". */

void
annotate_ignore_count_change (void)
{
  if (annotation_level > 1)
    ignore_count_changed = 1;
}

void
annotate_breakpoint (int num)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032breakpoint %d\n", num);
}

void
annotate_catchpoint (int num)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032catchpoint %d\n", num);
}

void
annotate_watchpoint (int num)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032watchpoint %d\n", num);
}

void
annotate_starting (void)
{

  if (annotate_starting_hook)
    annotate_starting_hook ();
  else
    {
      if (annotation_level > 1)
	{
	  printf_filtered ("\n\032\032starting\n");
	}
    }
}

void
annotate_stopped (void)
{
  if (annotate_stopped_hook)
    annotate_stopped_hook ();
  else
    {
      if (annotation_level > 1)
	printf_filtered ("\n\032\032stopped\n");
    }
  if (annotation_level > 1 && ignore_count_changed)
    {
      ignore_count_changed = 0;
      breakpoints_changed ();
    }
}

void
annotate_exited (int exitstatus)
{
  if (annotate_exited_hook)
    annotate_exited_hook ();
  else
    {
      if (annotation_level > 1)
	printf_filtered ("\n\032\032exited %d\n", exitstatus);
    }
}

void
annotate_signalled (void)
{
  if (annotate_signalled_hook)
    annotate_signalled_hook ();

  if (annotation_level > 1)
    printf_filtered ("\n\032\032signalled\n");
}

void
annotate_signal_name (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032signal-name\n");
}

void
annotate_signal_name_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032signal-name-end\n");
}

void
annotate_signal_string (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032signal-string\n");
}

void
annotate_signal_string_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032signal-string-end\n");
}

void
annotate_signal (void)
{
  if (annotate_signal_hook)
    annotate_signal_hook ();

  if (annotation_level > 1)
    printf_filtered ("\n\032\032signal\n");
}

void
annotate_breakpoints_headers (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032breakpoints-headers\n");
}

void
annotate_field (int num)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032field %d\n", num);
}

void
annotate_breakpoints_table (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032breakpoints-table\n");
}

void
annotate_record (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032record\n");
}

void
annotate_breakpoints_table_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032breakpoints-table-end\n");
}

void
annotate_frames_invalid (void)
{
  if (annotation_level > 1)
    {
      target_terminal_ours ();
      printf_unfiltered ("\n\032\032frames-invalid\n");
    }
}

void
annotate_field_begin (struct type *type)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032field-begin ");
      print_value_flags (type);
      printf_filtered ("\n");
    }
}

void
annotate_field_name_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032field-name-end\n");
}

void
annotate_field_value (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032field-value\n");
}

void
annotate_field_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032field-end\n");
}

void
annotate_quit (void)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032quit\n");
}

void
annotate_error (void)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032error\n");
}

void
annotate_error_begin (void)
{
  if (annotation_level > 1)
    fprintf_filtered (gdb_stderr, "\n\032\032error-begin\n");
}

void
annotate_value_history_begin (int histindex, struct type *type)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032value-history-begin %d ", histindex);
      print_value_flags (type);
      printf_filtered ("\n");
    }
}

void
annotate_value_begin (struct type *type)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032value-begin ");
      print_value_flags (type);
      printf_filtered ("\n");
    }
}

void
annotate_value_history_value (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032value-history-value\n");
}

void
annotate_value_history_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032value-history-end\n");
}

void
annotate_value_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032value-end\n");
}

void
annotate_display_begin (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-begin\n");
}

void
annotate_display_number_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-number-end\n");
}

void
annotate_display_format (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-format\n");
}

void
annotate_display_expression (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-expression\n");
}

void
annotate_display_expression_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-expression-end\n");
}

void
annotate_display_value (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-value\n");
}

void
annotate_display_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032display-end\n");
}

void
annotate_arg_begin (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032arg-begin\n");
}

void
annotate_arg_name_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032arg-name-end\n");
}

void
annotate_arg_value (struct type *type)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032arg-value ");
      print_value_flags (type);
      printf_filtered ("\n");
    }
}

void
annotate_arg_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032arg-end\n");
}

void
annotate_source (char *filename, int line, int character, int mid, CORE_ADDR pc)
{
  if (annotation_level > 1)
    printf_filtered ("\n\032\032source ");
  else
    printf_filtered ("\032\032");

  printf_filtered ("%s:%d:%d:%s:0x", filename,
		   line, character,
		   mid ? "middle" : "beg");
  print_address_numeric (pc, 0, gdb_stdout);
  printf_filtered ("\n");
}

void
annotate_frame_begin (int level, CORE_ADDR pc)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032frame-begin %d 0x", level);
      print_address_numeric (pc, 0, gdb_stdout);
      printf_filtered ("\n");
    }
}

void
annotate_function_call (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032function-call\n");
}

void
annotate_signal_handler_caller (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032signal-handler-caller\n");
}

void
annotate_frame_address (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-address\n");
}

void
annotate_frame_address_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-address-end\n");
}

void
annotate_frame_function_name (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-function-name\n");
}

void
annotate_frame_args (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-args\n");
}

void
annotate_frame_source_begin (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-source-begin\n");
}

void
annotate_frame_source_file (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-source-file\n");
}

void
annotate_frame_source_file_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-source-file-end\n");
}

void
annotate_frame_source_line (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-source-line\n");
}

void
annotate_frame_source_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-source-end\n");
}

void
annotate_frame_where (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-where\n");
}

void
annotate_frame_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032frame-end\n");
}

void
annotate_array_section_begin (int index, struct type *elttype)
{
  if (annotation_level == 2)
    {
      printf_filtered ("\n\032\032array-section-begin %d ", index);
      print_value_flags (elttype);
      printf_filtered ("\n");
    }
}

void
annotate_elt_rep (unsigned int repcount)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032elt-rep %u\n", repcount);
}

void
annotate_elt_rep_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032elt-rep-end\n");
}

void
annotate_elt (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032elt\n");
}

void
annotate_array_section_end (void)
{
  if (annotation_level == 2)
    printf_filtered ("\n\032\032array-section-end\n");
}

static void
breakpoint_changed (struct breakpoint *b)
{
  breakpoints_changed ();
}

void
_initialize_annotate (void)
{
  if (annotation_level > 1)
    {
      delete_breakpoint_hook = breakpoint_changed;
      modify_breakpoint_hook = breakpoint_changed;
    }
}
