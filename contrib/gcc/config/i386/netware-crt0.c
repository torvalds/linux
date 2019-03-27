/* Startup routines for NetWare.
   Contributed by Jan Beulich (jbeulich@novell.com)
   Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include <stddef.h>
#include <stdint.h>
#include "unwind-dw2-fde.h"

int __init_environment (void *);
int __deinit_environment (void *);


#define SECTION_DECL(name, decl) decl __attribute__((__section__(name)))

SECTION_DECL(".ctors",   void(*const __CTOR_LIST__)(void))
  = (void(*)(void))(intptr_t)-1;
SECTION_DECL(".ctors$_", void(*const __CTOR_END__)(void)) = NULL;

SECTION_DECL(".dtors",   void(*const __DTOR_LIST__)(void))
  = (void(*)(void))(intptr_t)-1;
SECTION_DECL(".dtors$_", void(*const __DTOR_END__)(void)) = NULL;

/* No need to use the __[de]register_frame_info_bases functions since
   for us the bases are NULL always anyway. */
void __register_frame_info (const void *, struct object *)
  __attribute__((__weak__));
void *__deregister_frame_info (const void *) __attribute__((__weak__));

SECTION_DECL(".eh_frame", /*const*/ uint32_t __EH_FRAME_BEGIN__[]) = { };
SECTION_DECL(".eh_frame$_", /*const*/ uint32_t __EH_FRAME_END__[]) = {0};

int
__init_environment (void *unused __attribute__((__unused__)))
{
  void (* const * pctor)(void);
  static struct object object;

  if (__register_frame_info)
    __register_frame_info (__EH_FRAME_BEGIN__, &object);

  for (pctor = &__CTOR_END__ - 1; pctor > &__CTOR_LIST__; --pctor)
    if (*pctor != NULL)
      (*pctor)();

  return 0;
}

int
__deinit_environment (void *unused __attribute__((__unused__)))
{
  /* This should be static to prevent calling the same destructor
     twice (just in case where we get here multiple times).  */
  static void (* const * pdtor)(void) = &__DTOR_LIST__ + 1;

  while (pdtor < &__DTOR_END__)
    if (*pdtor++ != NULL)
      pdtor[-1] ();

  if (__deregister_frame_info)
    __deregister_frame_info(__EH_FRAME_BEGIN__);

  return 0;
}
