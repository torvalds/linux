/* sb.c - string buffer manipulation routines
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

#include "as.h"
#include "sb.h"

/* These routines are about manipulating strings.

   They are managed in things called `sb's which is an abbreviation
   for string buffers.  An sb has to be created, things can be glued
   on to it, and at the end of it's life it should be freed.  The
   contents should never be pointed at whilst it is still growing,
   since it could be moved at any time

   eg:
   sb_new (&foo);
   sb_grow... (&foo,...);
   use foo->ptr[*];
   sb_kill (&foo);  */

static int dsize = 5;
static void sb_check (sb *, int);

/* Statistics of sb structures.  */
static int string_count[sb_max_power_two];

/* Free list of sb structures.  */
static struct
{
  sb_element *size[sb_max_power_two];
} free_list;

/* Initializes an sb.  */

static void
sb_build (sb *ptr, int size)
{
  /* See if we can find one to allocate.  */
  sb_element *e;

  assert (size < sb_max_power_two);

  e = free_list.size[size];
  if (!e)
    {
      /* Nothing there, allocate one and stick into the free list.  */
      e = (sb_element *) xmalloc (sizeof (sb_element) + (1 << size));
      e->next = free_list.size[size];
      e->size = 1 << size;
      free_list.size[size] = e;
      string_count[size]++;
    }

  /* Remove from free list.  */
  free_list.size[size] = e->next;

  /* Copy into callers world.  */
  ptr->ptr = e->data;
  ptr->pot = size;
  ptr->len = 0;
  ptr->item = e;
}

void
sb_new (sb *ptr)
{
  sb_build (ptr, dsize);
}

/* Deallocate the sb at ptr.  */

void
sb_kill (sb *ptr)
{
  /* Return item to free list.  */
  ptr->item->next = free_list.size[ptr->pot];
  free_list.size[ptr->pot] = ptr->item;
}

/* Add the sb at s to the end of the sb at ptr.  */

void
sb_add_sb (sb *ptr, sb *s)
{
  sb_check (ptr, s->len);
  memcpy (ptr->ptr + ptr->len, s->ptr, s->len);
  ptr->len += s->len;
}

/* Helper for sb_scrub_and_add_sb.  */

static sb *sb_to_scrub;
static char *scrub_position;
static int
scrub_from_sb (char *buf, int buflen)
{
  int copy;
  copy = sb_to_scrub->len - (scrub_position - sb_to_scrub->ptr);
  if (copy > buflen)
    copy = buflen;
  memcpy (buf, scrub_position, copy);
  scrub_position += copy;
  return copy;
}

/* Run the sb at s through do_scrub_chars and add the result to the sb
   at ptr.  */

void
sb_scrub_and_add_sb (sb *ptr, sb *s)
{
  sb_to_scrub = s;
  scrub_position = s->ptr;
  
  sb_check (ptr, s->len);
  ptr->len += do_scrub_chars (scrub_from_sb, ptr->ptr + ptr->len, s->len);

  sb_to_scrub = 0;
  scrub_position = 0;
}

/* Make sure that the sb at ptr has room for another len characters,
   and grow it if it doesn't.  */

static void
sb_check (sb *ptr, int len)
{
  if (ptr->len + len >= 1 << ptr->pot)
    {
      sb tmp;
      int pot = ptr->pot;

      while (ptr->len + len >= 1 << pot)
	pot++;
      sb_build (&tmp, pot);
      sb_add_sb (&tmp, ptr);
      sb_kill (ptr);
      *ptr = tmp;
    }
}

/* Make the sb at ptr point back to the beginning.  */

void
sb_reset (sb *ptr)
{
  ptr->len = 0;
}

/* Add character c to the end of the sb at ptr.  */

void
sb_add_char (sb *ptr, int c)
{
  sb_check (ptr, 1);
  ptr->ptr[ptr->len++] = c;
}

/* Add null terminated string s to the end of sb at ptr.  */

void
sb_add_string (sb *ptr, const char *s)
{
  int len = strlen (s);
  sb_check (ptr, len);
  memcpy (ptr->ptr + ptr->len, s, len);
  ptr->len += len;
}

/* Add string at s of length len to sb at ptr */

void
sb_add_buffer (sb *ptr, const char *s, int len)
{
  sb_check (ptr, len);
  memcpy (ptr->ptr + ptr->len, s, len);
  ptr->len += len;
}

/* Like sb_name, but don't include the null byte in the string.  */

char *
sb_terminate (sb *in)
{
  sb_add_char (in, 0);
  --in->len;
  return in->ptr;
}

/* Start at the index idx into the string in sb at ptr and skip
   whitespace. return the index of the first non whitespace character.  */

int
sb_skip_white (int idx, sb *ptr)
{
  while (idx < ptr->len
	 && (ptr->ptr[idx] == ' '
	     || ptr->ptr[idx] == '\t'))
    idx++;
  return idx;
}

/* Start at the index idx into the sb at ptr. skips whitespace,
   a comma and any following whitespace. returns the index of the
   next character.  */

int
sb_skip_comma (int idx, sb *ptr)
{
  while (idx < ptr->len
	 && (ptr->ptr[idx] == ' '
	     || ptr->ptr[idx] == '\t'))
    idx++;

  if (idx < ptr->len
      && ptr->ptr[idx] == ',')
    idx++;

  while (idx < ptr->len
	 && (ptr->ptr[idx] == ' '
	     || ptr->ptr[idx] == '\t'))
    idx++;

  return idx;
}
