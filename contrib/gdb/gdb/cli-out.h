/* Output generating routines for GDB CLI.
   Copyright 1999, 2000 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions.

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

#ifndef CLI_OUT_H
#define CLI_OUT_H

struct ui_file;

extern struct ui_out *cli_out_new (struct ui_file *stream);

extern struct ui_file *cli_out_set_stream (struct ui_out *uiout,
					   struct ui_file *stream);

#endif
