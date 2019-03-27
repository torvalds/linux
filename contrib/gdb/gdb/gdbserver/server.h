/* Common definitions for remote server for GDB.
   Copyright 1993, 1995, 1997, 1998, 1999, 2000, 2002, 2003, 2004
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

#ifndef SERVER_H
#define SERVER_H

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef NEED_DECLARATION_STRERROR
#ifndef strerror
extern char *strerror (int);	/* X3.159-1989  4.11.6.2 */
#endif
#endif

#ifndef ATTR_NORETURN
#if defined(__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7))
#define ATTR_NORETURN __attribute__ ((noreturn))
#else
#define ATTR_NORETURN           /* nothing */
#endif
#endif

#ifndef ATTR_FORMAT
#if defined(__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 4))
#define ATTR_FORMAT(type, x, y) __attribute__ ((format(type, x, y)))
#else
#define ATTR_FORMAT(type, x, y) /* nothing */
#endif
#endif

/* FIXME: This should probably be autoconf'd for.  It's an integer type at
   least the size of a (void *).  */
typedef long long CORE_ADDR;

/* Generic information for tracking a list of ``inferiors'' - threads,
   processes, etc.  */
struct inferior_list
{
  struct inferior_list_entry *head;
  struct inferior_list_entry *tail;
};
struct inferior_list_entry
{
  int id;
  struct inferior_list_entry *next;
};

/* Opaque type for user-visible threads.  */
struct thread_info;

#include "regcache.h"
#include "gdb/signals.h"

#include "target.h"
#include "mem-break.h"

/* Target-specific functions */

void initialize_low ();

/* From inferiors.c.  */

extern struct inferior_list all_threads;
void add_inferior_to_list (struct inferior_list *list,
			   struct inferior_list_entry *new_inferior);
void for_each_inferior (struct inferior_list *list,
			void (*action) (struct inferior_list_entry *));
extern struct thread_info *current_inferior;
void remove_inferior (struct inferior_list *list,
		      struct inferior_list_entry *entry);
void remove_thread (struct thread_info *thread);
void add_thread (int thread_id, void *target_data);
void clear_inferiors (void);
struct inferior_list_entry *find_inferior
     (struct inferior_list *,
      int (*func) (struct inferior_list_entry *,
		   void *),
      void *arg);
struct inferior_list_entry *find_inferior_id (struct inferior_list *list,
					      int id);
void *inferior_target_data (struct thread_info *);
void set_inferior_target_data (struct thread_info *, void *);
void *inferior_regcache_data (struct thread_info *);
void set_inferior_regcache_data (struct thread_info *, void *);
void change_inferior_id (struct inferior_list *list,
			 int new_id);

/* Public variables in server.c */

extern int cont_thread;
extern int general_thread;
extern int step_thread;
extern int thread_from_wait;
extern int old_thread_from_wait;
extern int server_waiting;

extern jmp_buf toplevel;

/* Functions from remote-utils.c */

int putpkt (char *buf);
int getpkt (char *buf);
void remote_open (char *name);
void remote_close (void);
void write_ok (char *buf);
void write_enn (char *buf);
void enable_async_io (void);
void disable_async_io (void);
void unblock_async_io (void);
void block_async_io (void);
void convert_ascii_to_int (char *from, char *to, int n);
void convert_int_to_ascii (char *from, char *to, int n);
void new_thread_notify (int id);
void dead_thread_notify (int id);
void prepare_resume_reply (char *buf, char status, unsigned char sig);

void decode_m_packet (char *from, CORE_ADDR * mem_addr_ptr,
		      unsigned int *len_ptr);
void decode_M_packet (char *from, CORE_ADDR * mem_addr_ptr,
		      unsigned int *len_ptr, char *to);

int unhexify (char *bin, const char *hex, int count);
int hexify (char *hex, const char *bin, int count);

int look_up_one_symbol (const char *name, CORE_ADDR *addrp);

/* Functions from ``signals.c''.  */
enum target_signal target_signal_from_host (int hostsig);
int target_signal_to_host_p (enum target_signal oursig);
int target_signal_to_host (enum target_signal oursig);

/* Functions from utils.c */

void perror_with_name (char *string);
void error (const char *string,...) ATTR_NORETURN;
void fatal (const char *string,...) ATTR_NORETURN;
void warning (const char *string,...);

/* Functions from the register cache definition.  */

void init_registers (void);

/* Maximum number of bytes to read/write at once.  The value here
   is chosen to fill up a packet (the headers account for the 32).  */
#define MAXBUFBYTES(N) (((N)-32)/2)

/* Buffer sizes for transferring memory, registers, etc.  Round up PBUFSIZ to
   hold all the registers, at least.  */
#define	PBUFSIZ ((registers_length () + 32 > 2000) \
		 ? (registers_length () + 32) \
		 : 2000)

#endif /* SERVER_H */
