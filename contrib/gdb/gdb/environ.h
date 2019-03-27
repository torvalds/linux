/* Header for environment manipulation library.
   Copyright 1989, 1992, 2000 Free Software Foundation, Inc.

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

#if !defined (ENVIRON_H)
#define ENVIRON_H 1

/* We manipulate environments represented as these structures.  */

struct environ
  {
    /* Number of usable slots allocated in VECTOR.
       VECTOR always has one slot not counted here,
       to hold the terminating zero.  */
    int allocated;
    /* A vector of slots, ALLOCATED + 1 of them.
       The first few slots contain strings "VAR=VALUE"
       and the next one contains zero.
       Then come some unused slots.  */
    char **vector;
  };

extern struct environ *make_environ (void);

extern void free_environ (struct environ *);

extern void init_environ (struct environ *);

extern char *get_in_environ (const struct environ *, const char *);

extern void set_in_environ (struct environ *, const char *, const char *);

extern void unset_in_environ (struct environ *, char *);

extern char **environ_vector (struct environ *);

#endif /* defined (ENVIRON_H) */
