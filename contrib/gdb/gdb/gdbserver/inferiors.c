/* Inferior process information for the remote server for GDB.
   Copyright 2002
   Free Software Foundation, Inc.

   Contributed by MontaVista Software.

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

#include <stdlib.h>

#include "server.h"

struct thread_info
{
  struct inferior_list_entry entry;
  void *target_data;
  void *regcache_data;
};

struct inferior_list all_threads;

struct thread_info *current_inferior;

#define get_thread(inf) ((struct thread_info *)(inf))

void
add_inferior_to_list (struct inferior_list *list,
		      struct inferior_list_entry *new_inferior)
{
  new_inferior->next = NULL;
  if (list->tail != NULL)
    list->tail->next = new_inferior;
  else
    list->head = new_inferior;
  list->tail = new_inferior;
}

void
for_each_inferior (struct inferior_list *list,
		   void (*action) (struct inferior_list_entry *))
{
  struct inferior_list_entry *cur = list->head, *next;

  while (cur != NULL)
    {
      next = cur->next;
      (*action) (cur);
      cur = next;
    }
}

void
change_inferior_id (struct inferior_list *list,
		    int new_id)
{
  if (list->head != list->tail)
    error ("tried to change thread ID after multiple threads are created");

  list->head->id = new_id;
}

void
remove_inferior (struct inferior_list *list,
		 struct inferior_list_entry *entry)
{
  struct inferior_list_entry **cur;

  if (list->head == entry)
    {
      list->head = entry->next;
      if (list->tail == entry)
	list->tail = list->head;
      return;
    }

  cur = &list->head;
  while (*cur && (*cur)->next != entry)
    cur = &(*cur)->next;

  if (*cur == NULL)
    return;

  (*cur)->next = entry->next;

  if (list->tail == entry)
    list->tail = *cur;
}

void
add_thread (int thread_id, void *target_data)
{
  struct thread_info *new_thread
    = (struct thread_info *) malloc (sizeof (*new_thread));

  memset (new_thread, 0, sizeof (*new_thread));

  new_thread->entry.id = thread_id;

  add_inferior_to_list (&all_threads, & new_thread->entry);
  
  if (current_inferior == NULL)
    current_inferior = new_thread;

  new_thread->target_data = target_data;
  set_inferior_regcache_data (new_thread, new_register_cache ());
}

static void
free_one_thread (struct inferior_list_entry *inf)
{
  struct thread_info *thread = get_thread (inf);
  free_register_cache (inferior_regcache_data (thread));
  free (thread);
}

void
remove_thread (struct thread_info *thread)
{
  remove_inferior (&all_threads, (struct inferior_list_entry *) thread);
  free_one_thread (&thread->entry);
}

void
clear_inferiors (void)
{
  for_each_inferior (&all_threads, free_one_thread);

  all_threads.head = all_threads.tail = NULL;
}

struct inferior_list_entry *
find_inferior (struct inferior_list *list,
	       int (*func) (struct inferior_list_entry *, void *), void *arg)
{
  struct inferior_list_entry *inf = list->head;

  while (inf != NULL)
    {
      if ((*func) (inf, arg))
	return inf;
      inf = inf->next;
    }

  return NULL;
}

struct inferior_list_entry *
find_inferior_id (struct inferior_list *list, int id)
{
  struct inferior_list_entry *inf = list->head;

  while (inf != NULL)
    {
      if (inf->id == id)
	return inf;
      inf = inf->next;
    }

  return NULL;
}

void *
inferior_target_data (struct thread_info *inferior)
{
  return inferior->target_data;
}

void
set_inferior_target_data (struct thread_info *inferior, void *data)
{
  inferior->target_data = data;
}

void *
inferior_regcache_data (struct thread_info *inferior)
{
  return inferior->regcache_data;
}

void
set_inferior_regcache_data (struct thread_info *inferior, void *data)
{
  inferior->regcache_data = data;
}
