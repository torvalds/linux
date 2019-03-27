/* Map logical line numbers to (source file, line number) pairs.
   Copyright (C) 2001, 2003, 2004
   Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#ifndef LIBCPP_LINE_MAP_H
#define LIBCPP_LINE_MAP_H

/* Reason for adding a line change with add_line_map ().  LC_ENTER is
   when including a new file, e.g. a #include directive in C.
   LC_LEAVE is when reaching a file's end.  LC_RENAME is when a file
   name or line number changes for neither of the above reasons
   (e.g. a #line directive in C).  */
enum lc_reason {LC_ENTER = 0, LC_LEAVE, LC_RENAME};

/* A logical line/column number, i.e. an "index" into a line_map.  */
/* Long-term, we want to use this to replace struct location_s (in input.h),
   and effectively typedef source_location location_t.  */
typedef unsigned int source_location;

/* Physical source file TO_FILE at line TO_LINE at column 0 is represented
   by the logical START_LOCATION.  TO_LINE+L at column C is represented by
   START_LOCATION+(L*(1<<column_bits))+C, as long as C<(1<<column_bits),
   and the result_location is less than the next line_map's start_location.
   (The top line is line 1 and the leftmost column is column 1; line/column 0
   means "entire file/line" or "unknown line/column" or "not applicable".)
   INCLUDED_FROM is an index into the set that gives the line mapping
   at whose end the current one was included.  File(s) at the bottom
   of the include stack have this set to -1.  REASON is the reason for
   creation of this line map, SYSP is one for a system header, two for
   a C system header file that therefore needs to be extern "C"
   protected in C++, and zero otherwise.  */
struct line_map
{
  const char *to_file;
  unsigned int to_line;
  source_location start_location;
  int included_from;
  ENUM_BITFIELD (lc_reason) reason : CHAR_BIT;
  /* The sysp field isn't really needed now that it's in cpp_buffer.  */
  unsigned char sysp;
  /* Number of the low-order source_location bits used for a column number.  */
  unsigned int column_bits : 8;
};

/* A set of chronological line_map structures.  */
struct line_maps
{
  struct line_map *maps;
  unsigned int allocated;
  unsigned int used;

  unsigned int cache;

  /* The most recently listed include stack, if any, starts with
     LAST_LISTED as the topmost including file.  -1 indicates nothing
     has been listed yet.  */
  int last_listed;

  /* Depth of the include stack, including the current file.  */
  unsigned int depth;

  /* If true, prints an include trace a la -H.  */
  bool trace_includes;

  /* Highest source_location "given out".  */
  source_location highest_location;

  /* Start of line of highest source_location "given out".  */
  source_location highest_line;

  /* The maximum column number we can quickly allocate.  Higher numbers
     may require allocating a new line_map.  */
  unsigned int max_column_hint;
};

/* Initialize a line map set.  */
extern void linemap_init (struct line_maps *);

/* Free a line map set.  */
extern void linemap_free (struct line_maps *);

/* Check for and warn about line_maps entered but not exited.  */

extern void linemap_check_files_exited (struct line_maps *);

/* Return a source_location for the start (i.e. column==0) of
   (physical) line TO_LINE in the current source file (as in the
   most recent linemap_add).   MAX_COLUMN_HINT is the highest column
   number we expect to use in this line (but it does not change
   the highest_location).  */

extern source_location linemap_line_start
(struct line_maps *set, unsigned int to_line,  unsigned int max_column_hint);

/* Add a mapping of logical source line to physical source file and
   line number.

   The text pointed to by TO_FILE must have a lifetime
   at least as long as the final call to lookup_line ().  An empty
   TO_FILE means standard input.  If reason is LC_LEAVE, and
   TO_FILE is NULL, then TO_FILE, TO_LINE and SYSP are given their
   natural values considering the file we are returning to.

   A call to this function can relocate the previous set of
   maps, so any stored line_map pointers should not be used.  */
extern const struct line_map *linemap_add
  (struct line_maps *, enum lc_reason, unsigned int sysp,
   const char *to_file, unsigned int to_line);

/* Given a logical line, returns the map from which the corresponding
   (source file, line) pair can be deduced.  */
extern const struct line_map *linemap_lookup
  (struct line_maps *, source_location);

/* Print the file names and line numbers of the #include commands
   which led to the map MAP, if any, to stderr.  Nothing is output if
   the most recently listed stack is the same as the current one.  */
extern void linemap_print_containing_files (struct line_maps *,
					    const struct line_map *);

/* Converts a map and a source_location to source line.  */
#define SOURCE_LINE(MAP, LINE) \
  ((((LINE) - (MAP)->start_location) >> (MAP)->column_bits) + (MAP)->to_line)

#define SOURCE_COLUMN(MAP, LINE) \
  (((LINE) - (MAP)->start_location) & ((1 << (MAP)->column_bits) - 1))

/* Returns the last source line within a map.  This is the (last) line
   of the #include, or other directive, that caused a map change.  */
#define LAST_SOURCE_LINE(MAP) \
  SOURCE_LINE (MAP, LAST_SOURCE_LINE_LOCATION (MAP))
#define LAST_SOURCE_LINE_LOCATION(MAP) \
  ((((MAP)[1].start_location - 1 - (MAP)->start_location) \
    & ~((1 << (MAP)->column_bits) - 1))			  \
   + (MAP)->start_location)

/* Returns the map a given map was included from.  */
#define INCLUDED_FROM(SET, MAP) (&(SET)->maps[(MAP)->included_from])

/* Nonzero if the map is at the bottom of the include stack.  */
#define MAIN_FILE_P(MAP) ((MAP)->included_from < 0)

/* Set LOC to a source position that is the same line as the most recent
   linemap_line_start, but with the specified TO_COLUMN column number.  */

#define LINEMAP_POSITION_FOR_COLUMN(LOC, SET, TO_COLUMN) { \
  unsigned int to_column = (TO_COLUMN); \
  struct line_maps *set = (SET); \
  if (__builtin_expect (to_column >= set->max_column_hint, 0)) \
    (LOC) = linemap_position_for_column (set, to_column); \
  else { \
    source_location r = set->highest_line; \
    r = r + to_column; \
    if (r >= set->highest_location) \
      set->highest_location = r; \
    (LOC) = r;			 \
  }}
    

extern source_location
linemap_position_for_column (struct line_maps *set, unsigned int to_column);
#endif /* !LIBCPP_LINE_MAP_H  */
