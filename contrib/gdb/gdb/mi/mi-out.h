/* MI Command Set - MI output generating routines for GDB.
   Copyright 2000 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

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

#ifndef MI_OUT_H
#define MI_OUT_H 1

struct ui_out;
struct ui_file;

extern struct ui_out *mi_out_new (int mi_version);
extern void mi_out_put (struct ui_out *uiout, struct ui_file *stream);
extern void mi_out_rewind (struct ui_out *uiout);
extern void mi_out_buffered (struct ui_out *uiout, char *string);

/* Return the version number of the current MI.  */
extern int mi_version (struct ui_out *uiout);

#endif /* MI_OUT_H */
