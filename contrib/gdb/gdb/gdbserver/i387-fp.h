/* i387-specific utility functions, for the remote server for GDB.
   Copyright 2000, 2001, 2002
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

#ifndef I387_FP_H
#define I387_FP_H

void i387_cache_to_fsave (void *buf);
void i387_fsave_to_cache (const void *buf);

void i387_cache_to_fxsave (void *buf);
void i387_fxsave_to_cache (const void *buf);

extern int num_xmm_registers;

#endif /* I387_FP_H */
