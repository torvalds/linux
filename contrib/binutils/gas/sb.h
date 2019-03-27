/* sb.h - header file for string buffer manipulation routines
   Copyright 1994, 1995, 2000, 2003, 2006 Free Software Foundation, Inc.

   Written by Steve and Judy Chamberlain of Cygnus Support,
      sac@cygnus.com

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef SB_H

#define SB_H

/* String blocks

   I had a couple of choices when deciding upon this data structure.
   gas uses null terminated strings for all its internal work.  This
   often means that parts of the program that want to examine
   substrings have to manipulate the data in the string to do the
   right thing (a common operation is to single out a bit of text by
   saving away the character after it, nulling it out, operating on
   the substring and then replacing the character which was under the
   null).  This is a pain and I remember a load of problems that I had with
   code in gas which almost got this right.  Also, it's harder to grow and
   allocate null terminated strings efficiently.

   Obstacks provide all the functionality needed, but are too
   complicated, hence the sb.

   An sb is allocated by the caller, and is initialized to point to an
   sb_element.  sb_elements are kept on a free lists, and used when
   needed, replaced onto the free list when unused.  */

#define sb_max_power_two    30	/* Don't allow strings more than
			           2^sb_max_power_two long.  */

typedef struct sb
{
  char *ptr;			/* Points to the current block.  */
  int len;			/* How much is used.  */
  int pot;			/* The maximum length is 1<<pot.  */
  struct le *item;
}
sb;

/* Structure of the free list object of a string block.  */

typedef struct le
{
  struct le *next;
  int size;
  char data[1];
}
sb_element;

extern void sb_new (sb *);
extern void sb_kill (sb *);
extern void sb_add_sb (sb *, sb *);
extern void sb_scrub_and_add_sb (sb *, sb *);
extern void sb_reset (sb *);
extern void sb_add_char (sb *, int);
extern void sb_add_string (sb *, const char *);
extern void sb_add_buffer (sb *, const char *, int);
extern char *sb_terminate (sb *);
extern int sb_skip_white (int, sb *);
extern int sb_skip_comma (int, sb *);

/* Actually in input-scrub.c.  */
extern void input_scrub_include_sb (sb *, char *, int);

#endif /* SB_H */
