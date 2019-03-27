/* Kernel Object Display facility for Cisco
   Copyright 1999 Free Software Foundation, Inc.
   
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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef KOD_H
#define KOD_H

typedef void kod_display_callback_ftype (char *);
typedef void kod_query_callback_ftype (char *, char *, int *);

/* ???/???: Functions imported from the library for all supported
   OSes.  FIXME: we really should do something better, such as
   dynamically loading the KOD modules.  */

/* FIXME: cagney/1999-09-20: The kod-cisco.c et.al. kernel modules
   should register themselve with kod.c during the _initialization*()
   phase.  With that implemented the extern declarations below would
   be replaced with the KOD register function that the various kernel
   modules should call.  An example of this mechanism can be seen in
   gdbarch.c:register_gdbarch_init(). */

#if 0
/* Don't have ecos code yet. */
extern char *ecos_kod_open (kod_display_callback_ftype *display_func,
			    kod_query_callback_ftype *query_func);
extern void ecos_kod_request (char *, int);
extern void ecos_kod_close (void);
#endif

/* Initialize and return library name and version.  The gdb side of
   KOD, kod.c, passes us two functions: one for displaying output
   (presumably to the user) and the other for querying the target.  */

extern char *cisco_kod_open (kod_display_callback_ftype *display_func,
			     kod_query_callback_ftype *query_func);

/* Print information about currently known kernel objects.  We
   currently ignore the argument.  There is only one mode of querying
   the Cisco kernel: we ask for a dump of everything, and it returns
   it.  */

extern void cisco_kod_request (char *arg, int from_tty);

extern void cisco_kod_close (void);

#endif
